#ifndef _BUTTON_H
#define _BUTTON_H

#ifdef X11_BACKEND
#include<X11/Xlib.h>
#include<X11/Intrinsic.h>
#include"../../utilities/rendering/x11Backend.H"
#endif
#include"../../utilities/rendering/x11types.H"
#include"../../utilities/rendering/renderingEngine.H"
#include"../../utilities/rendering/windowSize.H"

class Button
 {typedef int Boolean;
  enum {off, on};

  public:
    Button(RenderingEngine& engine,
           const char* const label,
           const int upperLeftX, const int upperLeftY,
           const int borderWidth=5);
    ~Button();
    Button& PressButton();
    Button& ReleaseButton();
    const Button& DrawButton() const;
#ifdef X11_BACKEND
    Boolean PointerOnButton(const XButtonEvent&) const;
    Boolean PointerOnButton(const XPointerMovedEvent&) const;
#endif
    Boolean ButtonPressed() const;
    const int ButtonWidth() const;
    const int ButtonHeight() const;

  private:
    Button(const Button&);
    Button& operator=(const Button&);
#ifdef X11_BACKEND
    void CreateButton(const char* const label, const XFontStruct* const fontInfo,
                      XColor& foreground, XColor& background,
                      XColor& topBorderColor, XColor& bottomBorderColor);
#endif
    void FindColorChange(const int colorValue, int& minChange, Boolean& lowSideMin);

    static const int borderPercentVariance;

    Display* display;

    // Task-13 demotion (D11): was `Drawable&` — a reference cannot bind to
    // the prvalue nativeHandle() returns, so the member is now a value copy
    // of the backend window taken at construction.
    Window drawable;

    Pixmap buttonUp,
           buttonDown;

    GC buttonGC;

    int upperLeftX,
        upperLeftY,
        borderWidth,
        buttonWidth,
        buttonHeight;

    Boolean buttonPressed;
 };

inline const int Button::borderPercentVariance=25;

#ifdef X11_BACKEND
inline void Button::CreateButton(const char* const label, const XFontStruct* const fontInfo,
                                 XColor& fg, XColor& bg,
                                 XColor& topBorderColor, XColor& bottomBorderColor)
 {int screen=DefaultScreen(display),
      foreground=XAllocColor(display,DefaultColormap(display,screen),&fg)
                ? fg.pixel
                : WhitePixel(display,screen),
      background=XAllocColor(display,DefaultColormap(display,screen),&bg)
                ? bg.pixel
                : BlackPixel(display,screen),
      topBorder=XAllocColor(display,DefaultColormap(display,screen),&topBorderColor)
               ? topBorderColor.pixel
               : WhitePixel(display,screen),
      bottomBorder=XAllocColor(display,DefaultColormap(display,screen),&bottomBorderColor)
                  ? bottomBorderColor.pixel
                  : BlackPixel(display,screen);
  buttonUp=XCreatePixmap(display,drawable,buttonWidth,buttonHeight,
                         DefaultDepth(display,screen));
  buttonDown=XCreatePixmap(display,drawable,buttonWidth,buttonHeight,
                           DefaultDepth(display,screen));
  buttonGC=XCreateGC(display,drawable,NULL,0);
  XSetForeground(display,buttonGC,background);
  XSetBackground(display,buttonGC,background);
  XSetGraphicsExposures(display,buttonGC,FALSE);
  XFillRectangle(display,buttonUp,buttonGC,borderWidth,borderWidth,
                 buttonWidth-borderWidth-1,buttonHeight-borderWidth-1);
  XFillRectangle(display,buttonDown,buttonGC,borderWidth,borderWidth,
                 buttonWidth-borderWidth-1,buttonHeight-borderWidth-1);
  XSetLineAttributes(display,buttonGC,1,LineSolid,CapButt,JoinMiter);
  XPoint points[7];
  points[0].x=0;                       points[0].y=0;
  points[1].x=buttonWidth-1;           points[1].y=0;
  points[2].x=buttonWidth-borderWidth; points[2].y=borderWidth-1;
  points[3].x=borderWidth-1;           points[3].y=borderWidth-1;
  points[4].x=borderWidth-1;           points[4].y=buttonHeight-borderWidth;
  points[5].x=0;                       points[5].y=buttonHeight-1;
  points[6]=points[0];
  XSetForeground(display,buttonGC,topBorder);
  XFillPolygon(display,buttonUp,buttonGC,points,7,Nonconvex,CoordModeOrigin);
  XDrawLines(display,buttonUp,buttonGC,points,7,CoordModeOrigin);
  XFillPolygon(display,buttonDown,buttonGC,points,7,Nonconvex,CoordModeOrigin);
  XDrawLines(display,buttonDown,buttonGC,points,7,CoordModeOrigin);
  points[0].x=0;                       points[0].y=buttonHeight-1;
  points[1].x=buttonWidth-1;           points[1].y=buttonHeight-1;
  points[2].x=buttonWidth-1;           points[2].y=0;
  points[3].x=buttonWidth-borderWidth; points[3].y=borderWidth-1;
  points[4].x=buttonWidth-borderWidth; points[4].y=buttonHeight-borderWidth;
  points[5].x=borderWidth-1;           points[5].y=buttonHeight-borderWidth;
  points[6]=points[0];
  XSetForeground(display,buttonGC,bottomBorder);
  XFillPolygon(display,buttonUp,buttonGC,points,7,Nonconvex,CoordModeOrigin);
  XDrawLines(display,buttonUp,buttonGC,points,7,CoordModeOrigin);
  XFillPolygon(display,buttonDown,buttonGC,points,7,Nonconvex,CoordModeOrigin);
  XDrawLines(display,buttonDown,buttonGC,points,7,CoordModeOrigin);
  points[0].x=0;             points[0].y=0;
  points[1].x=buttonWidth-1; points[1].y=0;
  points[2].x=buttonWidth-1; points[2].y=buttonHeight-1;
  points[3].x=0;             points[3].y=buttonHeight-1;
  points[4]=points[0];
  XSetForeground(display,buttonGC,BlackPixel(display,screen));
  XDrawLines(display,buttonUp,buttonGC,points,5,CoordModeOrigin);
  points[0].x=1;             points[0].y=1;
  points[1].x=buttonWidth-2; points[1].y=1;
  points[2].x=buttonWidth-2; points[2].y=buttonHeight-2;
  points[3].x=1;             points[3].y=buttonHeight-2;
  points[4]=points[0];
  XSetLineAttributes(display,buttonGC,3,LineSolid,CapButt,JoinMiter);
  XDrawLines(display,buttonDown,buttonGC,points,5,CoordModeOrigin);
  XSetForeground(display,buttonGC,foreground);
  XSetFont(display,buttonGC,fontInfo->fid);
  int direction,
      ascent,
      descent;
  XCharStruct charInfo;
  XTextExtents((XFontStruct*)fontInfo,label,1,&direction,&ascent,&descent,&charInfo);
  XDrawImageString(display,buttonUp,buttonGC,
                   borderWidth+3-charInfo.lbearing,borderWidth+3+fontInfo->max_bounds.ascent,
                   label,strlen(label));
  XDrawImageString(display,buttonDown,buttonGC,
                   borderWidth+3-charInfo.lbearing,borderWidth+3+fontInfo->max_bounds.ascent,
                   label,strlen(label));
 }
#endif

inline void Button::FindColorChange(const int colorValue, int& minChange, Boolean& lowSideMin)
 {if (colorValue<65535-colorValue)
   {if (colorValue<minChange)
     {minChange=colorValue;
      lowSideMin=on;
     }
   }
  else
    if (65535-colorValue<minChange)
     {minChange=65535-colorValue;
      lowSideMin=off;
     }
 }

inline Button::Button(RenderingEngine& engine,
                      const char* const label,
                      const int x, const int y,
		      const int borderSize): display((Display*)engine.nativeHandle().display),
                                             drawable(engine.nativeHandle().window),
                                             upperLeftX(x),
                                             upperLeftY(y),
                                             borderWidth(borderSize>3 ? borderSize
                                                                      : 3),
                                             buttonPressed(off)
 {// Sizing via the engine (D11 font-by-id): measureText IS XTextWidth on X11
  // and FontMetrics carries the exact short values, so these reproduce the
  // original XTextWidth/max_bounds arithmetic bit-for-bit.
  buttonWidth=(int)engine.measureText(label,EngineFont_Button)+6+2*borderWidth;
  FontMetrics fm;
  engine.getFontMetrics(EngineFont_Button,fm);
  buttonHeight=(int)fm.max_bounds.ascent+(int)fm.max_bounds.descent+6+2*borderWidth;
  #ifdef X11_BACKEND
  // Colors are engine-managed from here on (D11); until task 16 migrates the
  // face drawing, the guarded raw body reconstructs the exact Stage values
  // the call site used to pass (stage.buttonFg/buttonBg literals).
  XColor fg={0,
             0,//0
             0,//0
             0,//0
             DoRed|DoGreen|DoBlue,
             0},
          bg={0,
              46260,//180
              46260,//180
              46260,//180
              DoRed|DoGreen|DoBlue,
              0};
  int minChange=65535;
  Boolean lowSideMin;
  FindColorChange(bg.red,minChange,lowSideMin);
  FindColorChange(bg.green,minChange,lowSideMin);
  FindColorChange(bg.blue,minChange,lowSideMin);
  int redChange,
      greenChange,
      blueChange;
  if (lowSideMin)
   {redChange=(bg.red+50)*borderPercentVariance/100;
    greenChange=(bg.green+50)*borderPercentVariance/100;
    blueChange=(bg.blue+50)*borderPercentVariance/100;
   }
  else
   {redChange=(65535-bg.red+50)*borderPercentVariance/100;
    greenChange=(65535-bg.green+50)*borderPercentVariance/100;
    blueChange=(65535-bg.blue+50)*borderPercentVariance/100;
   }
  XColor topBorderColor={0,
                         bg.red+redChange,
                         bg.green+greenChange,
                         bg.blue+blueChange,
                         DoRed|DoGreen|DoBlue,
                         0},
	 bottomBorderColor={0,
                            bg.red-redChange,
                            bg.green-greenChange,
                            bg.blue-blueChange,
                            DoRed|DoGreen|DoBlue,
                            0};
  CreateButton(label,static_cast<X11Backend&>(engine).buttonFont(),
               fg,bg,topBorderColor,bottomBorderColor);
  #endif
 }

inline Button::~Button()
 {
  #ifdef X11_BACKEND
  XFreeGC(display,buttonGC);
  XFreePixmap(display,buttonDown);
  XFreePixmap(display,buttonUp);
  #endif
 }

inline Button& Button::PressButton()
 {
  #ifdef X11_BACKEND
  XCopyArea(display,
            buttonDown,
            drawable,
            buttonGC,
            0,0,
	          buttonWidth,buttonHeight,
            upperLeftX,upperLeftY);
  #endif
  buttonPressed=on;
  return *this;
 }

inline Button& Button::ReleaseButton()
 {
  #ifdef X11_BACKEND
  XCopyArea(display,
            buttonUp,
            drawable,
            buttonGC,
            0,0,
	          buttonWidth,buttonHeight,
            upperLeftX,upperLeftY);
  #endif
  buttonPressed=off;
  return *this;
 }

inline const Button& Button::DrawButton() const
 {
  #ifdef X11_BACKEND
  if (buttonPressed)
    XCopyArea(display,
              buttonDown,
              drawable,
              buttonGC,
              0,0,
	            buttonWidth,buttonHeight,
              upperLeftX,upperLeftY);
  else
    XCopyArea(display,
              buttonUp,
              drawable,
              buttonGC,
              0,0,
	            buttonWidth,buttonHeight,
              upperLeftX,upperLeftY);
  #endif
  return *this;
 }

#ifdef X11_BACKEND
inline Button::Boolean Button::PointerOnButton(const XButtonEvent& event) const
 {return event.x>=upperLeftX && event.x<upperLeftX+buttonWidth &&
         event.y>=upperLeftY && event.y<upperLeftY+buttonHeight;
 }

inline Button::Boolean Button::PointerOnButton(const XPointerMovedEvent& event) const
 {return event.x>=upperLeftX && event.x<upperLeftX+buttonWidth &&
         event.y>=upperLeftY && event.y<upperLeftY+buttonHeight;
 }
#endif

inline Button::Boolean Button::ButtonPressed() const
 {return buttonPressed;
 }

inline const int Button::ButtonWidth() const
 {return buttonWidth;
 }

inline const int Button::ButtonHeight() const
 {return buttonHeight;
 }

#endif
