/*
 * PDW Linux main: GUI (GTK3) + ALSA capture in a thread.
 */
#ifdef __linux__
#include "platform/pdw_platform.h"
#include "Headers/pdw.h"
#include "Headers/initapp.h"
#include "Headers/decode.h"
#include "Headers/sound_in.h"
#include "linux/gui_gtk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

extern void pdw_linux_init_panes(void);
extern int pdw_linux_alsa_open(const char *device, unsigned int sample_rate);
extern void pdw_linux_alsa_stop(void);
extern void pdw_linux_alsa_close(void);
extern int pdw_linux_alsa_run(void);

static pthread_t s_capture_thread;

static void *capture_thread_fn(void *arg)
{
	(void)arg;
	pdw_linux_alsa_run();
	return NULL;
}

void pdw_linux_restart_capture(void)
{
	pdw_linux_alsa_stop();
	pthread_join(s_capture_thread, NULL);
	pdw_linux_alsa_close();
	unsigned int rate = (Profile.audioSampleRate > 0) ? (unsigned)Profile.audioSampleRate : 44100;
	const char *dev = pdw_linux_gui_get_capture_device();
	if (!dev || !dev[0]) dev = "default";
	if (pdw_linux_alsa_open(dev, rate) != 0) {
		fprintf(stderr, "Failed to reopen capture device '%s'\n", dev ? dev : "default");
		return;
	}
	if (pthread_create(&s_capture_thread, NULL, capture_thread_fn, NULL) != 0) {
		fprintf(stderr, "Failed to restart capture thread\n");
		pdw_linux_alsa_close();
	}
}

static int parse_verbose_level(const char *arg)
{
	if (!arg || !*arg) return 1;
	if (strcmp(arg, "full") == 0 || strcmp(arg, "2") == 0) return 2;
	if (strcmp(arg, "1") == 0 || strcmp(arg, "messages") == 0) return 1;
	if (strcmp(arg, "0") == 0 || strcmp(arg, "off") == 0) return 0;
	return 1; /* default */
}

int main(int argc, char **argv)
{
	const char *log_path = NULL;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
			log_path = argv[++i];
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			printf("PDW Linux - POCSAG/FLEX/ACARS/MOBITEX/ERMES decoder\n");
			printf("Usage: %s [options]\n", argv[0]);
			printf("  -o <file>           Append decoded lines to <file>\n");
			printf("  -v, --verbose [level]  Verbose: list messages and/or debug display\n");
			printf("                       level: 1 (or 'messages'), 2 (or 'full'), 0 (off)\n");
			printf("                       e.g. --verbose full\n");
			return 0;
		} else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
			const char *level = (i + 1 < argc) ? argv[i + 1] : "";
			int v = parse_verbose_level(level);
			if (v > 0 && level[0] && level[0] != '-')
				i++;
			pdw_platform_set_verbose(v);
		}
	}

	if (!InitApplication(NULL)) {
		fprintf(stderr, "Init failed\n");
		return 1;
	}
	pdw_linux_init_panes();
	pd_reset_all();

	if (log_path)
		pdw_platform_set_log_file(log_path);

	if (pdw_linux_gui_init(&argc, &argv) != 0) {
		fprintf(stderr, "Failed to init GTK. Is libgtk-3 installed?\n");
		return 1;
	}

	unsigned int rate = (Profile.audioSampleRate > 0) ? (unsigned)Profile.audioSampleRate : 44100;
	const char *capture_dev = pdw_linux_gui_get_capture_device();
	if (!capture_dev || !capture_dev[0]) capture_dev = "default";
	if (pdw_linux_alsa_open(capture_dev, rate) != 0) {
		fprintf(stderr, "Failed to open ALSA input device '%s' at rate %u.\n", capture_dev, rate);
		return 1;
	}

	if (pthread_create(&s_capture_thread, NULL, capture_thread_fn, NULL) != 0) {
		fprintf(stderr, "Failed to start capture thread\n");
		pdw_linux_alsa_close();
		return 1;
	}

	pdw_linux_gui_run();

	pthread_join(s_capture_thread, NULL);
	pdw_linux_alsa_close();
	return 0;
}

#endif
