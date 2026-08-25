#!/usr/bin/env bash
# qa/vk-probe.sh — Vulkan-leg environment gate + machine probe (task 37, U15/m15).
#
# Two modes:
#   qa/vk-probe.sh --link-check   Loader-presence ONLY. Runs as the first recipe
#                                 line of BOTH VK link rules ($(VK_LOADER_GATE) in
#                                 the makefile): a machine without libvulkan must
#                                 abort `make BACKEND=VK` BEFORE any link line.
#                                 Deliberately does NOT depend on vulkaninfo or the
#                                 compiled probe exe — that would gate the probe's
#                                 own build on itself.
#   qa/vk-probe.sh                Full probe: loader check, then `vulkaninfo
#                                 --summary` when present, else falls back to the
#                                 compiled probe (obj/VK/vkprobe) for instance/
#                                 device/layer facts.
#
# GOTCHA (fixed during task-37 testing): the --link-check block MUST sit AFTER
# fail()'s definition — the loader-absent path calls fail() and would die with
# "fail: command not found" if this ordering is ever inverted.

set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

fail()
 {
  echo "vk-probe: FAIL: $*" >&2
  exit 1
}

# ---------------------------------------------------------------------------
# Loader check (shared by both modes). Two independent probes:
#   1. ldconfig -p        — the canonical cache lookup
#   2. /usr/lib glob      — covers cache-less setups (and is what dlopen would
#                           hit first via the default search path)
# ---------------------------------------------------------------------------
loader_present()
 {
  ldconfig -p 2>/dev/null | grep -q 'libvulkan\.so\.1' && return 0
  ls /usr/lib/libvulkan.so.1 >/dev/null 2>&1 && return 0
  return 1
}

LINK_CHECK=0
if [ "${1:-}" = "--link-check" ]; then
  LINK_CHECK=1
fi

if ! loader_present; then
  fail "libvulkan.so.1 not found (ldconfig -p and /usr/lib glob). Install vulkan-icd-loader (Arch) — VK leg cannot link."
fi

if [ "$LINK_CHECK" = "1" ]; then
  echo "vk-probe: link-check OK: libvulkan.so.1 present"
  exit 0
fi

# ---------------------------------------------------------------------------
# Full probe mode
# ---------------------------------------------------------------------------
echo "== vk-probe: full run $(date -u '+%Y-%m-%dT%H:%M:%SZ') =="

LOADER_PKG="$(pacman -Q vulkan-icd-loader 2>/dev/null || echo 'vulkan-icd-loader: unknown')"
echo "loader: $LOADER_PKG"
ldconfig -p | grep 'libvulkan\.so\.1' || true

VKPROBE="$REPO_ROOT/obj/VK/vkprobe"

if command -v vulkaninfo >/dev/null 2>&1; then
  echo "-- vulkaninfo --summary --"
  vulkaninfo --summary 2>&1
  RC=$?
  echo "vulkaninfo rc=$RC"
  # vulkaninfo summarizing devices is the primary evidence; the compiled probe
  # (when built) still runs below for instance/layer-level facts, but its
  # failure alone does not fail the script when vulkaninfo already succeeded.
  if [ $RC -eq 0 ] && [ ! -x "$VKPROBE" ]; then
    echo "vk-probe: PASS (vulkaninfo path; compiled probe not built)"
    exit 0
  fi
else
  echo "vulkaninfo: NOT PRESENT — falling back to compiled probe"
fi

if [ ! -x "$VKPROBE" ]; then
  fail "no vulkaninfo AND no compiled probe at obj/VK/vkprobe (make BACKEND=VK vkprobe)"
fi

echo "-- compiled probe (obj/VK/vkprobe) --"
"$VKPROBE"
RC=$?
echo "compiled probe rc=$RC"
[ $RC -eq 0 ] || fail "compiled probe reported FAIL (rc=$RC)"

echo "vk-probe: PASS"
exit 0
