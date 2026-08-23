# harness script format — test/harness scripts (plan task 9, D17.3/B7)

Scripted input vocabulary for `obj/harness` (build: `make harness`). The same
vocabulary replays on X11 today and on the GL backend later (D16): every
coordinate is a CLIENT-area coordinate, every key is a character the game
translates via `XLookupString` (US layout under Xvfb), and every event is
injected via XTest (`XTestFakeKeyEvent` / `XTestFakeButtonEvent` /
`XTestFakeMotionEvent`, libXtst) — real server events, indistinguishable from
hardware input.

## Line grammar

    [# comment]
    [seq,] action [arg...]

The bracketed/comma form `[seq, action, arg]` and plain whitespace-separated
form are both accepted; `#` starts a comment. Blank lines are ignored.

- `seq` — optional boundary index at which the line executes. Boundaries are
  monotonic; an explicit seq must never go backwards. When omitted, the target
  is the previous line's target + 1.
- Multiple lines may share one seq: they execute at the same boundary, in file
  order (but see the same-drain caveat below — never split a press/release
  across zero boundaries).

## Actions

| action          | arg              | effect |
|-----------------|------------------|--------|
| `keydown`       | CHAR             | XTest key press for CHAR (`s`, `e`, `r`, `o`, `p`, `q`, `n`, `h`, or `space`) |
| `keyup`         | CHAR             | XTest key release for CHAR |
| `mousedown`     | X Y [BTN]        | warp pointer to client (X,Y), then button BTN (default 1) press |
| `mouseup`       | X Y [BTN]        | warp pointer to client (X,Y), then button BTN release |
| `move`          | X Y              | warp pointer to client (X,Y) |
| `enter`         | —                | warp pointer to the client-area center (generates EnterNotify) |
| `leave`         | —                | warp pointer outside the window (generates LeaveNotify; NOTE: the game PAUSES on LeaveNotify during play — playingField.H nested loop waits for EnterNotify) |
| `resize`        | W H              | programmatic `XResizeWindow` driving ConfigureNotify → `stage->Refresh()` (Q15/R6). Works under bare Xvfb because no WM enforces the game's PSizeHints pin. |
| `capture`       | NAME             | grab the client area via `XGetImage`, write `<out>/NAME.png`, record a checkpoint |
| `wait`          | N                | pure pacing: advances the auto-increment target by N boundaries |
| `exit`          | —                | stop the script and wait up to 20s for a natural game exit ('q') |

## Frame sync / handshake (`--handshake` or `XAST_HANDSHAKE`)

Events are injected ONLY at boundaries — never mid-frame.

### `quiescence` (default; the PRE-endFrame tree)

A boundary ticks when a NEW visible change of the client area (any pixel
differing from the previous `XGetImage` grab) has settled for `--stability`
consecutive identical grabs (`--poll-ms` apart). During play the game renders
a distinct frame every 62.5ms (`uSecondsPerFrame=62500`), so **boundaries
count visible frame completions** — a deterministic function of the seeded
trajectory, which is what makes two runs capture the same sim frame and
inject between the same frames. Plain wall-clock pacing was tried and REJECTED
empirically: it diverges within the first seconds of gameplay (phase drift
between the tick clock and the frame clock).

Static screens (help, hi-score) never change; there an **idle escape** ticks
boundaries on a timer instead (`--idle-escape-ms`, default 150ms of total
stillness, then every `--tick-gap-ms`). Safe because nothing simulates while
the outer event loop blocks in `XNextEvent` — any injection instant is
equivalent on a static screen.

Injection timing safety: a tick fires ≥ `stability × poll-ms` (~30ms) after
the blit that caused the change; the game's next event drain is ~59ms after
that blit (62.5ms period minus ~3ms of work). Events therefore land in the
quiet tail of a frame period with ~25ms margin, and after `XSync` the server
has queued them — the only residual race is a >55ms Xvfb scheduling stall
skipping an entire inter-frame gap (would shift the change count by one;
treat any determinism failure as a signal to re-run before investigating the
tree).

### `counter` (task 12+, D17.3/O5-M3 — slots in without redesign)

The game publishes a monotonically increasing frame counter into the
`_XAST_FRAME_COUNTER` property (CARDINAL, 32-bit) on its app window inside
`endFrame()`. The harness polls the property (~2ms) and executes exactly ONE
boundary per published value: `seq` means the literal published frame number,
captures sample each counter value exactly once, inputs inject between
counter increments (never mid-frame). A repeated value aborts as a uniqueness
violation; a non-monotonic value aborts as corruption; no advance within
`--stall-timeout-ms` (default 5000) aborts as a hang. Skipped values are
reported (nearest-completed-frame matching) but not fatal. On the current
pre-endFrame tree this mode aborts with the stall diagnostic by design — it
becomes usable the moment task 12 lands the publisher.

## Authoring rules

1. Separate `keydown`/`keyup` by ≥3 boundaries. The game drains ALL queued
   events once per frame; a press+release consumed by the same drain cancels
   (e.g. 'p' would fire nothing, 'e' would never rotate).
2. Never let the pointer leave the window during play (`leave` pauses the
   game). The harness parks the pointer in the client center before the
   script runs.
3. Derive gameplay seq values empirically: run once with probe captures, read
   the freeze/transition points from `compare -metric AE` between captures,
   then pin targets with margin. Static-screen targets need only exceed the
   observed transition boundary.
4. One action per boundary unless ordering within a single drain is intended.

## Diff engine

Each `capture NAME` checkpoint is verified against `<ref>/NAME.png` when
`--ref DIR` is given, using ImageMagick
`compare -metric AE [-read-mask <ref>/NAME.mask.png] mine ref null:`.

- Pass iff AE == 0 exactly (this IM7 build reports fractional AE values, but
  identical images always print exactly `0 (0)`).
- Mask semantics (IM7 `-read-mask`, verified empirically): WHITE mask pixels
  are COMPARED, BLACK pixels are IGNORED. A `<name>.mask.png` beside the
  reference is applied automatically.
- The committed masks under `qa/baseline-x11/session/` are full-content
  PLACEHOLDERS (all white = compare everything = no-op) so the plumbing is
  exercised on every X11-vs-X11 gate; they will be refined to black-out text
  regions when cross-backend gates (GL/VK font rasterization differences)
  need them (F2).
- Exit code: 0 all checkpoints pass · 1 pixel/mask failure or missing
  reference · 2 infrastructure failure (pre-flight, Xvfb, window, timeouts).

## Canonical invocations

    source qa/env/env.sh
    make harness && make XAsteroids

    # baseline capture (Q13 reference):
    ./obj/harness --seed 12345 --script test/harness/scripts/session.script \
        --out qa/baseline-x11/session \
        --hiscore test/harness/fixtures/hiScore.nul.data

    # verification gate (tasks 23/28/36/43/48 run this shape):
    ./obj/harness --seed 12345 --script test/harness/scripts/session.script \
        --out /tmp/run --ref qa/baseline-x11/session \
        --hiscore test/harness/fixtures/hiScore.nul.data

## Fixture note (hi-score file)

`--hiscore FILE` stages FILE as `hiScore.data` in the game's cwd, where
score.H's candidate chain picks it up (the `$HOME/XAsteroids/` candidate
earlier in the chain must not exist). Use
`test/harness/fixtures/hiScore.nul.data` — the NUL-format encoding of
`qa/fixtures/hiScore.data`. The whitespace-delimited committed fixture cannot
be parsed by the PRE-task-10 reader (its custom `operator>>(ifstream&, char*)`
consumes names up to a NUL byte): against it the game yields `numScores=1`,
a mangled name, and an UNINITIALIZED `score[0]` rendered as per-run garbage
digits — nondeterministic by construction. See `qa/baseline-x11/MANIFEST`.
