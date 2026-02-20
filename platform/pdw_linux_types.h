/* Minimal type definitions for PDW Linux build.
 * Shared decoding code (decode, Pocsag, Flex, Misc, etc.) uses these types;
 * this header provides the same layout so it compiles without Windows. */
#ifndef PDW_LINUX_TYPES_H
#define PDW_LINUX_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#define stricmp strcasecmp
#define strnicmp strncasecmp

typedef long LRESULT;
typedef unsigned long WPARAM;
typedef long LPARAM;
typedef void VOID;
typedef int INT;
#define CALLBACK
#define PASCAL
#define NEAR
#define FAR
#define WM_USER 0x0400
typedef void* DLGPROC;

typedef int                BOOL;
typedef unsigned char      BYTE;
typedef unsigned short     WORD;
typedef unsigned int       DWORD;
typedef unsigned int       UINT;
typedef long               LONG;
typedef void*              HANDLE;
typedef void*              HWND;
typedef void*              HINSTANCE;
typedef void*              HDC;
typedef void*              HACCEL;
typedef void*              HMENU;
typedef void*              HBITMAP;
typedef void*              HBRUSH;
typedef void*              HFONT;
typedef void*              HPEN;
typedef void*              HWAVEIN;
typedef void*              HWAVEOUT;
typedef void*              HGLOBAL;

#define TRUE  1
#define FALSE 0
#ifndef MAX_PATH
#define MAX_PATH 260
#endif

typedef char               TCHAR;
typedef char*              LPTSTR;
typedef const char*        LPCTSTR;
typedef char*              LPSTR;
typedef const char*        LPCSTR;
typedef DWORD*             LPDWORD;
#define TEXT(x) x
/* wsprintf(buf, fmt, ...) - Windows has no size limit; use sprintf for compatibility */
#define wsprintf sprintf

struct RECT { int left, top, right, bottom; };

typedef struct {
	int lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
	BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision, lfClipPrecision, lfQuality, lfPitchAndFamily;
	char lfFaceName[32];
} LOGFONT;

typedef DWORD COLORREF;

typedef struct {
	WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME;

void GetLocalTime(SYSTEMTIME *st);
void SetLocalTime(SYSTEMTIME *st);

typedef struct {
	DWORD lStructSize;
	HWND hwndOwner;
	void* hInstance;
	LPCSTR lpstrFilter;
	LPTSTR lpstrCustomFilter;
	DWORD nMaxCustFilter, nFilterIndex;
	LPTSTR lpstrFile;
	DWORD nMaxFile;
	LPTSTR lpstrFileTitle;
	DWORD nMaxFileTitle;
	LPCSTR lpstrInitialDir, lpstrTitle;
	DWORD Flags;
	WORD nFileOffset, nFileExtension;
	LPCSTR lpstrDefExt;
	DWORD lCustData;
	void* lpfnHook;
	LPCSTR lpTemplateName;
} OPENFILENAME;

typedef struct {
	LPSTR lpData;
	DWORD dwBufferLength, dwBytesRecorded, dwUser, dwFlags, dwLoops;
	void* lpNext;
	DWORD reserved;
} WAVEHDR;

typedef struct { DWORD dwOSVersionInfoSize; DWORD dwMajorVersion; DWORD dwMinorVersion; DWORD dwBuildNumber; DWORD dwPlatformId; TCHAR szCSDVersion[128]; } OSVERSIONINFO;
typedef struct { DWORD lStructSize; HWND hwndOwner; void* hDevMode; void* hDevNames; HANDLE hDC; DWORD Flags; WORD nFromPage, nToPage, nMinPage, nMaxPage, nCopies; HINSTANCE hInstance; DWORD lCustData; void* lpfnPrintHook; void* lpfnSetupHook; LPCSTR lpPrintTemplateName; LPCSTR lpSetupTemplateName; HANDLE hPrintTemplate; HANDLE hSetupTemplate; } PRINTDLG;

#define OFN_HIDEREADONLY 0x04
#define MB_ICONWARNING   0x30

#define MAKELONG(lo, hi) ((LONG)(((WORD)(lo)) | ((DWORD)((WORD)(hi)) << 16)))
#define FILE_ATTRIBUTE_NORMAL 0x80
#define GENERIC_READ 0x80000000
#define FILE_SHARE_READ 1
#define OPEN_EXISTING 3
#define SND_FILENAME 0x01
#define SND_ASYNC 0x01
#define SND_NOSTOP 0x10
#define LOCALE_USER_DEFAULT 0
#define TIME_FORCE24HOURFORMAT 0x80

static inline void ZeroMemory(void *p, size_t n) { memset(p, 0, n); }
#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif

/* Stub types for code that is #ifdef _WIN32 on Linux */
typedef struct { HANDLE hProcess; HANDLE hThread; DWORD dwProcessId; DWORD dwThreadId; } PROCESS_INFORMATION;
typedef struct { DWORD cb; char *lpReserved; char *lpDesktop; char *lpTitle; DWORD dwX; DWORD dwY; DWORD dwXSize; DWORD dwYSize; DWORD dwXCountChars; DWORD dwYCountChars; DWORD dwFillAttribute; DWORD dwFlags; WORD wShowWindow; WORD cbReserved2; void *lpReserved2; HANDLE hStdInput; HANDLE hStdOutput; HANDLE hStdError; } STARTUPINFO;

typedef DWORD LCID;

/* Declarations for stubs (defined in linux/stubs_linux.cpp) */
HANDLE CreateFile(LPCSTR, DWORD, DWORD, void*, DWORD, DWORD, HANDLE);
DWORD GetFileSize(HANDLE, LPDWORD);
BOOL CloseHandle(HANDLE);
int sndPlaySound(const char*, UINT);
BOOL CreateProcess(void*, char*, void*, void*, BOOL, void*, DWORD, void*, STARTUPINFO*, PROCESS_INFORMATION*);
int GetDateFormat(LCID, DWORD, const SYSTEMTIME*, const char*, char*, int);
int GetTimeFormat(LCID, DWORD, const SYSTEMTIME*, const char*, char*, int);
BOOL CreateDirectory(LPCSTR path, void* sec);

/* Baud rate constants for utils/rs232.h (CBR_SLICER_2K, CBR_SLICER_XP, etc.) */
#ifndef CBR_110
#define CBR_110   110
#define CBR_300   300
#define CBR_19200 19200
#endif

#define MB_OK 0
int MessageBox(void *hWnd, const char *text, const char *caption, UINT type);
void MessageBeep(UINT uType);
void SetWindowText(HWND hWnd, const char *str);
BOOL GetOpenFileName(OPENFILENAME *pofn);

#endif
