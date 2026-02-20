/*
 * GTK3 GUI for PDW Linux — layout and look to match Windows:
 * Toolbar, column headers (Address/Time/Date/Mode/Type/Bitrate/Message),
 * dark background, resizable panes.
 */
#ifdef __linux__

#include "Headers/pdw.h"
#include "Headers/initapp.h"
#include "Headers/sound_in.h"
#include "Headers/mobitex.h"
#include "platform/pdw_platform.h"
#include "linux/gui_gtk.h"
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <cairo/cairo.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static GtkWidget *s_win = NULL;
static GtkWidget *s_pane1_text = NULL;
static GtkWidget *s_pane2_text = NULL;
static GtkWidget *s_signal_meter = NULL;       /* custom-drawn VU meter (GtkDrawingArea) */
static GtkWidget *s_input_device_label = NULL;
/* Last pane line count so we only redraw/scroll when content changed (stops scrollbar jumping). */
static unsigned int s_last_pane1_bottom = 0, s_last_pane2_bottom = 0;
/* Current level for meter: timer only writes this and queue_draw (no GTK in timer = responsive). */
static double s_meter_vu = -20.0;               /* -20 to +3 VU */
static double s_meter_display_vu = -20.0;      /* smoothed for needle movement */
static volatile int s_quit = 0;
extern double dRX_Quality;

/* Menu command IDs — match Rsrc.rc / Resource.h (IDM_*) so behaviour matches Windows. */
enum {
	IDM_LOGFILE = 201, IDM_EXIT = 202,
	IDM_COPY_SELECTION = 210, IDM_COPY_UPPER = 211, IDM_COPY_LOWER = 212, IDM_COPY_SAVE = 213, IDM_COPY_PRINT = 214,
	IDM_INTERFACE = 220, IDM_VOLUME = 221,
	IDM_OPTIONS = 230, IDM_GENERAL = 231, IDM_MAIL = 232,
	IDM_FILTERS = 240, IDM_FILTEROPTIONS = 241, IDM_RELOAD = 242, IDM_RESET_HITCOUNTERS = 243, IDM_FILTERFILE_EN = 244, IDM_FILTERCOMMANDFILE = 245,
	IDM_CLEARDISPLAY = 250, IDM_COLOR = 251, IDM_FONT = 252, IDM_SCREENOPTIONS = 253, IDM_SCROLLBACK = 254, IDM_SYSTEMTRAY = 255,
	IDM_POCSAGFLEX = 260, IDM_ACARS = 261, IDM_MOBITEX = 262, IDM_ERMES = 263, IDM_MONSTAT = 264,
	IDM_HELP = 280, IDM_ABOUT = 281, IDM_DEBUG = 290,
	IDM_ENGLISH = 300
};
/* Pulse device names can be long (e.g. alsa_output....analog-stereo.monitor); 63 truncated and broke selector */
#define MAX_DEVICE_LEN  256
#define MAX_DEVICE_DESC_LEN  128
static char s_capture_device[MAX_DEVICE_LEN + 1] = "default";
static char s_capture_device_desc[MAX_DEVICE_DESC_LEN + 1] = "Default";
static char s_playback_device[MAX_DEVICE_LEN + 1] = "default";

#define TOOLBAR_HEIGHT  36
#define HEADER_HEIGHT   20
#define MAX_AUDIO_DEVICES_IN_LIST  64

typedef struct { GtkComboBoxText *combo; int count; } audio_combo_ctx_t;

/* Try to load a toolbar icon from GFX/ (match Windows Rsrc.rc). Returns new GtkButton with image or NULL. */
static GtkWidget *toolbar_button_from_gfx(const char *gfx_name, const char *tooltip, const char *fallback_icon)
{
	const char *paths[] = { "GFX/", "../GFX/", "../../GFX/", NULL };
	GdkPixbuf *pix = NULL;
	char path[256];
	for (int i = 0; paths[i]; i++) {
		snprintf(path, sizeof(path), "%s%s", paths[i], gfx_name);
		if (g_file_test(path, G_FILE_TEST_EXISTS)) {
			pix = gdk_pixbuf_new_from_file(path, NULL);
			if (pix) break;
		}
	}
	GtkWidget *btn = gtk_button_new();
	if (pix) {
		GtkWidget *img = gtk_image_new_from_pixbuf(pix);
		g_object_unref(pix);
		gtk_button_set_image(GTK_BUTTON(btn), img);
		gtk_button_set_always_show_image(GTK_BUTTON(btn), TRUE);
	} else {
		GtkWidget *img = gtk_image_new_from_icon_name(fallback_icon, GTK_ICON_SIZE_BUTTON);
		gtk_button_set_image(GTK_BUTTON(btn), img);
		gtk_button_set_always_show_image(GTK_BUTTON(btn), TRUE);
	}
	if (tooltip) gtk_widget_set_tooltip_text(btn, tooltip);
	return btn;
}

static void add_capture_device_cb(const char *name, const char *desc, void *ctx)
{
	audio_combo_ctx_t *c = (audio_combo_ctx_t *)ctx;
	if (c->count >= MAX_AUDIO_DEVICES_IN_LIST) return;
	c->count++;
	gtk_combo_box_text_append(c->combo, name, desc);
}
static void add_playback_device_cb(const char *name, const char *desc, void *ctx)
{
	audio_combo_ctx_t *c = (audio_combo_ctx_t *)ctx;
	if (c->count >= MAX_AUDIO_DEVICES_IN_LIST) return;
	c->count++;
	gtk_combo_box_text_append(c->combo, name, desc);
}

static void on_audio_response(GtkDialog *dialog, int response_id, gpointer user_data)
{
	(void)user_data;
	if (response_id == GTK_RESPONSE_ACCEPT) {
		GtkWidget *cap = (GtkWidget *)g_object_get_data(G_OBJECT(dialog), "pdw-cap-combo");
		if (cap) {
			const gchar *cid = gtk_combo_box_get_active_id(GTK_COMBO_BOX(cap));
			const gchar *ctext = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(cap));
			if (cid) {
				strncpy(s_capture_device, cid, MAX_DEVICE_LEN);
				s_capture_device[MAX_DEVICE_LEN] = '\0';
				if (ctext) {
					strncpy(s_capture_device_desc, ctext, MAX_DEVICE_DESC_LEN);
					s_capture_device_desc[MAX_DEVICE_DESC_LEN] = '\0';
					g_free((void *)ctext);
				} else
					s_capture_device_desc[0] = '\0';
				pdw_linux_restart_capture();
			}
		}
	}
	gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void log_ui(const char *action, const char *detail);

static void on_audio_clicked(GtkWidget *btn, gpointer data)
{
	log_ui("Audio / Setup", "dialog opened");
	(void)btn;
	(void)data;
	GtkWidget *dialog = gtk_dialog_new_with_buttons("Audio input (for decoding)", NULL,
		(GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
		"Cancel", GTK_RESPONSE_REJECT, "OK", GTK_RESPONSE_ACCEPT, NULL);
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	GtkWidget *grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	gtk_container_add(GTK_CONTAINER(content), grid);

	gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Input device:"), 0, 0, 1, 1);
	GtkWidget *cap_combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(cap_combo), "default", "Default");
	audio_combo_ctx_t cap_ctx = { GTK_COMBO_BOX_TEXT(cap_combo), 0 };
	pdw_linux_alsa_enumerate_capture(add_capture_device_cb, &cap_ctx);
	if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(cap_combo), s_capture_device))
		gtk_combo_box_set_active(GTK_COMBO_BOX(cap_combo), 0);
	gtk_grid_attach(GTK_GRID(grid), cap_combo, 1, 0, 1, 1);
	g_object_set_data(G_OBJECT(dialog), "pdw-cap-combo", cap_combo);

	g_signal_connect(dialog, "response", G_CALLBACK(on_audio_response), NULL);
	gtk_widget_show_all(dialog);
}

const char *pdw_linux_gui_get_capture_device(void)
{
	return s_capture_device;
}
const char *pdw_linux_gui_get_playback_device(void)
{
	return s_playback_device;
}

static void log_ui(const char *action, const char *detail)
{
	pdw_platform_log_ui(action, detail);
}

static void clear_pane1_cb(gpointer unused) {
	(void)unused;
	log_ui("Clear Pane 1", NULL);
	ClearPanes(true, false);
}
static void clear_pane2_cb(gpointer unused) {
	(void)unused;
	log_ui("Clear Pane 2", NULL);
	ClearPanes(false, true);
}
static void on_options_clicked(void);

static void on_menu_activate(GtkMenuItem *item, gpointer data)
{
	(void)item;
	int id = GPOINTER_TO_INT(data);
	switch (id) {
		case IDM_EXIT:
			log_ui("Menu Exit", NULL);
			gtk_widget_destroy(s_win);
			return;
		case IDM_INTERFACE:
		case IDM_VOLUME:
			log_ui("Menu Setup/Volume", NULL);
			on_audio_clicked(NULL, NULL);
			return;
		case IDM_CLEARDISPLAY:
			log_ui("Menu Clear Screen", NULL);
			ClearPanes(true, true);
			return;
		case IDM_OPTIONS:
			log_ui("Menu Options", "opening Options dialog");
			on_options_clicked();
			return;
		case IDM_ABOUT: {
			log_ui("Menu About", NULL);
			GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(s_win), GTK_DIALOG_MODAL,
				GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "PDW Linux\nPager Data Decoder (Linux port)");
			gtk_dialog_run(GTK_DIALOG(d));
			gtk_widget_destroy(d);
			return;
		}
		default:
			return;
	}
}

/* Options dialog: mirrors Windows OPTIONSDLGBOX — POCSAG, FLEX, MOBITEX, ACARS, optional titlebar. */
static void on_options_clicked(void)
{
	extern void WriteSettings(void);
	extern void pd_reset_all(void);
	extern void Reset_ATB(void);

	GtkWidget *dlg = gtk_dialog_new_with_buttons("PDW Options", GTK_WINDOW(s_win),
		(GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
		"Cancel", GTK_RESPONSE_REJECT, "OK", GTK_RESPONSE_ACCEPT, NULL);
	gtk_window_set_default_size(GTK_WINDOW(dlg), 480, 520);
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 440);
	gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 4);
	GtkWidget *grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	gtk_container_add(GTK_CONTAINER(scroll), grid);
	int row = 0;

#define LABEL(s) do { GtkWidget *_l = gtk_label_new(s); gtk_widget_set_halign(_l, GTK_ALIGN_START); gtk_grid_attach(GTK_GRID(grid), _l, 0, row, 3, 1); row++; } while(0)
	/* POCSAG */
	LABEL("POCSAG");
	GtkWidget *ch_decodepocsag, *ch_pocsag_512, *ch_pocsag_1200, *ch_pocsag_2400, *ch_pocsag_fnu, *ch_pocsag_showboth;
	{ GtkWidget *_c = gtk_check_button_new_with_label("Enable POCSAG decoding"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), Profile.decodepocsag ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_decodepocsag = _c; row++; }
	{ GtkWidget *_c = gtk_check_button_new_with_label("512 baud"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), Profile.pocsag_512 ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_pocsag_512 = _c; row++; }
	{ GtkWidget *_c = gtk_check_button_new_with_label("1200 baud"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), Profile.pocsag_1200 ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_pocsag_1200 = _c; row++; }
	{ GtkWidget *_c = gtk_check_button_new_with_label("2400 baud"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), Profile.pocsag_2400 ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_pocsag_2400 = _c; row++; }
	{ GtkWidget *_c = gtk_check_button_new_with_label("Decode function numbers always as default"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), Profile.pocsag_fnu ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_pocsag_fnu = _c; row++; }
	{ GtkWidget *_c = gtk_check_button_new_with_label("If no accurate guess, display both numeric and alphanumeric"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), Profile.pocsag_showboth ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_pocsag_showboth = _c; row++; }

	/* FLEX */
	LABEL("FLEX");
	GtkWidget *ch_decodeflex, *ch_flex_1600, *ch_flex_3200, *ch_flex_6400, *ch_showinstr, *ch_convert_si, *ch_FlexTIME;
	{ GtkWidget *_c = gtk_check_button_new_with_label("Enable FLEX decoding"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), Profile.decodeflex ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_decodeflex = _c; row++; }
	{ GtkWidget *_c = gtk_check_button_new_with_label("1600 bps"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), Profile.flex_1600 ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_flex_1600 = _c; row++; }
	{ GtkWidget *_c = gtk_check_button_new_with_label("3200 bps"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), Profile.flex_3200 ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_flex_3200 = _c; row++; }
	{ GtkWidget *_c = gtk_check_button_new_with_label("6400 bps"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), Profile.flex_6400 ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_flex_6400 = _c; row++; }
	{ GtkWidget *_c = gtk_check_button_new_with_label("Show Short Instructions"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), Profile.showinstr ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_showinstr = _c; row++; }
	{ GtkWidget *_c = gtk_check_button_new_with_label("Convert Short Instructions to Text"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), Profile.convert_si ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_convert_si = _c; row++; }
	{ GtkWidget *_c = gtk_check_button_new_with_label("Use FlexTIME as System time"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), Profile.FlexTIME ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_FlexTIME = _c; row++; }

	/* MOBITEX */
	LABEL("MOBITEX");
	GtkWidget *ch_mb_usefrsync, *ch_mb_bitscrambler, *ch_mb_ramnet, *ch_mb_showmpak, *ch_mb_showtext, *ch_mb_showdata, *ch_mb_showhpdata, *ch_mb_showhpid, *ch_mb_showsweep, *ch_mb_verbose;
	{ GtkWidget *_c = gtk_check_button_new_with_label("Check Frame Sync (use custom Frame Sync)"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), mb.cfs ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_mb_usefrsync = _c; row++; }
	GtkWidget *l_frsync = gtk_label_new("Frame Sync (hex):");
	gtk_widget_set_halign(l_frsync, GTK_ALIGN_END);
	gtk_grid_attach(GTK_GRID(grid), l_frsync, 0, row, 1, 1);
	char frsync_buf[8];
	snprintf(frsync_buf, sizeof(frsync_buf), "%04X", mb.frsync);
	GtkWidget *e_frsync = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(e_frsync), frsync_buf);
	gtk_entry_set_max_length(GTK_ENTRY(e_frsync), 4);
	gtk_grid_attach(GTK_GRID(grid), e_frsync, 1, row, 1, 1);
	row++;
	GtkWidget *l_bitsync = gtk_label_new("Bit Sync:");
	gtk_widget_set_halign(l_bitsync, GTK_ALIGN_END);
	gtk_grid_attach(GTK_GRID(grid), l_bitsync, 0, row, 1, 1);
	GtkWidget *combo_bitsync = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_bitsync), "Base");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_bitsync), "Mobile");
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo_bitsync), (mb.bitsync == 0xCCCC) ? 0 : 1);
	gtk_grid_attach(GTK_GRID(grid), combo_bitsync, 1, row, 1, 1);
	row++;
	GtkWidget *l_minmsg = gtk_label_new("Min. characters:");
	gtk_widget_set_halign(l_minmsg, GTK_ALIGN_END);
	gtk_grid_attach(GTK_GRID(grid), l_minmsg, 0, row, 1, 1);
	char minmsg_buf[8];
	snprintf(minmsg_buf, sizeof(minmsg_buf), "%d", mb.min_msg_len);
	GtkWidget *e_minmsg = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(e_minmsg), minmsg_buf);
	gtk_entry_set_max_length(GTK_ENTRY(e_minmsg), 2);
	gtk_grid_attach(GTK_GRID(grid), e_minmsg, 1, row, 1, 1);
	row++;
	GtkWidget *l_brate = gtk_label_new("Bitrate:");
	gtk_widget_set_halign(l_brate, GTK_ALIGN_END);
	gtk_grid_attach(GTK_GRID(grid), l_brate, 0, row, 1, 1);
	GtkWidget *combo_bitrate = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_bitrate), "1200");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_bitrate), "8000");
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo_bitrate), (mb.bitrate == 8000) ? 1 : 0);
	gtk_grid_attach(GTK_GRID(grid), combo_bitrate, 1, row, 1, 1);
	row++;
	{ GtkWidget *_c = gtk_check_button_new_with_label("Bit Scrambler"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), mb.bitscr ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_mb_bitscrambler = _c; row++; }
	{ GtkWidget *_c = gtk_check_button_new_with_label("Ramnet"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), mb.ramnet ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_mb_ramnet = _c; row++; }
	{ GtkWidget *_c = gtk_check_button_new_with_label("Show Network Messages (MPAK)"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), (mb.show & MOBITEX_SHOW_MPAK) ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_mb_showmpak = _c; row++; }
	{ GtkWidget *_c = gtk_check_button_new_with_label("Show TEXT"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), (mb.show & MOBITEX_SHOW_TEXT) ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_mb_showtext = _c; row++; }
	{ GtkWidget *_c = gtk_check_button_new_with_label("Show DATA"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), (mb.show & MOBITEX_SHOW_DATA) ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_mb_showdata = _c; row++; }
	{ GtkWidget *_c = gtk_check_button_new_with_label("Show HPDATA"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), (mb.show & MOBITEX_SHOW_HPDATA) ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_mb_showhpdata = _c; row++; }
	{ GtkWidget *_c = gtk_check_button_new_with_label("Show HPID"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), (mb.show & MOBITEX_SHOW_HPID) ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_mb_showhpid = _c; row++; }
	{ GtkWidget *_c = gtk_check_button_new_with_label("Show Sweep Frames"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), (mb.show & MOBITEX_SHOW_SWEEP) ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_mb_showsweep = _c; row++; }
	{ GtkWidget *_c = gtk_check_button_new_with_label("Verbose"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), (mb.show & MOBITEX_VERBOSE) ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_mb_verbose = _c; row++; }

	/* ACARS */
	LABEL("ACARS");
	GtkWidget *acars_yes = gtk_radio_button_new_with_label(NULL, "Full Parity Checking");
	GSList *acars_group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(acars_yes));
	GtkWidget *acars_no = gtk_radio_button_new_with_label(acars_group, "No Parity Checking");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(Profile.acars_parity_check ? acars_yes : acars_no), TRUE);
	gtk_grid_attach(GTK_GRID(grid), acars_yes, 0, row, 2, 1); row++;
	gtk_grid_attach(GTK_GRID(grid), acars_no, 0, row, 2, 1); row++;

	/* Optional in title bar */
	LABEL("Optional in title bar");
	GtkWidget *ch_show_cfs, *ch_show_rejectblocked;
	{ GtkWidget *_c = gtk_check_button_new_with_label("FLEX/ERMES Cycles & Frames"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), Profile.show_cfs ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_show_cfs = _c; row++; }
	{ GtkWidget *_c = gtk_check_button_new_with_label("Rejected/Blocked Messages"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_c), Profile.show_rejectblocked ? TRUE : FALSE); gtk_grid_attach(GTK_GRID(grid), _c, 0, row, 3, 1); ch_show_rejectblocked = _c; row++; }

	gtk_widget_show_all(scroll);

	gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
	if (resp == GTK_RESPONSE_ACCEPT) {
		log_ui("Options", "applied");
		Profile.decodepocsag    = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_decodepocsag)) ? 1 : 0;
		Profile.pocsag_512     = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_pocsag_512)) ? 1 : 0;
		Profile.pocsag_1200    = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_pocsag_1200)) ? 1 : 0;
		Profile.pocsag_2400    = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_pocsag_2400)) ? 1 : 0;
		Profile.pocsag_fnu     = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_pocsag_fnu)) ? 1 : 0;
		Profile.pocsag_showboth = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_pocsag_showboth)) ? 1 : 0;
		Profile.decodeflex     = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_decodeflex)) ? 1 : 0;
		Profile.flex_1600      = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_flex_1600)) ? 1 : 0;
		Profile.flex_3200      = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_flex_3200)) ? 1 : 0;
		Profile.flex_6400      = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_flex_6400)) ? 1 : 0;
		Profile.showinstr      = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_showinstr)) ? 1 : 0;
		Profile.convert_si     = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_convert_si)) ? 1 : 0;
		Profile.FlexTIME       = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_FlexTIME)) ? 1 : 0;
		Profile.show_cfs       = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_show_cfs)) ? 1 : 0;
		Profile.show_rejectblocked = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_show_rejectblocked)) ? 1 : 0;
		Profile.acars_parity_check = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(acars_yes)) ? 1 : 0;
		mb.cfs    = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_mb_usefrsync)) ? 1 : 0;
		mb.bitscr = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_mb_bitscrambler)) ? 1 : 0;
		mb.ramnet = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_mb_ramnet)) ? 1 : 0;
		mb.show   = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_mb_showmpak)) ? MOBITEX_SHOW_MPAK : 0;
		mb.show  += gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_mb_showtext)) ? MOBITEX_SHOW_TEXT : 0;
		mb.show  += gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_mb_showhpdata)) ? MOBITEX_SHOW_HPDATA : 0;
		mb.show  += gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_mb_showdata)) ? MOBITEX_SHOW_DATA : 0;
		mb.show  += gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_mb_showhpid)) ? MOBITEX_SHOW_HPID : 0;
		mb.show  += gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_mb_showsweep)) ? MOBITEX_SHOW_SWEEP : 0;
		mb.show  += gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_mb_verbose)) ? MOBITEX_VERBOSE : 0;
		gint bitsync_sel = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_bitsync));
		mb.bitsync = (bitsync_sel == 1) ? 0x3333 : 0xCCCC;
		mb.bitsync_rev = ~mb.bitsync;
		const char *fs = gtk_entry_get_text(GTK_ENTRY(e_frsync));
		unsigned int frsync_val = 0;
		if (fs && sscanf(fs, "%04X", &frsync_val) >= 1) { mb.frsync = frsync_val; mb.frsync_rev = ~mb.frsync; }
		int min_msg_val = 0;
		if (sscanf(gtk_entry_get_text(GTK_ENTRY(e_minmsg)), "%d", &min_msg_val) >= 1) mb.min_msg_len = min_msg_val;
		gint br_sel = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_bitrate));
		mb.bitrate = (br_sel == 1) ? 8000 : 1200;
		if (!Profile.decodeflex || !Profile.decodepocsag) {
			if (!Profile.monitor_acars) {
				pd_reset_all();
				Reset_ATB();
			}
		}
		WriteSettings();
	} else {
		log_ui("Options", "cancelled");
	}
	gtk_widget_destroy(dlg);
#undef LABEL
}

static GtkWidget *add_menu_item(GtkWidget *menu, const char *label, int id)
{
	GtkWidget *mi = gtk_menu_item_new_with_label(label);
	g_signal_connect(mi, "activate", G_CALLBACK(on_menu_activate), GINT_TO_POINTER(id));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
	return mi;
}

static void apply_dark_theme(void)
{
	GtkCssProvider *css = gtk_css_provider_new();
	const char *style =
		"window, .pdw-main { background-color: #2b2b2b; }\n"
		/* Menubar and dropdown menus: light background and dark text (like Windows). */
		".pdw-menubar { background-color: #e8e8e8; color: #1a1a1a; }\n"
		".pdw-menubar:selected { background-color: #c0c0c0; color: #1a1a1a; }\n"
		"menubar, .pdw-menubar { padding: 2px 0; }\n"
		"menu { background-color: #e8e8e8; color: #1a1a1a; }\n"
		"menuitem { color: #1a1a1a; }\n"
		"menuitem:hover { background-color: #c0c0c0; color: #1a1a1a; }\n"
		".pdw-toolbar { background: #3c3c3c; border-bottom: 1px solid #555; padding: 4px; }\n"
		".pdw-header { background: #505050; color: #e0e0e0; font-family: monospace; font-size: 11px; padding: 2px 6px; }\n"
		".pdw-pane { background-color: #000000; color: #c0c0c0; font-family: monospace; font-size: 11px; }\n"
		".pdw-divider { background: #3c3c3c; min-height: 6px; }\n"
		/* Limit combo dropdown height so the audio device list doesn't create a huge white area */
		"GtkComboBox menu { max-height: 260px; }\n";
	gtk_css_provider_load_from_data(css, style, -1, NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
		GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(css);
}

static GtkWidget *make_header_row(const char *last_col_label)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_size_request(box, -1, HEADER_HEIGHT);
	GtkStyleContext *ctx = gtk_widget_get_style_context(box);
	gtk_style_context_add_class(ctx, "pdw-header");
	static const char *cols[] = { "Address", "Time", "Date", "Mode", "Type", "Bitrate", NULL };
	static const int widths[] = { 75, 70, 75, 50, 55, 55 };
	for (int i = 0; cols[i]; i++) {
		GtkWidget *l = gtk_label_new(cols[i]);
		gtk_widget_set_size_request(l, widths[i], -1);
		gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
		gtk_box_pack_start(GTK_BOX(box), l, FALSE, FALSE, 2);
	}
	GtkWidget *lmsg = gtk_label_new(last_col_label);
	gtk_label_set_xalign(GTK_LABEL(lmsg), 0.0f);
	gtk_box_pack_start(GTK_BOX(box), lmsg, TRUE, TRUE, 2);
	return box;
}

/* Default palette: 0=unused (light so visible on black), 1=background, 2=address, 3=timestamp, ... R,G,B. */
static const unsigned char s_color_rgb[32][3] = {
	{192,192,192},{0,0,0},{255,255,255},{0,0,255},{255,0,0},{0,255,255},{255,0,0},{128,128,64},{192,192,192},{0,255,0},
	{0,255,255},{255,255,255},{255,0,0},{255,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},
	{0,0,255},{255,255,0},{255,0,0},{0,0,0},{0,0,255},{0,255,255},{255,255,255},{0,255,0},{192,192,192},{255,165,0},{0,255,255}
};

static void ensure_color_tags(GtkTextBuffer *buf)
{
	if (gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buf), "c0")) return;
	for (int i = 0; i < 32; i++) {
		char name[8];
		snprintf(name, sizeof(name), "c%d", i);
		GdkRGBA fg;
		fg.red = s_color_rgb[i][0] / 255.; fg.green = s_color_rgb[i][1] / 255.; fg.blue = s_color_rgb[i][2] / 255.; fg.alpha = 1.0;
		gtk_text_buffer_create_tag(buf, name, "foreground-rgba", &fg, NULL);
	}
}

/* Pane content is Latin-1 (like Windows); convert to UTF-8 for GTK. Caller g_free result. */
static char *latin1_to_utf8(const char *str, int len)
{
	if (!str || len == 0) return NULL;
	if (len < 0) len = (int)strlen(str);
	/* Max UTF-8 size: each Latin-1 byte can become 2 bytes (0x80-0xFF). */
	size_t out_cap = (size_t)len * 2 + 1;
	char *out = (char *)g_malloc(out_cap);
	if (!out) return NULL;
	size_t o = 0;
	for (int i = 0; i < len && o + 4 < out_cap; i++) {
		unsigned char b = (unsigned char)str[i];
		if (b <= 0x7F)
			out[o++] = (char)b;
		else {
			out[o++] = (char)(0xC0u | (b >> 6));
			out[o++] = (char)(0x80u | (b & 0x3F));
		}
	}
	out[o] = '\0';
	return out;
}

static void refresh_pane(PaneStruct *pane, GtkTextView *view, unsigned int *p_last_bottom)
{
	if (!pane || !pane->buff_char || !view || !p_last_bottom) return;
	unsigned int n = pane->Bottom;
	if (n > pane->buff_lines) n = pane->buff_lines;
	/* Don’t redraw or touch scroll when nothing changed — prevents scrollbar jumping. */
	if (n == *p_last_bottom) return;
	gboolean new_lines = (n > *p_last_bottom);
	*p_last_bottom = n;

	GtkTextBuffer *buf = gtk_text_view_get_buffer(view);
	GtkTextIter start, end;
	gtk_text_buffer_get_bounds(buf, &start, &end);
	gtk_text_buffer_delete(buf, &start, &end);
	BYTE *col = pane->buff_color;
	ensure_color_tags(buf);
	/* Draw in reverse order: newest (last line in buffer) at top, older below. */
	for (unsigned int i = n; i > 0; i--) {
		unsigned int idx = i - 1;
		size_t off = idx * (LINE_SIZE + 1);
		const char *line = &pane->buff_char[off];
		if (!col) {
			char plain[LINE_SIZE + 2];
			memcpy(plain, line, LINE_SIZE);
			plain[LINE_SIZE] = '\n'; plain[LINE_SIZE + 1] = '\0';
			char *valid = latin1_to_utf8(plain, -1);
			if (valid) {
				gtk_text_buffer_get_end_iter(buf, &end);
				gtk_text_buffer_insert(buf, &end, valid, -1);
				g_free(valid);
			}
			continue;
		}
		for (size_t j = 0; j < LINE_SIZE; ) {
			BYTE c = col[off + j];
			if (c > 31) c = 0;
			size_t run = j;
			while (run < LINE_SIZE && col[off + run] == c) run++;
			char tag_name[8];
			snprintf(tag_name, sizeof(tag_name), "c%d", (int)c);
			gint count_before = gtk_text_buffer_get_char_count(buf);
			GtkTextIter iter;
			gtk_text_buffer_get_end_iter(buf, &iter);
			char *valid = latin1_to_utf8(line + j, (int)(run - j));
			if (valid) {
				gtk_text_buffer_insert(buf, &iter, valid, -1);
				g_free(valid);
			}
			gint count_after = gtk_text_buffer_get_char_count(buf);
			if (count_after > count_before) {
				GtkTextIter start_run, end_run;
				gtk_text_buffer_get_iter_at_offset(buf, &start_run, count_before);
				gtk_text_buffer_get_iter_at_offset(buf, &end_run, count_after);
				gtk_text_buffer_apply_tag_by_name(buf, tag_name, &start_run, &end_run);
			}
			j = run;
		}
		gtk_text_buffer_get_end_iter(buf, &end);
		gtk_text_buffer_insert(buf, &end, "\n", 1);
	}
	/* Scroll to top only when new message(s) arrived, so we track latest without constant jumping. */
	if (new_lines && n > 0) {
		gtk_text_buffer_get_start_iter(buf, &start);
		gtk_text_view_scroll_to_iter(view, &start, 0.0, FALSE, 0.0, 0.0);
	}
}

/* Map 0..100% to -20..+3 VU (like analog VU meter). */
static double level_to_vu(double level)
{
	if (level <= 0.0) return -20.0;
	if (level >= 100.0) return 3.0;
	/* Logarithmic-ish: 0% -> -20, 100% -> +3 */
	double db = 20.0 * log10(level / 100.0 + 0.01);
	if (db < -20.0) return -20.0;
	if (db > 3.0) return 3.0;
	return db;
}

/* Timer: only read level, write VU, queue draw — no other GTK (keeps meter responsive). */
static gboolean meter_cb(gpointer user_data)
{
	(void)user_data;
	if (s_quit) return G_SOURCE_REMOVE;
	double level = pdw_linux_get_input_level();
	if (level < 0.0) level = 0.0;
	if (level > 100.0) level = 100.0;
	s_meter_vu = level_to_vu(level);
	/* Smooth needle (ballistic): follow with decay so it doesn’t jump. */
	double diff = s_meter_vu - s_meter_display_vu;
	double alpha = (diff > 0.0) ? 0.88 : 0.58;   /* fast attack, smooth decay */
	s_meter_display_vu += alpha * diff;
	if (s_signal_meter)
		gtk_widget_queue_draw(s_signal_meter);
	return G_SOURCE_CONTINUE;
}

/* Draw classic analog VU meter: -20..+3 VU scale, red overload zone, percentage scale, needle. */
static gboolean vu_meter_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
	(void)data;
	int w = gtk_widget_get_allocated_width(widget);
	int h = gtk_widget_get_allocated_height(widget);
	if (w < 20 || h < 40) return G_DBUS_METHOD_INVOCATION_HANDLED;

	double cx = w * 0.5;
	double cy = h - 8.0;           /* pivot at bottom center */
	double radius = (double)(h - 24) * 0.85;
	double arc_start = 135.0 * (M_PI / 180.0);   /* left side (-20 VU) */
	double arc_end   = 45.0 * (M_PI / 180.0);    /* right side (+3 VU) */
	double arc_len   = arc_start - arc_end;       /* 90° */

	/* Beige face */
	cairo_set_source_rgb(cr, 0.96, 0.94, 0.88);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_fill(cr);

	/* Dark frame */
	cairo_set_source_rgb(cr, 0.25, 0.25, 0.25);
	cairo_set_line_width(cr, 2.0);
	cairo_rectangle(cr, 1, 1, w - 2, h - 2);
	cairo_stroke(cr);

	/* Red zone arc (0 to +3 VU) */
	double red_start = arc_start - (20.0 / 23.0) * arc_len;
	cairo_arc_negative(cr, cx, cy, radius + 4, arc_start, red_start);
	cairo_set_source_rgb(cr, 0.85, 0.15, 0.15);
	cairo_set_line_width(cr, 8.0);
	cairo_stroke(cr);

	/* Black scale arc */
	cairo_arc_negative(cr, cx, cy, radius + 4, arc_start, arc_end);
	cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
	cairo_set_line_width(cr, 6.0);
	cairo_stroke(cr);

	/* Tick marks and VU labels: -20, -10, -7, -5, -3, -2, -1, 0, +1, +2, +3 */
	static const int vu_ticks[] = { -20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3 };
	cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
	cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 9.0);
	for (int i = 0; i < (int)(sizeof(vu_ticks)/sizeof(vu_ticks[0])); i++) {
		double t = (double)(vu_ticks[i] + 20) / 23.0;
		double a = arc_start - t * arc_len;
		double inner = radius - 2, outer = radius + 6;
		double x1 = cx + inner * cos(a), y1 = cy - inner * sin(a);
		double x2 = cx + outer * cos(a), y2 = cy - outer * sin(a);
		cairo_move_to(cr, x1, y1);
		cairo_line_to(cr, x2, y2);
		cairo_stroke(cr);
		char buf[8];
		snprintf(buf, sizeof(buf), "%+d", vu_ticks[i]);
		double tx = cx + (radius + 12) * cos(a) - 4;
		double ty = cy - (radius + 12) * sin(a) + 4;
		cairo_move_to(cr, tx, ty);
		cairo_show_text(cr, buf);
	}

	/* Percentage scale (20, 40, 60, 80, 100) below */
	cairo_set_font_size(cr, 8.0);
	for (int p = 20; p <= 100; p += 20) {
		double t = (double)p / 100.0;
		double vu = -20.0 + 23.0 * t;
		double a = arc_start - (vu + 20.0) / 23.0 * arc_len;
		double tx = cx + (radius - 14) * cos(a) - 4;
		double ty = cy - (radius - 14) * sin(a) + 3;
		char buf[8];
		snprintf(buf, sizeof(buf), "%d", p);
		cairo_move_to(cr, tx, ty);
		cairo_show_text(cr, buf);
	}

	/* Needle: from pivot to tip at s_meter_display_vu */
	double t = (s_meter_display_vu + 20.0) / 23.0;
	if (t < 0.0) t = 0.0;
	if (t > 1.0) t = 1.0;
	double angle = arc_start - t * arc_len;
	double tip_x = cx + (radius - 4) * cos(angle);
	double tip_y = cy - (radius - 4) * sin(angle);
	cairo_move_to(cr, cx, cy);
	cairo_line_to(cr, tip_x, tip_y);
	cairo_set_source_rgb(cr, 0.05, 0.05, 0.05);
	cairo_set_line_width(cr, 2.0);
	cairo_stroke(cr);

	return FALSE;
}

/* One-shot idle: refresh panes (called from main thread when decode completes a line). */
static gboolean refresh_panes_idle(gpointer unused)
{
	(void)unused;
	if (s_quit) return G_SOURCE_REMOVE;
	if (pdw_platform_verbose() >= 2)
		fprintf(stderr, "[PDW] DISPLAY: refresh_pane Pane1.Bottom=%u Pane2.Bottom=%u\n",
			(unsigned)Pane1.Bottom, (unsigned)Pane2.Bottom);
	if (s_pane1_text) refresh_pane(&Pane1, GTK_TEXT_VIEW(s_pane1_text), &s_last_pane1_bottom);
	if (s_pane2_text) refresh_pane(&Pane2, GTK_TEXT_VIEW(s_pane2_text), &s_last_pane2_bottom);
	return G_SOURCE_REMOVE;
}

/* Called from capture thread when a line is flushed; schedule refresh on main thread. */
static void schedule_pane_refresh(void)
{
	g_idle_add(refresh_panes_idle, NULL);
}

/* Slower: panes + device label (reduces UI load). */
static gboolean refresh_cb(gpointer user_data)
{
	(void)user_data;
	if (s_quit) return G_SOURCE_REMOVE;
	if (s_pane1_text) refresh_pane(&Pane1, GTK_TEXT_VIEW(s_pane1_text), &s_last_pane1_bottom);
	if (s_pane2_text) refresh_pane(&Pane2, GTK_TEXT_VIEW(s_pane2_text), &s_last_pane2_bottom);
	if (s_input_device_label) {
		char buf[MAX_DEVICE_DESC_LEN + 16];
		if (s_capture_device_desc[0])
			snprintf(buf, sizeof(buf), "Input: %s", s_capture_device_desc);
		else
			snprintf(buf, sizeof(buf), "Input: %s", s_capture_device[0] ? s_capture_device : "default");
		gtk_label_set_text(GTK_LABEL(s_input_device_label), buf);
	}
	return G_SOURCE_CONTINUE;
}

static void on_destroy(GtkWidget *widget, gpointer data)
{
	(void)widget;
	(void)data;
	s_quit = 1;
	pdw_linux_alsa_stop();
	gtk_main_quit();
}

void pdw_linux_gui_quit(void)
{
	s_quit = 1;
}

int pdw_linux_gui_init(int *argc, char ***argv)
{
	if (!gtk_init_check(argc, argv)) return -1;
	apply_dark_theme();

	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	if (!win) return -1;
	gtk_window_set_title(GTK_WINDOW(win), pdw_version ? pdw_version : "PDW Linux");
	gtk_window_set_default_size(GTK_WINDOW(win), 800, 600);
	g_signal_connect(win, "destroy", G_CALLBACK(on_destroy), NULL);

	GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(win), main_box);
	gtk_style_context_add_class(gtk_widget_get_style_context(main_box), "pdw-main");

	/* Toolbar: icons from GFX folder (match Windows Rsrc.rc: options, filter, stats, clr). */
	GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_widget_set_size_request(toolbar, -1, TOOLBAR_HEIGHT);
	gtk_style_context_add_class(gtk_widget_get_style_context(toolbar), "pdw-toolbar");
	GtkWidget *tb_setup = toolbar_button_from_gfx("options.bmp", "Setup", "preferences-system");
	GtkWidget *tb_audio = toolbar_button_from_gfx("stats.bmp", "Sound / Input & Playback devices", "audio-volume-high");
	GtkWidget *tb_clear1 = toolbar_button_from_gfx("clr.bmp", "Clear Pane 1", "edit-clear");
	GtkWidget *tb_clear2 = toolbar_button_from_gfx("clr.bmp", "Clear Pane 2", "edit-clear");
	g_signal_connect(tb_setup, "clicked", G_CALLBACK(on_audio_clicked), NULL);
	g_signal_connect(tb_audio, "clicked", G_CALLBACK(on_audio_clicked), NULL);
	g_signal_connect(tb_clear1, "clicked", G_CALLBACK(clear_pane1_cb), NULL);
	g_signal_connect(tb_clear2, "clicked", G_CALLBACK(clear_pane2_cb), NULL);
	gtk_box_pack_start(GTK_BOX(toolbar), tb_setup, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(toolbar), tb_audio, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(toolbar), tb_clear1, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(toolbar), tb_clear2, FALSE, FALSE, 2);
	/* Menu bar: structure and labels match Windows Rsrc.rc (PDWMENU). */
	s_win = win;
	GtkWidget *menubar = gtk_menu_bar_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(menubar), "pdw-menubar");

	GtkWidget *file_menu = gtk_menu_new();
	GtkWidget *mi_file = gtk_menu_item_new_with_label("File");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi_file), file_menu);
	add_menu_item(file_menu, "Exit", IDM_EXIT);

	GtkWidget *interface_menu = gtk_menu_new();
	GtkWidget *mi_interface = gtk_menu_item_new_with_label("Interface");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi_interface), interface_menu);
	add_menu_item(interface_menu, "Setup...", IDM_INTERFACE);
	add_menu_item(interface_menu, "Volume...", IDM_VOLUME);

	GtkWidget *options_menu = gtk_menu_new();
	GtkWidget *mi_options = gtk_menu_item_new_with_label("Options");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi_options), options_menu);
	add_menu_item(options_menu, "Options...", IDM_OPTIONS);

	GtkWidget *display_menu = gtk_menu_new();
	GtkWidget *mi_display = gtk_menu_item_new_with_label("Display");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi_display), display_menu);
	add_menu_item(display_menu, "Clear Screen...", IDM_CLEARDISPLAY);

	GtkWidget *help_menu = gtk_menu_new();
	GtkWidget *mi_help = gtk_menu_item_new_with_label("Help");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi_help), help_menu);
	add_menu_item(help_menu, "About...", IDM_ABOUT);

	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), mi_file);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), mi_interface);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), mi_options);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), mi_display);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), mi_help);
	gtk_box_pack_start(GTK_BOX(main_box), menubar, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(main_box), toolbar, FALSE, FALSE, 0);

	/* Paned: Pane1 (top) and Pane2 (bottom), resizable */
	GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);

	/* Pane 1 block: header + scrolled text (no meter here; meter is in sidebar) */
	GtkWidget *pane1_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *pane1_header_row = make_header_row("Monitored Messages");
	GtkWidget *pane1_header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_pack_start(GTK_BOX(pane1_header_box), pane1_header_row, TRUE, TRUE, 0);
	s_input_device_label = gtk_label_new("Input: —");
	gtk_widget_set_tooltip_text(s_input_device_label, "Current capture device (set via Speaker/Audio)");
	gtk_box_pack_end(GTK_BOX(pane1_header_box), s_input_device_label, FALSE, FALSE, 4);
	gtk_box_pack_start(GTK_BOX(pane1_box), pane1_header_box, FALSE, FALSE, 0);
	GtkWidget *sw1 = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw1), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(sw1), 180);
	s_pane1_text = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(s_pane1_text), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(s_pane1_text), TRUE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(s_pane1_text), GTK_WRAP_NONE);
	{ GdkRGBA black = { 0.0, 0.0, 0.0, 1.0 }; gtk_widget_override_background_color(s_pane1_text, GTK_STATE_FLAG_NORMAL, &black); }
	{ GdkRGBA fg = { 0.75, 0.75, 0.75, 1.0 }; gtk_widget_override_color(s_pane1_text, GTK_STATE_FLAG_NORMAL, &fg); }
	gtk_container_add(GTK_CONTAINER(sw1), s_pane1_text);
	gtk_box_pack_start(GTK_BOX(pane1_box), sw1, TRUE, TRUE, 0);
	gtk_paned_add1(GTK_PANED(paned), pane1_box);

	/* Pane 2 block: header + scrolled text (paned draws divider between pane1 and pane2) */
	GtkWidget *pane2_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(pane2_box), make_header_row("Filtered Messages"), FALSE, FALSE, 0);
	GtkWidget *sw2 = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw2), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(sw2), 120);
	s_pane2_text = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(s_pane2_text), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(s_pane2_text), TRUE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(s_pane2_text), GTK_WRAP_NONE);
	{ GdkRGBA black = { 0.0, 0.0, 0.0, 1.0 }; gtk_widget_override_background_color(s_pane2_text, GTK_STATE_FLAG_NORMAL, &black); }
	{ GdkRGBA fg = { 0.75, 0.75, 0.75, 1.0 }; gtk_widget_override_color(s_pane2_text, GTK_STATE_FLAG_NORMAL, &fg); }
	gtk_container_add(GTK_CONTAINER(sw2), s_pane2_text);
	gtk_box_pack_start(GTK_BOX(pane2_box), sw2, TRUE, TRUE, 0);
	gtk_paned_add2(GTK_PANED(paned), pane2_box);

	/* Set paned position so Pane1 gets ~70% height (approx 800x600 -> ~420 for pane1) */
	gtk_paned_set_position(GTK_PANED(paned), 420);

	/* Content: paned (main area) + right sidebar with dB meter */
	GtkWidget *content_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_pack_start(GTK_BOX(content_hbox), paned, TRUE, TRUE, 0);

	GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_style_context_add_class(gtk_widget_get_style_context(sidebar), "pdw-toolbar");
	gtk_widget_set_size_request(sidebar, 130, -1);
	GtkWidget *meter_title = gtk_label_new("VU");
	gtk_box_pack_start(GTK_BOX(sidebar), meter_title, FALSE, FALSE, 2);
	s_signal_meter = gtk_drawing_area_new();
	gtk_widget_set_size_request(s_signal_meter, 120, 160);
	gtk_widget_set_vexpand(s_signal_meter, FALSE);
	g_signal_connect(G_OBJECT(s_signal_meter), "draw", G_CALLBACK(vu_meter_draw), NULL);
	gtk_widget_set_tooltip_text(s_signal_meter, "Input level — VU meter");
	gtk_box_pack_start(GTK_BOX(sidebar), s_signal_meter, FALSE, FALSE, 4);
	gtk_box_pack_end(GTK_BOX(content_hbox), sidebar, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(main_box), content_hbox, TRUE, TRUE, 0);

	gtk_widget_show_all(win);
	gtk_window_present(GTK_WINDOW(win));
	pdw_platform_register_pane_refresh_cb(schedule_pane_refresh);
	g_timeout_add(8, meter_cb, NULL);    /* ~120 Hz: snappy meter updates */
	g_timeout_add(150, refresh_cb, NULL); /* panes + device label */
	return 0;
}

void pdw_linux_gui_run(void)
{
	gtk_main();
}

#endif
