# D16 per-event state machine — pollEvents seam contract (task 25)

Reusable specification for the GL/VK `pollEvents` implementations (tasks 31
and 42 **consume this document and the Q12 fixtures; do not re-derive**).
The X11 half of the contract lives as code in
`utilities/rendering/x11Backend.H::pollEvents` (the relocated island); this
document pins everything a second backend must reproduce to be
indistinguishable at the domain boundary.

## 1. The key→action table (domain-owned, backend-agnostic)

The domain applies this table in `PlayingField::RunGame` /
`PlayingField::PlayTheGame` over the drained `GameEvent` stream. Backends
only DELIVER events; they never interpret game keys.

| Event            | Character | Domain action (verbatim semantics, post-T25 site) |
|------------------|-----------|---------------------------------------------------|
| KeyDown          | `'e'`     | rotating? set `leftRotPending` : set AngularVelocity = `-angularVelocity` (+thrust coupling) |
| KeyDown          | `'r'`     | mirror of `'e'` with `rightRotPending` / `+angularVelocity` |
| KeyDown          | `'o'`     | `NewThrust()` + `thrusting=on` + `AddPermeable(thrust)` |
| KeyDown          | `'p'`     | `fireThisFrame=on` + `firing=on` latch |
| KeyDown          | `' '`     | gate `!hyperspacing`: `RemoveShip()` + `NewHyper()` |
| KeyDown          | `'q'`     | `return on` (app exit path) |
| KeyDown          | `'n'`/`'h'` | `goto ResetGameUpdateScoreAndReturn` (reset + score table) |
| KeyUp            | `'e'`     | consume pending flag: flip or zero velocity, `leftRotPending=False` |
| KeyUp            | `'r'`     | mirror of `'e'` release |
| KeyUp            | `'o'`     | `RemovePermeable(thrust)` + acceleration zeroed + `thrusting=off` |
| KeyUp            | `'p'`     | `fireThisFrame=off` + latch off |
| MouseDown btn1   | —         | title: press latch + hover press · play: starts click session |
| MouseUp btn1     | —         | title: release (+RealizeWindow if on button) · play: ends click session |
| MouseMove        | —         | title: hover press/release by position · play: only inside a click session |
| CursorEnter      | —         | play: frame-clock re-arm (`ResumePlay`) at pause-spin exit / session end |
| CursorLeave      | —         | play: ignored (pause is backend-side on X11; see §4) |

Release-consumption is the load-bearing semantic: a per-frame boolean
key-state array CANNOT express "KeyRelease consumes the pending flag"
(the B3 regression). Never port this table to polling.

## 2. GameEvent field contracts

- `character` — printable character via the backend's text translation
  (X11: `XLookupString`; GL/VK: US-layout physical-key table, §5).
  Non-printable keys deliver `character == 0` and are IGNORED by the table
  (matches X11's lookup-failure behavior).
- `key` — backend key id. Keys: X11 keysym at delivery time. Mouse buttons:
  **X11 button numbering** (Button1=1 left, 2 middle, 3 right) — a GLFW
  backend must map its 0-based mouse-button indices to +1 so the domain
  comparison stays byte-stable.
- `x`,`y` — LOGICAL CLIENT coordinates: the backend inverse-transforms raw
  window/framebuffer pointer coords through the letterbox present transform
  (S2/D17.4d). The D7 play-area offset does NOT apply.
- NO repeat member exists. See §6.

## 3. Cadence invariant (load-bearing for harness determinism)

The game drains ALL queued events ONCE PER FRAME and applies them in order
within that frame:

- A press+release consumed by the same drain cancels ('p' fires nothing,
  'e' never rotates). Q12 fixtures encode this as a negative control.
- NEVER pump events per-action or mid-frame. The quiescence sampler counts
  visible frame completions; per-event pumping changes which frame an input
  lands in and destroys cross-backend checkpoint parity.
- If the queue exceeds the caller's buffer, the remainder rides the NEXT
  frame's drain (X11 behavior; overflow is unreachable in practice because
  session motion is compressed, §4).

## 4. Pause and click-session semantics (what blocks, what is discarded)

X11 preserves the original blocking structure INSIDE `pollEvents`; a GL/VK
backend may implement the same observable contract either by blocking
(`glfwWaitEvents`) or by a domain-visible paused flag — tasks 31/42 choose;
the fixtures test observables only.

- Pointer leaves during play → PAUSE: the game stops simulating until the
  pointer re-enters. Events arriving during the pause are DISCARDED except:
  keyboard-mapping refreshes and expose-class damage. Keys pressed while
  paused never reach the game (original behavior, preserved verbatim).
- Re-enter after a pause → frame-clock re-arm (§7) delivered as
  CursorEnter.
- Button1 press during play → CLICK SESSION until release-in-window (or
  release-then-reenter): motion/up/enter/leave drive the hover stream;
  keys and other buttons are discarded mid-session; session end re-arms
  the frame clock. Hover motion may be COALESCED to the latest position
  (X11 compresses into one buffer slot) — hover is positional, so only the
  final position per drain is observable.
- Title screen: no pause, no session; MouseDown/MouseUp/MouseMove drive
  the button hover/click state machine directly.

## 5. Keymap boundary (stated difference)

US layout is the baseline. On X11, characters follow the live X keymap via
`XLookupString` (layout-dependent). GL/VK follow the static physical-key
table below, so non-US layouts diverge from X11 BY DESIGN at the keymap
boundary — documented here as accepted, not a defect:

  E→'e' R→'r' O→'o' P→'p' SPACE→' ' Q→'q' N→'n' H→'h'

`XRefreshKeyboardMapping` (MappingNotify) is a documented NO-OP on GL/VK:
the physical mapping is static per session.

## 6. GLFW_REPEAT is dropped at the boundary

Delivering repeats would cause three functional regressions with one flag:
re-fire `NewThrust` every repeat tick, re-fire random hyperspace, and
invert the pending-rotation flags at the next KeyRelease. The X11 design
suppresses repeats SERVER-SIDE (`XAutoRepeatOff` while the pointer is in
the window), so the game never sees them; dropping GLFW_REPEAT reproduces
exactly those semantics. Autofire ('p' held) is driven by the DOMAIN's
frame-head latch check, not by event repetition — holding the key must
produce one bullet per FRAME, which needs no repeats.

## 7. Frame-clock re-arm points

The D4 pacing window (`diffTime` feeding the sleep) restarts at:

1. Each frame head (`gettimeofday(&startTime)` before the drain).
2. Pause-spin exit (pointer re-enters) — X11 fires raise+sync inside
   pollEvents, then the domain's `ResumePlay` re-reads the clock.
3. Click-session end — same pair.
4. Modal Options close (X11-only; the Motif pump owns events while open —
   a GL/VK backend has no modal pump to match, D9).

GL/VK have no request-flush equivalent: measure `diffTime` from their own
frame timestamps and treat swap/present as the implicit flush (documented
swap-vs-XSync tradeoff). Do NOT add a sync inside `endFrame` — on X11 that
was empirically proven to break the harness's visible-frame sampling
(T14), and the same reasoning applies to any mid-draw-sequence pipeline
stall.

## 8. Q12 fixture index (test/harness/scripts/)

| Fixture                        | Contract encoded |
|--------------------------------|------------------|
| `q12-rotation.script`          | e/r press-then-release; pending-flag consumption; hold-e/tap-r flip |
| `q12-thrust.script`            | 'o' on/off; exactly one NewThrust per hold (repeat-drop proof) |
| `q12-fire.script`              | 'p' latch + autofire-per-frame; same-drain cancel negative control |
| `q12-hyperspace-unarmed.script`| space teleports when gate unarmed; seeded re-entry |
| `q12-hyperspace-armed.script`  | space ignored while hyperspacing; no double RNG draws |
| `q12-exits.script`             | q/n/h paths incl. reset-and-return and hard exit rc |
| `q12-title-hover.script`       | title hover/click faces; pointerButtonReleased key gating |

Pass criteria (Q12): identical object state at every scripted checkpoint
between backends (positions/velocities/hashes) and no repeat-delivery
divergence (thrust count, hyperspace RNG draws, rotation flag state).
These scripts run unmodified on X11 today; gameplay seq values are
conservative starting points — pin empirically per script-format.md rule 3.
Cross-backend runs additionally require frame-pacing parity first (the seq
targets assume the 62.5ms X11 frame clock).

## 9. X11 relocation notes (task 25 — what moved where)

- The 22-site playingField.H island relocated into
  `X11Backend::pollEvents` verbatim: pause spin, click session, server-side
  auto-repeat suppression, mapping refresh, and the resume raise+sync pair
  (now fired at spin-exit/session-end inside the backend).
- Damage (expose-class) and WM-close have no GameEvent representation in
  the frozen interface; they latch via `takeRefreshRequest()` /
  `takeCloseRequest()` and the domain applies them after each drain.
- The once-per-frame request flush is `frameClockSync()`, called by RunGame
  at the old sync's exact statement position — deliberately NOT in
  `endFrame` (T14) and NOT in pollEvents (the title loop must not pay a
  round trip per idle drain).
- Accepted micro-deltas (documented in the T25 learnings entry): title-loop
  idle parks in a 1ms sleep instead of a blocking read; MapNotify damage
  now latches in all modes (idempotent redraw); close/damage apply after
  the drained batch rather than mid-queue; press-face draws land when the
  drain returns (post-session) rather than mid-block.
