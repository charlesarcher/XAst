# Momus Review — Round 10 (maximum-parallelism unit extraction)

- **round_id:** 10 · **launch_id:** launch-r10-momus-01
- **Target:** `.omo/plans/rendering-abstraction.md` (v6.3 FINAL)
- **Pre-flight:** sha256 `08f0e8c28222328de15280f77d71f137857fdaddb9043b916fae48a59551bff1` ✓ (exact) · 1179 lines ✓ · 253024 bytes ✓ — **MATCH, review proceeded.** Fresh full re-read from disk this round (re-read rule honored).
- **Scope honored:** read-only everywhere except this receipt. Settled owner decisions treated as FIXED (KEEP VULKAN plan:7/:798; D9 ImGui seam plan:92-99; S2/D17.4 letterbox plan:187). No gate weakened, deferred, or deleted anywhere below; B9 contiguity inviolable — every split below is lettered seams INSIDE existing rows (13a/13b precedent, plan:627/:1154).
- **Baselines consumed:** round-8 receipt (§2-§5, scorecard S1-S11) + round-9 receipt (M1-M4, N1-N6, watch-items) — round-9 used only as cross-check; all round-10 derivations below were re-derived independently from plan + sources.

## 1. Baseline verification — round-8 claims CONFIRMED / REFUTED / REFINED

| # | Round-8 claim | Verdict | Evidence |
|---|---|---|---|
| B1 | Hoists 26→W1, 30→W2 | **CONFIRMED** | In-body amendments plan:719 ("MAY execute as early as wave W1"), plan:751 ("as early as wave W2"); sole prereq = task 1 verified in both Change texts |
| B2 | Nine-wide W6; task 15 sole real x11Backend.H writer; 14/16/17/18 impls dissolvable into 12; 19-22 zero-edit | **CONFIRMED as intent / REFINED** | plan:616 ("all 27 methods implemented as pass-through") supports dissolution; BUT plan:575 "(implemented at 17-18)" + Files cards :630/:651/:658 still instruct header impls — the R9-M3 contradiction cluster. Unit cards below encode the resolution: impls land at T12a; W′6 carries ONE header writer (T15) |
| B3 | Three-wide W13 via 32a/33a/34a disjoint groups | **CONFIRMED** | Wave notes in-row plan:765/:772/:779 + Appendix :902; groups are method-disjoint; serial fallback +2 stated |
| B4 | 44a/44b and 45a/45b in-row seams | **CONFIRMED / REFINED** | plan:848-854, :855-857. REFINEMENT: 45a appears in NO Appendix wave (R9-M4) — scheduled here at W′3; 45b further split 45b-i (X11 lane) / 45b-ii (GL parity leg) |
| B5 | Barrier exemptions: 29 build-only, 30 vendoring | **CONFIRMED and EXTENDED** | plan:735 exempts 29/30 from the 28-barrier. EXTENSION: 29's own deps (1 + 30a's glad.c + 23-green) never require 28, so T29 floats to ∥T24 (W′8) — round 8 kept it at W11 out of conservatism |
| B6 | Critical path 25 waves (VK kept) / 20 (excised) | **REFINED → 23 / 17** | Mechanisms: 10∥12 share a wave; 29 floated (B5); 45b-i hoisted to ∥28; 46 split per-backend and removed from the spine (46a∥Phase-4, 46b∥47); 44b∥43. Full derivation §5-§6 |
| B7 | VK 37-42 "honestly cumulative — NOT split" | **CONFIRMED for gates / REFINED for authoring** | Swapchain←instance/device (plan:811), acquire-loop←swapchain (:817), passes/framebuffers←swapchain images (:823), pipelines←render pass (:829): every ACCEPTANCE is chain-serial. Authoring-only floats exist (T39a sync objects need device only, :817; T41a offline glslc assets, :829) but both write vkBackend.H and move no gate → net wave saving 0. Serial recommendation UPHELD (§ST-1) |
| B8 | 24↔25 atomic (function-level collision); 47 atomic terminal | **CONFIRMED** | plan:705/:712 both rewrite RunGame core; 47's greps "only meaningful at terminal state" (:872) — no split proposed |
| B9 | R8-N4 near-hunk note (2 vs 5 on playingField.H) | **CONFIRMED applied / REFINED** | Note present plan:529. Same pattern UNFLAGGED at: stage.H :6 (T5b strstream) vs :11 (T2a guard) — 5-line gap; options.H :6 (T5c) vs :8 (T2a) — 2-line gap; plus T6a's playingField.H:98 hunk joins the W′1 set → **R10-N1** |
| B10 | Wave-pair file safety (W2 {9,30}, W16 {37,44a}) | **REFUTED as incomplete in R8 / CLASSIFIED** | Both pairs co-write `makefile` (9: harness target :594; 30: vendor -I :750; 37: VK rule :803; 44a: GL menu units :849). Hunk-disjoint regions of a 22-line file — probable auto-merge, but must be sequenced-commit classified. Independently derived here; already logged as R9 watch-item → **R10-N2** |
| B11 | Arithmetic (186/205/207/229/434; 27=21+6) | **CONFIRMED** | Replicated: 8+66+41+29+22+4+8+3+5=186 ✓; 146+11+47+3=207 ✓; +22=229 ✓; 229+205=434 ✓; API sketch groups 3+2+6+5+4+5+2=27 ✓ |

## 2. Stress-test rulings (mandated attack list)

- **ST-1 VK 37-42:** serial UPHELD (B7). Splitting shader-authoring/render-pass design behind stub interfaces would violate nothing ("all Vulkan defects fixed inside Phase 4" stays intact — B4/m15/U15 fixes remain in rows 37-42), but gains zero waves because every gate consumes the live chain and all units share vkBackend.h. T39a/T41a recorded as authoring floats only (W′17 note).
- **ST-2 Task 9 harness:** separable → T9a (driver/Xvfb/XTest/capture), T9b (script format+parser), T9c (diff+masks), T9d (baseline-finalize RUN). Handshake has TWO regimes: harness-side XGetImage round-trip barrier on the pre-refactor tree (plan:186 names it as the pre-endFrame alternative), switching to the app-side endFrame counter once T12 lands — the switchover rides T12's Q13 gate (plan:618); T9's Files correctly list no game-tree instrumentation. No hidden dependency.
- **ST-3 Tasks 31-35:** T34 ∥ T32 holds ONLY with a self-contained textured-quad/shader path inside 34a's pre-agreed seam (its Q3/Q8 goldens render isolated scenes — executable). T35 is NOT parallel with 32/34: plan:786 names both as suppliers (MVP uniform + mask path) → strictly-serial. 33 independent (text atlas).
- **ST-4 Tasks 14-22 symbol prereqs:** NONE needs 26/27. T18 needs only T11/T12a/T13 — X11 keeps native clip-mask GCs relocated into the backend (plan:659); xbmDecode is consumed by T27a/T27b/T34 (/T42). Per-unit cards below carry true prereqs.
- **ST-5 Phase 5:** YES — T45b-i (assertions, gravity guards, 500-angle suite, seeded runs) runs on X11-only goldens post-T25; only the X11-vs-GL parity leg (T45b-ii) waits for T36. YES — T46 splits per-backend: T46a after T36+**T44a** (soak must cover the FINAL GL surface incl. overlay — final-surface caveat, not a gate weakening), T46b after T43+T44b. Effect: 46 leaves the critical path entirely.
- **ST-6 7/8/9 ordering:** T7a ∥ T7b ∥ T8 mutually independent (disjoint files; title/help deterministic without seed); T9d consumes T7b+T8+T9a-c; T10's only edge to 9 is the G-edge on the hi-score baseline artifact (O5-M1, plan:579/:595/:604) — acyclic, confirmed.

## 3. UNIT DECOMPOSITION TABLE — 48 rows → 71 units

Legend — class: **FI** fully-independent · **SFC** shared-file-contended · **SS** strictly-serial. Commit: all letters land as sequenced commits under their ROW's prefix/message (plan:472 convention); "row" = the row commit. Gates cite the row's Acceptance machinery.

### Phase 0 (rows 1-6 + hoisted 26)

| Unit | Write set | Prereqs | Executable gate | Commit | Class |
|---|---|---|---|---|---|
| T1 | makefile | — | `make BACKEND=X11` + `GL objects`; `make V=1` shows `-DX11_BACKEND` X11-only; warning baseline recorded (plan:535) | build: | root |
| T2a | stage.H:11, playingField.H:18-19+:69/:300/:457/:565/:591/:616, options.H:8-23 | T1 | `g++ -E -P` GL-config: no X11/Motif header content; X11 byte-identical (plan:542) | refactor: | SFC (R10-N1 order) |
| T2b | utilities/rendering/x11types.H (new) | — | file present; opaque typedefs grep (plan:540) | refactor: | FI |
| T3 | 9 object/leaf headers (plan:546 set) | T1, T2b | per-file `-E -P` clean; X11 identical (plan:549) | refactor: | FI |
| T4 | button.H:4-5, explosionGraphic.H:4, compositePixmap.H:4, rotatorDisplayData.H:5/.C:4-5, rotator.H:5, frameList.H:4, AutoRepeatOn.C:3 | T1, T2b | tree-wide unguarded-include grep = 0 (plan:556) | refactor: | FI |
| T5a | playingField.H :14/:519/:524 snprintf rewrite | T1 | file-level ostrstream grep=0; format byte-identical spot (plan:563) | refactor: | SFC (after T2a) |
| T5b | stage.H :6/:240/:245/:283/:287 | T1 | same | refactor: | SFC (after T2a) |
| T5c | options.H :6/:3112/:3125/:3859/:3865 | T1, T2a | same | refactor: | SFC (after T2a) |
| T6a | ODR inline marks (audit-hit headers incl. playingField.H:98) | T1 | ODR audit grep=0; 0 multiple-definition link (plan:570) | refactor: | SFC (light) |
| T6b | XAsteroids.C:13-30 pointer globals | — | pointer-form grep; ASan title smoke (plan:570) | refactor: | FI |
| T7a | qa/capture-x11.sh, qa/baseline-x11/{title,help}.png+MANIFEST | — | 2-run capture diff 0 px; xlsfonts pre-flight (plan:581) | qa: | FI |
| T7b | qa/fixtures/hiScore.data | — | 10 well-formed entries (plan:581) | qa: | FI |
| T8 | stage.H:218-219 | — | srand grep=1 w/ env fallback; XAST_SEED determinism smoke (plan:590) | feat: | SFC (stage.H) |
| T26 | utilities/pixmaps/xbmDecode.H (new), makefile (GL/VK object line) | T1 | 28-dataset golden checksums; `GL objects` green (plan:721) | feat: | SFC (makefile) |

### Phase 1 (rows 7-23)

| Unit | Write set | Prereqs | Executable gate | Commit | Class |
|---|---|---|---|---|---|
| T9a | test/harness driver TU (Xvfb+XTest+capture+handshake barrier), makefile harness target | T7a, T8 | builds; single-frame capture determinism (plan:595 (1)) | qa: | SFC (makefile) |
| T9b | test/harness/script-format.md + parser TU | — | vocabulary doc + parse round-trip (plan:595 (2)) | qa: | FI |
| T9c | test/harness diff TU + text-region masks | — | `compare -metric AE` leg operational (plan:595 (3)) | qa: | FI |
| T9d | qa/baseline-x11/ finalized (game-over/hi-score + full-session frames) | T9a,b,c, T7b, T8 | 2-run same-seed 0 px on every checkpoint; MANIFEST complete (plan:597) | qa: | SS |
| T10 | gamePlay/score.H (:68-69/:71-85/:108-110) | T9d (G: Q9 leg vs PRE-10 baseline), T7b | acceptance legs a-d incl. ASan (plan:604) | fix: | SS |
| T11 | utilities/rendering/renderingEngine.H (new) | T2b | `-fsyntax-only` w/ shim; `grep -c virtual`=28; F1 new-file leg (plan:611) | feat: | FI |
| T12a | utilities/rendering/x11Backend.H (new) — ALL 27 pass-through incl. endFrame blit, render-target trio, createTextureFromBitmap/deleteTexture, drawTextureMasked, ownership-map comment | T11 | X11 target compiles; methods dormant-safe (plan:616; dissolution per B2/R9-M3) | feat: | FI |
| T12b | stage.H (~Stage disarm; layout-formula extraction) | T11 | `grep XCloseDisplay\|XFreeFont` in ~Stage = 0 (plan:618) | feat: | SFC (sole in-wave) |
| T12c | XAsteroids.C (main engine-first) | T12a, T12b | ROW GATE: `make BACKEND=X11` + Q13 0 px + ASan (plan:618) | feat: | SS (in-row) |
| T13 | XAsteroids.C + 5 ctor sites + 9-header body-guard wrap (contingency letters 13a ctors / 13b wrap, plan:627) | T12 row, T6b | ROW GATE: GL/VK `objects` FIRST mandatory-green; Q13 0 px (plan:625) | refactor: | SS |
| T14 | stage.H (8 HUD/blit sites → engine) | T13 | F1 sub-grep stage.H=0; Q13 0 px (plan:633) | refactor: | FI |
| T15 | playingField.H (44 sites) + x11Backend.H (**EXCLUSIVE writer**: WM-block/canvas/gxor absorption, plan:638/:639) | T13 | F1 island-residual = exactly 22 (R9-M2 list incl. :477); Q13 0 px (plan:640) | refactor: | SFC (exclusive) |
| T16 | button.H (render-target faces) | T13 | F1=0; Q13+Q12 hover checkpoints (plan:647) | refactor: | FI |
| T17 | shipYard.H (texture lifecycle) | T13 | F1=0; Q13 (plan:654) | refactor: | FI |
| T18 | explosionGraphic.H + explosion.H (masked paths; NOT dep on 26/27 — ST-4) | T13 | F1=0 both; Q13 (plan:661) | refactor: | FI |
| T19 | rockGroup.H (4 sites) | T13 | F1=0; Q13 (plan:668) | refactor: | FI |
| T20 | shipGroup.H (8 sites incl. XAllocColor:222) | T13 | F1=0; Q13 (plan:675) | refactor: | FI |
| T21a | enemyGroup.H (:230/:288) | T13 | F1=0; Q13 (plan:682) | refactor: | FI |
| T21b | enemyBulletGroup.H (:143) | T13 | F1=0; Q7 (plan:682) | refactor: | FI |
| T22a | bullet.H (:81/:103/:104; alias-last-consumer flag) | T13 | F1=0; joint alias grep (plan:689) | refactor: | FI |
| T22b | shipBulletGroup.H (:132/:175) | T13 | F1=0 | refactor: | FI |
| T23 | qa/phase1-evidence.md (verification only) | T14-T22 ALL | 5 legs: Q13 0px / ASan / census 207+22 / GL-VK objects / transitional surface (plan:694-696) | qa: | SS (barrier) |

### Phase 2 (rows 24-28)

| Unit | Write set | Prereqs | Executable gate | Commit | Class |
|---|---|---|---|---|---|
| T24a | object headers — additive per-object `Step()`/`Render()` (disjoint files) | T23 | structure grep partial (plan:707) | refactor: | FI |
| T24b | playingField.H RunGame/scissor/order + stage.H HUD ordering | T24a | ROW GATE: Q13 0px + Q6 ±2ms + Q7 wrap + HUD-present probe (plan:707-708) | refactor: | SS (in-row) |
| T25 | x11Backend.H + playingField.H (**ATOMIC pair** — island relocation), stage.H (title gating); GL/VK half = D16 table + Q12 scripts (home at 31/42 per plan:835 — R10-N4) | T24 | island sub-grep=0; Q12 event parity; Q13 0px; Q6 re-arm (plan:714) | feat: | SS |
| T27a | rotatorDisplayData.{H,C} `#else` engine-rotation branch | T26, T11 | F1 Gate 2 (rotator); GL/VK objects green; X11 byte-identical (plan:728) | feat: | FI (∥T27b) |
| T27b | compositePixmap.{H,C} `#else` CPU compositing + 5 explosion frames | T26, T11 | F1 Gate 2 (composite); 5-frame RGBA assertion (plan:728) | feat: | FI (∥T27a) |
| T28 | qa/phase2-evidence.md | T25, T27a, T27b | full suite re-run (plan:733) | qa: | SS (barrier) |

### Phase 3 (rows 29-36)

| Unit | Write set | Prereqs | Executable gate | Commit | Class |
|---|---|---|---|---|---|
| T29 | makefile (GL link rule, obj-dir verification, XBM deps refresh) | T1, T30a (glad.c input), T23-green (R8-N5 exemption — NOT 28) | F3 recipe no-Motif; distinct obj paths; Q13 no-drift smoke (plan:746) | build: | SFC |
| T30a | vendor/glfw + vendor/glad (4.5 core) + PINNED.md row + makefile -I | T1 | glad profile check; `GL objects` green (plan:753) | chore: | SFC |
| T30b | vendor/stb/stb_truetype.h + PINNED.md row | T1 | checksum; present (plan:753) | chore: | SFC (PINNED.md) |
| T30c | vendor/dear_imgui + PINNED.md row + makefile (GL+VK leg units) | T1 | checksum; compiles both legs (plan:750/:751) | chore: | SFC |
| T31 | utilities/rendering/glBackend.H (new) — window/context/two-pass/4.5-gate/close-cb | T29, T30a, T30b, T28 (output-changing rule plan:735) | first GL binary; formula-size probe; 4.4 clean-exit; Q6 (plan:760) | feat: | SS (spine) |
| T32 [32a] | glBackend.H primitives/frame/scissor/transform group | T31 | primitives golden/determinism smoke; scissor 0-px-outside; identity reset (plan:767) | feat: | SFC (seam) |
| T33 [33a] | glBackend.H text-atlas/metrics group | T31, T30b | F2 font pixel gate; centering ≤1px; max_bounds>0 (plan:774) | feat: | SFC (seam) |
| T34 [34a] | glBackend.H textures/R8-masks/FBO group (self-contained textured-quad path — ST-3) | T31, T26, T27b | Q3 GL leg; Q8 FBO face golden (plan:781) | feat: | SFC (seam) |
| T35 | glBackend.H rotation path (D2; m13 degenerate) | **T32 + T34** (plan:786), T27a data supply | Q1 8+50-angle; Q2 masks; Q4 no-transform trace (plan:788) | feat: | SS |
| T36 | qa/phase3-evidence.md | T32-T35 | Q10 tiered + Q1-Q4,Q6-Q9,Q12,Q14 (Q5→44 per R8-C1) (plan:795) | qa: | SS (barrier) |

### Phase 4 (rows 37-43; USER GATE RESOLVED — executes)

| Unit | Write set | Prereqs | Executable gate | Commit | Class |
|---|---|---|---|---|---|
| T37a | qa/ vk-probe script, vendor/vulkan, makefile VK rule | T1-pattern, T30-regime | loader fast-fail; `VK objects` green (plan:806) | feat: | SFC |
| T37b | utilities/rendering/vkBackend.H (new) — instance/device/layer probe | T37a | 0 validation ERRORs; probe-log artifact (plan:806) | feat: | SS (in-row) |
| T38 | vkBackend.H surface+swapchain | T37, T31-window | extent==window assert; ≥2 images (plan:812) | feat: | SS |
| T39a | vkBackend.H sync OBJECTS (pool/fences/sems — device-only deps) | T37 | creation smoke (authoring float — ST-1) | feat: | SFC |
| T39b | vkBackend.H acquire/present loop + OUT_OF_DATE path | T38, T39a | 10-min soak; forced-OUT_OF_DATE; 100ms grep (plan:818) | feat: | SS |
| T40 | vkBackend.H render passes + framebuffers | T38 (format/image views) | scissor 0-px-outside; black clear (plan:824) | feat: | SS |
| T41a | shader ASSETS: GLSL sources + offline glslc + module helper | — (offline toolchain) | glslc log clean (plan:829; authoring float — ST-1) | feat: | FI (authoring) |
| T41b | vkBackend.h pipelines | T40, T41a | variants render; thick-quad count; transform identity (plan:830) | feat: | SS |
| T42 | vkBackend.H all-27 methods | T38-T41, T26, T27b, T33-regime | VK-vs-GL smoke 0px non-text; m13 trace (plan:836) | feat: | SS |
| T43 | qa/phase4-evidence.md | T42 | Q11 tiered byte-diff (plan:842) | qa: | SS |

### Phase 5 (rows 44-48)

| Unit | Write set | Prereqs | Executable gate | Commit | Class |
|---|---|---|---|---|---|
| T44a | menuAdapter.H (new), optionsMenu.H/.C (new), glBackend.H glue, options.H (MotifOptionsMenu guard region), XAsteroids.C, makefile GL menu units, README | T30c, T25, T2-contract, T13, T31-T34 | Q5 GL leg + F5 X11 unchanged + symbol greps + Q12 checkpoint (plan:852) | feat: | SFC |
| T44b | vkBackend.H adapter-glue append + Q5 VK leg | T42 | Q5 VK leg formula-worded; DELETED if Phase 4 excised (plan:850) | feat: | SS |
| T45a | test/numeric/golden/ (PRE-task-24 capture riding T9d's session) | T9d, T8 | two-run determinism: identical hashes (plan:857/:859) | qa: | SS (must precede T24) |
| T45b-i | test/numeric/ runner + assertions (X11 lane) | T45a, T25 (conservative: post-T28) | assertions vs goldens; NaN/gravity; 500-angle; `make test-numeric` X11 (plan:859) | qa: | FI |
| T45b-ii | (verification) X11-vs-GL parity leg | T45b-i, T36 | X11-vs-GL numeric match (plan:859) | qa: | SS |
| T46a | evidence commits — GL leg (soak <5%, drift replay, ASan, close-path, Q15 GL) | T36, **T44a** (final-surface caveat) | plan:866 legs scoped to GL | qa: | FI |
| T46b | evidence commits — VK leg | T43, T44b | plan:866 legs scoped to VK | qa: | FI |
| T47 | XAsteroids.C, stage.H members, aliases, x11types.H deletion, backend accessors + nativeHandle no-op bodies | T25, T22, T44b, T42-if-kept | F1 terminal 229/0; Q13; 3-leg rebuild (plan:873) | refactor: | SS (terminal) |
| T48 | final evidence commits | T47 + all gates | F1-F5 + SC1-SC13 + Q1-Q15 (plan:880) | chore: | SS (terminal) |

## 4. CONTENTION MATRIX (merge-isolation map for multi-agent worktrees)

| Shared file | Writer units | Same-wave collisions & ruling |
|---|---|---|
| `makefile` | T1, T9a, T26, T29, T30a, T30c, T37a, T44a | W′2 {T9a ∥ T30a/c}, W′15 {T37a ∥ T44a}: **sequenced commits** (disjoint regions, 22-line file) — R10-N2 |
| `stage.H` | T2a(:11), T5b(:6,:240…), T8(:218-9), T12b, T13(wrap), T14, T24b, T25, T47 | W′1 {T2a,T5b,T8} hunk-disjoint, land T2a's :11 hunk first (R10-N1); all later waves single-writer |
| `playingField.H` | T2a(:18-9,:69…), T5a(:14,:519,:524), T6a(:98), T15, T24b, T25, T47 | W′1 {T2a,T5a,T6a}: R8-N4 order + :98 far-hunk note; W′7 T24 → W′8 T25 strict order (F-collision) |
| `options.H` | T2a(:8-23), T5c(:6,…), T44a(MotifOptionsMenu region) | W′1 {T2a,T5c}: land T2a first — :6 vs :8 = 2-line gap (R10-N1); otherwise single-writer waves |
| `XAsteroids.C` | T6b, T12c, T13, T44a, T47 | No same-wave pairing in W′0-W′23 |
| `x11Backend.H` | T12a(create), T15(exclusive W′6), T25, T47 | Holds ONLY with B2/R9-M3 dissolution (impls at T12a); else ≥3 W′6 writers |
| `glBackend.H` | T31, T32, T33, T34, T35, T44a | W′12 three-wide via sanctioned 32a/33a/34a seams; all other waves single-writer |
| `vkBackend.H` | T37b, T38, T39a, T39b, T40, T41a, T41b, T42, T44b, T47 | Serial chain by design; T39a/T41a floats need pre-agreed member/region seams |
| `qa/baseline-x11/` | T7a, T9d (writers; ordered) | Read-mostly afterwards (23/28/36/43/48 consumers) |
| `vendor/PINNED.md` | T30a, T30b, T30c, T37a | Sequenced row-appends within W′2 / W′15 |
| `README.md` | T44a, T45(T45b-i purpose line, plan:856) | No same-wave pairing in W′ (45b-i@W′10 vs 44a@W′15) |
| `score.H` / `test/harness/` / `test/numeric/` / `qa/fixtures/` | T10 / T9a-c / T45a,45b-i / T7b | Sole-writer or disjoint-path — no isolation needed beyond normal git |
| Runtime (non-file) | T46a executes GL binary while W′16 edits vkBackend.H | Worktree isolation OR sequenced (46a evidence is GL-leg only — no VK binary needed) |

## 5. REFINED WAVE SCHEDULE W′0-W′23 (units; VK kept)

Agent assumption column = concurrent agents demanded. ⚠ = width EXCEEDS what B9 row-granularity can express; expressed via lettered seams inside rows (13a/13b, 44a/44b precedent) and cross-row pairing (Appendix precedent).

| Wave | Units (width) | Agents | Notes |
|---|---|---|---|
| W′0 | T1 (1) | 1 | root |
| W′1 | T2a T2b T5a T5b T5c T6a T6b T7a T7b T8 T26 (11/6 rows) ⚠ | 6 (2 batches) | 26 hoisted (B1); R10-N1 hunk ordering: T2a's stage.H/playingField.H/options.H guard hunks first |
| W′2 | T3 T4 T11 T9a T9b T9c T30a T30b T30c (9/5 rows) ⚠ | 6 (2 batches) | 30 hoisted (B1); makefile sequenced T9a→T30a/c (R10-N2) |
| W′3 | T9d T10 T12a T12b T45a (5/4 rows) | 5 | 45a rides T9d's session on the PRE-T24 tree (fixes R9-M4 scheduling hole) |
| W′4 | T12c (1) | 1 | row-12 gate binds (Q13 first changed-tree gate) |
| W′5 | T13 (1; 13a/13b contingency) | 1 | DI cutover |
| W′6 | T14 T15 T16 T17 T18 T19 T20 T21a T21b T22a T22b (11/9 rows) ⚠ | 6 (2 batches) | NINE-WIDE row wave, ELEVEN unit-wide; T15 sole x11Backend.H writer |
| W′7 | T23 (1) | 1 | Phase-1 exit barrier |
| W′8 | T24, T29 (2/2 rows) | 2 | **29 floated** ∥ T24 per plan:735 R8-N5 exemption (build-only; deps 1+30a+23-green) |
| W′9 | T25, T27a, T27b (3/2 rows) ⚠ | 3 | 27 lettered seams; 25 atomic pair |
| W′10 | T28, T45b-i (2/2 rows) | 2 | 45b-i X11 lane ∥ barrier (ST-5) |
| W′11 | T31 (1) | 1 | first output-changing GL task — post-28 (plan:735) |
| W′12 | T32 T33 T34 (3/3 rows) | 3 | sanctioned 32a/33a/34a seams |
| W′13 | T35 (1) | 1 | needs 32+34 |
| W′14 | T36 (1) | 1 | minimum-visible-surface barrier |
| W′15 | T37(a→b sequenced), T44a (2/2 rows) | 2-3 | menus ∥ Vulkan start (B4); makefile sequenced (R10-N2) |
| W′16 | T38, T46a (2/2 rows) | 2 | **46a de-serialized** from round-8's W23 |
| W′17 | T39b (1) [+T39a/T41a authoring floats] | 1 | floats save 0 waves (ST-1) |
| W′18 | T40 (1) | 1 | |
| W′19 | T41b (1; +T41a if unstaged) | 1 | |
| W′20 | T42 (1) | 1 | |
| W′21 | T43, T44b (2/2 rows) | 2 | 44b gated on 42 |
| W′22 | T46b, T47 (2/2 rows) | 2 | 47 needs 44b's appended bodies; runtime-isolate 46b's VK soak (or sequence) |
| W′23 | T48 (1) | 1 | terminal |

**Excised variant (Phase 4 deleted):** drop T37a-b, T38-T42, T43, T44b, T46b; T44a@W′15, T46a@W′16, T47@W′17, T48@W′18.

## 6. CRITICAL PATH QUANTIFICATION

- **As-written numbered order:** 48 sequential steps (unchanged).
- **Round-8 extracted:** 25 waves VK-kept / 20 excised.
- **Round-10 unit-extracted, UNLIMITED agents: 23 execution waves VK-kept (−2 vs R8, −52% vs 48) / 17 excised (−3 vs R8, −65%).**
- **Realistic 4-6 agents:** wave COUNT unchanged (spine waves are 1-wide); W′1/W′2/W′6 run as 2 batches each → wall-clock ≈ 26 batch-slots, still below round-8's 25 uneven waves + its idle lanes. Delta vs unlimited: 0 waves, ~3 waves of batch-stretching absorbed by floatable units (T26, T30b/c, T45b-i) filling idle lanes.

**Irreducible serial spine (23 nodes):** T1→T2a→T11→T12(a,b,c)→T13→T{14-22 any}→T23→T24→T25→T28→T31→T32→T35→T36→T37→T38→T39→T40→T41→T42→T43→T47→T48. Every link is a TRUE edge — zero PH/VB/SW/STALE edges remain:
1→2: guards are dead without `-DX11_BACKEND` (B1, plan:106/:533) · 2b→11: syntax-check consumes the shim (plan:611) · 11→12: implements the contract (plan:616) · 12→13: cutover consumes backend+disarmed Stage; row Q13 gate · 13→migrations: engine ctors live; per-task Q13 · →23: census sweeps exactly those files (plan:694) · 23→24: LOAD-BEARING pixel gate (plan:696) · 24→25: RunGame function collision (plan:705/:712) · 25→28: behavior phase must prove byte-identity (plan:733) · 28→31: first output-changing GL task (plan:735/:760) · 31→32: live context · 32→35: MVP uniform (plan:786) · 35→36: gate needs full surface (plan:793) · 36→37: min-surface before Phase 4 (plan:795) · 37→38→39→40→41→42: cumulative VK object graph (plan:811/:817/:823/:829) · 42→43: gate needs completeness (plan:841) · 43/44b→47: deletes 44b's appended bodies (plan:870) · 47→48: terminal certification (plan:876).
Off-spine floaters proven: T29 (VB-exempt), T45b-i, T46a, T46b, T44a, T27a/b, T33, T34, T10, T9a-d, T7a/b, T26, T30a-c — none can join the spine without weakening a gate; none needs to.

## 7. New findings (round 10)

- **R10-N1 (MINOR):** extend the R8-N4 same-wave hunk-ordering note to stage.H (:6 vs :11, T5b/T2a) and options.H (:6 vs :8, T5c/T2a); add T6a's playingField.H:98 hunk to the W′1 note. Verified in-tree (strstream includes :6/:14; guards :11/:18-19/:8).
- **R10-N2 (MINOR):** makefile co-writer pairs inside waves (W′2 {T9a,T30a,c}; W′15 {T37a,T44a}) — one-line sequencing note mirroring R8-N4 (matches R9 watch-item).
- **R10-N3 (OBSERVATION):** GL home of `drawTriangles`/`createTextureFromRGBA32` is unnamed across 32/33/34 — assign both to the 34a textured group in the seam spec so T44a's prereq is exact.
- **R10-N4 (OBSERVATION):** T25's GL/VK state-machine half has no named home file before glBackend.H exists — deliver as D16 table + Q12 scripts at 25, wired at 31/42 ("reused, not re-derived", plan:835).
None is blocking; all are scheduling-hygiene items inside the authorized lettered-seam grammar.

## VERDICT

`R10 VERDICT: PARALLELISM-EXTRACTED, NO BLOCKER — v6.3 (sha 08f0e8c2…51bff1, 1179/253024) decomposes into 71 lettered units inside the 48 contiguous rows (B9 intact); refined schedule W′0-W′23 = 23 execution waves VK-kept / 17 excised (−2/−3 vs round-8's 25/20; −52%/−65% vs 48 as-written) with every F/Q/SC gate intact and the spine 100% true-edge; round-8 claims 10 CONFIRMED / 1 REFUTED-incomplete (wave-pair file safety → R10-N2) / 5 REFINED; 2 new minors + 2 observations (R10-N1..N4) — safe to execute at 4-6 agents with sequenced-commit discipline on makefile/guard-hunk pairs.`
