/*
 * Hardware RS232 bitstream decode glue for Linux.
 * Wires rs232_connect buffers into pdl_decode() on a GTK-friendly timer.
 */
#ifdef __linux__
#include "linux/hw_decode.h"
#include "Headers/pdl.h"
#include "Headers/decode.h"
#include "Headers/SLICER.H"
#include "utils/rs232.h"
#include "linux/gui_gtk.h"
#include "linux/pagercast_stream.h"
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

extern unsigned long bufsize;
extern unsigned long *cpstn;
extern unsigned short int *freqdata;
extern unsigned char *linedata;
extern unsigned int pd_i;

static guint s_decode_timer = 0;
static int s_active = 0;

static gboolean on_hw_decode_tick(gpointer data)
{
	(void)data;
	if (!s_active || !cpstn || !freqdata || !linedata) return G_SOURCE_CONTINUE;
	pdl_decode();
	return G_SOURCE_CONTINUE;
}

int pdl_linux_hw_decode_active(void)
{
	return s_active;
}

void pdl_linux_hw_decode_stop(void)
{
	s_active = 0;
	rs232_disconnect();
	if (s_decode_timer) {
		/* g_source_remove must run on the GTK main thread. */
		if (g_main_context_is_owner(g_main_context_default())) {
			g_source_remove(s_decode_timer);
			s_decode_timer = 0;
		} else {
			guint id = s_decode_timer;
			s_decode_timer = 0;
			g_idle_add(+[](gpointer p) -> gboolean {
				guint tid = GPOINTER_TO_UINT(p);
				if (tid) g_source_remove(tid);
				return G_SOURCE_REMOVE;
			}, GUINT_TO_POINTER(id));
		}
		fprintf(stderr, "RS232 decode: stopped\n");
	}
}

int pdl_linux_hw_decode_start(void)
{
	if (s_active)
		pdl_linux_hw_decode_stop();

	if (Profile.comPort < 1) {
		fprintf(stderr, "RS232 decode: select a COM port first\n");
		return -1;
	}
	if (Profile.comPortRS232 <= 0)
		Profile.comPortRS232 = 2;

	if (pdl_pagercast_is_wanted())
		pdl_pagercast_disconnect();
	pdl_linux_pause_local_capture();

	SLICER_IN_STR in;
	SLICER_OUT_STR out;
	memset(&in, 0, sizeof(in));
	memset(&out, 0, sizeof(out));
	in.com_port = (unsigned int)Profile.comPort;

	int rc = rs232_connect(&in, &out);
	if (rc != RS232_SUCCESS)
		return -1;

	freqdata = out.freqdata;
	linedata = out.linedata;
	cpstn = out.cpstn;
	bufsize = (unsigned long)out.bufsize;
	pd_i = 0;
	pd_reset_all();

	s_active = 1;
	s_decode_timer = g_timeout_add(20, on_hw_decode_tick, NULL);
	return 0;
}

void pdl_linux_hw_decode_apply_settings(void)
{
	if (Profile.comPortRS232 > 0) {
		if (pdl_linux_hw_decode_start() != 0) {
			Profile.comPortRS232 = 0;
			pdl_linux_resume_local_capture();
		}
	} else {
		pdl_linux_hw_decode_stop();
		if (!pdl_pagercast_is_wanted())
			pdl_linux_resume_local_capture();
	}
}

#endif
