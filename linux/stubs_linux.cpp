/*
 * Linux stubs / shims for Windows-only APIs used by shared PDL code.
 */
#ifdef __linux__
#include "pdl_linux_types.h"
#include "pdl_platform.h"
#include "pdl.h"
#include "gfx.h"
#include "sigind.h"
#include "Headers/sound_in.h"
#include "linux/gui_gtk.h"
#include "pdl_version.h"
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <spawn.h>
#include <sys/wait.h>
#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

extern char **environ;

void GetLocalTime(SYSTEMTIME *st)
{
	if (!st) return;
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	if (!tm) return;
	st->wYear = (WORD)(tm->tm_year + 1900);
	st->wMonth = (WORD)(tm->tm_mon + 1);
	st->wDayOfWeek = (WORD)tm->tm_wday;
	st->wDay = (WORD)tm->tm_mday;
	st->wHour = (WORD)tm->tm_hour;
	st->wMinute = (WORD)tm->tm_min;
	st->wSecond = (WORD)tm->tm_sec;
	st->wMilliseconds = 0;
}

int MessageBox(void *hWnd, const char *text, const char *caption, UINT type)
{
	(void)type;
	if (gtk_init_check(NULL, NULL)) {
		GtkWindow *parent = hWnd ? GTK_WINDOW(hWnd) : NULL;
		GtkWidget *d = gtk_dialog_new_with_buttons(
			caption && caption[0] ? caption : "PDL",
			parent,
			(GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
			"OK", GTK_RESPONSE_ACCEPT, NULL);
		pdl_linux_gui_prepare_dialog(d);
		if (parent)
			gtk_window_set_transient_for(GTK_WINDOW(d), parent);
		GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(d));
		gtk_container_set_border_width(GTK_CONTAINER(content), 12);
		GtkWidget *lab = gtk_label_new(text ? text : "");
		gtk_label_set_selectable(GTK_LABEL(lab), TRUE);
		gtk_widget_set_halign(lab, GTK_ALIGN_START);
		gtk_label_set_xalign(GTK_LABEL(lab), 0.0f);
		gtk_label_set_line_wrap(GTK_LABEL(lab), TRUE);
		gtk_box_pack_start(GTK_BOX(content), lab, TRUE, TRUE, 0);
		gtk_widget_show_all(d);
		gtk_dialog_run(GTK_DIALOG(d));
		gtk_widget_destroy(d);
		return 1;
	}
	fprintf(stderr, "%s: %s\n", caption ? caption : "PDL", text ? text : "");
	return 0;
}

void SetWindowText(HWND hWnd, const char *str)
{
	(void)hWnd;
	if (str) pdl_linux_gui_update_title();
}

BOOL GetOpenFileName(OPENFILENAME *pofn)
{
	if (!pofn || !pofn->lpstrFile) return FALSE;
	if (!gtk_init_check(NULL, NULL)) return FALSE;

	GtkFileChooserAction action = (pofn->Flags & 0x800) /* OFN_OVERWRITEPROMPT-ish save */
		? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN;
	/* Prefer open unless title says Start Recording / save */
	if (pofn->lpstrTitle && strstr(pofn->lpstrTitle, "Start"))
		action = GTK_FILE_CHOOSER_ACTION_SAVE;

	GtkWidget *dlg = gtk_file_chooser_dialog_new(
		pofn->lpstrTitle ? pofn->lpstrTitle : "Select file",
		NULL, action,
		"_Cancel", GTK_RESPONSE_CANCEL,
		(action == GTK_FILE_CHOOSER_ACTION_SAVE) ? "_Save" : "_Open", GTK_RESPONSE_ACCEPT,
		NULL);
	if (pofn->lpstrInitialDir && pofn->lpstrInitialDir[0])
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), pofn->lpstrInitialDir);
	if (pofn->lpstrFile[0] && action == GTK_FILE_CHOOSER_ACTION_SAVE)
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), pofn->lpstrFile);

	gboolean ok = FALSE;
	if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
		char *fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
		if (fn) {
			strncpy(pofn->lpstrFile, fn, pofn->nMaxFile > 0 ? pofn->nMaxFile - 1 : 0);
			if (pofn->nMaxFile > 0) pofn->lpstrFile[pofn->nMaxFile - 1] = '\0';
			g_free(fn);
			ok = TRUE;
		}
	}
	gtk_widget_destroy(dlg);
	/* Drain pending events so nested dialogs behave */
	while (gtk_events_pending()) gtk_main_iteration();
	return ok ? TRUE : FALSE;
}

void SetNewWindowText(char *text)
{
	(void)text;
	pdl_linux_gui_update_title();
}

void UpdateSigInd(int direction)
{
	pdl_platform_signal_indicator(direction);
}

BOOL LoadSigInd(HINSTANCE hInstance)
{
	(void)hInstance;
	return TRUE;
}

void FreeSigInd(void) {}
void DrawSigInd(HWND hwnd) { (void)hwnd; }
void show_sigind(int new_pos, int old_pos) { (void)new_pos; (void)old_pos; }

/* Gfx globals (defined in Gfx.cpp on Windows) */
HBRUSH hbr = NULL, hboxbr = NULL, gray_brush = NULL, lgray_brush = NULL, black_brush = NULL;
HFONT hfont = NULL, hboxfont = NULL, pdl_font[2] = { NULL, NULL };
LOGFONT boxfontInfo = { 0 };
HPEN null_pen = NULL, SysPEN[16] = { 0 };
DWORD rgbColor[17][3] = {
	{ 0,   0,   0  },	/* BLACK */
	{ 0,   0,   128},	/* BLUE */
	{ 0,   128, 0  },	/* GREEN */
	{ 0,   128, 128},	/* CYAN */
	{ 255, 0,   0  },	/* RED */
	{ 64,  0,   128},	/* MAGENTA */
	{ 128, 128, 64 },	/* BROWN */
	{ 192, 192, 192},	/* LIGHTGRAY */
	{ 138, 138, 138},	/* DARKGRAY */
	{ 0,   0,   255},	/* LIGHTBLUE */
	{ 0,   255, 0  },	/* LIGHTGREEN */
	{ 0,   255, 255},	/* LIGHTCYAN */
	{ 255, 64,  64 },	/* LIGHTRED */
	{ 255, 0,   255},	/* LIGHTMAGENTA */
	{ 255, 255, 0  },	/* YELLOW */
	{ 255, 255, 255},	/* WHITE */
	{ 255, 165, 0  }	/* ORANGE */
};
int Max_X_Client = 0, PL1_SCount = 0, PL2_SCount = 0;
int iItemPositions[9] = { 0,
	1, 11, 20, 29, 38, 46, 54, 0 };
int iItemWidths[7] = { 0, 75, 70, 75, 50, 55, 55 };

void MessageBeep(UINT uType) { (void)uType; }

bool bUpdateFilters = false;
double dRX_Quality = -1.0;   /* -1 = unknown until CountBiterrors() sees real decode samples */
bool bRX_Quality_Valid = false;
int nCount_Messages = 0;
int nCount_Groupcalls = 0;
int nCount_Rejected = 0;
int nCount_Blocked = 0;
int nCount_CleanRx = 0;   /* displayed msgs with acceptable BCH */
int nCount_CorruptRx = 0; /* displayed msgs with high BCH errors */
int nCount_Missed[2] = { 0, 0 };
int nCount_BlockBuffer[2] = { 0, 0 };
bool bTrayed = false;
char szFilenameDate[16] = { 0 };
#ifndef PDL_VERSION_STRING
#define PDL_VERSION_STRING "PDL"
#endif
char *pdl_version = (char *)PDL_VERSION_STRING;

static GtkStatusIcon *s_tray = NULL;

static void on_tray_activate(GtkStatusIcon *icon, gpointer data)
{
	(void)icon;
	GtkWidget *win = (GtkWidget *)data;
	if (!win) return;
	if (gtk_widget_get_visible(win)) {
		gtk_widget_hide(win);
		bTrayed = true;
	} else {
		gtk_widget_show(win);
		gtk_window_present(GTK_WINDOW(win));
		bTrayed = false;
	}
}

void SystemTrayWindow(bool bHideWindow)
{
	extern HWND ghWnd;
	GtkWidget *win = (GtkWidget *)ghWnd;
	if (!Profile.SystemTray) {
		SystemTrayIcon(true);
		return;
	}
	if (!s_tray) {
		GdkPixbuf *pb = win ? gtk_window_get_icon(GTK_WINDOW(win)) : NULL;
		if (pb)
			s_tray = gtk_status_icon_new_from_pixbuf(pb);
		else
			s_tray = gtk_status_icon_new_from_icon_name("pdl");
		gtk_status_icon_set_tooltip_text(s_tray, pdl_version ? pdl_version : "PDL");
		if (win) g_signal_connect(s_tray, "activate", G_CALLBACK(on_tray_activate), win);
	}
	gtk_status_icon_set_visible(s_tray, TRUE);
	if (bHideWindow && win) {
		gtk_widget_hide(win);
		bTrayed = true;
	} else if (!bHideWindow && win) {
		gtk_widget_show(win);
		gtk_window_present(GTK_WINDOW(win));
		bTrayed = false;
	}
}

void SystemTrayIcon(bool bRemoveIcon)
{
	if (bRemoveIcon) {
		if (s_tray) {
			gtk_status_icon_set_visible(s_tray, FALSE);
			g_object_unref(s_tray);
			s_tray = NULL;
		}
		Profile.SystemTray = 0;
		bTrayed = false;
		return;
	}
	Profile.SystemTray = 1;
	SystemTrayWindow(false);
}

void SetLocalTime(SYSTEMTIME *st)
{
	if (!st || !Profile.FlexTIME) return;
	/* Best-effort: requires privileges; ignore failure. */
	struct tm tm;
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = st->wYear - 1900;
	tm.tm_mon = st->wMonth - 1;
	tm.tm_mday = st->wDay;
	tm.tm_hour = st->wHour;
	tm.tm_min = st->wMinute;
	tm.tm_sec = st->wSecond;
	time_t t = mktime(&tm);
	if (t != (time_t)-1) {
		struct timespec ts;
		ts.tv_sec = t;
		ts.tv_nsec = 0;
		clock_settime(CLOCK_REALTIME, &ts);
	}
}

/* Tagged handles so CloseHandle can free files without mistaking PIDs. */
#define PDL_H_FILE 0x46494C45u
#define PDL_H_PROC 0x50524F43u
struct linux_handle {
	unsigned magic;
	int fd_or_pid;
	off_t size;
};

HANDLE CreateFile(LPCSTR a, DWORD b, DWORD c, void* d, DWORD e, DWORD f, HANDLE g)
{
	(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;
	if (!a || !a[0]) return (HANDLE)-1;
	int fd = open(a, O_RDONLY);
	if (fd < 0) return (HANDLE)-1;
	linux_handle *h = (linux_handle *)malloc(sizeof(linux_handle));
	if (!h) { close(fd); return (HANDLE)-1; }
	h->magic = PDL_H_FILE;
	h->fd_or_pid = fd;
	struct stat st;
	h->size = (fstat(fd, &st) == 0) ? st.st_size : 0;
	return (HANDLE)h;
}

DWORD GetFileSize(HANDLE h, LPDWORD high)
{
	if (high) *high = 0;
	if (!h || h == (HANDLE)-1) return 0;
	linux_handle *fh = (linux_handle *)h;
	if (fh->magic != PDL_H_FILE) return 0;
	return (DWORD)fh->size;
}

BOOL CloseHandle(HANDLE h)
{
	if (!h || h == (HANDLE)-1) return TRUE;
	linux_handle *fh = (linux_handle *)h;
	if (fh->magic == PDL_H_FILE) {
		if (fh->fd_or_pid >= 0) close(fh->fd_or_pid);
		free(fh);
		return TRUE;
	}
	if (fh->magic == PDL_H_PROC) {
		/* Reap child if still running; ignore errors. */
		int status;
		waitpid(fh->fd_or_pid, &status, WNOHANG);
		free(fh);
		return TRUE;
	}
	return TRUE;
}

int sndPlaySound(const char *path, UINT flags)
{
	(void)flags;
	if (!path || !path[0]) return 0;
	const char *dev = pdl_linux_gui_get_playback_device();
	if (!dev || !dev[0]) dev = "default";
	return pdl_linux_play_wav(path, dev) == 0 ? 1 : 0;
}

BOOL CreateProcess(void* a, char* b, void* c, void* d, BOOL e, void* f, DWORD g, void* h, STARTUPINFO* si, PROCESS_INFORMATION* pi)
{
	(void)a;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)si;
	if (!b || !b[0]) return FALSE;
	pid_t pid = 0;
	char *argv[] = { (char *)"/bin/sh", (char *)"-c", b, NULL };
	int rc = posix_spawn(&pid, "/bin/sh", NULL, NULL, argv, environ);
	if (rc != 0) return FALSE;
	linux_handle *ph = (linux_handle *)malloc(sizeof(linux_handle));
	if (ph) {
		ph->magic = PDL_H_PROC;
		ph->fd_or_pid = (int)pid;
		ph->size = 0;
	}
	if (pi) {
		memset(pi, 0, sizeof(*pi));
		pi->dwProcessId = (DWORD)pid;
		pi->hProcess = ph ? (HANDLE)ph : (HANDLE)(intptr_t)pid;
		pi->hThread = pi->hProcess;
	}
	return TRUE;
}

int GetDateFormat(LCID locale, DWORD flags, const SYSTEMTIME* st, const char* fmt, char* buf, int len)
{
	(void)locale;(void)flags;(void)fmt;
	SYSTEMTIME t;
	if (st) t = *st; else GetLocalTime(&t);
	snprintf(buf, len, "%02u-%02u-%02u", t.wDay, t.wMonth, t.wYear % 100);
	return (int)strlen(buf);
}
int GetTimeFormat(LCID locale, DWORD flags, const SYSTEMTIME* st, const char* fmt, char* buf, int len)
{
	(void)locale;(void)flags;(void)fmt;
	SYSTEMTIME t;
	if (st) t = *st; else GetLocalTime(&t);
	snprintf(buf, len, "%02u:%02u:%02u", t.wHour, t.wMinute, t.wSecond);
	return (int)strlen(buf);
}

BOOL CreateDirectory(LPCSTR path, void* sec)
{
	(void)sec;
	return (mkdir(path, 0755) == 0) ? TRUE : FALSE;
}

#endif
