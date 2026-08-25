// ============================================================================
// optionsMenu.C — ImGuiOptionsMenu implementation (task 44a, D9).
// The ONLY new ImGui-bearing domain file. Links into the GL leg
// (makefile MENU_OBJECTS); the VK leg links guards-closed without it.
//
// This TU is deliberately DOMAIN-FREE (see the header comment): it includes
// only optionsMenu.H/menuAdapter.H/imgui.h plus std headers. The D4
// frames-per-second path and the preferences I/O live behind OptionsMenuHost
// and are implemented inline in playingField.H against the same statics the
// X11 Motif callbacks mutate.
//
// Fixed pixel geometry (SetNextWindowPos/Size with ImGuiCond_Always) makes
// the layout deterministic for scripted QA: the harness drives this menu
// with absolute mouse coordinates (test/harness/scripts/menu-gl.script).
// ============================================================================

#include"optionsMenu.H"

#include<stdlib.h>
#include<stdio.h>
#include<sys/time.h>
#include<unistd.h>

namespace
 {
  double nowSeconds()
   {timeval now;
    gettimeofday(&now,nullptr);
    return (double)now.tv_sec+(double)now.tv_usec/1E6;
   }
 }

// Ordering contract: main() calls GLBackend::installMenuInputBridge()
// immediately after this constructor — it configures io on the context
// created here, so the context must already exist.
ImGuiOptionsMenu::ImGuiOptionsMenu(RenderingEngine& engine,
                                   OptionsMenuHost& host)
  :engine_(engine),
   host_(host),
   adapter_(),
   open_(false),
   lastTickSeconds_(nowSeconds()),
   canonicalWidth_(0),canonicalHeight_(0),
   framesPerSecond_(16),
   volume_(80.0f),
   mute_(false)
 {canonicalWidth_=host_.canonicalClientWidth();
  canonicalHeight_=host_.canonicalClientHeight();
  framesPerSecond_=host_.currentFramesPerSecond();
  adapter_.init(engine_,canonicalWidth_,canonicalHeight_);
 }

ImGuiOptionsMenu::~ImGuiOptionsMenu()
 {close();
  adapter_.shutdown();
 }

void ImGuiOptionsMenu::open()
 {if (open_||!adapter_.initialized())
    return;
  open_=true;
  lastTickSeconds_=nowSeconds();
 }

void ImGuiOptionsMenu::close()
 {open_=false;
 }

bool ImGuiOptionsMenu::isOpen() const
 {return open_;
 }

void ImGuiOptionsMenu::handleGameEvent(const GameEvent& event)
 {adapter_.feedEvent(event);
 }

void ImGuiOptionsMenu::renderOverlay(RenderingEngine& engine)
 {adapter_.renderDrawData(engine);
 }

void ImGuiOptionsMenu::tick()
 {if (!open_)
   {lastTickSeconds_=nowSeconds();
    return;
   }
  const double now=nowSeconds();
  float deltaTime=(float)(now-lastTickSeconds_);
  lastTickSeconds_=now;
  adapter_.newFrame(deltaTime);

  ImGui::SetNextWindowPos(ImVec2(40.0f,60.0f),ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(430.0f,190.0f),ImGuiCond_Always);
  bool windowOpen=true;
  ImGui::Begin("Options",&windowOpen,ImGuiWindowFlags_NoCollapse);
  if (!windowOpen)
    open_=false;                       // title-bar [x] closes like Close

  int fps=framesPerSecond_;
  if (ImGui::SliderInt("Frames Per Second",&fps,16,72))
   {framesPerSecond_=fps;
    // XmNvalueChangedCallback equivalent: writes through the SAME D4 path
    // the X11 scale widget drives (uSecondsPerFrame = 1E6/fps).
    host_.applyFramesPerSecondPath(1E6/(double)fps);
    if (getenv("XAST_QA_MENU_RECTS"))
      fprintf(stderr,"[menu] fps %d\n",fps);
   }
  float volume=volume_;
  if (ImGui::SliderFloat("Volume",&volume,0.0f,100.0f))
    volume_=volume;
  ImGui::Checkbox("Mute",&mute_);

  if (ImGui::Button("Save"))
    host_.writePreferencesFile();
  ImGui::SameLine();
  if (ImGui::Button("Load"))
   {host_.readPreferencesFile();
    framesPerSecond_=host_.currentFramesPerSecond();
   }
  ImGui::SameLine();
  if (ImGui::Button("Close"))
    open_=false;

  // Env-gated widget-rect dump for QA coordinate authoring (inert by
  // default; printed once after the first laid-out frame).
  static bool rectsDumped=false;
  if (!rectsDumped&&getenv("XAST_QA_MENU_RECTS"))
   {rectsDumped=true;
    ImVec2 wPos=ImGui::GetWindowPos();
    fprintf(stderr,"[menu] window %.0f %.0f %.0f %.0f\n",
            wPos.x,wPos.y,ImGui::GetWindowSize().x,ImGui::GetWindowSize().y);
   }
  ImGui::End();
  adapter_.endFrameUi();               // ImGui::Render()
 }
