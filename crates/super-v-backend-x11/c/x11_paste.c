#include "x11_paste.h"
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <unistd.h>

int super_v_x11_is_available(void) {
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        return 0;
    }
    int event_base, error_base, major, minor;
    int has_xtest = XTestQueryExtension(display, &event_base, &error_base, &major, &minor);
    XCloseDisplay(display);
    return has_xtest ? 1 : 0;
}

int super_v_x11_fake_paste(void) {
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        return -1;
    }

    KeyCode ctrl_key = XKeysymToKeycode(display, XK_Control_L);
    KeyCode v_key = XKeysymToKeycode(display, XK_v);

    if (!ctrl_key || !v_key) {
        XCloseDisplay(display);
        return -2;
    }

    // Ctrl Down
    XTestFakeKeyEvent(display, ctrl_key, True, 0);
    // V Down
    XTestFakeKeyEvent(display, v_key, True, 0);
    // V Up
    XTestFakeKeyEvent(display, v_key, False, 0);
    // Ctrl Up
    XTestFakeKeyEvent(display, ctrl_key, False, 0);

    XFlush(display);
    XCloseDisplay(display);
    return 0;
}
