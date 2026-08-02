/*
 * PulseAudio device enumeration and capture for PDL Linux.
 * Lists ALL sources and sinks (no state filtering). Capture from selected source by name.
 */
#ifdef __linux__
#include "platform/pdl_linux_types.h"
#include "Headers/sound_in.h"
#include "Headers/pdl.h"
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void Reset_ATB(void);

static pa_simple *g_pa_simple = NULL;
static volatile int g_pulse_running = 1;
static volatile double g_pulse_input_level = 0.0;

struct enum_ctx {
	void (*cb)(const char *name, const char *desc, void *ctx);
	void *ctx;
	pa_mainloop *mainloop;
};

static void sink_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata)
{
	struct enum_ctx *e = (struct enum_ctx *)userdata;
	if (eol < 0) { pa_mainloop_quit(e->mainloop, 1); return; }
	if (eol > 0) { pa_mainloop_quit(e->mainloop, 0); return; }
	char desc[256];
	char monitor_name[256];
	/* Friendly label for sink monitors (used as loopback capture). */
	snprintf(desc, sizeof(desc), "Monitor of %s",
		i->description ? i->description : i->name);
	snprintf(monitor_name, sizeof(monitor_name), "%s.monitor", i->name);
	e->cb(monitor_name, desc, e->ctx);
}

static void source_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata)
{
	struct enum_ctx *e = (struct enum_ctx *)userdata;
	if (eol < 0) { pa_mainloop_quit(e->mainloop, 1); return; }
	if (eol > 0) {
		pa_operation *op = pa_context_get_sink_info_list(c, sink_cb, userdata);
		if (op) pa_operation_unref(op);
		return;
	}
	char desc[256];
	snprintf(desc, sizeof(desc), "%s", i->description ? i->description : i->name);
	e->cb(i->name, desc, e->ctx);
}

static void state_cb(pa_context *c, void *userdata)
{
	pa_context_state_t s = pa_context_get_state(c);
	struct enum_ctx *e = (struct enum_ctx *)userdata;
	if (s == PA_CONTEXT_FAILED || s == PA_CONTEXT_TERMINATED) {
		pa_mainloop_quit(e->mainloop, 1);
		return;
	}
	if (s != PA_CONTEXT_READY) return;
	pa_operation *op = pa_context_get_source_info_list(c, source_cb, userdata);
	if (op) pa_operation_unref(op);
}

void pdl_linux_pulse_enumerate_capture(void (*cb)(const char *name, const char *desc, void *ctx), void *ctx)
{
	if (!cb) return;
	pa_mainloop *ml = pa_mainloop_new();
	if (!ml) return;
	pa_mainloop_api *api = pa_mainloop_get_api(ml);
	pa_context *c = pa_context_new(api, "pdl_linux_enum");
	if (!c) { pa_mainloop_free(ml); return; }
	struct enum_ctx e_ctx;
	e_ctx.cb = cb;
	e_ctx.ctx = ctx;
	e_ctx.mainloop = ml;
	pa_context_set_state_callback(c, state_cb, &e_ctx);
	if (pa_context_connect(c, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
		pa_context_unref(c);
		pa_mainloop_free(ml);
		return;
	}
	int r;
	pa_mainloop_run(ml, &r);
	pa_context_set_state_callback(c, NULL, NULL);
	pa_context_disconnect(c);
	pa_context_unref(c);
	pa_mainloop_free(ml);
}

int pdl_linux_pulse_open(const char *device, unsigned int sample_rate)
{
	if (!device || !device[0]) return -1;
	pa_sample_spec ss;
	/* PagerCast / browser audio is 16-bit PCM — U8 squashes baseband FSK. */
	ss.format = PA_SAMPLE_S16LE;
	ss.rate = sample_rate;
	ss.channels = 1;
	int err;
	pa_simple *s = pa_simple_new(NULL, "pdl", PA_STREAM_RECORD, device, "decode", &ss, NULL, NULL, &err);
	if (!s) {
		fprintf(stderr, "Pulse: open source '%s' failed: %s\n", device, pa_strerror(err));
		return -1;
	}
	g_pa_simple = s;
	g_pulse_running = 1;
	Profile.audioSampleRate = (int)sample_rate;
	fprintf(stderr, "Pulse: capture %u Hz S16LE mono from '%s'\n", sample_rate, device);
	return 0;
}

void pdl_linux_pulse_stop(void)
{
	g_pulse_running = 0;
}

void pdl_linux_pulse_close(void)
{
	g_pulse_running = 0;
	if (g_pa_simple) {
		pa_simple_free(g_pa_simple);
		g_pa_simple = NULL;
	}
}

#define PULSE_SAMPLES 512

int pdl_linux_pulse_run(void)
{
	if (!g_pa_simple) return -1;
	int16_t *s16 = (int16_t *)malloc(PULSE_SAMPLES * sizeof(int16_t));
	char *u8 = (char *)malloc(PULSE_SAMPLES);
	if (!s16 || !u8) {
		free(s16);
		free(u8);
		return -1;
	}
	Reset_ATB();
	while (g_pulse_running) {
		int err;
		if (pa_simple_read(g_pa_simple, s16, PULSE_SAMPLES * sizeof(int16_t), &err) < 0) {
			if (g_pulse_running) fprintf(stderr, "Pulse read: %s\n", pa_strerror(err));
			break;
		}
		unsigned int peak = 0;
		for (int i = 0; i < PULSE_SAMPLES; i++) {
			int v = (int)s16[i];
			if (v < 0) v = -v;
			unsigned int pct = (unsigned int)((v * 100ULL) / 32768ULL);
			if (pct > peak) peak = pct;
			/* Audio_To_Bits expects char samples; XOR 0x80 recenters around zero. */
			u8[i] = (char)(s16[i] >> 8);
		}
		g_pulse_input_level = (double)peak;
		pdl_linux_feed_audio(u8, (long)PULSE_SAMPLES);
	}
	free(s16);
	free(u8);
	return 0;
}

double pdl_linux_pulse_get_input_level(void)
{
	return g_pulse_input_level;
}

int pdl_linux_pulse_is_open(void)
{
	return g_pa_simple != NULL;
}

#endif
