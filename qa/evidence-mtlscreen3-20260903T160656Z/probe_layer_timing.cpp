// probe_layer_timing.cpp — decisive: which layer configuration gets a drawable pool on this macOS build?
// Phase 1 (0-15s):  GLFW's own CAMetalLayer at ds=(0,0) — expect nil
// Phase 2 (15-30s): GLFW's own layer with drawableSize set to (1376,1404)
// Phase 3 (30-50s): swap in a game-style CAMetalLayer AFTER the window is mature; set its ds
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static double nowsec() { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec*1e-9; }

int main() {
  if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 2; }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* win = glfwCreateWindow(688, 702, "LayerTiming", nullptr, nullptr);
  if (!win) { fprintf(stderr, "glfwCreateWindow failed\n"); return 2; }
  NSView* view = (NSView*)glfwGetCocoaView(win);

  // GLFW's own layer: in a NO_API window GLFW may not create one until
  // glfwCreateWindowSurface — but per glfw#1340 it installs one at window
  // creation for the metal path; if absent, we create our own stand-in in
  // the same style GLFW uses (layer first, then wantsLayer).
  CAMetalLayer* gl = nil;
  CALayer* vlayer = [view layer];
  if (vlayer && [vlayer isKindOfClass:[CAMetalLayer class]]) {
    gl = (CAMetalLayer*)vlayer;
    fprintf(stderr, "P1: GLFW installed its own CAMetalLayer (class=%s)\n", [[vlayer class] description].UTF8String);
  } else {
    fprintf(stderr, "P1: NO metal layer on the NO_API window's view (class=%s) — GLFW defers it; creating GLFW-style stand-in now (pre-display)\n",
            vlayer ? [[vlayer class] description].UTF8String : "nil");
    gl = [CAMetalLayer layer];
    [gl setPixelFormat:MTLPixelFormatBGRA8Unorm];
    [gl setContentsScale:2.0];
    [view setLayer:(CALayer*)gl];
    [view setWantsLayer:YES];
  }
  fprintf(stderr, "P1: ds=(%.0f,%.0f) superlayer=%s inWindow=%s\n",
          [gl drawableSize].width, [gl drawableSize].height,
          [gl superlayer] ? "SET" : "nil", [view superview] ? "yes" : "no");

  long n1 = 0, n2 = 0, n3 = 0;
  double t0 = nowsec();
  CAMetalLayer* my = nil;
  int phase = 1;
  for (long tick = 0; nowsec() - t0 < 50.0; tick++) {
    double t = nowsec() - t0;
    if (phase == 1 && t >= 15.0) {
      phase = 2;
      [gl setDrawableSize:CGSizeMake(1376, 1404)];
      fprintf(stderr, "P2: set GLFW layer drawableSize=(1376,1404); ds now=(%.0f,%.0f)\n",
              [gl drawableSize].width, [gl drawableSize].height);
    } else if (phase == 2 && t >= 30.0) {
      phase = 3;
      my = [CAMetalLayer layer];
      [my setPixelFormat:MTLPixelFormatBGRA8Unorm];
      [my setContentsScale:2.0];
      [my setDisplaySyncEnabled:YES];
      [view setLayer:(CALayer*)my];          // game-style swap, post-maturity
      [view setWantsLayer:YES];
      [my setDrawableSize:CGSizeMake(1376, 1404)];
      fprintf(stderr, "P3: swapped in game-style layer (post-maturity); superlayer=%s ds=(%.0f,%.0f)\n",
              [my superlayer] ? "SET" : "nil", [my drawableSize].width, [my drawableSize].height);
    }
    @autoreleasepool {
      CAMetalLayer* probe = (phase == 3) ? my : gl;
      id<CAMetalDrawable> d = [probe nextDrawable];
      if (d) (phase == 1 ? n1 : (phase == 2 ? n2 : n3))++;
    }
    if (tick % 10 == 0)
      fprintf(stderr, "[t=%2ds p=%d] nonNil: p1=%ld p2=%ld p3=%ld\n", (int)t, phase, n1, n2, n3);
    glfwPollEvents();
    usleep(100000);
  }
  fprintf(stderr, "FINAL: GLFWlayer-ds0=%ld  GLFWlayer-ds1376=%ld  swapped-late=%ld\n", n1, n2, n3);
  if (n2 > 0) fprintf(stderr, "FIX PATH A: GLFW's own layer + set drawableSize works -> MTL backend should use/keep the layer GLFW manages (never swap it)\n");
  if (n3 > 0) fprintf(stderr, "FIX PATH B: a game-style layer attached AFTER window maturity gets a pool -> fix = defer mtlAttachToView until first displayed frame\n");
  if (n2 == 0 && n3 == 0) fprintf(stderr, "FIX PATH C: neither works -> window-server pool binding is not layer/ds dependent; escalate\n");
  return 0;
}