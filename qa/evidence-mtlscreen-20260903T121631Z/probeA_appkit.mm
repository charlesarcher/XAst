// probeA_appkit.mm — LANE 1, PROBE A (round 3, confound-free).
// Pure AppKit CLI (NO GLFW): does a plain NSWindow + CAMetalLayer obtain a
// non-nil [layer nextDrawable] within 30s on this machine, and — critically —
// is the window actually ON SCREEN (isOnScreen read as a string every tick)?
//
// Confound fixes vs. the prior session's probes:
//   1. finishLaunching is called AND awaited (2s run-loop settle) before the
//      first drawable sample; app state (isRunning, isFinishedLaunching if
//      the selector exists) is logged at boot.
//   2. window.isOnScreen is read and logged EVERY tick as "yes"/"no"/"n/a"
//      (never dropped). The verdict is only meaningful once isOnScreen=yes.
//   3. contentsScale comes from the window's OWN backingScaleFactor.
//   4. A real repeating 0.5s run-loop timer drives sampling; a pump-step
//      fallback samples if the timer ever fails to fire on this OS.
// Exit code: 0 = a non-nil drawable was obtained within 30s; 1 = never.
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AppKit/AppKit.h>

static NSWindow* g_win = nil;
static CAMetalLayer* g_layer = nil;
static id<MTLCommandQueue> g_queue = nil;
static int g_sample = 0;
static int g_firstNonNil = -1;   // tick index of first non-nil drawable
static int g_nonNilCount = 0;
static double g_t0 = 0.0;
static double g_lastSampleAt = -1.0;

static double nowSec(void) {
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

static const char* yesno(BOOL b) { return b ? "yes" : "no"; }

static void sampleTick(const char* why) {
    @autoreleasepool {
        g_sample++;
        double t = nowSec() - g_t0;
        if (t < 0) t = 0;
        // --- per-tick state (all values actually read and printed) ---
        const char* onScreen = "n/a";
        if (g_win && [g_win respondsToSelector:@selector(isOnScreen)])
            onScreen = [g_win isOnScreen] ? "yes" : "no";
        const char* isKey = "n/a";
        if (g_win && [g_win respondsToSelector:@selector(isKeyWindow)])
            isKey = [g_win isKeyWindow] ? "yes" : "no";
        const char* isMain = [NSThread isMainThread] ? "yes" : "no";
        fprintf(stderr, "tick %2d t=%4.1fs sample=%d [%s] isOnScreen=%s isKey=%s "
                "isMainThread=%s bounds=(%.0f,%.0f) drawableSize=(%.0f,%.0f) "
                "superlayer=%s contentsScale=%.2f\n",
                g_sample, t, g_sample, why,
                onScreen, isKey, isMain,
                g_layer.bounds.size.width, g_layer.bounds.size.height,
                g_layer.drawableSize.width, g_layer.drawableSize.height,
                g_layer.superlayer ? "set" : "nil",
                (double)g_layer.contentsScale);
        // --- the decisive call ---
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
                    MTLClearColorMake(0.0, 1.0, 0.0, 1.0);  // green: visible if composited
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
    // --- app bootstrap: finishLaunching called and AWAITED ---
    NSApplication* app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    [app finishLaunching];
    // await: pump the run loop 2s so activation/window-server registration
    // completes before any drawable is judged (fixes the prior confound).
    { NSRunLoop* rl = [NSRunLoop mainRunLoop];
      for (int i = 0; i < 4; ++i)
        [rl runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.5]]; }
    const char* fin = "n/a";
    if ([app respondsToSelector:@selector(isFinishedLaunching)])
        fin = [(id)app isFinishedLaunching] ? "yes" : "no";
    fprintf(stderr, "probeA boot: isRunning=%s isFinishedLaunching=%s policy=Regular\n",
            [app isRunning] ? "yes" : "no", fin);
    // --- window on the main screen ---
    NSScreen* scr = [NSScreen mainScreen];
    NSRect sf = scr ? [scr frame] : NSMakeRect(0, 0, 1920, 1080);
    NSRect origin = NSMakeRect(sf.origin.x + (sf.size.width - W) / 2.0,
                               sf.origin.y + (sf.size.height - H) / 2.0,
                               (double)W, (double)H);
    g_win = [[NSWindow alloc] initWithContentRect:origin
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                   NSWindowStyleMaskResizable)
        backing:NSBackingStoreBuffered defer:NO];
    [g_win setTitle:@"probeA"];
    [g_win makeKeyAndOrderFront:nil];
    [app activateIgnoringOtherApps:YES];
    NSView* cv = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, W, H)];
    [g_win setContentView:cv];
    double scale = [g_win backingScaleFactor];
    if (scale <= 0.0) scale = 1.0;
    // --- CAMetalLayer on the contentView (window's OWN backingScaleFactor) ---
    g_layer = [CAMetalLayer layer];
    g_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    g_layer.contentsScale = scale;
    g_layer.displaySyncEnabled = YES;
    g_layer.drawableSize = CGSizeMake((double)W * scale, (double)H * scale);
    cv.layer = g_layer;
    cv.wantsLayer = YES;
    // --- GPU ---
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if (!dev) { fprintf(stderr, "probeA: no MTLDevice\n"); return 1; }
    g_queue = [dev newCommandQueue];
    fprintf(stderr, "probeA setup: device=%s windowBackScale=%.2f ds=(%.0f,%.0f) "
            "windowFrame=(%.0f,%.0f,%.0f,%.0f) mainScreen=%s\n",
            [(NSString*)[dev name] UTF8String], scale,
            g_layer.drawableSize.width, g_layer.drawableSize.height,
            [g_win frame].origin.x, [g_win frame].origin.y,
            [g_win frame].size.width, [g_win frame].size.height,
            [NSStringFromRect(sf) UTF8String]);
    // --- 0.5s repeating run-loop timer, up to 30s ---
    [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer* t) {
        (void)t;
        sampleTick("timer");
    }];
    NSRunLoop* rl = [NSRunLoop mainRunLoop];
    g_t0 = nowSec();
    double tEnd = g_t0 + 30.0;
    while (nowSec() < tEnd) {
        [rl runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.5]];
        // fallback: if the timer never fired this step, sample from the pump
        if (nowSec() - g_lastSampleAt > 0.75)
            sampleTick("pump-fallback");
    }
    double tOn = nowSec() - g_t0;
    fprintf(stderr, "probeA done: t=%.1fs samples=%d firstNonNilTick=%d "
            "nonNilDrawables=%d\n",
            tOn, g_sample, g_firstNonNil, g_nonNilCount);
    [g_win close];
    return g_firstNonNil >= 0 ? 0 : 1;
}