/*
 * GTK3 GUI for PDL Linux — Mockup-1 light layout:
 * Toolbar, column headers, light chrome, resizable panes.
 */
#ifdef __linux__

#include "Headers/pdl.h"
#include "Headers/initapp.h"
#include "Headers/sound_in.h"
#include "Headers/mobitex.h"
#include "Headers/misc.h"
#include "platform/pdl_platform.h"
#include "pdl_version.h"
#include "linux/gui_gtk.h"
#include "linux/gui_web.h"
#include "linux/pagercast_stream.h"
#include "linux/hw_decode.h"
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <cairo/cairo.h>
#include <pango/pango.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static GtkWidget *s_win = NULL;
static GtkWindow *s_dialog_parent = NULL; /* Web UI can set this so dialogs attach correctly */

static GtkWindow *dialog_parent(void)
{
	if (s_dialog_parent)
		return s_dialog_parent;
	if (s_win)
		return GTK_WINDOW(s_win);
	return NULL;
}

void pdl_linux_gui_set_dialog_parent(GtkWindow *parent)
{
	s_dialog_parent = parent;
}

static int path_is_readable(const char *path)
{
	return path && path[0] && access(path, R_OK) == 0;
}

static int find_app_icon_path(char *out, size_t outlen)
{
	/* Canonical app icon is GFX/pdl.ico (PDLICON). PNG variants are for
	 * freedesktop/hicolor installs and Wayland compositors that prefer PNG. */
	char c_exe_png[PATH_MAX], c_exe_ico[PATH_MAX];
	char c_gfx_ico[PATH_MAX], c_gfx_png[PATH_MAX];
	char c_share[PATH_MAX];
	const char *candidates[16];
	int nc = 0;

	char exe[PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n > 0) {
		exe[n] = '\0';
		char tmp[PATH_MAX];
		char *dir;

		strncpy(tmp, exe, sizeof(tmp) - 1);
		tmp[sizeof(tmp) - 1] = '\0';
		dir = dirname(tmp);
		snprintf(c_exe_png, sizeof(c_exe_png), "%s/pdl.png", dir);
		candidates[nc++] = c_exe_png;
		snprintf(c_exe_ico, sizeof(c_exe_ico), "%s/pdl.ico", dir);
		candidates[nc++] = c_exe_ico;

		strncpy(tmp, exe, sizeof(tmp) - 1);
		tmp[sizeof(tmp) - 1] = '\0';
		dir = dirname(tmp);
		snprintf(c_gfx_ico, sizeof(c_gfx_ico), "%s/../GFX/pdl.ico", dir);
		candidates[nc++] = c_gfx_ico;
		snprintf(c_gfx_png, sizeof(c_gfx_png), "%s/../GFX/pdl.png", dir);
		candidates[nc++] = c_gfx_png;

		strncpy(tmp, exe, sizeof(tmp) - 1);
		tmp[sizeof(tmp) - 1] = '\0';
		dir = dirname(tmp);
		snprintf(c_share, sizeof(c_share),
			"%s/../share/icons/hicolor/256x256/apps/pdl.png", dir);
		candidates[nc++] = c_share;
	}
	candidates[nc++] = "GFX/pdl.ico";
	candidates[nc++] = "../GFX/pdl.ico";
	candidates[nc++] = "GFX/pdl.png";
	candidates[nc++] = "../GFX/pdl.png";
	candidates[nc++] = "pdl.png";
	candidates[nc++] = "pdl.ico";
	candidates[nc++] = "/usr/share/icons/hicolor/256x256/apps/pdl.png";
#ifdef PDL_DATADIR
	candidates[nc++] = PDL_DATADIR "/../icons/hicolor/256x256/apps/pdl.png";
#endif
	candidates[nc] = NULL;

	for (int i = 0; candidates[i]; i++) {
		if (!path_is_readable(candidates[i]))
			continue;
		char *rp = realpath(candidates[i], NULL);
		if (rp) {
			strncpy(out, rp, outlen - 1);
			out[outlen - 1] = '\0';
			free(rp);
		} else {
			strncpy(out, candidates[i], outlen - 1);
			out[outlen - 1] = '\0';
		}
		return 0;
	}
	return -1;
}

void pdl_linux_gui_apply_app_icon(GtkWindow *win)
{
	char path[PATH_MAX];
	if (find_app_icon_path(path, sizeof(path)) != 0) {
		/* Last resort: theme name (only works once icons are installed). */
		gtk_window_set_default_icon_name("pdl");
		if (win)
			gtk_window_set_icon_name(win, "pdl");
		return;
	}

	/* Load several sizes — Wayland/X11 pick from the list; name alone is not enough. */
	static const int sizes[] = { 16, 24, 32, 48, 64, 128, 256 };
	GList *list = NULL;
	for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		GError *err = NULL;
		GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_size(path, sizes[i], sizes[i], &err);
		if (err) {
			g_error_free(err);
			continue;
		}
		if (pb)
			list = g_list_append(list, pb);
	}
	if (!list) {
		GError *err = NULL;
		GdkPixbuf *pb = gdk_pixbuf_new_from_file(path, &err);
		if (err) g_error_free(err);
		if (pb)
			list = g_list_append(list, pb);
	}
	if (!list)
		return;

	gtk_window_set_default_icon_list(list);
	if (win)
		gtk_window_set_icon_list(win, list);
	g_list_free_full(list, g_object_unref);

	/* Also register theme name once hicolor/desktop are present. */
	if (gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), "pdl")) {
		gtk_window_set_default_icon_name("pdl");
		if (win)
			gtk_window_set_icon_name(win, "pdl");
	}
}

/*
 * Dialog chrome: match main window styling. Do NOT reparent/grab into the
 * parent GdkWindow — that left an invisible gtk_grab on Hyprland/XWayland and
 * made the whole app look frozen after any menu that opened a dialog.
 */
void pdl_linux_gui_prepare_dialog(GtkWidget *dlg)
{
	if (!dlg || !GTK_IS_WINDOW(dlg)) return;
	GtkWindow *win = GTK_WINDOW(dlg);
	GtkWindow *parent = dialog_parent();

	gtk_style_context_add_class(gtk_widget_get_style_context(dlg), "pdl-dialog");
	gtk_window_set_type_hint(win, GDK_WINDOW_TYPE_HINT_DIALOG);
	gtk_window_set_skip_taskbar_hint(win, TRUE);
	gtk_window_set_skip_pager_hint(win, TRUE);
	gtk_window_set_decorated(win, TRUE);
	gtk_window_set_urgency_hint(win, FALSE);

	if (parent) {
		gtk_window_set_transient_for(win, parent);
		gtk_window_set_destroy_with_parent(win, TRUE);
		gtk_window_set_modal(win, TRUE);
		gtk_window_set_position(win, GTK_WIN_POS_CENTER_ON_PARENT);
	} else {
		gtk_window_set_position(win, GTK_WIN_POS_CENTER);
	}
}

/* Same shell as Options/Settings — never use GtkMessageDialog / GtkAboutDialog. */
static gint pdl_run_styled_dialog(GtkWidget *dlg)
{
	gtk_widget_show_all(dlg);
	gint r = gtk_dialog_run(GTK_DIALOG(dlg));
	gtk_widget_destroy(dlg);
	return r;
}

static void pdl_info_dialog(GtkWindow *parent, const char *title, const char *body)
{
	GtkWidget *dlg = gtk_dialog_new_with_buttons(title, parent ? parent : dialog_parent(),
		(GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
		"OK", GTK_RESPONSE_ACCEPT, NULL);
	pdl_linux_gui_prepare_dialog(dlg);
	if (parent)
		gtk_window_set_transient_for(GTK_WINDOW(dlg), parent);
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
	gtk_container_set_border_width(GTK_CONTAINER(content), 12);
	GtkWidget *lab = gtk_label_new(body);
	gtk_label_set_selectable(GTK_LABEL(lab), TRUE);
	gtk_widget_set_halign(lab, GTK_ALIGN_START);
	gtk_label_set_xalign(GTK_LABEL(lab), 0.0f);
	gtk_label_set_line_wrap(GTK_LABEL(lab), TRUE);
	gtk_box_pack_start(GTK_BOX(content), lab, TRUE, TRUE, 0);
	pdl_run_styled_dialog(dlg);
}

static gint pdl_confirm_dialog(GtkWindow *parent, const char *title, const char *body)
{
	GtkWidget *dlg = gtk_dialog_new_with_buttons(title, parent ? parent : dialog_parent(),
		(GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
		"No", GTK_RESPONSE_REJECT, "Yes", GTK_RESPONSE_ACCEPT, NULL);
	pdl_linux_gui_prepare_dialog(dlg);
	if (parent)
		gtk_window_set_transient_for(GTK_WINDOW(dlg), parent);
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
	gtk_container_set_border_width(GTK_CONTAINER(content), 12);
	GtkWidget *lab = gtk_label_new(body);
	gtk_widget_set_halign(lab, GTK_ALIGN_START);
	gtk_label_set_xalign(GTK_LABEL(lab), 0.0f);
	gtk_label_set_line_wrap(GTK_LABEL(lab), TRUE);
	gtk_box_pack_start(GTK_BOX(content), lab, TRUE, TRUE, 0);
	return pdl_run_styled_dialog(dlg);
}

static GtkWidget *s_pane1_text = NULL;
static GtkWidget *s_pane2_text = NULL;
static GtkWidget *s_sig_rssi_bar = NULL, *s_sig_rssi_lbl = NULL;
static GtkWidget *s_sig_sn_bar = NULL, *s_sig_sn_lbl = NULL;
static GtkWidget *s_sig_qual_bar = NULL, *s_sig_qual_lbl = NULL;
static GtkWidget *s_sig_ber_bar = NULL, *s_sig_ber_lbl = NULL;
static GtkWidget *s_dec_frames_bar = NULL, *s_dec_frames_lbl = NULL;
static GtkWidget *s_dec_errors_bar = NULL, *s_dec_errors_lbl = NULL;
static GtkWidget *s_dec_missed_bar = NULL, *s_dec_missed_lbl = NULL;
static GtkWidget *s_dec_rate_bar = NULL, *s_dec_rate_lbl = NULL;
static GtkWidget *s_footer_device_label = NULL;
static GtkWidget *s_pc_footer_led = NULL;
static GtkWidget *s_pc_footer_label = NULL;
static GtkWidget *s_pc_footer_box = NULL;
static GtkWidget *s_tb_pc_toggle = NULL; /* notebook: Local | PagerCast tabs */
static GtkWidget *s_tb_pc_sep = NULL;
static GtkWidget *s_header_label = NULL;
static int s_tb_pc_syncing = 0;
static GtkWidget *s_paned = NULL;
static GtkWidget *s_mi_monitor[4] = { NULL, NULL, NULL, NULL };
static GtkWidget *s_mi_tray = NULL;
static unsigned int s_last_pane1_bottom = 0, s_last_pane2_bottom = 0;
static double s_meter_level = 0.0;
static double s_meter_display_level = 0.0;
static double s_rx_display_quality = 0.0;
static volatile int s_quit = 0;
extern double dRX_Quality;
extern double dRX_MessageQuality;
extern int iRX_LastBchErrors;
extern int iRX_LastBchCodewords;
extern bool bRX_MessageQuality_Valid;
extern char szWindowText[6][1000];
extern HWND ghWnd;
extern void WriteSettings(void);
extern void UpdateFilters(void);
extern void pd_reset_all(void);
extern void Reset_ATB(void);
extern void SystemTrayWindow(bool bHideWindow);
extern void SystemTrayIcon(bool bRemoveIcon);
extern void SetAudioConfig(int n);
extern "C" int OpenComPort(void);
extern "C" int CloseComPort(void);
extern "C" int *FindComPorts(void);
extern "C" const char *GetComPortPath(int one_based_index);
void pdl_linux_set_ini_decrypt_key(const char *key);
const char *pdl_linux_get_ini_decrypt_key(void);
void pdl_linux_gui_sync_pagercast_toggle(void);

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
static char s_playback_device_desc[MAX_DEVICE_DESC_LEN + 1] = "Default";

#define TOOLBAR_HEIGHT  36
#define HEADER_HEIGHT   20
#define MAX_AUDIO_DEVICES_IN_LIST  64

typedef struct { GtkComboBoxText *combo; int count; } audio_combo_ctx_t;
typedef struct {
	const char *want;
	char *out;
	size_t outlen;
	int found;
} device_resolve_ctx_t;

/* Prefer Pulse description; never show raw alsa_*. paths in the UI. */
static void pretty_device_label(const char *name, const char *desc, char *out, size_t outlen)
{
	if (!out || outlen == 0) return;
	out[0] = '\0';
	if (desc && desc[0] && strncmp(desc, "alsa_", 5) != 0) {
		strncpy(out, desc, outlen - 1);
		out[outlen - 1] = '\0';
		/* Strip trailing " [STATE]" if still present from older builds. */
		char *br = strrchr(out, '[');
		if (br && br > out && br[-1] == ' ') {
			char *end = strchr(br, ']');
			if (end && end[1] == '\0')
				br[-1] = '\0';
		}
		return;
	}
	if (name && strcmp(name, "default") == 0) {
		strncpy(out, "Default", outlen - 1);
		out[outlen - 1] = '\0';
		return;
	}
	if (name && name[0]) {
		/* Last resort: short token from path, not the full id. */
		const char *p = strrchr(name, '.');
		const char *base = name;
		if (strstr(name, "usb-")) {
			const char *u = strstr(name, "usb-");
			base = u + 4;
		}
		strncpy(out, base, outlen - 1);
		out[outlen - 1] = '\0';
		for (char *c = out; *c; c++) {
			if (*c == '_' || *c == '-') *c = ' ';
		}
		(void)p;
		return;
	}
	strncpy(out, "Unknown", outlen - 1);
	out[outlen - 1] = '\0';
}

static void resolve_device_desc_cb(const char *name, const char *desc, void *ctx)
{
	device_resolve_ctx_t *r = (device_resolve_ctx_t *)ctx;
	if (!r || r->found || !name || !r->want) return;
	if (strcmp(name, r->want) != 0) return;
	pretty_device_label(name, desc, r->out, r->outlen);
	r->found = 1;
}

static void resolve_capture_desc(void)
{
	device_resolve_ctx_t r = { s_capture_device, s_capture_device_desc, sizeof(s_capture_device_desc), 0 };
	pdl_linux_alsa_enumerate_capture(resolve_device_desc_cb, &r);
	if (!r.found)
		pretty_device_label(s_capture_device, NULL, s_capture_device_desc, sizeof(s_capture_device_desc));
}

static void resolve_playback_desc(void)
{
	device_resolve_ctx_t r = { s_playback_device, s_playback_device_desc, sizeof(s_playback_device_desc), 0 };
	pdl_linux_alsa_enumerate_playback(resolve_device_desc_cb, &r);
	if (!r.found)
		pretty_device_label(s_playback_device, NULL, s_playback_device_desc, sizeof(s_playback_device_desc));
}

static GtkWidget *setup_frame(const char *title)
{
	GtkWidget *frame = gtk_frame_new(title);
	gtk_frame_set_label_align(GTK_FRAME(frame), 0.02f, 0.5f);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_start(box, 10);
	gtk_widget_set_margin_end(box, 10);
	gtk_widget_set_margin_top(box, 8);
	gtk_widget_set_margin_bottom(box, 8);
	gtk_container_add(GTK_CONTAINER(frame), box);
	return frame;
}

static GtkWidget *setup_frame_box(GtkWidget *frame)
{
	return gtk_bin_get_child(GTK_BIN(frame));
}

static GtkWidget *setup_form_row(GtkWidget *box, const char *label, GtkWidget *field)
{
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *lab = gtk_label_new(label);
	gtk_widget_set_halign(lab, GTK_ALIGN_START);
	gtk_widget_set_valign(lab, GTK_ALIGN_CENTER);
	gtk_widget_set_size_request(lab, 90, -1);
	gtk_box_pack_start(GTK_BOX(row), lab, FALSE, FALSE, 0);
	gtk_widget_set_hexpand(field, TRUE);
	gtk_widget_set_valign(field, GTK_ALIGN_CENTER);
	gtk_box_pack_start(GTK_BOX(row), field, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
	return field;
}

/* Mockup-1 style: symbolic icon above label (consistent mono look). */
static GtkWidget *toolbar_icon_button(const char *icon_name, const char *label, const char *tooltip)
{
	GtkWidget *btn = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
	gtk_style_context_add_class(gtk_widget_get_style_context(btn), "pdl-toolbtn");
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	/* Prefer *-symbolic so Adwaita/Papirus stay consistent (no mixed full-color icons). */
	char sym[96];
	snprintf(sym, sizeof(sym), "%s-symbolic", icon_name);
	GtkWidget *img = gtk_image_new_from_icon_name(sym, GTK_ICON_SIZE_LARGE_TOOLBAR);
	if (!gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), sym))
		img = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_LARGE_TOOLBAR);
	gtk_box_pack_start(GTK_BOX(box), img, FALSE, FALSE, 0);
	if (label && label[0]) {
		GtkWidget *lab = gtk_label_new(label);
		gtk_widget_set_halign(lab, GTK_ALIGN_CENTER);
		gtk_box_pack_start(GTK_BOX(box), lab, FALSE, FALSE, 0);
	}
	gtk_container_add(GTK_CONTAINER(btn), box);
	if (tooltip) gtk_widget_set_tooltip_text(btn, tooltip);
	return btn;
}

static GtkWidget *toolbar_stock_button(const char *icon_name, const char *label, const char *tooltip)
{
	return toolbar_icon_button(icon_name, label, tooltip);
}

/* Legacy BMP path kept for callers; prefers theme icon for a modern look. */
static GtkWidget *toolbar_button_from_gfx(const char *gfx_name, const char *label, const char *tooltip, const char *fallback_icon)
{
	(void)gfx_name;
	return toolbar_icon_button(fallback_icon, label, tooltip);
}

static void add_capture_device_cb(const char *name, const char *desc, void *ctx)
{
	audio_combo_ctx_t *c = (audio_combo_ctx_t *)ctx;
	if (c->count >= MAX_AUDIO_DEVICES_IN_LIST) return;
	c->count++;
	char label[MAX_DEVICE_DESC_LEN + 1];
	pretty_device_label(name, desc, label, sizeof(label));
	gtk_combo_box_text_append(c->combo, name, label);
}
static void add_playback_device_cb(const char *name, const char *desc, void *ctx)
{
	audio_combo_ctx_t *c = (audio_combo_ctx_t *)ctx;
	if (c->count >= MAX_AUDIO_DEVICES_IN_LIST) return;
	c->count++;
	char label[MAX_DEVICE_DESC_LEN + 1];
	pretty_device_label(name, desc, label, sizeof(label));
	gtk_combo_box_text_append(c->combo, name, label);
}

static void on_audio_response(GtkDialog *dialog, int response_id, gpointer user_data)
{
	(void)user_data;
	if (response_id == GTK_RESPONSE_ACCEPT) {
		GtkWidget *cap = (GtkWidget *)g_object_get_data(G_OBJECT(dialog), "pdl-cap-combo");
		GtkWidget *play = (GtkWidget *)g_object_get_data(G_OBJECT(dialog), "pdl-play-combo");
		GtkWidget *rate = (GtkWidget *)g_object_get_data(G_OBJECT(dialog), "pdl-rate-combo");
		int need_restart = 0;
		if (cap) {
			const gchar *cid = gtk_combo_box_get_active_id(GTK_COMBO_BOX(cap));
			const gchar *ctext = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(cap));
			if (cid) {
				if (strcmp(s_capture_device, cid) != 0) need_restart = 1;
				strncpy(s_capture_device, cid, MAX_DEVICE_LEN);
				s_capture_device[MAX_DEVICE_LEN] = '\0';
				if (ctext) {
					pretty_device_label(cid, ctext, s_capture_device_desc, sizeof(s_capture_device_desc));
					g_free((void *)ctext);
				} else
					resolve_capture_desc();
			}
		}
		if (play) {
			const gchar *pid = gtk_combo_box_get_active_id(GTK_COMBO_BOX(play));
			const gchar *ptext = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(play));
			if (pid) {
				strncpy(s_playback_device, pid, MAX_DEVICE_LEN);
				s_playback_device[MAX_DEVICE_LEN] = '\0';
				if (ptext) {
					pretty_device_label(pid, ptext, s_playback_device_desc, sizeof(s_playback_device_desc));
					g_free((void *)ptext);
				} else
					resolve_playback_desc();
			}
		}
		if (rate) {
			gint rsel = gtk_combo_box_get_active(GTK_COMBO_BOX(rate));
			static const int rates[] = { 22050, 44100, 48000 };
			if (rsel >= 0 && rsel < 3 && Profile.audioSampleRate != rates[rsel]) {
				Profile.audioSampleRate = rates[rsel];
				need_restart = 1;
			}
		}
		GtkWidget *inv = (GtkWidget *)g_object_get_data(G_OBJECT(dialog), "pdl-invert");
		if (inv) {
			int inv_v = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(inv)) ? 1 : 0;
			if (inv_v != Profile.invert) {
				Profile.invert = inv_v;
				need_restart = 1;
			}
		}
		GtkWidget *rs = (GtkWidget *)g_object_get_data(G_OBJECT(dialog), "pdl-rs232");
		GtkWidget *rs_dec = (GtkWidget *)g_object_get_data(G_OBJECT(dialog), "pdl-rs232-decode");
		GtkWidget *rs_4lvl = (GtkWidget *)g_object_get_data(G_OBJECT(dialog), "pdl-rs232-4lvl");
		GtkWidget *com = (GtkWidget *)g_object_get_data(G_OBJECT(dialog), "pdl-com-combo");
		GtkWidget *baud = (GtkWidget *)g_object_get_data(G_OBJECT(dialog), "pdl-baud-combo");
		if (rs)
			Profile.comPortEnabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rs)) ? 1 : 0;
		if (rs_dec) {
			int want = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rs_dec)) ? 1 : 0;
			Profile.comPortRS232 = want ? (Profile.comPortRS232 > 0 ? Profile.comPortRS232 : 2) : 0;
		}
		if (rs_4lvl)
			Profile.fourlevel = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rs_4lvl)) ? 1 : 0;
		if (com) {
			const gchar *cid = gtk_combo_box_get_active_id(GTK_COMBO_BOX(com));
			Profile.comPort = cid ? atoi(cid) : 0;
		}
		if (baud) {
			gint bi = gtk_combo_box_get_active(GTK_COMBO_BOX(baud));
			static const int bauds[] = { 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 };
			if (bi >= 0 && bi < 8) Profile.comRS232bitrate = bauds[bi];
		}
		CloseComPort();
		pdl_linux_hw_decode_apply_settings();
		if (Profile.comPortEnabled && Profile.comPortRS232 <= 0)
			OpenComPort();
		WriteSettings();
		if (need_restart && Profile.comPortRS232 <= 0) {
			SetAudioConfig(Profile.audioConfig > 0 ? Profile.audioConfig : 1);
			pdl_linux_restart_capture();
		}
	}
	gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void log_ui(const char *action, const char *detail);
static void set_bar_pct(GtkWidget *bar, GtkWidget *lbl, double pct, const char *text);

typedef struct {
	GtkWidget *messages;
	GtkWidget *groupcalls;
	GtkWidget *rejected;
	GtkWidget *blocked;
	GtkWidget *missed;
	GtkWidget *errors;
	GtkWidget *clean;
	GtkWidget *corrupt;
	GtkWidget *rate_bar;
	GtkWidget *rate_lbl;
	GtkWidget *rssi_bar;
	GtkWidget *rssi_lbl;
	GtkWidget *sn_bar;
	GtkWidget *sn_lbl;
	GtkWidget *quality_bar;
	GtkWidget *quality_lbl;
	GtkWidget *ber_bar;
	GtkWidget *ber_lbl;
	GtkWidget *bch;
	GtkWidget *filters;
	GtkWidget *filter_hits;
	guint timer_id;
} StatsDialogUi;

static GtkWidget *stats_kv_row(GtkWidget *box, const char *label, GtkWidget **val_out)
{
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *lab = gtk_label_new(label);
	gtk_widget_set_halign(lab, GTK_ALIGN_START);
	gtk_widget_set_valign(lab, GTK_ALIGN_CENTER);
	gtk_widget_set_size_request(lab, 130, -1);
	GtkWidget *val = gtk_label_new("—");
	gtk_widget_set_halign(val, GTK_ALIGN_END);
	gtk_widget_set_hexpand(val, TRUE);
	gtk_label_set_selectable(GTK_LABEL(val), TRUE);
	gtk_box_pack_start(GTK_BOX(row), lab, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), val, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
	*val_out = val;
	return val;
}

static GtkWidget *stats_meter_row(GtkWidget *box, const char *name, GtkWidget **bar_out, GtkWidget **lbl_out)
{
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
	GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *lab = gtk_label_new(name);
	gtk_widget_set_halign(lab, GTK_ALIGN_START);
	gtk_widget_set_hexpand(lab, TRUE);
	gtk_style_context_add_class(gtk_widget_get_style_context(lab), "pdl-meter-label");
	GtkWidget *val = gtk_label_new("—");
	gtk_widget_set_halign(val, GTK_ALIGN_END);
	gtk_style_context_add_class(gtk_widget_get_style_context(val), "pdl-meter-val");
	gtk_box_pack_start(GTK_BOX(top), lab, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(top), val, FALSE, FALSE, 0);
	GtkWidget *bar = gtk_progress_bar_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(bar), "pdl-meter-bar");
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar), 0.0);
	gtk_widget_set_hexpand(bar, TRUE);
	gtk_box_pack_start(GTK_BOX(row), top, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), bar, FALSE, FALSE, 0);
	gtk_widget_set_margin_bottom(row, 4);
	gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
	*bar_out = bar;
	*lbl_out = val;
	return row;
}

static void stats_set_int(GtkWidget *lbl, int v)
{
	if (!lbl) return;
	char buf[32];
	snprintf(buf, sizeof(buf), "%d", v);
	gtk_label_set_text(GTK_LABEL(lbl), buf);
}

static void stats_dialog_refresh(StatsDialogUi *ui)
{
	if (!ui) return;
	extern int nCount_Messages, nCount_Groupcalls, nCount_Rejected, nCount_Blocked;
	extern int nCount_Missed[2];
	extern int nCount_CleanRx, nCount_CorruptRx;

	stats_set_int(ui->messages, nCount_Messages);
	stats_set_int(ui->groupcalls, nCount_Groupcalls);
	stats_set_int(ui->rejected, nCount_Rejected);
	stats_set_int(ui->blocked, nCount_Blocked);
	stats_set_int(ui->missed, nCount_Missed[0] + nCount_Missed[1]);
	stats_set_int(ui->errors, nCount_Rejected + iRX_LastBchErrors);
	stats_set_int(ui->clean, nCount_CleanRx);
	stats_set_int(ui->corrupt, nCount_CorruptRx);
	stats_set_int(ui->filters, (int)Profile.filters.size());

	unsigned long hits = 0;
	for (size_t i = 0; i < Profile.filters.size(); i++)
		hits += Profile.filters[i].hitcounter;
	stats_set_int(ui->filter_hits, (int)hits);

	int judged = nCount_CleanRx + nCount_CorruptRx;
	double rate = (judged > 0) ? (100.0 * nCount_CleanRx / (double)judged) : 0.0;
	char buf[64];
	snprintf(buf, sizeof(buf), "%.1f%%", rate);
	set_bar_pct(ui->rate_bar, ui->rate_lbl, rate, buf);

	double level = s_meter_display_level;
	snprintf(buf, sizeof(buf), "%.0f dBm", -90.0 + level * 0.4);
	set_bar_pct(ui->rssi_bar, ui->rssi_lbl, level, buf);

	double sn = level * 0.35;
	snprintf(buf, sizeof(buf), "%.0f dB", sn);
	set_bar_pct(ui->sn_bar, ui->sn_lbl, (sn / 40.0) * 100.0, buf);

	double q = (bRX_MessageQuality_Valid && dRX_MessageQuality >= 0.0) ? dRX_MessageQuality
		: (dRX_Quality >= 0.0 ? dRX_Quality : 0.0);
	if (q < 0.0) q = 0.0;
	if (q > 100.0) q = 100.0;
	snprintf(buf, sizeof(buf), "%.1f%%", q);
	set_bar_pct(ui->quality_bar, ui->quality_lbl, q, buf);

	double ber = 0.0;
	if (iRX_LastBchCodewords > 0)
		ber = (100.0 * iRX_LastBchErrors) / (double)iRX_LastBchCodewords;
	snprintf(buf, sizeof(buf), "%.1f%%", ber);
	set_bar_pct(ui->ber_bar, ui->ber_lbl, ber > 100.0 ? 100.0 : ber, buf);

	snprintf(buf, sizeof(buf), "%d / %d", iRX_LastBchErrors, iRX_LastBchCodewords);
	if (ui->bch) gtk_label_set_text(GTK_LABEL(ui->bch), buf);
}

static gboolean stats_dialog_tick(gpointer data)
{
	StatsDialogUi *ui = (StatsDialogUi *)data;
	if (!ui || s_quit) return G_SOURCE_REMOVE;
	stats_dialog_refresh(ui);
	return G_SOURCE_CONTINUE;
}

static void stats_dialog_reset_counters(void)
{
	extern int nCount_Messages, nCount_Groupcalls, nCount_Rejected, nCount_Blocked;
	extern int nCount_CleanRx, nCount_CorruptRx;
	extern int nCount_Missed[2];
	nCount_Messages = 0;
	nCount_Groupcalls = 0;
	nCount_Rejected = 0;
	nCount_Blocked = 0;
	nCount_CleanRx = 0;
	nCount_CorruptRx = 0;
	nCount_Missed[0] = 0;
	nCount_Missed[1] = 0;
	WriteSettings();
}

static void on_stats_clicked(void)
{
	log_ui("Statistics", "dialog opened");
	GtkWidget *dlg = gtk_dialog_new_with_buttons("Statistics", dialog_parent(),
		(GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
		"Reset", 1,
		"Close", GTK_RESPONSE_CLOSE, NULL);
	gtk_window_set_default_size(GTK_WINDOW(dlg), 440, 620);
	pdl_linux_gui_prepare_dialog(dlg);

	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
	gtk_container_set_border_width(GTK_CONTAINER(content), 8);
	GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand(scroller, TRUE);
	gtk_box_pack_start(GTK_BOX(content), scroller, TRUE, TRUE, 0);
	GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_container_add(GTK_CONTAINER(scroller), outer);
	gtk_widget_set_margin_end(outer, 4);

	StatsDialogUi *ui = g_new0(StatsDialogUi, 1);

	{
		GtkWidget *frame = setup_frame("Signal");
		GtkWidget *box = setup_frame_box(frame);
		stats_meter_row(box, "RSSI", &ui->rssi_bar, &ui->rssi_lbl);
		stats_meter_row(box, "S/N", &ui->sn_bar, &ui->sn_lbl);
		stats_meter_row(box, "Quality", &ui->quality_bar, &ui->quality_lbl);
		stats_meter_row(box, "BER", &ui->ber_bar, &ui->ber_lbl);
		stats_kv_row(box, "Last BCH (err/cw):", &ui->bch);
		gtk_box_pack_start(GTK_BOX(outer), frame, FALSE, FALSE, 0);
	}
	{
		GtkWidget *frame = setup_frame("Decode");
		GtkWidget *box = setup_frame_box(frame);
		stats_kv_row(box, "Messages:", &ui->messages);
		stats_kv_row(box, "Groupcalls:", &ui->groupcalls);
		stats_kv_row(box, "Rejected:", &ui->rejected);
		stats_kv_row(box, "Blocked:", &ui->blocked);
		stats_kv_row(box, "Missed:", &ui->missed);
		stats_kv_row(box, "Errors:", &ui->errors);
		stats_kv_row(box, "Clean RX:", &ui->clean);
		stats_kv_row(box, "Corrupt RX:", &ui->corrupt);
		stats_meter_row(box, "Rate", &ui->rate_bar, &ui->rate_lbl);
		gtk_box_pack_start(GTK_BOX(outer), frame, FALSE, FALSE, 0);
	}
	{
		GtkWidget *frame = setup_frame("Filters");
		GtkWidget *box = setup_frame_box(frame);
		stats_kv_row(box, "Active filters:", &ui->filters);
		stats_kv_row(box, "Total hits:", &ui->filter_hits);
		gtk_box_pack_start(GTK_BOX(outer), frame, FALSE, FALSE, 0);
	}

	stats_dialog_refresh(ui);
	ui->timer_id = g_timeout_add(250, stats_dialog_tick, ui);

	gtk_widget_show_all(dlg);
	for (;;) {
		gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
		if (resp == 1) {
			stats_dialog_reset_counters();
			stats_dialog_refresh(ui);
			continue;
		}
		break;
	}
	if (ui->timer_id)
		g_source_remove(ui->timer_id);
	g_free(ui);
	gtk_widget_destroy(dlg);
}

static void on_audio_clicked(GtkWidget *btn, gpointer data)
{
	log_ui("Audio / Setup", "dialog opened");
	(void)btn;
	(void)data;
	GtkWidget *dialog = gtk_dialog_new_with_buttons("Audio Setup", dialog_parent(),
		(GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
		"Cancel", GTK_RESPONSE_REJECT, "OK", GTK_RESPONSE_ACCEPT, NULL);
	gtk_window_set_default_size(GTK_WINDOW(dialog), 520, 360);
	pdl_linux_gui_prepare_dialog(dialog);
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	gtk_container_set_border_width(GTK_CONTAINER(content), 8);
	GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_box_pack_start(GTK_BOX(content), outer, TRUE, TRUE, 0);

	/* --- Audio --- */
	{
		GtkWidget *frame = setup_frame("Audio");
		GtkWidget *box = setup_frame_box(frame);

		GtkWidget *cap_combo = gtk_combo_box_text_new();
		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(cap_combo), "default", "Default");
		audio_combo_ctx_t cap_ctx = { GTK_COMBO_BOX_TEXT(cap_combo), 0 };
		pdl_linux_alsa_enumerate_capture(add_capture_device_cb, &cap_ctx);
		if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(cap_combo), s_capture_device))
			gtk_combo_box_set_active(GTK_COMBO_BOX(cap_combo), 0);
		setup_form_row(box, "Input:", cap_combo);
		g_object_set_data(G_OBJECT(dialog), "pdl-cap-combo", cap_combo);

		GtkWidget *play_combo = gtk_combo_box_text_new();
		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(play_combo), "default", "Default");
		audio_combo_ctx_t play_ctx = { GTK_COMBO_BOX_TEXT(play_combo), 0 };
		pdl_linux_alsa_enumerate_playback(add_playback_device_cb, &play_ctx);
		if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(play_combo), s_playback_device))
			gtk_combo_box_set_active(GTK_COMBO_BOX(play_combo), 0);
		setup_form_row(box, "Playback:", play_combo);
		g_object_set_data(G_OBJECT(dialog), "pdl-play-combo", play_combo);

		GtkWidget *rate_combo = gtk_combo_box_text_new();
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(rate_combo), "22050");
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(rate_combo), "44100");
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(rate_combo), "48000");
		{
			int ri = 1;
			if (Profile.audioSampleRate == 22050) ri = 0;
			else if (Profile.audioSampleRate == 48000) ri = 2;
			gtk_combo_box_set_active(GTK_COMBO_BOX(rate_combo), ri);
		}
		setup_form_row(box, "Sample rate:", rate_combo);
		g_object_set_data(G_OBJECT(dialog), "pdl-rate-combo", rate_combo);

		GtkWidget *ch_invert = gtk_check_button_new_with_label("Invert polarity");
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_invert), Profile.invert ? TRUE : FALSE);
		gtk_box_pack_start(GTK_BOX(box), ch_invert, FALSE, FALSE, 0);
		g_object_set_data(G_OBJECT(dialog), "pdl-invert", ch_invert);

		gtk_box_pack_start(GTK_BOX(outer), frame, FALSE, FALSE, 0);
	}

	/* --- RS232 --- */
	{
		GtkWidget *frame = setup_frame("RS232");
		GtkWidget *box = setup_frame_box(frame);

		GtkWidget *ch_rs_dec = gtk_check_button_new_with_label("Enable RS232 decode input (bitstream converter)");
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_rs_dec), Profile.comPortRS232 > 0 ? TRUE : FALSE);
		gtk_widget_set_tooltip_text(ch_rs_dec,
			"Hardware FSK→serial converter → live decode (pauses soundcard / PagerCast)");
		gtk_box_pack_start(GTK_BOX(box), ch_rs_dec, FALSE, FALSE, 0);
		g_object_set_data(G_OBJECT(dialog), "pdl-rs232-decode", ch_rs_dec);

		GtkWidget *ch_4lvl = gtk_check_button_new_with_label("4-level FSK interface");
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_4lvl), Profile.fourlevel ? TRUE : FALSE);
		gtk_box_pack_start(GTK_BOX(box), ch_4lvl, FALSE, FALSE, 0);
		g_object_set_data(G_OBJECT(dialog), "pdl-rs232-4lvl", ch_4lvl);

		GtkWidget *ch_rs232 = gtk_check_button_new_with_label("Enable RS232 message output");
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_rs232), Profile.comPortEnabled ? TRUE : FALSE);
		gtk_widget_set_tooltip_text(ch_rs232, "Write decoded lines to the serial port (separate from decode input)");
		gtk_box_pack_start(GTK_BOX(box), ch_rs232, FALSE, FALSE, 0);
		g_object_set_data(G_OBJECT(dialog), "pdl-rs232", ch_rs232);

		GtkWidget *com_combo = gtk_combo_box_text_new();
		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(com_combo), "0", "(none)");
		FindComPorts();
		for (int i = 1; ; i++) {
			const char *path = GetComPortPath(i);
			if (!path) break;
			char id[8];
			snprintf(id, sizeof(id), "%d", i);
			const char *base = strrchr(path, '/');
			gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(com_combo), id, base ? base + 1 : path);
		}
		{
			char id[8];
			snprintf(id, sizeof(id), "%d", Profile.comPort > 0 ? Profile.comPort : 0);
			if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(com_combo), id))
				gtk_combo_box_set_active(GTK_COMBO_BOX(com_combo), 0);
		}
		setup_form_row(box, "Port:", com_combo);
		g_object_set_data(G_OBJECT(dialog), "pdl-com-combo", com_combo);

		GtkWidget *baud_combo = gtk_combo_box_text_new();
		static const int bauds[] = { 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 };
		int baud_sel = 4; /* 19200 typical for converters */
		for (int i = 0; i < 8; i++) {
			char b[16];
			snprintf(b, sizeof(b), "%d", bauds[i]);
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(baud_combo), b);
			if (Profile.comRS232bitrate == bauds[i]) baud_sel = i;
		}
		gtk_combo_box_set_active(GTK_COMBO_BOX(baud_combo), baud_sel);
		setup_form_row(box, "Baud:", baud_combo);
		g_object_set_data(G_OBJECT(dialog), "pdl-baud-combo", baud_combo);

		gtk_box_pack_start(GTK_BOX(outer), frame, FALSE, FALSE, 0);
	}

	g_signal_connect(dialog, "response", G_CALLBACK(on_audio_response), NULL);
	gtk_widget_show_all(dialog);
}

const char *pdl_linux_gui_get_capture_device(void)
{
	return s_capture_device;
}
const char *pdl_linux_gui_get_playback_device(void)
{
	return s_playback_device;
}
void pdl_linux_gui_set_capture_device(const char *name)
{
	if (!name || !name[0]) return;
	strncpy(s_capture_device, name, MAX_DEVICE_LEN);
	s_capture_device[MAX_DEVICE_LEN] = '\0';
	resolve_capture_desc();
}
void pdl_linux_gui_set_playback_device(const char *name)
{
	if (!name || !name[0]) return;
	strncpy(s_playback_device, name, MAX_DEVICE_LEN);
	s_playback_device[MAX_DEVICE_LEN] = '\0';
	resolve_playback_desc();
}

static void log_ui(const char *action, const char *detail)
{
	pdl_platform_log_ui(action, detail);
}

static void clear_pane1_cb(gpointer unused) {
	(void)unused;
	log_ui("Clear messages", NULL);
	ClearPanes(true, true);
	s_last_pane1_bottom = (unsigned)-1;
	s_last_pane2_bottom = (unsigned)-1;
}
static void clear_pane2_cb(gpointer unused) {
	/* Kept for call sites; single-pane UI clears everything. */
	clear_pane1_cb(unused);
}
static void on_options_clicked(void);
static void on_filters_clicked(void);
static void set_monitor_mode(int which);
static void copy_pane_to_clipboard(int which);
static void tb_filters_cb(GtkWidget *w, gpointer d);

void pdl_linux_gui_update_title(void)
{
	if (!s_win) return;
	char title[512];
	snprintf(title, sizeof(title), "PDL");
	if (szWindowText[2][0]) {
		strncat(title, " — ", sizeof(title) - strlen(title) - 1);
		strncat(title, szWindowText[2], sizeof(title) - strlen(title) - 1);
	}
	if (Profile.show_cfs && szWindowText[3][0]) {
		strncat(title, "  ", sizeof(title) - strlen(title) - 1);
		strncat(title, szWindowText[3], sizeof(title) - strlen(title) - 1);
	}
	if (Profile.show_rejectblocked && szWindowText[5][0]) {
		strncat(title, "  [", sizeof(title) - strlen(title) - 1);
		strncat(title, szWindowText[5], sizeof(title) - strlen(title) - 1);
		strncat(title, "]", sizeof(title) - strlen(title) - 1);
	}
	gtk_window_set_title(GTK_WINDOW(s_win), title);
}

static void set_monitor_mode(int which)
{
	/* Only POCSAG paging is supported on Linux for now. */
	if (which != 0) {
		log_ui("Monitor mode", "unavailable");
		which = 0;
	}
	int cur = Profile.monitor_acars ? 1 : Profile.monitor_mobitex ? 2 : Profile.monitor_ermes ? 3 : 0;
	if (cur == which) return;
	Profile.monitor_paging = TRUE;
	Profile.monitor_acars = FALSE;
	Profile.monitor_mobitex = FALSE;
	Profile.monitor_ermes = FALSE;
	pd_reset_all();
	Reset_ATB();
	WriteSettings();
	pdl_linux_gui_update_title();
	log_ui("Monitor mode", "POCSAG");
}

static void sync_monitor_menu_checks(void)
{
	int which = 0;
	if (Profile.monitor_acars) which = 1;
	else if (Profile.monitor_mobitex) which = 2;
	else if (Profile.monitor_ermes) which = 3;
	for (int i = 0; i < 4; i++) {
		if (s_mi_monitor[i])
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s_mi_monitor[i]), i == which);
	}
}

static void on_clear_screen_dialog(void)
{
	GtkWidget *dlg = gtk_dialog_new_with_buttons("Clear Screen", dialog_parent(),
		(GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
		"Cancel", GTK_RESPONSE_CANCEL,
		"Clear", GTK_RESPONSE_ACCEPT, NULL);
	pdl_linux_gui_prepare_dialog(dlg);
	GtkWidget *lbl = gtk_label_new("Clear all messages?");
	gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), lbl);
	gtk_widget_show_all(dlg);
	gint r = gtk_dialog_run(GTK_DIALOG(dlg));
	gtk_widget_destroy(dlg);
	if (r == GTK_RESPONSE_ACCEPT)
		clear_pane1_cb(NULL);
}

static void copy_pane_to_clipboard(int which)
{
	GtkTextView *view = NULL;
	if (which == 0 && s_pane1_text) view = GTK_TEXT_VIEW(s_pane1_text);
	else if (which == 1 && s_pane2_text) view = GTK_TEXT_VIEW(s_pane2_text);
	else return;
	GtkTextBuffer *buf = gtk_text_view_get_buffer(view);
	GtkTextIter start, end;
	if (gtk_text_buffer_get_selection_bounds(buf, &start, &end)) {
		gchar *t = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
		gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), t, -1);
		g_free(t);
	} else {
		gtk_text_buffer_get_bounds(buf, &start, &end);
		gchar *t = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
		gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), t, -1);
		g_free(t);
	}
}

static void on_menu_activate(GtkMenuItem *item, gpointer data)
{
	(void)item;
	int id = GPOINTER_TO_INT(data);
	switch (id) {
		case IDM_EXIT:
			log_ui("Menu Exit", NULL);
			WriteSettings();
			gtk_widget_destroy(s_win);
			return;
		case IDM_INTERFACE:
			log_ui("Menu Setup", NULL);
			on_audio_clicked(NULL, NULL);
			return;
		case IDM_VOLUME:
			log_ui("Menu Volume/Audio", NULL);
			on_audio_clicked(NULL, NULL);
			return;
		case IDM_CLEARDISPLAY:
			log_ui("Menu Clear Screen", NULL);
			on_clear_screen_dialog();
			return;
		case IDM_OPTIONS:
			log_ui("Menu Options", "opening Options dialog");
			on_options_clicked();
			return;
		case IDM_GENERAL:
			log_ui("Menu General", NULL);
			pdl_linux_general_dialog(dialog_parent());
			if (s_mi_tray)
				gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s_mi_tray), Profile.SystemTray ? TRUE : FALSE);
			return;
		case IDM_MAIL:
			log_ui("Menu Mail", NULL);
			pdl_linux_mail_dialog(dialog_parent());
			return;
		case IDM_COLOR:
			log_ui("Menu Colors", NULL);
			pdl_linux_colors_dialog(dialog_parent());
			return;
		case IDM_FONT:
		case IDM_SCREENOPTIONS:
		case IDM_SCROLLBACK:
			log_ui("Menu Display", NULL);
			pdl_linux_display_dialog(dialog_parent());
			return;
		case IDM_FILTERS:
		case IDM_FILTEROPTIONS:
			log_ui("Menu Filters", NULL);
			on_filters_clicked();
			return;
		case IDM_RELOAD:
			log_ui("Menu Reload Filters", NULL);
			ReadFilters(szFilterPathName, &Profile, false);
			return;
		case IDM_RESET_HITCOUNTERS:
			log_ui("Menu Reset Hitcounters", NULL);
			for (size_t i = 0; i < Profile.filters.size(); i++)
				Profile.filters[i].hitcounter = 0;
			WriteFilters(&Profile, 0);
			return;
		case IDM_POCSAGFLEX:
			set_monitor_mode(0);
			return;
		case IDM_ACARS:
		case IDM_MOBITEX:
		case IDM_ERMES:
			return;
		case IDM_COPY_SELECTION:
			copy_pane_to_clipboard(0);
			return;
		case IDM_COPY_UPPER:
			copy_pane_to_clipboard(0);
			return;
		case IDM_COPY_LOWER:
			copy_pane_to_clipboard(1);
			return;
		case IDM_MONSTAT:
			on_stats_clicked();
			return;
		case IDM_SYSTEMTRAY:
			if (s_mi_tray)
				Profile.SystemTray = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(s_mi_tray)) ? 1 : 0;
			else
				Profile.SystemTray = !Profile.SystemTray;
			if (Profile.SystemTray)
				SystemTrayWindow(false);
			else
				SystemTrayIcon(true);
			WriteSettings();
			return;
		case IDM_LOGFILE: {
			OPENFILENAME ofn;
			char file[MAX_PATH] = {0};
			memset(&ofn, 0, sizeof(ofn));
			ofn.lStructSize = sizeof(ofn);
			ofn.lpstrFile = file;
			ofn.nMaxFile = sizeof(file);
			ofn.lpstrTitle = "Log file";
			ofn.lpstrInitialDir = szLogPathName;
			if (GetOpenFileName(&ofn)) {
				strncpy(Profile.logfile, file, sizeof(Profile.logfile) - 1);
				Profile.logfile_enabled = 1;
				pdl_platform_set_log_file(Profile.logfile);
				WriteSettings();
			}
			return;
		}
		case IDM_ABOUT: {
			log_ui("Menu About", NULL);
			{
				GtkWidget *dlg = gtk_dialog_new_with_buttons("About PDL", dialog_parent(),
					(GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
					"OK", GTK_RESPONSE_ACCEPT, NULL);
				gtk_window_set_default_size(GTK_WINDOW(dlg), 420, 220);
				pdl_linux_gui_prepare_dialog(dlg);

				GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
				gtk_container_set_border_width(GTK_CONTAINER(content), 12);

				GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
				gtk_box_pack_start(GTK_BOX(content), row, TRUE, TRUE, 0);

				{
					char icon_path[PATH_MAX];
					GtkWidget *img = NULL;
					if (find_app_icon_path(icon_path, sizeof(icon_path)) == 0) {
						GError *err = NULL;
						GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_size(icon_path, 64, 64, &err);
						if (pb) {
							img = gtk_image_new_from_pixbuf(pb);
							g_object_unref(pb);
						} else if (err) {
							g_error_free(err);
						}
					}
					if (!img)
						img = gtk_image_new_from_icon_name("pdl", GTK_ICON_SIZE_DIALOG);
					gtk_widget_set_valign(img, GTK_ALIGN_START);
					gtk_box_pack_start(GTK_BOX(row), img, FALSE, FALSE, 0);
				}

				GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
				gtk_box_pack_start(GTK_BOX(row), col, TRUE, TRUE, 0);

				GtkWidget *name = gtk_label_new(NULL);
				gtk_label_set_markup(GTK_LABEL(name), "<span size='large'><b>PDL</b></span>");
				gtk_widget_set_halign(name, GTK_ALIGN_START);
				gtk_box_pack_start(GTK_BOX(col), name, FALSE, FALSE, 0);

				char line[160];
				snprintf(line, sizeof(line), "Version %s", PDL_VERSION);
				GtkWidget *ver = gtk_label_new(line);
				gtk_widget_set_halign(ver, GTK_ALIGN_START);
				gtk_box_pack_start(GTK_BOX(col), ver, FALSE, FALSE, 0);

				GtkWidget *desc = gtk_label_new("Pager Data Linux — native pager decoder.");
				gtk_widget_set_halign(desc, GTK_ALIGN_START);
				gtk_label_set_line_wrap(GTK_LABEL(desc), TRUE);
				gtk_box_pack_start(GTK_BOX(col), desc, FALSE, FALSE, 0);

				snprintf(line, sizeof(line), "Built %s %s", PDL_BUILD_DATE, PDL_BUILD_TIME);
				GtkWidget *built = gtk_label_new(line);
				gtk_widget_set_halign(built, GTK_ALIGN_START);
				gtk_box_pack_start(GTK_BOX(col), built, FALSE, FALSE, 0);

				pdl_run_styled_dialog(dlg);
			}
			return;
		}
		default:
			return;
	}
}

/* Helpers for tabbed options layout (aligned rows inside each tab). */
static gboolean on_options_change_current_page(GtkNotebook *nb, gint page_num, gpointer data)
{
	(void)data;
	GtkWidget *page = gtk_notebook_get_nth_page(nb, page_num);
	if (page && g_object_get_data(G_OBJECT(page), "pdl-tab-disabled"))
		return TRUE; /* block switch to unavailable tab */
	return FALSE;
}

static GtkWidget *options_tab_page(GtkNotebook *nb, const char *label, gboolean scrollable, gboolean available)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_start(box, 12);
	gtk_widget_set_margin_end(box, 12);
	gtk_widget_set_margin_top(box, 12);
	gtk_widget_set_margin_bottom(box, 12);

	GtkWidget *page = box;
	if (scrollable) {
		GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
		gtk_style_context_add_class(gtk_widget_get_style_context(sw), "pdl-tab-scroll");
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
			GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
		/* No inset frame — theme shadow reads as the weird pink/gray border. */
		gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_NONE);
		gtk_container_add(GTK_CONTAINER(sw), box);
		page = sw;
	}
	GtkWidget *tab_lab = gtk_label_new(label);
	gtk_notebook_append_page(nb, page, tab_lab);
	if (!available) {
		g_object_set_data(G_OBJECT(page), "pdl-tab-disabled", GINT_TO_POINTER(1));
		gtk_widget_set_sensitive(tab_lab, FALSE);
		gtk_widget_set_sensitive(page, FALSE);
		gtk_widget_set_sensitive(box, FALSE);
	}
	return box;
}

static GtkWidget *options_check(GtkWidget *box, const char *label, gboolean active)
{
	GtkWidget *ch = gtk_check_button_new_with_label(label);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch), active);
	gtk_widget_set_halign(ch, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(box), ch, FALSE, FALSE, 0);
	return ch;
}

static GtkWidget *options_form_row(GtkWidget *box, const char *label, GtkWidget *field)
{
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *lab = gtk_label_new(label);
	gtk_widget_set_halign(lab, GTK_ALIGN_END);
	gtk_widget_set_size_request(lab, 140, -1);
	gtk_box_pack_start(GTK_BOX(row), lab, FALSE, FALSE, 0);
	gtk_widget_set_hexpand(field, TRUE);
	gtk_widget_set_halign(field, GTK_ALIGN_FILL);
	gtk_box_pack_start(GTK_BOX(row), field, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
	return field;
}

typedef struct {
	GtkWidget *enable;
	GtkWidget *combo;
	GtkWidget *custom;
	GtkWidget *freq;
	GtkWidget *key;
	GtkWidget *status;
	GtkWidget *led;
} PagerCastUi;

static void pc_state_rgb(PdlPagerCastState st, double *r, double *g, double *b)
{
	switch (st) {
	case PDL_PC_STREAMING:  *r = 0.20; *g = 0.82; *b = 0.28; break; /* green */
	case PDL_PC_CONNECTING: *r = 0.95; *g = 0.62; *b = 0.10; break; /* orange */
	case PDL_PC_ERROR:      *r = 0.90; *g = 0.18; *b = 0.18; break; /* red */
	default:                *r = 0.55; *g = 0.12; *b = 0.12; break; /* dark red / offline */
	}
}

static gboolean on_pc_led_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
	(void)data;
	int w = gtk_widget_get_allocated_width(widget);
	int h = gtk_widget_get_allocated_height(widget);
	double cx = w * 0.5, cy = h * 0.5;
	double rad = (w < h ? w : h) * 0.38;
	double r, g, b;
	pc_state_rgb(pdl_pagercast_state(), &r, &g, &b);

	/* soft glow */
	cairo_set_source_rgba(cr, r, g, b, 0.35);
	cairo_arc(cr, cx, cy, rad + 2.5, 0, 2 * M_PI);
	cairo_fill(cr);

	cairo_set_source_rgb(cr, r, g, b);
	cairo_arc(cr, cx, cy, rad, 0, 2 * M_PI);
	cairo_fill(cr);

	/* highlight */
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.35);
	cairo_arc(cr, cx - rad * 0.25, cy - rad * 0.25, rad * 0.35, 0, 2 * M_PI);
	cairo_fill(cr);

	/* rim */
	cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.45);
	cairo_set_line_width(cr, 1.0);
	cairo_arc(cr, cx, cy, rad, 0, 2 * M_PI);
	cairo_stroke(cr);
	return FALSE;
}

static void pc_update_status_widgets(GtkWidget *led, GtkWidget *label)
{
	if (label && GTK_IS_LABEL(label)) {
		const char *st = pdl_pagercast_state_label();
		const char *detail = pdl_pagercast_status();
		char buf[320];
		if (!detail || !detail[0] || strcmp(st, detail) == 0)
			snprintf(buf, sizeof(buf), "%s", st);
		else if (strncmp(detail, st, strlen(st)) == 0)
			snprintf(buf, sizeof(buf), "%s", detail);
		else
			snprintf(buf, sizeof(buf), "%s — %s", st, detail);
		gtk_label_set_text(GTK_LABEL(label), buf);
	}
	if (led && GTK_IS_WIDGET(led))
		gtk_widget_queue_draw(led);
}

static gboolean on_pc_status_tick(gpointer data)
{
	PagerCastUi *u = (PagerCastUi *)data;
	if (!u || !u->status || !GTK_IS_LABEL(u->status))
		return G_SOURCE_REMOVE;
	pc_update_status_widgets(u->led, u->status);
	return G_SOURCE_CONTINUE;
}

static void options_apply_pagercast_fields(PagerCastUi *u)
{
	if (!u) return;
	Profile.pagercast_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(u->enable)) ? 1 : 0;
	const char *custom = gtk_entry_get_text(GTK_ENTRY(u->custom));
	if (custom && custom[0]) {
		strncpy(Profile.pagercast_api_base, custom, sizeof(Profile.pagercast_api_base) - 1);
	} else {
		const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(u->combo));
		if (id)
			strncpy(Profile.pagercast_api_base, id, sizeof(Profile.pagercast_api_base) - 1);
	}
	Profile.pagercast_api_base[sizeof(Profile.pagercast_api_base) - 1] = '\0';
	strncpy(Profile.pagercast_frequency, gtk_entry_get_text(GTK_ENTRY(u->freq)),
		sizeof(Profile.pagercast_frequency) - 1);
	Profile.pagercast_frequency[sizeof(Profile.pagercast_frequency) - 1] = '\0';
	strncpy(Profile.pagercast_api_key, gtk_entry_get_text(GTK_ENTRY(u->key)),
		sizeof(Profile.pagercast_api_key) - 1);
	Profile.pagercast_api_key[sizeof(Profile.pagercast_api_key) - 1] = '\0';
}

static void on_pc_refresh_clicked(GtkButton *btn, gpointer data)
{
	(void)btn;
	PagerCastUi *u = (PagerCastUi *)data;
	options_apply_pagercast_fields(u);
	gtk_label_set_text(GTK_LABEL(u->status), "Refreshing nodes…");
	if (u->led) gtk_widget_queue_draw(u->led);
	while (gtk_events_pending()) gtk_main_iteration();
	pdl_pagercast_refresh_nodes();
	gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(u->combo));
	int active = 0;
	for (int i = 0; i < pdl_pagercast_node_count(); i++) {
		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(u->combo),
			pdl_pagercast_node_url(i), pdl_pagercast_node_name(i));
		if (Profile.pagercast_api_base[0] &&
			strcmp(Profile.pagercast_api_base, pdl_pagercast_node_url(i)) == 0)
			active = i;
	}
	if (pdl_pagercast_node_count() > 0)
		gtk_combo_box_set_active(GTK_COMBO_BOX(u->combo), active);
	pc_update_status_widgets(u->led, u->status);
}

static void on_pc_connect_clicked(GtkButton *btn, gpointer data)
{
	(void)btn;
	PagerCastUi *u = (PagerCastUi *)data;
	options_apply_pagercast_fields(u);
	if (!Profile.pagercast_enabled) {
		Profile.pagercast_enabled = 1;
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->enable), TRUE);
	}
	WriteSettings();
	pdl_pagercast_connect();
	pc_update_status_widgets(u->led, u->status);
	pdl_apply_message_column_layout();
	pdl_linux_gui_sync_pagercast_toggle();
}

static void on_pc_disconnect_clicked(GtkButton *btn, gpointer data)
{
	(void)btn;
	PagerCastUi *u = (PagerCastUi *)data;
	gtk_label_set_text(GTK_LABEL(u->status), "Disconnecting…");
	if (u->led) gtk_widget_queue_draw(u->led);
	while (gtk_events_pending()) gtk_main_iteration();
	/* Leave Integrations Enable alone — only stop the stream. */
	pdl_pagercast_disconnect();
	pc_update_status_widgets(u->led, u->status);
	pdl_apply_message_column_layout();
	pdl_linux_gui_sync_pagercast_toggle();
}

/* Options dialog — classic tabs (POCSAG / Display / Integrations). */
static void on_options_clicked(void)
{
	GtkWidget *dlg = gtk_dialog_new_with_buttons("Options", dialog_parent(),
		(GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
		"Cancel", GTK_RESPONSE_REJECT, "OK", GTK_RESPONSE_ACCEPT, NULL);
	gtk_window_set_default_size(GTK_WINDOW(dlg), 520, 480);
	pdl_linux_gui_prepare_dialog(dlg);

	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
	gtk_container_set_border_width(GTK_CONTAINER(content), 8);

	GtkWidget *notebook = gtk_notebook_new();
	gtk_widget_set_vexpand(notebook, TRUE);
	gtk_box_pack_start(GTK_BOX(content), notebook, TRUE, TRUE, 0);
	g_signal_connect(notebook, "change-current-page",
		G_CALLBACK(on_options_change_current_page), NULL);

	GtkWidget *ch_decodepocsag, *ch_pocsag_512, *ch_pocsag_1200, *ch_pocsag_2400;
	GtkWidget *ch_pocsag_fnu, *ch_pocsag_showboth, *e_decrypt;
	GtkWidget *ch_show_cfs, *ch_show_rejectblocked;
	GtkWidget *combo_ui_mode = NULL;

	/* --- POCSAG --- */
	{
		GtkWidget *box = options_tab_page(GTK_NOTEBOOK(notebook), "POCSAG", FALSE, TRUE);
		ch_decodepocsag = options_check(box, "Enable POCSAG decoding", Profile.decodepocsag ? TRUE : FALSE);

		GtkWidget *baud_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
		gtk_widget_set_margin_start(baud_row, 4);
		ch_pocsag_512  = gtk_check_button_new_with_label("512");
		ch_pocsag_1200 = gtk_check_button_new_with_label("1200");
		ch_pocsag_2400 = gtk_check_button_new_with_label("2400");
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_pocsag_512),  Profile.pocsag_512 ? TRUE : FALSE);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_pocsag_1200), Profile.pocsag_1200 ? TRUE : FALSE);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_pocsag_2400), Profile.pocsag_2400 ? TRUE : FALSE);
		gtk_box_pack_start(GTK_BOX(baud_row), gtk_label_new("Baud:"), FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(baud_row), ch_pocsag_512, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(baud_row), ch_pocsag_1200, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(baud_row), ch_pocsag_2400, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(box), baud_row, FALSE, FALSE, 0);

		ch_pocsag_fnu = options_check(box, "Always decode function numbers as default", Profile.pocsag_fnu ? TRUE : FALSE);
		ch_pocsag_showboth = options_check(box, "If unsure, show both numeric and alphanumeric", Profile.pocsag_showboth ? TRUE : FALSE);

		e_decrypt = gtk_entry_new();
		gtk_entry_set_visibility(GTK_ENTRY(e_decrypt), FALSE);
		{
			const char *dk = pdl_linux_get_ini_decrypt_key();
			if (!dk) dk = pdl_platform_pocsag_decrypt_key();
			if (dk) gtk_entry_set_text(GTK_ENTRY(e_decrypt), dk);
		}
		options_form_row(box, "Decrypt key:", e_decrypt);
	}

	/* --- FLEX (unavailable) --- */
	{
		GtkWidget *box = options_tab_page(GTK_NOTEBOOK(notebook), "FLEX", FALSE, FALSE);
		options_check(box, "Enable FLEX decoding", FALSE);
		GtkWidget *rate_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
		gtk_box_pack_start(GTK_BOX(rate_row), gtk_check_button_new_with_label("1600"), FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(rate_row), gtk_check_button_new_with_label("3200"), FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(rate_row), gtk_check_button_new_with_label("6400"), FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(box), rate_row, FALSE, FALSE, 0);
		options_check(box, "Show short instructions", FALSE);
		options_check(box, "Convert short instructions to text", FALSE);
		options_check(box, "Use FlexTIME as system time", FALSE);
	}

	/* --- MOBITEX (unavailable) --- */
	{
		GtkWidget *box = options_tab_page(GTK_NOTEBOOK(notebook), "MOBITEX", FALSE, FALSE);
		options_check(box, "Use custom frame sync", FALSE);
		options_form_row(box, "Frame sync (hex):", gtk_entry_new());
		options_form_row(box, "Bit sync:", gtk_combo_box_text_new());
		options_form_row(box, "Min. characters:", gtk_entry_new());
		options_form_row(box, "Bitrate:", gtk_combo_box_text_new());
		options_check(box, "Bit scrambler", FALSE);
		options_check(box, "Ramnet", FALSE);
		options_check(box, "Show network messages (MPAK)", FALSE);
		options_check(box, "Show TEXT", FALSE);
		options_check(box, "Show DATA", FALSE);
		options_check(box, "Show HPDATA", FALSE);
		options_check(box, "Show HPID", FALSE);
		options_check(box, "Show sweep frames", FALSE);
		options_check(box, "Verbose", FALSE);
	}

	/* --- ACARS (unavailable) --- */
	{
		GtkWidget *box = options_tab_page(GTK_NOTEBOOK(notebook), "ACARS", FALSE, FALSE);
		GtkWidget *acars_yes = gtk_radio_button_new_with_label(NULL, "Full parity checking");
		GSList *acars_group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(acars_yes));
		GtkWidget *acars_no = gtk_radio_button_new_with_label(acars_group, "No parity checking");
		gtk_box_pack_start(GTK_BOX(box), acars_yes, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(box), acars_no, FALSE, FALSE, 0);
	}

	/* --- Display --- */
	{
		GtkWidget *box = options_tab_page(GTK_NOTEBOOK(notebook), "Display", FALSE, TRUE);
		ch_show_cfs = options_check(box, "Show FLEX/ERMES cycles and frames in title bar", Profile.show_cfs ? TRUE : FALSE);
		ch_show_rejectblocked = options_check(box, "Show rejected/blocked messages in title bar", Profile.show_rejectblocked ? TRUE : FALSE);
		combo_ui_mode = gtk_combo_box_text_new();
		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_ui_mode), "gtk", "Classic GTK (default)");
		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_ui_mode), "web", "Modern Web UI");
		gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo_ui_mode),
			Profile.ui_mode == PDL_UI_WEB ? "web" : "gtk");
		options_form_row(box, "Interface:", combo_ui_mode);
		GtkWidget *ui_hint = gtk_label_new("Changing Interface restarts the app on OK.");
		gtk_widget_set_halign(ui_hint, GTK_ALIGN_START);
		gtk_widget_set_opacity(ui_hint, 0.7);
		gtk_box_pack_start(GTK_BOX(box), ui_hint, FALSE, FALSE, 0);
	}

	/* --- Integrations (PagerCast section) --- */
	PagerCastUi *pcui = g_new0(PagerCastUi, 1);
	{
		GtkWidget *box = options_tab_page(GTK_NOTEBOOK(notebook), "Integrations", TRUE, TRUE);
		GtkWidget *frame = setup_frame("PagerCast");
		GtkWidget *pcbox = setup_frame_box(frame);
		gtk_box_pack_start(GTK_BOX(box), frame, FALSE, FALSE, 0);

		pcui->enable = options_check(pcbox, "Enable PagerCast", Profile.pagercast_enabled ? TRUE : FALSE);

		pcui->combo = gtk_combo_box_text_new();
		pdl_pagercast_seed_fallback_nodes();
		int active = 0;
		for (int i = 0; i < pdl_pagercast_node_count(); i++) {
			gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(pcui->combo),
				pdl_pagercast_node_url(i), pdl_pagercast_node_name(i));
			if (Profile.pagercast_api_base[0] &&
				strcmp(Profile.pagercast_api_base, pdl_pagercast_node_url(i)) == 0)
				active = i;
		}
		if (pdl_pagercast_node_count() > 0)
			gtk_combo_box_set_active(GTK_COMBO_BOX(pcui->combo), active);
		options_form_row(pcbox, "Node:", pcui->combo);

		pcui->custom = gtk_entry_new();
		gtk_entry_set_placeholder_text(GTK_ENTRY(pcui->custom), "https://… (optional override)");
		if (Profile.pagercast_api_base[0])
			gtk_entry_set_text(GTK_ENTRY(pcui->custom), Profile.pagercast_api_base);
		options_form_row(pcbox, "Custom URL:", pcui->custom);

		pcui->freq = gtk_entry_new();
		gtk_entry_set_placeholder_text(GTK_ENTRY(pcui->freq), "154.600 or 154600");
		if (Profile.pagercast_frequency[0])
			gtk_entry_set_text(GTK_ENTRY(pcui->freq), Profile.pagercast_frequency);
		options_form_row(pcbox, "Frequency (MHz):", pcui->freq);

		pcui->key = gtk_entry_new();
		gtk_entry_set_visibility(GTK_ENTRY(pcui->key), FALSE);
		gtk_entry_set_placeholder_text(GTK_ENTRY(pcui->key), "pcat_…");
		if (Profile.pagercast_api_key[0])
			gtk_entry_set_text(GTK_ENTRY(pcui->key), Profile.pagercast_api_key);
		options_form_row(pcbox, "API key:", pcui->key);

		GtkWidget *btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
		GtkWidget *btn_refresh = gtk_button_new_with_label("Refresh nodes");
		GtkWidget *btn_connect = gtk_button_new_with_label("Connect");
		GtkWidget *btn_disconnect = gtk_button_new_with_label("Disconnect");
		gtk_box_pack_start(GTK_BOX(btn_row), btn_refresh, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(btn_row), btn_connect, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(btn_row), btn_disconnect, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(pcbox), btn_row, FALSE, FALSE, 4);

		GtkWidget *status_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
		gtk_widget_set_margin_top(status_row, 6);
		pcui->led = gtk_drawing_area_new();
		gtk_widget_set_size_request(pcui->led, 18, 18);
		gtk_widget_set_valign(pcui->led, GTK_ALIGN_CENTER);
		g_signal_connect(pcui->led, "draw", G_CALLBACK(on_pc_led_draw), NULL);
		gtk_box_pack_start(GTK_BOX(status_row), pcui->led, FALSE, FALSE, 0);
		pcui->status = gtk_label_new("");
		gtk_widget_set_halign(pcui->status, GTK_ALIGN_START);
		gtk_label_set_line_wrap(GTK_LABEL(pcui->status), TRUE);
		gtk_box_pack_start(GTK_BOX(status_row), pcui->status, TRUE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX(pcbox), status_row, FALSE, FALSE, 0);
		pc_update_status_widgets(pcui->led, pcui->status);

		g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_pc_refresh_clicked), pcui);
		g_signal_connect(btn_connect, "clicked", G_CALLBACK(on_pc_connect_clicked), pcui);
		g_signal_connect(btn_disconnect, "clicked", G_CALLBACK(on_pc_disconnect_clicked), pcui);

		guint tick = g_timeout_add(250, on_pc_status_tick, pcui);
		g_object_set_data(G_OBJECT(dlg), "pdl-pc-tick", GUINT_TO_POINTER(tick));
	}

	gtk_widget_show_all(dlg);

	gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
	{
		guint tick = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(dlg), "pdl-pc-tick"));
		if (tick) g_source_remove(tick);
	}
	if (resp == GTK_RESPONSE_ACCEPT) {
		log_ui("Options", "applied");
		Profile.decodepocsag    = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_decodepocsag)) ? 1 : 0;
		Profile.pocsag_512     = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_pocsag_512)) ? 1 : 0;
		Profile.pocsag_1200    = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_pocsag_1200)) ? 1 : 0;
		Profile.pocsag_2400    = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_pocsag_2400)) ? 1 : 0;
		Profile.pocsag_fnu     = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_pocsag_fnu)) ? 1 : 0;
		Profile.pocsag_showboth = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_pocsag_showboth)) ? 1 : 0;
		Profile.decodeflex     = 0;
		Profile.show_cfs       = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_show_cfs)) ? 1 : 0;
		Profile.show_rejectblocked = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_show_rejectblocked)) ? 1 : 0;
		int want_ui = PDL_UI_GTK;
		{
			const gchar *uid = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo_ui_mode));
			if (uid && strcmp(uid, "web") == 0)
				want_ui = PDL_UI_WEB;
		}
		int ui_changed = (want_ui != (Profile.ui_mode == PDL_UI_WEB ? PDL_UI_WEB : PDL_UI_GTK));
		Profile.ui_mode = want_ui;
		options_apply_pagercast_fields(pcui);
		if (!Profile.pagercast_enabled && pdl_pagercast_is_wanted())
			pdl_pagercast_disconnect();
		pdl_apply_message_column_layout();
		pdl_linux_gui_sync_pagercast_toggle();
		if (!Profile.decodepocsag) {
			pd_reset_all();
			Reset_ATB();
		}
		pdl_linux_set_ini_decrypt_key(gtk_entry_get_text(GTK_ENTRY(e_decrypt)));
		WriteSettings();
		pdl_linux_gui_update_title();
		if (ui_changed) {
			gint r = pdl_confirm_dialog(GTK_WINDOW(dlg), "Restart",
				"Interface mode saved. Restart now to apply?");
			if (r == GTK_RESPONSE_ACCEPT) {
				gtk_widget_destroy(dlg);
				g_free(pcui);
				pdl_linux_apply_ui_mode(want_ui, 1);
				return;
			}
		}
	} else {
		log_ui("Options", "cancelled");
	}
	g_free(pcui);
	gtk_widget_destroy(dlg);
}


static const char *filter_type_name(FILTER_TYPE t)
{
	switch (t) {
	case FLEX_FILTER: return "FLEX";
	case POCSAG_FILTER: return "POCSAG";
	case TEXT_FILTER: return "TEXT";
	case ERMES_FILTER: return "ERMES";
	case ACARS_FILTER: return "ACARS";
	case MOBITEX_FILTER: return "MOBITEX";
	default: return "?";
	}
}

static void filters_format_row(const FILTER &f, char *row, size_t rowlen)
{
	char flags[64] = "";
	if (f.reject) strcat(flags, " REJECT");
	if (f.monitor_only) strcat(flags, " MON");
	if (f.smtp) strcat(flags, " SMTP");
	snprintf(row, rowlen, "%-7s  %-9s  \"%.40s\"%s  hits=%u",
		filter_type_name(f.type),
		f.capcode[0] ? f.capcode : "—",
		f.text,
		flags,
		f.hitcounter);
}

static void on_filters_clicked(void)
{
	GtkWidget *dlg = gtk_dialog_new_with_buttons("Filters", dialog_parent(),
		(GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
		"Close", GTK_RESPONSE_CLOSE, "Add", 1, "Remove", 2, "Save", 3, NULL);
	gtk_window_set_default_size(GTK_WINDOW(dlg), 680, 460);
	pdl_linux_gui_prepare_dialog(dlg);
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
	gtk_container_set_border_width(GTK_CONTAINER(content), 8);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_box_pack_start(GTK_BOX(content), box, TRUE, TRUE, 0);

	/* --- Filter list --- */
	GtkWidget *list_frame = setup_frame("Filters");
	GtkWidget *list_box = setup_frame_box(list_frame);
	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_widget_set_size_request(scroll, -1, 180);
	GtkWidget *list = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
	gtk_container_add(GTK_CONTAINER(scroll), list);
	gtk_box_pack_start(GTK_BOX(list_box), scroll, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(box), list_frame, TRUE, TRUE, 0);

	/* --- Add form --- */
	GtkWidget *edit_frame = setup_frame("Add filter");
	GtkWidget *edit_box = setup_frame_box(edit_frame);
	GtkWidget *edit_grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(edit_grid), 8);
	gtk_grid_set_row_spacing(GTK_GRID(edit_grid), 6);
	gtk_box_pack_start(GTK_BOX(edit_box), edit_grid, FALSE, FALSE, 0);

	GtkWidget *e_cap = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(e_cap), FILTER_CAPCODE_LEN);
	gtk_entry_set_placeholder_text(GTK_ENTRY(e_cap), "Capcode");
	gtk_grid_attach(GTK_GRID(edit_grid), gtk_label_new("Capcode:"), 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(edit_grid), e_cap, 1, 0, 1, 1);

	GtkWidget *e_text = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(e_text), FILTER_TEXT_LEN);
	gtk_entry_set_placeholder_text(GTK_ENTRY(e_text), "Message text");
	gtk_grid_attach(GTK_GRID(edit_grid), gtk_label_new("Text:"), 2, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(edit_grid), e_text, 3, 0, 1, 1);

	GtkWidget *e_label = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(e_label), FILTER_LABEL_LEN);
	gtk_entry_set_placeholder_text(GTK_ENTRY(e_label), "Optional label");
	gtk_grid_attach(GTK_GRID(edit_grid), gtk_label_new("Label:"), 0, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(edit_grid), e_label, 1, 1, 3, 1);

	GtkWidget *ch_reject = gtk_check_button_new_with_label("Reject");
	GtkWidget *ch_mon = gtk_check_button_new_with_label("Monitor only");
	GtkWidget *ch_smtp = gtk_check_button_new_with_label("SMTP");
	gtk_grid_attach(GTK_GRID(edit_grid), ch_reject, 0, 2, 1, 1);
	gtk_grid_attach(GTK_GRID(edit_grid), ch_mon, 1, 2, 1, 1);
	gtk_grid_attach(GTK_GRID(edit_grid), ch_smtp, 2, 2, 1, 1);

	GtkWidget *type_combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "FLEX");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "POCSAG");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "TEXT");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "ERMES");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "ACARS");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "MOBITEX");
	gtk_combo_box_set_active(GTK_COMBO_BOX(type_combo), 1);
	gtk_grid_attach(GTK_GRID(edit_grid), gtk_label_new("Type:"), 0, 3, 1, 1);
	gtk_grid_attach(GTK_GRID(edit_grid), type_combo, 1, 3, 3, 1);

	gtk_box_pack_start(GTK_BOX(box), edit_frame, FALSE, FALSE, 0);

	auto refresh_list = [&]() {
		GList *children = gtk_container_get_children(GTK_CONTAINER(list));
		for (GList *l = children; l; l = l->next)
			gtk_widget_destroy(GTK_WIDGET(l->data));
		g_list_free(children);
		if (Profile.filters.empty()) {
			GtkWidget *roww = gtk_list_box_row_new();
			gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(roww), FALSE);
			GtkWidget *lab = gtk_label_new("No filters yet.");
			gtk_widget_set_halign(lab, GTK_ALIGN_START);
			gtk_widget_set_margin_start(lab, 8);
			gtk_widget_set_margin_top(lab, 6);
			gtk_widget_set_margin_bottom(lab, 6);
			gtk_style_context_add_class(gtk_widget_get_style_context(lab), "dim-label");
			gtk_container_add(GTK_CONTAINER(roww), lab);
			gtk_list_box_insert(GTK_LIST_BOX(list), roww, -1);
		} else {
			for (size_t i = 0; i < Profile.filters.size(); i++) {
				char row[320];
				filters_format_row(Profile.filters[i], row, sizeof(row));
				GtkWidget *roww = gtk_list_box_row_new();
				GtkWidget *lab = gtk_label_new(row);
				gtk_widget_set_halign(lab, GTK_ALIGN_START);
				gtk_widget_set_margin_start(lab, 6);
				gtk_widget_set_margin_end(lab, 6);
				gtk_widget_set_margin_top(lab, 3);
				gtk_widget_set_margin_bottom(lab, 3);
				gtk_container_add(GTK_CONTAINER(roww), lab);
				gtk_list_box_insert(GTK_LIST_BOX(list), roww, -1);
				g_object_set_data(G_OBJECT(roww), "pdl-idx", GINT_TO_POINTER((int)i));
			}
		}
		gtk_widget_show_all(list);
	};

	gtk_widget_show_all(dlg);
	refresh_list();

	for (;;) {
		gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
		if (resp == GTK_RESPONSE_CLOSE || resp == GTK_RESPONSE_DELETE_EVENT)
			break;
		if (resp == 1) { /* Add */
			const char *cap = gtk_entry_get_text(GTK_ENTRY(e_cap));
			const char *txt = gtk_entry_get_text(GTK_ENTRY(e_text));
			while (cap && *cap == ' ') cap++;
			while (txt && *txt == ' ') txt++;
			if ((!cap || !cap[0]) && (!txt || !txt[0])) {
				pdl_info_dialog(GTK_WINDOW(dlg), "Filters",
					"Enter a capcode and/or text before adding a filter.");
				continue;
			}
			FILTER f;
			memset(&f, 0, sizeof(f));
			static const FILTER_TYPE types[] = {
				FLEX_FILTER, POCSAG_FILTER, TEXT_FILTER, ERMES_FILTER, ACARS_FILTER, MOBITEX_FILTER
			};
			gint ti = gtk_combo_box_get_active(GTK_COMBO_BOX(type_combo));
			if (ti < 0 || ti > 5) ti = 1;
			f.type = types[ti];
			strncpy(f.capcode, cap ? cap : "", FILTER_CAPCODE_LEN);
			strncpy(f.text, txt ? txt : "", FILTER_TEXT_LEN);
			strncpy(f.label, gtk_entry_get_text(GTK_ENTRY(e_label)), FILTER_LABEL_LEN);
			f.reject = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_reject)) ? 1 : 0;
			f.monitor_only = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_mon)) ? 1 : 0;
			f.smtp = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_smtp)) ? 1 : 0;
			f.label_enabled = f.label[0] ? 1 : 0;
			Profile.filters.push_back(f);
			gtk_entry_set_text(GTK_ENTRY(e_cap), "");
			gtk_entry_set_text(GTK_ENTRY(e_text), "");
			gtk_entry_set_text(GTK_ENTRY(e_label), "");
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_reject), FALSE);
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_mon), FALSE);
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_smtp), FALSE);
		} else if (resp == 2) { /* Remove */
			GtkListBoxRow *sel = gtk_list_box_get_selected_row(GTK_LIST_BOX(list));
			if (sel) {
				gpointer idxp = g_object_get_data(G_OBJECT(sel), "pdl-idx");
				if (idxp) {
					int idx = GPOINTER_TO_INT(idxp);
					if (idx >= 0 && (size_t)idx < Profile.filters.size())
						Profile.filters.erase(Profile.filters.begin() + idx);
				}
			}
		} else if (resp == 3) {
			WriteFilters(&Profile, 1);
			log_ui("Filters", "saved");
			continue;
		}
		refresh_list();
	}
	WriteFilters(&Profile, 0);
	gtk_widget_destroy(dlg);
}

void pdl_linux_gui_show_filters(void)
{
	on_filters_clicked();
}

void pdl_linux_gui_show_options(void)
{
	on_options_clicked();
}

void pdl_linux_gui_show_audio(void)
{
	on_audio_clicked(NULL, NULL);
}

void pdl_linux_gui_show_stats(void)
{
	on_menu_activate(NULL, GINT_TO_POINTER(IDM_MONSTAT));
}

void pdl_linux_gui_show_clear(void)
{
	on_clear_screen_dialog();
}

void pdl_linux_gui_show_about(void)
{
	on_menu_activate(NULL, GINT_TO_POINTER(IDM_ABOUT));
}

static GtkWidget *add_menu_item(GtkWidget *menu, const char *label, int id)
{
	GtkWidget *mi = gtk_menu_item_new_with_label(label);
	g_signal_connect(mi, "activate", G_CALLBACK(on_menu_activate), GINT_TO_POINTER(id));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
	return mi;
}

static void migrate_dark_message_colors_to_light(void)
{
	/* Old PDL.ini black-pane palette is unreadable under Mockup-1 light chrome. */
	if (Profile.color_background != 0 && Profile.color_background != RGB(0, 0, 0))
		return;
	Profile.color_background = RGB(255, 255, 255);
	Profile.color_address = RGB(0, 0, 0);
	Profile.color_modetypebit = RGB(170, 0, 0);
	Profile.color_timestamp = RGB(0, 90, 160);
	Profile.color_numeric = RGB(180, 0, 0);
	Profile.color_message = RGB(25, 25, 25);
	Profile.color_misc = RGB(110, 75, 20);
	Profile.color_biterrors = RGB(130, 130, 130);
	Profile.color_filtermatch = RGB(0, 140, 30);
	Profile.color_instructions = RGB(0, 110, 130);
	Profile.color_ac_message_nr = RGB(0, 0, 0);
	Profile.color_ac_dbi = RGB(170, 0, 0);
	Profile.color_mb_sender = RGB(170, 0, 0);
}

static void apply_app_theme(void)
{
	migrate_dark_message_colors_to_light();
	/* Always keep filter-match green vivid on light chrome. */
	if (GetRValue(Profile.color_filtermatch) < 40 &&
	    GetGValue(Profile.color_filtermatch) > 100 &&
	    GetBValue(Profile.color_filtermatch) < 80)
		; /* already green */
	else if (Profile.color_background == RGB(255, 255, 255) ||
		 Profile.color_background == RGB(252, 252, 252))
		Profile.color_filtermatch = RGB(0, 140, 30);

	/*
	 * System theme is adw-gtk3, which draws combobox as nested white shells
	 * (the thick white halo). CSS overrides lose to its selectors — force a
	 * Emacs theme for this process, then paint everything flat grey.
	 */
	GtkSettings *settings = gtk_settings_get_default();
	if (settings) {
		g_object_set(settings,
			"gtk-theme-name", "Emacs",
			"gtk-application-prefer-dark-theme", FALSE,
			NULL);
	}

	GtkCssProvider *css = gtk_css_provider_new();
	const char *style =
		"window, .pdl-main { background-color: #e8e8e8; color: #1a1a1a; }\n"
		"* { text-shadow: none; -gtk-icon-shadow: none; }\n"
		"menubar, .pdl-menubar, .menubar {"
		"  background-color: #e8e8e8; color: #1a1a1a; border-bottom: 1px solid #c0c0c0;"
		"  box-shadow: none; padding: 2px 0; }\n"
		"menubar > menuitem, .menubar > menuitem {"
		"  padding: 3px 10px; color: #1a1a1a; border: none; border-radius: 0;"
		"  box-shadow: none; background-image: none; }\n"
		"menubar > menuitem:hover, .menubar > menuitem:hover {"
		"  background-color: #d0d0d0; color: #1a1a1a; box-shadow: none; border-radius: 0; }\n"
		".background.popup { background-color: #ffffff; }\n"
		"decoration { background-color: #ffffff; border: 1px solid #888; border-radius: 0; box-shadow: none; }\n"
		"menu, .menu, .context-menu {"
		"  margin: 0; padding: 0; background-color: #e8e8e8; color: #1a1a1a;"
		"  border: 1px solid #787878; border-radius: 0; box-shadow: none; }\n"
		"menuitem {"
		"  padding: 5px 14px; color: #1a1a1a; border: none; border-radius: 0;"
		"  background-color: #e8e8e8; background-image: none; box-shadow: none;"
		"  outline-style: none; outline-width: 0px; outline-offset: 0; }\n"
		"menuitem:hover, menuitem:selected, menuitem:hover:selected {"
		"  background-color: #3584e4; color: #ffffff;"
		"  border: none; border-radius: 0; box-shadow: none;"
		"  outline-style: none; outline-width: 0px; outline-offset: 0; }\n"
		"menuitem:disabled, menuitem:disabled:hover, menuitem:disabled:selected,"
		"menuitem:disabled label, menuitem:disabled:hover label {"
		"  color: #9a9a9a; background-color: #e8e8e8; background-image: none; }\n"
		"popover, popover.background {"
		"  background-color: #e8e8e8; border: 1px solid #787878; border-radius: 0;"
		"  box-shadow: none; padding: 0; }\n"
		"popover modelbutton {"
		"  background-color: #e8e8e8; color: #1a1a1a; border: none; border-radius: 0;"
		"  box-shadow: none; outline-style: none; outline-width: 0px; padding: 5px 14px; }\n"
		"popover modelbutton:hover, popover modelbutton:selected {"
		"  background-color: #3584e4; color: #ffffff; border: none; box-shadow: none;"
		"  outline-style: none; outline-width: 0px; outline-offset: 0; }\n"
		/*
		 * Combo popup = menu of menuitems, each with a CellView child
		 * (docs.gtk.org/gtk3/class.ComboBox.html , class.CellView.html).
		 * Menuitem:hover paints blue; CellView must stay TRANSPARENT or it
		 * covers the blue with a grey slab behind the text (hollow selection).
		 * `.pdl-dialog *` is more specific than bare `cellview {}` — override it.
		 * TreeView list mode also paints via STYLE_CLASS_CELL (.cell).
		 */
		"@define-color theme_selected_bg_color #3584e4;\n"
		"@define-color theme_selected_fg_color #ffffff;\n"
		"@define-color theme_unfocused_selected_bg_color #3584e4;\n"
		"@define-color theme_unfocused_selected_fg_color #ffffff;\n"
		"@define-color selected_bg_color #3584e4;\n"
		"@define-color selected_fg_color #ffffff;\n"
		"combobox window.popup { background-color: #e8e8e8; border: 1px solid #787878; box-shadow: none; }\n"
		"cellview, cellview:hover, cellview:selected, cellview:selected:focus,"
		".pdl-dialog cellview, .pdl-dialog cellview:hover, .pdl-dialog cellview:selected,"
		"menuitem cellview, menuitem:hover cellview, menuitem:selected cellview {"
		"  background-color: transparent; background-image: none; color: inherit;"
		"  border: none; border-style: none; box-shadow: none;"
		"  outline-style: none; outline-width: 0px; }\n"
		"cellview:hover, cellview:selected { color: #ffffff; }\n"
		"treeview.view, .view {"
		"  background-color: #e8e8e8; color: #1a1a1a; background-image: none;"
		"  border-style: none; border-width: 0;"
		"  border-left-color: transparent; border-top-color: transparent;"
		"  border-right-color: transparent; border-bottom-color: transparent;"
		"  outline-style: none; outline-width: 0px; outline-offset: 0;"
		"  box-shadow: none; -gtk-outline-radius: 0; }\n"
		"treeview.view:selected, treeview.view:selected:focus,"
		".view:selected, .view:selected:focus,"
		"treeview.view:selected:hover, .view:selected:hover,"
		".cell:selected, .cell:selected:focus, .cell:hover,"
		"treeview.view.cell:selected, treeview.view.cell:selected:focus,"
		".view text:selected, .view text:selected:focus,"
		"treeview.view text:selected, treeview.view text:selected:focus {"
		"  background-color: #3584e4; background-image: none; color: #ffffff;"
		"  border-style: none; border-width: 0; border-radius: 0;"
		"  border-left-color: #3584e4; border-top-color: #3584e4;"
		"  border-right-color: #3584e4; border-bottom-color: #3584e4;"
		"  outline-style: none; outline-width: 0px; outline-offset: 0;"
		"  outline-color: transparent; -gtk-outline-radius: 0;"
		"  box-shadow: none; }\n"
		"treeview.view:hover, .view:hover {"
		"  background-color: #3584e4; color: #ffffff;"
		"  outline-style: none; outline-width: 0px; border-style: none; box-shadow: none; }\n"
		/* Uniform grey everywhere in chrome/dialogs — no white fills, no white frame pads */
		"combobox, combobox > box, combobox > box.linked,"
		"combobox:hover, combobox:focus, combobox:active,"
		"combobox > box:hover, combobox > box.linked:hover {"
		"  background-color: transparent; background-image: none;"
		"  border: none; border-style: none; border-width: 0;"
		"  box-shadow: none; outline-width: 0; outline-style: none;"
		"  padding: 0; margin: 0; min-height: 0; }\n"
		"combobox button, combobox button.combo,"
		"combobox > box > button, combobox > box.linked > button, button.combo,"
		"combobox button:hover, combobox button:active, combobox button:checked,"
		"combobox button:focus, combobox button:disabled, combobox button:backdrop,"
		"button.combo:hover, button.combo:active, button.combo:focus, button.combo:backdrop {"
		"  background-color: #e8e8e8; background-image: none; color: #1a1a1a;"
		"  border: 1px solid #787878;"
		"  border-top-color: #787878; border-right-color: #787878;"
		"  border-bottom-color: #787878; border-left-color: #787878;"
		"  border-radius: 0; border-style: solid;"
		"  box-shadow: none; text-shadow: none;"
		"  outline-width: 0; outline-style: none; outline-offset: 0;"
		"  -gtk-outline-radius: 0; -gtk-icon-shadow: none;"
		"  background-clip: border-box;"
		"  min-height: 28px; padding: 4px 8px; }\n"
		"combobox button:focus, button.combo:focus {"
		"  border-color: #3a7bd5; border-top-color: #3a7bd5; border-bottom-color: #3a7bd5;"
		"  border-left-color: #3a7bd5; border-right-color: #3a7bd5;"
		"  background-color: #e8e8e8; box-shadow: none; outline-width: 0; outline-style: none; }\n"
		"combobox button:hover, button.combo:hover {"
		"  background-color: #dedede; box-shadow: none; }\n"
		"entry {"
		"  background-color: #e8e8e8; background-image: none; color: #1a1a1a;"
		"  border: 1px solid #787878; border-radius: 0; box-shadow: none;"
		"  outline-width: 0; outline-style: none; min-height: 28px; padding: 4px 8px; }\n"
		"button {"
		"  background-color: #e8e8e8; background-image: none; color: #1a1a1a;"
		"  border: 1px solid #787878; border-radius: 0;"
		"  box-shadow: none; outline-width: 0; outline-style: none; text-shadow: none; }\n"
		"button:hover { background-color: #dedede; }\n"
		/* Frames: kill Emacs groove (white highlight). One grey fill + one dark line. */
		"frame {"
		"  background-color: #e8e8e8; background-image: none;"
		"  border: none; padding: 0; margin: 0; box-shadow: none; }\n"
		"frame > border {"
		"  background-color: #e8e8e8; background-image: none;"
		"  border: 1px solid #787878; border-style: solid; border-radius: 0;"
		"  border-top-color: #787878; border-right-color: #787878;"
		"  border-bottom-color: #787878; border-left-color: #787878;"
		"  padding: 8px; margin: 0; box-shadow: none; }\n"
		"frame > label {"
		"  background-color: #e8e8e8; color: #333; font-weight: bold;"
		"  padding: 0 4px; }\n"
		/*
		 * Check/radio — docs.gtk.org/gtk3/class.CheckButton.html
		 * Nodes: checkbutton > check , radiobutton > radio.
		 * Emacs has no assets; must set -gtk-icon-source for :checked or
		 * the tick never appears (empty grey box forever).
		 */
		"checkbutton, radiobutton {"
		"  background-color: transparent; background-image: none;"
		"  border: none; box-shadow: none; outline-style: none; outline-width: 0;"
		"  padding: 2px 0; min-height: 0; }\n"
		"checkbutton label, radiobutton label {"
		"  background-color: transparent; color: #1a1a1a; }\n"
		"checkbutton check, radiobutton radio {"
		"  min-width: 16px; min-height: 16px; margin: 0 8px 0 0; padding: 0;"
		"  background-color: #e8e8e8; background-image: none;"
		"  border: 1px solid #787878; border-radius: 0; box-shadow: none;"
		"  color: transparent; -gtk-icon-source: none; -gtk-icon-shadow: none;"
		"  outline-style: none; outline-width: 0; }\n"
		"radiobutton radio { border-radius: 100%; }\n"
		"checkbutton check:hover, radiobutton radio:hover {"
		"  background-color: #dedede; border-color: #666; }\n"
		"checkbutton check:checked, checkbutton check:checked:hover,"
		"radiobutton radio:checked, radiobutton radio:checked:hover {"
		"  background-color: #3584e4; background-image: none;"
		"  border-color: #2f6bc0; color: #ffffff;"
		"  -gtk-icon-source: -gtk-icontheme('object-select-symbolic');"
		"  -gtk-icon-shadow: none; }\n"
		"checkbutton check:disabled, radiobutton radio:disabled {"
		"  background-color: #d0d0d0; border-color: #aaa; color: transparent;"
		"  -gtk-icon-source: none; }\n"
		"checkbutton check:checked:disabled, radiobutton radio:checked:disabled {"
		"  background-color: #9ab6d8; border-color: #8aa0bc; color: #e8e8e8;"
		"  -gtk-icon-source: -gtk-icontheme('object-select-symbolic'); }\n"
		".pdl-toolbar { background: #e4e4e4; border-bottom: 1px solid #b8b8b8; padding: 4px 6px; }\n"
		".pdl-toolbtn {"
		"  background: transparent; border: 1px solid transparent; border-radius: 4px;"
		"  padding: 4px 8px; color: #222; box-shadow: none; min-height: 0; }\n"
		".pdl-toolbtn:hover { background: #d6d6d6; border-color: #aaa; }\n"
		".pdl-toolbtn:active { background: #c8c8c8; border-color: #888; }\n"
		".pdl-toolbtn label { color: #333; font-size: 10px; }\n"
		".pdl-toolbtn image { -gtk-icon-style: symbolic; color: #333; }\n"
		".pdl-pc-tabs {"
		"  background: transparent; border: none; padding: 0; margin-left: 4px; box-shadow: none; }\n"
		".pdl-pc-tabs > header {"
		"  background: #e0e0e0; border: 1px solid #b0b0b0; border-radius: 6px; padding: 2px; box-shadow: none; }\n"
		".pdl-pc-tabs > header tabs { padding: 0; }\n"
		".pdl-pc-tabs tab {"
		"  background: transparent; color: #666; padding: 5px 12px; border: none; border-radius: 4px;"
		"  box-shadow: none; min-height: 0; font-size: 11px; }\n"
		".pdl-pc-tabs tab:checked {"
		"  background: #3a7bd5; color: #ffffff; font-weight: bold; box-shadow: none; }\n"
		".pdl-pc-tabs tab:hover { background: #dde6f5; color: #222; }\n"
		".pdl-pc-tabs tab:checked:hover { background: #2f6bc0; color: #ffffff; }\n"
		".pdl-pc-tabs > stack, .pdl-pc-tabs > stack > * {"
		"  min-height: 0; padding: 0; margin: 0; background: transparent; border: none; }\n"
		".pdl-header { background: #dcdcdc; color: #222; font-family: monospace; font-size: 11px;"
		"  padding: 3px 6px 3px 0; border-bottom: 1px solid #b0b0b0; }\n"
		".pdl-header label { font-family: monospace; font-size: 11px; color: #222; }\n"
		".pdl-pane-title { background: #d0d0d0; color: #333; font-family: monospace; font-size: 11px;"
		"  font-weight: bold; padding: 2px 8px; border-bottom: 1px solid #b8b8b8; }\n"
		".pdl-footer { background: #e4e4e4; color: #333; font-family: monospace; font-size: 11px;"
		"  padding: 4px 8px; border-top: 1px solid #b8b8b8; }\n"
		".pdl-sidebar { background: #e8e8e8; border-left: 1px solid #b8b8b8; padding: 10px 8px; }\n"
		".pdl-side-panel { background: #e8e8e8; border: 1px solid #b0b0b0; border-radius: 4px; margin-bottom: 10px; box-shadow: none; }\n"
		".pdl-side-panel-title { color: #222; font-weight: bold; font-size: 11px; letter-spacing: 0.8px;"
		"  padding: 14px 16px 10px 16px; }\n"
		".pdl-side-panel-body { padding: 4px 16px 16px 16px; }\n"
		".pdl-meter-label { color: #444; font-size: 10px; font-weight: bold; letter-spacing: 0.5px; }\n"
		".pdl-meter-val { color: #1a1a1a; font-family: monospace; font-size: 11px; }\n"
		".pdl-meter-bar trough {\n"
		"  background: #c8c8c8; border-radius: 3px; min-height: 10px;\n"
		"  padding: 1px 3px; box-shadow: none; }\n"
		".pdl-meter-bar progress {\n"
		"  background: #3a7bd5; border-radius: 2px; min-height: 6px;\n"
		"  box-shadow: none; margin: 0; }\n"
		"paned separator { background: #c0c0c0; }\n"
		"scrolledwindow { border: none; box-shadow: none; background-color: #e8e8e8; }\n"
		"dialog.pdl-dialog, messagedialog.pdl-dialog, .pdl-dialog,"
		".pdl-dialog box, .pdl-dialog .dialog-vbox,"
		".pdl-dialog viewport, .pdl-dialog scrolledwindow {"
		"  background-color: #e8e8e8; background-image: none; color: #1a1a1a;"
		"  box-shadow: none; }\n"
		/*
		 * Do NOT use `.pdl-dialog *` for background — it paints CellView
		 * grey over menuitem:hover blue (grey slab behind combo text).
		 * Re-assert transparent cellview after dialog rules (higher specificity).
		 */
		".pdl-dialog cellview, .pdl-dialog cellview:hover, .pdl-dialog cellview:selected,"
		".pdl-dialog menuitem cellview, menu cellview, menuitem cellview,"
		"cellview, cellview:hover, cellview:selected {"
		"  background-color: transparent; background-image: none; color: inherit;"
		"  border: none; box-shadow: none; }\n"
		".pdl-dialog menuitem:hover, .pdl-dialog menuitem:selected,"
		"menu menuitem:hover, menu menuitem:selected {"
		"  background-color: #3584e4; color: #ffffff; }\n"
		"dialog.pdl-dialog, messagedialog.pdl-dialog, .pdl-dialog {"
		"  border: 1px solid #888; }\n"
		".pdl-dialog-title { background-color: #d8d8d8; border-bottom: 1px solid #aaa; }\n"
		".pdl-dialog-title label { color: #111; font-weight: bold; font-size: 12px; background-color: #d8d8d8; }\n"
		".pdl-dialog label { color: #1a1a1a; background-color: transparent; }\n"
		".pdl-dialog frame {"
		"  background-color: #e8e8e8; border: none; padding: 0; box-shadow: none; }\n"
		".pdl-dialog frame > border {"
		"  background-color: #e8e8e8; background-image: none;"
		"  border: 1px solid #787878; border-style: solid; border-radius: 0;"
		"  border-top-color: #787878; border-right-color: #787878;"
		"  border-bottom-color: #787878; border-left-color: #787878;"
		"  padding: 8px; margin: 0; box-shadow: none; }\n"
		".pdl-dialog frame > label {"
		"  color: #333; font-weight: bold; background-color: #e8e8e8; padding: 0 4px; }\n"
		".pdl-dialog combobox button, .pdl-dialog button.combo, .pdl-dialog entry,"
		".pdl-dialog button {"
		"  background-color: #e8e8e8; background-image: none; color: #1a1a1a;"
		"  border: 1px solid #787878; border-radius: 0; box-shadow: none; min-height: 28px; }\n"
		".pdl-dialog button:hover, .pdl-dialog combobox button:hover {"
		"  background-color: #dedede; }\n"
		".pdl-dialog checkbutton, .pdl-dialog radiobutton {"
		"  background-color: transparent; border: none; box-shadow: none; }\n"
		".pdl-dialog checkbutton label, .pdl-dialog radiobutton label {"
		"  background-color: transparent; color: #1a1a1a; }\n"
		".pdl-dialog checkbutton:disabled label, .pdl-dialog radiobutton:disabled label,"
		".pdl-dialog label:disabled, .pdl-dialog *:disabled {"
		"  color: #9a9a9a; }\n"
		".pdl-dialog checkbutton check, .pdl-dialog radiobutton radio {"
		"  min-width: 16px; min-height: 16px; margin: 0 8px 0 0;"
		"  background-color: #e8e8e8; background-image: none;"
		"  border: 1px solid #787878; border-radius: 0; box-shadow: none;"
		"  color: transparent; -gtk-icon-source: none; }\n"
		".pdl-dialog radiobutton radio { border-radius: 100%; }\n"
		".pdl-dialog checkbutton check:checked, .pdl-dialog checkbutton check:checked:hover,"
		".pdl-dialog radiobutton radio:checked, .pdl-dialog radiobutton radio:checked:hover {"
		"  background-color: #3584e4; border-color: #2f6bc0; color: #ffffff;"
		"  -gtk-icon-source: -gtk-icontheme('object-select-symbolic');"
		"  -gtk-icon-shadow: none; }\n"
		".pdl-dialog checkbutton check:disabled, .pdl-dialog radiobutton radio:disabled {"
		"  background-color: #d8d8d8; border-color: #aaa; color: transparent;"
		"  -gtk-icon-source: none; }\n"
		".pdl-dialog entry:disabled, .pdl-dialog combobox:disabled button,"
		".pdl-dialog button:disabled {"
		"  color: #9a9a9a; background-color: #d8d8d8; border-color: #aaa; }\n"
		".pdl-dialog .dialog-action-area {"
		"  background-color: #e8e8e8; border-top: 1px solid #aaa; padding: 6px; }\n"
		".pdl-dialog notebook, .pdl-dialog notebook > stack, .pdl-dialog notebook > stack > * {"
		"  background-color: #e8e8e8; border: none; box-shadow: none; }\n"
		".pdl-dialog notebook header { background-color: #e0e0e0; border: none; box-shadow: none; }\n"
		".pdl-dialog notebook tab {"
		"  background: #d8d8d8; color: #333; padding: 4px 10px; border: 1px solid #787878;"
		"  border-radius: 0; box-shadow: none; }\n"
		".pdl-dialog notebook tab:checked {"
		"  background: #e8e8e8; color: #111; box-shadow: inset 0 -2px #3a7bd5; }\n"
		".pdl-dialog notebook tab:disabled, .pdl-dialog notebook tab:disabled:checked,"
		".pdl-dialog notebook tab:disabled label {"
		"  color: #9a9a9a; background: #d0d0d0; box-shadow: none; }\n";
	GError *css_err = NULL;
	gtk_css_provider_load_from_data(css, style, -1, &css_err);
	if (css_err) {
		g_warning("PDL theme CSS: %s", css_err->message);
		g_error_free(css_err);
	}
	GdkDisplay *display = gdk_display_get_default();
	GdkScreen *screen = display ? gdk_display_get_default_screen(display) : NULL;
	if (screen)
		gtk_style_context_add_provider_for_screen(screen,
			GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_USER);
	g_object_unref(css);
	pdl_linux_gui_apply_display_style();
}

void pdl_linux_gui_invalidate_panes(void)
{
	s_last_pane1_bottom = (unsigned)-1;
	s_last_pane2_bottom = (unsigned)-1;
}

void pdl_linux_gui_apply_display_style(void)
{
	static GtkCssProvider *pane_css = NULL;
	if (!pane_css)
		pane_css = gtk_css_provider_new();

	int pt = Profile.fontInfo.lfHeight;
	if (pt < 0) pt = -pt;
	if (pt < 8 || pt > 36) pt = 11;

	COLORREF bg = Profile.color_background;
	COLORREF fg = Profile.color_message;
	/* Classic PDL defaults are black-on-cyan; force Mockup-1 light panes. */
	if (bg == 0 || bg == RGB(0, 0, 0))
		bg = RGB(255, 255, 255);
	if (!fg || fg == RGB(0, 255, 255) || fg == RGB(0, 0xc0, 0xc0))
		fg = RGB(20, 20, 20);
	int br = (bg >> 0) & 0xFF, bg_ = (bg >> 8) & 0xFF, bb = (bg >> 16) & 0xFF;
	int fr = (fg >> 0) & 0xFF, fg_ = (fg >> 8) & 0xFF, fb = (fg >> 16) & 0xFF;

	char style[512];
	snprintf(style, sizeof(style),
		"textview.pdl-pane { padding: 4px 8px 4px 0; }\n"
		"textview.pdl-pane, textview.pdl-pane text {"
		" background-color: rgb(%d,%d,%d); color: rgb(%d,%d,%d);"
		" font-family: monospace; font-size: %dpx; }\n",
		br, bg_, bb, fr, fg_, fb, pt);
	gtk_css_provider_load_from_data(pane_css, style, -1, NULL);
	GdkDisplay *display = gdk_display_get_default();
	GdkScreen *screen = display ? gdk_display_get_default_screen(display) : NULL;
	if (screen)
		gtk_style_context_add_provider_for_screen(screen,
			GTK_STYLE_PROVIDER(pane_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
	pdl_linux_gui_invalidate_panes();
}

static void format_message_header_line(char *line, size_t linelen)
{
	extern int iItemPositions[9];
	static const char *names[] = {
		NULL, "Address", "Time", "Date", "Mode", "Type", "Bitrate", "Message", "Phone"
	};
	memset(line, ' ', linelen - 1);
	line[linelen - 1] = '\0';
	for (int i = 0; i < 8; i++) {
		int id = Profile.ScreenColumns[i];
		if (id == 0) break;
		if (id < 1 || id > 8) continue;
		int pos = iItemPositions[id];
		if (pos <= 0 || pos >= (int)linelen - 1) continue;
		const char *n = names[id];
		if (!n) continue;
		for (int k = 0; n[k] && pos + k < (int)linelen - 1; k++)
			line[pos + k] = n[k];
	}
	int end = (int)linelen - 2;
	while (end > 0 && line[end] == ' ') end--;
	line[end + 1] = '\0';
}

static void refresh_message_header(void)
{
	if (!s_header_label) return;
	char line[192];
	format_message_header_line(line, sizeof(line));
	gtk_label_set_text(GTK_LABEL(s_header_label), line);
}

static GtkWidget *make_header_row(void)
{
	char line[192];
	format_message_header_line(line, sizeof(line));

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_size_request(box, -1, HEADER_HEIGHT);
	GtkStyleContext *ctx = gtk_widget_get_style_context(box);
	gtk_style_context_add_class(ctx, "pdl-header");
	s_header_label = gtk_label_new(line);
	gtk_label_set_xalign(GTK_LABEL(s_header_label), 0.0f);
	gtk_label_set_yalign(GTK_LABEL(s_header_label), 0.5f);
	/* Match textview left_margin so headers sit over the same columns. */
	gtk_widget_set_margin_start(s_header_label, 12);
	gtk_box_pack_start(GTK_BOX(box), s_header_label, TRUE, TRUE, 0);
	return box;
}

static GtkWidget *make_pane_title(const char *title)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_size_request(box, -1, HEADER_HEIGHT);
	gtk_style_context_add_class(gtk_widget_get_style_context(box), "pdl-pane-title");
	GtkWidget *l = gtk_label_new(title);
	gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
	gtk_box_pack_start(GTK_BOX(box), l, TRUE, TRUE, 2);
	return box;
}

/* Map buff_color indices → Profile colors (same as original PDL.cpp GetColorRGB). */
static DWORD GetColorRGB(BYTE color)
{
	switch (color) {
	case COLOR_BACKGROUND:     return Profile.color_background;
	case COLOR_ADDRESS:        return Profile.color_address;
	case COLOR_MODETYPEBIT:    return Profile.color_modetypebit;
	case COLOR_TIMESTAMP:      return Profile.color_timestamp;
	case COLOR_NUMERIC:        return Profile.color_numeric;
	case COLOR_MESSAGE:        return Profile.color_message;
	case COLOR_MISC:           return Profile.color_misc;
	case COLOR_BITERRORS:      return Profile.color_biterrors;
	case COLOR_INSTRUCTIONS:   return Profile.color_instructions;
	case COLOR_AC_MESSAGE_NR:  return Profile.color_ac_message_nr;
	case COLOR_AC_DBI:         return Profile.color_ac_dbi;
	case COLOR_MB_SENDER:      return Profile.color_mb_sender;
	case COLOR_FILTERMATCH:    return Profile.color_filtermatch;
	default:
		if (color >= COLOR_FILTERLABEL && color <= COLOR_FILTERLABEL + 16)
			return Profile.color_filterlabel[color - COLOR_FILTERLABEL];
		/* COLOR_UNUSED and unknown → message color (visible on black) */
		return Profile.color_message;
	}
}

static void colorref_to_rgba(COLORREF c, GdkRGBA *fg)
{
	fg->red   = GetRValue(c) / 255.0;
	fg->green = GetGValue(c) / 255.0;
	fg->blue  = GetBValue(c) / 255.0;
	fg->alpha = 1.0;
}

static void sync_color_tags(GtkTextBuffer *buf)
{
	GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buf);
	for (int i = 0; i < 37; i++) {
		char name[8];
		snprintf(name, sizeof(name), "c%d", i);
		GtkTextTag *tag = gtk_text_tag_table_lookup(table, name);
		GdkRGBA fg;
		colorref_to_rgba(GetColorRGB((BYTE)i), &fg);
		if (!tag)
			gtk_text_buffer_create_tag(buf, name, "foreground-rgba", &fg, NULL);
		else
			g_object_set(tag, "foreground-rgba", &fg, NULL);
	}
	/* Row highlight: character background only (paragraph-bg ignores left margin
	 * and makes filtered rows look shifted relative to normal rows/headers). */
	GtkTextTag *frow = gtk_text_tag_table_lookup(table, "filter-row");
	if (!frow) {
		frow = gtk_text_buffer_create_tag(buf, "filter-row",
			"background", "#B7E0A8",
			NULL);
	} else {
		g_object_set(frow,
			"background", "#B7E0A8",
			"paragraph-background", NULL,
			NULL);
	}
}

static char *latin1_to_utf8(const char *str, int len)
{
	if (!str || len == 0) return NULL;
	if (len < 0) len = (int)strlen(str);
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

static void refresh_pane(PaneStruct *pane, GtkTextView *view, unsigned int *p_last)
{
	if (!pane || !view || !pane->buff_char || !p_last) return;
	unsigned int n = pane->Bottom;
	if (n > pane->buff_lines) n = pane->buff_lines;
	if (n == *p_last) return;
	*p_last = n;

	GtkTextBuffer *buf = gtk_text_view_get_buffer(view);
	sync_color_tags(buf);
	gtk_text_buffer_set_text(buf, "", -1);

	GtkTextIter end;
	for (unsigned int i = 0; i < n; i++) {
		const char *line = &pane->buff_char[i * (LINE_SIZE + 1)];
		const BYTE *cols = pane->buff_color ? &pane->buff_color[i * (LINE_SIZE + 1)] : NULL;
		int len = 0;
		while (len < LINE_SIZE && line[len] && line[len] != '\r' && line[len] != '\n') len++;
		while (len > 0 && line[len - 1] == ' ') len--;
		if (len == 0) {
			gtk_text_buffer_get_end_iter(buf, &end);
			gtk_text_buffer_insert(buf, &end, "\n", 1);
			continue;
		}
		int filter_hit = 0;
		if (cols) {
			for (int k = 0; k < len; k++) {
				if (cols[k] == COLOR_FILTERMATCH) { filter_hit = 1; break; }
			}
		}

		int pos = 0;
		while (pos < len) {
			BYTE c0 = cols ? cols[pos] : 0;
			int run = pos + 1;
			while (run < len && (!cols || cols[run] == c0)) run++;
			char *utf = latin1_to_utf8(line + pos, run - pos);
			char tagname[8];
			snprintf(tagname, sizeof(tagname), "c%d", (int)c0);
			gtk_text_buffer_get_end_iter(buf, &end);
			if (utf) {
				if (filter_hit)
					gtk_text_buffer_insert_with_tags_by_name(buf, &end, utf, -1,
						tagname, "filter-row", NULL);
				else
					gtk_text_buffer_insert_with_tags_by_name(buf, &end, utf, -1, tagname, NULL);
			}
			g_free(utf);
			pos = run;
		}
		/* Extend highlight under empty columns so the green bar matches row width. */
		if (filter_hit && len < 96) {
			char pad[97];
			int n_pad = 96 - len;
			memset(pad, ' ', (size_t)n_pad);
			pad[n_pad] = '\0';
			gtk_text_buffer_get_end_iter(buf, &end);
			gtk_text_buffer_insert_with_tags_by_name(buf, &end, pad, n_pad, "filter-row", NULL);
		}
		gtk_text_buffer_get_end_iter(buf, &end);
		gtk_text_buffer_insert(buf, &end, "\n", 1);
	}
	gtk_text_buffer_get_end_iter(buf, &end);
	gtk_text_view_scroll_to_iter(view, &end, 0.0, FALSE, 0.0, 1.0);
}

static void set_bar_pct(GtkWidget *bar, GtkWidget *lbl, double pct, const char *text)
{
	if (pct < 0.0) pct = 0.0;
	if (pct > 100.0) pct = 100.0;
	if (bar) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar), pct / 100.0);
	if (lbl && text) gtk_label_set_text(GTK_LABEL(lbl), text);
}

/* Mockup 1 sidebar metrics (features only — keep classic chrome). */
static void update_rx_quality_display(void)
{
	extern int nCount_Messages, nCount_Rejected;
	extern int nCount_Missed[2];

	double level = s_meter_display_level;
	double q = (bRX_MessageQuality_Valid && dRX_MessageQuality >= 0.0) ? dRX_MessageQuality
		: (dRX_Quality >= 0.0 ? dRX_Quality : 0.0);
	if (q < 0.0) q = 0.0;
	if (q > 100.0) q = 100.0;
	double diff = q - s_rx_display_quality;
	double alpha = (diff > 0.0) ? 0.25 : 0.12;
	s_rx_display_quality += alpha * diff;

	char buf[64];
	/* Approximate dBm from audio input level (no true RF RSSI on soundcard). */
	snprintf(buf, sizeof(buf), "%.0f dBm", -90.0 + level * 0.4);
	set_bar_pct(s_sig_rssi_bar, s_sig_rssi_lbl, level, buf);

	double sn = level * 0.35;
	snprintf(buf, sizeof(buf), "%.0f dB", sn);
	set_bar_pct(s_sig_sn_bar, s_sig_sn_lbl, (sn / 40.0) * 100.0, buf);

	double qual = s_rx_display_quality > 0.0 ? s_rx_display_quality : level;
	snprintf(buf, sizeof(buf), "%.0f%%", qual);
	set_bar_pct(s_sig_qual_bar, s_sig_qual_lbl, qual, buf);

	double ber = 0.0;
	if (iRX_LastBchCodewords > 0)
		ber = (100.0 * iRX_LastBchErrors) / (double)iRX_LastBchCodewords;
	snprintf(buf, sizeof(buf), "%.1f%%", ber);
	set_bar_pct(s_sig_ber_bar, s_sig_ber_lbl, ber > 100.0 ? 100.0 : ber, buf);

	snprintf(buf, sizeof(buf), "%d", nCount_Messages);
	set_bar_pct(s_dec_frames_bar, s_dec_frames_lbl, nCount_Messages > 0 ? 100.0 : 0.0, buf);

	int errors = nCount_Rejected + iRX_LastBchErrors;
	snprintf(buf, sizeof(buf), "%d", errors);
	set_bar_pct(s_dec_errors_bar, s_dec_errors_lbl, errors > 0 ? fmin(100.0, (double)errors) : 0.0, buf);

	int missed = nCount_Missed[0] + nCount_Missed[1];
	snprintf(buf, sizeof(buf), "%d", missed);
	set_bar_pct(s_dec_missed_bar, s_dec_missed_lbl, missed > 0 ? fmin(100.0, (double)missed) : 0.0, buf);

	/* Clean / corrupt among messages that actually got BCH stats. */
	extern int nCount_CleanRx, nCount_CorruptRx;
	int judged = nCount_CleanRx + nCount_CorruptRx;
	double rate = (judged > 0) ? (100.0 * nCount_CleanRx / (double)judged) : 0.0;
	snprintf(buf, sizeof(buf), "%.1f%%", rate);
	set_bar_pct(s_dec_rate_bar, s_dec_rate_lbl, rate, buf);
}

/* Timer: read level, smooth, refresh sidebar meters. */
static gboolean meter_cb(gpointer user_data)
{
	(void)user_data;
	if (s_quit) return G_SOURCE_REMOVE;
	double level = pdl_linux_get_input_level();
	if (level < 0.0) level = 0.0;
	if (level > 100.0) level = 100.0;
	s_meter_level = level;
	double diff = s_meter_level - s_meter_display_level;
	double alpha = (diff > 0.0) ? 0.72 : 0.38;
	s_meter_display_level += alpha * diff;
	update_rx_quality_display();
	if (Profile.pagercast_enabled && (s_pc_footer_led || s_pc_footer_label))
		pc_update_status_widgets(s_pc_footer_led, s_pc_footer_label);
	return G_SOURCE_CONTINUE;
}

static GtkWidget *meter_row(const char *name, GtkWidget **bar_out, GtkWidget **lbl_out)
{
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
	GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *lab = gtk_label_new(name);
	gtk_widget_set_halign(lab, GTK_ALIGN_START);
	gtk_widget_set_hexpand(lab, TRUE);
	gtk_style_context_add_class(gtk_widget_get_style_context(lab), "pdl-meter-label");
	GtkWidget *val = gtk_label_new("—");
	gtk_widget_set_halign(val, GTK_ALIGN_END);
	gtk_style_context_add_class(gtk_widget_get_style_context(val), "pdl-meter-val");
	gtk_box_pack_start(GTK_BOX(top), lab, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(top), val, FALSE, FALSE, 0);
	GtkWidget *bar = gtk_progress_bar_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(bar), "pdl-meter-bar");
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar), 0.0);
	gtk_widget_set_hexpand(bar, TRUE);
	gtk_box_pack_start(GTK_BOX(row), top, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), bar, FALSE, FALSE, 0);
	gtk_widget_set_hexpand(row, TRUE);
	gtk_widget_set_margin_bottom(row, 4);
	*bar_out = bar;
	*lbl_out = val;
	return row;
}

/* Sidebar section with padded title (avoids GtkFrame label flush on the border). */
static GtkWidget *sidebar_panel(const char *title, GtkWidget **body_out)
{
	GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(panel), "pdl-side-panel");
	GtkWidget *title_lab = gtk_label_new(title);
	gtk_widget_set_halign(title_lab, GTK_ALIGN_START);
	gtk_style_context_add_class(gtk_widget_get_style_context(title_lab), "pdl-side-panel-title");
	GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_style_context_add_class(gtk_widget_get_style_context(body), "pdl-side-panel-body");
	gtk_box_pack_start(GTK_BOX(panel), title_lab, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(panel), body, FALSE, FALSE, 0);
	*body_out = body;
	return panel;
}

static gboolean refresh_panes_idle(gpointer unused)
{
	(void)unused;
	if (s_quit) return G_SOURCE_REMOVE;
	if (s_pane1_text) refresh_pane(&Pane1, GTK_TEXT_VIEW(s_pane1_text), &s_last_pane1_bottom);
	if (s_pane2_text) refresh_pane(&Pane2, GTK_TEXT_VIEW(s_pane2_text), &s_last_pane2_bottom);
	return G_SOURCE_REMOVE;
}

static void schedule_pane_refresh(void)
{
	g_idle_add(refresh_panes_idle, NULL);
}

static gboolean refresh_cb(gpointer user_data)
{
	(void)user_data;
	if (s_quit) return G_SOURCE_REMOVE;
	if (s_pane1_text) refresh_pane(&Pane1, GTK_TEXT_VIEW(s_pane1_text), &s_last_pane1_bottom);
	if (s_pane2_text) refresh_pane(&Pane2, GTK_TEXT_VIEW(s_pane2_text), &s_last_pane2_bottom);
	if (s_footer_device_label) {
		char buf[MAX_DEVICE_DESC_LEN * 2 + 96];
		const char *pc_in = pdl_pagercast_input_label();
		const char *in = pc_in ? pc_in :
			(s_capture_device_desc[0] ? s_capture_device_desc : "Default");
		const char *out = s_playback_device_desc[0] ? s_playback_device_desc : "Default";
		snprintf(buf, sizeof(buf), "Input: %s    Output: %s", in, out);
		gtk_label_set_text(GTK_LABEL(s_footer_device_label), buf);
	}
	UpdateFilters();
	pdl_linux_gui_update_title();
	update_rx_quality_display();
	pdl_linux_gui_sync_pagercast_toggle();
	return G_SOURCE_CONTINUE;
}


static void on_destroy(GtkWidget *widget, gpointer data)
{
	(void)widget;
	(void)data;
	if (s_paned)
		Profile.percent = gtk_paned_get_position(GTK_PANED(s_paned));
	if (s_win) {
		gint x, y, w, h;
		gtk_window_get_position(GTK_WINDOW(s_win), &x, &y);
		gtk_window_get_size(GTK_WINDOW(s_win), &w, &h);
		Profile.xPos = x; Profile.yPos = y;
		Profile.xSize = w; Profile.ySize = h;
	}
	WriteSettings();
	UpdateFilters();
	CloseComPort();
	s_quit = 1;
	pdl_linux_alsa_stop();
	gtk_main_quit();
}

static gboolean on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	(void)widget; (void)event; (void)data;
	if (!Profile.confirmExit) return FALSE;
	gint r = pdl_confirm_dialog(GTK_WINDOW(s_win), "Exit", "Exit PDL?");
	return (r != GTK_RESPONSE_ACCEPT);
}

void pdl_linux_gui_quit(void)
{
	s_quit = 1;
}

static void tb_options_cb(GtkWidget *w, gpointer d) { (void)w; (void)d; on_options_clicked(); }
static void tb_filters_cb(GtkWidget *w, gpointer d) { (void)w; (void)d; on_filters_clicked(); }
static void tb_stats_cb(GtkWidget *w, gpointer d) { (void)w; (void)d; on_menu_activate(NULL, GINT_TO_POINTER(IDM_MONSTAT)); }
static void tb_about_cb(GtkWidget *w, gpointer d) { (void)w; (void)d; on_menu_activate(NULL, GINT_TO_POINTER(IDM_ABOUT)); }
static void tb_open_cb(GtkWidget *w, gpointer d) { (void)w; (void)d; on_menu_activate(NULL, GINT_TO_POINTER(IDM_LOGFILE)); }
static void tb_stop_cb(GtkWidget *w, gpointer d)
{
	(void)w; (void)d;
	pdl_pagercast_disconnect();
	pdl_apply_message_column_layout();
	pdl_linux_gui_sync_pagercast_toggle();
}
static void tb_clear_cb(GtkWidget *w, gpointer d)
{
	(void)w; (void)d;
	clear_pane1_cb(NULL);
	clear_pane2_cb(NULL);
}

static void pc_toggle_update_labels(int pagercast_on)
{
	if (!s_tb_pc_toggle || !GTK_IS_NOTEBOOK(s_tb_pc_toggle)) return;
	s_tb_pc_syncing = 1;
	gtk_notebook_set_current_page(GTK_NOTEBOOK(s_tb_pc_toggle), pagercast_on ? 1 : 0);
	s_tb_pc_syncing = 0;
	gtk_widget_set_tooltip_text(s_tb_pc_toggle,
		pagercast_on ? "Input: PagerCast stream (select Local for sound card)"
		             : "Input: Local audio (select PagerCast for radio stream)");
}

void pdl_linux_gui_sync_pagercast_toggle(void)
{
	int show = Profile.pagercast_enabled ? 1 : 0;
	if (!show && pdl_pagercast_is_wanted())
		pdl_pagercast_disconnect();
	if (s_tb_pc_toggle) {
		if (show) gtk_widget_show(s_tb_pc_toggle);
		else gtk_widget_hide(s_tb_pc_toggle);
	}
	if (s_tb_pc_sep) {
		if (show) gtk_widget_show(s_tb_pc_sep);
		else gtk_widget_hide(s_tb_pc_sep);
	}
	if (s_pc_footer_box) {
		if (show) gtk_widget_show(s_pc_footer_box);
		else gtk_widget_hide(s_pc_footer_box);
	}
	pdl_apply_message_column_layout();
	refresh_message_header();
	if (!show || !s_tb_pc_toggle) return;
	pc_toggle_update_labels(pdl_pagercast_is_wanted());
}

static void set_pagercast_source(int pagercast_on)
{
	if (s_tb_pc_syncing) return;
	if (!Profile.pagercast_enabled) {
		pc_toggle_update_labels(0);
		return;
	}
	if (pagercast_on) {
		if (!Profile.pagercast_frequency[0] || !Profile.pagercast_api_key[0]) {
			pc_toggle_update_labels(0);
			pdl_info_dialog(NULL, "PagerCast",
				"Configure frequency and API key in Settings → Integrations first.");
			return;
		}
		pc_toggle_update_labels(1);
		if (pdl_pagercast_connect() != 0)
			pc_toggle_update_labels(0);
	} else {
		pc_toggle_update_labels(0);
		pdl_pagercast_disconnect();
	}
	pdl_apply_message_column_layout();
	refresh_message_header();
}

static void on_tb_pc_switch_page(GtkNotebook *nb, GtkWidget *page, guint page_num, gpointer data)
{
	(void)nb; (void)page; (void)data;
	if (s_tb_pc_syncing) return;
	set_pagercast_source(page_num == 1 ? 1 : 0);
}

static gboolean on_pc_autostart(gpointer data)
{
	(void)data;
	/* Enable only shows the Local/PagerCast tabs — do not auto-connect. */
	pdl_linux_gui_sync_pagercast_toggle();
	return G_SOURCE_REMOVE;
}

int pdl_linux_gui_init(int *argc, char ***argv)
{
	/* Must be set before gtk_init — prefer Breeze over adw-gtk3 (combo halo). */
	g_setenv("GTK_THEME", "Emacs", TRUE);
	g_set_prgname("pdl");
	g_set_application_name("PDL");
	gdk_set_program_class("pdl");
	if (!gtk_init_check(argc, argv)) return -1;
	apply_app_theme();
	resolve_capture_desc();
	resolve_playback_desc();

	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	if (!win) return -1;
	s_win = win;
	ghWnd = (HWND)win;
	gtk_window_set_title(GTK_WINDOW(win), "PDL");
	pdl_linux_gui_apply_app_icon(GTK_WINDOW(win));
	{
		int ww = Profile.xSize > 100 ? Profile.xSize : 1000;
		int wh = Profile.ySize > 100 ? Profile.ySize : 700;
		gtk_window_set_default_size(GTK_WINDOW(win), ww, wh);
		if (Profile.xPos || Profile.yPos)
			gtk_window_move(GTK_WINDOW(win), Profile.xPos, Profile.yPos);
	}
	g_signal_connect(win, "destroy", G_CALLBACK(on_destroy), NULL);
	g_signal_connect(win, "delete-event", G_CALLBACK(on_delete_event), NULL);

	GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(win), main_box);
	gtk_style_context_add_class(gtk_widget_get_style_context(main_box), "pdl-main");

	GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_widget_set_size_request(toolbar, -1, 52);
	gtk_style_context_add_class(gtk_widget_get_style_context(toolbar), "pdl-toolbar");
	GtkWidget *tb_setup = toolbar_icon_button("preferences-system", "Settings", "Options");
	GtkWidget *tb_audio = toolbar_icon_button("audio-headphones", "Audio", "Sound / Input & Playback devices");
	GtkWidget *tb_filter = toolbar_icon_button("system-search", "Filters", "Filters");
	GtkWidget *tb_stats = toolbar_icon_button("view-list", "Stats", "Monitor statistics");
	GtkWidget *tb_clear1 = toolbar_icon_button("edit-clear", "Clear", "Clear messages");
	GtkWidget *tb_open = toolbar_icon_button("document-open", "Log", "Open log file");
	GtkWidget *tb_about = toolbar_icon_button("help-about", "About", "About PDL");
	g_signal_connect(tb_open, "clicked", G_CALLBACK(tb_open_cb), NULL);
	g_signal_connect(tb_setup, "clicked", G_CALLBACK(tb_options_cb), NULL);
	g_signal_connect(tb_audio, "clicked", G_CALLBACK(on_audio_clicked), NULL);
	g_signal_connect(tb_filter, "clicked", G_CALLBACK(tb_filters_cb), NULL);
	g_signal_connect(tb_stats, "clicked", G_CALLBACK(tb_stats_cb), NULL);
	g_signal_connect(tb_clear1, "clicked", G_CALLBACK(clear_pane1_cb), NULL);
	g_signal_connect(tb_about, "clicked", G_CALLBACK(tb_about_cb), NULL);
	gtk_box_pack_start(GTK_BOX(toolbar), tb_open, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(toolbar), tb_setup, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(toolbar), tb_audio, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(toolbar), tb_filter, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(toolbar), tb_stats, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(toolbar), tb_clear1, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(toolbar), tb_about, FALSE, FALSE, 2);

	/* Local | PagerCast notebook tabs — only shown when Integrations Enable is on. */
	GtkWidget *pc_tabs = gtk_notebook_new();
	s_tb_pc_toggle = pc_tabs;
	gtk_notebook_set_show_border(GTK_NOTEBOOK(pc_tabs), FALSE);
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(pc_tabs), FALSE);
	gtk_style_context_add_class(gtk_widget_get_style_context(pc_tabs), "pdl-pc-tabs");
	gtk_widget_set_valign(pc_tabs, GTK_ALIGN_CENTER);
	{
		GtkWidget *local_page = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		GtkWidget *remote_page = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_widget_set_size_request(local_page, 0, 0);
		gtk_widget_set_size_request(remote_page, 0, 0);
		gtk_notebook_append_page(GTK_NOTEBOOK(pc_tabs), local_page, gtk_label_new("Local"));
		gtk_notebook_append_page(GTK_NOTEBOOK(pc_tabs), remote_page, gtk_label_new("PagerCast"));
	}
	g_signal_connect(pc_tabs, "switch-page", G_CALLBACK(on_tb_pc_switch_page), NULL);
	s_tb_pc_sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
	gtk_box_pack_start(GTK_BOX(toolbar), s_tb_pc_sep, FALSE, FALSE, 6);
	gtk_box_pack_start(GTK_BOX(toolbar), pc_tabs, FALSE, FALSE, 2);
	if (!Profile.pagercast_enabled) {
		gtk_widget_hide(s_tb_pc_sep);
		gtk_widget_hide(pc_tabs);
	} else {
		pc_toggle_update_labels(pdl_pagercast_is_wanted() ? 1 : 0);
	}

	GtkWidget *menubar = gtk_menu_bar_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(menubar), "pdl-menubar");

	GtkWidget *file_menu = gtk_menu_new();
	GtkWidget *mi_file = gtk_menu_item_new_with_label("File");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi_file), file_menu);
	add_menu_item(file_menu, "Log File...", IDM_LOGFILE);
	add_menu_item(file_menu, "Exit", IDM_EXIT);

	GtkWidget *edit_menu = gtk_menu_new();
	GtkWidget *mi_edit = gtk_menu_item_new_with_label("Edit");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi_edit), edit_menu);
	add_menu_item(edit_menu, "Copy Selection", IDM_COPY_SELECTION);
	add_menu_item(edit_menu, "Copy Messages", IDM_COPY_UPPER);

	GtkWidget *interface_menu = gtk_menu_new();
	GtkWidget *mi_interface = gtk_menu_item_new_with_label("Interface");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi_interface), interface_menu);
	add_menu_item(interface_menu, "Setup...", IDM_INTERFACE);
	add_menu_item(interface_menu, "Volume / Audio...", IDM_VOLUME);

	GtkWidget *options_menu = gtk_menu_new();
	GtkWidget *mi_options = gtk_menu_item_new_with_label("Options");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi_options), options_menu);
	add_menu_item(options_menu, "Options...", IDM_OPTIONS);
	add_menu_item(options_menu, "General...", IDM_GENERAL);
	add_menu_item(options_menu, "Mail / SMTP...", IDM_MAIL);

	GtkWidget *filter_menu = gtk_menu_new();
	GtkWidget *mi_filters = gtk_menu_item_new_with_label("Filters");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi_filters), filter_menu);
	add_menu_item(filter_menu, "Filters...", IDM_FILTERS);
	add_menu_item(filter_menu, "Reload Filters", IDM_RELOAD);
	add_menu_item(filter_menu, "Reset Hitcounters", IDM_RESET_HITCOUNTERS);

	GtkWidget *monitor_menu = gtk_menu_new();
	GtkWidget *mi_monitor = gtk_menu_item_new_with_label("Monitor");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi_monitor), monitor_menu);
	s_mi_monitor[0] = gtk_radio_menu_item_new_with_label(NULL, "POCSAG");
	GSList *mgrp = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(s_mi_monitor[0]));
	s_mi_monitor[1] = gtk_radio_menu_item_new_with_label(mgrp, "ACARS");
	mgrp = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(s_mi_monitor[1]));
	s_mi_monitor[2] = gtk_radio_menu_item_new_with_label(mgrp, "MOBITEX");
	mgrp = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(s_mi_monitor[2]));
	s_mi_monitor[3] = gtk_radio_menu_item_new_with_label(mgrp, "ERMES");
	static const int mon_ids[] = { IDM_POCSAGFLEX, IDM_ACARS, IDM_MOBITEX, IDM_ERMES };
	for (int i = 0; i < 4; i++) {
		g_signal_connect(s_mi_monitor[i], "activate", G_CALLBACK(on_menu_activate), GINT_TO_POINTER(mon_ids[i]));
		gtk_menu_shell_append(GTK_MENU_SHELL(monitor_menu), s_mi_monitor[i]);
		if (i > 0)
			gtk_widget_set_sensitive(s_mi_monitor[i], FALSE);
	}
	add_menu_item(monitor_menu, "Statistics...", IDM_MONSTAT);
	sync_monitor_menu_checks();

	GtkWidget *display_menu = gtk_menu_new();
	GtkWidget *mi_display = gtk_menu_item_new_with_label("Display");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi_display), display_menu);
	add_menu_item(display_menu, "Clear Screen...", IDM_CLEARDISPLAY);
	add_menu_item(display_menu, "Colors...", IDM_COLOR);
	add_menu_item(display_menu, "Font / Scrollback...", IDM_SCREENOPTIONS);
	s_mi_tray = gtk_check_menu_item_new_with_label("System Tray");
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s_mi_tray), Profile.SystemTray ? TRUE : FALSE);
	g_signal_connect(s_mi_tray, "activate", G_CALLBACK(on_menu_activate), GINT_TO_POINTER(IDM_SYSTEMTRAY));
	gtk_menu_shell_append(GTK_MENU_SHELL(display_menu), s_mi_tray);

	GtkWidget *help_menu = gtk_menu_new();
	GtkWidget *mi_help = gtk_menu_item_new_with_label("Help");
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi_help), help_menu);
	add_menu_item(help_menu, "About...", IDM_ABOUT);

	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), mi_file);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), mi_edit);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), mi_interface);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), mi_options);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), mi_filters);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), mi_monitor);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), mi_display);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), mi_help);
	gtk_box_pack_start(GTK_BOX(main_box), menubar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(main_box), toolbar, FALSE, FALSE, 0);

	s_paned = NULL;

	GtkWidget *left_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	/* Single message list (Mockup-1): filter hits stay here and are highlighted. */
	GtkWidget *pane1_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(pane1_box), make_pane_title("Messages"), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(pane1_box), make_header_row(), FALSE, FALSE, 0);
	GtkWidget *sw1 = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw1), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand(sw1, TRUE);
	s_pane1_text = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(s_pane1_text), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(s_pane1_text), TRUE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(s_pane1_text), GTK_WRAP_NONE);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(s_pane1_text), 12);
	gtk_text_view_set_right_margin(GTK_TEXT_VIEW(s_pane1_text), 8);
	gtk_text_view_set_top_margin(GTK_TEXT_VIEW(s_pane1_text), 4);
	gtk_style_context_add_class(gtk_widget_get_style_context(s_pane1_text), "pdl-pane");
	gtk_container_add(GTK_CONTAINER(sw1), s_pane1_text);
	gtk_box_pack_start(GTK_BOX(pane1_box), sw1, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(left_col), pane1_box, TRUE, TRUE, 0);
	s_pane2_text = NULL;

	GtkWidget *content_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_hexpand(left_col, TRUE);
	gtk_widget_set_vexpand(left_col, TRUE);
	gtk_box_pack_start(GTK_BOX(content_hbox), left_col, TRUE, TRUE, 0);

	GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_style_context_add_class(gtk_widget_get_style_context(sidebar), "pdl-sidebar");
	/* Fixed right rail — progress bars hexpand and would otherwise steal width. */
	gtk_widget_set_size_request(sidebar, 200, -1);
	gtk_widget_set_hexpand(sidebar, FALSE);
	gtk_widget_set_vexpand(sidebar, TRUE);
	gtk_widget_set_halign(sidebar, GTK_ALIGN_FILL);

	GtkWidget *sig_box = NULL;
	GtkWidget *sig_panel = sidebar_panel("SIGNAL", &sig_box);
	gtk_box_pack_start(GTK_BOX(sig_box), meter_row("RSSI", &s_sig_rssi_bar, &s_sig_rssi_lbl), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sig_box), meter_row("S/N", &s_sig_sn_bar, &s_sig_sn_lbl), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sig_box), meter_row("Quality", &s_sig_qual_bar, &s_sig_qual_lbl), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sig_box), meter_row("BER", &s_sig_ber_bar, &s_sig_ber_lbl), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sidebar), sig_panel, FALSE, TRUE, 0);

	GtkWidget *rx_box = NULL;
	GtkWidget *rx_panel = sidebar_panel("DECODE", &rx_box);
	gtk_box_pack_start(GTK_BOX(rx_box), meter_row("Frames", &s_dec_frames_bar, &s_dec_frames_lbl), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(rx_box), meter_row("Errors", &s_dec_errors_bar, &s_dec_errors_lbl), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(rx_box), meter_row("Missed", &s_dec_missed_bar, &s_dec_missed_lbl), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(rx_box), meter_row("Rate", &s_dec_rate_bar, &s_dec_rate_lbl), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sidebar), rx_panel, FALSE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(content_hbox), sidebar, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(main_box), content_hbox, TRUE, TRUE, 0);

	/* Footer: selected audio input / output */
	GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_style_context_add_class(gtk_widget_get_style_context(footer), "pdl-footer");
	s_footer_device_label = gtk_label_new("Input: —    Output: —");
	gtk_label_set_xalign(GTK_LABEL(s_footer_device_label), 0.0f);
	gtk_label_set_ellipsize(GTK_LABEL(s_footer_device_label), PANGO_ELLIPSIZE_MIDDLE);
	gtk_widget_set_tooltip_text(s_footer_device_label, "Audio devices (change via Interface → Audio)");
	gtk_box_pack_start(GTK_BOX(footer), s_footer_device_label, TRUE, TRUE, 0);

	GtkWidget *pc_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	s_pc_footer_box = pc_box;
	gtk_widget_set_halign(pc_box, GTK_ALIGN_END);
	gtk_widget_set_margin_end(pc_box, 4);
	GtkWidget *pc_tag = gtk_label_new("PagerCast");
	gtk_style_context_add_class(gtk_widget_get_style_context(pc_tag), "pdl-footer");
	gtk_box_pack_start(GTK_BOX(pc_box), pc_tag, FALSE, FALSE, 0);
	s_pc_footer_led = gtk_drawing_area_new();
	gtk_widget_set_size_request(s_pc_footer_led, 14, 14);
	gtk_widget_set_valign(s_pc_footer_led, GTK_ALIGN_CENTER);
	g_signal_connect(s_pc_footer_led, "draw", G_CALLBACK(on_pc_led_draw), NULL);
	gtk_box_pack_start(GTK_BOX(pc_box), s_pc_footer_led, FALSE, FALSE, 0);
	s_pc_footer_label = gtk_label_new("Offline");
	gtk_label_set_xalign(GTK_LABEL(s_pc_footer_label), 0.0f);
	gtk_widget_set_tooltip_text(s_pc_footer_label, "PagerCast radio stream status");
	gtk_box_pack_start(GTK_BOX(pc_box), s_pc_footer_label, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(footer), pc_box, FALSE, FALSE, 0);
	pc_update_status_widgets(s_pc_footer_led, s_pc_footer_label);

	gtk_box_pack_start(GTK_BOX(main_box), footer, FALSE, FALSE, 0);

	gtk_widget_show_all(win);
	pdl_linux_gui_sync_pagercast_toggle();
	gtk_window_present(GTK_WINDOW(win));
	pdl_linux_gui_update_title();
	update_rx_quality_display();
	if (Profile.SystemTray) SystemTrayWindow(false);
	pdl_platform_register_pane_refresh_cb(schedule_pane_refresh);
	g_timeout_add(50, meter_cb, NULL);
	g_timeout_add(150, refresh_cb, NULL);
	/* Auto-connect after the UI is up if Enable was saved. */
	g_idle_add(on_pc_autostart, NULL);
	return 0;
}


void pdl_linux_gui_run(void)
{
	gtk_main();
}

#endif
