#include"explosions/explosion.H"

#ifndef _BULLET_H
#define _BULLET_H

#include"../utilities/vector2d.H"
#include"../utilities/box.H"
#include"../utilities/pixmaps/rotated/rotator.H"
#include"../utilities/frames/frameList.H"
#include"../utilities/frames/frameTimer.H"
#ifdef X11_BACKEND
#include<X11/Xlib.h>
#endif
#include"../utilities/rendering/x11types.H"
#include"movableObject.H"
#include"../utilities/intersection2d.H"

class BulletExplosion;
class Bullet: public MovableObject
 {friend class BulletExplosion;
  public:
    inline Bullet(Rotator* const,
                  const int numFrames,
                  BulletExplosion* const);
    virtual Rotator& ObjectRotator();
    virtual void MissScript(Intersector&, const double createTime,
                                          const double existTime);
    virtual void HitScript(Intersector&, const double createTime,
                                         const double existTime,
                                         const Vector2d& intersectPoint)=0;

    FrameTimer frameTimer;

  protected:
    typedef int Boolean;
    enum {off, on};
    Rotator* rotator;
    Boolean inactive;
    BulletExplosion* explosion;
 };

class BulletExplosion: public Explosion
 {public:
    inline BulletExplosion(Bullet* const);

    virtual void MissScript(Intersector& intersector, const double createTime,
                    	                                const double existTime);
  private:
    Bullet* bullet;

    inline void DisplayFrame();
 };

#include "../gamePlay/playingField.H"

inline Bullet::Bullet(Rotator* const rot,
                       const int numFrames,
                       BulletExplosion* const exp): MovableObject(Vector2d(),
                                                                  2*rot->Radius(),
                                                                  2*rot->Radius(),
         	                                                        Vector2d(),
								                                                  PlayingField::maxLinearVelocity,
                                                                  Vector2d()),
                                                    rotator(rot),
                                                    frameTimer(numFrames),
                                                    inactive(on),
                                                    explosion(exp)
 {
 }

Rotator& Bullet::ObjectRotator()
 {return *rotator;
 }

void Bullet::MissScript(Intersector& intersector, const double createTime,
                                                  const double existTime)
 {if (frameTimer.TimerAlarm())
   {intersector.RemoveNonPermeable(this);
    inactive=on;
   }
  else
   {if (PlayingField::universalGravitationalConst)
      ObjectLiner().UpdateVelocity(existTime);
    #ifdef X11_BACKEND
    explosionGraphic->engine.beginRenderTo((TextureId)playingField->pixmap);
    explosionGraphic->engine.drawTexture((TextureId)rotator->GetPixAtTime(0),
                                   CurrentBox().WrapMovingBox(PlayingField::playArea,
                                                              ObjectLiner().Velocity())
                                               .NorthWestCorner().x+.5,
                                   CurrentBox().NorthWestCorner().y+.5,
                                   rotator->PixWidth(),
                                   rotator->PixHeight(),
                                   1.0f);
    explosionGraphic->engine.endRenderTo();
    #endif
   }
 }

inline BulletExplosion::BulletExplosion(Bullet* const b): bullet(b)
 {
 }

inline void BulletExplosion::DisplayFrame()
 {
  #ifdef X11_BACKEND
  int destX=CurrentBox().NorthWestCorner().x+.5,
      destY=CurrentBox().NorthWestCorner().y+.5;
  // Per-frame clip-origin + masked copy absorbed into the engine's masked
  // draw (D6); MaskId maps frame number to its original per-frame mask.
  explosionGraphic->engine.beginRenderTo((TextureId)playingField->pixmap);
  explosionGraphic->engine.drawTextureMasked(frameList.GetFrame(),
                                   explosionGraphic->MaskId(frameList.GetFrameNumber()),
                                   destX,destY,
                                   frameList.PixWidth(),
                                   frameList.PixHeight());
  explosionGraphic->engine.endRenderTo();
  #endif
 }

void BulletExplosion::MissScript(Intersector& intersector, const double createTime,
                   	                                   const double existTime)
 {if (frameDelay.TimerAlarm()||frameList.GetFirstFrameNumber()==frameList.GetFrameNumber())
   {frameDelay.Reset();
    if (frameTimer.TimerAlarm())
     {intersector.RemovePermeable(this);
      bullet->inactive=Bullet::on;
     }
    else
     {frameList.UpdateFrameNumber();
      DisplayFrame();
     }
   }
  else
    DisplayFrame();
 }

#endif
