#ifndef SUPER_V_X11_PASTE_H
#define SUPER_V_X11_PASTE_H

#ifdef __cplusplus
extern "C" {
#endif

int super_v_x11_is_available(void);
int super_v_x11_fake_paste(void);
int super_v_x11_listen_clipboard_events(void (*on_changed)(void));

#ifdef __cplusplus
}
#endif

#endif // SUPER_V_X11_PASTE_H
