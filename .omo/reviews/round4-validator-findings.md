# Round-4 Hyperplan — Validator Member Findings (Round 2 Cross-Attack)

Member: validator (integration tester / failure-mode attacker)
Date: 2026-08-21
Team run: a369a3a1-b0c3-4cd2-9c53-b1384caf4b74
Input brief: /tmp/opencode/hyperplan-r4-round2-input.md (unified set U1-U31 + 4 named probes)
Method: every U-claim re-verified against source in /home/archerc/code/XAst (HEAD of branch
rendering-abstraction-plan). Plan = .omo/plans/rendering-abstraction.md (v3, 750 lines).
Where a finding and the code disagree, the code wins and is cited below.

## DELIVERY NOTE

Delivered to lead via team messages (round-2): full verdicts msg 2fc87006; probe detail
c0d33fda / 717a414f; self-contained final stance 60f33e10; acknowledgment of duplicate
dispatch loop 9186847e. This file is the durable record (the lead's dispatch loop re-sent the
brief 9+ times with the same correlationId, suggesting intake-side loss; this file survives it).
Team task #1 ("[validator] Round-2 cross-attack...") marked completed.

VERIFICATION LOG (2026-08-21): every file:line citation in this document was systematically
re-verified against source in /home/archerc/code/XAst after the file was written — batches:
(1) U17 stage.H 188/196/204/212/258/269; (2) probe-2 QA-line inventory (47 QA: lines) +
explosion.H:45-51; (3) N2 XDrawString callsites (525/528/530/532 + 647/649/651/653/655/657/
659/661/663) + plan:413 verbatim; (4) full stage.H/playingField.H/XAsteroids.C citation
cluster; (5) explosionGraphic.H (94/97/100 bitmap, 103-115 GC, 121-124 dtor, XCopyArea=0),
button.H (63/236/248/261/269), rockGroup.H (184-186/196), options.H (536/2568/3870 LOC),
score.H (42-56/108-110), plan:78/548/604. 4 citation errors found+fixed during audit (U17
196/204, probe-2 gate count, N2 enumeration). All citations now confirmed.

## VERDICT TABLE (U1-U31)

| U-id | Verdict | Evidence (file:line) |
|------|---------|----------------------|
| U1 | STANDS | plan:726-750 ids C1-C4/M1-M7/A1-A8 match NEITHER persisted review (r2 C1-C5/M1-m5; r3 C1-C3/M1-m16/m1-m24); plan-table "C1: GXor" and "C2: font list" appear in neither file; "all addressed" unverifiable by construction |
| U2 | STANDS | Phase 0 rows 1-10 (plan:301-375); Phase 1 reuses 6-9 (plan:379-413), skips 10, duplicates 13 (plan:429 shipYard / plan:437 explosionGraphic); Phase 3 = 24,25,26,32-35 (27-31 absent, plan:530-584); 47 rows vs TL;DR "44" (plan:7) vs SC13 "42" (plan:722) |
| U3 | STANDS (reconciled, see count below) | methodology section below; F1 "~100" ≈ render-only; ~223 and ~469 match no reproducible slice |
| U4 | STANDS (stronger than stated) | plan T6 (plan:341-342) cites 8 headers with WRONG dirs (gamePlay/* vs actual objects/{enemies,ships,rocks,explosions}/* except shipYard.H) and WRONG lines (plan claims 15/14/16/12/12/12/11/11; actual enemyGroup.H:5, shipGroup.H:2, rockGroup.H:4, bullet.H:11, shipBulletGroup.H:2, explosion.H:4, enemyBulletGroup.H:2, shipYard.H:4); the 5 unguarded headers verified: playingField.H:18-19 (Xlib.h + Xutil.h), explosionGraphic.H:4, movableObject.H:8, rotator.H:5, frameList.H:4 — no task covers them |
| U5 | STANDS (consequence worse than stated) | makefile:1-4 has ZERO -D flags (CXXFLAGS=-I/usr/include/X11 -O3 -w); no plan task defines -DX11_BACKEND; after Phase 0 the X11 TARGET ITSELF fails to compile (guards strip all X11 code) ⇒ T9 "X11 target unchanged" false, T19/F2 baseline unbuildable |
| U6 | STANDS | no baseline-capture task exists; repo has no baseline (screenShots/*.gif are marketing images); game non-deterministic: srand(time(NULL)) stage.H:219; argv[1] = start LEVEL not seed (playingField.H:589) |
| U7 | STANDS (reinforced) | SC10 (plan:719) demands zero raw X11 outside backend; D16 (plan:250) mandates X11 backend keeps XNextEvent UNCHANGED in domain code: XNextEvent×4 (playingField.H:333/340/453/572) + XEventsQueued:332 + XLookupString:359/406/587 + XAutoRepeat×7 (:338/342/468/477/559/577/579) + XSelectInput:560 + XMapRaised:566 + XSetWMProtocols:558 + XSync:296/329; F1's grep pattern (plan:689) catches only XSetFunction of these families — verification gate blind to the violation D16 mandates |
| U8 | STANDS | extern RenderingEngine* global (plan:190); no mock/DI path; all 11 globals construct with engine state |
| U9 | STANDS | options.H = 3870 LOC, XtCreateApplicationContext :536, XtDispatchEvent :2568; D1 (plan:41) "identical feature parity" vs D9 (plan:161) "Options unavailable on GL/VK" is an internal contradiction; SC9 (plan:718) quietly concedes it |
| U10 | STANDS | plan:208: #ifdef X11_BACKEND dual-ctor approach for 6 classes (Button, ShipYard, ExplosionGraphic, RotatorDisplayData, Stage, Bullet) confirmed in plan text |
| U11 | STANDS (strongest verified evidence) | input is PER-EVENT, not polled. One-shot KeyPress side effects: 'o'→NewThrust()+thrusting=on (playingField.H:382-385); space→RemoveShip()+NewHyper() (:389-391); 'e'/'r'→AngularVelocity ± pending flags (:362-381); KeyRelease consumes pending flags & zeroes angular velocity (:408-445). Auto-repeat SUPPRESSED in-window: XAutoRepeatOff :559/:342/:468, XAutoRepeatOn on leave :338/:477. D16 "key states stored as boolean array" (plan:248) + M3 "game polls key state each frame... No additional API surface needed" (plan:253) are FALSE ⇒ GLFW_REPEAT re-fire = repeated NewThrust / repeated random hyperspace / pending-flag inversion flipping rotation at release |
| U12 | STANDS | plan:604 still mandates vkAcquireNextImageKHR UINT64_MAX; 0 grep hits for VK_ERROR_OUT_OF_DATE / VK_SUBOPTIMAL_KHR in entire plan |
| U13 | STANDS | plan:548 hard-gates "glGetString(GL_VERSION) reports 4.6 core"; workload (quads, line strips, R8 textures, FBO, stb text) needs ≤4.5; no fallback ⇒ 4.5-capped Mesa/Intel hits M12 exit(1) with no alternate path |
| U14 | STANDS | ~Stage (stage.H:222-236) XFreeFonts scoreInfo/hiScoreInfo/buttonInfo/titleInfo — NOT errorInfo (allocated :138, freed nowhere); leaked every run; no task addresses |
| U15 | STANDS | no loader-version / vulkaninfo / availability check in any task; T25 (plan:536-542) assumes vendorability |
| U16 | STANDS (reconciled) | actual explosionGraphic.H: XCreateGC×5 (:103/106/109/112/115), XSetClipMask×5, XSetGraphicsExposures×5, XCreateBitmapFromData×3 (:94/97/100), dtor XFreeGC×5 / XFreePixmap×3 (:121-124), XCopyArea=0. Per-frame cost = 1 XCopyArea + 1 XSetClipOrigin in Explosion::DisplayFrame (explosion.H:45-51). T13's "8 XCopyArea / drawTextureMasked ×8 / 3 GCs / 3 XFreePixmap" (plan:440) fabricated; three irreconcilable counts (8 vs 18 vs actual 20 init + 2/frame) |
| U17 | STANDS | stage.H: XCreateBitmapFromData exactly 1 (:188, window icon); XFillRectangle 0; XCopyArea 2 (:258 DrawShipYard, :269 DrawPlayingField — the real window-blit pair, missing from T9/T18 inventories). "4 mask bitmaps at 188/196/204/212" fictional (verified actual: 196=XCreateGC(hiScoreGC), 204=XSetForeground, 212=action.sa_mask=0) |
| U18 | STANDS as gap-flag (scope call = user's; reviewer opinion ≠ user evidence; repo grep: zero Vulkan outside .omo/) |
| U19 | STANDS | T23 (plan:521-523) routes GL/VK via createTextureFromBitmap; X11 keeps native XCreateBitmapFromData (D14); no task in ANY backend calls createTextureFromXBM → 1 of 20 API methods dead |
| U20 | STANDS | plan:227 concedes PCF-vs-TTF metrics "will differ"; SC3/F2 (plan:690) + T33 (plan:566) still gate "within 10% × 5 fonts × 3 metrics" — false precision |
| U21 | STANDS | plan:78 `virtual void initWindow(...)` vs plan:210 "initWindow() returns a success/failure indicator" — unreconciled, M12 unimplementable |
| U22 | STANDS | D5 API (plan:73-121) has NO render-target primitive; T12 (plan:422) mandates FBO render-to-texture for Button faces — unexecutable against the frozen 20-method API |
| U23 | STANDS | T9 (Phase 0, plan:363-368) drops -lXm/-lXt from XAsteroids-GL/-VK targets that don't exist until T24 (Phase 3, plan:530-536) — state machine incoherent |
| U24 | STANDS (mechanism corrected: COMPILE not link) | Options is NOT a global (XAsteroids.C:13-30, no Options) — it is a PlayTheGame LOCAL: playingField.H:565 `Options options(argc,argv);`. Unguarded sites: :69 (decl RunGame(Options&)), :300 (definition), :457 + :616 (options.RealizeWindow()), :565 (construction), :591 (RunGame(options) call) = 6 sites. T2's #ifdef-out of options.H ⇒ compile errors in every TU (header-only project; XAsteroids.C:12 includes playingField.H). No task #ifdefs any of the 6 sites |
| U25 | EVISCERATED (X11 half) — STANDS (GL/VK half) | "GXor compositing is order-sensitive" is mathematically FALSE on X11: per-pixel XOR against fixed source pixels is associative + commutative (playingField.gc GXor, playingField.H:135) — object draw order CANNOT change the X11 image. Real requirement: D8 (plan:154-159) must fix ONE canonical order for GL/VK alpha blending (non-commutative) and state X11 is order-invariant |
| U26 | STANDS as gap | "User requirement" (draft frontmatter) has no provenance in repo (no Vulkan mention in code/README/git) |
| U27 | EVISCERATED | "parity by construction" is FALSE vs actual X11-server behavior: XLookupString keymaps (playingField.H:359/406/587), XRefreshKeyboardMapping (:335), XAutoRepeatOn/Off GLOBAL server state (stage.H:209-210), Expose-driven stage.Refresh() (:351/481/583), 133-X11-call rotator pipeline (rotatorDisplayData.C, default-GXcopy mask GCs + XSetClipOrigin, rockGroup.H:184-186) all need CPU-side re-emulation. Inversion = a second full rewrite, not "3 dumb blitters" — valid only as a NEW project, not a plan correction |
| U28 | STANDS (quantified) | see PROBE 2 below: 15/16 of the plan's QA scenarios + 27/47 cross-task QA lines not agent-executable as written; ZERO tasks build the harness |
| U29 | STANDS | plan QA (plan:269-285) = 100% visual; zero assertions over intersection2d.H (998 LOC; swept-intersect sort :754-763) or FP-heavy gravity (playingField.H:216-228, zero-distance guard :222); T20 update/render split is exactly where float-order drift enters; README:3 states repo purpose = find compiler/FP bugs — no test lane for it |
| U30 | STANDS as written | T44 (plan:649-655) "side-by-side GL vs Vulkan — visual identity" = human-in-the-loop, no capture method/tolerance/regions; SC3 (plan:712) inherits. Fix = reuse U28 harness, byte-diff non-text regions |
| U31 | STANDS (own, strengthened — see PROBE 3) | zero test infra verified; score.H/options.H parse paths verified UNCHANGED by all 47 tasks; makefile has no -std (plan:352's "-std=c++17" acceptance has no makefile backing) |

Tally: 28/31 STANDS (U24 mechanism corrected to compile-not-link; counts reconciled in U3/U16),
2 EVISCERATED (U25 X11 half, U27), 1 mixed (U25: GL/VK half stands).

## U3 RECONCILED COUNT (methodology)

`grep -oE` over 47 X11 API families (XDrawString|XCopyArea|XFillRectangle|XFillPolygon|
XDrawLines|XDrawPoint|XCreateGC|XFreeGC|XCreatePixmap|XFreePixmap|XCreateBitmapFromData|
XLoadQueryFont|XFreeFont|XAllocColor|XSetFunction|XSetFont|XSetForeground|XSetBackground|
XSetLineAttributes|XSetClipMask|XSetClipOrigin|XSetGraphicsExposures|XTextWidth|XTextExtents|
XOpenDisplay|XCloseDisplay|XSelectInput|XMapRaised|XRaiseWindow|XSync|XSetWMProtocols|
XSetWMProperties|XAutoRepeatOn|XAutoRepeatOff|XNextEvent|XEventsQueued|XLookupString|
XRefreshKeyboardMapping|XDestroyWindow + window-hint allocators), 28 files scanned:

Total = 410 X11 API calls in 15 files. Per file: rotatorDisplayData.C 133, playingField.H 64,
stage.H 49, options.H 41, button.H 41, shipYard.H 29, explosionGraphic.H 20, compositePixmap.C 11,
shipGroup.H 8, rockGroup.H 4, bullet.H 3, shipBulletGroup.H 2, enemyGroup.H 2, explosion.H 2,
enemyBulletGroup.H 1. The other 13 scanned files = 0 (pure logic).

Render-primitive slice (XDraw*/XCopyArea/XFill* only, 13 files) = 91.

So: F1's "~100" ≈ the render slice (91). Plan:260/:295's "~223" and researcher's "~469" match no
reproducible slice of this methodology. Additionally, F1's grep gate (plan:689: XDraw|XCopy|XFill|
XSetClip|XSetFunction) can only detect the render families — 319 of the 410 calls (XCreateGC,
XFreePixmap, XNextEvent, XCreateBitmapFromData, XSetWMProperties, XAutoRepeat, XSync, ...) fall
OUTSIDE its pattern; the acceptance gate cannot detect its own domain.

## PROBE ANSWERS (lead-named)

### PROBE 1 — U24: exact line + #ifdef gap
`Options options(argc,argv);` at **playingField.H:565** — a local inside PlayTheGame
(playingField.H:538), NOT a global (XAsteroids.C:13-30 contains no Options).
Build breakage = COMPILE, not link: T2 (plan:310-314) #ifdefs options.H behind X11_BACKEND;
the class vanishes on GL/VK builds; playingField.H references survive unguarded at :69
(declaration `Boolean RunGame(Options& options);`), :300 (definition), :457 and :616
(`options.RealizeWindow()`), :591 (call `RunGame(options)`). Every TU includes playingField.H
(XAsteroids.C:12) ⇒ every TU fails to compile. No task in the plan #ifdefs any of these 6 sites,
and D9/D16/T20/T22 never mention a GL/VK RunGame signature.

### PROBE 2 — U6/U28: how much QA is verifiable by an agent
Plan has 47 "QA:" lines (grep-verified). Classification:

- AGENT-VERIFIABLE TODAY: ~20 — 14 grep/build gates (plan:307/314/321/329/337/345/353/367/374/
  383/392/534/541/673), XBM dimension check (plan:525), and runtime crash/validation-style
  counters (plan:593/599/606/613/623 — the last need GPU hardware to run).
- NOT VERIFIABLE (27):
  (a) 4 need a pre-refactor baseline + display + deterministic game state: plan:360 (T8 "identical
      behavior"), plan:407 (T9 xwd `compare -metric AE=0`), plan:488 (T19 full session), plus
      F1/F2 (plan:689-690).
  (b) 12 need a seeded-input deterministic gameplay harness (seed gary_rand, stage.H:20-25 +
      scripted events + per-frame capture): plan:415 (T11 play game), :426 (T12 click buttons),
      :434 (T13 lose ships + ASan), :442 (T13b trigger explosion), :449 (T14 rocks 8 angles),
      :456 (T15 fly+thrust), :462 (T16 enemies), :471 (T17 shoot+hit), :504 (T21 edge wrap),
      :515 (T22 focus/resize identity), :654 (T44 GL-vs-VK), :665 (T45 full session).
  (c) 9 are human-visual only, no tolerance: plan:558/575/583/631/639/646/680 + visual halves of
      :566/:496.
  Plus plan:548 (4.6-core gate) fails outright on 4.5-only drivers (r3-m17 / U13).

The 16 named QA scenarios (plan:270-285): 15/16 non-verifiable as written. Only #6 (plan:275,
"Options dialog is documented as unavailable on GL/VK — verify README") is a grep. Notably #15
(window resize, plan:284) is UNTESTABLE on the X11 backend — the window size is pinned via
PSizeHints min=max (playingField.H:539-544) and GLFW's resize path would be behavior the X11
backend never exercises, so "identical on all backends" has no X11 ground truth.

ZERO tasks build the harness. The game is non-deterministic (srand(time(NULL)) stage.H:219;
argv[1] = start level, playingField.H:589; no seed path anywhere) ⇒ even a captured baseline
cannot be byte-compared across a full game. U28 is a MISSING WORKSTREAM (harness task), not a QA gap.

### PROBE 3 — U31: does the refactor change hiScore/XAstOpts parse semantics
NO. Verified untouched by all 47 task rows: score.H :42-56 (custom char parser, ≤8 chars),
:71-101 (filename candidate cascade /Volumes/... → $HOME/... → cwd/...), :113-129 (insertion
sort of score array), :132+ (UpdateTopTen read/rewrite); options.H :2596-2640 (ReadWriteOptions
via XmStringGetLtoR). Parse semantics are unchanged — the correct risk statement is "unchanged,
and no validation exists; refactor verified not to perturb them."
HOWEVER the one format-adjacent task, T7 (plan:348-354), is mis-aimed AND non-compilable
(see N1).

### PROBE 4 — U11×U7: the actual GL/VK event path
Plan specifies: GLFW callbacks set flags (inWindow, mouseDown/mouseX/mouseY, key boolean array —
plan:245-249) which feed a "main-loop state machine" (T22, plan:507-516). **That state machine
is nowhere in the plan.** D16 (plan:244-253) maps transports only; it never specifies how flags
become the existing per-event behavior.

The repo's only input-consuming code is per-event handler code with X11 suppression semantics:
- One-shot KeyPress side effects (playingField.H:359-445): 'e'/'r' set AngularVelocity +
  leftRotPending/rightRotPending; 'o' → NewThrust + AddPermeable (:382-385); space →
  RemoveShip + NewHyper (:389-391); 'p' sets fireThisFrame + firing; 'q'/'n'/'h' return/goto.
- KeyRelease consumes pending flags and zeroes angular velocity (:408-445) — release-time
  semantics that no "boolean key array" model can express.
- Auto-repeat suppression: XAutoRepeatOff in-window (:559/:342/:468), XAutoRepeatOn on leave
  (:338/:477) — in-window repeats are SUPPRESSED at the server, so the game never sees repeats;
  GLFW delivers them (GLFW_REPEAT) and D16/M3 (plan:253) call this "harmless" — false:
  re-fired 'o' = repeated NewThrust, re-fired space = repeated random hyperspace, re-fired 'e'
  = pending-flag inversion flipping rotation at KeyRelease.
- XLookupString keymap translation (:359/:406/:587) — layout-dependent; boolean GLFW keycodes are
  not a substitute without an explicit key→action map (none exists in the plan).
- The NESTED MODAL loop (playingField.H:452-489) blocks inside the domain RunGame() while the
  Options dialog runs its OWN event pump (XtDispatchEvent, options.H:2568); only on
  ButtonRelease does `startTime=ResumePlay(intersector)` (:490) re-arm the frame clock. D8's
  "new flow" (plan:154-159) has no re-arm concept and the API has no sync/flush method matching
  the per-frame XSyncs (playingField.H:296/329) that bound the diffTime window (plan:503-505).

So the true GL/VK event path = [GLFW callbacks] → [UNSPECIFIED STATE MACHINE, missing] →
[per-event semantics, unmapped] → [modal Options loop, un-#ifdef'd (Probe 1's 6 sites)] →
[missing frame-clock re-arm]. T22's acceptance ("behave identically", plan:515) has no semantic
contract. U7 and U11 are the same defect seen from two directions.

## NEW FINDINGS (max 2, both source-verified)

### N1 (MAJOR, build-correctness): T7 strstream migration non-compilable as specified
plan:348-354. (i) Cites playingField.H:798 (istrstream) and :812 (ostrstream) — the file is 676
lines; `istrstream` is ABSENT from the entire repo (grep -rn istrstream → 0 hits; only
ostrstream exists). (ii) Real ostrstream sites: playingField.H:14 (include) + :519,
stage.H:6 (include) + :240 + :283, options.H:6 + :3112/:3125/:3859/:3865 (X11-only, deferred by
D9 — fine). T7 covers only playingField.H. (iii) The mechanical rename FAILS:
`strout.seekp(0)` at playingField.H:524, stage.H:245, stage.H:287 is an ostrstream-only
buffer-reuse idiom (reset the in-memory buffer); std::ostringstream has NO seekp
(append-only). The T7 rename as specified produces compile errors and silently changes
DrawScore()/Refresh() formatting unless the idiom is rewritten (e.g. snprintf into fixed
buffers, or ostringstream with .str() per use). (iv) makefile has no -std flag (only
`-I/usr/include/X11 -O3 -w`); T7's acceptance "Compiles with -std=c++17" (plan:352) has no
makefile change behind it, and no task adds -std at all.
Fix: rewrite T7 scope = 3 real sites + seekp-idiom rewrite + add explicit -std=... to makefile.

### N2 (MAJOR, fabrication cluster extension + self-contradiction): T11 XDrawString split
fabricated; plan claims 13 and 14 simultaneously. plan:413: "13 XDrawString (RunGame:525-532 = 8,
GenHelpScreen:647-663 = 5)". Actual (grep -n XDrawString playingField.H): RunGame game-over
region = 4 calls (:525/:528/:530/:532); GenHelpScreen = 9 calls (:647/:649/:653/:655/:657/:659/
:661/:663/:664 — note :664 missing from both the plan's range AND its count). Total 13 right,
split 8/5 wrong, line range misses :664. Worse: the v3 traceability table (plan:737, M2 row)
"fixed" this same file's count to "14 XDrawString" — the plan simultaneously claims 13 (T11,
:413) and 14 (:737) XDrawString in playingField.H. r3-C2 had already verified the true 4+9
split; v3 introduced the regression instead of applying it. The fabrication cluster (U16/U17)
extends to T11.

## ROUND-1 (RECON) FINDINGS STILL VALID — carried for the record
1. GL/VK header/score/shipyard/button CLIPPED by the plan's own scissor: D8 (plan:147-159) sets
   scissor to play area in beginFrame, then draws score/shipyard/title (plan:158) — all of
   scoreX/scoreY (stage.H:168-171), title (stage.H:161-162), shipyard (:172-175), button
   (:163) sit OUTSIDE the 640×512 play-area rect (playAreaY = headerHeight+shipYardHeight+35,
   stage.H:177). No task clears/resets scissor; API has no setScissor (r3-m14 absorbed into
   beginFrame — which IS the bug). GL/VK frame 1 = hollow shell.
2. Window-creation circularity: initWindow(w,h) called in main() BEFORE globals (D11,
   plan:189-199) but windowWidth/windowHeight derive from font metrics loaded INSIDE initWindow
   (D15, plan:240-241; stage.H:134-159). T26 hardcodes glfwCreateWindow(640,512) (plan:545) while
   the real X11 window is (max(header,640)+30) wide (stage.H:151-153) and windowHeight (stage.H:159).
   X11 also centers on root (stage.H:182-185) and pins size (playingField.H:539-544) ⇒ GL/VK need
   glfwSetWindowPos + glfwSetSizeLimits; no such method in API, no task.
3. Vulkan guaranteed hang: U12 + 0 out-of-date handlers; T45 "window resize" (plan:660) has no
   implementing task; no GL glViewport-on-resize anywhere.
4. Exit-path double-free: ~Stage (stage.H:222-236) frees 4 GCs + icon + window + 4 fonts +
   XCloseDisplay; D3/D15 move window/GC creation into X11Backend; M12 calls shutdown() from
   main; NO task assigns ownership of Display/Window/titleGC/scoreGC/hiScoreGC/defaultGC
   (X11Backend needs exactly titleGC/scoreGC/hiScoreGC/defaultGC for per-font XSetFont in
   drawStringOpaque). One destructor frees what the other owns.
5. Makefile .o collision across BACKEND switches: T24 (plan:530-536) specifies per-target LINK
   flags only; X11 and GL/VK targets reuse the same .o names (makefile:6-14) with unchanged
   source timestamps ⇒ `make BACKEND=X11` then `make BACKEND=GL` relinks stale X11 objects
   (or mixes backends). No per-backend object dir/suffix.
6. RunGame(Options&) + earlyExit return contract (playingField.H:69/300/591) undefined for GL/VK
   (see Probe 1/4).
7. Frame-clock re-arm (Probe 4, :490) and per-frame XSync (:296/:329) absent from D8's new flow
   and from the API (no sync/flush method).
8. Signal handler references stage.display (stage.H:296-305) — dead on GL/VK after D11; never
   #ifdef'd. M3's XAutoRepeat line refs (plan:253 "243/500") fabricated (actual Off: 342/468/
   559/579; On: 338/477/577; + stage.H:224/298, options.H:2548/2550).
9. T12 still carries the r3-C3 error "CreateButton:14 init copy" (plan:423) — CreateButton has 0
   XCopyArea (button.H:63-145); actual PressButton:1 (:236), ReleaseButton:1 (:248),
   DrawButton:2 (:261/:269).
10. QA tooling: xwd -root captures window decorations (X11 border 5px, stage.H:186) which GLFW
    windows differ on ⇒ F2 must crop to client area; display-dependent QA not CI-friendly (r3-m9).
11. Numbering/provenance: U1/U2 verified (see table).

## SUMMARY FOR LEAD (integration input)
- 28/31 U-findings stand; U25's X11 half and U27 are wrong (with stronger counter-evidence above).
- Plan's lethal gaps in integration order: (1) GL/VK input model mis-specified (U11+U7+Probe 4:
  state machine missing, per-event semantics unmapped, repeat re-fire bugs) + GL/VK build breaks
  at compile (U24/Probe 1: 6 unguarded Options sites); (2) build-system incoherence (U5 no
  -DX11_BACKEND, U23 T9-before-T24, N1 T7 non-compilable, .o collision); (3) QA verification
  unexecutable for ~57% of its own gates (Probe 2: 27/47), zero baseline/harness workstream
  (U6+U28), non-deterministic game makes byte-pixel QA unattainable without seeding.
- Count ground truth for U3: 410 total / 91 render-only, 15 files; plan must pick ONE + methodology
  and fix F1's grep gate (blind to 319 of the 410 calls).
- New: N1 (T7), N2 (T11 8/5 fabricated + 13-vs-14).