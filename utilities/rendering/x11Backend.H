#ifndef _X11BACKEND_H
#define _X11BACKEND_H

// utilities/rendering/x11Backend.H — X11 pass-through RenderingEngine (plan
// task 12, D14). Header-inline by repo convention (F1 exception-zone pattern
// covers x11Backend.(H|C) should a later split need it).
//
// ============================================================================
// D15 RESOURCE-OWNERSHIP MAP (the M4/M16/R4-M4 artifact — exactly ONE owner
// per resource; live table — extension rows are appended by tasks 15/16/17/18)
// ============================================================================
// | Resource                          | Owner      | Note                     |
// |-----------------------------------|------------|--------------------------|
// | Display                           | X11Backend | opened in initWindow     |
// |                                   |            | pass 1; XCloseDisplay in |
// |                                   |            | shutdown                 |
// | Window                            | X11Backend | created in initWindow    |
// |                                   |            | pass 2; XDestroyWindow   |
// |                                   |            | in shutdown              |
// | titleGC/scoreGC/hiScoreGC/        | X11Backend | per-font GCs (verbatim   |
// | defaultGC                         |            | stage.H:195-211 attrs);  |
// |                                   |            | freed in shutdown        |
// | backgroundGC (GXcopy, black fg)   | X11Backend | performs ALL clears      |
// |                                   |            | (playingField.H:77/:202/ |
// |                                   |            | :312/:638 semantics);    |
// |                                   |            | NEVER the gxor GC        |
// |                                   |            | (R4-M1)                  |
// | gxor GC                           | X11Backend | compositing only         |
// |                                   |            | (playingField.H:41/:133/ |
// |                                   |            | :135 attrs); created     |
// |                                   |            | here at task 12 ahead of |
// |                                   |            | task 15's canvas move    |
// | Icon pixmap                       | X11Backend | XCreateBitmapFromData    |
// |                                   |            | (stage.H:188); freed in  |
// |                                   |            | shutdown                 |
// | All 5 fonts, INCL. errorInfo      | X11Backend | XLoadQueryFont x5 at     |
// |                                   |            | initWindow pass 1;       |
// |                                   |            | XFreeFont x5 in shutdown |
// |                                   |            | — the missing 5th free   |
// |                                   |            | (errorInfo) closes the   |
// |                                   |            | m18/U14 leak (F4 named   |
// |                                   |            | assertion)               |
// | primitiveGC_/maskedGC_            | X11Backend | backend-internal helper  |
// |                                   |            | GCs (engine primitives / |
// |                                   |            | clip-mask draws); not    |
// |                                   |            | census sites             |
// | Keyboard auto-repeat saved state  | X11Backend | XGetKeyboardControl in   |
// | + SIGINT/SIGQUIT/SIGABRT/SIGTERM  |            | pass 2; restored in      |
// | signal handler (stage.H:208-221/  |            | shutdown BEFORE frees;   |
// | :296-305)                         |            | handler references THIS  |
// |                                   |            | backend's Display (M7/N6)|
// | Textures / render targets         | caller     | caller lifetime via      |
// | (createTexture*/createRender-     |            | deleteTexture (the flat  |
// | Target)                           |            | X11 model preserved)     |
// | GL context, FBOs, VBOs, textures  | GLBackend/ | backend-owned; freed in  |
// | window close on GL/VK             | VKBackend/ | shutdown; glfw close cb  |
// |                                   | glfw cb    | routes to same sequence  |
// | Extension rows: play-field canvas | (task 15)  | button faces (16),       |
// | pixmap                            |            | shipyard icons (17),     |
// |                                   |            | explosion frames (18)    |
// // ===========================================================================
//
// Task-12 staging notes (what deliberately does NOT move yet):
//  - The blit pair stage.H:258/:269 stays called by Stage until task 14; this
//    class's endFrame() is therefore XSync-bound bookkeeping ONLY (no blit,
//    no double-blit).
//  - Event HANDLING stays in RunGame (playingField.H island) until task 25's
//    verbatim relocation; pollEvents() below is a typed NON-BLOCKING drain
//    that nothing calls on X11 until then — no event dropped or double-
//    handled (the domain loop remains the pre-25 authority).
//  - The WM-hints block (XAllocSizeHints/XSetWMProperties/XSetWMProtocols/
//    XSelectInput/XMapRaised, playingField.H:553-582) relocates at TASK 15
//    with PSizeHints min/max UNSET (v6/S2 — no size pin anywhere). Until then
//    PlayTheGame keeps applying its pin exactly as before, which is what Q13
//    byte-identity requires. This file adds NO pin of its own.
//  - ConfigureNotify-driven present-transform caching arrives with the event
//    island (task 25): draining events from here NOW would steal them from
//    the domain loop. getPresentTransform() therefore recomputes on demand
//    from XGetGeometry (identity at the canonical size).
//  - The harness's quiescence handshake counts visible frame completions
//    (test/harness/script-format.md); it does NOT consume an X-property frame
//    counter, so endFrame() publishes none (zero risk accepted: skipped).

#include<stdlib.h>
#include<stdio.h>
#include<string.h>
#include<math.h>
#include<signal.h>
#include<map>

#ifdef X11_BACKEND
#include<X11/Xlib.h>
#include<X11/Xutil.h>
#endif

#include"renderingEngine.H"
#include"windowSize.H"
#include"../../bitmaps/starDestroyerIcon.xbm"

class X11Backend : public RenderingEngine
 {public:
    X11Backend()
      : display(nullptr),window(None),screen(0),
        iconPixmap(None),
        titleGC_(None),hiScoreGC_(None),scoreGC_(None),defaultGC_(None),
        backgroundGC_(None),gxorGC_(None),primitiveGC_(None),maskedGC_(None),
        currentTarget_(None),
        autoRepeatState_(AutoRepeatModeOff),
        windowWidth_(0),windowHeight_(0),
        canonicalPlayAreaWidth_(0),canonicalPlayAreaHeight_(0),
        canonicalMaxIconHeight_(0),
        scissorActive_(false),
        scissorX_(0),scissorY_(0),scissorW_(0),scissorH_(0)
     {fonts_[0]=fonts_[1]=fonts_[2]=fonts_[3]=fonts_[4]=nullptr;
     }

    virtual ~X11Backend()
     {shutdown();
     }

    // Transition configuration (deleted when DI ctors land at task 13): the
    // play-area dimensions and max icon height come from game constants
    // (PlayingField::playArea / ShipGroup::maxIconHeight) that this header
    // cannot include. main() calls this BEFORE initWindow.
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
     {(void)title;   // WM naming stays in PlayTheGame's XSetWMProperties site
                     // until task 15 relocates it; nothing maps the window
                     // here (XMapRaised stays in PlayTheGame until 15 too).
      if (!openDisplayAndFonts())                    // ---- pass 1 (D15)
        return false;
      return createWindowIconGCsAndHandlers();       // ---- pass 2 (D15)
     }

    virtual void shutdown() override
     {if (!display)
        return;                                      // exactly-once guard
      if (autoRepeatState_==AutoRepeatModeOn)        // ~Stage:227-228 verbatim
        XAutoRepeatOn(display);
      disarmTerminateHandler();                      // UAF fix (see below)
      // D15 release order: GCs -> pixmaps -> fonts -> window -> display
      if (titleGC_)      XFreeGC(display,titleGC_);
      if (scoreGC_)      XFreeGC(display,scoreGC_);
      if (hiScoreGC_)    XFreeGC(display,hiScoreGC_);
      if (defaultGC_)    XFreeGC(display,defaultGC_);
      if (backgroundGC_) XFreeGC(display,backgroundGC_);
      if (gxorGC_)       XFreeGC(display,gxorGC_);
      if (primitiveGC_)  XFreeGC(display,primitiveGC_);
      if (maskedGC_)     XFreeGC(display,maskedGC_);
      if (iconPixmap)    XFreePixmap(display,iconPixmap);
      // ALL FIVE fonts incl. errorInfo — F4 named assertion (m18/U14 closed).
      for (int i=0;i<5;++i)
       {if (fonts_[i])
          XFreeFont(display,fonts_[i]);
        fonts_[i]=nullptr;
       }
      if (window)
        XDestroyWindow(display,window);
      if (display)
        XCloseDisplay(display);
      display=nullptr;window=None;iconPixmap=None;
      titleGC_=hiScoreGC_=scoreGC_=defaultGC_=None;
      backgroundGC_=gxorGC_=primitiveGC_=maskedGC_=None;
      currentTarget_=None;
      textureDims_.clear();
      pixelCache_.clear();
     }

    virtual X11NativeHandle nativeHandle() const override
     {X11NativeHandle h;
      h.display=(void*)display;
      h.window=(unsigned long)window;
      return h;
     }

    // ------------------------------------------------------------------
    // (2) Frame lifecycle
    // ------------------------------------------------------------------
    virtual void beginFrame() override
     {// X11: noop (D8 — no scissor ownership here either).
     }

    virtual void endFrame() override
     {// XSync-bound bookkeeping ONLY at task 12: the canvas->window blit pair
      // (stage.H:258/:269) is still called by Stage until task 14 — moving or
      // duplicating it here would double-blit. XSync mirrors the frame-bound
      // sync the domain loops perform (:296/:329); harmless while uncalled.
      if (display)
        XSync(display,False);
     }

    // ------------------------------------------------------------------
    // (3) Seams
    // ------------------------------------------------------------------
    virtual int pollEvents(GameEvent* out,int maxCount) override
     {int n=0;
      if (!display||!out||maxCount<=0)
        return 0;
      while (n<maxCount&&XEventsQueued(display,QueuedAfterReading)>0)
       {XEvent e;
        XNextEvent(display,&e);
        GameEvent g={};
        bool emit=true;
        switch(e.type)
         {case KeyPress:
          case KeyRelease:
           {char keyText[2];
            g.type=(e.type==KeyPress)?GameEvent::KeyDown:GameEvent::KeyUp;
            g.key=(int)XLookupKeysym(&e.xkey,0);
            g.character=(XLookupString((XKeyEvent*)&e,keyText,2,NULL,NULL)==1)
                          ? keyText[0] : (char)0;
            break;}
          case ButtonPress:
          case ButtonRelease:
            g.type=(e.type==ButtonPress)?GameEvent::MouseDown:GameEvent::MouseUp;
            g.key=(int)e.xbutton.button;
            inverseTransformPoint(e.xbutton.x,e.xbutton.y,g.x,g.y);
            break;
          case MotionNotify:
            g.type=GameEvent::MouseMove;
            inverseTransformPoint(e.xmotion.x,e.xmotion.y,g.x,g.y);
            break;
          case EnterNotify:
            g.type=GameEvent::CursorEnter;
            break;
          case LeaveNotify:
            g.type=GameEvent::CursorLeave;
            break;
          default:
            // MappingNotify/Expose/ConfigureNotify/ClientMessage are not
            // representable as GameEvents; their VERBATIM handling relocates
            // at task 25. Until then RunGame drains the queue itself and
            // nothing calls this method on X11 — no event is dropped or
            // double-handled (stated dependency, plan task 12(a)).
            emit=false;
            break;
         }
        if (emit)
          out[n++]=g;
       }
      return n;
     }

    virtual void setScissorRect(const int* xywh) override
     {if (!xywh)
       {scissorActive_=false;
        return;
       }
      scissorX_=xywh[0];scissorY_=xywh[1];
      scissorW_=xywh[2];scissorH_=xywh[3];
      scissorActive_=scissorW_>0&&scissorH_>0;
     }

    virtual RenderTargetId createRenderTarget(int w,int h) override
     {if (!display||w<=0||h<=0)
        return 0;
      Pixmap p=XCreatePixmap(display,window,(unsigned int)w,(unsigned int)h,
                             (unsigned int)DefaultDepth(display,screen));
      if (!p)
        return 0;
      textureDims_[(TextureId)p]=std::pair<int,int>(w,h);
      return (TextureId)p;
     }

    virtual void beginRenderTo(RenderTargetId target) override
     {if (target&&display)
        currentTarget_=(Drawable)target;
     }

    virtual void endRenderTo() override
     {if (display)
        currentTarget_=window;
     }

    virtual void getPresentTransform(float& outScale,int& outOffsetX,
                                     int& outOffsetY) override
     {// Uniform-scale centered letterbox (D17.4b), recomputed on demand from
      // XGetGeometry — identity at the canonical size. ConfigureNotify-driven
      // caching defers to task 25 (see file header).
      float scale=1.0f;
      int ox=0,oy=0;
      if (display&&window&&windowWidth_>0&&windowHeight_>0)
       {unsigned int w=0,h=0,bb=0,depth=0;
        Window root;
        int wx,wy;
        if (XGetGeometry(display,window,&root,&wx,&wy,&w,&h,&bb,&depth))
         {scale=((float)w/windowWidth_<(float)h/windowHeight_)
                  ? (float)w/windowWidth_ : (float)h/windowHeight_;
          ox=((int)w-(int)(windowWidth_*scale))/2;
          oy=((int)h-(int)(windowHeight_*scale))/2;
         }
       }
      outScale=scale;outOffsetX=ox;outOffsetY=oy;
     }

    // ------------------------------------------------------------------
    // (4) Primitives
    // ------------------------------------------------------------------
    virtual void clear() override
     {// SPECIFIED: XFillRectangle via backgroundGC_ (GXcopy — the GC default
      // function, never changed — with black foreground; playingField.H:77/
      // :149-151 semantics). The gxor GC is compositing-only (R4-M1).
      if (!display)
        return;
      unsigned int w=0,h=0,bb=0,depth=0;
      Window root;int wx,wy;
      if (!XGetGeometry(display,currentTarget_,&root,&wx,&wy,&w,&h,&bb,&depth))
        return;
      XSetForeground(display,backgroundGC_,BlackPixel(display,screen));
      XFillRectangle(display,currentTarget_,backgroundGC_,0,0,w,h);
     }

    virtual void drawLine(float x1,float y1,float x2,float y2,
                          float r,float g,float b,float width) override
     {if (!display)
        return;
      XSetForeground(display,primitiveGC_,pixelFor(r,g,b));
      XSetLineAttributes(display,primitiveGC_,
                         width>=2.0f?3:1,LineSolid,CapNotLast,JoinRound);
      XDrawLine(display,currentTarget_,primitiveGC_,
                (int)x1,(int)y1,(int)x2,(int)y2);
     }

    virtual void drawPolygon(const float* verts,int count,
                             float r,float g,float b,bool fill) override
     {if (!display||!verts||count<2)
        return;
      XPoint pts[256];
      int n=count<256?count:256;
      for (int i=0;i<n;++i)
       {pts[i].x=(short)(int)verts[2*i];
        pts[i].y=(short)(int)verts[2*i+1];
       }
      XSetForeground(display,primitiveGC_,pixelFor(r,g,b));
      if (fill)
        XFillPolygon(display,currentTarget_,primitiveGC_,pts,n,Complex,
                     CoordModeOrigin);
      else
        XDrawLines(display,currentTarget_,primitiveGC_,pts,n,CoordModeOrigin);
     }

    virtual void drawRect(float x,float y,float w,float h,
                          float r,float g,float b,bool fill) override
     {if (!display)
        return;
      XSetForeground(display,primitiveGC_,pixelFor(r,g,b));
      if (fill)
        XFillRectangle(display,currentTarget_,primitiveGC_,
                       (int)x,(int)y,(unsigned int)w,(unsigned int)h);
      else
        XDrawRectangle(display,currentTarget_,primitiveGC_,
                       (int)x,(int)y,(unsigned int)w,(unsigned int)h);
     }

    // Menu-adapter batch path (D9): CPU software rasterizer — texture-
    // sampled, per-vertex color modulate, scissor-clipped, WINDOW-space.
    // See the blending note inside: X11 core pixmaps carry no alpha, so the
    // upload drops it and sampling treats texels as opaque (Phase-2 adapter
    // concern; no consumer exists at task 12).
    virtual void drawTriangles(const float* verts,int count,
                               TextureId tex) override;

    // ------------------------------------------------------------------
    // (5) Text
    // ------------------------------------------------------------------
    virtual void drawStringOpaque(const char* text,float x,float y,int fontId,
                                  float r,float g,float b,
                                  float bgR,float bgG,float bgB) override
     {if (!display||!validFont(fontId)||!text)
        return;
      GC gc=fontGC((EngineFontId)fontId);
      XSetForeground(display,gc,pixelFor(r,g,b));
      XSetBackground(display,gc,pixelFor(bgR,bgG,bgB));
      XDrawImageString(display,currentTarget_,gc,(int)x,(int)y,
                       text,(int)strlen(text));
     }

    virtual void drawStringTransparent(const char* text,float x,float y,
                                       int fontId,
                                       float r,float g,float b) override
     {if (!display||!validFont(fontId)||!text)
        return;
      GC gc=fontGC((EngineFontId)fontId);
      XSetForeground(display,gc,pixelFor(r,g,b));
      XDrawString(display,currentTarget_,gc,(int)x,(int)y,
                  text,(int)strlen(text));
     }

    virtual float measureText(const char* text,int fontId) override
     {if (!validFont(fontId)||!text)
        return 0.0f;
      return (float)XTextWidth(fonts_[fontId],text,(int)strlen(text));
     }

    virtual void getFontMetrics(int fontId,FontMetrics& out) override
     {if (!validFont(fontId))
       {out.ascent=out.descent=out.lbearing=0.0f;
        out.max_bounds.ascent=out.max_bounds.descent=out.max_bounds.width=0.0f;
        return;
       }
      XFontStruct* f=fonts_[fontId];
      out.ascent=(float)f->ascent;
      out.descent=(float)f->descent;
      out.lbearing=(float)f->max_bounds.lbearing;
      out.max_bounds.ascent=(float)f->max_bounds.ascent;
      out.max_bounds.descent=(float)f->max_bounds.descent;
      out.max_bounds.width=(float)f->max_bounds.width;
     }

    // ------------------------------------------------------------------
    // (6) Textures — TextureId IS Pixmap on X11 (D14)
    // ------------------------------------------------------------------
    virtual TextureId createTextureFromBitmap(const uint8_t* data,int w,int h,
                                              int channels) override
     {if (!display||!data||w<=0||h<=0)
        return 0;
      if (channels==4)
        return createTextureFromRGBA32(data,w,h);
      if (channels!=1)
        return 0;   // 2/3-channel textures unsupported on X11 (documented)
      // 1 = white, 0 = black — matches the xbmDecode set/clear contract.
      Pixmap p=XCreatePixmapFromBitmapData(display,window,
                                           (char*)const_cast<uint8_t*>(data),
                                           w,h,
                                           WhitePixel(display,screen),
                                           BlackPixel(display,screen),
                                           DefaultDepth(display,screen));
      if (!p)
        return 0;
      textureDims_[(TextureId)p]=std::pair<int,int>(w,h);
      return (TextureId)p;
     }

    virtual void drawTexture(TextureId tex,float x,float y,float w,float h,
                             float alpha) override
     {// XCopyArea pass-through; alpha ignored (core X11 has no blended copy —
      // D14 pass-through preserves current behavior exactly).
      (void)alpha;
      if (!display||!tex)
        return;
      XCopyArea(display,(Drawable)tex,currentTarget_,defaultGC_,
                0,0,(unsigned int)w,(unsigned int)h,(int)x,(int)y);
     }

    virtual void drawTextureMasked(TextureId content,TextureId mask,
                                   float x,float y,float w,float h) override
     {// Native clip-mask GC semantics (explosionGraphic.H:103-117 /
      // rockGroup.H:184-197): XSetClipMask + XSetClipOrigin + XCopyArea.
      if (!display||!content||!mask)
        return;
      XSetClipMask(display,maskedGC_,(Pixmap)mask);
      XSetClipOrigin(display,maskedGC_,(int)x,(int)y);
      XCopyArea(display,(Drawable)content,currentTarget_,maskedGC_,
                0,0,(unsigned int)w,(unsigned int)h,(int)x,(int)y);
     }

    virtual void deleteTexture(TextureId tex) override
     {if (!display||!tex)
        return;
      XFreePixmap(display,(Pixmap)tex);
      textureDims_.erase(tex);
     }

    // Menu-adapter upload (D9): RGBA8888 -> depth-matched ZPixmap -> pixmap.
    // Alpha is dropped (X11 core pixmaps have no alpha channel) — documented
    // D14/D9 boundary; no MIT-SHM required.
    virtual TextureId createTextureFromRGBA32(const uint8_t* pixels,int w,
                                              int h) override
     {if (!display||!pixels||w<=0||h<=0)
        return 0;
      XImage* img=XCreateImage(display,DefaultVisual(display,screen),
                               DefaultDepth(display,screen),ZPixmap,0,
                               nullptr,(unsigned int)w,(unsigned int)h,32,0);
      if (!img)
        return 0;
      img->data=(char*)malloc((size_t)img->bytes_per_line*(size_t)h);
      if (!img->data)
       {XDestroyImage(img);
        return 0;
       }
      for (int y=0;y<h;++y)
        for (int x=0;x<w;++x)
         {const uint8_t* px=pixels+4*((size_t)y*w+(size_t)x);
          img->f.put_pixel(img,x,y,pixelFrom8(px[0],px[1],px[2]));
         }
      Pixmap p=XCreatePixmap(display,window,(unsigned int)w,(unsigned int)h,
                             (unsigned int)DefaultDepth(display,screen));
      if (!p)
       {XDestroyImage(img);
        return 0;
       }
      XPutImage(display,p,primitiveGC_,img,0,0,0,0,(unsigned int)w,
                (unsigned int)h);
      XDestroyImage(img);
      textureDims_[(TextureId)p]=std::pair<int,int>(w,h);
      return (TextureId)p;
     }

    // ------------------------------------------------------------------
    // (7) Transform — documented no-ops on X11 (m19): there is no X11
    // transform stack; rotation is the D14 pre-computed-pixmap pipeline, so
    // the no-op is correct-by-construction, not a stub.
    // ------------------------------------------------------------------
    virtual void setTransform(float tx,float ty,float angleRadians) override
     {(void)tx;(void)ty;(void)angleRadians;
     }

    virtual void resetTransform() override
     {}

    // ------------------------------------------------------------------
    // Transition accessors (main() populates Stage's non-owning members from
    // the concrete backend until task 13's DI ctors / task 47 deletion).
    // ------------------------------------------------------------------
    inline Pixmap icon() const {return iconPixmap;}
    inline XFontStruct* titleFont() const {return fonts_[EngineFont_Title];}
    inline XFontStruct* buttonFont() const {return fonts_[EngineFont_Button];}
    inline XFontStruct* hiScoreFont() const {return fonts_[EngineFont_HiScore];}
    inline XFontStruct* scoreFont() const {return fonts_[EngineFont_Score];}
    inline XFontStruct* errorFont() const {return fonts_[EngineFont_Error];}
    inline GC titleGC() const {return titleGC_;}
    inline GC hiScoreGC() const {return hiScoreGC_;}
    inline GC scoreGC() const {return scoreGC_;}
    inline GC defaultGC() const {return defaultGC_;}
    inline int autoRepeatState() const {return autoRepeatState_;}
    inline int windowWidth() const {return windowWidth_;}
    inline int windowHeight() const {return windowHeight_;}

  private:
    // ---- pass 1 (D15): display + ALL FIVE fonts -------------------------
    bool openDisplayAndFonts()
     {display=XOpenDisplay(NULL);
      if (!display)
       {fprintf(stderr,"Could not open display.\n");
        return false;
       }
      static const char* const names[5]=
       {"white_shadow-48",
        "-schumacher-clean-bold-r-normal--10-100-75-75-c-60-iso8859-1",
        "-ibm-ergonomic-bold-r-normal--20-140-100-100-c-120-iso8859-9",
        "-urw-courier-bold-r-normal--40-300-100-100-m-240-iso8859-9",
        "-adobe-helvetica-bold-r-normal--14-100-100-100-p-82-iso8859-1"};
      for (int i=0;i<5;++i)
        fonts_[i]=XLoadQueryFont(display,names[i]);
      if (!fonts_[0]||!fonts_[1]||!fonts_[2]||!fonts_[3]||!fonts_[4])
       {fprintf(stderr,"Could not find font white_shadow-48, "
                        "-schumacher-clean-bold-r-normal--10-100-75-75-c-60-iso8859-1, "
                        "-ibm-ergonomic-bold-r-normal--20-140-100-100-c-120-iso8859-9, "
                        "-urw-courier-bold-r-normal--40-300-100-100-m-240-iso8859-9, "
                        "and/or -adobe-helvetica-bold-r-normal--14-100-100-100-p-82-iso8859-1.  "
                        "Please run XAstSetup and retry XAsteroids.\n");
        for (int i=0;i<5;++i)
         {if (fonts_[i])
            XFreeFont(display,fonts_[i]);
          fonts_[i]=nullptr;
         }
        XCloseDisplay(display);
        display=nullptr;
        return false;
       }
      return true;
     }

    // ---- pass 2 (D15): shared formula -> window -> icon -> GCs ----------
    bool createWindowIconGCsAndHandlers()
     {screen=DefaultScreen(display);
      FontMetrics titleFm,hiScoreFm,scoreFm;
      getFontMetrics(EngineFont_Title,titleFm);
      getFontMetrics(EngineFont_HiScore,hiScoreFm);
      getFontMetrics(EngineFont_Score,scoreFm);
      int titleTextWidth=XTextWidth(fonts_[EngineFont_Title],"Asteroids",
                                    (int)strlen("Asteroids"));
      WindowSizeFormula::Compute(titleFm,hiScoreFm,scoreFm,titleTextWidth,
                                 canonicalPlayAreaWidth_,
                                 canonicalPlayAreaHeight_,
                                 canonicalMaxIconHeight_,
                                 windowWidth_,windowHeight_);
      // Background pixel: Stage::windowBg gray (stage.H:111-115 initializer
      // duplicated here because the backend cannot include stage.H), with the
      // original WhitePixel fallback (stage.H:183-185).
      XColor windowBg={0,46260,46260,46260,DoRed|DoGreen|DoBlue};
      int background=XAllocColor(display,DefaultColormap(display,screen),
                                  &windowBg)
                       ? windowBg.pixel
                       : WhitePixel(display,screen);
      // Root-center placement mirrors stage.H:182-191 verbatim (5px border,
      // white border pixel); NO PSizeHints pin is added here (v6/S2/D15).
      window=XCreateSimpleWindow(display,DefaultRootWindow(display),
                                 (DisplayWidth(display,screen)-windowWidth_)/2,
                                 (DisplayHeight(display,screen)-windowHeight_)/2,
                                 windowWidth_,windowHeight_,
                                 5,WhitePixel(display,screen),
                                 background);
      if (!window)
       {fprintf(stderr,"Could not create the game window.\n");
        shutdownFontsAndDisplay();
        return false;
       }
      currentTarget_=window;
      iconPixmap=XCreateBitmapFromData(display,window,
                                       (const char* const)starDestroyerIcon_bits,
                                       starDestroyerIcon_width,
                                       starDestroyerIcon_height);
      // Per-font GCs — attributes verbatim from stage.H:195-211.
      titleGC_=makeFontGC(fonts_[EngineFont_Title],background);
      hiScoreGC_=makeFontGC(fonts_[EngineFont_HiScore],background);
      scoreGC_=makeFontGC(fonts_[EngineFont_Score],background);
      defaultGC_=makeFontGC(nullptr,0);
      // backgroundGC: GXcopy (GC default), black fg — ALL clears
      // (playingField.H:77/:149-151 semantics; created here at task 12 ahead
      // of task 15's canvas/GC ownership move).
      backgroundGC_=XCreateGC(display,window,NULL,0);
      XSetGraphicsExposures(display,backgroundGC_,False);
      XSetForeground(display,backgroundGC_,BlackPixel(display,screen));
      // gxor GC: compositing-only — attrs verbatim playingField.H:143-148.
      gxorGC_=XCreateGC(display,window,NULL,0);
      XSetGraphicsExposures(display,gxorGC_,False);
      XSetFunction(display,gxorGC_,GXor);
      XSetFont(display,gxorGC_,fonts_[EngineFont_HiScore]->fid);
      XSetForeground(display,gxorGC_,WhitePixel(display,screen));
      XSetBackground(display,gxorGC_,BlackPixel(display,screen));
      // Backend-internal helper GCs (not census sites).
      primitiveGC_=XCreateGC(display,window,NULL,0);
      XSetGraphicsExposures(display,primitiveGC_,False);
      XSetClipMask(display,primitiveGC_,None);
      maskedGC_=XCreateGC(display,window,NULL,0);
      XSetGraphicsExposures(display,maskedGC_,False);
      XSetClipMask(display,maskedGC_,None);
      // Saved keyboard auto-repeat state (stage.H:212-214 position: BEFORE
      // PlayTheGame's XAutoRepeatOff) + the terminate-path signal handler
      // (stage.H:215-221 verbatim registration; M7/N6 — the handler now
      // references THIS backend's Display, never Stage).
      XKeyboardState KBState;
      XGetKeyboardControl(display,&KBState);
      autoRepeatState_=KBState.global_auto_repeat;
      struct sigaction action;
      action.sa_handler=&X11Backend::terminateHandler;
      *(unsigned int*)&action.sa_mask=0;
      action.sa_flags=0;
      sigaction(SIGINT,&action,NULL);
      sigaction(SIGQUIT,&action,NULL);
      sigaction(SIGABRT,&action,NULL);
      sigaction(SIGTERM,&action,NULL);
      s_activeBackend=this;
      return true;
     }

    inline GC makeFontGC(XFontStruct* font,int background)
     {GC gc=XCreateGC(display,window,NULL,0);
      XSetGraphicsExposures(display,gc,False);
      if (font)
       {XSetFont(display,gc,font->fid);
        XSetForeground(display,gc,BlackPixel(display,screen));
        XSetBackground(display,gc,background);
       }
      return gc;
     }

    inline GC fontGC(EngineFontId id) const
     {switch(id)
       {case EngineFont_Title:   return titleGC_;
        case EngineFont_HiScore: return hiScoreGC_;
        case EngineFont_Score:   return scoreGC_;
        default:                 return defaultGC_;
       }
     }

    inline bool validFont(int fontId) const
     {return display&&fontId>=0&&fontId<5&&fonts_[fontId];}

    inline void shutdownFontsAndDisplay()
     {for (int i=0;i<5;++i)
       {if (fonts_[i])
          XFreeFont(display,fonts_[i]);
        fonts_[i]=nullptr;
       }
      XCloseDisplay(display);
      display=nullptr;
     }

    inline void inverseTransformPoint(int winX,int winY,int& outX,int& outY)
      const
     {float scale;int ox,oy;
      // const-cast-free route: recompute inline (getPresentTransform is
      // non-virtual-called only through the interface; keep logic in one
      // place by mirroring its math here).
      scale=1.0f;ox=0;oy=0;
      if (windowWidth_>0&&windowHeight_>0)
       {unsigned int w=0,h=0,bb=0,depth=0;
        Window root;int wx,wy;
        if (XGetGeometry(display,window,&root,&wx,&wy,&w,&h,&bb,&depth))
         {scale=((float)w/windowWidth_<(float)h/windowHeight_)
                  ? (float)w/windowWidth_ : (float)h/windowHeight_;
          ox=((int)w-(int)(windowWidth_*scale))/2;
          oy=((int)h-(int)(windowHeight_*scale))/2;
         }
       }
      outX=(int)(((float)winX-(float)ox)/(scale==0.0f?1.0f:scale));
      outY=(int)(((float)winY-(float)oy)/(scale==0.0f?1.0f:scale));
     }

    inline unsigned long pixelFor(float r,float g,float b)
     {if (r<0.0f)r=0.0f; if (r>1.0f)r=1.0f;
      if (g<0.0f)g=0.0f; if (g>1.0f)g=1.0f;
      if (b<0.0f)b=0.0f; if (b>1.0f)b=1.0f;
      unsigned long key=((unsigned long)(unsigned short)(r*65535.0f+0.5f)<<32)
                       |((unsigned long)(unsigned short)(g*65535.0f+0.5f)<<16)
                       |(unsigned long)(unsigned short)(b*65535.0f+0.5f);
      std::map<unsigned long,unsigned long>::const_iterator it=
        pixelCache_.find(key);
      if (it!=pixelCache_.end())
        return it->second;
      XColor c;
      c.flags=DoRed|DoGreen|DoBlue;
      c.red=(unsigned short)(r*65535.0f+0.5f);
      c.green=(unsigned short)(g*65535.0f+0.5f);
      c.blue=(unsigned short)(b*65535.0f+0.5f);
      unsigned long pixel=XAllocColor(display,DefaultColormap(display,screen),&c)
                            ? c.pixel
                            : WhitePixel(display,screen);
      pixelCache_[key]=pixel;
      return pixel;
     }

    inline unsigned long pixelFrom8(uint8_t r,uint8_t g,uint8_t b) const
     {Visual* v=DefaultVisual(display,screen);
      return (((unsigned long)r*65535UL/255UL)&v->red_mask)
            |(((unsigned long)g*65535UL/255UL)&v->green_mask)
            |(((unsigned long)b*65535UL/255UL)&v->blue_mask);
     }

    static void terminateHandler(int i)
     {X11Backend* b=s_activeBackend;
      if (b&&b->autoRepeatState_==AutoRepeatModeOn)
        XAutoRepeatOn(b->display);
      struct sigaction action;
      action.sa_handler=SIG_DFL;
      *(unsigned int*)&action.sa_mask=0;
      action.sa_flags=0;
      sigaction(i,&action,NULL);
      raise(i);
     }

    void disarmTerminateHandler()
     {s_activeBackend=nullptr;
      struct sigaction action;
      action.sa_handler=SIG_DFL;
      *(unsigned int*)&action.sa_mask=0;
      action.sa_flags=0;
      sigaction(SIGINT,&action,NULL);
      sigaction(SIGQUIT,&action,NULL);
      sigaction(SIGABRT,&action,NULL);
      sigaction(SIGTERM,&action,NULL);
     }

    // CPU triangle rasterizer internals (drawTriangles body below the class).
    inline bool currentTargetBounds(int& w,int& h)
     {if (!display)
        return false;
      unsigned int bw=0,bh=0,bb=0,depth=0;
      Window root;int wx,wy;
      if (!XGetGeometry(display,currentTarget_,&root,&wx,&wy,&bw,&bh,&bb,
                        &depth))
        return false;
      w=(int)bw;h=(int)bh;
      return w>0&&h>0;
     }

    static inline int maskBits(unsigned long m)
     {int n=0;
      while (m)
       {n+=(int)(m&1UL);
        m>>=1;
       }
      return n;
     }

    static inline int maskShift(unsigned long m)
     {int s=0;
      if (!m)
        return 0;
      while (!(m&1UL))
       {m>>=1;
        ++s;
       }
      return s;
     }

    Display* display;
    Window window;
    int screen;
    Pixmap iconPixmap;
    XFontStruct* fonts_[5];
    GC titleGC_,hiScoreGC_,scoreGC_,defaultGC_;
    GC backgroundGC_,gxorGC_,primitiveGC_,maskedGC_;
    Drawable currentTarget_;
    int autoRepeatState_;
    int windowWidth_,windowHeight_;
    int canonicalPlayAreaWidth_,canonicalPlayAreaHeight_,
        canonicalMaxIconHeight_;
    bool scissorActive_;
    int scissorX_,scissorY_,scissorW_,scissorH_;
    std::map<TextureId,std::pair<int,int> > textureDims_;
    std::map<unsigned long,unsigned long> pixelCache_;

    inline static X11Backend* s_activeBackend=nullptr;  // C++17 inline static
 };  // class X11Backend

// ===================== drawTriangles — CPU rasterizer =====================
inline void X11Backend::drawTriangles(const float* verts,int count,
                                      TextureId tex)
 {if (!display||!verts||count<3||(count%3))
    return;
  int tw,th;
  if (!currentTargetBounds(tw,th))
    return;
  // Clip box: target bounds ∩ scissor (logical client coords transformed to
  // window space through the present transform — v6.1 coordinate rule).
  int cx0=0,cy0=0,cx1=tw,cy1=th;
  if (scissorActive_)
   {float scale;int ox,oy;
    getPresentTransform(scale,ox,oy);
    int sx0=(int)((float)scissorX_*scale)+(float)ox,
        sy0=(int)((float)scissorY_*scale)+(float)oy,
        sx1=(int)((float)(scissorX_+scissorW_)*scale)+(float)ox,
        sy1=(int)((float)(scissorY_+scissorH_)*scale)+(float)oy;
    if (sx0>cx0)cx0=sx0;
    if (sy0>cy0)cy0=sy0;
    if (sx1<cx1)cx1=sx1;
    if (sy1<cy1)cy1=sy1;
   }
  // Batch bounding box ∩ clip box.
  float minx=verts[0],miny=verts[1],maxx=minx,maxy=miny;
  for (int i=1;i<count;++i)
   {float x=verts[7*i],y=verts[7*i+1];
    if (x<minx)minx=x;
    if (x>maxx)maxx=x;
    if (y<miny)miny=y;
    if (y>maxy)maxy=y;
   }
  int bx0=(int)floorf(minx),by0=(int)floorf(miny),
      bx1=(int)ceilf(maxx),by1=(int)ceilf(maxy);
  if (bx0<cx0)bx0=cx0;
  if (by0<cy0)by0=cy0;
  if (bx1>cx1)bx1=cx1;
  if (by1>cy1)by1=cy1;
  if (bx0>=bx1||by0>=by1)
    return;
  int W=bx1-bx0,H=by1-by0;
  // Read-modify-write the covered rectangle so pixels outside the triangle
  // union survive untouched.
  XImage* dst=XGetImage(display,currentTarget_,bx0,by0,
                        (unsigned int)W,(unsigned int)H,AllPlanes,ZPixmap);
  if (!dst)
    return;
  // Optional texture sampling source.
  XImage* src=nullptr;
  int sw=0,sh=0;
  std::map<TextureId,std::pair<int,int> >::const_iterator dit=
    textureDims_.find(tex);
  if (dit!=textureDims_.end())
   {sw=dit->second.first;
    sh=dit->second.second;
    if (sw>0&&sh>0)
      src=XGetImage(display,(Drawable)tex,0,0,(unsigned int)sw,
                    (unsigned int)sh,AllPlanes,ZPixmap);
   }
  Visual* vis=DefaultVisual(display,screen);
  int rShift=maskShift(vis->red_mask),gShift=maskShift(vis->green_mask),
      bShift=maskShift(vis->blue_mask);
  float rScale=maskBits(vis->red_mask)?1.0f/(float)((1<<maskBits(vis->red_mask))-1):0.0f,
        gScale=maskBits(vis->green_mask)?1.0f/(float)((1<<maskBits(vis->green_mask))-1):0.0f,
        bScale=maskBits(vis->blue_mask)?1.0f/(float)((1<<maskBits(vis->blue_mask))-1):0.0f;
  for (int t=0;t<count;t+=3)
   {const float* A=verts+7*t;
    const float* B=A+7;
    const float* C=B+7;
    double area=((double)B[0]-A[0])*((double)C[1]-A[1])
               -((double)C[0]-A[0])*((double)B[1]-A[1]);
    if (area>-1e-12&&area<1e-12)
      continue;
    double invArea=1.0/area;
    int px0=(int)floorf(A[0]<B[0]?(A[0]<C[0]?A[0]:C[0]):(B[0]<C[0]?B[0]:C[0]));
    int py0=(int)floorf(A[1]<B[1]?(A[1]<C[1]?A[1]:C[1]):(B[1]<C[1]?B[1]:C[1]));
    int px1=(int)ceilf(A[0]>B[0]?(A[0]>C[0]?A[0]:C[0]):(B[0]>C[0]?B[0]:C[0]));
    int py1=(int)ceilf(A[1]>B[1]?(A[1]>C[1]?A[1]:C[1]):(B[1]>C[1]?B[1]:C[1]));
    if (px0<bx0)px0=bx0;
    if (py0<by0)py0=by0;
    if (px1>bx1)px1=bx1;
    if (py1>by1)py1=by1;
    for (int py=py0;py<py1;++py)
      for (int px=px0;px<px1;++px)
       {double sx=px+0.5,sy=py+0.5;
        double l0=(((double)C[0]-B[0])*(sy-B[1])-((double)C[1]-B[1])*(sx-B[0]))*invArea,
               l1=(((double)A[0]-C[0])*(sy-C[1])-((double)A[1]-C[1])*(sx-C[0]))*invArea,
               l2=1.0-l0-l1;
        if (l0<0.0||l1<0.0||l2<0.0)
          continue;
        float u=(float)(l0*A[2]+l1*B[2]+l2*C[2]),
              v=(float)(l0*A[3]+l1*B[3]+l2*C[3]),
              cr=(float)(l0*A[4]+l1*B[4]+l2*C[4]),
              cg=(float)(l0*A[5]+l1*B[5]+l2*C[5]),
              cb=(float)(l0*A[6]+l1*B[6]+l2*C[6]);
        if (cr<0.0f)cr=0.0f; if (cr>1.0f)cr=1.0f;
        if (cg<0.0f)cg=0.0f; if (cg>1.0f)cg=1.0f;
        if (cb<0.0f)cb=0.0f; if (cb>1.0f)cb=1.0f;
        unsigned long pixel;
        if (src)
         {if (u<0.0f)u=0.0f; if (u>0.9999f)u=0.9999f;
          if (v<0.0f)v=0.0f; if (v>0.9999f)v=0.9999f;
          int tx=(int)(u*(float)sw),ty=(int)(v*(float)sh);
          if (tx>=sw)tx=sw-1;
          if (ty>=sh)ty=sh-1;
          unsigned long tp=src->f.get_pixel(src,tx,ty);
          float tr=((tp>>rShift)&(vis->red_mask>>rShift))*rScale,
                tg=((tp>>gShift)&(vis->green_mask>>gShift))*gScale,
                tb=((tp>>bShift)&(vis->blue_mask>>bShift))*bScale;
          cr*=tr;cg*=tg;cb*=tb;   // modulate: texel x vertex color
         }
        pixel=(((unsigned long)(cr/rScale+0.5f))<<rShift)
             |(((unsigned long)(cg/gScale+0.5f))<<gShift)
             |(((unsigned long)(cb/bScale+0.5f))<<bShift);
        dst->f.put_pixel(dst,px-bx0,py-by0,pixel);
       }
   }
  XPutImage(display,currentTarget_,primitiveGC_,dst,0,0,bx0,by0,
            (unsigned int)W,(unsigned int)H);
  if (src)
    XDestroyImage(src);
  XDestroyImage(dst);
 }

#endif
