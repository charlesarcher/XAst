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

// ---- Vertex buffers (task 6) ----
// Create a shared-storage MTLBuffer of the given size (bytes). Returns opaque
// id<MTLBuffer> (caller owns; release with mtlRelease). Returns NULL on failure.
void* mtlCreateVertexBuffer(void* device, size_t size);

// Copy CPU data into a vertex buffer at the given byte offset (replaceBytes).
// No-op if buffer or data is NULL.
void mtlUpdateVertexBuffer(void* buffer, size_t offset, const void* data,
                           size_t size);

// Bind a vertex buffer at the given byte offset on the encoder (slot 0).
void mtlSetVertexBuffer(MTLFrameContext* ctx, void* buffer, size_t offset);

// ---- MVP uniform (task 6) ----
// Set the MVP matrix (16 floats, column-major) as vertex bytes at buffer
// slot 1 (matches the shader's constant float4x4& uMVP [[buffer(1)]]).
void mtlSetMvp(MTLFrameContext* ctx, const float* mvp);

// ---- Textures (task 6) ----
// Texture pixel formats (mirror VK's R8 / RGBA8).
enum {
    MTL_TEX_R8 = 0,     // single-channel (mask)
    MTL_TEX_RGBA8       // 4-channel
};

// Create an MTLTexture from CPU data and upload it. Returns opaque
// id<MTLTexture> (caller owns; release with mtlRelease). Returns NULL on
// failure. Storage is shared (CPU-writable, GPU-samplable).
void* mtlCreateTexture(void* device, int w, int h, int format,
                       const void* data, size_t bytesPerRow);

// Bind a texture to a fragment texture slot on the encoder. No-op if ctx or
// texture is NULL.
void mtlSetTexture(MTLFrameContext* ctx, int slot, void* texture);

// ---- Draw (task 6) ----
// Primitive types (Metal draw-time topology).
enum {
    MTL_PRIMITIVE_LINE = 0,     // MTLPrimitiveTypeLine
    MTL_PRIMITIVE_TRIANGLE      // MTLPrimitiveTypeTriangle
};

// Issue a drawPrimitives call on the encoder.
void mtlDraw(MTLFrameContext* ctx, int primitiveType, int vertexCount);

// ---- Scissor / viewport (task 6) ----
// Set the scissor rect (window/pixel coords) on the encoder.
void mtlSetScissor(MTLFrameContext* ctx, int x, int y, int w, int h);

// Set the viewport (window/pixel coords) on the encoder.
void mtlSetViewport(MTLFrameContext* ctx, int w, int h);

// ---- Cleanup ----
// Release an opaque Metal object (non-ARC release). Safe to call with NULL.
void mtlRelease(void* obj);

#ifdef __cplusplus
}
#endif

#endif
