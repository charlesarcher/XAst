# Options menu — HEADLESS-SKIP (deferred to F3 human gate)

**Status:** HEADLESS-SKIP (not a failure)

## Why the menu interaction could not be completed

The game runs in a **headless environment** (XQuartz DISPLAY set, but no
composited visible display). The Metal backend's `beginFrame` hits the
nil-drawable guard on every frame (`mtlBackend: nil drawable — skipping
frame`), so the game window never renders a visible frame. The Options menu
is a Dear ImGui overlay drawn in WINDOW space on top of the rendered game
frame — with no visible frame, the menu cannot be visually opened, captured,
or interacted with.

## What WAS verified

- **Accessibility IS granted** to the executor: `osascript` System Events
  successfully enumerates the `XAsteroids` process and its `Asteroids`
  window (position 556,201; size 688x702), and can send `click` events.
- The game's Options button is at logical client (2,2) (stage.H:127
  `buttonX=buttonY=2`), top-left of the window.
- A System Events `click at {558,231}` was sent to the Options button
  position; System Events accepted it (returned the window), but no visible
  menu rendered (headless nil-drawable) and no `XAsteroids.prefs` was
  written (expected — the prefs file is only written by the FPS-slider drag
  or the Save button, not by menu open).

## Deferred to F3 (human gate)

On a machine with a visible display, the F3 human gate should:
1. Launch `./XAsteroids` (MTL build) from the repo root.
2. Click the Options button (top-left of the window).
3. Confirm the menu opens and the game pauses (evidence: `menu-open.*`
   screenshot).
4. Drag the FPS slider → confirm `XAsteroids.prefs` `uSecondsPerFrame`
   updates (evidence).
5. Close the menu → confirm the game resumes.

## Menu code-path proof (structural)

The menu is compiled and linked into the MTL build (`obj/MTL/optionsMenu.o`
is in the link line). The Options-button click path is
`playingField.H:1528 menu.open()` on MouseUp over the button. The FPS slider
writes through `host_.applyFramesPerSecondPath(1E6/fps)` →
`PlayingField::uSecondsPerFrame` (optionsMenu.C:100-107), and Save writes
`XAsteroids.prefs` via `writePreferencesFile()` (playingField.H:1755).
