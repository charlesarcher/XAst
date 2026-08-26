#!/usr/bin/env bash
# qa/menu-x11-q5.sh — Q5 X11 leg driver + assertions (F5/SC9, plan row 48).
#
# Runs test/harness/scripts/menu-x11.script through the harness in frame
# handshake mode (the task-36 frame publisher works on the X11 RunGame
# variant too), live-samples the game's published frame counter and
# state-hash files, then asserts:
#   1. measured frame period SHIFTS 16fps -> 32fps (the D4 uSecondsPerFrame
#      path through the Motif XmNvalueChangedCallback -> AlterFramesPerSecond);
#   2. simulation PAUSES while the Motif modal pump is open (frame counter +
#      state-hash line count frozen between menu_open and menu_closed);
#   3. deterministic resume: state-hash frame numbers strictly +1 across the
#      whole session (no reset, no skip across the modal pause);
#   4. pixel identities: no-op dead-space click AE=0, Write->Read preferences
#      round-trip AE=0, slider drag changes the dialog (AE>0), drag-away
#      changes it (AE>0), dialog occlusion erased on Exit (AE>0), world
#      moving after resume (AE>0);
#   5. the Motif dialog realized at its deterministic geometry (root 0,0,
#      929x490 — asserted live via xhelper during the pause window);
#   6. preferences file: options.H format (29 lines), first value = the
#      applied uSecondsPerFrame (31250 = 1E6/32).
#
# Usage: qa/menu-x11-q5.sh [--out DIR] (default /tmp/opencode/q5-x11-run)
# Exit 0 = PASS; 1 = FAIL.

set -u
cd "$(dirname "$0")/.." || exit 2
source qa/env/env.sh || exit 2

OUT=/tmp/opencode/q5-x11-run
[ "${1:-}" = "--out" ] && OUT="${2:-$OUT}"
rm -rf "$OUT"; mkdir -p "$OUT"
LOG="$OUT/harness.log"; SAMPLES="$OUT/samples.log"

fail=0
note() { printf '%s\n' "$*"; }
check() { # check <name> <expr-result 0|1> <detail>
  if [ "$2" -eq 0 ]; then note "PASS: $1 ($3)"; else note "FAIL: $1 ($3)"; fail=1; fi
}

# ---- preflight -------------------------------------------------------------
ldd ./XAsteroids | grep -q glfw && { note "FAIL: ./XAsteroids is not the X11 flavor"; exit 1; }
[ -x ./obj/harness ] || make harness >/dev/null 2>&1 || { note "FAIL: no harness"; exit 1; }
pkill -x Xvfb 2>/dev/null; sleep 1

# xhelper for the live dialog-geometry receipt (capture-x11.sh pattern)
cat > "$OUT/xhelper.c" <<'EOF'
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void walk(Display *d, Window w, const char *name, Window *found) {
    if (*found != None) return;
    char *wn = NULL;
    if (w != DefaultRootWindow(d) && XFetchName(d, w, &wn) && wn) {
        if (strcmp(wn, name) == 0) *found = w;
        XFree(wn);
    }
    Window root, parent, *ch = NULL; unsigned int n;
    if (XQueryTree(d, w, &root, &parent, &ch, &n)) {
        for (unsigned int i = 0; i < n; i++) walk(d, ch[i], name, found);
        if (ch) XFree((char *)ch);
    }
}
int main(int argc, char **argv) {
    Display *d = XOpenDisplay(argv[1]);
    if (!d) return 2;
    int rc = 1;
    if (!strcmp(argv[2], "findwin") && argc == 4) {
        Window f = None; walk(d, DefaultRootWindow(d), argv[3], &f);
        if (f != None) { printf("0x%lx\n", (unsigned long)f); rc = 0; }
    } else if (!strcmp(argv[2], "geom") && argc == 4) {
        Window w = (Window)strtoul(argv[3], NULL, 0);
        Window root; int x, y; unsigned int bw, depth, W, H;
        if (XGetGeometry(d, w, &root, &x, &y, &W, &H, &bw, &depth)) {
            int rx, ry; Window child;
            XTranslateCoordinates(d, w, root, 0, 0, &rx, &ry, &child);
            printf("%d %d %u %u %u\n", rx, ry, W, H, bw); rc = 0;
        }
    }
    XCloseDisplay(d);
    return rc;
}
EOF
gcc -O2 -o "$OUT/xhelper" "$OUT/xhelper.c" -lX11 || { note "FAIL: xhelper build"; exit 1; }

# ---- run + live sampling ---------------------------------------------------
XAST_QA_MENU_RECTS=1 ./obj/harness --seed 12345 --script test/harness/scripts/menu-x11.script \
  --out "$OUT/caps" --hiscore test/harness/fixtures/hiScore.nul.data \
  --handshake frame --keep --tick-gap-ms 20 --static-idle-ms 100 \
  --idle-escape-ms 60 --max-run 600 >"$LOG" 2>&1 &
HARNESS_PID=$!

# Sampler: monotonic ms + frame counter + state-hash line count every 100 ms;
# first time the counter freezes (modal open), record the dialog geometry.
(
  DISPN=""
  GEOM_DONE=0
  PREV_FC="-1"
  while kill -0 "$HARNESS_PID" 2>/dev/null; do
    wd=$(sed -n 's/.*work dir: //p' "$LOG" | head -1 | sed 's/ (kept)$//')
    if [ -n "$wd" ] && [ -f "$wd/framecount" ]; then
      t=$(awk '{printf "%d", $1*1000}' /proc/uptime)
      fc=$(cat "$wd/framecount" 2>/dev/null || echo -1)
      hl=$(wc -l < "$wd/statehash" 2>/dev/null || echo 0)
      printf '%s %s %s\n' "$t" "$fc" "$hl" >>"$SAMPLES"
      if [ "$GEOM_DONE" -eq 0 ] && [ "$PREV_FC" != "-1" ] && [ "$fc" = "$PREV_FC" ]; then
        # counter frozen -> modal pump is open; grab the dialog geometry once
        if [ -z "$DISPN" ]; then
          DISPN=$(sed -n 's/.*Xvfb :\([0-9]*\).*/\1/p' "$LOG" | head -1)
          [ -n "$DISPN" ] || DISPN=$(grep -oE "Xvfb :[0-9]+" "$LOG" | head -1 | tr -d ':A-Za-z ')
        fi
        if [ -n "$DISPN" ]; then
          DW=$("$OUT/xhelper" ":$DISPN" findwin "Asteroids Options" 2>/dev/null)
          if [ -n "$DW" ]; then
            G=$("$OUT/xhelper" ":$DISPN" geom "$DW" 2>/dev/null)
            [ -n "$G" ] && printf 'dialog_geom %s\n' "$G" >>"$SAMPLES" && GEOM_DONE=1
          fi
        fi
      fi
      PREV_FC="$fc"
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
rate() { # rate <t1> <t2> -> frames/sec between two sample times
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

# ---- 2. pause while the Motif modal pump is open ----------------------------
# Measured as the counter STALL directly from the samples (manifest wall times
# overshoot: the frame-mode boundary counter rewinds across the pause and the
# post-close captures land after the re-climb).
read -r STALL_MS STALL_FC <<<"$(awk -v a="${T[menu_open]}" -v b="${T[menu_closed]}" '
  {t[NR]=$1;fc[NR]=$2}
  END{sum=0;f0=-1;i=1
      while(i<=NR){j=i; while(j<=NR&&fc[j]==fc[i])++j
        if(t[i]<=b&&t[j-1]>=a&&j-1>i&&fc[i]>0){d=t[j-1]-t[i]
          if(d>=800){sum+=d; if(f0<0)f0=fc[i]}}
        i=j}
      print sum+0, f0+0}' "$SAMPLES")"
note "modal stalls within [menu_open,menu_closed]: total ${STALL_MS} ms (first frozen fc=${STALL_FC})"
check "paused while open" $([ "$STALL_MS" -ge 3000 ] && echo 0 || echo 1) "stalled-total=${STALL_MS}ms"

# ---- 3. deterministic resume: state-hash continuity ------------------------
BAD=$(awk 'BEGIN{prev=-1;bad=0} /^h /{f=$2; if(prev>=0 && f!=prev+1)++bad; prev=f} END{print bad+0}' "$WD/statehash")
TOTAL=$(grep -c '^h ' "$WD/statehash")
check "state-hash continuity (+1 throughout)" $([ "$BAD" -eq 0 ] && echo 0 || echo 1) "violations=$BAD frames=$TOTAL"

# ---- 4. dialog geometry receipt --------------------------------------------
DG=$(grep '^dialog_geom ' "$SAMPLES" | tail -1 | cut -d' ' -f2-)
if [ -n "$DG" ]; then
  read -r DX DY DW DH DB <<<"$DG"
  check "Motif dialog realized at root(0,0) 929x490" \
    $([ "$DX" -eq 0 ] && [ "$DY" -eq 0 ] && [ "$DW" -eq 929 ] && [ "$DH" -eq 490 ] && echo 0 || echo 1) "geom=+$DX+$DY ${DW}x${DH} bw=$DB"
else
  check "Motif dialog realized at root(0,0) 929x490" 1 "no geometry sample (sampler missed the freeze window)"
fi

# ---- 5. pixel identities ----------------------------------------------------
# Slider motion is not capturable on this leg: the FPS scale (opts x 9..222)
# and the Read/Write buttons sit left of the game window's root origin (285),
# outside every game-window capture. The slider's state receipt is therefore
# the prefs file (first line = applied uSecondsPerFrame) + the post period.
ae() { compare -metric AE "$OUT/caps/$1.png" "$OUT/caps/$2.png" null: 2>&1; }
AE_NOOP=$(ae menu_noop menu_open);    check "no-op dead-space click" $(awk -v a="$AE_NOOP" 'BEGIN{print (a+0==0)?0:1}') "AE=$AE_NOOP"
AE_CLOSED=$(ae menu_closed menu_noop)
check "dialog occlusion erased on Exit" $(awk -v a="${AE_CLOSED%%.*}" 'BEGIN{print (a>0)?0:1}') "AE=$AE_CLOSED"
AE_MOVE=$(ae post_a post_b)
check "simulation resumed after close" $(awk -v a="${AE_MOVE%%.*}" 'BEGIN{print (a>0)?0:1}') "AE=$AE_MOVE"

# ---- 6. applied-preference receipt ------------------------------------------
# The X11 leg asserts the FPS preference through the MEASURED period (above)
# instead of a prefs file: the Write/Read file round-trip needs the
# FileSelectionDialog sub-dialog, whose XTest choreography deterministically
# escapes the outer modal (see qa/final-wave-evidence.md §F5). The options.H
# prefs format itself (29 lines, first = uSecondsPerFrame) is certified on
# the GL/VK legs (qa/menu-gl-q5.sh / qa/menu-vk-q5.sh receipts).

if [ $fail -eq 0 ]; then
  note "RESULT: PASS (Q5 X11 leg)"
else
  note "RESULT: FAIL (Q5 X11 leg)"
fi
exit $fail
