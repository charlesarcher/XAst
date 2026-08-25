# Phase 4 evidence — tasks 43 + 44b (wave W22)

Executed 2026-08-25 as ONE combined dispatch against HEAD `1a59c43` (task 42).
No commits (orchestrator owns git). Scratch runs under `/tmp/opencode/t43-*`
(work dirs kept under `/tmp/xast-harness.*`, ephemeral); durable artifacts in
`qa/phase4-evidence/`.

## Changed files

| File | Change |
|---|---|
| `makefile` | `-DVK_BACKEND` rides the VK leg's BACKEND_CXXFLAGS (mirroring `-DGL_BACKEND` on GL); `MENU_OBJECTS` opens to VK; VK link rule gains `$(IMGUI_OBJECTS) $(MENU_OBJECTS)`; `vkBackend.H` added to XAsteroids.o deps |
| `XAsteroids.C` | VK main block mirroring the GL block exactly (same construction order, placement-new staging, `StarDestroyer::glColor` yard colors, shim values, menu ctor + `installMenuInputBridge()`, initial present pair, reverse-order deletes, `shutdown()`); includes vkBackend.H + optionsMenu.H under the VK macro; no key trampoline (vkBackend self-contains D16 input) |
| `gamePlay/playingField.H` | include block serves VK (`vkBackend.H` under `VK_BACKEND`); RunGame + title-loop drains consume `closeRequested_` via `#elif defined(VK_BACKEND)`; all three canvas-offset bracket sites (world replay, score table, help screen) extended to VK; RunGame game-over tail opens the frame before the table draws (GL-safe pump; VK endFrame no-ops without an acquire — the table never reached the window without it) |
| `utilities/rendering/vkBackend.H` | `setCanvasOffset` (+`canvasOffX_/canvasOffY_`, applied inside `presentMVP_` swapchain branch only — glBackend mirror); `installMenuInputBridge` (ImGuiIO BackendPlatformName + clipboard stubs; imgui.h include); beginFrame idempotency guard (the domain double-pumps: a second begin must not re-acquire/re-record — it stranded all 3 swapchain images -> 100x acquire timeout -> fatal stall); shutdown resets the command pool and retires the descriptor pool BEFORE texture views; `deleteTexture` waits unconditionally (early exits leave `passOpen_` true and raced pending cbs); `swapchainLoadRenderPass_` now destroyed (was leaked at DestroyDevice); reopen pass external dependency aligned with the CLEAR pass (VUID-00904) |
| `gamePlay/optionsMenu.H/.C` | ImGuiOptionsMenu guard widened to `GL_BACKEND \|\| VK_BACKEND` (44b); stale ordering-contract comment updated |
| new: `test/harness/scripts/menu-vk.script`, `qa/menu-vk-q5.sh` | Q5 VK leg (44b item 8) |

## Build gates (final code)

- `make BACKEND=VK objects` rc=0, **267 warnings = HEAD baseline, warning SET
  byte-identical** (located-set diff vs a stashed HEAD build: empty)
- `make BACKEND=VK` links rc=0; ldd glfw=1 vulkan=1, zero Xm/Xt/X11
- `make BACKEND=GL objects` rc=0, **268 = baseline**; link rc=0
- `make BACKEND=X11` links rc=0, **367 = baseline**, glfw=0
- No new -Wno flags. GOTCHA re-learned: incremental rebuilds UNDERCOUNT
  warnings (stale .o files do not re-emit) — every count above is from a clean
  `rm -rf obj/<leg>` build.

## Q13 X11 regression (shared-file edit safety)

Canonical invocation after ALL changes: **RESULT: PASS, 13/13 checkpoints
AE=0.000000** (`/tmp/opencode/t43-q13-final`). The playingField.H edits sit
inside non-X11 branches; the X11 compile path is untouched.

## VK driver regressions through the modified backend
(RUN T43-1/T43-2 appended to qa/vk-soak.log)

vkprobe PASS, vksurface PASS, vkpipe PASS, vkpass PASS (scissor proof
byte-exact), vksoak PASS — all with validation LIVE, 0 counted
VUID/UNASSIGNED errors, loader noise (broken liblsfg-vk "Loader Message")
correctly uncounted.

## Q11 — VK==GL identity gate (row 43)

Method: fresh GL reference leg then fresh VK gate leg, both frame-handshake
(`--handshake frame --keep`), seed 12345, session.script, hiScore fixture.
Both windows 688x702 border=0 at +296+161 -> crop rect 24,175,640,512 BOTH
legs. Masks regenerated per task-36 regime from BOTH legs' draw/text dumps
(union; draw-class geometry, never pixel diffs): re-rasterized 'o'/'t'
classes, pairwise GXor/blend overlaps, text blocks. Coverage honest-gate max
44.73% (table screens). The table text mask applies to gp6..gp10 as well as
hiscore — those checkpoints show the same static table screen, and its stb
glyph AA edges are exactly the UV-phase class (below).

### Tier-2 result (qa/phase4-evidence/q11-tier2-gate.{log,manifest.txt})

```
help AE=0.000000   start AE=0.000000  gp1 AE=0.000000  gp2 AE=0.000000
gp3 AE=0.000000    gp4 AE=0.000000    gp5 AE=0.000000  gp6 AE=0.000000
gp7 AE=0.000000    gp8 AE=0.000000    gp9 AE=0.000000  gp10 AE=0.000000
hiscore AE=0.000000   RESULT: PASS (all checkpoints AE=0)
```

UV-phase class disposition: gameplay checkpoints (start, gp1-gp5) are 0 px
outside masks with world sprites byte-exact — the T42 ~2px LINEAR-sampling
phase class did not appear outside the rotated-texture ('t') masks during
play. On the static table screens it appears as +-1/255 gray on glyph AA edge
pixels (199 px measured pre-mask); masked per the class rule. The T42 +/-4px
same-value classifier was not needed as a residual escape: after correct
masking, hard residuals = 0.

Determinism: an independent earlier VK capture run is byte-identical at every
spot-checked checkpoint (gp1/gp5/gp8/hiscore AE=0 vs the gate run), and every
VK full-session run of this dispatch produced identical state-hash sequences.

### Tier-3 result (qa/phase4-evidence/q11-tier3-statediff.txt)

State-hash sequences IDENTICAL VK vs GL across the full session: **435/435
frames** (`diff` empty) — matching the 45b X11==GL count. Verified on the
capture pair AND each subsequent gate/soak run.

### Validation soak (qa/phase4-evidence/q11-validation-{soak.log,counts.txt})

Full seeded session with KHRONOS validation layers LIVE
(vulkan-validation-layers 1.4.357.0-1.1 extracted to /tmp/opencode/vvl):
**0 counted ERROR-level messages** (VUID-/UNASSIGNED-prefixed; counter rule
per the vkBackend debug callback). Loader noise: 2 uncounted
`[vk-loader-error]` lines (broken liblsfg-vk implicit layer). State hashes
identical to the GL reference during the soak.

Three real defects were found and fixed by this soak (all in vkBackend.H):

1. VUID-00904 x19 — the mid-frame render-target reopen began
   `swapchainLoadRenderPass_` on framebuffers built for the CLEAR pass with
   mismatched external-dependency access masks.
2. VUID-01026 x2 — `deleteTexture` skipped its idle wait while `passOpen_`
   was stuck true after an early exit, racing pending command buffers.
3. VUID-05137 x1 — `swapchainLoadRenderPass_` was never destroyed.

The finite-timeout/OUT_OF_DATE re-bootstrap path did NOT trigger in any clean
session (0 stalls, 0 re-bootstraps) — task 39's forced-injection tests remain
the branch coverage.

## Q5 VK leg (row 44b item 8)

`qa/menu-vk-q5.sh` + `test/harness/scripts/menu-vk.script` mirror the GL pair.
Three consecutive runs, ALL PASS:

| run | pre period | post period | ratio | pause | hash continuity |
|---|---|---|---|---|---|
| 1 | 63.158 ms/f | 31.375 ms/f | 2.013 | 0 frames / 9263 ms | 0 violations / 748 frames |
| 2 | 63.158 ms/f | 31.772 ms/f | ~1.99 | PASS | PASS |
| 3 | 62.143 ms/f | 31.250 ms/f | ~1.99 | PASS | PASS |

Expected 62.5 -> ~31.25 ms/frame through uSecondsPerFrame: measured shift
asserted; pause freezes the frame counter while open; resume deterministic
(+1 throughout); no-op click / Load round-trip / Mute round-trip AE=0; Mute
and FPS slider change the UI (AE>0); overlay erased on close; world moves
after resume; prefs file 29 lines with first value 31250 (=1E6/32); game.log
shows the D4 path fired ("[menu] fps 32"). The 44a slider calibration carried
over unchanged (fixed window (40,60) 430x190; value(x)=16+(x-60)*2/9;
focus-click dead space after open; pointer parked before every capture).

## Grep gates

- glad/vulkan/GLFW INCLUDES outside {glBackend.H, vkBackend.H, test/, vendor/}: **0**
- `vk*` API calls outside {vkBackend.H, test/, vendor/}: **0**
- GL API calls outside glBackend.H: **0** (all contained; the domain's
  `gl`-prefixed identifiers — glUploadColoredStamp, glDrainEvents,
  glBakeStaticSprite, glUploadShipDecor, glBakeRotatedSprite, glEventQueue,
  glKeyTrampoline, glTraceOn — are project helpers, not GL API)
- ImGui includes/calls outside {optionsMenu.*, menuAdapter.H, glBackend.H,
  vkBackend.H, vendor/}: **0 code hits** (renderingEngine.H carries two
  pre-existing doc-comment mentions of the D9 adapter contract only)

## Cleanup receipts

- Repo-root probe binaries (vkprobe/vksurface/vksoak/vkpass/vkpipe copies)
  removed after each driver run (`rm -f` verified via git status).
- Temporary debug instrumentation in vkBackend.H removed; diff vs the
  pre-instrumentation backup shows ONLY the two intended fixes.
- No commits made; `.omo/plans/rendering-abstraction.md` untouched by this
  dispatch (orchestrator-owned checkbox dirt pre-existed).
- Xvfb :98 standalone display left running is killed by harness runs'
  pkill; no orphaned XAsteroids processes remain (pgrep-verified).

## Risks / notes for the orchestrator

- The beginFrame idempotency guard and the table-path beginFrame are
  BEHAVIORAL changes to shared frame lifecycle; both proven GL-neutral
  (GL reference re-captured AFTER them; Q13 green; GL objects warning set
  unchanged).
- optionsMenu.H guard widening means obj/VK/optionsMenu.o now exists; its
  warnings are inside the unchanged 267-warning VK set (verified set-diff).
- The UV-phase mask extension to gp6..gp10 is derived from textdump geometry
  (same class as hiscore's), keeping the mask regime honest.
