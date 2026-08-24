#include<stdlib.h>
#include<new>
#include<iostream>
#include<stdio.h>
#include<sys/time.h>
#include<unistd.h>
#ifdef X11_BACKEND
#include"utilities/rendering/x11Backend.H"
#elif defined(GL_BACKEND)
#include"utilities/rendering/glBackend.H"
#endif
#include"gamePlay/stage.H"
#include"gamePlay/score.H"
#include"gamePlay/shipYard.H"
#include"objects/explosions/explosionGraphic.H"
#include"objects/ships/shipGroup.H"
#include"objects/ships/shipBulletGroup.H"
#include"objects/rocks/rockGroup.H"
#include"objects/enemies/enemyGroup.H"
#include"objects/enemies/enemyBulletGroup.H"
#include"gamePlay/playingField.H"
Stage* stage = nullptr;
Button* button = nullptr;
Score* score = nullptr;
ExplosionGraphic* explosionGraphic = nullptr;
ShipGroup* shipGroup = nullptr;
ShipYard* shipYard = nullptr;
ShipBulletGroup* shipBulletGroup = nullptr;
RockGroup* rockGroup = nullptr;
EnemyGroup* enemyGroup = nullptr;
EnemyBulletGroup* enemyBulletGroup = nullptr;
PlayingField* playingField = nullptr;

int main (int argc, char *argv[])
 {
  #ifdef X11_BACKEND
  // main() owns the engine lifecycle (D11): construct the concrete backend,
  // two-pass initWindow (exit(1) on failure — M12), then inject the engine
  // into every global through its single RenderingEngine& constructor.
  X11Backend engine;
  engine.setCanonicalLayout(PlayingField::playArea.Width(),
                            PlayingField::playArea.Height(),
                            ShipGroup::maxIconHeight);
  if (!engine.initWindow("Asteroids"))
   {fprintf(stderr,"XAsteroids: initialization failed.\n");
    return 1;
   }
  stage = new Stage(engine);
  button = new Button(engine,
                      "Options",
                      stage->buttonX,stage->buttonY);
  score = new Score;
  explosionGraphic = new ExplosionGraphic(engine);
  // The ::operator new + placement-new staging for the three groups is the
  // task-6 static-init mechanism and stays: Hyper's ctor dereferences
  // ShipGroup::ship mid-construction, and the bullet groups take intra-object
  // addresses in their init lists — the pointers must be valid pre-ctor
  // exactly as before. Only the ctor calls gain the engine argument.
  shipGroup = (ShipGroup*)::operator new(sizeof(ShipGroup));
  ShipGroup::ship=&shipGroup->starDestroyer;
  ShipGroup::thrust=&shipGroup->starDestroyerThrust;
  new (shipGroup) ShipGroup(engine);
  shipYard = new ShipYard(engine,
                          shipGroup->ship->icon,
                          shipGroup->ship->iconWidth,
                          shipGroup->ship->iconHeight,
                          shipGroup->ship->iconColor.red,
                          shipGroup->ship->iconColor.green,
                          shipGroup->ship->iconColor.blue,
                          stage->shipYardWidth,stage->shipYardHeight);
  shipBulletGroup = (ShipBulletGroup*)::operator new(sizeof(ShipBulletGroup));
  new (shipBulletGroup) ShipBulletGroup(engine);
  rockGroup = new RockGroup(engine);
  enemyGroup = new EnemyGroup(engine);
  enemyBulletGroup = (EnemyBulletGroup*)::operator new(sizeof(EnemyBulletGroup));
  new (enemyBulletGroup) EnemyBulletGroup(engine);
  playingField = new PlayingField(engine);

  cout<<"Your highest score this game was "<<playingField->PlayTheGame(argc>1 ? atoi(argv[1])
                                                                                    : 1,
                                                                             argc, argv)<<'.'<<endl;

  delete playingField;
  delete enemyBulletGroup;
  delete enemyGroup;
  delete rockGroup;
  delete shipBulletGroup;
  delete shipYard;
  delete shipGroup;
  delete explosionGraphic;
  delete score;
  delete button;
  delete stage;
  engine.shutdown();
  return 0;
  #endif
  #ifdef GL_BACKEND
  // Task-31 stub loop (M5-M1 relocated clause): engine init + a paced
  // beginFrame/clear/endFrame loop under Xvfb. Non-interactive rendering —
  // primitives/text/textures/rotation land at tasks 32-35; the full scripted
  // session identity assertion is Q10 at task 36.
  GLBackend engine;
  engine.setCanonicalLayout(PlayingField::playArea.Width(),
                            PlayingField::playArea.Height(),
                            ShipGroup::maxIconHeight);
  if (!engine.initWindow("Asteroids"))
   {fprintf(stderr,"XAsteroids: initialization failed.\n");
    return 1;
   }
   {const long uSecondsPerFrame=62500;          // D4 default (16 fps)
    timeval start,now;
    gettimeofday(&start,nullptr);
    const char* frameLog=getenv("XAST_FRAME_LOG");
    const char* smoke=getenv("XAST_GL_SMOKE");  // task-32 acceptance scenes
    const bool smokeOn=smoke&&smoke[0];
    for (int frame=0;frame<160;++frame)
     {engine.beginFrame();
      engine.clear();                       // D8 flow: clear every frame
      if (smokeOn)
       {if (!strcmp(smoke,"thick"))
          engine.drawLine(100.0f,100.0f,200.0f,100.0f,1,1,1,3);
        else if (!strcmp(smoke,"scissor"))
         {const int rect[4]={50,50,100,100};
          engine.setScissorRect(rect);
          engine.drawRect(0.0f,0.0f,300.0f,300.0f,1,1,1,true);
          engine.setScissorRect(nullptr);
         }
        else if (!strcmp(smoke,"rot90"))
         {// A square rotated 90° about its center is pixel-identical to the
          // unrotated square — the identity assertion for the MVP path.
          engine.setTransform(200.0f,150.0f,1.5707963267948966f);
          engine.drawRect(-25.0f,-25.0f,50.0f,50.0f,1,1,1,true);
          engine.resetTransform();
         }
        else // "scene": determinism golden (fixed composition)
         {engine.drawLine(80.0f,80.0f,300.0f,80.0f,1,1,1,1);
          engine.drawLine(80.0f,120.0f,300.0f,120.0f,1,1,1,3);
          const float tri[6]={200.0f,300.0f,320.0f,300.0f,260.0f,420.0f};
          engine.drawPolygon(tri,3,1,0,0,true);
          engine.drawRect(60.0f,350.0f,90.0f,60.0f,0,1,0,false);
          const int rect[4]={400,60,200,200};
          engine.setScissorRect(rect);
          engine.drawRect(380.0f,40.0f,240.0f,240.0f,0,0,1,true);
          engine.setScissorRect(nullptr);
          engine.setTransform(500.0f,400.0f,0.7853981633974483f);
          engine.drawRect(-30.0f,-5.0f,60.0f,10.0f,1,1,0,true);
          engine.resetTransform();
         }
       }
      engine.endFrame();
      gettimeofday(&now,nullptr);
      long elapsed=(now.tv_sec-start.tv_sec)*1000000L+now.tv_usec-start.tv_usec;
      if (frameLog)
        fprintf(stderr,"[frame] %d %ld\n",frame,elapsed);
      if (elapsed<(frame+1)*uSecondsPerFrame)
        usleep((useconds_t)((frame+1)*uSecondsPerFrame-elapsed));
     }
   }
  engine.shutdown();
  return 0;
  #endif
  // GL/VK stub (task 13, M5-M1): no GPU backend exists until tasks 31/37 and
  // the GL/VK link rules until 29/37; the objects target only proves this TU
  // compiles guards-closed. The stub loop arrives with the event unification.
  return 0;
 }
