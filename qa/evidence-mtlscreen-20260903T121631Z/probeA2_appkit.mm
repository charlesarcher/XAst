// probeA2_appkit.mm — LANE 1, PROBE A2 (round 3, refines Probe A).
// Same contract as Probe A (pure AppKit CLI, 640x512 titled NSWindow on the
// main screen, CAMetalLayer on the contentView, 0.5s timer, 30s bound,
// exit 0 iff a non-nil drawable was obtained) PLUS two fixes:
//   * wall clock (clock_gettime CLOCK_REALTIME) for t= and the 30s bound —
//     Probe A used CPU clock() which barely advances while sleeping;
//   * on-screen ground truth from the WINDOW SERVER itself:
//       - CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, ...)
//         -> is our window (matched by owner name) in the on-screen list?
//       - CGWindowListCopyWindowInfo(kCFArrayAll, ...)
//         -> registered at all? (in All but not OnScreen = registered but
//            not mapped/displayed)
//     plus a guarded NSWindow.occlusionState read as a secondary signal.
//     AppKit isOnScreen is unavailable on this build (removed), hence CG.
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AppKit/AppKit.h>
#import <CoreGraphics/CGWindow.h>

static NSWindow* g_win = nil;
static CAMetalLayer* g_layer = nil;
static id<MTLCommandQueue> g_queue = nil;
static const char* g_owner = "probeA2";
static int g_sample = 0;
static int g_firstNonNil = -1;
static int g_nonNilCount = 0;
static double g_t0 = 0.0;
static double g_lastSampleAt = -1.0;
static int g_everOnScreen = 0;

static double nowSec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

// Window-server ground truth: is our window in the on-screen / all list?
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

static void sampleTick(const char* why) {
    @autoreleasepool {
        g_sample++;
        double t = nowSec() - g_t0;
        const char* occl = "n/a";
        if (g_win && [g_win respondsToSelector:@selector(occlusionState)]) {
            unsigned st = (unsigned)[g_win occlusionState];
            occl = st == 0 ? "visible(0)" : st == 1 ? "occluded(1)" : "other";
        }
        const char* wsOn = "absent", *wsReg = "absent";
        unsigned long wsTotal = 0;
        windowServerState(&wsOn, &wsReg, &wsTotal);
        if (strcmp(wsOn, "yes") == 0)
            g_everOnScreen = 1;
        const char* isMain = [NSThread isMainThread] ? "yes" : "no";
        fprintf(stderr, "tick %2d t=%4.1fs [%s] wsOnScreen=%s wsRegistered=%s "
                "wsOnScreenTotal=%lu occlusion=%s isMainThread=%s "
                "bounds=(%.0f,%.0f) ds=(%.0f,%.0f) superlayer=%s cs=%.2f\n",
                g_sample, t, why, wsOn, wsReg, wsTotal, occl, isMain,
                g_layer.bounds.size.width, g_layer.bounds.size.height,
                g_layer.drawableSize.width, g_layer.drawableSize.height,
                g_layer.superlayer ? "set" : "nil",
                (double)g_layer.contentsScale);
        id<CAMetalDrawable> d = [g_layer nextDrawable];
        if (d) {
            if (g_firstNonNil < 0)
                g_firstNonNil = g_sample;
            g_nonNilCount++;
            fprintf(stderr, "  nextDrawable=OK tex=%zux%zu\n",
                    (size_t)d.texture.width, (size_t)d.texture.height);
            if (g_firstNonNil == g_sample)
                fprintf(stderr,
                        "  *** FIRST NON-NIL DRAWABLE at t=%.1fs sample=%d ***\n",
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
    NSApplication* app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    [app finishLaunching];
    { NSRunLoop* rl = [NSRunLoop mainRunLoop];
      for (int i = 0; i < 4; ++i)
        [rl runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.5]]; }
    const char* fin = "n/a";
    if ([app respondsToSelector:@selector(isFinishedLaunching)])
        fin = [(id)app isFinishedLaunching] ? "yes" : "no";
    fprintf(stderr, "probeA2 boot: isRunning=%s isFinishedLaunching=%s\n",
            [app isRunning] ? "yes" : "no", fin);
    NSScreen* scr = [NSScreen mainScreen];
    NSRect sf = scr ? [scr frame] : NSMakeRect(0, 0, 1920, 1080);
    NSRect origin = NSMakeRect(sf.origin.x + (sf.size.width - W) / 2.0,
                               sf.origin.y + (sf.size.height - H) / 2.0,
                               (double)W, (double)H);
    g_win = [[NSWindow alloc] initWithContentRect:origin
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                   NSWindowStyleMaskResizable)
        backing:NSBackingStoreBuffered defer:NO];
    [g_win setTitle:@"probeA2"];
    [g_win makeKeyAndOrderFront:nil];
    [app activateIgnoringOtherApps:YES];
    NSView* cv = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, W, H)];
    [g_win setContentView:cv];
    double scale = [g_win backingScaleFactor];
    if (scale <= 0.0) scale = 1.0;
    g_layer = [CAMetalLayer layer];
    g_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    g_layer.contentsScale = scale;
    g_layer.displaySyncEnabled = YES;
    g_layer.drawableSize = CGSizeMake((double)W * scale, (double)H * scale);
    cv.layer = g_layer;
    cv.wantsLayer = YES;
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if (!dev) { fprintf(stderr, "probeA2: no MTLDevice\n"); return 1; }
    g_queue = [dev newCommandQueue];
    fprintf(stderr,
            "probeA2 setup: device=%s winBackScale=%.2f ds=(%.0f,%.0f) "
            "mainScreen=%s owner=%s\n",
            [(NSString*)[dev name] UTF8String], scale,
            g_layer.drawableSize.width, g_layer.drawableSize.height,
            [NSStringFromRect(sf) UTF8String], g_owner);
    [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer* t) {
        (void)t;
        sampleTick("timer");
    }];
    NSRunLoop* rl = [NSRunLoop mainRunLoop];
    g_t0 = nowSec();
    double tEnd = g_t0 + 30.0;
    while (nowSec() < tEnd) {
        [rl runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.5]];
        if (nowSec() - g_lastSampleAt > 0.75)
            sampleTick("pump-fallback");
    }
    double tOn = nowSec() - g_t0;
    fprintf(stderr,
            "probeA2 done: t=%.1fs samples=%d firstNonNilTick=%d "
            "nonNilDrawables=%d everWsOnScreen=%d\n",
            tOn, g_sample, g_firstNonNil, g_nonNilCount, g_everOnScreen);
    [g_win close];
    return g_firstNonNil >= 0 ? 0 : 1;
}