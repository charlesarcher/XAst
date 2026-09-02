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
#include<vector>
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
       linePipeline_(nullptr),triPipeline_(nullptr),
       outlinePipeline_(nullptr),texPipeline_(nullptr),
       maskedPipeline_(nullptr),
       vertexBuf_(nullptr),vertWriteOffset_(0),
       canonicalWidth_(0),canonicalHeight_(0),
       fbWidth_(0),fbHeight_(0),
       presentScale_(1.0f),presentOffX_(0),presentOffY_(0),
       closeRequested_(false),shutdownDone_(false),glfwInitialized_(false),
       tx_(0.0f),ty_(0.0f),angle_(0.0f),
       canvasOffX_(0.0f),canvasOffY_(0.0f),
       frameCtx_(nullptr),
       nextTextureId_(1),
       atlasTex_(0),
       rtActive_(false),rtWidth_(0),rtHeight_(0),
       scissorActive_(false),
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
       scissorRect_[0]=scissorRect_[1]=scissorRect_[2]=scissorRect_[3]=0;
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
       // Load the pre-compiled .metallib and create the 5 render pipelines
       // (mirroring VK's line/tri/outline/tex/masked). The metallib lives at
       // obj/MTL/aestroids.metallib next to the binary (makefile rule).
        if (!createPipelines_())
         {shutdown();
          return false;
         }
        // Build the text glyph atlas (task 7) — rasterizes ASCII 32..126 for
        // all four font slots into one RGBA8 coverage texture.
        if (!initText_())
         {fprintf(stderr,"mtlBackend: initText_ failed\n");
          shutdown();
          return false;
         }
       // Create the dynamic shared-storage vertex buffer (per-frame ring).
       vertexBuf_=mtlCreateVertexBuffer(device_,kVertexBufSize_);
       if (!vertexBuf_)
        {fprintf(stderr,"mtlBackend: mtlCreateVertexBuffer failed.\n");
         shutdown();
         return false;
        }
       vertWriteOffset_=0;
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
       // D15 release order: pipelines -> metallib -> queue -> (device/layer
       // are app-lifetime from mtlCocoa, intentionally never released).
       destroyPipelines_();
       // Release textures and the dynamic vertex buffer.
       for (auto& kv:textures_)
        {if (kv.second)
           mtlRelease(kv.second);
        }
       textures_.clear();
       if (vertexBuf_)
        {mtlRelease(vertexBuf_);
         vertexBuf_=nullptr;
        }
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
       // Bind the default solid pipeline so the first frame's clear renders
       // with the correct blend/cull/depth state (mirrors VK's preamble).
       mtlSetPipeline(frameCtx_,triPipeline_);
       // Reset the dynamic vertex-buffer write cursor for this frame.
       vertWriteOffset_=0;
       // Apply the latched scissor (present-transform-mapped to window space).
       applyPresentTransformToScissor_();
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
      {// D7/D8 (v6.1): LOGICAL CLIENT {x,y,w,h} in; the present transform is
       // applied at frame begin (applyPresentTransformToScissor_, identity
       // today). nullptr = full extent — the HUD pass is un-scissored.
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
      {// SC8, vkBackend verbatim: width<2 = 2-vert LINE_LIST; width>1 =
       // filled quad expanded perpendicular in LOGICAL space (thickLineQuad
       // port) drawn through triPipeline. Color verts repacked to the
       // shader's 32-byte stride (pos, uv=0, color).
       if (!frameCtx_)
         return;
       float verts[48];
       int count;
       void* pipe;
       int prim;
       if (width<2.0f)
        {makeColorVert(verts,0,x1,y1,r,g,b);
         makeColorVert(verts,1,x2,y2,r,g,b);
         count=2;
         pipe=linePipeline_;
         prim=MTL_PRIMITIVE_LINE;
        }
       else
        {thickLineQuad(verts,x1,y1,x2,y2,width*0.5f,r,g,b);
         count=6;
         pipe=triPipeline_;
         prim=MTL_PRIMITIVE_TRIANGLE;
        }
       drawColorPrims_(pipe,prim,verts,count);
      }

    virtual void drawPolygon(const float* verts,int count,
                             float r,float g,float b,bool fill) override
      {if (!frameCtx_||!verts||count<2)
         return;
       if (!fill)
        {// GL_LINE_LOOP emulation: Metal has no loop topology — emit the
         // closing segment explicitly (n+1 verts).
         float line[8*261];
         int n=count<260?count:260;
         for (int i=0;i<n;++i)
           makeColorVert(line,i,verts[2*i],verts[2*i+1],r,g,b);
         makeColorVert(line,n,verts[0],verts[1],r,g,b);
         drawColorPrims_(outlinePipeline_,MTL_PRIMITIVE_LINE,line,n+1);
         return;
        }
       // Ear-clipped triangles (glBackend port incl. the fan fallback).
       float pairs[2*1536];
       int ntri=earClip(verts,count,pairs);
       if (ntri<1)
        {float fan[8*256];
         int n=0;
         for (int i=1;i+1<count&&i<255;++i)
          {makeColorVert(fan,n++,verts[0],verts[1],r,g,b);
           makeColorVert(fan,n++,verts[2*i],verts[2*i+1],r,g,b);
           makeColorVert(fan,n++,verts[2*i+2],verts[2*i+3],r,g,b);
          }
         drawColorPrims_(triPipeline_,MTL_PRIMITIVE_TRIANGLE,fan,n);
         return;
        }
       float out[8*1536];
       for (int i=0;i<ntri*3;++i)
         makeColorVert(out,i,pairs[2*i],pairs[2*i+1],r,g,b);
       drawColorPrims_(triPipeline_,MTL_PRIMITIVE_TRIANGLE,out,ntri*3);
      }

    virtual void drawRect(float x,float y,float w,float h,
                          float r,float g,float b,bool fill) override
      {if (!frameCtx_)
         return;
       float quad[10]={x,y, x+w,y, x+w,y+h, x,y+h};
       if (fill)
        {static const int idx[6]={0,1,2,0,2,3};
         float tris[48];
         for (int i=0;i<6;++i)
           makeColorVert(tris,i,quad[2*idx[i]],quad[2*idx[i]+1],r,g,b);
         drawColorPrims_(triPipeline_,MTL_PRIMITIVE_TRIANGLE,tris,6);
        }
       else
         drawPolygon(quad,4,r,g,b,false);
      }

    virtual void drawTriangles(const float* verts,int count,
                               TextureId tex) override
      {// D9 menu pair: interleaved [x y u v r g b]*count, WINDOW-space
       // coords — the SOLE window-space method (v6.1 rule), so the MVP here
       // is windowMVP_ (pixel coords -> NDC). The 7-float engine contract
       // repacks onto the pipeline's clean 8-float stride with alpha=1.
       auto it=textures_.find(tex);
       if (!frameCtx_||!verts||count<3||it==textures_.end())
         return;
       std::vector<float> packed((size_t)count*8);
       for (int i=0;i<count;++i)
        {const float* s=verts+(size_t)i*7;
         float* d=&packed[(size_t)i*8];
         d[0]=s[0];d[1]=s[1];d[2]=s[2];d[3]=s[3];
         d[4]=s[4];d[5]=s[5];d[6]=s[6];d[7]=1.0f;
        }
       drawTexPrims_(texPipeline_,MTL_PRIMITIVE_TRIANGLE,it->second,nullptr,
                     packed.data(),count,windowMVP_());
      }

    // ---- (5) Text ----
    virtual void drawStringOpaque(const char* text,float x,float y,int fontId,
                                  float r,float g,float b,
                                  float bgR,float bgG,float bgB) override
      {// Opaque-text mapping (D12): cell background quad THEN glyphs
       // (vkBackend verbatim).
       if (!frameCtx_||!validFont(fontId)||!text||!*text)
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
       if (!frameCtx_||!validFont(fontId)||!text||!*text)
         return;
       drawGlyphs_(text,x,y,fontId,r,g,b);
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
      {// D13: the ONE texture entry point (xbmDecode output; channels
       // 1/3/4). Metal path: shared-storage MTLTexture upload. channels==3
       // expands to RGBA8 (documented divergence; content opaque).
       if (!device_||!data||w<=0||h<=0
           ||(channels!=1&&channels!=3&&channels!=4))
         return 0;
       int fmt=channels==1?MTL_TEX_R8:MTL_TEX_RGBA8;
       size_t bytesPerRow=(size_t)w*(channels==1?1u:4u);
       void* tex=nullptr;
       if (channels==3)
        {// expand RGB888 -> RGBA888 row by row
         std::vector<uint8_t> rgba((size_t)w*h*4);
         for (int y=0;y<h;++y)
          {const uint8_t* src=data+(size_t)y*w*3;
           uint8_t* drow=&rgba[(size_t)y*w*4];
           for (int x=0;x<w;++x)
            {drow[x*4+0]=src[x*3+0];
             drow[x*4+1]=src[x*3+1];
             drow[x*4+2]=src[x*3+2];
             drow[x*4+3]=255;
            }
          }
         tex=mtlCreateTexture(device_,w,h,fmt,rgba.data(),bytesPerRow);
        }
       else
         tex=mtlCreateTexture(device_,w,h,fmt,data,bytesPerRow);
       if (!tex)
        {fprintf(stderr,"mtlBackend: texture upload failed\n");
         return 0;
        }
       TextureId id=nextTextureId_++;
       textures_[id]=tex;
       return id;
      }

    virtual void drawTexture(TextureId tex,float x,float y,
                             float w,float h,float alpha) override
      {// Textured quad, alpha-blended (D8 canonical order governs call
       // order). Tint=(1,1,1,alpha): through FragColor = texture*vColor this
       // reproduces GL's vec4(c.rgb,c.a*uAlpha) exactly.
       auto it=textures_.find(tex);
       if (!frameCtx_||it==textures_.end())
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
       drawTexPrims_(texPipeline_,MTL_PRIMITIVE_TRIANGLE,it->second,nullptr,
                     verts,6,presentMVP_());
      }

    virtual void drawTextureMasked(TextureId content,TextureId mask,
                                   float x,float y,float w,float h) override
      {// D6 Metal half: content quad discarded where the R8 mask is off
       // (<0.5) — glBackend maskedProgram_ semantics on the masked pipeline.
       auto c=textures_.find(content);
       auto m=textures_.find(mask);
       if (!frameCtx_||c==textures_.end()||m==textures_.end())
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
         v[4]=1.0f;v[5]=1.0f;v[6]=1.0f;v[7]=1.0f;
        }
       drawTexPrims_(maskedPipeline_,MTL_PRIMITIVE_TRIANGLE,c->second,m->second,
                     verts,6,presentMVP_());
      }

    virtual void deleteTexture(TextureId tex) override
      {auto it=textures_.find(tex);
       if (it==textures_.end())
         return;
       if (it->second)
         mtlRelease(it->second);
       textures_.erase(it);
      }

    virtual TextureId createTextureFromRGBA32(const uint8_t* pixels,int w,
                                              int h) override
      {// D9 menu pair: Dear ImGui font atlas / UI textures (RGBA8888).
       return createTextureFromBitmap(pixels,w,h,4);
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
    void* linePipeline_;                // MTLRenderPipelineState (opaque)
    void* triPipeline_;
    void* outlinePipeline_;
    void* texPipeline_;
    void* maskedPipeline_;
    void* vertexBuf_;                   // MTLBuffer (shared-storage, dynamic)
    size_t vertWriteOffset_;            // per-frame write cursor into vertexBuf_

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

    // ---- MVP matrices (column-major, 16 floats) ----
    float mvp_[16];
    float winMvp_[16];

    // ---- Texture state ----
    TextureId nextTextureId_;
    std::map<TextureId,void*> textures_;   // TextureId -> MTLTexture* (opaque)
    TextureId atlasTex_;                   // task 7 text atlas (RGBA8, coverage
                                           // in all 4 channels — see initText_)
    bool rtActive_;
    int rtWidth_,rtHeight_;

    // ---- Scissor state (D7/D8: LOGICAL CLIENT {x,y,w,h}) ----
    bool scissorActive_;
    int scissorRect_[4];

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
      {float u0,v0,u1,v1;      // atlas UVs
       float w,h;              // bitmap size in logical px
       float advance;          // x advance (logical px)
       float bearingX,bearingY;// pen -> bitmap top-left offset
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

    // ---- Task 7 text: shelf-packed coverage atlas + per-glyph quads ----
    // (vkBackend initText_/drawGlyphs_ port). The atlas uploads as RGBA8 with
    // the coverage byte in ALL FOUR channels: through aestroidsFragTex
    // (texture*vColor) and the SRC_ALPHA blend this reproduces glBackend's
    // vec4(uColor*a,a) math exactly (rgb = tint*a^2 + dst*(1-a)).
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
           stbtt_FreeBitmap(bmp,nullptr);
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
       auto it=textures_.find(atlasTex_);
       if (!frameCtx_||it==textures_.end())
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
       drawTexPrims_(texPipeline_,MTL_PRIMITIVE_TRIANGLE,it->second,nullptr,
                     verts,quads*6,presentMVP_());
      }

    bool validFont(int fontId) const
      {return fontId>=0&&fontId<=3&&fontInfo_[fontId]!=nullptr
              &&fontData_[fontId]!=nullptr;
      }

    // ---- Pipeline creation (task 5: mirror VK's 5 pipelines) ----
    bool createPipelines_()
      {// Resolve the metallib path next to the binary (obj/MTL/aestroids.metallib),
       // with a CWD fallback so `./XAsteroids` from the repo root works.
       char dir[4096];
       if (!resolveExeDir_(dir,sizeof dir))
         strcpy(dir,".");
       char libPath[4200];
       snprintf(libPath,sizeof libPath,"%s/obj/MTL/aestroids.metallib",dir);
       metallib_=mtlLoadLibrary(device_,libPath);
       if (!metallib_)
        {fprintf(stderr,"mtlBackend: failed to load %s\n",libPath);
         fprintf(stderr,"mtlBackend: rebuild with `make BACKEND=MTL`\n");
         return false;
        }
       void** pipes[5]={&linePipeline_,&triPipeline_,&outlinePipeline_,
                        &texPipeline_,&maskedPipeline_};
       for (int i=0;i<5;++i)
        {*pipes[i]=mtlCreatePipeline(device_,metallib_,i);
         if (!*pipes[i])
          {fprintf(stderr,"mtlBackend: mtlCreatePipeline(%d) failed\n",i);
           return false;
          }
        }
       fprintf(stderr,"mtlBackend: 5 render pipelines created\n");
       return true;
      }

    void destroyPipelines_()
      {void* pipes[5]={linePipeline_,triPipeline_,outlinePipeline_,
                       texPipeline_,maskedPipeline_};
       for (int i=0;i<5;++i)
        {if (pipes[i])
           mtlRelease(pipes[i]);
         pipes[i]=nullptr;
        }
       linePipeline_=triPipeline_=outlinePipeline_=texPipeline_
                   =maskedPipeline_=nullptr;
      }

    // ---- Dynamic vertex buffer (per-frame ring, mirrors VK reserveVerts_) ----
    static const size_t kVertexBufSize_=1<<20;   // 1 MiB shared-storage
    size_t reserveVerts_(size_t bytes)
      {if (vertWriteOffset_+bytes>kVertexBufSize_)
         return (size_t)-1;
       size_t off=vertWriteOffset_;
       vertWriteOffset_+=bytes;
       return off;
      }

    // ---- Color-vertex repack: pos(2) + uv(2)=0 + color(4) = 32-byte stride ----
    static void makeColorVert(float* dst,int slot,float x,float y,
                              float r,float g,float b)
      {dst[slot*8+0]=x;dst[slot*8+1]=y;
       dst[slot*8+2]=0.0f;dst[slot*8+3]=0.0f;
       dst[slot*8+4]=r;dst[slot*8+5]=g;dst[slot*8+6]=b;dst[slot*8+7]=1.0f;
      }

    static void thickLineQuad(float* dst,float x1,float y1,float x2,
                              float y2,float halfW,float r,float g,float b)
      {// SC8 port (glBackend verbatim): perpendicular expansion in LOGICAL
       // space, butt caps. Writes 8-float color verts.
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
         makeColorVert(dst,i,cx[idx[i]],cy[idx[i]],r,g,b);
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

    // ---- MVP composition (vkBackend presentMVP_/windowMVP_ verbatim) ----
    const float* presentMVP_()
      {float W=(float)fbWidth_;
       float H=(float)fbHeight_;
       float a=2.0f*presentScale_/W;
       float c=2.0f*(float)presentOffX_/W-1.0f;
       float b=2.0f*presentScale_/H;
       float d=2.0f*(float)presentOffY_/H-1.0f;
       float cs=cosf(angle_),sn=sinf(angle_);
       float ma=cs,mb=-sn,mc=tx_+canvasOffX_;
       float md=sn,me=cs,mf=ty_+canvasOffY_;
       mvp_[0]=a*ma;    mvp_[1]=b*md;    mvp_[2]=0.0f;mvp_[3]=0.0f;
       mvp_[4]=a*mb;    mvp_[5]=b*me;    mvp_[6]=0.0f;mvp_[7]=0.0f;
       mvp_[8]=0.0f;    mvp_[9]=0.0f;    mvp_[10]=1.0f;mvp_[11]=0.0f;
       mvp_[12]=a*mc+c; mvp_[13]=b*mf+d; mvp_[14]=0.0f;mvp_[15]=1.0f;
       return mvp_;
      }

    const float* windowMVP_()
      {float W=(float)fbWidth_;
       float H=(float)fbHeight_;
       winMvp_[0]=2.0f/W; winMvp_[1]=0.0f; winMvp_[2]=0.0f; winMvp_[3]=0.0f;
       winMvp_[4]=0.0f; winMvp_[5]=2.0f/H; winMvp_[6]=0.0f; winMvp_[7]=0.0f;
       winMvp_[8]=0.0f; winMvp_[9]=0.0f; winMvp_[10]=1.0f;winMvp_[11]=0.0f;
       winMvp_[12]=-1.0f;winMvp_[13]=-1.0f;winMvp_[14]=0.0f;winMvp_[15]=1.0f;
       return winMvp_;
      }

    // ---- Draw helpers (vkBackend drawColorPrims_/drawTexPrims_ analogs) ----
    void drawColorPrims_(void* pipe,int primType,const float* interleaved,
                         int vertCount)
      {if (!frameCtx_||!interleaved||vertCount<1)
         return;
       size_t bytes=(size_t)vertCount*8*sizeof(float);
       size_t off=reserveVerts_(bytes);
       if (off==(size_t)-1)
         return;
       mtlUpdateVertexBuffer(vertexBuf_,off,interleaved,bytes);
       mtlSetPipeline(frameCtx_,pipe);
       mtlSetMvp(frameCtx_,presentMVP_());
       mtlSetVertexBuffer(frameCtx_,vertexBuf_,off);
       mtlDraw(frameCtx_,primType,vertCount);
      }

    void drawTexPrims_(void* pipe,int primType,void* tex0,void* tex1,
                       const float* verts,int count,const float* mvp)
      {if (!frameCtx_||count<3)
         return;
       size_t bytes=(size_t)count*8*sizeof(float);
       size_t off=reserveVerts_(bytes);
       if (off==(size_t)-1)
         return;
       mtlUpdateVertexBuffer(vertexBuf_,off,verts,bytes);
       mtlSetPipeline(frameCtx_,pipe);
       mtlSetMvp(frameCtx_,mvp);
       if (tex0)
         mtlSetTexture(frameCtx_,0,tex0);
       if (tex1)
         mtlSetTexture(frameCtx_,1,tex1);
       mtlSetVertexBuffer(frameCtx_,vertexBuf_,off);
       mtlDraw(frameCtx_,primType,count);
      }

    // ---- Scissor (vkBackend applyPresentTransformToScissor_ verbatim) ----
    void applyPresentTransformToScissor_()
      {if (!frameCtx_)
         return;
       if (!scissorActive_)
        {mtlSetScissor(frameCtx_,0,0,fbWidth_,fbHeight_);
         return;
        }
       int x=presentOffX_+(int)(scissorRect_[0]*presentScale_);
       int y=presentOffY_+(int)(scissorRect_[1]*presentScale_);
       int w=(int)(scissorRect_[2]*presentScale_);
       int h=(int)(scissorRect_[3]*presentScale_);
       mtlSetScissor(frameCtx_,x,y,w,h);
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
