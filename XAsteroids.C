#include<stdlib.h>
#include<new>
#include<iostream>
#include<stdio.h>
#ifdef X11_BACKEND
#include"utilities/rendering/x11Backend.H"
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
  // GL/VK stub (task 13, M5-M1): no GPU backend exists until tasks 31/37 and
  // the GL/VK link rules until 29/37; the objects target only proves this TU
  // compiles guards-closed. The stub loop arrives with the event unification.
  return 0;
 }
