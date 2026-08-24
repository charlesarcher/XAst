#ifndef _GLSPRITES_H
#define _GLSPRITES_H

// ============================================================================
// glSprites.H — GL-only static-sprite baker (rendering-abstraction task 35).
//
// The m13 degenerate path needs ONE pre-built texture per NonRotVectorData
// sprite (enemies, bullets): the X11 guarded pipeline stamps the wireframe
// outline + decor bitmap into a single sideSize×sideSize pixmap at init
// (rotatorDisplayData.C BuildPixmap/BitmapDecorInit) and blits it unrotated
// every frame. On GL the same content is baked here into an RGBA8 buffer
// (transparent background — the canvas is cleared black, so the guarded
// path's opaque black fill is visually equivalent) and uploaded once.
//
// Semantics mirrored from the guarded pipeline:
//   - outline point mapping:  (v.x + radius + .5, v.y + radius + .5)
//     truncated to integer pixels (BuildPixmap's XPoint assignment);
//   - closed loop: segments p0→p1 … p(n-1)→p0 (XDrawLines with n+1 points);
//   - decor stamp offset: ((side−w)/2, (side−h)/2) (XCopyPlane dest);
//   - outline AND decor share the object's foreground color (one GC);
//   - color components shrink 16→8 bit with the compositePixmap.C rounding
//     so every GL sprite tints identically to the CPU-composited frames.
//
// Pure data transformation: no backend includes, no allocation-order
// dependence; re-runs are byte-identical. Included ONLY under
// #ifndef X11_BACKEND — the guarded configuration never sees this file.
// ============================================================================

#include<cstdint>
#include<cmath>
#include<vector>
#include"../utilities/vector2d.H"

namespace glsprites_detail
 {
  inline uint8_t channel16to8(const unsigned short c)
   {return static_cast<uint8_t>((static_cast<uint32_t>(c)*255u+32767u)/65535u);
   }

  inline void plotPixel(std::vector<uint8_t>& buf,const int side,
                        const int x,const int y,
                        const uint8_t r,const uint8_t g,const uint8_t b)
   {if (x<0||y<0||x>=side||y>=side)
      return;
    uint8_t* const dst=&buf[(static_cast<std::size_t>(y)*side+x)*4u];
    dst[0]=r;dst[1]=g;dst[2]=b;dst[3]=255u;
   }

  // Integer Bresenham; 1px stroke approximates the guarded path's thin
  // XDrawLines segment set (exact rasterization parity is the task-36 gate).
  inline void strokeLine(std::vector<uint8_t>& buf,const int side,
                         int x0,int y0,const int x1,const int y1,
                         const uint8_t r,const uint8_t g,const uint8_t b)
   {int dx=x1-x0,
        dy=y1-y0;
    if (dx<0) dx=-dx;
    if (dy<0) dy=-dy;
    int sx=x0<x1?1:-1,
        sy=y0<y1?1:-1,
        err=dx-dy;
    for(;;)
     {plotPixel(buf,side,x0,y0,r,g,b);
      if (x0==x1&&y0==y1)
        break;
      int e2=2*err;
      if (e2>-dy)
       {err-=dy;x0+=sx;}
      if (e2<dx)
       {err+=dx;y0+=sy;}
     }
   }
 }

inline std::vector<uint8_t> glBakeStaticSprite(
    const int side,const double radius,
    const Vector2d* const vecs,const int numVecs,
    const unsigned short r16,const unsigned short g16,const unsigned short b16,
    const unsigned char* const decorBits,const int decorW,const int decorH)
 {std::vector<uint8_t> buf(static_cast<std::size_t>(side)*side*4u,0u);
  const uint8_t r=glsprites_detail::channel16to8(r16),
                g=glsprites_detail::channel16to8(g16),
                b=glsprites_detail::channel16to8(b16);
  if (vecs&&numVecs>0&&side>0)
   {// Closed polyline through the BuildPixmap point mapping; start at p(n-1)
    // so the closing segment p(n-1)→p0 is drawn like XDrawLines' extra point.
    int prevX=(int)(vecs[numVecs-1].x+radius+.5),
        prevY=(int)(vecs[numVecs-1].y+radius+.5);
    for (int j=0;j<numVecs;++j)
     {const int cx=(int)(vecs[j].x+radius+.5),
                cy=(int)(vecs[j].y+radius+.5);
      glsprites_detail::strokeLine(buf,side,prevX,prevY,cx,cy,r,g,b);
      prevX=cx;prevY=cy;
     }
   }
  if (decorBits&&decorW>0&&decorH>0&&side>0)
   {const int stride=(decorW+7)/8,
              offX=(side-decorW)/2,
              offY=(side-decorH)/2;
    for (int j=0;j<decorH;++j)
      for (int i=0;i<decorW;++i)
        if (decorBits[j*stride+i/8]>>(i%8)&0x01)
          glsprites_detail::plotPixel(buf,side,offX+i,offY+j,r,g,b);
   }
  return buf;
 }

// ---------------------------------------------------------------------------
// Task-36: per-angle scatter rotation for the pure-bitmap flames — replicates
// RotPixmapData/MaskedRotPixmapData::CreatePix point math exactly: source
// pixel (i,j) lands at int(x*cos(a)-y*sin(a)+radius+.5),
// int(y*cos(a)+x*sin(a)+radius+.5) with x=i-.5*(w-1), y=j-.5*(h-1),
// a=k*incAngle, numPix=int(2pi*radius+.5), radius=(max(w,h)-1)/2. The X11
// leg scatters every composite pixel that is not background; here the alpha
// byte carries painted-ness (the CPU composite contract). Out-of-range
// points clip, like XDrawPoint on the sideSize pixmap.
// ---------------------------------------------------------------------------
struct GlRotatedFrames
 {std::vector<std::vector<uint8_t>> rgba;
  std::vector<std::vector<uint8_t>> mask;
  int side=0,
      numPix=0;
 };

inline GlRotatedFrames glBakeRotatedSprite(const uint8_t* rgba8,const int w,
                                           const int h,const bool withMask)
 {const int side=w>h?w:h;
  const double radius=(side-1)/2.0;
  const int numPix=int(6.28318530717958*radius+.5);
  const double incAngle=6.28318530717958/numPix;
  GlRotatedFrames out;
  out.side=side;
  out.numPix=numPix;
  out.rgba.assign(numPix,std::vector<uint8_t>(static_cast<size_t>(side)*side*4,0));
  if (withMask)
    out.mask.assign(numPix,std::vector<uint8_t>(static_cast<size_t>(side)*side,0));
  for(int j=0;j<h;++j)
   for(int i=0;i<w;++i)
    {const uint8_t* px=rgba8+(static_cast<size_t>(j)*w+i)*4;
     if (!px[3])
       continue;
     const double x=i-.5*(w-1),
                  y=j-.5*(h-1);
     for(int k=0;k<numPix;++k)
      {const double angle=k*incAngle;
       const int xRot=int(x*cos(angle)-y*sin(angle)+radius+.5),
                 yRot=int(y*cos(angle)+x*sin(angle)+radius+.5);
       if (xRot<0||yRot<0||xRot>=side||yRot>=side)
         continue;
       uint8_t* dst=&out.rgba[k][(static_cast<size_t>(yRot)*side+xRot)*4];
       dst[0]=px[0];
       dst[1]=px[1];
       dst[2]=px[2];
       dst[3]=255;
       if (withMask)
         out.mask[k][static_cast<size_t>(yRot)*side+xRot]=255;
      }
    }
  return out;
 }

#endif
