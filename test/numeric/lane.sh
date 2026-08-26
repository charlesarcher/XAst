#!/usr/bin/env bash
# test/numeric/lane.sh — task 45b numeric QA lane runner (the live lane).
#
# Components (plan row 45, seam 45b):
#   (a) probe.C rebuilt from the CURRENT tree; its GOLD stream is diffed
#       against the committed PRE-task-24 goldens (golden/goldens.txt).
#       53/53 must match — any diff is a float-order regression (STOP).
#   (b) gravity FP guard asserts: probe C1 zero-distance guard + angles.C
#       G-section (same-point / denormal-offset constructed states emit
#       exactly Vector2d(), everything finite — no NaN/Inf anywhere).
#   (c) 500-angle seeded suite (angles.C): 2 runs per flavor byte-identical,
#       X11 flavor vs guards-closed GL-leg-domain-config hash equality,
#       pinned in golden/angles.golden.txt.
#   (d) seeded full-game runs through obj/harness (--handshake frame +
#       XAST_STATE_HASH_FILE) on the X11 and GL binaries: per-frame object-
#       state hashes must be identical across legs (435/435 frames).
#
# The GL leg of the unit suite compiles guards-closed with NO backend macro —
# exactly how the makefile compiles GAME_OBJECTS into obj/GL (only
# XAsteroids.o carries -DGL_BACKEND). See test/numeric/README.md.
#
# Usage:  source qa/env/env.sh && test/numeric/lane.sh
#         XAST_NUMERIC_SKIP_GAME=1 test/numeric/lane.sh   # unit legs only
# Exit 0 = all gates green.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO"
source "$REPO/qa/env/env.sh"

OUT="$REPO/test/numeric/out/45b"
WORK=/tmp/opencode/xast-numeric-45b
UNIT_DISPLAY=9510          # unit-leg Xvfb (probe/angles need a Display on the
                           # X11 flavor only; the harness manages its own for (d))
GAME_DISPLAY=9511
mkdir -p "$OUT" "$WORK"
FAIL=0

note()  { printf '[lane] %s\n' "$*"; }
gate()  { if [ "$1" -eq 0 ]; then note "GATE GREEN: $2"; else note "GATE RED: $2"; FAIL=1; fi; }

CXXFLAGS_LANE="-O3 -Wall -Wextra -Wno-unused-parameter -std=c++17 -fno-access-control"
VENDOR_INCS="-Ivendor/glad/include -Ivendor/stb -Ivendor/dear_imgui -Ivendor/glfw"

start_unit_xvfb() {
  "$XAST_ENV_ROOT/bin/Xvfb" ":$UNIT_DISPLAY" -screen 0 1280x1024x24 \
    -nolisten tcp > "$WORK/xvfb-unit.log" 2>&1 &
  UNIT_XVFB_PID=$!
  for _ in $(seq 1 50); do
    [ -S "/tmp/.X11-unix/X$UNIT_DISPLAY" ] && return 0
    sleep 0.1
  done
  note "unit Xvfb failed to start"; return 1
}
stop_unit_xvfb() { kill "${UNIT_XVFB_PID:-0}" 2>/dev/null || true; }
trap stop_unit_xvfb EXIT

# --- build both unit drivers --------------------------------------------------
note "building probe.C (X11 flavor) + angles.C (X11 + guards-closed flavors)"
flock /tmp/opencode/xast-build.lock \
  g++ $CXXFLAGS_LANE -DX11_BACKEND -I. -I/usr/include/X11 \
      -o "$WORK/probe-x11" \
      test/numeric/probe.C utilities/pixmaps/rotated/rotatorDisplayData.C \
      -lXm -lXt -lX11

flock /tmp/opencode/xast-build.lock \
  g++ $CXXFLAGS_LANE -DX11_BACKEND -I. -I/usr/include/X11 \
      -o "$WORK/angles-x11" \
      test/numeric/angles.C utilities/pixmaps/rotated/rotatorDisplayData.C \
      -lXm -lXt -lX11

flock /tmp/opencode/xast-build.lock \
  g++ $CXXFLAGS_LANE $VENDOR_INCS -I. \
      -o "$WORK/angles-gc" \
      test/numeric/angles.C utilities/pixmaps/rotated/rotatorDisplayData.C

start_unit_xvfb
export DISPLAY=":$UNIT_DISPLAY"

# --- (a)+(b): probe vs the committed PRE-task-24 goldens ----------------------
note "(a) probe run 1+2 vs golden/goldens.txt"
"$WORK/probe-x11" > "$WORK/probe-run1.txt"
"$WORK/probe-x11" > "$WORK/probe-run2.txt"
if diff -q "$WORK/probe-run1.txt" "$WORK/probe-run2.txt" >/dev/null; then
  gate 0 "probe two-run determinism"
else
  gate 1 "probe two-run determinism ($WORK/probe-run*.txt differ)"
fi
grep '^GOLD' "$WORK/probe-run1.txt" | sed 's/^GOLD //' > "$WORK/goldens-current.txt"
if diff "$WORK/goldens-current.txt" test/numeric/golden/goldens.txt > "$OUT/golden-drift.diff"; then
  gate 0 "goldens $(wc -l < test/numeric/golden/goldens.txt)/$(wc -l < test/numeric/golden/goldens.txt) match (zero drift)"
else
  gate 1 "GOLDEN DRIFT — see $OUT/golden-drift.diff (float-order regression: STOP and report)"
fi
AF=$(awk '/^assert-failures/{print $2}' "$WORK/probe-run1.txt")
[ "${AF:-1}" = "0" ]; gate $? "probe asserts green incl. C1 zero-distance guard (assert-failures=$AF)"

# --- (c)+(b): 500-angle suite, both flavors -----------------------------------
note "(c) 500-angle suite: 2 runs x 2 flavors"
for flavor in x11 gc; do
  "$WORK/angles-$flavor" > "$WORK/angles-$flavor-run1.txt"
  "$WORK/angles-$flavor" > "$WORK/angles-$flavor-run2.txt"
  if diff -q "$WORK/angles-$flavor-run1.txt" "$WORK/angles-$flavor-run2.txt" >/dev/null; then
    gate 0 "angles[$flavor] two-run determinism"
  else
    gate 1 "angles[$flavor] two-run determinism"
  fi
done
HASH_X11=$(awk '/^HASH/{print $3}' "$WORK/angles-x11-run1.txt")
HASH_GC=$(awk '/^HASH/{print $3}' "$WORK/angles-gc-run1.txt")
[ "$HASH_X11" = "$HASH_GC" ]
gate $? "500-angle hash identical across X11 vs guards-closed(GL-leg) configs"
grep '^GOLD' "$WORK/angles-x11-run1.txt" | sed 's/^GOLD //' > "$WORK/angles-current.golden.txt"
if diff "$WORK/angles-current.golden.txt" test/numeric/golden/angles.golden.txt >/dev/null; then
  gate 0 "500-angle golden pin matches golden/angles.golden.txt"
else
  gate 1 "500-angle golden pin drifted vs golden/angles.golden.txt"
fi
AFA=$(awk '/^assert-failures/{print $2}' "$WORK/angles-x11-run1.txt")
[ "${AFA:-1}" = "0" ]; gate $? "angles gravity guard asserts green (assert-failures=$AFA)"

stop_unit_xvfb

# --- archive unit evidence ----------------------------------------------------
cp "$WORK"/probe-run*.txt "$WORK"/angles-*-run*.txt "$OUT/" 2>/dev/null || true

# --- (d): seeded full-game state-hash parity, X11 vs GL -----------------------
if [ "${XAST_NUMERIC_SKIP_GAME:-0}" = "1" ]; then
  note "(d) skipped (XAST_NUMERIC_SKIP_GAME=1)"
else
  for flavor in X11 GL; do
    lf=$(echo "$flavor" | tr A-Z a-z)
    note "(d) building BACKEND=$flavor game binary"
    pgrep -x XAsteroids >/dev/null 2>&1 && pkill -x XAsteroids || true
    rm -f XAsteroids   # flavor rule: one ./XAsteroids target, last link wins
    flock /tmp/opencode/xast-build.lock make "BACKEND=$flavor" >/dev/null
    GLFWCNT=$(ldd ./XAsteroids | grep -c glfw || true)
    if [ "$flavor" = "X11" ]; then [ "$GLFWCNT" = "0" ]; else [ "$GLFWCNT" -ge 1 ]; fi
    gate $? "binary flavor check ($flavor, ldd glfw count=$GLFWCNT)"
    note "(d) seeded harness run, BACKEND=$flavor"
    export XAST_STATE_HASH_FILE="$OUT/$lf.state.hash"
    rm -f "$XAST_STATE_HASH_FILE"
    # --keep: the harness (frame mode) repoints XAST_STATE_HASH_FILE at its own
    # work dir, so the stream is harvested from there, not from $OUT.
    if ./obj/harness --seed 12345 --script test/harness/scripts/session.script \
         --out "$OUT/$lf-run" --handshake frame --keep \
         --hiscore test/harness/fixtures/hiScore.nul.data \
         --display ":$GAME_DISPLAY" > "$WORK/harness-$lf.log" 2>&1; then
      gate 0 "harness session completed ($flavor)"
    else
      gate 1 "harness session failed ($flavor) — see $WORK/harness-$lf.log"
    fi
  done
  for lf in x11 gl; do
    wd=$(sed -n 's/.*work dir: //p' "$WORK/harness-$lf.log" | head -1 | sed 's/ (kept)$//')
    grep '^h ' "$wd/statehash" > "$OUT/$lf.state.hash"
  done
  grep '^h ' "$OUT/x11.state.hash" > "$WORK/x11.frames"
  grep '^h ' "$OUT/gl.state.hash"  > "$WORK/gl.frames"
  N=$(wc -l < "$WORK/x11.frames")
  if cmp -s "$WORK/x11.frames" "$WORK/gl.frames"; then
    gate 0 "X11-vs-GL numeric match: $N/$N frame state hashes identical"
  else
    gate 1 "X11-vs-GL state-hash mismatch ($N frames) — D8 float-order regression across backends"
  fi
fi

# --- summary -------------------------------------------------------------------
if [ "$FAIL" -eq 0 ]; then
  note "ALL GATES GREEN"
else
  note "ONE OR MORE GATES RED — see lines above"
fi
exit "$FAIL"
