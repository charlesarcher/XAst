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
#ifdef X11_BACKEND
#include<X11/Xlib.h>
#endif
#include"../utilities/rendering/x11types.H"
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

    // TRANSITION members (D11 single-ownership-seam extension; DELETED at
    // task 47): non-owning aliases onto X11Backend-owned resources (the D15
    // map — exactly one owner, the backend). main() populates them from the
    // concrete X11Backend it constructs. The first eight are the plan's
    // durable set; the fonts/GCs below them are transient until task 14
    // converts the raw HUD/blit bodies below to engine methods.
    Display* display;
    Window window;

    Pixmap icon;

    int shipYardWidth,
        shipYardHeight,
        autoRepeatState;

    int buttonX,
        buttonY;

    XFontStruct* buttonInfo,
               * errorInfo;

    XFontStruct* titleInfo,
               * hiScoreInfo,
               * scoreInfo;

    GC titleGC,
       hiScoreGC,
       scoreGC,
       defaultGC;

    static XColor windowBg,
                  shipYardBg,
                  buttonFg,
                  buttonBg;

  private:

    int windowWidth,
        windowHeight,
        titleX,
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

inline XColor Stage::windowBg={0,
                        46260,//180
                        46260,//180
                        46260,//180
                        DoRed|DoGreen|DoBlue},
       Stage::shipYardBg={0,
                          28527,//111
                          28527,//111
                          28527,//111
                          DoRed|DoGreen|DoBlue},
       Stage::buttonFg={0,
                        0,//0
                        0,//0
                        0,//0
                        DoRed|DoGreen|DoBlue},
       Stage::buttonBg={0,
                        46260,//180
                        46260,//180
                        46260,//180
                        DoRed|DoGreen|DoBlue};

inline Stage::Stage(RenderingEngine& engine)
 {cout<<"XAsteroids is a Unix version of the original Atari Asteroids video game.  It features color, arbitrarily shaped drawables with interior decor, hidden line elimination, accurate collision detection, and dynamically selectable options including gravitation.  Game initialization requires a minute or two so please be patient.  If XAsteroids abnormally terminates the keyboard may no longer auto-repeat.  Auto-repeat can be reactivated by executing AutoRepeatOn."<<endl<<endl;
  // The X11 creation block that used to live here (display/fonts/window/icon/
  // GCs/KBState/signals — stage.H:130-221 pre-task-12) is now owned by
  // X11Backend::initWindow (D15 map); main() constructs the backend FIRST and
  // populates this class's non-owning transition members from it afterwards.
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
  int titleWidth=(int)engine.measureText("Asteroids",EngineFont_Title)
                +2*(int)titleFm.max_bounds.width,
      hiScoreWidth=10*(int)hiScoreFm.max_bounds.width,
      scoreWidth=10*(int)scoreFm.max_bounds.width,
      headerHeight;
  WindowSizeFormula::Compute(titleFm,hiScoreFm,scoreFm,
                             (int)engine.measureText("Asteroids",EngineFont_Title),
                             PlayingField::playArea.Width(),
                             PlayingField::playArea.Height(),
                             shipGroup->maxIconHeight,
                             windowWidth,windowHeight);
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
 }

inline Stage::~Stage()
 {// DISARMED (task 12/D15/M4): every resource-freeing call that used to live
   // here moved into X11Backend::shutdown, which also adds the missing fifth
   // font free (errorInfo). Layout + logic only remain — i.e. nothing at all
   // today.
 }

inline Stage& Stage::DrawScore()
 {char str[12];
  if (score->CurrentScore()>=score->TopTenScore(0))
   {snprintf(str,sizeof(str),"%10.10s",score->CurrentName());
    XDrawImageString(display,window,hiScoreGC,
                     hiScoreUserX,hiScoreUserY,str,10);
    snprintf(str,sizeof(str),"%10d",score->CurrentScore());
    XDrawImageString(display,window,hiScoreGC,
                     hiScoreX,hiScoreY,str,10);
   }
  else
    snprintf(str,sizeof(str),"%10d",score->CurrentScore());
  XDrawImageString(display,window,scoreGC,
                   scoreX,scoreY,str,10);
  return *this;
 }

inline Stage& Stage::DrawShipYard()
 {XCopyArea(display,
            shipYard->GetYard(),
            window,
            defaultGC,
            0,0,
            shipYardWidth,shipYardHeight,
            shipYardX,shipYardY);
  return *this;
 }

inline Stage& Stage::DrawPlayingField()
 {XCopyArea(display,
            playingField->pixmap,
            window,
            defaultGC,
	    PlayingField::playArea.NorthWestCorner().x,PlayingField::playArea.NorthWestCorner().y,
	    PlayingField::playArea.Width(),PlayingField::playArea.Height(),
            playAreaX,playAreaY);
  return *this;
 }

inline Stage& Stage::Refresh()
 {XDrawString(display,window,titleGC,
              titleX,titleY,"Asteroids",strlen("Asteroids"));
  char str[12];
  snprintf(str,sizeof(str),"%10.10s",score->TopTenName(0));
  XDrawImageString(display,window,hiScoreGC,
                   hiScoreUserX,hiScoreUserY,str,10);
  snprintf(str,sizeof(str),"%10d",score->TopTenScore(0));
  XDrawImageString(display,window,hiScoreGC,
                   hiScoreX,hiScoreY,str,10);
  button->DrawButton();
  DrawScore().DrawShipYard().DrawPlayingField();
  return *this;
 }

#endif
