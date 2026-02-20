/*
 * Native Linux implementation of the PDW platform API.
 * Output: stdout for decoded lines, stderr for mode, optional log file.
 * Verbose logging uses ANSI colors when stderr is a TTY.
 */

#include "platform/pdw_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static FILE *s_log_file = NULL;
static void (*s_pane_refresh_cb)(void) = NULL;
static int s_verbose = 0;

/* ANSI (only when stderr is a TTY) */
#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_DIM     "\033[2m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_BLUE    "\033[34m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_BR_CYAN "\033[96m"
#define ANSI_BR_BLUE "\033[94m"

static int stderr_has_color(void)
{
	return isatty(STDERR_FILENO);
}

void pdw_platform_set_verbose(int level)
{
	s_verbose = (level < 0) ? 0 : ((level > 2) ? 2 : level);
}

int pdw_platform_verbose(void)
{
	return s_verbose;
}

void pdw_platform_set_log_file(const char *path)
{
	if (s_log_file) { fclose(s_log_file); s_log_file = NULL; }
	if (path && path[0])
		s_log_file = fopen(path, "a");
}

void pdw_platform_flush_line(const char *line_text)
{
	if (!line_text) return;
	if (s_verbose >= 1) {
		if (stderr_has_color())
			fprintf(stderr, ANSI_BR_CYAN "▌" ANSI_RESET " " ANSI_CYAN "%s" ANSI_RESET "\n", line_text);
		else
			fprintf(stderr, "[LINE] %s\n", line_text);
	}
	fputs(line_text, stdout);
	fputc('\n', stdout);
	fflush(stdout);
	if (s_log_file) {
		fputs(line_text, s_log_file);
		fputc('\n', s_log_file);
		fflush(s_log_file);
	}
	if (s_pane_refresh_cb) {
		if (s_verbose >= 2) {
			if (stderr_has_color())
				fprintf(stderr, ANSI_DIM "▌ display refresh" ANSI_RESET "\n");
			else
				fprintf(stderr, "[DISPLAY] scheduling pane refresh\n");
		}
		s_pane_refresh_cb();
	}
}

void pdw_platform_register_pane_refresh_cb(void (*cb)(void))
{
	s_pane_refresh_cb = cb;
}

static const char* mode_string(int mode)
{
	if (mode == 0) return "IDLE";
	if (mode & 0x20) { /* POCSAG */
		if (mode & 0x01) return "POCSAG-512";
		if (mode & 0x02) return "POCSAG-1200";
		if (mode & 0x04) return "POCSAG-2400";
		return "POCSAG";
	}
	if (mode & 0x80) return "ACARS 2400";
	if (mode & 0x40) return "MOBITEX";
	if (mode & 0x30) return "ERMES 6250";
	if (mode & 0x08) return "FLEX"; /* and variants */
	return "?";
}

void pdw_platform_show_mode(int mode)
{
	const char *s = mode_string(mode);
	if (s_verbose >= 1) {
		if (stderr_has_color())
			fprintf(stderr, ANSI_YELLOW "◆" ANSI_RESET " " ANSI_BOLD ANSI_GREEN "%s" ANSI_RESET "\n", s);
		else
			fprintf(stderr, "[MODE] %s\n", s);
	} else {
		fprintf(stderr, "[MODE] %s\n", s);
	}
}

void pdw_platform_log_ui(const char *action, const char *detail)
{
	if (s_verbose < 1 || !action) return;
	if (stderr_has_color())
		fprintf(stderr, ANSI_BR_BLUE "▶" ANSI_RESET " " ANSI_BOLD "%s" ANSI_RESET "%s%s\n",
			action, detail ? "  " ANSI_DIM : "", detail ? detail : "");
	else
		fprintf(stderr, "[UI] %s%s%s\n", action, detail ? "  " : "", detail ? detail : "");
}

void pdw_platform_signal_indicator(int direction)
{
	(void)direction;
	/* Optional: could drive a simple CLI meter later */
}

void pdw_platform_get_time(int *year, int *month, int *day,
                           int *hour, int *min, int *sec)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	if (!tm) {
		if (year) *year = 0;
		if (month) *month = 0;
		if (day) *day = 0;
		if (hour) *hour = 0;
		if (min) *min = 0;
		if (sec) *sec = 0;
		return;
	}
	if (year)  *year  = tm->tm_year + 1900;
	if (month) *month = tm->tm_mon + 1;
	if (day)   *day   = tm->tm_mday;
	if (hour)  *hour  = tm->tm_hour;
	if (min)   *min   = tm->tm_min;
	if (sec)   *sec   = tm->tm_sec;
}

const char *pdw_platform_pocsag_decrypt_key(void)
{
	const char *k = getenv("PDW_POCSAG_DECRYPT_KEY");
	return (k && k[0]) ? k : NULL;
}
