// utilities/rendering/mtlCocoa.mm — ObjC++ CAMetalLayer bridge (C ABI)
//
// This is the ONLY Objective-C++ file in the entire repo. It bridges GLFW's
// Cocoa NSView to Metal's CAMetalLayer through a pure-C interface (mtlCocoa.H).
// Extreme isolation: no ObjC types leak into C++ headers.
//
// Note: ARC is OFF for this TU (Xcode default for .mm in non-ARC projects).
// __bridge/__bridge_retained are ARC-only — plain C casts are used instead.
//
// On-screen (2026-09-03): the GLFW-layer swapchain handshake. On this macOS
// build the window server allocates a CAMetalLayer drawable pool ONLY for the
// layer GLFW installs via glfwCreateWindowSurface, and only once a MoltenVK
// VkSwapchainKHR is bound to it. The app never links -lvulkan: the loader is
// dlopen'd at runtime (exactly like GLFW's own loader), so the MTL binary's
// link line and flavor identity are unchanged.
// Vulkan headers FIRST: they define the Vk* types and VK_VERSION_1_0, which
// the vendored <GLFW/glfw3.h> needs in order to compile its Vulkan section
// (the glfwCreateWindowSurface / glfwGetRequiredInstanceExtensions prototypes
// are guarded by VK_VERSION_1_0). GLFW_INCLUDE_VULKAN makes glfw3.h pull the
// same vulkan.h (idempotent), keeping one consistent set of Vk types.
#define GLFW_INCLUDE_VULKAN
#include <vulkan/vulkan.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

// The loader is a runtime dlopen (no link-time symbols) — like GLFW's own.
#include <dlfcn.h>
#include <vector>
#include <string.h>
#include <stdio.h>

#include "mtlCocoa.H"

// ---- GLFW-layer handshake state (implementation detail; opaque void* across
// ---- the C-ABI). Kept alive for process lifetime: destroying any of these
// ---- tears down the layer's drawable pool.
struct MtlGlfwVkState
 {void*        loaderHandle;   // dlopen handle (keeps the loader mapped)
  VkInstance   instance;
  VkPhysicalDevice physicalDevice;
  VkDevice     device;
  VkQueue      queue;
  VkSwapchainKHR swapchain;
  VkSurfaceKHR surface;
 };

static const char* kLoaderCandidates[] =
 {
  // Bare name first: resolves when DYLD_FALLBACK_LIBRARY_PATH points at the
  // homebrew vulkan-loader (the run recipe sets it). This is the same name
  // GLFW dlopens internally, so both share one loader instance.
  "libvulkan.1.dylib",
  "/opt/homebrew/lib/libvulkan.1.dylib",
  "/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib",
  "/usr/local/lib/libvulkan.1.dylib",
  "/usr/local/opt/vulkan-loader/lib/libvulkan.1.dylib",
 };

// Load the vulkan loader and resolve the instance-level entry points the
// handshake needs. Returns 1 on success (gLoader set), 0 otherwise.
static void* gLoader = nullptr;
static PFN_vkCreateInstance                 gVkCreateInstance = nullptr;
static PFN_vkEnumerateInstanceExtensionProperties gVkEnumInstExt = nullptr;
static PFN_vkGetInstanceProcAddr            gVkGIPA = nullptr;

static int mtlLoadVulkanLoader()
 {
  if (gLoader) return 1;
  for (const char* c : kLoaderCandidates)
   {void* h = dlopen(c, RTLD_NOW | RTLD_LOCAL);
    if (!h) continue;
    PFN_vkCreateInstance ci =
      (PFN_vkCreateInstance)dlsym(h, "vkCreateInstance");
    PFN_vkEnumerateInstanceExtensionProperties ee =
      (PFN_vkEnumerateInstanceExtensionProperties)dlsym(h,
          "vkEnumerateInstanceExtensionProperties");
    PFN_vkGetInstanceProcAddr gipa =
      (PFN_vkGetInstanceProcAddr)dlsym(h, "vkGetInstanceProcAddr");
    if (!ci || !ee || !gipa) { dlclose(h); continue; }
    gLoader = h;
    gVkCreateInstance = ci;
    gVkEnumInstExt = ee;
    gVkGIPA = gipa;
    return 1;
   }
  return 0;
 }

extern "C" {

void* mtlCreateLayer(void) {
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.contentsScale = [[NSScreen mainScreen] backingScaleFactor];
    // [layer] returns autoreleased; caller attaches to view immediately,
    // so the layer is retained by the view before the pool drains.
    return (void*)layer;
}

int mtlAttachToView(void* layer, GLFWwindow* window) {
    @autoreleasepool {
        NSView* view = (NSView*)glfwGetCocoaView(window);
        CAMetalLayer* metalLayer = (CAMetalLayer*)layer;
        // CRITICAL ORDER: layer must be set before wantsLayer, otherwise
        // AppKit creates a plain CALayer and ignores our Metal layer.
        view.layer = metalLayer;
        view.wantsLayer = YES;
    }
    return 0;
}

double mtlBackingScaleFactor(GLFWwindow* window) {
    @autoreleasepool {
        NSWindow* nswindow = (NSWindow*)glfwGetCocoaWindow(window);
        return (double)[nswindow backingScaleFactor];
    }
}

void* mtlCreateDevice(void) {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    // MTLCreateSystemDefaultDevice() returns a +1 retained object.
    // Caller takes ownership — intentionally never released (app lifetime).
    return (void*)device;
}

void mtlSetDrawableSize(void* layer, double scale, int fbW, int fbH) {
    @autoreleasepool {
        CAMetalLayer* metalLayer = (CAMetalLayer*)layer;
        metalLayer.drawableSize = CGSizeMake(fbW * scale, fbH * scale);
    }
}

// ---- GLFW-layer on-screen handshake (2026-09-03) -------------------------

int mtlGlfwMetalHandshake(GLFWwindow* window, double scale,
                          int width, int height,
                          void** outLayer, void** outState)
 {
  if (!window) return 1;
  if (outLayer) *outLayer = nullptr;
  if (outState) *outState = nullptr;
  if (!mtlLoadVulkanLoader())
   {fprintf(stderr, "mtlBackend: vulkan loader not found — "
                     "GLFW-layer handshake unavailable (headless fallback)\n");
    return 2;
   }
  if (scale <= 0.0) scale = 1.0;

  // (Device/swapchain entry points are resolved via GIPA *after* the instance
  // is created — see below. The loader's GIPA(NULL, ...) returns NULL for
  // device-level functions.)

  // GLFW-required instance extensions (VK_KHR_surface + VK_EXT_metal_surface),
  // cross-checked against what this loader actually exposes (a missing platform
  // ext would otherwise hard-fail vkCreateInstance with an opaque error).
  uint32_t nReq = 0;
  const char** req = glfwGetRequiredInstanceExtensions(&nReq);
  if (!req)
   {fprintf(stderr, "mtlBackend: glfwGetRequiredInstanceExtensions NULL "
                     "(GLFW loader unavailable) — headless fallback\n");
    return 4;
   }
  uint32_t nProp = 0;
  gVkEnumInstExt(nullptr, &nProp, nullptr);
  std::vector<VkExtensionProperties> props(nProp);
  if (nProp) gVkEnumInstExt(nullptr, &nProp, props.data());
  auto exposed = [&](const char* name) -> bool
   {for (const auto& p : props) if (!strcmp(p.extensionName, name)) return true;
    return false;
   };
  std::vector<const char*> enable;
  for (uint32_t i = 0; i < nReq; i++)
   {if (exposed(req[i])) enable.push_back(req[i]);
    else fprintf(stderr, "mtlBackend: skip %s (loader does not expose)\n",
                 req[i]);
   }
  // MoltenVK is a "portability driver": the loader refuses to enumerate
  // devices unless VK_KHR_portability_enumeration is enabled AND the
  // VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR flag is set.
  if (exposed(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
    enable.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

  VkApplicationInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  ai.pApplicationName = "XAsteroids";
  VkInstanceCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ici.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  ici.pApplicationInfo = &ai;
  ici.enabledExtensionCount = (uint32_t)enable.size();
  ici.ppEnabledExtensionNames = enable.empty() ? nullptr : enable.data();
  VkInstance instance = VK_NULL_HANDLE;
  if (gVkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS)
   {fprintf(stderr, "mtlBackend: vkCreateInstance failed — "
                     "headless fallback\n");
    return 5;
   }

  // Resolve entry points via the loader's GIPA. Device-level functions
  // (vkEnumeratePhysicalDevices, vkCreateDevice, vkCreateSwapchainKHR, ...)
  // REQUIRE a valid instance — the loader's GIPA(NULL, ...) returns NULL for
  // them, so this must happen now that the instance exists.
  PFN_vkEnumeratePhysicalDevices            enumPhys =
    (PFN_vkEnumeratePhysicalDevices)gVkGIPA(instance, "vkEnumeratePhysicalDevices");
  PFN_vkGetPhysicalDeviceQueueFamilyProperties getQFam =
    (PFN_vkGetPhysicalDeviceQueueFamilyProperties)gVkGIPA(instance,
        "vkGetPhysicalDeviceQueueFamilyProperties");
  PFN_vkEnumerateDeviceExtensionProperties  enumDevExt =
    (PFN_vkEnumerateDeviceExtensionProperties)gVkGIPA(instance,
        "vkEnumerateDeviceExtensionProperties");
  PFN_vkCreateDevice            createDevice =
    (PFN_vkCreateDevice)gVkGIPA(instance, "vkCreateDevice");
  PFN_vkGetDeviceQueue          getDevQueue =
    (PFN_vkGetDeviceQueue)gVkGIPA(instance, "vkGetDeviceQueue");
  PFN_vkCreateSwapchainKHR      createSwap =
    (PFN_vkCreateSwapchainKHR)gVkGIPA(instance, "vkCreateSwapchainKHR");
  PFN_vkDestroyDevice           destroyDevice =
    (PFN_vkDestroyDevice)gVkGIPA(instance, "vkDestroyDevice");
  PFN_vkDestroySurfaceKHR       destroySurface =
    (PFN_vkDestroySurfaceKHR)gVkGIPA(instance, "vkDestroySurfaceKHR");
  PFN_vkDestroyInstance         destroyInstance =
    (PFN_vkDestroyInstance)gVkGIPA(instance, "vkDestroyInstance");
  PFN_vkAcquireNextImageKHR     acquire =
    (PFN_vkAcquireNextImageKHR)gVkGIPA(instance, "vkAcquireNextImageKHR");
  PFN_vkQueuePresentKHR         present =
    (PFN_vkQueuePresentKHR)gVkGIPA(instance, "vkQueuePresentKHR");
  if (!enumPhys || !getQFam || !createDevice || !createSwap || !destroySurface)
   {fprintf(stderr, "mtlBackend: loader missing required entry points — "
                     "headless fallback\n");
    if (destroyInstance) destroyInstance(instance, nullptr);
    return 3;
   }

  // glfwCreateWindowSurface: GLFW installs its OWN CAMetalLayer on the view
  // (the only layer that gets a drawable pool on this macOS build).
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (glfwCreateWindowSurface(instance, window, nullptr, &surface)
      != VK_SUCCESS)
   {fprintf(stderr, "mtlBackend: glfwCreateWindowSurface failed — "
                     "headless fallback\n");
    destroyInstance(instance, nullptr);
    return 6;
   }
  NSView* view = (NSView*)glfwGetCocoaView(window);
  CALayer* vlayer = view ? [view layer] : nil;
  if (!vlayer || ![vlayer isKindOfClass:[CAMetalLayer class]])
   {fprintf(stderr, "mtlBackend: GLFW did not install a CAMetalLayer — "
                     "headless fallback\n");
    if (destroySurface) destroySurface(instance, surface, nullptr);
    destroyInstance(instance, nullptr);
    return 7;
   }
  CAMetalLayer* layer = (CAMetalLayer*)vlayer;

  // MoltenVK VkDevice + VkSwapchainKHR on the GLFW layer: the swapchain is
  // what makes the window server allocate the layer's drawable pool.
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  uint32_t ndev = 0;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  VkSwapchainKHR swap = VK_NULL_HANDLE;
  int haveSwap = 0;
  if (enumPhys(instance, &ndev, nullptr) == VK_SUCCESS && ndev > 0)
   {std::vector<VkPhysicalDevice> devs(ndev);
    if (enumPhys(instance, &ndev, devs.data()) == VK_SUCCESS && ndev > 0)
     {phys = devs[0];
uint32_t nf = 0;
       // vkGetPhysicalDeviceQueueFamilyProperties returns void (Vulkan 1.3+).
       getQFam(phys, &nf, nullptr);
       if (nf > 0)
        {std::vector<VkQueueFamilyProperties> fams(nf);
        getQFam(phys, &nf, fams.data());
        uint32_t qfam = 0; int found = 0;
        for (uint32_t i = 0; i < nf; i++)
         if (fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { qfam = i; found = 1; break; }
        if (found)
         {float prio = 1.0f;
          VkDeviceQueueCreateInfo qci{};
          qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
          qci.queueFamilyIndex = qfam; qci.queueCount = 1;
          qci.pQueuePriorities = &prio;
          // VK_KHR_swapchain must be device-enabled or vkCreateSwapchainKHR
          // is a silent no-op (driver fn pointer NULL -> fake VK_SUCCESS).
          std::vector<const char*> devExts;
          if (enumDevExt)
           {uint32_t nde = 0;
            if (enumDevExt(phys, nullptr, &nde, nullptr) == VK_SUCCESS && nde > 0)
             {std::vector<VkExtensionProperties> de(nde);
              enumDevExt(phys, nullptr, &nde, de.data());
              for (const auto& e : de)
               if (!strcmp(e.extensionName, "VK_KHR_swapchain"))
                devExts.push_back("VK_KHR_swapchain");
             }
          }
          VkDeviceCreateInfo dci{};
          dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
          dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
          dci.enabledExtensionCount = (uint32_t)devExts.size();
          dci.ppEnabledExtensionNames = devExts.empty() ? nullptr : devExts.data();
          if (createDevice(phys, &dci, nullptr, &device) == VK_SUCCESS)
            {getDevQueue(device, qfam, 0, &queue);
             uint32_t wpx = (uint32_t)(width  * scale + 0.5f);
             uint32_t hpx = (uint32_t)(height * scale + 0.5f);
             VkSwapchainCreateInfoKHR sci{};
             sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
             sci.surface = surface;
             sci.minImageCount = 2;
             sci.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
             sci.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
             sci.imageExtent = { wpx, hpx };
             sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
             sci.imageArrayLayers = 1;
             uint32_t qi = qfam;
             sci.queueFamilyIndexCount = 1; sci.pQueueFamilyIndices = &qi;
             sci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
             sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
             sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
             sci.clipped = VK_TRUE;
             if (createSwap(device, &sci, nullptr, &swap) == VK_SUCCESS)
              {haveSwap = 1;
               // Prime the pool with one acquire+present (MoltenVK calls
               // [layer nextDrawable] internally), mirroring the known-good
               // VK path. One black frame at init is acceptable.
               if (acquire && present)
                {uint32_t idx = 0;
                 if (acquire(device, swap, UINT32_MAX,
                             VK_NULL_HANDLE, VK_NULL_HANDLE, &idx)
                     == VK_SUCCESS)
                  {VkPresentInfoKHR pi{};
                   pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                   pi.swapchainCount = 1; pi.pSwapchains = &swap;
                   pi.pImageIndices = &idx;
present(queue, &pi);
                   }
                 }
               }
             }
           }
          }
         }
        }
   if (!haveSwap)
   {fprintf(stderr, "mtlBackend: no MoltenVK swapchain (no ICD/device) — "
                     "GLFW layer present but pool not primed; "
                     "headless fallback\n");
    if (device && destroyDevice) destroyDevice(device, nullptr);
    if (destroySurface) destroySurface(instance, surface, nullptr);
    destroyInstance(instance, nullptr);
    if (gLoader) { dlclose(gLoader); gLoader = nullptr; }
    gVkCreateInstance = nullptr;
    gVkEnumInstExt = nullptr;
    gVkGIPA = nullptr;
    return 8;
   }

  MtlGlfwVkState* st = new MtlGlfwVkState();
  st->loaderHandle = gLoader;
  st->instance = instance;
  st->physicalDevice = phys;
  st->device = device;
  st->queue = queue;
  st->swapchain = swap;
  st->surface = surface;
  if (outLayer) *outLayer = (void*)layer;
  if (outState) *outState = (void*)st;
  fprintf(stderr, "mtlBackend: GLFW-layer handshake OK (layer=%s extent=%ux%u)\n",
          [[layer class] description].UTF8String, (unsigned)(width*scale+0.5f),
          (unsigned)(height*scale+0.5f));
  return 0;
 }

void mtlGlfwShutdown(void* state)
 {
  if (!state) return;
  MtlGlfwVkState* st = (MtlGlfwVkState*)state;
  @autoreleasepool
   {
    PFN_vkDestroySwapchainKHR destroySwap =
      (PFN_vkDestroySwapchainKHR)(st->instance && gVkGIPA
          ? gVkGIPA(st->instance, "vkDestroySwapchainKHR") : nullptr);
    PFN_vkDestroyDevice destroyDevice =
      (PFN_vkDestroyDevice)(st->instance && gVkGIPA
          ? gVkGIPA(st->instance, "vkDestroyDevice") : nullptr);
    PFN_vkDestroySurfaceKHR destroySurface =
      (PFN_vkDestroySurfaceKHR)(st->instance && gVkGIPA
          ? gVkGIPA(st->instance, "vkDestroySurfaceKHR") : nullptr);
    PFN_vkDestroyInstance destroyInstance =
      (PFN_vkDestroyInstance)(st->instance && gVkGIPA
          ? gVkGIPA(st->instance, "vkDestroyInstance") : nullptr);
    if (st->swapchain && destroySwap && st->device)
      destroySwap(st->device, st->swapchain, nullptr);
    if (st->device && destroyDevice)
      destroyDevice(st->device, nullptr);
    if (st->surface != VK_NULL_HANDLE && destroySurface && st->instance)
      destroySurface(st->instance, st->surface, nullptr);
    if (st->instance && destroyInstance)
      destroyInstance(st->instance, nullptr);
    // The CAMetalLayer is owned by the view — never released here.
   }
  // dlclose only if this state owns the (last reference to) the loader.
  if (st->loaderHandle == gLoader && st->loaderHandle)
   {dlclose(st->loaderHandle);
    gLoader = nullptr;
    gVkCreateInstance = nullptr;
    gVkEnumInstExt = nullptr;
    gVkGIPA = nullptr;
   }
  delete st;
 }

} // extern "C"