// mtlholdprobe.m — keep ONE activated GLFW NO_API window up for ~25s, pump,
// and sample nextDrawable over time. Decisive test: over a long window does
// nextDrawable EVER become non-nil, and (via an external screenshot) is the
// window actually composited on screen?
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AppKit/AppKit.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

int main(int argc, char** argv) {
    int W=688,H=702;
    if (!glfwInit()) { fprintf(stderr,"glfwInit failed\n"); return 1; }
    NSApplication* app=[NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    glfwWindowHint(GLFW_RESIZABLE,GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE,GLFW_TRUE);
    glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
    GLFWwindow* win=glfwCreateWindow(W,H,"mtlholdprobe",nullptr,nullptr);
    if (!win) { fprintf(stderr,"createWindow failed\n"); return 1; }
    NSWindow* nswin=(NSWindow*)glfwGetCocoaWindow(win);
    NSView* view=(NSView*)glfwGetCocoaView(win);
    [app activateIgnoringOtherApps:YES];
    [nswin makeKeyAndOrderFront:nil];
    [nswin makeFirstResponder:view];
    id<MTLDevice> dev=MTLCreateSystemDefaultDevice();
    id<MTLCommandQueue> q=[dev newCommandQueue];
    double scale=[nswin backingScaleFactor];
    if (scale<=0) scale=1.0;
    CAMetalLayer* layer=[CAMetalLayer layer];
    layer.pixelFormat=MTLPixelFormatBGRA8Unorm;
    layer.contentsScale=scale;
    layer.displaySyncEnabled=NO;
    layer.drawableSize=CGSizeMake(W*scale,H*scale);
    view.layer=layer;
    view.wantsLayer=YES;

    time_t t0=time(NULL);
    int sec=0;
    while (time(NULL)-t0<25) {
        glfwPollEvents();
        [nswin makeKeyAndOrderFront:nil];
        if (sec%15==0) {  // ~15 polls ~= 1s at 10ms... actually poll is fast
        }
        if (time(NULL)-t0 != sec) {
            // sample nextDrawable ~once per second
            id<CAMetalDrawable> d=[layer nextDrawable];
            char extra[64]="";
            if (d)
                snprintf(extra,sizeof extra," tex=%zux%zu",
                         (size_t)d.texture.width,(size_t)d.texture.height);
            fprintf(stderr,"[%2ds] superlayer=%p drawableSize=(%.0f,%.0f) "
                     "nextDrawable=%s%s\n",
                     (int)(time(NULL)-t0),
                     (void*)layer.superlayer,
                     layer.drawableSize.width, layer.drawableSize.height,
                     d?"OK":"nil", extra);
            if (d) {
                id<MTLCommandBuffer> cb=[q commandBuffer];
                MTLRenderPassDescriptor* rd=[MTLRenderPassDescriptor renderPassDescriptor];
                rd.colorAttachments[0].texture=d.texture;
                rd.colorAttachments[0].loadAction=MTLLoadActionClear;
                rd.colorAttachments[0].storeAction=MTLStoreActionStore;
                rd.colorAttachments[0].clearColor=MTLClearColorMake(1,0,0,1);
                id<MTLRenderCommandEncoder> e=[cb renderCommandEncoderWithDescriptor:rd];
                [e endEncoding];
                [cb presentDrawable:d];
                [cb commit];
                [cb waitUntilCompleted];
            }
            sec=(int)(time(NULL)-t0);
        }
        usleep(10000);
    }
    fprintf(stderr,"hold done\n");
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}