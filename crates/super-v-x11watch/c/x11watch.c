/* super-v-x11watch: polls the X11 CLIPBOARD selection (bridged to the Wayland
 * clipboard by XWayland) and hands new content to super-v-ingest.
 *
 * Why this exists: on GNOME Wayland a background client may not read the
 * clipboard unless it has keyboard focus, so a resident watcher cannot poll it
 * directly. The X11 bridge has no such restriction, which is how clipboard
 * managers have always worked on X11 desktops.
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <spawn.h>

extern char **environ;

static Display *dpy;
static Window win;
static Atom prop;

static unsigned long long hash_bytes(const unsigned char *data, unsigned long len) {
    unsigned long long h = 1469598103934665603ULL;
    for (unsigned long i = 0; i < len; i++) {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* Ask for one target; returns 1 and fills data/len on success. */
static int fetch(const char *target_name, unsigned char **out, unsigned long *out_len) {
    Atom target = XInternAtom(dpy, target_name, False);
    Atom clip = XInternAtom(dpy, "CLIPBOARD", False);
    XConvertSelection(dpy, clip, target, prop, win, CurrentTime);
    XFlush(dpy);

    for (int waited = 0; waited < 120; waited++) {
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type != SelectionNotify) continue;
            if (ev.xselection.property == None) return 0;
            Atom type; int fmt; unsigned long n = 0, after = 0;
            unsigned char *data = NULL;
            if (XGetWindowProperty(dpy, win, prop, 0, 1 << 22, False, AnyPropertyType,
                                   &type, &fmt, &n, &after, &data) != Success)
                return 0;
            XDeleteProperty(dpy, win, prop);
            if (!data || n == 0) { if (data) XFree(data); return 0; }
            *out = data;
            *out_len = n;
            return 1;
        }
        usleep(2000);
    }
    return 0;
}

static void ingest(const char *mime, const char *suffix, const unsigned char *data, unsigned long len) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/super-v-x11-%lld.%s", (long long)getpid(), suffix);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return;
    unsigned long written = 0;
    while (written < len) {
        ssize_t w = write(fd, data + written, len - written);
        if (w <= 0) break;
        written += (unsigned long)w;
    }
    close(fd);

    const char *candidates[3];
    char home[256] = "/usr/local/bin/super-v-ingest";
    const char *h = getenv("HOME");
    if (h) {
        snprintf(home, sizeof(home), "%s/.local/bin/super-v-ingest", h);
    }
    candidates[0] = home;
    candidates[1] = "/usr/local/bin/super-v-ingest";
    candidates[2] = "/usr/bin/super-v-ingest";

    for (int i = 0; i < 3; i++) {
        if (access(candidates[i], X_OK) != 0) continue;
        pid_t pid = fork();
        if (pid == 0) {
            execl(candidates[i], "super-v-ingest", "--mime", mime, path, (char *)NULL);
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            waitpid(pid, &status, 0);
        }
        return;
    }
}

static int run(int interval_ms) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "super-v-x11watch: no X display\n"); return 1; }
    win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);
    XSelectInput(dpy, win, PropertyChangeMask);
    prop = XInternAtom(dpy, "SUPER_V_WATCH", False);

    unsigned long long last_hash = 0;
    int have_last = 0;

    for (;;) {
        unsigned char *data = NULL;
        unsigned long len = 0;
        const char *mime = NULL;
        const char *suffix = NULL;

        if (fetch("UTF8_STRING", &data, &len)) {
            mime = "text/plain";
            suffix = "txt";
        } else if (fetch("image/png", &data, &len)) {
            mime = "image/png";
            suffix = "png";
        }

        if (data && len > 0) {
            unsigned long long h = hash_bytes(data, len);
            if (!have_last || h != last_hash) {
                ingest(mime, suffix, data, len);
                last_hash = h;
                have_last = 1;
            }
            XFree(data);
        }

        usleep((useconds_t)interval_ms * 1000);
    }
    return 0;
}

int sv_x11watch_main(int interval_ms) {
    return run(interval_ms > 0 ? interval_ms : 400);
}
