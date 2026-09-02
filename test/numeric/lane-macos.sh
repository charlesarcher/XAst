#!/usr/bin/env bash
# test/numeric/lane-macos.sh — task 12 numeric state-hash parity lane (macOS).
#
# Proves the Metal (MTL) backend's simulation state is bit-identical to the
# Vulkan (VK) backend's on the same seed, by running BOTH game binaries
# DIRECTLY (no harness/Xvfb/XTest — those don't exist on macOS) with identical
# env vars and a frame-counter-bounded run, then comparing the per-frame
# state-hash streams.
#
# Mechanics (plan task 12, B4 fix): lane.sh's obj/harness + Xvfb + XTest +
# frame-gate handshake does NOT exist on macOS. Instead we:
#   - build BACKEND=VK  -> ./XAsteroids, run it from the repo root
#   - build BACKEND=MTL -> ./XAsteroids (overwrites the VK binary), run it
#   - run each binary directly with identical XAST_SEED + XAST_STATE_HASH_FILE
#     + XAST_FRAME_COUNTER_FILE
#   - poll XAST_FRAME_COUNTER_FILE until N=435 frames, then SIGTERM the run
#   - compare `grep '^h '` frame lines from both statehash files (lane.sh:158-162)
#
# NOTE on binary location: both backends resolve their runtime resources
# (VK: obj/VK/spv/*.spv; MTL: obj/MTL/aestroids.metallib + vendor/fonts/*.ttf)
# relative to the executable's own directory (resolveExeDir_). The makefile
# outputs every backend to the same ./XAsteroids name, so we run each backend
# from the repo root BEFORE the next build overwrites the binary — this keeps
# resource resolution correct without copying resource trees around.
#
# Exit 0 = streams match, exit 1 = mismatch.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO"

SEED=12345
N_FRAMES=435
WORK=/tmp/opencode/xast-numeric-macos
EVID="$REPO/qa/metal-evidence/statehash"
mkdir -p "$WORK" "$EVID"

# VK on Darwin needs the homebrew vulkan-loader discoverable by dyld (GLFW
# dlopens "libvulkan.1.dylib" by bare name) + the MoltenVK ICD.
VK_DYLD="DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/opt/vulkan-loader/lib"
VK_ICD="/opt/homebrew/opt/molten-vk/etc/vulkan/icd.d/MoltenVK_icd.json"

note() { printf '[lane-macos] %s\n' "$*"; }
FAIL=0
gate() { if [ "$1" -eq 0 ]; then note "GATE GREEN: $2"; else note "GATE RED: $2"; FAIL=1; fi; }

# --- run one backend, poll the frame counter, SIGTERM at N frames ------------
# xastQaPublishFrame (playingField.H:333-370) writes XAST_FRAME_COUNTER_FILE
# after each completed gameplay frame, so once we read N=435 the statehash
# stream already has 435 frames. SIGTERM (not SIGKILL) lets the process exit
# cleanly; the game installs no SIGTERM handler, so the default terminates it.
run_backend() {
  local name="$1" hash="$2" counter="$3" dyld_env="$4"
  note "running $name (./XAsteroids)"
  rm -f "$hash" "$counter"
  local pid
  if [ -n "$dyld_env" ]; then
    env $dyld_env \
      XAST_SEED="$SEED" \
      XAST_STATE_HASH_FILE="$hash" \
      XAST_FRAME_COUNTER_FILE="$counter" \
      XAST_AUTOSTART=1 \
      ./XAsteroids > "$WORK/$name.log" 2>&1 &
  else
    env \
      XAST_SEED="$SEED" \
      XAST_STATE_HASH_FILE="$hash" \
      XAST_FRAME_COUNTER_FILE="$counter" \
      XAST_AUTOSTART=1 \
      ./XAsteroids > "$WORK/$name.log" 2>&1 &
  fi
  pid=$!
  # Poll the counter until it reaches N_FRAMES (or a hard timeout).
  local waited=0
  while [ "$waited" -lt 3000 ]; do
    if ! kill -0 "$pid" 2>/dev/null; then
      note "$name exited early (pid $pid gone) — see $WORK/$name.log"
      return 1
    fi
    if [ -f "$counter" ]; then
      local n
      n=$(head -1 "$counter" 2>/dev/null || echo 0)
      if [ "${n:-0}" -ge "$N_FRAMES" ] 2>/dev/null; then
        break
      fi
    fi
    sleep 0.1
    waited=$((waited+1))
  done
  if [ "$waited" -ge 3000 ]; then
    note "$name timed out waiting for $N_FRAMES frames"
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    return 1
  fi
  note "$name reached $N_FRAMES frames — sending SIGTERM"
  kill -TERM "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  return 0
}

MTL_HASH="$EVID/mtl.statehash"
VK_HASH="$EVID/vk.statehash"
MTL_CNT="$WORK/mtl.counter"
VK_CNT="$WORK/vk.counter"

# --- build + run VK first (from repo root, so obj/VK/spv resolves) ------------
note "building BACKEND=VK"
pgrep -x XAsteroids >/dev/null 2>&1 && pkill -x XAsteroids || true
rm -f XAsteroids
make BACKEND=VK >/dev/null

if [ -f "$VK_ICD" ]; then
  VK_ENV="$VK_DYLD VK_ICD_FILENAMES=$VK_ICD"
else
  note "WARNING: MoltenVK ICD not found at $VK_ICD — running VK without it"
  VK_ENV="$VK_DYLD"
fi
run_backend "vk" "$VK_HASH" "$VK_CNT" "$VK_ENV"
gate $? "VK run completed ($N_FRAMES frames)"

# --- build + run MTL (overwrites ./XAsteroids; run from repo root) ------------
note "building BACKEND=MTL"
pgrep -x XAsteroids >/dev/null 2>&1 && pkill -x XAsteroids || true
rm -f XAsteroids
make BACKEND=MTL >/dev/null

run_backend "mtl" "$MTL_HASH" "$MTL_CNT" ""
gate $? "MTL run completed ($N_FRAMES frames)"

# --- compare the per-frame state-hash streams (lane.sh:158-162) ---------------
# The counter is written AFTER each frame completes, so when the poll reads
# N_FRAMES the stream already has N_FRAMES lines. But the game may produce one
# more frame before SIGTERM lands, so a leg can have N_FRAMES+1 lines. Compare
# only the first N_FRAMES lines of each stream (the deterministic prefix).
grep '^h ' "$MTL_HASH" > "$WORK/mtl.frames"
grep '^h ' "$VK_HASH"  > "$WORK/vk.frames"
N=$(wc -l < "$WORK/mtl.frames")
NV=$(wc -l < "$WORK/vk.frames")
note "MTL frames: $N, VK frames: $NV"
if [ "$N" -lt "$N_FRAMES" ] || [ "$NV" -lt "$N_FRAMES" ]; then
  gate 1 "insufficient frames (MTL=$N, VK=$NV, need $N_FRAMES)"
else
  head -n "$N_FRAMES" "$WORK/mtl.frames" > "$WORK/mtl.prefix"
  head -n "$N_FRAMES" "$WORK/vk.frames"  > "$WORK/vk.prefix"
  if cmp -s "$WORK/mtl.prefix" "$WORK/vk.prefix"; then
    gate 0 "MTL-vs-VK numeric match: $N_FRAMES/$N_FRAMES frame state hashes identical"
    echo "MATCH $N_FRAMES/$N_FRAMES" > "$EVID/compare.result"
  else
    gate 1 "MTL-vs-VK state-hash mismatch ($N_FRAMES frames) — simulation divergence"
    echo "MISMATCH $N_FRAMES/$N_FRAMES" > "$EVID/compare.result"
    diff "$WORK/mtl.prefix" "$WORK/vk.prefix" > "$EVID/compare.diff" || true
  fi
fi

# --- archive evidence ----------------------------------------------------------
cp "$WORK/mtl.frames" "$EVID/mtl.frames"
cp "$WORK/vk.frames"  "$EVID/vk.frames"
cp "$WORK/mtl.log" "$EVID/mtl.run.log" 2>/dev/null || true
cp "$WORK/vk.log"  "$EVID/vk.run.log"  2>/dev/null || true

# --- summary -------------------------------------------------------------------
if [ "$FAIL" -eq 0 ]; then
  note "ALL GATES GREEN — MTL and VK simulation state bit-identical ($N frames)"
else
  note "ONE OR MORE GATES RED — see lines above"
fi
exit "$FAIL"
