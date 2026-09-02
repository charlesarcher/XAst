#ifndef _XAST_MTLBACKEND_H
#define _XAST_MTLBACKEND_H

// ============================================================================
// mtlBackend.H — Metal RenderingEngine backend (Phase 5, tasks 49+).
//
// Task 4 scope (this file's landing): window + layer + frame lifecycle.
//   - Two-pass initWindow (D15): pass 1 loads the FOUR stb TTF font slots
//     + metrics BEFORE sizing (m21: errorInfo is X11-only, never loaded here);
//     pass 2 computes the canonical client size from the SHARED
//     WindowSizeFormula::Compute (one formula, no divergence — the same
//     header X11Backend/GLBackend/VKBackend call) and creates a
//     GLFW_RESIZABLE window at that size, root-centered. NO size limits
//     anywhere (v6/S2/D15).
//   - Letterbox present transform: uniform scale + centered offsets,
//     stored by recomputePresentTransform_() — called from the
//     framebuffer-size callback AND at init (D17.4b remediation).
//   - GLFW_CLIENT_API=GLFW_NO_API (no GL context; Metal via CAMetalLayer).
//   - CAMetalLayer attached to the GLFW NSView via the mtlCocoa C-ABI bridge.
//   - MTLDevice + MTLCommandQueue created via the bridge (device via
//     mtlCreateDevice, queue via mtlCreateCommandQueue).
//   - VSync enabled by default (displaySyncEnabled=YES on the layer).
//   - beginFrame/endFrame: acquire nextDrawable — nil drawable → skip
//     present gracefully (fprintf(stderr,...) + return, never crash).
//     Encode render pass (clear to black); presentDrawable + commit.
//   - D4 pacing sleep preserved outside the frame (playingField loop).
//   - glfwSetWindowCloseCallback routes to shutdown EXACTLY ONCE via a flag
//     — no teardown inside the callback (M13 re-entrancy rule).
//
// Draw/text methods are honest empty stubs for tasks 5-7 (unreachable from
// the task-4 stub loop). clear() is a documented no-op (the render pass
// clears to black in beginFrame via the bridge).
//
// Metal NDC = Vulkan NDC (both y-down) — no y-flip needed anywhere.
// Shader binding convention: [[stage_in]] at buffer slot 0, MVP uniform
// at [[buffer(1)]].
//
// Resource ownership map (D15, MTL rows):
//   | Resource                     | Owner    | Note                          |
//   |------------------------------|----------|-------------------------------|
//   | 4 stb TTF slots + buffers    | MTLBackend| loaded initWindow pass 1;    |
//   |                              |          | freed in shutdown             |
//   | GLFWwindow                   | MTLBackend| created pass 2; destroyed in |
//   |                              |          | shutdown                      |
//   | CAMetalLayer                 | mtlCocoa | created pass 2; lifetime app  |
//   | MTLDevice                    | mtlCocoa | created pass 2; app lifetime  |
//   | MTLCommandQueue              | MTLBackend| created pass 2; app lifetime |
//   | Metal .metallib              | MTLBackend| loaded at initWindow;         |
//   |                              |          | app lifetime                  |
//   | window close on MTL          | glfwSetWindowCloseCallback | routes to  |
//   |                              |          | shutdown exactly once (M13)   |
//
// Zero X11 includes; zero census symbols (F1-neutral new GPU code).
// ============================================================================

#include<stdlib.h>
#include<stdio.h>
#include<string.h>
#include<unistd.h>        // readlink/ssize_t
#ifdef __APPLE__
#include<mach-o/dyld.h>   // _NSGetExecutablePath (macOS exe-dir resolution)
#endif
#include<math.h>
#include<map>
#include<utility>
#include<initializer_list>

#include<GLFW/glfw3.h>    // system glfw3

#include"renderingEngine.H"
#include"windowSize.H"    // WindowSizeFormula::Compute + EngineFontId enum
#include"mtlCocoa.H"      // C-ABI bridge: CAMetalLayer + device creation
#include"mtlBridge.H"     // C-ABI bridge: command queue + frame lifecycle

#include"imgui.h"         // task 44: menu-glue platform half

#include"stb_truetype.h"  // vendor/stb v1.26; DECLARATIONS only here — the
                          // implementation TU is obj/MTL/stbTruetypeImpl.o

class MTLBackend final : public RenderingEngine
 {public:
    MTLBackend()
      :window(nullptr),
       device_(nullptr),layer_(nullptr),queue_(nullptr),
       metallib_(nullptr),
       canonicalWidth_(0),canonicalHeight_(0),
       fbWidth_(0),fbHeight_(0),
       presentScale_(1.0f),presentOffX_(0),presentOffY_(0),
       closeRequested_(false),shutdownDone_(false),glfwInitialized_(false),
       tx_(0.0f),ty_(0.0f),angle_(0.0f),
       canvasOffX_(0.0f),canvasOffY_(0.0f),
       frameCtx_(nullptr),
       nextTextureId_(1),
       rtActive_(false),rtWidth_(0),rtHeight_(0),
       eventHead_(0),eventTail_(0),
       lastCursorX_(0.0),lastCursorY_(0.0),cursorValid_(false)
      {for (int i=0;i<4;++i)
        {fontInfo_[i]=nullptr;fontData_[i]=nullptr;
         fontDataLen_[i]=0;fontScale_[i]=0.0f;
         maxAsciiAdvance_[i]=0.0f;
         slotShelfH_[i]=0;
         for (int c=0;c<128;++c)
           glyphs_[i][c].present=false;
        }
       s_instance=this;
      }

    virtual ~MTLBackend()
      {shutdown();
      }

    void setCanonicalLayout(int playAreaWidth,int playAreaHeight,
                            int maxIconHeight)
      {canonicalPlayAreaWidth_=playAreaWidth;
       canonicalPlayAreaHeight_=playAreaHeight;
       canonicalMaxIconHeight_=maxIconHeight;
      }

    void setCanvasOffset(float dx,float dy)
      {canvasOffX_=dx;canvasOffY_=dy;}

    // ---- (1) Window lifecycle ----
    virtual bool initWindow(const char* title) override
      {// Task 4: two-pass initWindow (D15). Pass 1: load stb fonts for metrics
       // (same four slots as GL/VK). Pass 2: compute canonical client size via
       // WindowSizeFormula::Compute, create GLFW_NO_API window, attach
       // CAMetalLayer, create MTLDevice + MTLCommandQueue, load metallib.
       if (!loadFonts_())
        {shutdown();
         return false;
        }
       FontMetrics titleFm,hiScoreFm,scoreFm;
       getFontMetrics(EngineFont_Title,titleFm);
       getFontMetrics(EngineFont_HiScore,hiScoreFm);
       getFontMetrics(EngineFont_Score,scoreFm);
       int titleTextWidth=(int)measureText("Asteroids",EngineFont_Title);
       WindowSizeFormula::Compute(titleFm,hiScoreFm,scoreFm,titleTextWidth,
                                  canonicalPlayAreaWidth_,
                                  canonicalPlayAreaHeight_,
                                  canonicalMaxIconHeight_,
                                  canonicalWidth_,canonicalHeight_);
       fprintf(stderr,"mtlBackend: canonical window %dx%d\n",
               canonicalWidth_,canonicalHeight_);
       if (!glfwInit())
        {fprintf(stderr,"mtlBackend: glfwInit failed.\n");
         return false;
        }
       glfwInitialized_=true;
       glfwWindowHint(GLFW_RESIZABLE,GLFW_TRUE);
       glfwWindowHint(GLFW_VISIBLE,GLFW_TRUE);
       glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);  // Metal: no GL context
       window=glfwCreateWindow(canonicalWidth_,canonicalHeight_,title,
                               nullptr,nullptr);
       if (!window)
        {fprintf(stderr,"mtlBackend: glfwCreateWindow failed.\n");
         glfwTerminate();glfwInitialized_=false;
         return false;
        }
       // Root-center placement (mirror stage.H:182-185).
       GLFWmonitor* mon=glfwGetPrimaryMonitor();
       if (mon)
        {const GLFWvidmode* mode=glfwGetVideoMode(mon);
         if (mode)
           glfwSetWindowPos(window,
                            (mode->width-canonicalWidth_)/2,
                            (mode->height-canonicalHeight_)/2);
        }
       // Attach CAMetalLayer to the GLFW window's NSView via the C-ABI bridge.
       layer_=mtlCreateLayer();
       if (!layer_||mtlAttachToView(layer_,window)!=0)
        {fprintf(stderr,"mtlBackend: mtlAttachToView failed.\n");
         shutdown();
         return false;
        }
       // Create MTLDevice and command queue.
       device_=mtlCreateDevice();
       if (!device_)
        {fprintf(stderr,"mtlBackend: mtlCreateDevice failed.\n");
         shutdown();
         return false;
        }
       queue_=mtlCreateCommandQueue(device_);
       if (!queue_)
        {fprintf(stderr,"mtlBackend: mtlCreateCommandQueue failed.\n");
         shutdown();
         return false;
        }
       // VSync on by default (displaySyncEnabled=YES on the layer).
       mtlSetDisplaySyncEnabled(layer_,1);
       // Set the initial drawable size from the backing scale factor.
       double scale=mtlBackingScaleFactor(window);
       if (scale<=0.0) scale=1.0;
       mtlSetDrawableSize(layer_,scale,canonicalWidth_,canonicalHeight_);
       // Callbacks: framebuffer-size dirties the letterbox transform; close
       // REQUESTS shutdown via flag — never tears down in-callback (M13).
       glfwSetFramebufferSizeCallback(window,framebufferSizeTrampoline_);
       glfwSetWindowCloseCallback(window,windowCloseTrampoline_);
       glfwSetKeyCallback(window,keyTrampoline_);
       glfwSetCursorPosCallback(window,cursorPosTrampoline_);
       glfwSetMouseButtonCallback(window,mouseButtonTrampoline_);
       glfwSetCursorEnterCallback(window,cursorEnterTrampoline_);
       int fbw=0,fbh=0;
       glfwGetFramebufferSize(window,&fbw,&fbh);
       fbWidth_=fbw;fbHeight_=fbh;
       recomputePresentTransform_();
       fprintf(stderr,"mtlBackend: initWindow OK (%dx%d fb, scale %.1f)\n",
               fbWidth_,fbHeight_,(double)presentScale_);
       return true;
      }

    virtual void shutdown() override
      {if (shutdownDone_) return;
       shutdownDone_=true;
       // Release frame context if mid-frame.
       if (frameCtx_)
        {mtlEndFrame(frameCtx_);
         frameCtx_=nullptr;
        }
       // D15 release order: metallib -> queue -> (device/layer are app-lifetime
       // from mtlCocoa, intentionally never released).
       if (metallib_)
        {mtlRelease(metallib_);
         metallib_=nullptr;
        }
       if (queue_)
        {mtlRelease(queue_);
         queue_=nullptr;
        }
       // Free stb font data.
       for (int i=0;i<4;++i)
        {if (fontData_[i])
          {delete[] fontData_[i];
           fontData_[i]=nullptr;
          }
         if (fontInfo_[i])
          {delete fontInfo_[i];
           fontInfo_[i]=nullptr;
          }
        }
       if (window)
        {glfwDestroyWindow(window);
         window=nullptr;
        }
       if (glfwInitialized_)
        {glfwTerminate();
         glfwInitialized_=false;
        }
      }

    virtual X11NativeHandle nativeHandle() const override
      {// D9 seam: X11-only consumer (Motif Options dialog). Undefined/unused
       // on MTL by design (documented no-op). Same as GL/VK.
       X11NativeHandle h;
       h.display=nullptr;
       h.window=0;
       return h;
      }

    // ---- (2) Frame lifecycle ----
    virtual void beginFrame() override
      {if (shutdownDone_)
         return;
       if (window)
         glfwPollEvents();             // GLFW requires a regular pump
       if (frameCtx_)                  // domain double-pumps — guard like VK
         return;
       // Acquire next drawable and begin render pass via the bridge.
       // nil drawable → skip frame gracefully (fprintf + return, never crash).
       frameCtx_=mtlBeginFrame(layer_,queue_);
       if (!frameCtx_)
        {fprintf(stderr,"mtlBackend: nil drawable — skipping frame\n");
         return;
        }
      }

    virtual void endFrame() override
      {if (!frameCtx_)
         return;                       // no frame in progress (skipped/presentLeg=off)
       mtlEndFrame(frameCtx_);
       frameCtx_=nullptr;
      }

    // ---- (3) Seams ----
    virtual int pollEvents(GameEvent* out,int maxCount) override
      {// D16 per-event state machine, VK-identical: the GLFW callbacks
       // installed in pass 2 enqueue typed events (keys through the
       // US-layout table with PRESS/RELEASE only — GLFW_REPEAT never
       // reaches this queue; mouse inverse-present-transformed to LOGICAL
       // client coords at enqueue) and this drain hands them out.
       int count=0;
       while (eventHead_!=eventTail_&&count<maxCount)
        {out[count++]=eventQueue_[eventHead_];
         eventHead_=(eventHead_+1)%maxEventQueue_;
        }
       return count;
      }

    virtual void setScissorRect(const int* xywh) override
      {// D7/D8 (v6.1): LOGICAL CLIENT {x,y,w,h} in. Stub for task 5.
       (void)xywh;
      }

    virtual RenderTargetId createRenderTarget(int w,int h) override
      {// Stub for task 5: offscreen render targets. Return 0 (not implemented).
       (void)w;(void)h;
       return 0;
      }

    virtual void beginRenderTo(RenderTargetId target) override
      {// Stub for task 5.
       (void)target;
      }

    virtual void endRenderTo() override
      {// Stub for task 5.
      }

    virtual void getPresentTransform(float& outScale,int& outOffsetX,
                                     int& outOffsetY) override
      {// v6.1 seam — current letterbox transform (uniform scale + centered
       // offsets). Same formula as GL/VK.
       outScale=presentScale_;
       outOffsetX=presentOffX_;
       outOffsetY=presentOffY_;
      }

    // ---- (4) Primitives ----
    virtual void clear() override
      {// The render pass clears to black in beginFrame via the bridge.
       // This is a documented no-op on the GPU legs (same as VK).
      }

    virtual void drawLine(float x1,float y1,float x2,float y2,
                          float r,float g,float b,float width) override
      {// Stub for task 6: thin/thick lines via quad expansion.
       (void)x1;(void)y1;(void)x2;(void)y2;
       (void)r;(void)g;(void)b;(void)width;
      }

    virtual void drawPolygon(const float* verts,int count,
                             float r,float g,float b,bool fill) override
      {// Stub for task 6: ear-clip triangulation + LINE_LOOP outline.
       (void)verts;(void)count;(void)r;(void)g;(void)b;(void)fill;
      }

    virtual void drawRect(float x,float y,float w,float h,
                          float r,float g,float b,bool fill) override
      {// Stub for task 6: filled or outlined rect.
       (void)x;(void)y;(void)w;(void)h;
       (void)r;(void)g;(void)b;(void)fill;
      }

    virtual void drawTriangles(const float* verts,int count,
                               TextureId tex) override
      {// Stub for task 6: interleaved triangle soup [x y u v r g b] * N.
       (void)verts;(void)count;(void)tex;
      }

    // ---- (5) Text ----
    virtual void drawStringOpaque(const char* text,float x,float y,int fontId,
                                  float r,float g,float b,
                                  float bgR,float bgG,float bgB) override
      {// Stub for task 7: opaque text draw.
       (void)text;(void)x;(void)y;(void)fontId;
       (void)r;(void)g;(void)b;(void)bgR;(void)bgG;(void)bgB;
      }

    virtual void drawStringTransparent(const char* text,float x,float y,
                                       int fontId,
                                       float r,float g,float b) override
      {// Stub for task 7: transparent text draw.
       (void)text;(void)x;(void)y;(void)fontId;
       (void)r;(void)g;(void)b;
      }

    virtual float measureText(const char* text,int fontId) override
      {// Advance-width sum over the loaded TTF slot (stb metrics; GL/VK
       // verbatim semantics). Working — needed by WindowSizeFormula in pass 2.
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
       // (GL/VK verbatim semantics; D12). Working — needed by initWindow.
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
       out.lbearing=0.0f;
       out.max_bounds.ascent=ascent*s;
       out.max_bounds.descent=-descent*s;
       out.max_bounds.width=maxAsciiAdvance_[fontId];
      }

    // ---- (6) Textures ----
    virtual TextureId createTextureFromBitmap(const uint8_t* data,int w,
                                              int h,int channels) override
      {// Stub for task 6: Metal texture upload.
       (void)data;(void)w;(void)h;(void)channels;
       return 0;
      }

    virtual void drawTexture(TextureId tex,float x,float y,
                             float w,float h,float alpha) override
      {// Stub for task 6: textured quad draw.
       (void)tex;(void)x;(void)y;(void)w;(void)h;(void)alpha;
      }

    virtual void drawTextureMasked(TextureId content,TextureId mask,
                                   float x,float y,float w,float h) override
      {// Stub for task 6: R8 mask discard path.
       (void)content;(void)mask;(void)x;(void)y;(void)w;(void)h;
      }

    virtual void deleteTexture(TextureId tex) override
      {// Stub for task 6: release Metal texture.
       (void)tex;
      }

    virtual TextureId createTextureFromRGBA32(const uint8_t* pixels,int w,
                                              int h) override
      {// Stub for task 6: Dear ImGui font atlas / UI textures.
       (void)pixels;(void)w;(void)h;
       return 0;
      }

    // ---- (7) Transform ----
    virtual void setTransform(float tx,float ty,float angleRadians) override
      {// GL/VK-only semantics (m19): rotate-about-own-origin THEN translate.
       tx_=tx;ty_=ty;angle_=angleRadians;
      }

    virtual void resetTransform() override
      {tx_=0.0f;ty_=0.0f;angle_=0.0f;
      }

    // ---- Menu/input glue (task 44, D9) ----
    void installMenuInputBridge()
      {ImGuiIO& io=ImGui::GetIO();
       io.BackendPlatformName="mtlBackend_XAsteroids_GameEvents";
       io.SetClipboardTextFn=clipboardSetStub_;
       io.GetClipboardTextFn=clipboardGetStub_;
      }

    // ---- Public members (accessed by playingField.H METAL_BACKEND branches) ----
    bool closeRequested_;               // M13: close callback sets flag only

 private:
    // ---- Frame context (from bridge) ----
    MTLFrameContext* frameCtx_;

    // ---- Metal resources ----
    GLFWwindow* window;
    void* device_;                      // MTLDevice (opaque, app lifetime)
    void* layer_;                       // CAMetalLayer (opaque, retained by view)
    void* queue_;                       // MTLCommandQueue (opaque)
    void* metallib_;                    // MTLLibrary (opaque, loaded at init)

    // ---- Window/transform ----
    int canonicalWidth_,canonicalHeight_;
    int fbWidth_,fbHeight_;
    float presentScale_;
    int presentOffX_,presentOffY_;
    bool shutdownDone_;
    bool glfwInitialized_;

    // ---- Transform state ----
    float tx_,ty_,angle_;
    float canvasOffX_,canvasOffY_;

    // ---- Texture state (stub for task 6) ----
    TextureId nextTextureId_;
    bool rtActive_;
    int rtWidth_,rtHeight_;

    // ---- Layout config (set before initWindow) ----
    int canonicalPlayAreaWidth_;
    int canonicalPlayAreaHeight_;
    int canonicalMaxIconHeight_;

    // ---- D16 typed event ring (VK verbatim) ----
    static const int maxEventQueue_=256;
    GameEvent eventQueue_[maxEventQueue_];
    int eventHead_,eventTail_;
    double lastCursorX_,lastCursorY_;
    bool cursorValid_;

    // ---- Fonts (stb) ----
    struct GlyphInfo
      {float u0,v0,u1,v1,w,h,xoff,yoff;
       bool present;
      };
    stbtt_fontinfo* fontInfo_[4];
    uint8_t* fontData_[4];
    long fontDataLen_[4];
    float fontScale_[4];
    float maxAsciiAdvance_[4];
    int slotShelfH_[4];
    GlyphInfo glyphs_[4][128];

    // ---- Static singleton (callback routing) ----
    static MTLBackend* s_instance;

    // ---- resolveExeDir_ (vkBackend.H:3167-3193 pattern) ----
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

    // ---- Pass 1: load stb fonts (GL/VK verbatim) ----
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
          {fprintf(stderr,"mtlBackend: cannot open %s (slot %s).\n",
                   path,slotNames[i]);
           return false;
          }
         fseek(f,0,SEEK_END);
         long len=ftell(f);
         fseek(f,0,SEEK_SET);
         uint8_t* buf=new uint8_t[len];
         if (fread(buf,1,(size_t)len,f)!=(size_t)len)
          {fclose(f);delete[] buf;
           fprintf(stderr,"mtlBackend: short read on %s.\n",path);
           return false;
          }
         fclose(f);
         int offset=stbtt_GetFontOffsetForIndex(buf,0);
         fontInfo_[i]=new stbtt_fontinfo;
         if (offset<0||
             !stbtt_InitFont(fontInfo_[i],buf,offset))
          {delete[] buf;
           fprintf(stderr,"mtlBackend: stb_init failed on %s.\n",path);
           return false;
          }
         fontData_[i]=buf;
         fontDataLen_[i]=len;
         fontScale_[i]=stbtt_ScaleForPixelHeight(fontInfo_[i],pixelSizes[i]);
         float maxAdv=0.0f;
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

    // ---- D17.4 present transform (GL/VK verbatim formula) ----
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

    // ---- GLFW callbacks (VK verbatim pattern) ----
    static void framebufferSizeTrampoline_(GLFWwindow* win,int w,int h)
      {(void)win;
       if (s_instance)
        {s_instance->fbWidth_=w;
         s_instance->fbHeight_=h;
         s_instance->recomputePresentTransform_();
         // Update the Metal layer's drawable size to match the new framebuffer.
         if (s_instance->layer_&&s_instance->window)
          {double scale=mtlBackingScaleFactor(s_instance->window);
           if (scale<=0.0) scale=1.0;
           mtlSetDrawableSize(s_instance->layer_,scale,w,h);
          }
        }
      }

    static void windowCloseTrampoline_(GLFWwindow* win)
      {(void)win;
       if (s_instance)
         s_instance->closeRequested_=true;   // M13: flag only, no teardown
      }

    // ---- D16 input bridge (VK verbatim) ----
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

    // ---- ImGui clipboard stubs (VK/GL verbatim) ----
    static void clipboardSetStub_(void*,const char*) {}
    static const char* clipboardGetStub_(void*) {return "";}
 };

// Static member definition (header-inline: one TU per link).
inline MTLBackend* MTLBackend::s_instance=nullptr;

#endif
