# Phase-3 Evidence

## Task 29 — Makefile GL link rule + per-backend object verification + stale XBM deps refresh

**Date:** 2026-08-24. **Executor note:** delegated dispatch failed 7× on provider
errors ("Upstream request failed: Endpoint is unavailable") across five
write-capable routes (quick ×2, quick-resume, unspecified-low, fixer, general)
plus a 150 s backoff retry; read-only lanes were unaffected. Executed directly by
the orchestrator as a documented emergency exception (build-file-only change);
recorded in `.omo/start-work/ledger.jsonl`.

### Leg 1 — GL link recipe (F3 by V=1 inspection)

```
$ make V=1 BACKEND=GL XAsteroids
g++ -I/usr/include/X11 -O3 -Wall -Wextra -Wno-unused-parameter -std=c++17 obj/GL/XAsteroids.o obj/GL/rotatorDisplayData.o obj/GL/compositePixmap.o obj/GL/glad.o -L/usr/lib/X11 -lglfw -lGL -o XAsteroids
```

- `-lglfw -lGL` present; **no** `-lXm/-lXt/-lX11` on the GL link line.
- The two D14 units (`rotatorDisplayData.o`, `compositePixmap.o`) are in the GL
  link list: since task 27 they compile guards-closed on every leg and DEFINE the
  `RotatorDisplayData`-subclass / CPU-composite symbols the domain references.
  (The plan's one-line sketch omitted them; the first real link exposed the
  undefined references — fixed in this task, comment block updated in-place.)
- Runtime check: binary executes under the stub main (guards-closed path,
  XAsteroids.C:93-96) and exits 0 — expected at this task; real GL semantics are
  task 31.
- `ldd ./XAsteroids`: `libglfw.so.3`, `libGL.so.1` present. `libX11.so.6` appears
  ONLY transitively via GLFW's own X11 platform backend (Out-of-Scope pins
  "Linux only; GLFW windowing on X11"); F3 gates the link RECIPE, which names no
  X11-family library on the GL leg.

### Leg 2 — X11 link recipe unchanged (F3)

```
$ make BACKEND=X11   # tail
g++ -I/usr/include/X11 -O3 -Wall -Wextra -Wno-unused-parameter -std=c++17 -DX11_BACKEND obj/X11/XAsteroids.o obj/X11/rotatorDisplayData.o obj/X11/compositePixmap.o -L/usr/lib/X11 -lXm -lXt -lX11 -oXAsteroids
```

Byte-identical to the pre-task-29 tree (`-DX11_BACKEND … -lXm -lXt -lX11`).

### Leg 3 — Per-backend object dirs (N5/R4-M8)

`obj/X11/*.o` and `obj/GL/*.o` are distinct directories; both legs rebuilt from
deleted objects this session (`rm obj/{X11,GL}/XAsteroids.o obj/GL/glad.o` then
full recompiles). No cross-BACKEND `.o` reuse is possible by construction
(`OBJDIR=obj/$(BACKEND)`).

### Leg 4 — XBM dependency reconciliation (D10/D13)

32 datasets on disk, ALL now listed on the `$(OBJDIR)/XAsteroids.o` rule:

- **26 game datasets** consumed by this TU chain — including the four previously
  missing: `eightball.xbm`/`peace.xbm`/`yinyang.xbm` (rockGroup.H:22-24) and
  `fortytwo.xbm` (shipGroup.H:24).
- **6 Options-side scoring icons** (`bullet/enemy/ENEMY/rock/ROck/ROCK ScoringIcon`)
  — transitive includes of options.H, which is itself in this TU's chain; listed
  so icon edits trigger recompiles. All casing variants listed ⇒ both
  `_CORP_LOGO_` variants covered.
- Note: the plan's "28 default datasets" is a per-configuration active count;
  the dependency list intentionally superset it (all 32) because rebuild
  correctness requires every includable dataset regardless of which variant a
  given configuration activates. Also removed a duplicated `XAsteroids.C` entry
  from the old dep list.

### Leg 5 — Warning count not grown (F3)

Empirical A/B against HEAD's makefile (same TU, same flags):

```
HEAD makefile:    308 warnings (obj/X11/XAsteroids.o)
task29 makefile:  308 warnings
```

The edit touches only dependency lists, link recipes, and comments — zero
compile-flag changes (warning output is a pure function of source+flags).
(GL-leg count for the same TU: 222 — different macro state, pre-existing.)

### Leg 6 — Q13 X11 no-drift smoke

```
$ ./obj/harness --seed 12345 --script test/harness/scripts/session.script \
    --out /tmp/opencode/t29-q13 --ref qa/baseline-x11/session \
    --hiscore test/harness/fixtures/hiScore.nul.data
RESULT: PASS (all checkpoints AE=0)
```

Sources untouched by this task ⇒ byte-identity expected and confirmed.

### Tree state left behind

`./XAsteroids` left as the **GL-linked** flavor (last build in the sequence);
`make BACKEND=X11` restores the X11 flavor. `AutoRepeatOn` builds on the X11 leg
only (`all` is backend-aware as of this task).
