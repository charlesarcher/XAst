# Evidence — GNU make 3.81 same-second flavor-switch race: PRE-fix reproduction + POST-fix proof

Date: 2026-09-03 (UTC) · Machine: macOS 26.6.2, Apple M2 Pro · **/usr/bin/make = GNU Make 3.81** (1-second mtime resolution)
Fix commit: see git log `fix(build): force per-flavor relink (close GNU make 3.81 same-second race)`

## Mechanism (from `qa/evidence-mtlscreen-20260903T121631Z/makerace-m2-debug.log`)

All four legs link the SAME output name `XAsteroids` with no flavor marker in the
prereq mtimes. The flavor stamp (`obj/.backend`, FORCE-prereq, rewritten + `rm -f
XAsteroids` on switch) is the only relink trigger for a flavor switch. GNU make 3.81
tests prereq freshness with a **strict-newer** (`>`) mtime compare at 1-second
granularity: if the stamp rewrite (make #2) and the just-linked binary from make #1
land in the **same filesystem second**, the target is judged up-to-date, the link
recipe is never run, and — because the stamp recipe already `rm`'d the binary —
make exits rc=0 with the root binary **missing** (or, if no rm happened on that
path, still the **old flavor**).

## PRE-fix loop (before the makefile edit) — `pre-fix-loop.sh`

Loop: 4x `make BACKEND=VK && make BACKEND=MTL` + `qa/flavor-check.sh MTL`;
1x `make BACKEND=VK && make BACKEND=VK` + `qa/flavor-check.sh VK`.
Each make call's full output in its own log (`pre-<i>-<backend>-make.log`);
flavor-check output in `pre-<i>-flavorcheck-<exp>.log`.
Verdict BROKEN iff any make rc != 0 OR `XAsteroids` missing OR flavor-check rc != 0
(make rc=0 alone is NOT pass).

`pre-loop-summary.log`:

```
iter=1 pair=VK->MTL make1_rc=0 make2_rc=0 binary=MISSING flavorcheck_rc=1 verdict=YES
iter=2 pair=VK->MTL make1_rc=0 make2_rc=0 binary=MISSING flavorcheck_rc=1 verdict=YES
iter=3 pair=VK->MTL make1_rc=0 make2_rc=0 binary=MISSING flavorcheck_rc=1 verdict=YES
iter=4 pair=VK->MTL make1_rc=0 make2_rc=0 binary=MISSING flavorcheck_rc=1 verdict=YES
iter=5 pair=VK->VK  make1_rc=0 make2_rc=0 binary=PRESENT flavorcheck_rc=0 verdict=NO
```

**RACE REPRODUCED: 4/5 iterations broken** (both makes rc=0, yet root binary missing
after the MTL make). `pre-1-MTL-make.log` is empty: the stamp recipe rewrote the
stamp + rm'd the binary, but make ran NO link recipe. Timestamp tie at capture:
`XAsteroids` mtime == `obj/.backend` mtime == same second. Iteration 5 (same-flavor
VK->VK) is unaffected because no switch means no rm — matching the mechanism.

## The fix (makefile, 7 lines)

```make
.PHONY: XAsteroids-flavor-$(BACKEND)
XAsteroids-flavor-$(BACKEND):
```
plus `XAsteroids-flavor-$(BACKEND)` appended to the prerequisite lists of ALL FOUR
link rules (GL, VK, MTL, X11), and one comment line in the FLAVOR_STAMP block.
A phony prereq is always "stale" independent of mtime -> the root binary is
ALWAYS relinked per invocation; the 1-second mtime tie is irrelevant.
Stamp recipe + file kept for tooling (its `rm -f` on switch stays as belt-and-braces).
Accepted consequence: same-flavor reruns show the link line (objects stay cached)
instead of a silent no-op.

## POST-fix loop (after the makefile edit) — `post-fix-loop.sh`

Same loop shape, 15 iterations (14x VK->MTL + 1x VK->VK).
`post-loop-summary.log`: **15/15 iterations** `make1_rc=0 make2_rc=0 binary=PRESENT
flavorcheck_rc=0 verdict=NO` → **BROKEN_ITERATIONS_POST=0/15**.

## Same-flavor double run (post-fix)

`make BACKEND=MTL && make BACKEND=MTL`: both rc=0, each shows the link line
(`sameflavor-1-make.log` / `sameflavor-2-make.log`, one `g++` line each), ends
`flavor-check MTL` rc=0 → MTL-PASS (`sameflavor-flavorcheck-MTL.log`).

## End-to-end MTL run

`make BACKEND=MTL && gtimeout 10 ./XAsteroids` → **rc=124** (process alive at 10 s,
killed by gtimeout). `e2e-run.log` line 3: `mtlBackend: initWindow OK (1376x1404 fb,
scale 2.0)`. `pgrep -fl XAsteroids` rc=1 afterwards (no process); `ps` clean.

## Regressions

- `make BACKEND=MTL mtlmethods && ./obj/MTL/mtlmethods /tmp/quick-mtlmethods.raw`
  → rc=0, `mtlmethods: PASS` (resize checks all MATCH) — `mtlmethods-make.log`,
  `mtlmethods-run.log`.
- `make BACKEND=GL` → rc=0; `qa/flavor-check.sh GL` rc=0 (GL flavor detected via
  OpenGL framework) — `gl-make.log`, `gl-flavorcheck.log`.

## File index

| file | content |
|---|---|
| `pre-fix-loop.sh` / `post-fix-loop.sh` | reproducible loop scripts (per-make log files) |
| `pre-<i>-{VK,MTL}-make.log` | per-make output, PRE-fix loop |
| `pre-<i>-flavorcheck-<exp>.log` | per-iteration flavor check, PRE-fix |
| `pre-loop-summary.log` | PRE summary (4/5 broken) |
| `post-<i>-{VK,MTL}-make.log` | per-make output, POST-fix loop |
| `post-<i>-flavorcheck-<exp>.log` | per-iteration flavor check, POST-fix |
| `post-loop-summary.log` | POST summary (0/15 broken) |
| `sameflavor-{1,2}-make.log`, `sameflavor-flavorcheck-MTL.log` | same-flavor double run |
| `e2e-make.log`, `e2e-run.log` | e2e timed run (rc=124, initWindow OK) |
| `mtlmethods-make.log`, `mtlmethods-run.log` | offscreen GPU self-diagnostic (PASS) |
| `gl-make.log`, `gl-flavorcheck.log` | GL regression (PASS) |
| `final-mtl-make.log` | final `make BACKEND=MTL` leaving repo in MTL flavor |
| `path.txt` | this evidence dir path |