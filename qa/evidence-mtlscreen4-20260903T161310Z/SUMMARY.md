# MTL on-screen fix — render through GLFW's installed CAMetalLayer (+ MoltenVK swapchain)

Date: 2026-09-03 (UTC stamp 20260903T161310Z)
Machine: macOS 26.6.2 (BuildVersion 25G83), Apple M2 Pro, real Aqua GUI session.
Displays: built-in XDR 3024x1964 @2x (main) + external 6720x3780 @2x.
Loader/ICD: homebrew vulkan-loader 1.4.357.0 + molten-vk 1.4.2 (ICD
/opt/homebrew/opt/molten-vk/etc/vulkan/icd.d/MoltenVK_icd.json).

## Problem

`make BACKEND=MTL run` opened the GLFW window but the client area was permanently
black: `[CAMetalLayer nextDrawable]` returned nil forever, so every frame was
skipped by the nil-drawable guard. The offscreen pipeline was proven good
(`mtlmethods` golden, 1,310,856 bytes) — the defect was strictly the
on-screen window/layer/drawable path.

The prior "environment-blocked" verdict (evidence-mtlscreen-20260903T121631Z,
H5/H6) was wrong: it was refuted by the control that the VK (MoltenVK) backend
renders on this very machine (36.1% non-black measured) — the window IS
composited and drawables DO flow for the right layer.

## Root cause (exhaustively established; probe trail in evidence-mtlscreen3)

On this macOS build the window server allocates a CAMetalLayer **drawable pool**
only for a layer that satisfies BOTH:

1. It is the layer **GLFW itself installs** via `glfwCreateWindowSurface`
   (a Vulkan presentation surface on the window's NSView) — a layer the app
   creates and attaches itself (`view.layer = myLayer; view.wantsLayer = YES`)
   **never** gets a pool. Proven across 9+ attach/ordering/deferred/drawableSize
   variants in evidence-mtlscreen3-20260903T160656Z (mode0-5 logs,
   probe_layer_run.log, probe_max_run.log).
2. A **Vulkan `VkSwapchainKHR` is bound to that layer** (via MoltenVK). Creating
   the swapchain is what makes the window server allocate the pool. A GLFW
   layer with no swapchain yields no drawables either (this campaign's
   probe5-glfw-layer-noswap.log: on-screen window, real Metal present path,
   `nextDrawable` nil for 20 s).

Both conditions hold for the VK backend (it creates a MoltenVK swapchain on
GLFW's layer), which is why VK renders and MTL (self-attached layer, no
swapchain) did not.

### Two launch-time facts that gate the fix (established this campaign)

* GLFW dlopens the vulkan loader **by bare name** (`libvulkan.1.dylib`) and does
  not link it. dyld resolves that bare name only via the launch-time search
  path. With no `DYLD_FALLBACK_LIBRARY_PATH` the loader is NOT found
  (probe_glfw_metal_layer4.cpp config A: `glfwGetRequiredInstanceExtensions`
  returns NULL). A **runtime `setenv` is inert** — dyld fixes the search path at
  process start (dyld_setenv_test.cpp: baseline=0 after-setenv=0). So the run
  recipe must set `DYLD_FALLBACK_LIBRARY_PATH` at launch (the makefile `run`
  target now does, mirroring the VK recipe).
* A "loader-only" instance (no MoltenVK ICD) is NOT sufficient: the pool needs a
  real swapchain, which needs a device, which needs the MoltenVK driver. On this
  machine the loader auto-discovers the MoltenVK ICD via the homebrew default
  path, so only `DYLD_FALLBACK_LIBRARY_PATH` is strictly required
  (`VK_ICD_FILENAMES` pins it deterministically).

## The fix (MTL path only)

`mtlCocoa.mm` gains a C-ABI handshake `mtlGlfwMetalHandshake(...)` that:
1. dlopens the vulkan loader (bare name, then homebrew absolute paths) — **no
   `-lvulkan` link**; the loader is a runtime dlopen, exactly like GLFW's own.
2. Creates a minimal `VkInstance` (portability flag always; GLFW-required
   instance exts `VK_KHR_surface`+`VK_EXT_metal_surface` cross-checked against
   `vkEnumerateInstanceExtensionProperties`; `VK_KHR_portability_enumeration` if
   exposed).
3. Calls `glfwCreateWindowSurface(instance, window, nullptr, &surface)` — this is
   what makes GLFW install its own CAMetalLayer on the view.
4. Reads the layer back from `glfwGetCocoaView(window)` -> `[view layer]`,
   verified to be a `CAMetalLayer`.
5. Creates a MoltenVK `VkPhysicalDevice` + `VkDevice` (enabling `VK_KHR_swapchain`
   — without it `vkCreateSwapchainKHR` is a silent no-op) + a
   `VkSwapchainKHR` on the GLFW layer at extent = scale x canonical size, and
   primes the pool with one acquire+present.
6. Returns the GLFW layer + an opaque state handle (loader/instance/device/
   swapchain kept alive for process lifetime — destroying them tears down the
   pool). `mtlGlfwShutdown` tears it down before window teardown.

`mtlBackend.H::initWindow` now calls the handshake after `glfwCreateWindow` +
positioning. On success it uses the GLFW layer; on any failure (no loader / no
ICD / no surface) it **falls back to the self-attached layer** (headless) so the
nil-drawable guard keeps the game running on a machine without the Vulkan
toolchain. The frame lifecycle (`mtlBridge.mm`: nextDrawable -> render encoder ->
presentDrawable -> commit) is **unchanged** — it just targets the GLFW layer,
whose pool the swapchain allocates. `drawableSize` (scale x size, refreshed in the
framebuffer callback) and `displaySyncEnabled` handling are unchanged.

Build: the MTL leg gains `-Ivendor/vulkan/include` (compile-only, for the
Vulkan headers the handshake uses). The MTL **link line is byte-identical**
(`-lglfw` + Metal/MetalKit/Foundation/QuartzCore/AppKit frameworks, NO
`-lvulkan`), so the binary's flavor identity is unchanged: `flavor-check`
detects MTL (Metal.framework), and `otool -L XAsteroids | grep -i vulkan` is
empty. The run recipe (makefile `run` target, MTL branch) sets
`DYLD_FALLBACK_LIBRARY_PATH` (+ optional `VK_ICD_FILENAMES`) at launch.

## Proof (all on this machine; vision-free pixel oracle)

* Build + identity: `make BACKEND=MTL` rc=0; `qa/flavor-check.sh MTL` PASS;
  `otool -L XAsteroids | grep -i vulkan` empty (no libvulkan link).
* Bounded run: `gtimeout 25 ./XAsteroids` (with the run env) ->
  `mtlBackend: GLFW-layer handshake OK (layer=CAMetalLayer extent=1376x1404)`,
  `initWindow OK (1376x1404 fb, scale 2.0)`, **nil-drawable count = 0** over the
  run, exit **124** (alive the whole 25 s), no crash markers, clean exit.
  See run.log.
* Pixel oracle (the deciding evidence): mid-run, the window (688x734 at 556,201)
  was captured per-window and by region, then `png_pixel_probe.py <png> 16`:
    - mtl-game-window.png:  non_black=724092  (26.75%)  VERDICT=NON-BLACK
    - mtl-game-region.png:  non_black=724804  (35.88%)  VERDICT=NON-BLACK
    - mtl-game-full.png:    non_black=6758619 (80.30%)  VERDICT=NON-BLACK
  The title screen is bright; a large non-black count means the user sees the
  game. See pixel-probe-*.txt.
* Offscreen regression (UNAFFECTED): `make BACKEND=MTL mtlmethods` + run ->
  PASS; output byte-identical to the golden `qa/metal-evidence/mtl-identity.raw`
  (1,310,856 bytes) BOTH with the vulkan env (handshake success) and without
  (self-attached fallback). See mtlmethods-run.log / mtlmethods-run-swap.log.
* Flavor round-trip (link-only): MTL -> VK (`flavor-check VK` PASS) -> MTL
  (`flavor-check MTL` PASS). VK binary was linked but NOT run.
* SIGTERM clean exit: launch, SIGTERM after the handshake -> exit **143**,
  `pgrep XAsteroids` empty. See sigterm.log.

## Probe trail (this campaign, in this directory)

* probe_glfw_metal_layer4.cpp / config runs — isolates loader discovery:
  no-env => GLFW's bare dlopen fails (no layer); DYLD_FLP => loader found,
  GLFW layer installed.
* probe_metal_render5.cpp (probe5-glfw-layer-noswap.log) — GLFW layer + real
  Metal present, NO swapchain: on-screen window, nextDrawable nil 20 s.
* probe_swapchain6.cpp (probe6-glfw-layer-swapchain.log, probe6-window.png,
  probe6-full.png) — GLFW layer + MoltenVK VkSwapchain + Metal present:
  drawables flow; pixel oracle on the probe window = 74.6% NON-BLACK (the first
  on-screen confirmation).
* probe6b-autoidc-swap.log — swapchain works with the ICD auto-discovered
  (no explicit VK_ICD_FILENAMES).
* dyld_setenv_test.cpp — runtime setenv of DYLD_FALLBACK_LIBRARY_PATH is inert.
* getwid.c — CGWindowList helper to locate the on-screen window for
  `screencapture -l<windowid>`.

Prior campaign: see qa/evidence-mtlscreen3-20260903T160656Z/ (the 9+ dead
attach-variant probes + probe_glfw_metal_layer.cpp that first showed GLFW's
layer is the one that gets a pool) and qa/evidence-mtlscreen-20260903T121631Z/
(the superseded environment-blocked verdict).