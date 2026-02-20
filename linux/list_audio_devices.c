/*
 * List audio devices using the same API as the system Sound panel: PulseAudio.
 * Prints every device with its STATE so you can see what is active vs inactive.
 * Build: gcc -o list_audio_devices list_audio_devices.c $(pkg-config --cflags --libs libpulse)
 * Run: ./list_audio_devices
 */
#include <pulse/pulseaudio.h>
#include <stdio.h>
#include <string.h>

static pa_mainloop *mainloop;
static pa_context *ctx;

static const char *sink_state_str(pa_sink_state_t s)
{
	switch (s) {
	case PA_SINK_RUNNING:   return "RUNNING";
	case PA_SINK_IDLE:      return "IDLE";
	case PA_SINK_SUSPENDED: return "SUSPENDED";
	case PA_SINK_INVALID_STATE: return "INVALID";
	default:                return "?";
	}
}

static const char *source_state_str(pa_source_state_t s)
{
	switch (s) {
	case PA_SOURCE_RUNNING:   return "RUNNING";
	case PA_SOURCE_IDLE:      return "IDLE";
	case PA_SOURCE_SUSPENDED: return "SUSPENDED";
	case PA_SOURCE_INVALID_STATE: return "INVALID";
	default:                  return "?";
	}
}

static void sink_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata)
{
	(void)c;
	(void)userdata;
	if (eol < 0) { pa_mainloop_quit(mainloop, 1); return; }
	if (eol > 0) { pa_mainloop_quit(mainloop, 0); return; }
	printf("  PLAYBACK  [%s]  %s\n          %s\n",
		sink_state_str(i->state), i->name,
		i->description ? i->description : "(no description)");
}

static void source_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata)
{
	(void)userdata;
	if (eol < 0) { pa_mainloop_quit(mainloop, 1); return; }
	if (eol > 0) {
		printf("\nPlayback devices (sinks):\n");
		pa_operation *op = pa_context_get_sink_info_list(c, sink_cb, NULL);
		if (op) pa_operation_unref(op);
		return;
	}
	printf("  RECORD   [%s]  %s\n          %s\n",
		source_state_str(i->state), i->name,
		i->description ? i->description : "(no description)");
}

static void state_cb(pa_context *c, void *userdata)
{
	pa_context_state_t s = pa_context_get_state(c);
	(void)userdata;
	if (s == PA_CONTEXT_FAILED || s == PA_CONTEXT_TERMINATED) {
		pa_mainloop_quit(mainloop, 1);
		return;
	}
	if (s != PA_CONTEXT_READY) return;
	pa_operation *op = pa_context_get_source_info_list(c, source_cb, NULL);
	if (op) pa_operation_unref(op);
}

int main(void)
{
	printf("Audio devices (PulseAudio — same as System Settings → Sound)\n");
	printf("=============================================================\n");
	printf("State: RUNNING = in use, IDLE = active/no stream, SUSPENDED = inactive/closed\n");
	printf("-------------------------------------------------------------\n");
	printf("Recording devices (sources):\n");
	mainloop = pa_mainloop_new();
	if (!mainloop) { fprintf(stderr, "pa_mainloop_new failed\n"); return 1; }
	pa_mainloop_api *api = pa_mainloop_get_api(mainloop);
	ctx = pa_context_new(api, "list_audio_devices");
	if (!ctx) { fprintf(stderr, "pa_context_new failed\n"); return 1; }
	pa_context_set_state_callback(ctx, state_cb, NULL);
	if (pa_context_connect(ctx, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
		fprintf(stderr, "pa_context_connect: %s\n", pa_strerror(pa_context_errno(ctx)));
		return 1;
	}
	int r;
	pa_mainloop_run(mainloop, &r);
	pa_context_disconnect(ctx);
	pa_context_unref(ctx);
	pa_mainloop_free(mainloop);
	if (r != 0) return 1;
	printf("\nDone.\n");
	return 0;
}
