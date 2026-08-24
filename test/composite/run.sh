#!/bin/bash
# Task-27 5-frame composite assertion probe (rendering-abstraction plan).
#
# Proves the CPU-composited 5 explosion frames (compositePixmap.C's #else
# engine branch, compiled guards-closed) are byte-equal to the guarded
# CompositePixmap pipeline's output, read back via XGetImage under a
# short-lived Xvfb on display :100 (:99 stays free for the harness).
#
# Usage: qa/env/env.sh must be sourced first; then
#   test/composite/run.sh [output-dir]      (default: test/composite/out)
# Writes PASS/FAIL per frame to <output-dir>/result.log and echoes it.
# Exit code 0 iff all frames + masks match.

set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
cd "$REPO"
OUT=${1:-test/composite/out}
mkdir -p "$OUT"

CXXFLAGS="-I/usr/include/X11 -O2 -Wall -Wextra -Wno-unused-parameter -std=c++17 -I."
LOG="$OUT/result.log"
: > "$LOG"

echo "== building legs ==" | tee -a "$LOG"

# CPU leg: guards-closed composite unit + probe.
g++ $CXXFLAGS -c utilities/pixmaps/composite/compositePixmap.C -o "$OUT/compositeCPU.o" \
  || { echo "BUILD FAIL: compositeCPU" | tee -a "$LOG"; exit 1; }
g++ $CXXFLAGS -c test/composite/probeCPU.C -o "$OUT/probeCPU.o" \
  || { echo "BUILD FAIL: probeCPU" | tee -a "$LOG"; exit 1; }
g++ "$OUT/compositeCPU.o" "$OUT/probeCPU.o" -o "$OUT/probeCPU" \
  || { echo "LINK FAIL: probeCPU" | tee -a "$LOG"; exit 1; }

# X11 leg: macro'd composite unit + twin probe.
g++ $CXXFLAGS -DX11_BACKEND -c utilities/pixmaps/composite/compositePixmap.C -o "$OUT/compositeX11.o" \
  || { echo "BUILD FAIL: compositeX11" | tee -a "$LOG"; exit 1; }
g++ $CXXFLAGS -DX11_BACKEND -c test/composite/probeX11.C -o "$OUT/probeX11.o" \
  || { echo "BUILD FAIL: probeX11" | tee -a "$LOG"; exit 1; }
g++ "$OUT/compositeX11.o" "$OUT/probeX11.o" -o "$OUT/probeX11" -L/usr/lib/X11 -lX11 \
  || { echo "LINK FAIL: probeX11" | tee -a "$LOG"; exit 1; }

echo "== running CPU leg ==" | tee -a "$LOG"
"$OUT/probeCPU" "$OUT" || { echo "RUN FAIL: probeCPU" | tee -a "$LOG"; exit 1; }

echo "== running X11 leg under Xvfb :100 ==" | tee -a "$LOG"
XVFB=$(command -v Xvfb)
[ -x "$XVFB" ] || XVFB="$HOME/.local/xast-env/bin/Xvfb"
"$XVFB" :100 -screen 0 1280x1024x24 -nolisten tcp >/dev/null 2>&1 &
XVFB_PID=$!
trap 'kill $XVFB_PID 2>/dev/null; wait $XVFB_PID 2>/dev/null' EXIT
sleep 2
DISPLAY=:100 "$OUT/probeX11" "$OUT" || { echo "RUN FAIL: probeX11" | tee -a "$LOG"; exit 1; }

echo "== comparing ==" | tee -a "$LOG"
RC=0
for i in 0 1 2 3 4; do
  if cmp -s "$OUT/cpu_frame$i.bin" "$OUT/x11_frame$i.bin"; then
    echo "frame$i: PASS ($(stat -c%s "$OUT/cpu_frame$i.bin") bytes byte-equal)" | tee -a "$LOG"
  else
    echo "frame$i: FAIL" | tee -a "$LOG"
    RC=1
  fi
done

# Mask sanity: expanded R8 planes must be all-{0,255} and non-empty.
for m in center middle edge; do
  python3 - "$OUT/cpu_mask_$m.bin" <<'PY' || RC=1
import sys, struct
data=open(sys.argv[1],'rb').read()
w,h=struct.unpack('<ii',data[:8])
px=data[8:]
assert w*h==len(px), f"{sys.argv[1]}: size mismatch"
assert set(px)<= {0,255} and max(px)==255, f"{sys.argv[1]}: not a clean R8 mask"
print(f"mask {sys.argv[1].split('/')[-1]}: PASS ({w}x{h}, {sum(1 for v in px if v==255)} set px)")
PY
done

if [ $RC -eq 0 ]; then
  echo "RESULT: PASS — 5/5 explosion composite frames byte-equal (CPU vs Xvfb XGetImage)" | tee -a "$LOG"
else
  echo "RESULT: FAIL" | tee -a "$LOG"
fi
exit $RC
