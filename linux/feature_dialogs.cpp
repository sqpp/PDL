/*
 * Extra PDL dialogs for Linux: Mail, General, Display.
 */
#ifdef __linux__
#include "platform/pdl_linux_types.h"
#include "Headers/pdl.h"
#include "utils/smtp.h"
#include "linux/gui_gtk.h"
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern void WriteSettings(void);
extern void pdl_linux_init_panes(void);

static GtkWidget *form_row(GtkWidget *box, const char *label, GtkWidget *field)
{
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *lab = gtk_label_new(label);
	gtk_widget_set_halign(lab, GTK_ALIGN_END);
	gtk_widget_set_size_request(lab, 120, -1);
	gtk_box_pack_start(GTK_BOX(row), lab, FALSE, FALSE, 0);
	gtk_widget_set_hexpand(field, TRUE);
	gtk_box_pack_start(GTK_BOX(row), field, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
	return field;
}

static void set_rgba_from_colorref(GdkRGBA *rgba, COLORREF c)
{
	rgba->red   = ((c >> 0) & 0xFF) / 255.0;
	rgba->green = ((c >> 8) & 0xFF) / 255.0;
	rgba->blue  = ((c >> 16) & 0xFF) / 255.0;
	rgba->alpha = 1.0;
}

static COLORREF colorref_from_rgba(const GdkRGBA *rgba)
{
	int r = (int)(rgba->red * 255.0 + 0.5);
	int g = (int)(rgba->green * 255.0 + 0.5);
	int b = (int)(rgba->blue * 255.0 + 0.5);
	if (r < 0) r = 0; if (r > 255) r = 255;
	if (g < 0) g = 0; if (g > 255) g = 255;
	if (b < 0) b = 0; if (b > 255) b = 255;
	return (COLORREF)(r | (g << 8) | (b << 16));
}

void pdl_linux_mail_dialog(GtkWindow *parent)
{
	GtkWidget *dlg = gtk_dialog_new_with_buttons("Mail / SMTP", parent,
		(GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
		"Cancel", GTK_RESPONSE_REJECT, "OK", GTK_RESPONSE_ACCEPT, NULL);
	pdl_linux_gui_prepare_dialog(dlg);
	gtk_window_set_default_size(GTK_WINDOW(dlg), 480, 420);
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
	gtk_container_set_border_width(GTK_CONTAINER(content), 8);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_box_pack_start(GTK_BOX(content), box, TRUE, TRUE, 0);

	GtkWidget *ch_enable = gtk_check_button_new_with_label("Enable SMTP email");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_enable),
		(Profile.SMTP || (Profile.nMailOptions & MAIL_OPTION_ENABLE)) ? TRUE : FALSE);
	gtk_box_pack_start(GTK_BOX(box), ch_enable, FALSE, FALSE, 0);

	GtkWidget *e_host = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(e_host), Profile.szMailHost);
	form_row(box, "SMTP host:", e_host);

	char port_buf[16];
	snprintf(port_buf, sizeof(port_buf), "%d", Profile.iMailPort > 0 ? Profile.iMailPort : 25);
	GtkWidget *e_port = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(e_port), port_buf);
	gtk_entry_set_width_chars(GTK_ENTRY(e_port), 6);
	form_row(box, "Port:", e_port);

	GtkWidget *e_helo = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(e_helo),
		Profile.szMailHeloDomain[0] ? Profile.szMailHeloDomain : "localhost");
	form_row(box, "HELO domain:", e_helo);

	GtkWidget *e_from = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(e_from), Profile.szMailFrom);
	form_row(box, "From:", e_from);

	GtkWidget *e_to = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(e_to), Profile.szMailTo);
	form_row(box, "To:", e_to);

	GtkWidget *ch_auth = gtk_check_button_new_with_label("Use AUTH LOGIN");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_auth),
		(Profile.nMailOptions & MAIL_OPTION_AUTH) ? TRUE : FALSE);
	gtk_box_pack_start(GTK_BOX(box), ch_auth, FALSE, FALSE, 0);

	GtkWidget *ch_ssl = gtk_check_button_new_with_label("Use SSL/TLS (STARTTLS; port 465 = implicit TLS)");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_ssl),
		(Profile.ssl || (Profile.nMailOptions & MAIL_OPTION_SSL)) ? TRUE : FALSE);
	gtk_box_pack_start(GTK_BOX(box), ch_ssl, FALSE, FALSE, 0);

	GtkWidget *e_user = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(e_user), Profile.szMailUser);
	form_row(box, "Username:", e_user);

	GtkWidget *e_pass = gtk_entry_new();
	gtk_entry_set_visibility(GTK_ENTRY(e_pass), FALSE);
	gtk_entry_set_text(GTK_ENTRY(e_pass), Profile.szMailPassword);
	form_row(box, "Password:", e_pass);

	GtkWidget *mode_combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "All messages");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "Filter matches only");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "Monitor-only matches");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "Selectable (per filter SMTP)");
	{
		int mode = Profile.nMailOptions & MAIL_OPTION_MODES;
		int idx = 0;
		if (mode == MAIL_OPTION_MODE_FILTER) idx = 1;
		else if (mode == MAIL_OPTION_MODE_MONITOR) idx = 2;
		else if (mode == MAIL_OPTION_MODE_SELECTABLE) idx = 3;
		gtk_combo_box_set_active(GTK_COMBO_BOX(mode_combo), idx);
	}
	form_row(box, "Send mode:", mode_combo);

	GtkWidget *note = gtk_label_new(
		"With SSL enabled: port 465 uses implicit TLS; other ports use STARTTLS after EHLO.");
	gtk_widget_set_halign(note, GTK_ALIGN_START);
	gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
	gtk_box_pack_start(GTK_BOX(box), note, FALSE, FALSE, 0);

	gtk_widget_show_all(dlg);
	if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
		Profile.SMTP = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_enable)) ? 1 : 0;
		strncpy(Profile.szMailHost, gtk_entry_get_text(GTK_ENTRY(e_host)), sizeof(Profile.szMailHost) - 1);
		strncpy(Profile.szMailHeloDomain, gtk_entry_get_text(GTK_ENTRY(e_helo)), sizeof(Profile.szMailHeloDomain) - 1);
		strncpy(Profile.szMailFrom, gtk_entry_get_text(GTK_ENTRY(e_from)), sizeof(Profile.szMailFrom) - 1);
		strncpy(Profile.szMailTo, gtk_entry_get_text(GTK_ENTRY(e_to)), sizeof(Profile.szMailTo) - 1);
		strncpy(Profile.szMailUser, gtk_entry_get_text(GTK_ENTRY(e_user)), sizeof(Profile.szMailUser) - 1);
		strncpy(Profile.szMailPassword, gtk_entry_get_text(GTK_ENTRY(e_pass)), sizeof(Profile.szMailPassword) - 1);
		int port = 25;
		sscanf(gtk_entry_get_text(GTK_ENTRY(e_port)), "%d", &port);
		Profile.iMailPort = port > 0 ? port : 25;

		int opts = Profile.nMailOptions & ~(MAIL_OPTION_MODES | MAIL_OPTION_AUTH | MAIL_OPTION_ENABLE | MAIL_OPTION_SSL);
		gint mi = gtk_combo_box_get_active(GTK_COMBO_BOX(mode_combo));
		if (mi == 1) opts |= MAIL_OPTION_MODE_FILTER;
		else if (mi == 2) opts |= MAIL_OPTION_MODE_MONITOR;
		else if (mi == 3) opts |= MAIL_OPTION_MODE_SELECTABLE;
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_auth)))
			opts |= MAIL_OPTION_AUTH;
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_ssl))) {
			opts |= MAIL_OPTION_SSL;
			Profile.ssl = true;
		} else {
			Profile.ssl = false;
		}
		if (Profile.SMTP)
			opts |= MAIL_OPTION_ENABLE;
		Profile.nMailOptions = opts;

		MailInit(Profile.szMailHost, Profile.szMailHeloDomain, Profile.szMailFrom,
			Profile.szMailTo, Profile.szMailUser, Profile.szMailPassword,
			Profile.iMailPort, Profile.nMailOptions);
		WriteSettings();
	}
	gtk_widget_destroy(dlg);
}

void pdl_linux_general_dialog(GtkWindow *parent)
{
	GtkWidget *dlg = gtk_dialog_new_with_buttons("General Options", parent,
		(GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
		"Cancel", GTK_RESPONSE_REJECT, "OK", GTK_RESPONSE_ACCEPT, NULL);
	gtk_window_set_default_size(GTK_WINDOW(dlg), 420, 360);
	pdl_linux_gui_prepare_dialog(dlg);
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
	gtk_container_set_border_width(GTK_CONTAINER(content), 8);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_box_pack_start(GTK_BOX(content), box, TRUE, TRUE, 0);

	GtkWidget *ch_tone = gtk_check_button_new_with_label("Show tone-only messages");
	GtkWidget *ch_num = gtk_check_button_new_with_label("Show numeric messages");
	GtkWidget *ch_misc = gtk_check_button_new_with_label("Show miscellaneous messages");
	GtkWidget *ch_beep = gtk_check_button_new_with_label("Beep on filter match");
	GtkWidget *ch_dup = gtk_check_button_new_with_label("Block duplicate messages");
	GtkWidget *ch_tray = gtk_check_button_new_with_label("Enable system tray icon");
	GtkWidget *ch_tray_restore = gtk_check_button_new_with_label("Restore from tray on activate");
	GtkWidget *ch_usa = gtk_check_button_new_with_label("US date format (MM-DD-YY)");
	GtkWidget *ch_confirm = gtk_check_button_new_with_label("Confirm before exit");
	GtkWidget *ch_invert = gtk_check_button_new_with_label("Invert audio / data polarity");

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_tone), Profile.showtone ? TRUE : FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_num), Profile.shownumeric ? TRUE : FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_misc), Profile.showmisc ? TRUE : FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_beep), Profile.filterbeep ? TRUE : FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_dup), Profile.BlockDuplicate ? TRUE : FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_tray), Profile.SystemTray ? TRUE : FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_tray_restore), Profile.SystemTrayRestore ? TRUE : FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_usa), Profile.Date_USA ? TRUE : FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_confirm), Profile.confirmExit ? TRUE : FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ch_invert), Profile.invert ? TRUE : FALSE);

	gtk_box_pack_start(GTK_BOX(box), ch_tone, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), ch_num, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), ch_misc, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);
	gtk_box_pack_start(GTK_BOX(box), ch_beep, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), ch_dup, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);
	gtk_box_pack_start(GTK_BOX(box), ch_tray, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), ch_tray_restore, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), ch_usa, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), ch_confirm, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), ch_invert, FALSE, FALSE, 0);

	gtk_widget_show_all(dlg);
	if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
		Profile.showtone = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_tone)) ? 1 : 0;
		Profile.shownumeric = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_num)) ? 1 : 0;
		Profile.showmisc = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_misc)) ? 1 : 0;
		Profile.filterbeep = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_beep)) ? 1 : 0;
		Profile.BlockDuplicate = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_dup)) ? 0x02 : 0; /* BLOCK_ONLYMSG */
		int tray = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_tray)) ? 1 : 0;
		Profile.SystemTrayRestore = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_tray_restore)) ? 1 : 0;
		Profile.Date_USA = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_usa)) ? 1 : 0;
		Profile.confirmExit = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_confirm)) ? 1 : 0;
		Profile.invert = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ch_invert)) ? 1 : 0;
		Profile.SystemTray = tray;
		extern void SystemTrayIcon(bool bRemoveIcon);
		SystemTrayIcon(!tray);
		if (tray) SystemTrayWindow(false);
		WriteSettings();
	}
	gtk_widget_destroy(dlg);
}

void pdl_linux_display_dialog(GtkWindow *parent)
{
	GtkWidget *dlg = gtk_dialog_new_with_buttons("Display Options", parent,
		(GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
		"Cancel", GTK_RESPONSE_REJECT, "OK", GTK_RESPONSE_ACCEPT, NULL);
	gtk_window_set_default_size(GTK_WINDOW(dlg), 420, 220);
	pdl_linux_gui_prepare_dialog(dlg);
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
	gtk_container_set_border_width(GTK_CONTAINER(content), 8);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_box_pack_start(GTK_BOX(content), box, TRUE, TRUE, 0);

	int font_pt = Profile.fontInfo.lfHeight;
	if (font_pt < 0) font_pt = -font_pt;
	if (font_pt < 8 || font_pt > 36) font_pt = 11;
	GtkWidget *spin_font = gtk_spin_button_new_with_range(8, 28, 1);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_font), font_pt);
	form_row(box, "Font size:", spin_font);

	GtkWidget *spin_p1 = gtk_spin_button_new_with_range(50, 5000, 50);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_p1),
		Profile.pane1_size > 0 ? Profile.pane1_size : 500);
	form_row(box, "Upper scrollback:", spin_p1);

	GtkWidget *spin_p2 = gtk_spin_button_new_with_range(50, 5000, 50);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_p2),
		Profile.pane2_size > 0 ? Profile.pane2_size : 500);
	form_row(box, "Lower scrollback:", spin_p2);

	GtkWidget *note = gtk_label_new("Use Display → Colors… for message item colors.");
	gtk_widget_set_halign(note, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(box), note, FALSE, FALSE, 0);

	gtk_widget_show_all(dlg);
	if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
		int pt = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_font));
		Profile.fontInfo.lfHeight = pt;
		int p1 = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_p1));
		int p2 = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_p2));
		int need_reinit = (p1 != Profile.pane1_size || p2 != Profile.pane2_size);
		Profile.pane1_size = p1;
		Profile.pane2_size = p2;
		if (need_reinit)
			pdl_linux_init_panes();
		pdl_linux_gui_apply_display_style();
		WriteSettings();
	}
	gtk_widget_destroy(dlg);
}

static GtkWidget *color_btn_row(GtkWidget *grid, int row, int col, const char *label, COLORREF *cref)
{
	GtkWidget *lab = gtk_label_new(label);
	gtk_widget_set_halign(lab, GTK_ALIGN_START);
	gtk_grid_attach(GTK_GRID(grid), lab, col * 2, row, 1, 1);
	GdkRGBA rgba;
	set_rgba_from_colorref(&rgba, *cref);
	GtkWidget *btn = gtk_color_button_new_with_rgba(&rgba);
	gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(btn), FALSE);
	gtk_grid_attach(GTK_GRID(grid), btn, col * 2 + 1, row, 1, 1);
	g_object_set_data(G_OBJECT(btn), "pdl-cref", cref);
	return btn;
}

static void color_btn_apply(GtkWidget *btn)
{
	COLORREF *cref = (COLORREF *)g_object_get_data(G_OBJECT(btn), "pdl-cref");
	if (!cref) return;
	GdkRGBA rgba;
	gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(btn), &rgba);
	*cref = colorref_from_rgba(&rgba);
}

typedef struct {
	GtkTextBuffer *buf;
	COLORREF *bg, *addr, *time, *mode, *numeric, *msg, *misc, *err, *filt;
} ColorPreviewCtx;

static void color_preview_insert(GtkTextBuffer *buf, GtkTextIter *it,
	const char *tag, const char *text)
{
	gtk_text_buffer_insert_with_tags_by_name(buf, it, text, -1, tag, NULL);
}

static void colors_refresh_preview(ColorPreviewCtx *ctx)
{
	if (!ctx || !ctx->buf) return;
	GtkTextBuffer *buf = ctx->buf;
	GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buf);

	struct { const char *name; COLORREF *c; } tags[] = {
		{ "addr", ctx->addr }, { "time", ctx->time }, { "mode", ctx->mode },
		{ "numeric", ctx->numeric }, { "msg", ctx->msg }, { "misc", ctx->misc },
		{ "err", ctx->err }, { "filt", ctx->filt },
	};
	for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
		GtkTextTag *tag = gtk_text_tag_table_lookup(table, tags[i].name);
		if (!tag) tag = gtk_text_buffer_create_tag(buf, tags[i].name, NULL);
		GdkRGBA rgba;
		set_rgba_from_colorref(&rgba, *tags[i].c);
		g_object_set(tag, "foreground-rgba", &rgba, NULL);
	}

	GdkRGBA bg;
	set_rgba_from_colorref(&bg, *ctx->bg);
	GtkTextTag *bg_tag = gtk_text_tag_table_lookup(table, "bg");
	if (!bg_tag)
		bg_tag = gtk_text_buffer_create_tag(buf, "bg", NULL);
	g_object_set(bg_tag, "paragraph-background-rgba", &bg, NULL);

	gtk_text_buffer_set_text(buf, "", 0);
	GtkTextIter it;
	gtk_text_buffer_get_start_iter(buf, &it);

	/* Match original ColorWndProc sample lines. */
	color_preview_insert(buf, &it, "addr", " 1234567");
	color_preview_insert(buf, &it, "time", "  14:13:33 17-08-00");
	color_preview_insert(buf, &it, "mode", "  POCSAG-1   512");
	color_preview_insert(buf, &it, "numeric", "  TONE-ONLY / 0123456789");
	gtk_text_buffer_insert(buf, &it, "\n", 1);

	color_preview_insert(buf, &it, "addr", " 7654321");
	color_preview_insert(buf, &it, "time", "  15:14:40 18-08-00");
	color_preview_insert(buf, &it, "mode", "  POCSAG-2  1200");
	color_preview_insert(buf, &it, "msg", "  Alphanumeric");
	gtk_text_buffer_insert(buf, &it, "\n", 1);

	color_preview_insert(buf, &it, "addr", " 1234567");
	color_preview_insert(buf, &it, "time", "  16:15:20 19-08-00");
	color_preview_insert(buf, &it, "mode", "  POCSAG-3  2400");
	color_preview_insert(buf, &it, "msg", "  Bit");
	color_preview_insert(buf, &it, "err", "errors");
	gtk_text_buffer_insert(buf, &it, "\n", 1);

	color_preview_insert(buf, &it, "addr", " 7654321");
	color_preview_insert(buf, &it, "time", "  17:12:14 20-08-00");
	color_preview_insert(buf, &it, "mode", "  FLEX-A    1600");
	color_preview_insert(buf, &it, "msg", "  Text");
	color_preview_insert(buf, &it, "filt", " match");
	gtk_text_buffer_insert(buf, &it, "\n", 1);

	color_preview_insert(buf, &it, "addr", " 1234567");
	color_preview_insert(buf, &it, "time", "  20:20:14 23-08-00");
	color_preview_insert(buf, &it, "mode", "  FLEX-B    3200");
	color_preview_insert(buf, &it, "misc", "  FLEX Binary");
	gtk_text_buffer_insert(buf, &it, "\n", 1);

	color_preview_insert(buf, &it, "addr", " 7654321");
	color_preview_insert(buf, &it, "time", "  19:40:21 22-08-00");
	color_preview_insert(buf, &it, "mode", "  FLEX-C    6400");
	color_preview_insert(buf, &it, "misc", "  TEMPORARY ADDRESS: 2029568");

	GtkTextIter start, end;
	gtk_text_buffer_get_bounds(buf, &start, &end);
	gtk_text_buffer_apply_tag_by_name(buf, "bg", &start, &end);
}

static void on_color_btn_changed(GtkColorButton *btn, gpointer user_data)
{
	ColorPreviewCtx *ctx = (ColorPreviewCtx *)user_data;
	color_btn_apply(GTK_WIDGET(btn));
	colors_refresh_preview(ctx);
}

void pdl_linux_colors_dialog(GtkWindow *parent)
{
	/* Match original COLORSDLGBOX field set for paging mode. */
	COLORREF tmp_background = Profile.color_background;
	COLORREF tmp_address = Profile.color_address;
	COLORREF tmp_timestamp = Profile.color_timestamp;
	COLORREF tmp_modetypebit = Profile.color_modetypebit;
	COLORREF tmp_numeric = Profile.color_numeric;
	COLORREF tmp_message = Profile.color_message;
	COLORREF tmp_misc = Profile.color_misc;
	COLORREF tmp_biterrors = Profile.color_biterrors;
	COLORREF tmp_filtermatch = Profile.color_filtermatch;

	GtkWidget *dlg = gtk_dialog_new_with_buttons("Colors", parent,
		(GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
		"Cancel", GTK_RESPONSE_REJECT,
		"Default Colors", 2,
		"OK", GTK_RESPONSE_ACCEPT, NULL);
	pdl_linux_gui_prepare_dialog(dlg);
	gtk_window_set_default_size(GTK_WINDOW(dlg), 640, 420);
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
	gtk_container_set_border_width(GTK_CONTAINER(content), 8);

	GtkWidget *grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	gtk_box_pack_start(GTK_BOX(content), grid, FALSE, FALSE, 0);

	GtkWidget *b_addr = color_btn_row(grid, 0, 0, "Address", &tmp_address);
	GtkWidget *b_time = color_btn_row(grid, 0, 1, "Time/Date", &tmp_timestamp);
	GtkWidget *b_phase = color_btn_row(grid, 1, 0, "Phase/Function", &tmp_modetypebit);
	GtkWidget *b_num = color_btn_row(grid, 1, 1, "Numeric/Tone", &tmp_numeric);
	GtkWidget *b_msg = color_btn_row(grid, 2, 0, "Message", &tmp_message);
	GtkWidget *b_bin = color_btn_row(grid, 2, 1, "FLEX Binary", &tmp_misc);
	GtkWidget *b_bg = color_btn_row(grid, 3, 0, "Background", &tmp_background);
	GtkWidget *b_err = color_btn_row(grid, 3, 1, "Bit Errors", &tmp_biterrors);
	GtkWidget *b_filt = color_btn_row(grid, 4, 0, "Filter Match", &tmp_filtermatch);

	GtkWidget *preview_frame = gtk_frame_new("Preview");
	gtk_widget_set_margin_top(preview_frame, 12);
	gtk_box_pack_start(GTK_BOX(content), preview_frame, TRUE, TRUE, 0);
	GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(preview_frame), scrolled);
	GtkWidget *preview_view = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(preview_view), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(preview_view), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(preview_view), TRUE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(preview_view), GTK_WRAP_NONE);
	gtk_container_add(GTK_CONTAINER(scrolled), preview_view);

	ColorPreviewCtx preview_ctx = {
		gtk_text_view_get_buffer(GTK_TEXT_VIEW(preview_view)),
		&tmp_background, &tmp_address, &tmp_timestamp, &tmp_modetypebit,
		&tmp_numeric, &tmp_message, &tmp_misc, &tmp_biterrors, &tmp_filtermatch
	};
	GtkWidget *btns[] = { b_addr, b_time, b_phase, b_num, b_msg, b_bin, b_bg, b_err, b_filt };
	for (size_t i = 0; i < sizeof(btns) / sizeof(btns[0]); i++)
		g_signal_connect(btns[i], "color-set", G_CALLBACK(on_color_btn_changed), &preview_ctx);
	colors_refresh_preview(&preview_ctx);

	gtk_widget_show_all(dlg);

	for (;;) {
		gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
		if (resp == GTK_RESPONSE_REJECT || resp == GTK_RESPONSE_DELETE_EVENT)
			break;
		if (resp == 2) {
			/* Defaults from original PDL.cpp */
			extern DWORD rgbColor[17][3];
			tmp_background = RGB(rgbColor[0][0], rgbColor[0][1], rgbColor[0][2]); /* BLACK */
			tmp_address = RGB(255, 255, 255);
			tmp_modetypebit = RGB(255, 0, 0);
			tmp_timestamp = RGB(0, 0, 255);
			tmp_numeric = RGB(255, 0, 0);
			tmp_message = RGB(0, 255, 255);
			tmp_misc = RGB(128, 128, 64);
			tmp_biterrors = RGB(192, 192, 192);
			tmp_filtermatch = RGB(0, 255, 0);
			GdkRGBA rgba;
			set_rgba_from_colorref(&rgba, tmp_address); gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(b_addr), &rgba);
			set_rgba_from_colorref(&rgba, tmp_timestamp); gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(b_time), &rgba);
			set_rgba_from_colorref(&rgba, tmp_modetypebit); gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(b_phase), &rgba);
			set_rgba_from_colorref(&rgba, tmp_numeric); gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(b_num), &rgba);
			set_rgba_from_colorref(&rgba, tmp_message); gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(b_msg), &rgba);
			set_rgba_from_colorref(&rgba, tmp_misc); gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(b_bin), &rgba);
			set_rgba_from_colorref(&rgba, tmp_background); gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(b_bg), &rgba);
			set_rgba_from_colorref(&rgba, tmp_biterrors); gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(b_err), &rgba);
			set_rgba_from_colorref(&rgba, tmp_filtermatch); gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(b_filt), &rgba);
			colors_refresh_preview(&preview_ctx);
			continue;
		}
		if (resp == GTK_RESPONSE_ACCEPT) {
			color_btn_apply(b_addr);
			color_btn_apply(b_time);
			color_btn_apply(b_phase);
			color_btn_apply(b_num);
			color_btn_apply(b_msg);
			color_btn_apply(b_bin);
			color_btn_apply(b_bg);
			color_btn_apply(b_err);
			color_btn_apply(b_filt);
			Profile.color_background = tmp_background;
			Profile.color_address = tmp_address;
			Profile.color_timestamp = tmp_timestamp;
			Profile.color_modetypebit = tmp_modetypebit;
			Profile.color_numeric = tmp_numeric;
			Profile.color_message = tmp_message;
			Profile.color_misc = tmp_misc;
			Profile.color_biterrors = tmp_biterrors;
			Profile.color_filtermatch = tmp_filtermatch;
			Profile.color_instructions = tmp_message;
			pdl_linux_gui_apply_display_style();
			WriteSettings();
			break;
		}
	}
	gtk_widget_destroy(dlg);
}

#endif
