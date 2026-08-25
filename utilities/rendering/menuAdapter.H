#ifndef _MENUADAPTER_H
#define _MENUADAPTER_H

// ============================================================================
// menuAdapter.H — engine-typed Dear ImGui bridge (task 44a, D9).
//
// The GL/VK leg of "menus on EVERY backend": drives Dear ImGui through the
// RenderingEngine seam ONLY. Zero glad/vulkan/GLFW includes are allowed in
// this file (grep gate, plan row 44) — everything flows through:
//   createTextureFromRGBA32  — font atlas uploaded ONCE at init (legacy
//                              atlas path: RendererHasTextures stays unset,
//                              so ImGui builds one static RGBA32 atlas);
//   drawTriangles            — draw lists batched as [x y u v r g b]
//                              interleaved triangle soup, WINDOW-space
//                              coords (the SOLE window-space method under
//                              the v6.1 coordinate rule);
//   setScissorRect           — per-ImDrawCmd clipping; each WINDOW-space
//                              clip rect is INVERSE-MAPPED to LOGICAL
//                              client coords first (v6.1: core methods take
//                              logical client coords and the backend
//                              re-applies the present transform);
//   getPresentTransform      — forward-maps logical->window for MousePos
//                              and sizes io.DisplaySize =
//                              canonical*scale + 2*offset (== the full
//                              framebuffer by the centered-letterbox
//                              formula), so ImGui coordinates ARE raw
//                              window coordinates.
//
// Input: io is fed from the SAME pollEvents(GameEvent*) stream the game
// drains (D9 — no second pump). GameEvent x/y are LOGICAL client coords;
// feedEvent() forward-maps them through getPresentTransform before
// AddMousePosEvent. GLFW_REPEAT is dropped at the pollEvents boundary
// (D16) — SAFE because this menu has no text fields (sliders/toggles/
// buttons only), so no repeat-delivery regression can hide here.
//
// Known cosmetic limitation (documented, accepted): the drawTriangles
// vertex contract carries RGB only (7 floats), so ImDrawVert's alpha byte
// cannot reach the GPU vertex attribute. Anti-aliased fringe vertices
// render with effective alpha 1 (hard edges); hit-testing and all widget
// functionality are unaffected.
// ============================================================================

#include <cstdint>
#include <cfloat>
#include <vector>

#include"imgui.h"                 // vendor/dear_imgui v1.92.9b (pinned)

#include"renderingEngine.H"

class ImGuiEngineAdapter
 {public:
    ImGuiEngineAdapter()
      :engine_(nullptr),initialized_(false),
       canonicalWidth_(0),canonicalHeight_(0),
       atlasTex_(0),frameActive_(false)
     {}

    ~ImGuiEngineAdapter()
     {shutdown();
     }

    // canonicalWidth/Height: the D15-formula client size (the same numbers
    // the backend's letterbox math centers on). The adapter is engine-typed
    // and cannot query them through the interface, so the owner supplies
    // them once; DisplaySize = canonical*scale + 2*offset then equals the
    // full framebuffer exactly (centered-offset identity).
    inline bool init(RenderingEngine& engine,
                     int canonicalWidth,int canonicalHeight)
     {engine_=&engine;
      canonicalWidth_=canonicalWidth;
      canonicalHeight_=canonicalHeight;
      IMGUI_CHECKVERSION();
      ImGui::CreateContext();
      ImGuiIO& io=ImGui::GetIO();
      // Legacy renderer profile: NO ImGuiBackendFlags_RendererHasTextures —
      // the font atlas is built once, uploaded once, and never resized.
      unsigned char* pixels=nullptr;
      int width=0,height=0;
      io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
      atlasTex_=engine_->createTextureFromRGBA32(pixels,width,height);
      if (!atlasTex_)
        return false;
      io.Fonts->SetTexID((ImTextureID)(uintptr_t)atlasTex_);
      initialized_=true;
      return true;
     }

    inline bool initialized() const
     {return initialized_;
     }

    inline void shutdown()
     {if (!initialized_)
        return;
      if (frameActive_)
       {ImGui::Render();          // close any half-built frame cleanly
        frameActive_=false;
       }
      if (engine_&&atlasTex_)
        engine_->deleteTexture(atlasTex_);
      atlasTex_=0;
      ImGui::DestroyContext();
      initialized_=false;
     }

    // ---- input side (D9): fed from the RunGame drain ---------------------
    inline void feedEvent(const GameEvent& event)
     {if (!initialized_)
        return;
      float scale=1.0f;
      int offsetX=0,offsetY=0;
      engine_->getPresentTransform(scale,offsetX,offsetY);
      ImGuiIO& io=ImGui::GetIO();
      switch(event.type)
       {case GameEvent::KeyDown:
         case GameEvent::KeyUp:
          {ImGuiKey key=keyFromCharacter(event.character);
           if (key!=ImGuiKey_None)
             io.AddKeyEvent(key,event.type==GameEvent::KeyDown);
           break;
          }
         case GameEvent::MouseDown:
          if (event.key==1)                    // Button1 -> ImGui left
            io.AddMouseButtonEvent(0,true);
          break;
         case GameEvent::MouseUp:
          if (event.key==1)
            io.AddMouseButtonEvent(0,false);
          break;
         case GameEvent::MouseMove:
          // LOGICAL client -> WINDOW space (forward present-transform map).
          io.AddMousePosEvent((float)event.x*scale+offsetX,
                              (float)event.y*scale+offsetY);
          break;
         case GameEvent::CursorEnter:
          break;                               // position rides MouseMove
         case GameEvent::CursorLeave:
          io.AddMousePosEvent(-FLT_MAX,-FLT_MAX);   // ImGui "no mouse"
          break;
        }
     }

    // ---- frame side: tick() calls newFrame() + endFrameUi() --------------
    inline void newFrame(float deltaTime)
     {ImGuiIO& io=ImGui::GetIO();
      float scale=1.0f;
      int offsetX=0,offsetY=0;
      engine_->getPresentTransform(scale,offsetX,offsetY);
      // Centered letterbox identity: fb = canonical*scale + 2*offset.
      io.DisplaySize=ImVec2(canonicalWidth_*scale+2*offsetX,
                            canonicalHeight_*scale+2*offsetY);
      io.DeltaTime=deltaTime>0.0f?deltaTime:(1.0f/60.0f);
      ImGui::NewFrame();
      frameActive_=true;
     }

    // Renders the UI built between newFrame()/endFrameUi(). Call BEFORE the
    // engine endFrame (the overlay lands on top of the game frame, in
    // WINDOW space, on the same swap).
    inline void endFrameUi()
     {if (frameActive_)
       {ImGui::Render();
        frameActive_=false;
       }
     }

    // ---- output side: batch the draw data through the engine -------------
    inline void renderDrawData(RenderingEngine& engine)
     {if (!initialized_)
        return;
      const ImDrawData* drawData=ImGui::GetDrawData();
      if (!drawData||drawData->CmdListsCount==0)
       {engine.setScissorRect(nullptr);       // leave scissor OFF behind us
        return;
       }
      float scale=1.0f;
      int offsetX=0,offsetY=0;
      engine.getPresentTransform(scale,offsetX,offsetY);
      for (int n=0;n<drawData->CmdListsCount;++n)
       {const ImDrawList* cmdList=drawData->CmdLists[n];
        const ImDrawVert* vtxBuffer=cmdList->VtxBuffer.Data;
        const ImDrawIdx* idxBuffer=cmdList->IdxBuffer.Data;
        for (int ci=0;ci<cmdList->CmdBuffer.Size;++ci)
         {const ImDrawCmd& cmd=cmdList->CmdBuffer[ci];
          if (cmd.ElemCount==0)
            continue;
          if (cmd.ClipRect.z<=cmd.ClipRect.x||cmd.ClipRect.w<=cmd.ClipRect.y)
            continue;
          // WINDOW-space clip rect -> LOGICAL client coords (v6.1 inverse
          // map); the backend applies the transform again on the way to the
          // hardware scissor.
          const int rect[4]=
           {(int)((cmd.ClipRect.x-offsetX)/scale),
            (int)((cmd.ClipRect.y-offsetY)/scale),
            (int)((cmd.ClipRect.z-cmd.ClipRect.x)/scale),
            (int)((cmd.ClipRect.w-cmd.ClipRect.y)/scale)};
          engine.setScissorRect(rect);
          // Expand the indexed triangle mesh into the [x y u v r g b] soup
          // drawTriangles consumes (count = VERTEX count, multiple of 3).
          const int numTriangles=cmd.ElemCount/3;
          vertices_.resize((size_t)numTriangles*3*7);
          float* out=vertices_.data();
          for (unsigned int i=0;i<cmd.ElemCount;++i)
           {const ImDrawVert& vertex=
              vtxBuffer[idxBuffer[cmd.IdxOffset+i]+cmd.VtxOffset];
            const std::uint32_t col=vertex.col;   // ImU32 packs ABGR
            float* dst=out+7*(size_t)i;
            dst[0]=vertex.pos.x;                  // WINDOW space
            dst[1]=vertex.pos.y;
            dst[2]=vertex.uv.x;
            dst[3]=vertex.uv.y;
            dst[4]=(float)((col>>0)&0xFFu)/255.0f;// R (low byte)
            dst[5]=(float)((col>>8)&0xFFu)/255.0f;// G
            dst[6]=(float)((col>>16)&0xFFu)/255.0f;// B; alpha byte dropped
           }
          engine.drawTriangles(vertices_.data(),numTriangles*3,
                               (TextureId)cmd.GetTexID());
         }
       }
      engine.setScissorRect(nullptr);            // HUD/world passes own none
     }

 private:
    // Printable-character mapping (the bridge delivers US-layout printable
    // chars in GameEvent::character). No text fields exist in this menu, so
    // character events beyond these names carry no UI meaning.
    static ImGuiKey keyFromCharacter(char character)
     {if (character>='a'&&character<='z')
        return (ImGuiKey)(ImGuiKey_A+(character-'a'));
      if (character>='A'&&character<='Z')
        return (ImGuiKey)(ImGuiKey_A+(character-'A'));
      if (character==' ')
        return ImGuiKey_Space;
      return ImGuiKey_None;
     }

    RenderingEngine* engine_;
    bool initialized_;
    int canonicalWidth_,canonicalHeight_;
    TextureId atlasTex_;
    bool frameActive_;
    std::vector<float> vertices_;
 };

#endif
