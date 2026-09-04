class Rock;
#include<stdlib.h>
#include<math.h>
#ifdef X11_BACKEND
#include<X11/Xlib.h>
#include"../../utilities/rendering/x11Backend.H"
#endif
#include"../../utilities/vector2d.H"
#include"../../utilities/pixmaps/rotated/rotatorDisplayData.H"
#include"../../utilities/pixmaps/rotated/rotator.H"
#include"spawner.H"
#include"../movableObject.H"
#include"../../utilities/intersection2d.H"
#include"../../gamePlay/stage.H"
#include"../ships/shipGroup.H"

#ifdef _CORP_LOGO_
#include"../../bitmaps/ROCKDecor1.xbm"
#include"../../bitmaps/ROCKDecor2.xbm"
#include"../../bitmaps/ROCKDecor3.xbm"
#else
#include"../../bitmaps/eightball.xbm"
#include"../../bitmaps/peace.xbm"
#include"../../bitmaps/yinyang.xbm"
#endif

#ifndef _ROCKGROUP_H
#define _ROCKGROUP_H

class Rock: public MovableObject
 {public:
    Rock(const int s, const double scale);
    void SetRock(const Vector2d& velocity,
                  MaskedRotVectorData* const, const double angularVelocity,
                  const int score);
    virtual Rotator& ObjectRotator();
    virtual void MissScript(Intersector&, const double createTime,
                                          const double existTime);
    virtual void HitScript(Intersector&, const double createTime,
                                         const double existTime,
                                         const Vector2d& intersectPoint);
    virtual void AlterMinVelocity(const double newMinVelocity);
    virtual void AlterMaxVelocity(const double newMaxVelocity)=0;
    virtual void AlterMaxAngularVelocity(const double newAngularVelocity);
    virtual void AlterVelocityLimits(const double newVelLimit);
    virtual void AlterFramesPerSec(const double frameRatio);
    virtual void AlterScore(const double scoreFactor);

#ifndef X11_BACKEND
    // Task-35 D2 composite path: the decor texture ids this rock draws with,
    // captured at SetRock from the owning group's init-time uploads (the
    // rotator's X11 pixmap/mask accessors abort on GL — GetMaskAtTime is
    // pixmap-based and NOT usable here).
    TextureId glContent=0,
              glMask=0;
    int glW=0,
        glH=0;
#endif

  protected:
    Rotator rotator;
    int score;
  };

class SpawningRock: public Rock
 {public:
    SpawningRock(const int score, const double scale, const int numSpawn);
    virtual void HitScript(Intersector&, const double createTime,
                                         const double existTime,
                                         const Vector2d& intersectPoint);
    virtual void AlterMinVelocity(const double newMinVelocity);
    virtual void AlterMaxVelocity(const double newMaxVelocity)=0;
    virtual void AlterMaxAngularVelocity(const double newAngularVelocity);
    virtual void AlterVelocityLimits(const double newVelLimit);
    virtual void AlterFramesPerSec(const double frameRatio);
    virtual void AlterScore(const double scoreFactor);

  protected:
    Spawner spawner;
 };

class RockGroup;
class ROCK: public SpawningRock
 {friend class RockGroup;
  public:
    ROCK();
    virtual void AlterMaxVelocity(const double newMaxVelocity);

    static const double scale;
    static double minVelocity,
                  maxVelocity,
                  maxAngularVelocity;
    static int score;
 };

class ROck: public SpawningRock
 {friend class RockGroup;
  public:
    ROck();
    virtual void AlterMaxVelocity(const double newMaxVelocity);

   static const double scale;

   static int score;
 };

class rock: public Rock
 {friend class RockGroup;
  public:
    rock();
    virtual void AlterMaxVelocity(const double newMaxVelocity);

    static const double scale;

    static int score;
 };

class RockGroup
 {public:
    typedef int Boolean;
    enum {off, on};

    ROCK ROCKs[16];
    ROck ROcks[32];
    rock rocks[128];

    RockGroup(RenderingEngine& engine);
    ~RockGroup();

    // TRANSITION member (D11 single-ownership-seam extension; DELETED at
    // task 47): non-owning reference to the engine injected at construction.
    // The migrated draw paths consume it; Rock reaches it through the global
    // group pointer, exactly as it does for the other shared group state.
    RenderingEngine& engine;

    inline void NewROCKs();

    inline void ResetMinVelocity(const double newMinVelocity);
    inline void ResetMaxVelocity(const double newMaxVelocity);
    inline void ResetMaxAngularVelocity(const double newAngularVelocity);
    inline void AdjustFramesPerSec(const double frameRatio);
    inline void ResetVelocityLimits(const double newVelLimit);
    inline void ResetScore(const double scoreFactor);

    static int initialROCKs,
               ROCKsPerLevel;
    static const int maxROCKs;

    static Boolean opaqueRocks;

#ifdef X11_BACKEND
    static XColor RockColor;
#endif

#ifndef X11_BACKEND
    // Task-35: the three decor bitmaps upload ONCE here (content RGBA8 in
    // RockColor + R8 coverage mask) at group init; rocks capture their ids
    // via GLDecorIds at SetRock.
    TextureId glDecorContent[3]={0,0,0},
              glDecorMask[3]={0,0,0};
    int glDecorW[3]={0,0,0},
        glDecorH[3]={0,0,0};
    inline void GLDecorIds(const unsigned char* bits,
                           TextureId& content,TextureId& mask,int& w,int& h)
      {for (int i=0;i<3;++i)
        {if (bits==ROCKDecor_bits[i])
          {content=glDecorContent[i];
           mask=glDecorMask[i];
           w=glDecorW[i];
           h=glDecorH[i];
           return;
          }
        }
        content=0;mask=0;w=0;h=0;
      }
#endif

  private:
    void SetVectorData(const Vector2d* const RockVecs, const int numvecs,
                       const int graphicIndex);

    static const Vector2d RockVectors1[8],
                          RockVectors2[8],
                          RockVectors3[11];

    static const unsigned char* const ROCKDecor_bits[3];
    static const int ROCKDecor_width[3],
                     ROCKDecor_height[3];

    MaskedRotVectorData* ROCKGraphic[3],
                       * ROckGraphic[3],
                       * rockGraphic[3];
 };

extern RockGroup* rockGroup;

#ifndef X11_BACKEND
// Task-36: the guarded rock pixmaps carry the RockColor wireframe outline
// under the decor; the GL legs draw it as a quantized-angle LINE_LOOP, so
// the components ride here as floats (the guarded static is X11-zone).
static const float glRockOutlineR=56026.0f/65535.0f,
                   glRockOutlineG=42405.0f/65535.0f,
                   glRockOutlineB=8224.0f/65535.0f;
#endif

#include"../../gamePlay/playingField.H"

inline Rock::Rock(const int s, const double scale): rotator(PlayingField::maxLinearVelocity/(4*scale)),
                                                    score(s)
 {
 }

inline void Rock::SetRock(const Vector2d& velocity,
                          MaskedRotVectorData* const graphic,
                          const double angularVelocity,
                          const int s)
 {rotator.SetRotator(graphic,
                     angularVelocity);
  SetMovableObject(Vector2d(),2*rotator.Radius(),2*rotator.Radius(),
		               velocity,PlayingField::maxLinearVelocity,Vector2d());
  score=s;
  #ifndef X11_BACKEND
  rockGroup->GLDecorIds(graphic->DecorBits(),glContent,glMask,glW,glH);
  #endif
 }

Rotator& Rock::ObjectRotator()
 {return rotator;
 }

void Rock::MissScript(Intersector& intersector, const double createTime,
                              const double existTime)
 {rotator.UpdateAngle(existTime);
  if (PlayingField::universalGravitationalConst)
    ObjectLiner().UpdateVelocity(existTime);
  if (rockGroup->opaqueRocks)
   {
    #ifdef X11_BACKEND
    int destX=CurrentBox().WrapMovingBox(PlayingField::playArea,
                                         ObjectLiner().Velocity())
                          .NorthWestCorner().x+.5,
        destY=CurrentBox().NorthWestCorner().y+.5;
    // The rotator owns pre-computed depth-1 clip bitmaps alongside its
    // pixmaps; GetMaskAtTime returns the same element the old clip-GC path
    // pointed at (identical time-index math), so the masked copy is
    // byte-identical to the pre-migration draw.
    playingField->DeferDrawMasked((TextureId)rotator.GetPixAtTime(0),
                                  (TextureId)rotator.GetMaskAtTime(0),
                                  destX,destY,
                                  rotator.PixWidth(),
                                  rotator.PixHeight());
    #endif
    #ifndef X11_BACKEND
    int destX=CurrentBox().WrapMovingBox(PlayingField::playArea,
                                         ObjectLiner().Velocity())
                          .NorthWestCorner().x+.5,
        destY=CurrentBox().NorthWestCorner().y+.5;
    // Task-36 outline leg: the guarded masked copy carries the RockColor
    // wireframe under the decor; GetVecsAtTime(0) is already quantized, so
    // the MVP is translation-only (ship outline idiom).
    playingField->DeferDrawOutline(
        destX+rotator.PixWidth()*.5f,
        destY+rotator.PixHeight()*.5f,
        0.0f,
        rotator.GetVecsAtTime(0),
        rotator.GetNumVecs(),
        glRockOutlineR,glRockOutlineG,glRockOutlineB);
    // D2 GL composite leg: GPU rotation about the rock center carries the
    // continuous angle; the decor quad is centered in local space so the
    // MVP reproduces the guarded pipeline's point-rotation math.
    playingField->DeferDrawTransformed(
        destX+rotator.PixWidth()*.5f,
        destY+rotator.PixHeight()*.5f,
        (float)rotator.Angle(),
        glContent,glMask,
        -glW*.5f,-glH*.5f,(float)glW,(float)glH);
    #endif
   }
  else
  #ifdef X11_BACKEND
   {playingField->DeferDraw((TextureId)rotator.GetPixAtTime(0),
                            CurrentBox().WrapMovingBox(PlayingField::playArea,
                                                       ObjectLiner().Velocity())
                                        .NorthWestCorner().x+.5,
                            CurrentBox().NorthWestCorner().y+.5,
                            rotator.PixWidth(),
                            rotator.PixHeight());
   }
  #else
   {int destX=CurrentBox().WrapMovingBox(PlayingField::playArea,
                                         ObjectLiner().Velocity())
                          .NorthWestCorner().x+.5,
        destY=CurrentBox().NorthWestCorner().y+.5;
    // Task-36 outline leg (non-opaque branch: the guarded plain copy also
    // carries the wireframe).
    playingField->DeferDrawOutline(
        destX+rotator.PixWidth()*.5f,
        destY+rotator.PixHeight()*.5f,
        0.0f,
        rotator.GetVecsAtTime(0),
        rotator.GetNumVecs(),
        glRockOutlineR,glRockOutlineG,glRockOutlineB);
    playingField->DeferDrawTransformed(
        destX+rotator.PixWidth()*.5f,
        destY+rotator.PixHeight()*.5f,
        (float)rotator.Angle(),
        glContent,0,
        -glW*.5f,-glH*.5f,(float)glW,(float)glH);
   }
  #endif
 }

void Rock::HitScript(Intersector& intersector, const double createTime,
                             const double existTime,
                             const Vector2d& intersectPoint)
 {intersector.RemoveSelfPermeable(this);
  if (playingField->lastHitFromAlly)
   {::score->CurrentScore()+=score;
    playingField->lastHitFromAlly=PlayingField::off;
   }
  else
    if (playingField->lastHitScore)
      playingField->lastHitScore=0;
    else
      playingField->lastHitScore=score;
 }

void Rock::AlterMinVelocity(const double newMinVelocity)
 {if (ObjectLiner().VelocityMagnitude()<newMinVelocity)
    ObjectLiner().Velocity()=newMinVelocity*ObjectLiner().Velocity()/ObjectLiner().VelocityMagnitude();
 }

void Rock::AlterMaxAngularVelocity(const double newMaxAngularVelocity)
 {if (fabs(ObjectRotator().AngularVelocity())>newMaxAngularVelocity)
    ObjectRotator().AngularVelocity()=ObjectRotator().AngularVelocity()<0 ? -newMaxAngularVelocity
                                                                          :  newMaxAngularVelocity;
 }

void Rock::AlterVelocityLimits(const double newVelLimit)
 {ObjectLiner().MaxVelocityMagnitude()=
  ObjectLiner().MaxAccelerationMagnitude()=newVelLimit;
  ObjectRotator().UpdateMaxLinearVelocity(PlayingField::maxLinearVelocity);
 }

void Rock::AlterFramesPerSec(const double frameRatio)
 {ObjectLiner().Velocity()=ObjectLiner().Velocity()*frameRatio;
  ObjectLiner().Acceleration()=ObjectLiner().Acceleration()*frameRatio*frameRatio;
  ObjectRotator().AngularVelocity()=ObjectRotator().AngularVelocity()*frameRatio;
 }

void Rock::AlterScore(const double scoreFactor)
 {if (!(score*=scoreFactor))
    score=1;
 }

inline SpawningRock::SpawningRock(const int score,
                                     const double scale,
                                     const int numSpawn): Rock(score,scale),
                                                          spawner(numSpawn)
 {
 }

void SpawningRock::HitScript(Intersector& intersector, const double createTime,
                             const double existTime,
                             const Vector2d& intersectPoint)
 {intersector.RemoveSelfPermeable(this);
  {for (int i=0; i<spawner.GetNumSpawn(); ++i)
     intersector.AddSelfPermeable(spawner.GetSpawnWithInheritance(*this,
                                                                  existTime,i),
                                  createTime+existTime);
  }
  if (playingField->lastHitFromAlly)
   {::score->CurrentScore()+=score;
    playingField->lastHitFromAlly=PlayingField::off;
   }
  else
    if (playingField->lastHitScore)
      playingField->lastHitScore=0;
    else
      playingField->lastHitScore=score;
 }

void SpawningRock::AlterMinVelocity(const double newMinVelocity)
 {if (ObjectLiner().VelocityMagnitude()<newMinVelocity)
    ObjectLiner().Velocity()=newMinVelocity*ObjectLiner().Velocity()/ObjectLiner().VelocityMagnitude();
  for (int i=0;i<spawner.GetNumSpawn();++i)
    spawner[i]->AlterMinVelocity(newMinVelocity);
 }

void SpawningRock::AlterMaxAngularVelocity(const double newMaxAngularVelocity)
 {if (fabs(ObjectRotator().AngularVelocity())>newMaxAngularVelocity)
    ObjectRotator().AngularVelocity()=ObjectRotator().AngularVelocity()<0 ? -newMaxAngularVelocity
                                                                          :  newMaxAngularVelocity;
  for (int i=0;i<spawner.GetNumSpawn();++i)
    spawner[i]->AlterMaxAngularVelocity(newMaxAngularVelocity);
 }

void SpawningRock::AlterVelocityLimits(const double newVelLimit)
 {ObjectLiner().MaxVelocityMagnitude()=
  ObjectLiner().MaxAccelerationMagnitude()=newVelLimit;
  ObjectRotator().UpdateMaxLinearVelocity(PlayingField::maxLinearVelocity);
  for (int i=0;i<spawner.GetNumSpawn();++i)
    spawner[i]->AlterVelocityLimits(newVelLimit);
 }

void SpawningRock::AlterFramesPerSec(const double frameRatio)
 {ObjectLiner().Velocity()=ObjectLiner().Velocity()*frameRatio;
  ObjectLiner().Acceleration()=ObjectLiner().Acceleration()*frameRatio*frameRatio;
  ObjectRotator().AngularVelocity()=ObjectRotator().AngularVelocity()*frameRatio;
  for (int i=0;i<spawner.GetNumSpawn();++i)
    spawner[i]->AlterFramesPerSec(frameRatio);
 }

void SpawningRock::AlterScore(const double scoreFactor)
 {if (!(score*=scoreFactor))
    score=1;
  for (int i=0;i<spawner.GetNumSpawn();++i)
    spawner[i]->AlterScore(scoreFactor);
 }

inline int ROCK::score=10;

inline const double ROCK::scale=40;

inline double ROCK::minVelocity=.07*PlayingField::maxLinearVelocity,
       ROCK::maxVelocity=.13*PlayingField::maxLinearVelocity,
       ROCK::maxAngularVelocity=PlayingField::maxLinearVelocity/(4*ROCK::scale);

inline ROCK::ROCK(): SpawningRock(score,scale,2)
 {
 }

void ROCK::AlterMaxVelocity(const double newMaxVelocity)
 {if (ObjectLiner().VelocityMagnitude()>newMaxVelocity)
    ObjectLiner().Velocity()=newMaxVelocity*ObjectLiner().Velocity()/ObjectLiner().VelocityMagnitude();
  for (int i=0;i<spawner.GetNumSpawn();++i)
    spawner[i]->AlterMaxVelocity(newMaxVelocity);
 }

inline int ROck::score=20;

inline const double ROck::scale=sqrt(ROCK::scale*ROCK::scale/2);

inline ROck::ROck(): SpawningRock(score,scale,4)
 {
 }

void ROck::AlterMaxVelocity(const double newMaxVelocity)
 {if (ObjectLiner().VelocityMagnitude()>2*newMaxVelocity)
    ObjectLiner().Velocity()=2*newMaxVelocity*ObjectLiner().Velocity()/ObjectLiner().VelocityMagnitude();
  for (int i=0;i<spawner.GetNumSpawn();++i)
    spawner[i]->AlterMaxVelocity(newMaxVelocity);
 }

inline int rock::score=50;

inline const double rock::scale=sqrt(ROck::scale*ROck::scale/4);

inline rock::rock(): Rock(score,scale)
 {
 }

void rock::AlterMaxVelocity(const double newMaxVelocity)
 {if (ObjectLiner().VelocityMagnitude()>4*newMaxVelocity)
    ObjectLiner().Velocity()=4*newMaxVelocity*ObjectLiner().Velocity()/ObjectLiner().VelocityMagnitude();
 }

inline int RockGroup::initialROCKs=4,
    RockGroup::ROCKsPerLevel=2;

inline const int RockGroup::maxROCKs=16;

inline RockGroup::Boolean RockGroup::opaqueRocks=RockGroup::on;

inline const Vector2d RockGroup::RockVectors1[8]={Vector2d(-1,-28)/32,
                                           Vector2d(16,-15)/32,
                                           Vector2d(28,-14)/32,
                                           Vector2d(25,20)/32,
                                           Vector2d(8,12)/32,
                                           Vector2d(-8,30)/32,
                                           Vector2d(-30,11)/32,
                                           Vector2d(-22,-23)/32},
               RockGroup::RockVectors2[8]={Vector2d(0,-32)/32,
                                           Vector2d(29,-13)/32,
                                           Vector2d(31,6)/32,
                                           Vector2d(17,27)/32,
                                           Vector2d(2,21)/32,
                                           Vector2d(-22,23)/32,
                                           Vector2d(-31,-4)/32,
                                           Vector2d(-21,-24)/32},
               RockGroup::RockVectors3[11]={Vector2d(-6,-37)/40,
                                            Vector2d(4,-34)/40,
                                            Vector2d(12,-23)/40,
                                            Vector2d(39,-8)/40,
                                            Vector2d(39,2)/40,
                                            Vector2d(31,26)/40,
                                            Vector2d(5,34)/40,
                                            Vector2d(-18,30)/40,
                                            Vector2d(-39,6)/40,
                                            Vector2d(-39,-6)/40,
                                            Vector2d(-28,-29)/40};

#ifdef _CORP_LOGO_
inline const unsigned char* const RockGroup::ROCKDecor_bits[3]={ROCKDecor1_bits,ROCKDecor2_bits,ROCKDecor3_bits};
inline const int RockGroup::ROCKDecor_width[3]={ROCKDecor1_width,ROCKDecor2_width,ROCKDecor3_width},
          RockGroup::ROCKDecor_height[3]={ROCKDecor1_height,ROCKDecor2_height,ROCKDecor3_height};
#else
inline const unsigned char* const RockGroup::ROCKDecor_bits[3]={eightball_bits,peace_bits,yinyang_bits};
inline const int RockGroup::ROCKDecor_width[3]={eightball_width,peace_width,yinyang_width},
          RockGroup::ROCKDecor_height[3]={eightball_height,peace_height,yinyang_height};
#endif

#ifdef X11_BACKEND
inline XColor RockGroup::RockColor={0,
                             56026,//218
                             42405,//165
                             8224,//32
                             DoRed|DoGreen|DoBlue};
#endif

inline void RockGroup::SetVectorData(const Vector2d* const RockVecs, const int numvecs,
                                 const int graphicIndex)
  {
   #ifdef X11_BACKEND
   // Task 47: RockColor.pixel feeds options.H's scoring-icon foregrounds, so
   // this leg allocates it here — the same request the D14 ctor issued when
   // the color rode in as an XColor reference.
   if (!RockColor.pixel)
     RockColor.pixel=static_cast<X11Backend&>(engine).allocateColor(
         56026.0f/65535.0f,42405.0f/65535.0f,8224.0f/65535.0f);
   Vector2d* scaledROCK=new Vector2d[numvecs],
            * scaledROck=new Vector2d[numvecs],
            * scaledrock=new Vector2d[numvecs];
   for(int i=0;i<numvecs;++i)
    {scaledROCK[i]=RockVecs[i]*ROCK::scale;
     scaledROck[i]=RockVecs[i]*ROck::scale;
     scaledrock[i]=RockVecs[i]*rock::scale;
    }
   ROCKGraphic[graphicIndex]=new MaskedRotVectorData(engine,RotColor{RockColor.red,RockColor.green,RockColor.blue},
                                                     scaledROCK,numvecs,
                                                     ROCKDecor_bits[graphicIndex],
                                                     ROCKDecor_width[graphicIndex],
                                                     ROCKDecor_height[graphicIndex]);
   ROckGraphic[graphicIndex]=new MaskedRotVectorData(engine,RotColor{RockColor.red,RockColor.green,RockColor.blue},
                                                     scaledROck,numvecs);
   rockGraphic[graphicIndex]=new MaskedRotVectorData(engine,RotColor{RockColor.red,RockColor.green,RockColor.blue},
                                                     scaledrock,numvecs);
   delete [] scaledrock;
   delete [] scaledROck;
   delete [] scaledROCK;
   #endif
   #ifndef X11_BACKEND
   // Task 35: the task-27 engine branches of the MaskedRotVectorData
   // constructors capture the same geometry+decor data server-free, so the
   // nine graphics must be built here too — NewROCKs hands these pointers
   // to SetRock, and a null graphic is fatal.
   Vector2d* scaledROCK=new Vector2d[numvecs],
            * scaledROck=new Vector2d[numvecs],
            * scaledrock=new Vector2d[numvecs];
   for(int i=0;i<numvecs;++i)
    {scaledROCK[i]=RockVecs[i]*ROCK::scale;
     scaledROck[i]=RockVecs[i]*ROck::scale;
     scaledrock[i]=RockVecs[i]*rock::scale;
    }
   const RotColor rockColor={56026,//218
                             42405,//165
                             8224//32
                             };
   ROCKGraphic[graphicIndex]=new MaskedRotVectorData(engine,rockColor,
                                                     scaledROCK,numvecs,
                                                     ROCKDecor_bits[graphicIndex],
                                                     ROCKDecor_width[graphicIndex],
                                                     ROCKDecor_height[graphicIndex]);
   ROckGraphic[graphicIndex]=new MaskedRotVectorData(engine,rockColor,
                                                     scaledROck,numvecs);
   rockGraphic[graphicIndex]=new MaskedRotVectorData(engine,rockColor,
                                                     scaledrock,numvecs);
   delete [] scaledrock;
   delete [] scaledROck;
   delete [] scaledROCK;
   #endif
  }

inline RockGroup::RockGroup(RenderingEngine& eng): engine(eng)
  {
   SetVectorData(RockVectors1,sizeof(RockVectors1)/sizeof(Vector2d),0);
   SetVectorData(RockVectors2,sizeof(RockVectors2)/sizeof(Vector2d),1);
   SetVectorData(RockVectors3,sizeof(RockVectors3)/sizeof(Vector2d),2);
   #ifndef X11_BACKEND
   // Decor uploads (content tinted with the guarded RockColor components;
   // mask = unpacked R8 coverage of the same bits).
   const unsigned short rockRed=56026,
                        rockGreen=42405,
                        rockBlue=8224;
   for (int i=0;i<3;++i)
    {const int w=ROCKDecor_width[i],
               h=ROCKDecor_height[i];
     CompositeLayerSpec layer={ROCKDecor_bits[i],w,h,rockRed,rockGreen,rockBlue};
     CompositeFrameRGBA8 frame;
     compositeFrameStack(frame,w,h,&layer,1);
     glDecorContent[i]=engine.createTextureFromBitmap(frame.rgba8.data(),w,h,4);
     std::vector<uint8_t> maskBytes;
     compositeMaskExpandR8(ROCKDecor_bits[i],w,h,maskBytes);
     glDecorMask[i]=engine.createTextureFromBitmap(maskBytes.data(),w,h,1);
     glDecorW[i]=w;
     glDecorH[i]=h;
    }
   #endif
  }


inline RockGroup::~RockGroup()
 {for(int i=0;i<3;++i)
   {delete rockGraphic[i];
    delete ROckGraphic[i];
    delete ROCKGraphic[i];
   }
 }

inline void RockGroup::NewROCKs()
 {int numROCKs=ROCKsPerLevel*(playingField->level-1)+initialROCKs;
  if (numROCKs>maxROCKs)
    numROCKs=maxROCKs;
  for(int i=0;i<numROCKs;++i)
   {double ROCKAngularVelocity=2*ROCK::maxAngularVelocity*gary_rand::rand_16()/RAND_MAX_16
                               -ROCK::maxAngularVelocity,
    ROCKVelocityMagnitude=ROCK::maxVelocity*gary_rand::rand_16()/RAND_MAX_16;
    if (ROCKVelocityMagnitude<ROCK::minVelocity)
      ROCKVelocityMagnitude=ROCK::minVelocity;
    Vector2d ROCKVelocity(gary_rand::rand_16()-RAND_MAX_16/2,gary_rand::rand_16()-RAND_MAX_16/2);
    ROCKVelocity*=ROCKVelocityMagnitude/ROCKVelocity.Magnitude();
    double ROckMaxAngularVelocity=2*ROCKAngularVelocity,
           ROckMaxVelocityMagnitude=2*ROCKVelocityMagnitude;
     for(int j=0,
            ROckIndex=i*sizeof(ROcks)*sizeof(ROCK)/(sizeof(ROck)*sizeof(ROCKs));
            j<(int)(sizeof(ROcks)*sizeof(ROCK)/(sizeof(ROck)*sizeof(ROCKs)));
            ++ROckIndex,++j)
     {double ROckAngularVelocity,
             ROckAVSum=0,
             ROckVelocityMagnitude;
      Vector2d ROckVelocity(gary_rand::rand_16()-RAND_MAX_16/2,gary_rand::rand_16()-RAND_MAX_16/2),
               ROckVMSum;
       if (j!=(int)(sizeof(ROcks)*sizeof(ROCK)/(sizeof(ROck)*sizeof(ROCKs))))
       {ROckAngularVelocity=2*ROckMaxAngularVelocity*gary_rand::rand_16()/RAND_MAX_16
                            -ROckMaxAngularVelocity;
        ROckAVSum+=ROckAngularVelocity;
        ROckVelocityMagnitude=ROckMaxVelocityMagnitude*gary_rand::rand_16()/RAND_MAX_16;
        if (ROckVelocityMagnitude<ROCK::minVelocity)
          ROckVelocityMagnitude=ROCK::minVelocity;
        ROckVelocity*=ROckVelocityMagnitude/ROckVelocity.Magnitude();
        ROckVMSum+=ROckVelocity;
       }
      else
       {ROckAngularVelocity=ROCKAngularVelocity
                            +.2*ROCKAngularVelocity*gary_rand::rand_16()/RAND_MAX_16-.1*ROCKAngularVelocity
                            -ROckAVSum;
        Vector2d randomVec(gary_rand::rand_16()-RAND_MAX_16/2,gary_rand::rand_16()-RAND_MAX_16/2);
        ROckVelocity=ROCKVelocity
                     +.1*ROCKVelocityMagnitude*gary_rand::rand_16()*randomVec/(randomVec.Magnitude()*RAND_MAX_16)
                     -ROckVMSum;
        if ((ROckVelocityMagnitude=ROckVelocity.Magnitude())<ROCK::minVelocity)
          ROckVelocity*=ROCK::minVelocity/ROckVelocityMagnitude;
       }
      double rockMaxAngularVelocity=4*ROckAngularVelocity,
             rockMaxVelocityMagnitude=2*ROckVelocityMagnitude;
       for(int k=0,
               rockIndex=ROckIndex*sizeof(rocks)*sizeof(ROck)/(sizeof(rock)*sizeof(ROcks));
           k<(int)(sizeof(rocks)*sizeof(ROck)/(sizeof(rock)*sizeof(ROcks)));
           ++rockIndex,++k)
       {double rockAngularVelocity,
               rockAVSum=0,
               rockVelocityMagnitude;
        Vector2d rockVelocity(gary_rand::rand_16()-RAND_MAX_16/2,gary_rand::rand_16()-RAND_MAX_16/2),
                 rockVMSum;
         if (k!=(int)(sizeof(rocks)*sizeof(ROck)/(sizeof(rock)*sizeof(ROcks))))
         {rockAngularVelocity=2*rockMaxAngularVelocity*gary_rand::rand_16()/RAND_MAX_16
                              -rockMaxAngularVelocity;
          rockAVSum+=rockAngularVelocity;
          rockVelocityMagnitude=rockMaxVelocityMagnitude*gary_rand::rand_16()/RAND_MAX_16;
          if (rockVelocityMagnitude<ROCK::minVelocity)
            rockVelocityMagnitude=ROCK::minVelocity;
          rockVelocity*=rockVelocityMagnitude/rockVelocity.Magnitude();
          rockVMSum+=rockVelocity;
         }
        else
         {rockAngularVelocity=ROckAngularVelocity
                              +.2*ROckAngularVelocity*gary_rand::rand_16()/RAND_MAX_16-.1*ROckAngularVelocity
                              -rockAVSum;
          Vector2d randomVec(gary_rand::rand_16()-RAND_MAX_16/2,gary_rand::rand_16()-RAND_MAX_16/2);
          rockVelocity=ROckVelocity
                       +.1*ROckVelocityMagnitude*gary_rand::rand_16()*randomVec/(randomVec.Magnitude()*RAND_MAX_16)
                       -rockVMSum;
          if ((rockVelocityMagnitude=rockVelocity.Magnitude())<ROCK::minVelocity)
            rockVelocity*=ROCK::minVelocity/rockVelocityMagnitude;
         }
        rocks[rockIndex].SetRock(rockVelocity,
                                 rockGraphic[3*gary_rand::rand_16()/(RAND_MAX_16+1)],
                                 rockAngularVelocity,
	                               rock::score);
        ROcks[ROckIndex].spawner[k]=&rocks[rockIndex];
       }
      ROcks[ROckIndex].SetRock(ROckVelocity,
                               ROckGraphic[3*gary_rand::rand_16()/(RAND_MAX_16+1)],
                               ROckAngularVelocity,
	                             ROck::score);
      ROCKs[i].spawner[j]=&ROcks[ROckIndex];
     }
    ROCKs[i].SetRock(ROCKVelocity,
                     ROCKGraphic[3*gary_rand::rand_16()/(RAND_MAX_16+1)],
                     ROCKAngularVelocity,
	                   ROCK::score);
   }
  double ROCKPosWidth=PlayingField::playArea.Width()-2*ROCK::scale,
         ROCKPosHeight=PlayingField::playArea.Height()-2*ROCK::scale;
  for(int i=0;i<numROCKs;++i)
    {do
       ROCKs[i].CurrentBox().MoveBox((2*gary_rand::rand_16()/(RAND_MAX_16+1)
                                     ? Vector2d(ROCKPosWidth*(2*gary_rand::rand_16()/(RAND_MAX_16+1))+ROCK::scale,
                                                ROCKPosHeight*gary_rand::rand_16()/(RAND_MAX_16+1)+ROCK::scale)
                                     : Vector2d(ROCKPosWidth*gary_rand::rand_16()/(RAND_MAX_16+1)+ROCK::scale,
                                                ROCKPosHeight*(2*gary_rand::rand_16()/(RAND_MAX_16+1))+ROCK::scale))
                                     +PlayingField::playArea.NorthWestCorner()
                                     -ROCKs[i].CurrentBox().Center());
     while(!shipGroup->hyper.hyperspacing&&Intersect(ROCKs[i].CurrentBox(),shipGroup->ship->CurrentBox()));
     playingField->selfPermeableList.SetCurrentAtHole().GetData()=&ROCKs[i];
     playingField->selfPermeableList.SetCurrentAtHead().AddPrior();
     #ifdef X11_BACKEND
     playingField->DeferDraw((TextureId)ROCKs[i].ObjectRotator().GetPixAtTime(0),
                             ROCKs[i].CurrentBox().NorthWestCorner().x+.5,
                             ROCKs[i].CurrentBox().NorthWestCorner().y+.5,
                             ROCKs[i].ObjectRotator().PixWidth(),
                             ROCKs[i].ObjectRotator().PixHeight());
     #endif
     #ifndef X11_BACKEND
     {const float centerX=(float)(ROCKs[i].CurrentBox().NorthWestCorner().x+.5)
                          +ROCKs[i].ObjectRotator().PixWidth()*.5f,
                 centerY=(float)(ROCKs[i].CurrentBox().NorthWestCorner().y+.5)
                          +ROCKs[i].ObjectRotator().PixHeight()*.5f;
      // Task-36 outline leg: the spawn-placement blit carries the wireframe.
      playingField->DeferDrawOutline(
          centerX,centerY,
          0.0f,
          ROCKs[i].ObjectRotator().GetVecsAtTime(0),
          ROCKs[i].ObjectRotator().GetNumVecs(),
          glRockOutlineR,glRockOutlineG,glRockOutlineB);
      playingField->DeferDrawTransformed(
          centerX,centerY,
          (float)ROCKs[i].ObjectRotator().Angle(),
          ROCKs[i].glContent,rockGroup->opaqueRocks ? ROCKs[i].glMask : 0,
          -ROCKs[i].glW*.5f,-ROCKs[i].glH*.5f,
          (float)ROCKs[i].glW,(float)ROCKs[i].glH);
     }
     #endif
    }
  }

void RockGroup::ResetMinVelocity(const double newMinVelocity)
 {playingField->selfPermeableList.SetCurrentAtHead();
  for (int i=0;i<playingField->selfPermeableList.GetNumItems();++i)
   {((Rock*)playingField->selfPermeableList.GetData())->AlterMinVelocity(newMinVelocity);
    playingField->selfPermeableList.Next();
   }
 }

void RockGroup::ResetMaxVelocity(const double newMaxVelocity)
 {playingField->selfPermeableList.SetCurrentAtHead();
  for (int i=0;i<playingField->selfPermeableList.GetNumItems();++i)
   {((Rock*)playingField->selfPermeableList.GetData())->AlterMaxVelocity(newMaxVelocity);
    playingField->selfPermeableList.Next();
   }
 }

void RockGroup::ResetMaxAngularVelocity(const double newMaxAngularVelocity)
 {playingField->selfPermeableList.SetCurrentAtHead();
  for (int i=0;i<playingField->selfPermeableList.GetNumItems();++i)
   {((Rock*)playingField->selfPermeableList.GetData())->AlterMaxAngularVelocity(newMaxAngularVelocity);
    playingField->selfPermeableList.Next();
   }
 }

void RockGroup::ResetVelocityLimits(const double newVelLimit)
 {playingField->selfPermeableList.SetCurrentAtHead();
  for (int i=0;i<playingField->selfPermeableList.GetNumItems();++i)
   {((Rock*)playingField->selfPermeableList.GetData())->AlterVelocityLimits(newVelLimit);
    playingField->selfPermeableList.Next();
   }
 }

void RockGroup::AdjustFramesPerSec(const double frameRatio)
 {playingField->selfPermeableList.SetCurrentAtHead();
  for (int i=0;i<playingField->selfPermeableList.GetNumItems();++i)
   {((Rock*)playingField->selfPermeableList.GetData())->AlterFramesPerSec(frameRatio);
    playingField->selfPermeableList.Next();
   }
 }

void RockGroup::ResetScore(const double scoreFactor)
 {playingField->selfPermeableList.SetCurrentAtHead();
  for (int i=0;i<playingField->selfPermeableList.GetNumItems();++i)
   {((Rock*)playingField->selfPermeableList.GetData())->AlterScore(scoreFactor);
    playingField->selfPermeableList.Next();
   }
 }

#endif
