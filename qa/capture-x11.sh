#!/usr/bin/env bash
# qa/capture-x11.sh — pre-refactor X11 baseline capture (plan task 7, B7/N7).
#
# Captures the UNMODIFIED X11 binary's title and help screens under a private
# Xvfb, cropped to the CLIENT AREA (the 5px window border of stage.H:186 is
# stripped — raw xwd includes it; GLFW windows later will not have one, m9/N7).
#
# Usage: qa/capture-x11.sh [OUT_DIR]        (default: qa/baseline-x11)
#
# Gates (in order):
#   1. tool presence (env.sh prefix + system)
#   2. font pre-flight (O5-N2): xlsfonts must resolve ALL FIVE stage.H:134-138
#      families incl. errorInfo — aborts NAMING any missing family
#   3. Xvfb :99 fixed geometry, game launched, ~10s static-init settle
#      (first-attempt black-frame lesson, task-1 QA)
#
# Determinism contract: two runs of this script into different OUT_DIRs must
# diff 0 px (`compare -metric AE`). Seed is NOT pinned here (XAST_SEED lands in
# task 8); the title/help screens are seed-independent (static init only).

set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
source "$REPO_ROOT/qa/env/env.sh" || exit 1

OUT_DIR="${1:-$REPO_ROOT/qa/baseline-x11}"
DISPLAY_NUM="${CAPTURE_DISPLAY:-99}"
SCREEN_GEOM="1280x1024x24"     # fixed Xvfb geometry (D15 pass 2 / D17.2)
GAME_WM_NAME="Asteroids"       # playingField.H:545 XSetWMProperties
BORDER_PX_EXPECT=5             # stage.H:186 border_width
SETTLE_SECS="${SETTLE_SECS:-10}"
HELP_SETTLE_SECS="${HELP_SETTLE_SECS:-3}"

die() { echo "capture-x11.sh: BLOCKED: $*" >&2; exit 1; }
log() { echo "capture-x11.sh: $*"; }

mkdir -p "$OUT_DIR" || die "cannot create $OUT_DIR"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/t7-capture.XXXXXX")" || die "mktemp failed"
XVFB_PID=""
GAME_PID=""

cleanup() {
    local rc=$?
    [ -n "$GAME_PID" ] && kill "$GAME_PID" 2>/dev/null
    [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
    sleep 0.5
    [ -n "$GAME_PID" ] && wait "$GAME_PID" 2>/dev/null
    [ -n "$XVFB_PID" ] && wait "$XVFB_PID" 2>/dev/null
    rm -rf "$WORK"
    exit $rc
}
trap cleanup EXIT INT TERM

# ---------------------------------------------------------------- gate 1: tools
for t in Xvfb xwd xlsfonts mkfontscale xdpyinfo convert compare gcc; do
    command -v "$t" >/dev/null 2>&1 || die "required tool '$t' not on PATH (is env.sh sourced?)"
done

# ------------------------------------------------------- build X helper
cat > "$WORK/xhelper.c" <<'EOF'
/* xhelper — window discovery + geometry + XTest key injection for QA captures */
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
    if (argc < 3) { fprintf(stderr, "usage: xhelper <disp> findwin|geom|key|focus ...\n"); return 64; }
    Display *d = XOpenDisplay(argv[1]);
    if (!d) { fprintf(stderr, "xhelper: cannot open display %s\n", argv[1]); return 2; }
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
            /* abs-x abs-y width height border-width depth */
            printf("%d %d %u %u %u %u\n", rx, ry, W, H, bw, (unsigned)depth);
            rc = 0;
        }
    } else if (!strcmp(argv[2], "focus") && argc == 4) {
        Window w = (Window)strtoul(argv[3], NULL, 0);
        XSetInputFocus(d, w, RevertToParent, CurrentTime);
        XFlush(d); rc = 0;
    } else if (!strcmp(argv[2], "key") && argc == 5) {
        Window focusw = None; int rev;
        XGetInputFocus(d, &focusw, &rev);
        if (focusw == PointerRoot || focusw == None) {
            fprintf(stderr, "xhelper: no window focused; run 'focus' first\n"); return 3;
        }
        KeyCode kc = XKeysymToKeycode(d, XStringToKeysym(argv[4]));
        if (!kc) { fprintf(stderr, "xhelper: no keycode for keysym %s\n", argv[4]); return 4; }
        XTestFakeKeyEvent(d, kc, True, CurrentTime);
        XSync(d, False); usleep(120000);
        XTestFakeKeyEvent(d, kc, False, CurrentTime);
        XSync(d, False); rc = 0;
    }
    XCloseDisplay(d);
    return rc;
}
EOF
gcc -O2 -o "$WORK/xhelper" "$WORK/xhelper.c" -lX11 -lXtst \
    || die "cannot compile xhelper (need libX11 + libXtst dev headers)"
log "xhelper compiled"

# ------------------------------------------------------------ gate 3: Xvfb :99
if xdpyinfo -display ":$DISPLAY_NUM" >/dev/null 2>&1; then
    die "display :$DISPLAY_NUM already has an X server — pick another CAPTURE_DISPLAY"
fi
{ Xvfb ":$DISPLAY_NUM" -screen 0 "$SCREEN_GEOM" -fp "$HOME/.local/xast-env/fonts" \
      </dev/null >"$WORK/xvfb.log" 2>&1 & } ; XVFB_PID=$!
sleep 2
xdpyinfo -display ":$DISPLAY_NUM" >/dev/null 2>&1 \
    || die "Xvfb :$DISPLAY_NUM did not come up (log: $WORK/xvfb.log)"
trap cleanup EXIT INT TERM   # re-arm now that XVFB_PID is set
log "Xvfb :$DISPLAY_NUM up ($SCREEN_GEOM, fp=$HOME/.local/xast-env/fonts)"

# ------------------------------------------------------- gate 2: font pre-flight
# NOTE: xlsfonts enumerates a SERVER's font path, so this gate can only run
# once :99 exists — it still fires strictly BEFORE any game launch or capture
# (O5-N2 ordering property: no capture happens with fonts unresolved).
FONT_FAMILIES=(
    "white_shadow-48"
    "-schumacher-clean-bold-r-normal--10-100-75-75-c-60-iso8859-1"
    "-ibm-ergonomic-bold-r-normal--20-140-100-100-c-120-iso8859-9"
    "-urw-courier-bold-r-normal--40-300-100-100-m-240-iso8859-9"
    "-adobe-helvetica-bold-r-normal--14-100-100-100-p-82-iso8859-1"
)
MISSING=()
for fam in "${FONT_FAMILIES[@]}"; do
    if ! xlsfonts -display ":$DISPLAY_NUM" -fn "$fam" >/dev/null 2>&1 \
       || [ -z "$(xlsfonts -display ":$DISPLAY_NUM" -fn "$fam" 2>/dev/null)" ]; then
        MISSING+=("$fam")
    fi
done
if [ "${#MISSING[@]}" -gt 0 ]; then
    echo "capture-x11.sh: BLOCKED: missing font families (stage.H:134-138):" >&2
    printf '  - %s\n' "${MISSING[@]}" >&2
    echo "Font path searched: ${XAST_FONTS:-<unset>} — run XAstSetup / rebuild the env fonts dir." >&2
    exit 1
fi
log "font pre-flight OK: all 5 stage.H:134-138 families resolve on :$DISPLAY_NUM"

# ------------------------------------------------------------------ run game

# ------------------------------------------------------------------ run game
DISP=":$DISPLAY_NUM"
DISPLAY="$DISP" "$REPO_ROOT/XAsteroids" >"$WORK/game.log" 2>&1 &
GAME_PID=$!
log "game pid $GAME_PID — settling ${SETTLE_SECS}s for static init"
sleep "$SETTLE_SECS"
kill -0 "$GAME_PID" 2>/dev/null || die "game exited early (log: $(tail -3 "$WORK/game.log" 2>/dev/null))"

WIN="$("$WORK/xhelper" "$DISP" findwin "$GAME_WM_NAME")" \
    || die "no window named '$GAME_WM_NAME' on $DISP"
log "game window $WIN"

read -r ABS_X ABS_Y CW CH BW DEPTH <<<"$("$WORK/xhelper" "$DISP" geom "$WIN")"
log "client geometry: ${CW}x${CH} at +${ABS_X}+${ABS_Y}, border=${BW}px depth=${DEPTH}"
[ "$BW" = "$BORDER_PX_EXPECT" ] || log "WARNING: border is ${BW}px, expected ${BORDER_PX_EXPECT} (stage.H:186)"

capture() { # $1 = output png
    DISPLAY="$DISP" xwd -id "$WIN" -silent | convert xwd:- -crop "${CW}x${CH}+${BW}+${BW}" +repage "png:$1" \
        || die "xwd|convert failed for $1"
}

# ---- title screen (initial state)
"$WORK/xhelper" "$DISP" focus "$WIN"
sleep 1
capture "$OUT_DIR/title.png"
log "captured title.png ($(identify -format '%wx%h' "$OUT_DIR/title.png" 2>/dev/null || echo '?'))"

# ---- help screen ('h' from title — single deterministic keypress)
"$WORK/xhelper" "$DISP" key "$WIN" h || die "'h' injection failed"
sleep "$HELP_SETTLE_SECS"
capture "$OUT_DIR/help.png"
log "captured help.png ($(identify -format '%wx%h' "$OUT_DIR/help.png" 2>/dev/null || echo '?'))"

# ------------------------------------------------------------------ teardown
kill "$GAME_PID" 2>/dev/null; GAME_PID=""
sleep 1
kill "$XVFB_PID" 2>/dev/null; XVFB_PID=""
wait 2>/dev/null
log "DONE: $OUT_DIR/title.png $OUT_DIR/help.png (${CW}x${CH}, border ${BW}px stripped)"
exit 0
