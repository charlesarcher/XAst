# High-Accuracy Review: rendering-abstraction.md

Reviewer: Prometheus (direct verification — all claims cross-checked against source)
Date: 2026-08-19
Plan under review: /home/archerc/code/XAst/.omo/plans/rendering-abstraction.md (459 lines)
Method: line-by-line plan cross-check against verified codebase evidence. Subagent reviewer runs (6) failed on model-backend timeouts; all findings below are from direct file verification.

**Verdict: PLAN NEEDS REWORK — 5 critical findings that would stop an executor or break visual fidelity. Direction is sound; every finding has a concrete fix.**

---

## CRITICAL

### C1 — D11 "engine created on stack in main() before globals" is impossible
- **Fact:** XAsteroids.C:13-30 declares 11 namespace-scope globals (`Stage stage; Button button(stage.display,...); ... PlayingField playingField;`). C++ constructs these during static initialization, BEFORE main() (line 32) begins. `Stage::Stage()` (stage.H:128-130) calls `XOpenDisplay(NULL)` during that static init.
- **Why it breaks:** Task 3 and D11 say "initialize X11Backend before global constructors" / "created on stack in main() before globals initialize". Neither is expressible in C++ — the globals are already built by the time main() exists. Executor hits this at task 3.
- **Fix:** Create the engine inside `Stage::Stage()` immediately after XOpenDisplay + window creation (Stage is global #1, so all later globals can reference it). Expose via `extern RenderingEngine* engine` assigned in Stage's constructor. For GL/Vulkan (no X Display), the engine factory is invoked at the same point but performs GLFW/Vulkan window creation instead — which is exactly what C4 must specify.

### C2 — Build-system contradiction: CMake task vs. a makefile project
- **Fact:** The repo has a 22-line `makefile`, no CMake. Link line: `${LDFLAGS} -lXm -lXt -lX11` (makefile:7). Three object files only.
- **Why it breaks:** Task 26 invents "CMake option: XAst_BACKEND" with zero prior CMake task. D10 says "No change to build structure". Neither is consistent with the other. Worse, **no task adds the link flags the backends need** — task 15 (GLFW/GL) cannot be QA'd without `-lglfw -lGL` and the generated `glad.c` compiled in; phase 4 needs the Vulkan loader. Executor hits this at task 15.
- **Fix:** Replace task 26's location: add build-system task as **first task of Phase 3** (makefile `BACKEND` variable → per-target link: `XAsteroids` (-lXm -lXt -lX11), `XAsteroids-GL` (+ -lglfw -lGL), `XAsteroids-VK` (+ -lglfw -lvulkan), plus compile rules for vendor/glad.c). Or commit to full CMake migration as ONE explicit task. Pick one, delete the other claim.

### C3 — X11 backend strategy is self-contradictory ("thin wrapper" vs. full translation)
- **Fact:** Scope: "X11 backend (thin wrapper preserving exact current behavior)". But task 5 migrates `playingField.H` XCopyArea → `engine->drawTexture()`, and task 10 says "Replace all XLoadQueryFont / XFontStruct usage" (there are 5 XLoadQueryFont, all in stage.H).
- **Why it breaks:** If objects call `engine->drawTexture(tex,...)` on the X11 backend, that backend must re-implement texture semantics from pre-computed pixmaps — i.e., it becomes a re-implementation, not a thin wrapper — and its `TextureId` semantics are undefined throughout the plan. Task 10's font swap makes the "pixel-identical" QA of tasks 4 and 27 **unachievable** inside Phase 1.
- **Fix (explicit design decision D-new):** X11 backend = pass-through: `TextureId` on X11 **is** the Pixmap; `drawTexture()` → `XCopyArea`; the 5 X fonts stay X-native in the X11 backend; `rotatorDisplayData.C`/`compositePixmap.C` are UNCHANGED for X11 (they still produce the pixmaps; the new backends get the same XBM data via `createTextureFromXBM()`). stb_truetype is scoped to GL/Vulkan backends only. Consequence: pixel-identical QA becomes honest and achievable; the "~300 calls" in task 9 shrinks (the .C files drop out).

### C4 — No window/display lifecycle in the API; Stage.H unaddressed for GL/Vulkan
- **Fact:** The API's 19 enumerated methods (D5) include no `createWindow`/`initWindow`/`shutdown`. `Stage::Stage()` (stage.H:128+) creates the X Display, window, 5 fonts, and GCs. Task 4 migrates "X11 calls" in stage.H to engine methods — but the engine has no method that provides a window. Task 15 creates the GLFW window with no task saying what happens to `Stage`'s X11 members (`display`, `window`, `gc*`, `*Info` font structs) on GL/Vulkan.
- **Why it breaks:** Stage is a structural class (layout geometry, font metrics, colors), not just a renderer. The plan never says whether Stage is refactored, shimmed, or duplicated per backend. Every later task implicitly assumes it.
- **Fix:** Add design decision: windowing goes through the engine (`engine->initWindow(w,h,title)` returning nothing; window state owned by backend). `Stage` keeps its geometry/metrics/role; its X11 creation code becomes backend-specific via the engine. Add one task in Phase 1: "Refactor Stage so display/window/font/GC creation routes through RenderingEngine (X11 backend: current behavior; GL/Vulkan stubs until Phase 3/4)".

### C5 — D7 screen-wrap strategy contradicts the actual wrap semantics (ghost copies)
- **Fact:** `Box::WrapMovingBox` (box.H:251-287) wraps **only when the box is fully outside** the opposite edge: `if (EastSide()<=container.WestSide() && vx<0)` → offset by full container width, or `fmod` for multi-screen overspeed. While an object **straddles** the edge (partially on-screen), NO wrap occurs and the original draws it **once**, clipped by the 640×512 pixmap boundary.
- **Why it breaks:** D7's body and tasks 12/20 specify **double-draw** ("draw twice: normal position + offset by ±screenW") for "each object near an edge". During the straddle phase the original shows a single clipped object; double-draw would additionally paint a ghost copy at the opposite edge — a visual artifact the original game does not have. D7's own rationale sentence even contradicts itself: "matches the current X11 approach (which draws at wrapped position)" — that describes single-draw.
- **Fix:** Delete double-draw. GL/Vulkan rendering = single draw at the wrapped box position + **scissor/viewport clip to the play area** (set once per frame in `beginFrame`). Remove `drawTextureWrapped()` from the API (C5 makes it unnecessary); backends clip automatically. Update task 12 to "verify play-area clipping reproduces X11 straddle visuals" and task 20 accordingly.

---

## MAJOR

### M1 — Event-loop rework has no task (the hardest remaining unknown)
- **Fact:** Four `XNextEvent` sites in playingField.H: :333 (main loop), :340 (blocking LeaveNotify spin-wait until EnterNotify), :453 (nested ButtonPress loop blocking until ButtonRelease), :572. GLFW/Vulkan use callback-based events — no nested loops, no blocking spins.
- **Why it breaks:** D8 restructures the render flow but never touches the event model. No task in all 30 covers translating nested/blocking event loops into a main-loop state machine (flags: `inWindow`, `mouseDown`, pending key). Executor discovers this mid-Phase 2/3 with no guidance.
- **Fix:** Add Phase 2 task: "Event translation: map 4 XNextEvent sites to GLFW callbacks + main-loop state machine; de-nest ButtonPress loop into mouseDown/mousePos state; convert LeaveNotify spin into inWindow flag. Acceptance: key press/release, mouse buttons, window enter/leave behave identically to X11 backend."

### M2 — Frame pacing is dynamic and user-configurable; D4 and D9 underspecify it
- **Fact:** `uSecondsPerFrame=62500` (playingField.H:98), pacing: `if (diffTime<uSecondsPerFrame) usleep(uSecondsPerFrame-diffTime);` (:504-505) — diffTime-compensated, not a fixed sleep. Options dialog **changes it at runtime** (FPS scale widget, options.H:729; callback → `AlterFramesPerSecond` options.H:3086-3088 → playingField.H:672 `uSecondsPerFrame=newUSecondsPerFrame; speedAdjust=...`).
- **Why it breaks:** D4 says "retain `usleep(62500)`" (inaccurate mechanism) and QA asserts "frame rate stable at 16fps" — but 16fps is the *default*, a user setting. D9 defers Options for GL/Vulkan, which **silently freezes frame-rate configurability** there — a user-visible feature loss the plan never states.
- **Fix:** D4: "Preserve the uSecondsPerFrame + diffTime compensation mechanism verbatim; glfwSwapInterval(0)". D9: add explicit consequence: "On GL/Vulkan, frame-rate option is unavailable until Options is ported; game runs at default 62500µs". QA criterion: "default frame period 62.5ms; pacing responds to uSecondsPerFrame on X11".

### M3 — Core-profile/Vulkan line width limits make `drawLine(width)` unimplementable as specified
- **Fact:** GL 3.3 **Core Profile** removes/ignores `glLineWidth` for widths >1 (compatibility feature only). Vulkan guarantees `lineWidth == 1` (anything else is undefined/not rasterized). The game uses line widths 1 **and 3** (button.H `XSetLineAttributes`).
- **Why it breaks:** Tasks 16/23 accept "lines drawn correctly" — a 3-px button bevel cannot be produced by GL_LINE_LOOP in 3.3 core, nor by a Vulkan LINE pipeline.
- **Fix:** Note in tasks 16/23: "Lines with width >1 rendered as thin filled quads / polygon outlines (2 triangles per segment, or expanded edge loop). Width-1 lines use GL_LINE_STRIP / VK_LINE."

### M4 — Vulkan phase omits command submission & sync, and both phases omit dependency vendoring
- **Fact:** Tasks 21-25 cover instance/swapchain, render pass/framebuffers, pipelines, "methods", "text+textures+rotation+wrap". Missing: command pool, per-frame fences, image-acquire semaphores, `vkQueueSubmit`, `vkQueuePresentKHR` — the highest-bug-density Vulkan code. Task 22's acceptance ("window clears to solid color") cannot pass without submission/sync, so the gap surfaces as unexplained failure. Also: no task anywhere vendors GLFW, generated `glad.c`, `stb_truetype.h`, or the Vulkan loader/SDK.
- **Fix:** Add Phase 3 task: "Vendor dependencies: GLFW (header+lib), generate glad 3.3 core, stb_truetype.h, Vulkan headers+loader; add to build (C2 task)". Add Phase 4 task: "Frame sync: command pool, fences, acquire/semaphores, triple-buffered submit/present. Acceptance: 60+ sustained frames without vkAcquireNextImage timeout / GPU hang."

### M5 — Task 9 is a mega-task with a wrong call count and unassigned files
- **Fact:** Measured rendering X11 calls: **93 across 15 files** (rotatorDisplayData.C 18, button.H 18, playingField.H 17, shipGroup.H 7, shipYard.H 6, explosionGraphic.H 5, stage.H 4, rockGroup.H 4, bullet.H 3, options.H 3, shipBulletGroup.H 2, explosion.H 2, enemyGroup.H 2, compositePixmap.C 1, enemyBulletGroup.H 1) — not "~300". Task 9 bundles ≥7 files and assigns **no home** to rotatorDisplayData.C / compositePixmap.C / explosion.H / options.H bitmaps (options.H:2450,2474 XCreateBitmapFromData).
- **Fix:** Under C3's decision, the two .C files are X11-only/unchanged — state that explicitly. Split task 9 into per-file rows (one per remaining .H: rockGroup, shipGroup, enemyGroup, enemyBulletGroup, bullet, shipBulletGroup, explosion). Fix the count to 93.

### M6 — "12 methods" is wrong; the API enumerates 19 pure virtuals
- **Fact:** D5 lists: beginFrame, endFrame, clear, drawLine, drawPolygon, drawRect, drawString, measureText, getFontMetrics, createTextureFromBitmap, createTextureFromXBM, drawTexture, drawTextureMasked, deleteTexture, pushTransform, popTransform, rotate, translate, drawTextureWrapped = **19**. TL;DR, Scope, D5 title, tasks 1 and 2 all say "12".
- **Fix:** State 19 (or trim the API and say what was cut — note C5 removes drawTextureWrapped → 18; consider whether translate/rotate/pushTransform/popTransform are needed as 4 separate methods vs. `setTransform(mat2)` — the game only composes translate∘rotate per object).

### M7 — Backend selection (task 26) sits in Phase 5; executor needs it at Phase 3 start
- **Why it breaks:** Without a build path, the GL backend of Phase 3 cannot be compiled or QA'd; task 27's "run all 3 backends" is also unreachable until Phase 5.
- **Fix:** Fold selection into the Phase 3 build-system task (C2).

---

## MINOR

- **m1** — D7 heading says "Vertex-shader wrapping", body/tasks say double-draw: naming contradiction (superseded by C5 fix — single draw + scissor).
- **m2** — `drawTextureWrapped(tex,x,y,w,h,alpha,screenW,screenH)`: backends already know their dimensions; parameter bloat (removed by C5).
- **m3** — `drawTextureMasked(content, mask, x, y)` has no size params while `drawTexture` has w/h; also 1-bit masks should be uploaded as single-channel textures (e.g., GL_R8 / Vulkan R8) with `discard` in the fragment shader — state the format in task 14.
- **m4** — Task 4 QA "pixel-identical" is only honest under the C3 fix (X fonts retained in X11 backend). Make the criterion conditional on that decision.
- **m5** — Success criterion 2 "visual output identical across backends (within font metric tolerance)" should be explicit: glyph shapes WILL differ across backends (different font sources); the honest criterion is "layout metrics within 10%, glyph shapes visually equivalent, non-text pixels identical".

---

## Carried over from round-1 adversarial review (still valid)

- Round-1 critical #2 (rotation model) — addressed by D2 hybrid; verify executor follows per-subclass mapping during task 19.
- Round-1 critical #3 (API gaps: measureText/getFontMetrics/fill ops) — addressed by D5; counts still need M6 fix.
- Round-1 #4 (event handling "not a black box") — still open; this is M1.
- Round-1 #8 (Motif dialog coupling) — now explicit in D9; M2 adds the missing frame-rate consequence.
- Round-1 #6 (stencil wrong tool) — fixed by D6 (texture masks). Confirmed correct: masks are pre-computed 1-bit data (explosionGraphic.H:94-100, rotatorDisplayData.C:137).

---

## Score

| Dimension | Rating |
|---|---|
| Structure (canonical plan) | PASS |
| Scope realism | PASS (5-6 weeks reasonable once C2/M5/M7 sequencing fixed) |
| Design correctness | FAIL (C1, C3, C4, C5) |
| Build/compile viability | FAIL (C2, M4) |
| Visual-fidelity strategy | FAIL (C5, M3; M2 partial) |
| Completeness | PARTIAL (M1, M4, M5 gaps) |

## Required before execution
C1, C2, C3, C4, C5 (mandatory), then M1–M7, then m1–m5. Estimated rework: ~1.5 hours of plan editing; no scope changes (Vulkan and GL remain essential deliverables per user direction; ~30 tasks may become ~34 after the splits/additions).
