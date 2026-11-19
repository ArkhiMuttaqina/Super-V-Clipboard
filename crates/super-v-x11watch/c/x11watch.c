/* super-v-x11watch: polls the X11 CLIPBOARD selection (bridged to the Wayland
 * clipboard by XWayland) and hands new content to super-v-ingest.
 *
 * Protocol notes:
 * - the selection is negotiated: ask TARGETS, then pick the best type there is,
 *   never assume a fixed pair of MIME types;
 * - payloads above the request size are delivered through INCR, a stream of
 *   property updates terminated by a zero-length chunk;
 * - text/uri-list is a file or image reference, not text to paste.
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <strings.h>
#include <sys/wait.h>

static Display *dpy;
static Window win;
static Atom prop;
static Atom incr_atom;

static unsigned long long hash_bytes(const unsigned char *data, unsigned long len) {
    unsigned long long h = 1469598103934665603ULL;
    for (unsigned long i = 0; i < len; i++) {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static int wait_selection_notify(void) {
    for (int waited = 0; waited < 250; waited++) {
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == SelectionNotify) {
                return ev.xselection.property != None;
            }
        }
        usleep(2000);
    }
    return 0;
}

/* Reads the whole property, following INCR when the owner streams it. */
static int read_property(Window requestor, Atom property, unsigned char **out, unsigned long *out_len) {
    Atom type;
    int fmt;
    unsigned long n = 0, after = 0;
    unsigned char *data = NULL;

    if (XGetWindowProperty(dpy, requestor, property, 0, 1 << 22, False, AnyPropertyType,
                           &type, &fmt, &n, &after, &data) != Success)
        return 0;

    if (type == None) {
        if (data) XFree(data);
        return 0;
    }

    if (type != incr_atom) {
        XDeleteProperty(dpy, requestor, property);
        if (!data || n == 0) { if (data) XFree(data); return 0; }
        *out = data;
        *out_len = n;
        return 1;
    }

    /* INCR: deleting the property starts the transfer. */
    if (data) XFree(data);
    XDeleteProperty(dpy, requestor, property);
    XFlush(dpy);

    unsigned char *acc = NULL;
    unsigned long acc_len = 0;
    for (;;) {
        int got_event = 0;
        for (int waited = 0; waited < 500 && !got_event; waited++) {
            while (XPending(dpy)) {
                XEvent ev;
                XNextEvent(dpy, &ev);
                if (ev.type == PropertyNotify && ev.xproperty.atom == property &&
                    ev.xproperty.state == PropertyNewValue) {
                    got_event = 1;
                    break;
                }
            }
            if (!got_event) usleep(2000);
        }
        if (!got_event) {
            free(acc);
            return 0;
        }

        data = NULL;
        n = 0;
        after = 0;
        type = None;
        if (XGetWindowProperty(dpy, requestor, property, 0, 1 << 22, False, AnyPropertyType,
                               &type, &fmt, &n, &after, &data) != Success) {
            free(acc);
            return 0;
        }
        XDeleteProperty(dpy, requestor, property);
        XFlush(dpy);

        if (n == 0) {
            if (data) XFree(data);
            break;
        }
        unsigned char *grown = realloc(acc, acc_len + n);
        if (!grown) { if (data) XFree(data); free(acc); return 0; }
        acc = grown;
        memcpy(acc + acc_len, data, n);
        acc_len += n;
        if (data) XFree(data);
    }

    if (!acc || acc_len == 0) { free(acc); return 0; }
    *out = acc;
    *out_len = acc_len;
    return 1;
}

/* Each request uses a throwaway window, so a late reply to a refused conversion
 * can never be mistaken for the current one: XWayland simply stays silent for
 * targets it does not offer. */
static int fetch(const char *target_name, unsigned char **out, unsigned long *out_len) {
    Atom target = XInternAtom(dpy, target_name, False);
    Atom clip = XInternAtom(dpy, "CLIPBOARD", False);
    Atom property = XInternAtom(dpy, "SUPER_V_WATCH", False);
    Window requestor = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);
    XSelectInput(dpy, requestor, PropertyChangeMask);   /* needed for INCR */

    XConvertSelection(dpy, clip, target, property, requestor, CurrentTime);
    XFlush(dpy);

    int ok = 0;
    for (int waited = 0; waited < 60; waited++) {   /* 60 * 2 ms = 120 ms */
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type != SelectionNotify || ev.xselection.requestor != requestor) continue;
            if (ev.xselection.property != None)
                ok = read_property(requestor, property, out, out_len);
            XDestroyWindow(dpy, requestor);
            return ok;
        }
        usleep(2000);
    }
    XDestroyWindow(dpy, requestor);
    return 0;
}

/* Returns 1 when the offered target list contains the given name. */
static int has_target(const Atom *targets, unsigned long count, const char *name) {
    if (!name) return 0;
    Atom a = XInternAtom(dpy, name, False);
    for (unsigned long i = 0; i < count; i++)
        if (targets[i] == a) return 1;
    return 0;
}

static int get_targets(Atom **out, unsigned long *count) {
    unsigned char *data = NULL;
    unsigned long len = 0;
    if (!fetch("TARGETS", &data, &len)) return 0;
    *out = (Atom *)data;
    *count = len / sizeof(Atom);
    return *count > 0;
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

    char home[256];
    const char *h = getenv("HOME");
    if (h) snprintf(home, sizeof(home), "%s/.local/bin/super-v-ingest", h);
    else snprintf(home, sizeof(home), "/usr/local/bin/super-v-ingest");

    const char *candidates[3];
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
    unlink(path);
}

/* A uri-list points at files; an image file should become an image entry. */
static int ingest_uri_list(const unsigned char *data, unsigned long len) {
    char *copy = strndup((const char *)data, len);
    if (!copy) return 0;
    int handled = 0;
    char *save = NULL;
    for (char *line = strtok_r(copy, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
        if (strncmp(line, "file://", 7) != 0) continue;
        const char *path = line + 7;
        const char *dot = strrchr(path, '.');
        const char *mime = NULL;
        const char *suffix = NULL;
        if (dot) {
            if (!strcasecmp(dot, ".png")) { mime = "image/png"; suffix = "png"; }
            else if (!strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg")) { mime = "image/jpeg"; suffix = "jpg"; }
            else if (!strcasecmp(dot, ".webp")) { mime = "image/webp"; suffix = "webp"; }
            else if (!strcasecmp(dot, ".gif")) { mime = "image/gif"; suffix = "gif"; }
            else if (!strcasecmp(dot, ".bmp")) { mime = "image/bmp"; suffix = "bmp"; }
        }
        if (!mime) continue;

        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size > 0 && size < 32 * 1024 * 1024) {
            unsigned char *buf = malloc((size_t)size);
            if (buf && fread(buf, 1, (size_t)size, f) == (size_t)size) {
                ingest(mime, suffix, buf, (unsigned long)size);
                handled = 1;
            }
            free(buf);
        }
        fclose(f);
    }
    free(copy);
    return handled;
}

/* Our own clipboard writes must not be recorded as new copies. */
static int self_owned(void) {
    const char *marker = "/tmp/super-v-clipboard-owned";
    struct stat st;
    if (stat(marker, &st) != 0) return 0;
    time_t now = time(NULL);
    return (now - st.st_mtime) <= 2;
}

/* XWayland does not always answer TARGETS, so candidates are probed in order of
 * usefulness. A refused conversion costs one round trip: the owner answers
 * SelectionNotify with property None immediately. */
static const char *IMAGE_TARGETS[] = {
    "image/png", "image/jpeg", "image/webp", "image/gif", "image/bmp", NULL
};
static const char *TEXT_TARGETS[] = {
    "UTF8_STRING", "text/plain;charset=utf-8", "text/plain", "STRING", "TEXT", NULL
};

static const char *suffix_for(const char *mime) {
    if (!strcmp(mime, "image/jpeg")) return "jpg";
    if (!strcmp(mime, "image/webp")) return "webp";
    if (!strcmp(mime, "image/gif")) return "gif";
    if (!strcmp(mime, "image/bmp")) return "bmp";
    if (!strcmp(mime, "image/png")) return "png";
    return "txt";
}

static int run(int interval_ms) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "super-v-x11watch: no X display\n"); return 1; }
    win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);
    XSelectInput(dpy, win, PropertyChangeMask);
    prop = XInternAtom(dpy, "SUPER_V_WATCH", False);
    incr_atom = XInternAtom(dpy, "INCR", False);
    const int debug = getenv("SUPER_V_X11WATCH_DEBUG") != NULL;

    unsigned long long last_hash = 0;
    int have_last = 0;

    for (;;) {
        unsigned char *data = NULL;
        unsigned long len = 0;
        const char *mime = NULL;
        int is_uri = 0;

        /* 1. file references (a copied image file must become an image) */
        if (fetch("text/uri-list", &data, &len)) {
            is_uri = 1;
            mime = "text/uri-list";
        }

        /* 2. images */
        for (int i = 0; !data && IMAGE_TARGETS[i]; i++) {
            if (fetch(IMAGE_TARGETS[i], &data, &len)) {
                mime = IMAGE_TARGETS[i];
            }
        }

        /* 3. text */
        for (int i = 0; !data && TEXT_TARGETS[i]; i++) {
            if (fetch(TEXT_TARGETS[i], &data, &len)) {
                mime = "text/plain";
            }
        }

        if (data && len > 0) {
            unsigned long long h = hash_bytes(data, len);
            if (!have_last || h != last_hash) {
                if (self_owned()) {
                    if (debug) fprintf(stderr, "skip self %s\n", mime ? mime : "?");
                } else if (is_uri) {
                    if (ingest_uri_list(data, len)) {
                        last_hash = h;
                        have_last = 1;
                        if (debug) fprintf(stderr, "ingested uri-list\n");
                    } else if (debug) {
                        fprintf(stderr, "uri-list without image\n");
                    }
                } else {
                    if (debug) fprintf(stderr, "ingest %s %lu bytes\n", mime, len);
                    ingest(mime, suffix_for(mime), data, len);
                    last_hash = h;
                    have_last = 1;
                }
            }
            XFree(data);
        }

        usleep((useconds_t)interval_ms * 1000);
    }
    return 0;
}

int sv_x11watch_main(int interval_ms) {
    return run(interval_ms > 0 ? interval_ms : 150);
}
