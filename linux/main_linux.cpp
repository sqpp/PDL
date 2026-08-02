/*
 * PDL main: GUI (GTK3 classic or WebKit modern) + ALSA capture in a thread.
 */
#ifdef __linux__
#include "platform/pdl_platform.h"
#include "Headers/pdl.h"
#include "Headers/initapp.h"
#include "Headers/decode.h"
#include "Headers/sound_in.h"
#include "linux/gui_gtk.h"
#include "linux/gui_web.h"
#include "linux/hw_decode.h"
#include "linux/gpu_spectrum.h"
#include "pdl_version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <curl/curl.h>
#include <unistd.h>

extern void pdl_linux_init_panes(void);
extern int pdl_linux_alsa_open(const char *device, unsigned int sample_rate);
extern void pdl_linux_alsa_stop(void);
extern void pdl_linux_alsa_close(void);
extern int pdl_linux_alsa_run(void);

static pthread_t s_capture_thread;
static int s_capture_active;
static int s_capture_paused;

/* Saved for UI-mode restart via execv. */
static int s_saved_argc;
static char **s_saved_argv;

static void *capture_thread_fn(void *arg)
{
	(void)arg;
	pdl_linux_alsa_run();
	return NULL;
}

static int start_capture_thread(void)
{
	unsigned int rate = (Profile.audioSampleRate > 0) ? (unsigned)Profile.audioSampleRate : 48000;
	const char *dev = pdl_linux_gui_get_capture_device();
	if (!dev || !dev[0]) dev = "default";
	if (pdl_linux_alsa_open(dev, rate) != 0) {
		fprintf(stderr, "Failed to open capture device '%s'\n", dev ? dev : "default");
		s_capture_active = 0;
		return -1;
	}
	if (pthread_create(&s_capture_thread, NULL, capture_thread_fn, NULL) != 0) {
		fprintf(stderr, "Failed to start capture thread\n");
		pdl_linux_alsa_close();
		s_capture_active = 0;
		return -1;
	}
	s_capture_active = 1;
	s_capture_paused = 0;
	return 0;
}

void pdl_linux_pause_local_capture(void)
{
	if (!s_capture_active || s_capture_paused) return;
	pdl_linux_alsa_stop();
	pthread_join(s_capture_thread, NULL);
	pdl_linux_alsa_close();
	s_capture_active = 0;
	s_capture_paused = 1;
}

void pdl_linux_resume_local_capture(void)
{
	if (!s_capture_paused) return;
	s_capture_paused = 0;
	start_capture_thread();
}

void pdl_linux_restart_capture(void)
{
	if (s_capture_paused) {
		/* Device change while PagerCast owns audio — apply on resume. */
		return;
	}
	if (s_capture_active) {
		pdl_linux_alsa_stop();
		pthread_join(s_capture_thread, NULL);
		pdl_linux_alsa_close();
		s_capture_active = 0;
	}
	start_capture_thread();
}

static int parse_verbose_level(const char *arg)
{
	if (!arg || !*arg) return 1;
	if (strcmp(arg, "full") == 0 || strcmp(arg, "2") == 0) return 2;
	if (strcmp(arg, "1") == 0 || strcmp(arg, "messages") == 0) return 1;
	if (strcmp(arg, "0") == 0 || strcmp(arg, "off") == 0) return 0;
	return 1; /* default */
}

static int parse_ui_token(const char *s)
{
	if (!s || !*s) return -1;
	if (strcasecmp(s, "gtk") == 0 || strcasecmp(s, "classic") == 0 || strcmp(s, "0") == 0)
		return PDL_UI_GTK;
	if (strcasecmp(s, "web") == 0 || strcasecmp(s, "modern") == 0 || strcmp(s, "1") == 0)
		return PDL_UI_WEB;
	return -1;
}

/* Build argv for restart with --ui=gtk|web, dropping any previous --ui. */
static char **build_restart_argv(int ui_mode, int *out_argc)
{
	int n = 1; /* program */
	for (int i = 1; i < s_saved_argc; i++) {
		if (strcmp(s_saved_argv[i], "--ui") == 0) {
			if (i + 1 < s_saved_argc) i++;
			continue;
		}
		if (strncmp(s_saved_argv[i], "--ui=", 5) == 0)
			continue;
		n++;
	}
	n += 2; /* --ui value */
	char **av = (char **)calloc((size_t)n + 1, sizeof(char *));
	if (!av) return NULL;
	int o = 0;
	av[o++] = s_saved_argv[0];
	for (int i = 1; i < s_saved_argc; i++) {
		if (strcmp(s_saved_argv[i], "--ui") == 0) {
			if (i + 1 < s_saved_argc) i++;
			continue;
		}
		if (strncmp(s_saved_argv[i], "--ui=", 5) == 0)
			continue;
		av[o++] = s_saved_argv[i];
	}
	av[o++] = (char *)"--ui";
	av[o++] = (ui_mode == PDL_UI_WEB) ? (char *)"web" : (char *)"gtk";
	av[o] = NULL;
	*out_argc = o;
	return av;
}

void pdl_linux_apply_ui_mode(int ui_mode, int restart_now)
{
	if (ui_mode != PDL_UI_WEB)
		ui_mode = PDL_UI_GTK;
	Profile.ui_mode = ui_mode;
	WriteSettings();
	if (!restart_now || !s_saved_argv)
		return;

	extern void pdl_pagercast_shutdown(void);
	pdl_pagercast_shutdown();
	if (s_capture_active) {
		pdl_linux_alsa_stop();
		pthread_join(s_capture_thread, NULL);
		pdl_linux_alsa_close();
		s_capture_active = 0;
	}

	int nac = 0;
	char **nav = build_restart_argv(ui_mode, &nac);
	if (!nav) {
		fprintf(stderr, "UI restart: out of memory\n");
		return;
	}
	execv(nav[0], nav);
	perror("execv");
	free(nav);
}

int main(int argc, char **argv)
{
	curl_global_init(CURL_GLOBAL_DEFAULT);
	s_saved_argc = argc;
	s_saved_argv = argv;

	const char *log_path = NULL;
	int cli_ui = -1;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
			log_path = argv[++i];
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			printf("PDL " PDL_VERSION " — Linux pager decoder (POCSAG)\n");
			printf("Usage: %s [options]\n", argv[0]);
			printf("  -o <file>           Append decoded lines to <file>\n");
			printf("  -v, --verbose [level]  Verbose: list messages and/or debug display\n");
			printf("                       level: 1 (or 'messages'), 2 (or 'full'), 0 (off)\n");
			printf("                       e.g. --verbose full\n");
			printf("  --ui=gtk|web         UI: classic GTK (default) or modern Web UI\n");
			printf("  --ui gtk|web         Same as --ui=\n");
			return 0;
		} else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
			const char *level = (i + 1 < argc) ? argv[i + 1] : "";
			int v = parse_verbose_level(level);
			if (v > 0 && level[0] && level[0] != '-')
				i++;
			pdl_platform_set_verbose(v);
		} else if (strncmp(argv[i], "--ui=", 5) == 0) {
			cli_ui = parse_ui_token(argv[i] + 5);
			if (cli_ui < 0) {
				fprintf(stderr, "Unknown --ui value '%s' (use gtk or web)\n", argv[i] + 5);
				return 1;
			}
		} else if (strcmp(argv[i], "--ui") == 0 && i + 1 < argc) {
			cli_ui = parse_ui_token(argv[++i]);
			if (cli_ui < 0) {
				fprintf(stderr, "Unknown --ui value (use gtk or web)\n");
				return 1;
			}
		}
	}

	if (!InitApplication(NULL)) {
		fprintf(stderr, "Init failed\n");
		return 1;
	}
	pdl_linux_init_panes();
	pd_reset_all();

	if (log_path)
		pdl_platform_set_log_file(log_path);

	int ui = (cli_ui >= 0) ? cli_ui : (Profile.ui_mode == PDL_UI_WEB ? PDL_UI_WEB : PDL_UI_GTK);

	if (ui == PDL_UI_WEB) {
		if (pdl_linux_web_gui_init(&argc, &argv) != 0) {
			fprintf(stderr, "Failed to init Web UI (WebKitGTK). Falling back to classic GTK.\n");
			ui = PDL_UI_GTK;
		}
	}
	if (ui == PDL_UI_GTK) {
		if (pdl_linux_gui_init(&argc, &argv) != 0) {
			fprintf(stderr, "Failed to init GTK. Is libgtk-3 installed?\n");
			return 1;
		}
	}

	if (start_capture_thread() != 0) {
		fprintf(stderr, "Failed to open audio capture.\n");
		return 1;
	}

	/* Hardware RS232 decode (after GUI so the decode timer can attach). */
	if (Profile.comPortRS232 > 0)
		pdl_linux_hw_decode_apply_settings();

	if (ui == PDL_UI_WEB)
		pdl_linux_web_gui_run();
	else
		pdl_linux_gui_run();

	extern void pdl_pagercast_shutdown(void);
	pdl_pagercast_shutdown();
	pdl_linux_hw_decode_stop();
	pdl_gpu_spectrum_shutdown();

	if (s_capture_active) {
		pdl_linux_alsa_stop();
		pthread_join(s_capture_thread, NULL);
		pdl_linux_alsa_close();
	}
	curl_global_cleanup();
	return 0;
}

#endif
