#include<stdlib.h>
#include<new>
#include<iostream>
#include<stdio.h>
#include<sys/time.h>
#include<unistd.h>
#ifdef X11_BACKEND
#include"utilities/rendering/x11Backend.H"
#elif defined(GL_BACKEND)
#include"utilities/rendering/glBackend.H"
#include"utilities/pixmaps/xbmDecode.H"
#include"gamePlay/optionsMenu.H"
#endif
#include"gamePlay/stage.H"
#include"gamePlay/score.H"
#include"gamePlay/shipYard.H"
#include"objects/explosions/explosionGraphic.H"
#include"objects/ships/shipGroup.H"
#include"objects/ships/shipBulletGroup.H"
#include"objects/rocks/rockGroup.H"
#include"objects/enemies/enemyGroup.H"
#include"objects/enemies/enemyBulletGroup.H"
#include"gamePlay/playingField.H"
Stage* stage = nullptr;
Button* button = nullptr;
Score* score = nullptr;
ExplosionGraphic* explosionGraphic = nullptr;
ShipGroup* shipGroup = nullptr;
ShipYard* shipYard = nullptr;
ShipBulletGroup* shipBulletGroup = nullptr;
RockGroup* rockGroup = nullptr;
EnemyGroup* enemyGroup = nullptr;
EnemyBulletGroup* enemyBulletGroup = nullptr;
PlayingField* playingField = nullptr;

#ifdef GL_BACKEND
// Task-35 input bridge: glBackend's typed-event seam (D16) is not wired yet,
// so the harness's XTest synthetic keys are captured here (GLFW receives
// them through X11) and translated into the playingField.H queue that the
// GL game loop drains. US-layout mapping, press/release only — GLFW repeat
// events are dropped exactly like the X11 boundary drops repeats.
static void glKeyTrampoline(GLFWwindow*,int key,int,int action,int)
 {if (action!=GLFW_PRESS&&action!=GLFW_RELEASE)
    return;
  char character=0;
  switch(key)
   {case GLFW_KEY_S: character='s'; break;
    case GLFW_KEY_Q: character='q'; break;
    case GLFW_KEY_H: character='h'; break;
    case GLFW_KEY_E: character='e'; break;
    case GLFW_KEY_R: character='r'; break;
    case GLFW_KEY_O: character='o'; break;
    case GLFW_KEY_P: character='p'; break;
    case GLFW_KEY_N: character='n'; break;
    case GLFW_KEY_SPACE: character=' '; break;
   }
  if (character)
    glEnqueueKeyEvent(action==GLFW_PRESS,character);
 }
#endif

int main (int argc, char *argv[])
 {
  #ifdef X11_BACKEND
  // main() owns the engine lifecycle (D11): construct the concrete backend,
  // two-pass initWindow (exit(1) on failure — M12), then inject the engine
  // into every global through its single RenderingEngine& constructor.
  X11Backend engine;
  engine.setCanonicalLayout(PlayingField::playArea.Width(),
                            PlayingField::playArea.Height(),
                            ShipGroup::maxIconHeight);
  if (!engine.initWindow("Asteroids"))
   {fprintf(stderr,"XAsteroids: initialization failed.\n");
    return 1;
   }
  stage = new Stage(engine);
  button = new Button(engine,
                      "Options",
                      stage->buttonX,stage->buttonY);
  score = new Score;
  explosionGraphic = new ExplosionGraphic(engine);
  // The ::operator new + placement-new staging for the three groups is the
  // task-6 static-init mechanism and stays: Hyper's ctor dereferences
  // ShipGroup::ship mid-construction, and the bullet groups take intra-object
  // addresses in their init lists — the pointers must be valid pre-ctor
  // exactly as before. Only the ctor calls gain the engine argument.
  shipGroup = (ShipGroup*)::operator new(sizeof(ShipGroup));
  ShipGroup::ship=&shipGroup->starDestroyer;
  ShipGroup::thrust=&shipGroup->starDestroyerThrust;
  new (shipGroup) ShipGroup(engine);
  shipYard = new ShipYard(engine,
                          shipGroup->ship->icon,
                          shipGroup->ship->iconWidth,
                          shipGroup->ship->iconHeight,
                          shipGroup->ship->iconColor.red,
                          shipGroup->ship->iconColor.green,
                          shipGroup->ship->iconColor.blue,
                          stage->shipYardWidth,stage->shipYardHeight);
  shipBulletGroup = (ShipBulletGroup*)::operator new(sizeof(ShipBulletGroup));
  new (shipBulletGroup) ShipBulletGroup(engine);
  rockGroup = new RockGroup(engine);
  enemyGroup = new EnemyGroup(engine);
  enemyBulletGroup = (EnemyBulletGroup*)::operator new(sizeof(EnemyBulletGroup));
  new (enemyBulletGroup) EnemyBulletGroup(engine);
  playingField = new PlayingField(engine);

  cout<<"Your highest score this game was "<<playingField->PlayTheGame(argc>1 ? atoi(argv[1])
                                                                                    : 1,
                                                                             argc, argv)<<'.'<<endl;

  delete playingField;
  delete enemyBulletGroup;
  delete enemyGroup;
  delete rockGroup;
  delete shipBulletGroup;
  delete shipYard;
  delete shipGroup;
  delete explosionGraphic;
  delete score;
  delete button;
  delete stage;
  engine.shutdown();
  return 0;
  #endif
  #ifdef GL_BACKEND
  // Task 35: engine init as at task 31. With XAST_GL_SMOKE set the old
  // paced smoke loop below still runs (task-32/34 acceptance scenes);
  // otherwise the REAL game runs — all 11 globals constructed exactly like
  // the X11 branch (same order; ctor arguments identical modulo the guarded
  // X11-only ones) and playingField->PlayTheGame drives the full loop.
  GLBackend engine;
  engine.setCanonicalLayout(PlayingField::playArea.Width(),
                            PlayingField::playArea.Height(),
                            ShipGroup::maxIconHeight);
  if (!engine.initWindow("Asteroids"))
   {fprintf(stderr,"XAsteroids: initialization failed.\n");
    return 1;
   }
  const char* smoke=getenv("XAST_GL_SMOKE");  // task-32 acceptance scenes
  if (smoke&&smoke[0])
   {const long uSecondsPerFrame=62500;          // D4 default (16 fps)
    timeval start,now;
    gettimeofday(&start,nullptr);
    const char* frameLog=getenv("XAST_FRAME_LOG");
    const bool smokeOn=true;
    for (int frame=0;frame<160;++frame)
     {engine.beginFrame();
      engine.clear();                       // D8 flow: clear every frame
      if (smokeOn)
       {if (!strcmp(smoke,"thick"))
          engine.drawLine(100.0f,100.0f,200.0f,100.0f,1,1,1,3);
        else if (!strcmp(smoke,"scissor"))
         {const int rect[4]={50,50,100,100};
          engine.setScissorRect(rect);
          engine.drawRect(0.0f,0.0f,300.0f,300.0f,1,1,1,true);
          engine.setScissorRect(nullptr);
         }
        else if (!strcmp(smoke,"rot90"))
         {// A square rotated 90° about its center is pixel-identical to the
          // unrotated square — the identity assertion for the MVP path.
          engine.setTransform(200.0f,150.0f,1.5707963267948966f);
          engine.drawRect(-25.0f,-25.0f,50.0f,50.0f,1,1,1,true);
          engine.resetTransform();
         }
        else if (!strcmp(smoke,"text"))
         {engine.drawStringTransparent("Asteroids",50.0f,100.0f,
                                       EngineFont_Title,1,1,1);
          engine.drawStringOpaque("Options",400.0f,100.0f,
                                  EngineFont_Button,0,0,0,1,1,0);
          engine.drawStringTransparent("HIGH SCORE",60.0f,300.0f,
                                       EngineFont_HiScore,1,1,1);
          engine.drawStringTransparent("0123456789",60.0f,380.0f,
                                       EngineFont_Score,1,1,1);
         }
        else if (!strcmp(smoke,"tex"))
         {// Task-34 acceptance: real XBM -> createTextureFromBitmap ->
          // drawTexture; R8 mask discard; FBO round-trip; menu triangles.
          DecodedXBM dec=decodeXBM(ENEMYDecor_bits,
                                   ENEMYDecor_width,ENEMYDecor_height);
          TextureId content=engine.createTextureFromBitmap(
              dec.rgba8.data(),dec.w,dec.h,4);
          engine.drawTexture(content,60.0f,80.0f,
                             (float)dec.w,(float)dec.h,1.0f);
          // half-plane mask: left half on, right half off
          const int mw=32,mh=32;
          static uint8_t mask[mw*mh];
          static uint8_t solid[mw*mh*4];
          for (int yy=0;yy<mh;++yy)
           {for (int xx=0;xx<mw;++xx)
             {mask[yy*mw+xx]=xx<16?255:0;
              solid[(yy*mw+xx)*4+0]=255;
              solid[(yy*mw+xx)*4+1]=255;
              solid[(yy*mw+xx)*4+2]=255;
              solid[(yy*mw+xx)*4+3]=255;
             }
           }
          TextureId m=engine.createTextureFromBitmap(mask,mw,mh,1);
          engine.drawTextureMasked(engine.createTextureFromBitmap(solid,mw,mh,4),
                                   m,200.0f,80.0f,(float)mw,(float)mh);
          // FBO round-trip: red square rendered off-screen, blitted to window
          RenderTargetId rt=engine.createRenderTarget(64,64);
          engine.beginRenderTo(rt);
          engine.clear();
          engine.drawRect(16.0f,16.0f,32.0f,32.0f,1,0,0,true);
          engine.endRenderTo();
          engine.drawTexture(rt,300.0f,80.0f,64.0f,64.0f,1.0f);
          // menu pair: RGBA32 texture + window-space triangles
          uint8_t blue[2*2*4]={0,0,255,255, 0,0,255,255, 0,0,255,255, 0,0,255,255};
          TextureId bt=engine.createTextureFromRGBA32(blue,2,2);
          const float tris[3*7*2]={
            // quad at window px (420..484, 80..144) as two triangles
            420.0f, 80.0f, 0.0f,0.0f, 1,1,1,
            484.0f, 80.0f, 1.0f,0.0f, 1,1,1,
            484.0f,144.0f, 1.0f,1.0f, 1,1,1,
            420.0f, 80.0f, 0.0f,0.0f, 1,1,1,
            484.0f,144.0f, 1.0f,1.0f, 1,1,1,
            420.0f,144.0f, 0.0f,1.0f, 1,1,1};
          engine.drawTriangles(tris,6,bt);
         }
        else // "scene": determinism golden (fixed composition)
         {engine.drawLine(80.0f,80.0f,300.0f,80.0f,1,1,1,1);
          engine.drawLine(80.0f,120.0f,300.0f,120.0f,1,1,1,3);
          const float tri[6]={200.0f,300.0f,320.0f,300.0f,260.0f,420.0f};
          engine.drawPolygon(tri,3,1,0,0,true);
          engine.drawRect(60.0f,350.0f,90.0f,60.0f,0,1,0,false);
          const int rect[4]={400,60,200,200};
          engine.setScissorRect(rect);
          engine.drawRect(380.0f,40.0f,240.0f,240.0f,0,0,1,true);
          engine.setScissorRect(nullptr);
          engine.setTransform(500.0f,400.0f,0.7853981633974483f);
          engine.drawRect(-30.0f,-5.0f,60.0f,10.0f,1,1,0,true);
          engine.resetTransform();
         }
       }
      engine.endFrame();
      gettimeofday(&now,nullptr);
      long elapsed=(now.tv_sec-start.tv_sec)*1000000L+now.tv_usec-start.tv_usec;
      if (frameLog)
        fprintf(stderr,"[frame] %d %ld\n",frame,elapsed);
      if (elapsed<(frame+1)*uSecondsPerFrame)
        usleep((useconds_t)((frame+1)*uSecondsPerFrame-elapsed));
     }
    engine.shutdown();
    return 0;
   }
  // ---- the real game on GL (task 35) ----
  glfwSetKeyCallback(engine.window,glKeyTrampoline);
  stage = new Stage(engine);
  stage->display=nullptr;                    // deterministic shim values: the
  stage->window=0;                           // GL rotator-data path ignores them
  button = new Button(engine,
                      "Options",
                      stage->buttonX,stage->buttonY);
  score = new Score;
  explosionGraphic = new ExplosionGraphic(engine);
  shipGroup = (ShipGroup*)::operator new(sizeof(ShipGroup));
  ShipGroup::ship=&shipGroup->starDestroyer;
  ShipGroup::thrust=&shipGroup->starDestroyerThrust;
  new (shipGroup) ShipGroup(engine);
  shipYard = new ShipYard(engine,
                          shipGroup->ship->icon,
                          shipGroup->ship->iconWidth,
                          shipGroup->ship->iconHeight,
                          StarDestroyer::glColor.red,
                          StarDestroyer::glColor.green,
                          StarDestroyer::glColor.blue,
                          stage->shipYardWidth,stage->shipYardHeight);
  shipBulletGroup = (ShipBulletGroup*)::operator new(sizeof(ShipBulletGroup));
  new (shipBulletGroup) ShipBulletGroup(engine);
  rockGroup = new RockGroup(engine);
  enemyGroup = new EnemyGroup(engine);
  enemyBulletGroup = (EnemyBulletGroup*)::operator new(sizeof(EnemyBulletGroup));
  new (enemyBulletGroup) EnemyBulletGroup(engine);
  playingField = new PlayingField(engine);

  // Task 44a (D9): main() constructs the backend-appropriate OptionsMenu
  // (task-13 pattern) and hands it to PlayTheGame; RunGame feeds it every
  // polled GameEvent, ticks it once per frame, and renders its overlay after
  // the letterboxed game frame. Opening the menu pauses the simulation;
  // closing re-arms startTime = ResumePlay(...) like the X11 modal pause.
  ImGuiOptionsMenu* menu=new ImGuiOptionsMenu(engine,
                                             playingFieldOptionsMenuHost());
  engine.installMenuInputBridge();

  // Initial present: the ctor-drawn help screen reaches the window here
  // (the guarded branch's stage present legs own this step on X11).
  engine.beginFrame();
  engine.endFrame();

  cout<<"Your highest score this game was "<<playingField->PlayTheGame(argc>1 ? atoi(argv[1])
                                                                                    : 1,
                                                                           argc, argv,
                                                                           *menu)<<'.'<<endl;

  delete menu;
  delete playingField;
  delete enemyBulletGroup;
  delete enemyGroup;
  delete rockGroup;
  delete shipBulletGroup;
  delete shipYard;
  delete shipGroup;
  delete explosionGraphic;
  delete score;
  delete button;
  delete stage;
  engine.shutdown();
  return 0;
  #endif
  // GL/VK stub (task 13, M5-M1): no GPU backend exists until tasks 31/37 and
  // the GL/VK link rules until 29/37; the objects target only proves this TU
  // compiles guards-closed. The stub loop arrives with the event unification.
  return 0;
 }
