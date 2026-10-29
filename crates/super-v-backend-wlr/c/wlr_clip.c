#include "wlr_clip.h"
#include <wayland-client.h>
#include <stdlib.h>
#include <string.h>

int super_v_wayland_is_available(void) {
    const char *display_name = getenv("WAYLAND_DISPLAY");
    if (!display_name || strlen(display_name) == 0) {
        return 0;
    }
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        return 0;
    }
    wl_display_disconnect(display);
    return 1;
}
