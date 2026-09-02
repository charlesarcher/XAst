// utilities/rendering/mtlBridge.mm — ObjC++ Metal rendering bridge (C ABI)
//
// Extends mtlCocoa.H with command queue, frame lifecycle, and render-pass
// operations. This is the SECOND Objective-C++ file in the repo (after
// mtlCocoa.mm). All Objective-C types are confined to this TU.
//
// ARC is OFF for this TU (consistent with mtlCocoa.mm).
// __bridge are ARC-only — plain C casts are used instead.

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "mtlBridge.H"

// ---- Internal frame context (holds ObjC types) ----

struct MTLFrameContext {
    id<CAMetalDrawable>       drawable;
    id<MTLCommandBuffer>      commandBuffer;
    id<MTLRenderCommandEncoder> encoder;
};

extern "C" {

// ---- Queue ----

void* mtlCreateCommandQueue(void* device) {
    @autoreleasepool {
        id<MTLDevice> d = (id<MTLDevice>)device;
        id<MTLCommandQueue> q = [d newCommandQueue];
        // newCommandQueue returns +1 retained (app lifetime, never released).
        return (void*)q;
    }
}

// ---- Layer configuration ----

void mtlSetDisplaySyncEnabled(void* layer, int enabled) {
    @autoreleasepool {
        CAMetalLayer* l = (CAMetalLayer*)layer;
        l.displaySyncEnabled = enabled ? YES : NO;
    }
}

// ---- Frame lifecycle ----

MTLFrameContext* mtlBeginFrame(void* layer, void* queue) {
    @autoreleasepool {
        CAMetalLayer* l = (CAMetalLayer*)layer;
        id<CAMetalDrawable> d = [l nextDrawable];
        if (!d)
            return NULL;

        // Retain the drawable so it survives the pool drain.
        [d retain];

        id<MTLCommandQueue> q = (id<MTLCommandQueue>)queue;
        id<MTLCommandBuffer> buf = [q commandBuffer];
        [buf retain];

        MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
        desc.colorAttachments[0].texture = d.texture;
        desc.colorAttachments[0].loadAction = MTLLoadActionClear;
        desc.colorAttachments[0].storeAction = MTLStoreActionStore;
        desc.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

        id<MTLRenderCommandEncoder> enc = [buf renderCommandEncoderWithDescriptor:desc];
        [enc retain];

        MTLFrameContext* ctx = new MTLFrameContext();
        ctx->drawable = d;
        ctx->commandBuffer = buf;
        ctx->encoder = enc;
        return ctx;
    }
}

void mtlEndFrame(MTLFrameContext* ctx) {
    if (!ctx)
        return;
    @autoreleasepool {
        [ctx->encoder endEncoding];
        [ctx->commandBuffer presentDrawable:ctx->drawable];
        [ctx->commandBuffer commit];
        // Release frame-scoped retained objects.
        [ctx->encoder release];
        [ctx->commandBuffer release];
        [ctx->drawable release];
    }
    delete ctx;
}

// ---- Cleanup ----

void mtlRelease(void* obj) {
    if (!obj)
        return;
    // In non-ARC, the caller is responsible for knowing the type.
    // For app-lifetime objects (device, queue, layer) this is never called.
    // For frame-scoped objects, mtlEndFrame handles release.
    // This exists for future use by tasks 5/6/7.
    (void)obj;
}

} // extern "C"
