#ifndef _SHIPYARD_H
#define _SHIPYARD_H

#ifdef X11_BACKEND
#include<X11/Xlib.h>
#endif
#include"../utilities/rendering/x11types.H"
#include"../utilities/rendering/renderingEngine.H"

#ifdef X11_BACKEND
// Yard background: the exact Stage::shipYardBg value (16-bit component 28527,
// i.e. 111/255 scaled). The engine fill path allocates the color or falls back
// to white — the same semantics as the colormap allocations this file retains
// solely to feed pixel arguments to its raw colored stamps.
static const float yardGray=28527.0f/65535.0f;
#endif

class ShipYard
 {public:
    ShipYard(RenderingEngine& engine,
             const unsigned char* const shipBitmap, const int shipBitmapWidth, const int shipBitmapHeight,
             const unsigned short shipColorRed, const unsigned short shipColorGreen, const unsigned short shipColorBlue,
             const int yardWidth, const int yardHeight);
    ~ShipYard();
    ShipYard& AddShip();
    ShipYard& RemoveShip();
    const int NumShips() const;
    const TextureId& GetYard() const;
    ShipYard& ClearYard();
#ifdef X11_BACKEND
    ShipYard& AlterIcon(const unsigned char* const shipBitmap,
                        const int shipBitmapWidth, const int shipBitmapHeight,
                        XColor& shipColor);
#endif

  private:
    ShipYard(const ShipYard&);
    ShipYard& operator=(const ShipYard&);

    // TRANSITION member (deleted at task 47): the destructor releases the
    // engine-owned textures, so the yard must keep the engine reference.
    RenderingEngine& engine;

    int shipWidth,
        shipHeight,
        shipColor,
        numShips,
        maxVisibleShips,
        yardWidth,
        yardHeight,
        yardColor,
        rowStartOffset,
        rowEndOffset,
        colStartOffset,
        lastShipInRowOffset,
        shipRow,
        shipCol;

    TextureId yard,
              ship,
              shipErase;

    Display* const display;
 };

inline ShipYard::ShipYard(RenderingEngine& engine,
                          const unsigned char* const shipBits,
                          const int shipBitsWidth, const int shipBitsHeight,
                          const unsigned short sr, const unsigned short sg, const unsigned short sb,
   		                  const int yWidth, const int yHeight):
                                                 engine(engine),
                                                 shipWidth(shipBitsWidth),
                                                   shipHeight(shipBitsHeight),
                                                   numShips(0),
                                                   maxVisibleShips(((yHeight-10)/shipBitsHeight)*((yWidth-10)/shipBitsWidth)),
                                                   yardWidth(yWidth),
                                                   yardHeight(yHeight),
                                                   rowStartOffset((yWidth-10)%shipBitsWidth/2+5),
                                                   rowEndOffset(rowStartOffset+shipBitsWidth*((yWidth-10)/shipBitsWidth)),
                                                   colStartOffset((yHeight-10)%shipBitsHeight/2+5),
                                                   lastShipInRowOffset(rowEndOffset-shipBitsWidth),
                                                   shipRow(rowStartOffset-shipBitsWidth),
                                                   shipCol(colStartOffset),
                                                   display((Display*)engine.nativeHandle().display)
  {
   #ifdef X11_BACKEND
   // The yard background is the exact Stage::shipYardBg value the call site
   // used to pass, and the ship color arrives as the RGB triple of
   // shipGroup->ship->iconColor (allocated by Ship's ctor). The old
   // `drawable` parameter was always stage->window, now taken from the
   // engine handle. The two colormap allocations below survive only to feed
   // pixel arguments to the raw colored stamps (see the retained call).
   int screen=DefaultScreen(display);
   XColor yardBackground={0,
                          28527,//111
                          28527,//111
                          28527,//111
                          DoRed|DoGreen|DoBlue,
                          0};
   XColor shipForeground={0,sr,sg,sb,DoRed|DoGreen|DoBlue,0};
   yardColor=XAllocColor(display,DefaultColormap(display,screen),&yardBackground)
            ? yardBackground.pixel
            : WhitePixel(display,screen);
   yard=engine.createRenderTarget(yWidth,yHeight);
   engine.beginRenderTo(yard);
   engine.drawRect(0,0,yWidth,yHeight,yardGray,yardGray,yardGray,true);
   engine.endRenderTo();
   shipErase=engine.createRenderTarget(shipBitsWidth,shipBitsHeight);
   engine.beginRenderTo(shipErase);
   engine.drawRect(0,0,shipBitsWidth,shipBitsHeight,yardGray,yardGray,yardGray,true);
   engine.endRenderTo();
   shipColor=XAllocColor(display,DefaultColormap(display,screen),&shipForeground)
            ? shipForeground.pixel
            : WhitePixel(display,screen);
   // Colored stamp retained: set->shipColor, clear->yardColor cannot be
   // expressed 1:1 through the engine yet — closure = backend fg/bg-capable
   // bitmap seam or task-26 decoder + fixed RGBA upload.
   ship=XCreatePixmapFromBitmapData(display,yard,(char*)shipBits,shipBitsWidth,shipBitsHeight,
                                    shipColor,yardColor,DefaultDepth(display,screen));
   #endif
  }

inline ShipYard::~ShipYard()
 {
  #ifdef X11_BACKEND
  engine.deleteTexture(ship);
  engine.deleteTexture(shipErase);
  engine.deleteTexture(yard);
  #endif
 }

inline ShipYard& ShipYard::AddShip()
 {if (++numShips<=maxVisibleShips)
   {shipRow+=shipWidth;
    if (shipRow>=rowEndOffset)
     {shipRow=rowStartOffset;
      shipCol+=shipHeight;
     }
    #ifdef X11_BACKEND
    engine.beginRenderTo(yard);
    engine.drawTexture(ship,shipRow,shipCol,shipWidth,shipHeight,1.0f);
    engine.endRenderTo();
    #endif
   }
  return *this;
 }

inline ShipYard& ShipYard::RemoveShip()
  {if (numShips&&numShips--<=maxVisibleShips)
   {
    #ifdef X11_BACKEND
    engine.beginRenderTo(yard);
    engine.drawTexture(shipErase,shipRow,shipCol,shipWidth,shipHeight,1.0f);
    engine.endRenderTo();
    #endif
    shipRow-=shipWidth;
    if (shipRow<rowStartOffset)
     {shipRow=lastShipInRowOffset;
      shipCol-=shipHeight;
     }
   }
  return *this;
 }

inline const int ShipYard::NumShips() const
 {return numShips;
 }

inline const TextureId& ShipYard::GetYard() const
 {return yard;
 }

inline ShipYard& ShipYard::ClearYard()
 {
  #ifdef X11_BACKEND
  engine.beginRenderTo(yard);
  engine.drawRect(0,0,yardWidth,yardHeight,yardGray,yardGray,yardGray,true);
  engine.endRenderTo();
  #endif
  shipRow=rowStartOffset-shipWidth;
  shipCol=colStartOffset;
  numShips=0;
  return *this;
 }

#ifdef X11_BACKEND
inline ShipYard& ShipYard::AlterIcon(const unsigned char* const shipBits,
                                     const int shipBitsWidth, const int shipBitsHeight,
                                     XColor& shipForeground)
 {shipWidth=shipBitsWidth;
  shipHeight=shipBitsHeight;
  maxVisibleShips=((yardHeight-10)/shipBitsHeight)*((yardWidth-10)/shipBitsWidth);
  rowStartOffset=(yardWidth-10)%shipBitsWidth/2+5;
  rowEndOffset=rowStartOffset+shipBitsWidth*((yardWidth-10)/shipBitsWidth);
  colStartOffset=(yardHeight-10)%shipBitsHeight/2+5;
  lastShipInRowOffset=rowEndOffset-shipBitsWidth;
  int screen=DefaultScreen(display);
  shipColor=XAllocColor(display,DefaultColormap(display,screen),&shipForeground)
            ? shipForeground.pixel
            : WhitePixel(display,screen);
  engine.deleteTexture(ship);
  engine.deleteTexture(shipErase);
  shipErase=engine.createRenderTarget(shipBitsWidth,shipBitsHeight);
  engine.beginRenderTo(shipErase);
  engine.drawRect(0,0,shipBitsWidth,shipBitsHeight,yardGray,yardGray,yardGray,true);
  engine.endRenderTo();
  // Colored stamp retained: set->shipColor, clear->yardColor cannot be
  // expressed 1:1 through the engine yet — closure = backend fg/bg-capable
  // bitmap seam or task-26 decoder + fixed RGBA upload.
  ship=XCreatePixmapFromBitmapData(display,yard,(char*)shipBits,shipBitsWidth,shipBitsHeight,
                                   shipColor,yardColor,DefaultDepth(display,screen));
  int numShipsSave=numShips;
  ClearYard();
  for (int i=0;i<numShipsSave;++i)
    AddShip();
  return *this;
 }
#endif


#endif
