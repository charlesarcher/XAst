#!/usr/bin/env python3
# flipfix screen oracle — vision-free vertical-orientation check for the MTL
# on-screen render. Decodes the screencapture PNG (pure stdlib: zlib + manual
# unfilter, 8-bit RGB/RGBA only) and measures the dark-pixel fraction of the
# TOP half vs the BOTTOM half of the captured client region.
#
# XAsteroids title screen (non-X11 chrome, playingField.DrawHudChrome):
#   - the whole window is filled with the 46260 gray (0.706*255 ~ 180),
#   - the play area (bottom ~3/4 of the canvas, 640x512 centered) is BLACK,
#   - the title / hi-score / score / yard / options button live in the top
#     header band (dark text on gray).
# So the black play-area rectangle is the strongest orientation signal:
#   CORRECT orientation  -> play area sits in the BOTTOM  -> darkBottom >> darkTop
#   INVERTED (the bug)  -> play area mirrored to the TOP  -> darkTop  >> darkBottom
# Exit 0 = CORRECT, 1 = INVERTED, 2 = INDETERMINATE/usage.

import struct
import sys
import zlib

def load_png(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a PNG')
    pos = 8
    w = h = None
    color_type = None
    idat = b''
    while pos < len(data):
        (length,) = struct.unpack('>I', data[pos:pos+4])
        ctype = data[pos+4:pos+8]
        chunk = data[pos+8:pos+8+length]
        if ctype == b'IHDR':
            w, h, bitdepth, color_type = struct.unpack('>IIBB', chunk[:10])
            if bitdepth != 8 or color_type not in (2, 6):
                raise ValueError('only 8-bit RGB/RGBA supported')
        elif ctype == b'IDAT':
            idat += chunk
        elif ctype == b'IEND':
            break
        pos += 12 + length
    raw = zlib.decompress(idat)
    nch = 4 if color_type == 6 else 3
    stride = w * nch
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p+stride]); p += stride
        if f == 1:
            for i in range(nch, stride):
                line[i] = (line[i] + line[i-nch]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = line[i-nch] if i >= nch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i-nch] if i >= nch else 0
                b = prev[i]
                c = prev[i-nch] if i >= nch else 0
                pp = a + b - c
                pa, pb, pc = abs(pp-a), abs(pp-b), abs(pp-c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        out[y*stride:(y+1)*stride] = line
        prev = line
    return w, h, nch, out

def main():
    if len(sys.argv) < 2:
        print('usage: screen-oracle.py <capture.png>')
        return 2
    w, h, nch, px = load_png(sys.argv[1])
    half = h // 2
    def dark_fraction(y0, y1):
        dark = tot = 0
        for y in range(y0, y1):
            base = y * w * nch
            for x in range(w):
                i = base + x * nch
                if px[i] < 60 and px[i+1] < 60 and px[i+2] < 60:
                    dark += 1
                tot += 1
        return dark / tot if tot else 0.0
    dtop = dark_fraction(0, half)
    dbot = dark_fraction(half, h)
    print(f'oracle: {sys.argv[1]} {w}x{h}px, half={half}px')
    print(f'oracle: darkFraction topHalf={dtop:.4f} bottomHalf={dbot:.4f} '
          f'(delta bottom-top={dbot-dtop:+.4f})')
    if dbot > dtop + 0.15:
        print('oracle: VERDICT CORRECT (top of canvas at top of window)')
        return 0
    if dtop > dbot + 0.15:
        print('oracle: VERDICT INVERTED (render upside down)')
        return 1
    print('oracle: VERDICT INDETERMINATE')
    return 2

if __name__ == '__main__':
    sys.exit(main())