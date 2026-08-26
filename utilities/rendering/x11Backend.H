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
// | Play-field canvas pixmap          | X11Backend | created in initWindow    |
// |                                   |            | pass 2 (task 15), parent |
// |                                   |            | window; freed in         |
// |                                   |            | shutdown; NOT registered |
// |                                   |            | in textureDims_          |
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
// | Extension rows: button faces (16), shipyard icons (17), explosion frames |
// | (18)                                                                     |
// // ===========================================================================
//
// Task-12 staging notes (what deliberately does NOT move yet):
//  - The Stage window-blit pair was absorbed into endFrame() at task 14:
//    each call replays the yard-region copy and the play-area copy, each
//    when its region was requested. The play-area leg is request-gated
//    because the standalone yard-refresh call points run while the canvas
//    holds its post-present cleared state — publishing it there would
//    flash the play area black (visible divergence). endFrame adds NO
//    sync of its own: the domain loops flush at their frame boundaries,
//    and an extra round trip inside the draw sequence deterministically
//    perturbs the sampled visible-frame sequence (Q13 red).
//  - Event HANDLING relocated at task 25 (D16): pollEvents() below absorbed
//    the playingField.H island verbatim — same order, same blocking
//    semantics (the pointer-leave pause spin and the Button1 click session
//    block INSIDE this call, exactly as they blocked inside the old drain),
//    same server-side auto-repeat suppression. The domain loops now drain
//    typed events once per frame and apply the key→action table themselves.
//    Damage (expose-class) and window-close have no GameEvent representation
//    in the frozen interface; they latch into takeRefreshRequest()/
//    takeCloseRequest(). The once-per-frame request flush that bounds the
//    frame-clock window is frameClockSync(), called by RunGame at the exact
//    statement position of the old raw sync (an internal sync in endFrame is
//    Q13-proven harmful — see that method).
//  - The WM-hints block (naming/protocol-registration/input-selection/map-
//    and-raise) RELOCATED here at task 15 (D-C C1), verbatim-order after the
//    KBState save, with the size-pin hint structure dropped entirely
//    (v6/S2/D15 — no pin anywhere). Two recorded deltas vs the old
//    PlayTheGame site: argv/argc pass as NULL,0 (frozen initWindow signature;
//    command-name property unset, pixel-inert) and the map-and-raise fires at
//    init instead of first PlayTheGame entry.
//  - ConfigureNotify-driven present-transform caching arrived with the event
//    island (task 25). D17.4(b) remediation: the cached transform now
//    describes the ACTIVE present path (XAST_XRENDER=auto|on|off selected at
//    init; xrender = uniform-scale centered composite, fallback = centered
//    1:1 with black bars / symmetric crop), endFrame presents both published
//    regions through it (identity fast path = the exact legacy XCopyArea),
//    and window-target draws map through the same values.
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
#include<X11/extensions/Xrender.h>
#include<dlfcn.h>
#endif

#include"renderingEngine.H"
#include"windowSize.H"
#include"../../bitmaps/starDestroyerIcon.xbm"

#ifdef X11_BACKEND

#ifdef X11_BACKEND
// D17.4(b): the RENDER extension is bound at RUN TIME (dlopen/dlsym) — no new
// link-time dependency, and a missing library degrades cleanly to the core-
// X11 fallback path. Signatures mirror Xrender.h exactly.
typedef Bool (*xastXRenderQueryExtensionFn)(Display*,int*,int*);
typedef XRenderPictFormat* (*xastXRenderFindVisualFormatFn)(Display*,
                                                            const Visual*);
typedef Picture (*xastXRenderCreatePictureFn)(Display*,Drawable,
                                              _Xconst XRenderPictFormat*,
                                              unsigned long,
                                              _Xconst XRenderPictureAttributes*);
typedef void (*xastXRenderFreePictureFn)(Display*,Picture);
typedef Bool (*xastXRenderSetPictureTransformFn)(Display*,Picture,
                                                 XTransform*);
typedef Bool (*xastXRenderSetPictureFilterFn)(Display*,Picture,const char*,
                                              XFixed*,int);
typedef void (*xastXRenderCompositeFn)(Display*,int,Picture,Picture,Picture,
                                       int,int,int,int,int,int,
                                       unsigned int,unsigned int);
#endif

class X11Backend : public RenderingEngine
 {public:
    X11Backend()
      : display(nullptr),window(None),screen(0),
        iconPixmap(None),canvasPixmap_(None),
        titleGC_(None),hiScoreGC_(None),scoreGC_(None),defaultGC_(None),
        buttonGC_(None),
        backgroundGC_(None),gxorGC_(None),primitiveGC_(None),maskedGC_(None),
        currentTarget_(None),
        wmProtocolsAtom_(None),wmDeleteWindowAtom_(None),
        autoRepeatState_(AutoRepeatModeOff),
        windowWidth_(0),windowHeight_(0),
        canonicalPlayAreaWidth_(0),canonicalPlayAreaHeight_(0),
        canonicalMaxIconHeight_(0),
        canvasClearX_(0),canvasClearY_(0),
        canvasClearW_(0),canvasClearH_(0),
        frameYardTex_(0),
        frameYardX_(0),frameYardY_(0),frameYardW_(0),frameYardH_(0),
        frameFieldDstX_(0),frameFieldDstY_(0),
        frameYardPending_(false),frameFieldPending_(false),
        raisePending_(false),refreshPending_(false),closePending_(false),
        presentDirty_(true),presentScale_(1.0f),presentOffX_(0),presentOffY_(0),
        scissorActive_(false),
        scissorX_(0),scissorY_(0),scissorW_(0),scissorH_(0),
        useXRender_(false),xrenderLib_(nullptr),
        canvasPicture_(None),windowPicture_(None),
        cachedWinW_(0),cachedWinH_(0),windowBgPixel_(0)
     {fonts_[0]=fonts_[1]=fonts_[2]=fonts_[3]=fonts_[4]=nullptr;
      xQueryExtension_=nullptr;xFindVisualFormat_=nullptr;xCreatePicture_=nullptr;
      xFreePicture_=nullptr;xSetPictureTransform_=nullptr;
      xSetPictureFilter_=nullptr;xComposite_=nullptr;
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
     {(void)title;   // WM naming now lives in the pass-2 relocated block
                     // (task 15); the title argument stays for the frozen
                     // interface signature.
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
      if (buttonGC_)     XFreeGC(display,buttonGC_);
      if (defaultGC_)    XFreeGC(display,defaultGC_);
      if (backgroundGC_) XFreeGC(display,backgroundGC_);
      if (gxorGC_)       XFreeGC(display,gxorGC_);
      if (primitiveGC_)  XFreeGC(display,primitiveGC_);
      if (maskedGC_)     XFreeGC(display,maskedGC_);
      if (iconPixmap)    XFreePixmap(display,iconPixmap);
      if (canvasPixmap_) XFreePixmap(display,canvasPixmap_);
      // D17.4(b): RENDER pictures die with the display that owns them.
      for (std::map<TextureId,Picture>::iterator it=renderPictures_.begin();
           it!=renderPictures_.end();++it)
       {if (xFreePicture_)
          xFreePicture_(display,it->second);
       }
      renderPictures_.clear();
      canvasPicture_=windowPicture_=None;
      if (xrenderLib_)
       {dlclose(xrenderLib_);
        xrenderLib_=nullptr;
       }
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
      display=nullptr;window=None;iconPixmap=None;canvasPixmap_=None;
      titleGC_=hiScoreGC_=scoreGC_=defaultGC_=buttonGC_=None;
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
     {// Task-14 frame present: replays the two region copies Stage used to
      // issue raw (yard region, then play-area region) with identical source
      // rects, destination coordinates, order, and default-GC copy semantics.
      // Each leg fires only when its region was requested since the last
      // frame end (see the file-header note): the standalone yard-refresh
      // call points must not publish the cleared canvas.
      // NO internal sync here (Q13-proven): the domain loops already flush
      // at their own frame boundaries, and an extra mid-draw-sequence round
      // trip deterministically perturbs the visible-frame sequence the
      // harness samples (start/gp1-gp5 diverge, AE 1675.73 signature). The
      // once-per-frame sync belongs to the future engine-owned loop (task
      // 25), which calls this exactly once per frame.
      if (!display)
        return;
      if (presentDirty_)                  // belt: legs always present through
        updatePresentTransformCache();    // the ACTIVE transform (D17.4b)
      if (frameYardTex_&&frameYardPending_)
       {presentRegion((Drawable)frameYardTex_,0,0,
                      frameYardW_,frameYardH_,frameYardX_,frameYardY_);
        frameYardPending_=false;
       }
      if (frameFieldPending_)
       {presentRegion((Drawable)canvasPixmap_,canvasClearX_,canvasClearY_,
                      canvasClearW_,canvasClearH_,
                      frameFieldDstX_,frameFieldDstY_);
        frameFieldPending_=false;
       }
     }

    // ------------------------------------------------------------------
    // D17.4(b) X11 letterbox present leg (both published regions).
    // Identity fast path (canonical size: scale exactly 1.0f, offsets
    // exactly 0) issues the byte-identical legacy XCopyArea — this is the
    // canonical-size request-stream guarantee Q13 holds on every
    // XAST_XRENDER path. Non-canonical:
    //   xrender  — scaled composite (picture transform; NEAREST filter set
    //              once per picture: pixel-edge fidelity, an unfiltered copy
    //              at scale 1 and hard-edged pixels elsewhere — LINEAR would
    //              blur game pixels);
    //   fallback — centered 1:1 copy; larger window shows black bars (filled
    //              once by repaintWindowBase at the geometry change), smaller
    //              window crops symmetrically (server clips the negative-
    //              offset blit).
    // ------------------------------------------------------------------
    inline void presentRegion(Drawable src,int sx,int sy,int sw,int sh,
                              int dx,int dy)
     {if (!display||sw<=0||sh<=0)
        return;
      if (presentScale_==1.0f&&presentOffX_==0&&presentOffY_==0)
       {XCopyArea(display,src,window,defaultGC_,sx,sy,
                  (unsigned int)sw,(unsigned int)sh,dx,dy);
        return;
       }
      if (useXRender_)
       {Picture sp=pictureFor(src);
        if (sp&&windowPicture_&&xSetPictureTransform_&&xComposite_)
         {float s=presentScale_;
          int dw=(int)(sw*s),dh=(int)(sh*s);
          if (dw>0&&dh>0)
           {// The Render picture transform is the DESTINATION->SOURCE
            // sampling matrix: dest pixel (i,j) samples the source at
            // matrix*(i,j)+(src_x,src_y). Magnifying by s therefore needs
            // the INVERSE scale in the matrix (a forward s here shrinks the
            // content by 1/s — measured live before this was corrected).
            // NEAREST keeps source-pixel edges hard (see pictureFor).
            const float inv=1.0f/s;
            XTransform xf;
            xf.matrix[0][0]=(XFixed)(inv*65536.0f+0.5f);
            xf.matrix[0][1]=0;xf.matrix[0][2]=0;
            xf.matrix[1][0]=0;
            xf.matrix[1][1]=(XFixed)(inv*65536.0f+0.5f);
            xf.matrix[1][2]=0;
            xf.matrix[2][0]=0;xf.matrix[2][1]=0;xf.matrix[2][2]=65536;
            xSetPictureTransform_(display,sp,&xf);
            xComposite_(display,PictOpSrc,sp,None,windowPicture_,
                        sx,sy,0,0,
                        presentOffX_+(int)(dx*s),presentOffY_+(int)(dy*s),
                        (unsigned int)dw,(unsigned int)dh);
           }
          return;
         }
       }
      XCopyArea(display,src,window,defaultGC_,sx,sy,
                (unsigned int)sw,(unsigned int)sh,
                presentOffX_+dx,presentOffY_+dy);
     }

    // Lazy RENDER picture per source drawable (the yard texture re-creates
    // on icon changes — deleteTexture retires its picture with it). NEAREST
    // filter latched at creation; see presentRegion for the rationale.
    inline Picture pictureFor(Drawable d)
     {TextureId id=(TextureId)d;
      std::map<TextureId,Picture>::iterator it=renderPictures_.find(id);
      if (it!=renderPictures_.end())
        return it->second;
      if (!xFindVisualFormat_||!xCreatePicture_||!xSetPictureFilter_)
        return None;
      XRenderPictFormat* fmt=xFindVisualFormat_(display,
                                                DefaultVisual(display,screen));
      if (!fmt)
        return None;
      Picture p=xCreatePicture_(display,d,fmt,0,nullptr);
      if (p)
       {xSetPictureFilter_(display,p,"nearest",nullptr,0);
        renderPictures_[id]=p;
       }
      return p;
     }

    // ------------------------------------------------------------------
    // (3) Seams
    // ------------------------------------------------------------------
    virtual int pollEvents(GameEvent* out,int maxCount) override
     {// ---- Task 25 (D16): the playingField.H event island, relocated
      // verbatim. The pause spin and Button1 click session block INSIDE this
      // call (as they blocked inside the old drain), so a drain returns only
      // once the game is unpaused and the caller keeps its once-per-frame
      // cadence. Mode rules mirror the old switches exactly:
      //   normal     — keys/buttons/motion translate; bare enters only reset
      //                auto-repeat; leave enters the pause spin; close and
      //                damage latch for the domain.
      //   pause spin — consume until re-enter: mapping refresh + damage
      //                handled, everything else discarded (keys pressed
      //                while paused never reached the game — preserved).
      //   click      — motion/up/enter/leave drive the hover stream, keys
      //                and other buttons discarded; ends on release-in-
      //                window or release-then-reenter.
      int n=0;
      if (!display||!out||maxCount<=0)
        return 0;
      if (raisePending_)
       {XRaiseWindow(display,window);
        raisePending_=false;
       }
      if (presentDirty_)
        updatePresentTransformCache();
      bool pauseSpin=false;
      bool clickSession=false;
      bool clickReleased=false,clickInWindow=true;
      int motionSlot=-1;
      for (;;)
        {// Normal mode: NON-blocking drain — QueuedAfterFlush, not
         // QueuedAfterReading: an empty queue must first FLUSH the output
         // buffer (map+draw requests at init sit there unflushed; without
         // the flush the server never maps the window and no Expose ever
         // arrives — uniform-gray title). This is exactly when the old
         // blocking read flushed before parking. In RunGame the buffer is
         // already empty here (the frame-clock sync ran one statement
         // earlier), so zero extra round trips. Pause spin / click session:
         // fall through to the blocking read — they waited for their
         // terminating event no matter how quiet the queue got.
         if (!pauseSpin&&!clickSession)
          {if (n>=maxCount||XEventsQueued(display,QueuedAfterFlush)==0)
             break;
          }
        XEvent e;
        XNextEvent(display,&e);
        GameEvent g={};
        bool emit=false;
        switch(e.type)
         {case MappingNotify:
           XRefreshKeyboardMapping((XMappingEvent*)&e);
           break;
          case Expose:
          case MapNotify:
          case ConfigureNotify:
           // damage latches for the domain: the frozen interface has no
           // event type to carry it, so Refresh() fires after the drain
           refreshPending_=true;
           if (e.type==ConfigureNotify)
             presentDirty_=true;
           break;
          case ClientMessage:
           if (!pauseSpin&&!clickSession&&
               ((XClientMessageEvent*)&e)->message_type==wmProtocolsAtom_&&
               ((XClientMessageEvent*)&e)->data.l[0]==wmDeleteWindowAtom_)
             closePending_=true;       // discarded verbatim inside both inner loops
           break;
          case EnterNotify:
           XAutoRepeatOff(display);
           if (pauseSpin)
            {XRaiseWindow(display,window);
             XSync(display,False);
             pauseSpin=false;
             g.type=GameEvent::CursorEnter;
             emit=true;
            }
           else if (clickSession)
            {clickInWindow=true;
             g.type=GameEvent::CursorEnter;
             emit=true;
             if (clickReleased)
              {XRaiseWindow(display,window);
               XSync(display,False);
               clickSession=false;
              }
            }
           break;
          case LeaveNotify:
           if (autoRepeatState_==AutoRepeatModeOn)
             XAutoRepeatOn(display);
           g.type=GameEvent::CursorLeave;
           emit=true;
           if (clickSession)
             clickInWindow=false;
           else
             pauseSpin=true;           // block below until re-enter
           break;
          case KeyPress:
          case KeyRelease:
           if (pauseSpin||clickSession)
             break;                    // consumed-and-discarded, as the island did
           {char keyText[2];
            g.type=(e.type==KeyPress)?GameEvent::KeyDown:GameEvent::KeyUp;
            g.key=(int)XLookupKeysym(&e.xkey,0);
            g.character=(XLookupString((XKeyEvent*)&e,keyText,2,NULL,NULL)==1)
                          ? keyText[0] : (char)0;
            emit=true;
           }
           break;
          case ButtonPress:
           if (pauseSpin||clickSession)
             break;                    // discarded verbatim
           g.type=GameEvent::MouseDown;
           g.key=(int)e.xbutton.button;
           inverseTransformPoint(e.xbutton.x,e.xbutton.y,g.x,g.y);
           emit=true;
           if (e.xbutton.button==Button1)
            {clickSession=true;
             clickReleased=false;clickInWindow=true;motionSlot=-1;
            }
           break;
          case ButtonRelease:
           if (!clickSession||e.xbutton.button!=Button1)
             break;                    // the main drain had no release case
           g.type=GameEvent::MouseUp;
           g.key=(int)e.xbutton.button;
           inverseTransformPoint(e.xbutton.x,e.xbutton.y,g.x,g.y);
           emit=true;
           clickReleased=true;
           if (clickInWindow)
            {XRaiseWindow(display,window);
             XSync(display,False);
             clickSession=false;
            }
           break;
          case MotionNotify:
           if (pauseSpin)
             break;                    // discarded verbatim in the spin
           g.type=GameEvent::MouseMove;
           inverseTransformPoint(e.xmotion.x,e.xmotion.y,g.x,g.y);
           if (clickSession)
            {// hover consumes only the latest position: compress into one
             // slot so a drag can never overflow the caller's buffer
             if (motionSlot>=0)
               out[motionSlot]=g;
             else if (n<maxCount)
              {motionSlot=n;out[n++]=g;}
            }
           else
             emit=true;
           break;
         }
        if (emit&&n<maxCount)
          out[n++]=g;
       }
      // D17.4(b): a ConfigureNotify processed INSIDE this drain latched
      // presentDirty_ after the head check — refresh the cache BEFORE the
      // caller consumes takeRefreshRequest() and repaints chrome, so the
      // geometry-change base repaint (bars) strictly precedes any redraw.
      if (presentDirty_)
        updatePresentTransformCache();
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
     {// Active-path cache read (D17.4b): consumers must agree with what
      // endFrame presents — see updatePresentTransformCache for the per-path
      // math. Identity at the canonical size.
      if (presentDirty_)
        updatePresentTransformCache();
      outScale=presentScale_;
      outOffsetX=presentOffX_;
      outOffsetY=presentOffY_;
     }

    // ------------------------------------------------------------------
    // D17.4b/D17.4e coordinate seam: draws whose target IS the window carry
    // canonical-client coordinates and map through the ACTIVE present
    // transform; render-target draws (canvas, button faces, yard) stay
    // target-local. Identity at the canonical size => the request stream is
    // identical to the pre-letterbox backend on every gate that runs there.
    // ------------------------------------------------------------------
    inline bool windowTarget() const
      {return currentTarget_==window;}
    inline int mapX(float x) const
      {return presentOffX_+(int)(x*presentScale_);}
    inline int mapY(float y) const
      {return presentOffY_+(int)(y*presentScale_);}
    inline int mapLen(float len) const
      {return (int)(len*presentScale_);}

    // ------------------------------------------------------------------
    // (4) Primitives
    // ------------------------------------------------------------------
    virtual void clear() override
     {// The raw clears filled only the play-area SUB-RECT of the canvas
      // (playingField.H :226/:343/:685 semantics): rect (80,80,W,H) inside
      // the (W+4*40,H+4*40) canvas — NEVER the whole drawable (the canvas
      // border ring survives). backgroundGC_ is GXcopy black (GC default
      // function, never changed); the gxor GC is compositing-only (R4-M1).
      // Callers bracket beginRenderTo(canvas)/clear()/endRenderTo().
      if (!display)
        return;
      XSetForeground(display,backgroundGC_,BlackPixel(display,screen));
      XFillRectangle(display,currentTarget_,backgroundGC_,
                     canvasClearX_,canvasClearY_,
                     (unsigned int)canvasClearW_,(unsigned int)canvasClearH_);
     }

    virtual void drawLine(float x1,float y1,float x2,float y2,
                          float r,float g,float b,float width) override
      {if (!display)
         return;
       XSetForeground(display,primitiveGC_,pixelFor(r,g,b));
       XSetLineAttributes(display,primitiveGC_,
                          width>=2.0f?3:1,LineSolid,CapNotLast,JoinRound);
       XDrawLine(display,currentTarget_,primitiveGC_,
                 windowTarget()?mapX(x1):(int)x1,
                 windowTarget()?mapY(y1):(int)y1,
                 windowTarget()?mapX(x2):(int)x2,
                 windowTarget()?mapY(y2):(int)y2);
      }

    virtual void drawPolygon(const float* verts,int count,
                              float r,float g,float b,bool fill) override
      {if (!display||!verts||count<2)
         return;
       XPoint pts[256];
       int n=count<256?count:256;
       for (int i=0;i<n;++i)
        {pts[i].x=(short)(windowTarget()?mapX(verts[2*i])
                                        :(int)verts[2*i]);
         pts[i].y=(short)(windowTarget()?mapY(verts[2*i+1])
                                        :(int)verts[2*i+1]);
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
       int xi=windowTarget()?mapX(x):(int)x;
       int yi=windowTarget()?mapY(y):(int)y;
       unsigned int wi=(unsigned int)(windowTarget()?mapLen(w):(int)w);
       unsigned int hi=(unsigned int)(windowTarget()?mapLen(h):(int)h);
       XSetForeground(display,primitiveGC_,pixelFor(r,g,b));
       if (fill)
         XFillRectangle(display,currentTarget_,primitiveGC_,xi,yi,wi,hi);
       else
         XDrawRectangle(display,currentTarget_,primitiveGC_,xi,yi,wi,hi);
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
       XDrawImageString(display,currentTarget_,gc,
                        windowTarget()?mapX(x):(int)x,
                        windowTarget()?mapY(y):(int)y,
                        text,(int)strlen(text));
      }

    virtual void drawStringTransparent(const char* text,float x,float y,
                                        int fontId,
                                        float r,float g,float b) override
      {if (!display||!validFont(fontId)||!text)
         return;
       // Canvas route keeps the gxor GC VERBATIM (attributes untouched): the
       // game-over score table XOR-composites over the surviving final frame
       // (function bit load-bearing, D3); the help screen draws over fresh
       // black where GXor ≡ GXcopy pixel-wise. Any other target uses the
       // per-font GC + requested color (stage's title site needs GXcopy).
       if (currentTarget_==(Drawable)canvasPixmap_)
        {XDrawString(display,currentTarget_,gxorGC_,(int)x,(int)y,
                     text,(int)strlen(text));
         return;
        }
       GC gc=fontGC((EngineFontId)fontId);
       XSetForeground(display,gc,pixelFor(r,g,b));
       XDrawString(display,currentTarget_,gc,
                   windowTarget()?mapX(x):(int)x,
                   windowTarget()?mapY(y):(int)y,
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
     {// Copy pass-through; alpha ignored (core X11 has no blended copy —
      // D14 pass-through preserves current behavior exactly). Target routing
      // (task 15, resolves the T19/T21 divergence): world sprites drew onto
      // the canvas through THE gxor compositing GC (the single repo-wide
      // function bit, D3 — XOR is load-bearing over prior frame content),
      // while render-target faces copy GXcopy. One method, two required
      // functions on X11 → route on the current target.
      (void)alpha;
      if (!display||!tex)
        return;
      GC gc=(currentTarget_==(Drawable)canvasPixmap_)?gxorGC_:defaultGC_;
      int xi=windowTarget()?mapX(x):(int)x;
      int yi=windowTarget()?mapY(y):(int)y;
      unsigned int wi=(unsigned int)(windowTarget()?mapLen(w):(int)w);
      unsigned int hi=(unsigned int)(windowTarget()?mapLen(h):(int)h);
      XCopyArea(display,(Drawable)tex,currentTarget_,gc,
                0,0,wi,hi,xi,yi);
     }

    virtual void drawTextureMasked(TextureId content,TextureId mask,
                                    float x,float y,float w,float h) override
     {// Native clip-mask GC semantics (explosionGraphic.H:103-117 /
       // rockGroup.H:184-197): XSetClipMask + XSetClipOrigin + XCopyArea.
       if (!display||!content||!mask)
         return;
       int xi=windowTarget()?mapX(x):(int)x;
       int yi=windowTarget()?mapY(y):(int)y;
       unsigned int wi=(unsigned int)(windowTarget()?mapLen(w):(int)w);
       unsigned int hi=(unsigned int)(windowTarget()?mapLen(h):(int)h);
       XSetClipMask(display,maskedGC_,(Pixmap)mask);
       XSetClipOrigin(display,maskedGC_,xi,yi);
       XCopyArea(display,(Drawable)content,currentTarget_,maskedGC_,
                 0,0,wi,hi,xi,yi);
      }

    virtual void deleteTexture(TextureId tex) override
      {if (!display||!tex)
         return;
       std::map<TextureId,Picture>::iterator pit=renderPictures_.find(tex);
       if (pit!=renderPictures_.end())
        {if (xFreePicture_)
           xFreePicture_(display,pit->second);
         renderPictures_.erase(pit);
        }
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
    // Transition accessors. Survivors (task 47): fonts/icon/state/atoms are
    // load-bearing — options.H (the D9 X11-consumer island) and playingField's
    // guarded X11 loop read them now that Stage's non-owning alias members
    // are gone. The GC accessors and the windowWidth/windowHeight pair died
    // with their last readers at 47.
    // ------------------------------------------------------------------
    inline Pixmap icon() const {return iconPixmap;}
    inline XFontStruct* titleFont() const {return fonts_[EngineFont_Title];}
    inline XFontStruct* buttonFont() const {return fonts_[EngineFont_Button];}
    inline XFontStruct* hiScoreFont() const {return fonts_[EngineFont_HiScore];}
    inline XFontStruct* scoreFont() const {return fonts_[EngineFont_Score];}
    inline XFontStruct* errorFont() const {return fonts_[EngineFont_Error];}
    // Task-15 accessors: canvas feeds PlayingField's render-target brackets
    // (the GPU twin returns 0, where beginRenderTo is a documented no-op);
    // atoms are captured in pass 2, consumed by options.H's modal pump.
    inline Pixmap canvas() const {return canvasPixmap_;}
    inline Atom wmProtocolsAtom() const {return wmProtocolsAtom_;}
    inline Atom wmDeleteWindowAtom() const {return wmDeleteWindowAtom_;}
    inline int autoRepeatState() const {return autoRepeatState_;}
    // Task-14 frame-present plumbing (transition alias class — DELETED at
    // task 47): Stage publishes the two present regions it computes in its
    // constructor plus the live yard texture (the yard re-creates itself on
    // icon changes), and declares which region a frame refresh covers.
    inline void setFrameGeometry(int yardX,int yardY,int yardW,int yardH,
                                 int fieldDstX,int fieldDstY)
     {frameYardX_=yardX;frameYardY_=yardY;
      frameYardW_=yardW;frameYardH_=yardH;
      frameFieldDstX_=fieldDstX;frameFieldDstY_=fieldDstY;
     }
    inline void setFrameYardTexture(TextureId tex)
     {frameYardTex_=tex;
      frameYardPending_=true;
     }
    inline void requestFieldPresent()
      {frameFieldPending_=true;}
    // ---- Task-25 transition plumbing (DELETED at task 47) -----------------
    // The frozen 27-method interface has no damage/close event types and no
    // X11-specific pacing primitive, so the relocated island's remaining
    // observable effects ride four tiny non-virtual helpers (exception zone
    // (a); createBitmapMask/textFirstCharLbearing precedent):
    //   requestRaise        — RunGame-entry window-raise latch, performed at
    //                         the next drain (the raw call stood at RunGame's
    //                         first statement).
    //   frameClockSync      — the once-per-frame request flush bounding the
    //                         D4 diffTime window; RunGame calls it at the old
    //                         sync's exact statement position. Deliberately
    //                         NOT inside endFrame (Q13-proven harmful there)
    //                         and NOT inside pollEvents (the title loop must
    //                         not pay a round trip per drain). GL/VK replace
    //                         this with their own frame timestamps (D16).
    //   takeRefreshRequest  — expose-class damage latch → stage->Refresh().
    //   takeCloseRequest    — WM-delete latch → RunGame returns on /
    //                         PlayTheGame returns its high score.
    inline void requestRaise()
      {raisePending_=true;}
    inline void frameClockSync()
     {if (display)
        XSync(display,False);}
    inline bool takeRefreshRequest()
     {bool r=refreshPending_;
      refreshPending_=false;
      return r;}
    inline bool takeCloseRequest()
     {bool c=closePending_;
      closePending_=false;
      return c;}
    // Color-allocation seam (the T17/T20 closure path): domain code that
    // must hand a live colormap pixel to a non-engine consumer gets it from
    // here instead of allocating directly.
    inline unsigned long allocateColor(float r,float g,float b)
     {return pixelFor(r,g,b);}
    // Colored stamp (task 47): engine-level closure of the last raw
    // colored-stamp sites (shipYard ctor/AlterIcon). Same allocate-or-fallback
    // arithmetic as allocateColor for both colors, then one colored-stamp
    // request parented to the window at DefaultDepth — the yard-parented
    // originals resolved the identical depth, and a pixmap's drawable argument
    // contributes nothing else, so the created resource is identical.
    virtual TextureId createColoredStamp(const unsigned char* bits,int w,int h,
                                         float fgR,float fgG,float fgB,
                                         float bgR,float bgG,float bgB) override
     {if (!display) return 0;
      unsigned long fg=pixelFor(fgR,fgG,fgB),
                    bg=pixelFor(bgR,bgG,bgB);
      Pixmap p=XCreatePixmapFromBitmapData(display,window,(char*)bits,w,h,
                                           fg,bg,
                                           DefaultDepth(display,screen));
      return (TextureId)p;}
    // Depth-1 bitmap creation for clip-mask consumers (D6): the generic texture
    // path targets window depth; GC clip masks require depth 1.
    inline TextureId createBitmapMask(const uint8_t* bits,int w,int h)
     {if (!display) return 0;
      Pixmap p=XCreateBitmapFromData(display,window,(const char*)bits,w,h);
      return (TextureId)p;}
    // First-character left bearing of text in the given font: the raw label
    // placement consumed this from per-string extents; FontMetrics carries
    // only the font-wide maximum, which differs for the button face label.
    inline int textFirstCharLbearing(const char* text,int fontId)
     {if (!validFont(fontId)||!text||!text[0])
        return 0;
      int direction,ascent,descent;
      XCharStruct info;
      XTextExtents(fonts_[fontId],text,1,&direction,&ascent,&descent,&info);
      return info.lbearing;}

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
      buttonGC_=makeFontGC(fonts_[EngineFont_Button],background);
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
      // Play-field canvas pixmap (D15 row): created here at task 15 — owner
      // = this backend, freed in shutdown — parented to the window (the same
      // drawable argument the old PlayingField ctor site used). Geometry
      // duplicates two domain constants because layering forbids gamePlay
      // includes (windowBg-gray precedent): the rock scale is 40, so the
      // canvas is play-area + 4*40 per side and the clear sub-rect sits at
      // (2*40, 2*40). Deliberately NOT registered in textureDims_ (not
      // caller-lifetime; prevents an accidental deleteTexture double-free).
      canvasPixmap_=XCreatePixmap(display,window,
                                  (unsigned int)(canonicalPlayAreaWidth_+4*40),
                                  (unsigned int)(canonicalPlayAreaHeight_+4*40),
                                  (unsigned int)DefaultDepth(display,screen));
      canvasClearX_=2*40;canvasClearY_=2*40;
      canvasClearW_=canonicalPlayAreaWidth_;
      canvasClearH_=canonicalPlayAreaHeight_;
      // D17.4(b): one-time present-path selection (XAST_XRENDER=auto|on|off,
      // default auto = RENDER-extension probe). Logs the choice to stderr —
      // the harness greps this marker into its manifest.
      windowBgPixel_=(unsigned long)background;
      selectPresentPath();
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
      // ---- WM-hints block relocated VERBATIM-ORDER from PlayTheGame
      // (playingField.H:581-604) at task 15 (D-C C1). Position is AFTER the
      // KBState save so autoRepeatState_ captures the pre-off state (order
      // trap: an earlier off would break exit-restore). Two recorded deltas:
      // argv/argc pass as NULL,0 (frozen initWindow(const char*) signature;
      // command-name property unset — pixel-inert) and map-and-raise fires
      // at init instead of first PlayTheGame entry. The size-pin hint
      // structure is dropped entirely per v6/S2/D15 (no pin anywhere): its
      // allocation AND its free slot are gone; min/max UNSET. The pre-
      // existing text-property leak below is PRESERVED (ASan baseline).
      wmProtocolsAtom_=XInternAtom(display,"WM_PROTOCOLS",False);
      wmDeleteWindowAtom_=XInternAtom(display,"WM_DELETE_WINDOW",False);
      {char gameName[10]="Asteroids",
         * gameNameAddr=gameName;
       XTextProperty textProperty;
       XStringListToTextProperty(&gameNameAddr,1,&textProperty);
       XWMHints* windowHints=XAllocWMHints();
       windowHints->icon_pixmap=iconPixmap;
       windowHints->flags=IconPixmapHint;
       XClassHint* classHint=XAllocClassHint();
       XSetWMProperties(display,window,&textProperty,&textProperty,
                        NULL,0,NULL,windowHints,classHint);
       XFree((char*)classHint);
       XFree((char*)windowHints);
       XSetWMProtocols(display,window,&wmDeleteWindowAtom_,1);
       XAutoRepeatOff(display);
       XSelectInput(display,window,EnterWindowMask|LeaveWindowMask
                                       |StructureNotifyMask|ExposureMask
                                       |KeyPressMask|KeyReleaseMask
                                        |ButtonPressMask|ButtonReleaseMask
                                        |Button1MotionMask);
       XMapRaised(display,window);
      }
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
        case EngineFont_Button:  return buttonGC_;
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

    // Present-transform cache for event coordinates: recomputed only on
    // ConfigureNotify (file-header note) so a motion flood costs zero server
    // round trips — the island used raw window coords directly, and a
    // per-event XGetGeometry here would reintroduce the mid-drain round
    // trips T14 proved harmful.
    //
    // D17.4(b): the stored scale/offset now describe the ACTIVE presentation
    // path — xrender: uniform-scale centered; fallback: centered 1:1 (scale
    // 1, centering offsets may be negative => symmetric crop) — so event
    // inverse-mapping (D17.4d, inverseTransformPoint) and every window-space
    // consumer agree with what endFrame actually presents. A geometry change
    // repaints the window base ONCE (black bars when larger; background gray
    // again at canonical so stale bars/content cannot linger as ghosts).
    // The FIRST computation never repaints: canonical-size sessions issue
    // byte-identical requests to the pre-letterbox backend.
    inline void updatePresentTransformCache()
     {if (!display||!window)
        return;
      unsigned int w=0,h=0,bb=0,depth=0;
      Window root;
      int wx,wy;
      if (!XGetGeometry(display,window,&root,&wx,&wy,&w,&h,&bb,&depth))
        return;
      const bool canonical=((int)w==windowWidth_&&(int)h==windowHeight_);
      float scale=1.0f;
      int ox=0,oy=0;
      if (!canonical)
       {if (useXRender_)
         {scale=((float)w/windowWidth_<(float)h/windowHeight_)
                  ? (float)w/windowWidth_ : (float)h/windowHeight_;
          ox=((int)w-(int)(windowWidth_*scale))/2;
          oy=((int)h-(int)(windowHeight_*scale))/2;
         }
        else
         {ox=((int)w-windowWidth_)/2;
          oy=((int)h-windowHeight_)/2;
         }
       }
      if ((int)w!=cachedWinW_||(int)h!=cachedWinH_)
       {const bool first=(cachedWinW_==0&&cachedWinH_==0);
        cachedWinW_=(int)w;
        cachedWinH_=(int)h;
        if (!first)
          repaintWindowBase(canonical,(int)w,(int)h,scale,ox,oy);
       }
      presentScale_=scale;presentOffX_=ox;presentOffY_=oy;
      presentDirty_=false;
     }

    // One-shot per geometry change. The X11 presentation model paints chrome
    // ONCE over the PERSISTENT window background (Stage::Refresh emits text
    // glyphs/cells only — the gray backdrop is the window's own background
    // pixel), so the base repaint must restore that gray EVERYWHERE first;
    // black letterbox bars are then painted only OUTSIDE the mapped content
    // rect. Filling everything black instead would leave chrome rendered
    // black-on-black after the refresh (measured: invisible title).
    inline void repaintWindowBase(bool canonical,int w,int h,
                                  float scale,int ox,int oy)
     {if (!display)
        return;
      XSetForeground(display,backgroundGC_,(unsigned long)windowBgPixel_);
      XFillRectangle(display,window,backgroundGC_,0,0,
                     (unsigned int)w,(unsigned int)h);
      if (canonical)
        return;
      const int cw=(int)(windowWidth_*scale),ch=(int)(windowHeight_*scale);
      XSetForeground(display,backgroundGC_,BlackPixel(display,screen));
      if (ox>0)
        XFillRectangle(display,window,backgroundGC_,0,0,
                       (unsigned int)ox,(unsigned int)h);
      if (ox+cw<w)
        XFillRectangle(display,window,backgroundGC_,ox+cw,0,
                       (unsigned int)(w-ox-cw),(unsigned int)h);
      if (oy>0)
        XFillRectangle(display,window,backgroundGC_,0,0,
                       (unsigned int)w,(unsigned int)oy);
      if (oy+ch<h)
        XFillRectangle(display,window,backgroundGC_,0,oy+ch,
                       (unsigned int)w,(unsigned int)(h-oy-ch));
     }

    // One-time D17.4(b) path selection. XAST_XRENDER: unset/"auto" probes
    // the server's RENDER extension (via run-time-bound libXrender); "on"
    // forces the probe but degrades to fallback with a logged reason;
    // "off" skips the probe entirely. The chosen path is logged once —
    // "[x11] present path: xrender|fallback" — for the harness manifest.
    void selectPresentPath()
     {useXRender_=false;
      const char* env=getenv("XAST_XRENDER");
      const bool off=(env&&strcmp(env,"off")==0);
      const bool forceOn=(env&&strcmp(env,"on")==0);
      if (!off)
       {if (loadXRender())
         {int evBase=0,errBase=0;
          if (xQueryExtension_(display,&evBase,&errBase))
            useXRender_=true;
          else if (forceOn)
            fprintf(stderr,"[x11] XAST_XRENDER=on but server lacks RENDER; "
                           "using fallback\n");
         }
        else if (forceOn)
          fprintf(stderr,"[x11] XAST_XRENDER=on but libXrender unavailable; "
                         "using fallback\n");
       }
      if (useXRender_)
       {XRenderPictFormat* fmt=xFindVisualFormat_(
           display,DefaultVisual(display,screen));
        if (fmt)
         {canvasPicture_=xCreatePicture_(display,(Drawable)canvasPixmap_,
                                         fmt,0,nullptr);
          windowPicture_=xCreatePicture_(display,window,fmt,0,nullptr);
         }
        if (!canvasPicture_||!windowPicture_)
         {useXRender_=false;
          fprintf(stderr,"[x11] RENDER picture creation failed; "
                         "using fallback\n");
         }
       }
      fprintf(stderr,"[x11] present path: %s\n",
              useXRender_?"xrender":"fallback");
     }

    bool loadXRender()
     {const char* names[2]={"libXrender.so.1","libXrender.so"};
      for (int i=0;i<2&&!xrenderLib_;++i)
        xrenderLib_=dlopen(names[i],RTLD_LAZY|RTLD_LOCAL);
      if (!xrenderLib_)
        return false;
      xQueryExtension_=(xastXRenderQueryExtensionFn)dlsym(
        xrenderLib_,"XRenderQueryExtension");
      xFindVisualFormat_=(xastXRenderFindVisualFormatFn)dlsym(
        xrenderLib_,"XRenderFindVisualFormat");
      xCreatePicture_=(xastXRenderCreatePictureFn)dlsym(
        xrenderLib_,"XRenderCreatePicture");
      xFreePicture_=(xastXRenderFreePictureFn)dlsym(
        xrenderLib_,"XRenderFreePicture");
      xSetPictureTransform_=(xastXRenderSetPictureTransformFn)dlsym(
        xrenderLib_,"XRenderSetPictureTransform");
      xSetPictureFilter_=(xastXRenderSetPictureFilterFn)dlsym(
        xrenderLib_,"XRenderSetPictureFilter");
      xComposite_=(xastXRenderCompositeFn)dlsym(
        xrenderLib_,"XRenderComposite");
      const bool ok=xQueryExtension_&&xFindVisualFormat_&&xCreatePicture_
                     &&xFreePicture_&&xSetPictureTransform_
                     &&xSetPictureFilter_&&xComposite_;
      if (!ok)
       {dlclose(xrenderLib_);
        xrenderLib_=nullptr;
       }
      return ok;
     }

    inline void inverseTransformPoint(int winX,int winY,int& outX,int& outY)
      const
     {float scale=presentScale_;
      int ox=presentOffX_,oy=presentOffY_;
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
    Pixmap canvasPixmap_;
    XFontStruct* fonts_[5];
    GC titleGC_,hiScoreGC_,scoreGC_,defaultGC_;
    GC buttonGC_;
    GC backgroundGC_,gxorGC_,primitiveGC_,maskedGC_;
    Drawable currentTarget_;
    Atom wmProtocolsAtom_,wmDeleteWindowAtom_;
    int autoRepeatState_;
    int windowWidth_,windowHeight_;
    int canonicalPlayAreaWidth_,canonicalPlayAreaHeight_,
        canonicalMaxIconHeight_;
    int canvasClearX_,canvasClearY_,canvasClearW_,canvasClearH_;
    TextureId frameYardTex_;
    int frameYardX_,frameYardY_,frameYardW_,frameYardH_;
    int frameFieldDstX_,frameFieldDstY_;
    bool frameYardPending_,frameFieldPending_;
    bool raisePending_,refreshPending_,closePending_;
    bool presentDirty_;
    float presentScale_;
    int presentOffX_,presentOffY_;
    bool scissorActive_;
    int scissorX_,scissorY_,scissorW_,scissorH_;
    // ---- D17.4(b) X11 letterbox present path ------------------------------
    bool useXRender_;                    // one-time init selection (XAST_XRENDER)
    void* xrenderLib_;                   // dlopen handle; closed in shutdown
    Picture canvasPicture_,windowPicture_;// eager pictures (canvas + window)
    int cachedWinW_,cachedWinH_;         // geometry the cache was built from
    unsigned long windowBgPixel_;        // canonical-restore fill color
    std::map<TextureId,Picture> renderPictures_;   // lazy per-texture pictures
    xastXRenderQueryExtensionFn xQueryExtension_;
    xastXRenderFindVisualFormatFn xFindVisualFormat_;
    xastXRenderCreatePictureFn xCreatePicture_;
    xastXRenderFreePictureFn xFreePicture_;
    xastXRenderSetPictureTransformFn xSetPictureTransform_;
    xastXRenderSetPictureFilterFn xSetPictureFilter_;
    xastXRenderCompositeFn xComposite_;
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

#else
// Guards-closed twin (task 47). Domain code calls four X11Backend transition
// helpers WITHOUT preprocessor guards (Stage's present publishing, the canvas
// render-target brackets); this mirror keeps those call sites legal on the
// GPU legs. Every entry is provably inert there: canvas() returns 0 and
// beginRenderTo(0) is a documented no-op on GL/VK, and the frame-publish trio
// has no GPU consumer (GL/VK present through their own endFrame).
class X11Backend : public RenderingEngine
 {public:
    inline TextureId canvas() const {return 0;}
    inline void setFrameGeometry(int,int,int,int,int,int) {}
    inline void setFrameYardTexture(TextureId) {}
    inline void requestFieldPresent() {}
 };
#endif

#endif
