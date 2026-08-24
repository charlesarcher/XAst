# test/numeric — numeric QA lane (task 45a: golden capture)

**Purpose (repo README honored):** this codebase exists to find compiler and
floating-point bugs. Visual parity alone does not cover that; this lane pins
the float-order-sensitive numeric paths as **state hashes** so any drift —
especially the drift the D8 update/render split (task 24) could introduce —
is detectable by diffing, not by eyeballing.

- **45a (this directory, committed now):** golden capture on the **PRE-task-24
  tree** + two-run determinism proof.
- **45b (W16, later):** the live lane diffs fresh runs against
  `golden/goldens.txt` on X11 **and** GL to prove the split is
  float-order-stable. `make test-numeric` is wired at 45b — deliberately NOT
  here (no makefile changes in 45a).

## What is pinned (region → observation)

| Region | Where | How observed | Hashed |
|---|---|---|---|
| Swept-intersect sort | `utilities/intersection2d.H:754-763` | **Real code**, driven through the public `Intersector::Intersect()` with fixture `MovableObject`s; hit/miss event order recorded via virtuals | Event sequence (type, object id, intersect time bits, point bits) + final per-object state |
| Mid-pass removal | `objects/ships/shipGroup.H:253-275` (`Ship::HitScript`) | **Replicated call sequence** in the fixture `HitScript`: `RemoveNonPermeable(this)` mid-loop (+ companion `RemovePermeable(thrust)`), the `:261-265` explosion box/velocity arithmetic on real `Box`/`Liner` code, then `AddPermeable(explosion)`. The removal machinery executed is the REAL `intersection2d.H` code (`RemoveNonPermeable :933-948`, removeList recycling, sorted-list deletion while the process loop iterates). See "Limitations" for why `Ship` itself cannot run headless | Same as above plus explosion-analog final box center / velocity bits |
| Pass-count snapshot | `utilities/intersection2d.H:766-784` (post-pass `Miss` sweeps — note: plan text says "shipGroup.H ~:766-784", but shipGroup.H ends at line 742; the region is intersection2d.H's Miss sweep) | **Real code**; each fixture counts its own `MissScript` invocations | Per-object miss/hit counts inside the case hash; Section B cases pin exactly-one-miss-per-survivor-per-pass |
| Gravity FP paths | `gamePlay/playingField.H:217-240` (`CalcGravityAcceleration`, zero-distance guard `distMagSquared ? ... : Vector2d()`) | **Real private member function**, called directly (see white-box note); explicit `isfinite` asserts + a dedicated same-point case asserting the guard emits exactly `Vector2d()` | Input geometry/radii/areas + result vector bits |
| Gravity summation order | `playingField.F:242-293` (`SetGravityAcceleration`) | Loop scaffolding replicated in exact `+=` order (non-permeable then self-permeable lists); every per-pair value comes from the REAL `CalcGravityAcceleration`. The real method iterates PlayingField's own list members, which the headless fake instance does not carry | Resulting acceleration bits per object, in summation order |

## Case matrix (53 hashes in `golden/goldens.txt`)

- **A1-A8 + A1b** edge classes: exact-tangency boundary decision (centers placed
  at precisely 2*radius using the same doubles RotatorDisplayData computed),
  tangent-with-drift, near-miss (radiusSum+eps), coincident spawn,
  zero-relative-velocity, staggered chain exercising sorted hit order +
  mid-pass self/thrust removal + same-pass explosion miss, equal-time sort ties
  (qsort tie order pinned), deceleration miss, self-permeable mix.
- **R00-R21** (22) seeded random intersect configurations (2-5 objects, mixed
  permeability classes, three shapes, random angles).
- **B0-B2** pass-count snapshots across three geometry variants.
- **C1-C7** gravity edges: zero-distance guard (explicit assert), denormal
  offset, overlap branch, boundary distance, inverse-square, relativistic mass,
  frame-rate scaling; **R00-R07** (8) seeded random pairs.
- **D1-D3** summation-order cases (triangle, five-body, mixed lists).
- **E1** 64-angle rotator sweep over REAL rotated-vector tables +
  `UpdateAngle` fmod checks.

Seed: fixed in code (`20260823`), printed on every run; `XAST_SEED=<n>`
overrides. Randomness flows exclusively through the repo's own
`gary_rand::rand_16()` (`stage.H`), i.e. `rand()>>16` after `srand`.

## Hash layout

SHA-256 over a canonical byte stream, all integers little-endian fixed-width,
doubles as raw IEEE-754 bit patterns (uint64 LE), strings as bytes+NUL:

```
IX cases:  "IX"\0 caseId\0 u32 seed i32 nObj
           { u8 class('N'/'S'/'P') i32 shape f64 cx cy vx vy ax ay maxVel angVel
             u8 shipLike u8 thrusting } * nObj
           u8 withThrust i32 nEvents
           { u8 type(1=HIT/2=MISS) i32 id f64 t px py } * nEvents
           i32 nFinal { i32 id u8 class u8 collided i32 miss i32 hit
                        f64 centerX centerY velX velY createTime angle } * nFinal
GR cases:  "GR"\0 caseId\0 u32 seed u8 relativistic i32 uspf f64 G
           f64 aCx aCy bCx bCy aRadius aArea bRadius bArea bVelMag
           f64 gX gY
GS cases:  "GS"\0 caseId\0 u32 seed f64 G i32 n { i32 id f64 accX accY } * n
ROT case:  "ROT"\0 caseId\0 u32 seed
           { f64 angle i32 numVecs f64 vecX vecY } * 64  { f64 t f64 angle } * 3
```

`goldens.txt` format: `caseId<TAB>sha256hex`, one line per case, stable
contract for 45b's diff.

## Exact build+run commands (no makefile changes)

```sh
source qa/env/env.sh
flock /tmp/opencode/xast-build.lock \
  g++ -O3 -Wall -Wextra -Wno-unused-parameter -std=c++17 -DX11_BACKEND \
      -fno-access-control \
      -I. -I/usr/include/X11 \
      -o /tmp/opencode/xast-numeric-probe \
      test/numeric/probe.C \
      utilities/pixmaps/rotated/rotatorDisplayData.C \
      -lX11

# headless display (real RotVectorData pixmaps need a Display):
Xvfb :9507 -screen 0 1280x1024x24 -nolisten tcp &
DISPLAY=:9507 /tmp/opencode/xast-numeric-probe > run1.txt
DISPLAY=:9507 /tmp/opencode/xast-numeric-probe > run2.txt
diff run1.txt run2.txt        # must be empty (two-run determinism proof)
grep '^GOLD' run1.txt | sed 's/^GOLD //' > golden/goldens.txt
```

Or simply: `test/numeric/run.sh` (build + Xvfb + two runs + diff + goldens).

Compile notes:
- `-DX11_BACKEND` required (repo headers pull Motif transitively via
  `options.H`; the qa env prefix supplies Xm headers/libs via CPATH/
  LIBRARY_PATH). Only `-lX11` is linked — nothing else is odr-used.
- The probe never draws; X is used only to build the real rotated-vector
  pixmap tables (`RotVectorData`), which the intersector's unit-stage reads.

## White-box access disclosure (no source modified)

`CalcGravityAcceleration` is private and a real `PlayingField` cannot be
constructed headless (fonts/window/pixmap in its ctor). The probe TU is
compiled with **`-fno-access-control`** — GCC's own switch for exactly this
situation (template/debug access testing). It disables access checking in this
TU only; zero game-source files are touched and no macro tricks are used.
`CalcGravityAcceleration` dereferences no instance state (verified by
inspection of `playingField.H:217-240`), so it is invoked on an aligned
storage block that is never constructed.

## Limitations (honest coverage report)

1. `Ship::HitScript` itself is unexecutable headless: constructing any `Ship`
   requires `XAllocColor` + `stage->display/window` + the full global web
   (`shipGroup`, `score`, `playingField`). Its intersector-call sequence and
   the FP-bearing `:261-265` arithmetic are replicated line-for-line in the
   fixture; the removal machinery they drive is the real `intersection2d.H`
   code. If task 24 changes `Ship::HitScript` arithmetic WITHOUT changing
   `intersection2d.H`, these goldens will NOT flag it — flagged here explicitly.
2. `SetGravityAcceleration` loop scaffolding is replicated (identical `+=`
   order); per-pair math is real. A change confined to the loop structure of
   `:242-293` would not be caught until 45b wires the full-game lane.
3. qsort tie order (A6) is libc-dependent by nature; goldens pin THIS
   environment's behavior — cross-libc diffs at 45b are expected noise, not
   game-code drift.
4. Hyper (`shipGroup.H:589-646`) and bullet/enemy scripts are out of 45a scope
   (the four pinned regions above are the plan's list).

## Files

- `probe.C` — the standalone observer driver (pure; links real repo code).
- `run.sh` — build + Xvfb + two-run determinism proof + goldens emission.
- `golden/goldens.txt` — one `case<TAB>hash` line per case (committed).
- `golden/run1.txt`, `golden/run2.txt` — archived full outputs of both runs
  (byte-identical; the determinism proof).
- `golden/determinism.diff` — empty diff artifact proving byte identity.
