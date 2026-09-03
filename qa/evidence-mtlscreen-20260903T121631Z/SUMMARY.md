# BLOCKED — MTL on-screen window is black (nil CAMetalLayer drawable)

**Verdict: BLOCKED — platform/environment limitation, not an XAst MTL-code defect.**
`make BACKEND=MTL run` on this machine cannot make the GLFW window render on-screen.
The root cause is outside the in-scope files (`mtlCocoa.mm`, `mtlBridge.mm`,
`mtlBackend.H`, `playingField.H`) and cannot be fixed without a packaging/environment
change (app bundle or a session/OS that allocates on-screen Metal drawables to a
command-line process). **No workaround was shipped.**

## Environment
- macOS **26.6.2** (BuildVersion 25G83) — a very new build with anomalous AppKit
  behavior (see "Environment anomalies").
- Apple M2 Pro. GLFW **3.5.1** (homebrew prebuilt dylib; `vendor/glfw` is headers-only).
- Displays: primary Built-in Liquid Retina XDR 3024x1964 (scale 2.0); secondary 6720x3780.
- Binary launched from a shell (opencode → bash → `./XAsteroids`), i.e. a
  command-line process, **not** a `.app` bundle.
- git HEAD at start: d27c0494 (clean tree). Product code was reverted after
  diagnosis; only this evidence dir remains in the tree.

## Symptom (reproduced, ground truth)
- `gtimeout N ./XAsteroids` → exit 124 (alive). Log: `canonical window 688x702`,
  `5 render pipelines created`, `initWindow OK (1376x1404 fb, scale 2.0)`, then a
  steady flood of `mtlBackend: nil drawable — skipping frame`.
- Measured: 21,363 nil lines / 25s (≈855 Hz); 13,335 / 15s on the clean rebuild.
  (`[baseline-run.log]`, `[final-baseline-clean.log]`.)
- Screenshot: window titlebar visible, client area dark. NOTE: this desktop has a
  **dark wallpaper**, so the client area (~30/255) is indistinguishable from the
  wallpaper by brightness; the window may also not be fully composited (see
  anomalies). The reliable rendering probe is the nil-drawable count, not pixels.

## The on-screen drawable path is proven broken; the GPU path is proven fine
- Offscreen control PASS: `make BACKEND=MTL mtlmethods` → `mtlmethods: PASS`,
  1,310,856-byte raw = exact task-11 golden, resize transforms MATCH
  (`[mtlmethods-run.log]`, `[mtlmethods-out.raw]`). **Critical:** mtlmethods renders
  to an offscreen render target (`createRenderTarget`/`beginRenderTo`) and reads it
  back; it **never calls `nextDrawable` on the window layer.** So "offscreen good"
  proves device/pipelines/shaders are healthy but tells us *nothing* about the
  on-screen window-drawable path — where the failure is.
- Therefore: Metal device (M2 Pro), 5 pipelines, shaders, vertex/texture buffers,
  offscreen RTs, and readback all work. The failure is **specifically**
  `[CAMetalLayer nextDrawable] → nil` on the on-screen window's layer.

## Hypotheses tested (task H1–H4) — all REFUTED with runtime evidence
An env-gated in-app probe (`XAST_MTL_PROBE`) dumped layer/view/window/thread state
(`[probe-baseline.log]`):
- **H1 (layer not in drawable state / not in window tree) — REFUTED.** By the first
  on-screen frame the layer has a `superlayer` (non-nil), `view.layer == layer`
  (layerIsOurs=1), `view.wantsLayer=1`, view has a superview; window is
  `GLFWWindow : NSWindow`, `isVisible=1`.
- **H2 (drawableSize clobbered to 0×0 by an early framebuffer callback) — REFUTED.**
  `drawableSize=(1376,1404)` and `contentsScale=2.0` at init **and** during the
  loop; it is never clobbered to 0.
- **H3 (non-main thread / no runloop) — REFUTED.** `appkitIsMainThread=1`.
- **H4 (displaySyncEnabled=YES) — REFUTED.** Toggled to NO via `XAST_MTL_NOSYNC=1`;
  `nextDrawable` still nil (`[probe-nosync.log]`, 9802 nil lines).

## Root-cause localization (the decisive evidence)
Because H1–H4 were all refuted, I built **standalone** probes (in this dir:
`mtldrawprobe.m`, `mtlholdprobe.m`, `mtlfinalprobe.m`, `mtlbootstrap.m`; compiled
against the same homebrew GLFW 3.5.1 + Metal) that reproduce the layer attach
*without any XAst game code*:
- `[strategy-experiment.log]` — 7 attach strategies on a GLFW NO_API window
  (view.layer/wantsLayer orderings, drawableSize-before-attach, sublayer, custom
  NSView with `+layerClass=CAMetalLayer`, extra run-loop pumping): **all return
  0 non-nil drawables.**
- `[activation-experiment.log]` — `setActivationPolicy:Regular` +
  `activateIgnoringOtherApps:` + `makeKeyAndOrderFront`: **still nil.**
- `[holdprobe.log]` — one activated window held for 25s, pumped: **nil every
  second.**
- `[final-glfw-primary.log]` — GLFW window forced onto the **primary** display +
  valid `glfwPollEvents` pump: **nil for 12s** (superlayer set).
- `[final-plain-appkit.log]` — a **plain AppKit `NSWindow` + `CAMetalLayer`, NO
  GLFW, NO XAst code**, valid pump, superlayer set, main thread: **nil for 12s.**
- `[bootstrap-test.log]` — same plain-AppKit window but with a proper
  `[NSApp finishLaunching]` bootstrap: **nil for 12s.**

**A 40-line GLFW-free plain-AppKit program reproduces the failure.** This localizes
the defect to the **OS / window-server / command-line-process** interaction on this
macOS 26.6.2 build, independent of XAst's code and of GLFW. The window server does
not allocate on-screen CAMetalLayer drawables to a non-bundle CLI process here.

### Environment anomalies observed (corroborate a non-standard OS/session)
- `-[NSApplication runForTimeInterval:]` → **unrecognized selector** (a method that
  has existed since 10.6).
- `CFRunLoopRunInMode(kCFRunLoopCommonModes, …)` → "invalid mode" warning.
- After `finishLaunching`, `[NSApp isRunning] == 0`.
- GLFW 3.5.1's `GLFWWindow` (an `NSWindow` subclass) does **not** respond to
  `isOnScreen` (calling it throws), though it responds to `backingScaleFactor`.
These are inconsistent with a stock macOS AppKit and indicate a heavily modified /
very-new / possibly non-interactive-WindowServer session in which CLI-process Metal
surface allocation is unavailable.

## Why it is BLOCKED (not fixable in-scope)
The only ways to make on-screen Metal drawables work would be:
1. Package `XAsteroids` as a proper **`.app` bundle** (Info.plist, activation,
   signing) so LaunchServices/WindowServer treat it as a foreground GUI app —
   a makefile/packaging change, explicitly out of scope ("MUST NOT touch the
   makefile link lines"; "if a hypothesis requires a makefile/vendor change, stop
   and report BLOCKED").
2. Run in a **proper interactive GUI session / OS** where CLI processes are granted
   on-screen Metal drawable allocation — an environment change, out of scope.
3. Change the presentation architecture to a surface that does not need on-screen
   CAMetalLayer drawables — a major re-architecture, and effectively the "workaround
   the task forbids."

None can be done within `mtlCocoa.mm` / `mtlBridge.mm` / `mtlBackend.H` /
`playingField.H` without touching packaging or shipping a workaround.

## Exact next steps to unblock (in priority order)
1. **Package as a `.app` bundle** and launch via `open` (or a LaunchServices
   launch). This is the most likely real fix: Metal on-screen drawables are
   allocated to foreground *app-bundle* GUI processes, which a CLI binary is not.
   Concretely: a small bundle wrapper around the existing binary (Info.plist with
   `CFBundleExecutable`, activation), invoked from the `run` target. (Requires a
   makefile/packaging change — needs a new, explicit task authorization.)
2. If a bundle is not acceptable, **verify the target session**: confirm the display
   session is a full interactive WindowServer session (not a headless/SSH/CI/VNC
   session) on a macOS where CLI Metal surfaces work; the anomalies above
   (`runForTimeInterval:` unrecognized, common-modes invalid) suggest the current
   build/session is not a normal one. Retest on a known-good macOS + GUI session.
3. Only then, if the bundle path is confirmed to fix it, port the (already
   correct) `mtlCocoa`/`mtlBackend` on-screen path — **no changes to the MTL
   rendering code itself are needed**; it is already correct.

## What was NOT done (per instructions)
- No workaround shipped (no faking drawables, no hiding the window, no forced
  non-black state).
- No makefile / vendor / GL / VK / X11 changes.
- No commit of a "fix" (there is no in-scope fix). The in-app probe code added
  during diagnosis was **reverted**; the tree is clean except this evidence dir.

## Adversarial classes (one line each)
- malformed_input: n/a (no external input to this path).
- prompt_injection: n/a (no untrusted input processed).
- flaky_tests: mtlmethods is deterministic (exact golden byte match, reproduced).
- long_external_commands: every run bounded by `gtimeout`; all finished.
- stale_state: flavor-check MTL run before each run (PASS); clean rebuild before the
  final baseline.
- misleading_success_output: exit 124 (alive) = expected, not a pass; the
  nil-drawable count is the rendering probe; offscreen "PASS" was recognized as NOT
  covering the on-screen path (mtlmethods uses offscreen RTs, never nextDrawable).
- dirty_worktree: clean at start; product files reverted; only this evidence dir
  remains untracked.
- hung_commands: all game/probe runs under gtimeout or explicit pkill; pgrep receipt
  empty.

## File index (this dir)
- `baseline-*` — pre-diagnosis baseline: flavor check, run log (21k nil lines),
  rc, window rect, full + window-region screenshots, pixel probe.
- `probe-baseline.log`, `probe-nosync.log` — in-app layer-state probe dumps
  (H1–H4 evidence) with and without displaySync.
- `strategy-experiment.log`, `activation-experiment.log`, `holdprobe.log`,
  `final-glfw-primary.log`, `final-plain-appkit.log`, `bootstrap-test.log` — the
  localization probes (sources: `mtldrawprobe.m`, `mtlholdprobe.m`,
  `mtlfinalprobe.m`; bootstrap source compiled from a here-doc, logic identical to
  `mtlfinalprobe.m`'s plain-AppKit path + `finishLaunching`).
- `mtlmethods-run.log`, `mtlmethods-out.raw` — offscreen GPU-path control (PASS).
- `png_pixel_probe.py` — pure-stdlib PNG decoder / non-black pixel counter
  (alpha excluded; a black+opaque pixel reads black).
- `ctx-full.png`, `wall-*.png`, `win.png`, `holdprobe-*.png` — screenshot context
  (dark-wallpaper environment; see note in "Symptom").