// probe_metal_render5.cpp — FAITHFUL probe: GLFW-layer handshake + REAL Metal
// render+present loop (like mtlBridge) + on-screen (CGWindowList) verification.
//
// This mirrors exactly what the MTL game will do after the fix:
//   1. NO_API GLFW window
//   2. minimal VkInstance (loader via dlopen, no -lvulkan) + glfwCreateWindowSurface
//      -> GLFW installs its CAMetalLayer on the view
//   3. MTLDevice + MTLCommandQueue
//   4. set layer drawableSize
//   5. loop: nextDrawable -> render a BRIGHT clear -> presentDrawable -> commit
//   6. verify the window is on-screen (CGWindowList) so a screencapture is meaningful
//
// The difference from probe4: it actually PRESENTS. If drawables flow and the
// window is on-screen, a screencapture of the window region will be NON-BLACK.
//
// Build (NO -lvulkan):
//   xcrun -sdk macosx clang++ -x objective-c++ -std=c++17 \
//     -Ivendor/glfw -Ivendor/vulkan/include probe_metal_render5.cpp \
//     -L/opt/homebrew/opt/glfw/lib -Wl,-rpath,/opt/homebrew/opt/glfw/lib \
//     -lglfw -framework Metal -framework QuartzCore -framework Foundation \
//     -framework AppKit -framework CoreGraphics -ldl -o probe5

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_metal.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <CoreGraphics/CoreGraphics.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>

static double nowsec() { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec*1e-9; }

typedef PFN_vkCreateInstance fpCreateInstance;
typedef PFN_vkEnumerateInstanceExtensionProperties fpEnumInstExt;
typedef PFN_vkDestroyInstance fpDestroyInstance;
typedef PFN_vkDestroySurfaceKHR fpDestroySurface;
static fpCreateInstance p_vkCreateInstance = NULL;
static fpEnumInstExt    p_vkEnumerateInstanceExtensionProperties = NULL;
static fpDestroyInstance p_vkDestroyInstance = NULL;
static fpDestroySurface p_vkDestroySurfaceKHR = NULL;
static void* g_loader = NULL;

static int loadLoader() {
  const char* cand[5] = {
    "libvulkan.1.dylib",
    "/opt/homebrew/lib/libvulkan.1.dylib",
    "/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib",
    "/usr/local/lib/libvulkan.1.dylib",
    "/usr/local/opt/vulkan-loader/lib/libvulkan.1.dylib",
  };
  for (int i = 0; i < 5; i++) {
    void* h = dlopen(cand[i], RTLD_NOW | RTLD_LOCAL);
    if (h) {
      g_loader = h;
      p_vkCreateInstance = (fpCreateInstance)dlsym(h, "vkCreateInstance");
      p_vkEnumerateInstanceExtensionProperties = (fpEnumInstExt)dlsym(h, "vkEnumerateInstanceExtensionProperties");
      p_vkDestroyInstance = (fpDestroyInstance)dlsym(h, "vkDestroyInstance");
      p_vkDestroySurfaceKHR = (fpDestroySurface)dlsym(h, "vkDestroySurfaceKHR");
      fprintf(stderr, "[loader] dlopen(\"%s\") OK\n", cand[i]);
      return (p_vkCreateInstance && p_vkEnumerateInstanceExtensionProperties) ? 1 : 0;
    }
  }
  fprintf(stderr, "[loader] NOT FOUND\n");
  return 0;
}

// Is the app's window currently on-screen per the window server?
static int windowOnScreen() {
  CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, kCGNullWindowID);
  if (!list) return -1;
  CFIndex n = CFArrayGetCount(list);
  int onScreen = 0;
  pid_t me = getpid();
  for (CFIndex i = 0; i < n; i++) {
    CFDictionaryRef d = (CFDictionaryRef)CFArrayGetValueAtIndex(list, i);
    CFNumberRef pidRef = (CFNumberRef)CFDictionaryGetValue(d, kCGWindowOwnerPID);
    if (!pidRef) continue;
    int wpid = 0; CFNumberGetValue(pidRef, kCFNumberIntType, &wpid);
    if ((pid_t)wpid != me) continue;
    CFNumberRef layerRef = (CFNumberRef)CFDictionaryGetValue(d, kCGWindowLayer);
    if (!layerRef) continue;
    int layer = 0; CFNumberGetValue(layerRef, kCFNumberIntType, &layer);
    if (layer == 0) onScreen++;
  }
  CFRelease(list);
  return onScreen;  // >0 = on-screen normal-layer windows for this pid
}

int main(int argc, char** argv) {
  const char* flp = getenv("DYLD_FALLBACK_LIBRARY_PATH");
  const char* icd = getenv("VK_ICD_FILENAMES");
  double dur = 15.0;
  const char* du = getenv("XAST_PROBE_DUR");
  if (du && du[0]) dur = atof(du);
  fprintf(stderr, "=== probe5 env: DYLD_FLP=%s VK_ICD=%s dur=%.0fs ===\n",
          flp ? flp : "(null)", icd ? icd : "(null)", dur);

  if (!loadLoader()) { fprintf(stderr, "CONCLUSION: no loader -> no handshake\n"); return 3; }
  if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 2; }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
  GLFWwindow* win = glfwCreateWindow(688, 702, "XAstMTLProbe5", nullptr, nullptr);
  if (!win) { fprintf(stderr, "glfwCreateWindow failed\n"); return 2; }

  // instance (loader-only + portability)
  uint32_t nExt = 0; const char** req = glfwGetRequiredInstanceExtensions(&nExt);
  if (!req) { fprintf(stderr, "glfwGetRequiredInstanceExtensions NULL\n"); return 4; }
  uint32_t nprop = 0; p_vkEnumerateInstanceExtensionProperties(nullptr, &nprop, nullptr);
  std::vector<VkExtensionProperties> props(nprop);
  if (nprop) p_vkEnumerateInstanceExtensionProperties(nullptr, &nprop, props.data());
  auto exposed = [&](const char* nm){ for (auto& p: props) if(!strcmp(p.extensionName,nm)) return true; return false; };
  std::vector<const char*> en;
  for (uint32_t i=0;i<nExt;i++) if (exposed(req[i])) en.push_back(req[i]);
  if (exposed("VK_KHR_portability_enumeration")) en.push_back("VK_KHR_portability_enumeration");
  VkApplicationInfo ai={VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.pApplicationName="XAstMTLProbe5";
  VkInstanceCreateInfo ici={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.flags=VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR; ici.pApplicationInfo=&ai;
  ici.enabledExtensionCount=(uint32_t)en.size(); ici.ppEnabledExtensionNames=en.empty()?nullptr:en.data();
  VkInstance inst;
  if (p_vkCreateInstance(&ici,nullptr,&inst)!=VK_SUCCESS){ fprintf(stderr,"vkCreateInstance FAILED\n"); return 5; }
  VkSurfaceKHR surface=VK_NULL_HANDLE;
  if (glfwCreateWindowSurface(inst,win,nullptr,&surface)!=VK_SUCCESS){ fprintf(stderr,"glfwCreateWindowSurface FAILED\n"); return 6; }
  NSView* view=(NSView*)glfwGetCocoaView(win);
  CAMetalLayer* ml=(CAMetalLayer*)([view layer]);
  fprintf(stderr, "layer=%s\n", (ml&&[ml isKindOfClass:[CAMetalLayer class]])?"CAMetalLayer":"??");
  if (!ml){ fprintf(stderr,"no metal layer\n"); return 7; }
  id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
  id<MTLCommandQueue> q = [dev newCommandQueue];
  [ml setDrawableSize:CGSizeMake(1376,1404)];
  fprintf(stderr, "device=%s queue=%s ds=%zx\n", dev?"yes":"no", q?"yes":"no", (size_t)[ml drawableSize].width);

  long presented=0, nilCnt=0;
  double t0=nowsec();
  int tick=0;
  while (nowsec()-t0 < dur) {
    tick++;
    @autoreleasepool {
      int os = windowOnScreen();
      id<CAMetalDrawable> d = [ml nextDrawable];
      if (d) {
        nilCnt = nilCnt; // (kept for symmetry)
        id<MTLCommandBuffer> cb = [q commandBuffer];
        MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
        desc.colorAttachments[0].texture = d.texture;
        desc.colorAttachments[0].loadAction = MTLLoadActionClear;
        desc.colorAttachments[0].storeAction = MTLStoreActionStore;
        desc.colorAttachments[0].clearColor = MTLClearColorMake(1.0,0.15,0.15,1.0); // BRIGHT RED
        id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:desc];
        [e endEncoding];
        [cb presentDrawable:d];
        [cb commit];
        presented++;
        if (presented % 30 == 0 || tick%30==0)
          fprintf(stderr, "[t=%.1fs onScreen=%d] presented=%ld nilSeen=%ld\n", nowsec()-t0, os, presented, nilCnt);
      } else {
        nilCnt++;
        if (nilCnt % 30 == 0)
          fprintf(stderr, "[t=%.1fs onScreen=%d] still-nil nilCnt=%ld presented=%ld\n", nowsec()-t0, os, nilCnt, presented);
      }
    }
    glfwPollEvents();
    usleep(16000);
  }
  fprintf(stderr, "=== probe5 RESULT: presented=%ld nilSeen=%ld (dur %.0fs) ===\n", presented, nilCnt, dur);
  if (presented>0) fprintf(stderr, ">>> DRAWABLES FLOW + METAL PRESENT WORKS on the GLFW layer\n");
  else fprintf(stderr, ">>> NO drawables even with real presents\n");
  if (surface!=VK_NULL_HANDLE && p_vkDestroySurfaceKHR) p_vkDestroySurfaceKHR(inst,surface,nullptr);
  if (p_vkDestroyInstance) p_vkDestroyInstance(inst,nullptr);
  glfwDestroyWindow(win); glfwTerminate();
  return presented>0?0:1;
}