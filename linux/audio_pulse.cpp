/*
 * PulseAudio device enumeration and capture for PDW Linux.
 * Lists ALL sources and sinks (no state filtering). Capture from selected source by name.
 */
#ifdef __linux__
#include "platform/pdw_linux_types.h"
#include "Headers/sound_in.h"
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void Reset_ATB(void);

static pa_simple *g_pa_simple = NULL;
static volatile int g_pulse_running = 1;
static volatile double g_pulse_input_level = 0.0;

static const char *source_state_str(pa_source_state_t s)
{
	switch (s) {
	case PA_SOURCE_RUNNING:   return "RUNNING";
	case PA_SOURCE_IDLE:     return "IDLE";
	case PA_SOURCE_SUSPENDED: return "SUSPENDED";
	case PA_SOURCE_INVALID_STATE: return "INVALID";
	default:                 return "?";
	}
}

static const char *sink_state_str(pa_sink_state_t s)
{
	switch (s) {
	case PA_SINK_RUNNING:   return "RUNNING";
	case PA_SINK_IDLE:     return "IDLE";
	case PA_SINK_SUSPENDED: return "SUSPENDED";
	case PA_SINK_INVALID_STATE: return "INVALID";
	default:               return "?";
	}
}

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
	snprintf(desc, sizeof(desc), "%s [%s]", i->description ? i->description : i->name, sink_state_str(i->state));
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
	snprintf(desc, sizeof(desc), "%s [%s]", i->description ? i->description : i->name, source_state_str(i->state));
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

void pdw_linux_pulse_enumerate_capture(void (*cb)(const char *name, const char *desc, void *ctx), void *ctx)
{
	if (!cb) return;
	pa_mainloop *ml = pa_mainloop_new();
	if (!ml) return;
	pa_mainloop_api *api = pa_mainloop_get_api(ml);
	pa_context *c = pa_context_new(api, "pdw_linux_enum");
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

int pdw_linux_pulse_open(const char *device, unsigned int sample_rate)
{
	if (!device || !device[0]) return -1;
	pa_sample_spec ss;
	ss.format = PA_SAMPLE_U8;
	ss.rate = sample_rate;
	ss.channels = 1;
	int err;
	pa_simple *s = pa_simple_new(NULL, "pdw_linux", PA_STREAM_RECORD, device, "decode", &ss, NULL, NULL, &err);
	if (!s) {
		fprintf(stderr, "Pulse: open source '%s' failed: %s\n", device, pa_strerror(err));
		return -1;
	}
	g_pa_simple = s;
	g_pulse_running = 1;
	return 0;
}

void pdw_linux_pulse_stop(void)
{
	g_pulse_running = 0;
}

void pdw_linux_pulse_close(void)
{
	g_pulse_running = 0;
	if (g_pa_simple) {
		pa_simple_free(g_pa_simple);
		g_pa_simple = NULL;
	}
}

#define PULSE_BUF_SIZE 512  /* small so we check g_pulse_running often and can restart quickly */

int pdw_linux_pulse_run(void)
{
	if (!g_pa_simple) return -1;
	char *buf = (char *)malloc(PULSE_BUF_SIZE);
	if (!buf) return -1;
	Reset_ATB();
	while (g_pulse_running) {
		int err;
		if (pa_simple_read(g_pa_simple, buf, PULSE_BUF_SIZE, &err) < 0) {
			if (g_pulse_running) fprintf(stderr, "Pulse read: %s\n", pa_strerror(err));
			break;
		}
		unsigned int peak = 0;
		for (int i = 0; i < PULSE_BUF_SIZE; i++) {
			unsigned int v = (unsigned char)buf[i];
			unsigned int d = (v >= 128) ? (v - 128) : (128 - v);
			if (d > peak) peak = d;
		}
		g_pulse_input_level = (peak * 100.0) / 128.0;
		pdw_linux_feed_audio(buf, (long)PULSE_BUF_SIZE);
	}
	free(buf);
	return 0;
}

double pdw_linux_pulse_get_input_level(void)
{
	return g_pulse_input_level;
}

int pdw_linux_pulse_is_open(void)
{
	return g_pa_simple != NULL;
}

#endif
