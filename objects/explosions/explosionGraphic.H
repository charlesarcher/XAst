#ifndef _EXPLOSIONGRAPHIC_H
#define _EXPLOSIONGRAPHIC_H

#ifdef X11_BACKEND
#include<X11/Xlib.h>
#include"../../utilities/rendering/x11Backend.H"
#endif
#include"../../utilities/rendering/x11types.H"
#include"../../utilities/rendering/renderingEngine.H"
#include<new>
#include"../../gamePlay/stage.H"
#include"../../utilities/pixmaps/composite/compositePixmap.H"
#include"../../bitmaps/explosionEdge.xbm"
#include"../../bitmaps/explosionMiddle.xbm"
#include"../../bitmaps/explosionCenter.xbm"

class ExplosionGraphic
 {public:
    static const int numFrames;

    CompositePixmap* const frames;
    // Vestigial non-owning alias: the per-mask GCs were absorbed into the
    // backend's masked-draw path (the native clip-mask GCs live inside the
    // backend implementation). Kept declared+zeroed only for the transitional
    // raw readers (shipGroup.H / bullet.H, last consumer leaves at task 22);
    // the member is deleted at task 47.
    GC gc[5];
    RenderingEngine& engine;

    ExplosionGraphic(RenderingEngine&);
    ~ExplosionGraphic();

    // Mask texture for a frame's masked draw — the clip geometry is absorbed
    // into the engine call (D6). Mapping is the original gc[]->clipMask[]
    // wiring: frames 0/1/2/3/4 wore center/middle/edge/edge/middle.
    inline TextureId MaskId(const int frameNumber) const;

  private:

    // Guards-closed transitional fallback: ids stay 0 so masked draws skip
    // (the engine call no-ops on a zero mask); the real cross-backend path
    // lands at Phase 3/task 34.
    TextureId clipMask[3]={0,0,0};

#ifdef X11_BACKEND
    static XColor hottest,
                  hotter,
                  hot;
#endif
 };

extern ExplosionGraphic* explosionGraphic;

inline const int ExplosionGraphic::numFrames=5;

inline TextureId ExplosionGraphic::MaskId(const int frameNumber) const
 {static const int maskOfFrame[numFrames]={0,1,2,2,1};
  return clipMask[maskOfFrame[frameNumber]];}

#ifdef X11_BACKEND
inline XColor ExplosionGraphic::hottest={0,
                                  65535,//255
                                  65535,//255
                                  65535,//255
                                  DoRed|DoGreen|DoBlue},
       ExplosionGraphic::hotter={0,
                                 65535,//255
                                 65535,//255
                                 0,
                                 DoRed|DoGreen|DoBlue},
       ExplosionGraphic::hot={0,
                              65535,//255
                              42405,//165
                              0,
                              DoRed|DoGreen|DoBlue};
#endif
inline ExplosionGraphic::ExplosionGraphic(RenderingEngine& eng):
                                frames((CompositePixmap*)new char[sizeof(CompositePixmap)*numFrames]),
                                engine(eng)
 {
  #ifdef X11_BACKEND
  for(int i=5;i--;)
    gc[i]=nullptr;
  (new(&frames[0]) CompositePixmap(stage->display,stage->window,
                                   explosionEdge_width,explosionEdge_height))
      ->AddBitmapData(explosionCenter_bits,
                      explosionCenter_width,explosionCenter_height,
                      hottest);
  (new(&frames[1]) CompositePixmap(stage->display,stage->window,
                                   explosionEdge_width,explosionEdge_height))
      ->AddBitmapData(explosionMiddle_bits,
                      explosionMiddle_width,explosionMiddle_height,
                      hotter)
       .AddBitmapData(explosionCenter_bits,
                      explosionCenter_width,explosionCenter_height,
                      hottest);
  (new(&frames[2]) CompositePixmap(stage->display,stage->window,
                                   explosionEdge_width,explosionEdge_height))
      ->AddBitmapData(explosionEdge_bits,
                      explosionEdge_width,explosionEdge_height,
                      hot)
       .AddBitmapData(explosionMiddle_bits,
                      explosionMiddle_width,explosionMiddle_height,
                      hotter)
       .AddBitmapData(explosionCenter_bits,
                      explosionCenter_width,explosionCenter_height,
                      hottest);
  (new(&frames[3]) CompositePixmap(stage->display,stage->window,
                                  explosionEdge_width,explosionEdge_height))
      ->AddBitmapData(explosionEdge_bits,
                      explosionEdge_width,explosionEdge_height,
                      hot)
       .AddBitmapData(explosionMiddle_bits,
                      explosionMiddle_width,explosionMiddle_height,
                      hotter)
       .AddBitmapData(explosionCenter_bits,
                      explosionCenter_width,explosionCenter_height,
                      hottest);
  (new(&frames[4]) CompositePixmap(stage->display,stage->window,
                                  explosionEdge_width,explosionEdge_height))
      ->AddBitmapData(explosionMiddle_bits,
                      explosionMiddle_width,explosionMiddle_height,
                      hot)
       .AddBitmapData(explosionCenter_bits,
                      explosionCenter_width,explosionCenter_height,
                      hotter);
  // The 3 clip masks become backend-owned depth-1 mask bitmaps (a GC clip
  // mask must be depth 1 — the generic texture path targets window depth),
  // created at the same init point and in the same order (center/middle/edge)
  // as before; their lifetime is caller-side via deleteTexture in the dtor
  // (D15 row "explosion frames → 18").
  clipMask[0]=static_cast<X11Backend&>(engine).createBitmapMask(explosionCenter_bits,
                                                                explosionCenter_width,
                                                                explosionCenter_height);
  clipMask[1]=static_cast<X11Backend&>(engine).createBitmapMask(explosionMiddle_bits,
                                                                explosionMiddle_width,
                                                                explosionMiddle_height);
  clipMask[2]=static_cast<X11Backend&>(engine).createBitmapMask(explosionEdge_bits,
                                                                explosionEdge_width,
                                                                explosionEdge_height);
  #endif
 }

inline ExplosionGraphic::~ExplosionGraphic()
 {
  #ifdef X11_BACKEND
  for(int i=3;i--;)
    engine.deleteTexture(clipMask[i]);
  #endif
  for(int i=numFrames;i--;)
    frames[i].~CompositePixmap();
  delete [] (char*)frames;
 }

#endif
