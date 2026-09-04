# Task 15 — flipfix: MTL on-screen render was upside down (user-reported 2026-09-03)

UTC stamp dir: `flipfix-20260904T000105Z`

## Root cause (confirmed, matches orchestrator verification)

Metal NDC is **y-UP** (origin bottom-left, like OpenGL); only Vulkan is
y-down. The task-1-14 MTL port copied the VK y-down projection verbatim on
the false premise "Metal NDC = Vulkan NDC (both y-down)"
(`mtlBackend.H:34`, `aestroids.metal:17`), so logical top (y=0) mapped to
NDC −1 — the **bottom** on Metal → the whole frame mirrored vertically on
screen. `MTLScissorRect` is bottom-left-origin too, so `mtlSetScissor`
passing top-left y through unchanged would have clipped the wrong vertical
region (D7 play-area wrap).

## Why the task-11 identity gate passed pre-fix (forensics)

The MTL probe's readback **explicitly flipped rows** (`mtlmethods.C`
pre-fix, documented in `qa/metal-evidence/task11-identity-gate.md`:
"the thin line drawn at logical y=100 appeared at framebuffer y=411 … the
dump compensates by flipping rows"). The readback flip exactly **canceled**
the backend's missing MVP flip, so the offscreen dump came out top-down and
byte-identical to the reference — while the on-screen present path (drawable)
was never exercised by any gate. Proof: the pre-fix tree's dump
(`preA-mtl.raw`) is **byte-identical** to the committed task-11 dump
(`cmp` clean, 1,310,856 bytes), and the on-screen oracle measured the
inversion (below). The numeric lane (state hashes) never sees pixels.

## Fix (MTL sources only; frozen backends untouched)

- `utilities/rendering/mtlBackend.H`:
  - `presentMVP_()` rt branch: `b=-2.0f/rtHeight_`, `mvp_[13]=b*mf+1.0f`;
    present branch: `b=-2.0f*presentScale_/H`, `d=1.0f-2.0f*presentOffY_/H`
    (x terms, offsets, rotation coupling unchanged — the rotation rows pick
    up the sign of b, which is the correct mirror).
  - `windowMVP_()`: `winMvp_[5]=-2.0f/H`, `winMvp_[13]=+1.0f` (menu/ImGui
    path un-mirrors too).
  - `applyPresentTransformToScissor_()` comment now states the convention
    split (backend: logical→top-left window space; bridge: bottom-left).
  - File header comment (the false premise) corrected.
- `utilities/rendering/mtlBridge.mm`: `mtlSetScissor` is now the SINGLE
  y-conversion point: `y_mtl = passHeight - y - h` (`passHeight` tracked on
  the frame context for window and offscreen passes; h≤0 keeps the legacy
  clamp; full-frame (0,0,W,H) stays a no-op).
- `utilities/rendering/mtlShaders/aestroids.metal`: comment corrected — the
  flip lives in the MVP uniform; the vertex shader stays flip-free. **No
  shader flip added.**
- Texture upload row order + UV sampling verified consistent (top-down
  uploads, v=0 = top, NDC +1 = top): textured content is NOT per-sprite
  flipped.

## Asymmetric orientation probe (so a flip can never pass silently)

- `test/vk/identityScene.H`: cyan marker rect ONLY at the top edge
  (10,4,48,12) + magenta marker rect ONLY at the bottom edge (10,496,48,12)
  — rendered by every gate leg (backend-neutral shared scene).
- `test/vk/mtlmethods.C`: the readback row-flip is **removed** (dump is
  already top-down post-fix; convention documented) and a self-check asserts
  topCyan / botMagenta / topNotMagenta / botNotCyan **plus** per-sprite
  texture orientation (masked-quad top-left must read yellow, bottom-left
  magenta). A flip remaining in exactly ONE stage (MVP or readback) swaps
  the halves and fails the probe.
- `test/vk/vkmethods.C`: marker edges added to `g_edges[]` (edge class in
  the cross-backend compare).

## Gate results (this host: macOS 26, Apple Silicon, Retina 2x)

| Gate | Command | Result |
|---|---|---|
| MTL build | `make BACKEND=MTL` | exit 0; warnings all pre-existing classes (`-Wreorder-ctor` ctor, `-Wignored-qualifiers`, …) — none introduced by this diff |
| MTL identity (post) | `make BACKEND=MTL mtlmethods` + `./mtlmethods <dir>/post-mtl.raw` | **PASS exit 0**, orientation flags all 1 (`topCyan=1 botMagenta=1 topNotMagenta=1 botNotCyan=1 texTopYellow=1 texBotMagenta=1`), resize probe MATCH |
| MTL identity (pre-B) | same probe vs pre-fix backend | **FAIL exit 1**, all flags 0 — the new probe catches the legacy bug (dump not top-down once the old readback compensation is removed) |
| Dump invariance | preA vs post pixel compare | bit-identical except (a) the new markers and (b) a 1-px **line-phase** class on the 5 LINE_LIST edges — classified in `identity-analysis.md` (sub-pixel line coverage artifact, same class the cross-backend comparator tolerates as `edge`); fills/textures/markers bit-identical |
| VK identity (Darwin) | `make BACKEND=VK vkmethods` + two-pass self-reference (bootstrap `vk-blackref.raw` → wrap `probe-vk.raw` as `vk-selfref.raw`) | build green; phase C byte-compare **HARD=0** (exact=1,888,923 / 97.78% + documented classes); phases A (32/32), B (m13 0/1), F (0 VUID) green; E skipped (`XAST_NO_XTEST`); the 7 small pixel-spot checks + phase D are **structurally invalid on a 2x Retina display** (they sample logical coords as pixel coords — pre-existing, not a regression: the VK scene content is empirically top-down at 2x, green triangle at px rows 160..358 = logical 80..180 ×2) |
| Numeric lane | `test/numeric/lane-macos.sh` | **ALL GATES GREEN** — MTL-vs-VK 435/435 frame state hashes identical; first 435 MTL lines byte-identical to the task-12 capture (fix is render-only) |
| Frozen backends | `make BACKEND=GL`, `make BACKEND=VK` | both exit 0 |
| On-screen oracle (pre) | `flaunch ./XAsteroids` (MTL flavor, title screen) + `screencapture` + `screen-oracle.py` on `pre-screen.png` | handshake OK; **VERDICT INVERTED** — darkFraction top=0.8710 bottom=0.4792 (black play-area band at the TOP = mirrored) |
| On-screen oracle (post) | same on the fixed build, `post-screen.png` | handshake OK; **VERDICT CORRECT** — darkFraction top=0.4804 bottom=0.8707 (play area at the bottom, exactly mirrored) |

Oracle: vision-free dark-pixel fraction, top half vs bottom half of the
captured client region (title screen = gray chrome + black play-area rect;
the band position is the orientation signal). Pure-stdlib PNG decode
(`screen-oracle.py`, committed beside this pack). `flaunch` (small local
launcher in /tmp) restores `DYLD_FALLBACK_LIBRARY_PATH`/`VK_ICD_FILENAMES`
for the handshake: the agent shell is hardened-runtime and the kernel strips
DYLD_* vars from its children; the user's interactive terminal does not hit
this restriction (their `make BACKEND=MTL run` reproduced the bug directly).

## Before / after

- `pre-screen.png` / `preA-mtl.raw` / `prefix-run.log` / `prefix-mtlmethods*.log` — pre-fix state (inverted on screen; offscreen dump "correct" only via the canceled flip)
- `post-screen.png` / `post-mtl.raw` / `postfix-run.log` / `postfix-mtlmethods.log` — post-fix state (correct on screen; dump top-down without compensation)
- `preB-mtl.raw` + `prefix-mtlmethods-probe.log` — new probe vs pre-fix backend (RED: proves the probe detects the bug)
- `identity-analysis.md` — the 1-px line-phase diff classification
- `lane-macos.log`, `mtl.*`/`vk.*` statehash artifacts — numeric parity
- `prefix-statehash/` — the pre-run task-12 captures (unchanged on disk except the documented N+1 frame line)
- `vk-leg-frame.raw` / `vkmethods-pass1.log` / `vkmethods-pass2.log` — VK leg evidence

Human confirmation (plan F3): the user re-runs `make BACKEND=MTL run` —
top of canvas now at the top of the window.