#!/usr/bin/env bash
# qa/expose-probe.sh — task-46 Expose-handling probe (any backend leg).
#
# Launches the game standalone under its own Xvfb, captures the title/help
# screen, inflicts REAL exposure damage on the whole window (XClearArea
# exposures=True via qa/xevt.c), waits, captures again, and asserts the
# content is fully restored (AE==0):
#   X11  — stage.Refresh() redraws from canvas+yard at the drain head
#   GL/VK — D8 redraw-every-frame republishes; the next swap restores
#
# Usage: qa/expose-probe.sh <game-binary> <evidence-dir>
set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$REPO_ROOT/qa/env/env.sh" || exit 1

GAME="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
EV="$2"
DISPLAY_NUM="${EXPOSE_DISPLAY:-97}"
DISP=":$DISPLAY_NUM"
WM_NAME="Asteroids"
SETTLE="${SETTLE_SECS:-8}"

die() { echo "expose-probe.sh: BLOCKED: $*" >&2; exit 1; }
log() { echo "expose-probe.sh: $*"; }

[ -x "$GAME" ] || die "game binary '$GAME' not executable"
mkdir -p "$EV"
for t in Xvfb xdpyinfo xwd convert compare cc; do
    command -v "$t" >/dev/null 2>&1 || die "missing $t"
done

WORK="$(mktemp -d "${TMPDIR:-/tmp}/t46-expose.XXXXXX")" || die "mktemp failed"
XVFB_PID="" GAME_PID=""
cleanup() {
    [ -n "$GAME_PID" ] && kill -9 "$GAME_PID" 2>/dev/null
    [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
    sleep 0.3; wait 2>/dev/null; rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

cp "$REPO_ROOT/qa/xevt.c" "$WORK/" && cc -O2 -o "$WORK/xevt" "$WORK/xevt.c" -lX11 -lXtst \
    || die "cannot compile xevt"

if xdpyinfo -display "$DISP" >/dev/null 2>&1; then
    die "display $DISP already has an X server — pick another EXPOSE_DISPLAY"
fi
{ Xvfb "$DISP" -screen 0 1280x1024x24 -fp "$HOME/.local/xast-env/fonts" \
      </dev/null >"$WORK/xvfb.log" 2>&1 & } ; XVFB_PID=$!
sleep 2
xdpyinfo -display "$DISP" >/dev/null 2>&1 || die "Xvfb $DISP did not come up"

DISPLAY="$DISP" XAST_SEED=12345 "$GAME" >"$WORK/game.log" 2>&1 &
GAME_PID=$!
sleep "$SETTLE"
kill -0 "$GAME_PID" 2>/dev/null || die "game exited early ($(tail -2 "$WORK/game.log"))"

WIN="$("$WORK/xevt" "$DISP" findwin "$WM_NAME")" || die "no window named $WM_NAME"
"$WORK/xevt" "$DISP" focus "$WIN"
read -r AX AY CW CH BW DEPTH <<<"$("$WORK/xevt" "$DISP" geom "$WIN" 2>/dev/null || echo 0 0 688 702 0 24)"

grab() { # $1 = out png — client-area grab via xwd (border stripped)
    DISPLAY="$DISP" xwd -id "$WIN" -silent | convert xwd:- -crop "${CW}x${CH}+${BW}+${BW}" +repage "png:$1" \
        || die "capture failed"
}

grab "$EV/expose-before.png"
"$WORK/xevt" "$DISP" expose "$WIN"     # real Expose damage, whole window
sleep 3                                 # let Refresh()/swaps restore
kill -0 "$GAME_PID" 2>/dev/null || die "game died during expose handling"
grab "$EV/expose-after.png"

AE=$(compare -metric AE "$EV/expose-after.png" "$EV/expose-before.png" null: 2>&1 || true)
AE_HEAD="${AE%% *}"          # "0" for identical ("0 (0)"); fractional otherwise
printf 'leg_binary=%s window=%sx%s expose=XClearArea(whole,True) settle=3s AE=%s\n' \
    "$(basename "$GAME")" "$CW" "$CH" "$AE" >> "$EV/expose-summary.txt"
log "AE=$AE ($(basename "$GAME"))"
[ "$AE_HEAD" = "0" ] || die "content NOT restored after Expose: AE=$AE"

kill -9 "$GAME_PID" 2>/dev/null; GAME_PID=""
log "DONE"
exit 0
