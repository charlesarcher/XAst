#ifndef _SHIPYARD_H
#define _SHIPYARD_H

#include"../utilities/rendering/renderingEngine.H"

// Yard background: the exact Stage::shipYardBg value (16-bit component 28527,
// i.e. 111/255 scaled). The engine fill path allocates the color or falls back
// to white — the same semantics as the colormap allocations the raw stamps
// used before task 47 closed them.
static const float yardGray=28527.0f/65535.0f;

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
    // Task 47: one macro-free body on every leg. The colored stamp rides the
    // engine's createColoredStamp (X11: allocate-or-fallback pixels + one
    // colored-stamp request — byte-identical to the retired raw pair; GL/VK:
    // the RGBA8 decode upload that always served those legs). The icon-swap
    // entry takes plain color components now; its sole caller (options.H)
    // re-derives them from RGB anyway.
    ShipYard& AlterIcon(const unsigned char* const shipBitmap,
                        const int shipBitmapWidth, const int shipBitmapHeight,
                        const unsigned short sr, const unsigned short sg,
                        const unsigned short sb);

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
                                                   shipCol(colStartOffset)
  {// The yard background is the exact Stage::shipYardBg value the call site
   // used to pass, and the ship color arrives as the RGB triple of
   // shipGroup->ship->iconColor (allocated by Ship's ctor). The old
   // `drawable` parameter was always stage->window.
   yard=engine.createRenderTarget(yWidth,yHeight);
   engine.beginRenderTo(yard);
   engine.drawRect(0,0,yWidth,yHeight,yardGray,yardGray,yardGray,true);
   engine.endRenderTo();
   shipErase=engine.createRenderTarget(shipBitsWidth,shipBitsHeight);
   engine.beginRenderTo(shipErase);
   engine.drawRect(0,0,shipBitsWidth,shipBitsHeight,yardGray,yardGray,yardGray,true);
   engine.endRenderTo();
   // Colored stamp: set bits carry the ship color, clear bits the yard gray
   // (the raw stamp's fg/bg mapping, closed at 47 through the engine seam).
   ship=engine.createColoredStamp(shipBits,shipBitsWidth,shipBitsHeight,
                                  sr/65535.0f,sg/65535.0f,sb/65535.0f,
                                  yardGray,yardGray,yardGray);
  }

inline ShipYard::~ShipYard()
 {engine.deleteTexture(ship);
  engine.deleteTexture(shipErase);
  engine.deleteTexture(yard);
 }

inline ShipYard& ShipYard::AddShip()
 {if (++numShips<=maxVisibleShips)
   {shipRow+=shipWidth;
    if (shipRow>=rowEndOffset)
     {shipRow=rowStartOffset;
      shipCol+=shipHeight;
     }
    engine.beginRenderTo(yard);
    engine.drawTexture(ship,shipRow,shipCol,shipWidth,shipHeight,1.0f);
    engine.endRenderTo();
   }
  return *this;
 }

inline ShipYard& ShipYard::RemoveShip()
  {if (numShips&&numShips--<=maxVisibleShips)
   {
    engine.beginRenderTo(yard);
    engine.drawTexture(shipErase,shipRow,shipCol,shipWidth,shipHeight,1.0f);
    engine.endRenderTo();
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
  engine.beginRenderTo(yard);
  engine.drawRect(0,0,yardWidth,yardHeight,yardGray,yardGray,yardGray,true);
  engine.endRenderTo();
  shipRow=rowStartOffset-shipWidth;
  shipCol=colStartOffset;
  numShips=0;
  return *this;
 }

inline ShipYard& ShipYard::AlterIcon(const unsigned char* const shipBits,
                                     const int shipBitsWidth, const int shipBitsHeight,
                                     const unsigned short sr, const unsigned short sg,
                                     const unsigned short sb)
 {shipWidth=shipBitsWidth;
  shipHeight=shipBitsHeight;
  maxVisibleShips=((yardHeight-10)/shipBitsHeight)*((yardWidth-10)/shipBitsWidth);
  rowStartOffset=(yardWidth-10)%shipBitsWidth/2+5;
  rowEndOffset=rowStartOffset+shipBitsWidth*((yardWidth-10)/shipBitsWidth);
  colStartOffset=(yardHeight-10)%shipBitsHeight/2+5;
  lastShipInRowOffset=rowEndOffset-shipBitsWidth;
  engine.deleteTexture(ship);
  engine.deleteTexture(shipErase);
  shipErase=engine.createRenderTarget(shipBitsWidth,shipBitsHeight);
  engine.beginRenderTo(shipErase);
  engine.drawRect(0,0,shipBitsWidth,shipBitsHeight,yardGray,yardGray,yardGray,true);
  engine.endRenderTo();
  ship=engine.createColoredStamp(shipBits,shipBitsWidth,shipBitsHeight,
                                 sr/65535.0f,sg/65535.0f,sb/65535.0f,
                                 yardGray,yardGray,yardGray);
  int numShipsSave=numShips;
  ClearYard();
  for (int i=0;i<numShipsSave;++i)
    AddShip();
  return *this;
 }


#endif
