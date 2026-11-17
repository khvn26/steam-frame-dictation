// Minimal X11 clipboard helper for --paste-command on systems without
// wl-copy/xclip (such as the Steam Frame). Depends on libX11 only.
//   sfd-x11-copy          read stdin, own CLIPBOARD, serve it in the background
//   sfd-x11-copy --read   print the current CLIPBOARD as UTF-8
#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int read_clipboard(Display *d, Window w, Atom clip, Atom utf8) {
    Atom prop = XInternAtom(d, "SFD_X11_COPY_SEL", False);
    XConvertSelection(d, clip, utf8, prop, w, CurrentTime);
    XFlush(d);
    struct pollfd pfd = {.fd = ConnectionNumber(d), .events = POLLIN};
    for (int tries = 0; tries < 20; tries++) {
        while (XPending(d)) {
            XEvent ev; XNextEvent(d, &ev);
            if (ev.type != SelectionNotify) continue;
            if (ev.xselection.property == None) { fprintf(stderr, "clipboard empty or not text\n"); return 1; }
            Atom type; int fmt; unsigned long n, left; unsigned char *data = NULL;
            XGetWindowProperty(d, w, prop, 0, 1 << 20, True, AnyPropertyType, &type, &fmt, &n, &left, &data);
            if (data) { fwrite(data, 1, n, stdout); XFree(data); }
            return 0;
        }
        poll(&pfd, 1, 100);
    }
    fprintf(stderr, "timeout waiting for clipboard owner\n");
    return 1;
}

int main(int argc, char **argv) {
    Display *d = XOpenDisplay(NULL);
    if (!d) { fprintf(stderr, "cannot open display\n"); return 1; }
    Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, 1, 1, 0, 0, 0);
    Atom clip = XInternAtom(d, "CLIPBOARD", False), utf8 = XInternAtom(d, "UTF8_STRING", False);
    Atom targets = XInternAtom(d, "TARGETS", False), textplain = XInternAtom(d, "text/plain;charset=utf-8", False);
    if (argc > 1 && !strcmp(argv[1], "--read")) return read_clipboard(d, w, clip, utf8);

    char *buf = NULL; size_t n = 0, cap = 0;
    for (;;) {
        if (n + 4096 > cap) { cap = cap ? cap * 2 : 8192; buf = realloc(buf, cap); if (!buf) return 1; }
        ssize_t r = read(STDIN_FILENO, buf + n, 4096);
        if (r <= 0) break;
        n += (size_t)r;
    }
    XSetSelectionOwner(d, clip, w, CurrentTime);
    XSync(d, False);
    if (XGetSelectionOwner(d, clip) != w) { fprintf(stderr, "could not own clipboard\n"); return 1; }
    pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid > 0) _exit(0); // parent returns once the clipboard is owned
    setsid();
    close(STDIN_FILENO); close(STDOUT_FILENO); close(STDERR_FILENO);
    for (;;) {
        XEvent ev; XNextEvent(d, &ev);
        if (ev.type == SelectionClear) break; // someone else copied
        if (ev.type != SelectionRequest) continue;
        XSelectionRequestEvent *rq = &ev.xselectionrequest;
        XEvent rep; memset(&rep, 0, sizeof(rep));
        rep.xselection.type = SelectionNotify; rep.xselection.requestor = rq->requestor;
        rep.xselection.selection = rq->selection; rep.xselection.target = rq->target;
        rep.xselection.time = rq->time;
        Atom prop = rq->property ? rq->property : rq->target;
        rep.xselection.property = prop;
        if (rq->target == targets) {
            Atom list[] = {targets, utf8, textplain, XA_STRING};
            XChangeProperty(d, rq->requestor, prop, XA_ATOM, 32, PropModeReplace, (unsigned char *)list, 4);
        } else if (rq->target == utf8 || rq->target == textplain || rq->target == XA_STRING) {
            XChangeProperty(d, rq->requestor, prop, rq->target, 8, PropModeReplace, (unsigned char *)buf, (int)n);
        } else rep.xselection.property = None;
        XSendEvent(d, rq->requestor, False, 0, &rep);
        XFlush(d);
    }
    return 0;
}
