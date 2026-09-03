// utilities/rendering/mtlCocoa.H — thin C-ABI bridge: GLFW Cocoa view ↔ Metal CAMetalLayer
//
// Pure C header — no ObjC types, no Metal/QuartzCore includes.
// All Objective-C pointers are opaque void* on this side of the ABI.
#ifndef _MTL_COCOA_H
#define _MTL_COCOA_H

#ifdef __cplusplus
extern "C" {
#endif

struct GLFWwindow;  // forward decl, no GLFW include in header

// Create a CAMetalLayer. Returns opaque pointer (actually CAMetalLayer*).
void* mtlCreateLayer(void);

// Attach the layer to a GLFW window's NSView. Returns 0 on success.
// CRITICAL ORDER: set view.layer = layer FIRST, then view.wantsLayer = YES.
int mtlAttachToView(void* layer, GLFWwindow* window);

// Get the backing scale factor (contentsScale) for a window.
double mtlBackingScaleFactor(GLFWwindow* window);

// Create a MTLDevice. Returns opaque pointer (actually id<MTLDevice>).
void* mtlCreateDevice(void);

// Set the drawable size on the layer (pixels = bounds × contentsScale).
void mtlSetDrawableSize(void* layer, double scale, int fbW, int fbH);

// ---- GLFW-layer on-screen handshake (2026-09-03) -------------------------
// On this macOS build the window server allocates a CAMetalLayer drawable
// pool ONLY for the layer GLFW installs via glfwCreateWindowSurface (a Vulkan
// presentation surface) — AND only once a MoltenVK VkSwapchainKHR is bound to
// that layer. A layer the app creates and attaches itself never gets a pool.
//
// mtlGlfwMetalHandshake performs that handshake: dlopen the vulkan loader (NO
// -lvulkan link; the loader is a runtime dlopen, like GLFW's own), create a
// minimal VkInstance (loader-only: portability flag + GLFW-required instance
// exts cross-checked vs the loader), call glfwCreateWindowSurface (GLFW
// installs its CAMetalLayer on the view), then create a MoltenVK VkDevice +
// VkSwapchainKHR on that layer (the swapchain is what allocates the pool).
// The Metal frame path (nextDrawable/presentDrawable) then draws from the SAME
// shared layer pool.
//
// On success: *outLayer = the GLFW-installed CAMetalLayer (opaque), *outState =
// an opaque handle keeping the loader/instance/device/swapchain alive for
// process lifetime (the pool is torn down if they are destroyed). Returns 0.
// On any failure (loader absent, no ICD, no window): returns non-zero and leaves
// *outLayer/*outState untouched — the caller falls back to a self-attached
// layer (headless; the nil-drawable guard keeps the game alive).
//   scale/width/height: the swapchain extent = (width*scale, height*scale),
//                       matching the layer's drawableSize the backend sets.
int mtlGlfwMetalHandshake(GLFWwindow* window, double scale,
                          int width, int height,
                          void** outLayer, void** outState);

// Tear down a mtlGlfwMetalHandshake state (swapchain/device/instance, then
// dlclose the loader). Safe to call with NULL (no-op). The CAMetalLayer itself
// is owned by the view and is NOT released here.
void mtlGlfwShutdown(void* state);

#ifdef __cplusplus
}
#endif

#endif
