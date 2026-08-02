/*
 * Modern WebKit UI shell for PDL Linux.
 * Decoder/audio unchanged — registers the same pane refresh callback as classic GTK.
 */
#ifdef __linux__
/* WebKit/JSC pull C++ std headers — include them BEFORE Win32-compat min/max macros. */
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include "linux/gui_web.h"
#include "linux/gui_gtk.h"
#include "linux/pagercast_stream.h"
#include "platform/pdl_platform.h"
#include "Headers/pdl.h"
#include "Headers/misc.h"
#include "Headers/sound_in.h"
#include "Headers/decode.h"
#include "Headers/initapp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <unistd.h>
#include <libgen.h>

/* Prefer std::min/max if anything still needs them after our macros. */
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

extern PROFILE Profile;
extern PaneStruct Pane1, Pane2;
extern char szWindowText[6][1000];
extern char *pdl_version;
extern double dRX_MessageQuality;
extern int iRX_LastBchErrors;
extern int iRX_LastBchCodewords;
extern bool bRX_MessageQuality_Valid;
extern void WriteSettings(void);
extern void ClearPanes(bool bPane1, bool bPane2);
extern void Reset_ATB(void);

static GtkWidget *s_win;
static WebKitWebView *s_view;
static unsigned int s_last_pane1_bottom = (unsigned)-1;
static unsigned int s_last_pane2_bottom = (unsigned)-1;
static int s_ready;
static char s_webui_dir[PATH_MAX];

static char *json_escape(const char *s, int len)
{
	if (!s) s = "";
	if (len < 0) len = (int)strlen(s);
	GString *g = g_string_sized_new((size_t)len * 2 + 8);
	for (int i = 0; i < len; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c == '"' || c == '\\')
			g_string_append_c(g, '\\'), g_string_append_c(g, (char)c);
		else if (c == '\n')
			g_string_append(g, "\\n");
		else if (c == '\r')
			g_string_append(g, "\\r");
		else if (c == '\t')
			g_string_append(g, "\\t");
		else if (c < 0x20)
			g_string_append_printf(g, "\\u%04x", c);
		else if (c <= 0x7F)
			g_string_append_c(g, (char)c);
		else {
			/* Latin-1 → UTF-8 */
			g_string_append_c(g, (char)(0xC0u | (c >> 6)));
			g_string_append_c(g, (char)(0x80u | (c & 0x3F)));
		}
	}
	return g_string_free(g, FALSE);
}

static void run_js(const char *script)
{
	if (!s_view || !script) return;
#if WEBKIT_CHECK_VERSION(2, 40, 0)
	webkit_web_view_evaluate_javascript(s_view, script, -1, NULL, NULL, NULL, NULL, NULL);
#else
	webkit_web_view_run_javascript(s_view, script, NULL, NULL, NULL);
#endif
}

static void field_slice(const char *line, int len, int start, int end, char *out, size_t outlen)
{
	if (start < 0) start = 0;
	if (end > len) end = len;
	if (end < start) end = start;
	int ncopy = end - start;
	if (ncopy < 0) ncopy = 0;
	if ((size_t)ncopy >= outlen) ncopy = (int)outlen - 1;
	memcpy(out, line + start, (size_t)ncopy);
	out[ncopy] = '\0';
	char *s = out;
	while (*s == ' ') s++;
	if (s != out) memmove(out, s, strlen(s) + 1);
	size_t L = strlen(out);
	while (L > 0 && out[L - 1] == ' ') out[--L] = '\0';
}

static void append_pane_lines_json(GString *g, PaneStruct *pane, unsigned int *p_last)
{
	extern int iItemPositions[9];
	if (!pane || !pane->buff_char || !p_last) {
		g_string_append(g, "[]");
		return;
	}
	unsigned int n = pane->Bottom;
	if (n > pane->buff_lines) n = pane->buff_lines;
	*p_last = n;
	g_string_append_c(g, '[');
	int first = 1;
	int pc_layout = 0;
	for (int ci = 0; ci < 8; ci++) {
		if (Profile.ScreenColumns[ci] == MSG_PHONE) { pc_layout = 1; break; }
		if (Profile.ScreenColumns[ci] == 0) break;
	}
	for (unsigned int i = n; i > 0; i--) {
		unsigned int idx = i - 1;
		const char *line = &pane->buff_char[idx * (LINE_SIZE + 1)];
		int len = 0;
		while (len < LINE_SIZE && line[len] && line[len] != '\r' && line[len] != '\n')
			len++;
		while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t'))
			len--;
		if (len == 0) continue;

		char addr[32], phone[32], timeb[32], dateb[32], mode[32], type[32], br[32], msg[LINE_SIZE + 1];
		phone[0] = '\0';
		if (pc_layout) {
			field_slice(line, len, iItemPositions[MSG_CAPCODE], iItemPositions[MSG_PHONE], addr, sizeof(addr));
			field_slice(line, len, iItemPositions[MSG_PHONE], iItemPositions[MSG_TIME], phone, sizeof(phone));
			field_slice(line, len, iItemPositions[MSG_TIME], iItemPositions[MSG_DATE], timeb, sizeof(timeb));
			field_slice(line, len, iItemPositions[MSG_DATE], iItemPositions[MSG_MODE], dateb, sizeof(dateb));
			field_slice(line, len, iItemPositions[MSG_MODE], iItemPositions[MSG_TYPE], mode, sizeof(mode));
			field_slice(line, len, iItemPositions[MSG_TYPE], iItemPositions[MSG_BITRATE], type, sizeof(type));
			field_slice(line, len, iItemPositions[MSG_BITRATE], iItemPositions[MSG_MESSAGE], br, sizeof(br));
			field_slice(line, len, iItemPositions[MSG_MESSAGE], len, msg, sizeof(msg));
		} else {
			field_slice(line, len, iItemPositions[MSG_CAPCODE], iItemPositions[MSG_TIME], addr, sizeof(addr));
			field_slice(line, len, iItemPositions[MSG_TIME], iItemPositions[MSG_DATE], timeb, sizeof(timeb));
			field_slice(line, len, iItemPositions[MSG_DATE], iItemPositions[MSG_MODE], dateb, sizeof(dateb));
			field_slice(line, len, iItemPositions[MSG_MODE], iItemPositions[MSG_TYPE], mode, sizeof(mode));
			field_slice(line, len, iItemPositions[MSG_TYPE], iItemPositions[MSG_BITRATE], type, sizeof(type));
			field_slice(line, len, iItemPositions[MSG_BITRATE], iItemPositions[MSG_MESSAGE], br, sizeof(br));
			field_slice(line, len, iItemPositions[MSG_MESSAGE], len, msg, sizeof(msg));
		}
		if (!addr[0] && !msg[0] && !timeb[0]) continue;

		char *eaddr = json_escape(addr, -1);
		char *ephone = json_escape(phone, -1);
		char *etime = json_escape(timeb, -1);
		char *edate = json_escape(dateb, -1);
		char *emode = json_escape(mode, -1);
		char *etype = json_escape(type, -1);
		char *ebr = json_escape(br, -1);
		char *emsg = json_escape(msg, -1);
		if (!first) g_string_append_c(g, ',');
		first = 0;
		g_string_append_printf(g,
			"{\"addr\":\"%s\",\"phone\":\"%s\",\"time\":\"%s\",\"date\":\"%s\",\"mode\":\"%s\","
			"\"type\":\"%s\",\"bitrate\":\"%s\",\"message\":\"%s\"}",
			eaddr, ephone, etime, edate, emode, etype, ebr, emsg);
		g_free(eaddr); g_free(ephone); g_free(etime); g_free(edate);
		g_free(emode); g_free(etype); g_free(ebr); g_free(emsg);
	}
	g_string_append_c(g, ']');
}

static void push_panes(void)
{
	if (!s_ready || !s_view) return;
	GString *g = g_string_new("window.pdlSetPanes && window.pdlSetPanes(");
	append_pane_lines_json(g, &Pane1, &s_last_pane1_bottom);
	g_string_append_c(g, ',');
	append_pane_lines_json(g, &Pane2, &s_last_pane2_bottom);
	g_string_append(g, ");");
	run_js(g->str);
	g_string_free(g, TRUE);
}

static void push_status(void)
{
	if (!s_ready || !s_view) return;

	double level = pdl_linux_get_input_level();
	double quality = (bRX_MessageQuality_Valid && dRX_MessageQuality >= 0.0)
		? dRX_MessageQuality : 0.0;

	const char *pc_label = pdl_pagercast_state_label();
	const char *pc_status = pdl_pagercast_status();
	const char *pc_input = pdl_pagercast_input_label();
	int pc_state = (int)pdl_pagercast_state();
	int pc_want = pdl_pagercast_is_wanted();

	const char *cap = pdl_linux_gui_get_capture_device();
	char *esc_status = json_escape(pc_status ? pc_status : "", -1);
	char *esc_label = json_escape(pc_label ? pc_label : "", -1);
	char *esc_input = json_escape(pc_input ? pc_input : (cap ? cap : "default"), -1);
	char *esc_mode = json_escape(szWindowText[2], -1);

	char script[2048];
	snprintf(script, sizeof(script),
		"window.pdlSetStatus && window.pdlSetStatus({"
		"signal:%.1f,quality:%.1f,bch:%d,cw:%d,"
		"pcState:%d,pcWant:%d,pcEnable:%d,pcLabel:\"%s\",pcStatus:\"%s\","
		"input:\"%s\",mode:\"%s\",uiMode:\"%s\""
		"});",
		level, quality, iRX_LastBchErrors, iRX_LastBchCodewords,
		pc_state, pc_want, Profile.pagercast_enabled ? 1 : 0,
		esc_label, esc_status, esc_input, esc_mode,
		Profile.ui_mode == PDL_UI_WEB ? "web" : "gtk");
	run_js(script);
	g_free(esc_status);
	g_free(esc_label);
	g_free(esc_input);
	g_free(esc_mode);
}

static void push_settings(void)
{
	if (!s_view) return;
	char *api = json_escape(Profile.pagercast_api_base, -1);
	char *freq = json_escape(Profile.pagercast_frequency, -1);
	char *key = json_escape(Profile.pagercast_api_key, -1);
	char *host = json_escape(Profile.pagercast_host_suffix, -1);
	char *cap = json_escape(pdl_linux_gui_get_capture_device(), -1);

	GString *nodes = g_string_new("[");
	pdl_pagercast_seed_fallback_nodes();
	for (int i = 0; i < pdl_pagercast_node_count(); i++) {
		char *n = json_escape(pdl_pagercast_node_name(i), -1);
		char *u = json_escape(pdl_pagercast_node_url(i), -1);
		if (i) g_string_append_c(nodes, ',');
		g_string_append_printf(nodes, "{\"name\":\"%s\",\"url\":\"%s\"}", n, u);
		g_free(n);
		g_free(u);
	}
	g_string_append_c(nodes, ']');

	GString *g = g_string_new("window.pdlSetSettings && window.pdlSetSettings({");
	g_string_append_printf(g,
		"uiMode:\"%s\",pcEnable:%d,apiBase:\"%s\",frequency:\"%s\","
		"apiKey:\"%s\",hostSuffix:\"%s\",capture:\"%s\",nodes:%s});",
		Profile.ui_mode == PDL_UI_WEB ? "web" : "gtk",
		Profile.pagercast_enabled ? 1 : 0,
		api, freq, key, host, cap, nodes->str);
	run_js(g->str);
	g_string_free(g, TRUE);
	g_string_free(nodes, TRUE);
	g_free(api);
	g_free(freq);
	g_free(key);
	g_free(host);
	g_free(cap);
}

static gboolean refresh_idle(gpointer unused)
{
	(void)unused;
	push_panes();
	return G_SOURCE_REMOVE;
}

static void schedule_pane_refresh(void)
{
	g_idle_add(refresh_idle, NULL);
}

static gboolean meter_tick(gpointer unused)
{
	(void)unused;
	push_status();
	return G_SOURCE_CONTINUE;
}

static char *extract_json_string(const char *json, const char *key)
{
	char pat[128];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char *p = strstr(json, pat);
	if (!p) return NULL;
	p = strchr(p + strlen(pat), ':');
	if (!p) return NULL;
	p++;
	while (*p == ' ' || *p == '\t') p++;
	if (*p != '"') return NULL;
	p++;
	GString *g = g_string_new(NULL);
	while (*p && *p != '"') {
		if (*p == '\\' && p[1]) {
			p++;
			if (*p == 'n') g_string_append_c(g, '\n');
			else if (*p == 'r') g_string_append_c(g, '\r');
			else if (*p == 't') g_string_append_c(g, '\t');
			else g_string_append_c(g, *p);
		} else {
			g_string_append_c(g, *p);
		}
		p++;
	}
	return g_string_free(g, FALSE);
}

static int extract_json_int(const char *json, const char *key, int def)
{
	char pat[128];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char *p = strstr(json, pat);
	if (!p) return def;
	p = strchr(p + strlen(pat), ':');
	if (!p) return def;
	return atoi(p + 1);
}

static void handle_bridge_json(const char *json)
{
	if (!json || !json[0]) return;
	char *cmd = extract_json_string(json, "cmd");
	if (!cmd) return;

	if (strcmp(cmd, "ready") == 0) {
		s_ready = 1;
		s_last_pane1_bottom = (unsigned)-1;
		s_last_pane2_bottom = (unsigned)-1;
		push_settings();
		push_panes();
		push_status();
	} else if (strcmp(cmd, "quit") == 0) {
		pdl_linux_web_gui_quit();
	} else if (strcmp(cmd, "clear") == 0) {
		int pane = extract_json_int(json, "pane", 0);
		if (pane == 1) ClearPanes(true, false);
		else if (pane == 2) ClearPanes(false, true);
		else ClearPanes(true, true);
		s_last_pane1_bottom = s_last_pane2_bottom = (unsigned)-1;
		push_panes();
	} else if (strcmp(cmd, "get_settings") == 0) {
		push_settings();
	} else if (strcmp(cmd, "dialog") == 0) {
		char *name = extract_json_string(json, "name");
		if (name) {
			if (strcmp(name, "options") == 0)
				pdl_linux_gui_show_options();
			else if (strcmp(name, "audio") == 0)
				pdl_linux_gui_show_audio();
			else if (strcmp(name, "filters") == 0)
				pdl_linux_gui_show_filters();
			else if (strcmp(name, "stats") == 0)
				pdl_linux_gui_show_stats();
			else if (strcmp(name, "clear") == 0)
				pdl_linux_gui_show_clear();
			else if (strcmp(name, "about") == 0)
				pdl_linux_gui_show_about();
			else if (strcmp(name, "general") == 0)
				pdl_linux_general_dialog(GTK_WINDOW(s_win));
			else if (strcmp(name, "display") == 0)
				pdl_linux_display_dialog(GTK_WINDOW(s_win));
			else if (strcmp(name, "colors") == 0)
				pdl_linux_colors_dialog(GTK_WINDOW(s_win));
			else if (strcmp(name, "mail") == 0)
				pdl_linux_mail_dialog(GTK_WINDOW(s_win));
			g_free(name);
		}
		/* Dialogs may change Profile / panes. */
		s_last_pane1_bottom = s_last_pane2_bottom = (unsigned)-1;
		push_panes();
		push_status();
	} else if (strcmp(cmd, "pagercast") == 0) {
		char *action = extract_json_string(json, "action");
		if (action) {
			if (strcmp(action, "connect") == 0) {
				if (!Profile.pagercast_enabled) {
					Profile.pagercast_enabled = 1;
					WriteSettings();
				}
				pdl_pagercast_connect();
			} else if (strcmp(action, "disconnect") == 0) {
				pdl_pagercast_disconnect();
			} else if (strcmp(action, "toggle") == 0) {
				if (pdl_pagercast_is_wanted())
					pdl_pagercast_disconnect();
				else if (Profile.pagercast_enabled)
					pdl_pagercast_connect();
			} else if (strcmp(action, "refresh") == 0) {
				pdl_pagercast_refresh_nodes();
				push_settings();
			}
			g_free(action);
		}
		pdl_apply_message_column_layout();
		push_status();
	} else if (strcmp(cmd, "save_settings") == 0) {
		char *api = extract_json_string(json, "apiBase");
		char *freq = extract_json_string(json, "frequency");
		char *key = extract_json_string(json, "apiKey");
		char *host = extract_json_string(json, "hostSuffix");
		char *cap = extract_json_string(json, "capture");
		int en = extract_json_int(json, "pcEnable", Profile.pagercast_enabled);
		Profile.pagercast_enabled = en ? 1 : 0;
		if (!Profile.pagercast_enabled && pdl_pagercast_is_wanted())
			pdl_pagercast_disconnect();
		pdl_apply_message_column_layout();
		if (api) {
			strncpy(Profile.pagercast_api_base, api, sizeof(Profile.pagercast_api_base) - 1);
			Profile.pagercast_api_base[sizeof(Profile.pagercast_api_base) - 1] = '\0';
			g_free(api);
		}
		if (freq) {
			strncpy(Profile.pagercast_frequency, freq, sizeof(Profile.pagercast_frequency) - 1);
			Profile.pagercast_frequency[sizeof(Profile.pagercast_frequency) - 1] = '\0';
			g_free(freq);
		}
		if (key) {
			strncpy(Profile.pagercast_api_key, key, sizeof(Profile.pagercast_api_key) - 1);
			Profile.pagercast_api_key[sizeof(Profile.pagercast_api_key) - 1] = '\0';
			g_free(key);
		}
		if (host) {
			strncpy(Profile.pagercast_host_suffix, host, sizeof(Profile.pagercast_host_suffix) - 1);
			Profile.pagercast_host_suffix[sizeof(Profile.pagercast_host_suffix) - 1] = '\0';
			g_free(host);
		}
		if (cap && cap[0]) {
			pdl_linux_gui_set_capture_device(cap);
			g_free(cap);
			pdl_linux_restart_capture();
		} else {
			g_free(cap);
		}
		WriteSettings();
		push_settings();
		push_status();
	} else if (strcmp(cmd, "set_ui_mode") == 0) {
		char *mode = extract_json_string(json, "mode");
		int restart = extract_json_int(json, "restart", 1);
		int ui = PDL_UI_GTK;
		if (mode && (strcasecmp(mode, "web") == 0 || strcmp(mode, "1") == 0))
			ui = PDL_UI_WEB;
		g_free(mode);
		if (ui == Profile.ui_mode && !restart) {
			WriteSettings();
		} else {
			pdl_linux_apply_ui_mode(ui, restart);
		}
	}

	g_free(cmd);
}

static void on_script_message(WebKitUserContentManager *ucm, WebKitJavascriptResult *js_result, gpointer user_data)
{
	(void)ucm;
	(void)user_data;
	if (!js_result) return;
#if WEBKIT_CHECK_VERSION(2, 22, 0)
	JSCValue *val = webkit_javascript_result_get_js_value(js_result);
	gchar *str = jsc_value_to_string(val);
#else
	JSGlobalContextRef ctx = webkit_javascript_result_get_global_context(js_result);
	JSValueRef val = webkit_javascript_result_get_value(js_result);
	JSStringRef jsstr = JSValueToStringCopy(ctx, val, NULL);
	size_t n = JSStringGetMaximumUTF8CStringSize(jsstr);
	gchar *str = (gchar *)g_malloc(n);
	JSStringGetUTF8CString(jsstr, str, n);
	JSStringRelease(jsstr);
#endif
	if (str) {
		handle_bridge_json(str);
		g_free(str);
	}
}

static gboolean on_delete(GtkWidget *w, GdkEvent *e, gpointer data)
{
	(void)w; (void)e; (void)data;
	pdl_linux_web_gui_quit();
	return TRUE;
}

static int find_webui_dir(char *out, size_t outlen)
{
	char c1[PATH_MAX], c2[PATH_MAX], c3[PATH_MAX];
	const char *candidates[8];
	int nc = 0;

	char exe[PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n > 0) {
		exe[n] = '\0';
		char tmp[PATH_MAX];
		strncpy(tmp, exe, sizeof(tmp) - 1);
		tmp[sizeof(tmp) - 1] = '\0';
		char *dir = dirname(tmp);
		snprintf(c1, sizeof(c1), "%s/webui", dir);
		candidates[nc++] = c1;

		strncpy(tmp, exe, sizeof(tmp) - 1);
		tmp[sizeof(tmp) - 1] = '\0';
		dir = dirname(tmp);
		snprintf(c2, sizeof(c2), "%s/../webui", dir);
		candidates[nc++] = c2;

		/* Packaged: /usr/bin/pdl → /usr/share/pdl/webui */
		strncpy(tmp, exe, sizeof(tmp) - 1);
		tmp[sizeof(tmp) - 1] = '\0';
		dir = dirname(tmp);
		snprintf(c3, sizeof(c3), "%s/../share/pdl/webui", dir);
		candidates[nc++] = c3;
	}
	candidates[nc++] = "webui";
	candidates[nc++] = "../webui";
#ifdef PDL_DATADIR
	candidates[nc++] = PDL_DATADIR "/webui";
#endif
	candidates[nc] = NULL;

	for (int i = 0; candidates[i]; i++) {
		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/index.html", candidates[i]);
		if (access(path, R_OK) == 0) {
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
	}
	return -1;
}

/*
 * WebKitGTK + proprietary NVIDIA on Wayland hits Error 71 (protocol error)
 * from DMA-BUF / explicit sync (webkit bug 280210). Must set env *before*
 * WebKit/GTK GPU init.
 */
static void apply_webkit_gpu_quirks(void)
{
	const char *wayland = getenv("WAYLAND_DISPLAY");
	int on_wayland = (wayland && wayland[0]);
	int nvidia = (access("/sys/module/nvidia", F_OK) == 0);

	if (!nvidia)
		return;

	if (on_wayland) {
		if (!getenv("__NV_DISABLE_EXPLICIT_SYNC")) {
			setenv("__NV_DISABLE_EXPLICIT_SYNC", "1", 1);
			fprintf(stderr, "Web UI: NVIDIA+Wayland quirk __NV_DISABLE_EXPLICIT_SYNC=1\n");
		}
		/* Fallback if explicit-sync quirk alone is not enough on some drivers. */
		if (!getenv("WEBKIT_DISABLE_DMABUF_RENDERER")) {
			setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1", 1);
			fprintf(stderr, "Web UI: NVIDIA quirk WEBKIT_DISABLE_DMABUF_RENDERER=1\n");
		}
	} else {
		/* X11 + NVIDIA: DMA-BUF often yields a blank WebView. */
		if (!getenv("WEBKIT_DISABLE_DMABUF_RENDERER")) {
			setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1", 1);
			fprintf(stderr, "Web UI: NVIDIA+X11 quirk WEBKIT_DISABLE_DMABUF_RENDERER=1\n");
		}
	}
}

int pdl_linux_web_gui_init(int *argc, char ***argv)
{
	apply_webkit_gpu_quirks();

	g_set_prgname("pdl");
	g_set_application_name("PDL");
	gdk_set_program_class("pdl");
	if (!gtk_init_check(argc, argv))
		return -1;

	if (find_webui_dir(s_webui_dir, sizeof(s_webui_dir)) != 0) {
		fprintf(stderr, "Web UI: cannot find webui/index.html (looked next to binary and ../webui)\n");
		return -1;
	}

	s_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(s_win), "PDL");
	pdl_linux_gui_apply_app_icon(GTK_WINDOW(s_win));
	gtk_window_set_default_size(GTK_WINDOW(s_win),
		Profile.xSize > 400 ? Profile.xSize : 1100,
		Profile.ySize > 300 ? Profile.ySize : 700);
	g_signal_connect(s_win, "delete-event", G_CALLBACK(on_delete), NULL);
	pdl_linux_gui_set_dialog_parent(GTK_WINDOW(s_win));
	ghWnd = (HWND)s_win;

	WebKitUserContentManager *ucm = webkit_user_content_manager_new();
	webkit_user_content_manager_register_script_message_handler(ucm, "pdl");
	g_signal_connect(ucm, "script-message-received::pdl", G_CALLBACK(on_script_message), NULL);

	s_view = WEBKIT_WEB_VIEW(webkit_web_view_new_with_user_content_manager(ucm));
	WebKitSettings *ws = webkit_web_view_get_settings(s_view);
	webkit_settings_set_enable_developer_extras(ws, TRUE);
	webkit_settings_set_allow_file_access_from_file_urls(ws, TRUE);
	webkit_settings_set_allow_universal_access_from_file_urls(ws, TRUE);
	webkit_settings_set_javascript_can_access_clipboard(ws, TRUE);

	gtk_container_add(GTK_CONTAINER(s_win), GTK_WIDGET(s_view));

	char uri[PATH_MAX + 32];
	snprintf(uri, sizeof(uri), "file://%s/index.html", s_webui_dir);
	webkit_web_view_load_uri(s_view, uri);

	pdl_platform_register_pane_refresh_cb(schedule_pane_refresh);
	g_timeout_add(150, meter_tick, NULL);

	gtk_widget_show_all(s_win);
	fprintf(stderr, "Web UI: loaded %s\n", uri);
	return 0;
}

void pdl_linux_web_gui_run(void)
{
	gtk_main();
}

void pdl_linux_web_gui_quit(void)
{
	WriteSettings();
	gtk_main_quit();
}

#endif
