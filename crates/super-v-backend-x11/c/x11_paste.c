#include "x11_paste.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xfixes.h>
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

    XTestFakeKeyEvent(display, ctrl_key, True, 0);
    XTestFakeKeyEvent(display, v_key, True, 0);
    XTestFakeKeyEvent(display, v_key, False, 0);
    XTestFakeKeyEvent(display, ctrl_key, False, 0);

    XFlush(display);
    XCloseDisplay(display);
    return 0;
}

int super_v_x11_listen_clipboard_events(void (*on_changed)(void)) {
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        return -1;
    }

    int event_base, error_base;
    if (!XFixesQueryExtension(display, &event_base, &error_base)) {
        XCloseDisplay(display);
        return -2;
    }

    Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
    Window root = DefaultRootWindow(display);
    XFixesSelectSelectionInput(display, root, clipboard, XFixesSetSelectionOwnerNotifyMask);

    XEvent ev;
    while (1) {
        XNextEvent(display, &ev);
        if (ev.type == event_base + XFixesSelectionNotify) {
            if (on_changed) {
                on_changed();
            }
        }
    }

    XCloseDisplay(display);
    return 0;
}
