/*
 * Linux stubs for symbols that are Windows-only (GUI, dialogs) or
 * implemented natively here (GetLocalTime, UpdateSigInd).
 */
#ifdef __linux__
#include "pdw_linux_types.h"
#include "pdw_platform.h"
#include "pdw.h"
#include "gfx.h"
#include "sigind.h"
#include "Headers/sound_in.h"
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

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
	(void)hWnd;
	(void)type;
	fprintf(stderr, "%s: %s\n", caption ? caption : "PDW", text ? text : "");
	return 0;
}

void SetWindowText(HWND hWnd, const char *str)
{
	(void)hWnd;
	(void)str;
}

BOOL GetOpenFileName(OPENFILENAME *pofn)
{
	(void)pofn;
	return FALSE;
}

void SetNewWindowText(char *text)
{
	(void)text;
}

void UpdateSigInd(int direction)
{
	pdw_platform_signal_indicator(direction);
}

BOOL LoadSigInd(HINSTANCE hInstance)
{
	(void)hInstance;
	return TRUE;
}

void FreeSigInd(void) {}
void DrawSigInd(HWND hwnd) { (void)hwnd; }
void show_sigind(int new_pos, int old_pos) { (void)new_pos; (void)old_pos; }

/* Stub so Options dialog can call WriteSettings; options apply in-memory. */
void WriteSettings(void) { }

/* Gfx globals (defined in Gfx.cpp on Windows) */
HBRUSH hbr = NULL, hboxbr = NULL, gray_brush = NULL, lgray_brush = NULL, black_brush = NULL;
HFONT hfont = NULL, hboxfont = NULL, pdw_font[2] = { NULL, NULL };
LOGFONT boxfontInfo = { 0 };
HPEN null_pen = NULL, SysPEN[16] = { 0 };
DWORD rgbColor[17][3] = { 0 };
int Max_X_Client = 0, PL1_SCount = 0, PL2_SCount = 0;
/* Column start positions in CHARACTERS (like Windows SetMessageItemPositionsWidth).
 * Must fit within LINE_SIZE (180). Windows uses iTempPosition=1 and iItemWidths/cxChar.
 * Paging layout: Address 10, Time 9, Date 9, Mode 9, Type 8, Bitrate 7 chars -> Message at 53. */
int iItemPositions[8] = { 0,
	1,    /* 1 Address */
	11,   /* 2 Time   (1+10) */
	20,   /* 3 Date   (11+9) */
	29,   /* 4 Mode   (20+9) */
	38,   /* 5 Type   (29+9) */
	46,   /* 6 Bitrate (38+8) */
	54 }; /* 7 Message (46+7+1), so wrap adds 54 spaces and we stay under NewLinePoint */
int iItemWidths[7] = { 0, 75, 70, 75, 50, 55, 55 };

void MessageBeep(UINT uType) { (void)uType; }

/* Globals normally defined in PDW.cpp (GUI); provide for Linux link. */
bool bUpdateFilters = false;
double dRX_Quality = 0.0;
int nCount_Messages = 0;
int nCount_Groupcalls = 0;
int nCount_Rejected = 0;
int nCount_Blocked = 0;
int nCount_Missed[2] = { 0, 0 };
int nCount_BlockBuffer[2] = { 0, 0 };
bool bTrayed = false;
char szFilenameDate[16] = { 0 };
char *pdw_version = (char *)"PDW v1.0.0 Linux";

void SystemTrayWindow(bool bHideWindow)
{
	(void)bHideWindow;
}

void SetLocalTime(SYSTEMTIME *st)
{
	(void)st;
}

HANDLE CreateFile(LPCSTR a, DWORD b, DWORD c, void* d, DWORD e, DWORD f, HANDLE g)
{
	(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;
	return (HANDLE)-1;
}
DWORD GetFileSize(HANDLE h, LPDWORD high)
{
	(void)h;(void)high;
	return 0;
}
BOOL CloseHandle(HANDLE h) { (void)h; return TRUE; }

int sndPlaySound(const char *path, UINT flags)
{
	(void)flags;
	if (!path || !path[0]) return 0; /* stop: no-op for now */
	/* Alert sounds go to default output; GUI "output" selector is for input (decoding). */
	return pdw_linux_play_wav(path, "default") == 0 ? 1 : 0;
}

BOOL CreateProcess(void* a, char* b, void* c, void* d, BOOL e, void* f, DWORD g, void* h, STARTUPINFO* si, PROCESS_INFORMATION* pi)
{
	(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)si;(void)pi;
	return FALSE;
}

int GetDateFormat(LCID locale, DWORD flags, const SYSTEMTIME* st, const char* fmt, char* buf, int len)
{
	(void)locale;(void)flags;(void)st;(void)fmt;
	/* Use strftime-style; caller uses "dd'-'MM'-'yy" etc. */
	SYSTEMTIME t;
	if (st) t = *st; else GetLocalTime(&t);
	snprintf(buf, len, "%02u-%02u-%02u", t.wDay, t.wMonth, t.wYear % 100);
	return (int)strlen(buf);
}
int GetTimeFormat(LCID locale, DWORD flags, const SYSTEMTIME* st, const char* fmt, char* buf, int len)
{
	(void)locale;(void)flags;(void)st;(void)fmt;
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
