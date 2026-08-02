#ifndef PDL_LINUX_GUI_WEB_H
#define PDL_LINUX_GUI_WEB_H

#ifdef __linux__

/* Modern WebKit UI. Call instead of pdl_linux_gui_init / pdl_linux_gui_run. */
int pdl_linux_web_gui_init(int *argc, char ***argv);
void pdl_linux_web_gui_run(void);
void pdl_linux_web_gui_quit(void);

/* Persist UI mode and optionally restart into the selected UI. */
void pdl_linux_apply_ui_mode(int ui_mode, int restart_now);

#endif
#endif
