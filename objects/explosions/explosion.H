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
  {SetMovableObject(Vector2d(),
                    #ifdef X11_BACKEND
                    frameList.PixWidth(),
                    frameList.PixHeight(),
                    #else
                    explosionGraphic->FrameWidth(),
                    explosionGraphic->FrameHeight(),
                    #endif
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
   // Deferred like every world-sprite draw (task 24): the record replays
   // onto the freshly cleared canvas in original emission order.
   playingField->DeferDrawMasked(frameList.GetFrame(),
                                 explosionGraphic->MaskId(frameList.GetFrameNumber()),
                                 destX,destY,
                                 frameList.PixWidth(),
                                 frameList.PixHeight());
   #endif
   #ifndef X11_BACKEND
   // Task-36: same masked draw with the CPU-composited frame textures; the
   // frameList dims are not readable on this leg (the CompositePixmap storage
   // is never constructed guards-closed), so the graphic's own dims feed w/h.
   int destX=CurrentBox().WrapMovingBox(PlayingField::playArea,
                                        ObjectLiner().Velocity())
                       .NorthWestCorner().x+.5,
       destY=CurrentBox().NorthWestCorner().y+.5;
   playingField->DeferDrawMasked(explosionGraphic->FrameId(frameList.GetFrameNumber()),
                                 explosionGraphic->MaskId(frameList.GetFrameNumber()),
                                 destX,destY,
                                 (float)explosionGraphic->FrameWidth(),
                                 (float)explosionGraphic->FrameHeight());
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
