# VERDICT — XAst MTL black window (round 3, confound-free probe matrix)

Date: 2026-09-03 · Machine: macOS 26.6.2 (25G83), Apple M2 Pro · GLFW 3.5.1 (homebrew dylib)

## Decision-table cell (from the task's matrix)

**A nil-forever, B nil-forever → environment blocker confirmed for BOTH the AppKit
and the GLFW path on this macOS build. C (GL control) was unavailable on this
machine (see below).**

Per the matrix: product rendering code was NOT modified; only the Lane-2 pacing
fix was shipped (commit `fix(metal): pace MTL frame loop (no 870 Hz
nil-drawable spin)`).

## Probe matrix results

### Probe A — pure AppKit CLI, NO GLFW (probeA2_appkit.mm → probeA2.log)
- `NSApplication` + `finishLaunching` awaited (2 s run-loop settle; boot line
  logs `isRunning`/`isFinishedLaunching`), 640x512 titled `NSWindow`
  `makeKeyAndOrderFront` on the main screen, `CAMetalLayer` on the contentView
  (BGRA8Unorm, `contentsScale` = the window's OWN `backingScaleFactor`,
  `displaySyncEnabled=YES`), real `MTLDevice` (Apple M2 Pro) + command queue,
  0.5 s repeating run-loop timer, 30 s bound, self-terminating.
- On-screen ground truth from the WINDOW SERVER itself:
  `CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly)` — AppKit's
  `isOnScreen` is removed on this build (returns `n/a`), so the probe used the
  CG on-screen list, which is immune to both that removal and to TCC
  screen-capture restrictions.
- Result: **`probeA2 done: t=30.4s samples=120 firstNonNilTick=-1
  nonNilDrawables=0 everWsOnScreen=1`** — `wsOnScreen=yes` on ALL 120 ticks
  (the window is on the window server's on-screen list the whole time),
  `superlayer=set`, `bounds=(640,512)`, `ds=(1280,1024)`, `cs=2.0`, main
  thread, run loop pumping — and `nextDrawable=NIL` on every tick for 30 s.
  Exit code 1.
- (Superseded earlier run: probeA.log — same outcome; it had the CPU-clock bug
  and lacked the CG on-screen check, kept for the record.)

### Probe B — GLFW 3.5.1 NO_API + the EXACT mtlCocoa.mm attach (probeB_glfw.mm → probeB.log)
- Line-for-line mirror of `mtlCocoa.mm` (`[CAMetalLayer layer]`,
  BGRA8Unorm, `contentsScale` from mainScreen, `view.layer=layer; then
  view.wantsLayer=YES`, `drawableSize = fb × window-backing-scale` via the
  same trampoline-mirror callback, `displaySyncEnabled=YES`), plus the same
  0.5 s timer / 30 s / present / exit-code contract as A.
- Result: **`probeB done: t=30.5s samples=120 firstNonNilTick=-1
  nonNilDrawables=0 everWsOnScreen=1`** — `wsOnScreen=yes` on all 120 ticks,
  the GLFW window even became key window (`isKey=yes` early ticks),
  `fb=1280x1024`, `ds=(2560,2048)`, `superlayer=set` — `nextDrawable=NIL`
  on every tick for 30 s. Exit code 1.
- The failure reproduces with the EXACT product attach sequence — and,
  decisively, ALSO without any GLFW (Probe A), so the common factor is the
  process/session/OS, not the attach code or the GLFW version.

### Probe C — GL control (probeC-gl-run.log) — UNAVAILABLE on this machine
- `make BACKEND=GL` + `qa/flavor-check.sh GL` PASS, then `gtimeout 25
  ./XAsteroids`: **`glBackend: GL_VERSION=2.1 ... OpenGL 4.5 core required,
  got 2.1.` → "XAsteroids: initialization failed."** (exit 1, no window).
- Apple's deprecated OpenGL stack on this macOS caps below the GL leg's 4.5
  requirement, so a CLI-GLFW-GL control cannot be produced here (this is why
  the macOS GPU legs of this project are VK/MTL). Recorded as
  control-unavailable; it neither confirms nor refutes the MTL finding.

## What this proves (and what it does not)

- CONFIRMED: on this macOS 26.6.2 build, a command-line (non-.app-bundle)
  process whose window IS on-screen (per the window server's own on-screen
  list) cannot obtain a single `CAMetalLayer` drawable for 30 s, with a
  correctly configured layer (tree, bounds, drawableSize, contentsScale,
  displaySync) and a healthy GPU (M2 Pro; offscreen golden
  `mtlmethods: PASS`, 1,310,856 bytes).
- The prior session's confounds are resolved: the window's on-screen state was
  read every tick from the window server (not the removed AppKit selector),
  `finishLaunching` was awaited, and the timeline uses wall-clock time.
- NOT provable from here: the exact OS subsystem that withholds drawables
  (window-server session policy vs. this build's altered AppKit — note the
  anomalies also observed: `runForTimeInterval:` unrecognized,
  `kCFRunLoopCommonModes` "invalid mode", `isRunning=no` after
  `finishLaunching`, `isOnScreen` removed from NSWindow). A standard,
  unmodified macOS install would be the control for that question.

## User-facing implication + the user's call

On this machine the MTL window will remain black no matter what the product
code does. If on-screen MTL is required HERE, the options are all outside
this repo's MTL code:
1. **Package XAsteroids as a `.app` bundle** (Info.plist + launch via
   `open`/LaunchServices) — the standard way to become a foreground GUI app;
   a makefile/packaging change, to be authorized as a separate task.
2. **Run on a standard macOS install / standard interactive GUI session** and
   re-run this probe matrix — Probe A is the 40-line regression test.
3. Use the **VK (MoltenVK) macOS leg** for on-screen GPU rendering (its
   separate macOS surface issue is a different lane, per the task brief).

## Lane 2 — pacing fix (shipped, independent of Lane 1)

- Defect: the non-X11 title-screen outer loop (playingField.H ~1444) ended in
  `if (!numEvents) usleep(1000);` with no `uSecondsPerFrame` pacing (that
  mechanism exists only in the in-game loop), so a nil-drawable MTL run
  busy-spun at ~870 Hz (65,309 iterations / 75 s measured pre-fix).
- Fix (MTL-only, `#ifdef METAL_BACKEND`; GL/VK/X11 branches byte-identical,
  makefile untouched): stamp `startTime` at the loop head and enforce
  `uSecondsPerFrame` at the tail with the same
  `diffTime/usleep(uSecondsPerFrame-diffTime)` mechanism as the in-game loop.
- Proof (final MTL binary): `gtimeout 15 ./XAsteroids` → rc 124 (alive);
  **226 nil-drawable lines / 15 s = 15.07 fps** (nominal 16 fps at
  `uSecondsPerFrame=62500`; no `XAsteroids.prefs` in the run cwd, so the
  default 16 fps applies — see reconciliation note below); CPU **0.0 %**
  (top, t≈7 s) / 0.26 s total CPU time over 12 s of wall (ps); SIGTERM →
  clean immediate exit (rc 143, pgrep empty).
- Reconciliation: the task's literal window of [12,26] lines/15 s corresponds
  to ~1 fps, which would require a prefs file setting FPS=1; none exists in
  the run cwd, and the task's primary spec is "≈16 fps cadence" via the
  `uSecondsPerFrame` mechanism — measured 15.07–15.3 fps is within 16 ± 2 fps.
- Regressions: `make BACKEND=MTL mtlmethods` → `mtlmethods: PASS` (exit 0,
  1,310,856-byte golden); flavor round-trip MTL→VK→MTL green (VK link-only,
  VK binary NOT run, per task); `make BACKEND=GL` links; makefile diff = 0
  lines (link lines byte-identical).

## Secondary finding (pre-existing, out of scope, reported only)

`/usr/bin/make` is **GNU Make 3.81** (1-second mtime granularity, no
sub-second support). The flavor-stamp recipe (commit d27c0494) does
`echo $(BACKEND) > obj/.backend && rm -f XAsteroids`; when a flavor-switching
`make` invocation lands its stamp rewrite in the same filesystem second as
the mtime make recorded, make concludes nothing changed and **silently skips
the relink** (rc=0, no root binary, zero output). Reproduced 5/5 in a tight
VK→MTL loop (see m2-debug.log / loop1..5 logs); a plain re-run of the same
`make BACKEND=MTL` always recovers (missing target forces the rebuild).
Mitigations without touching the makefile: repeat the make, or space
flavor-switching builds ≥1 s apart (or build with a modern make ≥4.x).
A makefile-level fix is a separate task (this task forbids makefile changes).

## File index (this directory)

- probeA2_appkit.mm / probeA2.log — Lane-1 Probe A (decisive; on-screen via CG)
- probeA_appkit.mm / probeA.log / probeA-rerun.log — Probe A v1 (superseded)
- probeB_glfw.mm / probeB.log — Lane-1 Probe B (GLFW, exact mtlCocoa attach)
- probeC-gl-run.log / probeC-flavor.txt / probeC-gl-mid-full.png /
  probeC-gl-mid-win.png — Lane-1 Probe C (GL control; init failure recorded)
- mtldrawprobe.m / strategy-experiment.log, mtlholdprobe.m / holdprobe.log,
  mtlfinalprobe.m / final-glfw-primary.log + final-plain-appkit.log,
  bootstrap-test.log — prior-session localization probes (kept per task rule)
- pace-run15.log / final-prove15.log — pre/post pacing-proof 15 s runs
- sigterm-probe.log — SIGTERM exit-path probe (rc 143, clean)
- mtlmethods-postfix.log / mtlmethods-postfix.raw — offscreen golden, post-fix
- roundtrip-vk.txt / roundtrip-mtl.txt — flavor round-trip checks
- png_pixel_probe.py + baseline/hold probe PNGs — prior-session pixel context
  (dark-wallpaper caveat; pixels are corroborating only, per task)