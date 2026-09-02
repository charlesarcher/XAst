// utilities/rendering/mtlBridge.H — thin C-ABI bridge for Metal rendering operations
//
// Extends mtlCocoa.H with command queue, frame lifecycle, and render-pass
// operations. Pure C header — no ObjC types, no Metal/QuartzCore includes.
// All Objective-C pointers are opaque void* on this side of the ABI.
#ifndef _MTL_BRIDGE_H
#define _MTL_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

// ---- Queue ----
// Create an MTLCommandQueue from an MTLDevice. Returns opaque pointer
// (actually id<MTLCommandQueue>). Caller takes ownership (app lifetime).
void* mtlCreateCommandQueue(void* device);

// ---- Layer configuration ----
// Set displaySyncEnabled (VSync) on a CAMetalLayer.
void mtlSetDisplaySyncEnabled(void* layer, int enabled);

// ---- Frame context (opaque) ----
// Holds the drawable, command buffer, and render encoder for one frame.
// Created by mtlBeginFrame, consumed and freed by mtlEndFrame.
typedef struct MTLFrameContext MTLFrameContext;

// Acquire the next drawable from the layer and open a render pass.
// Returns NULL if no drawable is available (caller skips the frame).
// The render pass clears to opaque black (0,0,0,1).
MTLFrameContext* mtlBeginFrame(void* layer, void* queue);

// Close the render pass, present the drawable, commit the command buffer,
// and release all frame-scoped resources. Safe to call with NULL (no-op).
void mtlEndFrame(MTLFrameContext* ctx);

// ---- Pipeline state ----
// Pipeline indices (mirror VK's 5 pipelines; see mtlBackend.H).
enum {
    MTL_PIPELINE_LINE = 0,      // LINE_LIST topology, solid fragment (thin lines)
    MTL_PIPELINE_TRI,           // TRIANGLE_LIST, solid fragment (thick lines/filled)
    MTL_PIPELINE_OUTLINE,       // LINE_LIST, solid fragment (polygon/rect outlines)
    MTL_PIPELINE_TEX,           // TRIANGLE_LIST, tex fragment (textured quads)
    MTL_PIPELINE_MASKED         // TRIANGLE_LIST, masked fragment (content + R8 mask)
};

// Load the pre-compiled .metallib from disk. Returns opaque MTLLibrary*
// (caller owns; release with mtlRelease). Returns NULL on failure.
void* mtlLoadLibrary(void* device, const char* path);

// Create one of the 5 render pipelines from the library. Returns opaque
// id<MTLRenderPipelineState> (caller owns; release with mtlRelease).
// Returns NULL on failure.
void* mtlCreatePipeline(void* device, void* library, int pipelineIndex);

// Set the render pipeline on the current frame's encoder. No-op if ctx or
// pipeline is NULL.
void mtlSetPipeline(MTLFrameContext* ctx, void* pipeline);

// ---- Cleanup ----
// Release an opaque Metal object (non-ARC release). Safe to call with NULL.
void mtlRelease(void* obj);

#ifdef __cplusplus
}
#endif

#endif
