#ifndef PDL_LINUX_GUI_GTK_H
#define PDL_LINUX_GUI_GTK_H

#ifdef __linux__
#include <gtk/gtk.h>

/* Initialize GTK and create main window with two panes. Call before run. */
int pdl_linux_gui_init(int *argc, char ***argv);
/* Run GTK main loop (blocks until window closed). */
void pdl_linux_gui_run(void);
/* Request GUI and capture to stop (e.g. from signal). */
void pdl_linux_gui_quit(void);
/* Selected audio devices (call after gui_init, e.g. before starting capture). */
const char *pdl_linux_gui_get_capture_device(void);
const char *pdl_linux_gui_get_playback_device(void);
void pdl_linux_gui_set_capture_device(const char *name);
void pdl_linux_gui_set_playback_device(const char *name);
/* Update window title from szWindowText / mode status. */
void pdl_linux_gui_update_title(void);
/* Open filter editor dialog. */
void pdl_linux_gui_show_filters(void);
/* Restart capture with the currently selected device (call after user changes device in Audio dialog). */
void pdl_linux_restart_capture(void);
/* Pause/resume ALSA/Pulse capture while PagerCast owns the audio path. */
void pdl_linux_pause_local_capture(void);
void pdl_linux_resume_local_capture(void);
/* Apply pane font/colors from Profile. */
void pdl_linux_gui_apply_display_style(void);
void pdl_linux_gui_invalidate_panes(void);
/* Keep toolbar Local/PagerCast tabs in sync; hide unless Integrations Enable is on. */
void pdl_linux_gui_sync_pagercast_toggle(void);
/* Dialogs usable from classic toolbar or Web UI (parent via set_dialog_parent). */
void pdl_linux_gui_set_dialog_parent(GtkWindow *parent);
/* Match main-window chrome + center on parent (call after creating any dialog). */
void pdl_linux_gui_prepare_dialog(GtkWidget *dlg);
/* Set window / default icon from GFX/pdl.ico (PDLICON). */
void pdl_linux_gui_apply_app_icon(GtkWindow *win);
void pdl_linux_gui_show_options(void);
void pdl_linux_gui_show_audio(void);
void pdl_linux_gui_show_stats(void);
void pdl_linux_gui_show_clear(void);
void pdl_linux_gui_show_about(void);
/* Feature dialogs */
void pdl_linux_mail_dialog(GtkWindow *parent);
void pdl_linux_general_dialog(GtkWindow *parent);
void pdl_linux_display_dialog(GtkWindow *parent);
void pdl_linux_colors_dialog(GtkWindow *parent);
#endif

#endif
