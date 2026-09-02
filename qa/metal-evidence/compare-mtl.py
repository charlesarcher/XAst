#!/usr/bin/env python3
# qa/metal-evidence/compare-mtl.py — byte-compare the MTL identity dump against
# a GL/VK reference dump, masking text rects (the documented text class).
#
# Usage: compare-mtl.py <mtl.raw> <reference.raw>
#
# Both dumps share the format: 136-byte header (uint32 hdr[34]:
#   hdr[0]=w, hdr[1]=h, hdr[2+4i..5+4i]=textRects, hdr[30]=numTextRects)
# followed by top-down RGBA pixels (w*h*4 bytes).
#
# Classification (mirrors vkmethods.C Phase C):
#   exact / tol1 (+-1) / text (inside a text rect) / HARD (must be 0)
import struct
import sys

def load(path):
    with open(path, 'rb') as f:
        hdr = f.read(136)
        px = f.read()
    w, h = struct.unpack('<II', hdr[:8])
    nrects = struct.unpack('<I', hdr[120:124])[0]
    rects = []
    for i in range(nrects):
        x, y, rw, rh = struct.unpack('<4I', hdr[8+i*16:24+i*16])
        rects.append((x, y, rw, rh))
    return w, h, rects, px

def in_rects(rects, x, y, pad=3):
    for (rx, ry, rw, rh) in rects:
        if rx-pad <= x <= rx+rw+pad and ry-pad <= y <= ry+rh+pad:
            return True
    return False

def main():
    if len(sys.argv) != 3:
        print('usage: compare-mtl.py <mtl.raw> <reference.raw>')
        return 2
    w1, h1, r1, p1 = load(sys.argv[1])
    w2, h2, r2, p2 = load(sys.argv[2])
    if (w1, h1) != (w2, h2):
        print(f'FAIL: size mismatch MTL {w1}x{h1} vs ref {w2}x{h2}')
        return 1
    w, h = w1, h1
    rects = r1
    total = w * h
    exact = tol1 = text = hard = 0
    hard_samples = []
    for y in range(h):
        for x in range(w):
            off = (y*w + x) * 4
            a = p1[off:off+4]
            b = p2[off:off+4]
            maxd = max(abs(a[c]-b[c]) for c in range(3))
            if maxd == 0:
                exact += 1
            elif in_rects(rects, x, y):
                text += 1
            elif maxd <= 1:
                tol1 += 1
            else:
                hard += 1
                if len(hard_samples) < 10:
                    hard_samples.append((x, y, a, b))
    print(f'compare {sys.argv[1]} vs {sys.argv[2]} ({w}x{h}, {len(rects)} text rects):')
    print(f'  exact={exact} ({100.0*exact/total:.4f}%) tol1={tol1} '
          f'text={text} HARD={hard}')
    for (x, y, a, b) in hard_samples:
        print(f'  HARD ({x},{y}) mtl={a[0]:02x}{a[1]:02x}{a[2]:02x} '
              f'ref={b[0]:02x}{b[1]:02x}{b[2]:02x}')
    if hard != 0:
        print('FAIL: HARD divergent pixels outside text/tol1 classes')
        return 1
    print('PASS: no HARD divergent pixels')
    return 0

if __name__ == '__main__':
    sys.exit(main())
