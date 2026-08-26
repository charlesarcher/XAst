# Phase-1 Exit Gate Evidence — Task 23 (X11 pixel-identity + resource-ownership verification)

**Date:** 2026-08-24 · **HEAD:** `9274a7d` ("build: XAsteroids.o depends on x11Backend.H …")
**Verdict: GATE GREEN — all five legs pass.** No leg weakened; every number below is a fresh measurement taken on this date at this HEAD.

---

## 0. Environment & load-gating rule

- **Load-gate rule (mandatory precondition):** Q13's quiescence sampler is load/timing sensitive (documented in learnings W6-hotfix/T14: same binary PASS then FAIL back-to-back under load). **Gates only count when 1-minute load average < 2.** Measured 1-min load before/during this gate: **0.12 → 0.08 → 0.13** (all runs gated green).
- Toolchain: g++ (GCC) **16.2.1**, Linux **7.2.0-1-cachyos** (x86_64).
- QA env: `qa/env/env.sh` wires PATH/CPATH/LIBRARY_PATH/LD_LIBRARY_PATH to **`~/.local/xast-env`** (vendored Motif `libXm.so.4` + private `libjpeg.so.62`, Xvfb, xlsfonts/xwd, mkfontscale font set incl. all five stage.H families).
- Display: private **Xvfb :99, geometry 1280x1024x24**, font path `~/.local/xast-env/fonts`, one fresh server per harness run (killed between runs — see cleanup receipt §7).
- Harness: `./obj/harness` (test/harness/harness.C), seed **12345** (`XAST_SEED`), quiescence handshake, client-area captures (720x706), reference `qa/baseline-x11/session`, hi-score fixture `test/harness/fixtures/hiScore.nul.data`.

---

## 1. Leg 1 — Q13 pixel identity: 3 consecutive full-session runs

Command (per run, fresh out-dir, Xvfb killed first):

```
source qa/env/env.sh && pkill -f "Xvfb :99"; ./obj/harness --seed 12345 \
  --script test/harness/scripts/session.script --out <fresh dir> \
  --ref qa/baseline-x11/session --hiscore test/harness/fixtures/hiScore.nul.data
```

| Run | Evidence dir | Checkpoints | AE | Boundaries | Game rc | Harness rc | Result |
|-----|--------------|-------------|----|------------|---------|-----------|--------|
| 1 | `/tmp/opencode/t23/q13-run1` | **13/13** | **ALL 0.000000** | 1720 | 0 | 0 | **PASS** |
| 2 | `/tmp/opencode/t23/q13-run2` (+`.log`) | **13/13** | **ALL 0.000000** | 1720 | 0 | 0 | **PASS** |
| 3 | `/tmp/opencode/t23/q13-run3` (+`.log`) | **13/13** | **ALL 0.000000** | 1720 | 0 | 0 | **PASS** |

Per-checkpoint (run 3 shown; identical in all three): help, start, gp1..gp10, hiscore — each `[diff-ok] … AE=0.000000`; `RESULT: PASS (all checkpoints AE=0)`.

Notes:
- Runs 1–2 emit one `[ERROR] X error 9 (BadDrawable) request=73` after the final hiscore capture: the documented **benign post-'q' reap race** (game destroyed its window during harness teardown; process reaped normally with rc=0). Gate criterion is checkpoint COUNT (13) + rc=0 + per-checkpoint AE — all met.
- This closes B7's "T19 deterministic test inexecutable as written": it ran three times, machine-judged, byte-zero.

## 2. Leg 2 — ASan/LSan resource-ownership verification

Instrumented rebuild (exact flags):

```
make BACKEND=X11 -B CXXFLAGS="-I/usr/include/X11 -I$HOME/.local/xast-env/include -std=c++17 -Wall -Wextra -Wno-unused-parameter -O1 -g -fsanitize=address" LDFLAGS="-fsanitize=address"
```

- Build rc=0; instrumentation proven: `nm XAsteroids | grep -c __asan` = **32 (> 0)**.
- Full session under `ASAN_OPTIONS=detect_leaks=1:log_path=/tmp/opencode/t23/asan/leak`: **13/13 checkpoints AE=0.000000** (even under −O1+ASan pacing — no capture-phase drift this time), 1720 boundaries, harness rc=0. Game rc=**1** = LSan's leaks-found exit code (expected: the documented baseline leaks below are still present by design).
- **Leak report = EXACTLY the pre-existing baseline:**

```
SUMMARY: AddressSanitizer: 410 byte(s) leaked in 19 allocation(s).
```

| Bucket | Size | Alloc family |
|--------|------|--------------|
| Direct | 64 B / 4 objs | XtCalloc |
| Direct | 32 B / 2 objs | XtMalloc |
| Direct | 10 B / 1 obj | XStringListToTextProperty (documented pre-existing WM text-property leak — PRESERVED by design, T15) |
| Indirect | 112 B / 1 obj | XtRealloc |
| Indirect | 96 B / 4 objs | XtCalloc |
| Indirect | 96 B / 7 objs | XtMalloc |

- **Zero new leaks, zero use-after-free, zero double-free, zero overflow, zero SEGV** (grep over harness log + leak report = 0 hits). Every stack frame is the XtMalloc family inside libXt/libX11 — none in project code.
- D15 ownership map holds: every project allocation has exactly one releaser; the named `errorInfo` `XFreeFont` assertion (F4) is green (no font leaks in report).
- **O3 build restored:** `make BACKEND=X11 -B` rc=0; `nm XAsteroids | grep -c __asan` = **0**. Binary back to production config (425192 bytes).

Evidence: `/tmp/opencode/t23/asan/{harness.log,leak.909125,run/}`.

## 3. Leg 3 — Residual census (EXACT, 56-symbol whitelist sweep)

Method: plan §F1 Gate-1 grep — full 56-symbol whitelist, `\b`-bounded, over all `*.H`/`*.C`, then per-file counts. Raw residual list archived at `/tmp/opencode/t23/f1_gate1_residuals.txt`.

### Census table (product tree, exact counts measured 2026-08-24 @ 9274a7d)

| File | Sites | Status |
|------|-------|--------|
| `gamePlay/options/options.H` | **47** | D9 permanent exception zone (c) — whole body inside `#ifdef X11_BACKEND` (:43–:3884) |
| `utilities/pixmaps/rotated/rotatorDisplayData.C` | **146** | D14 permanent exception zone (b) |
| `utilities/pixmaps/composite/compositePixmap.C` | **11** | D14 permanent exception zone (b) |
| `AutoRepeatOn.C` | **3** | D9/D14 standalone X11-only utility target (d) |
| `gamePlay/playingField.H` | **22** | Declared event island (task 15, M5-C1 list) — deletion at **task 25** |
| `gamePlay/shipYard.H` | **5** | ⚠ STOPPED SITES (T17) — see justifications |
| `objects/ships/shipGroup.H` | **1** | ⚠ STOPPED SITE (T20) — see justifications |
| `gamePlay/stage.H` | **0** | clean (T14) |
| `gamePlay/options/button.H` | **0** | clean (T16 + W6-hotfix-followup) |
| `objects/explosions/explosionGraphic.H` | **0** | clean (W6-hotfix) |
| `objects/explosions/explosion.H` | **0** | clean (W6-hotfix defect C) |
| `objects/rocks/rockGroup.H` | **0** | clean (T18/W6-hotfix) |
| `objects/enemies/enemyGroup.H` | **0** | clean (T21) |
| `objects/enemies/enemyBulletGroup.H` | **0** | clean (T21) |
| `objects/bullet.H` | **0** | clean (T22) |
| `objects/ships/shipBulletGroup.H` | **0** | clean (T22) |
| `utilities/pixmaps/rotated/rotatorDisplayData.H` | **0** | clean |
| `utilities/pixmaps/composite/compositePixmap.H` | **0** | clean |
| **Domain+exception total (excl. backend)** | **235** | |

### Reconciliation vs plan ideal — HONEST accounting

- Plan ideal (task 23 text): **207 exception-zone + 22 island = 229**.
- **Measured actual: 235 = 229 ideal + 6 documented stopped-sites** (shipYard.H 5 + shipGroup.H 1). The delta is exactly the two wave-documented stops below — nothing undocumented, nothing silent.
- Backend implementation zone (a): `utilities/rendering/x11Backend.H` = **107 sites** (outside the 434-site census denominator; F1 zone (a) by construction). The island is still in `playingField.H` — its relocation into the backend happens at task 25 (the "relocated island" state is post-25, not current).
- Non-call-site mentions excluded from the census (verified by inspection): `renderingEngine.H` **3 comment-only mentions** (:26/:57/:99 — API doc comments describing X11 semantics, zero call syntax); `test/harness/harness.C` + `test/numeric/probe.C` — QA infrastructure, outside the product tree and the census denominator.
- Alias-last-consumer assertion (task 22 acceptance): `grep 'playingField.pixmap\|playingField.gc\|explosionGraphic.gc'` across `objects/` = **0 hits** ✓.
- F1 Gate 2 (D14 preprocess-zero, `-U X11_BACKEND`): rotatorDisplayData.C = 146, compositePixmap.C = 11 — **correctly still unwrapped at Phase-1 exit; flips green at task 27** (Phase 2). Recorded here so nobody mistakes it for a regression.

### Stopped-site residuals with in-file justification + closure path

| # | Site | Symbol | Justification (learnings ref) | Closure path |
|---|------|--------|-------------------------------|--------------|
| 1 | `shipYard.H:101` | XAllocColor | T17 stop: ctor yard-bg colormap allocation feeding `.pixel` to the raw stamp; engine replacement existed but backend color primitive did not at wave time | consume backend `allocateColor(r,g,b)` seam (exists since T14) + fg/bg-capable bitmap seam or task-26 decoder + fixed RGBA upload |
| 2 | `shipYard.H:112` | XAllocColor | T17 stop: ctor ship-fg allocation (same mechanism) | same as #1 |
| 3 | `shipYard.H:118` | XCreatePixmapFromBitmapData | T17 stop: raw colored ship stamp creation (needs fg/bg-capable target, plain drawTexture can't reproduce) | backend fg/bg-capable bitmap seam or task-26 decoder + fixed RGBA upload |
| 4 | `shipYard.H:198` | XAllocColor | T17 stop: AlterIcon ship-fg allocation (same mechanism) | same as #1 |
| 5 | `shipYard.H:210` | XCreatePixmapFromBitmapData | T17 stop: AlterIcon raw stamp recreation | same as #3 |
| 6 | `shipGroup.H:277` | XAllocColor | T20 stop: ctor allocation writes `.pixel` into member `iconColor`, read by options.H:1223/:1245/:1265 (`CreateToggleButtonPixmaps` icon foregrounds — D9 permanent exception-zone readers); removal ⇒ pixel 0 ⇒ black-on-black icons = visual regression | backend-exposed color primitive (`allocateColor` seam exists; consumption pending) |

Each site carries an in-file STOPPED-SITE comment with zero whitelist tokens beyond the call itself. These 6 sites are the declared Phase-1 residual floor; they do NOT block Phase 2 (they are draw-data producers feeding the D9 exception zone, not domain draws).

## 4. Leg 4 — GL/VK objects targets green

```
make BACKEND=GL objects   → rc=0   (/tmp/opencode/t23/gl_objects.log)
make BACKEND=VK objects   → rc=0   (/tmp/opencode/t23/vk_objects.log)
```

Guards closed, island excluded, stub loop unchanged — D-A A3 holds. (Warnings in logs are the recorded pre-existing baseline set; none reference migrated lines.)

## 5. Leg 5 — Transitional surface declaration (deletion tasks 25/47)

Everything below is *declared transitional*: behavior-load-bearing today, scheduled for deletion, and grep-auditable.

1. **The 22-site event island** — `playingField.H` (symbol-for-symbol the task-15 M5-C1 list; current lines :301/:313/:320/:348/:351/:352/:354/:357/:359/:361/:367/:378/:425/:472/:490/:496/:499/:579/:581/:584/:586/:594): XNextEvent×4, XEventsQueued×1, XLookupString×3, XRefreshKeyboardMapping×4, XAutoRepeatOn×3, XAutoRepeatOff×3, XSync×2, XRaiseWindow×2. **Deletion: task 25** (verbatim relocation into `X11Backend::pollEvents`).
2. **PlayingField sanctioned alias survivors** — `gc`/`pixmap` (playingField.H:52-53), `WM_PROTOCOLS`/`WM_DELETE_WINDOW` atoms (:100), populated via backend accessors `canvas()/gxorGC()/wmProtocolsAtom()/wmDeleteWindowAtom()` (x11Backend.H:598-601). **Deletion: task 47.**
3. **Transitional type-shim consumers (the shim header itself deleted at task 47)** — 20 files: AutoRepeatOn.C, options.H, button.H, stage.H, playingField.H, shipYard.H, enemyBulletGroup.H, enemyGroup.H, explosion.H, explosionGraphic.H, rockGroup.H, shipBulletGroup.H, shipGroup.H, movableObject.H, bullet.H, frameList.H, compositePixmap.H, rotatorDisplayData.{H,C}, rotator.H. **Shim deletion: task 47.**
4. **Stage transition members (8)** — stage.H:65-…: `engine` (RenderingEngine&), `display`, `window`, `icon`, `titleGC`, `hiScoreGC`, `scoreGC`, `defaultGC` — non-owning aliases onto backend-owned resources (D15 map), flagged DELETED-at-47 in-file. Plus guarded decls: `XFontStruct*` ×5 (buttonInfo/errorInfo/titleInfo/hiScoreInfo/scoreInfo) and `static XColor` ×4 (windowBg/shipYardBg/buttonFg/buttonBg) — guarded because the shim has no stand-in for XFontStruct. **Deletion: task 47.**
5. **Group/field/yard/button engine members (8× `RenderingEngine& engine`)** — stage.H:65, playingField.H:42, shipYard.H:42, button.H:43, shipGroup.H:217, enemyGroup.H:104, enemyBulletGroup.H:50, shipBulletGroup.H:61 — each flagged DELETED-at-47 in-file. **Deletion: task 47.**
6. **Backend transition helpers** (x11Backend.H, exception zone (a)): `setFrameGeometry` (:609), `setFrameYardTexture` (:615), `requestFieldPresent` (:619) — T14 endFrame request-gating plumbing; `allocateColor` (:624) — T14 seam (also the designated closure path for stopped-sites #1/#2/#4/#6); `createBitmapMask` (:628) — W6-hotfix depth-1 mask bridge; `textFirstCharLbearing` (:635) — W6-hotfix-followup label-bearing bridge; `buttonGC_` font slot (:118/:722/:827/:960) — W6-hotfix-followup. **Deletion: task 47** (or absorption into their consuming features where the plan says so).
7. **B2 Options guard sites** — options.H `#ifdef X11_BACKEND` blocks :9–:26 (includes) and :43–:3884 (entire class body, containing all 47 census sites). The guard is the D9 transition marker until the GL/VK Dear ImGui menu path lands. **Guard/zone disposition: task 47** (menu seam completion).

## 6. Gate arithmetic (M5-C1)

434 pinned census − 207 exception − 22 island = **205 sites migrated-to-engine in Phase 1** (227 domain − 22 island). The island returns to the backend at task 25 (205 + 22 = 227). The 6 stopped-sites are the documented delta between the 229 ideal and the measured 235; their migration belongs to the task-26-decoder/backend-seam follow-ups, not to Phase-1 scope.

## 7. Cleanup receipt

- Xvfb :99 killed between every run (harness self-cleanup + explicit `pkill`; verified `:99 down`, `pgrep Xvfb/XAsteroids` = only the checking shell itself). **No stray processes.**
- O3 production binary restored and verified ASan-free (`__asan` syms = 0); instrumented objects fully overwritten by `-B`.
- Temp evidence dirs (all under `/tmp/opencode/t23/`): `q13-run1/`, `q13-run2/`(+`.log`), `q13-run3/`(+`.log`), `asan/{harness.log,leak.909125,run/}`, `f1_gate1_residuals.txt`, `gl_objects.log`, `vk_objects.log`. Scratch lists from earlier tasks (`/tmp/opencode/t1[4-7]_*`) untouched.
- Tree changes: **none** except this evidence file (+ learnings append). Product sources, harness, makefile untouched.

## 8. Risks / carry-forward

- Q13 flakiness remains environmental (load-correlated sampler alignment). All three gate runs were on a quiet box (load ≤ 0.13); re-verify the load rule before any future gate.
- LSan keeps the process alive seconds inside exit() — post-'q' reap races (BadDrawable request=73) are benign iff the game reaps rc=0 (held in all runs).
- The 6 stopped-sites need the `allocateColor`/bitmap-seam consumption follow-up; the seam already exists, so closure is mechanical once a task owns those files.
- F1 Gate 2 stays red-by-design until task 27 wraps the D14 files in `#ifdef X11_BACKEND` — do not treat the 146/11 preprocess counts as a Phase-2 entry failure.
