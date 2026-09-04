#ifndef _COMPOSITEPIXMAP_H
#define _COMPOSITEPIXMAP_H

// Task 47: domain-owned 16-bit color triple. The D14 pipeline constructors
// and the composite layer API take the components directly on every leg —
// the guarded impls assemble their own XColor and allocate exactly as before
// (same request, same pixel), so no group header needs an X11-typed static.
struct RotColor
 {unsigned short red,green,blue;
 };

#ifdef X11_BACKEND
#include<X11/Xlib.h>
#endif
#include"../../rendering/renderingEngine.H"
#ifdef X11_BACKEND

class CompositePixmap
 {public:
   CompositePixmap(RenderingEngine&,
                   const int maxWidth,
                   const int maxHeight);
   ~CompositePixmap();
   CompositePixmap& AddBitmapData(const unsigned char* bitmap,
                                  const int width,
                                  const int height,
                                  const RotColor&);
   const Pixmap& GetPixmap() const;
   int GetPixWidth() const;
   int GetPixHeight() const;

 private:
   CompositePixmap(const CompositePixmap&);
   CompositePixmap& operator=(const CompositePixmap&);
   Display* const display;
   Pixmap pixmap;
   int width,
       height;
 };

inline const Pixmap& CompositePixmap::GetPixmap() const
 {return pixmap;
 }

inline int CompositePixmap::GetPixWidth() const
 {return width;
 }

inline int CompositePixmap::GetPixHeight() const
 {return height;
 }

#else
// Guards-closed twin (task 47): the GPU pipeline composites via the CPU
// stack below; this handle exists only so ExplosionGraphic's frame storage
// and its dtor stay well-formed without any X11 surface (never constructed
// there — the placement news live in the guarded configuration).
class CompositePixmap
 {public:
   ~CompositePixmap();
   int GetPixWidth() const;
   int GetPixHeight() const;

 private:
   int width,
       height;
 };

inline int CompositePixmap::GetPixWidth() const
 {return width;
 }

inline int CompositePixmap::GetPixHeight() const
 {return height;
 }
#endif
#ifndef X11_BACKEND
// ---------------------------------------------------------------------------
// CPU-side software compositing — the D14 `#else` engine branch of this TU
// (rendering-abstraction task 27). It reproduces the guarded pipeline's
// init-time stacked-bitmap compositing on plain RGBA8 buffers: same layer
// order, same later-layer-overwrites-earlier semantics, same integer
// centering expression. This is the surface the texture-upload task (plan
// task 34) consumes to produce the 5 explosion composite frames without any
// server connection; the class above remains the guarded configuration's
// resource handle and deliberately carries no buffer.
//
// Frame buffer contract (byte-layout aligned with xbmDecode.H's pinned
// RGBA8 shape): rgba8 holds exactly width*height*4 bytes, row-major
// top-to-bottom, R,G,B,A per pixel. A pixel no layer touched stays
// transparent black {0,0,0,0}; a painted pixel carries the layer color with
// alpha 255. 16-bit color components shrink to 8 bits with rounding — for
// the explosion palette {65535,65535,65535}/{65535,65535,0}/
// {65535,42405,0} this yields {255,255,255}/{255,255,0}/{255,165,0},
// bit-identical to how the guarded pipeline's color allocations resolve on
// an 8-bit-per-channel visual. Per-frame mask planes come straight from the
// source 1-bit data (decodeXBM().mask1bit, or compositeMaskExpandR8 below
// for the one-byte-per-pixel upload shape); the 5-frame explosion stack
// wears masks center/middle/edge/edge/middle — that mapping lives with the
// explosion graphic, not here.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <vector>
#include "../xbmDecode.H"

struct CompositeLayerSpec
 {public:
    const unsigned char* bits;        // 1-bit plane: ceil(width/8)-byte rows, LSB-first
    int width;
    int height;
    unsigned short red, green, blue;  // 16-bit components, as passed to the color struct
 };

struct CompositeFrameRGBA8
 {public:
    std::vector<uint8_t> rgba8;       // width*height*4 bytes (contract above)
    int width;
    int height;
 };

// Allocate and fill transparent black.
void compositeFrameInit(CompositeFrameRGBA8& frame,
                        const int width, const int height);

// Stamp one layer onto the frame; later layers overwrite earlier ones.
CompositeFrameRGBA8& compositeFrameAddLayer(CompositeFrameRGBA8& frame,
                                            const CompositeLayerSpec& layer);

// One-shot convenience: init + add layers in array order.
CompositeFrameRGBA8& compositeFrameStack(CompositeFrameRGBA8& frame,
                                         const int width, const int height,
                                         const CompositeLayerSpec* const layers,
                                         const int numLayers);

// Expand a packed 1-bit plane (ceil(w/8)-byte strided, LSB-first) into one
// byte per pixel, 255=set / 0=clear — the R8 mask shape for masked draws.
void compositeMaskExpandR8(const unsigned char* const bits,
                           const int width, const int height,
                           std::vector<uint8_t>& out);
#endif

#endif
