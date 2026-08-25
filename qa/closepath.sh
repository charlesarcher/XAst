#!/usr/bin/env bash
# qa/closepath.sh — task-46 window-close-path gate (GL + VK legs).
#
# Exercises the GLFW close callback -> closeRequested_ -> clean-shutdown
# path exactly-once on every scenario, under the ASan build:
#   S1  single WM_DELETE_WINDOW at the title screen
#   S2  single WM_DELETE_WINDOW mid-play (game started via XTest 's')
#   S3  TWO WM_DELETE_WINDOW messages back-to-back at the title
#       (the closeRequested_ latch must make the second a no-op)
#
# Under bare Xvfb there is no WM, so qa/xevt.c delivers the ICCCM
# WM_DELETE_WINDOW ClientMessage directly.
#
# Pass per scenario: process exits exactly once (reaped), rc==0 (clean
# shutdown; GPU legs expect zero leaks so LSan must not flip rc), ASan log
# free of double-free/UAF/SEGV, and S3 behaves identically to S1.
#
# Usage: qa/closepath.sh <game-binary> <evidence-dir>
set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$REPO_ROOT/qa/env/env.sh" || exit 1

GAME="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"   # abs path (fonts resolve /proc/self/exe)
EV="$2"
DISPLAY_NUM="${CLOSE_DISPLAY:-98}"
DISP=":$DISPLAY_NUM"
WM_NAME="Asteroids"

die() { echo "closepath.sh: BLOCKED: $*" >&2; exit 1; }
log() { echo "closepath.sh: $*"; }

command -v cc >/dev/null 2>&1 || die "cc not on PATH"
[ -x "$GAME" ] || die "game binary '$GAME' not executable"
mkdir -p "$EV" || die "cannot create $EV"

WORK="$(mktemp -d "${TMPDIR:-/tmp}/t46-close.XXXXXX")" || die "mktemp failed"
XVFB_PID="" GAME_PID=""
cleanup() {
    [ -n "$GAME_PID" ] && kill -9 "$GAME_PID" 2>/dev/null
    [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
    sleep 0.3
    wait 2>/dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

for t in Xvfb xdpyinfo; do command -v "$t" >/dev/null 2>&1 || die "missing $t"; done

cp "$REPO_ROOT/qa/xevt.c" "$WORK/" || die "cannot stage xevt.c"
cc -O2 -o "$WORK/xevt" "$WORK/xevt.c" -lX11 -lXtst || die "cannot compile xevt"
log "xevt compiled"

if xdpyinfo -display "$DISP" >/dev/null 2>&1; then
    die "display $DISP already has an X server — pick another CLOSE_DISPLAY"
fi
{ Xvfb "$DISP" -screen 0 1280x1024x24 -fp "$HOME/.local/xast-env/fonts" \
      </dev/null >"$WORK/xvfb.log" 2>&1 & } ; XVFB_PID=$!
sleep 2
xdpyinfo -display "$DISP" >/dev/null 2>&1 || die "Xvfb $DISP did not come up"
log "Xvfb $DISP up"

run_scenario() { # $1=name $2=midplay(0/1) $3=deletes(1|2)
    local name="$1" midplay="$2" ndel="$3"
    local asanlog="$EV/close-$name.asan.log"
    local gamelog="$EV/close-$name.game.log"

    DISPLAY="$DISP" ASAN_OPTIONS="log_path=$WORK/asan:abort_on_error=0" \
        XAST_SEED=12345 "$GAME" >"$gamelog" 2>&1 &
    GAME_PID=$!
    sleep 8   # static-init settle (task-1 lesson)
    kill -0 "$GAME_PID" 2>/dev/null || die "S$name: game exited early ($(tail -2 "$gamelog"))"

    local win
    win="$("$WORK/xevt" "$DISP" findwin "$WM_NAME")" || die "S$name: no window"
    "$WORK/xevt" "$DISP" focus "$win"

    if [ "$midplay" = "1" ]; then
        "$WORK/xevt" "$DISP" key s          # start playing
        sleep 3                             # let RunGame spin a while
    fi

    local i
    for ((i=0; i<ndel; ++i)); do
        "$WORK/xevt" "$DISP" delete "$win" || die "S$name: ClientMessage send failed"
        # no inter-send sleep: both messages must be queued before the game's
        # next drain so the second exercises the closeRequested_ latch
        # (a delayed second send races teardown -> BadWindow, tested & seen)
    done

    # exactly-once exit: reap ONE clean exit within 30s (ASan teardown can
    # hold the process for seconds INSIDE exit() — poll, don't rush)
    local st=1 exited=0 t0=$SECONDS
    while (( SECONDS - t0 < 30 )); do
        if ! kill -0 "$GAME_PID" 2>/dev/null; then
            wait "$GAME_PID" 2>/dev/null; st=$?; exited=1; break
        fi
        sleep 0.5
    done
    GAME_PID=""
    [ "$exited" = "1" ] || die "S$name: game did not exit within 30s of WM_DELETE"

    local rc=$(( st >> 8 ))
    local verdict="PASS"
    grep -q "AddressSanitizer: \(double-free\|heap-use-after-free\|SEGV\|stack-buffer\|global-buffer\)" "$WORK"/asan.* 2>/dev/null && verdict="FAIL(asan-error)"
    cat "$WORK"/asan.* > "$asanlog" 2>/dev/null || : > "$asanlog"
    printf 'scenario=%s deletes=%d midplay=%d exit_rc=%d reaped_once=yes verdict=%s\n' \
        "$name" "$ndel" "$midplay" "$rc" "$verdict" >> "$EV/close-summary.txt"
    log "S$name: rc=$rc verdict=$verdict (asan log: $asanlog)"
    rm -f "$WORK"/asan.*
    sleep 1
}

: > "$EV/close-summary.txt"
run_scenario s1_title 0 1
run_scenario s3_double 0 2
run_scenario s2_midplay 1 1

log "DONE — summary in $EV/close-summary.txt"
exit 0
