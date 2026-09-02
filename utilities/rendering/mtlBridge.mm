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
#include <string.h>

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

        // Viewport = full drawable (window target). Scissor is set per-frame
        // by the backend (mtlSetScissor) from its stored logical rect.
        MTLViewport vp;
        vp.originX = 0.0;
        vp.originY = 0.0;
        vp.width = (double)d.texture.width;
        vp.height = (double)d.texture.height;
        vp.znear = 0.0;
        vp.zfar = 1.0;
        [enc setViewport:vp];

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

// ---- Vertex buffers ----

void* mtlCreateVertexBuffer(void* device, size_t size) {
    @autoreleasepool {
        id<MTLDevice> d = (id<MTLDevice>)device;
        id<MTLBuffer> buf = [d newBufferWithLength:size
                                           options:MTLResourceStorageModeShared];
        // newBufferWithLength returns +1 retained (caller owns).
        return (void*)buf;
    }
}

void mtlUpdateVertexBuffer(void* buffer, size_t offset, const void* data,
                           size_t size) {
    if (!buffer || !data)
        return;
    @autoreleasepool {
        id<MTLBuffer> buf = (id<MTLBuffer>)buffer;
        // Shared-storage: CPU writes directly into the GPU-visible memory.
        void* dst = (uint8_t*)[buf contents] + offset;
        memcpy(dst, data, size);
    }
}

void mtlSetVertexBuffer(MTLFrameContext* ctx, void* buffer, size_t offset) {
    if (!ctx || !buffer)
        return;
    @autoreleasepool {
        id<MTLBuffer> buf = (id<MTLBuffer>)buffer;
        [ctx->encoder setVertexBuffer:buf offset:offset atIndex:0];
    }
}

// ---- MVP uniform ----

void mtlSetMvp(MTLFrameContext* ctx, const float* mvp) {
    if (!ctx || !mvp)
        return;
    @autoreleasepool {
        [ctx->encoder setVertexBytes:mvp length:sizeof(float)*16 atIndex:1];
    }
}

// ---- Textures ----

void* mtlCreateTexture(void* device, int w, int h, int format,
                       const void* data, size_t bytesPerRow) {
    @autoreleasepool {
        id<MTLDevice> d = (id<MTLDevice>)device;
        MTLPixelFormat pf = (format == MTL_TEX_R8)
                                ? MTLPixelFormatR8Unorm
                                : MTLPixelFormatRGBA8Unorm;
        MTLTextureDescriptor* desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pf
                                                               width:(NSUInteger)w
                                                              height:(NSUInteger)h
                                                           mipmapped:NO];
        desc.storageMode = MTLStorageModeShared;
        desc.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> tex = [d newTextureWithDescriptor:desc];
        if (!tex)
            return NULL;
        [tex replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)w, (NSUInteger)h)
               mipmapLevel:0
                 withBytes:data
               bytesPerRow:bytesPerRow];
        // newTextureWithDescriptor returns +1 retained (caller owns).
        return (void*)tex;
    }
}

void mtlSetTexture(MTLFrameContext* ctx, int slot, void* texture) {
    if (!ctx || !texture)
        return;
    @autoreleasepool {
        id<MTLTexture> tex = (id<MTLTexture>)texture;
        [ctx->encoder setFragmentTexture:tex atIndex:(NSUInteger)slot];
    }
}

// ---- Draw ----

void mtlDraw(MTLFrameContext* ctx, int primitiveType, int vertexCount) {
    if (!ctx || vertexCount < 1)
        return;
    @autoreleasepool {
        MTLPrimitiveType pt = (primitiveType == MTL_PRIMITIVE_LINE)
                                  ? MTLPrimitiveTypeLine
                                  : MTLPrimitiveTypeTriangle;
        [ctx->encoder drawPrimitives:pt
                         vertexStart:0
                         vertexCount:(NSUInteger)vertexCount];
    }
}

// ---- Scissor / viewport ----

void mtlSetScissor(MTLFrameContext* ctx, int x, int y, int w, int h) {
    if (!ctx)
        return;
    @autoreleasepool {
        MTLScissorRect r;
        r.x = (NSUInteger)(x < 0 ? 0 : x);
        r.y = (NSUInteger)(y < 0 ? 0 : y);
        r.width = (NSUInteger)(w < 0 ? 0 : w);
        r.height = (NSUInteger)(h < 0 ? 0 : h);
        [ctx->encoder setScissorRect:r];
    }
}

void mtlSetViewport(MTLFrameContext* ctx, int w, int h) {
    if (!ctx)
        return;
    @autoreleasepool {
        MTLViewport vp;
        vp.originX = 0.0;
        vp.originY = 0.0;
        vp.width = (double)w;
        vp.height = (double)h;
        vp.znear = 0.0;
        vp.zfar = 1.0;
        [ctx->encoder setViewport:vp];
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
