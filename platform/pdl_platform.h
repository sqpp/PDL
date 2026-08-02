#ifndef PDL_PLATFORM_H
#define PDL_PLATFORM_H

/*
 * PDL platform abstraction — same API, two native implementations.
 * Windows: uses Win32 (GDI, window text, etc.)
 * Linux: uses stdout, log file, stderr.
 * No fake Windows layer; this is the contract the decoding core uses.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Output one completed decoded message line (null-terminated). */
void pdl_platform_flush_line(const char *line_text);

/* Update displayed decode mode (e.g. "POCSAG-1200", "FLEX 1600", "IDLE"). */
void pdl_platform_show_mode(int mode);

/* Optional: signal strength / indicator (Linux may no-op). */
void pdl_platform_signal_indicator(int direction);

/* Get current local time for logging/stats. Fills 0 on error. */
void pdl_platform_get_time(int *year, int *month, int *day,
                          int *hour, int *min, int *sec);

/* Linux-only: set log file path for decoded lines (NULL = no file). */
void pdl_platform_set_log_file(const char *path);

/* Linux-only: register callback to refresh GUI panes (called when a line is flushed). */
void pdl_platform_register_pane_refresh_cb(void (*cb)(void));

/* Verbose level: 0=off, 1=log decoded lines and mode, 2=full (display pipeline debug). */
void pdl_platform_set_verbose(int level);
int pdl_platform_verbose(void);

/* Verbose UI logging: when level >= 1, log user actions (menu/toolbar) with colors to stderr. */
void pdl_platform_log_ui(const char *action, const char *detail);

/* POCSAG decryption key (pocsag-golang compatible). NULL = no decryption.
 * Env PDL_POCSAG_DECRYPT_KEY (or legacy PDL_POCSAG_DECRYPT_KEY) overrides config/ini key. */
const char *pdl_platform_pocsag_decrypt_key(void);
/* Set decrypt key from config (ignored if env is set). */
void pdl_platform_set_pocsag_decrypt_key(const char *key);

#ifdef __cplusplus
}
#endif

#endif
