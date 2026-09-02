# Task 11 — Identity gate: MTL leg vs VK leg

## Status: PARTIAL (MTL dump produced + verified; full GL/VK byte-compare needs Linux)

## What was done

- **`test/vk/mtlmethods.C` upgraded** to render the identity scene to an
  OFFSCREEN shared-storage render target (640x512, BGRA8Unorm), wait for GPU
  completion, read pixels via `getBytes`, and write a full raw dump
  (136-byte header + top-down RGBA pixels) to `argv[1]`.
- **`mtlBridge.H`/`mtlBridge.mm`**: added `mtlWaitForGPU(queue)` (submit empty
  command buffer + `waitUntilCompleted`) and `mtlGetTextureBytes(texture,
  buffer, w, h)` (`[tex getBytes:...]`).
- **`mtlBackend.H`**: added public `waitIdle()` and `getTexturePtr(TextureId)`
  helpers so the test can wait for the GPU and reach the opaque RT texture.
- **`test/vk/vkmethods.C`**: guarded the XTest injection (extern "C" decls +
  Phase E) with `#ifndef XAST_NO_XTEST` so it builds on Darwin.
- **`makefile`**: Darwin-specific `obj/VK/vkmethods` rule compiles with
  `-DXAST_NO_XTEST` and no `-lX11 -lXtst`; the Linux rule is unchanged.
- **Pre-existing fix**: removed the non-existent `vk.shadersCompiledAtInit_`
  reference in vkmethods.C (only `shaderModulesLoaded_` exists) — this was a
  compile error blocking the Darwin build.

## Evidence

- `mtl-identity.raw` — the MTL identity dump (640x512, 2 text rects,
  1,310,856 bytes = 136 header + 1,310,720 RGBA pixels).
- `compare-mtl.py` — byte-compare script (masks text rects; classifies
  exact/tol1/text/HARD).

## MTL dump content verification

Sampled pixels confirm the scene renders correctly (top-down RGBA after
B↔R swizzle + vertical flip):

| Feature | Location | Expected | Got |
|---------|----------|----------|-----|
| thin red line | (150,100) | red | (255,0,0,255) |
| green tri fill | (360,130) | green | (0,255,0,255) |
| blue outline | (500,250) | blue | (0,0,255,255) |
| thick white line | (160,400) | white | (255,255,255,255) |
| checker left | (90,280) | white | (255,255,255,255) |
| checker right | (150,280) | black | (0,0,0,255) |
| masked left | (510,100) | colored | (255,255,0,255) |
| masked right | (556,100) | discarded | (0,0,0,255) |

Pixel counts match the VK reference expectations: reds=200, greens=6000,
blues=200, cyans=160.

## Full byte-compare limitation

The GL reference leg (`vkmethods-gl`) cannot run on Darwin: Apple's OpenGL
stack is capped at 4.1 Core / 2.1 (this machine reports `GL_VERSION=2.1
Metal`), while `glBackend` requires OpenGL 4.5 core. The full GL-vs-MTL
byte-compare therefore requires a Linux machine with Xvfb + a real OpenGL
4.5 driver. The MTL dump is verified self-consistent and content-correct
against the documented scene expectations.

## Key finding: Metal NDC is y-UP, not y-down

The learnings (task 2/6) claimed "Metal NDC = Vulkan NDC (y-down) — no
y-flip". This is **empirically WRONG**. The raw RT readback was vertically
flipped (logical top at framebuffer bottom): the thin line drawn at logical
y=100 appeared at framebuffer y=411. Metal NDC is y-UP (like OpenGL), so the
MVP mapping logical y=0 → NDC y=-1 lands at the framebuffer bottom.

The dump compensates by flipping rows in the readback (matching the GL
reference's bottom-up→top-down flip). **The underlying backend MVP is
y-flipped for BOTH the window and RT paths** — a pre-existing bug from task 6
that was never caught because no pixel readback existed until now. Fixing the
backend MVP is out of scope for task 11 (would change game rendering); it is
documented here for a future task.
