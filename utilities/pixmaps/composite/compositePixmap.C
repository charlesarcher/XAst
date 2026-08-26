#include<math.h>
#include"compositePixmap.H"

#ifdef X11_BACKEND

CompositePixmap::CompositePixmap(RenderingEngine& eng,
                               const int maxW,
                               const int maxH): display((Display*)eng.nativeHandle().display),
                                                width(maxW),
                                                height(maxH)
 {// Task 47: the server handles come through the injected engine's native
  // handle seam (construction always runs after backend init) — the
  // shim-typed parameters are gone. The display member feeds AddBitmapData.
  Drawable drawable=(Drawable)eng.nativeHandle().window;
  int screen=DefaultScreen(display),
      background=BlackPixel(display,screen);
  GC gc=XCreateGC(display,drawable,0,NULL);
  XSetForeground(display,gc,background);
  pixmap=XCreatePixmap(display,drawable,width,height,DefaultDepth(display,screen));
  XFillRectangle(display,pixmap,gc,0,0,width,height);
  XFreeGC(display,gc);
 }

CompositePixmap::~CompositePixmap()
 {XFreePixmap(display,pixmap);
 }

CompositePixmap& CompositePixmap::AddBitmapData(const unsigned char* bitmap,
                                                const int w,
                                                const int h,
                                                const RotColor& color)
 {// the display member (sourced from the injected engine at construction)
  // feeds the allocation; the request assembles from the RotColor components
  // exactly as the referenced-XColor form did.
  XColor requested={0,color.red,color.green,color.blue,DoRed|DoGreen|DoBlue,0};
  int screen=DefaultScreen(display),
      foreground=XAllocColor(display,DefaultColormap(display,screen),&requested)
                 ? requested.pixel
                 : WhitePixel(display,screen);
  GC gc=XCreateGC(display,pixmap,0,NULL);
  XSetForeground(display,gc,foreground);
  {int lineFill=8-w%8,
       bitNum=0;
   for(int j=0;j<h;++j)
    {for(int i=0;i<w;++i)
      {if (bitmap[bitNum/8]>>bitNum%8&0x01)
         XDrawPoint(display,pixmap,gc,(2*i-w+width)/2,(2*j-h+height)/2);
       ++bitNum;
      }
     bitNum+=lineFill;
    }
  }
  XFreeGC(display,gc);
  return *this;
 }
#else
#include<cstdlib>
#include<iostream>

using namespace std;

// The guards-closed twin is never constructed (the placement news live in
// the guarded configuration); its destructor is deliberately EMPTY because
// an unguarded owner's cleanup loop invokes it on raw storage that was never
// constructed in this configuration, so it must touch nothing.
CompositePixmap::~CompositePixmap()
 {
 }

namespace
 {
  uint8_t channel16to8(const unsigned short c)
   {return static_cast<uint8_t>((static_cast<uint32_t>(c)*255u+32767u)/65535u);
   }
 }

void compositeFrameInit(CompositeFrameRGBA8& frame,
                        const int width, const int height)
 {frame.width=width;
  frame.height=height;
  frame.rgba8.assign(static_cast<std::size_t>(width)*height*4u,0u);
 }

CompositeFrameRGBA8& compositeFrameAddLayer(CompositeFrameRGBA8& frame,
                                            const CompositeLayerSpec& layer)
 {const int w=layer.width,
            h=layer.height,
            lineFill=8-w%8;
  int bitNum=0;
  for(int j=0;j<h;++j)
   {for(int i=0;i<w;++i)
     {if (layer.bits[bitNum/8]>>bitNum%8&0x01)
       {const int px=(2*i-w+frame.width)/2,
                  py=(2*j-h+frame.height)/2;
        uint8_t* const dst=&frame.rgba8[(static_cast<std::size_t>(py)*frame.width+px)*4u];
        dst[0]=channel16to8(layer.red);
        dst[1]=channel16to8(layer.green);
        dst[2]=channel16to8(layer.blue);
        dst[3]=255u;
       }
      ++bitNum;
     }
    bitNum+=lineFill;
   }
  return frame;
 }

CompositeFrameRGBA8& compositeFrameStack(CompositeFrameRGBA8& frame,
                                         const int width, const int height,
                                         const CompositeLayerSpec* const layers,
                                         const int numLayers)
 {compositeFrameInit(frame,width,height);
  for(int i=0;i<numLayers;++i)
    compositeFrameAddLayer(frame,layers[i]);
  return frame;
 }

void compositeMaskExpandR8(const unsigned char* const bits,
                           const int width, const int height,
                           std::vector<uint8_t>& out)
 {out.assign(static_cast<std::size_t>(width)*height,0u);
  const int stride=(width+7)/8;
  for(int j=0;j<height;++j)
    for(int i=0;i<width;++i)
      if (bits[j*stride+i/8]>>(i%8)&0x01)
        out[static_cast<std::size_t>(j)*width+i]=255u;
 }
#endif
