#!/usr/bin/env bash
# t48-probe-x11menu.sh — one-shot probe: open the X11 Motif Options dialog under
# a private Xvfb and report its geometry + capture it, to derive scripted-click
# coordinates for the F5 X11 leg (menu-x11.script).
set -u
cd "$(dirname "$0")/.." || exit 2
source qa/env/env.sh || exit 2
DISP=:97
WORK=/tmp/opencode/t48/probe
rm -rf "$WORK"; mkdir -p "$WORK"

pkill -x Xvfb 2>/dev/null; sleep 1
{ Xvfb $DISP -screen 0 1280x1024x24 -fp "$HOME/.local/xast-env/fonts" </dev/null >"$WORK/xvfb.log" 2>&1 & }
sleep 2
xdpyinfo -display $DISP >/dev/null || { echo "no Xvfb"; exit 1; }

cat > "$WORK/xhelper.c" <<'EOF'
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void walk(Display *d, Window w, const char *name, Window *found) {
    if (*found != None) return;
    char *wn = NULL;
    if (w != DefaultRootWindow(d) && XFetchName(d, w, &wn) && wn) {
        if (strcmp(wn, name) == 0) *found = w;
        XFree(wn);
    }
    Window root, parent, *ch = NULL; unsigned int n;
    if (XQueryTree(d, w, &root, &parent, &ch, &n)) {
        for (unsigned int i = 0; i < n; i++) walk(d, ch[i], name, found);
        if (ch) XFree((char *)ch);
    }
}
int main(int argc, char **argv) {
    Display *d = XOpenDisplay(argv[1]);
    if (!d) return 2;
    int rc = 1;
    if (!strcmp(argv[2], "findwin") && argc == 4) {
        Window f = None; walk(d, DefaultRootWindow(d), argv[3], &f);
        if (f != None) { printf("0x%lx\n", (unsigned long)f); rc = 0; }
    } else if (!strcmp(argv[2], "geom") && argc == 4) {
        Window w = (Window)strtoul(argv[3], NULL, 0);
        Window root; int x, y; unsigned int bw, depth, W, H;
        if (XGetGeometry(d, w, &root, &x, &y, &W, &H, &bw, &depth)) {
            int rx, ry; Window child;
            XTranslateCoordinates(d, w, root, 0, 0, &rx, &ry, &child);
            printf("%d %d %u %u %u\n", rx, ry, W, H, bw); rc = 0;
        }
    } else if (!strcmp(argv[2], "click") && argc == 6) {
        int rx = atoi(argv[3]), ry = atoi(argv[4]);
        XTestFakeMotionEvent(d, 0, rx, ry, CurrentTime); XSync(d, False);
        usleep(60000);
        XTestFakeButtonEvent(d, Button1, True, CurrentTime); XSync(d, False);
        usleep(120000);
        XTestFakeButtonEvent(d, Button1, False, CurrentTime); XSync(d, False);
        rc = 0;
    } else if (!strcmp(argv[2], "tree") && argc == 3) {
        Window root = DefaultRootWindow(d), r, p, *ch = NULL; unsigned n;
        if (XQueryTree(d, root, &r, &p, &ch, &n)) {
            for (unsigned i = 0; i < n; i++) {
                char *wn = NULL; XWindowAttributes a;
                XGetWindowAttributes(d, ch[i], &a);
                XFetchName(d, ch[i], &wn);
                printf("0x%lx '%s' %dx%d+%d+%d map=%s\n", (unsigned long)ch[i],
                       wn ? wn : "", a.width, a.height, a.x, a.y,
                       a.map_state == IsViewable ? "V" : "U");
                if (wn) XFree(wn);
            }
            rc = 0;
        }
    }
    XCloseDisplay(d);
    return rc;
}
EOF
gcc -O2 -o "$WORK/xhelper" "$WORK/xhelper.c" -lX11 -lXtst || { echo "xhelper build fail"; exit 1; }

# launch game
DISPLAY=$DISP ./XAsteroids >"$WORK/game.log" 2>&1 &
GAME_PID=$!
sleep 10

GAME_WIN=$("$WORK/xhelper" $DISP findwin Asteroids)
echo "game win: $GAME_WIN"
read -r GX GY GW GH GB <<<"$("$WORK/xhelper" $DISP geom "$GAME_WIN")"
echo "game geom(client origin): +$GX+$GY ${GW}x${GH} border=$GB"

DISPLAY=$DISP xwd -id "$GAME_WIN" -silent | convert xwd:- "$WORK/title.png"
echo "title captured"

# click Options button at client (20,15)
"$WORK/xhelper" $DISP click $((GX+20)) $((GY+15))
sleep 3

echo "--- tree after Options click:"
"$WORK/xhelper" $DISP tree

DIALOG_WIN=$("$WORK/xhelper" $DISP findwin "Asteroids Options")
echo "dialog win: $DIALOG_WIN"
if [ -n "$DIALOG_WIN" ]; then
  read -r DX DY DW DH DB <<<"$("$WORK/xhelper" $DISP geom "$DIALOG_WIN")"
  echo "dialog geom: +$DX+$DY ${DW}x${DH} border=$DB"
  echo "dialog offset vs game client origin: dx=$((DX-GX)) dy=$((DY-GY))"
  DISPLAY=$DISP xwd -id "$DIALOG_WIN" -silent | convert xwd:- "$WORK/dialog.png"
  ls -la "$WORK/dialog.png"
fi
kill $GAME_PID 2>/dev/null; sleep 1; pkill -x Xvfb; echo done
