# Hyperplan Review: XAst Rendering Abstraction

**Review Type:** 5-Critic Adversarial Review (Architecture Skeptic, Implementation Pragmatist, Risk Hunter, Code Archaeologist, Minimalist)
**Plan Under Review:** `.omo/plans/rendering-abstraction.md`
**Date:** 2026-08-20
**Status:** FINDINGS REQUIRE PLAN REVISION

---

## Executive Summary

The plan is architecturally sound in its high-level design (abstract interface + backend implementations), but contains **3 critical**, **16 major**, and **~15 minor** findings across factual errors, missing scope, and over-engineering. The most impactful finding is that **Phase 0 header-chain refactoring is massively insufficient** -- 16+ header files with bare `#include<X11/Xlib.h>` will prevent GL/VK builds when `-lX11` is dropped, but only 3 files are guarded. The plan also contains factual errors in call-site counts (T10, T11) and underestimates the scope of several migration tasks.

**Recommended action:** Revise plan to address all CRITICAL and MAJOR findings before execution. Consider deferring Vulkan to a separate project (biggest scope reduction: ~9 tasks).

---

## CRITICAL Findings

### C1: Phase 0 Header Refactoring Is Massively Insufficient
**Sources:** Architecture Skeptic #1, Implementation Pragmatist #1
**Severity:** CRITICAL -- blocks the entire GL/VK build strategy

Phase 0 (T1-T4) guards Motif/Xt includes in 3 files: `options.H`, `button.H`, `rotatorDisplayData.C`. But Phase 0 Task 4 drops `-lX11` from GL/VK link flags. Without `-lX11`, **16+ header files** with bare `#include<X11/Xlib.h>` will cause compilation failure:

Files with unguarded X11 includes:
- `stage.H:11`, `playingField.H:18` (also `X11/Xutil.h`), `shipYard.H:4`
- `shipGroup.H:2`, `shipBulletGroup.H:2`, `rockGroup.H:4`
- `enemyGroup.H:5`, `enemyBulletGroup.H:2`, `bullet.H:11`
- `explosionGraphic.H:4`, `explosion.H:8`, `movableObject.H:8`
- `compositePixmap.H:4`, `rotatorDisplayData.H:5`, `rotator.H:5`, `frameList.H:4`

Note: Phase 0 Task 3 guards `rotatorDisplayData.C` but **not** `rotatorDisplayData.H`, yet the `.H` is what propagates the include to all consumers.

**Fix:** Create a `x11types.h` shim that provides opaque typedefs (`typedef void* Display; typedef unsigned long Pixmap;` etc.) behind `#ifndef X11_BACKEND`, and replace all 16+ bare includes with `#include "x11types.h"`. Alternatively, restructure Phase 0 Task 4 to only drop `-lXm -lXt` and defer `-lX11` removal to end of Phase 1.

---

### C2: T10 XDrawString Count Is Wrong (14 -> 13)
**Sources:** Implementation Pragmatist #2, Code Archaeologist
**Severity:** CRITICAL -- factual error in task specification

Verified by counting every `XDrawString` in `playingField.H`:
- `RunGame` area (lines 525, 528, 530, 532): **4 calls**
- `GenHelpScreen` (lines 647, 649, 651, 653, 655, 657, 659, 661, 663): **9 calls**
- Total: **13, not 14**

**Fix:** Change T10 from "14 XDrawString" to "13 XDrawString".

---

### C3: T11 XCopyArea Count Is Wrong (3 -> 4) and CreateButton Attribution Is Wrong
**Sources:** Implementation Pragmatist #3, Code Archaeologist, Minimalist #6
**Severity:** CRITICAL -- factual error in task specification

Verified in `button.H`:
- `PressButton` (line 236): 1 XCopyArea
- `ReleaseButton` (line 248): 1 XCopyArea
- `DrawButton` (lines 261 AND 269, if/else branches): **2 XCopyArea**
- Total: **4, not 3**

Additionally, the plan's parenthetical "CreateButton:14 init" is wrong -- `CreateButton()` has **zero** XCopyArea calls.

**Fix:** Change T11 from "3 XCopyArea (CreateButton:14 init, PressButton:1, ReleaseButton:1, DrawButton:1)" to "4 XCopyArea (PressButton:1, ReleaseButton:1, DrawButton:2)".

---

## MAJOR Findings

### M1: Button's Off-Screen Pixmap Rendering Has No API Equivalent
**Source:** Architecture Skeptic #2

`button.H` creates two off-screen Pixmaps (`buttonUp`, `buttonDown`), draws 3D bevels onto them, then blits them via XCopyArea. The 20-method API has **no render-to-texture concept**. On GL/VK, Button needs either:
1. Render-to-texture (FBO) -- not in API
2. Procedural drawing each frame
3. Keep Button X11-only -- contradicts D1 "co-equal backends"

**Fix:** Add `createOffscreenTarget(w,h) -> RenderTargetId` + `beginRenderTo(target)` + `endRenderTo()` to API (now 22 methods), OR redesign Button to render procedurally each frame on GL/VK.

---

### M2: D11 "Unchanged Signatures" Claim Is Incorrect
**Source:** Architecture Skeptic #3

The plan states constructors keep "(unchanged signatures)" but `Button(Display*, Drawable&, ...)`, `ShipYard(Display*, Drawable&, ...)` take X11-specific parameters that don't exist on GL/VK. The signatures **must** change to accept `RenderingEngine*` or use the global `engine` pointer.

**Fix:** Rewrite D11 to specify constructor signatures are refactored, removing all X11-type parameters.

---

### M3: XAutoRepeatOn/Off Not Addressed for GL/VK
**Sources:** Architecture Skeptic #4, Risk Hunter

7 sites across `playingField.H` (lines 338, 342, 468, 477, 559, 577, 579) use `XAutoRepeatOn`/`XAutoRepeatOff` for gameplay control. GLFW has **no auto-repeat control API**. The D16 event translation section does not mention this.

**Fix:** Add to D16: implement software debounce for GL/VK -- track key-down timestamp and suppress repeat events within a configurable threshold (e.g., 200ms).

---

### M4: ShipYard Resource Lifecycle Missing from T12
**Sources:** Architecture Skeptic #5, Implementation Pragmatist #8

T12 only lists rendering calls (4 XFillRectangle + 2 XCopyArea) but omits the **resource lifecycle** that must also be migrated:
- `XCreatePixmapFromBitmapData` x2, `XCreatePixmap` x3, `XFreePixmap` x5
- `XAllocColor` x3, `XCreateGC` x1, `XFreeGC` x1, `XSetForeground`/`XSetBackground` x6

On GL/VK, ShipYard needs a full rewrite to use engine textures. T12's task description is woefully incomplete.

**Fix:** Rewrite T12 to describe a full ShipYard refactoring, not just rendering call migration.

---

### M5: Button Uses Per-Character lbearing, API Returns Font-Level lbearing
**Source:** Architecture Skeptic #6

`button.H:137` calls `XTextExtents` on a **single character** for per-character lbearing. The API's `getFontMetrics()` returns font-level lbearing. For monospace bitmap fonts these are equivalent, but for proportional TTF fonts on GL/VK, Button labels will render at wrong positions.

**Fix:** Either (a) require the 5 TTF substitutions to be monospace, (b) add `measureChar(fontId, char) -> CharMetrics`, or (c) change Button centering to use `measureText()`.

---

### M6: Options Dialog Coupling Is Far Deeper Than nativeHandle()
**Source:** Architecture Skeptic #7

Options directly accesses 10+ extern globals beyond Display*/Window: `playingField.WM_DELETE_WINDOW`, `stage.windowBg`, `stage.shipYardBg`, `stage.buttonInfo`, `stage.errorInfo`, `shipGroup.*`, `score.*`, etc. The plan's description of nativeHandle as the sole seam is misleading.

**Fix:** Document the full set of extern globals Options depends on in D9. Add a pre-refactoring checklist ensuring these globals remain stable throughout Phase 1-2.

---

### M7: T18 Misses 4 XCreateBitmapFromData Call Sites
**Source:** Implementation Pragmatist #4

T18 only lists `stage.H:188` and `options.H:2450,2474`. Missing:
- `explosionGraphic.H:94,97,100` -- 3 calls for clip masks
- `rotatorDisplayData.C:137` -- 1 call

Total XCreateBitmapFromData: **7, not 3**.

**Fix:** Add missing call sites to T18 or attribute them to T13 (explosionGraphic) and D14 (rotatorDisplayData).

---

### M8: T9 Omits stage.H Call-Site Counts
**Source:** Implementation Pragmatist #5

Every other migration task lists explicit call counts. T9 does not. Stage.H has:
- 5 XDrawImageString, 1 XDrawString, 2 XCopyArea, 1 XCreateBitmapFromData
- 5 XLoadQueryFont, 2 XTextWidth, ~12 GC/font setup calls

**Fix:** Add explicit counts to T9 for consistency.

---

### M9: "~100 rendering + resource calls across ~14 files" Is Inaccurate
**Source:** Implementation Pragmatist #6

Actual counts: **109 rendering calls across 15 files**, plus **~114 resource management calls** across 6 files. Grand total: **~223 X11 calls across ~18 unique files**.

**Fix:** Update Verification Strategy section.

---

### M10: T13 Omits explosionGraphic.H Resource Allocation Calls
**Source:** Implementation Pragmatist #8

T13 mentions 1 XCopyArea but explosionGraphic.H has **18 X11 calls** (3 XCreateBitmapFromData, 5 XCreateGC, 5 XSetClipMask, 5 XSetGraphicsExposures).

**Fix:** Expand T13 description to include all call sites.

---

### M11: QA Step in T2 Is Premature
**Source:** Implementation Pragmatist #9

T2's QA says `grep -r '#include.*X11' --include='*.H'` -- expect 0 outside backend files. This cannot pass after only T2 (16+ files still have X11 includes). Move to F1/T41.

**Fix:** Change T2 QA to "GL/VK build compiles without X11/Intrinsic.h from button.H".

---

### M12: No Error Recovery on Backend Initialization Failure
**Source:** Risk Hunter #17

`initWindow()` returns `void` -- cannot signal failure. If `glfwCreateWindow` returns NULL or `vkCreateInstance` returns error, no fallback strategy exists.

**Fix:** Define failure hierarchy: try requested backend -> print error -> optionally fall back to X11. Add return type or exception semantics to `initWindow()`.

---

### M13: Options UX Gap on GL/VK
**Source:** Risk Hunter #2

The Options button remains drawn and clickable on GL/VK (T11 migrates button rendering), but clicking it does nothing (D9 defers Options). No UX feedback.

**Fix:** T22 must decide: hide/disable the Options button on GL/VK, or show "not available" message.

---

### M14: Static Member Definitions in Headers Cause ODR Violations
**Source:** Risk Hunter #15

Headers define static members inline (e.g., `playingField.H:98`: `int PlayingField::uSecondsPerFrame=62500`). With separate compilation for GL/VK, these cause multiple-definition errors.

**Fix:** Phase 0 must move `Type::staticMember=value` patterns from headers to `.C` files or make them `inline` (C++17).

---

### M15: strstream Deprecation
**Source:** Risk Hunter #8

`playingField.H`, `stage.H`, `options.H`, `score.H` use `<strstream>` / `ostrstream`, removed in C++17. The makefile has no explicit `-std=` flag.

**Fix:** Add `-std=c++14` to makefile, or migrate to `<sstream>`.

---

### M16: Memory Ownership Ambiguity Across Backends
**Source:** Risk Hunter #4

~40+ Pixmap/GC allocations scattered across 10+ classes with varying ownership patterns (raw new/delete, placement new, stack members, public members). D14 says rotatorDisplayData.C/compositePixmap.C are "unchanged" but they need Display* which moves to X11Backend.

**Fix:** Create a resource ownership map before Phase 1. Verify all allocation sites can reach Display* via nativeHandle or engine.

---

## MINOR Findings

| # | Source | Finding | Fix |
|---|--------|---------|-----|
| m1 | Code Archaeologist | `rotatorDisplayData.C:192` should be `:193` (XFillRectangle off by 1) | Correct line reference |
| m2 | Architecture Skeptic | F1 verification grep misses `XDrawPoint` in compositePixmap.C | Add to pattern |
| m3 | Architecture Skeptic | Scissor clip needs explicit window coordinate mapping (playAreaX, playAreaY) | Add to D7 |
| m4 | Implementation Pragmatist | TL;DR says 39 tasks, actual is 42 (+5 final verification) | Correct count |
| m5 | Implementation Pragmatist | D16 event site pattern incomplete (missing XEventsQueued) | Add to D16 |
| m6 | Implementation Pragmatist | Commit message T4 uses `build:` not `refactor:` per strategy | Align or note exception |
| m7 | Implementation Pragmatist | T9-T18 are independent and could be parallelized | Note in Phase 1 |
| m8 | Implementation Pragmatist | Options.H XAutoRepeatOn/Off (lines 2548,2550) not counted | Note as deferred per D9 |
| m9 | Implementation Pragmatist | QA tools (xwd, compare) require X11 display -- not CI-friendly | Document limitation |
| m10 | Implementation Pragmatist | T20 QA timing may fail if compositor overrides glfwSwapInterval(0) | Add escape clause |
| m11 | Risk Hunter | `#include` before `#ifndef` guard in playingField.H and stage.H | Normalize in Phase 0 |
| m12 | Risk Hunter | makefile `-w` flag suppresses all warnings during massive refactor | Replace with targeted flags |
| m13 | Risk Hunter | NonRotVectorData degenerate case (no rotation) not called out in T30 | Add note |
| m14 | Risk Hunter | playArea pixmap offset: scissor rect must know offset; API has no setScissor() | Add setScissorRect to API or bake at init |
| m15 | Risk Hunter | VK_LAYER_KHRONOS_validation availability not checked before enable | Add vkEnumerateInstanceLayerProperties check |
| m16 | Risk Hunter | vkAcquireNextImageKHR with UINT64_MAX timeout hangs on swapchain out-of-date | Use finite timeout, handle VK_SUBOPTIMAL_KHR |
| m17 | Risk Hunter | GL 4.6 may not be available on Mesa/Intel (4.5 max on older Mesa) | Downgrade to GL 4.5 Core |
| m18 | Risk Hunter | errorInfo font leaked in Stage destructor (not freed) | Add XFreeFont in X11Backend destructor |
| m19 | Minimalist | Transform stack "simplification" compares against non-existent stack | Acknowledge setTransform is GL/VK-only |
| m20 | Minimalist | TextureId as void* is wrong -- should be uint64_t for type safety | Change to uint64_t typedef |
| m21 | Minimalist | errorInfo font (#5) not needed for GL/VK (Options-only) | Remove from D12 GL/VK font list |
| m22 | Minimalist | Phase 0 tasks T1-T4 could merge (same #ifdef pattern) | Reduce 5 tasks to 2-3 |
| m23 | Minimalist | Small tasks mergeable: T14+T16, T12 foldable | Reduce Phase 1 from 14 to ~10-11 |
| m24 | Minimalist | 8 discrete angles tests only 4-6% of angle space | Add randomized angle test |

---

## Cross-Critic Agreement Matrix

The following findings were identified by multiple critics independently, increasing confidence:

- **C1 (Phase 0 insufficient):** Architecture Skeptic + Implementation Pragmatist + Risk Hunter (ODR) -- 3 critics
- **C2/C3 (count errors):** Implementation Pragmatist + Code Archaeologist + Minimalist -- 3 critics
- **M3 (XAutoRepeat):** Architecture Skeptic + Risk Hunter -- 2 critics
- **M4 (ShipYard lifecycle):** Architecture Skeptic + Implementation Pragmatist -- 2 critics
- **M16 (memory ownership):** Risk Hunter + Architecture Skeptic -- 2 critics
- **Vulkan overkill:** Minimalist (primary) -- 1 critic, but strongest scope argument

---

## Recommended Revision Priority

1. **Fix C1 first** -- Phase 0 is the foundation; everything else depends on it
2. **Fix C2, C3** -- trivial text corrections
3. **Address M1 (Button render-to-texture)** -- design decision needed before Phase 1
4. **Address M2 (D11 signatures)** -- design decision needed before Phase 0 Task 5
5. **Address M3 (XAutoRepeat)** -- add to D16 before Phase 2
6. **Address M12 (init failure)** -- add return type before Phase 3
7. **Address M14 (ODR)** -- add to Phase 0 scope
8. **Address M15 (strstream)** -- add to Phase 0 or pre-Phase 0
9. **Decide on Vulkan** -- if deferred, eliminates 9 tasks and ~15 MAJOR/MINOR findings become moot
10. **Apply all minor fixes** -- mechanical text corrections throughout
