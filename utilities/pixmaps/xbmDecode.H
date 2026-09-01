#ifndef _XBMDECODE_H
#define _XBMDECODE_H

// xbmDecode.H — backend-agnostic XBM decode utility (rendering-abstraction task 26).
//
// The ONE XBM entry point for the GL/VK rendering path (D13): takes an XBM
// C-array symbol (the `name_bits[]` data that the .xbm files define) plus its
// `name_width`/`name_height` constants and returns the decoded pixel planes.
// Pure data transformation: includes only <cstdint> and <vector> — ZERO
// backend/X11 headers, zero F1-whitelist symbols (the X11 legs keep their
// native server-side bitmap-creation paths untouched;
// this utility is not on the X11 hot path).
//
// ---------------------------------------------------------------------------
// INPUT FORMAT (XBM, as produced by `bitmap` and as included by this repo):
//   - 1 bit per pixel; bit 0 (LSB) of each byte is the LEFTMOST pixel of the
//     row ("LSB-first within each byte" — the order the X server's
//     bitmap-from-data routines define for bitmap data).
//   - Each row is padded to a whole number of bytes: row stride is
//     ceil(width/8) bytes; there are no inter-row pad bits beyond that.
//   - A SET bit (1) is a foreground pixel; a CLEAR bit (0) is background/
//     transparent. Verified against bitmaps/enemyDecor_7x3.xbm (7x3,
//     {0x3e,0x00,0x55} -> ".#####." / "......." / "#.#.#.#").
//
// OUTPUT CONTRACT (binding for consumers — tasks 27 CPU compositing, 34 GL
// upload, 42 VK upload):
//   rgba8    : exactly w*h*4 bytes, tightly packed, row-major top-to-bottom,
//              byte order R,G,B,A per pixel (matches GL_RGBA /
//              VK_FORMAT_R8G8B8A8_UNORM upload without swizzle).
//              set bit   -> {255,255,255,255}  (white, opaque)
//              clear bit -> {0,0,0,0}          (transparent black)
//              Alpha is straight/unassociated but coincides with premultiplied
//              (white * alpha == white), so either blend mode uploads as-is.
//              Foreground COLOR (e.g. RockColor) is applied downstream at
//              draw time by tinting; the decode itself is color-free.
//   w, h     : echo of the passed width/height constants.
//   mask1bit : byte-exact copy of the source 1-bit plane (ceil(w/8)*h bytes,
//              same LSB-first/padded layout as the input) for consumers that
//              want the raw mask (e.g. masked texture draws) without
//              re-deriving it from alpha.
//
// The decoder is variant-agnostic: the _CORP_LOGO_ compile-time swap in
// rockGroup.H/shipGroup.H (ROCKDecor1/2/3 + starDestroyerDecor vs
// eightball/peace/yinyang + fortytwo) happens upstream at include time —
// here it is just bits + width + height. Both variants decode identically
// well; xbmDecodeSelfTest.C proves all 28 default-build datasets plus the 4
// _CORP_LOGO_-only files with committed golden checksums (qa/xbm-golden/).
//
// Determinism: pure function of (bits, width, height); no globals, no RNG,
// no allocation-order dependence. Re-runs are byte-identical.

#include <cstdint>
#include <vector>

struct DecodedXBM
 {public:
    std::vector<uint8_t> rgba8;     // w*h*4 bytes, R,G,B,A per pixel (see contract above)
    int w;
    int h;
    std::vector<uint8_t> mask1bit;  // ceil(w/8)*h bytes, copy of the source plane
 };

inline int XBRowStride(const int width) { return (width + 7) / 8; }

inline DecodedXBM decodeXBM(const unsigned char* const bits,
                            const int width, const int height)
 {
    DecodedXBM out;
    out.w = width;
    out.h = height;

    if (width <= 0 || height <= 0 || bits == 0)
        return out;                       // degenerate input -> empty planes

    const int stride = XBRowStride(width);
    const std::size_t planeBytes = static_cast<std::size_t>(stride) *
                                   static_cast<std::size_t>(height);

    out.rgba8.assign(static_cast<std::size_t>(width) *
                     static_cast<std::size_t>(height) * 4u, 0u);
    out.mask1bit.assign(bits, bits + planeBytes);

    for (int y = 0; y < height; ++y)
     {
        const unsigned char* const src = bits + static_cast<std::size_t>(y) * stride;
        uint8_t* const dst = out.rgba8.data() +
                             static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4u;
        for (int x = 0; x < width; ++x)
            if (src[x >> 3] & (1u << (x & 7)))      // LSB-first within each byte
             {
                dst[x*4+0] = 255u;
                dst[x*4+1] = 255u;
                dst[x*4+2] = 255u;
                dst[x*4+3] = 255u;
             }
     }
    return out;
 }

#endif
