#ifndef _XAST_VKBACKEND_H
#define _XAST_VKBACKEND_H

// ============================================================================
// vkBackend.H — Vulkan RenderingEngine backend (Phase 4, tasks 37-38 scope).
//
// Task 37 landed PASS 0: instance + logical device + debug/layer probing,
// fully surfaceless-capable. Task 38 lands PASS 2: GLFW_NO_API window +
// VkSurfaceKHR + present-capable queue + swapchain + per-image views.
// Tasks 39-42 add frame sync/pipelines/engine methods at the marked append
// points — do NOT shuffle this file's order blindly.
//
// Task-38 restructure (documented per the handover spec): pass 0 is split
// internally into initInstance_() (instance + physical device + probes +
// debug messenger) and initDevice_(bool withSwapchain) (logical device).
// The public entry points keep their task-37 signatures:
//   - initInstanceAndDevice(bool) = initInstance_ + initDevice_(false):
//     byte-equivalent to the task-37 standalone behavior (device with ZERO
//     extensions — the surfaceless probe needs no swapchain).
//   - initWindow(title): initInstance_(true) -> pass 1 fonts -> GLFW window
//     -> surface -> initDevice_(true) [VK_KHR_swapchain ENABLED here, after
//     the surface exists so vkGetPhysicalDeviceSurfaceSupportKHR can pick a
//     present-capable family] -> swapchain -> image views.
//   - DELIBERATE REORDER: the debug messenger is now created immediately
//     after the instance (was: after the device). Required by this task's
//     "0 ERROR-level validation messages during surface/swapchain init"
//     gate — without it nothing observes messages between instance and
//     messenger creation. Strictly more capture coverage than task 37.
//
//   - ONE public init entry point: initInstanceAndDevice(bool
//     withGlfwExtensions). initWindow(title) calls it with true and returns;
//     task 38 inserts pass 1 (stb fonts) + pass 2 (GLFW_NO_API window +
//     surface + swapchain) at the TODO(task 38) markers below.
//   - probeStandalone() runs the SAME pass 0 surfaceless (no glfwInit, zero
//     surface extensions) — driven by obj/VK/vkprobe (test/vk/vkprobe.C).
//   - VkResult is checked at EVERY vulkan call (vkCheck helper); any failure
//     makes init return false -> main() exit(1) semantics like GL.
//   - m15 conditional validation: VK_LAYER_KHRONOS_validation is enabled ONLY
//     when probed-present on the instance side (vkEnumerateInstanceLayerProperties);
//     the device side is probed too (vkEnumerateDeviceLayerProperties) and the
//     result recorded/reported. Absent layer => clean continue WITHOUT
//     validation (never a hard failure). XAST_VK_VALIDATION=off forces off.
//   - VK_EXT_debug_utils: messenger created iff the extension is available
//     (proc-addr lookup, since the loader exports the EXT entry points only
//     through vkGetInstanceProcAddr). The same create-info is chained via
//     pNext so messages emitted DURING vkCreateInstance are captured too.
//   - Validation-error counting (machine-specific gotcha, see learnings.md):
//     the counter increments ONLY for ERROR severity whose pMessageIdName
//     starts with "VUID-" or "UNASSIGNED" (KHRONOS_validation convention).
//     Broken third-party implicit layers make the LOADER emit ERROR-severity
//     messages with id "Loader Message" — those are logged tagged
//     [vk-loader-error:...] and NEVER counted, or machines with e.g. a broken
//     liblsfg-vk layer could never pass a 0-errors gate.
//   - Graphics queue family: first family with VK_QUEUE_GRAPHICS_BIT.
//     Present-support checking is DEFERRED to task 38 (needs a surface —
//     vkGetPhysicalDeviceSurfaceSupportKHR lands there).
//   - VK_KHR_swapchain: availability probed + recorded (swapchainAvailable_)
//     but NOT enabled. Device is created with ZERO extensions; task 38 must
//     enable VK_KHR_swapchain in ITS device creation (or recreate).
//   - shutdown order: messenger -> device -> instance (task 38 inserts
//     swapchain -> surface -> window BEFORE the device teardown).
//
// Resource ownership map (D15, VK rows, tasks 37-38):
//   | Resource                | Owner    | Note                             |
//   |-------------------------|----------|----------------------------------|
//   | VkInstance              | VKBackend| created pass 0; destroyed last   |
//   | VkDebugUtilsMessengerEXT| VKBackend| iff debug_utils; before instance |
//   | VkPhysicalDevice        | (none)   | owned by the instance            |
//   | VkDevice                | VKBackend| created after surface (38);      |
//   | GLFWwindow              | VKBackend| pass 2a; dies before the device  |
//   | VkSurfaceKHR            | VKBackend| pass 2a; after window, b4 device |
//   | VkSwapchainKHR + views  | VKBackend| pass 2b; FIRST things destroyed  |
//
// Zero X11 includes; zero census symbols (F1-neutral new GPU code).
// NO backend macro: nothing here defines or tests VK_BACKEND (domain stays
// macro-free per playingField.H's GL-bridge precedent).
// ============================================================================

#include<stdlib.h>
#include<stdio.h>
#include<string.h>
#include<unistd.h>        // readlink/ssize_t (pass-1 font path resolution)
#include<vector>

#include<vulkan/vulkan.h> // vendor/vulkan (pin 1:1.4.357.0-1 == installed loader)
#include<GLFW/glfw3.h>    // system glfw3 3.5.1 — pass 0 queries
                          // glfwGetRequiredInstanceExtensions when asked for
                          // window-surface extensions (standalone probe does
                          // NOT glfwInit; the TU still references glfw symbols,
                          // hence obj/VK/vkprobe links -lglfw).

#include"renderingEngine.H"
#include"windowSize.H"    // WindowSizeFormula::Compute + EngineFontId enum
#include"stb_truetype.h"  // vendor/stb v1.26; DECLARATIONS only here — the
                          // implementation TU is obj/*/stbTruetypeImpl.o
                          // (glBackend.H precedent; linked into the VK leg
                          // from task 38 for pass-1 font metrics).

#define kValidationLayerName "VK_LAYER_KHRONOS_validation"

#if VK_HEADER_VERSION < 357
#error "vkBackend.H expects vendored Vulkan headers 1.4.357 (vendor/vulkan); wrong include path?"
#endif

class VKBackend : public RenderingEngine
 {public:
    VKBackend()
      :instance(VK_NULL_HANDLE),
        physicalDevice(VK_NULL_HANDLE),
        device(VK_NULL_HANDLE),
        debugMessenger(VK_NULL_HANDLE),
        graphicsFamily(UINT32_MAX),
        presentFamily(UINT32_MAX),
        deviceApiVersion_(0),deviceType_(0),
        swapchainAvailable_(false),
        debugUtilsAvailable_(false),
        instanceLayerPresent_(false),
        deviceLayerPresent_(false),
        validationEnabled_(false),
        validationErrorCount_(0),
        loaderErrorCount_(0),
        glfwInitialized_(false),
        window(NULL),
        surface(VK_NULL_HANDLE),
        swapchain(VK_NULL_HANDLE),
        graphicsQueue(VK_NULL_HANDLE),
        presentQueue(VK_NULL_HANDLE),
        swapchainImageCount_(0),
        swapchainFormat_(VK_FORMAT_UNDEFINED),
        swapchainColorSpace_(VK_COLOR_SPACE_SRGB_NONLINEAR_KHR),
        swapchainExtent_{0,0},
        presentFamilyMatchesGraphics_(false),
        swapchainDirty_(false),
        closeRequested_(false),
        fbWidth_(0),fbHeight_(0),
        canonicalWidth_(0),canonicalHeight_(0),
        shutdownDone_(false),
        vkDestroyDebugUtilsMessengerEXT_(NULL),
        canonicalPlayAreaWidth_(0),canonicalPlayAreaHeight_(0),
        canonicalMaxIconHeight_(0)
      {deviceName_[0]='\0';
       for (int i=0;i<4;++i)
        {fontInfo_[i]=NULL;fontData_[i]=NULL;
         fontDataLen_[i]=0;fontScale_[i]=0.0f;maxAsciiAdvance_[i]=0.0f;
        }
       s_instance=this;   // debug callback routes here (exactly one engine)
      }

    virtual ~VKBackend()
      {shutdown();
      }

    // Transition configuration — identical contract to GLBackend's setter:
    // play-area dims + max icon height come from game constants this header
    // cannot include; main() calls it BEFORE initWindow. Recorded pass 0;
    // consumed by task 38's swapchain sizing.
    inline void setCanonicalLayout(int playAreaWidth,int playAreaHeight,
                                   int maxIconHeight)
      {canonicalPlayAreaWidth_=playAreaWidth;
       canonicalPlayAreaHeight_=playAreaHeight;
       canonicalMaxIconHeight_=maxIconHeight;
      }

    // ------------------------------------------------------------------
    // Pass 0 (task 37, task-38 split): public entry points.
    //   initInstanceAndDevice(withGlfwExtensions): instance + device with
    //   ZERO extensions — the surfaceless/standalone shape (probe path).
    //   initWindow() is the windowed path: instance -> window/surface ->
    //   initDevice_(true) so VK_KHR_swapchain rides the real device.
    // ------------------------------------------------------------------
    bool initInstanceAndDevice(bool withGlfwExtensions)
      {if (!initInstance_(withGlfwExtensions))
         return false;
       return initDevice_(false);
      }

    // Surfaceless pass 0 (no glfwInit, zero surface extensions) — the
    // obj/VK/vkprobe driver (test/vk/vkprobe.C) calls this and prints the
    // report straight from the public recorded-fact members below.
    bool probeStandalone()
      {return initInstanceAndDevice(false);
      }

   private:
    // ------------------------------------------------------------------
    // initInstance_: glfwInit (when asked) + layer/extension probes +
    // instance + physical-device pick + graphics-family enumeration +
    // debug messenger (created immediately after the instance so messages
    // from every later call are captured). Idempotent via instance guard.
    // ------------------------------------------------------------------
    bool initInstance_(bool withGlfwExtensions)
      {if (instance!=VK_NULL_HANDLE)
         return true;

       // XAST_VK_VALIDATION=off: documented escape hatch forcing the off
       // branch even when the layer IS present (m15 evidence tooling).
       const char* valEnv=getenv("XAST_VK_VALIDATION");
       bool wantValidation=(valEnv==NULL||strcmp(valEnv,"off")!=0);

       if (withGlfwExtensions)
        {// ---- GLFW: library init + required-instance-extension query ----
         if (glfwInit()!=GLFW_TRUE)
           {fprintf(stderr,"vkBackend: glfwInit failed\n");
            return false;
           }
         glfwInitialized_=true;
        }

       // ---- m15 probe: instance-side layers ---------------------------
       uint32_t nlayers=0;
       if (!vkCheck(vkEnumerateInstanceLayerProperties(&nlayers,NULL),
                    "vkEnumerateInstanceLayerProperties(count)"))
         return false;
       std::vector<VkLayerProperties> layers(nlayers);
       if (nlayers>0
           && !vkCheck(vkEnumerateInstanceLayerProperties(&nlayers,
                        layers.data()),
                       "vkEnumerateInstanceLayerProperties"))
         return false;
       instanceLayerPresent_=layerInList(kValidationLayerName,
                                         layers.data(),nlayers);

       // ---- instance extensions: availability probe --------------------
       // TRAP (header API): vkEnumerateInstanceExtensionProperties takes
       // pLayerName FIRST, then pCount — natural C instinct puts the count
       // first and g++ rejects it.
       uint32_t next=0;
       if (!vkCheck(vkEnumerateInstanceExtensionProperties(NULL,&next,NULL),
                    "vkEnumerateInstanceExtensionProperties(count)"))
         return false;
       std::vector<VkExtensionProperties> iext(next);
       if (next>0
           && !vkCheck(vkEnumerateInstanceExtensionProperties(NULL,&next,
                        iext.data()),
                       "vkEnumerateInstanceExtensionProperties"))
         return false;
       debugUtilsAvailable_=extInList(VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
                                      iext.data(),next);

       // Enabled-set assembly. GLFW-required extensions are intersected
       // against the probed-available list: each is enabled only when the
       // loader actually exposes it (a missing platform ext would otherwise
       // hard-fail vkCreateInstance with an opaque error).
       std::vector<const char*> exts;
       std::vector<const char*> lay;
       if (withGlfwExtensions&&glfwInitialized_)
        {uint32_t ng=0;
         const char** req=glfwGetRequiredInstanceExtensions(&ng);
         if (req)
          {for (uint32_t i=0;i<ng;++i)
            {if (extInList(req[i],iext.data(),next))
               exts.push_back(req[i]);
             else
               fprintf(stderr,"vkBackend: GLFW requests %s but loader does "
                               "not expose it — skipped\n",req[i]);
            }
          }
        }
       if (debugUtilsAvailable_)
         exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

       // m15 branch: enable the validation layer ONLY when probed-present.
       if (wantValidation&&!instanceLayerPresent_)
         fprintf(stderr,"vkBackend: %s not present — continuing WITHOUT "
                         "validation (clean continue, m15)\n",
                         kValidationLayerName);
       validationEnabled_=wantValidation&&instanceLayerPresent_;
       if (validationEnabled_)
         lay.push_back(kValidationLayerName);

       // ---- instance creation -----------------------------------------
       VkApplicationInfo app{};
       app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;
       app.pApplicationName="XAsteroids";
       app.applicationVersion=VK_MAKE_VERSION(6,1,0);
       app.pEngineName="XAstRendering";
       app.engineVersion=VK_MAKE_VERSION(1,0,0);
       app.apiVersion=VK_API_VERSION_1_4;   // == pinned header/loader version

       VkDebugUtilsMessengerCreateInfoEXT cbInfo{};
       if (debugUtilsAvailable_)
        {cbInfo.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
         cbInfo.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                                |VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
         cbInfo.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                           |VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                           |VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
         cbInfo.pfnUserCallback=&vkDebugCallback;
         cbInfo.pUserData=this;
        }

       VkInstanceCreateInfo ici{};
       ici.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
       if (debugUtilsAvailable_)
         ici.pNext=&cbInfo;   // capture messages emitted DURING creation
       ici.pApplicationInfo=&app;
       ici.enabledExtensionCount=(uint32_t)exts.size();
       ici.ppEnabledExtensionNames=exts.empty()?NULL:exts.data();
       ici.enabledLayerCount=(uint32_t)lay.size();
       ici.ppEnabledLayerNames=lay.empty()?NULL:lay.data();
       if (!vkCheck(vkCreateInstance(&ici,NULL,&instance),"vkCreateInstance"))
         return false;

       // ---- physical device pick --------------------------------------
       uint32_t ndev=0;
       if (!vkCheck(vkEnumeratePhysicalDevices(instance,&ndev,NULL),
                    "vkEnumeratePhysicalDevices(count)"))
         return false;
       if (ndev<1)
         {fprintf(stderr,"vkBackend: no Vulkan physical devices\n");
          return false;
         }
       std::vector<VkPhysicalDevice> devs(ndev);
       if (!vkCheck(vkEnumeratePhysicalDevices(instance,&ndev,devs.data()),
                    "vkEnumeratePhysicalDevices"))
         return false;
       physicalDevice=devs[0];
       for (uint32_t i=1;i<ndev;++i)      // prefer discrete (RTX 5090 box)
        {if (deviceIsDiscrete(devs[i])&&!deviceIsDiscrete(physicalDevice))
           physicalDevice=devs[i];
        }
       VkPhysicalDeviceProperties dp;
       vkGetPhysicalDeviceProperties(physicalDevice,&dp);
       deviceApiVersion_=dp.apiVersion;
       deviceType_=(uint32_t)dp.deviceType;
       snprintf(deviceName_,sizeof deviceName_,"%s",dp.deviceName);

       // ---- m15 probe: device-side layers ------------------------------
       uint32_t ndl=0;
       if (!vkCheck(vkEnumerateDeviceLayerProperties(physicalDevice,&ndl,
                     NULL),
                    "vkEnumerateDeviceLayerProperties(count)"))
         return false;
       std::vector<VkLayerProperties> dlayers(ndl);
       if (ndl>0
           && !vkCheck(vkEnumerateDeviceLayerProperties(physicalDevice,
                        &ndl,dlayers.data()),
                       "vkEnumerateDeviceLayerProperties"))
         return false;
       deviceLayerPresent_=layerInList(kValidationLayerName,
                                       dlayers.data(),ndl);
       if (validationEnabled_&&!deviceLayerPresent_)
         fprintf(stderr,"vkBackend: WARNING %s enumerated on the instance "
                         "but NOT on the device\n",
                         kValidationLayerName);

       // ---- device extensions: swapchain AVAILABILITY only (recorded,
       //      NOT enabled — task 38 enables it in ITS device creation) ----
       uint32_t ndex=0;
       if (!vkCheck(vkEnumerateDeviceExtensionProperties(physicalDevice,
                     NULL,&ndex,NULL),
                    "vkEnumerateDeviceExtensionProperties(count)"))
         return false;
       std::vector<VkExtensionProperties> dext(ndex);
       if (ndex>0
           && !vkCheck(vkEnumerateDeviceExtensionProperties(physicalDevice,
                        NULL,&ndex,dext.data()),
                       "vkEnumerateDeviceExtensionProperties"))
         return false;
       swapchainAvailable_=extInList(VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                     dext.data(),ndex);

       // ---- graphics queue family (present-support: task 38, needs a
       //      surface for vkGetPhysicalDeviceSurfaceSupportKHR) ----------
       uint32_t nq=0;
       vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice,&nq,NULL);
       std::vector<VkQueueFamilyProperties> qs(nq);
       vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice,&nq,
                                                qs.data());
       graphicsFamily=UINT32_MAX;
       for (uint32_t i=0;i<nq;++i)
        {if (qs[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)
          {graphicsFamily=i;
           break;
          }
        }
       if (graphicsFamily==UINT32_MAX)
         {fprintf(stderr,"vkBackend: no graphics-capable queue family on "
                         "%s\n",deviceName_);
          return false;
         }

       // ---- debug messenger (immediately post-instance, proc-addr
       //      lookup; BEFORE device/surface/swapchain so this task's
       //      0-errors gate observes every message those calls emit) -----
       if (debugUtilsAvailable_)
        {auto create=(PFN_vkCreateDebugUtilsMessengerEXT)
           vkGetInstanceProcAddr(instance,
                                 "vkCreateDebugUtilsMessengerEXT");
         auto destroy=(PFN_vkDestroyDebugUtilsMessengerEXT)
           vkGetInstanceProcAddr(instance,
                                 "vkDestroyDebugUtilsMessengerEXT");
         if (create&&destroy)
          {if (!vkCheck(create(instance,&cbInfo,NULL,&debugMessenger),
                        "vkCreateDebugUtilsMessengerEXT"))
             return false;
           vkDestroyDebugUtilsMessengerEXT_=destroy;
          }
         else
           fprintf(stderr,"vkBackend: %s advertised but EXT entry points "
                           "did not resolve — continuing without messenger\n",
                           VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

       return true;
      }

    // ------------------------------------------------------------------
    // initDevice_: present-family resolution (needs the surface when
    // withSwapchain) + logical device + queue handles. withSwapchain=false
    // reproduces task 37 exactly: zero extensions, graphics family only.
    // Idempotent via device guard.
    // ------------------------------------------------------------------
    bool initDevice_(bool withSwapchain)
      {if (device!=VK_NULL_HANDLE)
         return true;

       if (withSwapchain)
        {if (surface==VK_NULL_HANDLE)
           {fprintf(stderr,"vkBackend: swapchain requested without a "
                           "surface\n");
            return false;
           }
         if (!swapchainAvailable_)
           {fprintf(stderr,"vkBackend: %s not supported by %s — cannot "
                           "present\n",VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                           deviceName_);
            return false;
           }
         // Present support on the task-37 graphics family first; only if it
         // lacks present support fall back to graphics+present, then to any
         // present-capable family (separate present queue).
         VkBool32 sup=VK_FALSE;
         if (!vkCheck(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice,
                      graphicsFamily,surface,&sup),
                      "vkGetPhysicalDeviceSurfaceSupportKHR"))
           return false;
         if (sup)
          {presentFamily=graphicsFamily;
           presentFamilyMatchesGraphics_=true;
          }
         else
          {uint32_t nqf=0;
           vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice,&nqf,
                                                    NULL);
           std::vector<VkQueueFamilyProperties> qfs(nqf);
           vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice,&nqf,
                                                    qfs.data());
           for (uint32_t i=0;i<nqf&&presentFamily==UINT32_MAX;++i)
            {if (i==graphicsFamily)
               continue;
             VkBool32 s=VK_FALSE;
             if (!vkCheck(vkGetPhysicalDeviceSurfaceSupportKHR(
                          physicalDevice,i,surface,&s),
                          "vkGetPhysicalDeviceSurfaceSupportKHR"))
               return false;
             if (s&&(qfs[i].queueFlags&VK_QUEUE_GRAPHICS_BIT))
              {presentFamily=i;
               break;
              }
            }
           for (uint32_t i=0;i<nqf&&presentFamily==UINT32_MAX;++i)
            {VkBool32 s=VK_FALSE;
             if (!vkCheck(vkGetPhysicalDeviceSurfaceSupportKHR(
                          physicalDevice,i,surface,&s),
                          "vkGetPhysicalDeviceSurfaceSupportKHR"))
               return false;
             if (s)
              {presentFamily=i;
               break;
              }
            }
           if (presentFamily==UINT32_MAX)
             {fprintf(stderr,"vkBackend: no present-capable queue family "
                             "for the surface on %s\n",deviceName_);
              return false;
             }
          }
        }

       // ---- logical device: VK_KHR_swapchain iff surface-backed --------
       float prio=1.0f;
       VkDeviceQueueCreateInfo qci[2]{};
       uint32_t nqci=1;
       qci[0].sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
       qci[0].queueFamilyIndex=graphicsFamily;
       qci[0].queueCount=1;
       qci[0].pQueuePriorities=&prio;
       if (withSwapchain&&presentFamily!=graphicsFamily)
        {qci[1].sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
         qci[1].queueFamilyIndex=presentFamily;
         qci[1].queueCount=1;
         qci[1].pQueuePriorities=&prio;
         nqci=2;
        }
       const char* swapchainExt=VK_KHR_SWAPCHAIN_EXTENSION_NAME;
       VkDeviceCreateInfo dci{};
       dci.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
       dci.queueCreateInfoCount=nqci;
       dci.pQueueCreateInfos=qci;
       if (withSwapchain)
        {dci.enabledExtensionCount=1;
         dci.ppEnabledExtensionNames=&swapchainExt;
        }
       else
        {dci.enabledExtensionCount=0;   // standalone probe shape (task 37)
         dci.ppEnabledExtensionNames=NULL;
        }
       if (!vkCheck(vkCreateDevice(physicalDevice,&dci,NULL,&device),
                    "vkCreateDevice"))
         return false;

       vkGetDeviceQueue(device,graphicsFamily,0,&graphicsQueue);
       if (withSwapchain)
         vkGetDeviceQueue(device,presentFamily,0,&presentQueue);

       return true;
      }

   public:

    // ------------------------------------------------------------------
    // (1) Window lifecycle — D15 two-pass, VK shape:
    //   pass 0  initInstance_(true): glfwInit + instance (+GLFW surface
    //           extensions) + physical device + probes + messenger
    //   pass 1  loadFonts_: four stb TTF slots (metrics feed the shared
    //           WindowSizeFormula; glyph rendering stays task 42)
    //   pass 2a createWindowAndSurface_: GLFW_NO_API window + VkSurfaceKHR
    //   pass 0' initDevice_(true): logical device WITH VK_KHR_swapchain,
    //           present family resolved against the live surface
    //   pass 2b createSwapchain_: swapchain + per-image views
    // ------------------------------------------------------------------
    virtual bool initWindow(const char* title) override
      {if (!initInstance_(true))
         return false;
       if (!loadFonts_())
        {shutdown();
         return false;
        }
       if (!createWindowAndSurface_(title))
        {shutdown();
         return false;
        }
       if (!initDevice_(true))
        {shutdown();
         return false;
        }
       if (!createSwapchain_())
        {shutdown();
         return false;
        }
       return true;
      }

    virtual void shutdown() override
      {// Exactly-once guard (M13/D15): safe from dtor AND close-callback path.
       if (shutdownDone_)
         return;
       shutdownDone_=true;
       // D15 release order (task-38 extension): swapchain views -> swapchain
       // -> surface -> window, THEN the task-37 chain: device -> messenger
       // -> instance -> GLFW library.
       for (size_t i=0;i<swapchainImageViews_.size();++i)
         vkDestroyImageView(device,swapchainImageViews_[i],NULL);
       swapchainImageViews_.clear();
       swapchainImages_.clear();
       if (swapchain!=VK_NULL_HANDLE)
         {vkDestroySwapchainKHR(device,swapchain,NULL);
          swapchain=VK_NULL_HANDLE;
         }
       if (surface!=VK_NULL_HANDLE)
         {vkDestroySurfaceKHR(instance,surface,NULL);
          surface=VK_NULL_HANDLE;
         }
       if (window)
         {glfwDestroyWindow(window);
          window=NULL;
         }
       for (int i=0;i<4;++i)
        {if (fontData_[i])
          {delete[] fontData_[i];
           fontData_[i]=NULL;
          }
         fontDataLen_[i]=0;
         if (fontInfo_[i])
          {delete fontInfo_[i];
           fontInfo_[i]=NULL;
          }
        }
       if (device!=VK_NULL_HANDLE)
         {vkDestroyDevice(device,NULL);
          device=VK_NULL_HANDLE;
         }
       if (debugMessenger!=VK_NULL_HANDLE
           &&vkDestroyDebugUtilsMessengerEXT_)
         {vkDestroyDebugUtilsMessengerEXT_(instance,debugMessenger,NULL);
          debugMessenger=VK_NULL_HANDLE;
         }
       if (instance!=VK_NULL_HANDLE)
         {vkDestroyInstance(instance,NULL);
          instance=VK_NULL_HANDLE;
         }
       if (glfwInitialized_)
         {glfwTerminate();
          glfwInitialized_=false;
         }
      }

    virtual X11NativeHandle nativeHandle() const override
      {// D9 seam: X11-only consumer (Motif Options dialog). Undefined/unused
       // on VK by design (documented no-op).
       X11NativeHandle h;
       h.display=nullptr;
       h.window=0;
       return h;
      }

    // ------------------------------------------------------------------
    // (2) Frame lifecycle — benign seams until tasks 38/42
    // ------------------------------------------------------------------
    virtual void beginFrame() override
      {// TODO(task 39): acquire (finite 100ms timeout) + render-pass begin.
       // The swapchainDirty_ flag set by the framebuffer-size callback is
       // consumed by task 39's re-bootstrap — nothing else touches it here.
      }

    virtual void endFrame() override
      {// TODO(task 39/42): queue submit + present land here.
      }

    // ------------------------------------------------------------------
    // (3) Seams
    // ------------------------------------------------------------------
    virtual int pollEvents(GameEvent* out,int maxCount) override
      {(void)out;(void)maxCount;
       // Benign until the task-25-reuse/menu integration wires the D16 seam
       // (GL precedent: underlying pump lives in beginFrame once real).
       return 0;
      }

    virtual void setScissorRect(const int* xywh) override
      {(void)xywh;   // TODO(task 42): dynamic scissor state
      }

    virtual RenderTargetId createRenderTarget(int w,int h) override
      {(void)w;(void)h;
       return 0;      // TODO(task 42): off-screen VkImage+framebuffer
      }

    virtual void beginRenderTo(RenderTargetId target) override
      {(void)target; // TODO(task 42)
      }

    virtual void endRenderTo() override
      {// TODO(task 42)
      }

    virtual void getPresentTransform(float& outScale,int& outOffsetX,
                                     int& outOffsetY) override
      {// Identity while the swapchain extent equals the window size (task
       // 38 always creates it that way). The D17.4a recompute hook is
       // recomputePresentTransform_ (called from the framebuffer-size
       // callback); a real letterbox transform lands with task 40's render
       // passes, driven by swapchainDirty_.
       outScale=1.0f;outOffsetX=0;outOffsetY=0;
      }

    // ------------------------------------------------------------------
    // (4) Primitives
    // ------------------------------------------------------------------
    virtual void clear() override
      {// Benign no-op (render-pass clear owns this on VK from task 38).
      }

    virtual void drawLine(float x1,float y1,float x2,float y2,
                          float r,float g,float b,float width) override
      {(void)x1;(void)y1;(void)x2;(void)y2;(void)r;(void)g;(void)b;(void)width;
       notImplemented();
      }

    virtual void drawPolygon(const float* verts,int count,
                             float r,float g,float b,bool fill) override
      {(void)verts;(void)count;(void)r;(void)g;(void)b;(void)fill;
       notImplemented();
      }

    virtual void drawRect(float x,float y,float w,float h,
                          float r,float g,float b,bool fill) override
      {(void)x;(void)y;(void)w;(void)h;(void)r;(void)g;(void)b;(void)fill;
       notImplemented();
      }

    virtual void drawTriangles(const float* verts,int count,
                               TextureId tex) override
      {(void)verts;(void)count;(void)tex;
       notImplemented();
      }

    // ------------------------------------------------------------------
    // (5) Text
    // ------------------------------------------------------------------
    virtual void drawStringOpaque(const char* text,float x,float y,int fontId,
                                  float r,float g,float b,
                                  float bgR,float bgG,float bgB) override
      {(void)text;(void)x;(void)y;(void)fontId;
       (void)r;(void)g;(void)b;(void)bgR;(void)bgG;(void)bgB;
       notImplemented();
      }

    virtual void drawStringTransparent(const char* text,float x,float y,
                                       int fontId,
                                       float r,float g,float b) override
      {(void)text;(void)x;(void)y;(void)fontId;(void)r;(void)g;(void)b;
       notImplemented();
      }

    virtual float measureText(const char* text,int fontId) override
      {// Advance-width sum over the loaded TTF slot (stb metrics; glBackend
       // verbatim semantics). Total-width centering policy per D12/M5.
       // Returns 0 for invalid slot/text like the X11 backend. Glyph
       // RASTERIZATION stays task 42 — metrics exist from task 38 because
       // the shared window-size formula consumes them.
       if (!text||fontId<0||fontId>3||!fontInfo_[fontId]||!fontData_[fontId])
         return 0.0f;
       float total=0.0f;
       for (const char* p=text;*p;++p)
        {int advance=0,lbearing=0;
         stbtt_GetCodepointHMetrics(fontInfo_[fontId],(unsigned char)*p,
                                    &advance,&lbearing);
         total+=advance*fontScale_[fontId];
        }
       return total;
      }

    virtual void getFontMetrics(int fontId,FontMetrics& out) override
      {// stb metrics mapped onto FontMetrics incl. per-font max_bounds
       // (glBackend verbatim semantics; D12 signature-level requirement).
       if (fontId<0||fontId>3||!fontInfo_[fontId]||!fontData_[fontId])
        {out.ascent=out.descent=out.lbearing=0.0f;
         out.max_bounds.ascent=out.max_bounds.descent=out.max_bounds.width=0.0f;
         return;
        }
       float s=fontScale_[fontId];
       int ascent=0,descent=0,lineGap=0;
       stbtt_GetFontVMetrics(fontInfo_[fontId],&ascent,&descent,&lineGap);
       out.ascent=ascent*s;
       out.descent=-descent*s;             // stb descent is <=0; X11-style positive
       out.lbearing=0.0f;                  // conservative font-level value
       out.max_bounds.ascent=ascent*s;
       out.max_bounds.descent=-descent*s;
       out.max_bounds.width=maxAsciiAdvance_[fontId];
      }

    // ------------------------------------------------------------------
    // (6) Textures
    // ------------------------------------------------------------------
    virtual TextureId createTextureFromBitmap(const uint8_t* data,int w,
                                              int h,int channels) override
      {(void)data;(void)w;(void)h;(void)channels;
       notImplemented();
       return 0;      // not reached
      }

    virtual void drawTexture(TextureId tex,float x,float y,float w,float h,
                             float alpha) override
      {(void)tex;(void)x;(void)y;(void)w;(void)h;(void)alpha;
       notImplemented();
      }

    virtual void drawTextureMasked(TextureId content,TextureId mask,
                                   float x,float y,float w,float h) override
      {(void)content;(void)mask;(void)x;(void)y;(void)w;(void)h;
       notImplemented();
      }

    virtual void deleteTexture(TextureId tex) override
      {(void)tex;    // benign: nothing can be created yet (create aborts)
      }

    virtual TextureId createTextureFromRGBA32(const uint8_t* pixels,int w,
                                              int h) override
      {(void)pixels;(void)w;(void)h;
       notImplemented();
       return 0;      // not reached
      }

    // ------------------------------------------------------------------
    // (7) Transform
    // ------------------------------------------------------------------
    virtual void setTransform(float tx,float ty,float angleRadians) override
      {(void)tx;(void)ty;(void)angleRadians;   // TODO(task 42): push constants
      }

    virtual void resetTransform() override
      {// TODO(task 42)
      }

    // ------------------------------------------------------------------
    // Recorded pass-0/pass-2 facts (public: the vkprobe/vksurface drivers
    // print the report straight from these; naming follows the task-38
    // handover notes).
    // ------------------------------------------------------------------
    VkInstance instance;                 // VK_NULL_HANDLE until pass 0
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkDebugUtilsMessengerEXT debugMessenger;
    uint32_t graphicsFamily;             // UINT32_MAX until picked
    uint32_t presentFamily;              // UINT32_MAX until surface-backed pick
    uint32_t deviceApiVersion_;          // pick results (task 38 reads these)
    uint32_t deviceType_;                // VkPhysicalDeviceType raw value
    char deviceName_[256];
    bool swapchainAvailable_;            // probed + recorded, NOT enabled
    bool debugUtilsAvailable_;
    bool instanceLayerPresent_;          // m15: instance-side KHRONOS probe
    bool deviceLayerPresent_;            // m15: device-side KHRONOS probe
    bool validationEnabled_;             // actual enabled decision
    unsigned validationErrorCount_;      // VUID-/UNASSIGNED ERRORs ONLY
    unsigned loaderErrorCount_;          // non-VUID ERROR noise (never gated)
    bool glfwInitialized_;
    GLFWwindow* window;                  // pass 2a; NULL until initWindow
    VkSurfaceKHR surface;                // pass 2a
    VkSwapchainKHR swapchain;            // pass 2b
    VkQueue graphicsQueue;               // fetched post-device
    VkQueue presentQueue;                // == graphicsQueue when families match
    uint32_t swapchainImageCount_;       // recorded-fact: >=2 asserted by QA
    VkFormat swapchainFormat_;           // recorded-fact (rank-order pick)
    VkColorSpaceKHR swapchainColorSpace_;
    VkExtent2D swapchainExtent_;         // recorded-fact: == framebuffer size
    bool presentFamilyMatchesGraphics_;  // sharing-mode decision input
    bool swapchainDirty_;                // framebuffer-size callback flag
                                         // (task 39 re-bootstrap consumes it)
    bool closeRequested_;                // M13: close callback sets flag only
    int fbWidth_,fbHeight_;              // last-known framebuffer size
    int canonicalWidth_,canonicalHeight_; // WindowSizeFormula result (the
                                         // requested client size)

   private:
    bool shutdownDone_;
    PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT_;
    int canonicalPlayAreaWidth_,canonicalPlayAreaHeight_;
    int canonicalMaxIconHeight_;
    stbtt_fontinfo* fontInfo_[4];        // slots 0..3 = EngineFont 0..3
    uint8_t* fontData_[4];
    long fontDataLen_[4];
    float fontScale_[4];
    float maxAsciiAdvance_[4];
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;

    static VKBackend* s_instance;        // callback routing (single instance)

    bool vkCheck(VkResult rc,const char* what)
      {if (rc<0)
        {fprintf(stderr,"vkBackend: %s failed: %d (%s)\n",what,(int)rc,
                 vkResultName(rc));
         return false;
        }
       return true;
      }

    // ------------------------------------------------------------------
    // Pass 1 (D15): four stb TTF slots — glBackend::loadFonts verbatim
    // semantics (same files, same pixel sizes, same /proc/self/exe path
    // resolution). Metrics only; glyph rasterization is task 42.
    // ------------------------------------------------------------------
    bool loadFonts_()
      {static char fontDir[4096];
       static bool fontDirInit=false;
       if (!fontDirInit)
        {ssize_t n=readlink("/proc/self/exe",fontDir,sizeof fontDir-1);
         if (n>0)
          {fontDir[n]='\0';
           char* slash=strrchr(fontDir,'/');
           if (slash)
             *slash='\0';                 // strip binary name -> repo root
          }
         else
           strcpy(fontDir,".");
         fontDirInit=true;
        }
       static const char* const names[4]=
        {"/vendor/fonts/DejaVuSans-Bold.ttf",
         "/vendor/fonts/DejaVuSans-Bold.ttf",
         "/vendor/fonts/DejaVuSans-Bold.ttf",
         "/vendor/fonts/DejaVuSansMono-Bold.ttf"};
       static const float pixelSizes[4]={40.0f,10.0f,20.0f,40.0f};
       static const char* const slotNames[4]=
        {"title","button","hi-score","score"};
       char path[4200];
       for (int i=0;i<4;++i)
        {snprintf(path,sizeof path,"%s%s",fontDir,names[i]);
         FILE* f=fopen(path,"rb");
         if (!f)
          {fprintf(stderr,"vkBackend: cannot open %s (slot %s).\n",
                   path,slotNames[i]);
           return false;
          }
         fseek(f,0,SEEK_END);
         long len=ftell(f);
         fseek(f,0,SEEK_SET);
         uint8_t* buf=new uint8_t[len];
         if (fread(buf,1,(size_t)len,f)!=(size_t)len)
          {fclose(f);delete[] buf;
           fprintf(stderr,"vkBackend: short read on %s.\n",path);
           return false;
          }
         fclose(f);
         int offset=stbtt_GetFontOffsetForIndex(buf,0);
         fontInfo_[i]=new stbtt_fontinfo;
         if (offset<0||
             !stbtt_InitFont(fontInfo_[i],buf,offset))
          {delete[] buf;
           fprintf(stderr,"vkBackend: stb_init failed on %s.\n",path);
           return false;
          }
         fontData_[i]=buf;
         fontDataLen_[i]=len;
         fontScale_[i]=stbtt_ScaleForPixelHeight(fontInfo_[i],pixelSizes[i]);
         float maxAdv=0.0f;               // max ASCII advance (32..126)
         for (int cp=32;cp<=126;++cp)
          {int advance=0,bearing=0;
           stbtt_GetCodepointHMetrics(fontInfo_[i],cp,&advance,&bearing);
           float a=advance*fontScale_[i];
           if (a>maxAdv)
             maxAdv=a;
          }
         maxAsciiAdvance_[i]=maxAdv;
        }
       return true;
      }

    // ------------------------------------------------------------------
    // Pass 2a: GLFW_NO_API window sized by the SHARED D15 formula
    // (WindowSizeFormula::Compute — identical inputs to glBackend pass 2
    // and Stage), root-centered per the stage.H pattern, then
    // glfwCreateWindowSurface. Callbacks registered here: framebuffer-size
    // dirties swapchainDirty_ (task 39 consumes), close sets a flag only.
    // ------------------------------------------------------------------
    bool createWindowAndSurface_(const char* title)
      {FontMetrics titleFm,hiScoreFm,scoreFm;
       getFontMetrics(EngineFont_Title,titleFm);
       getFontMetrics(EngineFont_HiScore,hiScoreFm);
       getFontMetrics(EngineFont_Score,scoreFm);
       int titleTextWidth=(int)measureText("Asteroids",EngineFont_Title);
       WindowSizeFormula::Compute(titleFm,hiScoreFm,scoreFm,titleTextWidth,
                                  canonicalPlayAreaWidth_,
                                  canonicalPlayAreaHeight_,
                                  canonicalMaxIconHeight_,
                                  canonicalWidth_,canonicalHeight_);
       glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);   // Vulkan: no GL context
       glfwWindowHint(GLFW_RESIZABLE,GLFW_TRUE);      // v6/S2: freely resizable
       glfwWindowHint(GLFW_VISIBLE,GLFW_TRUE);
       window=glfwCreateWindow(canonicalWidth_,canonicalHeight_,title,
                               nullptr,nullptr);
       if (!window)
        {fprintf(stderr,"vkBackend: glfwCreateWindow failed.\n");
         return false;
        }
       GLFWmonitor* mon=glfwGetPrimaryMonitor();
       if (mon)
        {const GLFWvidmode* mode=glfwGetVideoMode(mon);
         if (mode)
           glfwSetWindowPos(window,
                            (mode->width-canonicalWidth_)/2,
                            (mode->height-canonicalHeight_)/2);
        }
       glfwSetFramebufferSizeCallback(window,framebufferSizeTrampoline_);
       glfwSetWindowCloseCallback(window,windowCloseTrampoline_);
       glfwGetFramebufferSize(window,&fbWidth_,&fbHeight_);
       VkResult rc=glfwCreateWindowSurface(instance,window,NULL,&surface);
       if (!vkCheck(rc,"glfwCreateWindowSurface"))
        {surface=VK_NULL_HANDLE;
         return false;
        }
       return true;
      }

    // ------------------------------------------------------------------
    // Pass 2b: swapchain + per-image views.
    //   format: rank order (B8G8R8A8_UNORM,SRGB_NONLINEAR) >
    //           (B8G8R8A8_SRGB,SRGB_NONLINEAR) > first enumerated
    //           (VK_FORMAT_UNDEFINED list => B8G8R8A8_UNORM).
    //   present mode: FIFO (spec-guaranteed; availability verified anyway).
    //   image count: want 3, clamped into [minImageCount,maxImageCount]
    //           (maxImageCount==0 means unlimited).
    //   extent: currentExtent when pinned (X11 WSI pins it), else the
    //           framebuffer size clamped into [minImageExtent,
    //           maxImageExtent]. Recorded as swapchainExtent_ == the window
    //           framebuffer size at creation (QA assert).
    // ------------------------------------------------------------------
    bool createSwapchain_()
      {VkSurfaceCapabilitiesKHR caps{};
       if (!vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice,
                    surface,&caps),
                    "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
         return false;
       if (!(caps.supportedUsageFlags&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        {fprintf(stderr,"vkBackend: surface lacks COLOR_ATTACHMENT usage\n");
         return false;
        }

       uint32_t nfmt=0;
       if (!vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice,
                    surface,&nfmt,NULL),
                    "vkGetPhysicalDeviceSurfaceFormatsKHR(count)"))
         return false;
       if (nfmt<1)
        {fprintf(stderr,"vkBackend: surface enumerates no formats\n");
         return false;
        }
       std::vector<VkSurfaceFormatKHR> fmts(nfmt);
       if (!vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice,
                    surface,&nfmt,fmts.data()),
                    "vkGetPhysicalDeviceSurfaceFormatsKHR"))
         return false;
       VkSurfaceFormatKHR chosen=fmts[0];
       bool ranked=false;
       if (chosen.format==VK_FORMAT_UNDEFINED)   // "any format" marker
        {chosen.format=VK_FORMAT_B8G8R8A8_UNORM;
         chosen.colorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
         ranked=true;
        }
       for (uint32_t i=0;i<nfmt&&!ranked;++i)
         if (fmts[i].format==VK_FORMAT_B8G8R8A8_UNORM
             &&fmts[i].colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
          {chosen=fmts[i];
           ranked=true;
          }
       for (uint32_t i=0;i<nfmt&&!ranked;++i)
         if (fmts[i].format==VK_FORMAT_B8G8R8A8_SRGB
             &&fmts[i].colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
          {chosen=fmts[i];
           ranked=true;
          }

       uint32_t npm=0;
       if (!vkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice,
                    surface,&npm,NULL),
                    "vkGetPhysicalDeviceSurfacePresentModesKHR(count)"))
         return false;
       std::vector<VkPresentModeKHR> pms(npm);
       if (!vkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice,
                    surface,&npm,pms.data()),
                    "vkGetPhysicalDeviceSurfacePresentModesKHR"))
         return false;
       bool fifo=false;
       for (uint32_t i=0;i<npm;++i)
         if (pms[i]==VK_PRESENT_MODE_FIFO_KHR)
           fifo=true;
       if (!fifo)
        {fprintf(stderr,"vkBackend: FIFO present mode absent (spec "
                        "violation by %s)\n",deviceName_);
         return false;
        }

       uint32_t imageCount=3;
       if (caps.maxImageCount>0&&imageCount>caps.maxImageCount)
         imageCount=caps.maxImageCount;
       if (imageCount<caps.minImageCount)
         imageCount=caps.minImageCount;

       VkExtent2D ext;
       if (caps.currentExtent.width!=UINT32_MAX)
         ext=caps.currentExtent;        // X11 WSI pins extent == window size
       else
        {ext.width=(uint32_t)fbWidth_;
         ext.height=(uint32_t)fbHeight_;
         if (ext.width<caps.minImageExtent.width)
           ext.width=caps.minImageExtent.width;
         if (ext.width>caps.maxImageExtent.width)
           ext.width=caps.maxImageExtent.width;
         if (ext.height<caps.minImageExtent.height)
           ext.height=caps.minImageExtent.height;
         if (ext.height>caps.maxImageExtent.height)
           ext.height=caps.maxImageExtent.height;
        }

       VkSwapchainCreateInfoKHR sci{};
       sci.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
       sci.surface=surface;
       sci.minImageCount=imageCount;
       sci.imageFormat=chosen.format;
       sci.imageColorSpace=chosen.colorSpace;
       sci.imageExtent=ext;
       sci.imageArrayLayers=1;
       sci.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
       uint32_t families[2]={graphicsFamily,presentFamily};
       if (presentFamily!=graphicsFamily)
        {sci.imageSharingMode=VK_SHARING_MODE_CONCURRENT;
         sci.queueFamilyIndexCount=2;
         sci.pQueueFamilyIndices=families;
        }
       else
        {sci.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;
        }
       sci.preTransform=caps.currentTransform;
       sci.compositeAlpha=
         (caps.supportedCompositeAlpha&VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
          ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
          : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
       sci.presentMode=VK_PRESENT_MODE_FIFO_KHR;
       sci.clipped=VK_TRUE;
       sci.oldSwapchain=VK_NULL_HANDLE; // re-bootstrap chains old handles (39)
       if (!vkCheck(vkCreateSwapchainKHR(device,&sci,NULL,&swapchain),
                    "vkCreateSwapchainKHR"))
         return false;

       uint32_t nimgs=0;
       if (!vkCheck(vkGetSwapchainImagesKHR(device,swapchain,&nimgs,NULL),
                    "vkGetSwapchainImagesKHR(count)"))
         return false;
       swapchainImages_.resize(nimgs);
       if (!vkCheck(vkGetSwapchainImagesKHR(device,swapchain,&nimgs,
                    swapchainImages_.data()),
                    "vkGetSwapchainImagesKHR"))
         return false;
       for (uint32_t i=0;i<nimgs;++i)
        {VkImageViewCreateInfo vci{};
         vci.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
         vci.image=swapchainImages_[i];
         vci.viewType=VK_IMAGE_VIEW_TYPE_2D;
         vci.format=chosen.format;
         vci.components.r=VK_COMPONENT_SWIZZLE_IDENTITY;
         vci.components.g=VK_COMPONENT_SWIZZLE_IDENTITY;
         vci.components.b=VK_COMPONENT_SWIZZLE_IDENTITY;
         vci.components.a=VK_COMPONENT_SWIZZLE_IDENTITY;
         vci.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
         vci.subresourceRange.levelCount=1;
         vci.subresourceRange.layerCount=1;
         VkImageView view=VK_NULL_HANDLE;
         if (!vkCheck(vkCreateImageView(device,&vci,NULL,&view),
                      "vkCreateImageView"))
          {return false;
          }
         swapchainImageViews_.push_back(view);
        }

       swapchainImageCount_=nimgs;
       swapchainFormat_=chosen.format;
       swapchainColorSpace_=chosen.colorSpace;
       swapchainExtent_=ext;
       swapchainDirty_=false;
       return true;
      }

    // D17.4a hook: the framebuffer-size callback records the new size and
    // marks the swapchain dirty; task 39's re-bootstrap consumes the flag
    // (re-create at the new extent + present-transform recompute). Nothing
    // else may clear or act on swapchainDirty_ until then.
    void recomputePresentTransform_()
      {// Identity transform while extent tracks the window exactly; the
       // letterbox math lands with task 40's render passes.
      }

    static void framebufferSizeTrampoline_(GLFWwindow* win,int w,int h)
      {(void)win;
       if (s_instance)
        {s_instance->fbWidth_=w;
         s_instance->fbHeight_=h;
         s_instance->swapchainDirty_=true;   // task 39 re-bootstraps
         s_instance->recomputePresentTransform_();
        }
      }

    static void windowCloseTrampoline_(GLFWwindow* win)
      {(void)win;
       if (s_instance)
         s_instance->closeRequested_=true;   // M13: flag only, no teardown
      }

    static bool deviceIsDiscrete(VkPhysicalDevice d)
      {VkPhysicalDeviceProperties p;
       vkGetPhysicalDeviceProperties(d,&p);
       return p.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
      }

    static bool layerInList(const char* name,const VkLayerProperties* l,
                            uint32_t n)
      {for (uint32_t i=0;i<n;++i)
        {if (strcmp(l[i].layerName,name)==0)
           return true;
        }
       return false;
      }

    static bool extInList(const char* name,const VkExtensionProperties* e,
                          uint32_t n)
      {for (uint32_t i=0;i<n;++i)
        {if (strcmp(e[i].extensionName,name)==0)
           return true;
        }
       return false;
      }

    [[noreturn]] static void notImplemented()
      {fprintf(stderr,"vkBackend: %s not implemented until task 42\n",
               __func__);
       abort();
      }

    // Minimal result-name helper: the C headers ship no
    // string_VkResult (that is an SDK/Hpp utility); cover the results this
    // backend can actually observe, numeric fallback otherwise.
    static const char* vkResultName(VkResult rc)
      {switch (rc)
        {case VK_SUCCESS:                   return "VK_SUCCESS";
         case VK_NOT_READY:                 return "VK_NOT_READY";
         case VK_TIMEOUT:                   return "VK_TIMEOUT";
         case VK_EVENT_SET:                 return "VK_EVENT_SET";
         case VK_EVENT_RESET:               return "VK_EVENT_RESET";
         case VK_INCOMPLETE:                return "VK_INCOMPLETE";
         case VK_ERROR_OUT_OF_HOST_MEMORY:  return "VK_ERROR_OUT_OF_HOST_MEMORY";
         case VK_ERROR_OUT_OF_DEVICE_MEMORY:return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
         case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
         case VK_ERROR_DEVICE_LOST:         return "VK_ERROR_DEVICE_LOST";
         case VK_ERROR_MEMORY_MAP_FAILED:   return "VK_ERROR_MEMORY_MAP_FAILED";
         case VK_ERROR_LAYER_NOT_PRESENT:   return "VK_ERROR_LAYER_NOT_PRESENT";
         case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
         case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
         case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
         case VK_ERROR_TOO_MANY_OBJECTS:    return "VK_ERROR_TOO_MANY_OBJECTS";
         case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
         case VK_ERROR_FRAGMENTED_POOL:     return "VK_ERROR_FRAGMENTED_POOL";
         case VK_ERROR_UNKNOWN:             return "VK_ERROR_UNKNOWN";
         default:                           return "VK_RESULT_(see spec)";
        }
      }

    // Debug-messenger callback. Counting rule (see file header): ERROR
    // severity counts ONLY when pMessageIdName starts with "VUID-" or
    // "UNASSIGNED"; every other ERROR (this box's broken liblsfg-vk implicit
    // layer makes the LOADER emit ERROR "Loader Message") is logged tagged
    // [vk-loader-error:...] and never counted.
    // TRAP (header API): the data type is VkDebugUtilsMessengerCallbackDataEXT
    // (NOT VkDebugUtilsCallbackDataEXT).
    static VKAPI_ATTR VkBool32 VKAPI_CALL vkDebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT types,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void* userData)
      {(void)types;(void)userData;
       if (!data)
         return VK_FALSE;
       const char* id=data->pMessageIdName?data->pMessageIdName:"";
       const char* msg=data->pMessage?data->pMessage:"(no message)";
       bool vuid=(strncmp(id,"VUID-",5)==0)||(
                  strncmp(id,"UNASSIGNED",10)==0);
       if (severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {if (vuid)
          {if (s_instance)
             ++s_instance->validationErrorCount_;
           fprintf(stderr,"[vk-validation-error:%s] %s\n",id,msg);
          }
         else
          {if (s_instance)
             ++s_instance->loaderErrorCount_;
           fprintf(stderr,"[vk-loader-error:%s] %s\n",id,msg);
          }
        }
       else if (severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
         fprintf(stderr,"[vk-warning:%s] %s\n",id,msg);
       return VK_FALSE;   // never abort the call on messages
      }
 };

// Header-inline static definition — vkBackend.H is included by exactly ONE TU
// per link (obj/VK/XAsteroids.o or obj/VK/vkprobe), mirroring glBackend.H's
// repo-wide header-inline convention; ODR-safe under the current structure.
VKBackend* VKBackend::s_instance=nullptr;

#endif
