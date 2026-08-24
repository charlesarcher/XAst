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

#ifndef X11_BACKEND
    // Task-36 GL legs: the five composite frames ride the task-27 CPU
    // compositing (proven byte-equal to the guarded CompositePixmap output in
    // test/composite/) uploaded as RGBA8 textures; clipMask[] holds real R8
    // coverage textures so MaskId() feeds drawTextureMasked directly.
    inline TextureId FrameId(const int frameNumber) const;
    inline int FrameWidth() const;
    inline int FrameHeight() const;
#endif

  private:

    // X11: backend-owned depth-1 mask bitmaps. GL: R8 discard-mask textures
    // created in the ctor below (same center/middle/edge order).
    TextureId clipMask[3]={0,0,0};

#ifndef X11_BACKEND
    TextureId glFrames[5]={0,0,0,0,0};
    int glFrameW=0,
        glFrameH=0;
#endif

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

#ifndef X11_BACKEND
inline TextureId ExplosionGraphic::FrameId(const int frameNumber) const
 {return glFrames[frameNumber];}

inline int ExplosionGraphic::FrameWidth() const
 {return glFrameW;}

inline int ExplosionGraphic::FrameHeight() const
 {return glFrameH;}
#endif

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
  #ifndef X11_BACKEND
  // The five frames via the CPU composite (same layer stacks, same order,
  // same later-layer-overwrites semantics as the guarded block above); the
  // three masks become R8 discard textures in center/middle/edge order.
  const unsigned short hottestR=65535,hottestG=65535,hottestB=65535,
                       hotterR=65535, hotterG=65535, hotterB=0,
                       hotR=65535,     hotG=42405,   hotB=0;
  const int w=explosionEdge_width,
            h=explosionEdge_height;
  {CompositeLayerSpec f0[1]={{explosionCenter_bits,explosionCenter_width,
                              explosionCenter_height,hottestR,hottestG,hottestB}};
   CompositeFrameRGBA8 frame;
   compositeFrameStack(frame,w,h,f0,1);
   glFrames[0]=engine.createTextureFromBitmap(frame.rgba8.data(),w,h,4);
  }
  {CompositeLayerSpec f1[2]={{explosionMiddle_bits,explosionMiddle_width,
                              explosionMiddle_height,hotterR,hotterG,hotterB},
                             {explosionCenter_bits,explosionCenter_width,
                              explosionCenter_height,hottestR,hottestG,hottestB}};
   CompositeFrameRGBA8 frame;
   compositeFrameStack(frame,w,h,f1,2);
   glFrames[1]=engine.createTextureFromBitmap(frame.rgba8.data(),w,h,4);
  }
  for(int i=2;i<4;++i)
   {CompositeLayerSpec f23[3]={{explosionEdge_bits,explosionEdge_width,
                                explosionEdge_height,hotR,hotG,hotB},
                               {explosionMiddle_bits,explosionMiddle_width,
                                explosionMiddle_height,hotterR,hotterG,hotterB},
                               {explosionCenter_bits,explosionCenter_width,
                                explosionCenter_height,hottestR,hottestG,hottestB}};
    CompositeFrameRGBA8 frame;
    compositeFrameStack(frame,w,h,f23,3);
    glFrames[i]=engine.createTextureFromBitmap(frame.rgba8.data(),w,h,4);
   }
  {CompositeLayerSpec f4[2]={{explosionMiddle_bits,explosionMiddle_width,
                              explosionMiddle_height,hotR,hotG,hotB},
                             {explosionCenter_bits,explosionCenter_width,
                              explosionCenter_height,hotterR,hotterG,hotterB}};
   CompositeFrameRGBA8 frame;
   compositeFrameStack(frame,w,h,f4,2);
   glFrames[4]=engine.createTextureFromBitmap(frame.rgba8.data(),w,h,4);
  }
  {std::vector<uint8_t> maskBytes;
   compositeMaskExpandR8(explosionCenter_bits,explosionCenter_width,
                         explosionCenter_height,maskBytes);
   clipMask[0]=engine.createTextureFromBitmap(maskBytes.data(),
                                              explosionCenter_width,
                                              explosionCenter_height,1);
  }
  {std::vector<uint8_t> maskBytes;
   compositeMaskExpandR8(explosionMiddle_bits,explosionMiddle_width,
                         explosionMiddle_height,maskBytes);
   clipMask[1]=engine.createTextureFromBitmap(maskBytes.data(),
                                              explosionMiddle_width,
                                              explosionMiddle_height,1);
  }
  {std::vector<uint8_t> maskBytes;
   compositeMaskExpandR8(explosionEdge_bits,explosionEdge_width,
                         explosionEdge_height,maskBytes);
   clipMask[2]=engine.createTextureFromBitmap(maskBytes.data(),
                                              explosionEdge_width,
                                              explosionEdge_height,1);
  }
  glFrameW=w;
  glFrameH=h;
  #endif
 }

inline ExplosionGraphic::~ExplosionGraphic()
  {
#ifndef X11_BACKEND
  for(int i=numFrames;i--;)
    engine.deleteTexture(glFrames[i]);
#endif
  for(int i=3;i--;)
    engine.deleteTexture(clipMask[i]);
  for(int i=numFrames;i--;)
    frames[i].~CompositePixmap();
  delete [] (char*)frames;
 }

#endif
