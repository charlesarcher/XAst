#include<stdlib.h>
#include<new>
#include<iostream>
#include<stdio.h>
#include"utilities/rendering/x11Backend.H"
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
 {X11Backend engine;
  engine.setCanonicalLayout(PlayingField::playArea.Width(),
                            PlayingField::playArea.Height(),
                            ShipGroup::maxIconHeight);
  if (!engine.initWindow("Asteroids"))
   {fprintf(stderr,"XAsteroids: initialization failed.\n");
    return 1;
   }
  stage = new Stage(engine);
  // Transitional population of Stage's non-owning members from the concrete
  // backend (D11 seam extension; the members die at tasks 14/47, this
  // mechanism is replaced by DI ctors at task 13).
  X11NativeHandle native=engine.nativeHandle();
  stage->display=(Display*)native.display;
  stage->window=(Window)native.window;
  stage->icon=engine.icon();
  stage->buttonInfo=engine.buttonFont();
  stage->errorInfo=engine.errorFont();
  stage->autoRepeatState=engine.autoRepeatState();
  stage->titleInfo=engine.titleFont();
  stage->hiScoreInfo=engine.hiScoreFont();
  stage->scoreInfo=engine.scoreFont();
  stage->titleGC=engine.titleGC();
  stage->hiScoreGC=engine.hiScoreGC();
  stage->scoreGC=engine.scoreGC();
  stage->defaultGC=engine.defaultGC();
  button = new Button(stage->display,stage->window,
                      "Options",stage->buttonInfo,
                      stage->buttonFg,stage->buttonBg,
                      stage->buttonX,stage->buttonY);
  score = new Score;
  explosionGraphic = new ExplosionGraphic;
  shipGroup = (ShipGroup*)::operator new(sizeof(ShipGroup));
  ShipGroup::ship=&shipGroup->starDestroyer;
  ShipGroup::thrust=&shipGroup->starDestroyerThrust;
  new (shipGroup) ShipGroup;
  shipYard = new ShipYard(stage->display,stage->window,
                          shipGroup->ship->icon,
                          shipGroup->ship->iconWidth,shipGroup->ship->iconHeight,
                          shipGroup->ship->iconColor,
                          stage->shipYardWidth,stage->shipYardHeight,Stage::shipYardBg);
  shipBulletGroup = (ShipBulletGroup*)::operator new(sizeof(ShipBulletGroup));
  new (shipBulletGroup) ShipBulletGroup;
  rockGroup = new RockGroup;
  enemyGroup = new EnemyGroup;
  enemyBulletGroup = (EnemyBulletGroup*)::operator new(sizeof(EnemyBulletGroup));
  new (enemyBulletGroup) EnemyBulletGroup;
  playingField = new PlayingField;

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
 }
