// probe_glfw_metal_layer.cpp — introspect GLFW's own CAMetalLayer (the path MoltenVK uses)
// 1) GLFW NO_API window 2) vkCreateInstance (cross-checked exts + optional debug utils)
// 3) glfwCreateWindowSurface 4) dump the layer GLFW installed on the view
// 5) hammer nextDrawable on it for 30s 6) if drawables flow: minimal red-clear VK swapchain
#include <vulkan/vulkan.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <vector>

static double nowsec() { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec*1e-9; }

static VKAPI_ATTR VkBool32 VKAPI_CALL dbgCb(VkDebugUtilsMessageSeverityFlagBitsEXT sev,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* d, void*) {
  if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    fprintf(stderr, "[VKMSG sev=%d] %s\n", (int)sev, d ? d->pMessage : "?");
  return VK_FALSE;
}

int main() {
  if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 2; }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* win = glfwCreateWindow(688, 702, "GLFWMetalProbe", nullptr, nullptr);
  if (!win) { fprintf(stderr, "glfwCreateWindow failed\n"); return 2; }

  uint32_t nExt = 0;
  const char** exts = glfwGetRequiredInstanceExtensions(&nExt);
  fprintf(stderr, "required instance exts (%u):", nExt);
  for (uint32_t i = 0; i < nExt; i++) fprintf(stderr, " %s", exts[i]);
  fprintf(stderr, "\n");
  uint32_t nprop = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &nprop, nullptr);
  std::vector<VkExtensionProperties> props(nprop);
  vkEnumerateInstanceExtensionProperties(nullptr, &nprop, props.data());
  fprintf(stderr, "loader exposes %u instance exts:", nprop);
  for (auto& p : props) fprintf(stderr, " %s", p.extensionName);
  fprintf(stderr, "\n");
  auto exposed = [&](const char* name) {
    for (auto& p : props) if (!strcmp(p.extensionName, name)) return true;
    return false;
  };
  std::vector<const char*> enable;
  for (uint32_t i = 0; i < nExt; i++) {
    if (exposed(exts[i])) enable.push_back(exts[i]);
    else fprintf(stderr, "skipping %s (loader does not expose)\n", exts[i]);
  }
  if (exposed("VK_KHR_portability_enumeration")) enable.push_back("VK_KHR_portability_enumeration");
  bool haveDebug = exposed("VK_EXT_debug_utils");
  VkDebugUtilsMessengerCreateInfoEXT mci = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
  if (haveDebug) {
    mci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
    mci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    mci.pfnUserCallback = dbgCb;
    enable.push_back("VK_EXT_debug_utils");
  }
  VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
  ai.pApplicationName = "GLFWMetalProbe";
  VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
  ici.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  ici.pApplicationInfo = &ai;
  ici.enabledExtensionCount = enable.size();
  ici.ppEnabledExtensionNames = enable.data();
  if (haveDebug) ici.pNext = &mci;
  VkInstance inst;
  VkResult rc = vkCreateInstance(&ici, nullptr, &inst);
  if (rc != VK_SUCCESS) { fprintf(stderr, "vkCreateInstance FAILED rc=%d\n", (int)rc); return 2; }
  fprintf(stderr, "vkCreateInstance OK\n");

  VkSurfaceKHR surface;
  if (glfwCreateWindowSurface(inst, win, nullptr, &surface) != VK_SUCCESS) {
    fprintf(stderr, "glfwCreateWindowSurface FAILED\n"); return 1;
  }
  fprintf(stderr, "glfwCreateWindowSurface OK\n");

  // --- introspect the layer GLFW installed ---
  NSView* view = (NSView*)glfwGetCocoaView(win);
  CALayer* vlayer = [view layer];
  fprintf(stderr, "view=%s inWindow=%s viewClass=%s\n",
          view ? "yes" : "no", (view && [view superview]) ? "yes" : "no",
          view ? [[view class] description].UTF8String : "?");
  fprintf(stderr, "view.layer class=%s superlayer=%s\n",
          vlayer ? [[vlayer class] description].UTF8String : "nil",
          vlayer && [vlayer superlayer] ? "SET" : "nil");
  if (!(vlayer && [vlayer isKindOfClass:[CAMetalLayer class]])) {
    fprintf(stderr, "view.layer is NOT a CAMetalLayer — cannot continue\n");
    return 1;
  }
  CAMetalLayer* ml = (CAMetalLayer*)vlayer;
  CGRect f = [ml frame];
  CGSize ds = [ml drawableSize];
  fprintf(stderr, "GLFW LAYER: pixelFormat=%lu ds=(%.0f,%.0f) cs=%.1f dsync=%s maxDrawables=%lu pWt=%s frame=(%.0f,%.0f,%.0f,%.0f)\n",
          (unsigned long)[ml pixelFormat], ds.width, ds.height, (double)[ml contentsScale],
          [ml displaySyncEnabled] ? "YES" : "no", (unsigned long)[ml maximumDrawableCount],
          [ml presentsWithTransaction] ? "YES" : "no",
          f.origin.x, f.origin.y, f.size.width, f.size.height);

  long nonNil = 0;
  double t0 = nowsec();
  for (int tick = 0; nowsec() - t0 < 30.0; tick++) {
    @autoreleasepool {
      id<CAMetalDrawable> d = [ml nextDrawable];
      if (d) nonNil++;
    }
    if (tick % 10 == 0) fprintf(stderr, "[t=%ds] glfw-layer nextDrawable nonNil=%ld\n", tick / 10, nonNil);
    glfwPollEvents();
    usleep(100000);
  }
  fprintf(stderr, "GLFW LAYER 30s: nonNil=%ld\n", nonNil);
  if (nonNil == 0) { fprintf(stderr, "CONCLUSION: even GLFW's own layer yields no drawables\n"); return 1; }

  // --- minimal Vulkan red-clear swapchain proof ---
  uint32_t ndev = 0;
  vkEnumeratePhysicalDevices(inst, &ndev, nullptr);
  if (ndev < 1) { fprintf(stderr, "no physical devices\n"); return 2; }
  std::vector<VkPhysicalDevice> devs(ndev);
  vkEnumeratePhysicalDevices(inst, &ndev, devs.data());
  VkPhysicalDevice dev = devs[0];
  uint32_t nfam = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(dev, &nfam, nullptr);
  std::vector<VkQueueFamilyProperties> fams(nfam);
  vkGetPhysicalDeviceQueueFamilyProperties(dev, &nfam, fams.data());
  int qfam = -1;
  VkBool32 presentSupp = 0;
  for (uint32_t i = 0; i < nfam; i++)
    if ((fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &presentSupp) == VK_SUCCESS && presentSupp) { qfam = i; break; }
  if (qfam < 0) { fprintf(stderr, "no graphics+present queue family\n"); return 2; }
  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
  qci.queueFamilyIndex = (uint32_t)qfam; qci.queueCount = 1; qci.pQueuePriorities = &prio;
  VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
  dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
  VkDevice dvc;
  if (vkCreateDevice(dev, &dci, nullptr, &dvc) != VK_SUCCESS) { fprintf(stderr, "vkCreateDevice failed\n"); return 2; }
  VkQueue q; vkGetDeviceQueue(dvc, (uint32_t)qfam, 0, &q);
  uint32_t nfmt = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &nfmt, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(nfmt ? nfmt : 1);
  vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &nfmt, formats.data());
  if (nfmt == 0) formats[0] = { VK_FORMAT_B8G8R8A8_UNORM, VK_COLORSPACE_SRGB_NONLINEAR_KHR };
  VkExtent2D ext = { 1376, 1404 };
  uint32_t qfidx = (uint32_t)qfam;
  VkSwapchainCreateInfoKHR sci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
  sci.surface = surface;
  sci.minImageCount = 2;
  sci.imageFormat = formats[0].format;
  sci.imageColorSpace = formats[0].colorSpace;
  sci.imageExtent = ext;
  sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  sci.queueFamilyIndexCount = 1;
  sci.pQueueFamilyIndices = &qfidx;
  sci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  sci.clipped = VK_TRUE;
  VkSwapchainKHR sc;
  if (vkCreateSwapchainKHR(dvc, &sci, nullptr, &sc) != VK_SUCCESS) { fprintf(stderr, "vkCreateSwapchain failed\n"); return 2; }
  uint32_t nimg = 0;
  vkGetSwapchainImagesKHR(dvc, sc, &nimg, nullptr);
  std::vector<VkImage> imgs(nimg);
  vkGetSwapchainImagesKHR(dvc, sc, &nimg, imgs.data());
  std::vector<VkImageView> ivs(nimg);
  for (uint32_t i = 0; i < nimg; i++) {
    VkImageViewCreateInfo ivci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ivci.image = imgs[i];
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = formats[0].format;
    VkImageSubresourceRange r; r.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    r.baseMipLevel = 0; r.levelCount = 1; r.baseArrayLayer = 0; r.layerCount = 1;
    ivci.subresourceRange = r;
    if (vkCreateImageView(dvc, &ivci, nullptr, &ivs[i]) != VK_SUCCESS) { fprintf(stderr, "vkCreateImageView failed\n"); return 2; }
  }
  fprintf(stderr, "swapchain: %u images fmt=%d\n", nimg, (int)formats[0].format);
  VkAttachmentDescription ad = {};
  ad.format = formats[0].format; ad.samples = VK_SAMPLE_COUNT_1_BIT;
  ad.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; ad.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  ad.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; ad.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  VkAttachmentReference ar = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
  VkSubpassDescription sp = {};
  sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sp.colorAttachmentCount = 1; sp.pColorAttachments = &ar;
  VkRenderPass rp;
  VkRenderPassCreateInfo rpci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
  rpci.attachmentCount = 1; rpci.pAttachments = &ad;
  rpci.subpassCount = 1; rpci.pSubpasses = &sp;
  vkCreateRenderPass(dvc, &rpci, nullptr, &rp);
  VkFramebuffer fbs[4];
  for (uint32_t i = 0; i < nimg && i < 4; i++) {
    VkFramebufferCreateInfo fci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fci.renderPass = rp; fci.width = ext.width; fci.height = ext.height; fci.layers = 1;
    fci.attachmentCount = 1; fci.pAttachments = &ivs[i];
    vkCreateFramebuffer(dvc, &fci, nullptr, &fbs[i]);
  }
  VkCommandPool cp;
  VkCommandPoolCreateInfo cpci2 = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
  cpci2.queueFamilyIndex = (uint32_t)qfam;
  vkCreateCommandPool(dvc, &cpci2, nullptr, &cp);
  VkClearValue cc; cc.color = { 1.0f, 0.1f, 0.1f, 1.0f };
  VkRenderPassBeginInfo rpbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
  rpbi.renderPass = rp; rpbi.clearValueCount = 1; rpbi.pClearValues = &cc;
  long presented = 0;
  double t1 = nowsec();
  int frames = 0;
  while (nowsec() - t1 < 20.0 && frames < 320) {
    uint32_t idx;
    if (vkAcquireNextImageKHR(dvc, sc, UINT64_MAX, VK_NULL_HANDLE, VK_NULL_HANDLE, &idx) != VK_SUCCESS) break;
    VkCommandBuffer cb;
    VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cai.commandPool = cp; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
    vkAllocateCommandBuffers(dvc, &cai, &cb);
    VkCommandBufferBeginInfo cbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(cb, &cbi);
    rpbi.framebuffer = fbs[idx];
    rpbi.renderArea = { { 0, 0 }, ext };
    vkCmdBeginRenderPass(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(cb);
    vkEndCommandBuffer(cb);
    VkSemaphore sig;
    VkSemaphoreCreateInfo scici = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    vkCreateSemaphore(dvc, &scici, nullptr, &sig);
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    si.signalSemaphoreCount = 1; si.pSignalSemaphores = &sig;
    if (vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS) break;
    VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &sig;
    pi.swapchainCount = 1; pi.pSwapchains = &sc;
    pi.pImageIndices = &idx;
    if (vkQueuePresentKHR(q, &pi) != VK_SUCCESS) break;
    vkDestroySemaphore(dvc, sig, nullptr);
    presented++;
    frames++;
    usleep(33000);
  }
  vkQueueWaitIdle(q);
  fprintf(stderr, "VK SWAPCHAIN: %ld frames presented in 20s\n", presented);
  return presented > 0 ? 0 : 1;
}