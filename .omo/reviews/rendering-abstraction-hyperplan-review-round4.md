# Hyperplan Review Round 4 — Consolidated Findings & Verdict

**Plan under review:** `.omo/plans/rendering-abstraction.md` (v3, 750 lines, 47 task rows + F1–F5)
**Round-3 findings base:** `.omo/reviews/rendering-abstraction-hyperplan-review.md` (C1–C3, M1–M16, m1–m24)
**Round-2 history:** `.omo/reviews/rendering-abstraction-high-accuracy-review.md` (C1–C5, M1–M7, m1–m5)
**Draft provenance:** `.omo/drafts/rendering-abstraction.md` (`status: needs-revision`)
**Team run:** `a369a3a1-b0c3-4cd2-9c53-b1384caf4b74` (members: validator, researcher, skeptic, architect, creative)
**Round-1 run:** `95383cb5` (reconstructed: `/tmp/opencode/hyperplan-r4-round1-bundle.md`)
**Date:** 2026-08-21 · **Method:** two adversarial rounds (round 2 = 5/5 cross-attack of the unified U1–U31 register + second pass). Every file:line/count re-verified against source at HEAD of branch `rendering-abstraction-plan`, `/home/archerc/code/XAst`. **Where a finding and the code disagree, the code wins.**
**Member durables:** validator `.omo/reviews/round4-validator-findings.md`; researcher census `/tmp/opencode/hyperplan-r4-round2-researcher-census.md`; R2 verbatim deliverables `/tmp/opencode/hyperplan-r4-round2-output.md` + `hyperplan-r4-round2-part2.md`; register `/tmp/opencode/hyperplan-r4-round2-input.md`; lead tie-break greps `/tmp/opencode/hyperplan-r4-lead-adjudication.md`.

---

## VERDICT

**PLAN v3 IS NOT EXECUTION-READY.** (Unanimous, source-verified.)

v3 is a regression against round 3: it ships the violations instead of the fixes. Strict per-finding
pass on the actual round-3 set: **4 ADDRESSED / 8 PARTIAL / 31 NOT-ADDRESSED of 43** (Section C; the
round-2 working tally 8A/8P/27N credited four minor rows that the strict pass downgraded after
checking v3's actual text — m5, m9, m13, m14; m14 is actively worse, see R4-M3). Round 4 adds a
blocker set **R4-B1…B8 + one structural defect (R4-B9)**, a new-defect set R4-N1…N7 (Section E), a
pinned X11 census (Section F = single source of truth for all v4 counts), a ruling that REFUTES the
round-2 "canvas-core inversion" (Section G), and documents the **Vulkan scope as an owner (user)
decision that was never taken** (Section H) — it goes into v4 as an explicit user-input gate, not a
settled decision. The decision-complete fix list for v4 is Section I; verified-correct anchors that
v4 must NOT disturb are Section J.

### Blockers — any one independently makes v3 unexecutable

| # | Blocker | Evidence |
|---|---------|----------|
| B1 | **Build incoherence: `-DX11_BACKEND` defined NOWHERE.** makefile:2 `CXXFLAGS=-I/usr/include/X11 -O3 -w` has zero `-D` flags; `X11_BACKEND` occurs in 0 source files; no task defines it. After Phase-0 header guarding (T1–T6) the X11 target itself compiles with ALL X11 includes stripped and fails; T9 "X11 target unchanged" and T19/F2/SC2 (0-pixel identity) are unreachable by construction. A `CXXFLAGS += -DX11_BACKEND` task for the X11 target is a precondition for any T1–T6 acceptance. | makefile:1-4; researcher U5; validator U5; lead grep A |
| B2 | **GL/VK build breaks at COMPILE: 6 unguarded Options sites.** `Options options(argc,argv)` is function-local at playingField.H:565 (NOT a global — XAsteroids.C:13-30 contains no Options). Unguarded sites: :69 (decl `Boolean RunGame(Options&)`), :300 (definition), :457 & :616 (`options.RealizeWindow()`), :565 (construction), :591 (`RunGame(options)` call). Every TU includes playingField.H (XAsteroids.C:12) ⇒ once T2 #ifdefs options.H, every TU fails to compile. No task #ifdefs any of the 6 sites; no task defines a GL/VK RunGame contract; D9/D16/T20/T22 never mention it. | validator Probe 1; researcher late corrections (mechanism = COMPILE, not link); U24 |
| B3 | **Event model mis-specified (U11+U7+Probe 4).** Game input is PER-EVENT, not polled: one-shot KeyPress side effects ('o'→NewThrust+thrusting on, playingField.H:382-385; space→RemoveShip+NewHyper :389-391; 'e'/'r' AngularVelocity ± pending flags :362-381; 'p' fireThisFrame/firing :378), KeyRelease consumes the pending flags and zeroes angular velocity (:408-445); auto-repeat is SUPPRESSED in-window (XAutoRepeatOff :342/:468/:559/:579; XAutoRepeatOn on leave :338/:477). plan:253 "the game polls key state each frame… No additional API surface needed" is FALSE, and its own cites (XAutoRepeatOff playingField.H:243 / XAutoRepeatOn :500) are fabricated (:243 = gravity math; :500 = `intersector.Intersect()`); the real in-window pause/resume pair is :468/:477. GLFW_REPEAT re-fire = repeated NewThrust / repeated random hyperspace / pending-flag inversion flipping rotation at KeyRelease. The GLFW-flag → per-event state machine D16/T22 rely on is **nowhere in the plan**; the modal Options loop (hand-rolled pump options.H:2542-2571 — a SECOND event island) and the frame-clock re-arm (startTime=ResumePlay at :343/:490) are unmapped. | validator U11/Probe 4; researcher U11 + census §4; lead C |
| B4 | **Vulkan guaranteed hang + no out-of-date path.** plan:604 still mandates `vkAcquireNextImageKHR(timeout=UINT64_MAX)` — the exact hang r3-m16 flagged; 0 grep hits for `VK_ERROR_OUT_OF_DATE`/`VK_SUBOPTIMAL_KHR` in the entire plan; no loader/layer availability check (r3-m15 unaddressed). | plan:604; U12; U15 |
| B5 | **GL 4.6 core gate buys nothing and bricks 4.5 drivers.** plan:548 hard-gates "glGetString(GL_VERSION) reports 4.6 core"; the workload (quads, line strips, R8 textures, FBO, stb text) needs ≤4.5; Mesa/Intel capped at 4.5 hit the gate with no fallback (r3-m17 unaddressed). | plan:548; U13 |
| B6 | **API lacks a render-target primitive; T12 unexecutable.** T12 (plan:422) mandates FBO render-to-texture for Button faces; the frozen 20-method API (plan:73-121) has no render-target method. | plan:422 vs plan:73-121; U22 |
| B7 | **Pixel-identity acceptance is unbuildable + the game is non-deterministic.** No baseline-capture task (screenShots/*.gif are marketing images); `srand(time(NULL))` stage.H:219 with no seed injection; argv[1] = start level (playingField.H:538/589), not a seed. 27 of 47 plan "QA:" lines and 15 of 16 named QA scenarios are not executable as written; ZERO tasks build the deterministic headless harness (U28). T19's "Deterministic test" (plan:486-487) is inexecutable as written. | U6; U28; validator Probe 2; researcher NEW-2 |
| B8 | **Traceability table matches NO real review, AND v3 re-corrupted ≥5 facts that round 3 had CORRECTED.** Table (plan:726-750) uses id-scheme C1-C4/M1-M7/A1-A8 matching neither r2 (C1-C5/M1-M7/m1-m5) nor r3 (C1-C3/M1-M16/m1-m24); plan:728 "all findings addressed" is unverifiable by construction. Re-corrupted R3-correct facts: **(1)** m1 cite still `rotatorDisplayData.C:192` (plan:54) — real XFillRectangle is :193; **(2)** T11 (plan:413) "13 XDrawString (RunGame:525-532 = 8, GenHelpScreen:647-663 = 5)" — real split is 4+9 (:525/:528/:530/:532 + :647-:664), and v3 simultaneously claims 13 (T11) and 14 (table, plan:737) for the same call set; **(3)** T12 (plan:423) keeps "CreateButton:14 init copy" — CreateButton has 0 XCopyArea (button.H:63-145); real: PressButton:1 (:236), ReleaseButton:1 (:248), DrawButton:2 (:261/:269) = 4; **(4)** T13b (plan:440) keeps "3 GCs / 3 XFreePixmap / 8 XCopyArea / drawTextureMasked ×8" — explosionGraphic.H actually has XCopyArea=0; XCreateGC×5 (:103/106/109/112/115), XSetClipMask×5, dtor XFreeGC×5/XFreePixmap×3 (:121-124); the per-frame cost is 1 XCopyArea + 1 XSetClipOrigin in explosion.H:45-51; **(5)** T18 (plan:477) keeps "XCreateBitmapFromData ×4 (188/196/204/212)" + "XFillRectangle ×2 (~140/150)" in stage.H — actual 1 (:188 only) and 0; stage.H's real window-blit XCopyArea pair (:258 DrawShipYard, :269 DrawPlayingField) is missing from T9/T18 inventories; **(6)** T26 hardcodes `glfwCreateWindow(640,512)` (plan:545) while the real X11 window is ≈(max(header,640)+30) wide × ≈(headerHeight+2·maxIconHeight+512+60) tall (stage.H:151-159), centered (stage.H:182-185) and pinned by PSizeHints min=max (playingField.H:539-544) — no sizing/centering task. | plan:54/413/737/423/440/477/545; validator N2 + recon; researcher U16/U17; U1 |
| B9 (structural) | **Task numbering corrupted: one doc, three counts, no true count.** duplicate #13 (plan:429 shipYard / plan:437 explosionGraphic); missing #10 and #27-31; Phase-1 rows reuse 6-9 (collide with Phase-0); TL;DR "44" (plan:7) vs SC13 "42" (plan:722) vs actual 47 rows. Counted separately from B1–B8 because a renumber rewrite changes every task id. | U2 (lead grep audit) |

**Execution gate:** v3 must not be executed. v4 must incorporate Sections A–J in full (fix list = Section I).

---

## A. Blocker detail

B1–B9 above are the consolidated, lead-adjudicated blocker set from the U1–U31 cross-attack plus the
lead tie-break greps. Full evidence chains: validator `.omo/reviews/round4-validator-findings.md`
(Probe 1/2/3/4 + recon list), researcher census §U5/§NEW-1/NEW-2, lead adjudication A–F.
Priority order for v4: B1 → B2 → B7 → B3 → B8 → B9 (numbering) → B4/B5 (Vulkan-conditional, Section H) → B6.

---

## B. R4 NEW MAJORS (R4-M1…M16)

| # | Major | Evidence / required v4 action |
|---|-------|-------------------------------|
| M1 | **Dual-GC architecture missed.** `GC backgroundGC` at playingField.H:77 (GXcopy, black fg :139-141) performs ALL three clears (:202/:312/:638); the composite `gxor` GC is :41/:133/:135. Plan mentions backgroundGC 0×. An engine `clear()` mapped to a single GC is wrong: clears → backgroundGC/GXcopy, compositing → the GXor GC. | lead B; plan 0 mentions |
| M2 | **DI rejected in favor of global pointer + #ifdef dual-ctors.** plan:190 `extern RenderingEngine* engine`; plan:208 mandates `#ifdef X11_BACKEND` dual constructors for 6 classes (Button, ShipYard, ExplosionGraphic, RotatorDisplayData, Stage, Bullet). plan:202's restatement of the Button ctor is itself wrong — real 2nd param is `Drawable&` not `Window` (button.H:160/:208), a 3rd plan-vs-code signature mismatch. Fix: ONE ctor taking `RenderingEngine&`, injected into the 11 globals at XAsteroids.C:13-30; no #ifdef. | U8; U10 |
| M3 | **D8 scissor clips the HUD.** D8 (plan:147-159) sets scissor = play area at beginFrame, then draws score/shipyard/title — all OUTSIDE the 640×512 play rect (scoreX/scoreY stage.H:168-171; title :161-162; shipyard :172-175; button :163; playAreaY = headerHeight+shipYardHeight+35 stage.H:177). No task clears/resets the scissor; r3-m14's `setScissor()` was absorbed into beginFrame — which IS the bug. GL/VK frame 1 = hollow shell. | validator recon #1; r3-m3/m14 |
| M4 | **Resource-ownership map absent; destructor double-free near-certain.** ~Stage (stage.H:222-236) frees 4 GCs + icon pixmap + window + 4 fonts + XCloseDisplay; D11/D15 move window/GC/font creation into X11Backend::initWindow; M12 calls shutdown() from main; NO task assigns ownership. X11Backend needs exactly titleGC/scoreGC/hiScoreGC/defaultGC for per-font XSetFont. One destructor frees what the other owns. | validator recon #4; r3-M16 |
| M5 | **T21/T22 (GL/VK event + window work) scheduled into Phase 3 after the API freeze, sequenced incoherently with T9/T24** (T9 Phase 0 drops -lXm/-lXt for targets that T24 Phase 3 creates — U23 state-machine incoherence), leaving 4.5-only GL drivers with no alternate path (B5). | U23; plan:530-584 |
| M6 | **Window-creation circularity.** initWindow(w,h) is called in main() BEFORE the globals (D11), but windowWidth/Height derive from font metrics loaded INSIDE initWindow (D15; stage.H:134-159). Two-pass init (fonts+metrics → compute size → create → `glfwSetWindowPos` center + `glfwSetSizeLimits` min=max) is required; no task does it. | validator recon #2 |
| M7 | **Signal handler references `stage.display` (stage.H:296-305, incl. XAutoRepeatOff :298)** — dead on GL/VK after D11; never #ifdef'd. | validator recon #8 |
| M8 | **Makefile `.o` collision across BACKEND switches.** T24 (plan:530-536) specifies per-target LINK flags only; X11 and GL/VK targets reuse the same .o names (makefile:6-14) with unchanged source timestamps ⇒ `make BACKEND=X11` then `make BACKEND=GL` relinks stale X11 objects or mixes backends. Needs per-backend object dir/suffix; `-w` (r3-m12) must also be replaced with targeted warning flags. | validator recon #5; r3-m12 |
| M9 | **Cross-backend identity QA is a human eyeball.** T44 (plan:654) "side-by-side GL vs Vulkan — visual identity" has no capture method/tolerance/regions; T45 "full session" (plan:665) has no implementing task; no GL `glViewport`-on-resize anywhere; xwd/compare tooling is X11-only (r3-m9). | U30; validator recon #3 |
| M10 | **F1's gate cannot certify SC10.** F1 regex (XDraw\|XCopy\|XFill\|XSetClip\|XSetFunction, plan:689) covers **112 of 434** pinned call sites (26%); 322 sites (XCreateGC, XFreePixmap, XNextEvent, XCreateBitmapFromData, XSetWM*, XAutoRepeat*, XSync, …) are invisible to it; it also misses XDrawPoint/XDrawLines/XDrawImageString (r3-m2). | Section F; r3-m2 |
| M11 | **Font-metrics "within 10% × 5 fonts × 3 metrics" (SC3/F2 plan:690; T33 plan:566) is false precision** — plan:227 itself concedes PCF-bitmap vs TTF metrics "will differ". Replace: 0 px diff outside text regions + text compared by masked screenshots only. | U20 |
| M12 | **TextureId = void*** (plan:380) persists although the API's own signatures (plan:110-116) use it — type-safety gap; should be `uint64_t` (r3-m20 unaddressed). | plan:380 |
| M13 | **Window-close path ungated on GL/VK.** No `glfwSetWindowCloseCallback` + clean resource release; X11 side is covered only by the broken ownership chain (M4). | validator recon #4 |
| M14 | **r3-M13 (Options button UX gap) fix cites a nonexistent task number** (T27 — numbering corruption, B9). Grayed-out button + README note are the right fix; the task row must exist under a real, contiguous number. | plan:178 |
| M15 | **T7 strstream task non-compilable as specified** — promoted to major for v4 ordering. See R4-N1. | Section E |
| M16 | **D8 has no draw-order/Z-order contract.** GL/VK alpha blending is non-commutative ⇒ D8 must fix ONE canonical draw order and STATE that X11 is order-invariant on the GXor path: `XSetFunction` appears exactly ONCE repo-wide (playingField.H:135, GXor); per-pixel XOR against fixed source pixels is associative+commutative, so object draw order cannot change the X11 image (round-2 "order-sensitive GXor" claim is mathematically false — U25 ruling, Section D). The GXcopy+clip-mask paths (rockGroup.H:184-197, explosionGraphic.H:103-117) are a separate masking concern, not a draw-order one. | U25 ruling |

---

## C. Round-3 finding-by-finding verdict against plan v3 (strict, source-checked)

Source of the finding set: `.omo/reviews/rendering-abstraction-hyperplan-review.md` (round 3).
Status: **A**dressed (verifiably corrected in v3) / **P**artial (attempted; defect or regression
remains) / **N**ot-addressed (still open in v3, or present with fabricated cites).

### C.1 CRITICAL (3)

| R3 id | Status | v3 state (plan:line) → residual defect |
|-------|--------|----------------------------------------|
| C1 | **N** | T1–T6 (plan:301-346) still cite 7/8 WRONG paths (`gamePlay/*` vs actual `objects/{enemies,ships,rocks,explosions}/*`) and wrong lines (claims 15/14/16/12/12/12/11/11; actual enemyGroup.H:5, shipGroup.H:2, rockGroup.H:4, bullet.H:11, shipBulletGroup.H:2, explosion.H:4, enemyBulletGroup.H:2, shipYard.H:4); stage.H cited :27 (real :11); T2 cites "options/options.H" (wrong path). 5 unguarded headers have NO covering task: playingField.H:18-19 (Xlib.h + Xutil.h), explosionGraphic.H:4, movableObject.H:8, rotator.H:5, frameList.H:4 (lead D counted 20 unguarded headers total). The guards themselves reference the macro B1 never defines ⇒ the fix is inoperative. |
| C2 | **P** (regressed) | Traceability (plan:737) "fixed" the count to 14; T11 (plan:413) simultaneously claims 13 with the fabricated 8/5 split. Real: 4+9 = 13. Round 3 had verified the true 4+9; v3 re-broke it (B8-2). |
| C3 | **P** | T12 (plan:423) keeps "CreateButton:14 init copy" (CreateButton has 0 XCopyArea). Count is now 4 (correct) but attribution still wrong: PressButton:1 (:236), ReleaseButton:1 (:248), DrawButton:2 (:261/:269). |

### C.2 MAJOR (16)

| R3 id | Status | v3 state → residual defect |
|-------|--------|----------------------------|
| M1 (Button render-to-texture) | **N** | T12 mandates FBO (plan:422); API still has no render-target primitive (B6). |
| M2 (D11 signatures) | **P** | D11 now admits signatures change (plan:201-208) ✓, but picks `#ifdef X11_BACKEND` dual-ctors (rejected, R4-M2) and its own Button signature restatement is wrong (Drawable& vs Window). |
| M3 (XAutoRepeat) | **N** | plan:253 "no-ops on GL/VK, harmless, no API surface needed" = false (per-event game; fabricated :243/:500 cites; GLFW_REPEAT re-fire bugs). The round-3 debounce suggestion does not even match the real mechanism (suppression-by-design via XAutoRepeatOff-on-EnterNotify :468). |
| M4 (ShipYard lifecycle) | **P** | plan:431 adds `std::vector<TextureId>` lifecycle ✓; task list still omits the full resource census (XCreatePixmapFromBitmapData×2, XCreatePixmap×3, XFreePixmap×5, XAllocColor×3, XCreateGC/XFreeGC, fg/bg×6) and depends on the missing render-target (M1). |
| M5 (per-char lbearing) | **P** | plan:424 "accept approximate (within 1px)" as a known limitation — contradicts the 10%-metrics gate (M11); the required decision (monospace TTF substitution, `measureChar`, or centering via `measureText`) was never made. |
| M6 (Options coupling) | **P** | D9 (plan:162-168) documents the deferral ✓; but the hand-rolled Options event pump (options.H:2542-2571: XtAppNextEvent switch + XPutBackEvent :2560 + XtDispatchEvent :2568) is a SECOND event island beyond the 4 XNextEvent sites and no task maps or #ifdefs it (B2 six). r3 "10+ extern globals" precisely: coupling via REFERENCED globals (stage.window :2543, stage.autoRepeatState :2550/2552, stage.Refresh() :2556, playingField.WM_PROTOCOLS/WM_DELETE_WINDOW :2559/2560) — options.H declares zero `extern`s. |
| M7 (XCreateBitmapFromData census) | **N** | T18 (plan:477) lists 4 FABRICATED stage.H sites (188/196/204/212; real :188 only). explosionGraphic.H:94/97/100 (3) and rotatorDisplayData.C:137 (1) still unattributed to a real task; repo total is 7 (census §U17). |
| M8 (T9 stage.H counts) | **N** | T9 still has no call-site counts; T18's replacement list is fabricated (B8-5). Real stage.H census in Section F (draw: XDrawImageString×5 :243/:247/:252/:285/:289, XDrawString×1 :280, XCopyArea×2 :258/:269; fonts: XLoadQueryFont×5 :134-138, XFreeFont×4 :231-234). |
| M9 (~223 count) | **N** | plan:260/:295 still "~223 across ~18 files". Pinned census: **434** total / 248 (leader pattern) / ~228 defensible draw+clip+GC-lifecycle subset — methodology must be stated (Section F). |
| M10 (T13 resource census) | **N** | T13b (plan:440) still carries the fabricated 8-XCopyArea/×8-drawTextureMasked numbers instead of the true 20-call explosionGraphic.H census (B8-4). |
| M11 (T2 QA premature) | **N** | T2 QA (plan:314) "make BACKEND=GL compiles without Motif/Xt headers" — no such target exists until Phase 3; T1's grep-expectation (plan:307) also cannot pass after T1 alone. |
| M12 (init failure) | **P** | D11 (plan:210) declares `initWindow()` returns a success/failure indicator (main exits 1) ✓, but D5 (plan:78) still declares `virtual void initWindow(...)` — unreconciled; M12 is unimplementable until the signature is fixed (U21). |
| M13 (Options UX gap) | **P** | fix present (plan:178: grayed-out button + README) but cites nonexistent T27 (R4-M14/B9) and no numbered task row implements it. |
| M14 (ODR static members) | **A** | plan:375 "fix: resolve ODR violations in header static members" — dedicated task exists. |
| M15 (strstream) | **N** | T7 exists (plan:348-354) but is non-compilable as specified (R4-N1): cites playingField.H:798 (istrstream) and :812 (ostrstream) — the file is 676 lines; `istrstream` is absent from the repo (grep 0; only `ostrstream` exists); covers only 1 of 3 real files; the mechanical rename breaks on `seekp` (ostrstream-only idiom at playingField.H:524, stage.H:245/:287 — std::ostringstream has NO seekp). No task adds `-std`. |
| M16 (ownership) | **N** | no resource ownership map; the double-free is now certain (R4-M4); the errorInfo font leak (m18) is part of it. |

### C.3 MINOR (24)

| R3 id | Status | v3 state → residual |
|-------|--------|--------------------|
| m1 | **N** (regressed) | plan:54 STILL cites `rotatorDisplayData.C:192`; real XFillRectangle is :193. Round 3 pinned :193; v3 re-broke it (B8-1). |
| m2 | **N** | F1 regex still misses XDrawPoint/XDrawLines/XDrawImageString + 322 non-render sites (M10). |
| m3 | **N** | no playAreaX/playAreaY coordinate mapping in D7 (worsened by M3: HUD outside scissor, no setScissorRect). |
| m4 | **N** | TL;DR 44 (plan:7) vs SC13 42 (plan:722) vs actual 47 (B9). |
| m5 | **N** | D16 (plan:244-251) still omits XEventsQueued/XLookupString/XRefreshKeyboardMapping/XPutBackEvent — the event boundary is leaky, not just incomplete (U7). |
| m6 | **A** | T4 commit (plan:330) is `refactor:`. |
| m7 | **A** | Phase 1 notes T9–T18 independent / parallelizable. |
| m8 | **N** | options.H XAutoRepeatOn/Off (2548/2550) uncounted; worse, plan:253's AutoRepeat cite cluster is fabricated entirely. |
| m9 | **N** | xwd/compare QA (plan:407/485) still display-dependent and un-cropped: `xwd -root` captures window decorations (X11 border 5px, stage.H:186) that GLFW windows differ on ⇒ F2 must crop to client area; not CI-friendly. |
| m10 | **N** | no `glfwSwapInterval(0)` compositor escape clause in T20 QA. |
| m11 | **A** | T1 (plan:301-305) normalizes include/guard ordering in the files it touches. (C1 residual is paths/coverage, not ordering.) |
| m12 | **N** | makefile `-w` persists (makefile:2); no targeted-warning task (M8). |
| m13 | **N** | NonRotVectorData degenerate-case note absent from T30/T31 (only the scope line plan:47 mentions the type pair). |
| m14 | **N** (worsened) | no setScissorRect in API; the HUD-clipping defect (R4-M3) is a direct consequence. |
| m15 | **N** | no `vkEnumerateInstanceLayerProperties` gate anywhere (U15). |
| m16 | **N** | plan:604 still UINT64_MAX (B4). |
| m17 | **N** | plan:548 still hard-gates 4.6 core (B5). |
| m18 | **N** | errorInfo font (stage.H:138) still never freed: XLoadQueryFont×5 vs XFreeFont×4 (stage.H:231-234); no task addresses it. |
| m19 | **N** | setTransform "simplification" (plan:129) still compares against a non-existent stack; must acknowledge setTransform is GL/VK-only (X11 has no transform stack). |
| m20 | **N** | TextureId still `void*` (plan:380) — R4-M12. |
| m21 | **N** | errorInfo still listed as font #5 "Error text" for GL/VK (plan:225) — it is Options/X11-only. |
| m22 | **N** | Phase 0 not merged (T1–T5 still 5 separate tasks, same #ifdef pattern). |
| m23 | **N** | small-task merges (T14+T16 etc.) not applied. |
| m24 | **N** | 8-discrete-angle QA persists (plan:449); no randomized/numeric lane (U29). |

### Tally (strict, from the tables above)

**A 4 / P 8 / N 31 = 43.** A = {M14, m6, m7, m11}. P = {C2, C3, M2, M4, M5, M6, M12, M13}.
The round-2 working tally (8A/8P/27N) credited four minor rows as addressed before the strict pass
checked v3's actual text; they are downgraded here: m5 (D16 still omits the event-family calls), m9
(xwd capture un-cropped/unci-friendly), m13 (no degenerate-case note), m14 (absorbed into beginFrame
= the HUD-clip bug R4-M3).

---

## D. U1–U31 unified-register verdicts (round-2 cross-attack, code-verified)

Tally: **28 STANDS** (two with mechanism/count corrections), **2 EVISCERATED** (U25 X11-half, U27),
**1 partial** (U19 — redundant-not-dead; final ruling still deletes it, Section I.7), **1 carried as
owner gate** (U26/U18/U19-cluster → Section H). Validator's independent durable:
`.omo/reviews/round4-validator-findings.md` (28/31 STANDS from its role, 4 probes answered).

| U | Verdict (final) | Evidence (file:line) |
|---|-----------------|----------------------|
| U1 | STANDS | plan:726-750 id-scheme matches NEITHER persisted review; plan:728 "all findings addressed" falsified empirically (m1 still :192 at plan:54; no vkEnumerateInstanceLayerProperties; plan:604 UINT64_MAX; plan:548 GL 4.6; errorInfo leak has no task). |
| U2 | STANDS | dup #13 (plan:429/437); missing #10, #27-31; Phase-1 reuses 6-9 (plan:379-413); 44 vs 42 vs 47 (plan:7/722/actual). |
| U3 | STANDS (pinned) | 434 full / 248 leader-pattern / 469 struck (Section F). |
| U4 | STANDS (reinforced) | 5 headers with no task (playingField.H:18-19, explosionGraphic.H:4, movableObject.H:8, rotator.H:5, frameList.H:4); T6 7/8 wrong paths + wrong lines (actual: enemyGroup.H:5, shipGroup.H:2, rockGroup.H:4, bullet.H:11, shipBulletGroup.H:2, explosion.H:4, enemyBulletGroup.H:2, shipYard.H:4); stage.H cite :27 real :11. Lead D: 20 unguarded headers total incl. AutoRepeatOn.C. |
| U5 | STANDS | = B1. makefile:1-4 zero -D flags; no task defines X11_BACKEND; X11 target breaks post-Phase-0. |
| U6 | STANDS | = B7 (baseline half). no capture task; srand(time(NULL)) stage.H:219; argv[1]=level (playingField.H:538/589). |
| U7 | STANDS | = B3/M10. D16 retains in DOMAIN code: 4 XNextEvent (playingField.H:333/340/453/572) + XEventsQueued :332 + XLookupString :359/406/587 + XAutoRepeat×7 (:338/342/468/477/559/577/579) + XSelectInput :560 + XMapRaised :566 + XSetWMProtocols :558 + XSync :296/329 + Options hand-rolled pump options.H:2542-2571; SC10/F1 demand zero raw X11 outside backend; F1 regex matches NONE of these families. |
| U8 | STANDS | plan:190 `extern RenderingEngine* engine`; no mock/DI path; 11 globals (XAsteroids.C:13-30, span verified correct — researcher retracted its own ":31" correction) construct with engine state. |
| U9 | STANDS (core; 2 sub-claim corrections) | options.H 3870 LOC; hand-rolled pump :2542-2571 (XtAppNextEvent switch over stage-window events; XPutBackEvent :2560 for WM_DELETE; XtDispatchEvent :2568 for the rest). Corrections: `XtAppMainLoop`/`XtAppInitialize`/`XtInitialize` appear NOWHERE (grep 0) — "own Xt event pump" is true, "XtAppMainLoop specifically" is false; zero `extern` declarations (grep 0) — coupling is via referenced globals (stage.window :2543, stage.autoRepeatState :2550/2552, stage.Refresh() :2556, playingField.WM_DELETE_WINDOW :2559/2560). D1 "identical feature parity" vs D9 "Options unavailable on GL/VK" is an internal contradiction SC9 (plan:718) quietly concedes. nativeHandle() is a leaky escape hatch making the abstraction either failing or scoped-with-Options-excluded — v4 must say which (it does: D9 + B2 fix). |
| U10 | STANDS | = R4-M2. plan:208 #ifdef dual-ctor; plan:202 Button signature wrong (real 2nd param Drawable&, button.H:160/:208); ShipYard ctor (Display*, Window/Drawable, GC) per r3-M4. |
| U11 | STANDS | = B3. Per-event input confirmed by validator + researcher + lead (lead C). plan:243/:500 cites fabricated. |
| U12 | STANDS | = B4. plan:604 UINT64_MAX; 0 OUT_OF_DATE/SUBOPTIMAL hits in plan. |
| U13 | STANDS | = B5. plan:548 4.6 core; workload ≤4.5. |
| U14 | STANDS | stage.H:134-138 ×5 XLoadQueryFont vs :231-234 ×4 XFreeFont; errorInfo (allocated :138) freed nowhere; leaked every run; no task. |
| U15 | STANDS | no loader-version/vulkaninfo/layer check in any task; T25 (plan:536-542) assumes vendorability. |
| U16 | STANDS (counts corrected) | explosionGraphic.H actual: XCreateBitmapFromData×3 (:94/97/100), XCreateGC×5 (:103/106/109/112/115), XSetClipMask×5 (:105/108/111/114/117), XSetGraphicsExposures×5, dtor XFreeGC×5 / XFreePixmap×3 (:121-124), XCopyArea=0. Per-frame cost = 1 XCopyArea + 1 XSetClipOrigin in Explosion::DisplayFrame (explosion.H:45-51). Plan:440 wrong on 4 of 5 claimed numbers; three irreconcilable counts (8 vs 18 vs actual 20) collapse to one (Section F). |
| U17 | STANDS (counts corrected) | stage.H: XCreateBitmapFromData×1 (:188 window icon only; :196=XCreateGC, :204=XSetForeground, :212=sigaction), XFillRectangle×0 (T18's "~140/150" cite fabricated). Real window blit = XCopyArea×2 (:258 DrawShipYard, :269 DrawPlayingField) — missing from T9/T18 inventories. |
| U18 | STANDS as gap-flag | 9 Vulkan tasks (T36-T44) ≈ 20% of plan; r3 review:14 recommended deferral ("strongest scope argument"); zero Vulkan in code/README/makefile/git outside `.omo/`; counter-evidence to deletion: `.omo/drafts/rendering-abstraction.md:33` records "Vulkan is essential (user requirement)". Scope call = user's (Section H). |
| U19 | EVISCERATED (partial) → final ruling | "DEAD" is overstated — the plan itself names createTextureFromXBM as the GL/VK texture path at plan:230/:235/:440/:571, so it is REDUNDANT, not dead (no backend ever receives raw XBM through it in any task flow: T23 already decodes XBM→RGBA backend-agnostic). Final ruling: DELETE the method, route XBM via T23 decode → createTextureFromBitmap. API lands at 19 methods + new seams (Section I.7). |
| U20 | STANDS | plan:227 concedes PCF-vs-TTF metrics "will differ"; plan:690/plan:566 still gate "10% × 5 fonts × 3 metrics" = false-precision theater (R4-M11). |
| U21 | STANDS | plan:78 `virtual void initWindow(...)` vs plan:210 "returns a success/failure indicator" — unreconciled; M12 unimplementable. |
| U22 | STANDS | = B6. plan:422 FBO vs plan:73-121 API with no render-target. |
| U23 | STANDS | T9 Phase 0 (plan:363-368) drops -lXm/-lXt for targets T24 Phase 3 (plan:530-536) creates; state machine incoherent (R4-M5). |
| U24 | STANDS (mechanism corrected: COMPILE, not link) | Options is a PlayTheGame LOCAL (playingField.H:565), not a global; six unguarded sites; every TU includes playingField.H (XAsteroids.C:12) ⇒ compile break, not link break (B2). |
| U25 | EVISCERATED (X11 half) — STANDS (GL/VK half) | "GXor compositing is order-sensitive" is mathematically FALSE on X11: XSetFunction appears exactly ONCE repo-wide (playingField.H:135, GXor); per-pixel XOR against fixed source pixels is associative+commutative — object draw order CANNOT change the X11 image. Requirement that survives (R4-M16): D8 fixes ONE canonical draw order for the non-commutative GL/VK alpha path and states X11 order-invariance; the GXcopy+clip-mask paths (rockGroup.H:184-197, explosionGraphic.H:103-117) need mask/correctness handling, not a Z-order contract. |
| U26 | EVISCERATED (by researcher) → owner-gate reinstated | "no provenance in repo" is false: drafts:33 records the user requirement. The real quarrel: provenance of the attestation (second-hand planner record) AND that the user never confirmed scope. Carried into v4 as the Section H user-input gate. |
| U27 | **EVISCERATED** | "Parity by construction via thin software-RGB core + 3 dumb blitters" is FALSE against real X11-server behavior the game depends on: XLookupString keymaps (playingField.H:359/406/587, layout-dependent), XRefreshKeyboardMapping (:335), XAutoRepeatOn/Off GLOBAL server state (stage.H:209-210 + 7 playingField.H sites + options.H:2548/2550 + AutoRepeatOn.C:10), Expose-driven stage.Refresh() (:351/:481/:583), and the 133-X11-call rotator pipeline (rotatorDisplayData.C; default-GXcopy mask GCs + XSetClipOrigin; rockGroup.H:184-186) all require CPU-side re-emulation to match server behavior byte-for-byte. The inversion is a SECOND FULL REWRITE, not a plan correction — valid only as a NEW project. Ruled in Section G, with the researcher's cost model on record. |
| U28 | STANDS (quantified) | = B7 (harness half). 47 "QA:" lines — ~20 agent-verifiable today (14 grep/build gates + XBM dimension check + runtime counters), 27 not: 4 need baseline+display+deterministic state, 12 need a seeded gameplay harness, 9 human-visual-only; plan:548 fails outright on 4.5 drivers. 15/16 named scenarios non-verifiable; #15 (resize) untestable on X11 because PSizeHints min=max pins the window (playingField.H:539-544). ZERO tasks build the harness ⇒ MISSING WORKSTREAM, not a QA gap. |
| U29 | STANDS | QA plan:269-285 = 100% visual; zero assertions over intersection2d.H (998 LOC; swept-intersect sort :754-763) or FP-heavy gravity (playingField.H:216-228, zero-distance guard :222); README:3 states the repo's purpose is finding compiler/FP bugs — no test lane exists for it. T20's update/render split is exactly where float-order drift enters. |
| U30 | STANDS | T44 (plan:654) = human-in-the-loop eyeball; no capture method/tolerance/regions; SC3 (plan:712) inherits. Fix: reuse the U28 harness + byte-diff non-text regions. |
| U31 | STANDS (strengthened) | zero test infra (every "run existing tests / no regression" QA step unsupported); score.H/options.H parse paths verified UNCHANGED by all 47 tasks — correct risk statement: "unchanged, and no validation exists"; makefile has no -std (T7's "-std=c++17" acceptance has no makefile backing). score.H I/O defects: R4-N3. |

---

## E. R4 NEW DEFECTS (source-verified)

Plan defects N1/N2; latent in-code defects N3–N7 the refactor must not perturb (and several that
SC2's 0-pixel identity depends on).

| # | Sev | Defect | Evidence |
|---|-----|--------|----------|
| N1 | MAJOR (build) | **T7 strstream migration non-compilable as specified.** (i) Cites playingField.H:798 (istrstream) and :812 (ostrstream) — the file is 676 lines; `istrstream` is ABSENT from the entire repo (grep 0; only `ostrstream` exists). (ii) Real `ostrstream` sites: playingField.H:14 (include) + :519; stage.H:6 (include) + :240 + :283; options.H:6 + :3112/:3125/:3859/:3865 (X11-only, deferred by D9 — fine). T7 covers only playingField.H. (iii) The mechanical rename FAILS: `strout.seekp(0)` at playingField.H:524, stage.H:245, stage.H:287 is an ostrstream-only buffer-reuse idiom (reset the in-memory buffer); std::ostringstream has NO seekp (append-only) ⇒ rename as specified produces compile errors and silently changes DrawScore()/Refresh() formatting unless the idiom is rewritten (snprintf into fixed buffers, or ostringstream with .str() per use). (iv) makefile has no -std flag (`-I/usr/include/X11 -O3 -w` only); T7 acceptance "Compiles with -std=c++17" (plan:352) has no makefile change behind it. Note: strstream was removed in C++17 (the plan's later "C++26" framing in v3 text is wrong — flag for the plan agent). **Fix: T7 = 3 real files + seekp-idiom rewrite + explicit `-std=` decision in makefile.** | validator N1 |
| N2 | MAJOR (fabrication cluster) | **T11 XDrawString split fabricated + 13-vs-14 self-contradiction.** plan:413 "13 XDrawString (RunGame:525-532 = 8, GenHelpScreen:647-663 = 5)". Actual: RunGame game-over region = 4 calls (:525/:528/:530/:532); GenHelpScreen = 9 calls (:647/:649/:653/:655/:657/:659/:661/:663/:664 — the 9th call's string argument sits on :664, inside the plan's own stated range, so the RANGE is fine; the SPLIT 8/5 is wrong — true 4/9 — and the v3 table (plan:737, M2 row) "fixed" the same file to 14, so the plan simultaneously claims 13 (T11) and 14 (table) for the identical call set. The fabrication cluster (B8-3/4/5) extends to T11. | validator N2 |
| N3 | MAJOR (latent I/O) | **score.H hi-score I/O defective, untouched by all 47 tasks, and feeds a screen inside SC2's 0-pixel identity.** score.H:68-69 `strncpy(currentName, getpwuid(geteuid())->pw_name, 8)` — NULL-deref where getpwuid fails; score.H:71/85 hardcoded `/Volumes/XAsteroids/XAst/hiScore.data` (macOS path; unopenable under the plan's Linux scope) with path selection at :77-84 driven by strlen() MAX (length-driven, not existence-ordered); score.H:106-108 `do {file>>name>>score;} while(++numScores<…)` increments numScores AFTER a possibly-failed `file>>` → uninitialized score entries render on the hi-score screen. Zero tasks touch score.H I/O; F1's regex cannot see it. **v4 must decide: fix (3 small edits, recommended) or explicitly fixture-freeze hiScore.data and scope the screen out of pixel-compare.** | researcher NEW-1 |
| N4 | MAJOR (QA prerequisite) | **Non-deterministic game = no reproducible QA.** stage.H:218-219 `srand(time(NULL))` — no seed injection (PlayTheGame's first param is wave level, playingField.H:35/538; XAsteroids.C:36). Every task QA line ("play full game", "trigger explosion", "measured via timestamp deltas") is non-reproducible without seeded RNG + scripted events + state freeze. T19's "Deterministic test" (plan:486-487) inexecutable as written. (U28/B7; fix = seed via env/argv defaulting to current behavior + harness.) | researcher NEW-2 |
| N5 | MAJOR (build) | **Makefile .o collision across BACKEND switches** = R4-M8: same .o names per target (makefile:6-14); `make BACKEND=X11` then `make BACKEND=GL` relinks stale/mixed objects; needs per-backend object dir or suffix; `-w` → targeted flags. | validator recon #5 |
| N6 | MAJOR (latent) | **Signal handler references `stage.display` (stage.H:296-305)** — dead on GL/VK after D11; never #ifdef'd (R4-M7). | validator recon #8 |
| N7 | MINOR (QA) | **xwd border artifact + resize untestable.** `xwd -root` captures window decorations (X11 border 5px, stage.H:186) that GLFW windows differ on ⇒ F2 must crop to client area; display-dependent QA is not CI-friendly (r3-m9). Resize QA has no X11 ground truth (window pinned by PSizeHints, playingField.H:539-544) — v4 must either drop resize parity rows (fixed-size all backends, recommended) or add real resize behavior. | validator recon #10; U28 |

---

## F. Pinned X11 census — SINGLE SOURCE OF TRUTH for all v4 counts

**Methodology (lead-pinned, reproducible, 2026-08-21, HEAD of `rendering-abstraction-plan`):**
1. Leader pattern `grep -o 'XDraw|XCopy|XFill|XSetClip|XLookup|XCreate|XFree'` over `*.H|*.C` (excl .omo): **248** (context number, superset-prone).
2. Full census: per-file `re.finditer(r'\bX[A-Z][A-Za-z0-9]*\b')` over all `*.H/*.C` (excl .git/.omo), restricted to the 55-call-symbol whitelist below, machine-summed: **434 call sites / 16 files** — **the single source of truth for every "Actual calls" line in v4 task rows T7–T18**.
3. Call-syntax audit `grep -rhoE 'X[A-Z][a-z][A-Za-z0-9]*\('`: 473 raw identifiers − 42 project-prefixed false positives (XVelocity×16, XAcceleration×10, XMove×13, XFree×3) = **431**; the +3 delta vs 434 is census-inclusive identifier counting.
4. **469 IS STRUCK FROM RECORD** (unreproducible by any method run this session; closest artifact = 434 + 34 XCreateGC double-counted = 468 ≈ 469 — a round-1 artifact).

**Per-file (sums to 434):**

| File | Calls | Note |
|------|-------|------|
| rotatorDisplayData.C | **146 (34%)** | D14 "unchanged" — a THIRD of the surface is permanently X11-only by the plan's own D14 terms; its 3 XSetClipMask (out of repo total 8) sit here (:575/:744/:780) |
| playingField.H | 66 | clears, input, pacing |
| stage.H | 49 | window, fonts, GCs |
| options.H | 47 | X11-only per D9 (hand-rolled Xt pump) |
| button.H | 41 | |
| shipYard.H | 29 | |
| explosionGraphic.H | 20 | 17 of the 7 XCreateBitmapFromData are repo-wide; 3 of these |
| compositePixmap.C | 11 | D14 "unchanged", linked into EVERY target |
| shipGroup.H | 8 | |
| rockGroup.H | 4 | |
| AutoRepeatOn.C | 3 | standalone util |
| bullet.H | 3 | |
| enemyGroup.H | 2 | |
| explosion.H | 2 | the per-frame 5-layer draw (:45-51) |
| shipBulletGroup.H | 2 | |
| enemyBulletGroup.H | 1 | |

**Categories (sum 434):** draw 98 · resource 172 · gcattr 97 · event 31 · window 18 · clip 13 · metrics 5.
(Cross-check: validator's independent render-primitive-only slice XDraw*/XCopyArea/XFill* = 91 over
13 files — method variance, both on record; the category-summed 98 is the census number.)

**55-symbol whitelist (sums to 434; v4's F1 gate must embed this table):**
XDrawString 14, XDrawLines 10, XDrawPoint 11, XDrawImageString 7, XCopyArea 25, XCopyPlane 4,
XFillRectangle 22, XFillPolygon 5, XSetClipMask 8, XSetClipOrigin 5, XCreateGC 34, XFreeGC 31,
XCreatePixmap 19, XFreePixmap 33, XCreatePixmapFromBitmapData 2, XCreateBitmapFromData 7,
XAllocColor 22, XFree 3, XLoadQueryFont 5, XFreeFont 4, XGetImage 4, XDestroyImage 4,
XGetPixel 4, XSetForeground 45, XSetBackground 19, XSetFont 5, XSetLineAttributes 7,
XSetGraphicsExposures 20, XSetFunction 1, XTextWidth 4, XTextExtents 1, XNextEvent 4,
XEventsQueued 1, XLookupString 3, XRefreshKeyboardMapping 5, XPutBackEvent 1, XAutoRepeatOn 7,
XAutoRepeatOff 5, XGetKeyboardControl 1, XInternAtom 2, XSync 2, XOpenDisplay 2, XCloseDisplay 2,
XRaiseWindow 2, XCreateSimpleWindow 1, XMapRaised 1, XSelectInput 1, XSetWMProtocols 1,
XSetWMProperties 1, XStringListToTextProperty 1, XAllocWMHints 1, XAllocSizeHints 1,
XAllocClassHint 1, XQueryColor 1, XGetWindowAttributes 1, XDestroyWindow 1.

> **[v4 assembly note, 2026-08-21 — normalization of the 55↔56 heading]:** the heading above says
> "55-symbol whitelist," but the enumeration contains **56 distinct symbols**, and the per-symbol
> counts sum to exactly **434** under the 56-symbol enumeration (machine-verified at v4 assembly).
> The off-by-one is in the receipt heading only, not in the census. **v4 normalizes to 56** (plan §Verification
> Strategy whitelist table, F1 gate, SC10, task 47). The F1 gate is the **enumeration** (full sweep over the
> named list), so the gate does not depend on the number. The receipt body is otherwise unchanged — this is the
> single in-file annotation; the plan's DISCREPANCIES section #3 carries the same record.

**Reconciliation of the plan's contested numbers:**
- "~100" (F1, plan:689) ≈ draw-only (98 measured). BUT F1's acceptance text claims to certify
  "rendering + resource" while its regex (`XDraw|XCopy|XFill|XSetClip|XSetFunction`) covers only
  **112 of 434 (26%)** and misses XDrawPoint/XDrawLines/XDrawImageString/XCopyPlane entirely ⇒
  **F1 cannot certify SC10** (R4-M10). v4 F1 = full 55-symbol whitelist, stated methodology, 434 as
  the pinned denominator; the gate's whitelist must also declare the D9 exception zone
  (options.H's hand-rolled Xt pump is X11-only BY DESIGN — X11Backend + options.H, not "zero raw
  X11 anywhere outside backend files").
- "~223" (plan:260/:295; r3-M9) ≈ defensible subset: draw+clip (111) + GC/Pixmap lifecycle core
  (117 = XCreateGC 34 + XFreeGC 31 + XCreatePixmap 19 + XFreePixmap 33) ≈ 228 — but excludes 211
  sites (gcattr 97, colors 22, bitmaps 9, fonts 9, image-pixel 12, events 31, window 18, metrics 5)
  that F1 claims to certify. v4 replaces "~223" with 434 + the category table above.
- "469": struck (see methodology item 4).

**v4 count corrections to task rows (from census + probes):** T11 XDrawString 4+9 (13); T12
XCopyArea 4 with Press:1/Release:1/Draw:2, CreateButton:0; T13b explosionGraphic.H 20-call
inventory (3 XCreateBitmapFromData, 5 GC trio, dtor 5+3) + per-frame 1 XCopyArea in explosion.H:45-51;
T18 stage.H actual inventory incl. XCopyArea×2 :258/:269 (window blit) and XCreateBitmapFromData×1;
m1 cite :193; stage.H XFreeFont×4 vs XLoadQueryFont×5 (m18 leak).

---

## G. U27 canvas-core inversion — ruling: REFUTED for v4 (adopted fragments kept)

**Claim (creative, round 1→2):** replace the 3 backend render stacks with a thin software-RGB-framebuffer
core (CPU pixel buffer + 3 dumb blitters); cross-backend parity then holds BY CONSTRUCTION and
byte-diff QA becomes trivial; "3 independent GPU render stacks" is the maximal-complexity corner.

**Ruling: EVISCERATED as a v4 plan correction.** "Parity by construction" is false against the real
X11 server behavior this game depends on — a CPU core would have to RE-EMULATE all of it to keep
SC2 (bit-identical X11 output):
- `XLookupString` keymaps (playingField.H:359/406/587) — layout-dependent translation, not keycodes;
- `XRefreshKeyboardMapping` (:335) — runtime keymap state;
- `XAutoRepeatOn/Off` GLOBAL server state (stage.H:209-210; 7 playingField.H sites; options.H:2548/2550;
  AutoRepeatOn.C:10) — the in-window suppression is server-side by design;
- Expose-driven `stage.Refresh()` (playingField.H :351/:481/:583) — server-driven redraw;
- the 133-X11-call rotator pipeline (rotatorDisplayData.C; default-GXcopy mask GCs + XSetClipOrigin;
  rockGroup.H:184-186) — pre-computed pixmap generation that D14 keeps X11-native.

That is a SECOND FULL REWRITE (and its own QA burden vs the live X server), not "3 dumb blitters".
The inversion is valid only as a **new standalone project**, never as a correction of this plan.
On record: the researcher's own cost model (`cd08284a`) — closed 2D op set, ~2MB buffer worst case,
text = X core bitmap fonts — which supports the standalone-project framing, not v4 adoption.

**Adopted fragments (folded into v4 spec, Section I):**
1. **Typed `pollEvents` seam** — the domain sees only typed events; the GL/VK seam DROPS
   GLFW_REPEAT entirely (exact XAutoRepeatOff semantics; input is per-event, Section B3). Kills U7/U11.
2. **Deterministic headless harness as a Phase-1 PREREQUISITE** (seeded RNG defaulting to current
   behavior + scripted events + per-frame capture + diff) — the U228 workstream.
3. **Minimum visible surface:** X11 + ONE GPU backend (GL) must both ship in v4; Vulkan is behind
   the Section H user gate.

---

## H. Vulkan scope — OWNER (USER) DECISION, unresolved → v4 user-input gate

**Evidence on the record:**
1. `.omo/drafts/rendering-abstraction.md:33` (planner draft, 2026-08-19): "Vulkan is essential
   (user requirement)" — SECOND-HAND; a session attestation recorded by the planner.
2. Zero first-party repo evidence: no Vulkan reference in code, README, makefile, or git history
   outside `.omo/` plan docs (grep-verified this session).
3. Round-3 review's own recommendation was DEFERRAL, rated the "strongest scope argument"
   (review:14, :267) — deleting 9 tasks (T36-T44, ≈20% of plan) plus the GL-vs-VK identity QA.
4. The user asked about the requirement; **no decision was ever taken**.

**Consequences if kept unfixed (all source-verified defects inside the Vulkan phase):**
`vkAcquireNextImageKHR(UINT64_MAX)` hang (plan:604, B4); no `VK_ERROR_OUT_OF_DATE_KHR`/
`VK_SUBOPTIMAL_KHR` path; validation layer enabled without `vkEnumerateInstanceLayerProperties`
(m15); GL-vs-VK identity QA by eyeball (T44, U30); "Vulkan 1.4 (Roadmap 2026 profile)" loader
availability unverified (T25, U15).

**v4 ruling (non-discretionary for the plan agent):**
- v4 must NOT resolve this gate. Phase 4 (Vulkan) is written **self-contained and excisable**:
  no task before it depends on Vulkan; deleting it must leave a complete, passing plan.
- If kept in v4, all six defects above are fixed inside Phase 4 (finite acquire timeout +
  OUT_OF_DATE/SUBOPTIMAL handling; layer-availability check; loader check; T44 via harness byte-diff).
- The plan header carries the verbatim gate: **"USER GATE: Vulkan scope — confirm before executing
  Phase 4. Recorded basis: drafts:33 'user requirement' (second-hand); r3 recommended deferral.
  The GL-side defects (Section I.12) are fixed regardless of this gate."**

---

## I. Decision-complete v4 spec (what the plan agent must produce)

1. **Renumbering (B9):** ONE contiguous sequence across all phases; Phase 1 stops reusing 6-9;
   no dup 13; 10/27-31 exist or are renumbered away; TL;DR count = SC13 count = actual row count
   (recomputed AFTER renumbering). Every cross-reference (D-notes, QA rows, traceability) uses the
   new ids.
2. **Pinned census (B8/B9):** Section F table + 55-symbol whitelist embedded in the verification
   strategy; "Actual calls" lines in every task row re-derived from the census with the specific
   corrections listed at the end of F. `m1` cite = :193. The plan cites ONE total (434) with the
   methodology footnote; "~100"/"~223" disappear.
3. **Traceability (B8):** table rebuilt against the ACTUAL round-3 ids (C1-C3, M1-M16, m1-m24) AND
   the R4 ids (R4-B1…B9, R4-M1…M16, R4-N1…N7, U1-U31 as consumed); per-row status =
   addressed/partial/not-addressed IN v4 with the v4 task pointer. The fictitious A1-A8 scheme is
   deleted. "All findings addressed" is a claim only if every row shows addressed.
4. **Build system (B1/B2/M5/M8/N1/N5):**
   - Task: `CXXFLAGS += -DX11_BACKEND` for the X11 target — FIRST Phase-0 task, precondition for
     every header-guard acceptance.
   - `#ifdef` the 6 unguarded Options sites (playingField.H:69/300/457/565/591/616) + define the
     GL/VK `RunGame` signature (B2).
   - ONE task owns the makefile transition: targets exist before flags drop (fixes U23 T9-before-T24).
   - Exclude compositePixmap.C + rotatorDisplayData.C from GL/VK link, OR move their X11 code into
     X11Backend (D14 fix — lead D: both carry ~20 live X11 calls and are linked into every target).
   - Per-backend object dir/suffix (N5); replace `-w` with targeted warning flags (r3-m12); decide
     and encode `-std` (C++17 for the sstream migration; strstream removal is a C++17 fact — correct
     the plan's C++26 framing).
   - T7 rewritten per N1 (3 real files: playingField.H, stage.H, options.H[X11-only]; seekp-idiom
     rewrite; no fake line cites).
5. **Baseline + harness (B7/N4/N7): Phase-1 PREREQUISITE tasks, before any migration:**
   - Pre-refactor baseline capture: xwd of title/help/shipyard/game-over screens at a fixed
     reachable state, CROPPED to client area (5px border, stage.H:186), stored under a committed
     path; documented as the SC2 reference.
   - Seed injection: stage.H:218-219 `srand` becomes seedable (env var or argv), DEFAULTING to
     `time(NULL)` when unset — current behavior preserved.
   - Deterministic headless harness: seeded RNG + scripted event queue + per-frame capture +
     diff; the 12 harness-dependent QA rows (and T19's "Deterministic test") are rewritten to run
     through it. (Acceptance note: xpra / XAstRunScaled.sh run_scaled is a real second display
     surface — README:7-13 — so baseline capture must be display-normalized or documented as
     X11-local.)
6. **Window & lifecycle (M4/M6/M13/B8-6):** two-pass window init (font metrics before window size);
   real GL/VK size ≈ (max(header,640)+30) × (headerHeight + 2·maxIconHeight + 512 + 60), NOT
   `glfwCreateWindow(640,512)`; `glfwSetWindowPos` (center, stage.H:182-185) + `glfwSetSizeLimits`
   min=max mirroring the X11 PSizeHints pin (playingField.H:539-544); `initWindow` reconciled to the
   success-indicator signature (U21/m12); `glfwSetWindowCloseCallback` + a resource-ownership map
   assigning Display/Window/titleGC/scoreGC/hiScoreGC/defaultGC/font/errorInfo exactly one owner
   (kills the ~Stage/X11Backend double frees; includes the m18 errorInfo XFreeFont).
7. **Engine API v4 (B6/M1/M2/M12 + U19/U27-frag):** 19 methods — DELETE `createTextureFromXBM`
   (route XBM via T23 backend-agnostic decode → `createTextureFromBitmap`); ADD (a) render-target
   primitive (`createRenderTarget/beginRenderTo/endRenderTo`) for T12's Button FBO, or the explicit
   procedural-redraw decision — one or the other, in the API; (b) `setScissorRect` — beginFrame stops
   owning the scissor; HUD (score/title/shipyard/button) drawn OUTSIDE the play-area scissor
   (R4-M3/m3/m14); (c) `pollEvents` — typed events only, GLFW_REPEAT dropped (B3); (d) `TextureId =
   uint64_t` (m20); (e) `getFontMetrics` must also expose per-font `max_bounds` (used by Stage
   layout :145-146 and Button label centering :140/143) — or document the X11-only path;
   (f) `clear()` SPECIFIED to the background GC (GXcopy, black fg) — the dual-GC design:
   backgroundGC for all clears (:202/:312/:638), gxor GC for compositing (:41/:133/:135) (R4-M1).
   **DI:** single ctor per class taking `RenderingEngine&`; no `#ifdef` dual-ctors, no extern global;
   injected into the 11 globals at XAsteroids.C:13-30; Button ctor restated from button.H:160/:208
   real signature (2nd param `Drawable&`).
8. **Input model (B3):** per-event ONLY — a key→action table in the plan: 'e'/'r' = angular velocity
   ± pending-rotation flags consumed on KeyRelease (:408-445); 'o' = NewThrust + AddPermeable
   (:382-385); 'p' = fireThisFrame/firing latch (:378); space = RemoveShip + NewHyper with the
   hyperspace gate (:397-403); 'q'/'n'/'h' = goto/return exits (:508 ResetGameUpdateScoreAndReturn);
   title-state gating (pointerButtonReleased + MotionNotify hover press/release :585, :623-627);
   NO per-frame key polling; NO GLFW_REPEAT (repeat = re-fire bug); frame-clock RE-ARM on resume
   (startTime=ResumePlay :343/:490) + per-frame sync equivalent bounding diffTime (XSync :296/:329
   semantics documented); modal Options loop (options.H:2542-2571) mapped as X11-only, with the
   re-arm specified.
9. **Phase 0 coverage (U4/C1):** ALL 20 unguarded headers with REAL paths and lines (T6 corrected
   to `objects/{enemies,ships,rocks,explosions}/*`; stage.H:11 not :27; button.H:4-5;
   rotatorDisplayData.C:4; AutoRepeatOn.C; playingField.H:18-19 incl. Xutil.h).
10. **Draw order (U25 ruling / R4-M16):** D8 names ONE canonical draw order for GL/VK alpha
    (non-commutative) and states X11 GXor order-invariance (single XSetFunction, playingField.H:135).
11. **Explosion frames for GL/VK:** the 5 frames are init-time composites of 3 stacked bitmaps made
    by CompositePixmap (X11-only per D14) — a Phase-1/2 task must produce the 5 composite frame
    textures for GL/VK via the T23 decode path (no task currently does — validator recon #10).
12. **GL defects fixed REGARDLESS of the Vulkan gate (B5/m17/m19/m10):** request **GL 4.5 core**
    (not 4.6) + explicit error-exit if unavailable; `glViewport` on framebuffer-size change;
    setTransform documented as GL/VK-only (X11 has no transform stack); T20 swap-interval escape
    clause. T21/T22 GL/VK parts move AFTER the event seam (Phase 3+), sequenced coherently with the
    makefile task (R4-M5).
13. **Vulkan Phase 4 (H):** self-contained/excisable + the six-defect fixes; carries the USER GATE
    verbatim.
14. **Options UX + pump (M6/M14/m8):** grayed-out Options button + README note under a REAL task
    number; the hand-rolled pump documented as the X11-only second event island (NO XtAppMainLoop —
    it does not exist in the repo).
15. **QA gates (M9/M10/M11/N7/U29/m24):** F1 = 55-symbol whitelist (M10); font gate replaced by
    0-px-outside-text + masked screenshots (M11); xwd crop to client area (m9/N7); T44 = harness
    byte-diff non-text regions (U30); resize rows DROPPED in favor of fixed-size parity (N7,
    recommended) or resize added for all backends — one or the other, encoded; NUMERIC LANE added
    (U29/README purpose): edge-case assertions over intersection2d.H (swept-intersect sort
    :754-763; mid-pass removal shipGroup.H:253-265; pass-count snapshot intersection2d.H:766-784) +
    gravity FP guards (playingField.H:216-228, zero-distance :222) — at minimum a randomized-angle
    suite (m24) + seeded numeric runs through the harness.
16. **score.H I/O (N3):** DECIDE — fix (recommended: getpwuid NULL-check, drop/fix the /Volumes
    candidate, increment numScores only on successful read) or explicit fixture-freeze + exclude the
    hi-score screen from pixel-compare. Either way, stated in the plan.

---

## J. What v3 got RIGHT (verified anchors — v4 must NOT disturb)

- **T14–T17 per-file XCopyArea/XSetClipOrigin line counts are ALL EXACT** and stay verbatim:
  rockGroup 186/196/561 + clip 185; shipGroup 237/459/563/610/632 + clip 562/631; enemyGroup
  230/288; enemyBulletGroup 143; bullet.H 81/104 + clip 103; shipBulletGroup 132/175.
- **T11 (playingField.H) "0 XCopyArea, 3 XFillRectangle (:202/:312/:638), gxor GC @ :135"** — verified
  correct (only the XDrawString split/count is wrong).
- **T12 XFillPolygon×4 / XDrawLines×6 / XDrawImageString×2** — verified; only the XCopyArea
  attribution is wrong. T13a (shipYard) 4 XFillRectangle + 2 XCopyArea counts correct (scope
  incomplete per M4).
- **D7 wrap semantics correct** (box.H:251-288: teleport only when fully off the opposite side;
  pixmap oversize, playingField.H:131).
- **D12 font list exact** (stage.H:134-138) — minus the m21 correction (errorInfo = X11-only).
- **D13 XBM counts verified** (32 files in bitmaps/; default build 28 unique; GL/VK needs 21) —
  noting the makefile XBM dependency list is STALE vs the default build (researcher #22); T19 must
  decode both `_CORP_LOGO_` variants.
- **D4 pacing cites all verified** (playingField.H:98 uSecondsPerFrame=62500; :504-505 diffTime
  sleep; :672 AlterFramesPerSecond; options.H:729/3086-3088 widget).
- **D16's 4 XNextEvent line cites (333/340/453/572) are the correct lines** — the surrounding
  input MODEL is wrong (B3), the cites are right.
- **D3 = GXor at playingField.H:135** — correct (and the single XSetFunction in the repo).
- **plan:191 "11 globals at XAsteroids.C:13-30"** — accurate (researcher retracted its own
  ":31" correction; PlayingField is at :30).
- **Architectural shape endorsed by all five members:** X11/GL/VK backends behind one abstract
  interface is the RIGHT design — every load-bearing decision is fixable without abandoning the
  abstraction. v4 is a repair of v3, not a redesign.

---

## Provenance & runtime caveat

- Round-4 receipt assembled by the hyperplan lead from, all on disk unless noted:
  - `/tmp/opencode/hyperplan-r4-round1-bundle.md` (round 1, run 95383cb5, reconstructed),
  - `/tmp/opencode/hyperplan-r4-round2-input.md` (canonical U1–U31 register),
  - `/tmp/opencode/hyperplan-r4-round2-output.md` + `hyperplan-r4-round2-part2.md` (R2 verbatim
    deliverables + census reconciliation + parked disputes),
  - `.omo/reviews/round4-validator-findings.md` (validator durable, self-audited citations),
  - `/tmp/opencode/hyperplan-r4-round2-researcher-census.md` (final machine census, 182 lines),
  - `/tmp/opencode/hyperplan-r4-lead-adjudication.md` (lead tie-break greps A–F).
- Every file:line/count was re-verified against source at HEAD of `rendering-abstraction-plan`
  on 2026-08-21; the codebase was untouched during the review (git clean except untracked `.omo/`).
  Reproduce the pinned census with the pipeline described in Section F (methodology items 2-3).
- **Runtime caveat:** the lead runtime died twice during this hyperplan (runs `effda855`,
  `95383cb5`), and the team's message replay loop re-delivered the round-2 brief to members
  repeatedly (the validator observed 17+ replays of one correlationId). All member deliverables were
  captured in the durables above before/after the failures; where a finding and a durable disagree
  with later source re-verification, the source verification wins (notably: the validator's final
  N2 correction — the :647-663 RANGE was fine; the split was the error; and the U9/U24 mechanism
  corrections: COMPILE not link; no XtAppMainLoop; zero extern declarations).

**— END OF ROUND-4 RECEIPT —**