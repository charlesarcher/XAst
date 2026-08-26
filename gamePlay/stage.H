#ifndef _STAGE_H
#define _STAGE_H

#include<stdlib.h>
#include<stdio.h>
#include<iostream>
#include<sstream>
#include<iomanip>
#include<string.h>
#include<signal.h>
#include<time.h>
#include"../utilities/rendering/x11Backend.H"
#include"../utilities/rendering/renderingEngine.H"
#include"../utilities/rendering/windowSize.H"
#include"options/button.H"
#include"score.H"
#include"shipYard.H"
#include"../bitmaps/starDestroyerIcon.xbm"

// This code was originally written where 0<=RAND_MAX<=2^16-1. To prevent overflow issues, a 16 bit random function is used since rand()
//   returns a full 32 bit value. This provides a single convenient place to change the game random number generator to anything desired.
#define RAND_MAX_16 ((RAND_MAX)>>16)
class gary_rand{
  public:
    static inline int rand_16()
     {
      return rand()>>16;
     }
};

using namespace std;

extern Button* button;
extern Score* score;
extern ShipYard* shipYard;

class PlayingField;
class Stage
 {friend class Button;
  friend class ShipYard;
  friend class PlayingField;

  public:

    Stage(RenderingEngine& engine);
    ~Stage();

    inline Stage& DrawScore();
    inline Stage& DrawShipYard();
    inline Stage& DrawPlayingField();
    inline Stage& Refresh();

    // Task 47: every non-owning X11 alias member (display/window/icon/fonts/
    // GCs/autoRepeatState/windowWidth/windowHeight) and the four static
    // color structs are GONE — the backend owns the resources (D15), options.H
    // consumes the backend accessors directly (D9), and the present-region
    // publishing rides the guards-closed backend twin so this header compiles
    // macro-free on every leg. The engine reference feeds the draw methods.
    RenderingEngine& engine;

    int shipYardWidth,
        shipYardHeight;

    int buttonX,
        buttonY;

  private:

    int titleX,
        titleY,
        hiScoreUserX,
        hiScoreUserY,
        hiScoreX,
        hiScoreY,
        scoreX,
        scoreY,
        shipYardX,
        shipYardY,
        playAreaX,
        playAreaY;
 };

extern Stage* stage;

#include"playingField.H"
#include"../objects/ships/shipGroup.H"
extern PlayingField* playingField;

inline Stage::Stage(RenderingEngine& engine): engine(engine)
 {cout<<"XAsteroids is a Unix version of the original Atari Asteroids video game.  It features color, arbitrarily shaped drawables with interior decor, hidden line elimination, accurate collision detection, and dynamically selectable options including gravitation.  Game initialization requires a minute or two so please be patient.  If XAsteroids abnormally terminates the keyboard may no longer auto-repeat.  Auto-repeat can be reactivated by executing AutoRepeatOn."<<endl<<endl;
  // The X11 creation block that used to live here (display/fonts/window/icon/
  // GCs/KBState/signals — stage.H:130-221 pre-task-12) is owned by
  // X11Backend::initWindow (D15 map); main() constructs the backend FIRST and
  // injects it here.
  //
  // Layout math kept (stage.H:143-181 pre-task-12) with metrics obtained
  // through the engine (D15/D7). Every FontMetrics field is a small integer
  // held exactly in float, so the (int) casts reproduce the original
  // short/int arithmetic bit-for-bit; measureText on X11 performs the same
  // font advance query the old :151 made directly.
  FontMetrics titleFm,
              hiScoreFm,
              scoreFm;
  engine.getFontMetrics(EngineFont_Title,titleFm);
  engine.getFontMetrics(EngineFont_HiScore,hiScoreFm);
  engine.getFontMetrics(EngineFont_Score,scoreFm);
  int windowWidth,
      windowHeight;
  WindowSizeFormula::Compute(titleFm,hiScoreFm,scoreFm,
                             (int)engine.measureText("Asteroids",EngineFont_Title),
                             PlayingField::playArea.Width(),
                             PlayingField::playArea.Height(),
                             shipGroup->maxIconHeight,
                             windowWidth,windowHeight);
  int titleWidth=(int)engine.measureText("Asteroids",EngineFont_Title)
                +2*(int)titleFm.max_bounds.width,
      hiScoreWidth=10*(int)hiScoreFm.max_bounds.width,
      scoreWidth=10*(int)scoreFm.max_bounds.width,
      headerHeight=2*((int)hiScoreFm.max_bounds.ascent+(int)hiScoreFm.max_bounds.descent)+4;
  if ((int)titleFm.max_bounds.ascent+(int)titleFm.max_bounds.descent>headerHeight)
    headerHeight=(int)titleFm.max_bounds.ascent+(int)titleFm.max_bounds.descent;
  if ((int)scoreFm.max_bounds.ascent+(int)scoreFm.max_bounds.descent>headerHeight)
    headerHeight=(int)scoreFm.max_bounds.ascent+(int)scoreFm.max_bounds.descent;
  int temp=(windowWidth-titleWidth)/2;
  titleX=temp+(int)titleFm.max_bounds.width-(int)titleFm.lbearing;
  titleY=(int)titleFm.max_bounds.ascent+15;
  buttonX=buttonY=2;
  hiScoreUserX=(temp-hiScoreWidth)/2-(int)hiScoreFm.lbearing;
  hiScoreUserY=(int)hiScoreFm.max_bounds.ascent+15;
  hiScoreX=hiScoreUserX;
  hiScoreY=hiScoreUserY+(int)hiScoreFm.max_bounds.ascent
                       +(int)hiScoreFm.max_bounds.descent+4;
  scoreX=titleX+titleWidth
         -(int)titleFm.max_bounds.width+(int)titleFm.lbearing
         +(temp-scoreWidth)/2-(int)scoreFm.lbearing;
  scoreY=(int)scoreFm.max_bounds.ascent+15;
  shipYardX=15;
  shipYardY=headerHeight+25;
  shipYardWidth=windowWidth-30;
  shipYardHeight=2*shipGroup->maxIconHeight+10;
  playAreaX=(windowWidth-PlayingField::playArea.Width())/2;
  playAreaY=headerHeight+shipYardHeight+35;
  const char* seedEnv=getenv("XAST_SEED");
  srand(seedEnv ? (time_t)strtoul(seedEnv,0,10) : (time_t)time(NULL));
  // Task-14: publish the two present regions this ctor computed so the
  // backend's frame-end replay copies them to the coordinates the raw pair
  // used. No-op on the GPU legs (their present path is transform-driven).
  static_cast<X11Backend&>(engine).setFrameGeometry(shipYardX,shipYardY,
                                                    shipYardWidth,shipYardHeight,
                                                    playAreaX,playAreaY);
 }

inline Stage::~Stage()
 {// DISARMED (task 12/D15/M4): every resource-freeing call that used to live
   // here moved into X11Backend::shutdown. Layout + logic only remain — i.e.
   // nothing at all today.
 }

inline Stage& Stage::DrawScore()
 {char str[12];
  // Task-36: the drawString calls are pure engine calls so the score header
  // renders on every leg; X11 emits the identical calls.
  if (score->CurrentScore()>=score->TopTenScore(0))
   {snprintf(str,sizeof(str),"%10.10s",score->CurrentName());
    engine.drawStringOpaque(str,(float)hiScoreUserX,(float)hiScoreUserY,
                            EngineFont_HiScore,
                            0.0f,0.0f,0.0f,
                            46260.0f/65535.0f,46260.0f/65535.0f,
                            46260.0f/65535.0f);
    snprintf(str,sizeof(str),"%10d",score->CurrentScore());
    engine.drawStringOpaque(str,(float)hiScoreX,(float)hiScoreY,
                            EngineFont_HiScore,
                            0.0f,0.0f,0.0f,
                            46260.0f/65535.0f,46260.0f/65535.0f,
                            46260.0f/65535.0f);
   }
  else
    snprintf(str,sizeof(str),"%10d",score->CurrentScore());
  engine.drawStringOpaque(str,(float)scoreX,(float)scoreY,
                          EngineFont_Score,
                          0.0f,0.0f,0.0f,
                          46260.0f/65535.0f,46260.0f/65535.0f,
                          46260.0f/65535.0f);
  return *this;
  }

inline Stage& Stage::DrawShipYard()
 {// Publish the live yard texture (re-created on icon changes), then the
  // request-gated frame end. GPU legs: both calls are inert twins.
  static_cast<X11Backend&>(engine).setFrameYardTexture(shipYard->GetYard());
  engine.endFrame();
  return *this;
  }

inline Stage& Stage::DrawPlayingField()
 {// Request the field-present leg, then the request-gated frame end.
  static_cast<X11Backend&>(engine).requestFieldPresent();
  engine.endFrame();
  return *this;
  }

inline Stage& Stage::Refresh()
 {engine.drawStringTransparent("Asteroids",(float)titleX,(float)titleY,
                               EngineFont_Title,0.0f,0.0f,0.0f);
  char str[12];
  snprintf(str,sizeof(str),"%10.10s",score->TopTenName(0));
  engine.drawStringOpaque(str,(float)hiScoreUserX,(float)hiScoreUserY,
                          EngineFont_HiScore,
                          0.0f,0.0f,0.0f,
                          46260.0f/65535.0f,46260.0f/65535.0f,
                          46260.0f/65535.0f);
  snprintf(str,sizeof(str),"%10d",score->TopTenScore(0));
  engine.drawStringOpaque(str,(float)hiScoreX,(float)hiScoreY,
                          EngineFont_HiScore,
                          0.0f,0.0f,0.0f,
                          46260.0f/65535.0f,46260.0f/65535.0f,
                          46260.0f/65535.0f);
  button->DrawButton();
  DrawScore().DrawShipYard().DrawPlayingField();
  return *this;
  }

#endif
