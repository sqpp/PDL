#ifndef PDW_PLATFORM_H
#define PDW_PLATFORM_H

/*
 * PDW platform abstraction — same API, two native implementations.
 * Windows: uses Win32 (GDI, window text, etc.)
 * Linux: uses stdout, log file, stderr.
 * No fake Windows layer; this is the contract the decoding core uses.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Output one completed decoded message line (null-terminated). */
void pdw_platform_flush_line(const char *line_text);

/* Update displayed decode mode (e.g. "POCSAG-1200", "FLEX 1600", "IDLE"). */
void pdw_platform_show_mode(int mode);

/* Optional: signal strength / indicator (Linux may no-op). */
void pdw_platform_signal_indicator(int direction);

/* Get current local time for logging/stats. Fills 0 on error. */
void pdw_platform_get_time(int *year, int *month, int *day,
                          int *hour, int *min, int *sec);

/* Linux-only: set log file path for decoded lines (NULL = no file). */
void pdw_platform_set_log_file(const char *path);

/* Linux-only: register callback to refresh GUI panes (called when a line is flushed). */
void pdw_platform_register_pane_refresh_cb(void (*cb)(void));

/* Verbose level: 0=off, 1=log decoded lines and mode, 2=full (display pipeline debug). */
void pdw_platform_set_verbose(int level);
int pdw_platform_verbose(void);

/* Verbose UI logging: when level >= 1, log user actions (menu/toolbar) with colors to stderr. */
void pdw_platform_log_ui(const char *action, const char *detail);

/* POCSAG decryption key (pocsag-golang compatible). NULL = no decryption. From env PDW_POCSAG_DECRYPT_KEY. */
const char *pdw_platform_pocsag_decrypt_key(void);

#ifdef __cplusplus
}
#endif

#endif
