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

// ---- Pipeline state ----

void* mtlLoadLibrary(void* device, const char* path) {
    @autoreleasepool {
        id<MTLDevice> d = (id<MTLDevice>)device;
        NSError* err = nil;
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
        id<MTLLibrary> lib = [d newLibraryWithURL:url error:&err];
        if (!lib) {
            fprintf(stderr, "mtlBridge: failed to load metallib %s: %s\n",
                    path, err ? [[err localizedDescription] UTF8String] : "unknown");
            return NULL;
        }
        // newLibraryWithURL returns +1 retained (caller owns).
        return (void*)lib;
    }
}

void* mtlCreatePipeline(void* device, void* library, int pipelineIndex) {
    @autoreleasepool {
        id<MTLDevice> d = (id<MTLDevice>)device;
        id<MTLLibrary> lib = (id<MTLLibrary>)library;

        // Vertex function is shared across all pipelines.
        id<MTLFunction> vertFn = [lib newFunctionWithName:@"aestroidsVert"];
        if (!vertFn)
            return NULL;

        // Select fragment function by pipeline index. (Topology is a
        // draw-time parameter in Metal — selected per-draw in task 6.)
        const char* fragName = "aestroidsFragSolid";
        switch (pipelineIndex) {
            case MTL_PIPELINE_LINE:     fragName = "aestroidsFragSolid"; break;
            case MTL_PIPELINE_TRI:      fragName = "aestroidsFragSolid"; break;
            case MTL_PIPELINE_OUTLINE:  fragName = "aestroidsFragSolid"; break;
            case MTL_PIPELINE_TEX:      fragName = "aestroidsFragTex";   break;
            case MTL_PIPELINE_MASKED:   fragName = "aestroidsFragMasked"; break;
            default:
                [vertFn release];
                return NULL;
        }
        id<MTLFunction> fragFn = [lib newFunctionWithName:[NSString stringWithUTF8String:fragName]];
        if (!fragFn) {
            [vertFn release];
            return NULL;
        }

        MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = vertFn;
        pd.fragmentFunction = fragFn;

        // Vertex descriptor matches the shader's AestroidsVertIn struct:
        //   attribute(0): float2 pos  offset 0
        //   attribute(1): float2 uv   offset 8
        //   attribute(2): float4 color offset 16
        //   stride 32 bytes (2+2+4 floats)
        MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
        vd.attributes[0].format = MTLVertexFormatFloat2;
        vd.attributes[0].offset = 0;
        vd.attributes[0].bufferIndex = 0;
        vd.attributes[1].format = MTLVertexFormatFloat2;
        vd.attributes[1].offset = 8;
        vd.attributes[1].bufferIndex = 0;
        vd.attributes[2].format = MTLVertexFormatFloat4;
        vd.attributes[2].offset = 16;
        vd.attributes[2].bufferIndex = 0;
        vd.layouts[0].stride = 32;
        vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
        pd.vertexDescriptor = vd;

        // Blend: mirror VK's color blend attachment exactly.
        //   blendEnable=YES, SRC_ALPHA/ONE_MINUS_SRC_ALPHA, ADD, writeMask=RGBA
        pd.colorAttachments[0].blendingEnabled = YES;
        pd.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        pd.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        pd.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        pd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
        pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pd.colorAttachments[0].writeMask = MTLColorWriteMaskAll;

        // Depth: no depth attachment (depthTestEnable=NO, depthWriteEnable=NO).
        // Cull: none (unculled, like GL). Sample count: 1 (no MSAA).
        pd.depthAttachmentPixelFormat = MTLPixelFormatInvalid;
        pd.stencilAttachmentPixelFormat = MTLPixelFormatInvalid;
        pd.rasterSampleCount = 1;

        // Color attachment pixel format: the CAMetalLayer's drawable format.
        // The layer is configured with BGRA8Unorm (see mtlCocoa.mm).
        pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

        NSError* err = nil;
        id<MTLRenderPipelineState> ps = [d newRenderPipelineStateWithDescriptor:pd
                                                                          error:&err];
        [vertFn release];
        [fragFn release];
        [pd release];
        if (!ps) {
            fprintf(stderr, "mtlBridge: newRenderPipelineState failed: %s\n",
                    err ? [[err localizedDescription] UTF8String] : "unknown");
            return NULL;
        }
        // newRenderPipelineState returns +1 retained (caller owns).
        return (void*)ps;
    }
}

void mtlSetPipeline(MTLFrameContext* ctx, void* pipeline) {
    if (!ctx || !pipeline)
        return;
    @autoreleasepool {
        id<MTLRenderPipelineState> ps = (id<MTLRenderPipelineState>)pipeline;
        [ctx->encoder setRenderPipelineState:ps];
    }
}

// ---- Cleanup ----

void mtlRelease(void* obj) {
    if (!obj)
        return;
    // Non-ARC: the caller owns the object and knows its type. For
    // app-lifetime objects (device, queue, layer) this is never called.
    // For library/pipeline objects (tasks 5+) this releases the +1 retain.
    id<NSObject> o = (id<NSObject>)obj;
    [o release];
}

} // extern "C"
