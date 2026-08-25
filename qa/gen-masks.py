#!/usr/bin/env python3
# gen-masks.py — Task-36 Tier-2 mask generator (rendering-abstraction).
#
# Derives per-checkpoint exclusion masks from DRAW-CLASS GEOMETRY dumped by
# the env-gated instrumentation in gamePlay/playingField.H (XAST_DRAWDUMP_FILE
# / XAST_TEXTDUMP_FILE) — NEVER from pixel diffs (a pixel-derived mask would
# make the gate vacuous). Mask semantics match harness.C's compare: WHITE is
# compared, BLACK is ignored.
#
# Masked classes (union across ALL supplied legs — a region is excluded if it
# is non-bitmap-comparable on ANY leg):
#   1. Re-rasterized content: GL 'o' wireframe outlines and 't' transformed
#      composites (rock decor, ship body). The t rect is the unrotated sprite
#      size centered on (tx,ty); rotation expands the extent by sqrt(2).
#   2. GXor-overlap regions: pairwise bounding-box intersections of ALL
#      records in the frame. X11 world sprites XOR-composite through the
#      gxor GC; GL alpha-blends — overlap pixels are blend-dependent on both
#      legs, so every pairwise intersection is excluded.
#   3. Text regions: the help-screen and game-over table blocks dumped at the
#      draw sites, unioned across legs (GL stb text metrics differ from X11).
#
# Everything else — isolated plain/masked blits of pre-rasterized bitmaps —
# stays WHITE and is what the Tier-2 gate actually proves.
#
# Usage:
#   python3 qa/gen-masks.py --out qa/masks \
#       --leg /tmp/run-x11:/tmp/run-gl \
#       --drawdump drawdump.txt --textdump textdump.txt \
#       --checkpoint help=static --checkpoint start=37 --checkpoint gp1=117 ...
#       [--max-coverage 0.5]
#
#   --leg DIR:DIR:...  one or more run directories per leg group; records and
#                      text are UNIONED across all of them. Each dump's leading
#                      "p X Y W H" line supplies that leg's play-rect origin;
#                      window coords are translated into crop-local coords.
#   --checkpoint NAME=FRAME  FRAME is the simulation-frame index (manifest
#                      boundary minus frame_base). The reserved value "static"
#                      means no world records apply (pure text/static screen).

import argparse
import os
import subprocess
import sys

CROP_W, CROP_H = 640, 512
# playArea.NorthWestCorner() — the logical origin every dumped draw/text
# coordinate is relative to (ROCK::scale*2; playingField.H playArea def).
LOGICAL_X, LOGICAL_Y = 80.0, 80.0


def parse_drawdump(path):
    """-> frames{f:[(cls,x,y,w,h)]}; coords are LOGICAL play-area space."""
    frames = {}
    cur = None
    with open(path) as f:
        for line in f:
            p = line.split()
            if not p:
                continue
            if p[0] == "f":
                cur = int(p[1])
                frames.setdefault(cur, [])
            elif p[0] == "r" and cur is not None:
                frames[cur].append((p[1], float(p[2]), float(p[3]),
                                    float(p[4]), float(p[5])))
    return frames


def parse_textdump(path):
    blocks = []  # (tag,x,yTop,w,h), LOGICAL play-area space
    with open(path) as f:
        for line in f:
            p = line.split()
            if not p:
                continue
            if p[0] == "t":
                blocks.append((p[1], float(p[2]), float(p[3]),
                               float(p[4]), float(p[5])))
    return blocks


def expand(rec):
    """record -> padded axis-aligned crop-space rect (x0,y0,x1,y1)."""
    cls, x, y, w, h = rec
    if cls == "t":  # rotated about (x,y): conservative sqrt(2) bound
        side = max(w, h) * 1.4143 + 2.0
        return (x - side / 2, y - side / 2, x + side / 2, y + side / 2)
    pad = 2.0 if cls == "o" else 0.0
    return (x - pad, y - pad, x + w + pad, y + h + pad)


# Phase-5 finding (task 46): recorded draw bboxes can UNDER-COVER the rendered
# extent by 1-3px — X11's pre-rasterized ROTATED rock pixmaps (RotVectorData)
# blit a few px past the logical box the dumper records, and masked-blit edges
# show the same sub-rect spill. PAD grows every rect (all classes + overlaps +
# text) by a uniform geometry-derived margin; it stays pixel-independent and
# coverage keeps its honest-gate cap. Default 0 reproduces the task-36 masks.
PAD = 0.0


def clip(r):
    x0, y0, x1, y1 = r
    return (max(x0, 0.0), max(y0, 0.0), min(x1, float(CROP_W)),
            min(y1, float(CROP_H)))


def pad(r):
    """PAD growth for DRAW-derived rects only — text blocks already carry
    their own margin and are large enough that a few px of spill is
    irrelevant; padding them blew the 50% honest-gate cap on table screens."""
    x0, y0, x1, y1 = r
    return (x0 - PAD, y0 - PAD, x1 + PAD, y1 + PAD)


def area(r):
    return max(0.0, r[2] - r[0]) * max(0.0, r[3] - r[1])


def intersect(a, b):
    return (max(a[0], b[0]), max(a[1], b[1]),
            min(a[2], b[2]), min(a[3], b[3]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--leg", action="append", required=True,
                    help="DIR[:DIR...] whose dumps union into the mask")
    ap.add_argument("--drawdump", default="drawdump.txt")
    ap.add_argument("--textdump", default="textdump.txt")
    ap.add_argument("--checkpoint", action="append", required=True,
                    help="NAME=FRAME|static")
    ap.add_argument("--text-on", action="append", default=[],
                    help="NAME=TAG: apply dumped text blocks tagged TAG to "
                         "checkpoint NAME (e.g. hiscore=table, help=help)")
    ap.add_argument("--out", default="qa/masks")
    ap.add_argument("--max-coverage", type=float, default=0.5)
    ap.add_argument("--pad", type=float, default=0.0,
                    help="uniform safety margin (px) grown around every "
                         "rect at raster time; 0 reproduces task-36 masks")
    args = ap.parse_args()
    global PAD
    PAD = args.pad

    legs = []
    for group in args.leg:
        for d in group.split(":"):
            dd = os.path.join(d, args.drawdump)
            td = os.path.join(d, args.textdump)
            draws = parse_drawdump(dd) if os.path.exists(dd) else {}
            texts = parse_textdump(td) if os.path.exists(td) else []
            legs.append((draws, texts))
    if not legs:
        sys.exit("no leg dumps found")

    os.makedirs(args.out, exist_ok=True)
    report = []
    for spec in args.checkpoint:
        name, _, frame_s = spec.partition("=")
        rects = []
        if frame_s != "static":
            frame = int(frame_s)
            for (frames, _texts) in legs:
                # Draw-record coords are LOGICAL play-area space on every
                # leg (playArea.NorthWestCorner() = (80,80) canvas origin;
                # verified against sprite pixels): crop-local = v - 80.
                for cls, x, y, w, h in frames.get(frame, []):
                    rects.append(pad(expand((cls, x - LOGICAL_X,
                                             y - LOGICAL_Y, w, h))))
            n = len(rects)
            for i in range(n):          # GXor/blend overlaps: all pairs
                for j in range(i + 1, n):
                    ov = clip(intersect(rects[i], rects[j]))
                    if area(ov) > 1.0:
                        rects.append(ov)
        # Text blocks (--text-on NAME=TAG), unioned across legs (stb metrics
        # differ from X11 server fonts).
        want_tag = dict(kv.split("=", 1) for kv in args.text_on).get(name)
        if want_tag:
            for (_texts_is_second, texts) in legs:
                # Text-block coords are logical play-area space too.
                for tag, x, y, w, h in texts:
                    if tag == want_tag:
                        rects.append(clip((x - LOGICAL_X - 2, y - LOGICAL_Y - 2,
                                           x - LOGICAL_X + w + 2,
                                           y - LOGICAL_Y + h + 2)))
        report.append((name, rects))

    total = float(CROP_W * CROP_H)
    failed = False
    for name, rects in report:
        grid = bytearray(CROP_W * CROP_H)   # union raster (exact coverage)
        clipped = []
        for r in rects:
            c = clip(r)
            if area(c) <= 0:
                continue
            clipped.append(c)
            x0, y0, x1, y1 = int(c[0]), int(c[1]), int(c[2]), int(c[3])
            for yy in range(y0, min(y1, CROP_H)):
                base = yy * CROP_W
                for xx in range(x0, min(x1, CROP_W)):
                    grid[base + xx] = 1
        masked = sum(grid)
        cov = masked / total
        status = "OK" if cov <= args.max_coverage else "COVERAGE-EXCEEDED"
        if cov > args.max_coverage:
            failed = True
        print(f"{name:10s} rects={len(clipped):4d} "
              f"masked={masked:7d}px coverage={cov*100:6.2f}% {status}")
        # Overlapping draw rects are fine: black-on-black idempotent.
        draw_cmd = " ".join(f"rectangle {r[0]:.0f},{r[1]:.0f}"
                            f" {r[2]-1:.0f},{r[3]-1:.0f}" for r in clipped)
        out = os.path.join(args.out, f"{name}.mask.png")
        cmd = (f"convert -size {CROP_W}x{CROP_H} xc:white "
               f"-fill black -draw \"{draw_cmd}\" png:'{out}'")
        subprocess.run(cmd, shell=True, check=True)
    if failed:
        sys.exit("mask coverage exceeded the honest-gate bound; STOP")


if __name__ == "__main__":
    main()
