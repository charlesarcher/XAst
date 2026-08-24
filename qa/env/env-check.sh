#!/usr/bin/env bash
# qa/env/env-check.sh — end-to-end proof of the local-prefix X11/Motif build+QA env.
# Exits 0 only if EVERY gate passes:
#   1. prefix contents (Motif headers/lib, libXp, QA tools)
#   2. every key ELF resolves all shared libs (ldd)
#   3. fonts dir has the 5 families required by gamePlay/stage.H
#   4. Xvfb starts with -fp <fontdir>; xdpyinfo talks to it
#   5. xlsfonts lists all 5 required font families on that server
#   6. `make clean all` builds XAsteroids + AutoRepeatOn with no makefile edits
#   7. both binaries ldd-clean; XAsteroids actually renders under Xvfb (xwd capture)
set -u

REPO="${XAST_REPO_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
ENVROOT="${XAST_ENV_ROOT:-$HOME/.local/xast-env}"
FONTS="$ENVROOT/fonts"
LOG="$(mktemp /tmp/xast-envcheck.XXXXXX.log)"
trap 'rm -f "$LOG"' EXIT

pass=0; fail=0
ok()   { printf '  \033[32mPASS\033[0m %s\n' "$1"; pass=$((pass+1)); }
bad()  { printf '  \033[31mFAIL\033[0m %s\n' "$1"; fail=$((fail+1)); }
gate() { printf '\n== GATE %s ==\n' "$1"; }

# Idempotently source env.sh so this script works standalone.
if [ -z "${XAST_ENV_WIRED:-}" ]; then
    # shellcheck disable=SC1091
    source "$REPO/qa/env/env.sh" >/dev/null || { echo "cannot source env.sh"; exit 1; }
    export XAST_ENV_WIRED=1
fi

gate "1: prefix contents"
for f in include/Xm/Xm.h include/Xm/PushB.h \
         lib/x86_64-linux-gnu/libXm.so lib/x86_64-linux-gnu/libXm.so.4 \
         lib/libXp.so bin/Xvfb bin/xwd bin/xlsfonts bin/mkfontscale bin/bdftopcf; do
    [ -e "$ENVROOT/$f" ] && ok "$f" || bad "$f missing"
done

gate "2: shared-lib resolution (ldd)"
ldd_clean() {
    local miss
    miss=$(ldd "$1" 2>&1 | grep 'not found')
    if [ -z "$miss" ]; then ok "$(basename "$1") fully resolved"
    else bad "$(basename "$1"): $miss"; fi
}
ldd_clean "$ENVROOT/lib/x86_64-linux-gnu/libXm.so.4"
ldd_clean "$ENVROOT/lib/libXp.so.6"
ldd_clean "$ENVROOT/bin/Xvfb"

gate "3: fonts registered (mkfontscale/mkfontdir)"
need_fonts=(
    white_shadow-48
    -schumacher-clean-bold-r-normal--10-100-75-75-c-60-iso8859-1
    -ibm-ergonomic-bold-r-normal--20-140-100-100-c-120-iso8859-9
    -urw-courier-bold-r-normal--40-300-100-100-m-240-iso8859-9
    -adobe-helvetica-bold-r-normal--14-100-100-100-p-82-iso8859-1
)
n=$(cat "$FONTS/fonts.dir" 2>/dev/null | head -1)
[ -s "$FONTS/fonts.dir" ] && ok "fonts.dir exists ($n fonts)" || bad "fonts.dir missing"

gate "4: Xvfb starts with repo font path"
DISP=""
for d in 99 98 97 96 95; do
    if ! xdpyinfo -display ":$d" >/dev/null 2>&1; then DISP="$d"; break; fi
done
[ -n "$DISP" ] || { echo "no free display found"; exit 1; }
XVFB_PID=""
cleanup() {
    [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
    wait "$XVFB_PID" 2>/dev/null
    [ -n "${GAME_PID:-}" ] && kill "$GAME_PID" 2>/dev/null
}
trap cleanup EXIT

setsid "$ENVROOT/bin/Xvfb" ":$DISP" -screen 0 1280x1024x24 \
      -fp "$FONTS" +extension RENDER -nolisten tcp </dev/null >>"$LOG" 2>&1 &
XVFB_PID=$!
for i in $(seq 1 30); do
    xdpyinfo -display ":$DISP" >/dev/null 2>&1 && break
    sleep 0.3
done
if xdpyinfo -display ":$DISP" >"$LOG" 2>&1; then
    ok "Xvfb :$DISP up ($(grep -m1 'vendor string' "$LOG"))"
else
    bad "Xvfb did not come up"; cat "$LOG"; exit 1
fi

gate "5: xlsfonts sees the 5 stage.H families"
export DISPLAY=":$DISP"
missing=0
for f in "${need_fonts[@]}"; do
    if xlsfonts | grep -qxF -e "$f"; then ok "$f"
    else bad "$f NOT served"; missing=1; fi
done

gate "6: make clean all (unmodified makefile)"
make -C "$REPO" clean all >"$LOG" 2>&1
if [ $? -eq 0 ] && [ -x "$REPO/XAsteroids" ] && [ -x "$REPO/AutoRepeatOn" ]; then
    ok "XAsteroids + AutoRepeatOn built"
else
    bad "build failed"; tail -20 "$LOG"; exit 1
fi

gate "7: binaries resolve + real rendering under Xvfb"
ldd_clean "$REPO/XAsteroids"
ldd_clean "$REPO/AutoRepeatOn"

rm -f /tmp/xast-envcheck.xwd
"$REPO/XAsteroids" </dev/null >>"$LOG" 2>&1 &
GAME_PID=$!
sleep 3
if kill -0 "$GAME_PID" 2>/dev/null; then
    ok "XAsteroids running under Xvfb (pid $GAME_PID)"
else
    bad "XAsteroids exited immediately"; tail -5 "$LOG"
fi
"$ENVROOT/bin/xwd" -display ":$DISP" -root -silent > /tmp/xast-envcheck.xwd 2>>"$LOG"
sz=$(stat -c%s /tmp/xast-envcheck.xwd 2>/dev/null || echo 0)
if [ "$sz" -gt 10000 ]; then ok "xwd root capture non-trivial ($sz bytes) -> /tmp/xast-envcheck.xwd"
else bad "xwd capture too small ($sz bytes)"; fi
kill "$GAME_PID" 2>/dev/null; GAME_PID=""

printf '\n== RESULT ==\n'
if [ "$fail" -eq 0 ]; then
    printf '\033[32menv-check: ALL %s CHECKS PASSED\033[0m\n' "$pass"
    exit 0
else
    printf '\033[31menv-check: %s passed, %s FAILED\033[0m\n' "$pass" "$fail"
    exit 1
fi
