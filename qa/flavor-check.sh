#!/bin/sh
# flavor-check.sh <expected-backend> — prove the root ./XAsteroids on disk is
# the expected backend flavor (X11|GL|VK|MTL) before anything is executed
# from it. Prints `otool -L` evidence, then:
#   exit 0 = detected flavor == $1 (PASS)
#   exit 1 = mismatch, binary missing, or flavor undetermined (FAIL)
#   exit 2 = usage error
#
# Detection (priority order matters: a macOS VK binary ALSO links
# Metal.framework via MoltenVK, so libvulkan must be tested first):
#   libvulkan*             -> VK
#   Metal.framework        -> MTL
#   OpenGL.framework/libGL -> GL
#   libX11                 -> X11
#
# Reusable by the flavor A/B matrix (stale-binary trap regression guard):
#   make BACKEND=MTL && qa/flavor-check.sh MTL
#   make BACKEND=VK  && qa/flavor-check.sh VK
# Run from anywhere; the repo root is derived from this script's location
# (qa/..). Override the binary under test with XAST_FLAVOR_BIN.

set -u

EXPECTED=${1:-}
case $EXPECTED in
X11|GL|VK|MTL) ;;
*)
	echo "usage: $(basename "$0") <X11|GL|VK|MTL>" >&2
	exit 2
	;;
esac

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(dirname "$SCRIPT_DIR")
BIN=${XAST_FLAVOR_BIN:-$REPO_ROOT/XAsteroids}

if [ ! -f "$BIN" ]; then
	echo "flavor-check: FAIL — $BIN not found (no root binary on disk)"
	exit 1
fi

OT=$(otool -L "$BIN" 2>/dev/null) || {
	echo "flavor-check: FAIL — otool -L failed on $BIN"
	exit 1
}

DETECTED=unknown
case $OT in
*libvulkan*) DETECTED=VK ;;
*)
	case $OT in
	*"/System/Library/Frameworks/Metal.framework"*|*"/usr/lib/libMetal"*)
		DETECTED=MTL ;;
	*)
		case $OT in
		*OpenGL.framework*|*libGL*|*libOpenGL*)
			DETECTED=GL ;;
		*)
			case $OT in
			*libX11*) DETECTED=X11 ;;
			esac
			;;
		esac
		;;
	esac
	;;
esac

echo "--- otool -L $BIN (evidence) ---"
printf '%s\n' "$OT"
echo "------------------------------------"
echo "flavor-check: expected=$EXPECTED detected=$DETECTED"

if [ "$DETECTED" = "$EXPECTED" ]; then
	echo "flavor-check: PASS (binary is $DETECTED flavor)"
	exit 0
fi
echo "flavor-check: FAIL (expected $EXPECTED, found $DETECTED — stale/foreign-flavor binary)"
exit 1