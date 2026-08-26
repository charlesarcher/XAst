// utilities/rendering/renderingEngine.H — no backend includes allowed in this file
// (Include guard added at task 12: windowSize.H and stage.H both include this
// header, and the pre-guard file redefined its types on the second inclusion.
// Guard-only change — every declaration below is still the task-11 verbatim
// plan sketch.)
#ifndef _RENDERINGENGINE_H
#define _RENDERINGENGINE_H
#include <cstdint>

using TextureId = uint64_t;          // m20: not void*
using RenderTargetId = TextureId;

struct FontMetrics {
    float ascent;          // font-level (X11: XFontStruct fields; GL/VK: stb)
    float descent;
    float lbearing;        // font-level; used by Stage title placement (stage.H:161)
    struct MaxBounds {     // per-font max_bounds — signature-level requirement (D12)
        float ascent;      // Stage layout: comparison at stage.H:155-158
        float descent;     // headerHeight at stage.H:154
        float width;       // 10*max_bounds.width at stage.H:145-146; Button sizing
    } max_bounds;          //   at button.H:173-174 / :222-223
};

struct GameEvent {           // typed, per-event (D16). NO repeat member — repeats are
    enum Type {              // dropped at the pollEvents boundary on both backends.
        KeyDown,             // character = printable char (X11: keysym decode;
        KeyUp,               //   GL/VK: US-layout table); key = backend key id,
        MouseDown,           //   used by the D16 table for non-printables
        MouseUp,             //   ('q'/'n'/'h'/space/title-hover paths)
        MouseMove,           // x,y = LOGICAL client coordinates (S2/D17.4d: each
        CursorEnter,         //   backend inverse-transforms raw window-space pointer
        CursorLeave          //   coords through the letterbox present transform first;
                             //   the D7 play-area offset still does NOT apply)
    };
    Type type;
    int key;
    char character;
    int x, y;
};

struct X11NativeHandle {     // D9 Options seam (opaque so this header stays
    void* display;           // X11-include-free); X11: {Display*, Window};
    unsigned long window;    // GL/VK: undefined/unused by design (documented)
};

class RenderingEngine {
public:
    virtual ~RenderingEngine() = default;

    // (1) Window lifecycle
    virtual bool initWindow(const char* title) = 0;  // two-pass per D15; failure -> main() exit(1)
    virtual void shutdown() = 0;                     // ownership-map order (D15); close cb routes here
    virtual X11NativeHandle nativeHandle() const = 0; // Options seam, D9 (X11-only consumer)

    // (2) Frame lifecycle
    virtual void beginFrame() = 0;   // NO scissor ownership (D8); X11: noop; GL/VK: frame-start bookkeeping
    virtual void endFrame() = 0;     // X11: pixmap->window blit (stage.H:258/:269) + server sync (:296/:329 bound);
                                     // GL/VK: swap/present

    // (3) Seams
    virtual int pollEvents(GameEvent* out, int maxCount) = 0;  // returns count drained;
                                                               // X11: relocated non-blocking per-event
                                                               // handling (D16 cites); GLFW_REPEAT dropped
    virtual void setScissorRect(const int* xywh) = 0;          // {x,y,w,h} in LOGICAL CLIENT coords (D7;
                                                               // v6.1: the backend applies the present
                                                               // transform); nullptr disables; beginFrame does NOT own it
    virtual RenderTargetId createRenderTarget(int w, int h) = 0; // Button off-screen faces (M1 decision:
    virtual void beginRenderTo(RenderTargetId target) = 0;       // render-to-texture — FBO on GL/VK,
    virtual void endRenderTo() = 0;                              // off-screen Pixmap on X11
    virtual void getPresentTransform(float& outScale, int& outOffsetX,
                                     int& outOffsetY) = 0;       // v6.1 seam — current letterbox transform
                                                                 // (uniform scale + centered offsets). Lets the menu
                                                                 // adapter convert logical<->window (ImGuiIO DisplaySize/
                                                                 // MousePos; ImDrawCmd clip rects) with ZERO platform
                                                                 // types (D9). X11 recomputes on ConfigureNotify;
                                                                 // GL/VK on framebuffer-size; VK re-bootstrap too.

    // (4) Primitives
    virtual void clear() = 0;          // SPECIFIED to the backgroundGC: GXcopy, black foreground
                                       // (playingField.H:77; clears at :202/:312/:638). The gxor GC
                                       // (playingField.H:41/:133/:135) is compositing-only and is
                                       // never the clear target (R4-M1).
    virtual void drawLine(float x1, float y1, float x2, float y2,
                          float r, float g, float b, float width) = 0; // width 1 or 3; quads on GL/VK
    virtual void drawPolygon(const float* verts, int count,
                             float r, float g, float b, bool fill) = 0;
    virtual void drawRect(float x, float y, float w, float h,
                          float r, float g, float b, bool fill) = 0;
    // Menu-adapter batch path (D9): interleaved triangle soup [x y u v r g b] * N,
    // count = VERTEX count (multiple of 3), WINDOW-space coords — the SOLE window-space
    // method under the v6.1 coordinate-space rule (every CORE method takes logical client
    // coords and is backend-transformed uniformly).
    virtual void drawTriangles(const float* verts, int count,
                               TextureId tex) = 0;

    // (5) Text
    virtual void drawStringOpaque(const char* text, float x, float y, int fontId,
                                  float r, float g, float b,
                                  float bgR, float bgG, float bgB) = 0; // X11: opaque text draw
    virtual void drawStringTransparent(const char* text, float x, float y, int fontId,
                                       float r, float g, float b) = 0;  // X11: transparent text draw (per-method mapping table)
    virtual float measureText(const char* text, int fontId) = 0;
    virtual void getFontMetrics(int fontId, FontMetrics& out) = 0;      // incl. per-font max_bounds (D12)

    // (6) Textures
    virtual TextureId createTextureFromBitmap(const uint8_t* data, int w, int h, int channels) = 0;
    // Colored stamp (task 47 closure of the last raw bitmap-creation sites):
    // decode a 1-bit XBM plane to an opaque tile — set bits carry the
    // foreground color, clear bits the background. Components are 0..1 floats
    // (16-bit value / 65535). Default impl: CPU-decode to RGBA8 +
    // createTextureFromBitmap(channels=4) — byte-identical to the GL/VK legs'
    // former free-function path. The X11 backend overrides with the native
    // pixel path: allocate-or-fallback for both colors (the exact arithmetic
    // the retired raw sites used) + one colored-stamp server request parented
    // to the window at DefaultDepth (a pixmap's drawable argument contributes
    // only its depth; the yard-parented originals had the same DefaultDepth,
    // so the created resource is identical).
    virtual TextureId createColoredStamp(const unsigned char* bits, int w, int h,
                                         float fgR, float fgG, float fgB,
                                         float bgR, float bgG, float bgB)
     {const int stride=(w+7)/8;
      uint8_t* rgba=new uint8_t[std::size_t(w)*h*4];
      for(int j=0;j<h;++j)
        for(int i=0;i<w;++i)
         {const bool set=(bits[j*stride+i/8]>>(i%8))&1;
          uint8_t* px=&rgba[(std::size_t(j)*w+i)*4];
          px[0]=uint8_t((set?fgR:bgR)*255.0f+0.5f);
          px[1]=uint8_t((set?fgG:bgG)*255.0f+0.5f);
          px[2]=uint8_t((set?fgB:bgB)*255.0f+0.5f);
          px[3]=255;
         }
      TextureId id=createTextureFromBitmap(rgba,w,h,4);
      delete[] rgba;
      return id;
     }
    virtual void drawTexture(TextureId tex, float x, float y, float w, float h, float alpha) = 0;
    virtual void drawTextureMasked(TextureId content, TextureId mask,
                                   float x, float y, float w, float h) = 0;  // R8 discard / native clip GC
    virtual void deleteTexture(TextureId tex) = 0;
    // Menu-adapter upload (D9): Dear ImGui font atlas / UI textures, RGBA8888.
    virtual TextureId createTextureFromRGBA32(const uint8_t* pixels, int w, int h) = 0;
    // createTextureFromXBM: ABSENT — XBM -> Phase-2 decode utility -> createTextureFromBitmap (D13)

    // (7) Transform
    virtual void setTransform(float tx, float ty, float angleRadians) = 0;  // GL/VK-only semantics (m19):
    virtual void resetTransform() = 0;                                      // X11 implements as documented no-op —
                                                                            // X11 rotation is the pre-computed-pixmap
                                                                            // mechanism (D14); there is no X11 stack
};

#endif
