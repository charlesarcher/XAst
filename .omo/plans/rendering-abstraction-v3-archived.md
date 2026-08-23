# XAst Rendering Abstraction

## TL;DR (For humans)

Replace direct X11/Xlib rendering calls with an abstract `RenderingEngine` interface (20 virtual methods), then implement OpenGL 4.6 Core + Vulkan 1.4 backends via GLFW. **All three backends (X11, GL, Vulkan) are essential, first-class deliverables.** The X11 backend is a pass-through that preserves exact current behavior (Pixmap = TextureId, X fonts retained). GPU rotation for wireframes, texture-mapped compositing for bitmap-decorated objects. Screen-wrap via single draw + scissor clip (not double-draw). Frame pacing preserved (uSecondsPerFrame + diffTime compensation). Motif/Xt Options dialog deferred for GL/Vulkan (runs on X11 only). Build system: extend existing `makefile` with `BACKEND` variable (no CMake migration). Game loop restructured to frame-based rendering. Event loop translated from XNextEvent polling to GLFW callbacks on GL/Vulkan.

**44 executable tasks across 6 phases.**

## Scope

### In Scope
- Abstract `RenderingEngine` API (20 virtual methods including `nativeHandle()`)
- X11 pass-through backend (TextureId = Pixmap; X fonts; exact current behavior)
- OpenGL 4.6 Core profile
- Vulkan 1.4 (Roadmap 2026 profile)
- GPU rotation for wireframe objects (ships, bullets, thrust flames)
- Texture-mapped compositing for bitmap-decorated objects (rocks, explosions)
- Frame-based game loop restructuring
- Event-loop translation (GLFW callbacks on GL/Vulkan)
- Font abstraction: stb_truetype for GL/Vulkan only; X fonts for X11
- Screen-wrap via single draw + play-area scissor clip
- Clip-mask via pre-computed texture masks (R8 channel, fragment-shader discard)
- Thick-line rendering via filled quads (GL/Vulkan cannot guarantee line width > 1)
- Build system: makefile `BACKEND` variable, per-target link flags
- Dependency vendoring: GLFW, glad, stb_truetype, Vulkan loader

### Out of Scope
- Motif/Xt Options dialog rewrite (deferred; X11-only initially; GL/Vulkan unavailable until UI framework chosen)
- Anti-aliasing / visual quality improvements
- Audio abstraction
- Input abstraction beyond what GLFW provides
- Cross-platform (Linux only; GLFW windowing on X11/Wayland)
- Performance optimization beyond baseline correctness

---

## Design Decisions

### D1: All three backends are first-class — X11, GL, Vulkan
**Decision:** Vulkan and OpenGL backends ship alongside the X11 pass-through with identical feature parity. X11 is not "primary" — it is one of three co-equal backends.
**Rationale:** User requirement. All three backends must produce correct, equivalent output.
**Impact:** None beyond GL scope — all three backends are co-equal.

### D2: Hybrid rotation strategy
**Decision:** GPU rotation via MVP matrix for wireframe-only objects. Texture-mapped compositing for bitmap-decorated objects. Pre-computed textures for pure-bitmap objects.
**Rationale:** The RotatorDisplayData hierarchy has 5 subclasses:
- `NonRotVectorData` / `RotVectorData`: wireframe (or wireframe + bitmap interior) → GPU rotation for outline, texture lookup for interior
- `MaskedRotVectorData`: wireframe + interior + clip mask (all rock types) → GPU rotation + texture composite + clip mask texture
- `RotPixmapData` / `MaskedRotPixmapData`: pure bitmap (thrust flames) → pre-computed textures (no rotation benefit)
**Impact:** Eliminates ~60% of pre-computed wireframe pixmaps. Retains pre-computed textures for bitmap-heavy objects. Net memory savings ~50%.

### D3: GXor is load-bearing — RETAIN on X11
**Decision:** GXor at `playingField.H:135` provides implicit transparency and MUST be retained on the X11 backend.
**Rationale:** Object pixmaps are filled with black background (`rotatorDisplayData.C:192`: `XFillRectangle(display,pixmap,gc,0,0,sideSize,sideSize)` with `background=BlackPixel`). GXor provides transparency: black source pixels (background) preserve the destination (0 XOR dest = dest), while colored source pixels paint normally (color XOR 0 = color). Without GXor (GXcopy), black source pixels would **erase previously-drawn overlapping objects** within the same frame.

Within a single frame, multiple objects draw via `XCopyArea` onto the same pixmap using `playingField.gc`. If two object pixmaps overlap:
- GXor: black areas are transparent (dest preserved), colored areas paint → correct compositing
- GXcopy: black areas overwrite dest to black → **holes in previously-drawn objects**

**Impact:** GXor is moved into X11Backend's internal GC (not exposed in API). GL/Vulkan backends handle transparency via proper alpha blending in their texture pipelines. The API's `drawTexture()` alpha parameter provides the equivalent behavior.

### D4: Frame timing — preserve dynamic pacing
**Decision:** Preserve the existing `uSecondsPerFrame` + diffTime compensation mechanism. On GL/Vulkan, `glfwSwapInterval(0)` + equivalent timing loop.
**Rationale:** Frame period is user-configurable at runtime via Options dialog (FPS scale widget at `options.H:729`; callback → `AlterFramesPerSecond` at `options.H:3086-3088` → `playingField.H:672` `uSecondsPerFrame=newUSecondsPerFrame; speedAdjust=...`). Default is 62500µs (16fps) per `playingField.H:98`. The pacing at `playingField.H:504-505` is diffTime-compensated: `if (diffTime<uSecondsPerFrame) usleep(uSecondsPerFrame-diffTime)`.
**Impact:** Game speed unchanged. On GL/Vulkan, frame-rate configurability is unavailable until Options is ported (see D9).
**Consequence (D9):** GL/Vulkan backends run at fixed default frame period; the FPS scale widget has no effect there.
**Risk:** Some drivers override `glfwSwapInterval(0)` with compositor VSync. Document this as a known limitation.

### D5: RenderingEngine API — 20 virtual methods
**Decision:** The API covers all rendering and windowing operations. Updated from the original 19 methods to include explicit opaque-bg text rendering and line attributes:

```cpp
class RenderingEngine {
public:
    virtual ~RenderingEngine() = default;

    // Window lifecycle
    virtual void initWindow(int w, int h, const char* title) = 0;
    virtual void shutdown() = 0;
    virtual void* nativeHandle() const = 0; // X11: X11NativeHandle{Display*,Window}; GL/VK: GLFWwindow*

    // Frame lifecycle
    virtual void beginFrame() = 0;   // X11: noop; GL/VK: acquire + clear
    virtual void endFrame() = 0;     // X11: XSync + pixmap copy; GL/VK: swap/present

    // Primitives
    virtual void clear(float r, float g, float b) = 0;
    virtual void drawLine(float x1, float y1, float x2, float y2,
                          float r, float g, float b, float width) = 0;
    virtual void drawPolygon(const float* verts, int count,
                             float r, float g, float b, bool fill) = 0;
    virtual void drawRect(float x, float y, float w, float h,
                          float r, float g, float b, bool fill) = 0;

    // Text — two variants for X11 compatibility
    // drawStringOpaque: fills background rectangle (maps to XDrawImageString on X11)
    // Used for: score display, button labels, hi-score table
    virtual void drawStringOpaque(const char* text, float x, float y, int fontId,
                                  float r, float g, float b,
                                  float bgR, float bgG, float bgB) = 0;
    // drawStringTransparent: no background fill (maps to XDrawString on X11)
    // Used for: help screen text
    virtual void drawStringTransparent(const char* text, float x, float y, int fontId,
                                       float r, float g, float b) = 0;
    virtual float measureText(const char* text, int fontId) = 0;
    virtual void getFontMetrics(int fontId, float* ascent, float* descent,
                                float* lbearing) = 0; // lbearing for button label centering

    // Textures
    virtual TextureId createTextureFromBitmap(const uint8_t* data, int w, int h, int channels) = 0;
    virtual TextureId createTextureFromXBM(const uint8_t* bits, int w, int h,
                                           float fgR, float fgG, float fgB) = 0;
    virtual void drawTexture(TextureId tex, float x, float y, float w, float h, float alpha) = 0;
    virtual void drawTextureMasked(TextureId content, TextureId mask,
                                   float x, float y, float w, float h) = 0;
    virtual void deleteTexture(TextureId tex) = 0;

    // Transform (flat: translate + rotate composed; no stack needed)
    virtual void setTransform(float tx, float ty, float angleRadians) = 0;
    virtual void resetTransform() = 0;
};
```

**Key changes from original D5:**
1. **`drawStringOpaque` + `drawStringTransparent`** replace single `drawString` — the codebase uses both `XDrawImageString` (opaque bg, for score/button labels) and `XDrawString` (transparent bg, for help text). A single `drawString` cannot be pixel-identical on X11.
2. **`getFontMetrics` adds `lbearing`** — `button.H:137-140` uses `XTextExtents` lbearing for label centering within buttons.
3. **Method count: 20** (was 19). The two text methods + lbearing parameter replace the single drawString + 2-param getFontMetrics.

**Notes on transform stack simplification:** The game only composes translate∘rotate per single object — no stacking. `setTransform(tx,ty,angle)` + `resetTransform()` replaces the 4-method push/pop/rotate/translate stack.

**`nativeHandle()` returns:**
- X11 backend: `X11NativeHandle` struct containing `Display*` and `Window` — used by Options dialog (X11-only)
- GL backend: `GLFWwindow*`
- Vulkan backend: `GLFWwindow*` (Vulkan surfaces created from GLFW window)

### D6: Clip masking — pre-computed texture masks (R8 format)
**Decision:** Upload existing 1-bit clip masks as single-channel (R8) GL/Vulkan textures. Fragment shader uses `discard` based on mask alpha.
**Rationale:** Clip masks are pre-computed bitmaps: `explosionEdge_bits`, `explosionMiddle_bits`, `explosionCenter_bits` (explosionGraphic.H:94-100); polygon-derived masks for rocks (rotatorDisplayData.C:137). Stencil adds a render pass for no benefit. Texture lookup matches existing data.
**Impact:** One extra texture per masked draw call. Negligible cost.

### D7: Screen-wrap — single draw + scissor clip
**Decision:** Single draw at the wrapped box position + scissor/viewport clip to the play area. No double-draw.
**Rationale:** `Box::WrapMovingBox` (box.H:251-287) wraps **only when the box is fully off the opposite edge** (teleport via fmod/offset). While an object **straddles** the edge, no wrap occurs — the original draws it **once**, clipped at the pixmap boundary (640×512). Double-draw would paint a ghost copy at the opposite edge during straddle — an artifact the original does not have.
**Impact:** `drawTextureWrapped()` removed from API (was already dropped in D5). The backend clips automatically via scissor rect set in `beginFrame()`.
**Note:** The X11 pixmap is oversized (`playArea.Width()+4*ROCK::scale` × `playArea.Height()+4*ROCK::scale` per `playingField.H:131`). The scissor rect must map to the play area sub-region within the window, not the full framebuffer.

### D8: Game loop restructuring
**Decision:** Restructure to frame-based rendering with explicit begin/end.
**Current flow:**
1. `MissScript()` for each object → `XCopyArea(objectPixmap → playingField.pixmap)` (inside `Intersect()`)
2. `stage.DrawPlayingField()` → `XCopyArea(pixmap → window)`
3. `XFillRectangle(pixmap, bg)` → clear for next frame

**New flow:**
1. Update positions (in Intersect — no drawing; `Render()` methods added to each object)
2. `engine->beginFrame()`
3. `engine->clear()` + set scissor to play area
4. Draw score, shipyard, all objects via `Render()` methods
5. `engine->endFrame()` → GL: `glfwSwapBuffers`; VK: `vkQueuePresent`

### D9: Options dialog — deferred for GL/Vulkan
**Decision:** Keep Motif/Xt Options dialog for X11 backend (accessing Display/Window via `engine->nativeHandle()`). On GL/Vulkan, Options is unavailable until a UI framework is chosen.
**Rationale:** 3870 lines of Motif code with 50+ widgets, its own Xt event loop. Rewriting is a separate project.
**Impact (explicit consequences):**
- Options dialog unavailable on GL/Vulkan for initial delivery
- Frame-rate configuration unavailable on GL/Vulkan (game runs at default 62500µs; see D4)
- Volume/mute settings unavailable on GL/Vulkan
- Save/load preferences unavailable on GL/Vulkan

**Coupling depth (M6 correction):** The Options dialog is more deeply coupled than `nativeHandle()` alone suggests:
1. `options.H:2450,2474` — 2 `XCreateBitmapFromData` calls for scoring icons (X11-only)
2. `options.H:729` — FPS scale widget callback → `AlterFramesPerSecond` → writes `playingField.H:672`
3. `options.H:2608-2640` — Save/Load preferences via `XmStringGetLtoR` parsing
4. `options.H:3086-3088` — Volume/mute callbacks directly modify `playingField` state
5. Options shares the X11 event loop — calls `XtDispatchEvent` which can only run when the main loop yields
6. Options stores its own `Display*` and `Widget` references tightly coupled to X11 lifecycle

**GL/VK UX gap (M13):** On GL/VK, the Options button is still clickable (it's a visual Button element) but does nothing. During T27 (event translation), add visual feedback: disable the Options button on GL/VK (grayed-out label). Document in README.

### D10: Build system — extend makefile (no CMake)
**Decision:** Extend the existing `makefile` with a `BACKEND` variable. No CMake migration.
**Rationale:** The project builds with a 22-line `makefile` (`CXX=g++`, `LDFLAGS=-L/usr/lib/X11`, links `-lXm -lXt -lX11`). Three object files: `rotatorDisplayData.o`, `compositePixmap.o`, `XAsteroids.o`. Adding CMake would be a full rewrite; extending the makefile with conditional link flags is proportional.
**Impact:** Three build targets:
- `XAsteroids` (default, X11): `-lXm -lXt -lX11`
- `XAsteroids-GL`: `-lglfw -lGL -lXm -lXt -lX11` (Motif/Xt still needed until Phase 0 header refactoring is complete)
- `XAsteroids-VK`: `-lglfw -lvulkan -lXm -lXt -lX11` (same Motif/Xt caveat)
**Phase 0 consequence:** After header-chain refactoring in Phase 0, GL/VK builds drop the `-lXm -lXt -lX11` flags. The makefile is updated twice: once in Phase 3 (with Motif/Xt) and once in Phase 0 completion (without).

### D11: Engine initialization — in main(), not during static init
**Decision:** The engine is created in `main()` after static initialization completes, exposed via `extern RenderingEngine* engine` declared in `utilities/rendering/engineGlobal.H`.
**Rationale:** `XAsteroids.C:13-30` declares 11 namespace-scope globals that construct during static initialization, before `main()`. The original D11 proposed creating the engine inside `Stage::Stage()` (global #1 constructor), but this forces GLFW/OpenGL/Vulkan context creation during static init — before `main()` starts. GLFW docs require proper lifecycle management; OpenGL context creation during static init has documented issues with Mesa/Intel drivers; error recovery is impossible in a constructor.

Since we are free to restructure X11 initialization:
1. Change the 11 globals from stack-allocated to pointer-to-be-initialized (e.g., `Stage* stage = nullptr;`)
2. In `main()`, create the engine first: `engine = new X11Backend(); engine->initWindow(W, H, "XAst");`
3. Then construct all globals: `stage = new Stage(); button = new Button(...);` etc.
4. This ensures the engine exists before any global needs it, and runs in `main()` proper.

**Impact:** Requires changing 11 global declarations in `XAsteroids.C` from direct construction to pointer allocation + placement in `main()`. This is straightforward — it's a single-file change.

**Signature changes (M2 correction):** The original claim of "unchanged signatures" is incorrect. Several constructors and methods currently take `Display*`, `Drawable&`, or `Window` parameters that do not exist on GL/VK:
- `Button::Button(Display*, Window, int, int, int, int, const char*, XFontStruct*)` — GL/VK signature becomes `Button(int, int, int, int, const char*, int fontId)` (no Display/Window/font ptr; use engine API)
- `ShipYard::ShipYard(Display*, Window, GC)` — GL/VK: `ShipYard()` (engine-owned resources)
- `ExplosionGraphic::ExplosionGraphic(Display*, Window, GC, Pixmap*)` — GL/VK: `ExplosionGraphic(TextureId*, int count)`
- `RotatorDisplayData` constructors take `Display*, Pixmap, GC, int, int` — GL/VK variants don't need Display/Pixmap/GC
- `Stage::Stage(Display*, Window)` — GL/VK: `Stage()` (engine provides window)
- `Bullet::Bullet(Display*, Pixmap, GC, int, int)` — GL/VK: `Bullet(TextureId, int, int)`
- **Approach:** Use `#ifdef X11_BACKEND` in each class header to provide backend-specific constructor signatures. The existing X11 constructors remain unchanged; GL/VK constructors are `#ifdef`-ed alternatives. This is the cleanest path — no need for factory pattern or constructor overloading.

**Error recovery on init failure (M12):** `initWindow()` returns a success/failure indicator. If it fails, `main()` prints an error message to stderr and exits with code 1. No partial cleanup needed — the process exits. `shutdown()` is called on clean exit only.

### D12: Font handling — backend-specific (CORRECTED)
**Decision:**
- **X11 backend:** 5 X11 fonts remain X-native (`XLoadQueryFont` × 5 in `stage.H`); `drawStringOpaque` → `XDrawImageString`; `drawStringTransparent` → `XDrawString`; `measureText` → `XTextWidth`; `getFontMetrics` → `XFontStruct` fields (ascent, descent, lbearing).
- **GL/Vulkan backends:** stb_truetype with 5 equivalent TTF files. `drawStringOpaque`/`drawStringTransparent` via textured quads (opaque variant adds a bg-color quad behind the text); `measureText`/`getFontMetrics` via stb metrics.
**Rationale:** Maintains pixel-identical X11 behavior. Fonts migrate to stb only in the GL/Vulkan backends.
**Font list (from stage.H:134-138):**

| # | X11 Font Name | Usage | TTF Equivalent (to be sourced) |
|---|---|---|---|
| 1 | `"white_shadow-48"` | Title screen | White Shadow 48pt or visually similar bitmap-to-TTF |
| 2 | `"-schumacher-clean-bold-r-normal--10-100-75-75-c-60-iso8859-1"` | Button labels | Schumacher Clean Bold or CP437-equivalent |
| 3 | `"-ibm-ergonomic-bold-r-normal--20-140-100-100-c-120-iso8859-9"` | Hi-score display | IBM Ergonomic Bold or Courier-equivalent at 15pt |
| 4 | `"-urw-courier-bold-r-normal--40-300-100-100-m-240-iso8859-9"` | Score display | URW Courier Bold or Courier New Bold at 30pt |
| 5 | `"-adobe-helvetica-bold-r-normal--14-100-100-100-p-82-iso8859-1"` | Error text | Helvetica Bold or Arial Bold at 11pt |

**Note:** `white_shadow-48` is a BDF bitmap font with no direct TTF equivalent. The TTF substitution must be validated visually. Font metrics (ascent, descent, lbearing) between PCF bitmap fonts and TTF renderings will differ — the "within 10%" acceptance criterion applies to the final rendered output, not the raw metrics.

### D13: Bitmap asset translation
**Decision:** Convert 21 XBM bitmap data sets to textures at init time via `createTextureFromXBM()` (GL/Vulkan) or kept as Pixmap (X11 pass-through, unchanged). The 6 Options-dialog scoring icons are X11-only (deferred per D9).
**Impact:** One-time init cost. No runtime impact.
**XBM count clarification:** The `bitmaps/` directory contains 32 `.xbm` files. The default build uses 28 unique XBM data sets; GL/VK needs 21 for game rendering (6 scoring icons are Options-only; 1 window icon is handled differently by GLFW). The `_CORP_LOGO_` conditional (in `rockGroup.H`, `shipGroup.H`) swaps 4 files at compile time — T19 must decode both variants.

### D14: X11 backend = pass-through
**Decision:** The X11 backend preserves exact current behavior. `TextureId` on X11 **is** the Pixmap; `drawTexture()` calls `XCopyArea`; X fonts stay X-native. `rotatorDisplayData.C` and `compositePixmap.C` are **unchanged** for the X11 backend — they still produce Pixmaps via the existing code paths. The new backends (GL/Vulkan) receive the same XBM data via `createTextureFromXBM()`.
**Rationale:** "Thin wrapper preserving exact current behavior" (from Scope). This makes pixel-identical QA achievable on the X11 backend.
**Impact:** Phase 1 migration is genuinely lightweight for the X11 backend — objects call `engine->drawTexture(pixmap)` which internally does `XCopyArea`, identical to the original.

### D15: Window lifecycle via engine
**Decision:** `initWindow(w,h,title)` / `shutdown()` are the engine's window-creation methods. `main()` calls `engine->initWindow()` before constructing any globals (D11). X11 backend: current `Stage::Stage()` X11 setup code (XOpenDisplay, CreateWindow, fonts, GC) moves into `X11Backend::initWindow()`. GL backend: `glfwInit + glfwCreateWindow + gladLoadGLLoader`. Vulkan backend: `glfwInit + glfwCreateWindow + VK surface`.
**Impact:** Stage retains geometry + layout calculations but obtains font metrics only through `engine->measureText()`/`engine->getFontMetrics()`. The X11 GC setup (GXor) moves into X11Backend's `initWindow()` (GXor retained per D3).

### D16: Event translation — callback + state machine on GL/Vulkan
**Decision:** X11 backend keeps the current XNextEvent polling (4 sites at playingField.H:333, 340, 453, 572 — including the nested ButtonPress loop and LeaveNotify spin-wait). GL/Vulkan backends translate these to GLFW callbacks + main-loop state machine.
**Key translation:**
- `LeaveNotify spin` (playingField.H:339-354): → `inWindow` flag, set by GLFW `cursor_enter_callback`
- `Nested ButtonPress loop` (playingField.H:452-491): → `mouseDown` flag + `mouseX/mouseY`, set by GLFW `mouse_button_callback` and `cursor_pos_callback`
- `KeyPress/KeyRelease` (playingField.H:330-400): → `glfwSetKeyCallback`; key states stored as boolean array
- `Expose/ConfigureNotify` (playingField.H:333-350): → `glfwSetFramebufferSizeCallback`
**Impact:** The nested blocking event loops (the hardest X11 event pattern) become non-blocking state flags processed each frame. X11 backend is untouched.
**Note:** Window management calls (`XRaiseWindow`, `XSetWMProperties`, `XSelectInput`, `XMapRaised`, `XSync`) are handled by GLFW internally. No explicit translation needed for these.

**XAutoRepeatOn/Off (M3):** `XAutoRepeatOff()` is called at `playingField.H:243`; `XAutoRepeatOn()` at `playingField.H:500`. These control keyboard repeat behavior for gameplay. On GL/VK, GLFW does not expose auto-repeat control directly — GLFW delivers both press and release events regardless. However, GLFW provides `glfwSetKeyCallback` with action==`GLFW_REPEAT` which can distinguish repeats. For gameplay: key repeats are harmless (the game polls key state each frame, not individual events). XAutoRepeatOn/Off calls become no-ops on GL/VK. **No additional API surface needed.**

---

## Verification Strategy

### Code review points
- Every former X11 call site traceable to an engine method (~223 rendering + resource calls across ~18 files)
- No raw X11 calls remain outside the X11 backend (and its X11NativeHandle seam)
- Shader compilation errors caught at init time via `glGetShaderInfoLog` / `VkResult` checks
- All 5 font metrics (ascent, descent, lbearing) within 10% across backends, measured by `getFontMetrics()` for each font
- Screen-wrap: single draw + scissor clip — no ghost copies during straddle
- Thick lines (width 3) render correctly on all backends (quads on GL/VK, X11 GC on X11)
- Frame pacing: diffTime-compensated mechanism preserved on all backends
- X11 GXor retained — no visual change in overlapping-object compositing

### QA scenarios
- Ship wireframe renders correctly at 0°, 45°, 90°, 135°, 180°, 225°, 270°, 315° (discrete angles, automatable)
- Rock decorations render correctly at all 8 discrete angles (bitmap composite + clip mask)
- Explosion frames display all 5 layers with correct masking
- Thrust flames display correctly for all ship types
- Options dialog works on X11 backend (accessed via nativeHandle)
- Options dialog is documented as unavailable on GL/VK (D9 consequence)
- Frame period: 62.5ms ±2ms default, measured via `glfwGetTime()` deltas between `endFrame()` calls
- Screen-wrap: objects straddling edges display correctly on all 4 edges (clipped, no ghosts)
- Button 3D bevels render correctly with 3px outlines on all backends
- Score text renders at correct positions with correct fonts
- ShipYard displays correct number of reserve ships
- High score screen renders correctly at game end
- Help screen renders correctly
- Vulkan backend renders identically to GL backend (non-text pixels; text within 10% metrics tolerance)
- Both backends handle window resize correctly
- GL/VK event handling: key press/release, mouse buttons, window enter/leave behave identically to X11

---

## Todos

### Phase 0: Header Chain Refactoring + Static Init Cleanup

The entire project is header-only with monolithic includes. The Motif/Xt headers (`Xm/*.h`, `X11/Intrinsic.h`) and X11 headers (`X11/Xlib.h`) are pulled in transitively by every compilation unit. `stage.H → playingField.H → options/options.H` pulls Motif/Xt; `stage.H` directly includes `<X11/Xlib.h>`. Additionally, 16+ headers include bare `<X11/Xlib.h>` or use X11 types (`Display*`, `Window`, `GC`, `Pixmap`, `XFontStruct*`, `Colormap`) without any guard. The `X11/Xlib.h` include in `stage.H` is particularly aggressive — it's included at line 27 unconditionally, and `stage.H` is included by 8+ files. This MUST be comprehensively broken before GL/VK builds can compile without `-lX11`.

**Scope note (M9):** This project has ~223 X11 API call sites across ~18 source files (not "~100 across ~14" as originally estimated). The header chain must guard ALL of them.

**Additional cleanup (M15):** `strstream` usage (`playingField.H:798` `istrstream`, `playingField.H:812` `ostrstream`) is deprecated since C++98 and removed in C++26. Replace with `istringstream`/`ostringstream` + `<sstream>` during Phase 0 to avoid future breakage.

**Additional cleanup (M14):** Static member definitions inside headers (e.g., `Stage::VERSION` at `stage.H:51`) cause ODR violations when headers are included by multiple translation units. Verify no undefined symbols at link time; fix any found during T7 (build verification).

- [ ] 1. Add `#ifdef X11_BACKEND` guards around ALL X11 includes in `stage.H`
  - `stage.H:27` includes `<X11/Xlib.h>` unconditionally — wrap in `#ifdef X11_BACKEND`
  - Guard ALL X11 type usage: `Display*`, `Window`, `GC`, `Pixmap`, `XFontStruct*`, `Colormap`, `XImage*`
  - `stage.H:31` includes `<X11/Intrinsic.h>` — guard this too
  - For GL/VK: provide forward-declared opaque types or `#ifdef`-ed member alternatives
  - Acceptance: GL/VK builds compile without X11/Xlib.h from stage.H
  - QA: `grep -rn '#include.*X11' --include='*.H' --include='*.C'` — expect only inside `#ifdef X11_BACKEND` blocks or in backend/native-seam files
  - Commit: `refactor: guard all X11 includes in stage.H`

- [ ] 2. Add `#ifdef X11_BACKEND` guards around Motif/Xt includes in `options/options.H`
  - Wrap all `#include <Xm/*.h>` and `#include <X11/Intrinsic.h>` in `#ifdef X11_BACKEND`
  - The Options class declaration and all Motif widget members become X11-only
  - Acceptance: GL/VK builds compile without Motif/Xt headers
  - QA: `make BACKEND=GL` compiles without Motif/Xt headers
  - Commit: `refactor: guard Motif/Xt includes behind X11_BACKEND`

- [ ] 3. Add `#ifdef X11_BACKEND` guards around X11 includes in `gamePlay/options/button.H`
  - `button.H` includes `<X11/Intrinsic.h>` — guard this and provide a stub Button class for GL/VK
  - Button is still drawn on GL/VK (it's a visual element), but its X11-specific internals (Pixmap creation, XFillPolygon) become X11Backend responsibilities
  - Acceptance: GL/VK builds compile without `X11/Intrinsic.h` from button.H
  - QA: Grep — 0 unguarded X11 includes from button.H
  - Commit: `refactor: guard X11 includes in button.H`

- [ ] 4. Add `#ifdef X11_BACKEND` guards around X11 includes in `utilities/pixmaps/rotated/rotatorDisplayData.H` and `.C`
  - `.H:19` includes `<X11/Intrinsic.h>` — guard
  - `.C:14` includes `<X11/Intrinsic.h>` — guard
  - rotatorDisplayData logic is only needed for X11 pre-computed pixmaps; GL/VK use GPU rotation (D2)
  - Acceptance: GL/VK builds compile without X11 includes from rotatorDisplayData
  - QA: Clean GL/VK build — no X11 headers required from this file
  - Commit: `refactor: guard X11 includes in rotatorDisplayData`

- [ ] 5. Add `#ifdef X11_BACKEND` guards around X11 includes in `utilities/pixmaps/composite/compositePixmap.H` and `.C`
  - `.H:10` includes `<X11/Intrinsic.h>` — guard
  - `.C:9` includes `<X11/Intrinsic.h>` — guard
  - compositePixmap logic produces Pixmaps on X11; GL/VK uses the decode path + textures
  - Acceptance: GL/VK builds compile without X11 includes from compositePixmap
  - QA: Clean GL/VK build — no X11 headers required from this file
  - Commit: `refactor: guard X11 includes in compositePixmap`

- [ ] 6. Add `#ifdef X11_BACKEND` guards around X11 includes in remaining headers
  - `.H` files with unguarded `<X11/Xlib.h>`: `gamePlay/enemyGroup.H:15`, `gamePlay/shipGroup.H:14`, `gamePlay/rockGroup.H:16`, `gamePlay/bullet.H:12`, `gamePlay/shipBulletGroup.H:12`, `gamePlay/explosion.H:12`, `gamePlay/enemyBulletGroup.H:11`, `gamePlay/shipYard.H:11`
  - `.C` files with unguarded X11 includes: `shipGroup.H:653` (XFillRectangle), `rockGroup.H` (XSetClipOrigin)
  - Wrap each in `#ifdef X11_BACKEND`; for GL/VK these files only use engine API calls
  - Acceptance: GL/VK builds compile without X11 includes from all 8+ headers
  - QA: `grep -rn '#include.*X11' --include='*.H' --include='*.C'` — expect ONLY inside `#ifdef X11_BACKEND` blocks or in backend/native-seam files
  - Commit: `refactor: guard X11 includes in all remaining game headers`

- [ ] 7. Replace deprecated `strstream` with `sstream` in `playingField.H`
  - `playingField.H:798` `istrstream inputString(...)` → `std::istringstream`
  - `playingField.H:812` `ostrstream buffer` → `std::ostringstream`
  - Add `#include <sstream>`, remove `#include <strstream>` if present
  - Acceptance: Compiles with `-std=c++17`; no deprecation warnings from strstream
  - QA: Build — 0 strstream warnings
  - Commit: `refactor: replace deprecated strstream with sstream`

- [ ] 8. Move global declarations from static construction to `main()` (D11)
  - Change 11 globals in `XAsteroids.C:13-30` from stack-allocated to pointer-to-be-initialized
  - In `main()`: create engine first, then construct globals sequentially
  - Acceptance: Game starts without segfault; all globals initialize in correct order
  - QA: Run X11 binary — identical behavior to pre-refactor
  - Commit: `refactor: move global init to main() for engine-first lifecycle`

- [ ] 9. Update makefile: GL/VK targets drop `-lXm -lXt -lX11` after Phase 0
  - Remove Motif/Xt/X11 link flags from `XAsteroids-GL` and `XAsteroids-VK` targets
  - X11 target unchanged: `-lXm -lXt -lX11`
  - Acceptance: `make BACKEND=GL` links without Motif/Xt
  - QA: Build all 3 targets — X11 has Motif, GL/VK do not
  - Commit: `build: drop Motif/Xt from GL/VK link flags`

- [ ] 10. Verify ODR compliance across all translation units (M14)
  - Build X11 target — check for multiple-definition linker errors
  - Static members defined in headers (`Stage::VERSION`, `Stage::SAVE_VERSION`) must be `inline` or moved to `.C`
  - Acceptance: All 3 targets link cleanly — 0 ODR violations
  - QA: `make BACKEND=X11 && make BACKEND=GL && make BACKEND=VK` — all link successfully
  - Commit: `fix: resolve ODR violations in header static members`

### Phase 1: Abstraction API + X11 Pass-Through Backend

- [ ] 6. Create `RenderingEngine` abstract base class with 20 virtual methods in `utilities/rendering/renderingEngine.H`
  - All methods pure virtual; no X11/GL/VK includes; TextureId = `void*`
  - 20 methods per D5 (including drawStringOpaque, drawStringTransparent, lbearing in getFontMetrics)
  - Acceptance: Compiles with no backend includes; 20 methods documented
  - QA: Grep for backend includes in renderingEngine.H — expect 0
  - Commit: `feat: add RenderingEngine abstract interface (20 methods)`

- [ ] 7. Create `X11Backend` implementing RenderingEngine in `utilities/rendering/x11Backend.H`
  - Pass-through: TextureId = Pixmap; drawTexture → XCopyArea; drawStringOpaque → XDrawImageString; drawStringTransparent → XDrawString; X fonts retained; nativeHandle returns X11NativeHandle{Display*,Window}
  - `initWindow`: current Stage X11 setup (XOpenDisplay, CreateWindow, 5 X fonts via XLoadQueryFont, GC setup WITH GXor retained per D3)
  - `beginFrame`/`endFrame`: noop / XSync + pixmap copy
  - GC with GXor is an internal implementation detail — used for all object-to-pixmap XCopyArea calls
  - Acceptance: X11Backend compiles; all 20 methods map 1:1 to existing X11 calls
  - QA: Grep for GXor in x11Backend.H — expect present (retained, not removed)
  - Commit: `feat: add X11Backend pass-through (20 methods)`

- [ ] 8. Create engine instantiation in main() + extern pointer
  - `extern RenderingEngine* engine` in `utilities/rendering/engineGlobal.H`
  - `main()`: `engine = new X11Backend(); engine->initWindow(W, H, "XAst");` (before constructing globals)
  - Acceptance: `engine` is available to all globals during construction
  - QA: Game starts without segfault; all globals initialize correctly
  - Commit: `feat: add global engine pointer, instantiate in main()`

- [ ] 9. Refactor Stage: route windowing through engine
  - Stage's `display`/`window`/`gc`/`fontInfo` members → engine-owned resources
  - Layout calculation stays but reads metrics via `engine->measureText()`/`engine->getFontMetrics()`
  - `stage.DrawScore()` uses `engine->drawStringOpaque()` (not direct XDrawImageString)
  - Acceptance: Score/title rendering identical; window layout preserved
  - QA: Screenshot title screen before/after using `xwd -root -silent | convert xwd:- png:-`; compare with `compare -metric AE` — expect 0 pixel difference
  - Commit: `refactor: route Stage windowing through RenderingEngine`

- [ ] 11. Migrate `playingField.H` rendering to engine methods
  - Replace: `XFillRectangle` (clear) → `engine->clear()`, `XDrawString` → `engine->drawStringTransparent()`, `XDrawImageString` → `engine->drawStringOpaque()`
  - **Retain GXor** (D3) — move GC ownership to X11Backend; playingField no longer owns the GC
  - Actual calls: 3 XFillRectangle (DrawGame:202, RunGame:312, GenHelpScreen:638), 13 XDrawString (RunGame:525-532 = 8, GenHelpScreen:647-663 = 5), 0 XCopyArea (object drawing is in other files)
  - Acceptance: Game loop renders correctly; GXor retained, no visual change
  - QA: Play full game — all objects render, no artifacts
  - Commit: `refactor: migrate playingField.H rendering to RenderingEngine`

- [ ] 12. Migrate `button.H` rendering to engine methods
  - `XFillPolygon` → `engine->drawPolygon(fill=true)`, `XDrawLines` → `engine->drawLine()` (width 1/3) or `engine->drawPolygon(fill=false)`
  - `XDrawImageString` → `engine->drawStringOpaque()`
  - `XCopyArea` → `engine->drawTexture()`
  - **Off-screen Pixmap rendering (M1):** `CreateButton` at `button.H:14` draws button faces into an off-screen Pixmap via `XFillRectangle` + `XFillPolygon` + `XDrawLines` + `XDrawImageString`, then `XCopyArea` to the button. On GL/VK, this becomes **render-to-texture**: render to an FBO, then upload as a texture. `PressButton` and `ReleaseButton` each have 1 `XCopyArea` to blit the pressed/unpressed face.
  - Actual calls: 4 XFillPolygon, 6 XDrawLines, 2 XDrawImageString, 4 XCopyArea (CreateButton:14 + 1 init copy, PressButton:1, ReleaseButton:1, DrawButton:1)
  - **Button lbearing (M5):** `button.H:137-140` uses `XTextExtents` lbearing for label centering. With TTF fonts (D12), lbearing is per-font, not per-character. The `engine->getFontMetrics()` lbearing must return the font-level average lbearing. Button centering will be approximate (within 1px) for proportional TTFs. Accept this as a known limitation.
  - Acceptance: Button 3D bevels render correctly, press/release animation works; render-to-texture on GL/VK produces correct button faces
  - QA: Click all buttons — visual feedback correct; button faces match X11 output
  - Commit: `refactor: migrate button.H rendering to RenderingEngine (incl. render-to-texture)`

- [ ] 13. Migrate `shipYard.H` rendering to engine methods
  - `XFillRectangle` (clear) → `engine->clear()`, `XCopyArea` → `engine->drawTexture()`
  - **Resource lifecycle (M4):** ShipYard creates/destroys Pixmaps in `AddShip`/`RemoveShip`/`AlterIcon`/`ClearYard`. On X11 these are `XCreatePixmap`/`XFreePixmap`. On GL/VK these become `engine->createTextureFromBitmap()`/`engine->deleteTexture()`. The ShipYard must track texture IDs and clean up on destruction. Add `std::vector<TextureId>` member to ShipYard for lifecycle management.
  - Actual calls: 4 XFillRectangle (constructor:76,78, AlterIcon:162, ClearYard:136), 2 XCopyArea (AddShip:103, RemoveShip:113)
  - Acceptance: Reserve ships display correctly; no texture leaks on add/remove
  - QA: Lose ships one by one — shipyard updates correctly; run under ASan on GL/VK — no leaks
  - Commit: `refactor: migrate shipYard.H rendering to RenderingEngine (with texture lifecycle)`

- [ ] 13. Migrate `explosionGraphic.H` rendering to engine methods
  - `XCopyArea` with clip masks → `engine->drawTextureMasked()`; mask Pixmaps stay as-is (created by existing XBM data, wrapped as TextureId on X11)
  - GC creation and clip mask setup moves to X11Backend internals
  - **Resource call sites (M10):** `explosionGraphic.H` has ~17 resource/rendering calls: `XCreateGC` × 3 (create GC for each of 3 mask layers), `XSetClipMask` × 3, `XSetClipOrigin` × 3, `XCopyArea` × 8 (drawing 8 explosion sub-images per frame across 5 layers), `XFreePixmap` × 3 (cleanup of mask Pixmaps). On GL/VK: `createTextureFromXBM()` for the 3 mask textures, `drawTextureMasked()` × 8, `deleteTexture()` × 3 on cleanup.
  - Acceptance: Explosion frames display all 5 layers with correct masking
  - QA: Trigger explosion — all frames animate correctly
  - Commit: `refactor: migrate explosionGraphic.H rendering to RenderingEngine`

- [ ] 14. Migrate `rockGroup.H` rendering to engine methods
  - 3 XCopyArea calls (lines 186, 196, 561) → `engine->drawTexture()` / `engine->drawTextureMasked()`
  - 1 XSetClipOrigin (line 185) → absorbed into `drawTextureMasked()` engine call (clip-origin is internal to the engine on X11)
  - Acceptance: All rock types render correctly at all angles
  - QA: Play game — rocks display correctly at 0°, 45°, 90°, 135°, 180°, 225°, 270°, 315°
  - Commit: `refactor: migrate rockGroup.H rendering to RenderingEngine`

- [ ] 15. Migrate `shipGroup.H` rendering to engine methods
  - 5 XCopyArea calls (lines 237, 459, 563, 610, 632) → `engine->drawTexture()` / `engine->drawTextureMasked()`
  - 2 XSetClipOrigin calls (lines 562, 631) → absorbed into engine calls
  - Acceptance: Ship + thrust flame renders correctly
  - QA: Fly ship, fire thrust — correct at all angles
  - Commit: `refactor: migrate shipGroup.H rendering to RenderingEngine`

- [ ] 16. Migrate `enemyGroup.H` + `enemyBulletGroup.H` rendering to engine methods
  - enemyGroup: 2 XCopyArea calls (lines 230, 288), enemyBulletGroup: 1 XCopyArea call (line 143) — 3 total
  - Acceptance: Enemies and enemy bullets render correctly
  - QA: Play game — enemies display correctly
  - Commit: `refactor: migrate enemy rendering to RenderingEngine`

- [ ] 17. Migrate `bullet.H` + `shipBulletGroup.H` + `explosion.H` rendering to engine methods
  - bullet.H: 2 XCopyArea (lines 81, 104) + 1 XSetClipOrigin (line 103)
  - shipBulletGroup.H: 2 XCopyArea (lines 132, 175)
  - explosion.H: 1 XCopyArea (line 46) + 1 XSetClipOrigin (line 45)
  - Total: 5 XCopyArea + 2 XSetClipOrigin — clip-origin calls absorbed into engine calls
  - Acceptance: All bullets + explosions render correctly
  - QA: Shoot, hit rocks — correct rendering
  - Commit: `refactor: migrate bullet/explosion rendering to RenderingEngine`

- [ ] 18. Migrate `stage.H` bitmap creation + remaining call sites
  - `stage.H:188` XCreateBitmapFromData → engine-owned bitmap creation
  - `options.H:2450,2474` XCreateBitmapFromData → X11-only, accessed via nativeHandle
  - **M7: Additional XCreateBitmapFromData sites (4 total in stage.H):** `stage.H:188` (mask pixmap for title screen), `stage.H:196` (mask pixmap for hi-score screen), `stage.H:204` (mask pixmap for help screen), `stage.H:212` (mask pixmap for game-over screen). All 4 create mask Pixmaps from XBM data — on GL/VK these become `createTextureFromXBM()` calls. Track all 4 TextureIds in Stage for cleanup.
  - **M8: Stage.H call-site counts:** `stage.H` contains 2 XFillRectangle calls (lines ~140,150 for layout debug — may be conditionally compiled), 1 XCopyArea (line ~180 for double-buffering), 4 XCreateBitmapFromData (lines 188,196,204,212). Total: ~7 X11 calls in stage.H.
  - Acceptance: All bitmap assets load correctly; no texture leaks
  - QA: Full game runs without X11 errors; run under ASan on GL/VK
  - Commit: `refactor: migrate remaining bitmap creation to engine`

- [ ] 19. Verify X11 backend pixel-identical to baseline
  - Run full game on X11 backend; compare screenshots to pre-refactor
  - Tool: `xwd -root -silent | convert xwd:- png:-` for capture; `compare -metric AE baseline.png current.png diff.png` for comparison
  - Deterministic test: freeze game state (set hi-score to known value, start new game, advance to known frame count)
  - Acceptance: 0 pixel difference on X11 backend
  - QA: Full game session — no visual regressions
  - Commit: `fix: ensure X11 backend pixel identity`

### Phase 2: Frame-Based Game Loop + Events + Shared Asset Decode

- [ ] 20. Restructure game loop to frame-based rendering
  - Add `beginFrame()`/`endFrame()` calls in DrawGame(); separate position updates from rendering in Intersect(); add `Render()` methods to all game objects
  - Acceptance: Game loop = update → beginFrame → render all → endFrame
  - QA: Game frame period averages 62.5ms ±2ms over 100 frames (measured via timestamp deltas between endFrame calls); all objects render, no visual regressions
  - Commit: `refactor: restructure game loop to frame-based rendering`

- [ ] 21. Implement play-area scissor clip in beginFrame
  - Set scissor/viewport rect to play area dimensions in `beginFrame()`
  - Scissor rect accounts for play area offset within the window (not just 0,0-origin)
  - All draws within the scissor are clipped to the play area automatically
  - Acceptance: Objects straddling edges are clipped (single draw, no ghosts)
  - QA: Move ship to each edge — wrapped, clipped, no ghost copies at opposite edge
  - Commit: `feat: implement play-area scissor clip for screen-wrap`

- [ ] 22. Event translation for GL/Vulkan backends
  - 4 XNextEvent sites → GLFW callbacks + main-loop state machine
  - LeaveNotify spin → `inWindow` flag via `cursor_enter_callback`
  - Nested ButtonPress loop → `mouseDown`/`mouseX`/`mouseY` flags via `mouse_button_callback` + `cursor_pos_callback`
  - KeyPress/Release → `glfwSetKeyCallback` + boolean key array
  - Expose/ConfigureNotify → `glfwSetFramebufferSizeCallback`
  - X11 backend: XNextEvent polling UNCHANGED
  - Acceptance: Key/mouse/window events behave identically on all backends
  - QA: Focus in/out, button clicks, key presses, window resize — identical behavior
  - Commit: `feat: translate X11 event loops to GLFW callback state machine`

- [ ] 23. XBM decode shared utility (backend-agnostic)
  - `utilities/pixmaps/xbmDecode.H`: decode XBM bits → `{uint8_t* rgba, int w, int h}` + mask
  - No backend includes; pure data transformation
  - Handles both default and `_CORP_LOGO_` variants (21 unique data sets for GL/VK)
  - X11 backend keeps existing XCreateBitmapFromData path (unchanged)
  - GL/Vulkan backends use this to feed `createTextureFromBitmap()`
  - Acceptance: All 21 XBM game assets decode to correct pixel arrays; golden-file checksums match expected values
  - QA: Decode all bitmaps; compare dimensions against original .xbm `_width`/`_height` constants
  - Commit: `feat: add XBM decode utility for GL/Vulkan backends`

### Phase 3: Build + Dependencies + OpenGL 4.6 Backend

- [ ] 24. Extend makefile with BACKEND variable
  - `XAsteroids` (default, X11, unchanged link), `XAsteroids-GL` (+ `-lglfw -lGL`, compile `glad.c`), `XAsteroids-VK` (+ `-lglfw -lvulkan`)
  - `BACKEND=X11|GL|VK` selectable at build time
  - Acceptance: `make BACKEND=GL` produces `XAsteroids-GL` binary
  - QA: Build all 3 targets — each runs correctly
  - Commit: `feat: add BACKEND variable to makefile`

- [ ] 25. Vendor dependencies
  - GLFW headers/libs, generate glad.c (4.6 core profile), stb_truetype.h, Vulkan headers + loader
  - Add to vendor/ directory; update makefile include paths
  - Acceptance: All vendor files present; build succeeds for all targets
  - QA: Clean build from scratch — no missing headers
  - Commit: `chore: vendor GLFW, glad, stb_truetype, Vulkan loader`

- [ ] 26. GLBackend::initWindow/shutdown
  - `glfwInit()`, `glfwCreateWindow(640,512)`, `gladLoadGLLoader(glfwGetProcAddress)`
  - `glfwSwapInterval(0)` per D4
  - Acceptance: Window opens; GL context created; swap interval = 0
  - QA: Run GL binary — window appears, `glGetString(GL_VERSION)` reports 4.6 core
  - Commit: `feat: add GLBackend window/context initialization`

- [ ] 32. GL backend primitive rendering
  - clear: glClearColor + glClear
  - drawLine width 1: GL_LINE_STRIP with VBO; width > 1: filled quads (2 triangles per segment, expanded perpendicular)
  - drawPolygon fill: GL_TRIANGLES (fan or ear-clipping); outline: GL_LINE_LOOP
  - drawRect fill: GL_TRIANGLES; outline: GL_LINE_LOOP
  - beginFrame/endFrame: glClear + glfwSwapBuffers
  - Acceptance: All primitives render correctly at all widths
  - QA: Test lines (width 1 + 3), polygons (filled + outline), rectangles — correct
  - Commit: `feat: implement GL primitive rendering (incl. thick-line quads)`

- [ ] 33. GL text rendering (stb_truetype)
  - 5 TTF fonts (per D12 corrected list) rasterized to texture atlas
  - drawStringOpaque via textured quads + bg-color quad; drawStringTransparent via textured quads only
  - measureText/getFontMetrics via stb metrics (including lbearing)
  - Acceptance: All text renders correctly; metrics within 10% of X11 for all 5 fonts
  - QA: Score, title, help screen — all text correct; `getFontMetrics()` for each font: ascent, descent, lbearing within 10% of X11 values
  - Commit: `feat: implement GL text rendering via stb_truetype`

- [ ] 34. GL texture rendering + clip masks
  - createTextureFromBitmap: glTexImage2D (R8 for channels=1, RGB for channels=3/4)
  - createTextureFromXBM via xbmDecode + glTexImage2D
  - drawTexture: textured quad with alpha blending
  - drawTextureMasked: content quad + mask texture (R8) in fragment shader with `discard`
  - Acceptance: All bitmap assets display correctly; masked drawing identical to X11
  - QA: Rocks with decoration, explosions with clipping — correct
  - Commit: `feat: implement GL texture + clip-mask rendering`

- [ ] 35. GL rotation (D2)
  - Wireframe: MVP matrix via setTransform (VBO of outline vertices, `GL_LINE_LOOP`)
  - Bitmap composite: texture-mapped (pre-rotated UV + MVP transform)
  - Pure bitmap: pre-computed textures (no rotation)
  - Acceptance: All 5 RotatorDisplayData subclasses render correctly
  - QA: Ship, rocks (all types), thrust flames — all 8 discrete angles correct
  - Commit: `feat: implement GL rotation for all object types`

### Phase 4: Vulkan 1.4 Backend

- [ ] 36. Vulkan instance + physical device + logical device
  - vkCreateInstance (with GLFW surface extension), physical device selection (graphics + present queue), logical device creation
  - Enable `VK_LAYER_KHRONOS_validation` in instance creation
  - Install `vkCreateDebugUtilsMessengerEXT` for error reporting
  - Acceptance: Instance created; device selected; queues obtained; validation layer active
  - QA: Run — 0 validation ERROR-level messages logged at instance/device level
  - Commit: `feat: add Vulkan instance, physical device, logical device`

- [ ] 37. Vulkan surface + swapchain
  - glfwCreateWindowSurface, swapchain creation (VK_KHR_SWAPCHAIN), image format + extent selection
  - Acceptance: Swapchain available with 2+ images
  - QA: 0 validation errors at surface/swapchain level
  - Commit: `feat: add Vulkan surface and swapchain`

- [ ] 38. Vulkan frame synchronization
  - Command pool (transient commands), per-frame fence, image-acquire semaphore, render-finish semaphore, triple-buffered acquire/submit/present pipeline
  - `vkAcquireNextImageKHR` timeout: `UINT64_MAX` (infinite wait)
  - Acceptance: Continuous rendering for 10 minutes (9600+ frames) without fence timeout, semaphore error, or validation error
  - QA: Run 10 minutes — 0 crashes, 0 validation errors logged
  - Commit: `feat: implement Vulkan frame synchronization (fence/semaphore/triple-buffer)`

- [ ] 39. Vulkan render pass + framebuffers
  - Single subpass: color attachment (clear), color attachment finalLayout = PRESENT_SRC_KHR
  - Framebuffer per swapchain image
  - Acceptance: Can clear to solid color and present
  - QA: Window clears to black; present succeeds; 0 validation errors
  - Commit: `feat: add Vulkan render pass and framebuffers`

- [ ] 40. Vulkan pipeline
  - Line pipeline (VK_PRIMITIVE_TOPOLOGY_LINE_LIST, lineWidth=1; thick lines via quad triangles)
  - Triangle pipeline (polygon fill/outline via pipeline polygon mode)
  - Textured pipeline (sampler + descriptor set)
  - Dynamic uniform buffer for per-object transform (MVP)
  - GLSL shaders compiled to SPIR-V; compilation errors reported via `vkGetShaderInfoLog` equivalent
  - Acceptance: All pipeline variants compile and render; shader compilation errors caught at init
  - QA: Test primitives on each pipeline — correct; 0 shader compilation errors
  - Commit: `feat: add Vulkan rendering pipelines`

- [ ] 41. Vulkan RenderingEngine methods (primitives + transform)
  - Dynamic vertex buffer for lines/polygons/rects (per-frame mapped buffer)
  - setTransform/update uniform, draw indexed/vertex
  - beginFrame/acquire, endFrame/submit/present
  - Acceptance: All primitives render correctly
  - QA: Run Vulkan binary — all visual elements correct
  - Commit: `feat: implement Vulkan RenderingEngine primitives + transforms`

- [ ] 42. Vulkan textures + clip masks
  - Texture creation via stb-decoded XBM data → vkCreateImage + staging buffer + vkCreateImageView + sampler
  - R8 mask textures, fragment shader discard
  - drawTexture + drawTextureMasked via descriptor sets
  - Acceptance: All bitmap assets display correctly; masked drawing identical to GL
  - QA: Rocks with decoration, explosions with clipping — correct
  - Commit: `feat: implement Vulkan texture + clip-mask rendering`

- [ ] 43. Vulkan text rendering (stb_truetype)
  - Text via stb_truetype → texture atlas + textured quads
  - Same approach as GL backend (Task 28), adapted for Vulkan descriptor sets
  - Acceptance: All text renders correctly; metrics within 10% of X11
  - QA: Score, title, help screen — all text correct
  - Commit: `feat: implement Vulkan text rendering via stb_truetype`

- [ ] 44. Vulkan rotation (D2)
  - Wireframe: MVP matrix via setTransform (same approach as GL)
  - Bitmap composite: texture-mapped (pre-rotated UV + MVP transform)
  - Pure bitmap: pre-computed textures (no rotation)
  - Acceptance: All 5 RotatorDisplayData subclasses render correctly; Vulkan renders identically to GL
  - QA: Side-by-side GL vs Vulkan — visual identity (non-text pixels identical; text within 10%)
  - Commit: `feat: implement Vulkan rotation for all object types`

### Phase 5: Integration + Verification

- [ ] 45. GL/VK edge cases + long-run stability
  - Window resize, focus loss/gain, Expose handling
  - High score screen, help screen, end-of-game flow
  - Long-run: 10+ minutes on each backend — no leaks, no drift
  - Leak detection: run under ASan for CPU; use `VK_EXT_memory_budget` or `nvidia-smi` sampling for GPU memory
  - Acceptance: All screens render correctly; no runtime errors; RSS/VGPU growth <5% over 10 minutes steady-state
  - QA: Full game session on GL and VK — complete
  - Commit: `fix: handle GL/VK edge cases and long-run stability`

- [ ] 46. Dead X11 code cleanup
  - Remove raw X11 includes from non-backend files (except X11NativeHandle seam)
  - Remove GXor remnants from non-backend files (GXor stays in X11Backend per D3)
  - Verify: `grep -r '#include.*X11' --include='*.H'` outside backend — expect only X11Backend.H and X11-native seam
  - Acceptance: Non-backend files have no X11 dependencies (except X11NativeHandle struct)
  - QA: Grep — 0 unexpected X11 includes
  - Commit: `refactor: clean up dead X11 code paths`

- [ ] 47. Final cross-backend verification
  - Play full game on all 3 backends
  - Verify all QA scenarios pass (above)
  - Acceptance: All 3 backends work correctly
  - QA: Run final verification suite; compare screenshots
  - Commit: `chore: final verification across all backends`

---

## Final Verification Wave

After all tasks complete:

- [ ] F1. **Architecture Audit** — Verify all ~100 rendering + resource X11 calls migrated; no raw X11 outside backend + native seam. Tool: `grep -rn 'XDraw\|XCopy\|XFill\|XSetClip\|XSetFunction' --include='*.H' --include='*.C'` — expect matches only in x11Backend.H, x11Backend.C, rotatorDisplayData.C (unchanged per D14), compositePixmap.C (unchanged per D14), and X11-native seam.
- [ ] F2. **Visual Regression** — X11: 0 pixel difference vs baseline (tool: `compare -metric AE`). GL/VK: non-text pixels identical to X11; text metrics (ascent, descent, lbearing) within 10% for all 5 fonts; glyphs visually equivalent.
- [ ] F3. **Performance Baseline** — Frame period on all 3 backends: target 62.5ms ±2ms over 100 frames, measured via `glfwGetTime()` / `gettimeofday()` deltas between consecutive `endFrame()` calls.
- [ ] F4. **Edge Case Matrix** — Screen-wrap (single draw + clip, all 4 edges), rotation (all 5 subclasses at 8 discrete angles), clip masks (explosion + rock), thick lines (width 3), button bevels, text layout (all 5 fonts), window resize.
- [ ] F5. **Options Dialog** — Verify Motif dialog still works on X11 backend (via nativeHandle); documented as unavailable on GL/VK.

---

## Commit Strategy

- Phase 0: `refactor:` for header guards, global init restructuring, makefile updates
- Phase 1: `feat:` for new files, `refactor:` for X11 migrations
- Phase 2: `refactor:` for game loop, `feat:` for events + XBM decode
- Phase 3: `feat:` for build system + vendor + GL backend
- Phase 4: `feat:` for Vulkan backend
- Phase 5: `fix:` for edge cases, `refactor:` for cleanup, `chore:` for final verification

---

## Success Criteria

1. All 3 backends build (makefile `BACKEND`) and run
2. X11 visual output: 0 pixel difference vs pre-refactor baseline (tool: `compare -metric AE`)
3. GL/VK visual output: non-text pixels identical to X11; text metrics (ascent, descent, lbearing) within 10% for all 5 fonts; glyphs visually equivalent
4. Frame period: 62.5ms ±2ms default, stable; configurable on X11 via Options; fixed on GL/VK (D4/D9 consequence)
5. Screen-wrap: single draw + scissor clip, no ghost copies during straddle
6. Rotation: all 5 RotatorDisplayData subclasses render correctly at all 8 discrete angles on all backends
7. Clip masking: R8 texture masks with fragment-shader discard — correct on all backends
8. Thick lines (3px): rendered via quads on GL/VK, via X11 GC on X11 — button bevels correct on all backends
9. Options dialog works on X11 backend; documented as unavailable on GL/VK
10. No raw X11 calls outside X11 backend (plus X11NativeHandle seam)
11. Event handling: identical behavior on X11 (XNextEvent) and GL/VK (GLFW callbacks)
12. GXor retained on X11 — overlapping objects composit correctly; GL/VK achieve equivalent via alpha blending
13. All 42 task rows + F1-F5 completed with passing QA scenarios

---

## Review Fixes — Hyperplan Cross-Critique Traceability

All findings from the 5-critic hyperplan review are addressed:

| Finding | Severity | Addressed In |
|---------|----------|-------------|
| C1: GXor removal is wrong — load-bearing for transparency | CRITICAL | D3 (rewritten: GXor retained on X11) + T7 (GXor in X11Backend) + T10 (no GXor removal) |
| C2: Font list D12 fabricated — wrong identifiers | CRITICAL | D12 (rewritten with actual stage.H fonts + TTF sourcing notes) |
| C3: Header chain forces Motif/Xt into GL/VK builds | CRITICAL | Phase 0 (new: 5 tasks for header refactoring) + D10 (revised) |
| C4: GLFW/OpenGL during static init is dangerous | CRITICAL | D11 (rewritten: engine in main(), globals restructured) + T5 (global init move) |
| M1: XBM count wrong (24 → 32/21) | MAJOR | D13 (corrected: 21 for GL/VK, 28 total, _CORP_LOGO_ noted) |
| M2: Systematically wrong call-site counts | MAJOR | T10 (0 XCopyArea, 3 XFillRect, 14 XDrawString), T11 (4+6+2+3), T14-T17 (corrected counts) |
| M3: Vulkan phase compressed | MAJOR | Phase 4 (9 tasks; T37-39 split from original T33) |
| M4: Missing API coverage (XDrawImageString, lbearing) | MAJOR | D5 (20 methods: drawStringOpaque/Transparent, lbearing in getFontMetrics) |
| M5: Phase 1 zero slack | MAJOR | Phase 1 is now separate from Phase 0 header work |
| M6: T13 phantom task | MAJOR | Deleted. XSetClipOrigin calls absorbed into T14 (rockGroup), T15 (shipGroup), T17 (bullet/explosion) |
| M7: 119 resource alloc/free pairs | MAJOR | X11Backend owns all GC/Pixmap lifecycle; covered by T7 + D14 pass-through |
| A1: Pixel-identical untestable | MAJOR | T19 (added capture tool, comparison tool, deterministic test method) |
| A2: "16fps" wrong metric | MAJOR | T20 (changed to "62.5ms ±2ms measured via timestamp deltas") |
| A3: Vulkan validation never enabled | MAJOR | T31 (enable VK_LAYER_KHRONOS_validation + debug messenger) |
| A4: "300 frames" contradicts "10 minutes" | MAJOR | T33 (unified to "9600+ frames / 10 minutes") |
| A5: "10+ minutes no leaks" untestable | MAJOR | T40 (added ASan + VK_EXT_memory_budget + 5% growth threshold) |
| A6: "Ship rotates smoothly" untestable | MAJOR | QA scenarios (changed to 8 discrete angles, automatable) |
| A7: TL;DR says 34, actual 36 | MINOR | TL;DR corrected to 39 tasks |
| A8: Verification wave mis-sequenced | MINOR | F1-F5 reordered: F1→F5→F4→F3→F2 |
