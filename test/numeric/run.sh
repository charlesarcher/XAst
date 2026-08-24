#!/usr/bin/env bash
# test/numeric/run.sh — 45a numeric golden capture: build + two-run determinism
# proof + goldens.txt emission. NO makefile involvement (45b wires
# `make test-numeric` later; see README.md for the exact commands).
#
# Usage:  source qa/env/env.sh && test/numeric/run.sh [OUT_DIR]
#         OUT_DIR defaults to test/numeric/golden
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$REPO/test/numeric/golden}"
PROBE_BIN=/tmp/opencode/xast-numeric-probe
DISPLAY_NUM=9507

mkdir -p "$OUT"

# --- build (serialized; direct g++, no make) --------------------------------
source "$REPO/qa/env/env.sh"
flock /tmp/opencode/xast-build.lock \
  g++ -O3 -Wall -Wextra -Wno-unused-parameter -std=c++17 -DX11_BACKEND \
      -fno-access-control \
      -I"$REPO" -I/usr/include/X11 \
      -o "$PROBE_BIN" \
      "$REPO/test/numeric/probe.C" \
      "$REPO/utilities/pixmaps/rotated/rotatorDisplayData.C" \
      -lXm -lXt -lX11

# --- headless X (real RotVectorData pixmaps need a Display) ------------------
"$XAST_ENV_ROOT/bin/Xvfb" ":$DISPLAY_NUM" -screen 0 1280x1024x24 -nolisten tcp &
XVFB_PID=$!
cleanup() { kill "$XVFB_PID" 2>/dev/null || true; }
trap cleanup EXIT

for _ in $(seq 1 50); do
  [ -S "/tmp/.X11-unix/X$DISPLAY_NUM" ] && break
  sleep 0.1
done

# --- two-run determinism proof ------------------------------------------------
export DISPLAY=":$DISPLAY_NUM"
RUN1="$OUT/run1.txt"
RUN2="$OUT/run2.txt"
"$PROBE_BIN" > "$RUN1"
"$PROBE_BIN" > "$RUN2"

if diff -u "$RUN1" "$RUN2" > "$OUT/determinism.diff"; then
  echo "determinism: IDENTICAL ($(wc -c < "$RUN1") bytes per run)"
else
  echo "determinism: FAIL — outputs differ (see $OUT/determinism.diff)" >&2
  exit 1
fi

grep '^GOLD' "$RUN1" | sed 's/^GOLD //' > "$OUT/goldens.txt"
echo "goldens: $(wc -l < "$OUT/goldens.txt") cases -> $OUT/goldens.txt"
