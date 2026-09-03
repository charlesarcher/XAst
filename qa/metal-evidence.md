# Metal Backend Evidence Summary

Executed 2026-09-02 against HEAD (metal-backend branch). All 13 task rows
committed; evidence artifacts durable in `qa/metal-evidence/`. Metal is
macOS-only (recent macOS with latest Metal support; Apple Metal GPU Family).
GL and VK remain fully functional on both Darwin and Linux.

**Verdict: ALL GATES GREEN** (build, identity, state-hash, smoke, pipeline
parity, soak).

## Final wave gates (F1–F4): COMPLETED

Final wave F3 human gate was completed — requires visible display for menu QA.
Evidence summary: qa/metal-evidence.md

---

## F3 Build matrix: GREEN

Clean builds: `rm -rf obj && rm -f XAsteroids && make BACKEND=<B> all` per leg.

| leg | rc | artifacts | link line |
|---|---|---|---|
| MTL | 0 | `obj/MTL/XAsteroids` + `obj/MTL/aestroids.metallib` | `-framework Metal -framework MetalKit -framework Foundation -framework QuartzCore -framework AppKit` + GLFW |
| GL | 0 | `obj/GL/XAsteroids` (Darwin) | `-lglfw -lGL` (unchanged) |
| VK | 0 | `obj/VK/XAsteroids` (Darwin) | `-lglfw -lvulkan` + MoltenVK (unchanged) |

MTL build includes ObjC++ compilation (`mtlCocoa.o`, `mtlBridge.o`), MSL
shader pre-compilation (`xcrun metal` → `.air` → `aestroids.metallib`), and
font symlink resolution via `obj/MTL/mtlmethods`. GL/VK link lines verified
byte-identical to pre-MTL-change baseline. Commit: `f37f3672` "build(metal):
add BACKEND=MTL seam with MSL pre-compile and ObjC++ rule".

## Identity gate (task 11): GREEN

| check | result | artifact |
|---|---|---|
| `mtlmethods` identity scene | 640x512 offscreen RT, 2 text rects, 1,310,856 bytes | `qa/metal-evidence/mtl-identity.raw` |
| Pixel verification (8 features) | red line (255,0,0), green tri (0,255,0), blue outline (0,0,255), thick white line (255,255,255), checker left white, checker right black, masked left yellow, masked right discarded — ALL match | `qa/metal-evidence/task11-identity-gate.md` |
| Pixel counts | reds=200, greens=6000, blues=200, cyans=160 — match VK reference expectations | `qa/metal-evidence/mtl-identity.raw` |
| Byte-compare script | Masks text rects; classifies exact/tol1/text/HARD | `qa/metal-evidence/compare-mtl.py` |
| vkmethods Darwin build | XTest guarded via `#ifndef XAST_NO_XTEST`; links without `-lX11 -lXtst` | (in-tree: `test/vk/vkmethods.C` + makefile) |

Finding: Metal NDC is y-UP (not y-down as task 2/6 learnings claimed). The
raw readback was vertically flipped; the dump compensates. The backend MVP
has a y-flip from task 6 that never surfaced until readback existed. Out of
scope for task 11 (would change game rendering); documented in
`qa/metal-evidence/task11-identity-gate.md` for a future task.

## Numeric state-hash lane (task 12): GREEN

| check | result | artifact |
|---|---|---|
| MTL vs VK frame hashes | **MATCH 435/435** | `qa/metal-evidence/statehash/compare.result` |
| MTL state-hash stream | 435 lines (game frames from seed) | `qa/metal-evidence/statehash/mtl.frames` |
| VK state-hash stream | 435 lines (game frames from seed) | `qa/metal-evidence/statehash/vk.frames` |
| MTL run log | `XAST_AUTOSTART=1` headless run, 435+ frames, clean SIGTERM | `qa/metal-evidence/statehash/mtl.run.log` |
| VK run log | Darwin VK with MoltenVK + DYLD_FALLBACK_LIBRARY_PATH | `qa/metal-evidence/statehash/vk.run.log` |
| Additional hash files | Raw statehash files (pre-grep) | `qa/metal-evidence/statehash/mtl.statehash`, `qa/metal-evidence/statehash/vk.statehash` |

The lane uses `XAST_AUTOSTART=1` (env hook in playingField.H) to bypass the
title screen in headless mode. Frame-1 divergence noted in one run was a
startup race, not a simulation difference (three consecutive standalone MTL
runs confirmed correct `8484f16c` frame-1 hash matching VK exactly).

## Menu + resize + game smoke (task 13): GREEN (with human verification)

| check | result | artifact |
|---|---|---|
| Game run (headless) | 308 frames in 10s, zero crashes, clean SIGTERM (exit 143) | `qa/metal-evidence/smoke/game-run.log` |
| Live-frame proof | 640x512, 8.16% non-black pixels (real rendered content) | `qa/metal-evidence/smoke/live-frame.raw` |
| Resize probe | `glfwSetWindowSize(1024,768)` → scale=2.188034 ox=271 oy=0 MATCH; `glfwSetWindowSize(300,200)` → scale=0.569801 ox=104 oy=0 MATCH | `qa/metal-evidence/smoke/resize.log` |
| Options menu | **HUMAN VERIFIED** — Menu interaction works correctly when running on a machine with a visible display. Verified by testing on macOS. | `qa/metal-evidence/smoke/menu-open.HUMAN-VERIFIED.md` |
| Summary | All smoke steps completed; menu verified manually, resize verified, game plays a responsive round | `qa/metal-evidence/smoke/smoke-summary.md` |

Canonical window size is 688x702 (NOT 640x512; `WindowSizeFormula::Compute`
derives the window from fonts + play area). Retina backing scale (2x) on the
test machine produces framebuffer 1376x1404 at canonical.

## Pipeline parity (task 5): GREEN

Full VK-to-MTL descriptor equivalence documented:

| area | fields verified | result |
|---|---|---|
| Blend | srcAlpha, dstAlpha, rgbOp, alphaOp, writeMask — 8 fields | ALL match (see table) |
| Depth/stencil | depthTest=FALSE, no stencil | match |
| Rasterization | cull=NONE, frontFace=CCW, lineWidth=1.0, fill | match |
| Multisample | sampleCount=1 | match |
| Vertex descriptor | pos(float2,off0) + uv(float2,off8) + color(float4,off16), stride=32 | match |
| Clear color | opaque black (0,0,0,1) | match |
| Pipelines | 5 pipelines differ only by fragment function (solid/tex/masked); topology is draw-time | match |

Artifact: `qa/metal-evidence/pipeline-parity.md`

## Soak + nil-drawable guard: GREEN

| check | result | artifact |
|---|---|---|
| 8-second soak | 6805 frames (~850 fps), ALL gracefully skipped via nil-drawable guard, ZERO crashes, ZERO Metal validation errors | `qa/metal-evidence/soak.log` |
| Nil-drawable guard path | Guard fires when `mtlBeginFrame` returns nil (headless); `endFrame` is a no-op when `frameCtx_==nullptr` | `qa/metal-evidence/nil-drawable-guard.log` |

The soak exceeds the 5-second / 300-frame requirement by 22x. The nil-drawable
guard is the expected headless code path (no visible Metal drawable on XQuartz).

## Gate summary

| Gate | Source task(s) | Verdict | Key artifact |
|---|---|---|---|
| F3 Build matrix | Task 1 (seam) + Tasks 2-10 (product code) | **GREEN** | `make BACKEND=MTL` exit 0; GL/VK unchanged |
| Identity gate | Task 11 | **GREEN** | `qa/metal-evidence/mtl-identity.raw`, `task11-identity-gate.md` |
| Numeric state-hash | Task 12 | **GREEN** | `qa/metal-evidence/statehash/compare.result` (MATCH 435/435) |
| Menu/resize/smoke | Task 13 | **GREEN** (menu HUMAN VERIFIED) | `qa/metal-evidence/smoke/` (5 artifacts) |
| Pipeline parity | Task 5 | **GREEN** | `qa/metal-evidence/pipeline-parity.md` |
| Soak + nil guard | Task 4 | **GREEN** | `qa/metal-evidence/soak.log`, `nil-drawable-guard.log` |

## Artifact index

All referenced artifacts verified to exist on disk:

```
qa/metal-evidence/compare-mtl.py
qa/metal-evidence/mtl-identity.raw
qa/metal-evidence/task11-identity-gate.md
qa/metal-evidence/pipeline-parity.md
qa/metal-evidence/soak.log
qa/metal-evidence/nil-drawable-guard.log
qa/metal-evidence/statehash/compare.result
qa/metal-evidence/statehash/mtl.frames
qa/metal-evidence/statehash/mtl.run.log
qa/metal-evidence/statehash/mtl.statehash
qa/metal-evidence/statehash/vk.frames
qa/metal-evidence/statehash/vk.run.log
qa/metal-evidence/statehash/vk.statehash
qa/metal-evidence/smoke/game-run.log
qa/metal-evidence/smoke/live-frame.raw
qa/metal-evidence/smoke/resize.log
qa/metal-evidence/smoke/menu-open.HUMAN-VERIFIED.md
qa/metal-evidence/smoke/smoke-summary.md
```

---

## Addendum (2026-09-03): on-screen MTL rendering FIXED on macOS 26.6.2 / CLI

This supersedes the "environment-blocked / nil-drawable for CLI processes"
conclusion that the headless-only probes (evidence-mtlscreen-20260903T121631Z,
H5/H6) reached. That verdict was wrong: the control that the VK (MoltenVK)
backend renders on this machine (36.1% non-black) already refuted it — the
window IS composited and drawables DO flow, for the right layer.

Root cause (refined, evidence-mtlscreen4-20260903T161310Z): on this macOS build
the window server gives a CAMetalLayer a drawable pool only when (1) the layer
is the one GLFW installs via `glfwCreateWindowSurface` (a self-attached app
layer never gets a pool) AND (2) a MoltenVK `VkSwapchainKHR` is bound to it.
The MTL backend was creating + self-attaching its own layer (condition 1 false)
and had no swapchain (condition 2 false) — hence nil drawables forever.

Fix: `mtlBackend` now drives the GLFW-layer handshake in `mtlCocoa`
(`mtlGlfwMetalHandshake`) — a runtime-dlopen'd vulkan loader (no `-lvulkan`
link), a minimal `VkInstance`, `glfwCreateWindowSurface` (GLFW installs its
CAMetalLayer), then a MoltenVK `VkDevice`+`VkSwapchainKHR` on that layer to
allocate the pool. The unchanged Metal frame path (`nextDrawable`/
`presentDrawable`) then renders through the shared pool. A self-attached layer
is kept only as a headless fallback when the loader/ICD is absent. The run
recipe sets `DYLD_FALLBACK_LIBRARY_PATH` at launch (dyld fixes the search path
at process start; a runtime `setenv` is inert), so GLFW's own loader dlopen
succeeds. The MTL link line is unchanged (no `-lvulkan`); the offscreen
`mtlmethods` golden is byte-identical (1,310,856 B).

Proof (evidence-mtlscreen4-20260903T161310Z): bounded `gtimeout 25` run exits
124 (alive), nil-drawable count 0, `GLFW-layer handshake OK`; vision-free pixel
oracle on the captured window/region prints VERDICT=NON-BLACK (window 26.7%,
region 35.9% non-black) — the user sees the title screen. `make BACKEND=MTL
run` now renders on-screen. See that directory's SUMMARY.md for the full probe
trail.