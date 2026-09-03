// mtlfinalprobe.m — final localization. Part 1: GLFW 3.5.1 NO_API window
// forced onto the PRIMARY display + full activation + full NSApp run-loop
// pumping. Part 2 (control): a PLAIN AppKit NSWindow (no GLFW at all) with a
// CAMetalLayer, run through [NSApp run]. If Part 2 works but Part 1 fails ->
// a GLFW 3.5.1 issue. If Part 2 also fails -> the command-line process itself
// cannot obtain CAMetalLayer drawables on this OS (not fixable in app code).
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AppKit/AppKit.h>
#import <CoreFoundation/CoreFoundation.h>
#ifdef PART1_GLFW
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#endif

static int sampleLoop(NSWindow* win, CAMetalLayer* layer, id<MTLCommandQueue> q,
                      int seconds, const char* tag) {
    time_t t0=time(NULL); int sec=0; int okCount=0;
    while (time(NULL)-t0<seconds) {
#ifdef PART1_GLFW
        glfwPollEvents();
#endif
        [[NSRunLoop currentRunLoop] runUntilDate:
            [NSDate dateWithTimeIntervalSinceNow:0.02]];
        if (time(NULL)-t0 != sec) {
            id<CAMetalDrawable> d=[layer nextDrawable];
            if (d) {
                okCount++;
                id<MTLCommandBuffer> cb=[q commandBuffer];
                MTLRenderPassDescriptor* rd=[MTLRenderPassDescriptor renderPassDescriptor];
                rd.colorAttachments[0].texture=d.texture;
                rd.colorAttachments[0].loadAction=MTLLoadActionClear;
                rd.colorAttachments[0].storeAction=MTLStoreActionStore;
                rd.colorAttachments[0].clearColor=MTLClearColorMake(0,1,0,1);
                id<MTLRenderCommandEncoder> e=[cb renderCommandEncoderWithDescriptor:rd];
                [e endEncoding];
                [cb presentDrawable:d]; [cb commit]; [cb waitUntilCompleted];
            }
            fprintf(stderr,"[%s][%2ds] superlayer=%p ds=(%.0f,%.0f) nextDrawable=%s\n",
                    tag,(int)(time(NULL)-t0),(void*)layer.superlayer,
                    layer.drawableSize.width,layer.drawableSize.height,d?"OK":"nil");
            sec=(int)(time(NULL)-t0);
        }
        usleep(20000);
    }
    fprintf(stderr,"[%s] DONE nonNilDrawables=%d over %ds\n",tag,okCount,seconds);
    return okCount;
}

int main(int argc,char** argv) {
    int W=688,H=702;
    NSApplication* app=[NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    id<MTLDevice> dev=MTLCreateSystemDefaultDevice();
    id<MTLCommandQueue> q=[dev newCommandQueue];
    if (!dev) { fprintf(stderr,"no MTLDevice\n"); return 1; }
    fprintf(stderr,"MTLDevice=%s\n",[(NSString*)[dev name] UTF8String]);

#ifdef PART1_GLFW
    if (!glfwInit()) { fprintf(stderr,"glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_RESIZABLE,GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE,GLFW_TRUE);
    glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
    GLFWwindow* win=glfwCreateWindow(W,H,"mtlfinal1",nullptr,nullptr);
    GLFWmonitor* mon=glfwGetPrimaryMonitor();
    if (mon) { const GLFWvidmode* m=glfwGetVideoMode(mon);
        if (m) glfwSetWindowPos(win,(m->width-W)/2,(m->height-H)/2); }
    [app activateIgnoringOtherApps:YES];
    NSWindow* nswin=(NSWindow*)glfwGetCocoaWindow(win);
    NSView* view=(NSView*)glfwGetCocoaView(win);
    [nswin makeKeyAndOrderFront:nil];
    double scale=[nswin backingScaleFactor]; if (scale<=0) scale=1.0;
    CAMetalLayer* layer=[CAMetalLayer layer];
    layer.pixelFormat=MTLPixelFormatBGRA8Unorm; layer.contentsScale=scale;
    layer.displaySyncEnabled=NO;
    layer.drawableSize=CGSizeMake(W*scale,H*scale);
    view.layer=layer; view.wantsLayer=YES;
    sampleLoop(nswin,layer,q,12,"GLFW-primary");
    glfwDestroyWindow(win); glfwTerminate();
#else
    NSRect r=NSMakeRect(100,100,W,H);
    NSWindow* win=[[NSWindow alloc] initWithContentRect:r
        styleMask:(NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable)
        backing:NSBackingStoreBuffered defer:NO];
    [win center];
    NSView* cv=[[NSView alloc] initWithFrame:r];
    [win setContentView:cv];
    [app activateIgnoringOtherApps:YES];
    [win makeKeyAndOrderFront:nil];
    double scale=[win backingScaleFactor]; if (scale<=0) scale=1.0;
    CAMetalLayer* layer=[CAMetalLayer layer];
    layer.pixelFormat=MTLPixelFormatBGRA8Unorm; layer.contentsScale=scale;
    layer.displaySyncEnabled=NO;
    layer.drawableSize=CGSizeMake(W*scale,H*scale);
    cv.layer=layer; cv.wantsLayer=YES;
    sampleLoop(win,layer,q,12,"PLAIN-AppKit");
    [win close];
#endif
    fprintf(stderr,"final probe done\n");
    return 0;
}