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

#ifdef __cplusplus
}
#endif

#endif
