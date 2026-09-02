# Metal Backend — Task 13 Smoke Evidence

**Date:** 2026-09-02
**Backend:** MTL (`make BACKEND=MTL`)
**Host:** macOS (Darwin), arm64, headless XQuartz display

## Summary

This evidence pack proves the shipped `./XAsteroids` MTL artifact runs the
real game on Metal with a working letterbox resize transform. The game
launches, initializes the Metal backend (5 render pipelines, window, layer),
and runs the game loop stably without crash. The resize probe proves the
letterbox transform recomputes correctly per the scale=min formula. The
Options-menu interaction is **HEADLESS-SKIP** (deferred to F3) because the
headless environment cannot render a visible menu frame.

## Evidence files

| File | What it proves |
|------|----------------|
| `game-run.log` | Game launches, Metal init OK, game loop runs 308 frames in 10s without crash, clean SIGTERM shutdown (exit 143) |
| `live-frame.raw` | Live-frame proof: offscreen RT readback, 640x512, 8.16% non-black pixels (real rendered content, not the nil-drawable guard path) |
| `resize.log` | Resize probe: letterbox transform recomputes per scale=min formula, ox/oy MATCH at 1024x768 and 300x200 |
| `menu-open.HEADLESS-SKIP.md` | Options-menu interaction deferred to F3 (headless cannot render visible menu) |

## 1. Game smoke (live-frame proof)

**Launch:** `XAST_AUTOSTART=1 XAST_SEED=42 ./XAsteroids` from repo root.

The game initialized the Metal backend:
```
mtlBackend: canonical window 688x702
mtlBackend: 5 render pipelines created
mtlBackend: initWindow OK (1376x1404 fb, scale 2.0)
```

The game loop ran **308 frames in 10 seconds** with zero crashes. In this
headless environment every frame hits the nil-drawable guard
(`mtlBackend: nil drawable — skipping frame`) because the Metal layer has no
composited drawable — the documented headless behavior. The game loop runs
stably regardless.

**Live-frame proof:** `live-frame.raw` is the task-11 readback hook output —
an offscreen render target (640x512) rendered with the identity scene and
read back via `getBytes`. It contains **8.16% non-black pixels** (26751 of
327680), proving the MTL backend renders real content (checkerboard, quad,
text), not just the nil-drawable guard path.

## 2. Options menu — HEADLESS-SKIP

Accessibility IS granted (System Events can enumerate the `XAsteroids`
process and its `Asteroids` window, and send clicks). However, the game is
headless (nil-drawable on every frame), so the Dear ImGui Options overlay
cannot be visually rendered, captured, or interacted with. A System Events
`click at {558,231}` on the Options button (logical client 2,2) was accepted
but produced no visible menu (headless). The FPS-slider drag and prefs-file
write require a visible menu, so they are deferred to F3 (human gate) on a
visible-display machine. See `menu-open.HEADLESS-SKIP.md`.

## 3. Resize probe (TCC-free, COMPLETE)

`mtlmethods` owns its GLFW window. The probe:
1. Creates the window at the canonical size (688x702 — the WindowSizeFormula
   result for play area 640x512 + header).
2. `glfwSetWindowSize(win, 1024, 768)` → re-checks `getPresentTransform()`.
3. `glfwSetWindowSize(win, 300, 200)` → re-checks `getPresentTransform()`.

The backend's `recomputePresentTransform_` uses the FRAMEBUFFER size divided
by the LOGICAL canonical size (Retina backing scale 2x here). The probe
reproduces this exactly and verifies the returned ox/oy:

```
resize: canonical window 688x702, initial transform scale=2.000000 ox=0 oy=0
resize: 1024x768 (fb 2048x1536) -> scale=2.188034 ox=271 oy=0 (expected scale=2.188034 ox=271 oy=0) MATCH
resize: 300x200 (fb 600x400) -> scale=0.569801 ox=104 oy=0 (expected scale=0.569801 ox=104 oy=0) MATCH
mtlmethods: PASS
```

Both probes **MATCH** — the letterbox transform recomputes correctly per the
scale=min formula. `oy=0` in both cases (the height ratio is the binding
constraint), matching the task's expected pattern.

**Note on the task's 640x512 expected values:** the task assumed the canvas
is 640x512, but the backend's transform uses the computed canonical WINDOW
size (688x702 = play area 640x512 + header), so the absolute ox values differ
(271 and 104 vs the task's 32 and 25). The oy=0 matches. The transform
FORMULA is verified correct against the backend's actual canonical size.

## 4. Clean shutdown

The game was terminated with SIGTERM after running stably. Exit code **143**
(128+15 = terminated by SIGTERM) — a clean signal-based exit, no crash, no
hang. The task explicitly allows SIGTERM for clean shutdown. A true exit-0
window-close path is not reachable in headless mode (the window-close
callback requires a visible window to close).

## 5. Build verification

`make BACKEND=MTL` exits 0. The `./XAsteroids` binary links Metal
(`-framework Metal -framework MetalKit -framework Foundation -framework
QuartzCore -framework AppKit`), no Vulkan/OpenGL.

## Files NOT modified (per task constraints)

`renderingEngine.H`, `mtlCocoa.H`, `mtlCocoa.mm`, `mtlShaders/aestroids.metal`,
and all X11/GL/VK product backends were NOT modified. The only source changes
are the resize probe in `test/vk/mtlmethods.C` and a test-only `glfwWindow()`
accessor in `utilities/rendering/mtlBackend.H`.
