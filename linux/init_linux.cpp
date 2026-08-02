/*
 * Linux init: globals and pane setup that Initapp.cpp provides on Windows.
 * No Windows API; paths from getcwd/argv[0].
 * Include Linux types first so pdl.h sees correct typedefs.
 */
#ifdef __linux__
#include "pdl_linux_types.h"
#define NEAR
#endif
#include "pdl.h"
#include "initapp.h"
#include "gfx.h"
#include "Headers/sound_in.h"
#include "Headers/acars.h"
#include "Headers/gfx.h"
#include "Headers/misc.h"
#include "platform/pdl_platform.h"
#include "utils/smtp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

extern const char *pdl_linux_get_ini_decrypt_key(void);
extern "C" int OpenComPort(void);

#define LINE_SIZE 180

PaneStruct Pane1;
PaneStruct Pane2;

HINSTANCE ghInstance = NULL;
HWND    ghWnd = (HWND)1;
HWND    hToolbar = NULL;
HACCEL  ghAccel = NULL;
HMENU   ghMenu = NULL;

TCHAR   szAppName[128] = "PDL";
TCHAR   szShortAppName[32] = "PDL";
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

char gszPDLClass[15] = "PDLWndClass";
char gszPane1Class[17] = "PDLPane1Class";
char gszPane2Class[17] = "PDLPane2Class";
char gszPane2LabelClass[22] = "PDLPane2LabelClass";
char gszColorClass[17] = "PDLColorClass";
char gszACARSColorClass[19] = "PDLACARSColorClass";
char gszMOBITEXColorClass[21] = "PDLMOBITEXColorClass";
char gszERMESColorClass[19] = "PDLERMESColorClass";

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
	strncpy(Profile.pagercast_host_suffix, "pagercast.com", sizeof(Profile.pagercast_host_suffix) - 1);
	Profile.monitor_paging = TRUE;
	Profile.decodepocsag = 1;
	Profile.decodeflex = 0;
	Profile.pocsag_512 = 1;
	Profile.pocsag_1200 = 1;
	Profile.pocsag_2400 = 1;
	Profile.flex_1600 = 0;
	Profile.flex_3200 = 0;
	Profile.flex_6400 = 0;
	Profile.showtone = 1;
	Profile.shownumeric = 1;
	Profile.showmisc = 1;
	Profile.audioEnabled = 1;
	/* PagerCast / pocsag-golang audio is 48000 Hz — bit timing must match. */
	Profile.audioSampleRate = 48000;
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
	Profile.ScreenColumns[7] = 0;
	pdl_apply_message_column_layout();

	/* Mockup-1 light message pane defaults (readable on white). */
	Profile.color_background	= RGB(255, 255, 255);
	Profile.color_address		= RGB(0, 0, 0);
	Profile.color_modetypebit	= RGB(170, 0, 0);
	Profile.color_timestamp		= RGB(0, 90, 160);
	Profile.color_numeric		= RGB(180, 0, 0);
	Profile.color_message		= RGB(25, 25, 25);
	Profile.color_misc			= RGB(110, 75, 20);
	Profile.color_biterrors		= RGB(130, 130, 130);
	Profile.color_filtermatch	= RGB(0, 130, 40);
	Profile.color_instructions	= RGB(0, 110, 130);
	Profile.color_ac_message_nr	= RGB(0, 0, 0);
	Profile.color_ac_dbi		= RGB(170, 0, 0);
	Profile.color_mb_sender		= RGB(170, 0, 0);
	Profile.color_filterlabel[0]	= RGB(0, 90, 160);
	Profile.color_filterlabel[1]	= RGB(160, 120, 0);
	Profile.color_filterlabel[2]	= RGB(170, 0, 0);
	Profile.color_filterlabel[3]	= RGB(0xFF, 0xAA, 0x00);
	Profile.color_filterlabel[4]	= RGB(0, 90, 160);
	Profile.color_filterlabel[5]	= RGB(0, 120, 140);
	Profile.color_filterlabel[6]	= RGB(40, 40, 40);
	Profile.color_filterlabel[7]	= RGB(0, 130, 40);
	Profile.color_filterlabel[8]	= RGB(100, 100, 100);
	Profile.color_filterlabel[9]	= RGB(110, 75, 20);
	Profile.color_filterlabel[10]	= RGB(0, 110, 130);
	Profile.color_filterlabel[11]	= RGB(0x00, 0x33, 0x99);
	Profile.color_filterlabel[12]	= RGB(0xC0, 0x00, 0xC0);
	Profile.color_filterlabel[13]	= RGB(0x33, 0x99, 0x66);
	Profile.color_filterlabel[14]	= RGB(0xC0, 0x60, 0x90);
	Profile.color_filterlabel[15]	= RGB(0x40, 0x90, 0x90);
	Profile.color_filterlabel[16]	= RGB(0x40, 0xA0, 0x70);
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
	snprintf(szExePathName, MAX_PATH, "%s/pdl", szPath);
	snprintf(szIniPathName, MAX_PATH, "%s/pdl.ini", szPath);
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
	/* Discriminator preset so bit thresholds are non-zero. */
	Profile.audioConfig = 1;
	{
		char load_ini[MAX_PATH];
		strncpy(load_ini, szIniPathName, MAX_PATH - 1);
		load_ini[MAX_PATH - 1] = '\0';
		if (access(load_ini, R_OK) != 0) {
			char legacy[MAX_PATH];
			snprintf(legacy, MAX_PATH, "%s/pdw.ini", szPath);
			if (access(legacy, R_OK) == 0)
				strncpy(load_ini, legacy, MAX_PATH - 1);
		}
		GetPrivateProfileSettings(szAppName, load_ini, &Profile);
	}
	ReadFilters(szFilterPathName, &Profile, false);
	{
		const char *dk = pdl_linux_get_ini_decrypt_key();
		if (dk) pdl_platform_set_pocsag_decrypt_key(dk);
	}
	SetAudioConfig(Profile.audioConfig > 0 ? Profile.audioConfig : 1);
	acars.read_data();
	if (Profile.SMTP || (Profile.nMailOptions & 0x008000)) {
		MailInit(Profile.szMailHost, Profile.szMailHeloDomain, Profile.szMailFrom,
			Profile.szMailTo, Profile.szMailUser, Profile.szMailPassword,
			Profile.iMailPort, Profile.nMailOptions | 0x008000);
	}
	if (Profile.comPortEnabled)
		OpenComPort();
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

void pdl_linux_init_panes(void)
{
	free(Pane1.buff_char);
	free(Pane1.buff_color);
	free(Pane2.buff_char);
	free(Pane2.buff_color);
	Pane1.buff_char = Pane2.buff_char = NULL;
	Pane1.buff_color = Pane2.buff_color = NULL;

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
