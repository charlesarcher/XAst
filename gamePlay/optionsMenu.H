#ifndef _OPTIONSMENU_H
#define _OPTIONSMENU_H

// ============================================================================
// optionsMenu.H/.C — the domain-owned OptionsMenu seam (task 44a, D9).
//
// The seam (fixed contract, plan D9): open()/close()/isOpen()/tick()/
// handleGameEvent(const GameEvent&)/renderOverlay(RenderingEngine&). The
// domain constructs exactly ONE instance per backend selection in main()
// and RunGame/PlayTheGame drive it at the fixed loop points:
//   - every polled GameEvent feeds handleGameEvent (no second pump);
//   - tick() runs once per frame before renderOverlay;
//   - renderOverlay(engine) draws AFTER the letterboxed game frame, in
//     WINDOW space;
//   - opening pauses simulation (the GL mirror of the X11 modal pause);
//     closing re-arms startTime = ResumePlay(...) exactly as X11 does.
//
// ImGuiOptionsMenu is the ONLY new ImGui-bearing domain file (D9/R6): the
// Motif implementation deliberately stays inside options.H under the
// backend guard so the F1 exception zone remains a closed set. Zero
// widget-library symbols may leak into any other domain file (grep gate).
//
// LAYERING (the repo's one-TU-per-link convention): this unit is deliberately
// domain-FREE — it includes only renderingEngine.H/menuAdapter.H/imgui.h —
// so optionsMenu.o links beside XAsteroids.o without re-emitting the
// header-inline domain definitions. Everything that mutates domain state
// (the D4 frames-per-second path, preferences I/O) is invoked through
// OptionsMenuHost, an abstract triple implemented inline in playingField.H
// (the one header whose include point has the whole domain chain complete).
//
// Controls (S1/D9-v6 scope list):
//   - FPS slider writing the SAME uSecondsPerFrame value through the SAME
//     D4 path the X11 Motif widget drives (host.applyFramesPerSecondPath);
//   - volume/mute toggle (session-scoped: the game is silent; there is no
//     sound domain to persist yet);
//   - preferences save/load in the SAME 29-value file format, order, and
//     scientific/precision-16 encoding as options.H WriteOptions/
//     ReadOptions (host.writePreferencesFile/readPreferencesFile).
// ============================================================================

#include"../utilities/rendering/renderingEngine.H"

// Domain operations the menu may invoke, provided by playingField.H (which
// implements them against the same statics options.H's Motif callbacks
// mutate). Kept virtual so this header stays domain-free.
class OptionsMenuHost
 {public:
    virtual ~OptionsMenuHost()
     {}
    // D15-formula client size (io.DisplaySize = canonical*scale + 2*offset).
    virtual int canonicalClientWidth() const = 0;
    virtual int canonicalClientHeight() const = 0;
    // The live slider value: int(1E6/uSecondsPerFrame+.5), the X11 scale's
    // own conversion (options.H XmScaleSetValue sites).
    virtual int currentFramesPerSecond() const = 0;
    // Options::AlterFramesPerSecond verbatim minus Motif (D4).
    virtual void applyFramesPerSecondPath(double newUSecondsPerFrame) = 0;
    // Options::WriteOptions/ReadOptions format (29 values, scientific,
    // precision 16). Read applies if-different exactly like ReadOptions.
    virtual void writePreferencesFile() = 0;
    virtual void readPreferencesFile() = 0;
 };

class OptionsMenu
 {public:
    virtual ~OptionsMenu()
     {}

    virtual void open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual void tick() = 0;
    virtual void handleGameEvent(const GameEvent& event) = 0;
    virtual void renderOverlay(RenderingEngine& engine) = 0;
 };

#if defined(GL_BACKEND) || defined(VK_BACKEND) || defined(METAL_BACKEND)
// The Dear ImGui implementation. Declared here, defined in optionsMenu.C —
// the ImGui-heavy translation unit stays out of XAsteroids.o so the per-leg
// warning baselines stay untouched. Task 44b opens this guard to VK: the
// adapter is engine-typed, so the same class serves both GPU legs.
#include"../utilities/rendering/menuAdapter.H"

class ImGuiOptionsMenu : public OptionsMenu
 {public:
    ImGuiOptionsMenu(RenderingEngine& engine,OptionsMenuHost& host);
    ~ImGuiOptionsMenu() override;

    void open() override;
    void close() override;
    bool isOpen() const override;
    void tick() override;
    void handleGameEvent(const GameEvent& event) override;
    void renderOverlay(RenderingEngine& engine) override;

  private:
    RenderingEngine& engine_;
    OptionsMenuHost& host_;
    ImGuiEngineAdapter adapter_;
    bool open_;
    double lastTickSeconds_;
    int canonicalWidth_,canonicalHeight_;

    int framesPerSecond_;      // slider state (16..72, the X11 scale range)
    float volume_;
    bool mute_;
 };
#endif

#endif
