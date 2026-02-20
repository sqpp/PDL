#ifndef PDW_LINUX_GUI_GTK_H
#define PDW_LINUX_GUI_GTK_H

#ifdef __linux__
/* Initialize GTK and create main window with two panes. Call before run. */
int pdw_linux_gui_init(int *argc, char ***argv);
/* Run GTK main loop (blocks until window closed). */
void pdw_linux_gui_run(void);
/* Request GUI and capture to stop (e.g. from signal). */
void pdw_linux_gui_quit(void);
/* Selected audio devices (call after gui_init, e.g. before starting capture). */
const char *pdw_linux_gui_get_capture_device(void);
const char *pdw_linux_gui_get_playback_device(void);
/* Restart capture with the currently selected device (call after user changes device in Audio dialog). */
void pdw_linux_restart_capture(void);
#endif

#endif
