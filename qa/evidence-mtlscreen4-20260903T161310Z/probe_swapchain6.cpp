// probe6.cpp — SWAPCHAIN hypothesis: GLFW-layer + MoltenVK VkDevice+VkSwapchainKHR
// (bound to the GLFW CAMetalLayer) then Metal [layer nextDrawable] loop.
// Also dumps detailed layer/view geometry to resolve the "ds=560" anomaly.
//
// Build (NO -lvulkan):
//   xcrun -sdk macosx clang++ -x objective-c++ -std=c++17 -Ivendor/glfw -Ivendor/vulkan/include \
//     probe6.cpp -L/opt/homebrew/opt/glfw/lib -Wl,-rpath,/opt/homebrew/opt/glfw/lib \
//     -lglfw -framework Metal -framework QuartzCore -framework Foundation \
//     -framework AppKit -framework CoreGraphics -ldl -o probe6
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

static double nowsec(){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec+ts.tv_nsec*1e-9; }

// instance-level entry points
typedef PFN_vkCreateInstance fcInstance;
typedef PFN_vkDestroyInstance fdInstance;
typedef PFN_vkEnumerateInstanceExtensionProperties feExt;
typedef PFN_vkDestroySurfaceKHR fdSurface;
typedef PFN_vkGetInstanceProcAddr fpGIPA;
static fcInstance pCreateInstance; static fdInstance pDestroyInstance;
static feExt pEnumExt; static fdSurface pDestroySurface; static fpGIPA pGIPA;

// device/swapchain entry points (via GIPA)
static PFN_vkEnumeratePhysicalDevices pEnumPhys;
static PFN_vkGetPhysicalDeviceQueueFamilyProperties pGetQFam;
static PFN_vkCreateDevice pCreateDevice;
static PFN_vkGetDeviceQueue pGetDevQueue;
static PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR pGetSurfCaps;
static PFN_vkGetPhysicalDeviceSurfaceFormatsKHR pGetSurfFmts;
static PFN_vkCreateSwapchainKHR pCreateSwap;
static PFN_vkDestroySwapchainKHR pDestroySwap;
static PFN_vkDestroyDevice pDestroyDevice;
static PFN_vkQueueWaitIdle pQueueWaitIdle;
static PFN_vkEnumerateDeviceExtensionProperties pEnumDevExt;

static void* g_loader=NULL;
static int loadAll(){
  const char* cand[4]={"libvulkan.1.dylib","/opt/homebrew/lib/libvulkan.1.dylib",
    "/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib","/usr/local/lib/libvulkan.1.dylib"};
  for(int i=0;i<4;i++){ g_loader=dlopen(cand[i],RTLD_NOW|RTLD_LOCAL); if(g_loader) break; }
  if(!g_loader){ fprintf(stderr,"[loader] NOT FOUND\n"); return 0; }
  pCreateInstance=(fcInstance)dlsym(g_loader,"vkCreateInstance");
  pDestroyInstance=(fdInstance)dlsym(g_loader,"vkDestroyInstance");
  pEnumExt=(feExt)dlsym(g_loader,"vkEnumerateInstanceExtensionProperties");
  pDestroySurface=(fdSurface)dlsym(g_loader,"vkDestroySurfaceKHR");
  pGIPA=(fpGIPA)dlsym(g_loader,"vkGetInstanceProcAddr");
  if(!pCreateInstance||!pGIPA){ fprintf(stderr,"[loader] dlsym incomplete\n"); return 0; }
  return 1;
}
static void resolveGIPA(VkInstance inst){
  pEnumPhys=(PFN_vkEnumeratePhysicalDevices)pGIPA(inst,"vkEnumeratePhysicalDevices");
  pGetQFam=(PFN_vkGetPhysicalDeviceQueueFamilyProperties)pGIPA(inst,"vkGetPhysicalDeviceQueueFamilyProperties");
  pCreateDevice=(PFN_vkCreateDevice)pGIPA(inst,"vkCreateDevice");
  pGetDevQueue=(PFN_vkGetDeviceQueue)pGIPA(inst,"vkGetDeviceQueue");
  pGetSurfCaps=(PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)pGIPA(inst,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
  pGetSurfFmts=(PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)pGIPA(inst,"vkGetPhysicalDeviceSurfaceFormatsKHR");
  pCreateSwap=(PFN_vkCreateSwapchainKHR)pGIPA(inst,"vkCreateSwapchainKHR");
  pDestroySwap=(PFN_vkDestroySwapchainKHR)pGIPA(inst,"vkDestroySwapchainKHR");
  pDestroyDevice=(PFN_vkDestroyDevice)pGIPA(inst,"vkDestroyDevice");
  pQueueWaitIdle=(PFN_vkQueueWaitIdle)pGIPA(inst,"vkQueueWaitIdle");
  pEnumDevExt=(PFN_vkEnumerateDeviceExtensionProperties)pGIPA(inst,"vkEnumerateDeviceExtensionProperties");
}

int main(int argc,char**argv){
  const char* flp=getenv("DYLD_FALLBACK_LIBRARY_PATH"); const char* icd=getenv("VK_ICD_FILENAMES");
  double dur=16.0; const char* du=getenv("XAST_PROBE_DUR"); if(du&&du[0])dur=atof(du);
  int doSwap = !getenv("XAST_PROBE_NOSWAP");
  fprintf(stderr,"=== probe6 env: DYLD_FLP=%s VK_ICD=%s dur=%.0fs doSwap=%d ===\n",
          flp?flp:"(null)",icd?icd:"(null)",dur,doSwap);
  if(!loadAll()){ fprintf(stderr,"no loader\n"); return 3; }
  if(!glfwInit()){ fprintf(stderr,"glfwInit fail\n"); return 2; }
  glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API); glfwWindowHint(GLFW_VISIBLE,GLFW_TRUE);
  GLFWwindow* win=glfwCreateWindow(688,702,"XAstMTLProbe6",nullptr,nullptr);
  if(!win){ fprintf(stderr,"no window\n"); return 2; }

  uint32_t nExt=0; const char** req=glfwGetRequiredInstanceExtensions(&nExt);
  if(!req){ fprintf(stderr,"req exts NULL\n"); return 4; }
  uint32_t nprop=0; pEnumExt(nullptr,&nprop,nullptr);
  std::vector<VkExtensionProperties> props(nprop); if(nprop)pEnumExt(nullptr,&nprop,props.data());
  auto exposed=[&](const char*nm){ for(auto&p:props) if(!strcmp(p.extensionName,nm))return true; return false; };
  std::vector<const char*> en; for(uint32_t i=0;i<nExt;i++) if(exposed(req[i])) en.push_back(req[i]);
  if(exposed("VK_KHR_portability_enumeration")) en.push_back("VK_KHR_portability_enumeration");
  VkApplicationInfo ai={VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.pApplicationName="XAstMTLProbe6";
  VkInstanceCreateInfo ici={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.flags=VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR; ici.pApplicationInfo=&ai;
  ici.enabledExtensionCount=(uint32_t)en.size(); ici.ppEnabledExtensionNames=en.empty()?nullptr:en.data();
  VkInstance inst; if(pCreateInstance(&ici,nullptr,&inst)!=VK_SUCCESS){ fprintf(stderr,"vkCreateInstance FAIL\n"); return 5; }
  resolveGIPA(inst);
  VkSurfaceKHR surface=VK_NULL_HANDLE;
  if(glfwCreateWindowSurface(inst,win,nullptr,&surface)!=VK_SUCCESS){ fprintf(stderr,"glfwCreateWindowSurface FAIL\n"); return 6; }
  NSView* view=(NSView*)glfwGetCocoaView(win);
  CAMetalLayer* ml=(CAMetalLayer*)([view layer]);
  if(!ml){ fprintf(stderr,"no metal layer\n"); return 7; }

  // ---- geometry introspection ----
  fprintf(stderr,"VIEW: frame=(%.0f,%.0f,%.0f,%.0f) bounds=(%.0f,%.0f,%.0f,%.0f) wantsLayer=%d\n",
      view.frame.origin.x,view.frame.origin.y,view.frame.size.width,view.frame.size.height,
      view.bounds.origin.x,view.bounds.origin.y,view.bounds.size.width,view.bounds.size.height,
      (int)view.wantsLayer);
  fprintf(stderr,"LAYER: class=%s frame=(%.0f,%.0f,%.0f,%.0f) superlayer=%s contentsScale=%.2f drawableSize=(%.0f,%.0f) opaque=%d maxDrawables=%lu\n",
      [[ml class]description].UTF8String, ml.frame.origin.x,ml.frame.origin.y,ml.frame.size.width,ml.frame.size.height,
      [ml superlayer]?"set":"nil", (double)[ml contentsScale], [ml drawableSize].width,[ml drawableSize].height,
      (int)[ml isOpaque], (unsigned long)[ml maximumDrawableCount]);

  // Optional: create a MoltenVK VkDevice + VkSwapchainKHR bound to the GLFW layer.
  VkDevice device=VK_NULL_HANDLE; VkSwapchainKHR swap=VK_NULL_HANDLE; VkQueue q=VK_NULL_HANDLE;
  if(doSwap){
    uint32_t ndev=0; if(!pEnumPhys||pEnumPhys(inst,&ndev,nullptr)!=VK_SUCCESS||ndev<1){
      fprintf(stderr,"[swap] no physical devices (ndev=%u) -> cannot swapchain\n",(unsigned)ndev);
    } else {
      std::vector<VkPhysicalDevice> devs(ndev); pEnumPhys(inst,&ndev,devs.data());
      VkPhysicalDevice dev=devs[0];
      uint32_t nf=0; pGetQFam(dev,&nf,nullptr); std::vector<VkQueueFamilyProperties> fams(nf); pGetQFam(dev,&nf,fams.data());
      uint32_t qfam=0; int found=0;
      for(uint32_t i=0;i<nf;i++) if(fams[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){ qfam=i; found=1; break; }
      if(!found){ fprintf(stderr,"[swap] no graphics queue family\n"); }
      else {
        float prio=1.0f; VkDeviceQueueCreateInfo qci={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex=qfam; qci.queueCount=1; qci.pQueuePriorities=&prio;
        // Query device-level extensions; enable VK_KHR_swapchain if the driver
        // exposes it (required for vkCreateSwapchainKHR to be a real call).
        std::vector<const char*> devExts;
        if(pEnumDevExt){
          uint32_t nde=0; pEnumDevExt(dev,nullptr,&nde,nullptr);
          if(nde){ std::vector<VkExtensionProperties> de(nde); pEnumDevExt(dev,nullptr,&nde,de.data());
            for(auto&e:de) if(!strcmp(e.extensionName,"VK_KHR_swapchain")) devExts.push_back("VK_KHR_swapchain");
            fprintf(stderr,"[swap] device exposes %u exts; swapchain_enabled=%d\n",nde,(int)!devExts.empty());
          } else fprintf(stderr,"[swap] device exposes 0 exts\n");
        }
        VkDeviceCreateInfo dci={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.queueCreateInfoCount=1; dci.pQueueCreateInfos=&qci;
        dci.enabledExtensionCount=(uint32_t)devExts.size();
        dci.ppEnabledExtensionNames=devExts.empty()?nullptr:devExts.data();
        if(pCreateDevice(dev,&dci,nullptr,&device)!=VK_SUCCESS){ fprintf(stderr,"[swap] vkCreateDevice FAIL\n"); device=VK_NULL_HANDLE; }
        else {
          pGetDevQueue(device,qfam,0,&q);
          uint32_t nfmt=0; pGetSurfFmts(dev,surface,&nfmt,nullptr);
          std::vector<VkSurfaceFormatKHR> fmts(nfmt?nfmt:1); if(nfmt)pGetSurfFmts(dev,surface,&nfmt,fmts.data());
          if(!nfmt) fmts[0]={VK_FORMAT_B8G8R8A8_UNORM,VK_COLORSPACE_SRGB_NONLINEAR_KHR};
          VkSurfaceCapabilitiesKHR caps; pGetSurfCaps(dev,surface,&caps);
          VkExtent2D ext={1376,1404};
          uint32_t qi=(uint32_t)qfam;
          VkSwapchainCreateInfoKHR sci={VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
          sci.surface=surface; sci.minImageCount=2; sci.imageFormat=fmts[0].format;
          sci.imageColorSpace=fmts[0].colorSpace; sci.imageExtent=ext;
          sci.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
          sci.imageArrayLayers=1; sci.queueFamilyIndexCount=1; sci.pQueueFamilyIndices=&qi;
          sci.preTransform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
          sci.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
          sci.presentMode=VK_PRESENT_MODE_FIFO_KHR; sci.clipped=VK_TRUE;
          VkResult src=pCreateSwap(device,&sci,nullptr,&swap);
          fprintf(stderr,"[swap] vkCreateSwapchain rc=%d fmt=%d caps.curExtent=(%u,%u) minImg=%u\n",
                  (int)src,(int)fmts[0].format,caps.currentExtent.width,caps.currentExtent.height,caps.minImageCount);
          if(src==VK_SUCCESS){
            // Nudge the pool: acquire one image through the swapchain (MoltenVK calls
            // [layer nextDrawable] internally), then present it back.
            uint32_t idx; PFN_vkAcquireNextImageKHR pAcq=(PFN_vkAcquireNextImageKHR)pGIPA(inst,"vkAcquireNextImageKHR");
            PFN_vkQueuePresentKHR pPres=(PFN_vkQueuePresentKHR)pGIPA(inst,"vkQueuePresentKHR");
            if(pAcq(device,swap,UINT32_MAX,VK_NULL_HANDLE,VK_NULL_HANDLE,&idx)==VK_SUCCESS){
              VkPresentInfoKHR pi={VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
              pi.swapchainCount=1; pi.pSwapchains=&swap; pi.pImageIndices=&idx;
              pPres(q,&pi);
              fprintf(stderr,"[swap] acquired+presented img %u via MoltenVK\n",idx);
            } else fprintf(stderr,"[swap] vkAcquireNextImage FAILED\n");
          }
        }
      }
    }
  }

  // Re-set drawable size AFTER swapchain (mirror the game: it sets it in the
  // framebuffer callback). Then Metal nextDrawable loop.
  [ml setDrawableSize:CGSizeMake(1376,1404)];
  fprintf(stderr,"after swapchain: drawableSize=(%.0f,%.0f)\n",[ml drawableSize].width,[ml drawableSize].height);
  id<MTLDevice> dev=MTLCreateSystemDefaultDevice(); id<MTLCommandQueue> cq=[dev newCommandQueue];

  long presented=0,nNil=0; double t0=nowsec(); int tick=0;
  while(nowsec()-t0<dur){
    tick++;
    @autoreleasepool{
      id<CAMetalDrawable> d=[ml nextDrawable];
      if(d){
        id<MTLCommandBuffer> cb=[cq commandBuffer];
        MTLRenderPassDescriptor* desc=[MTLRenderPassDescriptor renderPassDescriptor];
        desc.colorAttachments[0].texture=d.texture;
        desc.colorAttachments[0].loadAction=MTLLoadActionClear;
        desc.colorAttachments[0].storeAction=MTLStoreActionStore;
        desc.colorAttachments[0].clearColor=MTLClearColorMake(0.15,1.0,0.15,1.0); // BRIGHT GREEN
        id<MTLRenderCommandEncoder> e=[cb renderCommandEncoderWithDescriptor:desc];
        [e endEncoding]; [cb presentDrawable:d]; [cb commit];
        presented++;
        if(presented%20==0) fprintf(stderr,"[t=%.1fs] METAL presented=%ld\n",nowsec()-t0,presented);
      } else { nNil++; if(nNil%40==0) fprintf(stderr,"[t=%.1fs] nil=%ld presented=%ld\n",nowsec()-t0,nNil,presented); }
    }
    glfwPollEvents(); usleep(16000);
  }
  fprintf(stderr,"=== probe6 RESULT: presented=%ld nil=%ld (swap=%d) ===\n",presented,nNil,doSwap);
  if(presented>0) fprintf(stderr,">>> METAL nextDrawable FLOWS (swapchain=%d)\n",doSwap);
  else fprintf(stderr,">>> NO drawables (swapchain=%d)\n",doSwap);
  if(pDestroySwap&&swap) pDestroySwap(device,swap,nullptr);
  if(pDestroyDevice&&device) pDestroyDevice(device,nullptr);
  if(surface&&pDestroySurface) pDestroySurface(inst,surface,nullptr);
  if(pDestroyInstance) pDestroyInstance(inst,nullptr);
  glfwDestroyWindow(win); glfwTerminate();
  return presented>0?0:1;
}