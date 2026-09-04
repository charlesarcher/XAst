# Task 15 — flipfix: pre/post dump invariance classification (oracle N-1 escalation)

## Gate result

Post-fix MTL identity dump (`post-mtl.raw`, new probe + fixed backend) vs
pre-fix dump (`preA-mtl.raw`, old probe + pre-fix backend, byte-identical to
the committed task-11 `qa/metal-evidence/mtl-identity.raw`):

- **2672 px differ** in total:
  - **1152 px** = the two NEW scene markers added for this task (cyan rect
    at rows 4..15, magenta rect at rows 496..507) — expected scene change,
    present and correctly oriented in `post` (self-check all-green).
  - **1520 px** = a 1-px sub-pixel **line-phase** class, confined EXACTLY to
    the horizontal edges of the five LINE_LIST primitives (see table).
- **Everything else is bit-identical**: all filled polygons (green triangle
  rows 80..178 in both), all textured quads (checker halves, rotated quad,
  masked quad incl. per-sprite texture orientation), both text classes, all
  other scene content.

## The 1520-px line-phase diff (documented tolerance class)

| y rows (preA/post) | x extent | primitive (logical y) |
|---|---|---|
| 99 / 100 | 50..250 | thin red 1-px LINE_LIST line (y=100) |
| 249 / 250 | 450..550 | blue square outline top edge (y=250) |
| 349 / 350 | 450..550 | blue square outline bottom edge (y=350) |
| 397 / 402 | 60..260 | thick-5 white line half-pixel edges (y=397.5/402.5) |
| 419 / 420 | 430..510 | cyan rect outline top edge (y=420) |
| 479 / 480 | 430..510 | cyan rect outline bottom edge (y=480) |

**Root cause**: the y-NDC sign flip (task 15) changes the sub-pixel phase of
1-px line coverage. Pre-fix the line's NDC y is `2y/H-1` (negative for
logical top-half rows), post-fix it is its exact negation `1-2y/H`; the
rasterizer's edge-function sign therefore inverts and the coverage of a line
sitting on an integer pixel center rounds one pixel the other way (row K-1
vs K). Only LINE_LIST primitives are affected — fills and textured quads are
full-area coverage and are bit-identical.

This is the same artifact class the task-11 plan documented for the GL-vs-VK
gate (oracle N-1: "MoltenVK SPIR-V→MSL triangle-edge subpixel artifacts
(line thickness 1.0f quads) from real drift (blend/sampling)"), and the same
class `vkmethods.C` Phase C already classifies via `nearSceneEdge(x,y,2.0)`
(the edge class). It is NOT blend/sampling drift: the pixel VALUES are
identical, only the 1-px edge row swaps.

**Note for the on-device MTL-vs-VK compare (this host and Linux)**: the VK
leg routes its lines through MoltenVK's own MSL line rasterizer on the same
GPU, so MTL-vs-VK line edges are expected to agree; the pre/post phase shift
above is an artifact of comparing the pre-fix (flipped) code path against the
fixed code path, not of MTL vs VK.

## Verdict

PASS (documented tolerance): no real drift; the only out-of-marker diff is
the line-edge sub-pixel class, which the cross-backend comparator already
tolerates as the edge class.