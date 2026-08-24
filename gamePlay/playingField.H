#include"stage.H"
#include"options/options.H"
#include"../objects/ships/shipBulletGroup.H"
#include"../objects/ships/shipGroup.H"
#include"../objects/enemies/enemyBulletGroup.H"
#include"../objects/enemies/enemyGroup.H"
#include"../objects/rocks/rockGroup.H"

#ifndef _PLAYINGFIELD_H
#define _PLAYINGFIELD_H

#include<stdlib.h>
#include<stdio.h>
#include<iostream>
#include<sstream>
#include<iomanip>
#include<string.h>
#include<sys/time.h>
#ifdef X11_BACKEND
#include<X11/Xlib.h>
#include<X11/Xutil.h>
#include"../utilities/rendering/x11Backend.H"
#endif
#include"../utilities/rendering/x11types.H"
#include"../utilities/rendering/renderingEngine.H"
#include"../utilities/vector2d.H"
#include"../utilities/box.H"
#include"../utilities/linkedArray.H"
#include"../objects/movableObject.H"
#include"../utilities/intersection2d.H"

class PlayingField
 {friend class Options;

  public:
    typedef int Boolean;
    enum {off, on};

    // Transition member (T19/T21 pattern): the engine reference every
    // migrated site draws through; DELETED at task 47 with the other
    // transition aliases.
    RenderingEngine& engine;

    PlayingField(RenderingEngine& engine);
    ~PlayingField();
    int PlayTheGame(const int level, int argc, char** argv);
    PlayingField& AddShip();
    PlayingField& RemoveShip();
    void AlterExtraShipScore(const int newExtraShipScore);
    void AlterUSecondsPerFrame(const int newUSecondsPerFrame);

    GC gc;
    Pixmap pixmap;

    Boolean lastHitFromAlly;
    int lastHitScore,
        framesSinceLastBullet,
        framesSinceEnemy,
        level;

    LinkedArray<MovableObject*> nonPermeableList;
    LinkedArray<MovableObject*> selfPermeableList;
    LinkedArray<MovableObject*> permeableList;

    static int uSecondsPerFrame,
               initialShips,
               extraShipScore;

    static double maxLinearVelocity,
                  universalGravitationalConst;

    static Boolean enemiesExist,
                   relativisticMass;

    static const Box playArea;


  private:

#ifdef X11_BACKEND
    Boolean RunGame(Options& options);
#else
    class OptionsMenu;
    Boolean RunGame(RenderingEngine& engine, OptionsMenu& menu);
#endif
    void DrawGame();
    void NewRocksAndEnemy();
    Vector2d CalcGravityAcceleration(const MovableObject* const, const MovableObject* const);
    void SetGravityAcceleration();
    timeval ResumePlay(Intersector&);
    void GenHelpScreen();

    int fontHeight,
        framesThisLevel,
        nextShipScore;

    double speedAdjust;

#ifdef X11_BACKEND
    Atom WM_PROTOCOLS,
         WM_DELETE_WINDOW;
#endif

    Intersector intersector;

    Boolean leftRotPending,
            rightRotPending,
            firing;

 };

extern PlayingField* playingField;

inline int PlayingField::uSecondsPerFrame=62500,
    PlayingField::initialShips=3,
    PlayingField::extraShipScore=10000;

inline double PlayingField::maxLinearVelocity=28,
       PlayingField::universalGravitationalConst=0;

inline PlayingField::Boolean PlayingField::enemiesExist=PlayingField::on,
                      PlayingField::relativisticMass=PlayingField::off;

inline const Box PlayingField::playArea(640,512,Vector2d(320+2*ROCK::scale,256+2*ROCK::scale));

inline PlayingField::PlayingField(RenderingEngine& eng): engine(eng),
#ifdef X11_BACKEND
                                     gc(static_cast<X11Backend&>(eng).gxorGC()),
                                     pixmap(static_cast<X11Backend&>(eng).canvas()),
#endif
                                     lastHitFromAlly(off),
                                     lastHitScore(0),
#ifdef X11_BACKEND
                                     fontHeight(stage->hiScoreInfo->ascent+stage->hiScoreInfo->descent+4),
#else
                                     fontHeight(0),
#endif
                                     framesSinceLastBullet(0),
                                     framesThisLevel(0),
                                     nextShipScore(extraShipScore),
                                     framesSinceEnemy(0),
                                     speedAdjust(uSecondsPerFrame/125E3),
                                     level(0),
#ifdef X11_BACKEND
                                     WM_PROTOCOLS(static_cast<X11Backend&>(eng).wmProtocolsAtom()),
                                     WM_DELETE_WINDOW(static_cast<X11Backend&>(eng).wmDeleteWindowAtom()),
#endif
                                     nonPermeableList(28),
                                     selfPermeableList(128),
                                     permeableList(28),
                                     intersector(nonPermeableList,selfPermeableList,permeableList),
                                     leftRotPending(off),
                                     rightRotPending(off),
                                     firing(off)
 {GenHelpScreen();
 }

inline PlayingField::~PlayingField()
 {// Disarmed at task 15 (D15): the canvas and both GCs are backend-owned
  // now and are released in shutdown; this dtor keeps layout+logic only.
  }

inline PlayingField& PlayingField::AddShip()
 {if (!shipGroup->hyper.hyperspacing)
    intersector.AddNonPermeable(shipGroup->ship,0);
  return *this;
 }

inline PlayingField& PlayingField::RemoveShip()
 {leftRotPending=rightRotPending=firing=off;
  intersector.RemoveNonPermeable(shipGroup->ship);
  if (shipGroup->thrust->thrusting)
   {intersector.RemovePermeable(shipGroup->thrust);
    shipGroup->ship->ObjectLiner().Acceleration()=Vector2d();
    shipGroup->thrust->thrusting=Thrust::off;
   }
  return *this;
 }

inline void PlayingField::NewRocksAndEnemy()
 {++framesSinceLastBullet;
  if (framesThisLevel*speedAdjust<3600)
    ++framesThisLevel;
  ++framesSinceEnemy;
  if (selfPermeableList.GetNumItems())
   {int framesThisLevelDenominator=14400*(8-level+selfPermeableList.GetNumItems());
    if (enemiesExist&&enemyGroup->enemyInactive&&
	      gary_rand::rand_16()<(RAND_MAX_16*framesThisLevel
                              /(framesThisLevelDenominator<=0 ? 1 : framesThisLevelDenominator)
                              +framesSinceEnemy/1024+framesSinceLastBullet)*speedAdjust)
      enemyGroup->NewEnemy(level);
   }
  else
   {int nonRockItems=nonPermeableList.GetNumItems()+permeableList.GetNumItems();
    if (!shipGroup->ship->dead&&(nonRockItems==1||
                               shipGroup->thrust->thrusting&&nonRockItems==2)||
        !nonRockItems)
      {rockGroup->NewROCKs();
       level++;
       framesThisLevel=0;
      }
   }
 }

inline void PlayingField::DrawGame()
 {stage->DrawScore();
  if (score->CurrentScore()>=nextShipScore)
   {shipYard->AddShip();
    stage->DrawShipYard();
    nextShipScore+=extraShipScore;
   }
   stage->DrawPlayingField();
  #ifdef X11_BACKEND
   engine.beginRenderTo((TextureId)pixmap);
   engine.clear();
   engine.endRenderTo();
  #endif
 }

 inline Vector2d PlayingField::CalcGravityAcceleration(const MovableObject* const obj1,
                                                       const MovableObject* const obj2)
 {Vector2d dist=obj2->CurrentBox().Center()-obj1->CurrentBox().Center();
  double distMagSquared=dist.MagnitudeSquared(),
         distMag,
         radiusAverage=.5*(obj1->ObjectRotator().Radius()+obj2->ObjectRotator().Radius()),
         radiusAverageSquared=radiusAverage*radiusAverage,
         c,
         cSquared,
         mass=relativisticMass ? (c=maxLinearVelocity*1.001,
                                  cSquared=c*c,
                                  obj2->ObjectRotator().Area()*sqrt(cSquared/(cSquared-obj2->ObjectLiner().VelocityMagnitude()
                                                                                       *obj2->ObjectLiner().VelocityMagnitude())))
                               : obj2->ObjectRotator().Area(),
         uSecondPerFrameRatio;
  return distMagSquared ? (distMag=sqrt(distMagSquared),
                           uSecondPerFrameRatio=uSecondsPerFrame/6.25E4, // Scale the acceleration to the currwnt frame rate of the game. 16 fps is baseline.
                           (dist/distMag)*universalGravitationalConst
                           *(distMag<radiusAverage ? mass*distMag
                                                     /(radiusAverageSquared*radiusAverage) // Overlapping object gravitational acceleration
                                                   : mass/distMagSquared) // Non-overlapping object gravitational accelaration
                           *uSecondPerFrameRatio*uSecondPerFrameRatio)
                        : Vector2d();
 }

inline void PlayingField::SetGravityAcceleration()
 {if (universalGravitationalConst)
   {nonPermeableList.SetCurrentAtHead();
    for (int i=0;i<nonPermeableList.GetNumItems();++i)
     {Vector2d gravityAcceleration;
      const int itemIndex=nonPermeableList.GetItemIndex();
      MovableObject* const item1Addr=nonPermeableList.GetData();
      nonPermeableList.SetCurrentAtHead();
      for (int j=0;j<nonPermeableList.GetNumItems();++j)
       {const MovableObject* const item2Addr=nonPermeableList.GetData();
        if (item1Addr!=item2Addr)
          gravityAcceleration+=CalcGravityAcceleration(item1Addr,item2Addr);
        nonPermeableList.Next();
       }
      selfPermeableList.SetCurrentAtHead();
      for (int j=0;j<selfPermeableList.GetNumItems();++j)
       {gravityAcceleration+=CalcGravityAcceleration(item1Addr,
                                                     selfPermeableList.GetData());
        selfPermeableList.Next();
       }
      if (item1Addr==shipGroup->ship&&shipGroup->thrust->thrusting)
       {shipGroup->ship->ObjectLiner().Acceleration()=shipGroup->ship->ObjectLiner().Acceleration()
                                                     +gravityAcceleration;
        shipGroup->thrust->ObjectLiner().Acceleration()=shipGroup->ship->ObjectLiner().Acceleration();
       }
      else
        item1Addr->ObjectLiner().Acceleration()=gravityAcceleration;
      nonPermeableList.SetCurrentAtItem(itemIndex).Next();
     }
    selfPermeableList.SetCurrentAtHead();
    for (int i=0;i<selfPermeableList.GetNumItems();++i)
     {Vector2d gravityAcceleration;
      const int itemIndex=selfPermeableList.GetItemIndex();
      MovableObject* const item1Addr=selfPermeableList.GetData();
      nonPermeableList.SetCurrentAtHead();
      for (int j=0;j<nonPermeableList.GetNumItems();++j)
       {gravityAcceleration+=CalcGravityAcceleration(item1Addr,
                                                     nonPermeableList.GetData());
        nonPermeableList.Next();
       }
      selfPermeableList.SetCurrentAtHead();
      for (int j=0;j<selfPermeableList.GetNumItems();++j)
       {const MovableObject* const item2Addr=selfPermeableList.GetData();
        if (item1Addr!=item2Addr)
          gravityAcceleration+=CalcGravityAcceleration(item1Addr,item2Addr);
        selfPermeableList.Next();
       }
      item1Addr->ObjectLiner().Acceleration()=gravityAcceleration;
      selfPermeableList.SetCurrentAtItem(itemIndex).Next();
     }
   }
 }

inline timeval PlayingField::ResumePlay(Intersector& intersector)
 {
  #ifdef X11_BACKEND
  XRaiseWindow(stage->display,stage->window);
  #endif
  timeval time;
  gettimeofday(&time,NULL);
  leftRotPending=rightRotPending=firing=off;
  shipGroup->ship->ObjectRotator().AngularVelocity()=0;
  if (shipGroup->thrust->thrusting)
   {intersector.RemovePermeable(shipGroup->thrust);
    shipGroup->ship->ObjectLiner().Acceleration()=Vector2d();
    shipGroup->thrust->thrusting=Thrust::off;
   }
  #ifdef X11_BACKEND
  XSync(stage->display,False);
  #endif
  return time;
 }

#ifdef X11_BACKEND
inline PlayingField::Boolean PlayingField::RunGame(Options& options)
 {XRaiseWindow(stage->display,stage->window);
  nonPermeableList.Reset();
  selfPermeableList.Reset();
  permeableList.Reset();
  score->Reset();
  framesSinceLastBullet=0;
  framesThisLevel=0;
  nextShipScore=extraShipScore;
  framesSinceEnemy=0;
  for (int i=0;i<initialShips;++i)
    shipYard->AddShip();
  engine.beginRenderTo((TextureId)pixmap);
  engine.clear();
  engine.endRenderTo();
  NewRocksAndEnemy();
  DrawGame();
  while(shipYard->NumShips())
   {shipYard->RemoveShip();
    stage->DrawShipYard();
    shipGroup->NewShip();
    leftRotPending=rightRotPending=firing=off;
    while(nonPermeableList.GetNumItems()||permeableList.GetNumItems())
     {timeval startTime,
              endTime;
      gettimeofday(&startTime,NULL);
      NewRocksAndEnemy();
      if (ShipBulletGroup::autoFire&&firing)
        shipBulletGroup->fireThisFrame=ShipBulletGroup::on;
      XSync(stage->display,False);
      XEvent event;
      char keyText[2];
      while(XEventsQueued(stage->display,QueuedAfterReading))
       {XNextEvent(stage->display,&event);
        switch(event.type)
         {case MappingNotify:   XRefreshKeyboardMapping((XMappingEvent*)&event);
                                break;
	        case LeaveNotify:     if (stage->autoRepeatState==AutoRepeatModeOn)
	                                XAutoRepeatOn(stage->display);
                                for(;;)
	                               {XNextEvent(stage->display,&event);
                                  if (event.type==EnterNotify)
				                           {XAutoRepeatOff(stage->display);
                                    startTime=ResumePlay(intersector);
                                    break;
                                   }
                                  else
				                            switch (event.type)
				                             {case MappingNotify: XRefreshKeyboardMapping((XMappingEvent*)&event);
                                                          break;
                                      case Expose:
                                      case MapNotify:     stage->Refresh();
                                                          break;
                                     }
	                               }
                                break;
	        case Expose:
	        case ConfigureNotify: stage->Refresh();
                                break;
          case KeyPress:        if (XLookupString((XKeyEvent*)&event,keyText,2,NULL,NULL)==1)
                                  if (!shipGroup->hyper.hyperspacing)
                                    switch(keyText[0])
                                     {case 'e': if (shipGroup->ship->ObjectRotator().AngularVelocity())
                                                  leftRotPending=True;
                                                else
				                                         {shipGroup->ship->ObjectRotator().AngularVelocity()=
                                                    -shipGroup->ship->angularVelocity;
                                                  if (shipGroup->thrust->thrusting)
                                                    shipGroup->thrust->ObjectRotator().AngularVelocity()=
                                                      -shipGroup->ship->angularVelocity;
				                                         }
                                                break;
				                              case 'r': if (shipGroup->ship->ObjectRotator().AngularVelocity())
                                                  rightRotPending=True;
                                                else
                        				                 {shipGroup->ship->ObjectRotator().AngularVelocity()=
                                                    shipGroup->ship->angularVelocity;
                                                  if (shipGroup->thrust->thrusting)
                                                    shipGroup->thrust->ObjectRotator().AngularVelocity()=
                                                      shipGroup->ship->angularVelocity;
				                 }
                                                break;
                                      case 'o': shipGroup->NewThrust();
                                                shipGroup->thrust->thrusting=Thrust::on;
                                                intersector.AddPermeable(shipGroup->thrust,0);
                                                break;
                                      case 'p': shipBulletGroup->fireThisFrame=ShipBulletGroup::on;
                                                firing=on;
                                                break;
                                      case ' ': RemoveShip();
                                                shipGroup->NewHyper();
                                                break;
	                                    case 'q': return on;
                                                break;
                                      case 'n':
                                      case 'h': goto ResetGameUpdateScoreAndReturn;
                                     }
                                  else
                                    switch(keyText[0])
                                     {case 'q': return on;
                                                break;
                                      case 'n':
                                      case 'h': goto ResetGameUpdateScoreAndReturn;
				                             }
                                break;
          case KeyRelease:      if (!shipGroup->hyper.hyperspacing)
                                  if (XLookupString((XKeyEvent*)&event,keyText,2,NULL,NULL)==1)
                                    switch (keyText[0])
                                     {case 'e': if (shipGroup->ship->ObjectRotator().AngularVelocity()<0)
                                                  if (rightRotPending)
                                                   {shipGroup->ship->ObjectRotator().AngularVelocity()=
                                                      shipGroup->ship->angularVelocity;
                                                    if (shipGroup->thrust->thrusting)
                                                      shipGroup->thrust->ObjectRotator().AngularVelocity()=
                                                        shipGroup->ship->angularVelocity;
                                                   }
                                                  else
                                                   {shipGroup->ship->ObjectRotator().AngularVelocity()=0;
                                                    if (shipGroup->thrust->thrusting)
                                                      shipGroup->thrust->ObjectRotator().AngularVelocity()=0;
                                                   }
                                                leftRotPending=False;
                                                break;
                                      case 'r': if (shipGroup->ship->ObjectRotator().AngularVelocity()>0)
                                                  if (leftRotPending)
                                                   {shipGroup->ship->ObjectRotator().AngularVelocity()=
                                                      -shipGroup->ship->angularVelocity;
                                                    if (shipGroup->thrust->thrusting)
                                                      shipGroup->thrust->ObjectRotator().AngularVelocity()=
                                                        -shipGroup->ship->angularVelocity;
                                                   }
                                                  else
                                                   {shipGroup->ship->ObjectRotator().AngularVelocity()=0;
                                                    if (shipGroup->thrust->thrusting)
                                                      shipGroup->thrust->ObjectRotator().AngularVelocity()=0;
                                                   }
                                                rightRotPending=False;
                                                break;
                                      case 'o': intersector.RemovePermeable(shipGroup->thrust);
                                                shipGroup->ship->ObjectLiner().Acceleration()=Vector2d();
                                                shipGroup->thrust->thrusting=Thrust::off;
                                                break;
                                      case 'p': shipBulletGroup->fireThisFrame=ShipBulletGroup::off;
                                                firing=off;
                                                break;
                                     }
                                break;
          case ButtonPress:     if (((XButtonPressedEvent*)&event)->button==Button1)
                                 {Boolean pointerButtonReleased=off,
                                          pointerInWindow=on;
                                  if (button->PointerOnButton(*(XButtonEvent*)&event))
	                                  button->PressButton();
                                  for (;;)
                                   {XNextEvent(stage->display,&event);
                                    if (event.type==ButtonRelease)
                                     {if (((XButtonReleasedEvent*)&event)->button==Button1)
                                       {if (button->PointerOnButton(*(XButtonEvent*)&event))
                                          {
#ifdef X11_BACKEND
                                           options.RealizeWindow();
#endif
                                           button->ReleaseButton();
                                          }
                                        pointerButtonReleased=on;
                                        if (pointerInWindow)
                                          break;
                                       }
                                     }
                                    else
                                      if (event.type==EnterNotify)
                                       {pointerInWindow=on;
                                        XAutoRepeatOff(stage->display);
                                        if (pointerButtonReleased)
                                          break;
                                       }
                                      else
                                        switch (event.type)
                                         {case MappingNotify:   XRefreshKeyboardMapping((XMappingEvent*)&event);
                                                                break;
                                          case LeaveNotify:     if (stage->autoRepeatState==AutoRepeatModeOn)
                                                                  XAutoRepeatOn(stage->display);
                                                                pointerInWindow=off;
                                                                break;
                                          case Expose:
                                          case MapNotify:       stage->Refresh();
                                                                break;
                                          case MotionNotify:    if (button->PointerOnButton(*(XPointerMovedEvent*)&event))
                                                                  button->PressButton();
                                                                else
                                                                  button->ReleaseButton();
                                                                break;
                                         }
                                   }
                                  startTime=ResumePlay(intersector);
                                 }
                                break;
          case ClientMessage:   if (((XClientMessageEvent*)&event)->message_type==WM_PROTOCOLS&&
                                    ((XClientMessageEvent*)&event)->data.l[0]==WM_DELETE_WINDOW)
                                  return on;
                                break;
         }
       }
      SetGravityAcceleration();
      intersector.Intersect();
      DrawGame();
      gettimeofday(&endTime,NULL);
      unsigned int diffTime=endTime.tv_usec-startTime.tv_usec+(endTime.tv_sec-startTime.tv_sec)*1000000;
      if (diffTime<uSecondsPerFrame)
        usleep(uSecondsPerFrame-diffTime);
     }
   }
  ResetGameUpdateScoreAndReturn: shipYard->ClearYard();
  stage->DrawShipYard();
  shipBulletGroup->Reset();
  enemyGroup->Reset();
  enemyBulletGroup->Reset();
  score->UpdateTopTen();
  int xPos=playArea.NorthWestCorner().x+(playArea.Width()-stage->hiScoreInfo->max_bounds.width*22)/2
           -stage->hiScoreInfo->max_bounds.lbearing,
      yPosSave=playArea.NorthWestCorner().y+(playArea.Height()-15*fontHeight+4)/2+stage->hiScoreInfo->ascent,
      yPos=yPosSave;
  char str[23];
  engine.beginRenderTo((TextureId)pixmap);
  for(int i=0;i<score->NumScores();++i)
   {const int nameWidth=12-(int)strlen(score->TopTenName(i));
    snprintf(str,sizeof(str),"%s%*s%10d",
             score->TopTenName(i),nameWidth>0?nameWidth:1,"",
             score->TopTenScore(i));
    engine.drawStringTransparent(str,(float)xPos,(float)yPos,
                                 EngineFont_HiScore,1.0f,1.0f,1.0f);
    yPos+=fontHeight;
   }
  engine.drawStringTransparent("        S -- start",(float)xPos,
                               (float)(yPos=yPosSave+12*fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        Q -- quit",(float)xPos,
                               (float)(yPos+=fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        H -- help",(float)xPos,
                               (float)(yPos+=fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.endRenderTo();
  stage->DrawPlayingField();
  return off;
  }
#endif

inline int PlayingField::PlayTheGame(const int l, int argc, char** argv)
 {
  #ifdef X11_BACKEND
  // The WM-hints block (naming, protocol registration, input selection,
  // map-and-raise) relocated into the backend's pass-2 init at task 15
  // (D-C C1); its two recorded deltas live there (argv/argc as NULL,0;
  // map-and-raise at init). The size pin is dropped entirely (v6/S2/D15).
  Options options(argc,argv);
  XEvent event;
  int localHighScore=0;
  Boolean pointerButtonReleased=on,
          buttonActivated=off;
  for(;;)
   {XNextEvent(stage->display,&event);
    switch(event.type)
     {case MappingNotify:   XRefreshKeyboardMapping((XMappingEvent*)&event);
                            break;
      case LeaveNotify:     if (stage->autoRepeatState==AutoRepeatModeOn)
                              XAutoRepeatOn(stage->display);
                            break;
      case EnterNotify:     XAutoRepeatOff(stage->display);
                            break;
      case Expose:
      case MapNotify:
      case ConfigureNotify: stage->Refresh();
                            break;
      case KeyPress:        if (pointerButtonReleased)
                             {char keyText[2];
                              if (XLookupString((XKeyEvent*)&event,keyText,2,NULL,NULL)==1)
                                switch(keyText[0])
                                  {case 's': level=l;
                                             Boolean earlyExit;
#ifdef X11_BACKEND
                                             earlyExit=RunGame(options);
#endif
                                             if (localHighScore<score->CurrentScore())
                                              localHighScore=score->CurrentScore();
                                            if (earlyExit)
                                              return localHighScore;
                                            break;
                                  case 'q': return localHighScore;
                                            break;
                                  case 'h': GenHelpScreen();
                                            stage->DrawPlayingField();
                                            break;
                                 }
                             }
                            break;
      case ButtonPress:     if (((XButtonPressedEvent*)&event)->button==Button1)
                             {pointerButtonReleased=off;
                              if (button->PointerOnButton(*(XButtonEvent*)&event))
                               {button->PressButton();
                                buttonActivated=on;
                               }
                             }
                            break;
      case ButtonRelease:   if (((XButtonReleasedEvent*)&event)->button==Button1)
                              {if (buttonActivated &&
                                   button->PointerOnButton(*(XButtonEvent*)&event))
                                {
#ifdef X11_BACKEND
                                 options.RealizeWindow();
#endif
                                 button->ReleaseButton();
                                }
                              buttonActivated=off;
                              pointerButtonReleased=on;
                             }
                            break;
      case MotionNotify:    if (buttonActivated &&
                                button->PointerOnButton(*(XPointerMovedEvent*)&event))
                              button->PressButton();
                            else
                              button->ReleaseButton();
                            break;
      case ClientMessage:   if (((XClientMessageEvent*)&event)->message_type==WM_PROTOCOLS&&
                                ((XClientMessageEvent*)&event)->data.l[0]==WM_DELETE_WINDOW)
                              return localHighScore;
                            break;
     }
    }
  #endif
  return 0;
 }

void PlayingField::GenHelpScreen()
 {
  #ifdef X11_BACKEND
  engine.beginRenderTo((TextureId)pixmap);
  engine.clear();
  engine.endRenderTo();
  int xPos=playArea.NorthWestCorner().x+(playArea.Width()
                                         -(int)engine.measureText("        H -- help (this screen)",
                                                                  EngineFont_HiScore))/2
           -stage->hiScoreInfo->max_bounds.lbearing,
      yPos=playArea.NorthWestCorner().y+(playArea.Height()-12*fontHeight+4)/2+stage->hiScoreInfo->ascent;
  engine.beginRenderTo((TextureId)pixmap);
  engine.drawStringTransparent("        S -- start",(float)xPos,(float)yPos,
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        Q -- quit",(float)xPos,
                               (float)(yPos+=fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        E -- rotate left",(float)xPos,
                               (float)(yPos+=2*fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        R -- rotate right",(float)xPos,
                               (float)(yPos+=fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        O -- thrust",(float)xPos,
                               (float)(yPos+=fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        P -- fire",(float)xPos,
                               (float)(yPos+=fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("space bar -- hyperspace",(float)xPos,
                               (float)(yPos+=fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        N -- new game",(float)xPos,
                               (float)(yPos+=2*fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        H -- help (this screen)",(float)xPos,
                               (float)(yPos+=2*fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.endRenderTo();
  #endif
 }

inline void PlayingField::AlterExtraShipScore(const int newExtraShipScore)
 {nextShipScore=(score->CurrentScore()/newExtraShipScore+1)*newExtraShipScore;
 }

inline void PlayingField::AlterUSecondsPerFrame(const int newUSecondsPerFrame)
 {uSecondsPerFrame=newUSecondsPerFrame;
  speedAdjust=uSecondsPerFrame/125E3;
 }

#endif
