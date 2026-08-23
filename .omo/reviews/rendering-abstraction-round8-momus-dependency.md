# Round 8 — Momus Dependency Review: rendering-abstraction.md (v6.2)

**Lens (single, owner-mandated):** deep dependency analysis → smallest sound task decomposition + maximum exploitable parallelism. Correctness findings from rounds 1–7 are treated as closed and are not re-litigated except where they bear on granularity, ordering, or parallelism.
**Ground truth:** the plan file itself + this repo. All structural facts below were re-derived from a fresh full read of `rendering-abstraction.md` (1122 lines) and spot-checks of ≥10 task bodies against index rows (tasks 1–27, 29–36, 37–43, 44–48 all match their index rows; per-task Files/Change/Acceptance fields present throughout). Repo spot-checks: makefile:2 (`-O3 -w`, zero `-D`, no `-std`), single shared `.o` set makefile:6–14, stale XBM dep list makefile:15, XAsteroids.C:13–30 static globals incl. load-bearing `Button button(stage.display,...)`, playingField.H island anchors (:135 XSetFunction; :332/:333/:340/:453/:572 XNextEvent family), stage.H:218–219 srand, stage.H:134–138 five fonts, score.H do-while at :108–110, options.H pump ~:2540–2571, migration-site cites for bullet/shipBullet/enemy/enemyBullet/rock/ship/explosionGraphic all line-accurate, 32 `.xbm` files, `utilities/rendering/`, `qa/`, `test/`, `vendor/` absent today (all new per plan).

---

## 1. Verdict

**CHANGES_REQUESTED** — scoped strictly to the dependency/granularity/parallelism dimension.

The 48-task graph is *sound* (no cycles, no impossible edges, B9 contiguity intact), but it is **not minimal**: 3 findings (1 critical, 2 major) are ordering defects that either block a gate at its scheduled position (R8-C1), silently defeat a stated safety property (R8-M2), or leave two texts asserting opposite orderings of the same edge (R8-M3). Beyond the findings, the extraction analysis (§5) shows the plan's sequential-step count can be cut from **48 to 25** (Vulkan kept) or **20** (Phase 4 excised) without waiving any protected constraint: every Q13 byte-identity gate, the tiered identity regime, D8's GL/VK-only canonical order, harness-before-pixel-gates, USER GATE mechanics, B9 numbering, and the decision-complete executor contract are preserved in every proposal below.

---

## 2. Dependency matrix (tasks 1–48)

Legend — TRUE edge kinds: **D** = data/semantic (task consumes a symbol/method/artifact produced by the predecessor), **F** = real file/function-hunk contention (edits collide), **G** = gate arithmetic (acceptance runs predecessor's gate/harness/baseline). ARTIFICIAL edge kinds: **PH** = phase-numbering convention only, **VB** = verification batching (gate exists but task is non-pixel/non-behavioral), **SW** = single-writer file discipline (dissolvable per §3), **STALE** = co-listing or forward reference not backed by content.

| # | TRUE deps (why) | ARTIFICIAL deps (why) |
|---|---|---|
| 1 | — (root) | — |
| 2 | 1 [G: `-DX11_BACKEND` must exist or guards strip live code — L106, L531] | — |
| 3 | 2 [D: x11types shim stands in for X11-typed signatures — L545]; 1 [G] | after-4 serial position [PH: disjoint file sets, zero shared hunks] |
| 4 | 2 [D: shim]; 1 [G] | after-3 serial position [PH/VB: disjoint files from 3] |
| 5 | 1 [G: `-std=c++17` makes strstream removal testable — L561] | after-2/3/4 [PH: overlaps 2's three files but hunks near-disjoint — see R8-N4] |
| 6 | 1 [D: `inline` requalification requires C++17 — L566] | after-2–5 [PH: playingField.H:98 hunk far from 2's :18–19/:69/:300 hunks] |
| 7 | — (pre-refactor tree; stock makefile builds the capture binary) | after-1–6 [PH] |
| 8 | — | after-7 [PH: disjoint file, no data flow] |
| 9 | 7 [D: consumes/finalizes `qa/baseline-x11/` + fixture — L592]; 8 [D: `XAST_SEED` drives the determinism assertion — L595] | — |
| 10 | 9 [G: acceptance leg (b) diffs vs the task-9 PRE-10 hi-score baseline — L602]; 7 [D: fixture] | serial position [PH: sole writer of score.H] |
| 11 | 2 [G: standalone `-fsyntax-only` uses the shim — L609] | after-7–10 [VB] |
| 12 | 11 [D: implements the 27-method interface]; 1 [G: macro + obj dirs]; 9 [G: Q13 first harness gate on changed tree — L616]; 6 [D, soft: main() engine-first assumes pointer globals] | — |
| 13 | 12 [D: backend class + Stage ctor form]; 6 [D: re-forms global construction in main — L566/L620]; 9 [G: Q13] | — |
| 14 | 13 [F: stage.H body-guard wrap covers the same HUD block — L620–622]; 12 [D: `endFrame`]; 9 [G: Q13] | x11Backend.H co-listing [STALE/SW: endFrame fully specified at L324, implementable at 12] |
| 15 | 13; 12; 9 [same kinds as 14] | — (**15 is the one migration with REAL backend coupling**: WM-block/canvas/gxor absorption must be atomic with its playingField.H deletions — see §3) |
| 16 | 13; 12 [D: render-target trio]; 9 [G] | x11Backend.H co-listing [SW-dissolvable: trio spec'd L257–259/L643, dormant-safe at 12] |
| 17 | 13; 12 [D: createTextureFromBitmap/deleteTexture]; 9 [G] | x11Backend.H co-listing [SW-dissolvable: verbatim relocation per L650] |
| 18 | 13; 12 [D: drawTextureMasked]; 9 [G] | x11Backend.H co-listing [SW-dissolvable: D6 spec L321] |
| 19 | 13; 12; 9 | x11Backend.H co-listing [STALE: introduces NO new backend method — drawTexture/Masked exist from 12] |
| 20 | 13; 12; 9 | x11Backend.H co-listing [STALE: drawTexture + color value only] |
| 21 | 13; 12; 9 | x11Backend.H co-listing [STALE: drawTexture only] |
| 22 | 13; 12; 9 | x11Backend.H co-listing [STALE: drawTexture/masked only; alias reads work in either 15↔22 order] |
| 23 | 14–22 all [G: residual census sweep over exactly their files + Q13 on fully migrated tree — L692–693]; 9 | — (legitimate barrier) |
| 24 | 23 [G: "No pixel-changing task in Phase 2 may start without this gate green" — L694]; 12 [D: beginFrame/clear/setScissorRect/endFrame]; 14–22 [D: draws are engine calls] | — |
| 25 | 24 [F: both rewrite RunGame's core loop — genuine function-level collision]; 12 [D: pollEvents drain]; 23-barrier | — (24↔25 order could flip but one edge between them is real) |
| 26 | 1 [G: decode unit compiles into GL/VK object leg — L719] | Phase-2 position [PH: consumed by 27/34/42; hoistable to Phase 0 — S3 CONFIRMED] |
| 27 | 26 [D: xbmDecode consumer]; 4 [D: extends D14 include guards into the body wrap — L552]; 11 [D: engine API in `#else` branches]; 9 [G: Q13 byte-identity]; 1 | position after 24/25 [PH: no data flow from either]; x11Backend.H "(consumers)" co-listing [STALE — see R8-N3] |
| 28 | 24, 25, 27 [G: re-verifies exactly those changes — L731] | — (legitimate barrier for pixel state) |
| 29 | 28 [VB: build-only task behind a pixel-state barrier — artificial, see R8-N5]; 1 [D: obj dirs] | before-30 serial [F-lite: same makefile region, disjoint hunks] |
| 30 | 1 [G/D: BACKEND var + GL/VK object legs are its only prerequisites] | after-29 [F-lite: makefile region]; after-28 [VB]. **Hoistable to day one+1 — S2 CONFIRMED** |
| 31 | 29 [D: GL link rule]; 30 [D: glfw/glad/stb present]; 28 [G: first GL binary runs the migrated game] | — |
| 32 | 31 [D: context/window] | serial tail vs 33/34 [SW: glBackend.H — dissolvable, §3] |
| 33 | 31 [D]; 30 [D: stb] | serial position [SW-dissolvable] |
| 34 | 31 [D]; 26 [D: xbmDecode feed — L777]; 27 [D: 5 CPU-composited frames — L777] | serial position [SW-dissolvable] |
| 35 | 32 [D: setTransform MVP uniform — L783]; 34 [D: masked textures]; 27 [D: rotator data supply]; 19–22 [D: Render() consumers, done P1] | serial tail [real deps make tail honest] |
| 36 | 32–35 [D: full method surface]; 31 | **44 [STALE/FALSE EDGE: Q5 GL leg cites task 44 — R8-C1]** |
| 37 | USER GATE [L796–798]; 1-pattern [obj dirs]; 30-regime [vendoring pattern] | — (gate-conditioned; excisable set begins here) |
| 38 | 37 [D: instance/device; GLFW window from 31-pattern] | — |
| 39 | 38 [D: swapchain is what re-bootstraps] | — |
| 40 | 38/39 [D: swapchain images for framebuffers] | — |
| 41 | 40 [D: render passes host pipelines] | — |
| 42 | 41 [D]; 26 [D]; 27 [D]; 33-regime [D: stb text reuse — L833] | — |
| 43 | 42 [D]; 36 [D: GL reference output — L839] | — (gated gate) |
| 44 | 36 [G]; 30 [D: dear_imgui vendored at 30 — L97]; 25 [D: event feed contract]; 2 [D: guarded RunGame contract]; 13 [D: main construction] | whole-task serialization behind Phase 4 via vkBackend.H glue [SW-dissolvable by split — §4]; vendor Files entries [STALE: vendoring happens at 30] |
| 45 | 24 [D: asserts post-split float stability]; 9 [D: harness]; 36 [G: X11-vs-GL leg] | after-44 [PH: fully independent of menus]. **MISSED true edge: goldens ← pre-24 tree — R8-M2** |
| 46 | 36 [G: GL leg]; 43 [G: VK leg when kept]; 25 [D: focus-loss semantics] | after-44/45 [PH: independent of both] |
| 47 | 25 [D: island gone]; 22 [D: alias last-readers gone]; 14–22 [D: domain migrated]; 44b [F: deletes the vk/gl nativeHandle no-op bodies 44 appends — L868]; 42-if-kept [D] | after-45/46 [PH] |
| 48 | 47 [G] + all prior gates [G] | — (terminal) |

**Matrix summary:** of ~60 asserted precedence relations in the numbered order, **~34 are true** (D/F/G) and **~26 are artificial** (PH/VB/SW/STALE). The largest artificial clusters: tasks 3↔4 (mutually independent), 7/8/10/11 (four-way independent lane material), 14–22 mutual serialization (8 of 9 edges dissolvable), 26/30 phase placement, 44-behind-Phase-4, 45-behind-44.

---

## 3. Single-writer contention audit

### 3.1 `x11Backend.H` across tasks 12–22 (+25, +27)

Task 12 creates the file and — per its Change text — implements **all 27 methods as pass-through** ("all 27 methods implemented as pass-through X11 … the D9 menu pair … included", L614; mapping table L315–328 pins each method's exact behavior). Every later migration therefore *routes calls* to methods that already exist. Audit per task:

| Task | Claimed backend edit | Real? | Why |
|---|---|---|---|
| 14 | "endFrame blit implementation" (L628) | **Dissolvable** | endFrame = blit pair + XSync bound, fully specified (L324, L629); write it complete at 12; it is dormant until 14 flips the callers. Listing defensive. |
| 15 | WM-block extension + clear() + canvas/gxor ownership (L635–636) | **REAL** | The WM block :539–566, canvas/gxor/backgroundGC creation, and atom aliases move INTO initWindow/shutdown atomically with deleting them from playingField.H. Cannot pre-do at 12: from task 13 the backend's initWindow RUNS, so a pre-absorbed block would double-execute against the still-present playingField originals (XMapRaised/XSelectInput/XSetWMProperties twice) — a byte-identity hazard during the 13→15 window. 15 must hold exclusive write access. |
| 16 | render-target implementation (L642) | **Dissolvable** | Trio fully specified (L257–259, M1/R4-B6); off-screen-Pixmap impl is dormant until 16 routes button faces. |
| 17 | createTextureFromBitmap/deleteTexture (L649) | **Dissolvable** | "verbatim the same X11 calls, now in the exception zone" (L650) — relocatable at 12, dormant until 17. |
| 18 | drawTextureMasked impl (L656) | **Dissolvable** | D6 pins it (L321): native clip-mask GCs inside the backend; dormant until 18. |
| 19 | (none named) | **Defensive listing** | Change introduces no new backend capability — drawTexture/drawTextureMasked exist from 12. |
| 20 | (none named) | **Defensive listing** | drawTexture + color value only. |
| 21 | (none named) | **Defensive listing** | drawTexture only. |
| 22 | (none named) | **Defensive listing** | drawTexture/masked only; alias reads valid in either 15↔22 order. |
| 25 | pollEvents absorbs the island (L709) | **REAL** | Planned atomic pair with the playingField.H island deletion (relocation is 1:1, L711). |
| 27 | listed as "(consumers)" (L723) | **Likely zero-edit** | The `#else` branches live in rotatorDisplayData/compositePixmap; nothing in 27's Change text edits x11Backend.H. Clarify so 27 can share a wave with 25 (R8-N3). |

**Net:** the L573 serialization note ("backend-header edits are SERIALIZED across 14–22") is **8 parts artificial, 1 part real**. With task 12 completing all dormant-safe pass-through implementations (they are fully specified — no design judgment deferred), the migration wave needs exactly ONE exclusive writer slot (15) and can run **9-wide**.

### 3.2 `glBackend.H` across tasks 31–35

Contention is **real at file level** (header-inline is repo convention; D-A A2 keeps backends .C-free) but **dissolvable at hunk level**: 32 = primitives/frame/scissor/transform shader group; 33 = text atlas + metrics group; 34 = texture/mask/FBO group. Disjoint method bodies, no cross-group symbol writes except 35 consuming 32's uniform + 34's mask path. Two sound options: (a) keep 31→{32,33,34}→35 with lettered sub-commits and pre-agreed insertion seams (house style per 13a/13b, L625); (b) conservative serial fallback costs +2 waves. Either honors repo conventions; neither touches a protected constraint.

### 3.3 `vkBackend.H` across tasks 37–42 (+44b)

This serialization is **mostly honest**: 37→38→39→40→41→42 is a cumulative state-machine build-up (instance → surface/swapchain → sync → passes → pipelines → methods), each task consuming the previous VK state, not merely sharing a file. No split proposed. 44b's adapter-glue append is hunk-disjoint from 42's completion work and can land immediately after 42 (or with it, sequenced commits). Entire lane is USER-GATE-conditioned.

### 3.4 `playingField.H` across tasks 15/24/25

Real contention, correctly phased today: 15's hunks (clears :202/:312/:638, strings :525–663, WM :539–566, canvas :130–149) sit adjacent to the island and to RunGame's body; 24 restructures RunGame; 25 deletes the island inside it. 24↔25 is a genuine function-level collision; 15 predates both across the 23 barrier. Keep serial — no extraction available here, and none needed (the 23/28 barriers dominate anyway).

---

## 4. Split candidates

House style honored: pre-agreed lettered seams inside one row (13a/13b precedent, L625); B9 contiguous numbering preserved — sub-letters never become new rows.

**(a) Task 44 → 44a / 44b.** Seam: 44a = menuAdapter.H + optionsMenu.H/.C + glBackend.H glue + XAsteroids.C wiring + makefile GL-leg menu units + README + Q5 **GL leg** + pause/resume + grep gates. 44b = vkBackend.H adapter glue + Q5 **VK leg** assertion (formula-worded per O5-M2; deleted wholesale if Phase 4 excised). Acceptance decomposes cleanly: every row of 44's Acceptance (L850) names a leg; GL-leg rows + X11-regression rows → 44a; VK-leg rows → 44b. Why: vkBackend.H glue is 44's ONLY Phase-4 coupling (S8 CONFIRMED — items i–v of 44's Change are engine-only/domain-only/makefile-GL). Effect: menus land concurrent with all of Phase 4 (or immediately post-36 when excised).

**(b) Task 45 → 45a / 45b.** Seam: 45a = numeric golden capture on the **pre-task-24 tree** (state-hash goldens for the swept-intersect sort, mid-pass removal, pass-count snapshot, gravity paths) committed under `test/numeric/golden/`; executable as an extension of task 9's baseline session or a standalone capture against the pre-24 commit. 45b = the lane itself: NaN/gravity assertions, randomized-angle suite, seeded harness runs, `make test-numeric`, X11-vs-GL match. Acceptance decomposes: 45a proves capture determinism (2 runs identical hashes); 45b consumes the goldens and adds its own assertions. Why: closes R8-M2 — a pin recorded after the reorder cannot detect drift caused BY the reorder; D17.5/D8's stated purpose (L90, L189, L447) requires pre-D8 goldens.

**(c) Tasks 32/33/34 → optional lettered sub-commits (32a primitives, 33a text, 34a textures/FBO) inside their existing rows**, enabling the 3-wide wave of §5 with pre-agreed insertion points in glBackend.H. Acceptance holds per sub-task: each keeps its own QA row (primitives smoke / font pixel gate / Q3+Q8). Fallback if the team rejects concurrent header editing: serial 32→33→34 (+2 waves, still beats today's schedule).

**(d) NOT split:** 13 (seam already pre-agreed), 24/25 (function-level collision), 37–42 (honest cumulative chain), 47 (single atomic cleanup whose greps are only meaningful at terminal state).

---

## 5. Parallel-wave schedule (true dependencies only)

### 5.1 Wave tables

Conservative extracted schedule — keeps every Q13/pixel gate and the 23/28 phase barriers; hoists only non-pixel tasks (26, 30) and dissolves SW contention per §3:

| Wave | Tasks running concurrently | Gate/notes |
|---|---|---|
| W0 | 1 | root |
| W1 | 2, 5, 6, 7, 8, 26 | 26 hoisted (S3); stage.H has 3 far-apart writers (:11 / :218 / :240+) — hunk-disjoint |
| W2 | 3, 4, 9, 30 | 30 hoisted (S2); 9 needs 7+8 |
| W3 | 10, 11 | 10 needs 9's baseline; 11 needs 2's shim |
| W4 | 12 | needs 11+9+6 |
| W5 | 13 | needs 12+6 |
| W6 | **14, 15, 16, 17, 18, 19, 20, 21, 22** | **9-wide**; 15 sole x11Backend.H writer |
| W7 | 23 | Phase-1 exit gate |
| W8 | 24 | pixel-changing, post-23 |
| W9 | 25, 27 | 25 owns x11Backend.H; 27 zero-edit there (R8-N3) |
| W10 | 28 | Phase-2 exit gate |
| W11 | 29 | build-only |
| W12 | 31 | 30 already landed (W2) |
| W13 | 32, 33, 34 | 3-wide via §4(c) seams; serial fallback +2 waves |
| W14 | 35 | needs 32+34 |
| W15 | 36 | minimum-visible-surface gate (Q5 scoped to X11 leg per R8-C1) |
| — | **USER GATE** | Phase 4 conditional below |
| W16 | 37, **44a**, **45** | menus + numeric lane parallel to Vulkan |
| W17 | 38 | |
| W18 | 39 | |
| W19 | 40 | |
| W20 | 41 | |
| W21 | 42 | |
| W22 | 43, **44b** | 44b needs 42 |
| W23 | 46 | needs 43 (VK soak) + 36 |
| W24 | 47 | needs 44b (nativeHandle bodies) |
| W25 | 48 | terminal |

**Phase-4-excised variant:** delete W16–W22 VK members; 44a lands W16 (with 45), 46 W17, 47 W18, 48 W19.

### 5.2 Critical path & quantification

- **Plan-as-written (numbered order):** **48 sequential steps.**
- **Plan honoring its own Phase-1 note (L573: domain halves may overlap the serialized backend edits):** ≈ **41 steps** (14–22 collapse from 9 slots to ~2 pipelined slots; 48 − 7).
- **Extracted, Vulkan kept:** **25-step critical path** — 1 → 2 → 11 → 12 → 13 → W6(any one of 14–22) → 23 → 24 → 25 → 28 → 29 → 31 → 32 → 35 → 36 → 37 → 38 → 39 → 40 → 41 → 42 → 43 → 46 → 47 → 48. **Reduction: 48 %**.
- **Extracted, Phase 4 excised:** **20-step critical path** (…36 → 44a → 46 → 47 → 48). **Reduction: 58 %**.
- **Lanes idle today:** Phase 0 runs 6 serial tasks with ~4 mutually-independent (2/5/6 + 7/8 hoistable); Phase 1 idles 3 lanes among 7/8/10/11; **tasks 14–22 idle 8 lanes** behind the artificial backend serialization; Phase 3 idles 2 lanes among 32/33/34; Phase 5 idles 45 behind 44 and 44 behind ALL of Phase 4 (7 tasks) purely for the vkBackend.H glue append.

Arithmetic re-verified: 186 = 8+66+41+29+22+4+8+3+5 (L622) ✓; 205 = 227 − 22 (L693) ✓; 207 = 146+11+47+3 ✓; 229 = 207+22 ✓; 434 = 229+205 ✓; per-file census sums to 434 ✓; 27 methods = 21 core + 6 seams, API sketch groups sum 3+2+6+4+1+4+5+2 = 27 ✓; island list = 18 + 4 = 22 lines (L692) ✓; index rows 1–48 contiguous, 6 phases = 6+17+5+8+7+5 = 48 ✓.

---

## 6. Findings register

**R8-C1 (CRITICAL — false dependency edge; gate ungreenable at position).** `rendering-abstraction.md:L791` (task 36 Change embeds "Options FUNCTIONAL via the Dear ImGui overlay (S1/D9/task 44)") + `:L793` ("Q1-Q9 … green") + `:L457` (Q5's GL/VK leg requires the overlay) + `:L846–851` (overlay lands at task 44, Phase 5). As written, task 36's acceptance cannot be satisfied at its scheduled position: the ImGui overlay does not exist until 22 tasks later. An executor either deadlocks Phase 3's exit or improvises — violating the decision-complete contract. Evidence: Q5's X11 leg (Motif pump, F5) IS runnable at 36; only the GL/VK leg is forward-referenced. **Remediation (paste):** In task 36 Change, replace "Options FUNCTIONAL via the Dear ImGui overlay (S1/D9/task 44 — open/slider/close scripted)" with "Options FUNCTIONAL on X11 via the Motif pump (F5 scripted open/apply/close); the Q5 GL/VK Dear ImGui leg is asserted at task 44 and is OUT OF SCOPE here." In task 36 Acceptance, replace "Q1-Q9" with "Q1-Q4, Q6-Q9". No other text depends on Q5@36 (SC9 correctly cites task 44).

**R8-M2 (MAJOR — missed true dependency; defeats a stated safety property).** `:L855` (task 45 commits goldens at execution time, Phase 5) vs `:L90`/"D8 Impact", `:L189` (D17.5), `:L447`: "the numeric lane pins the intersector paths **so reordering cannot corrupt them silently**" — the reorder being task 24's update/render split. Goldens captured AFTER task 24 bake in any drift the split introduced; the pin then only guards future changes. Scheduling relevance: the missing edge (goldens ← pre-24 tree) is exactly the kind this round extracts. S5 CONFIRMED. **Remediation (paste):** Split task 45 into 45a/45b per §4(b): "45a (execute with task 9, pre-task-24): capture numeric state-hash goldens (swept-intersect sort, mid-pass removal, pass-count snapshot, gravity paths) on the pre-restructure tree into `test/numeric/golden/`; two-run determinism proof. 45b (unchanged position): assertions, randomized-angle suite, seeded runs, X11-vs-GL match — diffing against the 45a goldens."

**R8-M3 (MAJOR — internal ordering contradiction on the 9↔10 edge).** `:L593` (task 9 component 4: baseline completion "on the pre-refactor tree **(post task 10)**") directly contradicts `:L577` (task 7: "the hi-score baseline is captured at task 9 BEFORE task 10 touches score.H"), `:L602` (task 10 leg (b): "vs the task-9 hi-score baseline captured PRE-task-10"), and the O5-M1 resolution row `:L1072`. Three texts say 9 precedes 10; one parenthetical says the opposite. The contradiction decides whether 9→10 is a true edge (it is NOT — 10 only needs 9's baseline artifact) and thus whether the W3 lane pairing exists. S10 CONFIRMED as a residual. **Remediation (paste):** In task 9 component (4), replace "(post task 10)" with "(pre-task-10, per the O5-M1 ordering pinned in task 7 — the fixture renders identically before/after the score.H fixes)". 

**R8-N1 (MINOR — stale duplicate vendoring ownership).** `:L847` (task 44 Files lists `vendor/dear_imgui/` + `vendor/PINNED.md` as deliverables) vs `:L97`/`:L748–749` (vendoring lands at task 30). Two-writer ambiguity on PINNED.md. **Remediation:** task 44 Files: change the vendor entries to "`vendor/dear_imgui/` (consumed — vendored at task 30)". 

**R8-N2 (MINOR — artificial Phase-4 blocking of Phase 5 menus).** `:L847` (44 appends glue into vkBackend.H) serializes 44 behind all of 37–42 although items (i)–(v) of its Change are engine-only/domain-only. S8 CONFIRMED. **Remediation:** adopt split 44a/44b per §4(a): "44a (menus, GL leg, seam, adapter — may run concurrent with Phase 4); 44b (vkBackend.H glue + Q5 VK leg — after task 42; deleted if Phase 4 excised)." B9 preserved: letters stay inside row 44.

**R8-N3 (MINOR — ambiguous co-listing creates a phantom collision).** `:L723` lists `x11Backend.H` among task 27's files as "(consumers)" while task 25 owns that file in the same phase; 27's Change text contains no x11Backend.H edit. If the listing is real, 25∥27 collides; if defensive, they share wave W9. **Remediation:** task 27 Files: drop `x11Backend.H` or annotate "(include-only consumer; zero edits — 25 owns the file in this phase)".

**R8-N4 (MINOR — same-file near-hunk risk in the hoisted Phase-0 wave).** Tasks 2 and 5 both edit playingField.H with hunks 4–5 lines apart (`:14` strstream include vs `:18–19` Xlib/Xutil guards — verified in-tree); all other 2/5/6 overlaps are >20 lines apart. Git auto-merge is borderline only at that one cluster. **Remediation:** one sentence in the Phase-0 header: "When 2 and 5 run concurrently, land task 2's playingField.H include-guard hunk first (same commit window); remaining hunks are conflict-free."

**R8-N5 (MINOR — verification batching on a build-only task).** `:L733` gates ALL Phase-3 tasks (incl. build-only 29, and 30 which is pure vendoring) behind task 28's pixel-state re-verification. Neither task can perturb a pixel; the barrier is VB-class. **Remediation (optional, preserves harness-before-pixel-gates):** amend L733 to "no Phase-3 task that changes rendered output may start without this green; build-only tasks (29) and vendoring (30) may proceed once task 23 is green."

---

## Hypothesis scorecard (planner seeds S1–S11)

| Seed | Verdict | One-line basis |
|---|---|---|
| S1 | **CONFIRMED with refinement** | 14/16/17/18 dissolvable, 19–22 defensive listings (no new backend methods — verified against task bodies); **15 is the one real backend writer** (atomic absorption, double-execution hazard 13→15); 9-wide wave achievable once 12 completes all dormant-safe pass-throughs |
| S2 | **CONFIRMED** | 30's only prerequisite is task 1 (BACKEND var + object legs); dear_imgui unconsumed until 44; hoisted to W2 |
| S3 | **CONFIRMED** | 26 needs only task 1's GL objects target; consumers 27/34/42 all later; hoisted to W1 |
| S4 | **CONFIRMED → R8-C1** | Q5-GL leg cited inside 36 (L791) while landing at 44 (L846); acceptance "Q1-Q9 green" ungreenable at position |
| S5 | **CONFIRMED → R8-M2** | Post-D8 goldens cannot detect D8-induced FP drift; pre-24 golden capture edge added via 45a |
| S6 | **CONFIRMED** | 7∥8 (disjoint files, seed-neutral when unset); 10 needs 9's baseline only; 11 needs 2 only — W1–W3 lane material |
| S7 | **CONFIRMED** | 3/4 disjoint file sets, both need only 2's shim + 1's macro — W2 pair |
| S8 | **CONFIRMED → R8-N2** | vkBackend.H glue is 44's sole Phase-4 coupling; 44a∥Phase-4, 44b post-42 |
| S9 | **CONFIRMED (dissolvable)** | 32/33/34 disjoint method groups in one header-inline file; 3-wide wave via lettered sub-commits, serial fallback +2 |
| S10 | **CONFIRMED → R8-M3** | "(post task 10)" at L593 contradicts O5-M1 text at L577/L602/L1072 |
| S11 | **CONFIRMED (classified)** | 5→1 and 6→1 true; 2/5 hunks near-disjoint EXCEPT playingField.H :14 vs :18–19 (4-line gap, verified) → R8-N4 sequencing note; 6's hunks conflict-free |

— End of round-8 receipt. No plan, draft, or product file was modified; this receipt is the sole artifact.
