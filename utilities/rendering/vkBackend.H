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
// Task-39 frame sync (this revision): ONE transient command pool +
// kMaxFramesInFlight primary command buffers, one fence per in-flight
// frame (created SIGNALED), per-frame image-available + render-finished
// semaphores. beginFrame/endFrame own the real acquire/submit/present
// cycle (GLFW pump lives in beginFrame, glBackend precedent):
//   - B4 fix: vkAcquireNextImageKHR timeout is FINITE — kAcquireTimeoutMs
//     (100 ms = one frame-period margin over D4's 62.5 ms pacing, m16).
//     This is the ONLY acquire timeout in the tree (grep gate).
//   - OUT_OF_DATE/SUBOPTIMAL from acquire OR present re-bootstraps the
//     swapchain (fresh caps query, oldSwapchain-chained rebuild at the
//     current extent, fresh views) and the cycle continues. From ACQUIRE
//     the possibly-signaled semaphore is recycled by DESTROY+RECREATE
//     (never reused while possibly signaled — no double-wait, no
//     double-signal; R6 spec rule 1).
//   - Fence-wait timeout (>2 frame periods, kFenceTimeoutMs=125 ms) and
//     acquire VK_TIMEOUT ride the SAME recovered-stall path: log + skip
//     the frame; kStallShutdownFrames (100) consecutive stalls force a
//     clean shutdown — the failure mode is defined, never a hang.
//   - Degenerate surface (caps.currentExtent width or height == 0,
//     minimized window) SKIPS re-bootstrap and presentation until a
//     non-degenerate size arrives (vkCreateSwapchainKHR needs >= 1x1).
//   - swapchainDirty_ (task-38 framebuffer-size callback) is consumed
//     HERE, at the top of beginFrame, via the same re-bootstrap path —
//     resize needs no separate mechanism (S2/D17.4a).
//   - endFrame submits the frame's command buffer: a REAL RENDER-PASS
//     cycle since task 40 — vkCmdBeginRenderPass (load op CLEAR with
//     black = the background-GC equivalent, R4-M1) -> dynamic scissor ->
//     [task 41 pipeline slot] -> vkCmdEndRenderPass. The render pass's
//     implicit layout transitions (UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL
//     -> PRESENT_SRC_KHR via finalLayout) replaced task 39's manual
//     barriers + vkCmdClearColorImage entirely.
//
// Task-40 render pass + framebuffers (this revision): ONE render pass
// (single color attachment = the swapchain image view; format follows
// swapchainFormat_; loadOp CLEAR / storeOp STORE; initialLayout UNDEFINED,
// finalLayout PRESENT_SRC_KHR; one external dependency feeding subpass 0's
// COLOR_ATTACHMENT_OUTPUT) + ONE FRAMEBUFFER PER SWAPCHAIN IMAGE. Both are
// created at createSwapchain_'s commit point — so the task-39 re-bootstrap
// path recreates them automatically: OLD FRAMEBUFFERS ARE DESTROYED BEFORE
// THE OLD VIEWS/SWAPCHAIN RETIRE (framebuffers reference views), and the
// existing drainPresentEngine_ + vkDeviceWaitIdle inside re-bootstrap covers
// in-flight safety (no framebuffer may be in use by a pending submission
// when destroyed). The render pass itself survives re-bootstraps unless the
// swapchain format changes (destroy+recreate then).
//
// TASK-40 SCISSOR CONTRACT (D7/D8, v6.1): setScissorRect(const int* xywh)
// takes LOGICAL CLIENT coords {x,y,w,h}; the backend applies the present
// transform on the way to VkRect2D (identity today — the letterbox math is
// D17.4a and lands later); nullptr = full extent (the HUD pass is
// un-scissored, the D8 split). The rect rides ONE dynamic vkCmdSetScissor
// recorded per frame between BeginRenderPass and EndRenderPass. beginFrame
// does NOT own the scissor — callers set it per pass exactly like GL/X11.
//
// DRIVER FINDING (task 40, RTX 5090 + KHRONOS validation, see qa/vk-soak.log
// RUN T40-1): vkCmdClearAttachments is NOT clipped by the dynamic scissor
// here — the current spec states the command "is not affected by the bound
// pipeline state" and its page never mentions scissor; without a bound
// VK_DYNAMIC_STATE_SCISSOR pipeline the set scissor has NO consumer, and
// NVIDIA washed the full framebuffer. Scissor CLIPPING therefore becomes
// pixel-provable from task 41 (pipelines bind + draw inside this pass,
// consuming the latched vkCmdSetScissor). Until then the vkpass.C proof
// drives its clip region through the SAME setScissorRect ->
// applyPresentTransformToScissor_ chain via VkClearRect (spec-guaranteed),
// which pins the coordinate mapping end-to-end.
//
// TASK-41 PIPELINES (this revision): four graphics pipelines built against
// THE ONE task-40 render pass, all sharing VK_DYNAMIC_STATE_VIEWPORT|SCISSOR
// (the per-frame latched vkCmdSetScissor finally gets its consumer — the
// task-40 driver finding is closed: draws ARE scissor-clipped):
//   linePipeline     LINE_LIST topology, FILL raster — thin lines (width<2,
//                    GL core-profile parity: lineWidth static 1.0)
//   triPipeline      TRIANGLE_LIST + VK_POLYGON_MODE_FILL — thick lines
//                    (SC8 quad expansion, ported verbatim from glBackend),
//                    filled polygons/rects (earClip ported verbatim)
//   outlinePipeline  LINE_LIST + VK_POLYGON_MODE_LINE — polygon/rect
//                    outlines (GL_LINE_LOOP emulated by emitting the closing
//                    segment; VK has no LINE_LOOP)
//   texPipeline      TRIANGLE_LIST + FILL — textured quads/triangles;
//                    stride-7 [x y u v r g b] vertices (glBackend's
//                    drawTriangles contract); drawTexture synthesizes
//                    tint=(1,1,1,alpha) which reproduces GL's
//                    vec4(c.rgb,c.a*uAlpha) exactly through the shared
//                    FragColor = texture(uTex,vUV)*vColor shader. This MERGES
//                    glBackend's two textured programs (stride-4 texProgram_
//                    + uAlpha uniform vs stride-7 triProgram_ + vColor) into
//                    one pipeline — documented divergence, sampling result
//                    identical.
// SHADERS PRE-COMPILED: committed .vert/.frag sources live in
// utilities/rendering/vkShaders/ and are compiled to SPIR-V by glslc AT
// BUILD TIME (makefile rule -> obj/VK/spv/*.spv). The .spv blobs are loaded
// at initWindow from the shaderDir_ (resolved next to the binary) — no
// runtime glslc, no /proc/self/exe, no temp dirs. Any missing .spv fails
// initWindow before a frame is ever drawn (no mid-game first-use failure),
// mirroring glBackend's compile-at-init gate.
// MVP TRANSFORM = push-constant UNIFORM BLOCK (mat4, vertex stage), recorded
// PER DRAW via vkCmdPushConstants — the exact analog of GL's per-draw
// glUniformMatrix4fv(uMVP). A UBO+dynamic-offset scheme was rejected:
// engine draws are RECORDED at call time between beginFrame/endFrame, so
// mid-frame setTransform changes (ship then bullet) must reach each draw
// individually; per-draw dynamic offsets would force a descriptor rebind
// per draw. Composition (presentMVP_): M = rotate-about-own-origin THEN
// translate (D2 rotator placement, m19 GL/VK-only semantics); P maps
// logical client space onto NDC. Vulkan NDC is y-DOWN already, so P has NO
// y-flip (GL's b=-2*scale/fbH becomes +2*scale/fbH here) — same logical
// geometry renders to the same pixels as GL.
// RENDER-PASS LIFETIME DECISION: pipelines bind to THE render pass object,
// so the pass now lives for the BACKEND'S LIFE — destroySwapchainFramebuffers_
// no longer destroys it (framebuffers only); shutdown() does. Re-bootstraps
// rebuild framebuffers ONLY; pipelines survive untouched (verified live:
// resize soak with draws bound every frame). The defensive format-change
// branch (never taken on X11 WSI) destroys+recreates pipelines together
// with the pass.
// DRAW RESOURCES: one host-visible coherent VERTEX SCRATCH BUFFER per
// in-flight frame slot (4 MB, bump-allocated per draw, reset each preamble)
// replaces GL's per-draw glBufferData STREAM_DRAW. Textures: device-local
// images uploaded via staging + one-shot command buffer, one shared linear/
// clamp-to-edge sampler, one combined-image-sampler descriptor set per
// texture from a pool sized 256 sets/samplers. channels==3 uploads expand
// to RGBA8 on VK (RGB888 images are format-minefield; content opaque).
// FRAME SPLIT (was recordFrameCommands_): beginFrame records the PREAMBLE
// (BeginRenderPass + viewport + latched scissor + in-pass QA hook) and
// LEAVES THE COMMAND BUFFER OPEN; engine draws record into it; endFrame
// records the POSTAMBLE (EndRenderPass + after-pass QA hook + End) and
// submits. Hook ordering relative to the pass is unchanged from task 40.
//
// TASK-42 ENGINE METHODS (this revision): the 27-method surface is complete.
//   pollEvents        THE D16 per-event state machine, ported 1:1 from GL:
//                     GLFW key callback with the US-layout key->character
//                     table (S/Q/H/E/R/O/P/N/SPACE), PRESS/RELEASE only
//                     (GLFW_REPEAT dropped at the boundary), plus the task-44a
//                     mouse bridge (cursor pos / Button1 / enter-leave)
//                     inverse-present-transformed to LOGICAL client coords at
//                     enqueue. One typed ring; pollEvents drains it.
//   Text              stb glyph rasterization into ONE shelf-packed atlas
//                     (glBackend initText verbatim: ASCII 32..126, four
//                     slots, 2048x512). The atlas uploads as RGBA8 with the
//                     coverage byte in ALL FOUR channels so the existing
//                     texPipeline reproduces glBackend's text math exactly:
//                     GL FragColor=vec4(uColor*a,a) under SRC_ALPHA blending
//                     yields rgb=uColor*a^2+dst*(1-a); VK texel=(a,a,a,a) *
//                     tint=(r,g,b,1) = (r*a,g*a,b*a,a) blends to the SAME
//                     expression. drawGlyphs emits stride-8 quads through
//                     texPipeline (24-float-per-quad semantics preserved —
//                     never reintroduce the task-36 Defect-A 16-float stride).
//   drawTextureMasked new masked.frag (content binding 0 + R8 mask binding 1,
//                     discard <0.5 — glBackend maskedProgram_ verbatim) on a
//                     second descriptor set layout + pipeline sharing tex.vert;
//                     per-(content,mask) descriptor sets cached by id pair.
//   Render targets    offscreen VkImage (COLOR_ATTACHMENT|SAMPLED|TRANSFER_SRC,
//                     swapchain format -> pipeline-compatible) + view +
//                     framebuffer + sampling descriptor set + a SECOND render
//                     pass (loadOp DONT_CARE, finalLayout SHADER_READ_ONLY_
//                     OPTIMAL — layout does not affect pass compatibility).
//                     beginRenderTo inside an open frame records
//                     EndRenderPass -> layout barrier -> BeginRenderPass
//                     (offscreen) inline and endRenderTo reverses it;
//                     outside a frame (init-time Button/yard faces) both run
//                     as a one-shot transient command buffer submitted with a
//                     finite fence. presentMVP_ carries an rtActive_ branch
//                     mapping logical (0,0)-top-left 1:1 onto the target
//                     (GL rtActive_ analog without the y-flip).
//   getPresentTransform REAL letterbox math now (uniform scale + centered
//                     offsets from fbWidth_/fbHeight_ vs the canonical size —
//                     glBackend verbatim); recomputePresentTransform_ feeds
//                     presentScale_/presentOffX_/presentOffY_ from the
//                     framebuffer-size callback AND every re-bootstrap commit;
//                     applyPresentTransformToScissor_ applies scale+offset.
//   nativeHandle      documented undefined/unused no-op (D9 seam).
//   m13 evidence      transformCallCount_ counts setTransform calls; the QA
//                     lane snapshots it around a NonRot-style static-texture
//                     draw to assert the degenerate path issues 0 updates.
//
// QA HOOKS (task-40 scissor proof only; NULL by default, zero cost):
//   qaInRenderPassHook(cb,imageIndex,image,ud)    runs INSIDE the render
//                    pass, after the scissor is applied;
//   qaAfterRenderPassHook(cb,imageIndex,image,ud) runs AFTER
//                    vkCmdEndRenderPass (image back in PRESENT_SRC_KHR).
// test/vk/vkpass.C uses them to inject vkCmdClearAttachments (scissor-
// clipped) and a PRESENT_SRC->TRANSFER_SRC->copy->PRESENT_SRC readback into
// otherwise-normal frames.
// FRAMES-IN-FLIGHT CHOICE: 2. The swapchain itself stays triple-buffered
// (3 images, task 38); 2 frames-in-flight is the classic CPU/GPU overlap
// point — the CPU never laps the GPU by more than one frame, halving the
// sync-object count vs 3 with no throughput loss under FIFO pacing.
// Sync objects are DEVICE-level and independent of the swapchain:
// re-bootstrap never recreates them.
// TASK 40 CONTRACT (leave intact): beginFrame acquires into
// acquiredImageIndex_ and records commandBuffers_[currentFrame_]
// (pool: commandPool_, TRANSIENT + RESET_COMMAND_BUFFER); endFrame
// submits with wait=imageAvailableSemaphores_[currentFrame_] at
// COLOR_ATTACHMENT_OUTPUT, signal=renderFinishedSemaphores_[
// acquiredImageIndex_] (per swapchain image — VUID-
// vkQueueSubmit-pSignalSemaphores-00067), fence=inFlightFences_[
// currentFrame_]. Render-pass/framebuffer creation slots in around
// recordFrameCommands_ (task 40 landed it there).
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
//   | VkRenderPass            | VKBackend| task 40; after framebuffers die  |
//   | VkFramebuffer per image | VKBackend| task 40; destroyed BEFORE views  |
//   | VkCommandPool/buffers   | VKBackend| task 39; freed before the device |
//   | fences/semaphores (x2f) | VKBackend| task 39; freed before the device |
//
// Zero X11 includes; zero census symbols (F1-neutral new GPU code).
// NO backend macro: nothing here defines or tests VK_BACKEND (domain stays
// macro-free per playingField.H's GL-bridge precedent).
// ============================================================================

#include<stdlib.h>
#include<stdio.h>
#include<string.h>
#include<unistd.h>        // readlink/ssize_t, access()
#ifdef __APPLE__
#include<mach-o/dyld.h>   // _NSGetExecutablePath (macOS exe-dir resolution)
#include<dlfcn.h>         // dladdr fallback
#endif
#include<errno.h>
#include<math.h>          // cosf/sinf/sqrtf (MVP composition, SC8 quads)
#include<vector>
#include<map>

#include<vulkan/vulkan.h> // vendor/vulkan (pin 1:1.4.357.0-1 == installed loader)
#include<GLFW/glfw3.h>    // system glfw3 3.5.1 — pass 0 queries
                          // glfwGetRequiredInstanceExtensions when asked for
                          // window-surface extensions (standalone probe does
                          // NOT glfwInit; the TU still references glfw symbols,
                          // hence obj/VK/vkprobe links -lglfw).

#include"renderingEngine.H"
#include"windowSize.H"    // WindowSizeFormula::Compute + EngineFontId enum
#include"imgui.h"         // task 44b: menu-glue platform half (clipboard
                          // stubs + BackendPlatformName; mirrors glBackend).
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
    // Task-39 frame-sync tuning constants (public: soak/probe drivers print
    // them; the B4 grep gate keys on kAcquireTimeoutMs).
    static constexpr int kMaxFramesInFlight=2;       // see header comment
    static constexpr uint32_t kAcquireTimeoutMs=100; // B4: FINITE (m16)
    static constexpr uint32_t kFenceTimeoutMs=125;   // >2 frame periods
    static constexpr unsigned kStallShutdownFrames=100;

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
        commandPool(VK_NULL_HANDLE),
        renderPass(VK_NULL_HANDLE),
        linePipeline(VK_NULL_HANDLE),
        triPipeline(VK_NULL_HANDLE),
        outlinePipeline(VK_NULL_HANDLE),
        texPipeline(VK_NULL_HANDLE),
        maskedPipeline(VK_NULL_HANDLE),
        colorPipelineLayout(VK_NULL_HANDLE),
        texPipelineLayout(VK_NULL_HANDLE),
        descriptorPool(VK_NULL_HANDLE),
        shaderModulesLoaded_(0),
        frameCount_(0),
        rebootstrapCount_(0),
        stallSkipCount_(0),
        presentationSuspended_(false),
        fatalStallShutdown_(false),
        transformCallCount_(0),
        qaInRenderPassHook(NULL),
        qaInRenderPassHookUserData(NULL),
        qaAfterRenderPassHook(NULL),
        qaAfterRenderPassHookUserData(NULL),
        shutdownDone_(false),
        vkDestroyDebugUtilsMessengerEXT_(NULL),
        canonicalPlayAreaWidth_(0),canonicalPlayAreaHeight_(0),
        canonicalMaxIconHeight_(0),
        renderPassFormat_(VK_FORMAT_UNDEFINED),
        offscreenRenderPass_(VK_NULL_HANDLE),
        swapchainLoadRenderPass_(VK_NULL_HANDLE),
        scissorActive_(false),
        scissorRect_{0,0,0,0},
        primVs_(VK_NULL_HANDLE),
        primFs_(VK_NULL_HANDLE),
        texVs_(VK_NULL_HANDLE),
        texFs_(VK_NULL_HANDLE),
        maskedFs_(VK_NULL_HANDLE),
        texSetLayout_(VK_NULL_HANDLE),
        maskedSetLayout_(VK_NULL_HANDLE),
        maskedPipelineLayout_(VK_NULL_HANDLE),
        sharedSampler_(VK_NULL_HANDLE),
        nextTextureId_(1),
        atlasTex_(0),
        tx_(0.0f),ty_(0.0f),angle_(0.0f),
        canvasOffX_(0.0f),canvasOffY_(0.0f),
        presentScale_(1.0f),
        presentOffX_(0),presentOffY_(0),
        rtActive_(false),
        rtWidth_(0),rtHeight_(0),
        currentRenderTarget_(0),
        rtOneShot_(false),
        oneShotCb_(VK_NULL_HANDLE),
        activeCb_(VK_NULL_HANDLE),
        passOpen_(false),
        acquiredImageIndex_(0),
        currentFrame_(0),
        frameAcquired_(false),
        sessionDead_(false),
        swapchainTransferDstSupported_(false),
        swapchainTransferSrcSupported_(false),
        consecutiveStallCount_(0)
       {deviceName_[0]='\0';
       shaderDir_[0]='\0';
       for (int i=0;i<4;++i)
        {fontInfo_[i]=NULL;fontData_[i]=NULL;
         fontDataLen_[i]=0;fontScale_[i]=0.0f;maxAsciiAdvance_[i]=0.0f;
         slotShelfH_[i]=0;
         for (int c=0;c<128;++c)
           glyphs_[i][c].present=false;
        }
       eventHead_=0;eventTail_=0;
       lastCursorX_=0.0;lastCursorY_=0.0;cursorValid_=false;
        for (int i=0;i<kMaxFramesInFlight;++i)
         {commandBuffers_[i]=VK_NULL_HANDLE;
          inFlightFences_[i]=VK_NULL_HANDLE;
          imageAvailableSemaphores_[i]=VK_NULL_HANDLE;
          vertBuf_[i]=VK_NULL_HANDLE;
          vertMem_[i]=VK_NULL_HANDLE;
          vertMapped_[i]=NULL;
          vertUsed_[i]=0;
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
                                "not expose it \xe2\x80\x94 skipped\n",req[i]);
             }
           }
         }
       if (debugUtilsAvailable_)
         exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
       // Portability enumeration: required on macOS (MoltenVK is a
       // "portability driver" — the loader refuses to enumerate physical
       // devices unless VK_KHR_portability_enumeration is enabled AND the
       // VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR flag is set).
       // Probe-and-enable so Linux (non-portability ICDs) is unaffected.
       if (extInList(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
                     iext.data(),next))
         exts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

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
#ifdef XAST_DEBUG_VK
       fprintf(stderr,"vkBackend: enabled extensions (%u):\\n",(uint32_t)exts.size());
       for (size_t i=0;i<exts.size();++i)
         fprintf(stderr,"  %s\\n",exts[i]);
#endif

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
       ici.flags=VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
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
       // fillModeNonSolid: required by the outline pipeline's
       // VK_POLYGON_MODE_LINE (VUID-01507); universal on real GPUs.
       VkPhysicalDeviceFeatures feats{};
       feats.fillModeNonSolid=VK_TRUE;
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
        dci.pEnabledFeatures=&feats;
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
      {// Task 41: shaders must be present BEFORE any other resource — a
       // missing .spv fails initWindow here, never mid-game first-use.
       if (!loadPrecompiledShaders_())
         {shutdown();
          return false;
         }
       if (!initInstance_(true))
        {shutdown();
         return false;
        }
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
        if (!createSwapchain_(VK_NULL_HANDLE))
         {shutdown();
          return false;
         }
        if (!initFrameSync_())
         {shutdown();
          return false;
         }
        // Task 41: pipelines bind to THE render pass object created inside
        // createSwapchain_'s commit point; they are built once here and
        // survive re-bootstraps (the pass does too — see
        // destroySwapchainFramebuffers_).
        if (!initPipelines_())
         {shutdown();
           return false;
          }
        if (!initText_())
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
         // Task-39 frame-sync teardown: drain pending presents FIRST (the
         // presentation engine holds the last-presented images/semaphores
         // beyond queue execution), then free device-level objects BEFORE
         // the device.
         drainPresentEngine_();
        // Task 43: reset the pool BEFORE any texture/view teardown — a
        // recorded-but-not-reset command buffer keeps its bound descriptor
        // sets "in use" for validation (VUID-01026) even after the work
        // completed; resetting releases every recorded reference. The game
        // can exit from paths that leave slot cbs recorded (close on any
        // screen).
        if (device!=VK_NULL_HANDLE&&commandPool!=VK_NULL_HANDLE)
          vkResetCommandPool(device,commandPool,0);
        // Task 43: retire the descriptor pool BEFORE the texture views it
        // references — a set alive while its view dies is exactly the
        // VUID-01026 teardown chain (set-in-use-by-cb holds the view).
        if (descriptorPool!=VK_NULL_HANDLE)
         {vkDestroyDescriptorPool(device,descriptorPool,NULL);
          descriptorPool=VK_NULL_HANDLE;
         }
         destroyTextures_();
        destroyRenderTargets_();
        destroySwapchainFramebuffers_();   // task 40: BEFORE views (fbs
                                           // reference views); the render
                                           // pass outlives re-bootstraps and
                                           // dies in destroyDrawResources_
        destroyDrawResources_();   // task 41: vert scratch + pipelines +
                                   // layouts + pool + sampler + shader
                                   // modules + THE render pass (pipelines
                                   // reference it)
        for (size_t i=0;i<swapchainImageViews_.size();++i)
          vkDestroyImageView(device,swapchainImageViews_[i],NULL);
        swapchainImageViews_.clear();
        swapchainImages_.clear();
        if (swapchain!=VK_NULL_HANDLE)
          {vkDestroySwapchainKHR(device,swapchain,NULL);
           swapchain=VK_NULL_HANDLE;
          }
        destroyRenderFinishedSemaphores_();   // per-image (task 39)
        destroyFrameSync_();
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
    // (2) Frame lifecycle — task-39 real cycle.
    //   beginFrame: GLFW pump (callbacks fire here, glBackend precedent)
    //   -> consume swapchainDirty_ via re-bootstrap -> degenerate-extent
    //   guard -> fence wait (recovered-stall path) -> FINITE acquire
    //   (kAcquireTimeoutMs; B4) with OUT_OF_DATE/SUBOPTIMAL handling ->
    //   record the frame's render-pass cycle for this in-flight slot.
    //   endFrame: fence-armed submit + present, then advance the slot.
    //   Skipped frames (stall/suspend/re-bootstrap) make endFrame a no-op
    //   via frameAcquired_.
    // ------------------------------------------------------------------
    virtual void beginFrame() override
      {if (sessionDead_||shutdownDone_)
         return;
        if (window)                     // GLFW requires a regular pump or
          glfwPollEvents();             // resize/close callbacks never fire
        if (frameAcquired_)             // Task 43: the domain double-pumps —
          return;                       // RunGame's iteration head pumps
                                        // BEFORE the drain AND PresentFrame
                                        // opens its own frame (GL's beginFrame
                                        // is pump-only, so the redundant call
                                        // must not re-acquire/re-record: a
                                        // second acquire here strands images
                                        // outside the present engine and a
                                        // second preamble re-records an open
                                        // command buffer).
        if (swapchainDirty_)            // task-38 resize flag consumed HERE
         {if (!rebootstrapSwapchain_())
            return;
         }
        if (presentationSuspended_)
          return;
        VkResult wr=vkWaitForFences(device,1,
                                    &inFlightFences_[currentFrame_],
                                    VK_TRUE,
                                    (uint64_t)kFenceTimeoutMs*1000000ull);
        if (wr==VK_TIMEOUT)
         {handleStall_("fence wait timeout");
          return;
         }
        if (wr!=VK_SUCCESS)
         {fprintf(stderr,"vkBackend: vkWaitForFences failed: %d (%s)\n",
                  (int)wr,vkResultName(wr));
          handleFatalStall_("fence wait failed");
          return;
         }
        VkResult ar=vkAcquireNextImageKHR(device,swapchain,
                                          (uint64_t)kAcquireTimeoutMs
                                                    *1000000ull,
                                          imageAvailableSemaphores_[
                                                      currentFrame_],
                                          VK_NULL_HANDLE,
                                          &acquiredImageIndex_);
        if (ar==VK_ERROR_OUT_OF_DATE_KHR)
         {// R6 rule 1: on OUT_OF_DATE from ACQUIRE the semaphore MAY be
          // signaled per spec — recycle by destroy+recreate (never reused
          // while possibly signaled), then rebuild and retry once.
          if (!recreateAcquireSemaphore_())
            return;
          if (!rebootstrapSwapchain_())
            return;
          ar=vkAcquireNextImageKHR(device,swapchain,
                                   (uint64_t)kAcquireTimeoutMs*1000000ull,
                                   imageAvailableSemaphores_[currentFrame_],
                                   VK_NULL_HANDLE,&acquiredImageIndex_);
          if (ar==VK_ERROR_OUT_OF_DATE_KHR)
           {if (!recreateAcquireSemaphore_())
              return;
            fprintf(stderr,"vkBackend: acquire OUT_OF_DATE twice in one "
                            "frame — skipping\n");
            return;
           }
         }
        if (ar==VK_SUBOPTIMAL_KHR)
         {// A suboptimal image still presents: use THIS one, rebuild at the
          // next beginFrame.
          fprintf(stderr,"vkBackend: acquire SUBOPTIMAL — deferring "
                          "re-bootstrap to next beginFrame\n");
          swapchainDirty_=true;
         }
        else if (ar==VK_TIMEOUT)
         {handleStall_("acquire timeout");   // R6 rule 2: same stall path
          return;
         }
        if (!vkCheck(ar,"vkAcquireNextImageKHR"))
         {handleFatalStall_("acquire failed");
          return;
         }
        // Task 41: only the PREAMBLE is recorded here (BeginRenderPass +
        // viewport + latched scissor + in-pass hook); the command buffer
        // stays OPEN so engine draws record into it between begin/end.
        if (!recordFramePreamble_(acquiredImageIndex_))
          return;
        frameAcquired_=true;
      }

    virtual void endFrame() override
      {if (!frameAcquired_||sessionDead_||shutdownDone_)
         return;
       // Task 41: close the open render pass (after-pass hook rides here,
       // image back in PRESENT_SRC_KHR) before submitting.
       if (!recordFramePostamble_())
        {fprintf(stderr,"vkBackend: postamble record failed — frame "
                        "skipped\n");
         frameAcquired_=false;
         return;
        }
       // Arm the fence JUST before submit (not at wait time): a skipped
       // frame must leave the fence signaled so the next beginFrame's wait
       // succeeds instantly instead of false-stalling.
       if (!vkCheck(vkResetFences(device,1,
                  &inFlightFences_[currentFrame_]),"vkResetFences"))
        {handleFatalStall_("fence reset failed");
         return;
        }
       VkPipelineStageFlags waitStage=
         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
       VkSubmitInfo si{};
       si.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
       si.waitSemaphoreCount=1;
       si.pWaitSemaphores=&imageAvailableSemaphores_[currentFrame_];
       si.pWaitDstStageMask=&waitStage;
       si.commandBufferCount=1;
       si.pCommandBuffers=&commandBuffers_[currentFrame_];
       si.signalSemaphoreCount=1;
       si.pSignalSemaphores=
         &renderFinishedSemaphores_[acquiredImageIndex_];
       if (!vkCheck(vkQueueSubmit(graphicsQueue,1,&si,
                  inFlightFences_[currentFrame_]),"vkQueueSubmit"))
        {handleFatalStall_("queue submit failed");
         return;
        }
       VkPresentInfoKHR pi{};
       pi.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
       pi.waitSemaphoreCount=1;
       pi.pWaitSemaphores=&renderFinishedSemaphores_[acquiredImageIndex_];
       pi.swapchainCount=1;
       pi.pSwapchains=&swapchain;
       pi.pImageIndices=&acquiredImageIndex_;
       VkResult pr=vkQueuePresentKHR(presentQueue,&pi);
       if (pr==VK_ERROR_OUT_OF_DATE_KHR||pr==VK_SUBOPTIMAL_KHR)
        {// Present-side invalidation re-bootstraps now; the frame still
         // counts (submit + present both happened).
         fprintf(stderr,"vkBackend: present %s — re-bootstrapping\n",
                 pr==VK_ERROR_OUT_OF_DATE_KHR?"OUT_OF_DATE":"SUBOPTIMAL");
         rebootstrapSwapchain_();
        }
       else if (!vkCheck(pr,"vkQueuePresentKHR"))
        {handleFatalStall_("present failed");
         return;
        }
       ++frameCount_;
       consecutiveStallCount_=0;
       currentFrame_=(currentFrame_+1)%kMaxFramesInFlight;
       frameAcquired_=false;
      }

    // ------------------------------------------------------------------
    // (3) Seams
    // ------------------------------------------------------------------
    virtual int pollEvents(GameEvent* out,int maxCount) override
      {// D16 per-event state machine, GL-identical: the GLFW callbacks
       // installed in pass 2a enqueue typed events (keys through the
       // US-layout table with PRESS/RELEASE only — GLFW_REPEAT never
       // reaches this queue; mouse inverse-present-transformed to LOGICAL
       // client coords at enqueue) and this drain hands them out.
       // Unlike GL there is no out-of-backend key bridge to merge: keys AND
       // mouse both arrive through this one queue.
       int count=0;
       while (eventHead_!=eventTail_&&count<maxCount)
        {out[count++]=eventQueue_[eventHead_];
         eventHead_=(eventHead_+1)%maxEventQueue_;
        }
       return count;
      }

    // ------------------------------------------------------------------
    // Menu/input glue (task 44b, D9). The Dear ImGui PLATFORM half that the
    // engine owns — mirrors glBackend.H's installMenuInputBridge: clipboard
    // stubs for the ImGui context the menu adapter creates; the raw mouse/
    // key capture itself already rides initWindow pass 2a (task 42).
    // ------------------------------------------------------------------
    void installMenuInputBridge()
     {// Called once by ImGuiOptionsMenu after adapter init created the ImGui
      // context. Clipboard stubs: no clipboard domain exists; explicit no-ops.
      ImGuiIO& io=ImGui::GetIO();
      io.BackendPlatformName="vkBackend_XAsteroids_GameEvents";
      io.SetClipboardTextFn=clipboardSetStub_;
      io.GetClipboardTextFn=clipboardGetStub_;
     }

    // Task 43: canvas-domain translation (logical units, added inside
    // presentMVP_) — identical contract to glBackend's setCanvasOffset.
    // playingField brackets the passes whose coordinates are recorded in the
    // old canvas-relative space (world replay, help text, score table) with
    // (stage->playAreaXY - 80); window-domain chrome stays unbracketed.
    void setCanvasOffset(float dx,float dy)
     {canvasOffX_=dx;canvasOffY_=dy;}

    virtual void setScissorRect(const int* xywh) override
      {// D7/D8 (v6.1): LOGICAL CLIENT {x,y,w,h} in; the present transform is
       // applied at record time (applyPresentTransformToScissor_, identity
       // today). nullptr = full extent — the HUD pass is un-scissored.
       // beginFrame does NOT own the scissor; callers set it per pass.
       if (!xywh)
        {scissorActive_=false;
         return;
        }
       scissorRect_[0]=xywh[0];
       scissorRect_[1]=xywh[1];
       scissorRect_[2]=xywh[2];
       scissorRect_[3]=xywh[3];
       scissorActive_=true;
      }

    virtual RenderTargetId createRenderTarget(int w,int h) override
      {// M1 VK half: offscreen color image in the SWAPCHAIN FORMAT (keeps
       // every pipeline render-pass-compatible with the offscreen pass —
       // compatibility keys on attachment formats, not layouts) + view +
       // framebuffer against offscreenRenderPass_ + a sampling descriptor
       // set so drawTexture consumes the result like any TextureId.
       if (device==VK_NULL_HANDLE||swapchainFormat_==VK_FORMAT_UNDEFINED
           ||w<=0||h<=0)
         return 0;
       ensureOffscreenRenderPass_();
       VkImageCreateInfo ici{};
       ici.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
       ici.imageType=VK_IMAGE_TYPE_2D;
       ici.format=swapchainFormat_;
       ici.extent.width=(uint32_t)w;
       ici.extent.height=(uint32_t)h;
       ici.extent.depth=1;
       ici.mipLevels=1;
       ici.arrayLayers=1;
       ici.samples=VK_SAMPLE_COUNT_1_BIT;
       ici.tiling=VK_IMAGE_TILING_OPTIMAL;
       ici.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                |VK_IMAGE_USAGE_SAMPLED_BIT
                |VK_IMAGE_USAGE_TRANSFER_SRC_BIT;   // QA readback
       ici.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
       ici.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
       VkImage image=VK_NULL_HANDLE;
       if (!vkCheck(vkCreateImage(device,&ici,NULL,&image),
                    "vkCreateImage(render-target)"))
         return 0;
       VkMemoryRequirements mr;
       vkGetImageMemoryRequirements(device,image,&mr);
       uint32_t memType=findMemoryType_(mr.memoryTypeBits,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
       if (memType==UINT32_MAX)
         memType=findMemoryType_(mr.memoryTypeBits,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                 |VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
       VkDeviceMemory mem=VK_NULL_HANDLE;
       VkMemoryAllocateInfo mai{};
       mai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
       mai.allocationSize=mr.size;
       mai.memoryTypeIndex=memType;
       if (memType==UINT32_MAX
           ||!vkCheck(vkAllocateMemory(device,&mai,NULL,&mem),
                      "vkAllocateMemory(render-target)")
           ||!vkCheck(vkBindImageMemory(device,image,mem,0),
                      "vkBindImageMemory(render-target)"))
        {if (mem!=VK_NULL_HANDLE)
           vkFreeMemory(device,mem,NULL);
         vkDestroyImage(device,image,NULL);
         return 0;
        }
       VkImageViewCreateInfo vci{};
       vci.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
       vci.image=image;
       vci.viewType=VK_IMAGE_VIEW_TYPE_2D;
       vci.format=swapchainFormat_;
       vci.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
       vci.subresourceRange.levelCount=1;
       vci.subresourceRange.layerCount=1;
       VkImageView view=VK_NULL_HANDLE;
       if (!vkCheck(vkCreateImageView(device,&vci,NULL,&view),
                    "vkCreateImageView(render-target)"))
        {vkFreeMemory(device,mem,NULL);
         vkDestroyImage(device,image,NULL);
         return 0;
        }
       VkFramebufferCreateInfo fci{};
       fci.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
       fci.renderPass=offscreenRenderPass_;
       fci.attachmentCount=1;
       fci.pAttachments=&view;
       fci.width=(uint32_t)w;
       fci.height=(uint32_t)h;
       fci.layers=1;
       VkFramebuffer fb=VK_NULL_HANDLE;
       if (!vkCheck(vkCreateFramebuffer(device,&fci,NULL,&fb),
                    "vkCreateFramebuffer(render-target)"))
        {vkDestroyImageView(device,view,NULL);
         vkFreeMemory(device,mem,NULL);
         vkDestroyImage(device,image,NULL);
         return 0;
        }
       VkDescriptorSetLayout layouts[]={texSetLayout_};
       VkDescriptorSetAllocateInfo dai{};
       dai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
       dai.descriptorPool=descriptorPool;
       dai.descriptorSetCount=1;
       dai.pSetLayouts=layouts;
       VkDescriptorSet set=VK_NULL_HANDLE;
       if (!vkCheck(vkAllocateDescriptorSets(device,&dai,&set),
                    "vkAllocateDescriptorSets(render-target)"))
        {vkDestroyFramebuffer(device,fb,NULL);
         vkDestroyImageView(device,view,NULL);
         vkFreeMemory(device,mem,NULL);
         vkDestroyImage(device,image,NULL);
         return 0;
        }
       VkDescriptorImageInfo ii{};
       ii.sampler=sharedSampler_;
       ii.imageView=view;
       ii.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
       VkWriteDescriptorSet wr{};
       wr.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
       wr.dstSet=set;
       wr.dstBinding=0;
       wr.descriptorCount=1;
       wr.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
       wr.pImageInfo=&ii;
       vkUpdateDescriptorSets(device,1,&wr,0,NULL);
       RenderTargetId id=nextTextureId_++;
       RTEntry e;
       e.image=image;
       e.mem=mem;
       e.view=view;
       e.fb=fb;
       e.set=set;
       e.w=w;
       e.h=h;
       e.layout=VK_IMAGE_LAYOUT_UNDEFINED;
       renderTargets_[id]=e;
       return id;
      }

    virtual void beginRenderTo(RenderTargetId target) override
      {auto it=renderTargets_.find(target);
       if (it==renderTargets_.end()||device==VK_NULL_HANDLE
           ||rtActive_)
         return;
       RTEntry& rt=it->second;
       if (passOpen_)
        {// Mid-frame (AddShip re-faces the yard mid-game): close the
         // swapchain pass instance, flip the target's layout, open the
         // offscreen pass on its framebuffer. Dynamic viewport/scissor are
         // command-buffer state and survive the pass switch.
         vkCmdEndRenderPass(activeCb_);
         rtToColorBarrier_(rt);
         beginOffscreenPass_(rt);
        }
       else
        {// Init-time faces (Button/ShipYard ctors run before any frame):
         // record into a transient one-shot command buffer, submitted and
         // fenced by endRenderTo.
         VkCommandBufferAllocateInfo cai{};
         cai.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
         cai.commandPool=commandPool;
         cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
         cai.commandBufferCount=1;
         if (!vkCheck(vkAllocateCommandBuffers(device,&cai,&oneShotCb_),
                      "vkAllocateCommandBuffers(render-to)"))
          {oneShotCb_=VK_NULL_HANDLE;
           return;
          }
         VkCommandBufferBeginInfo bi{};
         bi.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
         bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
         if (!vkCheck(vkBeginCommandBuffer(oneShotCb_,&bi),
                      "vkBeginCommandBuffer(render-to)"))
          {vkFreeCommandBuffers(device,commandPool,1,&oneShotCb_);
           oneShotCb_=VK_NULL_HANDLE;
           return;
          }
         activeCb_=oneShotCb_;
         rtOneShot_=true;
         passOpen_=true;      // draws record into the one-shot buffer
         rtToColorBarrier_(rt);
         beginOffscreenPass_(rt);
        }
       currentRenderTarget_=target;
       rtWidth_=rt.w;
       rtHeight_=rt.h;
       rtActive_=true;
      }

    virtual void endRenderTo() override
      {if (!rtActive_||activeCb_==VK_NULL_HANDLE)
         return;
       auto it=renderTargets_.find(currentRenderTarget_);
       if (it==renderTargets_.end())
        {rtActive_=false;
         currentRenderTarget_=0;
         return;
        }
       RTEntry& rt=it->second;
       vkCmdEndRenderPass(activeCb_);
       // No manual ->SHADER_READ_ONLY barrier here: the offscreen pass's
       // finalLayout performs that transition at EndRenderPass; an extra
       // barrier declaring oldLayout=COLOR_ATTACHMENT trips VUID-01197.
       rt.layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
       if (rtOneShot_)
        {// Finish + submit + finite fence wait: init-time rendering is
         // synchronous exactly like the texture staging upload.
         if (vkCheck(vkEndCommandBuffer(oneShotCb_),
                     "vkEndCommandBuffer(render-to)"))
          {VkFenceCreateInfo fci{};
           fci.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
           VkFence fence=VK_NULL_HANDLE;
           if (vkCheck(vkCreateFence(device,&fci,NULL,&fence),
                       "vkCreateFence(render-to)"))
            {VkSubmitInfo si{};
             si.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
             si.commandBufferCount=1;
             si.pCommandBuffers=&oneShotCb_;
             if (vkCheck(vkQueueSubmit(graphicsQueue,1,&si,fence),
                         "vkQueueSubmit(render-to)"))
               vkWaitForFences(device,1,&fence,VK_TRUE,
                               (uint64_t)1000*1000000ull);
             vkDestroyFence(device,fence,NULL);
            }
          }
         vkFreeCommandBuffers(device,commandPool,1,&oneShotCb_);
         oneShotCb_=VK_NULL_HANDLE;
         activeCb_=passOpen_?commandBuffers_[currentFrame_]:VK_NULL_HANDLE;
         rtOneShot_=false;
         passOpen_=false;
        }
       else
        {// Reopen the swapchain pass instance for the HUD tail of the frame.
         // LOAD, not CLEAR: world pixels recorded earlier in THIS frame's
         // first pass instance must survive (GL's FBO rebind never clears).
         ensureSwapchainLoadRenderPass_();
         VkRenderPassBeginInfo rp{};
         rp.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
         rp.renderPass=swapchainLoadRenderPass_;
         rp.framebuffer=swapchainFramebuffers_[acquiredImageIndex_];
         rp.renderArea.offset.x=0;
         rp.renderArea.offset.y=0;
         rp.renderArea.extent=swapchainExtent_;
         rp.clearValueCount=0;
         vkCmdBeginRenderPass(activeCb_,&rp,VK_SUBPASS_CONTENTS_INLINE);
         VkViewport vp{};
         vp.x=0.0f;vp.y=0.0f;
         vp.width=(float)swapchainExtent_.width;
         vp.height=(float)swapchainExtent_.height;
         vp.minDepth=0.0f;vp.maxDepth=1.0f;
         vkCmdSetViewport(activeCb_,0,1,&vp);
         VkRect2D sci=applyPresentTransformToScissor_();
         vkCmdSetScissor(activeCb_,0,1,&sci);
        }
       rtActive_=false;
       currentRenderTarget_=0;
      }

    virtual void getPresentTransform(float& outScale,int& outOffsetX,
                                     int& outOffsetY) override
      {// Uniform-scale centered letterbox (D17.4a) — glBackend's math
       // verbatim, sourced from the recorded framebuffer size (kept current
       // by the size callback + every re-bootstrap commit). Identity at the
       // canonical size; aspect never stretched.
       float scale=1.0f;
       int ox=0,oy=0;
       if (window&&canonicalWidth_>0&&canonicalHeight_>0
           &&fbWidth_>0&&fbHeight_>0)
        {scale=((float)fbWidth_/canonicalWidth_
               <(float)fbHeight_/canonicalHeight_)
                ? (float)fbWidth_/canonicalWidth_
                : (float)fbHeight_/canonicalHeight_;
         ox=((int)fbWidth_-(int)(canonicalWidth_*scale))/2;
         oy=((int)fbHeight_-(int)(canonicalHeight_*scale))/2;
        }
       outScale=scale;outOffsetX=ox;outOffsetY=oy;
      }

    // ------------------------------------------------------------------
    // (4) Primitives
    // ------------------------------------------------------------------
    virtual void clear() override
      {// Benign no-op (render-pass clear owns this on VK from task 38).
      }

    virtual void drawLine(float x1,float y1,float x2,float y2,
                          float r,float g,float b,float width) override
      {// SC8, glBackend verbatim: width<2 = 2-vert LINE_LIST; width>1 =
       // filled quad expanded perpendicular in LOGICAL space (thickLineQuad
       // port) drawn through triPipeline.
       if (!passOpen_)
         return;
       float verts[30];
       int count;
       VkPipeline pipe;
       if (width<2.0f)
        {makeVert(verts,0,x1,y1,r,g,b);
         makeVert(verts,5,x2,y2,r,g,b);
         count=2;
         pipe=linePipeline;
        }
       else
        {thickLineQuad(verts,x1,y1,x2,y2,width*0.5f,r,g,b);
         count=6;
         pipe=triPipeline;
        }
       drawColorPrims_(pipe,verts,count);
      }

    virtual void drawPolygon(const float* verts,int count,
                             float r,float g,float b,bool fill) override
      {if (!passOpen_||!verts||count<2)
         return;
       if (!fill)
        {// GL_LINE_LOOP emulation: VK has no loop topology — emit the
         // closing segment explicitly (n+1 verts).
         float line[5*261];
         int n=count<260?count:260;
         for (int i=0;i<n;++i)
           makeVert(line,5*i,verts[2*i],verts[2*i+1],r,g,b);
         makeVert(line,5*n,verts[0],verts[1],r,g,b);
         drawColorPrims_(outlinePipeline,line,n+1);
         return;
        }
       // Ear-clipped triangles (glBackend port incl. the fan fallback).
       float pairs[2*1536];
       int ntri=earClip(verts,count,pairs);
       if (ntri<1)
        {float fan[5*256];
         int n=0;
         for (int i=1;i+1<count&&i<255;++i)
          {makeVert(fan,5*n++,verts[0],verts[1],r,g,b);
           makeVert(fan,5*n++,verts[2*i],verts[2*i+1],r,g,b);
           makeVert(fan,5*n++,verts[2*i+2],verts[2*i+3],r,g,b);
          }
         drawColorPrims_(triPipeline,fan,n);
         return;
        }
       float out[5*1536];
       for (int i=0;i<ntri*3;++i)
         makeVert(out,5*i,pairs[2*i],pairs[2*i+1],r,g,b);
       drawColorPrims_(triPipeline,out,ntri*3);
      }

    virtual void drawRect(float x,float y,float w,float h,
                          float r,float g,float b,bool fill) override
      {if (!passOpen_)
         return;
       float quad[10]={x,y, x+w,y, x+w,y+h, x,y+h};
       if (fill)
        {static const int idx[6]={0,1,2,0,2,3};
         float tris[30];
         for (int i=0;i<6;++i)
           makeVert(tris,5*i,quad[2*idx[i]],quad[2*idx[i]+1],r,g,b);
         drawColorPrims_(triPipeline,tris,6);
        }
       else
         drawPolygon(quad,4,r,g,b,false);
      }

    virtual void drawTriangles(const float* verts,int count,
                               TextureId tex) override
      {// D9 menu pair: interleaved [x y u v r g b]*count, WINDOW-space
       // coords — the SOLE window-space method (v6.1 rule), so the MVP here
       // is windowMVP_ (pixel coords -> NDC). The 7-float engine contract
       // repacks onto the pipeline's clean 8-float stride with alpha=1
       // (glBackend's overrun-alpha effective behavior; see initPipelines_).
       TexRef ref=lookupTex_(tex);
       if (!passOpen_||!verts||count<3||!ref.set)
         return;
       std::vector<float> packed((size_t)count*8);
       for (int i=0;i<count;++i)
        {const float* s=verts+(size_t)i*7;
         float* d=&packed[(size_t)i*8];
         d[0]=s[0];d[1]=s[1];d[2]=s[2];d[3]=s[3];
         d[4]=s[4];d[5]=s[5];d[6]=s[6];d[7]=1.0f;
        }
       drawTexPrims_(texPipeline,texPipelineLayout,ref.set,packed.data(),
                     count,windowMVP_());
      }

    // ------------------------------------------------------------------
    // (5) Text
    // ------------------------------------------------------------------
    virtual void drawStringOpaque(const char* text,float x,float y,int fontId,
                                  float r,float g,float b,
                                  float bgR,float bgG,float bgB) override
      {// Opaque-text mapping (D12): cell background quad THEN glyphs
       // (glBackend verbatim).
       if (!passOpen_||!validFont(fontId)||!text||!*text)
         return;
       float w=measureText(text,fontId);
       FontMetrics fm;
       getFontMetrics(fontId,fm);
       drawRect(x,y-fm.max_bounds.ascent,w,
                fm.max_bounds.ascent+fm.max_bounds.descent,bgR,bgG,bgB,true);
       drawGlyphs_(text,x,y,fontId,r,g,b);
      }

    virtual void drawStringTransparent(const char* text,float x,float y,
                                        int fontId,
                                        float r,float g,float b) override
      {// Transparent-text mapping (D12): glyphs only, no background cell.
       if (!passOpen_||!validFont(fontId)||!text||!*text)
         return;
       drawGlyphs_(text,x,y,fontId,r,g,b);
      }

    virtual float measureText(const char* text,int fontId) override
      {// Advance-width sum over the loaded TTF slot (stb metrics; glBackend
       // verbatim semantics). Total-width centering policy per D12/M5.
       // Returns 0 for invalid slot/text like the X11 backend.
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
      {// D13: the ONE texture entry point (xbmDecode output; channels
       // 1/3/4). VK path: staging upload -> device-local image -> view +
       // shared sampler + descriptor set. channels==3 expands to RGBA8
       // (documented divergence; content opaque).
       if (!device||!data||w<=0||h<=0||(channels!=1&&channels!=3&&channels!=4))
         return 0;
       VkFormat fmt=channels==1?VK_FORMAT_R8_UNORM:VK_FORMAT_R8G8B8A8_UNORM;
       size_t texelBytes=(size_t)w*h*(channels==1?1u:4u);
       // ---- staging buffer (host-visible) -----------------------------
       VkBufferCreateInfo bci{};
       bci.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
       bci.size=texelBytes;
       bci.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
       bci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
       VkBuffer staging=VK_NULL_HANDLE;
       if (!vkCheck(vkCreateBuffer(device,&bci,NULL,&staging),
                    "vkCreateBuffer(staging)"))
         return 0;
       VkMemoryRequirements mr;
       vkGetBufferMemoryRequirements(device,staging,&mr);
       uint32_t memType=findMemoryType_(mr.memoryTypeBits,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                       |VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
       VkMemoryAllocateInfo smai{};
       smai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
       smai.allocationSize=mr.size;
       smai.memoryTypeIndex=memType;
       VkDeviceMemory stagingMem=VK_NULL_HANDLE;
       void* mapped=NULL;
       if (memType==UINT32_MAX
           ||!vkCheck(vkAllocateMemory(device,&smai,NULL,&stagingMem),
                      "vkAllocateMemory(staging)")
           ||!vkCheck(vkBindBufferMemory(device,staging,stagingMem,0),
                    "vkBindBufferMemory(staging)")
           ||!vkCheck(vkMapMemory(device,stagingMem,0,texelBytes,0,&mapped),
                    "vkMapMemory(staging)"))
        {if (mapped) vkUnmapMemory(device,stagingMem);
         if (stagingMem!=VK_NULL_HANDLE)
           vkFreeMemory(device,stagingMem,NULL);
         vkDestroyBuffer(device,staging,NULL);
         return 0;
        }
       if (channels==3)
        {// expand RGB888 -> RGBA888 row by row
         uint8_t* dst=(uint8_t*)mapped;
         for (int y=0;y<h;++y)
          {const uint8_t* src=data+(size_t)y*w*3;
           uint8_t* drow=dst+(size_t)y*w*4;
           for (int x=0;x<w;++x)
            {drow[x*4+0]=src[x*3+0];
             drow[x*4+1]=src[x*3+1];
             drow[x*4+2]=src[x*3+2];
             drow[x*4+3]=255;
            }
          }
        }
       else
         memcpy(mapped,data,texelBytes);
       vkUnmapMemory(device,stagingMem);
       // ---- device-local image ----------------------------------------
       VkImageCreateInfo ici{};
       ici.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
       ici.imageType=VK_IMAGE_TYPE_2D;
       ici.format=fmt;
       ici.extent.width=(uint32_t)w;
       ici.extent.height=(uint32_t)h;
       ici.extent.depth=1;
       ici.mipLevels=1;
       ici.arrayLayers=1;
       ici.samples=VK_SAMPLE_COUNT_1_BIT;
       ici.tiling=VK_IMAGE_TILING_OPTIMAL;
       ici.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
       ici.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
       ici.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
       VkImage image=VK_NULL_HANDLE;
       if (!vkCheck(vkCreateImage(device,&ici,NULL,&image),
                    "vkCreateImage"))
        {vkDestroyBuffer(device,staging,NULL);
         vkFreeMemory(device,stagingMem,NULL);
         return 0;
        }
       vkGetImageMemoryRequirements(device,image,&mr);
       memType=findMemoryType_(mr.memoryTypeBits,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
       if (memType==UINT32_MAX)
         memType=findMemoryType_(mr.memoryTypeBits,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                 |VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
       VkDeviceMemory imageMem=VK_NULL_HANDLE;
       VkMemoryAllocateInfo mai{};
       mai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
       mai.allocationSize=mr.size;
       mai.memoryTypeIndex=memType;
       if (memType==UINT32_MAX
           ||!vkCheck(vkAllocateMemory(device,&mai,NULL,&imageMem),
                      "vkAllocateMemory(texture)")
           ||!vkCheck(vkBindImageMemory(device,image,imageMem,0),
                      "vkBindImageMemory"))
        {if (imageMem!=VK_NULL_HANDLE)
           vkFreeMemory(device,imageMem,NULL);
         vkDestroyImage(device,image,NULL);
         vkDestroyBuffer(device,staging,NULL);
         vkFreeMemory(device,stagingMem,NULL);
         return 0;
        }
       // ---- one-shot copy + layout transitions -------------------------
       // When passOpen_ (mid-frame), the frame's command buffer may already be
       // submitted to graphicsQueue. The upload uses its own one-shot buffer
       // and fence-wait, so it's safe — but we must ensure the device is idle
       // before touching the shared commandPool's transient allocations.
       // This is a cold path (ship icon creation mid-game), so the cost is
       // negligible and correctness trumps throughput here.
       if (passOpen_)
         vkDeviceWaitIdle(device);
       bool uploaded=uploadTexture_(staging,image,(uint32_t)w,(uint32_t)h);
       vkDestroyBuffer(device,staging,NULL);
       vkFreeMemory(device,stagingMem,NULL);
       if (!uploaded)
        {vkFreeMemory(device,imageMem,NULL);
         vkDestroyImage(device,image,NULL);
         fprintf(stderr,"vkBackend: texture upload failed\n");
         return 0;
        }
       // ---- view + sampler set ----------------------------------------
       VkImageViewCreateInfo vci{};
       vci.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
       vci.image=image;
       vci.viewType=VK_IMAGE_VIEW_TYPE_2D;
       vci.format=fmt;
       vci.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
       vci.subresourceRange.levelCount=1;
       vci.subresourceRange.layerCount=1;
       VkImageView view=VK_NULL_HANDLE;
       if (!vkCheck(vkCreateImageView(device,&vci,NULL,&view),
                    "vkCreateImageView(texture)"))
        {vkFreeMemory(device,imageMem,NULL);
         vkDestroyImage(device,image,NULL);
         return 0;
        }
       VkDescriptorSetLayout layouts[]={texSetLayout_};
       VkDescriptorSetAllocateInfo dai{};
       dai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
       dai.descriptorPool=descriptorPool;
       dai.descriptorSetCount=1;
       dai.pSetLayouts=layouts;
       VkDescriptorSet set=VK_NULL_HANDLE;
       if (!vkCheck(vkAllocateDescriptorSets(device,&dai,&set),
                    "vkAllocateDescriptorSets(texture)"))
        {vkDestroyImageView(device,view,NULL);
         vkFreeMemory(device,imageMem,NULL);
         vkDestroyImage(device,image,NULL);
         return 0;
        }
       VkDescriptorImageInfo ii{};
       ii.sampler=sharedSampler_;
       ii.imageView=view;
       ii.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
       VkWriteDescriptorSet wr{};
       wr.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
       wr.dstSet=set;
       wr.dstBinding=0;
       wr.descriptorCount=1;
       wr.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
       wr.pImageInfo=&ii;
       vkUpdateDescriptorSets(device,1,&wr,0,NULL);
       TextureId id=nextTextureId_++;
       TexEntry e;
       e.image=image;
       e.mem=imageMem;
       e.view=view;
       e.set=set;
       textures_[id]=e;
       return id;
      }

    virtual void drawTexture(TextureId tex,float x,float y,float w,float h,
                             float alpha) override
      {// Textured quad, alpha-blended (D8 canonical order governs call
       // order). Tint=(1,1,1,alpha): through FragColor = texture*vColor this
       // reproduces GL's vec4(c.rgb,c.a*uAlpha) exactly.
       TexRef ref=lookupTex_(tex);
       if (!passOpen_||!ref.set)
         return;
       float verts[48];
       static const int idx[6]={0,1,2,0,2,3};
       const float fx[4]={x,x+w,x+w,x};
       const float fy[4]={y,y,y+h,y+h};
       const float fu[4]={0.0f,1.0f,1.0f,0.0f};
       const float fv[4]={0.0f,0.0f,1.0f,1.0f};
       for (int i=0;i<6;++i)
        {float* v=verts+8*i;
         v[0]=fx[idx[i]];v[1]=fy[idx[i]];
         v[2]=fu[idx[i]];v[3]=fv[idx[i]];
         v[4]=1.0f;v[5]=1.0f;v[6]=1.0f;
         v[7]=alpha;            // tint = (1,1,1,uAlpha)
        }
       drawTexPrims_(texPipeline,texPipelineLayout,ref.set,verts,6,
                     presentMVP_());
      }

    virtual void drawTextureMasked(TextureId content,TextureId mask,
                                    float x,float y,float w,float h) override
      {// D6 VK half: content quad discarded where the R8 mask is off
       // (<0.5) — glBackend maskedProgram_ semantics on the masked pipeline.
       auto c=lookupTex_(content);
       auto m=lookupTex_(mask);
       if (!passOpen_||!c.set||!m.set)
         return;
       uint64_t key=((uint64_t)content<<32)|(uint64_t)mask;
       VkDescriptorSet set;
       auto cached=maskedSets_.find(key);
       if (cached!=maskedSets_.end())
         set=cached->second;
       else
        {VkDescriptorSetLayout layouts[]={maskedSetLayout_};
         VkDescriptorSetAllocateInfo dai{};
         dai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
         dai.descriptorPool=descriptorPool;
         dai.descriptorSetCount=1;
         dai.pSetLayouts=layouts;
         if (!vkCheck(vkAllocateDescriptorSets(device,&dai,&set),
                      "vkAllocateDescriptorSets(masked)"))
           return;
         VkDescriptorImageInfo infos[2]={};
         infos[0].sampler=sharedSampler_;
         infos[0].imageView=c.view;
         infos[0].imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
         infos[1].sampler=sharedSampler_;
         infos[1].imageView=m.view;
         infos[1].imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
         VkWriteDescriptorSet wrs[2]={};
         for (int i=0;i<2;++i)
          {wrs[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
           wrs[i].dstSet=set;
           wrs[i].dstBinding=i;
           wrs[i].descriptorCount=1;
           wrs[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
           wrs[i].pImageInfo=&infos[i];
          }
         vkUpdateDescriptorSets(device,2,wrs,0,NULL);
         maskedSets_[key]=set;
        }
       float verts[48];
       static const int idx[6]={0,1,2,0,2,3};
       const float fx[4]={x,x+w,x+w,x};
       const float fy[4]={y,y,y+h,y+h};
       const float fu[4]={0.0f,1.0f,1.0f,0.0f};
       const float fv[4]={0.0f,0.0f,1.0f,1.0f};
       for (int i=0;i<6;++i)
        {float* v=verts+8*i;
         v[0]=fx[idx[i]];v[1]=fy[idx[i]];
         v[2]=fu[idx[i]];v[3]=fv[idx[i]];
         v[4]=1.0f;v[5]=1.0f;v[6]=1.0f;v[7]=1.0f;
        }
       drawTexPrims_(maskedPipeline,maskedPipelineLayout_,set,verts,6,
                     presentMVP_());
      }

    virtual void deleteTexture(TextureId tex) override
      {auto it=textures_.find(tex);
       if (it==textures_.end()&&renderTargets_.find(tex)==renderTargets_.end())
         return;
       if (device==VK_NULL_HANDLE)
         return;
       // The texture's descriptor set may still be referenced by a submitted
       // (not-yet-complete) command buffer; destroying the view/image here
       // would trip VUID-01026. Deletion is rare (dtors, AlterIcon), so an
       // idle wait is the cheap correct sync. Task 43: UNCONDITIONAL — an
       // early game exit ('q'/close) leaves passOpen_ true with frames still
       // in flight, and the guarded skip then raced the pending cb.
       vkDeviceWaitIdle(device);
       auto rt=renderTargets_.find(tex);
       if (rt!=renderTargets_.end())
        {vkDestroyFramebuffer(device,rt->second.fb,NULL);
         vkDestroyImageView(device,rt->second.view,NULL);
         vkFreeMemory(device,rt->second.mem,NULL);
         vkDestroyImage(device,rt->second.image,NULL);
         renderTargets_.erase(rt);
         return;
        }
       auto entry=textures_.find(tex);
       if (entry!=textures_.end())
        {vkDestroyImageView(device,entry->second.view,NULL);
         vkFreeMemory(device,entry->second.mem,NULL);
         vkDestroyImage(device,entry->second.image,NULL);
         textures_.erase(entry);   // descriptor set returns with pool reset
        }
      }

    virtual TextureId createTextureFromRGBA32(const uint8_t* pixels,int w,
                                              int h) override
      {// D9 menu pair: Dear ImGui font atlas / UI textures (RGBA8888).
       return createTextureFromBitmap(pixels,w,h,4);
      }

    // ------------------------------------------------------------------
    // (7) Transform
    // ------------------------------------------------------------------
    virtual void setTransform(float tx,float ty,float angleRadians) override
      {// GL/VK-only semantics (m19): model transform applied BEFORE the
       // present transform; rotation is about the object's OWN origin, then
       // translation (D2 rotator placement). Consumed per-draw by
       // presentMVP_ via the push-constant uniform block. Counted for the
       // m13 degenerate-path assertion (NonRot static textures issue 0).
       ++transformCallCount_;
       tx_=tx;ty_=ty;angle_=angleRadians;
      }

    virtual void resetTransform() override
      {tx_=0.0f;ty_=0.0f;angle_=0.0f;   // identity (glBackend verbatim)
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
    VkCommandPool commandPool;           // task 39: TRANSIENT+RESET_COMMAND_
                                         // BUFFER, graphicsFamily
    VkRenderPass renderPass;             // task 40: ONE pass, color attachment
                                         // = swapchain image, finalLayout
                                         // PRESENT_SRC_KHR, loadOp CLEAR black
    VkPipeline linePipeline;             // task 41: LINE_LIST, thin lines
    VkPipeline triPipeline;              // task 41: TRIANGLE_LIST+FILL (thick
                                         // lines, filled polys/rects)
    VkPipeline outlinePipeline;          // task 41: LINE_LIST+POLYGON_MODE_LINE
    VkPipeline texPipeline;              // task 41: textured quads/triangles
    VkPipeline maskedPipeline;           // task 42: content+R8-mask discard
    VkPipelineLayout colorPipelineLayout;// push-constant uMVP only
    VkPipelineLayout texPipelineLayout;  // push-constant uMVP + sampler set 0
    VkDescriptorPool descriptorPool;     // combined-image-sampler sets
    unsigned shaderModulesLoaded_;       // task-41 init evidence (0..4)
    unsigned frameCount_;                // completed submit+present cycles
    unsigned rebootstrapCount_;          // swapchain rebuilds performed
    unsigned stallSkipCount_;            // recovered stalls (skipped frames)
    bool presentationSuspended_;         // degenerate extent (minimized)
    bool fatalStallShutdown_;            // stall-limit shutdown has run
    unsigned transformCallCount_;        // m13 evidence: setTransform calls

    // Task-40 QA hooks (see header comment; NULL = absent, zero cost).
    void (*qaInRenderPassHook)(VkCommandBuffer,uint32_t,VkImage,void*);
    void* qaInRenderPassHookUserData;
    void (*qaAfterRenderPassHook)(VkCommandBuffer,uint32_t,VkImage,void*);
    void* qaAfterRenderPassHookUserData;

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
    std::vector<VkFramebuffer> swapchainFramebuffers_;   // task 40, per image
    VkFormat renderPassFormat_;          // format renderPass was built for
    VkRenderPass offscreenRenderPass_;   // task 42: LOAD load,
                                         // SHADER_READ_ONLY final layout
    VkRenderPass swapchainLoadRenderPass_;  // task 42: endRenderTo reopen
    bool scissorActive_;                 // setScissorRect state (D7/D8)
    int scissorRect_[4];                 // LOGICAL CLIENT {x,y,w,h}

    // Task-41 draw resources. Shader modules hold the init-compiled SPIR-V;
    // pipelines/layouts/pool are public above. The vertex scratch buffers
    // are per in-flight frame slot (device-level like the sync objects).
    VkShaderModule primVs_,primFs_,texVs_,texFs_;
    VkShaderModule maskedFs_;            // task 42 (masked.frag)
    char shaderDir_[256];                 // resolved at init: dir holding pre-built
                                         // obj/VK/spv/*.spv (portable exe-dir
                                         // resolution, no /proc/self/exe)
    VkDescriptorSetLayout texSetLayout_;
    VkDescriptorSetLayout maskedSetLayout_;   // task 42: bindings 0+1
    VkPipelineLayout maskedPipelineLayout_;
    VkSampler sharedSampler_;            // linear + clamp-to-edge (GL params)
    struct TexEntry
      {VkImage image;
       VkDeviceMemory mem;
       VkImageView view;
       VkDescriptorSet set;
      };
    std::map<TextureId,TexEntry> textures_;
    struct RTEntry                       // task 42 render targets (M1)
      {VkImage image;
       VkDeviceMemory mem;
       VkImageView view;
       VkFramebuffer fb;
       VkDescriptorSet set;              // sampling after endRenderTo
       int w,h;
       VkImageLayout layout;
      };
    std::map<RenderTargetId,RTEntry> renderTargets_;
    std::map<uint64_t,VkDescriptorSet> maskedSets_;   // (content<<32|mask)
    TextureId nextTextureId_;
    TextureId atlasTex_;                 // task 42 text atlas (RGBA8, coverage
                                         // in all 4 channels — see header)
    struct GlyphInfo
      {float u0,v0,u1,v1;                // atlas UVs
       float w,h;                        // bitmap size in logical px
       float advance;                    // x advance (logical px)
       float bearingX,bearingY;          // pen -> bitmap top-left offset
       bool present;
      };
    GlyphInfo glyphs_[4][128];
    int slotShelfH_[4];
    static constexpr VkDeviceSize kVertBufBytes=(VkDeviceSize)4<<20;
    VkBuffer vertBuf_[kMaxFramesInFlight];
    VkDeviceMemory vertMem_[kMaxFramesInFlight];
    void* vertMapped_[kMaxFramesInFlight];
    VkDeviceSize vertUsed_[kMaxFramesInFlight];
    float tx_,ty_,angle_;                // setTransform state (m19)
    float canvasOffX_,canvasOffY_;       // task 43 canvas-domain shift
                                         // (setCanvasOffset; presentMVP_)
    float presentScale_;                 // present transform (letterbox,
    int presentOffX_,presentOffY_;       // D17.4a math since task 42)
    bool rtActive_;                      // inside begin/endRenderTo
    int rtWidth_,rtHeight_;              // active target dims (presentMVP_)
    RenderTargetId currentRenderTarget_;
    bool rtOneShot_;                     // init-time one-shot recording mode
    VkCommandBuffer oneShotCb_;
    VkCommandBuffer activeCb_;           // cb draws record into (frame slot
                                         // or one-shot)
    bool passOpen_;                      // command buffer open between
                                         // beginFrame preamble/endFrame
                                         // postamble; draws record only then
    float mvp_[16];                      // presentMVP_ scratch
    float winMvp_[16];                   // windowMVP_ scratch

    // Task-42 D16 event ring + GLFW input bridge state.
    static const int maxEventQueue_=256;
    GameEvent eventQueue_[maxEventQueue_];
    int eventHead_,eventTail_;
    double lastCursorX_,lastCursorY_;
    bool cursorValid_;

    // Task-39 frame-sync objects (device-level; independent of the
    // swapchain — re-bootstrap never recreates them). EXCEPTION:
    // renderFinishedSemaphores_ is PER SWAPCHAIN IMAGE (indexed by the
    // acquired image index, not the frame slot) — VUID-
    // vkQueueSubmit-pSignalSemaphores-00067: a present-wait semaphore may
    // still be in use by the presentation engine when a per-frame scheme
    // would re-signal it; per-image indexing is the spec-recommended fix
    // (created/destroyed with the swapchain in createSwapchain_).
    VkCommandBuffer commandBuffers_[kMaxFramesInFlight];
    VkFence inFlightFences_[kMaxFramesInFlight];
    VkSemaphore imageAvailableSemaphores_[kMaxFramesInFlight];
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    uint32_t acquiredImageIndex_;
    uint32_t currentFrame_;
    bool frameAcquired_;
    bool sessionDead_;
    bool swapchainTransferDstSupported_;
    bool swapchainTransferSrcSupported_;   // task-40 QA readback + task-42

    int consecutiveStallCount_;

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
    // semantics (same files, same pixel sizes, same portable exe-dir
    // resolution via resolveExeDir_). Metrics only; glyph rasterization is task 42.
    // ------------------------------------------------------------------
    bool loadFonts_()
      {static char fontDir[4096];
       static bool fontDirInit=false;
       if (!fontDirInit)
        {if (!resolveExeDir_(fontDir,sizeof fontDir))
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

    bool validFont(int fontId) const
      {return fontId>=0&&fontId<=3&&fontInfo_[fontId]!=NULL
              &&fontData_[fontId]!=NULL;
      }

    struct TexRef
      {VkImageView view;
       VkDescriptorSet set;
      };

    TexRef lookupTex_(TextureId id)
      {TexRef r{VK_NULL_HANDLE,VK_NULL_HANDLE};
       auto it=textures_.find(id);
       if (it!=textures_.end())
        {r.view=it->second.view;
         r.set=it->second.set;
         return r;
        }
       auto rt=renderTargets_.find(id);
       if (rt!=renderTargets_.end())
        {r.view=rt->second.view;
         r.set=rt->second.set;
        }
       return r;
      }

    // ------------------------------------------------------------------
    // Task 42 text: shelf-packed coverage atlas + per-glyph quads
    // (glBackend initText/drawGlyphs port). The atlas uploads as RGBA8 with
    // the coverage byte in ALL FOUR channels: through FragColor =
    // texture*vColor and the SRC_ALPHA blend this reproduces glBackend's
    // vec4(uColor*a,a) math exactly (rgb = tint*a^2 + dst*(1-a)).
    // ------------------------------------------------------------------
    bool initText_()
      {const int AW=2048,AH=512;
       std::vector<uint8_t> atlas((size_t)AW*AH*4,0);
       int shelfY[4];
       int curX=0;
       for (int s=0;s<4;++s)
        {shelfY[s]=s==0?0:shelfY[s-1]+slotShelfH_[s-1];
         curX=0;
         slotShelfH_[s]=0;
         for (int cp=32;cp<127;++cp)
          {int w=0,h=0,xoff=0,yoff=0;
           uint8_t* bmp=stbtt_GetCodepointBitmap(fontInfo_[s],fontScale_[s],
                                                 fontScale_[s],cp,
                                                 &w,&h,&xoff,&yoff);
           if (w>0&&h>0)
            {if (curX+w+1>=AW)                 // wrap inside the slot band
              {curX=0;
               shelfY[s]+=slotShelfH_[s]+1;
               slotShelfH_[s]=0;
              }
             for (int row=0;row<h;++row)
              {uint8_t* dst=&atlas[((size_t)(shelfY[s]+row)*AW+curX)*4];
               const uint8_t* src=bmp+row*w;
               for (int x=0;x<w;++x)
                {dst[x*4+0]=src[x];
                 dst[x*4+1]=src[x];
                 dst[x*4+2]=src[x];
                 dst[x*4+3]=src[x];
                }
              }
             GlyphInfo& g=glyphs_[s][cp];
             g.u0=(float)curX/AW;      g.v0=(float)shelfY[s]/AH;
             g.u1=(float)(curX+w)/AW;  g.v1=(float)(shelfY[s]+h)/AH;
             g.w=(float)w;g.h=(float)h;
             g.bearingX=(float)xoff;g.bearingY=(float)yoff;
             g.present=true;
             if (h>slotShelfH_[s])
               slotShelfH_[s]=h;
             curX+=w+1;
            }
           stbtt_FreeBitmap(bmp,NULL);
           int adv=0,lsb=0;
           stbtt_GetCodepointHMetrics(fontInfo_[s],cp,&adv,&lsb);
           glyphs_[s][cp].advance=adv*fontScale_[s];
          }
        }
       atlasTex_=createTextureFromBitmap(atlas.data(),AW,AH,4);
       return atlasTex_!=0;
      }

    void drawGlyphs_(const char* text,float x,float y,int fontId,
                     float r,float g,float b)
      {// Pen starts at (x, y) with y = BASELINE (X11 text semantics).
       // 6 expanded triangle verts x 8 floats per quad, up to 512 quads per
       // call — the stride discipline of the task-36 Defect-A fix.
       TexRef atlas=lookupTex_(atlasTex_);
       if (!atlas.set)
         return;
       float penX=x;
       float verts[6*8*512];
       int quads=0;
       for (const char* p=text;*p&&quads<512;++p)
        {GlyphInfo& gl=glyphs_[fontId][(unsigned char)*p];
         if (gl.present)
          {float x0=penX+gl.bearingX,y0=y+gl.bearingY;   // bearingY <= 0
           const float fx[4]={x0,x0+gl.w,x0+gl.w,x0};
           const float fy[4]={y0,y0,y0+gl.h,y0+gl.h};
           const float fu[4]={gl.u0,gl.u1,gl.u1,gl.u0};
           const float fv[4]={gl.v0,gl.v0,gl.v1,gl.v1};
           static const int idx[6]={0,1,2,0,2,3};
           for (int i=0;i<6;++i)
            {float* v=verts+48*quads+8*i;
             v[0]=fx[idx[i]];v[1]=fy[idx[i]];
             v[2]=fu[idx[i]];v[3]=fv[idx[i]];
             v[4]=r;v[5]=g;v[6]=b;v[7]=1.0f;
            }
           ++quads;
          }
         penX+=gl.advance;
        }
       if (!quads)
         return;
       drawTexPrims_(texPipeline,texPipelineLayout,atlas.set,verts,
                     quads*6,presentMVP_());
      }

    // ------------------------------------------------------------------
    // Task 42 render-target plumbing (M1 VK half).
    // ------------------------------------------------------------------
    void ensureOffscreenRenderPass_()
      {if (offscreenRenderPass_!=VK_NULL_HANDLE
           ||swapchainFormat_==VK_FORMAT_UNDEFINED)
         return;
       // LOAD (not DONT_CARE/CLEAR): ShipYard::AddShip/RemoveShip render
       // incrementally and rely on the target's persisted contents between
       // begin/end pairs — GL's FBO bind never discards either. First use
       // rides the UNDEFINED->COLOR_ATTACHMENT barrier in rtToColorBarrier_
       // (garbage contents, immediately overwritten by the caller's full
       // fill — every consumer paints the whole target first).
       VkAttachmentDescription color{};
       color.format=swapchainFormat_;
       color.samples=VK_SAMPLE_COUNT_1_BIT;
       color.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
       color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
       color.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
       color.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
       color.initialLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
       color.finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
       VkAttachmentReference colorRef{};
       colorRef.attachment=0;
       colorRef.layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
       VkSubpassDescription subpass{};
       subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
       subpass.colorAttachmentCount=1;
       subpass.pColorAttachments=&colorRef;
       VkSubpassDependency dep{};
       dep.srcSubpass=VK_SUBPASS_EXTERNAL;
       dep.dstSubpass=0;
       dep.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
       dep.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
       dep.srcAccessMask=0;
       dep.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
       VkRenderPassCreateInfo rpi{};
       rpi.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
       rpi.attachmentCount=1;
       rpi.pAttachments=&color;
       rpi.subpassCount=1;
       rpi.pSubpasses=&subpass;
       rpi.dependencyCount=1;
       rpi.pDependencies=&dep;
       if (!vkCheck(vkCreateRenderPass(device,&rpi,NULL,
                    &offscreenRenderPass_),
                    "vkCreateRenderPass(offscreen)"))
         offscreenRenderPass_=VK_NULL_HANDLE;
      }

    void rtToColorBarrier_(RTEntry& rt)
      {VkImageMemoryBarrier bar{};
       bar.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
       bar.srcAccessMask=VK_ACCESS_SHADER_READ_BIT;
       bar.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
       bar.oldLayout=rt.layout==VK_IMAGE_LAYOUT_UNDEFINED
                       ? VK_IMAGE_LAYOUT_UNDEFINED
                       : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
       bar.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
       bar.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
       bar.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
       bar.image=rt.image;
       bar.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
       bar.subresourceRange.levelCount=1;
       bar.subresourceRange.layerCount=1;
       vkCmdPipelineBarrier(activeCb_,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                            0,0,NULL,0,NULL,1,&bar);
       rt.layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      }

    void beginOffscreenPass_(RTEntry& rt)
      {VkRenderPassBeginInfo rp{};
       rp.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
       rp.renderPass=offscreenRenderPass_;
       rp.framebuffer=rt.fb;
       rp.renderArea.offset.x=0;
       rp.renderArea.offset.y=0;
       rp.renderArea.extent.width=(uint32_t)rt.w;
       rp.renderArea.extent.height=(uint32_t)rt.h;
       rp.clearValueCount=0;
       vkCmdBeginRenderPass(activeCb_,&rp,VK_SUBPASS_CONTENTS_INLINE);
       VkViewport vp{};
       vp.x=0.0f;vp.y=0.0f;
       vp.width=(float)rt.w;
       vp.height=(float)rt.h;
       vp.minDepth=0.0f;vp.maxDepth=1.0f;
       vkCmdSetViewport(activeCb_,0,1,&vp);
       VkRect2D sci{};
       sci.offset.x=0;
       sci.offset.y=0;
       sci.extent.width=(uint32_t)rt.w;
       sci.extent.height=(uint32_t)rt.h;
       vkCmdSetScissor(activeCb_,0,1,&sci);
      }

    void destroyRenderTargets_()
      {for (auto& kv:renderTargets_)
        {vkDestroyFramebuffer(device,kv.second.fb,NULL);
         vkDestroyImageView(device,kv.second.view,NULL);
         vkFreeMemory(device,kv.second.mem,NULL);
         vkDestroyImage(device,kv.second.image,NULL);
        }
       renderTargets_.clear();
       maskedSets_.clear();
       rtActive_=false;
       currentRenderTarget_=0;
      }

    // Second swapchain-facing pass: identical to the task-40 CLEAR pass
    // except loadOp LOAD + PRESENT_SRC initial layout — used ONLY by the
    // endRenderTo reopen so already-recorded world pixels survive the pass
    // switch. Same attachment format/count keeps every pipeline compatible.
    void ensureSwapchainLoadRenderPass_()
      {if (swapchainLoadRenderPass_!=VK_NULL_HANDLE)
         return;
       VkAttachmentDescription color{};
       color.format=swapchainFormat_;
       color.samples=VK_SAMPLE_COUNT_1_BIT;
       color.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
       color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
       color.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
       color.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
       color.initialLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
       color.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
       VkAttachmentReference colorRef{};
       colorRef.attachment=0;
       colorRef.layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
       VkSubpassDescription subpass{};
       subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
       subpass.colorAttachmentCount=1;
       subpass.pColorAttachments=&colorRef;
        VkSubpassDependency dep{};
        dep.srcSubpass=VK_SUBPASS_EXTERNAL;
        dep.dstSubpass=0;
        dep.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        // Task 43: MUST match createRenderPass_'s external dependency
        // byte-for-byte — this pass reopens a framebuffer created for THE
        // task-40 pass, and validation (VUID-00904) rejects begin/pass
        // pairs whose external srcAccessMask chains disagree.
        dep.srcAccessMask=0;
        dep.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
       VkRenderPassCreateInfo rpi{};
       rpi.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
       rpi.attachmentCount=1;
       rpi.pAttachments=&color;
       rpi.subpassCount=1;
       rpi.pSubpasses=&subpass;
       rpi.dependencyCount=1;
       rpi.pDependencies=&dep;
       if (!vkCheck(vkCreateRenderPass(device,&rpi,NULL,
                    &swapchainLoadRenderPass_),
                    "vkCreateRenderPass(swapchain-load)"))
         swapchainLoadRenderPass_=VK_NULL_HANDLE;
      }

    // ------------------------------------------------------------------
    // Task 42 D16 input bridge (GL task-44a mouse bridge + the task-35 US-
    // layout key table, both ported verbatim). Enqueues apply the INVERSE
    // present transform so the domain speaks LOGICAL client coords.
    // ------------------------------------------------------------------
    static void keyTrampoline_(GLFWwindow*,int key,int,int action,int)
     {if (action!=GLFW_PRESS&&action!=GLFW_RELEASE)
        return;                        // GLFW_REPEAT dropped at the boundary
      char character=0;
      switch(key)
       {case GLFW_KEY_S: character='s'; break;
        case GLFW_KEY_Q: character='q'; break;
        case GLFW_KEY_H: character='h'; break;
        case GLFW_KEY_E: character='e'; break;
        case GLFW_KEY_R: character='r'; break;
        case GLFW_KEY_O: character='o'; break;
        case GLFW_KEY_P: character='p'; break;
        case GLFW_KEY_N: character='n'; break;
        case GLFW_KEY_SPACE: character=' '; break;
       }
      if (character&&s_instance)
        s_instance->enqueueEvent_(action==GLFW_PRESS?GameEvent::KeyDown
                                                  :GameEvent::KeyUp,
                                  0,0,0,character);
     }

    static void cursorPosTrampoline_(GLFWwindow*,double x,double y)
     {if (s_instance)
        s_instance->enqueueMouseMove_(x,y);
     }

    static void mouseButtonTrampoline_(GLFWwindow*,int button,int action,int)
     {if (s_instance&&button==GLFW_MOUSE_BUTTON_LEFT)
        s_instance->enqueueMouseButton_(action==GLFW_PRESS);
     }

    static void cursorEnterTrampoline_(GLFWwindow*,int entered)
     {if (s_instance)
        s_instance->enqueueCursorEnter_(entered==GLFW_TRUE);
     }

    void enqueueMouseMove_(double x,double y)
     {float scale=1.0f;
      int offsetX=0,offsetY=0;
      getPresentTransform(scale,offsetX,offsetY);
      lastCursorX_=x;lastCursorY_=y;cursorValid_=true;
      enqueueEvent_(GameEvent::MouseMove,
                    (int)((x-offsetX)/scale),(int)((y-offsetY)/scale));
     }

    void enqueueMouseButton_(bool pressed)
     {int x=0,y=0;
      if (cursorValid_)
       {float scale=1.0f;
        int offsetX=0,offsetY=0;
        getPresentTransform(scale,offsetX,offsetY);
        x=(int)((lastCursorX_-offsetX)/scale);
        y=(int)((lastCursorY_-offsetY)/scale);
       }
      enqueueEvent_(pressed?GameEvent::MouseDown:GameEvent::MouseUp,x,y,1);
     }

    void enqueueCursorEnter_(bool entered)
     {enqueueEvent_(entered?GameEvent::CursorEnter:GameEvent::CursorLeave,
                    cursorValid_?(int)lastCursorX_:0,
                    cursorValid_?(int)lastCursorY_:0);
     }

    void enqueueEvent_(GameEvent::Type type,int x,int y,int key=0,
                       char character=0)
     {const int next=(eventTail_+1)%maxEventQueue_;
      if (next==eventHead_)
        return;                              // overflow drops (never blocks)
      GameEvent& event=eventQueue_[eventTail_];
      event.type=type;
      event.key=key;
      event.character=character;
      event.x=x;
      event.y=y;
      eventTail_=next;
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
       glfwSetKeyCallback(window,keyTrampoline_);
       glfwSetCursorPosCallback(window,cursorPosTrampoline_);
       glfwSetMouseButtonCallback(window,mouseButtonTrampoline_);
       glfwSetCursorEnterCallback(window,cursorEnterTrampoline_);
       glfwGetFramebufferSize(window,&fbWidth_,&fbHeight_);
       recomputePresentTransform_();
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
    //   task 39: oldSwapchain-chained rebuilds pass the live swapchain in;
    //           the new one is built fully (images + views) BEFORE the old
    //           is retired/destroyed at the commit point. TRANSFER_DST
    //           usage is kept when the surface supports it (task 42's
    //           offscreen blits are the remaining consumer; the task-40
    //           render pass no longer needs it).
    //   task 40: the commit point destroys old framebuffers BEFORE old
    //           views, then ensureRenderPassAndFramebuffers_ builds the
    //           render pass (format-change-aware) + per-image framebuffers.
    // ------------------------------------------------------------------
    bool createSwapchain_(VkSwapchainKHR oldSwapchain)
      {VkSurfaceCapabilitiesKHR caps{};
       if (!vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice,
                    surface,&caps),
                    "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
         return false;
       if (!(caps.supportedUsageFlags&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        {fprintf(stderr,"vkBackend: surface lacks COLOR_ATTACHMENT usage\n");
         return false;
        }
       swapchainTransferDstSupported_=
         (caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT)!=0;
       // TRANSFER_SRC: consumed by the task-40 QA readback (vkpass.C copies
       // a presented frame to a host-visible buffer) and later by task-42
       // offscreen blits. Added only when the surface supports it.
       swapchainTransferSrcSupported_=
         (caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)!=0;

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
        sci.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                       |(swapchainTransferDstSupported_
                          ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0)
                       |(swapchainTransferSrcSupported_
                          ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
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
       sci.oldSwapchain=oldSwapchain;   // task-39 re-bootstrap chains here
       VkSwapchainKHR newSwapchain=VK_NULL_HANDLE;
       if (!vkCheck(vkCreateSwapchainKHR(device,&sci,NULL,&newSwapchain),
                    "vkCreateSwapchainKHR"))
         return false;

       uint32_t nimgs=0;
       if (!vkCheck(vkGetSwapchainImagesKHR(device,newSwapchain,&nimgs,
                    NULL),
                    "vkGetSwapchainImagesKHR(count)"))
         return false;
       std::vector<VkImage> newImages(nimgs);
       if (!vkCheck(vkGetSwapchainImagesKHR(device,newSwapchain,&nimgs,
                    newImages.data()),
                    "vkGetSwapchainImagesKHR"))
         return false;
       std::vector<VkImageView> newViews;
       newViews.reserve(nimgs);
       for (uint32_t i=0;i<nimgs;++i)
        {VkImageViewCreateInfo vci{};
         vci.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
         vci.image=newImages[i];
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
          {vkDestroySwapchainKHR(device,newSwapchain,NULL);
           for (size_t v=0;v<newViews.size();++v)
             vkDestroyImageView(device,newViews[v],NULL);
           return false;
          }
         newViews.push_back(view);
        }

        // Commit point: everything for the new swapchain exists — retire the
        // old one (the implementation defers its actual destruction until
        // the presentation engine is done with retired images) and publish.
        // Task 40: framebuffers reference views, so they die FIRST.
        destroySwapchainFramebuffers_();
        for (size_t i=0;i<swapchainImageViews_.size();++i)
         vkDestroyImageView(device,swapchainImageViews_[i],NULL);
       swapchainImageViews_.clear();
       swapchainImages_.clear();
       if (oldSwapchain!=VK_NULL_HANDLE)
         vkDestroySwapchainKHR(device,oldSwapchain,NULL);
       destroyRenderFinishedSemaphores_();
       swapchain=newSwapchain;
       swapchainImages_.swap(newImages);
       swapchainImageViews_.swap(newViews);

       // Per-image render-finished semaphores (VUID-
       // vkQueueSubmit-pSignalSemaphores-00067 fix): one per swapchain
       // image, indexed by the acquired image index in endFrame.
       VkSemaphoreCreateInfo ssi{};
       ssi.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
       renderFinishedSemaphores_.resize(nimgs,VK_NULL_HANDLE);
       for (uint32_t i=0;i<nimgs;++i)
        {if (!vkCheck(vkCreateSemaphore(device,&ssi,NULL,
                       &renderFinishedSemaphores_[i]),
                      "vkCreateSemaphore(render-finished)"))
          {renderFinishedSemaphores_.clear();
           return false;
          }
        }

        swapchainImageCount_=nimgs;
         swapchainFormat_=chosen.format;
         swapchainColorSpace_=chosen.colorSpace;
         swapchainExtent_=ext;
         swapchainDirty_=false;
         recomputePresentTransform_();   // D17.4a: letterbox follows extent
        // Task 40: render pass (reused unless the format changed) + one
        // framebuffer per new view. Living here means BOTH the initial
        // bootstrap AND every task-39 re-bootstrap recreate them.
        if (!ensureRenderPassAndFramebuffers_())
          return false;
        return true;
       }

    // ------------------------------------------------------------------
    // Task-39 frame sync — object creation/teardown + re-bootstrap.
    // ------------------------------------------------------------------
    bool initFrameSync_()
      {VkCommandPoolCreateInfo pci{};
       pci.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
       pci.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
                |VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
       pci.queueFamilyIndex=graphicsFamily;
       if (!vkCheck(vkCreateCommandPool(device,&pci,NULL,&commandPool),
                    "vkCreateCommandPool"))
         return false;
       VkCommandBufferAllocateInfo cai{};
       cai.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
       cai.commandPool=commandPool;
       cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
       cai.commandBufferCount=(uint32_t)kMaxFramesInFlight;
       if (!vkCheck(vkAllocateCommandBuffers(device,&cai,commandBuffers_),
                    "vkAllocateCommandBuffers"))
         return false;
       VkFenceCreateInfo fci{};
       fci.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
       fci.flags=VK_FENCE_CREATE_SIGNALED_BIT;   // first wait passes
       VkSemaphoreCreateInfo sci{};
       sci.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
       for (int i=0;i<kMaxFramesInFlight;++i)
        {if (!vkCheck(vkCreateFence(device,&fci,NULL,
                     &inFlightFences_[i]),"vkCreateFence"))
           return false;
         if (!vkCheck(vkCreateSemaphore(device,&sci,NULL,
                      &imageAvailableSemaphores_[i]),"vkCreateSemaphore"))
           return false;
        }
       return true;
      }

    void destroyRenderFinishedSemaphores_()
      {for (size_t i=0;i<renderFinishedSemaphores_.size();++i)
        {if (renderFinishedSemaphores_[i]!=VK_NULL_HANDLE)
           vkDestroySemaphore(device,renderFinishedSemaphores_[i],NULL);
        }
       renderFinishedSemaphores_.clear();
      }

    void drainPresentEngine_()
      {// vkDeviceWaitIdle covers queue submissions but NOT presentation-
       // engine consumption (the present op waits on a render-finished
       // semaphore outside queue execution). Re-acquiring every image
       // returns ownership to the application, completing all uses —
       // required before destroying the swapchain/semaphores (VUID-
       // vkDestroySwapchainKHR-swapchain-01282 and VUID-
       // vkDestroySemaphore-semaphore-05149). Acquire needs at least one
       // sync object (VUID-vkAcquireNextImageKHR-semaphore-01780), so a
       // throwaway semaphore rides along; any non-SUCCESS result
       // (TIMEOUT/NOT_READY/OUT_OF_DATE/SUBOPTIMAL) means drained or
       // invalidated, and the finite 1ms timeout keeps this bounded.
       if (device==VK_NULL_HANDLE||swapchain==VK_NULL_HANDLE)
         return;
       vkDeviceWaitIdle(device);
       // Fresh unsignaled semaphore per attempt (an acquire SIGNALS it, so
       // reuse would trip VUID-vkAcquireNextImageKHR-semaphore-01286).
       for (unsigned guard=0;guard<32;++guard)
        {VkSemaphoreCreateInfo tsi{};
         tsi.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
         VkSemaphore tmp=VK_NULL_HANDLE;
         if (!vkCheck(vkCreateSemaphore(device,&tsi,NULL,&tmp),
                      "vkCreateSemaphore(drain)"))
           return;
         uint32_t idx=0;
         VkResult rc=vkAcquireNextImageKHR(device,swapchain,1000000,tmp,
                                           VK_NULL_HANDLE,&idx);
         vkDestroySemaphore(device,tmp,NULL);
         if (rc!=VK_SUCCESS)
           break;
        }
      }

    void destroyFrameSync_()
      {for (int i=0;i<kMaxFramesInFlight;++i)
        {if (imageAvailableSemaphores_[i]!=VK_NULL_HANDLE)
          {vkDestroySemaphore(device,imageAvailableSemaphores_[i],NULL);
           imageAvailableSemaphores_[i]=VK_NULL_HANDLE;
          }
         if (inFlightFences_[i]!=VK_NULL_HANDLE)
          {vkDestroyFence(device,inFlightFences_[i],NULL);
           inFlightFences_[i]=VK_NULL_HANDLE;
          }
         commandBuffers_[i]=VK_NULL_HANDLE;   // freed with the pool
        }
       if (device!=VK_NULL_HANDLE&&commandPool!=VK_NULL_HANDLE)
        {vkDestroyCommandPool(device,commandPool,NULL);
         commandPool=VK_NULL_HANDLE;
        }
      }

    bool recreateAcquireSemaphore_()
      {// R6 rule 1 helper: after OUT_OF_DATE from ACQUIRE the semaphore
       // may be left signaled (spec allows either) — destroy+recreate so
       // it can never be double-waited or double-signaled.
       vkDestroySemaphore(device,imageAvailableSemaphores_[currentFrame_],
                          NULL);
       imageAvailableSemaphores_[currentFrame_]=VK_NULL_HANDLE;
       VkSemaphoreCreateInfo sci{};
       sci.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
       return vkCheck(vkCreateSemaphore(device,&sci,NULL,
                      &imageAvailableSemaphores_[currentFrame_]),
                      "vkCreateSemaphore(acquire-recycle)");
      }

    bool rebootstrapSwapchain_()
      {VkSurfaceCapabilitiesKHR caps{};
       if (!vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice,
                    surface,&caps),
                    "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"
                    "(rebootstrap)"))
        {handleFatalStall_("surface-capability query failed");
         return false;
        }
       if (caps.currentExtent.width==0||caps.currentExtent.height==0)
        {// R6 rule 3: minimized/degenerate surface — vkCreateSwapchainKHR
         // needs extent >= 1x1, so SKIP the rebuild and presentation until
         // a non-degenerate size arrives.
         fprintf(stderr,"vkBackend: degenerate surface extent %ux%u — "
                        "presentation suspended until resized\n",
                 caps.currentExtent.width,caps.currentExtent.height);
         presentationSuspended_=true;
         swapchainDirty_=false;
         return false;
        }
       drainPresentEngine_();   // pending presents release old images/semaphores
       if (!createSwapchain_(swapchain))
        {handleFatalStall_("swapchain re-bootstrap failed");
         return false;
        }
       ++rebootstrapCount_;
       presentationSuspended_=false;
       fprintf(stderr,"vkBackend: swapchain re-bootstrapped at %ux%u "
                      "(count %u)\n",swapchainExtent_.width,
               swapchainExtent_.height,rebootstrapCount_);
       return true;
      }

    // ------------------------------------------------------------------
    // Task 40: ONE render pass (color attachment = swapchain image,
    // loadOp CLEAR black = the R4-M1 background-GC equivalent, finalLayout
    // PRESENT_SRC_KHR) + one framebuffer per swapchain image. The pass
    // survives re-bootstraps unless the swapchain format changes;
    // framebuffers are recreated with every swapchain.
    // ------------------------------------------------------------------
    bool ensureRenderPassAndFramebuffers_()
      {if (renderPass==VK_NULL_HANDLE)
         {if (!createRenderPass_())
            return false;
         }
        else if (renderPassFormat_!=swapchainFormat_)
         {// Defensive only (X11 WSI pins the format): a new pass invalidates
          // every pipeline built against the old one, so they are rebuilt
          // together. Safe point: re-bootstrap drained the device.
          vkDestroyRenderPass(device,renderPass,NULL);
          renderPass=VK_NULL_HANDLE;
          if (!createRenderPass_())
            return false;
          if (linePipeline!=VK_NULL_HANDLE)
           {destroyPipelines_();
            if (!initPipelines_())
              return false;
           }
         }
       swapchainFramebuffers_.resize(swapchainImageViews_.size(),
                                     VK_NULL_HANDLE);
       for (size_t i=0;i<swapchainImageViews_.size();++i)
        {VkFramebufferCreateInfo fci{};
         fci.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
         fci.renderPass=renderPass;
         fci.attachmentCount=1;
         fci.pAttachments=&swapchainImageViews_[i];
         fci.width=swapchainExtent_.width;
         fci.height=swapchainExtent_.height;
         fci.layers=1;
         if (!vkCheck(vkCreateFramebuffer(device,&fci,NULL,
                        &swapchainFramebuffers_[i]),
                      "vkCreateFramebuffer"))
          {destroySwapchainFramebuffers_();
           return false;
          }
        }
       return true;
      }

    bool createRenderPass_()
      {VkAttachmentDescription color{};
       color.format=swapchainFormat_;
       color.samples=VK_SAMPLE_COUNT_1_BIT;
       color.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;    // black clear lives HERE
       color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
       color.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
       color.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
       color.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
       color.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
       VkAttachmentReference colorRef{};
       colorRef.attachment=0;
       colorRef.layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
       VkSubpassDescription subpass{};
       subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
       subpass.colorAttachmentCount=1;
       subpass.pColorAttachments=&colorRef;
       VkSubpassDependency dep{};
       dep.srcSubpass=VK_SUBPASS_EXTERNAL;
       dep.dstSubpass=0;
       dep.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
       dep.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
       dep.srcAccessMask=0;
       dep.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
       VkRenderPassCreateInfo rpi{};
       rpi.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
       rpi.attachmentCount=1;
       rpi.pAttachments=&color;
       rpi.subpassCount=1;
       rpi.pSubpasses=&subpass;
       rpi.dependencyCount=1;
       rpi.pDependencies=&dep;
       if (!vkCheck(vkCreateRenderPass(device,&rpi,NULL,&renderPass),
                    "vkCreateRenderPass"))
         return false;
       renderPassFormat_=swapchainFormat_;
       return true;
      }

    void destroySwapchainFramebuffers_()
      {for (size_t i=0;i<swapchainFramebuffers_.size();++i)
         if (swapchainFramebuffers_[i]!=VK_NULL_HANDLE)
           vkDestroyFramebuffer(device,swapchainFramebuffers_[i],NULL);
       swapchainFramebuffers_.clear();
       // Task 41: the RENDER PASS IS DELIBERATELY NOT DESTROYED HERE. It
       // used to die with the framebuffers on every re-bootstrap, but
       // pipelines bind to THE pass object — recreating it would orphan
       // them. The pass now lives for the backend's lifetime (X11 WSI never
       // changes format); shutdown's destroyDrawResources_ retires it.
      }

    // D7/v6.1 seam: logical client rect -> framebuffer-space VkRect2D.
    // Applies the letterbox present transform (scale + centered offsets;
    // VK needs no y-flip — top-left origin both sides).
    VkRect2D applyPresentTransformToScissor_() const
      {if (!scissorActive_)
        {VkRect2D full{};
          full.offset.x=0;
          full.offset.y=0;
          full.extent=swapchainExtent_;
          return full;
         }
        VkRect2D r{};
        r.offset.x=presentOffX_+(int)(scissorRect_[0]*presentScale_);
        r.offset.y=presentOffY_+(int)(scissorRect_[1]*presentScale_);
        r.extent.width=(uint32_t)(scissorRect_[2]*presentScale_);
        r.extent.height=(uint32_t)(scissorRect_[3]*presentScale_);
        return r;
      }

    bool recordFramePreamble_(uint32_t imageIndex)
      {// The load-op CLEAR performs the black clear; the pass's implicit
       // layout transitions (UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL ->
       // PRESENT_SRC_KHR) replace any manual barriers. Task 41: this leaves
       // the command buffer OPEN between Begin/EndRenderPass so engine
       // draws record into it; endFrame's postamble closes it.
       VkCommandBuffer cb=commandBuffers_[currentFrame_];
       if (!vkCheck(vkResetCommandBuffer(cb,0),"vkResetCommandBuffer"))
         return false;
       VkCommandBufferBeginInfo bi{};
       bi.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
       bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
       if (!vkCheck(vkBeginCommandBuffer(cb,&bi),
                    "vkBeginCommandBuffer"))
         return false;
       VkClearValue clearBlack{};
       clearBlack.color.float32[0]=0.0f;
       clearBlack.color.float32[1]=0.0f;
       clearBlack.color.float32[2]=0.0f;
       clearBlack.color.float32[3]=1.0f;
       VkRenderPassBeginInfo rp{};
       rp.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
       rp.renderPass=renderPass;
       rp.framebuffer=swapchainFramebuffers_[imageIndex];
       rp.renderArea.offset.x=0;
       rp.renderArea.offset.y=0;
       rp.renderArea.extent=swapchainExtent_;
       rp.clearValueCount=1;
       rp.pClearValues=&clearBlack;
        vkCmdBeginRenderPass(cb,&rp,VK_SUBPASS_CONTENTS_INLINE);
        // Dynamic viewport + scissor: full extent unless setScissorRect
        // overrode it. The pipelines declare VK_DYNAMIC_STATE_VIEWPORT|
        // SCISSOR, so these latched commands drive every draw recorded
        // before the postamble.
        VkViewport vp{};
        vp.x=0.0f;vp.y=0.0f;
        vp.width=(float)swapchainExtent_.width;
        vp.height=(float)swapchainExtent_.height;
        vp.minDepth=0.0f;vp.maxDepth=1.0f;
        vkCmdSetViewport(cb,0,1,&vp);
        VkRect2D sci=applyPresentTransformToScissor_();
        vkCmdSetScissor(cb,0,1,&sci);
        vertUsed_[currentFrame_]=0;   // fresh scratch budget per frame
        activeCb_=cb;
        passOpen_=true;
       if (qaInRenderPassHook)
         qaInRenderPassHook(cb,imageIndex,
                            swapchainImages_[imageIndex],
                            qaInRenderPassHookUserData);
       return true;
      }

    bool recordFramePostamble_()
      {VkCommandBuffer cb=commandBuffers_[currentFrame_];
       passOpen_=false;
       activeCb_=VK_NULL_HANDLE;
       vkCmdEndRenderPass(cb);
       if (qaAfterRenderPassHook)
         qaAfterRenderPassHook(cb,acquiredImageIndex_,
                               swapchainImages_[acquiredImageIndex_],
                               qaAfterRenderPassHookUserData);
       return vkCheck(vkEndCommandBuffer(cb),"vkEndCommandBuffer");
      }

    // ------------------------------------------------------------------
    // Task 41 (revised): SPIR-V shaders are PRE-COMPILED at build time
    // (makefile: glslc -> obj/VK/spv/*.spv). No runtime glslc, no
    // /proc/self/exe, no temp dirs. loadPrecompiledShaders_ resolves
    // the executable's directory portably (see resolveExeDir_) and
    // verifies all 5 .spv blobs exist before initWindow proceeds.
    // createShaderModules_ loads them into VkShaderModule at device-init
    // time; the .spv files persist on disk (built artifacts, not temp).
    // ------------------------------------------------------------------
    static bool resolveExeDir_(char* outBuf,size_t outLen)
      {if (!outBuf||outLen<2)
         return false;
#ifdef __APPLE__
       char path[4096];
       uint32_t sz=sizeof path;
       if (_NSGetExecutablePath(path,&sz)==0)
         {char* rp=realpath(path,outBuf);
          if (rp)
           {char* slash=strrchr(outBuf,'/');
            if (slash)
              *slash='\0';
            return true;
           }
         }
       // realpath failed or buffer too small: fall through to readlink
#endif
       ssize_t n=readlink("/proc/self/exe",outBuf,outLen-1);
       if (n>0)
         {outBuf[n]='\0';
          char* slash=strrchr(outBuf,'/');
          if (slash)
            *slash='\0';
          return true;
         }
       return false;
      }

    bool loadPrecompiledShaders_()
      {// Verifies all 5 pre-compiled .spv blobs exist relative to the
      // binary's directory (resolved via resolveExeDir_, with a CWD
      // fallback so `./XAsteroids` run from the repo root also works).
      // On macOS the .spv files live at obj/VK/spv/ alongside the binary.
       static const char* const spvNames[5]={"prim.spv","prim_fs.spv",
                                            "tex.spv","tex_fs.spv",
                                            "masked.spv"};
       char dir[4096];
       if (!resolveExeDir_(dir,sizeof dir))
         strcpy(dir,".");
       // On macOS the .spv files live at obj/VK/spv/ alongside the binary.
       // On Linux the binary may be symlinked to obj/VK/; either way we
       // resolve relative to the binary's directory with a CWD fallback.
       char spvDir[4096];
       snprintf(spvDir,sizeof spvDir,"%s/obj/VK/spv",dir);
       // Check all 5 blobs; report the first missing one.
       for (int i=0;i<5;++i)
        {char path[4200];
         snprintf(path,sizeof path,"%s/%s",spvDir,spvNames[i]);
         if (access(path,R_OK)!=0)
          {fprintf(stderr,"vkBackend: missing pre-compiled SPIR-V %s\n",
                   path);
           fprintf(stderr,"vkBackend: rebuild with `make BACKEND=VK` "
                   "(glslc must be on PATH)\n");
           return false;
          }
         }
       snprintf(shaderDir_,sizeof shaderDir_,"%s",spvDir);
       printf("vkBackend: 5 pre-compiled SPIR-V shaders found in %s\n",
              shaderDir_);
       return true;
      }

    bool createShaderModules_()
      {if (primVs_!=VK_NULL_HANDLE)
         return true;
       VkShaderModule* slots[5]={&primVs_,&primFs_,&texVs_,&texFs_,
                                 &maskedFs_};
       static const char* const spvNames[5]={"prim.spv","prim_fs.spv",
                                            "tex.spv","tex_fs.spv",
                                            "masked.spv"};
       for (int i=0;i<5;++i)
        {char spvPath[4200];
         snprintf(spvPath,sizeof spvPath,"%s/%s",shaderDir_,spvNames[i]);
         if (!createShaderModuleFromSpv_(spvPath,slots[i]))
           return false;
         ++shaderModulesLoaded_;
        }
       printf("vkBackend: %u shader module(s) loaded\n",
              shaderModulesLoaded_);
       return true;
      }

    bool createShaderModuleFromSpv_(const char* path,VkShaderModule* out)
      {FILE* f=fopen(path,"rb");
       if (!f)
        {fprintf(stderr,"vkBackend: cannot open compiled shader %s\n",path);
         return false;
        }
       fseek(f,0,SEEK_END);
       long len=ftell(f);
       fseek(f,0,SEEK_SET);
       if (len<=0||len%4!=0)
        {fclose(f);
         fprintf(stderr,"vkBackend: bad SPIR-V size %ld (%s)\n",len,path);
         return false;
        }
       std::vector<uint32_t> code((size_t)len/4);
       bool readOk=fread(code.data(),4,code.size(),f)==code.size();
       fclose(f);
       if (!readOk)
        {fprintf(stderr,"vkBackend: short read on %s\n",path);
         return false;
        }
       VkShaderModuleCreateInfo ci{};
       ci.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
       ci.codeSize=code.size()*sizeof(uint32_t);
       ci.pCode=code.data();
       return vkCheck(vkCreateShaderModule(device,&ci,NULL,out),
                      "vkCreateShaderModule");
      }

    // ------------------------------------------------------------------
    // Task 41: pipelines + draw resources. All four pipelines bind THE
    // task-40 render pass (public member) and share dynamic
    // viewport+scissor state; they are created once at initWindow and
    // survive re-bootstraps because the pass does too.
    // ------------------------------------------------------------------
    bool initPipelines_()
      {if (linePipeline!=VK_NULL_HANDLE)
         return true;
       if (!createShaderModules_())
         return false;
       VkDescriptorSetLayoutBinding samp{};
        samp.binding=0;
        samp.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samp.descriptorCount=1;
        samp.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dli{};
        dli.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dli.bindingCount=1;
        dli.pBindings=&samp;
        if (!vkCheck(vkCreateDescriptorSetLayout(device,&dli,NULL,
                     &texSetLayout_),
                     "vkCreateDescriptorSetLayout"))
          return false;
        // Masked pipeline set layout: binding 0 content + binding 1 R8 mask.
        VkDescriptorSetLayoutBinding msamp[2]={};
        msamp[0].binding=0;
        msamp[0].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        msamp[0].descriptorCount=1;
        msamp[0].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
        msamp[1].binding=1;
        msamp[1].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        msamp[1].descriptorCount=1;
        msamp[1].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo mdli{};
        mdli.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        mdli.bindingCount=2;
        mdli.pBindings=msamp;
        if (!vkCheck(vkCreateDescriptorSetLayout(device,&mdli,NULL,
                     &maskedSetLayout_),
                     "vkCreateDescriptorSetLayout(masked)"))
          return false;
        // Pool sized for the textured path (one set per texture) plus the
        // render-target sampling sets and cached (content,mask) pairs.
        VkDescriptorPoolSize ps{};
        ps.type=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps.descriptorCount=512;
        VkDescriptorPoolCreateInfo dpi{};
        dpi.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpi.maxSets=512;
        dpi.poolSizeCount=1;
        dpi.pPoolSizes=&ps;
       if (!vkCheck(vkCreateDescriptorPool(device,&dpi,NULL,
                    &descriptorPool),
                    "vkCreateDescriptorPool"))
         return false;
       // Shared sampler: glBackend's texture params verbatim (task-36
       // defect-B state: LINEAR MIN+MAG, CLAMP_TO_EDGE).
       VkSamplerCreateInfo smci{};
       smci.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
       smci.magFilter=VK_FILTER_LINEAR;
       smci.minFilter=VK_FILTER_LINEAR;
       smci.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;
       smci.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
       smci.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
       smci.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
       smci.maxLod=0.0f;
       if (!vkCheck(vkCreateSampler(device,&smci,NULL,&sharedSampler_),
                    "vkCreateSampler"))
         return false;
       // uMVP rides a push-constant uniform block (per-draw recorded —
       // see header comment for why not a UBO).
       VkPushConstantRange pcr{};
       pcr.offset=0;
       pcr.size=(uint32_t)(sizeof(float)*16);
       pcr.stageFlags=VK_SHADER_STAGE_VERTEX_BIT;
       VkPipelineLayoutCreateInfo cli{};
       cli.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
       cli.pushConstantRangeCount=1;
       cli.pPushConstantRanges=&pcr;
       if (!vkCheck(vkCreatePipelineLayout(device,&cli,NULL,
                    &colorPipelineLayout),
                    "vkCreatePipelineLayout(color)"))
         return false;
        cli.setLayoutCount=1;
         cli.pSetLayouts=&texSetLayout_;
        if (!vkCheck(vkCreatePipelineLayout(device,&cli,NULL,
                     &texPipelineLayout),
                     "vkCreatePipelineLayout(tex)"))
          return false;
        cli.pSetLayouts=&maskedSetLayout_;
        if (!vkCheck(vkCreatePipelineLayout(device,&cli,NULL,
                     &maskedPipelineLayout_),
                     "vkCreatePipelineLayout(masked)"))
          return false;
       // Vertex scratch: one host-visible coherent buffer per in-flight
       // frame slot (GL's per-draw STREAM_DRAW equivalent).
       for (int i=0;i<kMaxFramesInFlight;++i)
        {VkBufferCreateInfo vbci{};
         vbci.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
         vbci.size=kVertBufBytes;
         vbci.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
         vbci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
         if (!vkCheck(vkCreateBuffer(device,&vbci,NULL,&vertBuf_[i]),
                      "vkCreateBuffer(vertex-scratch)"))
           return false;
         VkMemoryRequirements mr;
         vkGetBufferMemoryRequirements(device,vertBuf_[i],&mr);
         uint32_t mt=findMemoryType_(mr.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                      |VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
         VkMemoryAllocateInfo mai{};
         mai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
         mai.allocationSize=mr.size;
         mai.memoryTypeIndex=mt;
         if (mt==UINT32_MAX
             ||!vkCheck(vkAllocateMemory(device,&mai,NULL,&vertMem_[i]),
                        "vkAllocateMemory(vertex-scratch)")
             ||!vkCheck(vkBindBufferMemory(device,vertBuf_[i],
                        vertMem_[i],0),
                        "vkBindBufferMemory(vertex-scratch)")
             ||!vkCheck(vkMapMemory(device,vertMem_[i],0,kVertBufBytes,0,
                        &vertMapped_[i]),
                        "vkMapMemory(vertex-scratch)"))
           return false;
        }
       // Vertex layouts: color prims = pos(2)+rgb(3) stride 20 (glBackend
       // verbatim); textured = pos(2)+uv(2)+rgba(4) stride 32 — the
       // ENGINE-facing drawTriangles contract stays 7 floats [x y u v r g b]
       // like GL, but this pipeline stores a clean 4th alpha component
       // (glBackend's stride-7 vec4 attribute overran into the next
       // vertex's x, so its sampled alpha was always >=1: opaque. The
       // repack in drawTriangles pins a=1 = that effective behavior,
       // minus the overrun).
       VkVertexInputBindingDescription bindCol{};
       bindCol.binding=0;
       bindCol.stride=5*sizeof(float);
       bindCol.inputRate=VK_VERTEX_INPUT_RATE_VERTEX;
       VkVertexInputAttributeDescription attrCol[2];
       attrCol[0].location=0;
       attrCol[0].binding=0;
       attrCol[0].format=VK_FORMAT_R32G32_SFLOAT;
       attrCol[0].offset=0;
       attrCol[1].location=1;
       attrCol[1].binding=0;
       attrCol[1].format=VK_FORMAT_R32G32B32_SFLOAT;
       attrCol[1].offset=2*sizeof(float);
       VkVertexInputBindingDescription bindTex{};
       bindTex.binding=0;
       bindTex.stride=8*sizeof(float);
       bindTex.inputRate=VK_VERTEX_INPUT_RATE_VERTEX;
       VkVertexInputAttributeDescription attrTex[3];
       attrTex[0].location=0;
       attrTex[0].binding=0;
       attrTex[0].format=VK_FORMAT_R32G32_SFLOAT;
       attrTex[0].offset=0;
       attrTex[1].location=1;
       attrTex[1].binding=0;
       attrTex[1].format=VK_FORMAT_R32G32_SFLOAT;
       attrTex[1].offset=2*sizeof(float);
       attrTex[2].location=2;
       attrTex[2].binding=0;
       attrTex[2].format=VK_FORMAT_R32G32B32A32_SFLOAT;
       attrTex[2].offset=4*sizeof(float);
       if (!buildGraphicsPipeline_(primVs_,primFs_,&bindCol,attrCol,2,
                                   VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
                                   VK_POLYGON_MODE_FILL,
                                   colorPipelineLayout,&linePipeline))
         return false;
       if (!buildGraphicsPipeline_(primVs_,primFs_,&bindCol,attrCol,2,
                                   VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                   VK_POLYGON_MODE_FILL,
                                   colorPipelineLayout,&triPipeline))
         return false;
       if (!buildGraphicsPipeline_(primVs_,primFs_,&bindCol,attrCol,2,
                                   VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
                                   VK_POLYGON_MODE_LINE,
                                   colorPipelineLayout,&outlinePipeline))
         return false;
        if (!buildGraphicsPipeline_(texVs_,texFs_,&bindTex,attrTex,3,
                                    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                    VK_POLYGON_MODE_FILL,
                                    texPipelineLayout,&texPipeline))
          return false;
        if (!buildGraphicsPipeline_(texVs_,maskedFs_,&bindTex,attrTex,3,
                                    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                    VK_POLYGON_MODE_FILL,
                                    maskedPipelineLayout_,&maskedPipeline))
          return false;
        return true;
      }

    bool buildGraphicsPipeline_(VkShaderModule vs,VkShaderModule fs,
                                const VkVertexInputBindingDescription* bind,
                                const VkVertexInputAttributeDescription* attr,
                                uint32_t nattr,
                                VkPrimitiveTopology topo,
                                VkPolygonMode poly,
                                VkPipelineLayout layout,VkPipeline* out)
      {VkPipelineShaderStageCreateInfo stages[2]{};
       stages[0].sType=
         VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
       stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;
       stages[0].module=vs;
       stages[0].pName="main";
       stages[1].sType=
         VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
       stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;
       stages[1].module=fs;
       stages[1].pName="main";
       VkPipelineVertexInputStateCreateInfo vi{};
       vi.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
       vi.vertexBindingDescriptionCount=1;
       vi.pVertexBindingDescriptions=bind;
       vi.vertexAttributeDescriptionCount=nattr;
       vi.pVertexAttributeDescriptions=attr;
       VkPipelineInputAssemblyStateCreateInfo ia{};
       ia.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
       ia.topology=topo;
       ia.primitiveRestartEnable=VK_FALSE;
       VkPipelineViewportStateCreateInfo vp{};
       vp.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
       vp.viewportCount=1;
       vp.scissorCount=1;      // both DYNAMIC — latched per frame
       VkPipelineRasterizationStateCreateInfo rs{};
       rs.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
       rs.polygonMode=poly;
       rs.cullMode=VK_CULL_MODE_NONE;   // GL leg runs unculled
       rs.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;
       rs.lineWidth=1.0f;               // core-profile parity; thick lines
                                        // ride quads (SC8)
       VkPipelineMultisampleStateCreateInfo ms{};
       ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
       ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
       VkPipelineDepthStencilStateCreateInfo ds{};
       ds.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
       ds.depthTestEnable=VK_FALSE;
       ds.depthWriteEnable=VK_FALSE;
       // GL leg enables GL_BLEND SRC_ALPHA/ONE_MINUS_SRC_ALPHA globally
       // (initPrimitives); mirror it on every pipeline.
       VkPipelineColorBlendAttachmentState att{};
       att.blendEnable=VK_TRUE;
       att.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;
       att.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
       att.colorBlendOp=VK_BLEND_OP_ADD;
       att.srcAlphaBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;
       att.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
       att.alphaBlendOp=VK_BLEND_OP_ADD;
       att.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT
                         |VK_COLOR_COMPONENT_B_BIT
                         |VK_COLOR_COMPONENT_A_BIT;
       VkPipelineColorBlendStateCreateInfo cb{};
       cb.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
       cb.attachmentCount=1;
       cb.pAttachments=&att;
       VkDynamicState dyn[2]={VK_DYNAMIC_STATE_VIEWPORT,
                              VK_DYNAMIC_STATE_SCISSOR};
       VkPipelineDynamicStateCreateInfo dy{};
       dy.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
       dy.dynamicStateCount=2;
       dy.pDynamicStates=dyn;
       VkGraphicsPipelineCreateInfo pi{};
       pi.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
       pi.stageCount=2;
       pi.pStages=stages;
       pi.pVertexInputState=&vi;
       pi.pInputAssemblyState=&ia;
       pi.pViewportState=&vp;
       pi.pRasterizationState=&rs;
       pi.pMultisampleState=&ms;
       pi.pDepthStencilState=&ds;
       pi.pColorBlendState=&cb;
       pi.pDynamicState=&dy;
       pi.layout=layout;
       pi.renderPass=renderPass;
       pi.subpass=0;
       return vkCheck(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,
                     &pi,NULL,out),
                     "vkCreateGraphicsPipelines");
      }

    void destroyPipelines_()
      {VkPipeline pipes[5]={linePipeline,triPipeline,outlinePipeline,
                            texPipeline,maskedPipeline};
       for (int i=0;i<5;++i)
        {if (pipes[i]!=VK_NULL_HANDLE)
           vkDestroyPipeline(device,pipes[i],NULL);
         pipes[i]=VK_NULL_HANDLE;
        }
       linePipeline=triPipeline=outlinePipeline=texPipeline
                  =maskedPipeline=VK_NULL_HANDLE;
       if (colorPipelineLayout!=VK_NULL_HANDLE)
        {vkDestroyPipelineLayout(device,colorPipelineLayout,NULL);
         colorPipelineLayout=VK_NULL_HANDLE;
        }
       if (texPipelineLayout!=VK_NULL_HANDLE)
        {vkDestroyPipelineLayout(device,texPipelineLayout,NULL);
         texPipelineLayout=VK_NULL_HANDLE;
        }
       if (maskedPipelineLayout_!=VK_NULL_HANDLE)
        {vkDestroyPipelineLayout(device,maskedPipelineLayout_,NULL);
         maskedPipelineLayout_=VK_NULL_HANDLE;
        }
      }

    void destroyDrawResources_()
      {for (int i=0;i<kMaxFramesInFlight;++i)
        {if (vertMapped_[i])
           vkUnmapMemory(device,vertMem_[i]);
         if (vertMem_[i]!=VK_NULL_HANDLE)
           vkFreeMemory(device,vertMem_[i],NULL);
         if (vertBuf_[i]!=VK_NULL_HANDLE)
           vkDestroyBuffer(device,vertBuf_[i],NULL);
         vertMapped_[i]=NULL;
         vertMem_[i]=VK_NULL_HANDLE;
         vertBuf_[i]=VK_NULL_HANDLE;
         vertUsed_[i]=0;
        }
       destroyPipelines_();
        if (descriptorPool!=VK_NULL_HANDLE)
         {vkDestroyDescriptorPool(device,descriptorPool,NULL);
          descriptorPool=VK_NULL_HANDLE;
         }
        if (texSetLayout_!=VK_NULL_HANDLE)
         {vkDestroyDescriptorSetLayout(device,texSetLayout_,NULL);
          texSetLayout_=VK_NULL_HANDLE;
         }
        if (maskedSetLayout_!=VK_NULL_HANDLE)
         {vkDestroyDescriptorSetLayout(device,maskedSetLayout_,NULL);
          maskedSetLayout_=VK_NULL_HANDLE;
         }
        if (sharedSampler_!=VK_NULL_HANDLE)
         {vkDestroySampler(device,sharedSampler_,NULL);
          sharedSampler_=VK_NULL_HANDLE;
         }
        VkShaderModule mods[5]={primVs_,primFs_,texVs_,texFs_,maskedFs_};
        for (int i=0;i<5;++i)
         {if (mods[i]!=VK_NULL_HANDLE)
            vkDestroyShaderModule(device,mods[i],NULL);
         }
        primVs_=primFs_=texVs_=texFs_=maskedFs_=VK_NULL_HANDLE;
        if (offscreenRenderPass_!=VK_NULL_HANDLE&&device!=VK_NULL_HANDLE)
         {vkDestroyRenderPass(device,offscreenRenderPass_,NULL);
          offscreenRenderPass_=VK_NULL_HANDLE;
         }
        // Task 43: the endRenderTo reopen pass was created on demand but
        // never retired — the VUID-05137 leaked-object at DestroyDevice.
        if (swapchainLoadRenderPass_!=VK_NULL_HANDLE&&device!=VK_NULL_HANDLE)
         {vkDestroyRenderPass(device,swapchainLoadRenderPass_,NULL);
          swapchainLoadRenderPass_=VK_NULL_HANDLE;
         }
        if (renderPass!=VK_NULL_HANDLE&&device!=VK_NULL_HANDLE)
        {vkDestroyRenderPass(device,renderPass,NULL);
         renderPass=VK_NULL_HANDLE;
         renderPassFormat_=VK_FORMAT_UNDEFINED;
        }
      }

    void destroyTextures_()
      {for (auto& kv:textures_)
        {vkDestroyImageView(device,kv.second.view,NULL);
         vkFreeMemory(device,kv.second.mem,NULL);
         vkDestroyImage(device,kv.second.image,NULL);
        }
       textures_.clear();
      }

    uint32_t findMemoryType_(uint32_t typeBits,VkMemoryPropertyFlags props)
      {VkPhysicalDeviceMemoryProperties mp;
       vkGetPhysicalDeviceMemoryProperties(physicalDevice,&mp);
       for (uint32_t i=0;i<mp.memoryTypeCount;++i)
         if ((typeBits&(1u<<i))
             &&(mp.memoryTypes[i].propertyFlags&props)==props)
           return i;
       return UINT32_MAX;
      }

    bool uploadTexture_(VkBuffer staging,VkImage image,uint32_t w,
                        uint32_t h)
      {// One-shot command buffer from the transient pool: UNDEFINED ->
       // TRANSFER_DST -> copy -> SHADER_READ_ONLY. Runs only outside an
       // open frame (guarded by the caller); a finite fence wait bounds it.
       VkCommandBufferAllocateInfo cai{};
       cai.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
       cai.commandPool=commandPool;
       cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
       cai.commandBufferCount=1;
       VkCommandBuffer cb=VK_NULL_HANDLE;
       if (!vkCheck(vkAllocateCommandBuffers(device,&cai,&cb),
                    "vkAllocateCommandBuffers(upload)"))
         return false;
       VkCommandBufferBeginInfo bi{};
       bi.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
       bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
       if (!vkCheck(vkBeginCommandBuffer(cb,&bi),
                    "vkBeginCommandBuffer(upload)"))
         return false;
       VkImageMemoryBarrier toDst{};
       toDst.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
       toDst.srcAccessMask=0;
       toDst.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
       toDst.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
       toDst.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
       toDst.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
       toDst.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
       toDst.image=image;
       toDst.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
       toDst.subresourceRange.levelCount=1;
       toDst.subresourceRange.layerCount=1;
       vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,
                            1,&toDst);
       VkBufferImageCopy region{};
       region.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
       region.imageSubresource.layerCount=1;
       region.imageExtent.width=w;
       region.imageExtent.height=h;
       region.imageExtent.depth=1;
       vkCmdCopyBufferToImage(cb,staging,image,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,
                              &region);
       VkImageMemoryBarrier toShader{};
       toShader.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
       toShader.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
       toShader.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
       toShader.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
       toShader.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
       toShader.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
       toShader.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
       toShader.image=image;
       toShader.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
       toShader.subresourceRange.levelCount=1;
       toShader.subresourceRange.layerCount=1;
       vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,
                            0,NULL,1,&toShader);
       if (!vkCheck(vkEndCommandBuffer(cb),"vkEndCommandBuffer(upload)"))
         return false;
       VkFenceCreateInfo fci{};
       fci.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
       VkFence fence=VK_NULL_HANDLE;
       if (!vkCheck(vkCreateFence(device,&fci,NULL,&fence),
                    "vkCreateFence(upload)"))
         return false;
       VkSubmitInfo si{};
       si.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
       si.commandBufferCount=1;
       si.pCommandBuffers=&cb;
       VkResult sr=vkQueueSubmit(graphicsQueue,1,&si,fence);
       bool done=false;
       if (sr==VK_SUCCESS)
        {VkResult wr=vkWaitForFences(device,1,&fence,VK_TRUE,
                                     (uint64_t)1000*1000000ull);
         done=(wr==VK_SUCCESS);
         if (wr!=VK_SUCCESS)
           fprintf(stderr,"vkBackend: texture upload fence wait: %d (%s)\n",
                   (int)wr,vkResultName(wr));
        }
       else
         fprintf(stderr,"vkBackend: texture upload submit: %d (%s)\n",
                 (int)sr,vkResultName(sr));
       vkDestroyFence(device,fence,NULL);
       vkFreeCommandBuffers(device,commandPool,1,&cb);
       return done;
      }

    // ------------------------------------------------------------------
    // Task 41 draw plumbing: bump-allocated vertex scratch + per-draw
    // recorded MVP push constant.
    // ------------------------------------------------------------------
    void* reserveVerts_(VkDeviceSize bytes,VkDeviceSize* outOffset)
      {VkDeviceSize off=(vertUsed_[currentFrame_]+15)
                        &~(VkDeviceSize)15;
       if (off+bytes>kVertBufBytes)
        {fprintf(stderr,"vkBackend: vertex scratch exhausted (%llu bytes "
                        "wanted)\n",(unsigned long long)bytes);
         return NULL;
        }
       vertUsed_[currentFrame_]=off+bytes;
       *outOffset=off;
       return (char*)vertMapped_[currentFrame_]+off;
      }

    void pushMvp_(VkCommandBuffer cb,VkPipelineLayout layout,
                  const float* m)
      {vkCmdPushConstants(cb,layout,VK_SHADER_STAGE_VERTEX_BIT,0,
                          sizeof(float)*16,m);
      }

    void drawColorPrims_(VkPipeline pipe,const float* interleaved,
                         int vertCount)
      {if (!passOpen_||!interleaved||vertCount<1)
         return;
       VkDeviceSize off=0;
       void* p=reserveVerts_((VkDeviceSize)vertCount*5*sizeof(float),&off);
       if (!p)
         return;
       memcpy(p,interleaved,(size_t)vertCount*5*sizeof(float));
       VkCommandBuffer cb=activeCb_;
       vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipe);
       pushMvp_(cb,colorPipelineLayout,presentMVP_());
       vkCmdBindVertexBuffers(cb,0,1,&vertBuf_[currentFrame_],&off);
       vkCmdDraw(cb,(uint32_t)vertCount,1,0,0);
      }

    void drawTexPrims_(VkPipeline pipe,VkPipelineLayout layout,
                       VkDescriptorSet set,const float* verts,int count,
                       const float* mvp)
      {if (!passOpen_||count<3)
         return;
       VkDeviceSize off=0;
       void* p=reserveVerts_((VkDeviceSize)count*8*sizeof(float),&off);
       if (!p)
         return;
       memcpy(p,verts,(size_t)count*8*sizeof(float));
       VkCommandBuffer cb=activeCb_;
       vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipe);
       pushMvp_(cb,layout,mvp);
       vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,
                               layout,0,1,&set,0,NULL);
       vkCmdBindVertexBuffers(cb,0,1,&vertBuf_[currentFrame_],&off);
       vkCmdDraw(cb,(uint32_t)count,1,0,0);
      }

    // L = P*M, column-major (GLSL mat4 layout). M = R(angle)*local + t —
    // the object rotates about its OWN origin, then translates (D2 rotator
    // placement, m19). P maps logical client pixels onto NDC; Vulkan NDC is
    // y-DOWN like logical space, so P carries NO flip (GL's negative
    // y-scale becomes positive here — same geometry, same pixels).
    const float* presentMVP_()
      {if (rtActive_)
        {// Rendering INTO a render target: logical (0,0)-top-left maps 1:1
         // onto the target surface (Button face geometry is target-local;
         // glBackend rtActive_ analog without the GL y-flip — VK NDC is
         // y-down like logical space).
         float a=2.0f/(float)rtWidth_;
         float b=2.0f/(float)rtHeight_;
         float cs=cosf(angle_),sn=sinf(angle_);
         float ma=cs,mb=-sn,mc=tx_;
         float md=sn,me=cs,mf=ty_;
         mvp_[0]=a*ma;    mvp_[1]=b*md;    mvp_[2]=0.0f;mvp_[3]=0.0f;
         mvp_[4]=a*mb;    mvp_[5]=b*me;    mvp_[6]=0.0f;mvp_[7]=0.0f;
         mvp_[8]=0.0f;    mvp_[9]=0.0f;    mvp_[10]=1.0f;mvp_[11]=0.0f;
         mvp_[12]=a*mc-1.0f;mvp_[13]=b*mf-1.0f;mvp_[14]=0.0f;mvp_[15]=1.0f;
         return mvp_;
        }
       float W=(float)swapchainExtent_.width;
       float H=(float)swapchainExtent_.height;
       float a=2.0f*presentScale_/W;
       float c=2.0f*(float)presentOffX_/W-1.0f;
       float b=2.0f*presentScale_/H;
       float d=2.0f*(float)presentOffY_/H-1.0f;
       float cs=cosf(angle_),sn=sinf(angle_);
       // M: logical = R*local + t. Task 43: canvas-domain draws carry the
       // canvas-origin shift (playAreaXY - 80) so world/help/table content
       // lands exactly where glBackend places it (setCanvasOffset).
       float ma=cs,mb=-sn,mc=tx_+canvasOffX_;
       float md=sn,me=cs,mf=ty_+canvasOffY_;
       mvp_[0]=a*ma;    mvp_[1]=b*md;    mvp_[2]=0.0f;mvp_[3]=0.0f;
       mvp_[4]=a*mb;    mvp_[5]=b*me;    mvp_[6]=0.0f;mvp_[7]=0.0f;
       mvp_[8]=0.0f;    mvp_[9]=0.0f;    mvp_[10]=1.0f;mvp_[11]=0.0f;
       mvp_[12]=a*mc+c; mvp_[13]=b*mf+d; mvp_[14]=0.0f;mvp_[15]=1.0f;
       return mvp_;
      }

    // Window-space pixel coords -> NDC (y-down top-left origin, matching
    // the ImGui convention drawTriangles feeds in; glBackend windowMVP
    // analog without the GL flip).
    const float* windowMVP_()
      {float W=(float)swapchainExtent_.width;
       float H=(float)swapchainExtent_.height;
       winMvp_[0]=2.0f/W; winMvp_[1]=0.0f; winMvp_[2]=0.0f; winMvp_[3]=0.0f;
       winMvp_[4]=0.0f; winMvp_[5]=2.0f/H; winMvp_[6]=0.0f; winMvp_[7]=0.0f;
       winMvp_[8]=0.0f; winMvp_[9]=0.0f; winMvp_[10]=1.0f;winMvp_[11]=0.0f;
       winMvp_[12]=-1.0f;winMvp_[13]=-1.0f;winMvp_[14]=0.0f;winMvp_[15]=1.0f;
       return winMvp_;
      }

    static void makeVert(float* dst,int slot,float x,float y,
                         float r,float g,float b)
      {dst[slot]=x;dst[slot+1]=y;
       dst[slot+2]=r;dst[slot+3]=g;dst[slot+4]=b;
      }

    static void thickLineQuad(float* dst,float x1,float y1,float x2,
                              float y2,float halfW,float r,float g,float b)
      {// SC8 port (glBackend verbatim): perpendicular expansion in LOGICAL
       // space, butt caps.
       float dx=x2-x1,dy=y2-y1;
       float len=sqrtf(dx*dx+dy*dy);
       if (len<1e-6f)
        {dx=1.0f;dy=0.0f;len=1.0f;
        }
       float px=-dy/len*halfW,py=dx/len*halfW;
       const float cx[4]={x1+px,x2+px,x2-px,x1-px};
       const float cy[4]={y1+py,y2+py,y2-py,y1-py};
       static const int idx[6]={0,1,2,0,2,3};
       for (int i=0;i<6;++i)
         makeVert(dst,5*i,cx[idx[i]],cy[idx[i]],r,g,b);
      }

    static bool pointInTri(float px,float py,
                           float ax,float ay,float bx,float by,
                           float cx,float cy)
      {float d1=(px-bx)*(ay-by)-(ax-bx)*(py-by);
       float d2=(px-cx)*(by-cy)-(bx-cx)*(py-cy);
       float d3=(px-ax)*(cy-ay)-(cx-ax)*(py-ay);
       bool neg=(d1<0)||(d2<0)||(d3<0);
       bool pos=(d1>0)||(d2>0)||(d3>0);
       return !(neg&&pos);
      }

    static int earClip(const float* verts,int count,float* outPairs)
      {// glBackend port verbatim: O(n^2) ear clipping, orientation
       // normalized; caller fans when it cannot close.
       if (count<3)
         return 0;
       if (count>256)
         return 0;
       float px[256],py[256];
       double area=0.0;
       for (int i=0;i<count;++i)
        {int j=(i+1)%count;
         area+=(double)verts[2*i]*verts[2*j+1]
              -(double)verts[2*j]*verts[2*i+1];
         px[i]=verts[2*i];py[i]=verts[2*i+1];
        }
       if (area<0)
        {for (int i=0;i<count/2;++i)
          {float tx=px[i],ty=py[i];
           px[i]=px[count-1-i];py[i]=py[count-1-i];
           px[count-1-i]=tx;py[count-1-i]=ty;
          }
        }
       int out=0;
       int remaining=count;
       int guard=0;
       while (remaining>3&&guard++<4096)
        {bool clipped=false;
         for (int i=0;i<remaining;++i)
          {int prev=(i+remaining-1)%remaining,cur=i,next=(i+1)%remaining;
           float cross=(px[cur]-px[prev])*(py[next]-py[prev])
                      -(py[cur]-py[prev])*(px[next]-px[prev]);
           if (cross<=0)
             continue;
           bool ear=true;
           for (int k=0;k<remaining&&ear;++k)
            {if (k==prev||k==cur||k==next)
               continue;
             if (pointInTri(px[k],py[k],
                            px[prev],py[prev],px[cur],py[cur],
                            px[next],py[next]))
               ear=false;
            }
           if (!ear)
             continue;
           outPairs[out++]=px[prev];outPairs[out++]=py[prev];
           outPairs[out++]=px[cur]; outPairs[out++]=py[cur];
           outPairs[out++]=px[next];outPairs[out++]=py[next];
           for (int k=cur;k<remaining-1;++k)
            {px[k]=px[k+1];py[k]=py[k+1];
            }
           --remaining;
           clipped=true;
           break;
          }
         if (!clipped)
           break;
        }
       if (remaining==3)
        {outPairs[out++]=px[0];outPairs[out++]=py[0];
         outPairs[out++]=px[1];outPairs[out++]=py[1];
         outPairs[out++]=px[2];outPairs[out++]=py[2];
        }
       return out/6;
      }

    void handleStall_(const char* what)
      {// Recovered-stall path (R6 rule 2): fence-wait timeout AND acquire
       // VK_TIMEOUT land here — log, skip the frame; kStallShutdownFrames
       // consecutive stalls force a clean shutdown instead of a hang.
       ++consecutiveStallCount_;
       ++stallSkipCount_;
       fprintf(stderr,"vkBackend: recovered stall (%s): %d consecutive "
                      "(shutdown at %u)\n",what,consecutiveStallCount_,
               kStallShutdownFrames);
       if (consecutiveStallCount_>=(int)kStallShutdownFrames)
         handleFatalStall_("stall persisted 100 consecutive frames");
      }

    void handleFatalStall_(const char* why)
      {fprintf(stderr,"vkBackend: FATAL: %s — clean shutdown\n",why);
       fatalStallShutdown_=true;
       sessionDead_=true;
       shutdown();
      }

    // D17.4a hook: recompute the letterbox present transform from the last
    // known framebuffer size vs the canonical size. Called from the
    // framebuffer-size callback and after every swapchain (re-)creation.
    void recomputePresentTransform_()
      {float scale=1.0f;
       int ox=0,oy=0;
       if (canonicalWidth_>0&&canonicalHeight_>0
           &&fbWidth_>0&&fbHeight_>0)
        {scale=((float)fbWidth_/canonicalWidth_
               <(float)fbHeight_/canonicalHeight_)
                ? (float)fbWidth_/canonicalWidth_
                : (float)fbHeight_/canonicalHeight_;
         ox=((int)fbWidth_-(int)(canonicalWidth_*scale))/2;
         oy=((int)fbHeight_-(int)(canonicalHeight_*scale))/2;
        }
       presentScale_=scale;
       presentOffX_=ox;
       presentOffY_=oy;
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

    // task 44b clipboard stubs (glBackend verbatim; no clipboard domain).
    static void clipboardSetStub_(void*,const char*)
      {}

    static const char* clipboardGetStub_(void*)
      {return "";
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
         case VK_ERROR_OUT_OF_DATE_KHR:     return "VK_ERROR_OUT_OF_DATE_KHR";
         case VK_SUBOPTIMAL_KHR:            return "VK_SUBOPTIMAL_KHR";
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
