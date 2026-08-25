#!/usr/bin/env bash
# qa/menu-vk-q5.sh — Q5 VK leg driver + assertions (task 44b).
#
# Runs test/harness/scripts/menu-vk.script through the harness in frame
# handshake mode, live-samples the game's published frame counter and
# state-hash files, then asserts (the GL-leg assertion set, task 44a):
#   1. measured frame period SHIFTS 16fps -> 32fps (the D4 uSecondsPerFrame
#      path, verified through the game's own pacing);
#   2. simulation PAUSES while the menu is open (frame counter + state-hash
#      line count frozen between the menu_open and menu_closed captures);
#   3. deterministic resume: state-hash frame numbers strictly +1 across the
#      whole session (no reset, no skip across the pause);
#   4. pixel identities: no-op click AE=0, Load round-trip AE=0, Mute
#      round-trip AE=0, overlay erased on close, world moving after resume;
#   5. preferences file: options.H format (29 lines), first value = the
#      applied uSecondsPerFrame (31250 = 1E6/32).
#
# Usage: qa/menu-vk-q5.sh [--out DIR] (default /tmp/opencode/q5vk-run)
# Exit 0 = PASS; 1 = FAIL.

set -u
cd "$(dirname "$0")/.." || exit 2
source qa/env/env.sh || exit 2

OUT=/tmp/opencode/q5vk-run
[ "${1:-}" = "--out" ] && OUT="${2:-$OUT}"
rm -rf "$OUT"; mkdir -p "$OUT"
LOG="$OUT/harness.log"; SAMPLES="$OUT/samples.log"

fail=0
note() { printf '%s\n' "$*"; }
check() { # check <name> <expr-result 0|1> <detail>
  if [ "$2" -eq 0 ]; then note "PASS: $1 ($3)"; else note "FAIL: $1 ($3)"; fail=1; fi
}

# ---- preflight -------------------------------------------------------------
# Flavor rule (VK leg): glfw AND vulkan must both link; X11 libs must not.
ldd ./XAsteroids | grep -q glfw || { note "FAIL: ./XAsteroids is not a GPU flavor"; exit 1; }
ldd ./XAsteroids | grep -q vulkan || { note "FAIL: ./XAsteroids is not the VK flavor"; exit 1; }
[ -x ./obj/harness ] || make harness >/dev/null 2>&1 || { note "FAIL: no harness"; exit 1; }
pkill -x Xvfb 2>/dev/null; sleep 1

# ---- run + live sampling ---------------------------------------------------
# XAST_QA_MENU_RECTS: the menu's env-gated QA log (widget rects once, every
# applied fps value) — inert without it; the D4-path assertion reads it.
XAST_QA_MENU_RECTS=1 ./obj/harness --seed 12345 --script test/harness/scripts/menu-vk.script \
  --out "$OUT/caps" --hiscore test/harness/fixtures/hiScore.nul.data \
  --handshake frame --keep --tick-gap-ms 20 --static-idle-ms 100 \
  --idle-escape-ms 60 --max-run 600 >"$LOG" 2>&1 &
HARNESS_PID=$!

# Sampler: monotonic ms (matches the harness manifest clock) + frame counter
# + state-hash line count, every 100 ms until the harness exits.
(
  while kill -0 "$HARNESS_PID" 2>/dev/null; do
    wd=$(sed -n 's/.*work dir: //p' "$LOG" | head -1 | sed 's/ (kept)$//')
    if [ -n "$wd" ] && [ -f "$wd/framecount" ]; then
      t=$(awk '{printf "%d", $1*1000}' /proc/uptime)
      fc=$(cat "$wd/framecount" 2>/dev/null || echo -1)
      hl=$(wc -l < "$wd/statehash" 2>/dev/null || echo 0)
      printf '%s %s %s\n' "$t" "$fc" "$hl" >>"$SAMPLES"
    fi
    sleep 0.1
  done
) &
SAMPLER_PID=$!
wait "$HARNESS_PID"; HARNESS_RC=$?
kill "$SAMPLER_PID" 2>/dev/null; wait "$SAMPLER_PID" 2>/dev/null

WD=$(sed -n 's/.*work dir: //p' "$LOG" | head -1 | sed 's/ (kept)$//')
note "work dir: $WD (kept)  harness rc=$HARNESS_RC"
check "harness run" $([ $HARNESS_RC -eq 0 ] && grep -q "RESULT: captured" "$LOG" && echo 0 || echo 1) "rc=$HARNESS_RC"

# ---- manifest capture times ------------------------------------------------
declare -A T
while read -r name tms; do T[$name]=$tms; done \
  < <(sed -n 's/^  cp \([A-Za-z_0-9]*\) boundary=[0-9]* t=\([0-9]*\)ms.*/\1 \2/p' "$OUT/caps/manifest.txt")

# ---- 1. measured frame-period shift ----------------------------------------
rate() { # rate <t1> <t2> -> frames/sec between two sample times (nearest samples)
  awk -v a="$1" -v b="$2" '
    {t[NR]=$1; fc[NR]=$2}
    END{ia=0; ib=0
        for(i=1;i<=NR;++i){if(t[i]<=a)ia=i; if(t[i]<=b)ib=i}
        if(ib<=ia||t[ib]==t[ia]){print "0 0"; exit}
        printf "%.4f %.1f", (fc[ib]-fc[ia])*1000/(t[ib]-t[ia]), (t[ib]-t[ia])}' "$SAMPLES"
}
read -r R16 W16 <<<"$(rate "${T[pre_a]}" "${T[pre_b]}")"
read -r R32 W32 <<<"$(rate "${T[post_a]}" "${T[post_c]}")"
P16=$(awk -v r="$R16" 'BEGIN{print (r>0)?1000/r:0}')
P32=$(awk -v r="$R32" 'BEGIN{print (r>0)?1000/r:0}')
note "measured: pre  ${P16} ms/frame (${R16} fps over ${W16} ms)"
note "measured: post ${P32} ms/frame (${R32} fps over ${W32} ms)"
RATIO=$(awk -v a="$P16" -v b="$P32" 'BEGIN{print (b>0)?a/b:0}')
check "period shift 16->32" \
  $(awk -v r="$RATIO" 'BEGIN{print (r>=1.6 && r<=2.4)?0:1}') "ratio=${RATIO}"
check "post period ~31.25ms" \
  $(awk -v p="$P32" 'BEGIN{print (p>=25 && p<=40)?0:1}') "P32=${P32}"

# ---- 2. pause while the menu is open ----------------------------------------
# Window ends at menu_unmuted (the last capture BEFORE the close click): the
# frame-mode boundary counter rewinds when publishes resume after an
# idle-ticked pause, so later captures' wall times overshoot the close.
PAUSE_MS=$(( ${T[menu_unmuted]} - ${T[menu_open]} ))
FCADV=$(awk -v a="${T[menu_open]}" -v b="${T[menu_unmuted]}" '
  {t[NR]=$1;fc[NR]=$2}
  END{mn=-1;mx=-1;for(i=1;i<=NR;++i)if(t[i]>=a&&t[i]<=b){if(mn<0||fc[i]<mn)mn=fc[i];if(fc[i]>mx)mx=fc[i]}
      print mx-mn+0}' "$SAMPLES")
note "pause window ${PAUSE_MS} ms: frame counter advanced ${FCADV}"
check "paused while open" $([ "$FCADV" -le 2 ] && echo 0 || echo 1) "advanced=${FCADV} frames in ${PAUSE_MS}ms"

# ---- 3. deterministic resume: state-hash continuity ------------------------
BAD=$(awk 'BEGIN{prev=-1;bad=0} /^h /{f=$2; if(prev>=0 && f!=prev+1)++bad; prev=f} END{print bad+0}' "$WD/statehash")
TOTAL=$(grep -c '^h ' "$WD/statehash")
check "state-hash continuity (+1 throughout)" $([ "$BAD" -eq 0 ] && echo 0 || echo 1) "violations=$BAD frames=$TOTAL"

# ---- 4. pixel identities ----------------------------------------------------
ae() { compare -metric AE "$OUT/caps/$1.png" "$OUT/caps/$2.png" null: 2>&1; }
AE_NOOP=$(ae menu_noop menu_open);      check "no-op click checkpoint" $(awk -v a="$AE_NOOP" 'BEGIN{print (a+0==0)?0:1}') "AE=$AE_NOOP"
AE_LOAD=$(ae menu_loaded menu_saved);   check "preferences Load round-trip" $(awk -v a="$AE_LOAD" 'BEGIN{print (a+0==0)?0:1}') "AE=$AE_LOAD"
AE_UNMUTE=$(ae menu_unmuted menu_saved); check "Mute round-trip" $(awk -v a="$AE_UNMUTE" 'BEGIN{print (a+0==0)?0:1}') "AE=$AE_UNMUTE"
AE_MUTE=$(ae menu_muted menu_saved)
check "Mute toggles state" $(awk -v a="${AE_MUTE%%.*}" 'BEGIN{print (a>0)?0:1}') "AE=$AE_MUTE"
AE_FPS=$(ae menu_fps32 menu_open)
check "FPS slider changed UI" $(awk -v a="${AE_FPS%%.*}" 'BEGIN{print (a>0)?0:1}') "AE=$AE_FPS"
AE_CLOSED=$(ae menu_closed menu_unmuted)
check "overlay erased on close" $(awk -v a="${AE_CLOSED%%.*}" 'BEGIN{print (a>0)?0:1}') "AE=$AE_CLOSED"
AE_MOVE=$(ae post_a post_b)
check "simulation resumed after close" $(awk -v a="${AE_MOVE%%.*}" 'BEGIN{print (a>0)?0:1}') "AE=$AE_MOVE"

# ---- 5. preferences file (options.H format) ---------------------------------
PREFS="$WD/XAsteroids.prefs"
if [ -f "$PREFS" ]; then
  LINES=$(wc -l < "$PREFS")
  FIRST=$(head -1 "$PREFS" | tr -d '[:space:]')
  check "prefs file 29 lines" $([ "$LINES" -eq 29 ] && echo 0 || echo 1) "lines=$LINES"
  check "prefs uSecondsPerFrame=31250" $([ "$FIRST" = "31250" ] && echo 0 || echo 1) "first=$FIRST"
else
  check "prefs file exists" 1 "missing"
fi
grep -aq "menu] fps 32" "$WD/game.log"
check "D4 path fired (fps 32 applied)" $(grep -aq "menu] fps 32" "$WD/game.log" && echo 0 || echo 1) "game.log"

note ""
if [ $fail -eq 0 ]; then note "RESULT: PASS (Q5 VK leg)"; exit 0
else note "RESULT: FAIL (Q5 VK leg)"; exit 1; fi
