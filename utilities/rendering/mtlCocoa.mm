// utilities/rendering/mtlCocoa.mm — ObjC++ CAMetalLayer bridge (C ABI)
//
// This is the ONLY Objective-C++ file in the entire repo. It bridges GLFW's
// Cocoa NSView to Metal's CAMetalLayer through a pure-C interface (mtlCocoa.H).
// Extreme isolation: no ObjC types leak into C++ headers.
//
// Note: ARC is OFF for this TU (Xcode default for .mm in non-ARC projects).
// __bridge/__bridge_retained are ARC-only — plain C casts are used instead.
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "mtlCocoa.H"

extern "C" {

void* mtlCreateLayer(void) {
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.contentsScale = [[NSScreen mainScreen] backingScaleFactor];
    // [layer] returns autoreleased; caller attaches to view immediately,
    // so the layer is retained by the view before the pool drains.
    return (void*)layer;
}

int mtlAttachToView(void* layer, GLFWwindow* window) {
    @autoreleasepool {
        NSView* view = (NSView*)glfwGetCocoaView(window);
        CAMetalLayer* metalLayer = (CAMetalLayer*)layer;
        // CRITICAL ORDER: layer must be set before wantsLayer, otherwise
        // AppKit creates a plain CALayer and ignores our Metal layer.
        view.layer = metalLayer;
        view.wantsLayer = YES;
    }
    return 0;
}

double mtlBackingScaleFactor(GLFWwindow* window) {
    @autoreleasepool {
        NSWindow* nswindow = (NSWindow*)glfwGetCocoaWindow(window);
        return (double)[nswindow backingScaleFactor];
    }
}

void* mtlCreateDevice(void) {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    // MTLCreateSystemDefaultDevice() returns a +1 retained object.
    // Caller takes ownership — intentionally never released (app lifetime).
    return (void*)device;
}

void mtlSetDrawableSize(void* layer, double scale, int fbW, int fbH) {
    @autoreleasepool {
        CAMetalLayer* metalLayer = (CAMetalLayer*)layer;
        metalLayer.drawableSize = CGSizeMake(fbW * scale, fbH * scale);
    }
}

} // extern "C"
