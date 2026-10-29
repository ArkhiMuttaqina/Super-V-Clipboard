#ifndef SUPER_V_OVERLAY_H
#define SUPER_V_OVERLAY_H

#ifdef __cplusplus
extern "C" {
#endif

int super_v_ui_init(int *argc, char ***argv);
void super_v_ui_check_versions(char *buf, int max_len);

// Constructs the overlay window with Adwaita widgets
void* super_v_ui_create_window(void);

// Toggles visibility of the overlay window
void super_v_ui_set_visible(void *window, int visible);

#ifdef __cplusplus
}
#endif

#endif // SUPER_V_OVERLAY_H
