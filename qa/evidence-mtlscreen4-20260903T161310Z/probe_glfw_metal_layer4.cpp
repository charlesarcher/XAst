// probe_glfw_metal_layer4.cpp — GROUND-TRUTH probe for the MTL-on-screen fix.
//
// Answers, on THIS machine, with NO -lvulkan link (loader via dlopen, exactly
// like the MTL game will do):
//   1. Does a bare dlopen("libvulkan.1.dylib") succeed with the current env?
//      (This is what GLFW does internally — if it fails here, GLFW's own
//       loader install fails too.)
//   2. Can we create a minimal VkInstance (loader-only: portability flag +
//      GLFW-required exts cross-checked vs the loader, NO ICD env needed)?
//   3. Does glfwCreateWindowSurface install a CAMetalLayer on the view?
//   4. Once we set that layer's drawableSize, do nextDrawable calls flow?
//
// Env knobs (all optional):
//   XAST_PROBE_PRELOAD_ABS=<path>  dlopen this path FIRST (simulate the MTL
//                                  game pre-loading the loader by absolute
//                                  path so GLFW's bare dlopen finds it).
//   XAST_PROBE_HAMMER=<sec>        nextDrawable hammer duration (default 12).
//
// Build (NO -lvulkan):
//   c++ -std=c++17 -Ivendor/glfw/GLFW -Ivendor/vulkan/include -Iutilities/rendering \
//     -L/opt/homebrew/opt/glfw/lib -Wl,-rpath,/opt/homebrew/opt/glfw/lib \
//     probe_glfw_metal_layer4.cpp -lglfw -framework Metal -framework QuartzCore \
//     -framework Foundation -framework AppKit -ldl -o probe4
//
// Run under different env to compare configs.

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_metal.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>

static double nowsec() { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec*1e-9; }

// ---- loader entry points resolved via dlsym (NOT linked) ----
typedef PFN_vkCreateInstance                 fpCreateInstance;
typedef PFN_vkEnumerateInstanceExtensionProperties fpEnumInstExt;
typedef PFN_vkDestroyInstance                fpDestroyInstance;
typedef PFN_vkDestroySurfaceKHR              fpDestroySurface;

static fpCreateInstance            p_vkCreateInstance = NULL;
static fpEnumInstExt               p_vkEnumerateInstanceExtensionProperties = NULL;
static fpDestroyInstance           p_vkDestroyInstance = NULL;
static fpDestroySurface           p_vkDestroySurfaceKHR = NULL;

static void* g_loaderHandle = NULL;

static int loadLoader(const char* label) {
  // Try, in order: the pre-load abs path (if set), then bare name, then a
  // couple of known homebrew locations. Returns 1 if ANY succeeds and sets
  // g_loaderHandle + resolves the entry points.
  const char* cand[6];
  int n = 0;
  const char* pre = getenv("XAST_PROBE_PRELOAD_ABS");
  if (pre && pre[0]) cand[n++] = pre;
  cand[n++] = "libvulkan.1.dylib";               // bare name (GLFW's strategy)
  cand[n++] = "/opt/homebrew/lib/libvulkan.1.dylib";
  cand[n++] = "/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib";
  cand[n++] = "/usr/local/lib/libvulkan.1.dylib";
  cand[n++] = "/usr/local/opt/vulkan-loader/lib/libvulkan.1.dylib";
  for (int i = 0; i < n; i++) {
    void* h = dlopen(cand[i], RTLD_NOW | RTLD_LOCAL);
    if (h) {
      fprintf(stderr, "[loader] dlopen(\"%s\") OK  (via %s)\n", cand[i], label);
      g_loaderHandle = h;
      p_vkCreateInstance = (fpCreateInstance)dlsym(h, "vkCreateInstance");
      p_vkEnumerateInstanceExtensionProperties = (fpEnumInstExt)dlsym(h, "vkEnumerateInstanceExtensionProperties");
      p_vkDestroyInstance = (fpDestroyInstance)dlsym(h, "vkDestroyInstance");
      p_vkDestroySurfaceKHR = (fpDestroySurface)dlsym(h, "vkDestroySurfaceKHR");
      if (!p_vkCreateInstance || !p_vkEnumerateInstanceExtensionProperties) {
        fprintf(stderr, "[loader] dlopen OK but dlsym incomplete (ci=%p enum=%p)\n",
                (void*)p_vkCreateInstance, (void*)p_vkEnumerateInstanceExtensionProperties);
        return 0;
      }
      return 1;
    }
  }
  fprintf(stderr, "[loader] ALL dlopen attempts FAILED (%s)\n", label);
  return 0;
}

// Bare-name probe: does dlopen("libvulkan.1.dylib") succeed RIGHT NOW (before
// any abs preload)? Mirrors GLFW's internal dlopen exactly.
static int bareNameVisible() {
  void* h = dlopen("libvulkan.1.dylib", RTLD_NOW | RTLD_LOCAL);
  if (h) { fprintf(stderr, "[glfw-sim] bare dlopen(\"libvulkan.1.dylib\") -> FOUND\n"); return 1; }
  fprintf(stderr, "[glfw-sim] bare dlopen(\"libvulkan.1.dylib\") -> NOT FOUND (GLFW would fail here)\n");
  return 0;
}

int main(int argc, char** argv) {
  const char* flp = getenv("DYLD_FALLBACK_LIBRARY_PATH");
  const char* icd = getenv("VK_ICD_FILENAMES");
  const char* pre = getenv("XAST_PROBE_PRELOAD_ABS");
  long hammer = 12;
  const char* hm = getenv("XAST_PROBE_HAMMER");
  if (hm && hm[0]) hammer = atol(hm);
  fprintf(stderr, "=== probe4 env: DYLD_FALLBACK_LIBRARY_PATH=%s VK_ICD_FILENAMES=%s PRELOAD_ABS=%s hammer=%lds ===\n",
          flp ? flp : "(null)", icd ? icd : "(null)", pre ? pre : "(null)", hammer);

  // 0) Does the bare name resolve under the CURRENT env (before any preload)?
  int bareOk = bareNameVisible();

  if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 2; }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* win = glfwCreateWindow(688, 702, "XAstMTLProbe4", nullptr, nullptr);
  if (!win) { fprintf(stderr, "glfwCreateWindow failed\n"); return 2; }
  fprintf(stderr, "window created (NO_API)\n");

  // 1) Load the loader. If a preload-abs is set, use it; else bare-name/homebrew.
  if (!loadLoader(pre ? "preload-abs" : "default")) {
    fprintf(stderr, "CONCLUSION: loader not loadable in this config -> no instance, no surface, no layer\n");
    return 3;
  }

  // 2) Required instance extensions from GLFW, cross-checked vs the loader.
  uint32_t nExt = 0;
  const char** req = glfwGetRequiredInstanceExtensions(&nExt);
  fprintf(stderr, "GLFW required instance exts (%u):", nExt ? nExt : 0);
  if (req) for (uint32_t i = 0; i < nExt; i++) fprintf(stderr, " %s", req[i]);
  fprintf(stderr, "\n");
  if (!req) {
    fprintf(stderr, "CONCLUSION: glfwGetRequiredInstanceExtensions -> NULL (GLFW's loader dlopen failed in-process)\n");
    return 4;
  }
  uint32_t nprop = 0;
  p_vkEnumerateInstanceExtensionProperties(nullptr, &nprop, nullptr);
  std::vector<VkExtensionProperties> props(nprop);
  if (nprop) p_vkEnumerateInstanceExtensionProperties(nullptr, &nprop, props.data());
  fprintf(stderr, "loader exposes %u instance exts:", nprop);
  for (auto& p : props) fprintf(stderr, " %s", p.extensionName);
  fprintf(stderr, "\n");
  auto exposed = [&](const char* name) {
    for (auto& p : props) if (!strcmp(p.extensionName, name)) return true;
    return false;
  };
  std::vector<const char*> enable;
  for (uint32_t i = 0; i < nExt; i++) {
    if (exposed(req[i])) enable.push_back(req[i]);
    else fprintf(stderr, "  skip %s (loader does not expose it)\n", req[i]);
  }
  bool haveMetal = exposed("VK_EXT_metal_surface");
  bool havePort = exposed("VK_KHR_portability_enumeration");
  if (havePort) enable.push_back("VK_KHR_portability_enumeration");
  fprintf(stderr, "enable-set (%zu):", enable.size());
  for (auto* e : enable) fprintf(stderr, " %s", e);
  fprintf(stderr, "   [metal_surface_exposed=%d portability_exposed=%d]\n", (int)haveMetal, (int)havePort);

  // 3) Create the instance (loader-only: portability flag ALWAYS set).
  VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
  ai.pApplicationName = "XAstMTLProbe4";
  VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
  ici.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  ici.pApplicationInfo = &ai;
  ici.enabledExtensionCount = (uint32_t)enable.size();
  ici.ppEnabledExtensionNames = enable.empty() ? nullptr : enable.data();
  VkInstance inst;
  VkResult rc = p_vkCreateInstance(&ici, nullptr, &inst);
  if (rc != VK_SUCCESS) {
    fprintf(stderr, "vkCreateInstance FAILED rc=%d\n", (int)rc);
    return 5;
  }
  fprintf(stderr, "vkCreateInstance OK (loader-only)\n");

  // 4) glfwCreateWindowSurface -> GLFW installs its CAMetalLayer on the view.
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkResult src = glfwCreateWindowSurface(inst, win, nullptr, &surface);
  fprintf(stderr, "glfwCreateWindowSurface rc=%d (%s)\n", (int)src, src == VK_SUCCESS ? "OK" : "FAIL");

  // 5) Introspect the layer GLFW installed.
  NSView* view = (NSView*)glfwGetCocoaView(win);
  CALayer* vlayer = view ? [view layer] : nil;
  fprintf(stderr, "view=%s viewClass=%s layer=%s\n",
          view ? "yes" : "no",
          view ? [[view class] description].UTF8String : "?",
          vlayer ? [[vlayer class] description].UTF8String : "nil");
  int isMetal = (vlayer && [vlayer isKindOfClass:[CAMetalLayer class]]) ? 1 : 0;
  fprintf(stderr, "view.layer is CAMetalLayer: %s\n", isMetal ? "YES" : "no");
  if (!isMetal) {
    fprintf(stderr, "CONCLUSION: GLFW did not install a CAMetalLayer (surface rc=%d, loader bare-visible=%d)\n", (int)src, bareOk);
    return 6;
  }
  CAMetalLayer* ml = (CAMetalLayer*)vlayer;
  CGSize ds0 = [ml drawableSize];
  fprintf(stderr, "GLFW LAYER: pixelFormat=%lu ds=(%.0f,%.0f) cs=%.1f dsync=%s maxDrawables=%lu\n",
          (unsigned long)[ml pixelFormat], ds0.width, ds0.height, (double)[ml contentsScale],
          [ml displaySyncEnabled] ? "YES" : "no", (unsigned long)[ml maximumDrawableCount]);

  // 6) Set the drawable size EXACTLY as the game does (scale x canonical) and
  //    hammer nextDrawable. This is the step the prior run3 probe skipped.
  double scale = 2.0;
  [ml setDrawableSize:CGSizeMake(1376, 1404)];
  CGSize ds1 = [ml drawableSize];
  fprintf(stderr, "set drawableSize=(1376,1404); now ds=(%.0f,%.0f)\n", ds1.width, ds1.height);

  long nonNil = 0, nNil = 0;
  double t0 = nowsec();
  for (int tick = 0; nowsec() - t0 < (double)hammer; tick++) {
    @autoreleasepool {
      id<CAMetalDrawable> d = [ml nextDrawable];
      if (d) { nonNil++; } else { nNil++; }
    }
    if (tick % 20 == 0)
      fprintf(stderr, "[t=%ds] glfw-layer nextDrawable nonNil=%ld nNil=%ld\n", tick / 20, nonNil, nNil);
    glfwPollEvents();
    usleep(50000);
  }
  fprintf(stderr, "GLFW LAYER %lds: nonNil=%ld nNil=%ld\n", hammer, nonNil, nNil);

  // 7) Verdict.
  fprintf(stderr, "=== VERDICT ===\n");
  fprintf(stderr, "bare-name-loader-visible(no env effect yet)=%d  layerInstalled=%d  drawablesFlow=%ld\n",
          bareOk, isMetal, nonNil);
  if (isMetal && nonNil > 0)
    fprintf(stderr, ">>> GLFW layer + drawableSize => DRAWABLES FLOW. Fix works in this config.\n");
  else if (isMetal && nonNil == 0)
    fprintf(stderr, ">>> layer installed but NO drawables (need more: ICD? env? present?).\n");
  else
    fprintf(stderr, ">>> layer NOT installed in this config.\n");

  // cleanup
  if (surface != VK_NULL_HANDLE && p_vkDestroySurfaceKHR) p_vkDestroySurfaceKHR(inst, surface, nullptr);
  if (p_vkDestroyInstance) p_vkDestroyInstance(inst, nullptr);
  glfwDestroyWindow(win);
  glfwTerminate();
  return (isMetal && nonNil > 0) ? 0 : 1;
}