// probe_maxdrawables.cpp — final A/B: game-exact config vs game-exact + maximumDrawableCount=3
// Window A: layer created before window (game order), pixelFormat, contentsScale=mainScreen,
//           attach layer-first then wantsLayer, displaySync YES, ds=(1376,1404)   [no maxDrawables]
// Window B: identical, PLUS [layer setMaximumDrawableCount:3]
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

struct WinCfg { GLFWwindow* win; CAMetalLayer* layer; int setMax; };

static void hammer(WinCfg& w, int sec, long& count) {
  @autoreleasepool {
    for (int s = 0; s < sec; s++) {
      id<CAMetalDrawable> d = [w.layer nextDrawable];
      if (d) count++;
      glfwPollEvents();
      usleep(200000);
    }
  }
}

int main() {
  if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 2; }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

  WinCfg A = {}, B = {};
  // create both layers BEFORE the windows (game order: mtlCreateLayer precedes glfwCreateWindow)
  A.layer = [CAMetalLayer layer];
  [A.layer setPixelFormat:MTLPixelFormatBGRA8Unorm];
  [A.layer setContentsScale:[[NSScreen mainScreen] backingScaleFactor]];
  B.layer = [CAMetalLayer layer];
  [B.layer setPixelFormat:MTLPixelFormatBGRA8Unorm];
  [B.layer setContentsScale:[[NSScreen mainScreen] backingScaleFactor]];

  A.win = glfwCreateWindow(688, 702, "AB-A-gamexact", nullptr, nullptr);
  B.win = glfwCreateWindow(688, 702, "AB-B-maxdraw3", nullptr, nullptr);
  if (!A.win || !B.win) { fprintf(stderr, "window create failed\n"); return 2; }
  // offset B so the windows don't overlap
  glfwSetWindowPos(B.win, 760, 0);

  {
    WinCfg* ws[2] = { &A, &B };
    for (int wi = 0; wi < 2; wi++) {
      WinCfg* w = ws[wi];
      NSView* view = (NSView*)glfwGetCocoaView(w->win);
      [view setLayer:(CALayer*)w->layer];      // game attach order
      [view setWantsLayer:YES];
      if (w == &B) [B.layer setMaximumDrawableCount:3];
      [w->layer setDisplaySyncEnabled:YES];
      [w->layer setDrawableSize:CGSizeMake(1376, 1404)];
      fprintf(stderr, "%s: attached superlayer=%s ds=(%.0f,%.0f) maxDrawables=%lu\n",
              (w == &A) ? "A" : "B", [w->layer superlayer] ? "SET" : "nil",
              [w->layer drawableSize].width, [w->layer drawableSize].height,
              (unsigned long)[w->layer maximumDrawableCount]);
    }
  }
  long nA = 0, nB = 0;
  fprintf(stderr, "hammering both windows for 40s...\n");
  double t0 = nowsec();
  for (int s = 0; s < 40; s++) {
    hammer(A, 1, nA);
    hammer(B, 1, nB);
    if (s % 10 == 0) fprintf(stderr, "[t=%2ds] A(gamexact)=%ld B(+maxDraw3)=%ld\n", s, nA, nB);
    if (nowsec() - t0 > 45.0) break;
  }
  fprintf(stderr, "FINAL: A(gamexact)=%ld B(gamexact+maxDraw3)=%ld\n", nA, nB);
  if (nB > 0 && nA == 0) fprintf(stderr, "FIX FOUND: [layer setMaximumDrawableCount:3] is the missing piece — one-line product fix\n");
  else if (nA > 0) fprintf(stderr, "NOTE: game-exact config DOES work here (run-to-run variance?)\n");
  else fprintf(stderr, "NO FIX: even maximumDrawableCount=3 yields no pool\n");
  return 0;
}