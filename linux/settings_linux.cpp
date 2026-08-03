/*
 * Linux settings persistence: pdl.ini (PROFILE) and filters.ini (FILTERLIST).
 * Uses GLib GKeyFile (already linked via GTK3).
 */
#ifdef __linux__
#include "pdl_linux_types.h"
#include "pdl.h"
#include "initapp.h"
#include "Headers/sound_in.h"
#include "linux/gui_gtk.h"
#include "platform/pdl_platform.h"
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

extern bool bUpdateFilters;
extern char *pdl_version;

/* Optional decrypt key from ini (env PDL_POCSAG_DECRYPT_KEY overrides). */
static char s_ini_decrypt_key[256] = {0};

void pdl_linux_set_ini_decrypt_key(const char *key)
{
	if (!key) { s_ini_decrypt_key[0] = '\0'; return; }
	strncpy(s_ini_decrypt_key, key, sizeof(s_ini_decrypt_key) - 1);
	s_ini_decrypt_key[sizeof(s_ini_decrypt_key) - 1] = '\0';
	pdl_platform_set_pocsag_decrypt_key(s_ini_decrypt_key);
}

const char *pdl_linux_get_ini_decrypt_key(void)
{
	return s_ini_decrypt_key[0] ? s_ini_decrypt_key : NULL;
}

static void kf_set_int(GKeyFile *kf, const char *group, const char *key, int val)
{
	g_key_file_set_integer(kf, group, key, val);
}

static int kf_get_int(GKeyFile *kf, const char *group, const char *key, int def)
{
	GError *err = NULL;
	int v = g_key_file_get_integer(kf, group, key, &err);
	if (err) { g_error_free(err); return def; }
	return v;
}

static void kf_set_str(GKeyFile *kf, const char *group, const char *key, const char *val)
{
	g_key_file_set_string(kf, group, key, val ? val : "");
}

static void kf_get_str(GKeyFile *kf, const char *group, const char *key, char *buf, size_t buflen)
{
	GError *err = NULL;
	gchar *s = g_key_file_get_string(kf, group, key, &err);
	if (err || !s) {
		if (err) g_error_free(err);
		if (buflen) buf[0] = '\0';
		return;
	}
	strncpy(buf, s, buflen - 1);
	buf[buflen - 1] = '\0';
	g_free(s);
}

BOOL GetPrivateProfileSettings(LPCTSTR lpszAppTitle, LPCTSTR lpszIniPathName, PPROFILE pProfile)
{
	(void)lpszAppTitle;
	if (!pProfile || !lpszIniPathName || !lpszIniPathName[0]) return FALSE;

	GKeyFile *kf = g_key_file_new();
	GError *err = NULL;
	if (!g_key_file_load_from_file(kf, lpszIniPathName, G_KEY_FILE_NONE, &err)) {
		if (err) g_error_free(err);
		g_key_file_free(kf);
		return FALSE;
	}

	pProfile->decodepocsag = kf_get_int(kf, "POCSAG", "Enable", pProfile->decodepocsag);
	pProfile->pocsag_512 = kf_get_int(kf, "POCSAG", "Baud512", pProfile->pocsag_512);
	pProfile->pocsag_1200 = kf_get_int(kf, "POCSAG", "Baud1200", pProfile->pocsag_1200);
	pProfile->pocsag_2400 = kf_get_int(kf, "POCSAG", "Baud2400", pProfile->pocsag_2400);
	pProfile->pocsag_fnu = kf_get_int(kf, "POCSAG", "FNU", pProfile->pocsag_fnu);
	pProfile->pocsag_showboth = kf_get_int(kf, "POCSAG", "ShowBoth", pProfile->pocsag_showboth);
	kf_get_str(kf, "POCSAG", "DecryptKey", s_ini_decrypt_key, sizeof(s_ini_decrypt_key));

	/* FLEX / ACARS / MOBITEX / ERMES are not enabled on Linux (unverified). Ignore Enable=1. */
	(void)kf_get_int(kf, "FLEX", "Enable", 0);
	pProfile->decodeflex = 0;
	pProfile->flex_1600 = kf_get_int(kf, "FLEX", "Baud1600", pProfile->flex_1600);
	pProfile->flex_3200 = kf_get_int(kf, "FLEX", "Baud3200", pProfile->flex_3200);
	pProfile->flex_6400 = kf_get_int(kf, "FLEX", "Baud6400", pProfile->flex_6400);
	pProfile->showinstr = kf_get_int(kf, "FLEX", "ShowInstr", pProfile->showinstr);
	pProfile->convert_si = kf_get_int(kf, "FLEX", "ConvertSI", pProfile->convert_si);
	pProfile->FlexTIME = kf_get_int(kf, "FLEX", "FlexTIME", pProfile->FlexTIME);
	pProfile->show_cfs = kf_get_int(kf, "Display", "ShowCFS", pProfile->show_cfs);
	pProfile->show_rejectblocked = kf_get_int(kf, "Display", "ShowRejectBlocked", pProfile->show_rejectblocked);
	{
		char uimode[32] = {0};
		kf_get_str(kf, "Display", "UiMode", uimode, sizeof(uimode));
		if (uimode[0] == '\0')
			pProfile->ui_mode = kf_get_int(kf, "Display", "UiMode", PDL_UI_GTK);
		else if (strcasecmp(uimode, "web") == 0 || strcmp(uimode, "1") == 0)
			pProfile->ui_mode = PDL_UI_WEB;
		else
			pProfile->ui_mode = PDL_UI_GTK;
	}

	pProfile->acars_parity_check = kf_get_int(kf, "ACARS", "ParityCheck", pProfile->acars_parity_check);

	pProfile->audioSampleRate = kf_get_int(kf, "Audio", "SampleRate", pProfile->audioSampleRate);
	pProfile->audioConfig = kf_get_int(kf, "Audio", "Config", pProfile->audioConfig);
	pProfile->audioEnabled = kf_get_int(kf, "Audio", "Enabled", pProfile->audioEnabled);
	pProfile->invert = kf_get_int(kf, "Audio", "Invert", pProfile->invert);

	char cap[256] = {0}, play[256] = {0};
	kf_get_str(kf, "Audio", "CaptureDevice", cap, sizeof(cap));
	kf_get_str(kf, "Audio", "PlaybackDevice", play, sizeof(play));
	if (cap[0]) pdl_linux_gui_set_capture_device(cap);
	if (play[0]) pdl_linux_gui_set_playback_device(play);

	pProfile->monitor_paging = 1;
	pProfile->monitor_acars = 0;
	pProfile->monitor_mobitex = 0;
	pProfile->monitor_ermes = 0;
	(void)kf_get_int(kf, "Monitor", "Paging", 1);
	(void)kf_get_int(kf, "Monitor", "ACARS", 0);
	(void)kf_get_int(kf, "Monitor", "MOBITEX", 0);
	(void)kf_get_int(kf, "Monitor", "ERMES", 0);

	pProfile->xPos = kf_get_int(kf, "Window", "X", pProfile->xPos);
	pProfile->yPos = kf_get_int(kf, "Window", "Y", pProfile->yPos);
	pProfile->xSize = kf_get_int(kf, "Window", "Width", pProfile->xSize);
	pProfile->ySize = kf_get_int(kf, "Window", "Height", pProfile->ySize);
	pProfile->pane1_size = kf_get_int(kf, "Window", "Pane1Size", pProfile->pane1_size);
	pProfile->pane2_size = kf_get_int(kf, "Window", "Pane2Size", pProfile->pane2_size);
	pProfile->percent = kf_get_int(kf, "Window", "PaneSplit", pProfile->percent);

	pProfile->SMTP = kf_get_int(kf, "Mail", "Enable", pProfile->SMTP);
	kf_get_str(kf, "Mail", "Host", pProfile->szMailHost, sizeof(pProfile->szMailHost));
	kf_get_str(kf, "Mail", "Helo", pProfile->szMailHeloDomain, sizeof(pProfile->szMailHeloDomain));
	kf_get_str(kf, "Mail", "From", pProfile->szMailFrom, sizeof(pProfile->szMailFrom));
	kf_get_str(kf, "Mail", "To", pProfile->szMailTo, sizeof(pProfile->szMailTo));
	kf_get_str(kf, "Mail", "User", pProfile->szMailUser, sizeof(pProfile->szMailUser));
	kf_get_str(kf, "Mail", "Password", pProfile->szMailPassword, sizeof(pProfile->szMailPassword));
	pProfile->iMailPort = kf_get_int(kf, "Mail", "Port", pProfile->iMailPort ? pProfile->iMailPort : 25);
	pProfile->nMailOptions = kf_get_int(kf, "Mail", "Options", pProfile->nMailOptions);
	pProfile->ssl = kf_get_int(kf, "Mail", "SSL", pProfile->ssl) != 0;

	pProfile->SystemTray = kf_get_int(kf, "System", "Tray", pProfile->SystemTray);
	pProfile->SystemTrayRestore = kf_get_int(kf, "System", "TrayRestore", pProfile->SystemTrayRestore);
	pProfile->logfile_enabled = kf_get_int(kf, "Log", "Enabled", pProfile->logfile_enabled);
	kf_get_str(kf, "Log", "File", pProfile->logfile, sizeof(pProfile->logfile));

	pProfile->showtone = kf_get_int(kf, "General", "ShowTone", pProfile->showtone);
	pProfile->shownumeric = kf_get_int(kf, "General", "ShowNumeric", pProfile->shownumeric);
	pProfile->showmisc = kf_get_int(kf, "General", "ShowMisc", pProfile->showmisc);
	pProfile->filterbeep = kf_get_int(kf, "General", "FilterBeep", pProfile->filterbeep);
	pProfile->BlockDuplicate = kf_get_int(kf, "General", "BlockDuplicate", pProfile->BlockDuplicate);
	pProfile->Date_USA = kf_get_int(kf, "General", "DateUSA", pProfile->Date_USA);
	pProfile->confirmExit = kf_get_int(kf, "General", "ConfirmExit", pProfile->confirmExit);

	pProfile->fontInfo.lfHeight = kf_get_int(kf, "Display", "FontSize",
		pProfile->fontInfo.lfHeight ? pProfile->fontInfo.lfHeight : 11);

	/* Colors as "r,g,b" like original PDL.ini Color.* keys. */
	{
		char cs[32];
		int r, g, b;
		kf_get_str(kf, "Colors", "Background", cs, sizeof(cs));
		if (cs[0] && sscanf(cs, "%d,%d,%d", &r, &g, &b) == 3) pProfile->color_background = RGB(r, g, b);
		kf_get_str(kf, "Colors", "Capcode", cs, sizeof(cs));
		if (cs[0] && sscanf(cs, "%d,%d,%d", &r, &g, &b) == 3) pProfile->color_address = RGB(r, g, b);
		kf_get_str(kf, "Colors", "FLEXPhase", cs, sizeof(cs));
		if (cs[0] && sscanf(cs, "%d,%d,%d", &r, &g, &b) == 3) pProfile->color_modetypebit = RGB(r, g, b);
		kf_get_str(kf, "Colors", "Timestamp", cs, sizeof(cs));
		if (cs[0] && sscanf(cs, "%d,%d,%d", &r, &g, &b) == 3) pProfile->color_timestamp = RGB(r, g, b);
		kf_get_str(kf, "Colors", "Numeric", cs, sizeof(cs));
		if (cs[0] && sscanf(cs, "%d,%d,%d", &r, &g, &b) == 3) pProfile->color_numeric = RGB(r, g, b);
		kf_get_str(kf, "Colors", "Alphanumeric", cs, sizeof(cs));
		if (cs[0] && sscanf(cs, "%d,%d,%d", &r, &g, &b) == 3) pProfile->color_message = RGB(r, g, b);
		kf_get_str(kf, "Colors", "FLEXBinary", cs, sizeof(cs));
		if (cs[0] && sscanf(cs, "%d,%d,%d", &r, &g, &b) == 3) pProfile->color_misc = RGB(r, g, b);
		kf_get_str(kf, "Colors", "BitErrors", cs, sizeof(cs));
		if (cs[0] && sscanf(cs, "%d,%d,%d", &r, &g, &b) == 3) pProfile->color_biterrors = RGB(r, g, b);
		kf_get_str(kf, "Colors", "FilterMatch", cs, sizeof(cs));
		if (cs[0] && sscanf(cs, "%d,%d,%d", &r, &g, &b) == 3) pProfile->color_filtermatch = RGB(r, g, b);
		kf_get_str(kf, "Colors", "Instructions", cs, sizeof(cs));
		if (cs[0] && sscanf(cs, "%d,%d,%d", &r, &g, &b) == 3) pProfile->color_instructions = RGB(r, g, b);
	}

	pProfile->comPortEnabled = kf_get_int(kf, "RS232", "Enabled", pProfile->comPortEnabled);
	pProfile->comPort = kf_get_int(kf, "RS232", "Port", pProfile->comPort);
	pProfile->comRS232bitrate = kf_get_int(kf, "RS232", "Bitrate",
		pProfile->comRS232bitrate ? pProfile->comRS232bitrate : 9600);
	pProfile->comPortRS232 = kf_get_int(kf, "RS232", "DecodeMode", pProfile->comPortRS232);
	pProfile->fourlevel = kf_get_int(kf, "RS232", "FourLevel", pProfile->fourlevel);

	pProfile->pagercast_enabled = kf_get_int(kf, "PagerCast", "Enable", pProfile->pagercast_enabled);
	pProfile->pagercast_subscriber_id = kf_get_int(kf, "PagerCast", "SubscriberId", pProfile->pagercast_subscriber_id);
	kf_get_str(kf, "PagerCast", "ApiBase", pProfile->pagercast_api_base, sizeof(pProfile->pagercast_api_base));
	kf_get_str(kf, "PagerCast", "Frequency", pProfile->pagercast_frequency, sizeof(pProfile->pagercast_frequency));
	kf_get_str(kf, "PagerCast", "ApiKey", pProfile->pagercast_api_key, sizeof(pProfile->pagercast_api_key));
	kf_get_str(kf, "PagerCast", "HostSuffix", pProfile->pagercast_host_suffix, sizeof(pProfile->pagercast_host_suffix));

	{
		extern int nCount_Messages, nCount_Groupcalls, nCount_Rejected, nCount_Blocked;
		extern int nCount_CleanRx, nCount_CorruptRx;
		extern int nCount_Missed[2];
		nCount_Messages = kf_get_int(kf, "Stats", "Messages", nCount_Messages);
		nCount_Groupcalls = kf_get_int(kf, "Stats", "Groupcalls", nCount_Groupcalls);
		nCount_Rejected = kf_get_int(kf, "Stats", "Rejected", nCount_Rejected);
		nCount_Blocked = kf_get_int(kf, "Stats", "Blocked", nCount_Blocked);
		nCount_CleanRx = kf_get_int(kf, "Stats", "CleanRx", nCount_CleanRx);
		nCount_CorruptRx = kf_get_int(kf, "Stats", "CorruptRx", nCount_CorruptRx);
		nCount_Missed[0] = kf_get_int(kf, "Stats", "Missed0", nCount_Missed[0]);
		nCount_Missed[1] = kf_get_int(kf, "Stats", "Missed1", nCount_Missed[1]);
	}

	g_key_file_free(kf);
	if (s_ini_decrypt_key[0])
		pdl_platform_set_pocsag_decrypt_key(s_ini_decrypt_key);
	return TRUE;
}

void WriteSettings(void)
{
	if (!szIniPathName[0]) return;

	/* Never persist unverified protocols as enabled. */
	Profile.decodeflex = 0;
	Profile.monitor_paging = 1;
	Profile.monitor_acars = 0;
	Profile.monitor_mobitex = 0;
	Profile.monitor_ermes = 0;

	GKeyFile *kf = g_key_file_new();
	/* Keep existing keys when possible */
	g_key_file_load_from_file(kf, szIniPathName, G_KEY_FILE_KEEP_COMMENTS, NULL);

	kf_set_int(kf, "POCSAG", "Enable", Profile.decodepocsag);
	kf_set_int(kf, "POCSAG", "Baud512", Profile.pocsag_512);
	kf_set_int(kf, "POCSAG", "Baud1200", Profile.pocsag_1200);
	kf_set_int(kf, "POCSAG", "Baud2400", Profile.pocsag_2400);
	kf_set_int(kf, "POCSAG", "FNU", Profile.pocsag_fnu);
	kf_set_int(kf, "POCSAG", "ShowBoth", Profile.pocsag_showboth);
	kf_set_str(kf, "POCSAG", "DecryptKey", s_ini_decrypt_key);

	kf_set_int(kf, "FLEX", "Enable", 0);
	kf_set_int(kf, "FLEX", "Baud1600", Profile.flex_1600);
	kf_set_int(kf, "FLEX", "Baud3200", Profile.flex_3200);
	kf_set_int(kf, "FLEX", "Baud6400", Profile.flex_6400);
	kf_set_int(kf, "FLEX", "ShowInstr", Profile.showinstr);
	kf_set_int(kf, "FLEX", "ConvertSI", Profile.convert_si);
	kf_set_int(kf, "FLEX", "FlexTIME", Profile.FlexTIME);
	kf_set_int(kf, "Display", "ShowCFS", Profile.show_cfs);
	kf_set_int(kf, "Display", "ShowRejectBlocked", Profile.show_rejectblocked);
	kf_set_str(kf, "Display", "UiMode", Profile.ui_mode == PDL_UI_WEB ? "web" : "gtk");

	kf_set_int(kf, "ACARS", "ParityCheck", Profile.acars_parity_check);

	kf_set_int(kf, "Audio", "SampleRate", Profile.audioSampleRate);
	kf_set_int(kf, "Audio", "Config", Profile.audioConfig);
	kf_set_int(kf, "Audio", "Enabled", Profile.audioEnabled);
	kf_set_int(kf, "Audio", "Invert", Profile.invert);
	{
		const char *cap = pdl_linux_gui_get_capture_device();
		const char *play = pdl_linux_gui_get_playback_device();
		kf_set_str(kf, "Audio", "CaptureDevice", cap ? cap : "default");
		kf_set_str(kf, "Audio", "PlaybackDevice", play ? play : "default");
	}

	kf_set_int(kf, "Monitor", "Paging", Profile.monitor_paging ? 1 : 0);
	kf_set_int(kf, "Monitor", "ACARS", Profile.monitor_acars ? 1 : 0);
	kf_set_int(kf, "Monitor", "MOBITEX", Profile.monitor_mobitex ? 1 : 0);
	kf_set_int(kf, "Monitor", "ERMES", Profile.monitor_ermes ? 1 : 0);

	kf_set_int(kf, "Window", "X", Profile.xPos);
	kf_set_int(kf, "Window", "Y", Profile.yPos);
	kf_set_int(kf, "Window", "Width", Profile.xSize);
	kf_set_int(kf, "Window", "Height", Profile.ySize);
	kf_set_int(kf, "Window", "Pane1Size", Profile.pane1_size);
	kf_set_int(kf, "Window", "Pane2Size", Profile.pane2_size);
	kf_set_int(kf, "Window", "PaneSplit", Profile.percent);

	kf_set_int(kf, "Mail", "Enable", Profile.SMTP);
	kf_set_str(kf, "Mail", "Host", Profile.szMailHost);
	kf_set_str(kf, "Mail", "Helo", Profile.szMailHeloDomain);
	kf_set_str(kf, "Mail", "From", Profile.szMailFrom);
	kf_set_str(kf, "Mail", "To", Profile.szMailTo);
	kf_set_str(kf, "Mail", "User", Profile.szMailUser);
	kf_set_str(kf, "Mail", "Password", Profile.szMailPassword);
	kf_set_int(kf, "Mail", "Port", Profile.iMailPort);
	kf_set_int(kf, "Mail", "Options", Profile.nMailOptions);
	kf_set_int(kf, "Mail", "SSL", Profile.ssl ? 1 : 0);

	kf_set_int(kf, "System", "Tray", Profile.SystemTray);
	kf_set_int(kf, "System", "TrayRestore", Profile.SystemTrayRestore);
	kf_set_int(kf, "Log", "Enabled", Profile.logfile_enabled);
	kf_set_str(kf, "Log", "File", Profile.logfile);

	kf_set_int(kf, "General", "ShowTone", Profile.showtone);
	kf_set_int(kf, "General", "ShowNumeric", Profile.shownumeric);
	kf_set_int(kf, "General", "ShowMisc", Profile.showmisc);
	kf_set_int(kf, "General", "FilterBeep", Profile.filterbeep);
	kf_set_int(kf, "General", "BlockDuplicate", Profile.BlockDuplicate);
	kf_set_int(kf, "General", "DateUSA", Profile.Date_USA);
	kf_set_int(kf, "General", "ConfirmExit", Profile.confirmExit);

	kf_set_int(kf, "Display", "FontSize",
		Profile.fontInfo.lfHeight ? Profile.fontInfo.lfHeight : 11);
	{
		char cs[32];
		snprintf(cs, sizeof(cs), "%d,%d,%d", GetRValue(Profile.color_background), GetGValue(Profile.color_background), GetBValue(Profile.color_background));
		kf_set_str(kf, "Colors", "Background", cs);
		snprintf(cs, sizeof(cs), "%d,%d,%d", GetRValue(Profile.color_address), GetGValue(Profile.color_address), GetBValue(Profile.color_address));
		kf_set_str(kf, "Colors", "Capcode", cs);
		snprintf(cs, sizeof(cs), "%d,%d,%d", GetRValue(Profile.color_modetypebit), GetGValue(Profile.color_modetypebit), GetBValue(Profile.color_modetypebit));
		kf_set_str(kf, "Colors", "FLEXPhase", cs);
		snprintf(cs, sizeof(cs), "%d,%d,%d", GetRValue(Profile.color_timestamp), GetGValue(Profile.color_timestamp), GetBValue(Profile.color_timestamp));
		kf_set_str(kf, "Colors", "Timestamp", cs);
		snprintf(cs, sizeof(cs), "%d,%d,%d", GetRValue(Profile.color_numeric), GetGValue(Profile.color_numeric), GetBValue(Profile.color_numeric));
		kf_set_str(kf, "Colors", "Numeric", cs);
		snprintf(cs, sizeof(cs), "%d,%d,%d", GetRValue(Profile.color_message), GetGValue(Profile.color_message), GetBValue(Profile.color_message));
		kf_set_str(kf, "Colors", "Alphanumeric", cs);
		snprintf(cs, sizeof(cs), "%d,%d,%d", GetRValue(Profile.color_misc), GetGValue(Profile.color_misc), GetBValue(Profile.color_misc));
		kf_set_str(kf, "Colors", "FLEXBinary", cs);
		snprintf(cs, sizeof(cs), "%d,%d,%d", GetRValue(Profile.color_biterrors), GetGValue(Profile.color_biterrors), GetBValue(Profile.color_biterrors));
		kf_set_str(kf, "Colors", "BitErrors", cs);
		snprintf(cs, sizeof(cs), "%d,%d,%d", GetRValue(Profile.color_filtermatch), GetGValue(Profile.color_filtermatch), GetBValue(Profile.color_filtermatch));
		kf_set_str(kf, "Colors", "FilterMatch", cs);
		snprintf(cs, sizeof(cs), "%d,%d,%d", GetRValue(Profile.color_instructions), GetGValue(Profile.color_instructions), GetBValue(Profile.color_instructions));
		kf_set_str(kf, "Colors", "Instructions", cs);
	}

	kf_set_int(kf, "RS232", "Enabled", Profile.comPortEnabled);
	kf_set_int(kf, "RS232", "Port", Profile.comPort);
	kf_set_int(kf, "RS232", "Bitrate", Profile.comRS232bitrate);
	kf_set_int(kf, "RS232", "DecodeMode", Profile.comPortRS232);
	kf_set_int(kf, "RS232", "FourLevel", Profile.fourlevel);

	kf_set_int(kf, "PagerCast", "Enable", Profile.pagercast_enabled);
	kf_set_int(kf, "PagerCast", "SubscriberId", Profile.pagercast_subscriber_id);
	kf_set_str(kf, "PagerCast", "ApiBase", Profile.pagercast_api_base);
	kf_set_str(kf, "PagerCast", "Frequency", Profile.pagercast_frequency);
	kf_set_str(kf, "PagerCast", "ApiKey", Profile.pagercast_api_key);
	kf_set_str(kf, "PagerCast", "HostSuffix",
		Profile.pagercast_host_suffix[0] ? Profile.pagercast_host_suffix : "pagercast.com");

	{
		extern int nCount_Messages, nCount_Groupcalls, nCount_Rejected, nCount_Blocked;
		extern int nCount_CleanRx, nCount_CorruptRx;
		extern int nCount_Missed[2];
		kf_set_int(kf, "Stats", "Messages", nCount_Messages);
		kf_set_int(kf, "Stats", "Groupcalls", nCount_Groupcalls);
		kf_set_int(kf, "Stats", "Rejected", nCount_Rejected);
		kf_set_int(kf, "Stats", "Blocked", nCount_Blocked);
		kf_set_int(kf, "Stats", "CleanRx", nCount_CleanRx);
		kf_set_int(kf, "Stats", "CorruptRx", nCount_CorruptRx);
		kf_set_int(kf, "Stats", "Missed0", nCount_Missed[0]);
		kf_set_int(kf, "Stats", "Missed1", nCount_Missed[1]);
	}

	gsize len = 0;
	gchar *data = g_key_file_to_data(kf, &len, NULL);
	if (data) {
		g_file_set_contents(szIniPathName, data, (gssize)len, NULL);
		g_free(data);
	}
	g_key_file_free(kf);
}

void WriteFilters(PPROFILE pProfile, int backup)
{
	if (!pProfile || !szFilterPathName[0]) return;
	if (backup && szFilterBackup[0]) {
		FILE *in = fopen(szFilterPathName, "rb");
		if (in) {
			FILE *out = fopen(szFilterBackup, "wb");
			if (out) {
				char buf[4096];
				size_t n;
				while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
					fwrite(buf, 1, n, out);
				fclose(out);
			}
			fclose(in);
		}
	}

	GKeyFile *kf = g_key_file_new();
	kf_set_int(kf, "General", "Count", (int)pProfile->filters.size());
	for (size_t i = 0; i < pProfile->filters.size(); i++) {
		const FILTER &f = pProfile->filters[i];
		char group[32];
		snprintf(group, sizeof(group), "Filter%zu", i);
		kf_set_int(kf, group, "Type", (int)f.type);
		kf_set_str(kf, group, "Capcode", f.capcode);
		kf_set_str(kf, group, "Label", f.label);
		kf_set_str(kf, group, "Text", f.text);
		kf_set_int(kf, group, "MatchExact", f.match_exact_msg);
		kf_set_int(kf, group, "CmdEnabled", f.cmd_enabled);
		kf_set_int(kf, group, "Reject", f.reject);
		kf_set_int(kf, group, "MonitorOnly", f.monitor_only);
		kf_set_int(kf, group, "WaveNumber", f.wave_number);
		kf_set_int(kf, group, "LabelEnabled", f.label_enabled);
		kf_set_int(kf, group, "LabelColor", f.label_color);
		kf_set_int(kf, group, "SMTP", f.smtp);
		kf_set_int(kf, group, "HitCounter", (int)f.hitcounter);
		kf_set_str(kf, group, "LastHitDate", f.lasthit_date);
		kf_set_str(kf, group, "LastHitTime", f.lasthit_time);
	}
	gsize len = 0;
	gchar *data = g_key_file_to_data(kf, &len, NULL);
	if (data) {
		g_file_set_contents(szFilterPathName, data, (gssize)len, NULL);
		g_free(data);
	}
	g_key_file_free(kf);
}

bool ReadFilters(char *szFilters, PPROFILE pProfile, bool bNew)
{
	(void)bNew;
	const char *path = (szFilters && szFilters[0]) ? szFilters : szFilterPathName;
	if (!pProfile || !path || !path[0]) return false;

	GKeyFile *kf = g_key_file_new();
	GError *err = NULL;
	if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &err)) {
		if (err) g_error_free(err);
		g_key_file_free(kf);
		return false;
	}

	pProfile->filters.clear();
	int count = kf_get_int(kf, "General", "Count", 0);
	for (int i = 0; i < count; i++) {
		char group[32];
		snprintf(group, sizeof(group), "Filter%d", i);
		FILTER f;
		memset(&f, 0, sizeof(f));
		f.type = (FILTER_TYPE)kf_get_int(kf, group, "Type", UNUSED_FILTER);
		kf_get_str(kf, group, "Capcode", f.capcode, sizeof(f.capcode));
		kf_get_str(kf, group, "Label", f.label, sizeof(f.label));
		kf_get_str(kf, group, "Text", f.text, sizeof(f.text));
		f.match_exact_msg = kf_get_int(kf, group, "MatchExact", 0);
		f.cmd_enabled = kf_get_int(kf, group, "CmdEnabled", 0);
		f.reject = kf_get_int(kf, group, "Reject", 0);
		f.monitor_only = kf_get_int(kf, group, "MonitorOnly", 0);
		f.wave_number = kf_get_int(kf, group, "WaveNumber", 0);
		f.label_enabled = kf_get_int(kf, group, "LabelEnabled", 0);
		f.label_color = kf_get_int(kf, group, "LabelColor", 0);
		f.smtp = kf_get_int(kf, group, "SMTP", 0);
		f.hitcounter = (unsigned)kf_get_int(kf, group, "HitCounter", 0);
		kf_get_str(kf, group, "LastHitDate", f.lasthit_date, sizeof(f.lasthit_date));
		kf_get_str(kf, group, "LastHitTime", f.lasthit_time, sizeof(f.lasthit_time));
		if (f.type != UNUSED_FILTER)
			pProfile->filters.push_back(f);
	}
	g_key_file_free(kf);
	return true;
}

void UpdateFilters(void)
{
	if (!bUpdateFilters) return;
	WriteFilters(&Profile, 0);
	bUpdateFilters = false;
}

#endif
