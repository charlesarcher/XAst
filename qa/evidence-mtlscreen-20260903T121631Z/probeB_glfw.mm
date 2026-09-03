// probeB_glfw.mm — LANE 1, PROBE B (round 3, confound-free).
// GLFW 3.5.1 NO_API window + the EXACT mtlCocoa.mm attach sequence, then the
// same 0.5s-timer / 30s / present / exit-code contract as Probe A.
//
// Mirrors mtlCocoa.mm line for line:
//   mtlCreateLayer:        [CAMetalLayer layer]; BGRA8Unorm;
//                          contentsScale = [NSScreen mainScreen] backingScaleFactor
//   mtlAttachToView:       view.layer = layer; THEN view.wantsLayer = YES
//   mtlBackingScaleFactor: [nswindow backingScaleFactor]  (window's own)
//   mtlSetDrawableSize:    drawableSize = (fb * scale)
//   mtlSetDisplaySyncEnabled: displaySyncEnabled = YES
// and mtlBackend.H initWindow: window hints (RESIZABLE/VISIBLE/NO_API),
// root-center on primary monitor, framebuffer-size callback that re-sets
// drawableSize (unconditionally, like the product trampoline).
// isOnScreen is read every tick via respondsToSelector (GLFW 3.5.1's
// GLFWWindow does NOT implement isOnScreen — logged as "n/a" when absent,
// which is itself recorded evidence).
// Exit code: 0 = non-nil drawable within 30s; 1 = never.
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AppKit/AppKit.h>
#import <CoreGraphics/CGWindow.h>

static GLFWwindow* g_win = nullptr;
static CAMetalLayer* g_layer = nil;
static id<MTLCommandQueue> g_queue = nil;
static const char* g_owner = "probeB";
static int g_sample = 0;
static int g_firstNonNil = -1;
static int g_nonNilCount = 0;
static double g_t0 = 0.0;
static double g_lastSampleAt = -1.0;
static int g_everWsOnScreen = 0;

static double nowSec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

// Window-server ground truth (AppKit isOnScreen is unavailable on this build):
// is our window in the on-screen / all CGWindowList?
static void windowServerState(const char** onScreen, const char** registered,
                              unsigned long* onScreenTotal) {
    *onScreen = "absent";
    *registered = "absent";
    *onScreenTotal = 0;
    CFArrayRef onScreenList = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly, kCGNullWindowID);
    CFArrayRef allList = CGWindowListCopyWindowInfo(
        kCGWindowListOptionAll, kCGNullWindowID);
    *onScreenTotal = onScreenList ? (unsigned long)CFArrayGetCount(onScreenList) : 0;
    const char* owner = g_owner;
    if (onScreenList) {
        for (CFIndex i = 0; i < CFArrayGetCount(onScreenList); ++i) {
            CFDictionaryRef e =
                (CFDictionaryRef)CFArrayGetValueAtIndex(onScreenList, i);
            CFTypeRef ov = CFDictionaryGetValue(e, kCGWindowOwnerName);
            if (ov && CFGetTypeID(ov) == CFStringGetTypeID()) {
                char buf[128];
                if (CFStringGetCString((CFStringRef)ov, buf, sizeof buf,
                                       kCFStringEncodingUTF8) &&
                    strcmp(buf, owner) == 0) {
                    *onScreen = "yes";
                    break;
                }
            }
        }
    }
    if (allList) {
        for (CFIndex i = 0; i < CFArrayGetCount(allList); ++i) {
            CFDictionaryRef e =
                (CFDictionaryRef)CFArrayGetValueAtIndex(allList, i);
            CFTypeRef ov = CFDictionaryGetValue(e, kCGWindowOwnerName);
            if (ov && CFGetTypeID(ov) == CFStringGetTypeID()) {
                char buf[128];
                if (CFStringGetCString((CFStringRef)ov, buf, sizeof buf,
                                       kCFStringEncodingUTF8) &&
                    strcmp(buf, owner) == 0) {
                    *registered = "yes";
                    break;
                }
            }
        }
    }
    if (onScreenList) CFRelease(onScreenList);
    if (allList) CFRelease(allList);
}

static void fbSizeCB(GLFWwindow*, int w, int h) {
    // mtlBackend.H framebufferSizeTrampoline_ mirror: re-derive drawableSize
    // from the new fb size (unconditional, like the product code).
    if (!g_layer || !g_win)
        return;
    @autoreleasepool {
        NSWindow* nw = (NSWindow*)glfwGetCocoaWindow(g_win);
        double scale = [nw backingScaleFactor];
        if (scale <= 0.0) scale = 1.0;
        g_layer.drawableSize = CGSizeMake((double)w * scale, (double)h * scale);
        fprintf(stderr, "  [cb] framebufferSize %dx%d -> drawableSize (%.0f,%.0f)\n",
                w, h, g_layer.drawableSize.width, g_layer.drawableSize.height);
    }
}

static void sampleTick(const char* why) {
    @autoreleasepool {
        g_sample++;
        double t = nowSec() - g_t0;
        if (t < 0) t = 0;
        // --- per-tick state, actually read and printed ---
        const char* onScreen = "n/a";
        const char* isKey = "n/a";
        NSWindow* nw = (NSWindow*)glfwGetCocoaWindow(g_win);
        if (nw && [nw respondsToSelector:@selector(isOnScreen)])
            onScreen = [nw isOnScreen] ? "yes" : "no";
        if (nw && [nw respondsToSelector:@selector(isKeyWindow)])
            isKey = [nw isKeyWindow] ? "yes" : "no";
        const char* isMain = [NSThread isMainThread] ? "yes" : "no";
        const char* wsOn = "absent", *wsReg = "absent";
        unsigned long wsTotal = 0;
        windowServerState(&wsOn, &wsReg, &wsTotal);
        if (strcmp(wsOn, "yes") == 0)
            g_everWsOnScreen = 1;
        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(g_win, &fbw, &fbh);
        fprintf(stderr, "tick %2d t=%4.1fs sample=%d [%s] isOnScreen=%s isKey=%s "
                "wsOnScreen=%s wsRegistered=%s isMainThread=%s fb=%dx%d "
                "bounds=(%.0f,%.0f) drawableSize=(%.0f,%.0f) superlayer=%s "
                "contentsScale=%.2f winScale=%.2f\n",
                g_sample, t, g_sample, why,
                onScreen, isKey, wsOn, wsReg, isMain, fbw, fbh,
                g_layer.bounds.size.width, g_layer.bounds.size.height,
                g_layer.drawableSize.width, g_layer.drawableSize.height,
                g_layer.superlayer ? "set" : "nil",
                (double)g_layer.contentsScale,
                (double)[nw backingScaleFactor]);
        // --- the decisive call (mtlBeginFrame's first line) ---
        id<CAMetalDrawable> d = [g_layer nextDrawable];
        if (d) {
            if (g_firstNonNil < 0)
                g_firstNonNil = g_sample;
            g_nonNilCount++;
            fprintf(stderr, "  nextDrawable=OK tex=%zux%zu\n",
                    (size_t)d.texture.width, (size_t)d.texture.height);
            if (g_firstNonNil == g_sample)
                fprintf(stderr, "  *** FIRST NON-NIL DRAWABLE at t=%.1fs sample=%d ***\n",
                        t, g_sample);
            if (g_queue) {
                id<MTLCommandBuffer> cb = [g_queue commandBuffer];
                MTLRenderPassDescriptor* rd =
                    [MTLRenderPassDescriptor renderPassDescriptor];
                rd.colorAttachments[0].texture = d.texture;
                rd.colorAttachments[0].loadAction = MTLLoadActionClear;
                rd.colorAttachments[0].storeAction = MTLStoreActionStore;
                rd.colorAttachments[0].clearColor =
                    MTLClearColorMake(0.0, 1.0, 0.0, 1.0);
                id<MTLRenderCommandEncoder> e =
                    [cb renderCommandEncoderWithDescriptor:rd];
                [e endEncoding];
                [cb presentDrawable:d];
                [cb commit];
                [cb waitUntilCompleted];
                if (cb.error)
                    fprintf(stderr, "  present/commit ERROR: %s\n",
                            [[cb.error userInfo][NSLocalizedDescriptionKey]
                                     UTF8String]);
                else
                    fprintf(stderr, "  present+commit OK\n");
            }
        } else {
            fprintf(stderr, "  nextDrawable=NIL\n");
        }
        g_lastSampleAt = t;
    }
}

int main(int argc, char** argv) {
    const int W = 640, H = 512;
    if (!glfwInit()) { fprintf(stderr, "probeB: glfwInit failed\n"); return 1; }
    fprintf(stderr, "probeB boot: glfw=%s\n",
            glfwGetVersionString());
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);   // required: Metal, no GL
    g_win = glfwCreateWindow(W, H, "probeB", nullptr, nullptr);
    if (!g_win) {
        fprintf(stderr, "probeB: glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }
    // root-center on the primary monitor (mtlBackend.H initWindow mirror)
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    if (mon) {
        const GLFWvidmode* m = glfwGetVideoMode(mon);
        if (m)
            glfwSetWindowPos(g_win, (m->width - W) / 2, (m->height - H) / 2);
    }
    // --- mtlCocoa.mm sequence, line for line ---
    g_layer = [CAMetalLayer layer];                              // mtlCreateLayer
    g_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    g_layer.contentsScale = [[NSScreen mainScreen] backingScaleFactor];
    { NSView* view = (NSView*)glfwGetCocoaView(g_win);
      view.layer = g_layer;          // mtlAttachToView: layer FIRST
      view.wantsLayer = YES;         // then wantsLayer
    }
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();          // mtlCreateDevice
    if (!dev) { fprintf(stderr, "probeB: no MTLDevice\n"); return 1; }
    g_queue = [dev newCommandQueue];                             // mtlCreateCommandQueue
    g_layer.displaySyncEnabled = YES;                             // mtlSetDisplaySyncEnabled(l,1)
    NSWindow* nw = (NSWindow*)glfwGetCocoaWindow(g_win);
    double scale = [nw backingScaleFactor];                      // mtlBackingScaleFactor
    if (scale <= 0.0) scale = 1.0;
    glfwSetFramebufferSizeCallback(g_win, fbSizeCB);             // trampoline mirror
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(g_win, &fbw, &fbh);
    g_layer.drawableSize = CGSizeMake((double)fbw * scale, (double)fbh * scale);
    fprintf(stderr, "probeB setup: device=%s winScale=%.2f mainScreenScale=%.2f "
            "fb=%dx%d ds=(%.0f,%.0f) winClass=%s\n",
            [(NSString*)[dev name] UTF8String], scale,
            (double)[[NSScreen mainScreen] backingScaleFactor],
            fbw, fbh, g_layer.drawableSize.width, g_layer.drawableSize.height,
            [[nw class] className].UTF8String);
    // --- 0.5s repeating run-loop timer, up to 30s; pump GLFW each step ---
    [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer* t) {
        (void)t;
        glfwPollEvents();           // game beginFrame owns this pump
        sampleTick("timer");
    }];
    NSRunLoop* rl = [NSRunLoop mainRunLoop];
    g_t0 = nowSec();
    double tEnd = g_t0 + 30.0;
    while (nowSec() < tEnd) {
        glfwPollEvents();
        [rl runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.5]];
        if (nowSec() - g_lastSampleAt > 0.75) {
            glfwPollEvents();
            sampleTick("pump-fallback");
        }
    }
    double tOn = nowSec() - g_t0;
    fprintf(stderr, "probeB done: t=%.1fs samples=%d firstNonNilTick=%d "
            "nonNilDrawables=%d everWsOnScreen=%d\n",
            tOn, g_sample, g_firstNonNil, g_nonNilCount, g_everWsOnScreen);
    glfwDestroyWindow(g_win);
    glfwTerminate();
    return g_firstNonNil >= 0 ? 0 : 1;
}