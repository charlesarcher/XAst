# Phase-2 Re-Verification Evidence (task 28)

**Tree:** merge 9382927 (wt-t25 = 00b8aed + QueuedAfterFlush fix 0b26982; wt-t27 = 84f791a) on top of task-24 commit 937e5df.
**Date:** 2026-08-24. **Gate runner:** orchestrator (atlas), load-gated (1-min load 0.3–1.0 < 2 per protocol).
**Scope:** proves the three behavior-changing Phase-2 tasks — loop restructure (24), event-island relocation (25), D14 `#else` wrap (27) — are all behavior-preserving on X11, and flips F1 Gate 2 green for the first time.

## Leg 1 — Q13 full seeded session, 0 px (×3 consecutive)

Canonical invocation (per qa/phase1-evidence.md):
`./obj/harness --seed 12345 --script test/harness/scripts/session.script --out DIR --ref qa/baseline-x11/session --hiscore test/harness/fixtures/hiScore.nul.data`

| Run | Checkpoints | Result |
|---|---|---|
| w9-q13b-run1 | 13/13 AE=0.000000, 1720 boundaries, game rc=0 | PASS |
| w9-q13b-run2 | 13/13 AE=0.000000, 1720 boundaries, game rc=0 | PASS |
| w9-q13b-run3 | 13/13 AE=0.000000, 1720 boundaries, game rc=0 | PASS |

Covers the canonical draw order (24), the relocated event island (25), and the D14 wrap (27) in one byte-exact assertion. Q7 (screen-wrap, all edges + straddle) is exercised inside the session — any ghost/double-draw at a wrap crossing diverges from baseline; 0 px across 1720 boundaries is the wrap proof. Q1–Q5 spot content (ship angles, rock decorations, 5 explosion frames, thrust flames) likewise occurs inside the session and is covered by the same 0 px; the dedicated Q3 artifact is the 5-frame composite probe (Leg 5).

**Consolidation catch (recorded for provenance):** the first post-merge Q13 failed deterministically (help AE=250693, uniform-gray title) — root cause: the new non-blocking drain gated on `XEventsQueued(QueuedAfterReading)`, which never flushes Xlib's output buffer, so the init-time map+draw requests never reached the server and no Expose ever triggered `Refresh()`. Fix 0b26982: `QueuedAfterFlush` (flush-on-empty ≡ old XNextEvent's flush-before-blocking; zero extra round trips in RunGame where `frameClockSync()` runs one statement earlier). Lesson recorded in notepad learnings; any future backend event drain must preserve flush-on-empty semantics.

## Leg 2 — Q6 frame period (D4 pacing)

Derived from harness manifest timestamps at matching boundaries (quiescence handshake = visible-frame completions), new tree vs pre-refactor baseline:

| Segment (boundaries) | baseline ms/b | new ms/b | Δ |
|---|---|---|---|
| 1→40 | 60.26 | 60.33 | 0.08 |
| 40→120 | 62.59 | 62.67 | 0.09 |
| 120→200 | 62.52 | 62.46 | 0.06 |
| 200→240 | 62.83 | 62.60 | 0.23 |
| 240→340 | 62.62 | 62.57 | 0.05 |
| 340→400 | 62.42 | 62.55 | 0.13 |
| 400→600 | 37.94 | 37.91 | 0.03 |
| 600→800 | 31.39 | 31.36 | 0.04 |
| 800→1000 | 31.43 | 31.33 | 0.11 |

Paced segments sit at **62.5 ms ±0.25** (D4's 62.5 ms ±2 ms satisfied). The faster-ticking later segments are **baseline-identical** (present in the pre-refactor capture at the same boundaries) — quiescence-sampler dynamics on those scripted segments, not a pacing change. The `uSecondsPerFrame`/diffTime sleep arithmetic is untouched; the once-per-frame `frameClockSync()` stands at the old raw XSync's exact statement position.

## Leg 3 — Q12 event parity

X11 leg: behavior-identical island relocation proven by Leg 1 (0 px across the full scripted session — the script drives e/r/o/p/space/q/n/h and title hover through the new `pollEvents` drain + D16 table). The cross-backend X11-vs-GL comparison executes at task 36 (R8-C1 scoping — no GL binary exists until task 31); the reusable fixtures (`test/harness/scripts/q12-*.script`, 7 files incl. the same-drain-cancel negative control) and the state-machine spec (`test/harness/d16-state-machine.md`) are committed for tasks 31/42 to consume.

## Leg 4 — F1 architectural sweep

- **Gate 2 FIRST GREEN:** `g++ -E -P -U X11_BACKEND` whitelist count = **0** on `rotatorDisplayData.C` and **0** on `compositePixmap.C` (was 146/11 by design before task 27).
- **Domain sweep:** 0 whitelist hits outside the exception zone except the documented residuals — shipYard.H 5 (T17 stopped-site floor), shipGroup.H 1 (T20 stopped site), renderingEngine.H 3 (comment-only mentions). playingField.H island = **0** (relocated); stage.H = 0; button.H = 0.
- **Exception zone (closed set):** x11Backend.H (incl. the relocated island — the 4 duplicated island loops unified into one state machine, so raw symbol count nets +11 while every behavior site is preserved; Q13 is the behavioral arbiter), rotatorDisplayData.C 146 + compositePixmap.C 11 (D14, under macro), options.H 47 (D9), AutoRepeatOn.C 3.

## Leg 5 — Dedicated artifacts

- **5-frame composite probe (Q3 precursor):** `test/composite/run.sh` — CPU-composited frames (task-27 `#else` branch) byte-equal to the real X11 `CompositePixmap` output read back via XGetImage under Xvfb :100: **5/5 PASS** (`test/composite/pass.log`, 16908 bytes/frame).
- **Numeric lane (D17.5):** `test/numeric/run.sh` rc=0 — determinism IDENTICAL (40043 bytes/run), **53/53 goldens** match the pre-task-24 capture — the D8 split + event relocation are float-order-stable.

## Leg 6 — F4 resources (ASan/LSan)

ASan build (`-O1 -g -fsanitize=address`, `nm` __asan syms=33), full scripted session: **13/13 AE=0.000000**, RESULT: PASS, game rc=0. LSan report: **exactly the pre-existing 410 bytes / 19 allocations** Xt baseline (XtCalloc/XtMalloc/XtRealloc + documented XStringListToTextProperty) — zero new leaks, zero UAF/double-frees. O3 build restored afterwards (0 __asan syms verified).

## Leg 7 — F3 build matrix (X11 leg + objects legs)

- `make BACKEND=X11` rc=0 from clean; **367 warnings ≤ 384 baseline** (set-diff vs HEAD fully explained: island deletions −3, relocated comparison +1).
- `make BACKEND=GL objects` rc=0 and `make BACKEND=VK objects` rc=0 **with the D14 units compiled** (guards-closed `#else` branches; `nm -u` zero undefined X* symbols on the GL leg).
- Guards-closed `g++ -fsyntax-only -std=c++17 -I. XAsteroids.C` rc=0, 0 errors.

## Verdict

**BARRIER GREEN.** Every leg archived above; no output-changing Phase-3 task may start on a red leg, and none is red. Build-only task 29 and vendoring (already landed at 30) were exempt per R8-N5 and are additionally unblocked.
