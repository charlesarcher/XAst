#!/usr/bin/env bash
# qa/env/env.sh — local-prefix build+QA environment for XAst (no root required).
# Source it:  source qa/env/env.sh
#
# Provides: Motif (Xm headers + libXm), libXp, Xvfb, xwd, xlsfonts, mkfontscale,
# bdftopcf, patchelf — all extracted under ~/.local/xast-env.
# Makes `make` work WITHOUT makefile changes:
#   - g++ finds Xm headers via CPATH
#   - linker finds libXm via LIBRARY_PATH
#   - runtime finds libXm/libjpeg62 via LD_LIBRARY_PATH

_XAST_ENV_ROOT="${_XAST_ENV_ROOT:-$HOME/.local/xast-env}"

if [ ! -d "$_XAST_ENV_ROOT" ]; then
    echo "env.sh: ERROR: $_XAST_ENV_ROOT does not exist" >&2
    return 1 2>/dev/null || exit 1
fi

export PATH="$_XAST_ENV_ROOT/bin:$PATH"

# Compiler header search (g++ picks this up like -I)
export CPATH="$_XAST_ENV_ROOT/include${CPATH:+:$CPATH}"

# Linker library search (gcc converts to -L). Both layouts are present because
# Arch packages use lib/ while Debian-extracted Motif uses lib/x86_64-linux-gnu/.
export LIBRARY_PATH="$_XAST_ENV_ROOT/lib:$_XAST_ENV_ROOT/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}"

# Runtime loader path for the same reason. Note: libXm.so.4 also carries a
# DT_RUNPATH of $ORIGIN (set with patchelf) so its private libjpeg.so.62 ABI
# dependency resolves even if LD_LIBRARY_PATH is stripped by a sandbox.
export LD_LIBRARY_PATH="$_XAST_ENV_ROOT/lib:$_XAST_ENV_ROOT/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# pkg-config for libxp etc., should anything query it
export PKG_CONFIG_PATH="$_XAST_ENV_ROOT/lib/pkgconfig:$_XAST_ENV_ROOT/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

# Headless QA defaults used by env-check.sh (not mandatory for building)
export XAST_ENV_ROOT="$_XAST_ENV_ROOT"
export XAST_REPO_ROOT="${XAST_REPO_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
export XAST_FONTS="$_XAST_ENV_ROOT/fonts"

unset _XAST_ENV_ROOT
echo "xast env: PATH/CPATH/LIBRARY_PATH/LD_LIBRARY_PATH wired to $XAST_ENV_ROOT (repo: $XAST_REPO_ROOT)"
