#ifndef _XAST_GLBACKEND_H
#define _XAST_GLBACKEND_H

// ============================================================================
// glBackend.H — OpenGL 4.5 Core RenderingEngine backend (Phase 3, tasks 31+).
//
// Task 31 scope (this file's landing): window + context lifecycle.
//   - Two-pass initWindow (D15): pass 1 loads the FOUR stb TTF font slots
//     + metrics BEFORE sizing (m21: errorInfo is X11-only, never loaded here);
//     pass 2 computes the canonical client size from the SHARED
//     WindowSizeFormula::Compute (one formula, no divergence — the same
//     header X11Backend calls) and creates a GLFW_RESIZABLE window at that
//     size, root-centered. NO size limits anywhere (v6/S2/D15).
//   - Letterbox present transform: uniform scale + centered offsets,
//     recomputed lazily when dirty; the framebuffer-size callback only sets
//     the dirty flag (mirrors x11Backend.H semantics exactly).
//   - GL 4.5 core gate: gladLoadGLLoader then version check; any failure
//     makes initWindow return false -> main() exit(1) (M12/B5/m17).
//   - glfwSetWindowCloseCallback routes to shutdown EXACTLY ONCE via a flag
//     — no teardown inside the callback (M13 re-entrancy rule).
//   - glfwSwapInterval(0) (D4); compositor VSync override is a documented
//     escape clause (m10) — recorded, not failed.
//   - beginFrame() pumps glfwPollEvents(): GLFW requires a regular event
//     pump for close/resize callbacks to fire at all. Typed GameEvent
//     delivery stays in pollEvents() (the D16 seam; wiring lands with the
//     menu/game loop integration).
//
// Primitives/text/textures/rotation draw methods are honest empty stubs for
// tasks 32-35 (unreachable from the task-31 stub loop). clear() is real.
//
// Resource ownership map (D15, GL rows):
//   | Resource                     | Owner    | Note                          |
//   |------------------------------|----------|-------------------------------|
//   | 4 stb TTF slots + buffers    | GLBackend| loaded initWindow pass 1;     |
//   |                              |          | freed in shutdown             |
//   | GLFWwindow                   | GLBackend| created pass 2; destroyed in  |
//   |                              |          | shutdown                      |
//   | GL context / GL objects      | GLBackend| objects land tasks 32-35;     |
//   |                              |          | freed in shutdown             |
//   | window close on GL           | glfwSetWindowCloseCallback | routes to   |
//   |                              |          | shutdown exactly once (M13)   |
//
// Zero X11 includes; zero census symbols (F1-neutral new GPU code).
// ============================================================================

#include<stdlib.h>
#include<stdio.h>
#include<string.h>
#include<math.h>
#include<map>
#include<utility>

#include<glad/glad.h>     // vendor/glad (4.5 core) — MUST precede GLFW include
#include<GLFW/glfw3.h>    // system glfw3 3.5.1

#include"renderingEngine.H"
#include"windowSize.H"    // WindowSizeFormula::Compute + EngineFontId enum

#include"stb_truetype.h"  // vendor/stb v1.26; implementation expands in
                          // stbTruetypeImpl.C (extern "C") — NOT here.

class GLBackend : public RenderingEngine
 {public:
    GLBackend()
      :window(nullptr),
       canonicalWidth_(0),canonicalHeight_(0),
       fbWidth_(0),fbHeight_(0),
       canonicalPlayAreaWidth_(0),canonicalPlayAreaHeight_(0),
       canonicalMaxIconHeight_(0),
       presentDirty_(true),presentScale_(1.0f),presentOffX_(0),presentOffY_(0),
       closeRequested_(false),shutdownDone_(false),glfwInitialized_(false)
     {for (int i=0;i<4;++i)
        {fontInfo_[i]=new stbtt_fontinfo;
         fontData_[i]=nullptr;fontDataLen_[i]=0;
         fontScale_[i]=0.0f;
         maxAsciiAdvance_[i]=0.0f;
        }
      s_instance=this;   // callbacks route here (exactly one engine exists)
     }

    virtual ~GLBackend()
     {shutdown();
     }

    // Transition configuration — identical contract to X11Backend's setter:
    // play-area dims + max icon height come from game constants this header
    // cannot include; main() calls it BEFORE initWindow.
    inline void setCanonicalLayout(int playAreaWidth,int playAreaHeight,
                                   int maxIconHeight)
     {canonicalPlayAreaWidth_=playAreaWidth;
      canonicalPlayAreaHeight_=playAreaHeight;
      canonicalMaxIconHeight_=maxIconHeight;
     }

    // ------------------------------------------------------------------
    // (1) Window lifecycle
    // ------------------------------------------------------------------
    virtual bool initWindow(const char* title) override
     {if (!loadFonts())                               // ---- pass 1 (D15)
        return false;
      return createWindowAndContext(title);           // ---- pass 2 (D15)
     }

    virtual void shutdown() override
     {// Exactly-once guard (M13/D15): safe from dtor AND close callback path.
      if (shutdownDone_)
        return;
      shutdownDone_=true;
      // D15 release order: fonts -> GL objects -> window -> library.
      for (int i=0;i<4;++i)
       {if (fontData_[i])
          {delete[] fontData_[i];
           fontData_[i]=nullptr;
          }
        fontDataLen_[i]=0;
        if (fontInfo_[i])
          {delete fontInfo_[i];
           fontInfo_[i]=nullptr;
          }
       }
      // GL objects (tasks 32-35) free here as they land.
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
      // on GL by design (documented no-op).
      X11NativeHandle h;
      h.display=nullptr;
      h.window=0;
      return h;
     }

    // ------------------------------------------------------------------
    // (2) Frame lifecycle
    // ------------------------------------------------------------------
    virtual void beginFrame() override
     {// GLFW needs a regular pump or close/resize callbacks never fire.
      // Typed event DELIVERY is pollEvents()'s job (D16 seam, later task);
      // this pump only services callbacks (framebuffer-size, close).
      if (window)
        glfwPollEvents();
     }

    virtual void endFrame() override
     {if (window)
        glfwSwapBuffers(window);
     }

    // ------------------------------------------------------------------
    // (3) Seams
    // ------------------------------------------------------------------
    virtual int pollEvents(GameEvent* out,int maxCount) override
     {(void)out;(void)maxCount;
      // TODO(task 25-reuse/menu integration): drain through the D16 key->action
      // state machine fixtures (test/harness/d16-state-machine.md). The
      // underlying pump runs in beginFrame().
      return 0;
     }

    virtual void setScissorRect(const int* xywh) override
     {(void)xywh;   // TODO(task 32): glScissor AFTER present-transform map
                    // (v6.1 binding rule: logical-client in, backend maps).
     }

    virtual RenderTargetId createRenderTarget(int w,int h) override
     {(void)w;(void)h;return 0;   // TODO(task 34): FBO (M1 render-target leg)
     }
    virtual void beginRenderTo(RenderTargetId target) override
     {(void)target;               // TODO(task 34)
     }
    virtual void endRenderTo() override
     {                            // TODO(task 34)
     }

    virtual void getPresentTransform(float& outScale,int& outOffsetX,
                                     int& outOffsetY) override
     {// Uniform-scale centered letterbox (D17.4b) — math mirrors
      // x11Backend.H getPresentTransform exactly, sourced from
      // glfwGetFramebufferSize instead of XGetGeometry. Identity at the
      // canonical size; aspect never stretched; fractional scales fine.
      float scale=1.0f;
      int ox=0,oy=0;
      if (window&&canonicalWidth_>0&&canonicalHeight_>0)
       {if (presentDirty_)
          {int w=0,h=0;
           glfwGetFramebufferSize(window,&w,&h);
           fbWidth_=w;fbHeight_=h;
           presentDirty_=false;
          }
        if (fbWidth_>0&&fbHeight_>0)
         {scale=((float)fbWidth_/canonicalWidth_<(float)fbHeight_/canonicalHeight_)
                 ? (float)fbWidth_/canonicalWidth_
                 : (float)fbHeight_/canonicalHeight_;
          ox=((int)fbWidth_-(int)(canonicalWidth_*scale))/2;
          oy=((int)fbHeight_-(int)(canonicalHeight_*scale))/2;
         }
       }
      outScale=scale;outOffsetX=ox;outOffsetY=oy;
     }

    // ------------------------------------------------------------------
    // (4) Primitives
    // ------------------------------------------------------------------
    virtual void clear() override
     {// backgroundGC equivalent (R4-M1): GXcopy black — glClearColor black +
      // full-buffer clear. Real as of task 31 (trivial, needed by the stub
      // loop); viewport/scissor interplay lands with task 32.
      if (!window)
        return;
      glClearColor(0.0f,0.0f,0.0f,1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
     }

    virtual void drawLine(float x1,float y1,float x2,float y2,
                          float r,float g,float b,float width) override
     {(void)x1;(void)y1;(void)x2;(void)y2;(void)r;(void)g;(void)b;(void)width;
      // TODO(task 32): width 1 = GL_LINE_STRIP; width 3 = filled quads (SC8).
     }

    virtual void drawPolygon(const float* verts,int count,
                             float r,float g,float b,bool fill) override
     {(void)verts;(void)count;(void)r;(void)g;(void)b;(void)fill;
      // TODO(task 32): fill = ear-clipped GL_TRIANGLES; outline = GL_LINE_LOOP.
     }

    virtual void drawRect(float x,float y,float w,float h,
                          float r,float g,float b,bool fill) override
     {(void)x;(void)y;(void)w;(void)h;(void)r;(void)g;(void)b;(void)fill;
      // TODO(task 32): polygon path.
     }

    virtual void drawTriangles(const float* verts,int count,
                               TextureId tex) override
     {(void)verts;(void)count;(void)tex;
      // TODO(task 34a): indexed triangle list, per-vertex color, WINDOW-space
      // (the SOLE window-space method — v6.1 coordinate-space rule; consumed
      // by the Dear ImGui adapter, task 44a).
     }

    // ------------------------------------------------------------------
    // (5) Text
    // ------------------------------------------------------------------
    virtual void drawStringOpaque(const char* text,float x,float y,int fontId,
                                  float r,float g,float b,
                                  float bgR,float bgG,float bgB) override
     {(void)text;(void)x;(void)y;(void)fontId;(void)r;(void)g;(void)b;
      (void)bgR;(void)bgG;(void)bgB;
      // TODO(task 33): glyph quads over a bg quad (XDrawImageString mapping).
     }

    virtual void drawStringTransparent(const char* text,float x,float y,
                                       int fontId,
                                       float r,float g,float b) override
     {(void)text;(void)x;(void)y;(void)fontId;(void)r;(void)g;(void)b;
      // TODO(task 33): glyph quads only (XDrawString mapping).
     }

    virtual float measureText(const char* text,int fontId) override
     {// Advance-width sum over the loaded TTF slot (stb metrics). Total-
      // width centering policy per D12/M5 — callers must not assume
      // monospace. Returns 0 for invalid slot/text like the X11 backend.
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
     {// stb metrics mapped onto FontMetrics incl. per-font max_bounds (D12
      // signature-level requirement). max_bounds.width = max ASCII advance
      // (computed once at load); ascent/descent = font-level VMetrics scaled.
      if (fontId<0||fontId>3||!fontInfo_[fontId]||!fontData_[fontId])
        {out.ascent=out.descent=out.lbearing=0.0f;
         out.max_bounds.ascent=out.max_bounds.descent=out.max_bounds.width=0.0f;
         return;
        }
      float s=fontScale_[fontId];
      int ascent=0,descent=0,lineGap=0;
      stbtt_GetFontVMetrics(fontInfo_[fontId],&ascent,&descent,&lineGap);
      out.ascent=ascent*s;
      out.descent=-descent*s;              // stb descent is <=0; X11-style positive
      out.lbearing=0.0f;                   // conservative font-level value
      out.max_bounds.ascent=ascent*s;
      out.max_bounds.descent=-descent*s;
      out.max_bounds.width=maxAsciiAdvance_[fontId];
     }

    // ------------------------------------------------------------------
    // (6) Textures
    // ------------------------------------------------------------------
    virtual TextureId createTextureFromBitmap(const uint8_t* data,int w,int h,
                                              int channels) override
     {(void)data;(void)w;(void)h;(void)channels;
      // TODO(task 34): R8 for channels==1 else RGB/RGBA; fed by xbmDecode (26).
      return 0;
     }

    virtual void drawTexture(TextureId tex,float x,float y,float w,float h,
                             float alpha) override
     {(void)tex;(void)x;(void)y;(void)w;(void)h;(void)alpha;
      // TODO(task 34): textured quad, alpha blend in the D8 canonical order.
     }

    virtual void drawTextureMasked(TextureId content,TextureId mask,
                                   float x,float y,float w,float h) override
     {(void)content;(void)mask;(void)x;(void)y;(void)w;(void)h;
      // TODO(task 34): content quad + R8 mask texture, fragment discard (D6).
     }

    virtual void deleteTexture(TextureId tex) override
     {(void)tex;                  // TODO(task 34)
     }

    virtual TextureId createTextureFromRGBA32(const uint8_t* pixels,int w,
                                              int h) override
     {(void)pixels;(void)w;(void)h;
      // TODO(task 34a): RGBA8888 upload (Dear ImGui font atlas / UI textures).
      return 0;
     }

    // ------------------------------------------------------------------
    // (7) Transform
    // ------------------------------------------------------------------
    virtual void setTransform(float tx,float ty,float angleRadians) override
     {(void)tx;(void)ty;(void)angleRadians;
      // TODO(task 32): MVP uniform (GL/VK-only semantics, m19).
     }

    virtual void resetTransform() override
     {                            // TODO(task 32): identity.
     }

    // ------------------------------------------------------------------
    // Internals
    // ------------------------------------------------------------------
    bool loadFonts()
     {// Pass 1 (D15): FOUR stb TTF slots (D12 list; NO errorInfo — m21).
      // Fonts 1-3 (title/button/hi-score) render from DejaVuSans-Bold.ttf;
      // font 4 (score display, courier role) from DejaVuSansMono-Bold.ttf.
      static const char* const paths[4]=
       {"vendor/fonts/DejaVuSans-Bold.ttf",
        "vendor/fonts/DejaVuSans-Bold.ttf",
        "vendor/fonts/DejaVuSans-Bold.ttf",
        "vendor/fonts/DejaVuSansMono-Bold.ttf"};
      static const float pixelSizes[4]={40.0f,10.0f,20.0f,40.0f};
      static const char* const slotNames[4]=
       {"title","button","hi-score","score"};
      for (int i=0;i<4;++i)
       {FILE* f=fopen(paths[i],"rb");
        if (!f)
         {fprintf(stderr,"glBackend: cannot open %s (slot %s).\n",
                  paths[i],slotNames[i]);
          return false;
         }
        fseek(f,0,SEEK_END);
        long len=ftell(f);
        fseek(f,0,SEEK_SET);
        uint8_t* buf=new uint8_t[len];
        if (fread(buf,1,(size_t)len,f)!=(size_t)len)
         {fclose(f);delete[] buf;
          fprintf(stderr,"glBackend: short read on %s.\n",paths[i]);
          return false;
         }
        fclose(f);
        int offset=stbtt_GetFontOffsetForIndex(buf,0);
        if (offset<0||
            !stbtt_InitFont(fontInfo_[i],buf,offset))
         {delete[] buf;
          fprintf(stderr,"glBackend: stb_init failed on %s.\n",paths[i]);
          return false;
         }
        fontData_[i]=buf;
        fontDataLen_[i]=len;
        fontScale_[i]=stbtt_ScaleForPixelHeight(fontInfo_[i],pixelSizes[i]);
        // max ASCII advance (32..126) — the max_bounds.width analogue.
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

    bool createWindowAndContext(const char* title)
     {// Pass 2 (D15): SHARED formula -> GLFW_RESIZABLE window -> context ->
      // 4.5-core gate. The formula inputs mirror X11Backend pass 2 exactly
      // (titleFm/hiScoreFm/scoreFm + measureText("Asteroids") + play area +
      // maxIconHeight) so both backends agree on the canonical client size.
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
      if (!glfwInit())
       {fprintf(stderr,"glBackend: glfwInit failed.\n");
        return false;
       }
      glfwInitialized_=true;
      glfwWindowHint(GLFW_RESIZABLE,GLFW_TRUE);    // v6/S2: freely resizable
      glfwWindowHint(GLFW_VISIBLE,GLFW_TRUE);
      glfwWindowHint(GLFW_CLIENT_API,GLFW_OPENGL_API);
      window=glfwCreateWindow(canonicalWidth_,canonicalHeight_,title,
                              nullptr,nullptr);
      if (!window)
       {fprintf(stderr,"glBackend: glfwCreateWindow failed.\n");
        glfwTerminate();glfwInitialized_=false;
        return false;
       }
      // Root-center placement (mirror stage.H:182-185 via the primary
      // monitor's video mode). NO size limits anywhere (v6/S2/D15).
      GLFWmonitor* mon=glfwGetPrimaryMonitor();
      if (mon)
       {const GLFWvidmode* mode=glfwGetVideoMode(mon);
        if (mode)
          glfwSetWindowPos(window,
                           (mode->width-canonicalWidth_)/2,
                           (mode->height-canonicalHeight_)/2);
       }
      glfwMakeContextCurrent(window);
      glfwSwapInterval(0);                         // D4; m10 escape clause
      if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
       {fprintf(stderr,"glBackend: gladLoadGLLoader failed.\n");
        shutdown();
        return false;
       }
      // GL 4.5 core gate (B5/m17: NOT 4.6 — 4.5 keeps Mesa/Intel alive).
      const GLubyte* ver=glGetString(GL_VERSION);
      const GLubyte* ren=glGetString(GL_RENDERER);
      fprintf(stderr,"glBackend: GL_VERSION=%s GL_RENDERER=%s\n",
              ver?(const char*)ver:"?",ren?(const char*)ren:"?");
      if (GLVersion.major<4||(GLVersion.major==4&&GLVersion.minor<5))
       {fprintf(stderr,"glBackend: OpenGL 4.5 core required, got %d.%d.\n",
                GLVersion.major,GLVersion.minor);
        shutdown();
        return false;
       }
      // Callbacks: framebuffer-size dirties the letterbox transform; close
      // REQUESTS shutdown via flag — never tears down in-callback (M13).
      glfwSetFramebufferSizeCallback(window,framebufferSizeTrampoline);
      glfwSetWindowCloseCallback(window,windowCloseTrampoline);
      int fbw=0,fbh=0;
      glfwGetFramebufferSize(window,&fbw,&fbh);
      fbWidth_=fbw;fbHeight_=fbh;
      glViewport(0,0,fbw,fbh);
      presentDirty_=false;
      return true;
     }

    static void framebufferSizeTrampoline(GLFWwindow* win,int w,int h)
     {(void)win;
      // Single-instance backend (main() constructs exactly one engine).
      // Route through the file-static instance pointer set at creation.
      if (s_instance)
       {s_instance->fbWidth_=w;
        s_instance->fbHeight_=h;
        s_instance->presentDirty_=true;
        glViewport(0,0,w,h);
       }
     }

    static void windowCloseTrampoline(GLFWwindow* win)
     {(void)win;
      if (s_instance)
        s_instance->closeRequested_=true;   // M13: flag only, no teardown
     }

    bool validFont(int fontId) const
     {return fontId>=0&&fontId<=3&&fontInfo_[fontId]!=nullptr
             &&fontData_[fontId]!=nullptr;
     }

    // ------------------------------------------------------------------
    // Members
    // ------------------------------------------------------------------
    GLFWwindow* window;
    int canonicalWidth_,canonicalHeight_;    // D15-formula client size (fixed)
    int fbWidth_,fbHeight_;                  // last-known framebuffer size
    int canonicalPlayAreaWidth_,canonicalPlayAreaHeight_;
    int canonicalMaxIconHeight_;
    bool presentDirty_;
    float presentScale_;
    int presentOffX_,presentOffY_;
    bool closeRequested_;
    bool shutdownDone_;
    bool glfwInitialized_;

    stbtt_fontinfo* fontInfo_[4];            // slots 0..3 = EngineFont 0..3
    uint8_t* fontData_[4];                   // owned TTF buffers
    long fontDataLen_[4];
    float fontScale_[4];
    float maxAsciiAdvance_[4];

    static GLBackend* s_instance;            // callback routing (single instance)
 };

// Header-inline static definition — glBackend.H is included by exactly ONE TU
// per link (XAsteroids.o, the repo-wide header-inline convention), so this is
// ODR-safe under the current build structure.
GLBackend* GLBackend::s_instance=nullptr;

#endif
