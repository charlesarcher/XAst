/* xevt.c — task-46 QA helper: window discovery + WM_DELETE_WINDOW
 * ClientMessage delivery + XClearArea exposure damage + XTest keys.
 * Compiled at runtime by qa/closepath.sh / qa/expose-probe.sh
 * (capture-x11.sh xhelper pattern; no xdotool/wmctrl in the env).
 *
 * Build: cc -O2 -o xevt xevt.c -lX11 -lXtst
 * Usage:
 *   xevt <disp> findwin <WM_NAME>
 *   xevt <disp> geom   <win>          (abs-x abs-y w h border depth)
 *   xevt <disp> focus  <win>
 *   xevt <disp> key    <keysym>       (press+release, focused window)
 *   xevt <disp> warp   <rootX> <rootY>  (XTestFakeMotionEvent, ROOT coords)
 *   xevt <disp> delete <win>          (send WM_DELETE_WINDOW once)
 *   xevt <disp> expose <win>          (XClearArea whole window, exposures=True)
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void walk(Display *d, Window w, const char *name, Window *found) {
    if (*found != None) return;
    char *wn = NULL;
    if (w != DefaultRootWindow(d) && XFetchName(d, w, &wn) && wn) {
        if (strcmp(wn, name) == 0) *found = w;
        XFree(wn);
    }
    Window root, parent, *children = NULL;
    unsigned int n;
    if (XQueryTree(d, w, &root, &parent, &children, &n)) {
        for (unsigned int i = 0; i < n; i++) walk(d, children[i], name, found);
        if (children) XFree((char *)children);
    }
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: xevt <disp> cmd ...\n"); return 64; }
    Display *d = XOpenDisplay(argv[1]);
    if (!d) { fprintf(stderr, "xevt: cannot open display %s\n", argv[1]); return 2; }
    int rc = 1;

    if (!strcmp(argv[2], "findwin") && argc == 4) {
        Window f = None;
        walk(d, DefaultRootWindow(d), argv[3], &f);
        if (f != None) { printf("0x%lx\n", (unsigned long)f); rc = 0; }
    } else if (!strcmp(argv[2], "geom") && argc == 4) {
        Window w = (Window)strtoul(argv[3], NULL, 0);
        Window root; int x, y; unsigned int bw, depth, W, H;
        if (XGetGeometry(d, w, &root, &x, &y, &W, &H, &bw, &depth)) {
            int rx, ry; Window child;
            XTranslateCoordinates(d, w, root, 0, 0, &rx, &ry, &child);
            printf("%d %d %u %u %u %u\n", rx, ry, W, H, bw, (unsigned)depth);
            rc = 0;
        }
    } else if (!strcmp(argv[2], "focus") && argc == 4) {
        Window w = (Window)strtoul(argv[3], NULL, 0);
        XSetInputFocus(d, w, RevertToParent, CurrentTime);
        XFlush(d); rc = 0;
    } else if (!strcmp(argv[2], "key") && argc == 4) {
        Window fw = None; int rev;
        XGetInputFocus(d, &fw, &rev);
        if (fw == PointerRoot || fw == None) {
            fprintf(stderr, "xevt: no focused window; run 'focus' first\n"); return 3;
        }
        KeyCode kc = XKeysymToKeycode(d, XStringToKeysym(argv[3]));
        if (!kc) { fprintf(stderr, "xevt: no keycode for %s\n", argv[3]); return 4; }
        XTestFakeKeyEvent(d, kc, True, CurrentTime);
        XSync(d, False); usleep(120000);
        XTestFakeKeyEvent(d, kc, False, CurrentTime);
        XSync(d, False); rc = 0;
    } else if (!strcmp(argv[2], "warp") && argc == 5) {
        int screen = DefaultScreen(d);
        XTestFakeMotionEvent(d, screen, atoi(argv[3]), atoi(argv[4]), CurrentTime);
        XSync(d, False); rc = 0;
    } else if (!strcmp(argv[2], "delete") && argc == 4) {
        /* The ICCCM close request: a ClientMessage on WM_PROTOCOLS whose
         * first data long is the WM_DELETE_WINDOW atom. Under bare Xvfb
         * there is no WM, so this stands in for the window-manager. */
        Window w = (Window)strtoul(argv[3], NULL, 0);
        Atom protocols = XInternAtom(d, "WM_PROTOCOLS", False);
        Atom del = XInternAtom(d, "WM_DELETE_WINDOW", False);
        XEvent e;
        memset(&e, 0, sizeof e);
        e.xclient.type = ClientMessage;
        e.xclient.window = w;
        e.xclient.message_type = protocols;
        e.xclient.format = 32;
        e.xclient.data.l[0] = (long)del;
        e.xclient.data.l[1] = CurrentTime;
        if (XSendEvent(d, w, False, NoEventMask, &e)) { XFlush(d); rc = 0; }
    } else if (!strcmp(argv[2], "expose") && argc == 4) {
        Window w = (Window)strtoul(argv[3], NULL, 0);
        /* whole-window area, exposures=True -> real Expose events */
        XClearArea(d, w, 0, 0, 0, 0, True);
        XFlush(d); rc = 0;
    }
    XCloseDisplay(d);
    return rc;
}
