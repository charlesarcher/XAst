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
//     stored by recomputePresentTransform_() — called from the
//     framebuffer-size callback AND at init (D17.4b remediation: one
//     writer, one source of truth; the former lazy dirty-flag refresh let
//     draws revert to stale identity values after a resize).
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
#include<initializer_list>

#include<glad/glad.h>     // vendor/glad (4.5 core) — MUST precede GLFW include
#include<GLFW/glfw3.h>    // system glfw3 3.5.1

#include"renderingEngine.H"
#include"windowSize.H"    // WindowSizeFormula::Compute + EngineFontId enum

#include"imgui.h"         // task 44a: menu-glue platform half (clipboard
                          // stubs); the renderer half lives in menuAdapter.H

#include"stb_truetype.h"  // vendor/stb v1.26; implementation expands in
                          // stbTruetypeImpl.C (extern "C") — NOT here.


// ---- TEMP task-36 instrumentation (XAST_GL_TRACE) — REMOVE BEFORE COMMIT ----
inline bool glTraceOn()
 {static int on=-1; if(on<0) on=getenv("XAST_GL_TRACE")?1:0; return on==1;}
// -----------------------------------------------------------------------------

class GLBackend : public RenderingEngine
 {public:
    GLBackend()
      :window(nullptr),
       canonicalWidth_(0),canonicalHeight_(0),
       fbWidth_(0),fbHeight_(0),
       canonicalPlayAreaWidth_(0),canonicalPlayAreaHeight_(0),
       canonicalMaxIconHeight_(0),
       presentScale_(1.0f),presentOffX_(0),presentOffY_(0),
       closeRequested_(false),shutdownDone_(false),glfwInitialized_(false),
       colorProgram_(0),mvpLocation_(-1),vao_(0),vbo_(0),
       tx_(0.0f),ty_(0.0f),angle_(0.0f),
       atlasTex_(0),textProgram_(0),
       textMvpLocation_(-1),textColorLocation_(-1),textAtlasLocation_(-1),
       textVao_(0),textVbo_(0),
       nextTextureId_(1),
       texProgram_(0),maskedProgram_(0),triProgram_(0),
       texMvpLocation_(-1),texSamplerLocation_(-1),texAlphaLocation_(-1),
       maskedMvpLocation_(-1),maskedContentLocation_(-1),maskedMaskLocation_(-1),
       triMvpLocation_(-1),triTexLocation_(-1),
       triVao_(0),triVbo_(0),
       rtActive_(false),rtWidth_(0),rtHeight_(0)
     {for (int i=0;i<4;++i)
        {fontInfo_[i]=new stbtt_fontinfo;
         fontData_[i]=nullptr;fontDataLen_[i]=0;
         fontScale_[i]=0.0f;
         maxAsciiAdvance_[i]=0.0f;
         slotShelfH_[i]=0;
         for (int c=0;c<128;++c)
           glyphs_[i][c].present=false;
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
      // GL objects: program/buffer/array first (need a current context —
      // glfwDestroyWindow below would strip it), then the window/library.
      if (textVbo_)
        glDeleteBuffers(1,&textVbo_);
      if (textVao_)
        glDeleteVertexArrays(1,&textVao_);
      if (atlasTex_)
        glDeleteTextures(1,&atlasTex_);
      if (textProgram_)
        glDeleteProgram(textProgram_);
      textVbo_=0;textVao_=0;atlasTex_=0;textProgram_=0;
      for (auto& kv:textures_)
       {if (kv.second.fbo)
          glDeleteFramebuffers(1,&kv.second.fbo);
        glDeleteTextures(1,&kv.second.tex);
       }
      textures_.clear();
      if (triVbo_)
        glDeleteBuffers(1,&triVbo_);
      if (triVao_)
        glDeleteVertexArrays(1,&triVao_);
      if (texProgram_)
        glDeleteProgram(texProgram_);
      if (maskedProgram_)
        glDeleteProgram(maskedProgram_);
      if (triProgram_)
        glDeleteProgram(triProgram_);
      texProgram_=maskedProgram_=triProgram_=0;
      if (vbo_)
        glDeleteBuffers(1,&vbo_);
      if (vao_)
        glDeleteVertexArrays(1,&vao_);
      if (colorProgram_)
        glDeleteProgram(colorProgram_);
      vbo_=0;vao_=0;colorProgram_=0;mvpLocation_=-1;
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
     {// Task 44a: drains the GLFW mouse bridge (cursor position + Button1,
      // inverse-present-transformed to LOGICAL client coords at enqueue).
      // Keyboard events keep arriving through the task-35 key trampoline in
      // XAsteroids.C (glDrainEvents) — RunGame merges both drains. GLFW_REPEAT
      // never reaches this queue: only PRESS/RELEASE actions are enqueued
      // (D16 repeat drop; safe — the menu has no text fields).
      int count=0;
      while (mouseHead_!=mouseTail_&&count<maxCount)
       {out[count++]=mouseQueue_[mouseHead_];
        mouseHead_=(mouseHead_+1)%maxMouseQueue_;
       }
      return count;
     }

    // ------------------------------------------------------------------
    // Menu/input glue (task 44a, D9). The Dear ImGui PLATFORM half that the
    // engine owns: raw mouse capture -> typed GameEvents, plus clipboard
    // stubs for the ImGui context the menu adapter creates.
    // ------------------------------------------------------------------
    void installMenuInputBridge()
     {// Called once by ImGuiOptionsMenu after adapter init created the ImGui
      // context (the input callbacks themselves ride initWindow pass 2).
      // Clipboard stubs: no clipboard domain exists; explicit no-ops.
      ImGuiIO& io=ImGui::GetIO();
      io.BackendPlatformName="glBackend_XAsteroids_GameEvents";
      io.SetClipboardTextFn=clipboardSetStub;
      io.GetClipboardTextFn=clipboardGetStub;
     }

    // Task-36: canvas-domain translation (logical units, added inside
    // presentMVP). playingField brackets the passes whose coordinates are
    // X11-canvas-relative (world replay, help text, score table) with
    // (stage->playAreaXY - 80) so GL places them exactly where the X11
    // canvas blit does; window-domain chrome stays unbracketed.
    void setCanvasOffset(float dx,float dy)
     {canvasOffX_=dx;canvasOffY_=dy;}

    virtual void setScissorRect(const int* xywh) override
     {// D7/v6.1: LOGICAL CLIENT coords in; the backend applies the present
      // transform (scale+offset, y-flip) on the way to glScissor's window-
      // space rect. nullptr disables (the HUD pass, D8 step 6).
      if (!window)
        return;
      if (!xywh)
       {glDisable(GL_SCISSOR_TEST);
        return;
       }
      float scale=presentScale_;
      int ox=presentOffX_,oy=presentOffY_;
      int fx=ox+(int)(xywh[0]*scale);
      int fy=fbHeight_-(oy+(int)((xywh[1]+xywh[3])*scale));  // y-flip: top->bottom
      int fw=(int)(xywh[2]*scale);
      int fh=(int)(xywh[3]*scale);
      glScissor(fx,fy,fw,fh);
      glEnable(GL_SCISSOR_TEST);
     }

    virtual RenderTargetId createRenderTarget(int w,int h) override
     {// M1 render-target leg: off-screen FBO whose color attachment registers
      // as a normal TextureId (RenderTargetId = TextureId per the API).
      if (!window||w<=0||h<=0)
        return 0;
      GLuint t=0,fbo=0;
      glGenTextures(1,&t);
      glBindTexture(GL_TEXTURE_2D,t);
      glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,
                   nullptr);
      // Task-36 Defect B: pixel-exact blits (X11-copy parity) demand
      // nearest-neighbor sampling — LINEAR blended edge texels into 1px
      // rings around every rotated decor bitmap.
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
      glBindTexture(GL_TEXTURE_2D,0);
      glGenFramebuffers(1,&fbo);
      glBindFramebuffer(GL_FRAMEBUFFER,fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D,t,0);
      GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
      glBindFramebuffer(GL_FRAMEBUFFER,0);
      if (status!=GL_FRAMEBUFFER_COMPLETE)
       {glDeleteFramebuffers(1,&fbo);
        glDeleteTextures(1,&t);
        return 0;
       }
      TextureId id=nextTextureId_++;
      textures_[id]=TextureEntry{t,(GLuint)w,(GLuint)h,fbo};
      return id;
     }

    virtual void beginRenderTo(RenderTargetId target) override
     {auto it=textures_.find(target);
      if (!window||it==textures_.end()||!it->second.fbo)
        return;
      glBindFramebuffer(GL_FRAMEBUFFER,it->second.fbo);
      glViewport(0,0,(GLsizei)it->second.w,(GLsizei)it->second.h);
      rtActive_=true;
      rtWidth_=it->second.w;
      rtHeight_=it->second.h;
     }

    virtual void endRenderTo() override
     {if (!window)
        return;
      glBindFramebuffer(GL_FRAMEBUFFER,0);
      glViewport(0,0,(GLsizei)fbWidth_,(GLsizei)fbHeight_);
      rtActive_=false;
     }

    virtual void getPresentTransform(float& outScale,int& outOffsetX,
                                      int& outOffsetY) override
     {// Uniform-scale centered letterbox (D17.4b). D17.4(b) remediation: the
      // values live ONLY in presentScale_/presentOffX_/presentOffY_ — ONE
      // source of truth, recomputed by recomputePresentTransform_() from the
      // framebuffer-size callback and at init (vkBackend mirror). This getter
      // is a pure cache read: it must never diverge from what presentMVP_/
      // setScissorRect/enqueueMouseMove actually apply. Identity at the
      // canonical size; aspect never stretched; fractional scales fine.
      outScale=presentScale_;
      outOffsetX=presentOffX_;
      outOffsetY=presentOffY_;
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
     {// SC8: width 1 = GL_LINE_STRIP (core-profile glLineWidth caps at 1);
      // width>1 = filled quad expanded perpendicular in LOGICAL space so the
      // stroke scales with the letterbox like the X11 canvas blit does.
      // Task 35: verts must hold the THICK path's 6 verts x 5 floats — [20]
      // smashed the stack the moment the Button ctor's width-3 frames ran
      // (first exercise: the real-game main constructs the globals).
      if (!window)
        return;
      if (glTraceOn()) fprintf(stderr,"[TRACE] drawLine (%.1f,%.1f)-(%.1f,%.1f) w%.1f color %.2f,%.2f,%.2f\n",x1,y1,x2,y2,width,r,g,b);
      float verts[30];
      int count;
      GLenum mode;
      if (width<2.0f)
       {makeVert(verts,0,x1,y1,r,g,b);
        makeVert(verts,5,x2,y2,r,g,b);
        count=2;mode=GL_LINE_STRIP;
       }
      else
       {thickLineQuad(verts,x1,y1,x2,y2,width*0.5f,r,g,b);
        count=6;mode=GL_TRIANGLES;
       }
      drawColorPrims(mode,verts,count);
     }

    virtual void drawPolygon(const float* verts,int count,
                             float r,float g,float b,bool fill) override
     {if (!window||!verts||count<2)
        return;
      if (glTraceOn()) {fprintf(stderr,"[TRACE] drawPolygon n=%d fill=%d color %.2f,%.2f,%.2f:",count,(int)fill,r,g,b); for(int i=0;i<count&&i<12;++i) fprintf(stderr," (%.1f,%.1f)",verts[2*i],verts[2*i+1]); fprintf(stderr,"\n");}
      if (!fill)
       {float line[5*260];
        for (int i=0;i<count&&i<260;++i)
          makeVert(line,5*i,verts[2*i],verts[2*i+1],r,g,b);
        drawColorPrims(GL_LINE_LOOP,line,count);
        return;
       }
      // Ear-clipped triangles (simple polygons; falls back to a fan when the
      // clipper cannot close — degenerate inputs must not crash).
      float pairs[2*1536];                       // earClip output: [x,y]*n
      int ntri=earClip(verts,count,pairs);
      if (ntri<1)
       {float fan[5*256];
        int n=0;
        for (int i=1;i+1<count&&i<255;++i)
         {makeVert(fan,5*n++,verts[0],verts[1],r,g,b);
          makeVert(fan,5*n++,verts[2*i],verts[2*i+1],r,g,b);
          makeVert(fan,5*n++,verts[2*i+2],verts[2*i+3],r,g,b);
         }
        drawColorPrims(GL_TRIANGLES,fan,n);
        return;
       }
      float out[5*1536];
      for (int i=0;i<ntri*3;++i)
        makeVert(out,5*i,pairs[2*i],pairs[2*i+1],r,g,b);
      drawColorPrims(GL_TRIANGLES,out,ntri*3);
     }

    virtual void drawRect(float x,float y,float w,float h,
                          float r,float g,float b,bool fill) override
     {if (glTraceOn()) fprintf(stderr,"[TRACE] drawRect (%.1f,%.1f) %.1fx%.1f fill=%d\n",x,y,w,h,(int)fill);
      float quad[10]={x,y, x+w,y, x+w,y+h, x,y+h};
      if (fill)
       {static const int idx[6]={0,1,2,0,2,3};
        float tris[30];
        for (int i=0;i<6;++i)
          makeVert(tris,5*i,quad[2*idx[i]],quad[2*idx[i]+1],r,g,b);
        drawColorPrims(GL_TRIANGLES,tris,6);
       }
      else
        drawPolygon(quad,4,r,g,b,false);
     }

    virtual void drawTriangles(const float* verts,int count,
                               TextureId tex) override
     {// D9 menu pair (R10-N3 home): interleaved [x y u v r g b]*count,
      // WINDOW-space coords — the SOLE window-space method (v6.1 rule), so
      // the MVP here is IDENTITY. Consumed by the Dear ImGui adapter (44a).
      auto it=textures_.find(tex);
      if (!window||!verts||count<3||it==textures_.end())
        return;
      glUseProgram(triProgram_);
      glUniformMatrix4fv(triMvpLocation_,1,GL_FALSE,windowMVP());
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D,it->second.tex);
      glUniform1i(triTexLocation_,0);
      glBindVertexArray(triVao_);
      glBindBuffer(GL_ARRAY_BUFFER,triVbo_);
      glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(7*sizeof(float)*count),
                   verts,GL_STREAM_DRAW);
      glDrawArrays(GL_TRIANGLES,0,(GLint)count);
      glBindVertexArray(0);
      glBindTexture(GL_TEXTURE_2D,0);
      glUseProgram(0);
     }

    // ------------------------------------------------------------------
    // (5) Text
    // ------------------------------------------------------------------
    virtual void drawStringOpaque(const char* text,float x,float y,int fontId,
                                  float r,float g,float b,
                                  float bgR,float bgG,float bgB) override
     {// Opaque-text mapping (D12): cell background quad THEN glyphs.
      if (!window||!validFont(fontId)||!text||!*text)
        return;
      if (glTraceOn()) fprintf(stderr,"[TRACE] drawStringOpaque \"%s\" (%.1f,%.1f) f%d\n",text,x,y,fontId);
      float w=measureText(text,fontId);
      FontMetrics fm;
      getFontMetrics(fontId,fm);
      drawRect(x,y-fm.max_bounds.ascent,w,
               fm.max_bounds.ascent+fm.max_bounds.descent,bgR,bgG,bgB,true);
      drawGlyphs(text,x,y,fontId,r,g,b);
     }

    virtual void drawStringTransparent(const char* text,float x,float y,
                                       int fontId,
                                       float r,float g,float b) override
     {// Transparent-text mapping (D12): glyphs only, no background cell.
      if (!window||!validFont(fontId)||!text||!*text)
        return;
      if (glTraceOn()) fprintf(stderr,"[TRACE] drawStringTransparent \"%s\" (%.1f,%.1f) f%d\n",text,x,y,fontId);
      drawGlyphs(text,x,y,fontId,r,g,b);
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
     {// D13: the ONE texture entry point — callers feed xbmDecode output
      // (channels=4 RGBA8 content, or channels=1 unpacked coverage for masks).
      if (!window||!data||w<=0||h<=0||(channels!=1&&channels!=3&&channels!=4))
        return 0;
      GLuint t=0;
      glGenTextures(1,&t);
      glBindTexture(GL_TEXTURE_2D,t);
      glPixelStorei(GL_UNPACK_ALIGNMENT,1);
      GLenum fmt=channels==1?GL_R8:(channels==3?GL_RGB8:GL_RGBA8);
      glTexImage2D(GL_TEXTURE_2D,0,fmt,w,h,0,
                   channels==1?GL_RED:(channels==3?GL_RGB:GL_RGBA),
                   GL_UNSIGNED_BYTE,data);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
      glBindTexture(GL_TEXTURE_2D,0);
      TextureId id=nextTextureId_++;
      textures_[id]=TextureEntry{t,(GLuint)w,(GLuint)h};
      return id;
     }

    virtual void drawTexture(TextureId tex,float x,float y,float w,float h,
                             float alpha) override
     {// Textured quad, alpha-blended (D8 canonical order governs call order).
      auto it=textures_.find(tex);
      if (!window||it==textures_.end())
        return;
      if (glTraceOn()) fprintf(stderr,"[TRACE] drawTexture tex=%llu (%.1f,%.1f) %.1fx%.1f a%.2f\n",(unsigned long long)tex,x,y,w,h,alpha);
      float uv[4]={0.0f,0.0f,1.0f,1.0f};
      drawTexturedQuad(it->second.tex,x,y,w,h,uv,alpha);
     }

    virtual void drawTextureMasked(TextureId content,TextureId mask,
                                   float x,float y,float w,float h) override
     {// D6 GL leg: content quad discarded where the R8 mask is off (<0.5).
      auto c=textures_.find(content);
      auto m=textures_.find(mask);
      if (!window||c==textures_.end()||m==textures_.end())
        return;
      if (glTraceOn()) fprintf(stderr,"[TRACE] drawTextureMasked c=%llu m=%llu (%.1f,%.1f) %.1fx%.1f\n",(unsigned long long)content,(unsigned long long)mask,x,y,w,h);
      glUseProgram(maskedProgram_);
      glUniformMatrix4fv(maskedMvpLocation_,1,GL_FALSE,presentMVP());
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D,c->second.tex);
      glUniform1i(maskedContentLocation_,0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D,m->second.tex);
      glUniform1i(maskedMaskLocation_,1);
      float uv[4]={0,0,1,1};
      float verts[24];
      static const int idx[6]={0,1,2,0,2,3};
      const float fx[4]={x,x+w,x+w,x};
      const float fy[4]={y,y,y+h,y+h};
      const float fu[4]={uv[0],uv[2],uv[2],uv[0]};
      const float fv[4]={uv[1],uv[1],uv[3],uv[3]};
      for (int i=0;i<6;++i)
       {float* v=verts+4*i;
        v[0]=fx[idx[i]];v[1]=fy[idx[i]];v[2]=fu[idx[i]];v[3]=fv[idx[i]];
       }
      glBindVertexArray(textVao_);
      glBindBuffer(GL_ARRAY_BUFFER,textVbo_);
      glBufferData(GL_ARRAY_BUFFER,sizeof verts,verts,GL_STREAM_DRAW);
      glDrawArrays(GL_TRIANGLES,0,6);
      glBindVertexArray(0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D,0);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D,0);
      glUseProgram(0);
     }

    virtual void deleteTexture(TextureId tex) override
     {auto it=textures_.find(tex);
      if (it==textures_.end())
        return;
      if (it->second.fbo)
        glDeleteFramebuffers(1,&it->second.fbo);
      glDeleteTextures(1,&it->second.tex);
      textures_.erase(it);
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
     {// GL/VK-only semantics (m19): model translate+rotate applied BEFORE the
      // present transform. Rotation sign convention gets pinned against the
      // X11 rotator at task 35's Q1 gate; mechanism lands here (task 32).
      if (glTraceOn()) fprintf(stderr,"[TRACE] setTransform (%.1f,%.1f) ang=%.4f\n",tx,ty,angleRadians);
      tx_=tx;ty_=ty;angle_=angleRadians;
     }

    virtual void resetTransform() override
     {tx_=0.0f;ty_=0.0f;angle_=0.0f;
     }

    // ------------------------------------------------------------------
    // Internals
    // ------------------------------------------------------------------
    bool initPrimitives()
     {// Task 32: one color program (pos+rgba, MVP uniform). Compile errors
      // surface HERE at init via glGetShaderInfoLog -> false -> exit(1)
      // (plan review gate: no mid-game first-use failures).
      static const char* vsSrc=
        "#version 330 core\n"
        "layout(location=0) in vec2 aPos;\n"
        "layout(location=1) in vec3 aColor;\n"   // rgb only — stride must match
        "uniform mat4 uMVP;\n"
        "out vec3 vColor;\n"
        "void main(){ gl_Position = uMVP * vec4(aPos, 0.0, 1.0);\n"
        "             vColor = aColor; }\n";
      static const char* fsSrc=
        "#version 330 core\n"
        "in vec3 vColor;\n"
        "out vec4 FragColor;\n"
        "void main(){ FragColor = vec4(vColor, 1.0); }\n";
      GLuint vs=glCreateShader(GL_VERTEX_SHADER);
      glShaderSource(vs,1,&vsSrc,nullptr);
      glCompileShader(vs);
      if (!shaderOk(vs,"vertex"))
        return false;
      GLuint fs=glCreateShader(GL_FRAGMENT_SHADER);
      glShaderSource(fs,1,&fsSrc,nullptr);
      glCompileShader(fs);
      if (!shaderOk(fs,"fragment"))
       {glDeleteShader(vs);
        return false;
       }
      colorProgram_=glCreateProgram();
      glAttachShader(colorProgram_,vs);
      glAttachShader(colorProgram_,fs);
      glLinkProgram(colorProgram_);
      glDeleteShader(vs);
      glDeleteShader(fs);
      GLint linked=GL_FALSE;
      glGetProgramiv(colorProgram_,GL_LINK_STATUS,&linked);
      if (!linked)
       {char log[512];
        GLsizei n=0;
        glGetProgramInfoLog(colorProgram_,512,&n,log);
        fprintf(stderr,"glBackend: program link failed: %.*s\n",(int)n,log);
        return false;
       }
      mvpLocation_=glGetUniformLocation(colorProgram_,"uMVP");
      glGenVertexArrays(1,&vao_);
      glGenBuffers(1,&vbo_);
      glBindVertexArray(vao_);
      glBindBuffer(GL_ARRAY_BUFFER,vbo_);
      glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,5*sizeof(float),
                            (void*)(2*sizeof(float)));
      glEnableVertexAttribArray(1);
      glBindVertexArray(0);
      glEnable(GL_BLEND);          // D8 canonical-order alpha path (textures,
      glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);  // task 34, ride this)
      resetTransform();
      return true;
     }

    bool shaderOk(GLuint shader,const char* what)
      {GLint ok=GL_FALSE;
       glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
       if (!ok)
        {char log[512];
         GLsizei n=0;
         glGetShaderInfoLog(shader,512,&n,log);
         fprintf(stderr,"glBackend: %s shader compile failed: %.*s\n",
                 what,(int)n,log);
         return false;
        }
       return true;
      }

    // D17.4(b) present-transform store (vkBackend mirror): the ONE writer of
    // presentScale_/presentOffX_/presentOffY_. Uniform scale = min(fbW/canW,
    // fbH/canH) as float; offsets center the scaled content. Identity at the
    // canonical size (equal-int division is exactly 1.0f, offsets exactly 0),
    // which is what keeps canonical-size pixels byte-identical.
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

    static void makeVert(float* dst,int slot,float x,float y,
                         float r,float g,float b)
     {dst[slot]=x;dst[slot+1]=y;
      dst[slot+2]=r;dst[slot+3]=g;dst[slot+4]=b;
     }

    static void thickLineQuad(float* dst,float x1,float y1,float x2,float y2,
                              float halfW,float r,float g,float b)
     {// Perpendicular expansion in LOGICAL space (scales with the letterbox,
      // matching the X11 canvas-blit behavior). Butt caps — X11's
      // CapNotLast/JoinRound joins are not reproduced; line-visual parity is
      // judged under the task-36 mask regime.
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

    void drawColorPrims(GLenum mode,const float* interleaved,int vertCount)
     {if (!window||!interleaved||vertCount<1)
        return;
      glUseProgram(colorProgram_);
      glUniformMatrix4fv(mvpLocation_,1,GL_FALSE,presentMVP());
      glBindVertexArray(vao_);
      glBindBuffer(GL_ARRAY_BUFFER,vbo_);
      glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(5*sizeof(float)*vertCount),
                   interleaved,GL_STREAM_DRAW);
      glDrawArrays(mode,0,(GLint)vertCount);
      glBindVertexArray(0);
      glUseProgram(0);
     }

     const float* presentMVP()
      {// Affine compose: model (translate tx,ty + rotate angle) THEN present
       // (uniform scale s + offset ox,oy + y-flip to GL's bottom-up NDC).
       // Logical client space is y-DOWN like X11; ndc_y = 1 - 2*(y*s+oy)/fbH.
       if (rtActive_)
        {// Rendering INTO a render target: logical (0,0)-top-left maps 1:1
         // onto the target surface (Button face geometry is target-local).
         // FIX 2026-08-26 lives-bar inversion: GL NDC Y is bottom-up, but the
         // off-screen image's sampler expects top-left origin (0,0)=top like
         // Vulkan/X11 pixmaps. Using b=+2 and offset -1 (VK parity) stores the
         // content inverted in texel memory so the later drawTexture sampling
         // (v=0 top) retrieves the correct orientation. Matches vkBackend
         // rtActive branch (b=+2, d=-1) and fixes shipYard lives icons DOWN→UP.
         float a=2.0f/(float)rtWidth_;
         float b=2.0f/(float)rtHeight_;
         float cs=cosf(angle_),sn=sinf(angle_);
         float ma=cs,mb=-sn,mc=tx_;
         float md=sn,me=cs,mf=ty_;
         mvp_[0]=a*ma; mvp_[1]=b*md; mvp_[2]=0.0f; mvp_[3]=0.0f;
         mvp_[4]=a*mb; mvp_[5]=b*me; mvp_[6]=0.0f; mvp_[7]=0.0f;
         mvp_[8]=0.0f; mvp_[9]=0.0f; mvp_[10]=1.0f;mvp_[11]=0.0f;
         mvp_[12]=a*mc-1.0f;mvp_[13]=b*mf-1.0f;mvp_[14]=0.0f;mvp_[15]=1.0f;
         return mvp_;
        }
      float scale=presentScale_;
      int ox=presentOffX_,oy=presentOffY_;
      float a=2.0f*scale/(float)fbWidth_;    // P: x-scale
      float c=2.0f*(float)ox/(float)fbWidth_-1.0f;
      float b=-2.0f*scale/(float)fbHeight_;  // P: y-scale (flip)
      float d=1.0f-2.0f*(float)oy/(float)fbHeight_;
      float cs=cosf(angle_),sn=sinf(angle_);
      // M: logical = R*local + t (the object rotates about its OWN origin,
      // then translates to (tx,ty) — the D2 rotator placement semantic).
      // Task-36: canvas-domain draws carry the X11 canvas-origin shift
      // (playAreaXY - 80) so world/help/table content lands where the X11
      // canvas blit puts it; set by playingField via setCanvasOffset().
      float ma=cs,mb=-sn,mc=tx_+canvasOffX_;
      float md=sn,me=cs,mf=ty_+canvasOffY_;
      // L = P*M (apply M first)
      float la=a*ma,lb=a*mb,lc=a*mc+c;
      float ld=b*md,le=b*me,lf=b*mf+d;
      mvp_[0]=la; mvp_[1]=ld; mvp_[2]=0.0f; mvp_[3]=0.0f;   // column 0
      mvp_[4]=lb; mvp_[5]=le; mvp_[6]=0.0f; mvp_[7]=0.0f;   // column 1
      mvp_[8]=0.0f;mvp_[9]=0.0f;mvp_[10]=1.0f;mvp_[11]=0.0f;// column 2
      mvp_[12]=lc;mvp_[13]=lf;mvp_[14]=0.0f;mvp_[15]=1.0f;  // column 3
      return mvp_;
     }

    void drawTexturedQuad(GLuint tex,float x,float y,float w,float h,
                          const float* uv,float alpha)
     {glUseProgram(texProgram_);
      glUniformMatrix4fv(texMvpLocation_,1,GL_FALSE,presentMVP());
      glUniform1f(texAlphaLocation_,alpha);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D,tex);
      glUniform1i(texSamplerLocation_,0);
      float verts[24];
      static const int idx[6]={0,1,2,0,2,3};
      const float fx[4]={x,x+w,x+w,x};
      const float fy[4]={y,y,y+h,y+h};
      const float fu[4]={uv[0],uv[2],uv[2],uv[0]};
      const float fv[4]={uv[1],uv[1],uv[3],uv[3]};
      for (int i=0;i<6;++i)
       {float* v=verts+4*i;
        v[0]=fx[idx[i]];v[1]=fy[idx[i]];v[2]=fu[idx[i]];v[3]=fv[idx[i]];
       }
      glBindVertexArray(textVao_);
      glBindBuffer(GL_ARRAY_BUFFER,textVbo_);
      glBufferData(GL_ARRAY_BUFFER,sizeof verts,verts,GL_STREAM_DRAW);
      glDrawArrays(GL_TRIANGLES,0,6);
      glBindVertexArray(0);
      glBindTexture(GL_TEXTURE_2D,0);
      glUseProgram(0);
     }

    const float* windowMVP()
     {// Window-space pixel coords -> NDC (y-DOWN top-left origin, matching
      // the ImGui vertex convention the adapter feeds in). Recomputed per
      // call so resizes are honored.
      float a=2.0f/(float)fbWidth_;
      float b=-2.0f/(float)fbHeight_;
      winMvp_[0]=a; winMvp_[1]=0.0f; winMvp_[2]=0.0f; winMvp_[3]=0.0f;
      winMvp_[4]=0.0f; winMvp_[5]=b; winMvp_[6]=0.0f; winMvp_[7]=0.0f;
      winMvp_[8]=0.0f; winMvp_[9]=0.0f; winMvp_[10]=1.0f; winMvp_[11]=0.0f;
      winMvp_[12]=-1.0f; winMvp_[13]=1.0f; winMvp_[14]=0.0f; winMvp_[15]=1.0f;
      return winMvp_;
     }

    bool initTexturePrograms()
     {// Task 34: textured-quad + masked + menu-triangle programs. All share
      // compile-at-init error reporting (no mid-game first-use failures).
      static const char* tvs=
        "#version 330 core\n"
        "layout(location=0) in vec2 aPos;\n"
        "layout(location=1) in vec2 aUV;\n"
        "uniform mat4 uMVP;\n"
        "out vec2 vUV;\n"
        "void main(){ gl_Position = uMVP * vec4(aPos, 0.0, 1.0);\n"
        "             vUV = aUV; }\n";
      static const char* tfs=
        "#version 330 core\n"
        "in vec2 vUV;\n"
        "uniform sampler2D uTex;\n"
        "uniform float uAlpha;\n"
        "out vec4 FragColor;\n"
        "void main(){ vec4 c = texture(uTex, vUV);\n"
        "             FragColor = vec4(c.rgb, c.a * uAlpha); }\n";
      static const char* mfs=
        "#version 330 core\n"
        "in vec2 vUV;\n"
        "uniform sampler2D uContent;\n"
        "uniform sampler2D uMask;\n"
        "out vec4 FragColor;\n"
        "void main(){ if (texture(uMask, vUV).r < 0.5) discard;\n"
        "             FragColor = texture(uContent, vUV); }\n";
      static const char* xvs=
        "#version 330 core\n"
        "layout(location=0) in vec2 aPos;\n"
        "layout(location=1) in vec2 aUV;\n"
        "layout(location=2) in vec4 aColor;\n"
        "uniform mat4 uMVP;\n"
        "out vec2 vUV; out vec4 vColor;\n"
        "void main(){ gl_Position = uMVP * vec4(aPos, 0.0, 1.0);\n"
        "             vUV = aUV; vColor = aColor; }\n";
      static const char* xfs=
        "#version 330 core\n"
        "in vec2 vUV; in vec4 vColor;\n"
        "uniform sampler2D uTex;\n"
        "out vec4 FragColor;\n"
        "void main(){ FragColor = texture(uTex, vUV) * vColor; }\n";
      if (!buildProgram(texProgram_,texMvpLocation_,tvs,tfs,
                        {"uMVP","uTex","uAlpha"},
                        &texSamplerLocation_,&texAlphaLocation_))
        return false;
      if (!buildProgram(maskedProgram_,maskedMvpLocation_,tvs,mfs,
                        {"uMVP","uContent","uMask"},
                        &maskedContentLocation_,&maskedMaskLocation_))
        return false;
      if (!buildProgram(triProgram_,triMvpLocation_,xvs,xfs,
                        {"uMVP","uTex"},&triTexLocation_,nullptr))
        return false;
      // triVao_: pos(2)+uv(2)+color(4), stride 7 floats.
      glGenVertexArrays(1,&triVao_);
      glGenBuffers(1,&triVbo_);
      glBindVertexArray(triVao_);
      glBindBuffer(GL_ARRAY_BUFFER,triVbo_);
      glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,7*sizeof(float),(void*)0);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,7*sizeof(float),
                            (void*)(2*sizeof(float)));
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,7*sizeof(float),
                            (void*)(4*sizeof(float)));
      glEnableVertexAttribArray(2);
      glBindVertexArray(0);
      return true;
     }

    bool buildProgram(GLuint& prog,GLint& mvpLoc,const char* vsSrc,
                      const char* fsSrc,
                      std::initializer_list<const char*> uniforms,
                      GLint* u0=nullptr,GLint* u1=nullptr)
     {GLuint vs=glCreateShader(GL_VERTEX_SHADER);
      glShaderSource(vs,1,&vsSrc,nullptr);
      glCompileShader(vs);
      if (!shaderOk(vs,"vertex"))
       {glDeleteShader(vs);
        return false;
       }
      GLuint fs=glCreateShader(GL_FRAGMENT_SHADER);
      glShaderSource(fs,1,&fsSrc,nullptr);
      glCompileShader(fs);
      if (!shaderOk(fs,"fragment"))
       {glDeleteShader(vs);glDeleteShader(fs);
        return false;
       }
      prog=glCreateProgram();
      glAttachShader(prog,vs);
      glAttachShader(prog,fs);
      glLinkProgram(prog);
      glDeleteShader(vs);
      glDeleteShader(fs);
      GLint linked=GL_FALSE;
      glGetProgramiv(prog,GL_LINK_STATUS,&linked);
      if (!linked)
       {char log[512];
        GLsizei n=0;
        glGetProgramInfoLog(prog,512,&n,log);
        fprintf(stderr,"glBackend: program link failed: %.*s\n",(int)n,log);
        return false;
       }
      mvpLoc=glGetUniformLocation(prog,"uMVP");
      int slot=0;
      for (const char* u:uniforms)
       {GLint loc=glGetUniformLocation(prog,u);
        if (slot==1&&u0)
          *u0=loc;
        if (slot==2&&u1)
          *u1=loc;
        ++slot;
       }
      return true;
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
     {// Classic O(n^2) ear clipping for simple polygons (CCW or CW input —
      // orientation is normalized first). Emits triangle vertex [x,y] pairs.
      if (count<3)
        return 0;
      if (count>256)
        return 0;                        // game polygons are small; stay fixed-size
      float px[256],py[256];
      double area=0.0;
      for (int i=0;i<count;++i)
       {int j=(i+1)%count;
        area+=(double)verts[2*i]*verts[2*j+1]-(double)verts[2*j]*verts[2*i+1];
        px[i]=verts[2*i];py[i]=verts[2*i+1];
       }
      if (area<0)                        // normalize CCW
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
          if (cross<=0)                  // reflex or degenerate corner
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
          break;                         // numeric fallback -> caller fans
       }
      if (remaining==3)
       {outPairs[out++]=px[0];outPairs[out++]=py[0];
        outPairs[out++]=px[1];outPairs[out++]=py[1];
        outPairs[out++]=px[2];outPairs[out++]=py[2];
       }
      return out/6;                      // out counts floats: 6 per triangle
     }
    bool loadFonts()
     {// Pass 1 (D15): FOUR stb TTF slots (D12 list; NO errorInfo — m21).
      // Fonts 1-3 (title/button/hi-score) render from DejaVuSans-Bold.ttf;
      // font 4 (score display, courier role) from DejaVuSansMono-Bold.ttf.
      // Paths resolve against the BINARY's directory (/proc/self/exe), not
      // the process cwd — the QA harness chdirs the game into a scratch
      // work dir, which must not break font loading.
      static char fontDir[4096];
      static bool fontDirInit=false;
      if (!fontDirInit)
       {ssize_t n=readlink("/proc/self/exe",fontDir,sizeof fontDir-1);
        if (n>0)
         {fontDir[n]='\0';
          char* slash=strrchr(fontDir,'/');
          if (slash)
            *slash='\0';                  // strip binary name -> repo root
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
         {fprintf(stderr,"glBackend: cannot open %s (slot %s).\n",
                  path,slotNames[i]);
          return false;
         }
        fseek(f,0,SEEK_END);
        long len=ftell(f);
        fseek(f,0,SEEK_SET);
        uint8_t* buf=new uint8_t[len];
        if (fread(buf,1,(size_t)len,f)!=(size_t)len)
         {fclose(f);delete[] buf;
          fprintf(stderr,"glBackend: short read on %s.\n",path);
          return false;
         }
        fclose(f);
        int offset=stbtt_GetFontOffsetForIndex(buf,0);
        if (offset<0||
            !stbtt_InitFont(fontInfo_[i],buf,offset))
         {delete[] buf;
          fprintf(stderr,"glBackend: stb_init failed on %s.\n",path);
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
      // macOS hands out a 2.1 legacy context by default unless a Core Profile is
      // requested AND a valid display connection exists. Without Core Profile hints
      // we get the default 2.1; with them we'd ask for up to 4.1 (macOS max) but
      // only if the display server can surface a 3.2+ context, which requires a
      // live window server connection. Since this session has DISPLAY set to an
      // XQuartz socket but no confirmed live X server, we request the default and
      // let the GLVersion gate decide: 2.1 fails (correctly), any higher version
      // passes. Retarget: the backend actually uses only GL 2.0 + VAO (3.0) — no
      // 4.5 DSA entry points — so macOS's 4.1 ceiling is sufficient IF a display
      // is available. When no display is available at all, both paths fail at
      // glfwCreateWindow, which is the correct behavior.
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
      // Apple will not give us 4.5 on macOS (deprecated OpenGL, max 4.1 via Core
      // Profile or 2.1 legacy) — that is Apple's limitation, not ours. When the
      // gate fails here, telling the user to fall back to the Vulkan backend
      // (MoltenVK on macOS, real libvulkan on Linux) is the right answer: the
      // GLBackend code itself stays at 4.5 and does not bow to Apple's ceiling.
      const GLubyte* ver=glGetString(GL_VERSION);
      const GLubyte* ren=glGetString(GL_RENDERER);
      fprintf(stderr,"glBackend: GL_VERSION=%s GL_RENDERER=%s\n",
              ver?(const char*)ver:"?",ren?(const char*)ren:"?");
      if (GLVersion.major<4||(GLVersion.major==4&&GLVersion.minor<5))
       {fprintf(stderr,"glBackend: OpenGL 4.5 core required, got %d.%d.\n"
               "Apple's OpenGL stack will not serve 4.5 on macOS (max 4.1 Core / 2.1).\n"
               "To run on a Mac without bowing to Apple's GPU ceiling, rebuild with:\n"
               "  make BACKEND=VK   # uses MoltenVK -> real Vulkan via Metal, no XQuartz GL limit\n"
               "On Linux/Windows where a real 4.5+ OpenGL driver is present, this backend\n"
               "starts normally with no extra flags.\n",
               GLVersion.major,GLVersion.minor);
        shutdown();
        return false;
       }
      // Callbacks: framebuffer-size dirties the letterbox transform; close
      // REQUESTS shutdown via flag — never tears down in-callback (M13).
      glfwSetFramebufferSizeCallback(window,framebufferSizeTrampoline);
      glfwSetWindowCloseCallback(window,windowCloseTrampoline);
      // Task 44a: raw mouse capture feeds pollEvents' typed ring (installed
      // unconditionally — the title-screen Options button consumes clicks
      // before any menu exists).
      glfwSetCursorPosCallback(window,cursorPosTrampoline);
      glfwSetMouseButtonCallback(window,mouseButtonTrampoline);
      glfwSetCursorEnterCallback(window,cursorEnterTrampoline);
      int fbw=0,fbh=0;
      glfwGetFramebufferSize(window,&fbw,&fbh);
      fbWidth_=fbw;fbHeight_=fbh;
      glViewport(0,0,fbw,fbh);
      recomputePresentTransform_();
      if (!initPrimitives())
       {shutdown();
        return false;
       }
      if (!initText())
       {shutdown();
        return false;
       }
      if (!initTexturePrograms())
       {shutdown();
        return false;
       }
      if (getenv("XAST_FRAME_LOG"))
        for (int i=0;i<4;++i)
         {FontMetrics fm;
          getFontMetrics(i,fm);
          fprintf(stderr,"[metrics] slot=%d asc=%.1f desc=%.1f maxw=%.1f\n",
                  i,fm.max_bounds.ascent,fm.max_bounds.descent,
                  fm.max_bounds.width);
         }
      return true;
     }

    static void framebufferSizeTrampoline(GLFWwindow* win,int w,int h)
      {(void)win;
       // Single-instance backend (main() constructs exactly one engine).
       // Route through the file-static instance pointer set at creation.
       // D17.4(b) remediation: the callback now STORES the letterbox transform
       // (vkBackend recomputePresentTransform_ mirror) — a lazy dirty-flag
       // refresh let only the first post-resize draw consume fresh values and
       // silently reverted every later draw to stale identity (the measured
       // 1:1 top-left presentation). No in-callback GL calls beyond viewport.
       if (s_instance)
        {s_instance->fbWidth_=w;
         s_instance->fbHeight_=h;
         s_instance->recomputePresentTransform_();
         glViewport(0,0,w,h);
        }
      }

    static void windowCloseTrampoline(GLFWwindow* win)
     {(void)win;
      if (s_instance)
        s_instance->closeRequested_=true;   // M13: flag only, no teardown
     }

    // ---- task 44a mouse bridge trampolines --------------------------------
    // Raw cursor coords are WINDOW space; GameEvent x/y are LOGICAL client
    // coords — enqueue applies the INVERSE present transform so the domain
    // and the menu adapter both speak logical (the adapter forward-maps
    // back for ImGui). Window-vs-framebuffer scale is 1:1 on this platform
    // family (X11/Xvfb); documented assumption.
    static void cursorPosTrampoline(GLFWwindow*,double x,double y)
     {if (s_instance)
        s_instance->enqueueMouseMove(x,y);
     }

    static void mouseButtonTrampoline(GLFWwindow*,int button,int action,int)
     {if (s_instance&&button==GLFW_MOUSE_BUTTON_LEFT)
        s_instance->enqueueMouseButton(action==GLFW_PRESS);
     }

    static void cursorEnterTrampoline(GLFWwindow*,int entered)
     {if (s_instance)
        s_instance->enqueueCursorEnter(entered==GLFW_TRUE);
     }

    static void clipboardSetStub(void*,const char*)
     {}

    static const char* clipboardGetStub(void*)
     {return "";
     }

    void enqueueMouseMove(double x,double y)
     {float scale=1.0f;
      int offsetX=0,offsetY=0;
      getPresentTransform(scale,offsetX,offsetY);
      lastCursorX_=x;lastCursorY_=y;cursorValid_=true;
      enqueueEvent(GameEvent::MouseMove,
                   (int)((x-offsetX)/scale),(int)((y-offsetY)/scale));
     }

    void enqueueMouseButton(bool pressed)
     {int x=0,y=0;
      if (cursorValid_)
       {float scale=1.0f;
        int offsetX=0,offsetY=0;
        getPresentTransform(scale,offsetX,offsetY);
        x=(int)((lastCursorX_-offsetX)/scale);
        y=(int)((lastCursorY_-offsetY)/scale);
       }
      enqueueEvent(pressed?GameEvent::MouseDown:GameEvent::MouseUp,x,y,1);
     }

    void enqueueCursorEnter(bool entered)
     {enqueueEvent(entered?GameEvent::CursorEnter:GameEvent::CursorLeave,
                   cursorValid_?(int)lastCursorX_:0,
                   cursorValid_?(int)lastCursorY_:0);
     }

    void enqueueEvent(GameEvent::Type type,int x,int y,int key=0)
     {const int next=(mouseTail_+1)%maxMouseQueue_;
      if (next==mouseHead_)
        return;                              // overflow drops (never blocks)
      GameEvent& event=mouseQueue_[mouseTail_];
      event.type=type;
      event.key=key;
      event.character=0;
      event.x=x;
      event.y=y;
      mouseTail_=next;
     }

    bool validFont(int fontId) const
     {return fontId>=0&&fontId<=3&&fontInfo_[fontId]!=nullptr
             &&fontData_[fontId]!=nullptr;
     }

    // ------------------------------------------------------------------
    // Text (task 33): shelf-packed coverage atlas + per-glyph quads.
    // ------------------------------------------------------------------
    struct GlyphInfo
     {float u0,v0,u1,v1;      // atlas UVs (v flipped at draw time)
      float w,h;              // bitmap size in logical px
      float advance;          // x advance (logical px)
      float bearingX,bearingY;// offset from pen to bitmap top-left
      bool present;
     };

    bool initText()
     {// Rasterize ASCII 32..126 for all four slots into ONE atlas texture.
      // Shelf packing: one shelf per slot, glyphs left-to-right, wrap within
      // the slot's band. Coverage is single-channel; the text shader reads .r.
      const int AW=2048,AH=512;
      uint8_t* atlas=new uint8_t[AW*AH]();
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
              memcpy(atlas+(shelfY[s]+row)*AW+curX,bmp+row*w,(size_t)w);
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
      glGenTextures(1,&atlasTex_);
      glBindTexture(GL_TEXTURE_2D,atlasTex_);
      glPixelStorei(GL_UNPACK_ALIGNMENT,1);
      glTexImage2D(GL_TEXTURE_2D,0,GL_R8,AW,AH,0,GL_RED,GL_UNSIGNED_BYTE,atlas);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
      glBindTexture(GL_TEXTURE_2D,0);
      delete[] atlas;
      // Text program: pos(2)+uv(2) stride 4, tint uniform, samples atlas .r.
      static const char* tvs=
        "#version 330 core\n"
        "layout(location=0) in vec2 aPos;\n"
        "layout(location=1) in vec2 aUV;\n"
        "uniform mat4 uMVP;\n"
        "out vec2 vUV;\n"
        "void main(){ gl_Position = uMVP * vec4(aPos, 0.0, 1.0);\n"
        "             vUV = aUV; }\n";
      static const char* tfs=
        "#version 330 core\n"
        "in vec2 vUV;\n"
        "uniform vec3 uColor;\n"
        "uniform sampler2D uAtlas;\n"
        "out vec4 FragColor;\n"
        "void main(){ float a = texture(uAtlas, vUV).r;\n"
        "             FragColor = vec4(uColor * a, a); }\n";
      GLuint vs=glCreateShader(GL_VERTEX_SHADER);
      glShaderSource(vs,1,&tvs,nullptr);
      glCompileShader(vs);
      if (!shaderOk(vs,"text vertex"))
        return false;
      GLuint fs=glCreateShader(GL_FRAGMENT_SHADER);
      glShaderSource(fs,1,&tfs,nullptr);
      glCompileShader(fs);
      if (!shaderOk(fs,"text fragment"))
       {glDeleteShader(vs);
        return false;
       }
      textProgram_=glCreateProgram();
      glAttachShader(textProgram_,vs);
      glAttachShader(textProgram_,fs);
      glLinkProgram(textProgram_);
      glDeleteShader(vs);
      glDeleteShader(fs);
      GLint linked=GL_FALSE;
      glGetProgramiv(textProgram_,GL_LINK_STATUS,&linked);
      if (!linked)
       {char log[512];
        GLsizei n=0;
        glGetProgramInfoLog(textProgram_,512,&n,log);
        fprintf(stderr,"glBackend: text program link failed: %.*s\n",(int)n,log);
        return false;
       }
      textMvpLocation_=glGetUniformLocation(textProgram_,"uMVP");
      textColorLocation_=glGetUniformLocation(textProgram_,"uColor");
      textAtlasLocation_=glGetUniformLocation(textProgram_,"uAtlas");
      glGenVertexArrays(1,&textVao_);
      glGenBuffers(1,&textVbo_);
      glBindVertexArray(textVao_);
      glBindBuffer(GL_ARRAY_BUFFER,textVbo_);
      glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),
                            (void*)(2*sizeof(float)));
      glEnableVertexAttribArray(1);
      glBindVertexArray(0);
      return true;
     }

    void drawGlyphs(const char* text,float x,float y,int fontId,
                    float r,float g,float b)
     {      // Pen starts at (x, y) with y = BASELINE (X11 text semantics).
      // The present transform rides the MVP exclusively (one source of
      // truth: presentScale_/presentOffX_/presentOffY_).
      float penX=x;
      // 6 expanded triangle verts x 4 floats per quad. Task-36 Defect A fix:
      // this loop previously advanced 16 floats/quad while writing 24 — each
      // quad stomped the next one's head and every glyph's second triangle
      // stretched to the following glyph's origin (diagonal streaks across
      // every text run).
      float verts[6*4*512];            // up to 512 quads per call
      int quads=0;
      for (const char* p=text;*p&&quads<512;++p)
       {GlyphInfo& g=glyphs_[fontId][(unsigned char)*p];
        if (g.present)
         {float x0=penX+g.bearingX,y0=y+g.bearingY;   // bearingY is <=0 (above baseline)
          float u[4]={g.u0,g.v0,g.u1,g.v1};
          // quad corners: (x0,y0)-(x0+w,y0+h); UV v grows downward in atlas
          const float fx[4]={x0,x0+g.w,x0+g.w,x0};
          const float fy[4]={y0,y0,y0+g.h,y0+g.h};
          const float fu[4]={u[0],u[2],u[2],u[0]};
          const float fv[4]={u[1],u[1],u[3],u[3]};
          static const int idx[6]={0,1,2,0,2,3};
          for (int i=0;i<6;++i)
           {float* v=verts+24*quads+4*i;
            v[0]=fx[idx[i]];v[1]=fy[idx[i]];
            v[2]=fu[idx[i]];v[3]=fv[idx[i]];
           }
          ++quads;
         }
        penX+=g.advance;
       }
      if (!quads)
        return;
      glUseProgram(textProgram_);
      glUniformMatrix4fv(textMvpLocation_,1,GL_FALSE,presentMVP());
      glUniform3f(textColorLocation_,r,g,b);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D,atlasTex_);
      glUniform1i(textAtlasLocation_,0);
      glBindVertexArray(textVao_);
      glBindBuffer(GL_ARRAY_BUFFER,textVbo_);
      glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(24*sizeof(float)*quads),
                   verts,GL_STREAM_DRAW);
      glDrawArrays(GL_TRIANGLES,0,6*quads);
      glBindVertexArray(0);
      glBindTexture(GL_TEXTURE_2D,0);
      glUseProgram(0);
     }

    // ------------------------------------------------------------------
    // Members
    // ------------------------------------------------------------------
    GLFWwindow* window;
    int canonicalWidth_,canonicalHeight_;    // D15-formula client size (fixed)
    int fbWidth_,fbHeight_;                  // last-known framebuffer size
    int canonicalPlayAreaWidth_,canonicalPlayAreaHeight_;
    int canonicalMaxIconHeight_;
    float presentScale_;                     // present transform (letterbox,
    int presentOffX_,presentOffY_;           // D17.4b; ONE writer:
                                             // recomputePresentTransform_)
    float canvasOffX_=0.0f,canvasOffY_=0.0f;   // task-36 canvas-domain shift
    bool closeRequested_;
    bool shutdownDone_;
    bool glfwInitialized_;

    stbtt_fontinfo* fontInfo_[4];            // slots 0..3 = EngineFont 0..3
    uint8_t* fontData_[4];                   // owned TTF buffers
    long fontDataLen_[4];
    float fontScale_[4];
    float maxAsciiAdvance_[4];

    GLuint colorProgram_;                    // task-32 primitive program
    GLint mvpLocation_;
    GLuint vao_,vbo_;
    float tx_,ty_,angle_;                    // model transform (m19)
    float mvp_[16];
    float winMvp_[16];

    GLuint atlasTex_;                        // task-33 text atlas
    GLuint textProgram_;
    GLint textMvpLocation_,textColorLocation_,textAtlasLocation_;
    GLuint textVao_,textVbo_;
    GlyphInfo glyphs_[4][128];
    int slotShelfH_[4];

    struct TextureEntry                      // task-34 texture registry
     {GLuint tex;
      GLuint w,h;
      GLuint fbo;                            // 0 = plain texture (M1 targets)
     };
    std::map<TextureId,TextureEntry> textures_;
    TextureId nextTextureId_=1;
    GLuint texProgram_,maskedProgram_,triProgram_;
    GLint texMvpLocation_,texSamplerLocation_,texAlphaLocation_;
    GLint maskedMvpLocation_,maskedContentLocation_,maskedMaskLocation_;
    GLint triMvpLocation_,triTexLocation_;
    GLuint triVao_,triVbo_;
    bool rtActive_=false;
    int rtWidth_=0,rtHeight_=0;

    // ---- task 44a: GLFW mouse bridge (typed GameEvent ring) ---------------
    static const int maxMouseQueue_=256;
    GameEvent mouseQueue_[maxMouseQueue_];
    int mouseHead_=0,mouseTail_=0;
    double lastCursorX_=0.0,lastCursorY_=0.0;
    bool cursorValid_=false;

    static GLBackend* s_instance;            // callback routing (single instance)
 };

// Header-inline static definition — glBackend.H is included by exactly ONE TU
// per link (XAsteroids.o, the repo-wide header-inline convention), so this is
// ODR-safe under the current build structure.
GLBackend* GLBackend::s_instance=nullptr;

#endif
