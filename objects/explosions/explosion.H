#ifndef _EXPLOSION_H
#define _EXPLOSION_H

#ifdef X11_BACKEND
#include<X11/Xlib.h>
#endif
#include"../../utilities/rendering/x11types.H"
#include"../../utilities/vector2d.H"
#include"../../utilities/box.H"
#include"../../utilities/frames/frameList.H"
#include"../../utilities/frames/frameTimer.H"
#include"../movableObject.H"
#include"../../utilities/intersection2d.H"
#include"../explosions/explosionGraphic.H"

class Explosion: public MovableObject
 {public:
    inline Explosion();

    virtual void MissScript(Intersector&, const double createTime,
			                                    const double existTime);
    FrameList frameList;
    FrameTimer frameTimer,
               frameDelay;

  protected:
    inline void DisplayFrame();

 };

#include"../../gamePlay/playingField.H"

inline Explosion::Explosion(): frameList(explosionGraphic->frames,
                                         ExplosionGraphic::numFrames,
                                         ExplosionGraphic::numFrames),
		                           frameTimer(ExplosionGraphic::numFrames),
                               frameDelay(1)
 {SetMovableObject(Vector2d(),frameList.PixWidth(),frameList.PixHeight(),
		               Vector2d(),PlayingField::maxLinearVelocity,Vector2d());
 }

inline void Explosion::DisplayFrame()
 {
  #ifdef X11_BACKEND
  int destX=CurrentBox().WrapMovingBox(PlayingField::playArea,
                                       ObjectLiner().Velocity())
                        .NorthWestCorner().x+.5,
      destY=CurrentBox().NorthWestCorner().y+.5;
  // One masked-draw engine call per layer; every one of the 5 frames is
  // masked (each original gc[] carried a clip mask), so there is no plain
  // drawTexture leg here. The clip origin rides inside the call (D6).
  // Bracketed like every world-sprite draw: the raw path copied onto the
  // play-field canvas, and an unbracketed call would land on the standing
  // window target instead (W6-hotfix).
  explosionGraphic->engine.beginRenderTo((TextureId)playingField->pixmap);
  explosionGraphic->engine.drawTextureMasked(frameList.GetFrame(),
                                             explosionGraphic->MaskId(frameList.GetFrameNumber()),
                                             destX,destY,
                                             frameList.PixWidth(),
                                             frameList.PixHeight());
  explosionGraphic->engine.endRenderTo();
  #endif
 }

void Explosion::MissScript(Intersector& intersector, const double createTime,
                                                     const double existTime)
 {if (frameDelay.TimerAlarm()||frameList.GetFirstFrameNumber()==frameList.GetFrameNumber())
   {frameDelay.Reset();
    if (frameTimer.TimerAlarm())
      intersector.RemovePermeable(this);
    else
     {frameList.UpdateFrameNumber();
      DisplayFrame();
     }
   }
  else
    DisplayFrame();
 }

#endif
