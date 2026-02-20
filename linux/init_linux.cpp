/*
 * Linux init: globals and pane setup that Initapp.cpp provides on Windows.
 * No Windows API; paths from getcwd/argv[0].
 * Include Linux types first so pdw.h sees correct typedefs.
 */
#ifdef __linux__
#include "pdw_linux_types.h"
#define NEAR
#endif
#include "pdw.h"
#include "initapp.h"
#include "gfx.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define LINE_SIZE 180

PaneStruct Pane1;
PaneStruct Pane2;

HINSTANCE ghInstance = NULL;
HWND    ghWnd = (HWND)1;
HWND    hToolbar = NULL;
HACCEL  ghAccel = NULL;
HMENU   ghMenu = NULL;

TCHAR   szAppName[128] = "PDW Linux";
TCHAR   szShortAppName[32] = "PDW";
TCHAR   szApiFailedMsg[64] = "A Windows API Failed";

FILE *pLogFile = NULL;
FILE *pFilterFile = NULL;
FILE *pSepFilterFile = NULL;
FILE *pStatFile = NULL;
FILE *pFiltersFile = NULL;

unsigned int cxChar = 8, cyChar = 16, cxCaps = 12;
int iMaxWidth = 0;
int sizeSet = 0;
int pane1Height = 0, pane2Height = 0, pane1Pos = 0, pane2Pos = 0, pane1Top = 0;

char gszPDWClass[15] = "PDWWndClass";
char gszPane1Class[17] = "PDWPane1Class";
char gszPane2Class[17] = "PDWPane2Class";
char gszPane2LabelClass[22] = "PDWPane2LabelClass";
char gszColorClass[17] = "PDWColorClass";
char gszACARSColorClass[19] = "PDWACARSColorClass";
char gszMOBITEXColorClass[21] = "PDWMOBITEXColorClass";
char gszERMESColorClass[19] = "PDWERMESColorClass";

LPCTSTR lpszSourceFileName = __FILE__;
TCHAR szDialogErrorMsg[80] = {0};
TCHAR szCenterOpenDlgMsg[80] = {0};

TCHAR szPath[MAX_PATH];
TCHAR szLogPathName[MAX_PATH];
TCHAR szWavePathName[MAX_PATH];
TCHAR szExePathName[MAX_PATH];
TCHAR szHelpPathName[MAX_PATH];
TCHAR szIniPathName[MAX_PATH];
TCHAR szFilterPathName[MAX_PATH];
TCHAR szFilterBackup[MAX_PATH];
TCHAR szVolPathName[MAX_PATH];
FILE *pClipboardFile = NULL;

/* Max chars per line (wrap point). Windows: NewLinePoint = iMaxWidth/cxChar. We use full buffer line. */
int NewLinePoint = 180;

char szWindowText[6][1000];

PROFILE Profile;

static void set_profile_defaults(void)
{
	memset(&Profile, 0, sizeof(Profile));
	Profile.monitor_paging = TRUE;
	Profile.decodepocsag = 1;
	Profile.decodeflex = 1;
	Profile.pocsag_512 = 1;
	Profile.pocsag_1200 = 1;
	Profile.pocsag_2400 = 1;
	Profile.flex_1600 = 1;
	Profile.flex_3200 = 1;
	Profile.flex_6400 = 1;
	Profile.showtone = 1;
	Profile.shownumeric = 1;
	Profile.showmisc = 1;
	Profile.audioEnabled = 1;
	Profile.audioSampleRate = 44100;
	Profile.audioChannels = 1;
	Profile.pane1_size = 500;
	Profile.pane2_size = 500;
	Profile.show_cfs = 1;
	/* Default column order: Address, Time, Date, Mode, Type, Bitrate, Message (so messages are shown). */
	Profile.ScreenColumns[0] = 1;
	Profile.ScreenColumns[1] = 2;
	Profile.ScreenColumns[2] = 3;
	Profile.ScreenColumns[3] = 4;
	Profile.ScreenColumns[4] = 5;
	Profile.ScreenColumns[5] = 6;
	Profile.ScreenColumns[6] = 7;
}

static void set_paths(void)
{
	char cwd[MAX_PATH];
	if (getcwd(cwd, sizeof(cwd)))
		strncpy(szPath, cwd, MAX_PATH - 1);
	else
		szPath[0] = '\0';
	szPath[MAX_PATH - 1] = '\0';

	snprintf(szLogPathName, MAX_PATH, "%s/Logfiles", szPath);
	snprintf(szWavePathName, MAX_PATH, "%s/Wavfiles", szPath);
	snprintf(szExePathName, MAX_PATH, "%s/pdw", szPath);
	snprintf(szIniPathName, MAX_PATH, "%s/pdw.ini", szPath);
	snprintf(szFilterPathName, MAX_PATH, "%s/filters.ini", szPath);
	snprintf(szFilterBackup, MAX_PATH, "%s/filters.bak", szPath);
	szHelpPathName[0] = '\0';
}

UINT GetPathFromFullPathName(LPCTSTR lpFullPathName, LPTSTR lpPathBuffer, UINT nPathBufferLength)
{
	size_t n = strlen(lpFullPathName);
	if (n >= nPathBufferLength) return (UINT)n;
	strcpy(lpPathBuffer, lpFullPathName);
	int i = (int)n - 1;
	while (i >= 0 && lpPathBuffer[i] != '/' && lpPathBuffer[i] != '\\') i--;
	if (i >= 0) lpPathBuffer[i] = '\0';
	return (UINT)i;
}

BOOL InitApplication(HINSTANCE hInstance)
{
	(void)hInstance;
	set_profile_defaults();
	set_paths();
	return TRUE;
}

HWND InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	(void)hInstance;
	(void)nCmdShow;
	return ghWnd;
}

void InitializePane(PaneStruct *pane)
{
	pane->Bottom = 0;
	pane->currentPos = 0;
	pane->currentColor = 0;
	pane->iVscrollPos = 0;
	pane->iVscrollMax = 0;
	pane->iHscrollPos = 0;
	pane->iHscrollMax = 0;
	if (pane->buff_char && pane->buff_lines) {
		for (unsigned int x = 0; x < pane->buff_lines; x++) {
			pane->buff_char[x * (LINE_SIZE + 1)] = 0;
			pane->buff_color[x * (LINE_SIZE + 1)] = 0;
		}
	}
}

void pdw_linux_init_panes(void)
{
	unsigned int p1 = (Profile.pane1_size > 0) ? (unsigned)Profile.pane1_size : 500;
	unsigned int p2 = (Profile.pane2_size > 0) ? (unsigned)Profile.pane2_size : 500;

	size_t m1 = (p1 + 1) * (LINE_SIZE + 1);
	size_t m2 = (p2 + 1) * (LINE_SIZE + 1);

	Pane1.hWnd = NULL;
	Pane1.buff_lines = p1;
	Pane1.cyLines = 24;
	Pane1.cxClient = 800;
	Pane1.cyClient = 400;
	Pane1.buff_char = (char*)malloc(m1);
	Pane1.buff_color = (BYTE*)malloc(m1);
	memset(Pane1.buff_char, 0, m1);
	memset(Pane1.buff_color, 0, m1);

	Pane2.hWnd = NULL;
	Pane2.buff_lines = p2;
	Pane2.cyLines = 24;
	Pane2.cxClient = 800;
	Pane2.cyClient = 400;
	Pane2.buff_char = (char*)malloc(m2);
	Pane2.buff_color = (BYTE*)malloc(m2);
	memset(Pane2.buff_char, 0, m2);
	memset(Pane2.buff_color, 0, m2);

	InitializePane(&Pane1);
	InitializePane(&Pane2);
}

void ClearPanes(bool bPane1, bool bPane2)
{
	if (bPane1) InitializePane(&Pane1);
	if (bPane2) InitializePane(&Pane2);
}
