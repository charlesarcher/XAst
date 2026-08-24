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
    for (int frame=0;frame<160;++frame)
     {engine.beginFrame();
      if (frame==0)
        engine.clear();
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
