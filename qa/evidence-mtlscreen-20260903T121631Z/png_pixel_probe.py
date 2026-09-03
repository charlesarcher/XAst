#!/usr/bin/env python3
# png_pixel_probe.py <png> [thresh=16]
# Pure-stdlib PNG decoder (zlib + unfilter). Counts pixels that are NOT
# (near-)black: a pixel counts if max(R,G,B) > thresh. Used as a vision-free
# proof that the window region actually shows content (title screen text/graphics)
# versus a pure-black client area.
import sys, zlib, struct

CHANS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}

def read_png(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', "not a PNG: " + path
    pos = 8
    width = height = bitdepth = colortype = None
    interlace = 0
    idat = b''
    while pos < len(data):
        length = struct.unpack('>I', data[pos:pos+4])[0]
        ctype = data[pos+4:pos+8]
        cdata = data[pos+8:pos+8+length]
        pos += 12 + length
        if ctype == b'IHDR':
            width, height, bitdepth, colortype, comp, filt, interlace = \
                struct.unpack('>IIBBBBB', cdata)
        elif ctype == b'IDAT':
            idat += cdata
        elif ctype == b'IEND':
            break
    assert width is not None, "no IHDR"
    assert interlace == 0, "interlaced PNG unsupported"
    assert colortype in CHANS, "unsupported colortype %r" % colortype
    assert bitdepth == 8, "bitdepth %d unsupported (want 8)" % bitdepth
    ch = CHANS[colortype]
    bpp = ch  # 8-bit -> 1 byte per channel
    raw = zlib.decompress(idat)
    stride = width * bpp
    out = bytearray()
    prev = bytearray(stride)
    i = 0
    for y in range(height):
        ftype = raw[i]; i += 1
        line = bytearray(raw[i:i+stride]); i += stride
        if ftype == 0:
            pass
        elif ftype == 1:
            for x in range(bpp, stride):
                line[x] = (line[x] + line[x-bpp]) & 0xff
        elif ftype == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 0xff
        elif ftype == 3:
            for x in range(stride):
                a = line[x-bpp] if x >= bpp else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 0xff
        elif ftype == 4:
            for x in range(stride):
                a = line[x-bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x-bpp] if x >= bpp else 0
                p = a + b - c
                pa = abs(p-a); pb = abs(p-b); pc = abs(p-c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 0xff
        else:
            raise ValueError("bad filter %d" % ftype)
        out += line
        prev = line
    return width, height, colortype, out

def main():
    path = sys.argv[1]
    thresh = int(sys.argv[2]) if len(sys.argv) > 2 else 16
    w, h, ct, pix = read_png(path)
    ch = CHANS[ct]
    # Color channels to consider (exclude the alpha channel so an opaque black
    # pixel R=G=B=0,A=255 is correctly "black"). colortype: 0=gray,2=RGB,
    # 4=gray+alpha,6=RGBA.
    rgb_count = {0: 1, 2: 3, 4: 1, 6: 3}[ct]
    total = w * h
    nonblack = 0
    for p in range(total):
        base = p * ch
        m = 0
        for c in range(rgb_count):
            v = pix[base + c]
            if v > m:
                m = v
        if m > thresh:
            nonblack += 1
    pct = 100.0 * nonblack / total if total else 0.0
    print("png_pixel_probe %s: %dx%d colortype=%d thresh>%d" % (path, w, h, ct, thresh))
    print("  total_pixels=%d non_black=%d (%.3f%%)" % (total, nonblack, pct))
    print("  VERDICT=%s" % ("NON-BLACK" if nonblack > 1000 else "BLACK/NEAR-BLACK"))
    return 0

if __name__ == '__main__':
    sys.exit(main())