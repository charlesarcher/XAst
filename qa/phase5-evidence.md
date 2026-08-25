# Phase 5 evidence — task 46 (GL/VK edge cases + long-session stability + leak gates)

Executed 2026-08-25 against HEAD `fb286ef` (rows 43+44b). Verification-only
row: **no product source changed.** Scratch runs under `/tmp/opencode/t46/`
(harness work dirs under `/tmp/xast-harness.*`, kept via `--keep`); durable
artifacts in `qa/phase5-evidence/`. No commits (orchestrator owns git).

## Changed files (QA infrastructure only)

| File | Change |
|---|---|
| `qa/gen-masks.py` | adds `--pad N` (default 0 = byte-reproduces the task-36 masks): a uniform geometry-derived safety margin grown around every DRAW-derived rect at raster time. Text blocks are NOT padded (they carry their own ±2 and padding them blew the 50% honest-gate cap on table screens). Motivation: see "Q10 regime regeneration" below — recorded draw bboxes under-cover rendered rotated-pixmap extent by 1–3 px. |
| `test/harness/harness.C` | the scripted `resize W H` action now (a) re-queries the client geometry after `XResizeWindow`+`XSync` (stale cached dims made the very next `XGetImage` fail BadMatch error 8 / request 73 — this action's first live use ever), and (b) clamps the window position back on-screen accounting for the border footprint (`XGetImage` on a window BadMatches unless the whole rect lies within the root bounds; at +285 a 1000-wide window left the 1280px screen). |
| new: `test/harness/scripts/edge-base.script` | task-46 edge-flow BASE schedule: Q9 screens full (initial help, redrawn help, play start, engagements, 'n'-reset → hi-score table → restart, coast to natural game-over table) + natural 'q'. |
| new: `test/harness/scripts/edge-flows.script` | the SAME schedule plus six leave/enter disturbance pairs inserted only in input-free windows (title, mid-play ×3, hi-score table, death table). |
| new: `test/harness/scripts/resize-sweep.script` | session.script verbatim + five resizes (1000x800, 900x700, 500x400, 700x900, back to 688x702) in input-free windows with per-size captures. |
| new: `test/harness/scripts/soak.script` | sustained-gameplay soak: 8 engagement cycles (~1050 frames each: rotate/thrust/fire volleys + hyperspace), each closed by 'n' reset + 's' restart so 3 lives re-arm BEFORE death — simulation never settles on a static screen mid-soak; final cycle coasts to natural game-over. |
| new: `qa/xevt.c`, `qa/closepath.sh`, `qa/expose-probe.sh` | close-path/expose tooling (capture-x11.sh xhelper pattern): WM_DELETE_WINDOW ClientMessage delivery, XClearArea exposure damage, XTest keys/motion, window discovery. |

## Build/flavor discipline

Fresh O3 builds of all three legs + harness before any gate (`rm -rf obj/<leg>`
or `rm -f XAsteroids` on every flavor switch); flavor verified per run via
`ldd XAsteroids | grep -c glfw` (0=X11, 1=GPU) and `grep -c vulkan` for VK.
Pixel gates only at 1-min load < 2; `pgrep -fa XAsteroids` clean before runs.
After the ASan phase all three legs were rebuilt O3 from clean object dirs
(`nm XAsteroids | grep -c __asan` = 0) and the canonical Q13 receipt was
re-run on the restored binary: **RESULT: PASS, 13/13 AE=0**.

## Canonical references (frame handshake, seed 12345, session.script)

| leg | gate | result |
|---|---|---|
| X11 | quiescence vs `qa/baseline-x11/session` | PASS 13/13 AE=0 |
| GL | frame mode vs baseline-x11, masks-q10, crops 24,175,640,512 / 40,179,640,512 | PASS 13/13 AE=0 |
| VK | frame mode vs fresh GL ref, `qa/phase4-evidence/masks`, crop both 24,175,640,512 | PASS 13/13 AE=0 |

State-hash streams identical across ALL THREE legs, 435/435 frames
(`statehash/receipts.txt`) — matching the 45b/Q11 counts.

### Q10 regime regeneration (documented deviation)

The committed `qa/masks/` froze at task-36 rendering. Re-running the exact
Q10 comparison today scores gp1..gp5 residuals of 1–23 px — AND SO DOES the
37136bc-era GL binary (verified by worktree rebuild + rerun: old-vs-new GL
captures are byte-identical AE=0, trajectories byte-identical 435/435).
Root cause localized: recorded draw bboxes under-cover the RENDERED extent
of X11's pre-rasterized ROTATED rock pixmaps by 1–3 px (residual pixels hug
rect edges; e.g. logical x=524 vs rect start 526). The original Q10 green
therefore had zero reproducibility margin. Disposition: regime maintenance,
not product change — masks regenerated from CURRENT dumps of BOTH legs via
the committed tool (`--pad 5`), coverage honest-gate still green (max
45.96%, table screens unchanged). Artifacts: `masks-q10/` (+ gen.log).
Simulation identity is independently proven by the statehash receipts above,
which no mask can influence.

## 1. Edge-case flows (all three legs) — GREEN

Method: `edge-base.script` vs `edge-flows.script` (identical input/capture
schedule; the flows script adds leave/enter pairs in input-free windows),
frame handshake, capture-only, per leg.

Receipt (`statehash/edge-flows-receipt.txt`):

```
leg=x11 frames=955 statehash=IDENTICAL capture_diffs: NONE (all AE=0)
leg=gl  frames=955 statehash=IDENTICAL capture_diffs: NONE (all AE=0)
leg=vk  frames=955 statehash=IDENTICAL capture_diffs: NONE (all AE=0)
```

Focus loss/gain causes NO state loss on any leg; every game exits rc=0 via
natural 'q'; Q9 screens exercised in full (help initial/redrawn, shipyard
play starts, 'n' reset → hi-score table → restart, natural-death table).

Direct pause-engagement probe (`statehash/pause-probe-summary.txt`; pointer
warped out/in around a running game, frame publisher sampled):

```
x11 pause_frozen=YES (100->100) resume_advanced=YES (100->196)
gl  pause_frozen=NO  (100->196) resume_advanced=YES (196->292)
vk  pause_frozen=NO  (100->196) resume_advanced=YES (196->292)
```

**⚠ DIVERGENCE FOUND (flagged for orchestrator adjudication — NOT fixed;
fixing it is a product change):** X11 pauses simulation on LeaveNotify
(the backend pause-spin, original game semantics, task 25); the GL/VK D16
pollEvents machines deliver CursorLeave as a GameEvent that the domain's
non-X11 RunGame drain ignores (`default: break`) — GPU legs keep simulating
with the pointer outside the window. Invisible to all prior gates because
no earlier script leaves mid-play. Row-46 assertions are unaffected (no
state loss anywhere; frame clock trivially keeps publishing), but the
cross-leg semantic difference is real and now measured.

Expose handling: `qa/expose-probe.sh` inflicts REAL whole-window exposure
damage (XClearArea exposures=True) on the title/help screen and asserts
content restoration:

```
leg=x11 window=720x706 AE=0 (0)     # stage.Refresh() redraw path
leg=gl  window=688x702 AE=0 (0)     # D8 redraw-every-frame republish
leg=vk  window=688x702 AE=0 (0)
```

(`expose/expose-summary.txt`, sample before/after PNGs for X11.)

## 2. Q15 scripted-resize sweep (all three legs) — GREEN

`resize-sweep.script`: session.script inputs verbatim + resizes to
1000x800, 900x700, 500x400, 700x900, then back to canonical 688x702, with
per-size captures. All three legs: 18 checkpoints captured, game rc=0 via
natural 'q', **state-hash streams IDENTICAL to that leg's canonical
session.script run (435/435)** — simulation never sees the window size
(S2 retention constraint) (`statehash/resize-sweep-receipt.txt`). Frames
kept publishing through every resize including VK swapchain re-bootstraps.

Letterbox presentation measurements (`statehash/letterbox-measurements.txt`):

- **VK**: presented rect matches uniform-scale-centered math EXACTLY
  (784x800+108+0 at 1000x800; 700x714+0+93 at 700x900; bars black ≤0.001
  mean). Small bright spills beyond the rect at some sizes are the
  window-space HUD chrome, which draws AFTER the scaled content unscaled —
  exactly the D17.4e design. **VK is D17.4b-compliant.**
- **GL**: presentation stays 1:1 anchored top-left through resizes
  (canonical-size content at +0+0 in larger windows; symmetric crop when
  smaller) — the letterbox scale/offset is computed (getPresentTransform)
  but does not reach the effective present path. No crash, continuous
  rendering, simulation unaffected. **Flagged divergence** (product change
  required to fix; out of scope for a verification row).
- **X11**: fixed-dst 1:1 blits by construction (endFrame replays Stage's
  published dst coords; no re-centering/scaling) — content pinned, black
  margins right/bottom when larger, crop when smaller. Matches its code;
  noted as the same D17.4b gap as GL, on the leg where the plan itself
  defines the fallback semantics.

Two harness defects had to be fixed before the sweep could run at all (see
Changed files — BadMatch from stale dims, BadMatch from off-screen window);
both were hit on this action's first live use.

## 3. Long-session leak/drift gates (GL + VK) — GREEN

15-minute sustained-gameplay soak per leg (`soak.script`: 8 engagement
cycles with 'n'/'s' restarts so lives re-arm before death; final natural
game-over). RSS sampled every 15 s from `/proc/<pid>/status VmRSS`;
`soak/soak-{gl,vk}-rss.csv`, summaries in `soak/soak-summaries.txt`.

| leg | wall duration | boundaries | gameplay publishes | RSS steady-state growth (120 s → end) | raw growth (15 s → end) |
|---|---|---|---|---|---|
| GL | 903 s (15.05 min) | 9420 | 5546 frames | **+0.08%** (133564→133672 kB) | +5.38% (warm-up only, first ~min) |
| VK | 900 s (15.00 min) | 9420 | 5546 frames | **+0.02%** (123804→123824 kB) | +0.02% |

Both far under the 5% gate. The RSS curve is flat across gameplay AND
reset/table transitions (full curve in the CSVs). GPU memory where
obtainable: NVIDIA per-process `nvidia-smi --query-compute-apps` sampled
across a 2400-frame VK gameplay window — flat 45 MiB start-to-end
(`soak/soak-vk-gpumem.csv`); llvmpipe (GL) has no separate GPU memory.

**No-drift proof**, launched immediately after each soak finished:

- GL replay vs committed baseline-x11 under masks-q10: **PASS 13/13 AE=0**
- VK replay vs fresh GL ref under phase4 masks: **PASS 13/13 AE=0**
- State hashes byte-identical pre-soak vs post-soak on both legs, 435/435
  (`statehash/receipts.txt`, `manifests/postsoak-replays.txt`)

## 4. ASan gates (all three legs) — GREEN

Recipe per dispatch: `make BACKEND=<B> -B CXXFLAGS="-I/usr/include/X11 -O1
-g -fsanitize=address -fno-omit-frame-pointer -Wall -Wextra
-Wno-unused-parameter -std=c++17" LDFLAGS="-L/usr/lib/X11
-fsanitize=address"`; `nm XAsteroids | grep -c __asan` verified > 0 before
trusting results (33/37/37); natural-'q' exit only; validation layers OFF.

| leg | session | leaks | memory-safety errors |
|---|---|---|---|
| X11 | PASS 13/13 AE=0 (quiescence vs baseline) | EXACTLY the documented pre-existing baseline: **410 B / 19 allocs** (XtCalloc 64/4, XtMalloc 32/2, XtRealloc indirects, XStringListToTextProperty 10/1) — untouched per instructions; game rc=1 = LSan leaks-found code, expected | 0 |
| GL | PASS 13/13 AE=0 (masked Q10 regime) | **zero** — game rc=0, LSan produced no report file at all | 0 |
| VK | PASS 13/13 AE=0 (masked Q11 regime) | 53776 B / 816 allocs, **100% loader/driver noise**: 52534 B/806 allocs `<unknown module>` (NVIDIA blob), 1018 B libdbus, 224 B libxcb; ZERO frames from game code | 0 |

Reports: `asan/x11-session-lsan-report.txt`, `asan/vk-session-lsan-report.txt`,
`asan/gl-leg-zero-leaks.note`.

## 5. Close-path exactly-once (GL + VK, under ASan builds) — GREEN

`qa/closepath.sh` delivers the ICCCM WM_DELETE_WINDOW ClientMessage directly
(bare Xvfb has no WM). Three scenarios per leg, all under the ASan binary:
S1 single delete at title; S3 TWO deletes queued back-to-back (same-batch
latch exercise); S2 single delete mid-play (after XTest 's').

```
VK: s1_title rc=0 PASS | s3_double rc=0 PASS | s2_midplay rc=0 PASS   (reaped_once=yes ×3)
GL: s1_title rc=0 PASS | s3_double rc=0 PASS | s2_midplay rc=0 PASS   (reaped_once=yes ×3)
```

- Every scenario exits cleanly EXACTLY once (single reap, rc=0), zero
  double-free/UAF anywhere.
- GL ASan logs are 0 bytes across all scenarios (nothing to report).
- VK ASan logs show the byte-identical documented driver-noise baseline
  (53776 B / 816 allocs) in all three scenarios — deterministic teardown.
- S3 == S1 behavior proves the closeRequested_ latch collapses the second
  message (a delayed second send instead races teardown → BadWindow, which
  is why the sends are back-to-back; noted in the script).
Artifacts: `closepath/{gl,vk}/close-summary.txt` + per-scenario logs.

## Cleanup receipts

- No stray processes: `pgrep -x XAsteroids` / `pgrep -x Xvfb` empty at end
  (probe displays :95/:96/:97/:98 torn down by their scripts' traps).
- All three legs rebuilt O3 from clean object dirs after the ASan phase;
  `nm XAsteroids | grep -c __asan` = 0; canonical Q13 re-run PASS on the
  restored tree.
- Probe worktrees (`wt-59e5870`, `wt-q10`) removed via `git worktree remove`.
- `.omo/plans/rendering-abstraction.md` untouched; no commits made.
- Repo-root artifacts: none beyond the QA scripts listed above (`git status`
  shows only those + `obj/`, which is untracked build output).

## Risks / notes for the orchestrator

1. **Cross-leg semantic divergences found and measured, NOT fixed** (each
   needs a product change + its own full gate re-run):
   a. LeaveNotify pause: X11 pauses; GL/VK continue simulating.
   b. Letterbox presentation: only VK implements D17.4b scaled-centered
      presentation; GL renders 1:1 top-left (transform computed but not
      effectively applied); X11 pins fixed-dst 1:1 blits (its documented
      fallback, but without even the re-centering).
2. The Q10 mask regime needed `--pad 5` regeneration at HEAD (rationale and
   archaeology above). The committed qa/masks remain untouched; the new set
   lives in `qa/phase5-evidence/masks-q10/`. Any future GL-vs-X11 pixel gate
   must use it (and a MASKLESS copy of the baseline PNGs as --ref: the
   committed baseline dir contains pre-refactor 720x706-era masks that
   shadow --mask-dir because the harness resolves refDir first).
3. The harness `resize` action was never live before this row; it now works
   but captures race the resize if scheduled <2 boundaries after it (150 ms
   settle built into the action covers ConfigureNotify processing).
4. Soak wall-clock includes static-screen boundary ticking between restart
   cycles (~30–50 ms/tick); pure-simulation volume is the 5546 published
   frames per leg. The leak gate covers all phases either way.

## 6. Letterbox remediation (D17.4b) — glBackend + x11Backend — GREEN

Executed 2026-08-25 against the task-46 tree. Fixes the two Q15 divergences
recorded in section 2 above. Presentation-only: zero simulation/state-hash
behavior change (proven by 435/435 statehash identity on every leg below).

### Root causes + fixes

- **glBackend**: `getPresentTransform()` computed fresh scale/offsets into
  LOCALS but never wrote them back to `presentScale_/presentOffX_/
  presentOffY_`; `presentMVP()`'s lazy `presentDirty_` branch consumed fresh
  values exactly ONCE after a resize and every later draw silently reverted
  to stale identity members → measured 1:1 top-left presentation. Fix (VK
  mirror): `recomputePresentTransform_()` is now the ONE writer, called from
  the framebuffer-size callback AND at init; getPresentTransform/presentMVP/
  setScissorRect/mouse-enqueue are pure cache readers.
- **x11Backend**: endFrame replayed Stage's published dst coords verbatim at
  any window size. Fix: one-time present-path selection at init
  (`XAST_XRENDER=auto|on|off`, default auto = RENDER-extension probe via
  dlopen'd libXrender — no new link-time dependency), logged to stderr as
  `[x11] present path: xrender|fallback` and surfaced by the harness manifest
  (`present_path:` line). Both request-gated present legs (yard + field) and
  all window-target draws (title/score strings, button faces) map through the
  ACTIVE transform, recomputed on ConfigureNotify:
  - *xrender*: XRenderComposite with a picture transform (NEAREST filter set
    once per picture for pixel-edge fidelity). GOTCHA fixed live: the Render
    picture transform is the DESTINATION→SOURCE sampling matrix — a forward
    scale shrinks content by 1/s; the matrix carries 1/s.
  - *fallback*: centered 1:1 copy (negative offsets crop symmetrically).
  - Geometry change repaints the window base ONCE: windowBg gray everywhere
    first (the chrome model paints text over the PERSISTENT background — an
    all-black fill rendered the black title glyphs invisible), then black
    bars only OUTSIDE the mapped content rect.
  - Canonical size takes an identity fast path issuing the byte-identical
    legacy XCopyArea; the first cache computation never repaints.

### Gates (all re-run post-fix)

| gate | result |
|---|---|
| Q13 X11 canonical ×2 per path (auto/on/off) vs qa/baseline-x11/session | **6/6 PASS, 13/13 AE=0 each** (manifests record present_path xrender×4 / fallback×2) |
| Q10 GL masked (masks-q10 regime, frame handshake, crops 24,175,640,512 / 40,179,640,512) | **PASS 13/13 AE=0** |
| VK sanity (vksoak 600+200 frames, forced resize) | **PASS** — 1 re-bootstrap, extent==framebuffer, 0 validation errors |
| Resize sweep ×5 legs (x11 auto/on/off, GL, VK) | **5/5 rc=0**, 18 checkpoints each, **statehash IDENTICAL 435/435 vs that leg's canonical stream** |
| Q5 GL menu regression (qa/menu-gl-q5.sh) | **PASS 15/15 assertions** (period shift, pause, hash continuity, round-trips AE=0, prefs) |
| Warning baselines (clean obj dirs) | X11 **367** / GL **268** / VK **267** — all == baseline |

### Letterbox measurements (post-fix; same format as section 2)

`letterbox-fix/f-{gl,vk,x11-auto,x11-on,x11-off}.txt`. Highlights (expected
rect = uniform-scale-centered math replicated bit-exactly from backend float
arithmetic; bright_bbox = threshold-50% trim bbox):

- **GL**: bright_bbox == expected EXACTLY at every resized size
  (784x800+108+0 @1000x800; 392x400+54+0; 700x714+0+93; 686x700+107+0),
  bars mean 0. Now stricter than VK (whose known window-space HUD spills
  remain, unchanged from section 2).
- **X11 xrender (auto ≡ on)**: bbox == expected EXACTLY at 1000x800
  (815x800+92+0), 900x700, 700x900; +46+0 origin exact at 500x400 (single-
  pixel boundary class at the rect edge, same class as VK's documented
  spill); bars mean 0. auto-vs-on captures **18/18 pixel-identical**.
- **X11 fallback**: rsz@1000x800 bright_bbox == expected EXACTLY
  (720x706+140+47); static-title probe: cropped mapped rect vs canonical
  render **AE=0** (byte-exact centered 1:1); smaller windows crop
  symmetrically (720x700+90+0 @900x700 with oy=-3 clipped).
- Static-title probe artifacts (canonical / fallback-rsz / xrender-rsz /
  synthetics): `letterbox-fix/title-probe-*.png`, `syn*.png`.

### Environment notes

- A co-tenant compute job (`ff_sieve`, ~29/32 cores in 60s bursts)
  re-introduced the documented load-flake class mid-gating (T14 signature:
  start/gp4/gp6 world diffs, drifting AEs, help always exact). All gates
  above were (re-)run in verified quiet windows per the problems.md
  operational rule; flaked runs were discarded and re-run, not averaged.
