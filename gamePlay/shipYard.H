#ifndef _SHIPYARD_H
#define _SHIPYARD_H

#ifdef X11_BACKEND
#include<X11/Xlib.h>
#endif
#include"../utilities/rendering/x11types.H"
#include"../utilities/rendering/renderingEngine.H"

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
    const Pixmap& GetYard() const;
    ShipYard& ClearYard();
#ifdef X11_BACKEND
    ShipYard& AlterIcon(const unsigned char* const shipBitmap,
                        const int shipBitmapWidth, const int shipBitmapHeight,
                        XColor& shipColor);
#endif

  private:
    ShipYard(const ShipYard&);
    ShipYard& operator=(const ShipYard&);

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

    GC gc;

    Pixmap yard,
           ship,
           shipErase;

    Display* const display;
 };

inline ShipYard::ShipYard(RenderingEngine& engine,
                          const unsigned char* const shipBits,
                          const int shipBitsWidth, const int shipBitsHeight,
                          const unsigned short sr, const unsigned short sg, const unsigned short sb,
    		                  const int yWidth, const int yHeight):
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
  // Colors are engine-managed from here on (D11); the guarded raw body
  // reconstructs them: the yard background is the exact Stage::shipYardBg
  // value the call site used to pass, and the ship color arrives as the
  // RGB triple of shipGroup->ship->iconColor (allocated by Ship's ctor).
  // The old `drawable` parameter was always stage->window, now taken from
  // the engine handle.
  Window win=(Window)engine.nativeHandle().window;
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
  gc=XCreateGC(display,win,0,NULL);
  yard=XCreatePixmap(display,win,yWidth,yHeight,DefaultDepth(display,screen));
  XSetForeground(display,gc,yardColor);
  XFillRectangle(display,yard,gc,0,0,yWidth,yHeight);
  shipErase=XCreatePixmap(display,yard,shipBitsWidth,shipBitsHeight,DefaultDepth(display,screen));
  XFillRectangle(display,shipErase,gc,0,0,shipBitsWidth,shipBitsHeight);
  shipColor=XAllocColor(display,DefaultColormap(display,screen),&shipForeground)
           ? shipForeground.pixel
           : WhitePixel(display,screen);
  ship=XCreatePixmapFromBitmapData(display,yard,(char*)shipBits,shipBitsWidth,shipBitsHeight,
                                   shipColor,yardColor,DefaultDepth(display,screen));
  XSetForeground(display,gc,shipColor);
  XSetBackground(display,gc,yardColor);
  XSetGraphicsExposures(display,gc,FALSE);
  #endif
 }

inline ShipYard::~ShipYard()
 {
  #ifdef X11_BACKEND
  XFreePixmap(display,ship);
  XFreePixmap(display,shipErase);
  XFreePixmap(display,yard);
  XFreeGC(display,gc);
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
    XCopyArea(display,ship,yard,gc,
              0,0,shipWidth,shipHeight,
              shipRow,
              shipCol);
    #endif
   }
  return *this;
 }

inline ShipYard& ShipYard::RemoveShip()
  {if (numShips&&numShips--<=maxVisibleShips)
   {
    #ifdef X11_BACKEND
    XCopyArea(display,shipErase,yard,gc,
              0,0,shipWidth,shipHeight,
              shipRow,
              shipCol);
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

inline const Pixmap& ShipYard::GetYard() const
 {return yard;
 }

inline ShipYard& ShipYard::ClearYard()
 {
  #ifdef X11_BACKEND
  XSetForeground(display,gc,yardColor);
  XFillRectangle(display,yard,gc,0,0,yardWidth,yardHeight);
  XSetForeground(display,gc,shipColor);
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
  XFreePixmap(display,ship);
  XFreePixmap(display,shipErase);
  shipErase=XCreatePixmap(display,yard,shipBitsWidth,shipBitsHeight,DefaultDepth(display,screen));
  XSetForeground(display,gc,yardColor);
  XFillRectangle(display,shipErase,gc,0,0,shipBitsWidth,shipBitsHeight);
  XSetForeground(display,gc,shipColor);
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
