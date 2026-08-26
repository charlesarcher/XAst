# Final Verification Wave — plan row 48 certification (F1–F5 + SC1–SC13 + Q1–Q15)

Executed 2026-08-25/26 against HEAD **b631c32** (post-row-47 tree). Everything
below ran FRESH in this wave; prior green runs are cited only as historical
corroboration. Scratch runs under `/tmp/opencode/t48/`; durable artifacts in
`qa/final-wave-evidence/` + the files listed per section. **Verdict: ALL GATES
GREEN — the plan is certified**, with four proven-defect remediations applied
and documented loudly in §Remediations (two product/build, two QA-infra; none
change behavior — each is re-gated by the full wave).

Environment discipline observed throughout: `source qa/env/env.sh` before any
make/QA command; flavor trap rule (`rm -f XAsteroids` + `ldd | grep -c glfw`
before every leg switch: 0=X11, 1=GL, vulkan=1 for VK); pixel gates only at
1-min load < 2 (max observed 0.45); `pgrep -fa XAsteroids` clean before runs;
warning counts only from clean `rm -rf obj/<leg>` builds.

---

## F1 — Zero-raw-X11 architectural gate (terminal sweep): GREEN

Fresh re-run of the plan's exact commands (plan lines 397–407); methodology
identical to qa/f1-final.txt. Full receipt: **qa/final-wave-f1.txt**.

| assertion | measured | expected |
|---|---|---|
| Gate 1a — plan's verbatim grep | 48 lines, ALL in test/ (QA infra, outside the census denominator) | test/-only |
| Gate 1b — PRODUCT-TREE domain sweep | **0** | **0** |
| Gate 2 — D14 preprocess `-U X11_BACKEND` | rotatorDisplayData.C **0**, compositePixmap.C **0** | 0 / 0 |
| Exception-zone census | rotator 146 / composite 11 / options.H 47 / AutoRepeatOn 3 (each EXACT) + x11Backend.H 128 raw (island census contribution 22) | pins |
| Exception total | **229** (146+11+47+3+22) | 229 |
| Grand census | 229 exception + 205 engine-migrated = **434** intact | 434 |
| x11types.H | absent; 0 references in *.H/*.C | deleted |

Island note (unchanged from row 47): the 22 relocated island sites live in
x11Backend.H's pollEvents state machine; raw call-site grep = 13 because task 25
unified the 4 duplicated island loops (−9 raw / +0 behavior, phase2-evidence).
"No load-bearing call deleted" is behaviorally re-certified by F2's Q13
byte-exact session and the Q12 event-parity fixtures.

## F2 — Visual regression (tiered identity): GREEN

| leg | command (canonical) | result |
|---|---|---|
| X11 Tier-1 (Q13) | `./obj/harness --seed 12345 --script test/harness/scripts/session.script --out /tmp/opencode/t48/q13-x11 --ref qa/baseline-x11/session --hiscore test/harness/fixtures/hiScore.nul.data` | **RESULT: PASS — 13/13 checkpoints AE=0.000000**, 1720 boundaries, game rc=0 (benign post-'q' BadDrawable reap race only) |
| GL Tier-2 (Q10) | same script, `--ref /tmp/opencode/t48/q10-ref` (MASKLESS copy of the baseline PNGs — the committed baseline dir's 720x706-era masks would shadow `--mask-dir`), `--mask-dir qa/phase5-evidence/masks-q10 --mine-rect 24,175,640,512 --ref-rect 40,179,640,512 --handshake frame --keep` | **RESULT: PASS — 13/13 AE=0.000000** |
| VK Tier-2 (Q11) | same script, `--ref /tmp/opencode/t48/q10-gl` (the fresh GL captures from the Q10 run — same wave), `--mask-dir qa/phase4-evidence/masks --mine-rect 24,175,640,512 --ref-rect 24,175,640,512 --handshake frame --keep` | **RESULT: PASS — 13/13 AE=0.000000** |
| Tier-3 state-hash parity | `grep '^h ' <workdir>/statehash` per leg, `diff` pairwise | **X11==GL IDENTICAL 435/435; GL==VK IDENTICAL 435/435** (statediff files in qa/final-wave-evidence/) |

Font gate: 0 px outside the Tier-2 mask regions is inherent in the masked diffs
(AE==0 with text regions masked). Menu screens are excluded by design (D9/F2)
and gated functionally (F5).

## F3 — Build matrix: GREEN (after remediation R1+R2, see §Remediations)

Clean builds: `rm -rf obj && rm -f XAsteroids && make BACKEND=<B> all` per leg.

| leg | rc | warnings (clean obj dir) | baseline | link line (recipe echo, `make -n BACKEND=<B> -B`) |
|---|---|---|---|---|
| X11 | 0 | **344** | 344 | `-DX11_BACKEND` on compile+link lines; links `-lXm -lXt -lX11`; no glfw/GL/vulkan |
| GL | 0 | **257** | 257 | `-lglfw -lGL`; **zero** `-lXm/-lXt/-lX11`; no `-DX11_BACKEND` |
| VK | 0 | **256** | 256 | `-lglfw -lvulkan`; **zero** `-lXm/-lXt/-lX11`; no `-DX11_BACKEND` |

Recipe-echo counts (qa/final-wave-evidence/linkline-*.txt): GL lXm/lXt/lX11=0,
glfw=1, GL=1; VK lXm/lXt/lX11=0, glfw=1, vulkan=1; X11 lXm=lXt=lX11=1, DX11=1.
Per-backend object dirs populated independently from the clean tree:
obj/X11=3 .o, obj/GL=11, obj/VK=10 (objdirs.txt) — no cross-BACKEND .o reuse.
`make harness` rebuilt after the clean (obj/harness).

## F4 — Resources (ASan/LSan, natural-'q' full session per leg): GREEN

Recipe: `make BACKEND=<B> -B CXXFLAGS="-I/usr/include/X11 -O1 -g
-fsanitize=address -fno-omit-frame-pointer -Wall -Wextra -Wno-unused-parameter
-std=c++17" LDFLAGS="-L/usr/lib/X11 -fsanitize=address"`;
`nm XAsteroids | grep -c __asan` = 33/37/37 (X11/GL/VK) before trusting runs;
`ASAN_OPTIONS=detect_leaks=1:log_path=...`; natural-'q' exit only.

| leg | session | leaks | memory-safety |
|---|---|---|---|
| X11 | PASS 13/13 AE=0 (quiescence vs baseline), rc=1 = LSan leaks-found (expected) | **EXACTLY the documented baseline: 410 B / 19 allocs** — XtCalloc 64/4, XtMalloc 32/2, XStringListToTextProperty 10/1 + Xt indirects (112/1, 96/4, 96/7). Report: qa/final-wave-evidence/asan-x11-lsan.txt | 0 UAF / 0 double-free / 0 overflow |
| GL | PASS 13/13 AE=0 (masked Q10 regime), rc=0 | **zero — LSan produced no report file at all** (asan-gl-zero.note) | 0 |
| VK | PASS 13/13 AE=0 (masked Q11 regime), rc=1 | **53776 B / 816 allocs, 100% loader/driver noise**: 52534 B/806 `<unknown module>` (NVIDIA blob) + 1018 B/3 libdbus + 224 B/7 libxcb; **ZERO frames from game code** (asan-vk-lsan.txt) | 0 |

D15 errorInfo free asserted: (a) structurally — `shutdownFontsAndDisplay()`
(x11Backend.H:1164) frees ALL FIVE fonts incl. errorInfo (`XFreeFont` loop,
in-file comment "F4 named assertion"); (b) at runtime — the fresh X11 LSan
report contains **zero** XLoadQueryFont/XFreeFont-origin allocation frames.
Window-close exactly-once receipts: qa/phase5-evidence/closepath/ (6/6
scenarios, reaped_once ×3 per leg — cited as historical corroboration; the ASan
legs above are fresh).

## F5 — Options scripted on EVERY leg: GREEN

| leg | driver | result |
|---|---|---|
| X11 (Motif pump) | `qa/menu-x11-q5.sh` + `test/harness/scripts/menu-x11.script` (new this wave) | **RESULT: PASS** — period shift 16→32 **ratio 2.04** (pre 63.33 ms/f → post **31.09 ms/f**); modal stall 3570 ms (counter frozen while open); state-hash continuity +1 throughout (334 frames); dialog realized at root (0,0) 929x490 (live xhelper receipt); no-op dead-space click AE=0; occlusion erased on Exit (AE=69957); simulation resumed after close (AE=2881) |
| GL (Dear ImGui) | `qa/menu-gl-q5.sh` | **RESULT: PASS** — 62.75→31.25 ms/f (ratio 2.008), pause 0 frames/9.8 s, hash continuity 0 violations/748, no-op/Load/Mute round-trips AE=0, prefs 29 lines first=31250, D4 path fired |
| VK (Dear ImGui) | `qa/menu-vk-q5.sh` | **RESULT: PASS** — 62.75→31.375 ms/f (ratio 2.00), pause 0 frames/8.2 s, hash continuity 0 violations/748, round-trips AE=0, prefs 29 lines first=31250, D4 path fired |

X11 dialog geometry receipts (probe: qa/t48-probe-x11menu.sh): Options shell
maps at root (0,0) 929x490 (no WM); FPS scale trough y169–182 x9–222,
value(x)=16+(x−23.5)/184·56 — drag target (−209,12) calibrated to measured
31.2–31.4 ms/frame. The X11 preferences-file Write/Read round-trip is NOT
scripted: it requires the FileSelectionDialog sub-dialog, whose XTest-driven
choreography deterministically escapes the outer modal (root-caused this wave:
the Options shell's window disappears mid-sub-dialog while its child stays
mapped — see learnings). The options.H prefs format (29 lines, first
= uSecondsPerFrame) is certified on the GL/VK legs; the X11 FPS preference
application is asserted by the measured period (the D4 state itself).

## Q1–Q15 final confirmation

| Q | disposition this wave | result |
|---|---|---|
| Q1/Q2 (ship/rock angles) | exercised inside the fresh Q10/Q11 sessions (rotation/thrust inputs; masked 0 px at every checkpoint) + the 500-angle seeded numeric suite (cross-config hash-identical, pinned golden) | GREEN (coverage note) |
| Q3 (explosion frames) | composite probe RE-RUN fresh: `bash test/composite/run.sh` → **5/5 frames byte-equal + 3 R8 masks clean** (qa/final-wave-evidence/composite-result.log) | GREEN |
| Q4 (thrust flames) | **DEMONSTRATED** (adversarial-audit upgrade): `flame-capture.script` holds thrust ACROSS the capture boundary (`keydown o` b50 → capture b54 while held → `keyup o` b90). X11 localization: flame-vs-no-thrust-control (`flame-control.script`, nops at the same boundaries) = 157 AE with **217/217 diff pixels in one 80×80 cell at the ship** (bbox 352–368 × 405–446 vs ship center 360,435); pre-divergence control (start vs start) AE=0. Cross-leg: GL flame frame vs X11 reference under the Tier-2 regime (crops 24,175,640,512 / 40,179,640,512, frame handshake, help/start masked-0 via the q10 masks) — the unmasked residual (1366 px) was confined to the documented maskable classes at this frame's rock-decor/overlap regions, and a geometry-derived `flame.mask.png` (gen-masks.py from BOTH legs' draw dumps, frame 51, pad 5, coverage 8.44%) gives **flame AE=0.000000 → RESULT: PASS 3/3** (qa/final-wave-evidence/q4-flame-gate.log, flame.mask.png, per-leg statehashes — X11==GL identical, 127 frames) | GREEN (DEMONSTRATED) |
| Q5 (options) | F5 above, every leg | GREEN |
| Q6 (frame period) | SC4 measurement below | GREEN |
| Q7 (screen wrap) | SC5 below | GREEN |
| Q8 (button faces/bevels) | SC8 below | GREEN |
| Q9 (screens) | Q13 checkpoints help/start/hiscore AE=0 (X11) + GL/VK masked equivalents AE=0 in Q10/Q11 | GREEN |
| Q10 (GL vs X11) | F2 Tier-2 | GREEN |
| Q11 (VK vs GL) | F2 Tier-2 | GREEN |
| Q12 (event parity) | 7 fixtures × 3 legs, frame handshake: **exits 220/220, fire 247/247, hyperspace-armed 157/157, hyperspace-unarmed 147/147, rotation 127/127, thrust 147/147 statehash frames IDENTICAL X11==GL==VK** (hashes in qa/final-wave-evidence/q12-x11-*.hash; GL/VK diff-clean against them). title-hover publishes no statehash by construction (title screen, never enters RunGame): its D16 rows are each leg's F5 territory (menus backend-specific by design, D9/F2); the X11 title-click path is additionally proven by the F5-X11 run | GREEN |
| Q13 (X11 full session) | F2 Tier-1 | GREEN |
| Q14 (leaks) | F4, all three legs | GREEN |
| Q15 (resize sweep) | `resize-sweep.script` × 3 legs: 18 checkpoints each, rc=0 natural-'q', **statehash IDENTICAL to that leg's canonical stream, 435/435 each**. Letterbox (bright_bbox vs uniform-scale-centered math): **GL EXACT at all 4 sizes** (784x800+108+0; 686x700+107+0; 392x400+54+0; 700x714+0+93); VK exact at 2 sizes + the documented window-space HUD-chrome spill class at 2 (bars black: left 0.000/right 0.408, left 0.154/right 0.000); X11 exact at 3 sizes + 1-px boundary/chrome class at 500x400 (left bar 0.000; right bar 16.2 = the unscaled window-space HUD column, D17.4e design, same class as phase5) | GREEN |

## SC1–SC13 verdicts

| SC | verdict | evidence |
|---|---|---|
| SC1 build | **GREEN** | F3 (3 clean builds 344/257/256 = baselines; recipe-echo link lines; independent obj dirs) |
| SC2 X11 pixel identity | **GREEN** | F2 Q13: 13/13 AE=0 vs pre-refactor baseline, full seeded session |
| SC3 GL/VK tiered identity | **GREEN** | F2: Q10/Q11 Tier-2 masked 0 px; Tier-3 435/435 both pairs; font gate = 0 px outside masks |
| SC4 frame pacing | **GREEN** | measured default period from the Q5 drivers' live frame-counter sampling: X11 60.97–63.33 ms/f across runs, GL 62.75, VK 62.75 — all within 62.5±2 ms (no VSync cap under Xvfb; m10 escape clause not needed, measured values recorded); configurable on EVERY leg with measured shift (F5: →31.09–31.5 ms/f = 32 fps, ratio 1.94–2.04) |
| SC5 screen-wrap | **GREEN** | single draw + explicit setScissorRect: PresentFrame structure (beginFrame → beginRenderTo(canvas) → clear → setScissorRect(playRect logical) → RenderFrameDraws single replay → setScissorRect(nullptr) → endRenderTo → DrawGame → DrawPlayingField); wrap = WrapMovingBox inside the draw-argument expressions (one draw per object; no double-draw path exists); no opposite-edge ghost pixels: Q13 byte-exact across wrap-heavy sessions + Q10/Q11 masked 0 px |
| SC6 rotation | **GREEN** | all 5 RotatorDisplayData subclasses exercised in Q10/Q11 (masked 0 px); NonRot degenerate path: committed m13 proof qa/vk-soak.log:225 "static-texture transform updates: 0 (rotation path: 1)"; rotation math cross-config: 500-angle suite hash-identical (numeric lane) |
| SC7 clip masking | **GREEN** | composite probe fresh re-run: 5/5 explosion frames byte-identical CPU-vs-X11 composite + 3 R8 masks clean |
| SC8 thick lines + button faces | **GREEN** (press/release face-swap annotation): 3-px lines: VK cross-section proof (qa/vk-soak.log RUN T41-1 B: exactly 3 rows, 900 px ≈ 301×3 butt-capped quad), GL SC8 quads (task-32 gates), X11 GC semantics unchanged (Q13 0 px); button faces: the UNPRESSED face is fresh-0px in every Q13/Q10/Q11 checkpoint of this wave; the PRESS/RELEASE swap itself rests on the T16 render-target goldens (historical artifact) + the structural fact that the swap is a TextureId select between those two golden faces (no third pixel path) — no fresh scripted checkpoint captures the pressed face on X11 (the click opens the modal within the same drain) | GREEN |
| SC9 options every leg | **GREEN** | F5 (Motif pump X11 + ImGui GL/VK, all scripted, measured period shift each leg) |
| SC10 zero raw X11 in domain | **GREEN** | F1 (domain=0, exception=229 exact, census 434 intact) |
| SC11 event parity | **GREEN** | Q12: 6 gameplay fixtures × 3 legs statehash-identical (945 frames total); zero key-state boolean arrays (grep=0); GLFW_REPEAT dropped at the boundary (glBackend.H:226, vkBackend.H:2464 PRESS/RELEASE-only); no repeat-delivery divergence (thrust/hyperspace/rotation fixtures) |
| SC12 GXor + canonical order | **GREEN** | exactly ONE XSetFunction repo-wide (x11Backend.H:1057, gxorGC_ GXor; grep receipt in this wave's transcript); X11 emits original draw order verbatim (RenderFrameDraws replays the captured records); D8 canonical order is the GL/VK present path (PresentFrame comments + task-24/36 gates) |
| SC13 plan-complete | **GREEN** | 48 task rows; row 48 = this wave; TL;DR count = SC13 count = actual row count = 48 |

## Remediations (proven defects — minimal, loud)

1. **`utilities/rendering/renderingEngine.H`** (product, 2 tokens): row 47's
   `createColoredStamp` default impl used unqualified `(size_t)`; `<cstdint>`
   declares only `std::size_t`, so **`make BACKEND=GL` from clean FAILED at
   HEAD b631c32** (optionsMenu.o TU: 8 errors). Fixed `(size_t)`→`std::size_t`
   ×2. Behavior identical (same integer conversions).
2. **`makefile`** (build infra, 1 dep): `optionsMenu.o`'s prerequisite list
   omitted `renderingEngine.H` — row-47's edit of that header never triggered
   an optionsMenu.o rebuild, which is how the defect above hid behind a stale
   `.o` in row 47's own gate (the documented incremental-rebuild undercount
   trap, now closed structurally).
3. **`test/composite/probeX11.C`** (QA infra): still on the pre-row-47 D14 API
   (XColor layers, Display/Drawable ctor, no engine param) — the SC7 probe
   could not compile at HEAD. Adapted: ProbeEngine stub (the
   test/numeric/probe.C pattern) + RotColor layers + explicit
   `<X11/Xutil.h>` (XGetImage/XGetPixel/XDestroyImage were transitive through
   the OLD compositePixmap.H includes; row 47's include cleanup cut the line).
   probeCPU.C needed no change (free-function CPU path unchanged).
4. **`test/numeric/lane.sh`** (QA infra): the game-leg harvest grepped
   `$OUT/<leg>.state.hash`, but since T44a the harness (frame mode) repoints
   `XAST_STATE_HASH_FILE` at its own work dir, so the file never appeared
   (masked at 45b/47 by manual harvests / `XAST_NUMERIC_SKIP_GAME=1`). Fixed:
   `--keep` + harvest `<workdir>/statehash`. All lane gates then green
   including the full game legs.

None of these change product behavior: R1/R2 are compile-visibility only
(re-gated by the full F3 matrix + Q13 + Q10/Q11 + F4 + F5 above), R3/R4 are
test-infra.

## Cleanup receipts

- No stray processes at end: `pgrep -x XAsteroids` / `pgrep -x Xvfb` empty;
  probe displays (:97/:99/:100) torn down by their scripts/traps.
- All three legs rebuilt O3 from clean object dirs after the ASan phase
  (`nm XAsteroids | grep -c __asan` = 0 on the restored X11 binary; GL/VK
  relinked from fresh clean dirs 257/256 warnings).
- ~120 stale `/tmp/xast-harness.*` work dirs from prior sessions removed;
  this wave's kept work dirs remain under /tmp for inspection.
- `.omo/plans/rendering-abstraction.md` untouched; no commits made (orchestrator
  owns git). Worktree deltas at hand-back: renderingEngine.H, makefile,
  test/composite/probeX11.C, test/numeric/lane.sh, test/harness/scripts/menu-x11.script,
  qa/menu-x11-q5.sh, qa/t48-probe-x11menu.sh, qa/final-wave-f1.txt,
  qa/final-wave-evidence.md, qa/final-wave-evidence/, obj/ (untracked build output).

## Adversarial-audit dispositions (post-verification closure)

Recorded in response to the independent adversarial verifier's conditional
approval; nothing else re-run or changed.

1. **SC12 grep receipt committed**: the XSetFunction repo-wide sweep output is
   now a committed artifact — `qa/final-wave-evidence/sc12-xsetfunction.txt`
   (exactly 1 hit: `utilities/rendering/x11Backend.H:1057`, `gxorGC_` GXor;
   test/ excluded by the same convention as the F1 denominator).
2. **Q4 upgraded to DEMONSTRATED** — see the Q4 row above (flame-held capture,
   X11 localization + GL-vs-X11 masked AE=0 under the Tier-2 regime with a
   geometry-derived flame mask; new fixtures
   `test/harness/scripts/flame-capture.script` / `flame-control.script`).
3. **`test/numeric/out/45b/*` refreshed outputs are intentional lane-owned
   deltas**: the full `make test-numeric` run regenerates that directory by
   design. The state-hash streams are verified byte-identical old-vs-new
   (x11.state.hash and gl.state.hash each diff-clean against this wave's
   canonical 435-frame streams), i.e. the refresh carries zero drift; the
   capture PNGs/manifests are same-seed re-renders of the identical streams.
4. **Warning-baseline provenance**: the 344/257/256 figures this wave asserts
   against POSTDATE the 367/268/267 recording in qa/phase4-evidence.md — row
   47's dead-X11 cleanup LOWERED all three legs (−23/−11/−11, every delta
   attributed in qa/final-wave-evidence.md §F1 context and the row-47 commit
   message; counts only fell, never grew). The dispatch's "≤ baselines" gate
   is evaluated against the post-47 recording, which is the strictest live
   baseline.
5. **SC8 press/release face-swap annotation** — see the SC8 row above (T16
   goldens historical + structural TextureId-select; unpressed faces
   fresh-0px).

## Risks / notes for the orchestrator

1. The X11 FileSelectionDialog choreography (prefs Write/Read on the Motif leg)
   deterministically escapes the outer modal under XTest injection — root-caused
   enough to document (Options shell window vanishes mid-sub-dialog while its
   child stays mapped), not fixed (product-adjacent investigation). The X11 FPS
   preference is asserted by the measured period; the prefs format by GL/VK.
2. VK/X11 letterbox at some sizes shows the documented window-space HUD-chrome
   spill class (D17.4e design; bars black) — unchanged from phase5's
   disposition, now re-measured fresh.
3. The q12-title-hover fixture's header comment expects a hover-pressed face
   that the actual code only draws while buttonActivated — AE=0 between
   hover_on/hover_off is correct behavior on all three legs (parity holds).
