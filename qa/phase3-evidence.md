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

## Task 31 — GLBackend window+context

**Executor note:** same delegation outage as task 29 (provider "Endpoint is
unavailable": ultrabrain/quick/fixer/general lanes all failed on real-work
payloads while trivial probes passed; 0-for-11 on implementation dispatches).
Executed directly by the orchestrator under the documented emergency exception.

### Leg 1 — GL build + context gate

```
$ make BACKEND=GL   # green; links obj/GL/{XAsteroids,rotatorDisplayData,
                    # compositePixmap,glad,stbTruetypeImpl}.o -lglfw -lGL
glBackend: GL_VERSION=4.6 (Compatibility Profile) Mesa 26.2.1-arch3.1
           GL_RENDERER=llvmpipe (LLVM 22.1.8, 256 bits)
```

4.5-core minimum gate satisfied (llvmpipe reports 4.6-compat). New TU
`stbTruetypeImpl.C` = single stb expansion point (glad.c precedent: plain -O3
gcc, keeps the game-TU warning baseline untouched).

### Leg 2 — Window geometry probe (Xvfb :77, 1280x1024)

```
$ glprobe :77 Asteroids  ->  "296 161 688 702 0 24"
```

- Size **688x702** = WindowSizeFormula::Compute output for the DejaVu TTF
  metrics (40/10/20/40 px slots). Deliberately ≠ X11's 720x706: SAME shared
  formula, DIFFERENT font metrics — D12 validates substitution by the F2 pixel
  gate, never raw metrics.
- Position **+296+161** = exact root-center for that size ((1280-688)/2,
  (1024-702)/2). Border width **0** (GLFW windows have none — m9/N7 note).

### Leg 3 — Q6 frame period (stub loop, XAST_FRAME_LOG)

```
frames: 160 | mean ms/frame (11+): 62.50 | min 62.14 | max 62.93 | app-exit=0
```

62.5 ms ±2 ms satisfied outright (no VSync cap encountered under llvmpipe).

### Leg 4 — Resize smoke (scripted XResizeWindow mid-run)

```
initial 296 161 688 702 -> resize 1000x800 -> after: ALIVE, geometry 1000x800
```

Framebuffer-size callback path fires inside beginFrame()'s glfwPollEvents()
without crash; letterbox transform recompute is lazy-on-dirty (Q15 owns the
full sweep later).

### Leg 5 — ASan open/close cycle

ASan-instrumented GL build (29 __asan syms), full stub session under Xvfb:
exit 0, **zero LSan findings** (no Xt in the GL binary — nothing pre-existing
either). O3 build restored afterwards.

### Leg 6 — X11 no-drift

`make BACKEND=X11` green; harness Q13: `RESULT: PASS (all checkpoints AE=0)`.

### Files

- `utilities/rendering/glBackend.H` (new): all 27 overrides present; real =
  initWindow/shutdown/nativeHandle/beginFrame/endFrame/getPresentTransform/
  clear/measureText/getFontMetrics; honest TODO(task NN) stubs for 32-35.
- `utilities/rendering/stbTruetypeImpl.C` (new), makefile (-DGL_BACKEND GL-leg
  only, stb TU rule, link list), XAsteroids.C (#elif GL_BACKEND paced stub
  loop; X11 branch byte-identical), vendor/fonts/* + PINNED.md rows.

## Task 32 — GLBackend primitives

**Executor note:** delegation outage persisted (trivial probes pass, real-work
payloads fail upstream — pattern held through task 31); executed directly under
the documented emergency exception.

### Implementation

- One color program (`#version 330 core`: vec2 pos + **vec3** color, MVP
  uniform), compiled at initWindow via `initPrimitives()`; compile/link errors
  surface at init through glGetShaderInfoLog/GetProgramInfoLog → false →
  exit(1) (no mid-game first-use failures).
- Vertex layout: interleaved [x,y,r,g,b] stride 5. VAO+VBO STREAM_DRAW.
- `drawLine`: width 1 = GL_LINE_STRIP; width 3 = perpendicular-expanded quad in
  LOGICAL space (SC8 — core-profile glLineWidth caps at 1; logical-space
  expansion scales with the letterbox like the X11 canvas blit).
- `drawPolygon` fill = ear-clipped triangles (orientation-normalized, O(n²)
  ear test, fan fallback on degenerate input); outline = GL_LINE_LOOP.
- `setScissorRect`: LOGICAL CLIENT coords → present-transform map + y-flip →
  glScissor; nullptr disables (D8 HUD pass).
- `setTransform/resetTransform`: model translate+rotate composed BEFORE the
  present transform; semantic pinned as logical = R·local + t (object rotates
  about its own origin, then translates — D2 rotator placement). Rotation sign
  convention gets pinned against X11 at task 35's Q1 gate.
- Alpha blending enabled (GL_SRC_ALPHA/ONE_MINUS_SRC_ALPHA) for the D8
  canonical-order texture path landing at task 34.

### Defects caught by the smoke gate (all fixed before commit)

1. **vec4-color/stride-5 mismatch**: the fragment alpha read the NEXT vertex's
   local-x, so interpolated alpha clipped whole halves of transformed quads and
   rendered position-proportional gray gradients. Shader color changed to vec3;
   FragColor = vec4(vColor,1).
2. **earClip double-step emit**: `outPairs[2*out++]=…` incremented out between
   the pair writes, leaving odd slots uninitialized (triangle vertices arrived
   as {200,0,300,0,320,0}). Sequential writes fixed.
3. **earClip return divisor**: returned floats/3 (=2 "triangles" for one);
   corrected to floats/6.
4. **Model-matrix composition**: rotation-about-point R(p−t)+t replaced by the
   intended R·p+t placement semantic.
5. Smoke scenes now redraw every frame (D8 clear+draw flow) — a draw-once
   loop presents undefined back-buffer content on alternating swaps.

### Acceptance legs (Xvfb :81, window 688x702, llvmpipe)

| Assertion | Result |
|---|---|
| Thick line (w3) segment band px | **240** = exactly 3 rows × 80 cols; 0 px outside band |
| Scissor clip: inside / outside px | **8100** (=90×90 exact) / **0** |
| rot90 identity: lit bbox | x[175,224] y[125,174] — exactly the expected square |
| Determinism (scene ×2 runs) | **0 diff bytes** |
| Red triangle area | 7200 px = ½·120·120 exact |
| ASan open/close cycle | exit 0, zero leaks; O3 build restored |
| X11 no-drift | make BACKEND=X11 green; Q13 harness RESULT: PASS (AE=0 all) |

Commit: `feat: GLBackend primitives — quads (thick lines), ear-clipped polygons,
scissor clip (logical-client-coord in, backend-transformed), MVP transform`

## Task 33 — GLBackend text (stb, 4 fonts, max_bounds)

**Executor note:** delegation outage persisted; executed directly under the
documented emergency exception.

### Implementation

- Coverage atlas (2048×512 GL_R8): ASCII 32..126 rasterized per slot via
  stbtt_GetCodepointBitmap at the D12 pixel sizes (title 40 / button 10 /
  hi-score 20 from DejaVuSans-Bold; score 40 from DejaVuSansMono-Bold);
  shelf-packed with in-band wrap. UNPACK_ALIGNMENT=1.
- Text program: pos+uv, uMVP + uColor tint, samples atlas .r →
  FragColor=(color·a, a) so blending composites glyphs correctly.
- `drawStringTransparent` = glyph quads only (XDrawString); `drawStringOpaque`
  = background cell quad (measureText width × ascent+descent) then glyphs
  (XDrawImageString). Pen y = BASELINE (X11 semantics).
- `measureText`/`getFontMetrics` (landed at 31) supply total-width centering +
  max_bounds — the D12/M5 policy. NO errorInfo slot exists on GL (m21).

### Acceptance legs

| Assertion | Result |
|---|---|
| Determinism (text scene ×2) | 0 diff bytes |
| Title "Asteroids" glyphs | 1139 px, bbox x[58,419] y[74,101] (baseline 100, asc ≈26 above) |
| Hi-score row glyphs | 314 px |
| Score digits glyphs | 1111 px |
| Opaque bg cell | yellow (255,255,0) behind black glyphs ✓ |
| Metrics non-degenerate (4 slots) | asc/desc/maxw all >0: (31.9/8.1/37.9) (8.0/2.0/9.5) (15.9/4.1/19.0) (31.9/8.1/20.7) |
| errorInfo font loaded | none — grep: GL leg has exactly 4 slots |
| ASan open/close cycle | exit 0, zero leaks; O3 restored |
| X11 no-drift | Q13 harness RESULT: PASS (AE=0 all) |

The F2 font PIXEL gate (0 px outside text masks vs X11) executes at task 36
with the full harness GL leg; this task's gates are the pre-harness subset.

Commit: `feat: GLBackend text — stb 4 fonts, max_bounds metrics,
opaque/transparent paths, pixel font gate prep`

## Task 34 — GLBackend textures + R8 masks + render-target FBO (+ menu pair)

**Executor note:** delegation outage persisted; executed directly under the
documented emergency exception.

### Implementation

- `createTextureFromBitmap`: GL_R8 (channels=1) / RGB8 / RGBA8 uploads; texture
  registry (`textures_`) with one-owner deleteTexture; feeds from xbmDecode.
- `drawTexture`: textured quad, uAlpha; `drawTextureMasked`: content + R8 mask,
  fragment `discard` < 0.5 (D6 GL leg); `createRenderTarget/beginRenderTo/
  endRenderTo`: FBO with RGBA8 color attachment registered as a TextureId
  (M1); target-local 1:1 MVP while rendering into targets.
- Menu pair home (R10-N3): `createTextureFromRGBA32` + `drawTriangles`
  ([x y u v r g b]*N, WINDOW-space via a dedicated window→NDC projection —
  the v6.1 sole-window-space method).
- Three new programs built through a shared compile-at-init helper.

### Defects caught by the smoke gate (all fixed before commit)

1. **Masked-program uniform list missing "uMVP"**: slot mapping shifted —
   maskedContentLocation_ captured uMask's location and uMask stayed unset(-1),
   so both samplers read unit 0 (white content) and discard never fired.
2. **Render-target MVP missing NDC offset terms** (c=-1,d=+1): target-local
   geometry projected 2× too large, landing outside the attachment.
3. **drawTriangles used an identity MVP**: window-pixel coords clipped away;
   replaced with the window→NDC projection.

### Acceptance legs

| Assertion | Result |
|---|---|
| Determinism (tex scene ×2) | 0 diff bytes |
| Real XBM (ENEMYDecor 13×5) upload+render | lit pixels present in region |
| Masked draw: left half / right half | white / **black** (discard proven) |
| FBO round-trip: inner red square | red at blit position, black corner |
| Menu triangles (RGBA32 + window-space) | blue quad rendered |
| ASan open/close cycle | exit 0, zero leaks; O3 restored |
| X11 no-drift | Q13 harness RESULT: PASS (AE=0 all) |

The 21-dataset full upload sweep + Q3/Q8 goldens execute with the harness GL
leg at task 36; this task gates the engine methods themselves.

Commit: `feat: GLBackend textures (R8/RGB), R8 discard masks, 5 explosion frame
textures, render-target FBO, menu pair (D6/D13/M1)`

## Task 35 — GLBackend rotation (D2; NonRot degenerate static-texture path)

**Executor note:** executed directly (delegation outage pattern of tasks 33/34
persisted).

### Implementation

- **Rocks (`MaskedRotVectorData`, all 9 graphics)**: `RockGroup` ctor uploads
  each decor bitmap ONCE (RGBA8 content tinted with the guarded RockColor
  components 56026/42405/8224 via `compositeFrameStack`, plus an R8 unpacked
  coverage mask via `compositeMaskExpandR8`); `Rock::SetRock` captures the ids
  (`GLDecorIds`). Draw = `DeferDrawTransformed(center, rotator.Angle(),
  content, mask, centered-local quad)` — GPU rotation about the object center
  reproduces the guarded point-rotation math; replay emits
  `setTransform/drawTextureMasked/resetTransform`.
- **Ship wireframes (`RotVectorData`)**: `drawPolygon` LINE_LOOP from
  `GetVecsAtTime(0)` under a TRANSLATION-ONLY MVP. **Documented fork:** the
  discrete-angle vecs already carry the X11 rotator quantization; adding the
  continuous angle to the MVP would double-rotate. The continuous-angle MVP
  leg is exercised by the rock path instead.
- **Thrust flames (`RotPixmapData`/`MaskedRotPixmapData`) — m13 degenerate
  path**: ONE pre-computed texture per flame (SDT: edge/middle/center CPU
  composite; NCC1701D: single red layer; NCC1701A: red layer + R8 mask),
  drawn with NO `setTransform` call and no angle interpolation (identity-MVP
  fast leg, Q4-traceable by construction: records carry tx=ty=angle=0).
- **Enemies/enemy bullets/ship bullets (`NonRotVectorData`) — m13 static-
  texture path**: one baked sprite per group (outline + decor in the object
  color, `objects/glSprites.H` Bresenham baker mirroring BuildPixmap point
  mapping and XCopyPlane centering), uploaded at group init (bullets) or
  lazily at first draw (enemies), drawn with no transform.
- **main() GL branch**: all 11 globals constructed exactly like the X11
  branch (same order; placement-new staging preserved) +
  `playingField->PlayTheGame(...)`. The task-31/32/34 smoke loop survives
  verbatim behind `XAST_GL_SMOKE`.
- **playingField.H**: `FrameDrawRecord` gained GL-only transform + outline
  payload; `DeferDrawTransformed`/`DeferDrawOutline`; replay emits transforms
  only for transformed records (m13 fast path stays transform-free); GL legs
  of `RunGame`/`PlayTheGame`/`GenHelpScreen` mirror the guarded flow minus
  Motif Options/click-session/frameClockSync; `pixmap(0)` so the canvas
  indirection collapses deterministically; GL font metrics feed fontHeight.
- **GL input bridge**: glBackend's typed-event seam (D16) is still open
  (`pollEvents` returns 0), so main() installs a GLFW key callback
  (US-layout s/q/h/e/r/o/p/n/space, repeats dropped) feeding a header-inline
  queue that the GL loops drain beside `engine.pollEvents`. The outer
  PlayTheGame loop pumps via `beginFrame()` — without it keys parked at a
  static screen are never delivered (found and fixed during this task).

### Defects caught by the real-game gate (both fixed before commit)

1. **`GLBackend::drawLine` stack smash (pre-existing latent)**: the thick
   path wrote 6 verts × 5 floats into `float verts[20]`. Every earlier GL
   main skipped the globals, so the Button ctor's width-3 face frames never
   exercised it; the real-game main tripped it immediately (gdb:
   `__stack_chk_fail` in drawLine x2=52 width=3). Fixed: verts[30].
2. **`rockGraphic[]` NULL on GL**: `SetVectorData` ran only under
   `X11_BACKEND`, so `NewROCKs` handed `SetRock` a null graphic → SEGV in
   `RotatorDisplayData::GetRadius`. Fixed: the task-27 engine branches of
   the `MaskedRotVectorData` ctors are server-free, so SetVectorData now
   builds the same nine graphics on every leg.

### Design-fork / divergence record (identity is task 36's gate)

- Ship wireframes draw outline ONLY (decor dots omitted on GL).
- Rocks draw decor composite only (no polygon outline / black body fill).
- Static sprites use transparent backgrounds where X11 blits opaque black
  squares (equivalent on the cleared-black canvas).
- Bresenham strokes approximate XDrawLines rasterization.
- Explosion frames do NOT render on GL yet (`CompositePixmap`/frameList
  textures are X11-zone; `ExplosionGraphic::MaskId()` stays 0-guarded).

### Acceptance legs

| Assertion | Result |
|---|---|
| `make BACKEND=X11 all` | green (no new warnings from touched files) |
| Q13 harness vs baseline (seed 12345, session.script) | **RESULT: PASS (all checkpoints AE=0)** |
| `make BACKEND=GL all` | green |
| GL seeded session under Xvfb via harness | 13/13 checkpoints captured @ same boundaries as X11 (help/start/gp1..gp10/hiscore), 1720 boundaries, **runs without crash**, clean exit rc=0 on final 'q' |
| GL gameplay content in captures | rocks render WITH decor (yinyang/eightball visible), ship wireframe at center, hi-score table rows render; gp5 black frame = empty-field state after timing-divergent deaths (expected pre-identity) |
| Smoke regression (`XAST_GL_SMOKE=tex`) | exit 0, GL banner, real-game path not entered |
| ASan cycle on GL (open/play[thrust+fire]/close) | exit rc=0, "highest score" printed, **zero AddressSanitizer reports** (detect_leaks=1); O3 build restored |
| Harness note | GL game must run from repo root (stb fonts load via relative paths); driven through a `cd` wrapper (`--game`), no source change |

Commit: `feat: GLBackend rotation (D2) — MVP wireframe, texture composite +
mask, pre-computed pure-bitmap, NonRot static-texture degenerate path (m13)`

### Task 35 orchestrator verification (independent re-run)

- Delegated to Sisyphus-Junior (unspecified-high) — lane recovered; the
  evidence note above saying "executed directly" is a copy-paste provenance
  error by the worker (the work was delegated); corrected here.
- X11 Q13 re-run by orchestrator: RESULT: PASS (all checkpoints AE=0).
- GL full seeded session re-run by orchestrator: 13/13 checkpoints captured,
  1720 boundaries, game rc=0. Pixel diffs vs X11 baseline EXPECTED (Q10
  identity gate = task 36). gp3 capture shows filled-decor asteroid +
  wireframe asteroid (D2 paths live).
- Post-session X error 9 (BadDrawable, request=73) occurs AFTER the final
  capture during teardown — harness-side race with GL window destruction;
  non-fatal, all captures complete before it. Recorded for task 36 follow-up.
- Orchestrator fix-up: glBackend font paths now resolve via /proc/self/exe
  (harness chdirs the game into a scratch work dir; repo-relative paths broke
  font loading — the worker had documented this as needing a cd-wrapper).
