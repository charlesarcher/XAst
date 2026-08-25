#ifndef _XAST_VKBACKEND_H
#define _XAST_VKBACKEND_H

// ============================================================================
// vkBackend.H — Vulkan RenderingEngine backend (Phase 4, task 37 scope).
//
// Task 37 lands PASS 0 ONLY: instance + logical device + debug/layer probing,
// fully surfaceless-capable. Tasks 38-42 add surface/swapchain/pipeline work
// at the marked append points — do NOT shuffle this file's order blindly.
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
// Resource ownership map (D15, VK rows, task-37 subset):
//   | Resource                | Owner    | Note                             |
//   |-------------------------|----------|----------------------------------|
//   | VkInstance              | VKBackend| created pass 0; destroyed last   |
//   | VkDebugUtilsMessengerEXT| VKBackend| iff debug_utils; before instance |
//   | VkPhysicalDevice        | (none)   | owned by the instance            |
//   | VkDevice                | VKBackend| created pass 0; before instance  |
//   | GLFWwindow              | task 38  | pass 2; NOT this task            |
//
// Zero X11 includes; zero census symbols (F1-neutral new GPU code).
// NO backend macro: nothing here defines or tests VK_BACKEND (domain stays
// macro-free per playingField.H's GL-bridge precedent).
// ============================================================================

#include<stdlib.h>
#include<stdio.h>
#include<string.h>
#include<vector>

#include<vulkan/vulkan.h> // vendor/vulkan (pin 1:1.4.357.0-1 == installed loader)
#include<GLFW/glfw3.h>    // system glfw3 3.5.1 — pass 0 queries
                          // glfwGetRequiredInstanceExtensions when asked for
                          // window-surface extensions (standalone probe does
                          // NOT glfwInit; the TU still references glfw symbols,
                          // hence obj/VK/vkprobe links -lglfw).

#include"renderingEngine.H"
#include"windowSize.H"    // EngineFontId enum (task 38 pass 1 consumes it)

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
       deviceApiVersion_(0),deviceType_(0),
       swapchainAvailable_(false),
       debugUtilsAvailable_(false),
       instanceLayerPresent_(false),
       deviceLayerPresent_(false),
       validationEnabled_(false),
       validationErrorCount_(0),
       loaderErrorCount_(0),
       glfwInitialized_(false),
       shutdownDone_(false),
       vkDestroyDebugUtilsMessengerEXT_(NULL),
       canonicalPlayAreaWidth_(0),canonicalPlayAreaHeight_(0),
       canonicalMaxIconHeight_(0)
      {deviceName_[0]='\0';
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
    // Pass 0 (task 37): instance + device, optionally with GLFW surface
    // extensions. The SINGLE public init entry point — task 38 adds passes
    // 1/2 INSIDE initWindow, never beside this.
    // ------------------------------------------------------------------
    bool initInstanceAndDevice(bool withGlfwExtensions)
      {if (device!=VK_NULL_HANDLE)          // idempotent (dtor-safe re-entry)
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
         // [task 38 append point: pass 2 creates the GLFW_NO_API window +
         //  surface HERE, after glfwInit, before instance creation so the
         //  required-extension query covers the real window system]
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

       // ---- logical device: ZERO extensions (deliberate, task 37) ------
       float prio=1.0f;
       VkDeviceQueueCreateInfo qci{};
       qci.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
       qci.queueFamilyIndex=graphicsFamily;
       qci.queueCount=1;
       qci.pQueuePriorities=&prio;
       VkDeviceCreateInfo dci{};
       dci.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
       dci.queueCreateInfoCount=1;
       dci.pQueueCreateInfos=&qci;
       dci.enabledExtensionCount=0;       // VK_KHR_swapchain: task 38
       dci.ppEnabledExtensionNames=NULL;
       if (!vkCheck(vkCreateDevice(physicalDevice,&dci,NULL,&device),
                    "vkCreateDevice"))
         return false;

       // ---- debug messenger (post-instance, proc-addr lookup) ----------
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

    // Surfaceless pass 0 (no glfwInit, zero surface extensions) — the
    // obj/VK/vkprobe driver (test/vk/vkprobe.C) calls this and prints the
    // report straight from the public recorded-fact members below.
    bool probeStandalone()
      {return initInstanceAndDevice(false);
      }

    // ------------------------------------------------------------------
    // (1) Window lifecycle
    // ------------------------------------------------------------------
    virtual bool initWindow(const char* title) override
      {(void)title;
       // ---- pass 0 (task 37): instance + device, WITH GLFW extensions ---
       if (!initInstanceAndDevice(true))
         return false;
       // TODO(task 38): pass 1 (stb font slots, mirrors glBackend loadFonts)
       // then pass 2 (GLFW_NO_API window + VkSurfaceKHR + swapchain) go at
       // the marked append point inside initInstanceAndDevice; THIS method
       // grows into: fonts -> window/surface -> swapchain acquisition.
       return true;
      }

    virtual void shutdown() override
      {// Exactly-once guard (M13/D15): safe from dtor AND close-callback path.
       if (shutdownDone_)
         return;
       shutdownDone_=true;
       // TODO(task 38): swapchain -> surface -> window die HERE, BEFORE the
       // device (D15 ownership map; window close routes here exactly once).
       if (debugMessenger!=VK_NULL_HANDLE
           &&vkDestroyDebugUtilsMessengerEXT_)
         {vkDestroyDebugUtilsMessengerEXT_(instance,debugMessenger,NULL);
          debugMessenger=VK_NULL_HANDLE;
         }
       if (device!=VK_NULL_HANDLE)
         {vkDestroyDevice(device,NULL);
          device=VK_NULL_HANDLE;
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
      {// TODO(task 38): acquire + render-pass begin land here.
      }

    virtual void endFrame() override
      {// TODO(task 38/42): queue submit + present land here.
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
      {// Identity until task 38 introduces a real swapchain extent to
       // letterbox against (re-bootstrap on resize lands there too).
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
      {(void)text;(void)fontId;
       notImplemented();
       return 0.0f;   // not reached
      }

    virtual void getFontMetrics(int fontId,FontMetrics& out) override
      {(void)fontId;(void)out;
       notImplemented();
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
    // Recorded pass-0 facts (public: the vkprobe driver prints the report
    // straight from these; naming follows the task-38 handover notes).
    // ------------------------------------------------------------------
    VkInstance instance;                 // VK_NULL_HANDLE until pass 0
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkDebugUtilsMessengerEXT debugMessenger;
    uint32_t graphicsFamily;             // UINT32_MAX until picked
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

   private:
    bool shutdownDone_;
    PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT_;
    int canonicalPlayAreaWidth_,canonicalPlayAreaHeight_;
    int canonicalMaxIconHeight_;

    static VKBackend* s_instance;        // callback routing (single instance)

    bool vkCheck(VkResult rc,const char* what)
      {if (rc<0)
        {fprintf(stderr,"vkBackend: %s failed: %d (%s)\n",what,(int)rc,
                 vkResultName(rc));
         return false;
        }
       return true;
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
