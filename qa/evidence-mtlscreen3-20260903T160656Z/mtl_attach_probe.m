// mtl_attach_probe.m — GLFW NO_API + CAMetalLayer attach-variant probe
// mode 0: game-exact (layer first, then wantsLayer; displaySync YES; no explicit frame)
// mode 1: mode 0 + explicit [layer setFrame:view.bounds] (kept fresh each tick)
// mode 2: wantsLayer=YES BEFORE view.layer=layer (inverted order)
// mode 3: mode 0 + pump 1.5s of events before attach (window on-screen first)
// mode 4: mode 1 + displaySyncEnabled=NO
// mode 5: attach deferred to t=0.3s + explicit frame
// exit 0 = presented a drawable; 1 = nil-forever; 2 = setup failure
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char* kSrc =
"vertex float4 vs_main(uint vid [[vertex_id]]) {"
"  float2 p;"
"  if (vid==0) p=float2(-1.,-1.); else if (vid==1) p=float2(1.,-1.);"
"  else if (vid==2) p=float2(-1.,1.); else p=float2(1.,1.);"
"  float4 o; o.xy=p; o.zw=0.; return o; }"
"fragment float4 fs_main() { return float4(0.9,0.1,0.1,1.0); }";

static CAMetalLayer* layer = nil;
static NSView* view = nil;
static GLFWwindow* win = nil;
static id<MTLDevice> device = nil;
static id<MTLCommandQueue> queue = nil;
static id<MTLRenderPipelineState> rps = nil;
static int mode = 0;
static int attached = 0;
static int presented = 0;
static long nonNil = 0;
static double t0 = 0;

static double nowsec() { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec*1e-9; }

static void createLayer() {
  layer = [CAMetalLayer layer];
  [layer setPixelFormat:MTLPixelFormatBGRA8Unorm];
  [layer setContentsScale:[[NSScreen mainScreen] backingScaleFactor]];
}
static void attachGameOrder() {
  view = (NSView*)glfwGetCocoaView(win);
  [view setLayer:(CALayer*)layer];
  [view setWantsLayer:YES];
  attached = 1;
}
static void attachWantsLayerFirst() {
  view = (NSView*)glfwGetCocoaView(win);
  [view setWantsLayer:YES];
  [view setLayer:(CALayer*)layer];
  attached = 1;
}
static void setExplicitFrame() {
  if (layer && view) [layer setFrame:[view bounds]];
}
static void tryPresent() {
  @autoreleasepool {
    id<CAMetalDrawable> d = [layer nextDrawable];
    if (!d) return;
    nonNil++;
    if (presented) return;
    MTLRenderPassDescriptor* pd = [MTLRenderPassDescriptor renderPassDescriptor];
    pd.colorAttachments[0].texture = [d texture];
    pd.colorAttachments[0].loadAction = MTLLoadActionClear;
    pd.colorAttachments[0].storeAction = MTLStoreActionStore;
    pd.colorAttachments[0].clearColor = MTLClearColorMake(0.9,0.1,0.1,1.0);
    id<MTLRenderCommandEncoder> enc = [queue createRenderCommandEncoderWithDescriptor:pd];
    if (enc) {
      [enc setRenderPipelineState:rps];
      [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:1];
      [enc endEncoding];
    }
    id<MTLCommandBuffer> cb = [queue commandBuffer];
    [cb presentDrawable:d];
    [cb commit];
    presented = 1;
    fprintf(stderr, "PRESENTED at t=%.2f (mode=%d)\n", nowsec()-t0, mode);
  }
}
static void telemetry(int sec) {
  @autoreleasepool {
    CGRect f = layer ? [layer frame] : CGRectZero;
    CGSize ds = layer ? [layer drawableSize] : CGSizeZero;
    fprintf(stderr, "[t=%2ds] attached=%d view=%s inWindow=%s superlayer=%s frame=(%.0f,%.0f,%.0f,%.0f) ds=(%.0f,%.0f) cs=%.1f dsync=%s nonNil=%ld presented=%d\n",
      sec, attached, view?"yes":"no", (view && [view superview])?"yes":"no",
      (layer && [layer superlayer])?"SET":"nil",
      f.origin.x, f.origin.y, f.size.width, f.size.height,
      ds.width, ds.height,
      layer ? (double)[layer contentsScale] : 0.0,
      (layer && [layer displaySyncEnabled])?"YES":"no",
      nonNil, presented);
  }
}

int main(int argc, char** argv) {
  @autoreleasepool {
    mode = argc > 1 ? atoi(argv[1]) : 0;
    fprintf(stderr, "mode=%d\n", mode);
    if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 2; }
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    win = glfwCreateWindow(688, 702, "MTLProbe", nullptr, nullptr);
    if (!win) { fprintf(stderr, "glfwCreateWindow failed\n"); return 2; }
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    if (mon) {
      const GLFWvidmode* m = glfwGetVideoMode(mon);
      if (m) glfwSetWindowPos(win, (m->width-688)/2, (m->height-702)/2);
    }
    createLayer();
    if (mode == 3) { for (int i = 0; i < 3; i++) glfwWaitEventsTimeout(0.5); }
    if (mode == 2) attachWantsLayerFirst();
    else if (mode != 5) attachGameOrder(); // modes 0,1,3,4 attach at creation
    device = MTLCreateSystemDefaultDevice();
    if (!device) { fprintf(stderr, "no device\n"); return 2; }
    queue = [device newCommandQueue];
    NSError* err = nil;
    id<MTLLibrary> lib = [device newLibraryWithSource:[NSString stringWithUTF8String:kSrc] options:nil error:&err];
    if (!lib) { fprintf(stderr, "shader compile FAILED: %s\n", err ? [[err localizedDescription] UTF8String] : "?"); return 2; }
    id<MTLFunction> vf = [lib newFunctionWithName:@"vs_main"];
    id<MTLFunction> ff = [lib newFunctionWithName:@"fs_main"];
    if (!vf || !ff) { fprintf(stderr, "function lookup FAILED vf=%d ff=%d\n", (int)(vf!=nil), (int)(ff!=nil)); return 2; }
    MTLRenderPipelineDescriptor* pdesc = [MTLRenderPipelineDescriptor new];
    [pdesc setVertexFunction:vf];
    [pdesc setFragmentFunction:ff];
    pdesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    NSError* perr = nil;
    rps = [device newRenderPipelineStateWithDescriptor:pdesc error:&perr];
    if (!rps) { fprintf(stderr, "pipeline FAILED: %s\n", perr ? [[perr localizedDescription] UTF8String] : "?"); return 2; }
    [layer setDisplaySyncEnabled:(mode != 4)];
    NSWindow* nsw = (NSWindow*)glfwGetCocoaWindow(win);
    double scale = [nsw backingScaleFactor];
    if (scale <= 0.0) scale = 1.0;
    [layer setDrawableSize:CGSizeMake(688*scale, 702*scale)];
    t0 = nowsec();
    for (long tick = 0; nowsec()-t0 < 30.0; tick++) {
      glfwPollEvents();
      if (mode == 5 && tick == 3) { attachGameOrder(); setExplicitFrame(); }
      if (mode == 1 || mode == 4 || mode == 5) if (attached) setExplicitFrame();
      if (attached) tryPresent();
      if (tick % 10 == 0) telemetry((int)(tick/10));
      usleep(100000);
    }
    telemetry(30);
    fprintf(stderr, "FINAL mode=%d attached=%d presented=%d nonNil=%ld\n", mode, attached, presented, nonNil);
    return presented ? 0 : 1;
  }
}