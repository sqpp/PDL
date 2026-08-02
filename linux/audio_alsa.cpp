/*
 * ALSA capture for PDL Linux. Captures 8-bit mono at Profile.audioSampleRate
 * and feeds buffers to pdl_linux_feed_audio (→ Audio_To_Bits).
 */
#ifdef __linux__
#include "platform/pdl_linux_types.h"
#include "Headers/sound_in.h"
#include "Headers/pdl.h"
#include "Headers/decode.h"
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>

static snd_pcm_t *g_handle = NULL;
static volatile int g_running = 1;
static volatile double g_input_level = 0.0;  /* 0..100 for UI */

static void sig_handler(int sig)
{
	(void)sig;
	g_running = 0;
}

/* Skip null/virtual devices (used only for playback enumeration). */
static int is_real_device(const char *name, const char *desc)
{
	const char *s[] = { name, desc, NULL };
	for (int i = 0; s[i]; i++) {
		if (!s[i]) continue;
		if (strstr(s[i], "Discard") || strstr(s[i], "discard")) return 0;
		if (strstr(s[i], "null")) return 0;
		if (strstr(s[i], "generate zero samples")) return 0;
	}
	return 1;
}

void pdl_linux_alsa_enumerate_capture(void (*cb)(const char *name, const char *desc, void *ctx), void *ctx)
{
	if (!cb) return;
	/* List ALL Pulse sources then ALL sinks (no state filter). Same as system Sound panel. */
	pdl_linux_pulse_enumerate_capture(cb, ctx);
}

void pdl_linux_alsa_enumerate_playback(void (*cb)(const char *name, const char *desc, void *ctx), void *ctx)
{
	void **hints = NULL;
	if (snd_device_name_hint(-1, "pcm", &hints) < 0) return;
	for (void **p = hints; *p; p++) {
		char *ioid = snd_device_name_get_hint(*p, "IOID");
		if (ioid && strcmp(ioid, "Input") == 0) { free(ioid); continue; }
		free(ioid);
		char *name = snd_device_name_get_hint(*p, "NAME");
		char *desc = snd_device_name_get_hint(*p, "DESC");
		if (!name) continue;
		if (!is_real_device(name, desc ? desc : name)) { free(name); free(desc); continue; }
		if (cb) cb(name, desc ? desc : name, ctx);
		free(name);
		free(desc);
	}
	snd_device_name_free_hint(hints);
}

int pdl_linux_alsa_open(const char *device, unsigned int sample_rate)
{
	const char *dev = (device && device[0]) ? device : "default";

	if (strcmp(dev, "default") == 0) {
		int err;
		snd_pcm_hw_params_t *hw_params;
		if ((err = snd_pcm_open(&g_handle, "default", SND_PCM_STREAM_CAPTURE, 0)) < 0) {
			fprintf(stderr, "ALSA: open default failed: %s\n", snd_strerror(err));
			return -1;
		}
		snd_pcm_hw_params_alloca(&hw_params);
		snd_pcm_hw_params_any(g_handle, hw_params);
		snd_pcm_hw_params_set_access(g_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
		snd_pcm_hw_params_set_format(g_handle, hw_params, SND_PCM_FORMAT_U8);
		snd_pcm_hw_params_set_channels(g_handle, hw_params, 1);
		unsigned int requested = sample_rate;
		unsigned int sr = requested;
		snd_pcm_hw_params_set_rate_near(g_handle, hw_params, &sr, 0);
		/* Small period = frequent POLLIN so the level meter updates often (audio‑reactive). */
		snd_pcm_uframes_t period_frames = 256;
		snd_pcm_hw_params_set_period_size_near(g_handle, hw_params, &period_frames, 0);
		snd_pcm_uframes_t buf_frames = period_frames * 4;
		snd_pcm_hw_params_set_buffer_size_near(g_handle, hw_params, &buf_frames);
		if ((err = snd_pcm_hw_params(g_handle, hw_params)) < 0) {
			fprintf(stderr, "ALSA: set params failed: %s\n", snd_strerror(err));
			snd_pcm_close(g_handle);
			g_handle = NULL;
			return -1;
		}
		/* Read back negotiated rate — decoder timing depends on Profile.audioSampleRate. */
		if (snd_pcm_hw_params_get_rate(hw_params, &sr, 0) == 0) {
			if (sr != requested)
				fprintf(stderr, "ALSA: requested %u Hz, negotiated %u Hz\n", requested, sr);
			Profile.audioSampleRate = (int)sr;
		}
		g_running = 1;
		return 0;
	}
	return pdl_linux_pulse_open(dev, sample_rate);
}

void pdl_linux_alsa_stop(void)
{
	g_running = 0;
	pdl_linux_pulse_stop();
}

void pdl_linux_alsa_close(void)
{
	g_running = 0;
	if (pdl_linux_pulse_is_open())
		pdl_linux_pulse_close();
	if (g_handle) {
		snd_pcm_drop(g_handle);
		snd_pcm_close(g_handle);
		g_handle = NULL;
	}
}

#define ALSA_BUF_SIZE 512   /* small so level meter updates often (~10–60 ms) and feels audio‑reactive */

int pdl_linux_alsa_run(void)
{
	if (pdl_linux_pulse_is_open())
		return pdl_linux_pulse_run();
	if (!g_handle) return -1;
	char *buf = (char*)malloc(ALSA_BUF_SIZE);
	if (!buf) return -1;
	Reset_ATB();
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	fprintf(stderr, "PDL: capturing from ALSA device (Ctrl+C to stop)\n");
	unsigned int nfds = snd_pcm_poll_descriptors_count(g_handle);
	struct pollfd *pfds = (struct pollfd *)malloc(sizeof(struct pollfd) * nfds);
	if (!pfds) { free(buf); return -1; }
	snd_pcm_poll_descriptors(g_handle, pfds, nfds);
	while (g_running) {
		int poll_ret = poll(pfds, nfds, 15);  /* ~60 Hz max wait so meter stays reactive */
		if (poll_ret < 0) break;
		if (!g_running) break;
		unsigned short revents = 0;
		snd_pcm_poll_descriptors_revents(g_handle, pfds, nfds, &revents);
		if (!(revents & POLLIN)) continue;
		snd_pcm_sframes_t n = snd_pcm_readi(g_handle, buf, ALSA_BUF_SIZE);
		if (n > 0) {
			unsigned int peak = 0;
			for (int i = 0; i < (int)n; i++) {
				unsigned int v = (unsigned char)buf[i];
				unsigned int d = (v >= 128) ? (v - 128) : (128 - v);
				if (d > peak) peak = d;
			}
			g_input_level = (peak * 100.0) / 128.0;
			pdl_linux_feed_audio(buf, (long)n);
		} else {
			g_input_level = 0.0;
		}
		if (n == -EPIPE) {
			snd_pcm_prepare(g_handle);
		} else if (n < 0) {
			fprintf(stderr, "ALSA read: %s\n", snd_strerror((int)n));
			break;
		}
	}
	free(pfds);
	free(buf);
	return 0;
}

double pdl_linux_get_input_level(void)
{
	extern int pdl_pagercast_is_connected(void);
	extern double pdl_pagercast_get_input_level(void);
	if (pdl_pagercast_is_connected())
		return pdl_pagercast_get_input_level();
	if (pdl_linux_pulse_is_open())
		return pdl_linux_pulse_get_input_level();
	return g_input_level;
}

#endif
