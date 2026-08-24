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
    // Logical client-area coordinates (GameEvent x/y feed this directly).
    Boolean PointerOnButton(int x,int y) const;
    Boolean ButtonPressed() const;
    const int ButtonWidth() const;
    const int ButtonHeight() const;

  private:
    Button(const Button&);
    Button& operator=(const Button&);
    void FindColorChange(const int colorValue, int& minChange, Boolean& lowSideMin);

    static const int borderPercentVariance;

    // Task-16 transition member (T17/T19/T21/T22 pattern): the dtor pairs the
    // face creation/deletion through the engine; deleted at task 47.
    RenderingEngine& engine;

    // Task-13 demotion (D11): was `Drawable&` — a reference cannot bind to
    // the prvalue nativeHandle() returns, so the member is now a value copy
    // of the backend window taken at construction.
    Window drawable;

    // Task-16 (M1/R4-B6): the two faces are render targets — off-screen
    // buffers on X11, FBO textures on GL/VK. Engine-managed memory, caller
    // lifetime: created here, deleteTexture in the dtor (D-D invariant ii).
    TextureId buttonUp,
              buttonDown;

    int upperLeftX,
        upperLeftY,
        borderWidth,
        buttonWidth,
        buttonHeight;

    Boolean buttonPressed;
 };

inline const int Button::borderPercentVariance=25;

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

inline Button::Button(RenderingEngine& eng,
                      const char* const label,
                      const int x, const int y,
		      const int borderSize): engine(eng),
                                             drawable(eng.nativeHandle().window),
                                             upperLeftX(x),
                                             upperLeftY(y),
                                             borderWidth(borderSize>3 ? borderSize
                                                                       : 3),
                                             buttonPressed(off)
 {// Sizing via the engine (D11 font-by-id): these reproduce the original
  // width/max_bounds arithmetic bit-for-bit.
  buttonWidth=(int)engine.measureText(label,EngineFont_Button)+6+2*borderWidth;
  FontMetrics fm;
  engine.getFontMetrics(EngineFont_Button,fm);
  buttonHeight=(int)fm.max_bounds.ascent+(int)fm.max_bounds.descent+6+2*borderWidth;
  // Face colors (D11): plain RGB components — the values Stage holds; the
  // backend derives pixels internally with allocate-or-fallback semantics.
  const int fgRed=0,
            fgGreen=0,
            fgBlue=0,
            bgRed=46260,//180
            bgGreen=46260,//180
            bgBlue=46260;//180
  int minChange=65535;
  Boolean lowSideMin;
  FindColorChange(bgRed,minChange,lowSideMin);
  FindColorChange(bgGreen,minChange,lowSideMin);
  FindColorChange(bgBlue,minChange,lowSideMin);
  int redChange,
      greenChange,
      blueChange;
  if (lowSideMin)
   {redChange=(bgRed+50)*borderPercentVariance/100;
    greenChange=(bgGreen+50)*borderPercentVariance/100;
    blueChange=(bgBlue+50)*borderPercentVariance/100;
   }
  else
   {redChange=(65535-bgRed+50)*borderPercentVariance/100;
    greenChange=(65535-bgGreen+50)*borderPercentVariance/100;
    blueChange=(65535-bgBlue+50)*borderPercentVariance/100;
   }
  const float topRed=(bgRed+redChange)/65535.0f,
              topGreen=(bgGreen+greenChange)/65535.0f,
              topBlue=(bgBlue+blueChange)/65535.0f,
              bottomRed=(bgRed-redChange)/65535.0f,
              bottomGreen=(bgGreen-greenChange)/65535.0f,
              bottomBlue=(bgBlue-blueChange)/65535.0f,
              bgRedf=bgRed/65535.0f,
              bgGreenf=bgGreen/65535.0f,
              bgBluef=bgBlue/65535.0f;
  // Render-target faces (M1/R4-B6): each face is drawn once into its own
  // off-screen target; Press/Release/Draw later blit the chosen id.
  buttonUp=engine.createRenderTarget(buttonWidth,buttonHeight);
  buttonDown=engine.createRenderTarget(buttonWidth,buttonHeight);
  // Label placement: the raw site drew at borderWidth+3 minus the first
  // character's left bearing from per-string extents (the centering variant
  // shifted the glyphs 1px right vs baseline; the font-wide maximum in
  // FontMetrics is a different value still). The bearing helper lives on
  // the concrete backend — the frozen interface has no extents primitive.
#ifdef X11_BACKEND
  const int labelX=borderWidth+3
                  -static_cast<X11Backend&>(engine).textFirstCharLbearing(label,
                                                     EngineFont_Button);
#else
  const int labelX=borderWidth+3;
#endif
  const int labelY=borderWidth+3+(int)fm.max_bounds.ascent;
  // Bevel geometry — identical for both faces (paint order per face preserved:
  // interior fill, top bevel fill+outline, bottom bevel fill+outline, black
  // frame, label).
  float topBevel[14],
        bottomBevel[14];
  topBevel[0]=0;                              topBevel[1]=0;
  topBevel[2]=(float)(buttonWidth-1);         topBevel[3]=0;
  topBevel[4]=(float)(buttonWidth-borderWidth); topBevel[5]=(float)(borderWidth-1);
  topBevel[6]=(float)(borderWidth-1);         topBevel[7]=(float)(borderWidth-1);
  topBevel[8]=(float)(borderWidth-1);         topBevel[9]=(float)(buttonHeight-borderWidth);
  topBevel[10]=0;                             topBevel[11]=(float)(buttonHeight-1);
  topBevel[12]=0;                             topBevel[13]=0;
  bottomBevel[0]=0;                           bottomBevel[1]=(float)(buttonHeight-1);
  bottomBevel[2]=(float)(buttonWidth-1);      bottomBevel[3]=(float)(buttonHeight-1);
  bottomBevel[4]=(float)(buttonWidth-1);      bottomBevel[5]=0;
  bottomBevel[6]=(float)(buttonWidth-borderWidth); bottomBevel[7]=(float)(borderWidth-1);
  bottomBevel[8]=(float)(buttonWidth-borderWidth); bottomBevel[9]=(float)(buttonHeight-borderWidth);
  bottomBevel[10]=(float)(borderWidth-1);     bottomBevel[11]=(float)(buttonHeight-borderWidth);
  bottomBevel[12]=0;                          bottomBevel[13]=(float)(buttonHeight-1);
  const TextureId faces[2]={buttonUp,buttonDown};
  for (int f=0;f<2;++f)
   {engine.beginRenderTo(faces[f]);
    // Pin the shared primitive line state to thin before the outline passes
    // (a sibling instance's wide strokes could otherwise leak in); the two
    // probe pixels are overpainted by the top-bevel fill immediately after.
    engine.drawLine(0,0,1,0,bgRedf,bgGreenf,bgBluef,1.0f);
    engine.drawRect((float)borderWidth,(float)borderWidth,
                    (float)(buttonWidth-borderWidth-1),
                    (float)(buttonHeight-borderWidth-1),
                    bgRedf,bgGreenf,bgBluef,true);
    engine.drawPolygon(topBevel,7,topRed,topGreen,topBlue,true);
    engine.drawPolygon(topBevel,7,topRed,topGreen,topBlue,false);
    engine.drawPolygon(bottomBevel,7,bottomRed,bottomGreen,bottomBlue,true);
    engine.drawPolygon(bottomBevel,7,bottomRed,bottomGreen,bottomBlue,false);
    if (f==0)
     {// Thin black frame around the whole face — closed loop drawn as four
      // segments. The backend stroke omits each segment's far-end pixel
      // (raw used a both-endpoints cap), so the loop is walked so every
      // corner is some segment's near end — except the bottom-right, which
      // both adjacent segments omit; the right segment therefore runs one
      // pixel past the corner (the overshoot falls outside the face and
      // clips away), making the corner an interior pixel as raw had it.
      engine.drawLine(0,0,(float)(buttonWidth-1),0,0,0,0,1.0f);
      engine.drawLine((float)(buttonWidth-1),0,(float)(buttonWidth-1),
                      (float)(buttonHeight-1)+1,0,0,0,1.0f);
      engine.drawLine((float)(buttonWidth-1),(float)(buttonHeight-1),0,
                      (float)(buttonHeight-1),0,0,0,1.0f);
      engine.drawLine(0,(float)(buttonHeight-1),0,0,0,0,0,1.0f);
     }
    else
     {// Wide black frame inset by one pixel. The segments are extended by one
      // pixel past each corner so their union covers the corner blocks the
      // original joined wide stroke produced — pixel-for-pixel.
      engine.drawLine(0,1,(float)(buttonWidth-1),1,0,0,0,3.0f);
      engine.drawLine((float)(buttonWidth-2),0,(float)(buttonWidth-2),
                      (float)(buttonHeight-1),0,0,0,3.0f);
      engine.drawLine(0,(float)(buttonHeight-2),(float)(buttonWidth-1),
                      (float)(buttonHeight-2),0,0,0,3.0f);
      engine.drawLine(1,0,1,(float)(buttonHeight-1),0,0,0,3.0f);
     }
    engine.drawStringOpaque(label,(float)labelX,(float)labelY,EngineFont_Button,
                            (float)fgRed,(float)fgGreen,(float)fgBlue,
                            bgRedf,bgGreenf,bgBluef);
    engine.endRenderTo();
   }
 }

inline Button::~Button()
 {engine.deleteTexture(buttonDown);
  engine.deleteTexture(buttonUp);
 }

inline Button& Button::PressButton()
 {// Blit the pressed face to the window (the standing default target).
  engine.drawTexture(buttonDown,(float)upperLeftX,(float)upperLeftY,
                     (float)buttonWidth,(float)buttonHeight,1.0f);
  buttonPressed=on;
  return *this;
 }

inline Button& Button::ReleaseButton()
 {engine.drawTexture(buttonUp,(float)upperLeftX,(float)upperLeftY,
                     (float)buttonWidth,(float)buttonHeight,1.0f);
  buttonPressed=off;
  return *this;
 }

inline const Button& Button::DrawButton() const
 {// The press/release swap IS which face id gets blitted.
  if (buttonPressed)
    engine.drawTexture(buttonDown,(float)upperLeftX,(float)upperLeftY,
                       (float)buttonWidth,(float)buttonHeight,1.0f);
  else
    engine.drawTexture(buttonUp,(float)upperLeftX,(float)upperLeftY,
                       (float)buttonWidth,(float)buttonHeight,1.0f);
  return *this;
 }

inline Button::Boolean Button::PointerOnButton(int x,int y) const
 {return x>=upperLeftX && x<upperLeftX+buttonWidth &&
         y>=upperLeftY && y<upperLeftY+buttonHeight;
 }

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
