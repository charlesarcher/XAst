#include<stdlib.h>
#ifdef X11_BACKEND
#include<X11/Xlib.h>
#endif
#include"../../utilities/rendering/x11types.H"
#include"../../utilities/rendering/renderingEngine.H"
#include"../../utilities/vector2d.H"
#include"../../utilities/box.H"
#include"../../utilities/pixmaps/composite/compositePixmap.H"
#include"../../utilities/pixmaps/rotated/rotatorDisplayData.H"
#include"../../utilities/pixmaps/rotated/rotator.H"
#include"../../utilities/frames/frameList.H"
#include"../../utilities/frames/frameTimer.H"
#include"../movableObject.H"
#include"../../utilities/intersection2d.H"
#include"../explosions/explosion.H"
#include"shipBulletGroup.H"
#include"../../gamePlay/stage.H"
#include"../../gamePlay/options/options.H"

#ifdef _CORP_LOGO_
#include"../../bitmaps/starDestroyerDecor.xbm"
#else
#include"../../bitmaps/fortytwo.xbm"
#endif

#include"../../bitmaps/starDestroyerThrustEdge.xbm"
#include"../../bitmaps/starDestroyerThrustMiddle.xbm"
#include"../../bitmaps/starDestroyerThrustCenter.xbm"
#include"../../bitmaps/starDestroyerIcon.xbm"
#include"../../bitmaps/NCC1701DDecorBottom.xbm"
#include"../../bitmaps/NCC1701DDecorTop.xbm"
#include"../../bitmaps/NCC1701DThrustDecor.xbm"
#include"../../bitmaps/NCC1701DIcon.xbm"
#include"../../bitmaps/NCC1701ADecor.xbm"
#include"../../bitmaps/NCC1701AThrustDecor.xbm"
#include"../../bitmaps/NCC1701AIcon.xbm"
#ifndef X11_BACKEND
#include<vector>
#include"../glSprites.H"
#endif

#ifndef _SHIPGROUP_H
#define _SHIPGROUP_H

class Ship: public MovableObject
 {public:
    typedef int Boolean;
    enum {off, on};

#ifdef X11_BACKEND
    Ship(const unsigned char* const icon,
         const int iconWidth, const int iconHeight,
         XColor& iconColor);
#else
    Ship(const unsigned char* const icon,
         const int iconWidth, const int iconHeight);
#endif
    virtual ~Ship();
    virtual Rotator& ObjectRotator()=0;
    virtual void MissScript(Intersector&, const double createTime,
                                          const double existTime);
    virtual void HitScript(Intersector&, const double createTime,
                                         const double existTime,
                                         const Vector2d& intersectPoint);

    const unsigned char* const icon;
    const int iconWidth,
              iconHeight;
#ifdef X11_BACKEND
    XColor iconColor;
#else
    // Task-35 wireframe color (the guarded ctors pass their XColor static to
    // the rotator data; GL keeps the components for the outline draw).
    float glColorR=0.0f,
          glColorG=0.0f,
          glColorB=0.0f;
    // Task-36 D2 composite body: the decor bitmap uploaded once at ctor
    // (RGBA8 content tinted with the guarded color components + R8 coverage
    // mask), drawn under the quantized rotator angle about the sprite
    // center — the rock DeferDrawTransformed pattern.
    TextureId glDecorContent=0,
              glDecorMask=0;
    int glDecorW=0,
        glDecorH=0;
#endif
    static double maxVelocity,
                  acceleration,
                  angularVelocity;
    Boolean dead;
  };

class StarDestroyer: public Ship
 {public:
    StarDestroyer();
    virtual Rotator& ObjectRotator();

  private:
    static const Vector2d vectors[5];
#ifdef X11_BACKEND
    static XColor color;
#else
  public:
    // GL twin of the guarded color static (main() feeds it to ShipYard).
    static XColor glColor;
#endif
    RotVectorData graphic;
    Rotator rotator;
  };

class NextGenerationEnterprise: public Ship
 {public:
    NextGenerationEnterprise();
    virtual ~NextGenerationEnterprise();
    virtual Rotator& ObjectRotator();

  private:
    static const Vector2d vectors[34];
#ifdef X11_BACKEND
    static XColor bottomColor,
                  topColor;
#else
    static XColor glColor;
#endif
    static const double area;
    RotVectorData* graphic;
    Rotator* rotator;
  };

class MovieEnterprise: public Ship
 {public:
    MovieEnterprise();
    virtual Rotator& ObjectRotator();

  private:
    static const Vector2d vectors[32];
#ifdef X11_BACKEND
    static XColor color;
#else
    static XColor glColor;
#endif
    static const double area;
    RotVectorData graphic;
    Rotator rotator;
  };

class Thrust: public MovableObject
  {public:
    typedef int Boolean;
    enum {off, on};

    Thrust();
    virtual ~Thrust();
    virtual Rotator& ObjectRotator()=0;
    virtual void MissScript(Intersector&, const double createTime,
                                          const double existTime);
    Boolean thrusting;

#ifndef X11_BACKEND
    // Task-35 m13 degenerate path, task-36 revision: the X11 leg blits
    // PRE-ROTATED pixmaps (RotPixmapData rotates through numPix discrete
    // angles), so the GL twin bakes one texture per quantized angle with the
    // same scatter math (glSprites.H) and blits the matching slot unrotated.
    // The masked variant additionally carries per-angle R8 coverage masks.
    std::vector<TextureId> glTexArr,
                           glMaskArr;
    int glSide=0;
#endif
  };

class StarDestroyerThrust: public Thrust
  {public:
    StarDestroyerThrust();
    virtual ~StarDestroyerThrust();
    virtual Rotator& ObjectRotator();

   private:
#ifdef X11_BACKEND
    static XColor edgeColor,
                  middleColor,
                  centerColor;
#else
    static XColor glEdgeColor;
#endif
    RotPixmapData* graphic;
    Rotator* rotator;
  };

class NextGenerationEnterpriseThrust: public Thrust
  {public:
    NextGenerationEnterpriseThrust();
    virtual Rotator& ObjectRotator();

   private:
#ifdef X11_BACKEND
    static XColor color;
#else
    static XColor glColor;
#endif
    RotPixmapData graphic;
    Rotator rotator;
  };

class MovieEnterpriseThrust: public Thrust
  {public:
    MovieEnterpriseThrust();
    virtual Rotator& ObjectRotator();
    virtual void MissScript(Intersector&, const double createTime,
                                          const double existTime);

   private:
#ifdef X11_BACKEND
    static XColor color;
#else
    static XColor glColor;
#endif
    MaskedRotPixmapData graphic;
    Rotator rotator;
  };


class ShipGroup;
class Hyper: public MovableObject
 {friend class ShipGroup;
  friend class Options;
  public:
    typedef int Boolean;
    enum {off, on};

    Hyper();
    virtual void MissScript(Intersector&, const double createTime,
                                          const double existTime);

    Boolean hyperspacing;
    static double percentDestructRate;
    static int destructRate;

  private:
    int hyperCount;
 };


class ShipGroup
{friend class Options;
  public:
    ShipGroup(RenderingEngine& engine);

    // Transition DI handle (task 20): lets the ship bodies reach the
    // rendering abstraction without widening Stage. Removed with the other
    // transition members at task 47.
    RenderingEngine& engine;

    StarDestroyer starDestroyer;
    StarDestroyerThrust starDestroyerThrust;
    NextGenerationEnterprise NCC1701D;
    NextGenerationEnterpriseThrust NCC1701DThrust;
    MovieEnterprise NCC1701A;
    MovieEnterpriseThrust NCC1701AThrust;

    Hyper hyper;
    Explosion explosion;

    static Ship* ship;
    static Thrust* thrust;

    static const int maxIconHeight,
                     maxIconWidth;

    inline void NewShip();
    inline void NewThrust();
    inline void NewHyper();
    inline void ResetAcceleration(const double newAcceleration);
    inline void ResetAngularVelocity(const double newAngularVelocity);
    inline void ResetAccelerationLimits(const double newMaxAcceleration);
    inline void ResetVelocityLimits(const double newMaxVelocity);
    inline void AdjustFramesPerSec(const double frameRatio);
 };

extern ShipGroup* shipGroup;

#ifndef X11_BACKEND
// Task-36: single-layer decor upload (content tinted + R8 coverage mask).
// The multi-color NCC1701D stack composites inline in its ctor.
inline void glUploadShipDecor(RenderingEngine& engine,
                              const unsigned char* bits,const int w,const int h,
                              const unsigned short r,const unsigned short g,
                              const unsigned short b,
                              TextureId& content,TextureId& mask,
                              int& outW,int& outH)
 {CompositeLayerSpec layer={bits,w,h,r,g,b};
  CompositeFrameRGBA8 frame;
  compositeFrameStack(frame,w,h,&layer,1);
  content=engine.createTextureFromBitmap(frame.rgba8.data(),w,h,4);
  std::vector<uint8_t> maskBytes;
  compositeMaskExpandR8(bits,w,h,maskBytes);
  mask=engine.createTextureFromBitmap(maskBytes.data(),w,h,1);
  outW=w;
  outH=h;
 }
#endif

#ifdef X11_BACKEND
// Engine-first DI ctor (task 13, consumed from task 20). Members still pull
// stage->display themselves until task 22 migrates them. Memberwise
// construction in declaration order replicates the implicit ctor the
// placement-new staging has always relied on (task-6 order); the leading
// engine binding is a side-effect-free reference init and preserves it.
inline ShipGroup::ShipGroup(RenderingEngine& eng): engine(eng)
 {
 }
#else
inline ShipGroup::ShipGroup(RenderingEngine& eng): engine(eng)
 {
 }
#endif

#include"../../gamePlay/playingField.H"


inline double Ship::maxVelocity=.43*PlayingField::maxLinearVelocity,
       Ship::acceleration=1.425,
       Ship::angularVelocity=0.2;

#ifdef X11_BACKEND
inline Ship::Ship(const unsigned char* const i,
                  const int iWidth, const int iHeight,
                  XColor& iColor): icon(i),
                                   iconWidth(iWidth),
                                   iconHeight(iHeight),
                                   iconColor(iColor),
                                   dead(on)
  {/* STOPPED SITE (task 20): colormap allocation retained for its external
      readers — the assigned pixel value is consumed directly by the options
      header's icon foregrounds; closure path = backend-exposed color
      primitive. */
   if (!XAllocColor(stage->display,DefaultColormap(stage->display,
                                                   DefaultScreen(stage->display)),
                    &iconColor))
       iconColor.pixel=WhitePixel(stage->display,DefaultScreen(stage->display));
  }
#else
inline Ship::Ship(const unsigned char* const i,
                  const int iWidth, const int iHeight): icon(i),
                                                        iconWidth(iWidth),
                                                        iconHeight(iHeight),
                                                        dead(on)
  {
  }
#endif

Ship::~Ship()
 {
 }

void Ship::MissScript(Intersector& intersector,
                      const double createTime, const double existTime)
 {ObjectRotator().UpdateAngle(existTime);
  ObjectLiner().UpdateVelocity(existTime);
  shipBulletGroup->Fire(intersector,createTime+existTime);
  #ifdef X11_BACKEND
  playingField->DeferDraw((TextureId)ObjectRotator().GetPixAtTime(0),
                          CurrentBox().WrapMovingBox(PlayingField::playArea,
                                                     ObjectLiner().Velocity())
                                      .NorthWestCorner().x+.5,
                          CurrentBox().NorthWestCorner().y+.5,
                          ObjectRotator().PixWidth(),
                          ObjectRotator().PixHeight());
  #endif
  #ifndef X11_BACKEND
  {const int destX=CurrentBox().WrapMovingBox(PlayingField::playArea,
                                              ObjectLiner().Velocity())
                              .NorthWestCorner().x+.5,
           destY=CurrentBox().NorthWestCorner().y+.5;
   // D2 wireframe leg: GetVecsAtTime(0) already applies the rotator's
   // discrete-angle quantization (the exact vecs the X11 rasterizer walks),
   // so the MVP carries TRANSLATION ONLY — a continuous angle here would
   // double-rotate. Documented fork; the continuous-angle MVP rotation is
   // exercised by the rock composite path.
   playingField->DeferDrawOutline(
       destX+ObjectRotator().PixWidth()*.5f,
       destY+ObjectRotator().PixHeight()*.5f,
       0.0f,
       ObjectRotator().GetVecsAtTime(0),
       ObjectRotator().GetNumVecs(),
       glColorR,glColorG,glColorB);
   // Task-36 composite body: the decor quad rides the SAME quantized angle
   // the X11 pixmap rasterizer used (index math mirrored from GetVecs), so
   // it stays registered with the outline; rotation happens about the sprite
   // center, matching PixmapDecorInit's point-rotation placement.
   if (glDecorContent)
    {const double theta=ObjectRotator().Angle();
     const int numPix=int(6.28318530717958*ObjectRotator().Radius()+.5);
     const double incAngle=6.28318530717958/numPix;
     const int idx=int((fmod(theta,6.28318530717958)+6.28318530717958)
                       /incAngle+.5)%numPix;
     playingField->DeferDrawTransformed(
         destX+ObjectRotator().PixWidth()*.5f,
         destY+ObjectRotator().PixHeight()*.5f,
         (float)(idx*incAngle),
         glDecorContent,glDecorMask,
         -glDecorW*.5f,-glDecorH*.5f,(float)glDecorW,(float)glDecorH);
    }
  }
  #endif
 }

void Ship::HitScript(Intersector& intersector,
                     const double createTime, const double existTime,
                     const Vector2d& intersectPoint)
 {intersector.RemoveNonPermeable(this);
  if (shipGroup->thrust->thrusting)
    intersector.RemovePermeable(shipGroup->thrust);
  shipGroup->hyper.hyperspacing=Hyper::on;
  dead=on;
  shipGroup->explosion.CurrentBox().MoveBox(ObjectLiner()
                                           .Move(OldBox().Center(),existTime)
		                          -shipGroup->explosion.CurrentBox().Center());
  ObjectLiner().UpdateVelocity(existTime);
  shipGroup->explosion.ObjectLiner().Velocity()=ObjectLiner().Velocity();
  shipGroup->explosion.frameList.Reset();
  shipGroup->explosion.frameTimer.Reset();
  intersector.AddPermeable(&shipGroup->explosion,createTime+existTime);
  if (playingField->lastHitScore)
   {score->CurrentScore()+=playingField->lastHitScore;
    playingField->lastHitScore=0;
   }
  else
    playingField->lastHitFromAlly=!playingField->lastHitFromAlly;
 }

inline const Vector2d StarDestroyer::vectors[5]={Vector2d(0,-19),
                                          Vector2d(8,11),
                                          Vector2d(4,7),
                                          Vector2d(-4,7),
                                          Vector2d(-8,11)};

#ifdef X11_BACKEND
inline XColor StarDestroyer::color={0,
                             44461,//173
                             55512,//216
                             59110,//230
                             DoRed|DoGreen|DoBlue};
#endif

#ifdef X11_BACKEND
inline StarDestroyer::StarDestroyer(): Ship(starDestroyerIcon_bits,
                                            starDestroyerIcon_width,
                                            starDestroyerIcon_height,
                                            color),
                                       graphic(stage->display,stage->window,color,
                                               vectors,sizeof(vectors)/sizeof(Vector2d),
#ifdef _CORP_LOGO_
                                               starDestroyerDecor_bits,
                                               starDestroyerDecor_width,starDestroyerDecor_height),
#else
                                               fortytwo_bits,
                                               fortytwo_width,fortytwo_height),
#endif
                                               rotator(&graphic,angularVelocity,PlayingField::maxLinearVelocity)
 {SetMovableObject(Vector2d(),2*rotator.Radius(),2*rotator.Radius(),
                   Vector2d(),maxVelocity,Vector2d());
 }
#else
inline XColor StarDestroyer::glColor={0,
                              44461,//173
                              55512,//216
                              59110,//230
                              DoRed|DoGreen|DoBlue};

inline StarDestroyer::StarDestroyer(): Ship(starDestroyerIcon_bits,
                                            starDestroyerIcon_width,
                                            starDestroyerIcon_height),
                                       graphic(stage->display,stage->window,glColor,
                                               vectors,sizeof(vectors)/sizeof(Vector2d),
#ifdef _CORP_LOGO_
                                               starDestroyerDecor_bits,
                                               starDestroyerDecor_width,starDestroyerDecor_height),
#else
                                               fortytwo_bits,
                                               fortytwo_width,fortytwo_height),
#endif
                                               rotator(&graphic,angularVelocity,PlayingField::maxLinearVelocity)
 {SetMovableObject(Vector2d(),2*rotator.Radius(),2*rotator.Radius(),
                   Vector2d(),maxVelocity,Vector2d());
  glColorR=44461.0f/65535.0f;
  glColorG=55512.0f/65535.0f;
  glColorB=59110.0f/65535.0f;
  glUploadShipDecor(shipGroup->engine,
                    #ifdef _CORP_LOGO_
                    starDestroyerDecor_bits,starDestroyerDecor_width,
                    starDestroyerDecor_height,
                    #else
                    fortytwo_bits,fortytwo_width,fortytwo_height,
                    #endif
                    44461,55512,59110,
                    glDecorContent,glDecorMask,glDecorW,glDecorH);
  }
#endif

Rotator& StarDestroyer::ObjectRotator()
 {return rotator;
 }

inline const Vector2d NextGenerationEnterprise::vectors[34]={Vector2d(0,-10),
                                                      Vector2d(8,-8),
                                                      Vector2d(10,-6),
                                                      Vector2d(11,-2),
                                                      Vector2d(10,2),
                                                      Vector2d(8,4),
                                                      Vector2d(2,7),
                                                      Vector2d(3,8),
                                                      Vector2d(2,13),
                                                      Vector2d(4,15),
                                                      Vector2d(6,15),
                                                      Vector2d(6,8),
                                                      Vector2d(7,7),
                                                      Vector2d(8,8),
                                                      Vector2d(8,19),
                                                      Vector2d(6,19),
                                                      Vector2d(3,17),
                                                      Vector2d(0,17),
                                                      Vector2d(-3,17),
                                                      Vector2d(-6,19),
                                                      Vector2d(-8,19),
                                                      Vector2d(-8,8),
                                                      Vector2d(-7,7),
                                                      Vector2d(-6,8),
                                                      Vector2d(-6,15),
                                                      Vector2d(-4,15),
                                                      Vector2d(-2,13),
                                                      Vector2d(-3,8),
                                                      Vector2d(-2,7),
                                                      Vector2d(-8,4),
                                                      Vector2d(-10,2),
                                                      Vector2d(-11,-2),
                                                      Vector2d(-10,-6),
                                                      Vector2d(-8,-8)};

#ifdef X11_BACKEND
inline XColor NextGenerationEnterprise::bottomColor={0,
                                              61680,//240
                                              61680,//240
                                              61680,//240
                                              DoRed|DoGreen|DoBlue},
       NextGenerationEnterprise::topColor={0,
                                           49344,//192
                                           0,//0
                                           0,//0
                                           DoRed|DoGreen|DoBlue};
#endif

inline const double NextGenerationEnterprise::area=472;

#ifdef X11_BACKEND
inline NextGenerationEnterprise::NextGenerationEnterprise(): Ship(NCC1701DIcon_bits,
                                                                  NCC1701DIcon_width,
                                                                  NCC1701DIcon_height,
                                                                  bottomColor)
 {CompositePixmap NextGenerationEnterpriseCP(stage->display,stage->window,
                                             NCC1701DDecorBottom_width,
                                             NCC1701DDecorBottom_height);
  NextGenerationEnterpriseCP.AddBitmapData(NCC1701DDecorBottom_bits,
                                           NCC1701DDecorBottom_width,
                                           NCC1701DDecorBottom_height,
                                           bottomColor)
                            .AddBitmapData(NCC1701DDecorTop_bits,
                                           NCC1701DDecorTop_width,
                                           NCC1701DDecorTop_height,
                                           topColor);
  graphic=new RotVectorData(stage->display,stage->window,bottomColor,
                            vectors,sizeof(vectors)/sizeof(Vector2d),
                            area,
                            NextGenerationEnterpriseCP);
  rotator=new Rotator(graphic,angularVelocity,PlayingField::maxLinearVelocity);
  SetMovableObject(Vector2d(),2*rotator->Radius(),2*rotator->Radius(),
                   Vector2d(),maxVelocity,Vector2d());
  }
#else
inline XColor NextGenerationEnterprise::glColor={0,
                                          61680,//240
                                          61680,//240
                                          61680,//240
                                          DoRed|DoGreen|DoBlue};

inline NextGenerationEnterprise::NextGenerationEnterprise(): Ship(NCC1701DIcon_bits,
                                                                  NCC1701DIcon_width,
                                                                  NCC1701DIcon_height)
 {// No CompositePixmap on GL (its engine branch aborts): the wireframe-only
  // D2 path needs the outline data only, so the no-decor RotVectorData
  // overload feeds the identical geometry.
  graphic=new RotVectorData(stage->display,stage->window,glColor,
                            vectors,sizeof(vectors)/sizeof(Vector2d),
                            area);
  rotator=new Rotator(graphic,angularVelocity,PlayingField::maxLinearVelocity);
  SetMovableObject(Vector2d(),2*rotator->Radius(),2*rotator->Radius(),
                   Vector2d(),maxVelocity,Vector2d());
  glColorR=61680.0f/65535.0f;
  glColorG=61680.0f/65535.0f;
  glColorB=61680.0f/65535.0f;
  // Task-36 composite body: the guarded leg stamps a two-color CompositePixmap
  // (bottom gray + top red); the CPU stack reproduces it layer-for-layer.
  {CompositeLayerSpec layers[2]={{NCC1701DDecorBottom_bits,
                                  NCC1701DDecorBottom_width,
                                  NCC1701DDecorBottom_height,61680,61680,61680},
                                 {NCC1701DDecorTop_bits,
                                  NCC1701DDecorTop_width,
                                  NCC1701DDecorTop_height,49344,0,0}};
   CompositeFrameRGBA8 frame;
   compositeFrameStack(frame,NCC1701DDecorBottom_width,
                       NCC1701DDecorBottom_height,layers,2);
   glDecorContent=shipGroup->engine.createTextureFromBitmap(
       frame.rgba8.data(),frame.width,frame.height,4);
   // Mask = composite coverage (alpha), i.e. the union of both layers —
   // the guarded leg scatters every non-background composite pixel.
   {std::vector<uint8_t> maskBytes(frame.rgba8.size()/4);
    for (size_t p=0;p<maskBytes.size();++p)
      maskBytes[p]=frame.rgba8[p*4+3];
    glDecorMask=shipGroup->engine.createTextureFromBitmap(maskBytes.data(),
                                                          frame.width,
                                                          frame.height,1);
   }
   glDecorW=frame.width;
   glDecorH=frame.height;
  }
  }
#endif

NextGenerationEnterprise::~NextGenerationEnterprise()
 {delete rotator;
  delete graphic;
 }

Rotator& NextGenerationEnterprise::ObjectRotator()
 {return *rotator;
 }

inline const Vector2d MovieEnterprise::vectors[32]={Vector2d(0,-16),
                                             Vector2d(6,-14),
                                             Vector2d(7,-13),
                                             Vector2d(9,-7),
                                             Vector2d(7,-1),
                                             Vector2d(6,0),
                                             Vector2d(2,2),
                                             Vector2d(2,6),
                                             Vector2d(7,11),
                                             Vector2d(7,4),
                                             Vector2d(8,4),
                                             Vector2d(8,27),
                                             Vector2d(7,26),
                                             Vector2d(7,13),
                                             Vector2d(2,7),
                                             Vector2d(1,14),
                                             Vector2d(0,15),
                                             Vector2d(-1,14),
                                             Vector2d(-2,7),
                                             Vector2d(-7,13),
                                             Vector2d(-7,26),
                                             Vector2d(-8,27),
                                             Vector2d(-8,4),
                                             Vector2d(-7,4),
                                             Vector2d(-7,11),
                                             Vector2d(-2,6),
                                             Vector2d(-2,2),
                                             Vector2d(-6,0),
                                             Vector2d(-7,-1),
                                             Vector2d(-9,-7),
                                             Vector2d(-7,-13),
                                              Vector2d(-6,-14)};

#ifdef X11_BACKEND
inline XColor MovieEnterprise::color={0,
                               61680,//240
                               61680,//240
                               61680,//240
                               DoRed|DoGreen|DoBlue};
#endif

inline const double MovieEnterprise::area=446;

#ifdef X11_BACKEND
inline MovieEnterprise::MovieEnterprise(): Ship(NCC1701AIcon_bits,
                                                NCC1701AIcon_width,
                                                NCC1701AIcon_height,
                                                color),
                                           graphic(stage->display,stage->window,color,
                                                   vectors,sizeof(vectors)/sizeof(Vector2d),
                                                   area,
                                                   NCC1701ADecor_bits,
                                                   NCC1701ADecor_width,NCC1701ADecor_height),
                                           rotator(&graphic,angularVelocity,PlayingField::maxLinearVelocity)
 {SetMovableObject(Vector2d(),2*rotator.Radius(),2*rotator.Radius(),
                   Vector2d(),maxVelocity,Vector2d());
  }
#else
inline XColor MovieEnterprise::glColor={0,
                                 61680,//240
                                 61680,//240
                                 61680,//240
                                 DoRed|DoGreen|DoBlue};

inline MovieEnterprise::MovieEnterprise(): Ship(NCC1701AIcon_bits,
                                                NCC1701AIcon_width,
                                                NCC1701AIcon_height),
                                           graphic(stage->display,stage->window,glColor,
                                                   vectors,sizeof(vectors)/sizeof(Vector2d),
                                                   area,
                                                   NCC1701ADecor_bits,
                                                   NCC1701ADecor_width,NCC1701ADecor_height),
                                           rotator(&graphic,angularVelocity,PlayingField::maxLinearVelocity)
 {SetMovableObject(Vector2d(),2*rotator.Radius(),2*rotator.Radius(),
                   Vector2d(),maxVelocity,Vector2d());
  glColorR=61680.0f/65535.0f;
  glColorG=61680.0f/65535.0f;
  glColorB=61680.0f/65535.0f;
  glUploadShipDecor(shipGroup->engine,
                    NCC1701ADecor_bits,NCC1701ADecor_width,NCC1701ADecor_height,
                    61680,61680,61680,
                    glDecorContent,glDecorMask,glDecorW,glDecorH);
  }
#endif

Rotator& MovieEnterprise::ObjectRotator()
 {return rotator;
 }

inline Thrust::Thrust(): thrusting(off)
 {
 }

Thrust::~Thrust()
 {
 }

void Thrust::MissScript(Intersector& intersector, const double createTime,
                        const double existTime)
 {shipGroup->NewThrust();
  #ifdef X11_BACKEND
  playingField->DeferDraw((TextureId)ObjectRotator().GetPixAtTime(0),
                          CurrentBox().WrapMovingBox(PlayingField::playArea,
                                                     ObjectLiner().Velocity())
                                      .NorthWestCorner().x+.5,
                          CurrentBox().NorthWestCorner().y+.5,
                          ObjectRotator().PixWidth(),
                          ObjectRotator().PixHeight());
  #endif
  #ifndef X11_BACKEND
  // m13 static-texture path (task-36 revision): blit the pre-rotated frame
  // for the rotator's quantized angle — identity transform, no setTransform.
  {const double theta=ObjectRotator().Angle();
   const int numPix=(int)glTexArr.size();
   const double incAngle=6.28318530717958/numPix;
   const int idx=int((fmod(theta,6.28318530717958)+6.28318530717958)
                     /incAngle+.5)%numPix;
   playingField->DeferDraw(glTexArr[idx],
                           CurrentBox().WrapMovingBox(PlayingField::playArea,
                                                      ObjectLiner().Velocity())
                                       .NorthWestCorner().x+.5,
                           CurrentBox().NorthWestCorner().y+.5,
                           (float)glSide,(float)glSide);
  }
  #endif
  }

#ifdef X11_BACKEND
inline XColor StarDestroyerThrust::edgeColor={0,
                                       0,
                                       0,
                                       65535,//255
                                       DoRed|DoGreen|DoBlue},
       StarDestroyerThrust::middleColor={0,
                                         0,
                                         49087,//191
                                         65535,//255
                                         DoRed|DoGreen|DoBlue},
       StarDestroyerThrust::centerColor={0,
                                         61680,//240
                                         63736,//248
                                         65535,//255
                                         DoRed|DoGreen|DoBlue};

inline StarDestroyerThrust::StarDestroyerThrust()
 {CompositePixmap thrustCP(stage->display,stage->window,
                           starDestroyerThrustEdge_width,starDestroyerThrustEdge_height);
  thrustCP.AddBitmapData(starDestroyerThrustEdge_bits,
                         starDestroyerThrustEdge_width,starDestroyerThrustEdge_height,
                         edgeColor)
          .AddBitmapData(starDestroyerThrustMiddle_bits,
                         starDestroyerThrustMiddle_width,starDestroyerThrustMiddle_height,
                         middleColor)
          .AddBitmapData(starDestroyerThrustCenter_bits,
                         starDestroyerThrustCenter_width,starDestroyerThrustCenter_height,
                         centerColor);
  graphic=new RotPixmapData(stage->display,stage->window,thrustCP);
  rotator=new Rotator(graphic,Ship::angularVelocity,PlayingField::maxLinearVelocity);
  SetMovableObject(Vector2d(),2*rotator->Radius(),2*rotator->Radius(),
                   Vector2d(),Ship::maxVelocity,Vector2d());
  }
#else
inline XColor StarDestroyerThrust::glEdgeColor={0,
                                         0,
                                         0,
                                         65535,//255
                                         DoRed|DoGreen|DoBlue};

inline StarDestroyerThrust::StarDestroyerThrust()
 {// CPU composite mirrors the guarded stack (edge/middle/center, later
  // layers overwrite) into the m13 pre-computed texture. The rotator data
  // rides the edge-bitmap ctor: all three layers are 17x40, so the base
  // sideSize matches the guarded CompositePixmap variant exactly.
  CompositeLayerSpec layers[3]={
      {starDestroyerThrustEdge_bits,
       starDestroyerThrustEdge_width,starDestroyerThrustEdge_height,0,0,65535},
      {starDestroyerThrustMiddle_bits,
       starDestroyerThrustMiddle_width,starDestroyerThrustMiddle_height,0,49087,65535},
      {starDestroyerThrustCenter_bits,
       starDestroyerThrustCenter_width,starDestroyerThrustCenter_height,61680,63736,65535}};
  CompositeFrameRGBA8 frame;
  compositeFrameStack(frame,starDestroyerThrustEdge_width,
                      starDestroyerThrustEdge_height,layers,3);
  // Task-36: bake the per-angle pre-rotated frames (X11 blits RotPixmapData
  // pixmaps, not the raw bitmap).
  {GlRotatedFrames rot=glBakeRotatedSprite(frame.rgba8.data(),frame.width,
                                           frame.height,false);
   glTexArr.resize(rot.numPix);
   for(int k=0;k<rot.numPix;++k)
     glTexArr[k]=shipGroup->engine.createTextureFromBitmap(rot.rgba[k].data(),
                                                           rot.side,rot.side,4);
   glSide=rot.side;
  }
  graphic=new RotPixmapData(stage->display,stage->window,glEdgeColor,
                            starDestroyerThrustEdge_bits,
                            starDestroyerThrustEdge_width,
                            starDestroyerThrustEdge_height);
  rotator=new Rotator(graphic,Ship::angularVelocity,PlayingField::maxLinearVelocity);
  SetMovableObject(Vector2d(),2*rotator->Radius(),2*rotator->Radius(),
                   Vector2d(),Ship::maxVelocity,Vector2d());
  }
#endif

inline StarDestroyerThrust::~StarDestroyerThrust()
 {delete rotator;
  delete graphic;
 }

Rotator& StarDestroyerThrust::ObjectRotator()
 {return *rotator;
 }

#ifdef X11_BACKEND
inline XColor NextGenerationEnterpriseThrust::color={0,
                                              65535,//255
                                              0,
                                              0,
                                              DoRed|DoGreen|DoBlue};

inline NextGenerationEnterpriseThrust::NextGenerationEnterpriseThrust(): graphic(stage->display,stage->window,color,
                                                                                 NCC1701DThrustDecor_bits,
                                                                                 NCC1701DThrustDecor_width,
                                                                                 NCC1701DThrustDecor_height),
                                                                         rotator(&graphic,Ship::angularVelocity,
                                                                                 PlayingField::maxLinearVelocity)
 {SetMovableObject(Vector2d(),2*rotator.Radius(),2*rotator.Radius(),
                   Vector2d(),Ship::maxVelocity,Vector2d());
  }
#else
inline XColor NextGenerationEnterpriseThrust::glColor={0,
                                                65535,//255
                                                0,
                                                0,
                                                DoRed|DoGreen|DoBlue};

inline NextGenerationEnterpriseThrust::NextGenerationEnterpriseThrust(): graphic(stage->display,stage->window,glColor,
                                                                                 NCC1701DThrustDecor_bits,
                                                                                 NCC1701DThrustDecor_width,
                                                                                 NCC1701DThrustDecor_height),
                                                                         rotator(&graphic,Ship::angularVelocity,
                                                                                 PlayingField::maxLinearVelocity)
 {SetMovableObject(Vector2d(),2*rotator.Radius(),2*rotator.Radius(),
                   Vector2d(),Ship::maxVelocity,Vector2d());
  // m13 pre-computed texture: single red layer, no mask (RotPixmapData).
  // Task-36: baked per-angle like the guarded rotation pipeline.
  CompositeLayerSpec layer={NCC1701DThrustDecor_bits,
                            NCC1701DThrustDecor_width,NCC1701DThrustDecor_height,
                            65535,0,0};
  CompositeFrameRGBA8 frame;
  compositeFrameStack(frame,NCC1701DThrustDecor_width,
                      NCC1701DThrustDecor_height,&layer,1);
  {GlRotatedFrames rot=glBakeRotatedSprite(frame.rgba8.data(),frame.width,
                                           frame.height,false);
   glTexArr.resize(rot.numPix);
   for(int k=0;k<rot.numPix;++k)
     glTexArr[k]=shipGroup->engine.createTextureFromBitmap(rot.rgba[k].data(),
                                                           rot.side,rot.side,4);
   glSide=rot.side;
  }
  }
#endif

Rotator& NextGenerationEnterpriseThrust::ObjectRotator()
 {return rotator;
 }

#ifdef X11_BACKEND
inline XColor MovieEnterpriseThrust::color={0,
                                     65535,//255
                                     0,
                                     0,
                                     DoRed|DoGreen|DoBlue};

inline MovieEnterpriseThrust::MovieEnterpriseThrust():
                       graphic(stage->display,stage->window,color,
                               NCC1701AThrustDecor_bits,
                               NCC1701AThrustDecor_width,NCC1701AThrustDecor_height),
                       rotator(&graphic,Ship::angularVelocity,PlayingField::maxLinearVelocity)
 {SetMovableObject(Vector2d(),2*rotator.Radius(),2*rotator.Radius(),
                   Vector2d(),Ship::maxVelocity,Vector2d());
  }
#else
inline XColor MovieEnterpriseThrust::glColor={0,
                                       65535,//255
                                       0,
                                       0,
                                       DoRed|DoGreen|DoBlue};

inline MovieEnterpriseThrust::MovieEnterpriseThrust():
                       graphic(stage->display,stage->window,glColor,
                               NCC1701AThrustDecor_bits,
                               NCC1701AThrustDecor_width,NCC1701AThrustDecor_height),
                       rotator(&graphic,Ship::angularVelocity,PlayingField::maxLinearVelocity)
 {SetMovableObject(Vector2d(),2*rotator.Radius(),2*rotator.Radius(),
                   Vector2d(),Ship::maxVelocity,Vector2d());
  // m13 pre-computed texture + R8 coverage mask (MaskedRotPixmapData twin).
  // Task-36: baked per-angle (content + mask) like the guarded pipeline.
  CompositeLayerSpec layer={NCC1701AThrustDecor_bits,
                            NCC1701AThrustDecor_width,NCC1701AThrustDecor_height,
                            65535,0,0};
  CompositeFrameRGBA8 frame;
  compositeFrameStack(frame,NCC1701AThrustDecor_width,
                      NCC1701AThrustDecor_height,&layer,1);
  {GlRotatedFrames rot=glBakeRotatedSprite(frame.rgba8.data(),frame.width,
                                           frame.height,true);
   glTexArr.resize(rot.numPix);
   glMaskArr.resize(rot.numPix);
   for(int k=0;k<rot.numPix;++k)
    {glTexArr[k]=shipGroup->engine.createTextureFromBitmap(rot.rgba[k].data(),
                                                           rot.side,rot.side,4);
     glMaskArr[k]=shipGroup->engine.createTextureFromBitmap(rot.mask[k].data(),
                                                            rot.side,rot.side,1);
    }
   glSide=rot.side;
  }
  }
#endif

Rotator& MovieEnterpriseThrust::ObjectRotator()
 {return rotator;
 }

void MovieEnterpriseThrust::MissScript(Intersector& intersector, const double createTime,
                                       const double existTime)
 {shipGroup->NewThrust();
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
  // m13 static-texture path, masked variant (task-36 revision): per-angle
  // pre-rotated content + R8 mask, identity transform.
  {const double theta=ObjectRotator().Angle();
   const int numPix=(int)glTexArr.size();
   const double incAngle=6.28318530717958/numPix;
   const int idx=int((fmod(theta,6.28318530717958)+6.28318530717958)
                     /incAngle+.5)%numPix;
   playingField->DeferDrawMasked(glTexArr[idx],glMaskArr[idx],
                                 destX,destY,
                                 (float)glSide,(float)glSide);
  }
  #endif
  }

inline double Hyper::percentDestructRate=1.25;

inline int Hyper::destructRate=RAND_MAX_16*percentDestructRate/100;

inline Hyper::Hyper(): MovableObject(Vector2d(),
                                     3*shipGroup->ship->ObjectRotator().PixWidth(),
                                     3*shipGroup->ship->ObjectRotator().PixHeight(),
                                     Vector2d(),0,Vector2d()),
                       hyperspacing(on),
                       hyperCount(-destructRate)
 {
 }

void Hyper::MissScript(Intersector& intersector,
                       const double createTime, const double existTime)
 {playingField->nonPermeableList.SetCurrentAtHead();
  for (int i=0; i<playingField->nonPermeableList.GetNumItems(); ++i)
   {if (Intersect(playingField->nonPermeableList.GetData()->CurrentBox(),
                  CurrentBox()))
      return;
    playingField->nonPermeableList.Next();
   }
  playingField->selfPermeableList.SetCurrentAtHead();
  for (int i=0; i<playingField->selfPermeableList.GetNumItems(); ++i)
   {if (Intersect(playingField->selfPermeableList.GetData()->CurrentBox(),
                  CurrentBox()))
      return;
    playingField->selfPermeableList.Next();
   }
  intersector.RemovePermeable(this);
  if (gary_rand::rand_16()>(hyperCount+=destructRate))
   {shipGroup->ship->CurrentBox().MoveBox(CurrentBox().Center()
                                         -shipGroup->ship->CurrentBox().Center());
    shipGroup->ship->ObjectLiner().Velocity()=Vector2d();
    shipGroup->ship->ObjectLiner().Acceleration()=Vector2d();
    shipGroup->ship->ObjectRotator().AngularVelocity()=0;
     intersector.AddNonPermeable(shipGroup->ship,createTime+existTime);
     #ifdef X11_BACKEND
     playingField->DeferDraw((TextureId)shipGroup->ship->ObjectRotator().GetPixAtTime(0),
                             shipGroup->ship->CurrentBox().NorthWestCorner().x+.5,
                             shipGroup->ship->CurrentBox().NorthWestCorner().y+.5,
                             shipGroup->ship->ObjectRotator().PixWidth(),
                             shipGroup->ship->ObjectRotator().PixHeight());
     #endif
     #ifndef X11_BACKEND
     {const int destX=shipGroup->ship->CurrentBox().NorthWestCorner().x+.5,
                destY=shipGroup->ship->CurrentBox().NorthWestCorner().y+.5;
      playingField->DeferDrawOutline(
          (float)destX+shipGroup->ship->ObjectRotator().PixWidth()*.5f,
          (float)destY+shipGroup->ship->ObjectRotator().PixHeight()*.5f,
          0.0f,
          shipGroup->ship->ObjectRotator().GetVecsAtTime(0),
          shipGroup->ship->ObjectRotator().GetNumVecs(),
          shipGroup->ship->glColorR,
          shipGroup->ship->glColorG,
          shipGroup->ship->glColorB);
      // Task-36 composite body: same quantized-angle decor leg as
      // Ship::MissScript (the X11 blit carries outline + decor together).
      if (shipGroup->ship->glDecorContent)
       {const double theta=shipGroup->ship->ObjectRotator().Angle();
        const int numPix=int(6.28318530717958
                             *shipGroup->ship->ObjectRotator().Radius()+.5);
        const double incAngle=6.28318530717958/numPix;
        const int idx=int((fmod(theta,6.28318530717958)+6.28318530717958)
                          /incAngle+.5)%numPix;
        playingField->DeferDrawTransformed(
            (float)destX+shipGroup->ship->ObjectRotator().PixWidth()*.5f,
            (float)destY+shipGroup->ship->ObjectRotator().PixHeight()*.5f,
            (float)(idx*incAngle),
            shipGroup->ship->glDecorContent,
            shipGroup->ship->glDecorMask,
            -shipGroup->ship->glDecorW*.5f,
            -shipGroup->ship->glDecorH*.5f,
            (float)shipGroup->ship->glDecorW,
            (float)shipGroup->ship->glDecorH);
       }
     }
     #endif
    shipGroup->thrust->thrusting=Thrust::off;
    shipGroup->hyper.hyperspacing=Hyper::off;
   }
  else
   {shipGroup->explosion.CurrentBox().MoveBox(CurrentBox().Center()
	                                    -shipGroup->explosion.CurrentBox().Center());
    shipGroup->explosion.ObjectLiner().Velocity()=Vector2d();
    intersector.AddPermeable(&shipGroup->explosion,createTime+existTime);
    shipGroup->explosion.frameList.Reset();
    #ifdef X11_BACKEND
    int destX=shipGroup->explosion.CurrentBox().NorthWestCorner().x+.5,
        destY=shipGroup->explosion.CurrentBox().NorthWestCorner().y+.5;
    // The historical draw wired the CENTER clip bitmap (frame slot 0)
    // regardless of which frame is shown; MaskId(0) pins that exact choice.
    // Do NOT "fix" this to MaskId(frame number) — it would change pixels.
    playingField->DeferDrawMasked(shipGroup->explosion.frameList.GetFrame(),
                                  explosionGraphic->MaskId(0),
                                  destX,destY,
                                  shipGroup->explosion.frameList.PixWidth(),
                                  shipGroup->explosion.frameList.PixHeight());
    #endif
    shipGroup->explosion.frameList.UpdateFrameNumber();
    shipGroup->explosion.frameTimer.Reset().TimerAlarm();
   }
 }

#define max(a,b) (a>b?a:b)
inline const int ShipGroup::maxIconHeight=max(max(starDestroyerIcon_height,
                                           NCC1701DIcon_height),
                                       NCC1701AIcon_height),
          ShipGroup::maxIconWidth=max(max(starDestroyerIcon_width,
                                          NCC1701DIcon_width),
                                      NCC1701AIcon_width);
#undef max

inline Ship* ShipGroup::ship=nullptr;

inline Thrust* ShipGroup::thrust=nullptr;

inline void ShipGroup::NewShip()
 {ship->ObjectRotator().Angle()=0;
  hyper.CurrentBox().MoveBox(PlayingField::playArea.Center()
                             -hyper.CurrentBox().Center());
  hyper.hyperCount=-Hyper::destructRate;
  hyper.hyperspacing=Hyper::on;
  ship->dead=Ship::off;
  shipBulletGroup->fireThisFrame=ShipBulletGroup::off;
  playingField->permeableList.SetCurrentAtHole().GetData()=&hyper;
  playingField->permeableList.SetCurrentAtHead().AddPrior();
 }

inline void ShipGroup::NewThrust()
 {thrust->CurrentBox().MoveBox(ship->CurrentBox().Center()-
                              thrust->CurrentBox().Center());
  thrust->ObjectLiner().Velocity()=ship->ObjectLiner().Velocity();
  Vector2d accelDirection(ship->ObjectRotator().GetVecsAtTime(0)[0]);
  thrust->ObjectLiner().Acceleration()=ship->ObjectLiner().Acceleration()
                                     =ship->acceleration*accelDirection/accelDirection.Magnitude();
  thrust->ObjectRotator().Angle()=ship->ObjectRotator().Angle();
  thrust->ObjectRotator().AngularVelocity()=ship->ObjectRotator().AngularVelocity();
 }

inline void ShipGroup::NewHyper()
 {hyper.CurrentBox().MoveBox(Vector2d((PlayingField::playArea.Width()
                                       -2*ship->ObjectRotator().Radius())*gary_rand::rand_16()/(RAND_MAX_16+1)
                                      +ship->ObjectRotator().Radius(),
                                      (PlayingField::playArea.Height()
                                       -2*ship->ObjectRotator().Radius())*gary_rand::rand_16()/(RAND_MAX_16+1)
                                      +ship->ObjectRotator().Radius())
			                       +PlayingField::playArea.NorthWestCorner()
                             -hyper.CurrentBox().Center());
  hyper.hyperspacing=Hyper::on;
  playingField->permeableList.SetCurrentAtHole().GetData()=&hyper;
  playingField->permeableList.SetCurrentAtHead().AddPrior();
 }

inline void ShipGroup::ResetAngularVelocity(const double newAngularVelocity)
 {starDestroyer.ObjectRotator().AngularVelocity()=
  starDestroyerThrust.ObjectRotator().AngularVelocity()=
  NCC1701A.ObjectRotator().AngularVelocity()=
  NCC1701AThrust.ObjectRotator().AngularVelocity()=
  NCC1701D.ObjectRotator().AngularVelocity()=
  NCC1701DThrust.ObjectRotator().AngularVelocity()=newAngularVelocity;
 }

inline void ShipGroup::ResetAccelerationLimits(const double newMaxAcceleration)
 {starDestroyer.ObjectLiner().MaxAccelerationMagnitude()=
  starDestroyerThrust.ObjectLiner().MaxAccelerationMagnitude()=
  NCC1701A.ObjectLiner().MaxAccelerationMagnitude()=
  NCC1701AThrust.ObjectLiner().MaxAccelerationMagnitude()=
  NCC1701D.ObjectLiner().MaxAccelerationMagnitude()=
  NCC1701DThrust.ObjectLiner().MaxAccelerationMagnitude()=newMaxAcceleration;
 }

inline void ShipGroup::ResetVelocityLimits(const double newMaxVelocity)
 {starDestroyer.ObjectLiner().MaxVelocityMagnitude()=
  starDestroyerThrust.ObjectLiner().MaxVelocityMagnitude()=
  NCC1701A.ObjectLiner().MaxVelocityMagnitude()=
  NCC1701AThrust.ObjectLiner().MaxVelocityMagnitude()=
  NCC1701D.ObjectLiner().MaxVelocityMagnitude()=
  NCC1701DThrust.ObjectLiner().MaxVelocityMagnitude()=
  explosion.ObjectLiner().MaxVelocityMagnitude()=newMaxVelocity;
  starDestroyer.ObjectRotator().UpdateMaxLinearVelocity(PlayingField::maxLinearVelocity);
  NCC1701A.ObjectRotator().UpdateMaxLinearVelocity(PlayingField::maxLinearVelocity);
  NCC1701D.ObjectRotator().UpdateMaxLinearVelocity(PlayingField::maxLinearVelocity);
 }

inline void ShipGroup::AdjustFramesPerSec(const double frameRatio)
 {ship->ObjectLiner().Velocity()=ship->ObjectLiner().Velocity()*frameRatio;
  ship->ObjectLiner().Acceleration()=ship->ObjectLiner().Acceleration()*frameRatio*frameRatio;
  ship->ObjectRotator().AngularVelocity()=ship->ObjectRotator().AngularVelocity()*frameRatio;
  thrust->ObjectLiner().Velocity()=ship->ObjectLiner().Velocity();
  thrust->ObjectLiner().Acceleration()=ship->ObjectLiner().Acceleration();
  thrust->ObjectRotator().AngularVelocity()=ship->ObjectRotator().AngularVelocity();
  explosion.ObjectLiner().Velocity()=explosion.ObjectLiner().Velocity()*frameRatio;
  explosion.ObjectLiner().Acceleration()=
  explosion.ObjectLiner().Acceleration()*frameRatio*frameRatio;
  explosion.frameDelay.framesTillAlarm=double(explosion.frameDelay.framesTillAlarm)/frameRatio+.5;
 }

#endif
