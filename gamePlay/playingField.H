#include"stage.H"
#include"options/options.H"
#include"../objects/ships/shipBulletGroup.H"
#include"../objects/ships/shipGroup.H"
#include"../objects/enemies/enemyBulletGroup.H"
#include"../objects/enemies/enemyGroup.H"
#include"../objects/rocks/rockGroup.H"

#ifndef _PLAYINGFIELD_H
#define _PLAYINGFIELD_H

#include<stdlib.h>
#include<stdio.h>
#include<iostream>
#include<sstream>
#include<iomanip>
#include<string.h>
#include<sys/time.h>
#include<unistd.h>
#include<fcntl.h>
#ifdef X11_BACKEND
#include<X11/Xlib.h>
#include<X11/Xutil.h>
#include<unistd.h>
#include"../utilities/rendering/x11Backend.H"
#endif
#if !defined(X11_BACKEND) && defined(GL_BACKEND)
#include<unistd.h>
#include"../utilities/rendering/glBackend.H"
#endif
#if !defined(X11_BACKEND) && defined(VK_BACKEND)
#include<unistd.h>
#include"../utilities/rendering/vkBackend.H"
#endif
#include"../utilities/rendering/renderingEngine.H"
#include"../utilities/vector2d.H"
#include"../utilities/box.H"
#include"../utilities/linkedArray.H"
#include"../objects/movableObject.H"
#include"../utilities/intersection2d.H"

#ifdef X11_BACKEND
class OptionsMenu;                       // unused on this leg (Motif dialog)
#else
#include"optionsMenu.H"                  // D9 seam: complete type needed for
#endif                                   // the virtual menu calls below

class PlayingField
 {friend class Options;

  public:
    typedef int Boolean;
    enum {off, on};

    // Transition member (T19/T21 pattern): the engine reference every
    // migrated site draws through; DELETED at task 47 with the other
    // transition aliases.
    RenderingEngine& engine;

    PlayingField(RenderingEngine& engine);
    ~PlayingField();
#ifdef X11_BACKEND
    int PlayTheGame(const int level, int argc, char** argv);
#else
    // Task 44a (D9): the GL/VK loop takes the backend-constructed menu.
    int PlayTheGame(const int level, int argc, char** argv,
                    OptionsMenu& menu);
#endif
    PlayingField& AddShip();
    PlayingField& RemoveShip();
    void AlterExtraShipScore(const int newExtraShipScore);
    void AlterUSecondsPerFrame(const int newUSecondsPerFrame);

    Boolean lastHitFromAlly;
    int lastHitScore,
        framesSinceLastBullet,
        framesSinceEnemy,
        level;

    LinkedArray<MovableObject*> nonPermeableList;
    LinkedArray<MovableObject*> selfPermeableList;
    LinkedArray<MovableObject*> permeableList;

    static int uSecondsPerFrame,
               initialShips,
               extraShipScore;

    static double maxLinearVelocity,
                  universalGravitationalConst;

    static Boolean enemiesExist,
                   relativisticMass;

    static const Box playArea;

    // ---- D8 frame-draw deferral (task 24) ----------------------------------
    // World draws fire mid-physics today (inside Intersect→MissScript).
    // Objects removed later in the same pass WERE drawn this frame, and
    // collision response can mutate an object AFTER its draw fired — an
    // end-of-frame "draw all survivors" sweep would change membership,
    // order, AND state. Instead every draw site snapshots its exact
    // arguments into this frame-local ordered list at the old draw point;
    // the render pass (RenderFrameDraws) replays the records verbatim onto
    // the freshly cleared canvas. Identical membership, order, and state
    // by construction; the float arguments are captured exactly as the
    // immediate call received them (same double→float conversions at the
    // same statement positions).
    struct FrameDrawRecord
      {TextureId tex;     // content texture
       TextureId mask;    // 0 → plain texture leg
       float x,y,w,h;
#ifndef X11_BACKEND
       // Task-35 GL legs: transformed texture draws (rock composite) and
       // wireframe outline draws (ship) replay their MVP under the record.
       // Zero transform + !outline = the m13 fast path (no setTransform).
       float tx,ty,angle;
       bool outline;
       static const int maxOutlineVecs=40;
       float oVerts[2*maxOutlineVecs];
       int oCount;
       float oR,oG,oB;
#endif
      };
    // Capacity bound: worst case per frame ≈ 176 rock misses + 16 spawn
    // placements + ship/thrust/hyper/enemy sprites + ≤10 enemy-bullet and
    // ≤16 ship-bullet aims/misses/explosion frames + 2 big explosions
    // < 300; 1024 is >3x headroom. Overflow aborts loudly rather than
    // silently dropping a draw (a dropped record would break identity).
    static const int maxFrameDraws=1024;
    inline void DeferDraw(TextureId tex,float x,float y,float w,float h)
     {if (numFrameDraws>=maxFrameDraws)
       {fprintf(stderr,"frame draw list overflow\n");
        abort();
        }
      FrameDrawRecord& record=frameDraws[numFrameDraws++];
      record.tex=tex;record.mask=0;
      record.x=x;record.y=y;record.w=w;record.h=h;
      #ifndef X11_BACKEND
      record.tx=record.ty=record.angle=0.0f;
      record.outline=false;
      record.oCount=0;
      #endif
     }
    inline void DeferDrawMasked(TextureId tex,TextureId mask,
                                float x,float y,float w,float h)
     {if (numFrameDraws>=maxFrameDraws)
       {fprintf(stderr,"frame draw list overflow\n");
        abort();
        }
      FrameDrawRecord& record=frameDraws[numFrameDraws++];
      record.tex=tex;record.mask=mask;
      record.x=x;record.y=y;record.w=w;record.h=h;
      #ifndef X11_BACKEND
      record.tx=record.ty=record.angle=0.0f;
      record.outline=false;
      record.oCount=0;
      #endif
     }

#ifndef X11_BACKEND
    // Task-35 D2 legs: a texture drawn under an object-local MVP (rock
    // decor composite), and a wireframe outline replayed as a LINE_LOOP.
    inline void DeferDrawTransformed(float tx,float ty,float angle,
                                     TextureId tex,TextureId mask,
                                     float x,float y,float w,float h)
     {if (numFrameDraws>=maxFrameDraws)
       {fprintf(stderr,"frame draw list overflow\n");
        abort();
        }
      FrameDrawRecord& record=frameDraws[numFrameDraws++];
      record.tex=tex;record.mask=mask;
      record.x=x;record.y=y;record.w=w;record.h=h;
      record.tx=tx;record.ty=ty;record.angle=angle;
      record.outline=false;
      record.oCount=0;
     }
    inline void DeferDrawOutline(float tx,float ty,float angle,
                                 const Vector2d* verts,int count,
                                 float r,float g,float b)
     {if (numFrameDraws>=maxFrameDraws)
       {fprintf(stderr,"frame draw list overflow\n");
        abort();
        }
      FrameDrawRecord& record=frameDraws[numFrameDraws++];
      record.tex=0;record.mask=0;
      record.x=record.y=record.w=record.h=0.0f;
      record.tx=tx;record.ty=ty;record.angle=angle;
      record.outline=true;
      record.oCount=count<FrameDrawRecord::maxOutlineVecs
                      ? count
                      : FrameDrawRecord::maxOutlineVecs;
      for (int i=0;i<record.oCount;++i)
       {record.oVerts[2*i]=(float)verts[i].x;
        record.oVerts[2*i+1]=(float)verts[i].y;
       }
      record.oR=r;record.oG=g;record.oB=b;
     }
#endif

    // ---- Task-36 QA instrumentation (env-gated; inert by default) ----------
    // Cross-leg identity-gate plumbing; every entry point no-ops unless its
    // env var is set, so Q13 byte-identity runs never execute a changed
    // line. Contract (consumed by test/harness/harness.C --handshake frame
    // and qa/gen-masks.py):
    //   XAST_FRAME_COUNTER_FILE  rewritten once per completed gameplay frame
    //     ("N\n") — script boundaries become exact simulation-step indices
    //     on EVERY backend (drift-proof vs visibility-ticked sampling).
    //   XAST_STATE_HASH_FILE  per-frame object-state hash = trajectory-
    //     alignment PROOF (same seed+script => identical across legs/runs).
    //   XAST_DRAWDUMP_FILE  deferred-draw records + bitmap/transformed/
    //     outline class = Tier-2 mask source (NEVER pixel diffs).
    //   XAST_TEXTDUMP_FILE  help/game-over text-block rects = text mask.
    // One index per PresentFrame on both RunGame variants (1 = pre-loop
    // present). Publish lands AFTER the frame is observable: GL after
    // endFrame's swap, X11 after flushing the queued present copies — the
    // flush itself MUST stay gated or it perturbs the Q13-sampled visible-
    // frame sequence (T14 hazard class). Static screens never publish.
    inline static const char* xastQaPath(const char* env)
     {const char* v=getenv(env);
      return (v&&*v)?v:(const char*)0;
     }
    inline static long xastQaFrameNo=0;

    inline void xastQaRecordFrameState()
     {const bool wantDraws=xastQaPath("XAST_DRAWDUMP_FILE")!=0,
                 wantHash=xastQaPath("XAST_STATE_HASH_FILE")!=0;
      if (!wantDraws&&!wantHash)
        return;
      ++xastQaFrameNo;
      if (wantHash)
       {unsigned long h=1469598103934665603UL;
        auto mixU=[&h](unsigned long long v)
          {h^=v;
           h*=1099511628211UL;
          };
        auto mixI=[&mixU](long i)
          {mixU((unsigned long long)i);
          };
        auto mixD=[&mixU](double d)
          {unsigned long long u=0;
           memcpy(&u,&d,sizeof u);
           mixU(u);
          };
        auto mixObj=[&mixD,&mixU](MovableObject* o)
          {const Box& b=o->CurrentBox();
           mixD(b.Center().x);
           mixD(b.Center().y);
           mixD(b.Width());
           mixD(b.Height());
           const Vector2d& vel=o->ObjectLiner().Velocity();
           mixD(vel.x);
           mixD(vel.y);
           // Only these families override ObjectRotator(); every other
           // MovableObject leaf (Explosion/BulletExplosion/Hyper) inherits
           // the aborting base. A fixed sentinel keeps the hash stream
           // aligned either way; classification is identical on both legs,
           // which is all the equality proof needs.
           const bool hasRotator=
            dynamic_cast<Rock* const>(o)!=nullptr||
            dynamic_cast<Enemy* const>(o)!=nullptr||
            dynamic_cast<Bullet* const>(o)!=nullptr||
            dynamic_cast<Ship* const>(o)!=nullptr||
            dynamic_cast<Thrust* const>(o)!=nullptr;
           if (hasRotator)
            {Rotator& rot=o->ObjectRotator();
             mixD(rot.Angle());
             mixD((double)rot.AngularVelocity());
             mixD(rot.Radius());
            }
           else
             mixU(0x4570C510ULL);
          };
        mixI(score->CurrentScore());
        mixI(shipYard->NumShips());
        mixI(framesThisLevel);
        mixI(framesSinceLastBullet);
        mixI(framesSinceEnemy);
        LinkedArray<MovableObject*>* lists[3]=
          {&nonPermeableList,&selfPermeableList,&permeableList};
        for (int l=0;l<3;++l)
         {LinkedArray<MovableObject*>& L=*lists[l];
          mixI(L.GetNumItems());
          L.SetCurrentAtHead();
          for (int i=0;i<L.GetNumItems();++i)
           {mixObj(L.GetData());
            L.Next();
           }
         }
        xastQaPendingHash=h;
       }
      if (wantDraws)
       {FILE* f=fopen(xastQaPath("XAST_DRAWDUMP_FILE"),"a");
        if (f)
         {fprintf(f,"p %d %d %d %d\n",stage->playAreaX,stage->playAreaY,
                  (int)playArea.Width(),(int)playArea.Height());
          fprintf(f,"f %ld %d\n",xastQaFrameNo,numFrameDraws);
          for (int i=0;i<numFrameDraws;++i)
           {const FrameDrawRecord& r=frameDraws[i];
            #ifdef X11_BACKEND
            fprintf(f,"r %c %.1f %.1f %.1f %.1f\n",
                    r.mask?'m':'b',r.x,r.y,r.w,r.h);
            #else
            if (r.outline)
             {// wireframe: bbox of the quantized verts under the translation
              float minx=r.oVerts[0],maxx=minx,miny=r.oVerts[1],maxy=miny;
              for (int k=1;k<r.oCount;++k)
               {if (r.oVerts[2*k]<minx) minx=r.oVerts[2*k];
                if (r.oVerts[2*k]>maxx) maxx=r.oVerts[2*k];
                if (r.oVerts[2*k+1]<miny) miny=r.oVerts[2*k+1];
                if (r.oVerts[2*k+1]>maxy) maxy=r.oVerts[2*k+1];
               }
              fprintf(f,"r o %.1f %.1f %.1f %.1f\n",
                      r.tx+minx,r.ty+miny,maxx-minx,maxy-miny);
             }
            else if (r.tx!=0.0f||r.ty!=0.0f||r.angle!=0.0f)
             // transformed composite: dest rect is centered on (tx,ty); the
             // generator expands by the rotation bound (sqrt2 on the side).
             fprintf(f,"r t %.1f %.1f %.1f %.1f\n",r.tx,r.ty,r.w,r.h);
            else
             fprintf(f,"r %c %.1f %.1f %.1f %.1f\n",
                     r.mask?'m':'b',r.x,r.y,r.w,r.h);
            #endif
           }
          fclose(f);
         }
       }
     }

    inline void xastQaPublishFrame()
     {const char* cp=xastQaPath("XAST_FRAME_COUNTER_FILE");
      const char* hp=xastQaPath("XAST_STATE_HASH_FILE");
      if (!cp&&!hp)
        return;
      #ifdef X11_BACKEND
      // Observability barrier BEFORE publishing: PresentFrame queued the
      // present copies into this client's output buffer; flushing them
      // guarantees any grab issued after the counter is read executes
      // after those copies in server request order.
      XFlush((Display*)stage->engine.nativeHandle().display);
      #endif
      if (cp)
       {char buf[32];
        const int len=snprintf(buf,sizeof buf,"%ld\n",xastQaFrameNo);
        const int fd=open(cp,O_WRONLY|O_CREAT,0644);
        if (fd>=0)
         {pwrite(fd,buf,(size_t)len,0);
          ftruncate(fd,(off_t)len);
          close(fd);
         }
       }
      if (hp)
       {FILE* f=fopen(hp,"a");
        if (f)
         {fprintf(f,"p %d %d %d %d\n",stage->playAreaX,stage->playAreaY,
                  (int)playArea.Width(),(int)playArea.Height());
          fprintf(f,"h %ld %016llx %d %d %d %d %d\n",
                  xastQaFrameNo,xastQaPendingHash,
                  nonPermeableList.GetNumItems(),selfPermeableList.GetNumItems(),
                  permeableList.GetNumItems(),score->CurrentScore(),
                  shipYard->NumShips());
          fclose(f);
         }
       }
      // Lockstep gate: hold the simulation until the harness acknowledges
      // this frame, so scripted inputs always land in the NEXT drain and
      // captures see a frozen post-present frame regardless of render load
      // (the quiescence-era drift was exactly this race). Self-heals after
      // 2s if the harness dies.
      const char* gp=xastQaPath("XAST_FRAME_GATE_FILE");
      if (gp)
       {timeval start,end;
        gettimeofday(&start,NULL);
        for (;;)
         {FILE* f=fopen(gp,"r");
          long g=-1;
          if (f)
           {if (fscanf(f,"%ld",&g)!=1)
              g=-1;
            fclose(f);
           }
          if (g>=xastQaFrameNo)
            break;
          gettimeofday(&end,NULL);
          if ((end.tv_sec-start.tv_sec)*1000000+(end.tv_usec-start.tv_usec)
              >2000000)
            break;
          usleep(1000);
         }
       }
     }

    // One rect covering a whole text block (help screen / game-over table):
    // union semantics make per-string precision unnecessary; the generator
    // pads and clips to the play rect. tag ∈ {help, table}.
    inline void xastQaTextBlock(const char* tag,float x,float yTop,
                                float w,float h)
     {const char* tp=xastQaPath("XAST_TEXTDUMP_FILE");
      if (!tp)
        return;
      FILE* f=fopen(tp,"a");
      if (!f)
        return;
      fprintf(f,"p %d %d %d %d\n",stage->playAreaX,stage->playAreaY,
              (int)playArea.Width(),(int)playArea.Height());
      fprintf(f,"t %s %.1f %.1f %.1f %.1f\n",tag,x,yTop,w,h);
      fclose(f);
     }

  private:
    unsigned long long xastQaPendingHash=0;

#ifdef X11_BACKEND
    Boolean RunGame(Options& options);
#else
    Boolean RunGame(OptionsMenu& menu);
#endif
    void DrawGame();
    void NewRocksAndEnemy();
    Vector2d CalcGravityAcceleration(const MovableObject* const, const MovableObject* const);
    void SetGravityAcceleration();
    timeval ResumePlay(Intersector&);
    void GenHelpScreen();

    int fontHeight,
        framesThisLevel,
        nextShipScore;

    double speedAdjust;

    Intersector intersector;

    Boolean leftRotPending,
            rightRotPending,
            firing;

    FrameDrawRecord frameDraws[maxFrameDraws];
    int numFrameDraws;

    // Task 47: the canvas alias member died with the D-C C3 aliases; the id
    // comes from the backend each time (GPU twin returns 0, where
    // beginRenderTo is a documented no-op — identical to the old zeroed
    // alias on those legs).
    TextureId canvasId() const
     {return static_cast<X11Backend&>(engine).canvas();
     }
    void PresentFrame(Boolean presentLeg=on);
    void RenderFrameDraws();
#ifndef X11_BACKEND
    // Task-36: per-frame repaint of the persistent window chrome (title,
    // hi-score header, score, yard, button). The X11 window keeps these
    // between frames because the field present copies only the play rect;
    // the GL swap consumes the whole back buffer, so D8's redraw-everything
    // contract needs this leg every frame.
    inline void DrawHudChrome();
#endif

  };

extern PlayingField* playingField;

#ifdef GL_BACKEND
// ---------------------------------------------------------------------------
// GL input bridge (task 35). glBackend's typed-event seam (D16) is not wired
// yet — pollEvents() returns 0 — so main() installs a GLFW key callback that
// appends translated GameEvents here; RunGame/PlayTheGame drain this queue
// alongside engine.pollEvents. The harness's XTest synthetic keys reach GLFW
// through X11, which makes the seeded scripted session drivable on GL.
// Header-inline statics: this header is included by exactly ONE TU per link
// (XAsteroids.o) — the repo-wide header-inline convention.
// ---------------------------------------------------------------------------
inline GameEvent glEventQueue[256];
inline int glEventHead=0,
           glEventTail=0;

inline void glEnqueueKeyEvent(bool down,char character)
 {const int next=(glEventTail+1)%256;
  if (next==glEventHead)
    return;
  GameEvent& event=glEventQueue[glEventTail];
  event.type=down ? GameEvent::KeyDown : GameEvent::KeyUp;
  event.key=0;
  event.character=character;
  event.x=0;
  event.y=0;
  glEventTail=next;
 }

inline int glDrainEvents(GameEvent* out,int maxCount)
 {int n=0;
  while (glEventHead!=glEventTail&&n<maxCount)
   {out[n++]=glEventQueue[glEventHead];
    glEventHead=(glEventHead+1)%256;
   }
  return n;
 }
#endif

inline int PlayingField::uSecondsPerFrame=62500,
    PlayingField::initialShips=3,
    PlayingField::extraShipScore=10000;

inline double PlayingField::maxLinearVelocity=28,
       PlayingField::universalGravitationalConst=0;

inline PlayingField::Boolean PlayingField::enemiesExist=PlayingField::on,
                      PlayingField::relativisticMass=PlayingField::off;

inline const Box PlayingField::playArea(640,512,Vector2d(320+2*ROCK::scale,256+2*ROCK::scale));

inline PlayingField::PlayingField(RenderingEngine& eng): engine(eng),
                                     lastHitFromAlly(off),
                                     lastHitScore(0),
#ifdef X11_BACKEND
                                     fontHeight(static_cast<X11Backend&>(eng).hiScoreFont()->ascent
                                               +static_cast<X11Backend&>(eng).hiScoreFont()->descent+4),
#else
                                     fontHeight(0),
#endif
                                     framesSinceLastBullet(0),
                                     framesThisLevel(0),
                                     nextShipScore(extraShipScore),
                                     framesSinceEnemy(0),
                                     speedAdjust(uSecondsPerFrame/125E3),
                                     level(0),
                                     nonPermeableList(28),
                                     selfPermeableList(128),
                                     permeableList(28),
                                      intersector(nonPermeableList,selfPermeableList,permeableList),
                                      leftRotPending(off),
                                      rightRotPending(off),
                                      firing(off),
                                      numFrameDraws(0)
  {
   #ifndef X11_BACKEND
   FontMetrics hiScoreFm;
   engine.getFontMetrics(EngineFont_HiScore,hiScoreFm);
   fontHeight=(int)hiScoreFm.max_bounds.ascent
             +(int)hiScoreFm.max_bounds.descent+4;
   #endif
   GenHelpScreen();
  }

inline PlayingField::~PlayingField()
 {// Disarmed at task 15 (D15): the canvas and both GCs are backend-owned
  // now and are released in shutdown; this dtor keeps layout+logic only.
  }

inline PlayingField& PlayingField::AddShip()
 {if (!shipGroup->hyper.hyperspacing)
    intersector.AddNonPermeable(shipGroup->ship,0);
  return *this;
 }

inline PlayingField& PlayingField::RemoveShip()
 {leftRotPending=rightRotPending=firing=off;
  intersector.RemoveNonPermeable(shipGroup->ship);
  if (shipGroup->thrust->thrusting)
   {intersector.RemovePermeable(shipGroup->thrust);
    shipGroup->ship->ObjectLiner().Acceleration()=Vector2d();
    shipGroup->thrust->thrusting=Thrust::off;
   }
  return *this;
 }

inline void PlayingField::NewRocksAndEnemy()
 {++framesSinceLastBullet;
  if (framesThisLevel*speedAdjust<3600)
    ++framesThisLevel;
  ++framesSinceEnemy;
  if (selfPermeableList.GetNumItems())
   {int framesThisLevelDenominator=14400*(8-level+selfPermeableList.GetNumItems());
    if (enemiesExist&&enemyGroup->enemyInactive&&
	      gary_rand::rand_16()<(RAND_MAX_16*framesThisLevel
                              /(framesThisLevelDenominator<=0 ? 1 : framesThisLevelDenominator)
                              +framesSinceEnemy/1024+framesSinceLastBullet)*speedAdjust)
      enemyGroup->NewEnemy(level);
   }
  else
   {int nonRockItems=nonPermeableList.GetNumItems()+permeableList.GetNumItems();
    if (!shipGroup->ship->dead&&(nonRockItems==1||
                               shipGroup->thrust->thrusting&&nonRockItems==2)||
        !nonRockItems)
      {rockGroup->NewROCKs();
       level++;
       framesThisLevel=0;
      }
   }
 }

inline void PlayingField::DrawGame()
 {// D8 step (7): HUD pass — score/title/shipyard/button all live OUTSIDE the
  // play rect, so this runs with the scissor disabled. The canvas clear that
  // used to trail the present here moved to the head of the frame.
  stage->DrawScore();
  if (score->CurrentScore()>=nextShipScore)
   {shipYard->AddShip();
    stage->DrawShipYard();
    nextShipScore+=extraShipScore;
   }
 }

inline void PlayingField::RenderFrameDraws()
  {for (int i=0;i<numFrameDraws;++i)
    {const FrameDrawRecord& record=frameDraws[i];
     #ifndef X11_BACKEND
     if (record.outline)
      {engine.setTransform(record.tx,record.ty,record.angle);
       engine.drawPolygon(record.oVerts,record.oCount,
                          record.oR,record.oG,record.oB,false);
       engine.resetTransform();
       continue;
      }
     const bool transformed=record.tx!=0.0f||record.ty!=0.0f||record.angle!=0.0f;
     if (transformed)
       engine.setTransform(record.tx,record.ty,record.angle);
     #endif
     if (record.mask)
       engine.drawTextureMasked(record.tex,record.mask,record.x,record.y,
                                record.w,record.h);
     else
       engine.drawTexture(record.tex,record.x,record.y,record.w,record.h,
                          1.0f);
     #ifndef X11_BACKEND
     if (transformed)
       engine.resetTransform();
     #endif
    }
   numFrameDraws=0;
  }

inline void PlayingField::PresentFrame(Boolean presentLeg)
 {// ---- D8 canonical frame structure (task 24) ----
  // (2) frame start — owns NO scissor anywhere (m14);
  engine.beginFrame();
  // World pass targets the canvas (the standing target is the window; the
  // HUD below needs that standing target restored, hence the bracket).
  engine.beginRenderTo(canvasId());
  // (3) canvas play-area sub-rect only (the border ring survives);
  engine.clear();
  #ifndef X11_BACKEND
  // GL: clear() wipes the whole window (the canvas indirection collapsed),
  // so the chrome comes back before the world pass (disjoint regions).
  DrawHudChrome();
  #endif
  // (4) LOGICAL CLIENT coords: Stage layout values + play-area extents; the
  // backend applies any present transform uniformly. On X11 the scissor is
  // stored-only today, which is byte-identical by construction: the frame-end
  // present leg already copies exactly this rect out of the canvas, so a
  // clip here could not change a single pixel — GL/VK consume it for real.
  const int playRect[4]={stage->playAreaX,stage->playAreaY,
                         (int)playArea.Width(),(int)playArea.Height()};
  engine.setScissorRect(playRect);
   // (5) world pass — ORIGINAL per-object emission order verbatim on X11
   // (O5-M4: the canonical order is a GL/VK-only contract); replay of the
   // records captured at the old mid-physics draw points;
    #ifdef GL_BACKEND
    // Canvas-domain shift: record coords are X11-canvas-relative (80-based);
    // GL has no canvas indirection, so translate by playAreaXY-80.
    static_cast<GLBackend&>(engine).setCanvasOffset(
        (float)(stage->playAreaX-80),(float)(stage->playAreaY-80));
    #elif defined(VK_BACKEND)
    // VK mirrors GL exactly (no canvas indirection either).
    static_cast<VKBackend&>(engine).setCanvasOffset(
        (float)(stage->playAreaX-80),(float)(stage->playAreaY-80));
    #endif
    RenderFrameDraws();
    #ifdef GL_BACKEND
    static_cast<GLBackend&>(engine).setCanvasOffset(0.0f,0.0f);
    #elif defined(VK_BACKEND)
    static_cast<VKBackend&>(engine).setCanvasOffset(0.0f,0.0f);
    #endif
  engine.setScissorRect(nullptr);
  engine.endRenderTo();
  // (6) HUD lives outside the play rect;
  // (7) HUD pass;
  DrawGame();
   // (8) present — requests the field leg and replays both request-gated
   // legs (yard first, then field), no internal sync (Q13-proven).
   // presentLeg=off (D9 pause branch): leave the frame open so the caller
   // renders the menu overlay on top; its trailing endFrame is the single
   // swap the branch intends.
   if (presentLeg)
     stage->DrawPlayingField();
  }

#ifndef X11_BACKEND
// Task-36: full-surface base repaint + persistent chrome. The X11 window
// carries a gray background pixel, keeps title/header/yard/button between
// frames (the field present copies only the play rect), and blacks the play
// area via the canvas clear; the GL swap consumes the whole back buffer, so
// D8's redraw-everything contract needs all of it every frame. Regions are
// disjoint, so ordering against the world pass is pixel-irrelevant.
inline void PlayingField::DrawHudChrome()
 {// window backdrop (the 46260 gray); the extent is recomputed from the
  // shared formula with the Stage ctor's exact inputs — identical values.
  FontMetrics titleFm,hiScoreFm,scoreFm;
  engine.getFontMetrics(EngineFont_Title,titleFm);
  engine.getFontMetrics(EngineFont_HiScore,hiScoreFm);
  engine.getFontMetrics(EngineFont_Score,scoreFm);
  int windowWidth=0,windowHeight=0;
  WindowSizeFormula::Compute(titleFm,hiScoreFm,scoreFm,
                             (int)engine.measureText("Asteroids",EngineFont_Title),
                             playArea.Width(),playArea.Height(),
                             shipGroup->maxIconHeight,windowWidth,windowHeight);
  engine.drawRect(0.0f,0.0f,(float)windowWidth,(float)windowHeight,
                  46260.0f/65535.0f,46260.0f/65535.0f,46260.0f/65535.0f,true);
  // play-area base (the canvas clear's visible equivalent);
  engine.drawRect((float)stage->playAreaX,(float)stage->playAreaY,
                  (float)playArea.Width(),(float)playArea.Height(),
                  0.0f,0.0f,0.0f,true);
  // title + hi-score header (the static legs of Stage::Refresh — no expose
  // path exists on this leg);
  engine.drawStringTransparent("Asteroids",(float)stage->titleX,(float)stage->titleY,
                               EngineFont_Title,0.0f,0.0f,0.0f);
  char str[12];
  snprintf(str,sizeof(str),"%10.10s",score->TopTenName(0));
  engine.drawStringOpaque(str,(float)stage->hiScoreUserX,(float)stage->hiScoreUserY,
                          EngineFont_HiScore,
                          0.0f,0.0f,0.0f,
                          46260.0f/65535.0f,46260.0f/65535.0f,
                          46260.0f/65535.0f);
  snprintf(str,sizeof(str),"%10d",score->TopTenScore(0));
  engine.drawStringOpaque(str,(float)stage->hiScoreX,(float)stage->hiScoreY,
                          EngineFont_HiScore,
                          0.0f,0.0f,0.0f,
                          46260.0f/65535.0f,46260.0f/65535.0f,
                          46260.0f/65535.0f);
  stage->DrawScore();
  // yard + button (GL counterparts of the two request-gated present legs);
  engine.drawTexture(shipYard->GetYard(),(float)stage->shipYardX,
                     (float)stage->shipYardY,
                     (float)stage->shipYardWidth,(float)stage->shipYardHeight,
                     1.0f);
  button->DrawButton();
 }
#endif

 inline Vector2d PlayingField::CalcGravityAcceleration(const MovableObject* const obj1,
                                                       const MovableObject* const obj2)
 {Vector2d dist=obj2->CurrentBox().Center()-obj1->CurrentBox().Center();
  double distMagSquared=dist.MagnitudeSquared(),
         distMag,
         radiusAverage=.5*(obj1->ObjectRotator().Radius()+obj2->ObjectRotator().Radius()),
         radiusAverageSquared=radiusAverage*radiusAverage,
         c,
         cSquared,
         mass=relativisticMass ? (c=maxLinearVelocity*1.001,
                                  cSquared=c*c,
                                  obj2->ObjectRotator().Area()*sqrt(cSquared/(cSquared-obj2->ObjectLiner().VelocityMagnitude()
                                                                                       *obj2->ObjectLiner().VelocityMagnitude())))
                               : obj2->ObjectRotator().Area(),
         uSecondPerFrameRatio;
  return distMagSquared ? (distMag=sqrt(distMagSquared),
                           uSecondPerFrameRatio=uSecondsPerFrame/6.25E4, // Scale the acceleration to the currwnt frame rate of the game. 16 fps is baseline.
                           (dist/distMag)*universalGravitationalConst
                           *(distMag<radiusAverage ? mass*distMag
                                                     /(radiusAverageSquared*radiusAverage) // Overlapping object gravitational acceleration
                                                   : mass/distMagSquared) // Non-overlapping object gravitational accelaration
                           *uSecondPerFrameRatio*uSecondPerFrameRatio)
                        : Vector2d();
 }

inline void PlayingField::SetGravityAcceleration()
 {if (universalGravitationalConst)
   {nonPermeableList.SetCurrentAtHead();
    for (int i=0;i<nonPermeableList.GetNumItems();++i)
     {Vector2d gravityAcceleration;
      const int itemIndex=nonPermeableList.GetItemIndex();
      MovableObject* const item1Addr=nonPermeableList.GetData();
      nonPermeableList.SetCurrentAtHead();
      for (int j=0;j<nonPermeableList.GetNumItems();++j)
       {const MovableObject* const item2Addr=nonPermeableList.GetData();
        if (item1Addr!=item2Addr)
          gravityAcceleration+=CalcGravityAcceleration(item1Addr,item2Addr);
        nonPermeableList.Next();
       }
      selfPermeableList.SetCurrentAtHead();
      for (int j=0;j<selfPermeableList.GetNumItems();++j)
       {gravityAcceleration+=CalcGravityAcceleration(item1Addr,
                                                     selfPermeableList.GetData());
        selfPermeableList.Next();
       }
      if (item1Addr==shipGroup->ship&&shipGroup->thrust->thrusting)
       {shipGroup->ship->ObjectLiner().Acceleration()=shipGroup->ship->ObjectLiner().Acceleration()
                                                     +gravityAcceleration;
        shipGroup->thrust->ObjectLiner().Acceleration()=shipGroup->ship->ObjectLiner().Acceleration();
       }
      else
        item1Addr->ObjectLiner().Acceleration()=gravityAcceleration;
      nonPermeableList.SetCurrentAtItem(itemIndex).Next();
     }
    selfPermeableList.SetCurrentAtHead();
    for (int i=0;i<selfPermeableList.GetNumItems();++i)
     {Vector2d gravityAcceleration;
      const int itemIndex=selfPermeableList.GetItemIndex();
      MovableObject* const item1Addr=selfPermeableList.GetData();
      nonPermeableList.SetCurrentAtHead();
      for (int j=0;j<nonPermeableList.GetNumItems();++j)
       {gravityAcceleration+=CalcGravityAcceleration(item1Addr,
                                                     nonPermeableList.GetData());
        nonPermeableList.Next();
       }
      selfPermeableList.SetCurrentAtHead();
      for (int j=0;j<selfPermeableList.GetNumItems();++j)
       {const MovableObject* const item2Addr=selfPermeableList.GetData();
        if (item1Addr!=item2Addr)
          gravityAcceleration+=CalcGravityAcceleration(item1Addr,item2Addr);
        selfPermeableList.Next();
       }
      item1Addr->ObjectLiner().Acceleration()=gravityAcceleration;
      selfPermeableList.SetCurrentAtItem(itemIndex).Next();
     }
   }
 }

inline timeval PlayingField::ResumePlay(Intersector& intersector)
 {// Task 25: the window-raise and frame-clock flush that used to bracket
  // this function fire inside the backend's pollEvents at the exact resume
  // transitions (pause-spin exit, click-session end); the clock read and
  // the pause-state resets stay here untouched.
  timeval time;
  gettimeofday(&time,NULL);
  leftRotPending=rightRotPending=firing=off;
  shipGroup->ship->ObjectRotator().AngularVelocity()=0;
  if (shipGroup->thrust->thrusting)
   {intersector.RemovePermeable(shipGroup->thrust);
    shipGroup->ship->ObjectLiner().Acceleration()=Vector2d();
    shipGroup->thrust->thrusting=Thrust::off;
   }
  return time;
 }

#ifdef X11_BACKEND
inline PlayingField::Boolean PlayingField::RunGame(Options& options)
 {// latched here, performed at the next drain head
  static_cast<X11Backend&>(engine).requestRaise();
  nonPermeableList.Reset();
  selfPermeableList.Reset();
  permeableList.Reset();
  score->Reset();
  framesSinceLastBullet=0;
  framesThisLevel=0;
  nextShipScore=extraShipScore;
  framesSinceEnemy=0;
  for (int i=0;i<initialShips;++i)
    shipYard->AddShip();
  numFrameDraws=0;
  NewRocksAndEnemy();
  // The old init-time canvas clear folds into the frame structure below.
  PresentFrame();
  // One typed-event drain per frame (cadence invariant: every queued event
  // is consumed and applied in order within its frame; a press+release pair
  // landing in the same drain cancels). Generous bound — session motion is
  // compressed to one slot backend-side, so overflow is unreachable in
  // practice; overflowed normals simply ride the next drain.
  GameEvent events[256];
  while(shipYard->NumShips())
   {shipYard->RemoveShip();
    stage->DrawShipYard();
    shipGroup->NewShip();
    leftRotPending=rightRotPending=firing=off;
    while(nonPermeableList.GetNumItems()||permeableList.GetNumItems())
     {timeval startTime,
              endTime;
      gettimeofday(&startTime,NULL);
      NewRocksAndEnemy();
      if (ShipBulletGroup::autoFire&&firing)
        shipBulletGroup->fireThisFrame=ShipBulletGroup::on;
      static_cast<X11Backend&>(engine).frameClockSync();
      int numEvents=engine.pollEvents(events,256);
      // Click-session state for THIS drain's batch (the backend never
      // spans drains with a session): press → hover stream → qualifying
      // release or release-then-reenter ends it with the clock re-arm.
      Boolean clickSession=off,
              clickReleased=off,
              clickInWindow=on;
      if (static_cast<X11Backend&>(engine).takeCloseRequest())
        return on;
      if (static_cast<X11Backend&>(engine).takeRefreshRequest())
        stage->Refresh();
      for (int i=0;i<numEvents;++i)
       {const GameEvent& event=events[i];
        switch(event.type)
         {case GameEvent::CursorEnter:  if (clickSession)
                                         {if (clickReleased)
                                           {clickSession=off;
                                            startTime=ResumePlay(intersector);
                                           }
                                         }
                                        else
                                          startTime=ResumePlay(intersector);
                                        break;
          case GameEvent::CursorLeave:  if (clickSession)
                                          clickInWindow=off;
                                        break;
          case GameEvent::MouseDown:    if (event.key==1/*Button1*/)
                                         {if (button->PointerOnButton(event.x,event.y))
                                            button->PressButton();
                                          clickSession=on;
                                         }
                                        break;
          case GameEvent::MouseMove:    if (clickSession)
                                         {if (button->PointerOnButton(event.x,event.y))
                                            button->PressButton();
                                          else
                                            button->ReleaseButton();
                                         }
                                        break;
          case GameEvent::MouseUp:      if (clickSession&&event.key==1)
                                         {if (button->PointerOnButton(event.x,event.y))
                                           {options.RealizeWindow();
                                            button->ReleaseButton();
                                           }
                                          clickReleased=on;
                                          if (clickInWindow)
                                           {clickSession=off;
                                            startTime=ResumePlay(intersector);
                                           }
                                         }
                                        break;
          case GameEvent::KeyDown:      if (!shipGroup->hyper.hyperspacing)
                                          switch(event.character)
                                           {case 'e': if (shipGroup->ship->ObjectRotator().AngularVelocity())
                                                        leftRotPending=True;
                                                      else
				                                 {shipGroup->ship->ObjectRotator().AngularVelocity()=
                                                          -shipGroup->ship->angularVelocity;
                                                      if (shipGroup->thrust->thrusting)
                                                        shipGroup->thrust->ObjectRotator().AngularVelocity()=
                                                          -shipGroup->ship->angularVelocity;
				                                 }
                                                      break;
				                              case 'r': if (shipGroup->ship->ObjectRotator().AngularVelocity())
                                                        rightRotPending=True;
                                                      else
                        				                 {shipGroup->ship->ObjectRotator().AngularVelocity()=
                                                          shipGroup->ship->angularVelocity;
                                                      if (shipGroup->thrust->thrusting)
                                                        shipGroup->thrust->ObjectRotator().AngularVelocity()=
                                                          shipGroup->ship->angularVelocity;
				                 }
                                                      break;
                                              case 'o': shipGroup->NewThrust();
                                                        shipGroup->thrust->thrusting=Thrust::on;
                                                        intersector.AddPermeable(shipGroup->thrust,0);
                                                        break;
                                              case 'p': shipBulletGroup->fireThisFrame=ShipBulletGroup::on;
                                                        firing=on;
                                                        break;
                                              case ' ': RemoveShip();
                                                        shipGroup->NewHyper();
                                                        break;
	                                    case 'q': return on;
                                                      break;
                                              case 'n':
                                              case 'h': goto ResetGameUpdateScoreAndReturn;
                                             }
                                        else
                                          switch(event.character)
                                           {case 'q': return on;
                                                      break;
                                            case 'n':
                                            case 'h': goto ResetGameUpdateScoreAndReturn;
				                             }
                                        break;
          case GameEvent::KeyUp:        if (!shipGroup->hyper.hyperspacing)
                                          switch (event.character)
                                           {case 'e': if (shipGroup->ship->ObjectRotator().AngularVelocity()<0)
                                                        if (rightRotPending)
                                                         {shipGroup->ship->ObjectRotator().AngularVelocity()=
                                                            shipGroup->ship->angularVelocity;
                                                          if (shipGroup->thrust->thrusting)
                                                            shipGroup->thrust->ObjectRotator().AngularVelocity()=
                                                              shipGroup->ship->angularVelocity;
                                                         }
                                                        else
                                                         {shipGroup->ship->ObjectRotator().AngularVelocity()=0;
                                                          if (shipGroup->thrust->thrusting)
                                                            shipGroup->thrust->ObjectRotator().AngularVelocity()=0;
                                                         }
                                                      leftRotPending=False;
                                                      break;
                                              case 'r': if (shipGroup->ship->ObjectRotator().AngularVelocity()>0)
                                                        if (leftRotPending)
                                                         {shipGroup->ship->ObjectRotator().AngularVelocity()=
                                                            -shipGroup->ship->angularVelocity;
                                                          if (shipGroup->thrust->thrusting)
                                                            shipGroup->thrust->ObjectRotator().AngularVelocity()=
                                                              -shipGroup->ship->angularVelocity;
                                                         }
                                                        else
                                                         {shipGroup->ship->ObjectRotator().AngularVelocity()=0;
                                                          if (shipGroup->thrust->thrusting)
                                                            shipGroup->thrust->ObjectRotator().AngularVelocity()=0;
                                                         }
                                                      rightRotPending=False;
                                                      break;
                                              case 'o': intersector.RemovePermeable(shipGroup->thrust);
                                                        shipGroup->ship->ObjectLiner().Acceleration()=Vector2d();
                                                        shipGroup->thrust->thrusting=Thrust::off;
                                                        break;
                                              case 'p': shipBulletGroup->fireThisFrame=ShipBulletGroup::off;
                                                        firing=off;
                                                        break;
                                             }
                                        break;
          default:                      break;
         }
       }
      SetGravityAcceleration();
      intersector.Intersect();
      xastQaRecordFrameState();
      PresentFrame();
      xastQaPublishFrame();
      gettimeofday(&endTime,NULL);
      unsigned int diffTime=endTime.tv_usec-startTime.tv_usec+(endTime.tv_sec-startTime.tv_sec)*1000000;
      if (diffTime<uSecondsPerFrame)
        usleep(uSecondsPerFrame-diffTime);
     }
   }
  ResetGameUpdateScoreAndReturn: shipYard->ClearYard();
  stage->DrawShipYard();
  shipBulletGroup->Reset();
  enemyGroup->Reset();
  enemyBulletGroup->Reset();
  score->UpdateTopTen();
  XFontStruct* const hiScoreInfo=static_cast<X11Backend&>(engine).hiScoreFont();
  int xPos=playArea.NorthWestCorner().x+(playArea.Width()-hiScoreInfo->max_bounds.width*22)/2
           -hiScoreInfo->max_bounds.lbearing,
      yPosSave=playArea.NorthWestCorner().y+(playArea.Height()-15*fontHeight+4)/2+hiScoreInfo->ascent,
      yPos=yPosSave;
  char str[23];
  engine.beginRenderTo(canvasId());
  // The old flow cleared the canvas right after every present, so this
  // screen always drew over fresh black; the clear now heads each frame,
  // so restore that black base explicitly before the score table.
  engine.clear();
  for(int i=0;i<score->NumScores();++i)
   {const int nameWidth=12-(int)strlen(score->TopTenName(i));
    snprintf(str,sizeof(str),"%s%*s%10d",
             score->TopTenName(i),nameWidth>0?nameWidth:1,"",
             score->TopTenScore(i));
    engine.drawStringTransparent(str,(float)xPos,(float)yPos,
                                 EngineFont_HiScore,1.0f,1.0f,1.0f);
    yPos+=fontHeight;
   }
  engine.drawStringTransparent("        S -- start",(float)xPos,
                               (float)(yPos=yPosSave+12*fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        Q -- quit",(float)xPos,
                               (float)(yPos+=fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        H -- help",(float)xPos,
                               (float)(yPos+=fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  xastQaTextBlock("table",(float)xPos,
                  (float)(yPosSave-hiScoreInfo->ascent),
                  (float)(hiScoreInfo->max_bounds.width*22),
                  (float)(15*fontHeight));
  engine.endRenderTo();
  stage->DrawPlayingField();
  return off;
  }
#else
inline PlayingField::Boolean PlayingField::RunGame(OptionsMenu& menu)
 {// GL mirror of the guarded loop: same simulation order and event handling,
   // minus the Motif Options dialog, the click-session state machine and
   // frameClockSync (no X11 frame clock exists here; the tail usleep paces).
   // Events come from engine.pollEvents PLUS the GLFW key bridge above.
   // Task 44a (D9): every polled GameEvent feeds menu.handleGameEvent FIRST
   // (no second pump); while the menu is open the simulation pauses — the
   // frozen world is re-presented each iteration with the overlay drawn on
   // top in WINDOW space — and closing re-arms startTime=ResumePlay exactly
   // like the X11 modal pause.
  nonPermeableList.Reset();
  selfPermeableList.Reset();
  permeableList.Reset();
  score->Reset();
  framesSinceLastBullet=0;
  framesThisLevel=0;
  nextShipScore=extraShipScore;
  framesSinceEnemy=0;
  for (int i=0;i<initialShips;++i)
    shipYard->AddShip();
  numFrameDraws=0;
  NewRocksAndEnemy();
  PresentFrame();
  engine.endFrame();
  GameEvent events[256];
  while(shipYard->NumShips())
   {shipYard->RemoveShip();
    stage->DrawShipYard();
    shipGroup->NewShip();
    leftRotPending=rightRotPending=firing=off;
    while(nonPermeableList.GetNumItems()||permeableList.GetNumItems())
     {timeval startTime,
              endTime;
      gettimeofday(&startTime,NULL);
      NewRocksAndEnemy();
      if (ShipBulletGroup::autoFire&&firing)
        shipBulletGroup->fireThisFrame=ShipBulletGroup::on;
      // Pump BEFORE the drain: events injected while this loop sat gated
      // after the previous publish must be visible to THIS frame's drain.
      // X11's pollEvents reads the server queue directly, but GLFW only
      // delivers through glfwPollEvents, which otherwise first fires
      // inside PresentFrame — one full frame after X11 consumes the same
      // injection (the task-36 trajectory divergence).
      engine.beginFrame();
      int numEvents=engine.pollEvents(events,256);
#ifdef GL_BACKEND
      // GL-only: the task-35 key bridge drain (VK's pollEvents delivers the
      // full D16 stream natively since task 42 — no bridge exists there).
      numEvents+=glDrainEvents(events+numEvents,256-numEvents);
#endif
#if defined(GL_BACKEND) || defined(VK_BACKEND)
      // Window-close latch (M13): the backend's close callback only sets the
      // flag; the loop consumes it here, exactly like the X11 leg's
      // takeCloseRequest.
      if (
#ifdef GL_BACKEND
          static_cast<GLBackend&>(engine)
#elif defined(VK_BACKEND)
          static_cast<VKBackend&>(engine)
#endif
            .closeRequested_)
        return on;
#endif
      // Task 44a: the menu consumes the SAME drained stream before the game
      // table sees anything (D9 input mapping; io queues, NewFrame applies).
      for (int i=0;i<numEvents;++i)
        menu.handleGameEvent(events[i]);
      if (menu.isOpen())
       {// Paused: no simulation, no state-hash record/publish (the frame
        // counter freezes — the QA pause gate reads that freeze). The last
        // simulated frame's draw records replay verbatim, then the overlay
        // renders on top in WINDOW space, then one swap.
        PresentFrame(off);
        menu.tick();
        if (menu.isOpen())
          menu.renderOverlay(engine);
        engine.endFrame();
        if (!menu.isOpen())
          startTime=ResumePlay(intersector);   // D16 re-arm (X11 modal exit)
        gettimeofday(&endTime,NULL);
        int pausedDiff=endTime.tv_usec-startTime.tv_usec
                       +(endTime.tv_sec-startTime.tv_sec)*1000000;
        if (pausedDiff<uSecondsPerFrame)
          usleep(uSecondsPerFrame-pausedDiff);
        continue;
       }
      for (int i=0;i<numEvents;++i)
       {const GameEvent& event=events[i];
        switch(event.type)
         {case GameEvent::KeyDown:      if (!shipGroup->hyper.hyperspacing)
                                          switch(event.character)
                                           {case 'e': if (shipGroup->ship->ObjectRotator().AngularVelocity())
                                                        leftRotPending=on;
                                                      else
				                                 {shipGroup->ship->ObjectRotator().AngularVelocity()=
                                                          -shipGroup->ship->angularVelocity;
                                                      if (shipGroup->thrust->thrusting)
                                                        shipGroup->thrust->ObjectRotator().AngularVelocity()=
                                                          -shipGroup->ship->angularVelocity;
 				                                 }
                                                      break;
 				                              case 'r': if (shipGroup->ship->ObjectRotator().AngularVelocity())
                                                        rightRotPending=on;
                                                      else
                        				                 {shipGroup->ship->ObjectRotator().AngularVelocity()=
                                                          shipGroup->ship->angularVelocity;
                                                      if (shipGroup->thrust->thrusting)
                                                        shipGroup->thrust->ObjectRotator().AngularVelocity()=
                                                          shipGroup->ship->angularVelocity;
 				                                 }
                                                      break;
                                              case 'o': shipGroup->NewThrust();
                                                        shipGroup->thrust->thrusting=Thrust::on;
                                                        intersector.AddPermeable(shipGroup->thrust,0);
                                                        break;
                                              case 'p': shipBulletGroup->fireThisFrame=ShipBulletGroup::on;
                                                        firing=on;
                                                        break;
                                              case ' ': RemoveShip();
                                                        shipGroup->NewHyper();
                                                        break;
 	                                    case 'q': return on;
                                                      break;
                                              case 'n':
                                              case 'h': goto ResetGameUpdateScoreAndReturn;
                                             }
                                        else
                                          switch(event.character)
                                           {case 'q': return on;
                                                      break;
                                            case 'n':
                                            case 'h': goto ResetGameUpdateScoreAndReturn;
 				                             }
                                        break;
          case GameEvent::KeyUp:        if (!shipGroup->hyper.hyperspacing)
                                          switch (event.character)
                                           {case 'e': if (shipGroup->ship->ObjectRotator().AngularVelocity()<0)
                                                        if (rightRotPending)
                                                         {shipGroup->ship->ObjectRotator().AngularVelocity()=
                                                            shipGroup->ship->angularVelocity;
                                                          if (shipGroup->thrust->thrusting)
                                                            shipGroup->thrust->ObjectRotator().AngularVelocity()=
                                                              shipGroup->ship->angularVelocity;
                                                         }
                                                        else
                                                         {shipGroup->ship->ObjectRotator().AngularVelocity()=0;
                                                          if (shipGroup->thrust->thrusting)
                                                            shipGroup->thrust->ObjectRotator().AngularVelocity()=0;
                                                         }
                                                      leftRotPending=off;
                                                      break;
                                              case 'r': if (shipGroup->ship->ObjectRotator().AngularVelocity()>0)
                                                        if (leftRotPending)
                                                         {shipGroup->ship->ObjectRotator().AngularVelocity()=
                                                            -shipGroup->ship->angularVelocity;
                                                          if (shipGroup->thrust->thrusting)
                                                            shipGroup->thrust->ObjectRotator().AngularVelocity()=
                                                              -shipGroup->ship->angularVelocity;
                                                         }
                                                        else
                                                         {shipGroup->ship->ObjectRotator().AngularVelocity()=0;
                                                          if (shipGroup->thrust->thrusting)
                                                            shipGroup->thrust->ObjectRotator().AngularVelocity()=0;
                                                         }
                                                      rightRotPending=off;
                                                      break;
                                              case 'o': intersector.RemovePermeable(shipGroup->thrust);
                                                        shipGroup->ship->ObjectLiner().Acceleration()=Vector2d();
                                                        shipGroup->thrust->thrusting=Thrust::off;
                                                        break;
                                               case 'p': shipBulletGroup->fireThisFrame=ShipBulletGroup::off;
                                                         firing=off;
                                                         break;
                                              }
                                         break;
          case GameEvent::MouseDown:    if (event.key==1/*Button1*/)
                                          if (button->PointerOnButton(event.x,event.y))
                                            button->PressButton();
                                        break;
          case GameEvent::MouseUp:      if (event.key==1/*Button1*/)
                                         {if (button->PointerOnButton(event.x,event.y))
                                           {menu.open();       // 44a: the
                                            button->ReleaseButton(); // Options click
                                           }
                                          else
                                            button->ReleaseButton();
                                         }
                                        break;
           default:                      break;
          }
        }
       SetGravityAcceleration();
       intersector.Intersect();
       xastQaRecordFrameState();
       PresentFrame();
       engine.endFrame();
       xastQaPublishFrame();
      gettimeofday(&endTime,NULL);
      unsigned int diffTime=endTime.tv_usec-startTime.tv_usec+(endTime.tv_sec-startTime.tv_sec)*1000000;
      if (diffTime<uSecondsPerFrame)
        usleep(uSecondsPerFrame-diffTime);
     }
    }
  ResetGameUpdateScoreAndReturn: shipYard->ClearYard();
  stage->DrawShipYard();
  shipBulletGroup->Reset();
  enemyGroup->Reset();
  enemyBulletGroup->Reset();
  score->UpdateTopTen();
  FontMetrics hiScoreFm;
  engine.getFontMetrics(EngineFont_HiScore,hiScoreFm);
  int xPos=playArea.NorthWestCorner().x+(playArea.Width()-hiScoreFm.max_bounds.width*22)/2,
      yPosSave=playArea.NorthWestCorner().y+(playArea.Height()-15*fontHeight+4)/2+hiScoreFm.max_bounds.ascent,
      yPos=yPosSave;
  char str[23];
  // Task 43: open the frame before the table draws — GL's endFrame swap
  // needs no preceding begin, but VK's endFrame submits nothing unless
  // beginFrame acquired (the table would never reach the window).
  engine.beginFrame();
  engine.beginRenderTo(canvasId());
  engine.clear();
   // Task-36: base+chrome BEFORE the table (clear() wiped the whole window
   // on this leg).
    DrawHudChrome();
    #ifdef GL_BACKEND
    static_cast<GLBackend&>(engine).setCanvasOffset(
        (float)(stage->playAreaX-80),(float)(stage->playAreaY-80));
    #elif defined(VK_BACKEND)
    static_cast<VKBackend&>(engine).setCanvasOffset(
        (float)(stage->playAreaX-80),(float)(stage->playAreaY-80));
    #endif
  for(int i=0;i<score->NumScores();++i)
   {const int nameWidth=12-(int)strlen(score->TopTenName(i));
    snprintf(str,sizeof(str),"%s%*s%10d",
             score->TopTenName(i),nameWidth>0?nameWidth:1,"",
             score->TopTenScore(i));
    engine.drawStringTransparent(str,(float)xPos,(float)yPos,
                                 EngineFont_HiScore,1.0f,1.0f,1.0f);
    yPos+=fontHeight;
   }
  engine.drawStringTransparent("        S -- start",(float)xPos,
                               (float)(yPos=yPosSave+12*fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        Q -- quit",(float)xPos,
                               (float)(yPos+=fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
   engine.drawStringTransparent("        H -- help",(float)xPos,
                                (float)(yPos+=fontHeight),
                                EngineFont_HiScore,1.0f,1.0f,1.0f);
   xastQaTextBlock("table",(float)xPos,
                   (float)(yPosSave-hiScoreFm.max_bounds.ascent),
                   (float)(hiScoreFm.max_bounds.width*22),
                   (float)(15*fontHeight));
    #ifdef GL_BACKEND
    static_cast<GLBackend&>(engine).setCanvasOffset(0.0f,0.0f);
    #elif defined(VK_BACKEND)
    static_cast<VKBackend&>(engine).setCanvasOffset(0.0f,0.0f);
    #endif
   engine.endRenderTo();
   stage->DrawPlayingField();
   engine.endFrame();
   return off;
   }
#endif

#ifdef X11_BACKEND
inline int PlayingField::PlayTheGame(const int l, int argc, char** argv)
  {
#else
inline int PlayingField::PlayTheGame(const int l, int argc, char** argv,
                                     OptionsMenu& menu)
  {
#endif
  #ifdef X11_BACKEND
  // The WM-hints block (naming, protocol registration, input selection,
  // map-and-raise) relocated into the backend's pass-2 init at task 15
  // (D-C C1); its two recorded deltas live there (argv/argc as NULL,0;
  // map-and-raise at init). The size pin is dropped entirely (v6/S2/D15).
  // Task 25: this outer loop drains typed events like RunGame does. The
  // old blocking single-event read becomes a drain-plus-idle-sleep: when
  // no events are queued the loop parks for 1ms (sub-frame input latency,
  // zero server traffic while parked) instead of blocking in the backend.
  Options options(argc,argv);
  int localHighScore=0;
  Boolean pointerButtonReleased=on,
          buttonActivated=off;
  GameEvent events[64];
  for(;;)
   {int numEvents=engine.pollEvents(events,64);
    if (static_cast<X11Backend&>(engine).takeCloseRequest())
      return localHighScore;
    if (static_cast<X11Backend&>(engine).takeRefreshRequest())
      stage->Refresh();
    for (int i=0;i<numEvents;++i)
     {const GameEvent& event=events[i];
      switch(event.type)
       {case GameEvent::KeyDown:    if (pointerButtonReleased)
                                     switch(event.character)
                                       {case 's': level=l;
                                                  Boolean earlyExit;
                                                  earlyExit=RunGame(options);
                                                  if (localHighScore<score->CurrentScore())
                                                    localHighScore=score->CurrentScore();
                                                  if (earlyExit)
                                                    return localHighScore;
                                                  break;
                                        case 'q': return localHighScore;
                                                  break;
                                        case 'h': GenHelpScreen();
                                                  stage->DrawPlayingField();
                                                  break;
                                       }
                                    break;
        case GameEvent::MouseDown:  if (event.key==1/*Button1*/)
                                     {pointerButtonReleased=off;
                                      if (button->PointerOnButton(event.x,event.y))
                                       {button->PressButton();
                                        buttonActivated=on;
                                       }
                                     }
                                    break;
        case GameEvent::MouseUp:    if (event.key==1/*Button1*/)
                                     {if (buttonActivated &&
                                          button->PointerOnButton(event.x,event.y))
                                       {options.RealizeWindow();
                                        button->ReleaseButton();
                                       }
                                      buttonActivated=off;
                                      pointerButtonReleased=on;
                                     }
                                    break;
        case GameEvent::MouseMove:  if (buttonActivated &&
                                        button->PointerOnButton(event.x,event.y))
                                      button->PressButton();
                                    else
                                      button->ReleaseButton();
                                    break;
        default:                    break;
       }
      }
    if (!numEvents)
      usleep(1000);
    }
  #endif
  #ifndef X11_BACKEND
  // GL outer loop: mirrors the guarded one minus the Motif dialog; keys
  // arrive through engine.pollEvents + the GLFW bridge, mouse events through
  // the task-44a backend bridge. The Options button click opens the menu
  // (the X11 leg realizes the Motif shell instead); while it is open the
  // base screen is redrawn every iteration with the overlay on top.
  int localHighScore=0;
  GameEvent events[64];
  Boolean pointerButtonReleased=on,
          buttonActivated=off;
  // Task-36 initial present: full surface = base+chrome+help text. The
  // guarded branch gets this composite from the Expose-driven Stage::Refresh
  // after the ctor's GenHelpScreen; no damage events exist on this leg, and
  // the ctor's draw alone would be wiped by the first chrome repaint.
  engine.beginFrame();
  GenHelpScreen();
  stage->DrawPlayingField();
  engine.endFrame();
  for(;;)
   {// No backend frame clock here: beginFrame owns the glfwPollEvents pump
    // (the typed-event stub contributes nothing), so pump every iteration
    // or keys parked at a static screen are never delivered.
    engine.beginFrame();
    int numEvents=engine.pollEvents(events,64);
#ifdef GL_BACKEND
    numEvents+=glDrainEvents(events+numEvents,64-numEvents);
#endif
#if defined(GL_BACKEND) || defined(VK_BACKEND)
    if (
#ifdef GL_BACKEND
        static_cast<GLBackend&>(engine)
#elif defined(VK_BACKEND)
        static_cast<VKBackend&>(engine)
#endif
          .closeRequested_)
      return localHighScore;
#endif
    for (int i=0;i<numEvents;++i)
      menu.handleGameEvent(events[i]);
    if (menu.isOpen())
     {// Menu open on a static screen: redraw the base every iteration (D8),
      // tick, overlay after the frame, one swap. Closing erases the overlay
      // via this same clean redraw (tick's Render emits nothing then).
      menu.tick();
      GenHelpScreen();
      stage->DrawPlayingField();
      if (menu.isOpen())
        menu.renderOverlay(engine);
      engine.endFrame();
      if (!numEvents)
        usleep(1000);
      continue;
     }
    for (int i=0;i<numEvents;++i)
     {const GameEvent& event=events[i];
      switch(event.type)
       {case GameEvent::KeyDown:    if (pointerButtonReleased)
                                     switch(event.character)
                                       {case 's': level=l;
                                                  Boolean earlyExit;
                                                  earlyExit=RunGame(menu);
                                                  if (localHighScore<score->CurrentScore())
                                                    localHighScore=score->CurrentScore();
                                                  if (earlyExit)
                                                    return localHighScore;
                                                  break;
                                        case 'q': return localHighScore;
                                                  break;
                                        case 'h': GenHelpScreen();
                                                  stage->DrawPlayingField();
                                                  engine.endFrame();
                                                  break;
                                       }
                                    break;
        case GameEvent::MouseDown:  if (event.key==1/*Button1*/)
                                     {pointerButtonReleased=off;
                                      if (button->PointerOnButton(event.x,event.y))
                                       {button->PressButton();
                                        buttonActivated=on;
                                       }
                                     }
                                    break;
        case GameEvent::MouseUp:    if (event.key==1/*Button1*/)
                                     {if (buttonActivated&&
                                          button->PointerOnButton(event.x,event.y))
                                       {menu.open();           // 44a: Options click
                                        button->ReleaseButton();
                                       }
                                      buttonActivated=off;
                                      pointerButtonReleased=on;
                                     }
                                    break;
        case GameEvent::MouseMove:  if (buttonActivated&&
                                        button->PointerOnButton(event.x,event.y))
                                      button->PressButton();
                                    else
                                      button->ReleaseButton();
                                    break;
        default:                    break;
       }
     }
    if (!numEvents)
      usleep(1000);
   }
  #endif
  return 0;
 }

void PlayingField::GenHelpScreen()
 {
  #ifdef X11_BACKEND
  engine.beginRenderTo(canvasId());
  engine.clear();
  engine.endRenderTo();
  XFontStruct* const hiScoreInfo=static_cast<X11Backend&>(engine).hiScoreFont();
  int xPos=playArea.NorthWestCorner().x+(playArea.Width()
                                         -(int)engine.measureText("        H -- help (this screen)",
                                                                  EngineFont_HiScore))/2
           -hiScoreInfo->max_bounds.lbearing,
      yPos=playArea.NorthWestCorner().y+(playArea.Height()-12*fontHeight+4)/2+hiScoreInfo->ascent;
  engine.beginRenderTo(canvasId());
  engine.drawStringTransparent("        S -- start",(float)xPos,(float)yPos,
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        Q -- quit",(float)xPos,
                               (float)(yPos+=fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        E -- rotate left",(float)xPos,
                               (float)(yPos+=2*fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        R -- rotate right",(float)xPos,
                               (float)(yPos+=fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        O -- thrust",(float)xPos,
                               (float)(yPos+=fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        P -- fire",(float)xPos,
                               (float)(yPos+=fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("space bar -- hyperspace",(float)xPos,
                               (float)(yPos+=fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
   engine.drawStringTransparent("        N -- new game",(float)xPos,
                                 (float)(yPos+=2*fontHeight),
                                 EngineFont_HiScore,1.0f,1.0f,1.0f);
   engine.drawStringTransparent("        H -- help (this screen)",(float)xPos,
                                (float)(yPos+=2*fontHeight),
                                EngineFont_HiScore,1.0f,1.0f,1.0f);
   xastQaTextBlock("help",(float)xPos,
                   (float)(playArea.NorthWestCorner().y
                           +(playArea.Height()-12*fontHeight+4)/2),
                   engine.measureText("        H -- help (this screen)",
                                      EngineFont_HiScore),
                   (float)(12*fontHeight));
   engine.endRenderTo();
   #endif
   #ifndef X11_BACKEND
  // GL leg: the canvas indirection collapses (pixmap=0 → beginRenderTo is a
  // no-op), so the text draws straight onto the window; metrics come from
  // the engine instead of stage's guarded font structs.
  engine.beginRenderTo(canvasId());
  engine.clear();
  engine.endRenderTo();
  FontMetrics hiScoreFm;
  engine.getFontMetrics(EngineFont_HiScore,hiScoreFm);
  int xPos=playArea.NorthWestCorner().x+(playArea.Width()
                                         -(int)engine.measureText("        H -- help (this screen)",
                                                                  EngineFont_HiScore))/2,
      yPos=playArea.NorthWestCorner().y+(playArea.Height()-12*fontHeight+4)/2+hiScoreFm.max_bounds.ascent;
  engine.beginRenderTo(canvasId());
  // Task-36: base+chrome BEFORE the text — clear() wiped the whole window
  // and the strings must land on the restored surface.
  DrawHudChrome();
  #ifdef GL_BACKEND
  static_cast<GLBackend&>(engine).setCanvasOffset(
      (float)(stage->playAreaX-80),(float)(stage->playAreaY-80));
  #elif defined(VK_BACKEND)
  static_cast<VKBackend&>(engine).setCanvasOffset(
      (float)(stage->playAreaX-80),(float)(stage->playAreaY-80));
  #endif
  engine.drawStringTransparent("        S -- start",(float)xPos,(float)yPos,
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        Q -- quit",(float)xPos,
                               (float)(yPos+=fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        E -- rotate left",(float)xPos,
                               (float)(yPos+=2*fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        R -- rotate right",(float)xPos,
                               (float)(yPos+=fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        O -- thrust",(float)xPos,
                               (float)(yPos+=fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        P -- fire",(float)xPos,
                               (float)(yPos+=fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("space bar -- hyperspace",(float)xPos,
                               (float)(yPos+=fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
   engine.drawStringTransparent("        N -- new game",(float)xPos,
                                 (float)(yPos+=2*fontHeight),
                                 EngineFont_HiScore,1.0f,1.0f,1.0f);
  engine.drawStringTransparent("        H -- help (this screen)",(float)xPos,
                               (float)(yPos+=2*fontHeight),
                               EngineFont_HiScore,1.0f,1.0f,1.0f);
  xastQaTextBlock("help",(float)xPos,
                  (float)(playArea.NorthWestCorner().y
                          +(playArea.Height()-12*fontHeight+4)/2),
                  engine.measureText("        H -- help (this screen)",
                                     EngineFont_HiScore),
                  (float)(12*fontHeight));
  #ifdef GL_BACKEND
  static_cast<GLBackend&>(engine).setCanvasOffset(0.0f,0.0f);
  #elif defined(VK_BACKEND)
  static_cast<VKBackend&>(engine).setCanvasOffset(0.0f,0.0f);
  #endif
   engine.endRenderTo();
   #endif
  }

inline void PlayingField::AlterExtraShipScore(const int newExtraShipScore)
 {nextShipScore=(score->CurrentScore()/newExtraShipScore+1)*newExtraShipScore;
 }

inline void PlayingField::AlterUSecondsPerFrame(const int newUSecondsPerFrame)
 {uSecondsPerFrame=newUSecondsPerFrame;
  speedAdjust=uSecondsPerFrame/125E3;
 }

#ifndef X11_BACKEND
// ---------------------------------------------------------------------------
// Task-44a OptionsMenuHost implementation (D9). Lives HERE because this
// header's include point is where the whole domain chain (groups, bullets,
// rocks) is complete — optionsMenu.* stays domain-free so it can link as its
// own TU under the repo's one-TU-per-link convention. The three operations
// mirror the X11 Motif callbacks statement-for-statement:
//   applyFramesPerSecondPath == Options::AlterFramesPerSecond
//   writePreferencesFile     == Options::WriteOptions
//   readPreferencesFile      == Options::ReadOptions (minus widget updates;
//                               filedShipType parsed but NOT applied — the
//                               icon half of AlterShipShape,
//                               shipYard->AlterIcon, is an X11-guarded body,
//                               so a GL shape swap would leave the yard icon
//                               stale; every numeric value applies).
// Documented deviation: volume/mute are session-scoped menu state (the game
// is silent; no sound domain exists to persist), so the file holds exactly
// the 29 options.H values and nothing else.
// ---------------------------------------------------------------------------
class PlayingFieldOptionsMenuHost : public OptionsMenuHost
 {public:
    inline int canonicalClientWidth() const override
     {FontMetrics titleFm,hiScoreFm,scoreFm;
      int width=0,height=0;
      RecomputeFormula(titleFm,hiScoreFm,scoreFm,width,height);
      return width;
     }
    inline int canonicalClientHeight() const override
     {FontMetrics titleFm,hiScoreFm,scoreFm;
      int width=0,height=0;
      RecomputeFormula(titleFm,hiScoreFm,scoreFm,width,height);
      return height;
     }
    inline int currentFramesPerSecond() const override
     {return int(1E6/PlayingField::uSecondsPerFrame+.5);
     }
    inline void applyFramesPerSecondPath(
                  const double newUSecondsPerFrame) override
     {if (newUSecondsPerFrame==(double)PlayingField::uSecondsPerFrame)
        return;
      double velocityAdjustRatio=
               newUSecondsPerFrame/PlayingField::uSecondsPerFrame,
             scaledMaxVel=PlayingField::maxLinearVelocity*velocityAdjustRatio;

      playingField->AlterUSecondsPerFrame((int)newUSecondsPerFrame);

      AlterGlobalMaxVel(.5*(double)PlayingField::playArea.Height());
      if (!PlayingField::universalGravitationalConst)
        shipGroup->ResetVelocityLimits(.5*(double)PlayingField::playArea.Height());

      shipGroup->AdjustFramesPerSec(velocityAdjustRatio);
      Ship::maxVelocity*=velocityAdjustRatio;
      Ship::acceleration*=velocityAdjustRatio*velocityAdjustRatio;
      Ship::angularVelocity*=velocityAdjustRatio;

      shipBulletGroup->AdjustFramesPerSec(velocityAdjustRatio);
      ShipBullet::velocity*=velocityAdjustRatio;
      ShipBullet::numFrames/=velocityAdjustRatio;

      enemyGroup->AdjustFramesPerSec(velocityAdjustRatio);
      ENEMY::minVelocity*=velocityAdjustRatio;
      ENEMY::maxVelocity*=velocityAdjustRatio;
      enemy::minVelocity*=velocityAdjustRatio;
      enemy::maxVelocity*=velocityAdjustRatio;

      enemyBulletGroup->AdjustFramesPerSec(velocityAdjustRatio);
      EnemyBullet::velocity*=velocityAdjustRatio;
      EnemyBullet::numFrames/=velocityAdjustRatio;

      rockGroup->AdjustFramesPerSec(velocityAdjustRatio);
      ROCK::minVelocity*=velocityAdjustRatio;
      ROCK::maxVelocity*=velocityAdjustRatio;
      ROCK::maxAngularVelocity*=velocityAdjustRatio;

      AlterGlobalMaxVel(scaledMaxVel);
      if (!PlayingField::universalGravitationalConst)
        shipGroup->ResetVelocityLimits(Ship::maxVelocity);
     }
    inline void writePreferencesFile() override
     {std::ofstream ofile(preferencesFileName(),std::ios::out);
      if (!ofile)
       {fprintf(stderr,"OptionsMenuHost: cannot write %s\n",
                preferencesFileName());
        return;
       }
      ofile.setf(std::ios::scientific,std::ios::floatfield);
      ofile.precision(16);
      ofile <<PlayingField::uSecondsPerFrame            <<'\n'
            <<PlayingField::maxLinearVelocity           <<'\n'
            <<PlayingField::universalGravitationalConst <<'\n'
            <<PlayingField::relativisticMass            <<'\n'
            <<(shipGroup->ship==&shipGroup->NCC1701D
                 ? 2
                 : shipGroup->ship==&shipGroup->NCC1701A
                   ? 1
                   : 0)                                 <<'\n'
            <<Ship::maxVelocity                         <<'\n'
            <<Ship::acceleration                        <<'\n'
            <<Ship::angularVelocity                     <<'\n'
            <<Hyper::percentDestructRate                <<'\n'
            <<PlayingField::initialShips                <<'\n'
            <<PlayingField::extraShipScore              <<'\n'
            <<ShipBulletGroup::autoFire                 <<'\n'
            <<ShipBullet::velocity                      <<'\n'
            <<ShipBullet::numFrames                     <<'\n'
            <<ShipBulletGroup::numBullets               <<'\n'
            <<ROCK::maxVelocity                         <<'\n'
            <<ROCK::minVelocity                         <<'\n'
            <<ROCK::maxAngularVelocity                  <<'\n'
            <<RockGroup::initialROCKs                   <<'\n'
            <<RockGroup::ROCKsPerLevel                  <<'\n'
            <<RockGroup::opaqueRocks                    <<'\n'
            <<PlayingField::enemiesExist                <<'\n'
            <<ENEMY::maxVelocity                        <<'\n'
            <<ENEMY::minVelocity                        <<'\n'
            <<enemy::maxVelocity                        <<'\n'
            <<enemy::minVelocity                        <<'\n'
            <<EnemyBullet::velocity                     <<'\n'
            <<EnemyBullet::numFrames                    <<'\n'
            <<EnemyBulletGroup::numBullets              <<'\n';
      if (!ofile.good())
        fprintf(stderr,"OptionsMenuHost: short write on %s\n",
                preferencesFileName());
     }
    inline void readPreferencesFile() override
     {std::ifstream ifile(preferencesFileName(),std::ios::in);
      if (!ifile)
       {fprintf(stderr,"OptionsMenuHost: cannot read %s\n",
                preferencesFileName());
        return;
       }
      double filedGlobalMaxVel,
             filedUniversalGravitationalConstant,
             filedHyperspaceDestructRate,
             filedShipAccel,
             filedShipMaxVel,
             filedShipRotationalVel,
             filedShipBulletVel,
             filedROCKMinVel,
             filedROCKMaxVel,
             filedROCKMaxRotationalVel,
             filedENEMYMinVel,
             filedENEMYMaxVel,
             filedenemyMinVel,
             filedenemyMaxVel,
             filedEnemyBulletVel;
      int filedUSecondsPerFrame,
          filedShipType,
          filedInitialShips,
          filedExtraShipScore,
          filedShipBulletDuration,
          filedNumShipBullets,
          filedInitialROCKs,
          filedNewROCKsPerLevel,
          filedEnemyBulletDuration,
          filedNumEnemyBullets;
      int filedRelativisticMass,
          filedOpaqueRocks,
          filedAutoFire,
          filedEnemiesExist;
      if (!(ifile >>filedUSecondsPerFrame
                  >>filedGlobalMaxVel
                  >>filedUniversalGravitationalConstant
                  >>filedRelativisticMass
                  >>filedShipType
                  >>filedShipMaxVel
                  >>filedShipAccel
                  >>filedShipRotationalVel
                  >>filedHyperspaceDestructRate
                  >>filedInitialShips
                  >>filedExtraShipScore
                  >>filedAutoFire
                  >>filedShipBulletVel
                  >>filedShipBulletDuration
                  >>filedNumShipBullets
                  >>filedROCKMaxVel
                  >>filedROCKMinVel
                  >>filedROCKMaxRotationalVel
                  >>filedInitialROCKs
                  >>filedNewROCKsPerLevel
                  >>filedOpaqueRocks
                  >>filedEnemiesExist
                  >>filedENEMYMaxVel
                  >>filedENEMYMinVel
                  >>filedenemyMaxVel
                  >>filedenemyMinVel
                  >>filedEnemyBulletVel
                  >>filedEnemyBulletDuration
                  >>filedNumEnemyBullets))
       {fprintf(stderr,"OptionsMenuHost: malformed %s\n",
                preferencesFileName());
        return;
       }
      // Apply-if-different, mirroring Options::ReadOptions.
      if (PlayingField::uSecondsPerFrame!=filedUSecondsPerFrame)
        applyFramesPerSecondPath((double)filedUSecondsPerFrame);
      if (PlayingField::maxLinearVelocity!=filedGlobalMaxVel)
        AlterGlobalMaxVel(filedGlobalMaxVel);
      if (PlayingField::universalGravitationalConst
          !=filedUniversalGravitationalConstant)
        PlayingField::universalGravitationalConst=
            filedUniversalGravitationalConstant<-15.0
              ? -15.0
              : filedUniversalGravitationalConstant>15.0
                ? 15.0
                : filedUniversalGravitationalConstant;
      PlayingField::relativisticMass=filedRelativisticMass
                                       ? PlayingField::on
                                       : PlayingField::off;
      if (Ship::maxVelocity!=filedShipMaxVel)
        shipGroup->ResetVelocityLimits(Ship::maxVelocity=
            filedShipMaxVel>PlayingField::maxLinearVelocity
              ? PlayingField::maxLinearVelocity
              : filedShipMaxVel);
      if (Ship::acceleration!=filedShipAccel)
        shipGroup->ResetAccelerationLimits(Ship::acceleration=
            filedShipAccel>PlayingField::maxLinearVelocity
              ? PlayingField::maxLinearVelocity
              : filedShipAccel);
      if (Ship::angularVelocity!=filedShipRotationalVel)
        shipGroup->ResetAngularVelocity(Ship::angularVelocity=
            filedShipRotationalVel
              >shipGroup->ship->ObjectRotator().MaxAngularVelocity()
              ? shipGroup->ship->ObjectRotator().MaxAngularVelocity()
              : filedShipRotationalVel);
      if (Hyper::percentDestructRate!=filedHyperspaceDestructRate)
       {Hyper::percentDestructRate=
            filedHyperspaceDestructRate>100.0?100.0
                                             :filedHyperspaceDestructRate;
        Hyper::destructRate=RAND_MAX_16*Hyper::percentDestructRate/100;
       }
      if (PlayingField::initialShips!=filedInitialShips)
        PlayingField::initialShips=filedInitialShips<1?1:filedInitialShips;
      if (PlayingField::extraShipScore!=filedExtraShipScore)
        playingField->AlterExtraShipScore(PlayingField::extraShipScore=
            filedExtraShipScore<5000
              ? 5000
              : filedExtraShipScore>1000000
                ? 1000000
                : filedExtraShipScore);
      ShipBulletGroup::autoFire=filedAutoFire;
      if (ShipBullet::velocity!=filedShipBulletVel)
       {ShipBullet::velocity=
            filedShipBulletVel>PlayingField::maxLinearVelocity
              ? PlayingField::maxLinearVelocity
              : filedShipBulletVel<0.0?0.0:filedShipBulletVel;
        shipBulletGroup->ResetVelocity(ShipBullet::velocity);
       }
      if (ShipBullet::numFrames!=filedShipBulletDuration)
        shipBulletGroup->ResetBulletDuration(ShipBullet::numFrames=
            filedShipBulletDuration>1000
              ? 1000
              : filedShipBulletDuration<1?1:filedShipBulletDuration);
      ShipBulletGroup::numBullets=filedNumShipBullets;
      if (ROCK::maxVelocity!=filedROCKMaxVel)
       {rockGroup->ResetMaxVelocity(ROCK::maxVelocity=
            filedROCKMaxVel>PlayingField::maxLinearVelocity
              ? PlayingField::maxLinearVelocity
              : filedROCKMaxVel);
        if (ROCK::minVelocity>ROCK::maxVelocity)
          rockGroup->ResetMinVelocity(ROCK::minVelocity=ROCK::maxVelocity);
       }
      if (ROCK::minVelocity!=filedROCKMinVel)
        rockGroup->ResetMinVelocity(ROCK::minVelocity=
            filedROCKMinVel>ROCK::maxVelocity
              ? ROCK::maxVelocity
              : filedROCKMinVel);
      if (ROCK::maxAngularVelocity!=filedROCKMaxRotationalVel)
        rockGroup->ResetMaxAngularVelocity(ROCK::maxAngularVelocity=
            filedROCKMaxRotationalVel
              >PlayingField::maxLinearVelocity/(4*ROCK::scale)
              ? PlayingField::maxLinearVelocity/(4*ROCK::scale)
              : filedROCKMaxRotationalVel);
      RockGroup::initialROCKs=filedInitialROCKs;
      RockGroup::ROCKsPerLevel=filedNewROCKsPerLevel<0
                                  ? 0
                                  : filedNewROCKsPerLevel>8?8
                                                           :filedNewROCKsPerLevel;
      RockGroup::opaqueRocks=filedOpaqueRocks;
      PlayingField::enemiesExist=filedEnemiesExist;
      if (ENEMY::maxVelocity!=filedENEMYMaxVel)
       {enemyGroup->ResetENEMYMaxVelocity(ENEMY::maxVelocity=
            filedENEMYMaxVel>PlayingField::maxLinearVelocity
              ? PlayingField::maxLinearVelocity
              : filedENEMYMaxVel);
        if (ENEMY::minVelocity>ENEMY::maxVelocity)
          enemyGroup->ResetENEMYMinVelocity(ENEMY::minVelocity=
                                              ENEMY::maxVelocity);
       }
      if (ENEMY::minVelocity!=filedENEMYMinVel)
        enemyGroup->ResetENEMYMinVelocity(ENEMY::minVelocity=
            filedENEMYMinVel>ENEMY::maxVelocity
              ? ENEMY::maxVelocity
              : filedENEMYMinVel);
      if (enemy::maxVelocity!=filedenemyMaxVel)
       {enemyGroup->ResetenemyMaxVelocity(enemy::maxVelocity=
            filedenemyMaxVel>PlayingField::maxLinearVelocity
              ? PlayingField::maxLinearVelocity
              : filedenemyMaxVel);
        if (enemy::minVelocity>enemy::maxVelocity)
          enemyGroup->ResetenemyMinVelocity(enemy::minVelocity=
                                              enemy::maxVelocity);
       }
      if (enemy::minVelocity!=filedenemyMinVel)
        enemyGroup->ResetenemyMinVelocity(enemy::minVelocity=
            filedenemyMinVel>enemy::maxVelocity
              ? enemy::maxVelocity
              : filedenemyMinVel);
      if (EnemyBullet::velocity!=filedEnemyBulletVel)
       {EnemyBullet::velocity=
            filedEnemyBulletVel>PlayingField::maxLinearVelocity
              ? PlayingField::maxLinearVelocity
              : filedEnemyBulletVel<0.0?0.0:filedEnemyBulletVel;
        enemyBulletGroup->ResetVelocity(EnemyBullet::velocity);
       }
      if (EnemyBullet::numFrames!=filedEnemyBulletDuration)
        enemyBulletGroup->ResetBulletDuration(EnemyBullet::numFrames=
            filedEnemyBulletDuration>1000
              ? 1000
              : filedEnemyBulletDuration<1?1:filedEnemyBulletDuration);
      EnemyBulletGroup::numBullets=filedNumEnemyBullets;
     }

  private:
    static const char* preferencesFileName()
     {// cwd-relative like score.H's hiScore.data: the QA harness chdirs the
      // game into its scratch work dir, keeping runs hermetic.
      return "XAsteroids.prefs";
     }
    static void RecomputeFormula(FontMetrics& titleFm,FontMetrics& hiScoreFm,
                                 FontMetrics& scoreFm,int& width,int& height)
     {// Same formula, same inputs as the Stage ctor / glBackend pass 2 —
      // one formula, no divergence (task-12 rule). Stage keeps its copies
      // private, so the host recomputes instead of asking.
      playingField->engine.getFontMetrics(EngineFont_Title,titleFm);
      playingField->engine.getFontMetrics(EngineFont_HiScore,hiScoreFm);
      playingField->engine.getFontMetrics(EngineFont_Score,scoreFm);
      WindowSizeFormula::Compute(titleFm,hiScoreFm,scoreFm,
                                 (int)playingField->engine.measureText(
                                     "Asteroids",EngineFont_Title),
                                 PlayingField::playArea.Width(),
                                 PlayingField::playArea.Height(),
                                 shipGroup->maxIconHeight,width,height);
     }
    static void AlterGlobalMaxVel(const double newMV)
     {double newMaxVel=newMV;
      if (newMaxVel>.5*(double)PlayingField::playArea.Height())
        newMaxVel=.5*(double)PlayingField::playArea.Height();
      PlayingField::maxLinearVelocity=newMaxVel;

      if (Ship::maxVelocity>newMaxVel||
          PlayingField::universalGravitationalConst)
       {shipGroup->ResetVelocityLimits(newMaxVel);
        Ship::maxVelocity=newMaxVel;
        if (Ship::acceleration>newMaxVel)
          Ship::acceleration=newMaxVel;
        if (Ship::angularVelocity>
            shipGroup->ship->ObjectRotator().MaxAngularVelocity())
          Ship::angularVelocity=
            shipGroup->ship->ObjectRotator().MaxAngularVelocity();
       }

      shipBulletGroup->ResetVelocityLimits(newMaxVel);
      if (ShipBullet::velocity>newMaxVel)
        ShipBullet::velocity=newMaxVel;

      enemyGroup->ResetVelocityLimits(newMaxVel);
      if (ENEMY::minVelocity>newMaxVel)
        ENEMY::minVelocity=newMaxVel;
      if (ENEMY::maxVelocity>newMaxVel)
        ENEMY::maxVelocity=newMaxVel;
      if (enemy::minVelocity>newMaxVel)
        enemy::minVelocity=newMaxVel;
      if (enemy::maxVelocity>newMaxVel)
        enemy::maxVelocity=newMaxVel;

      enemyBulletGroup->ResetVelocityLimits(newMaxVel);
      if (EnemyBullet::velocity>newMaxVel)
        EnemyBullet::velocity=newMaxVel;

      rockGroup->ResetVelocityLimits(newMaxVel);
      if (ROCK::minVelocity>newMaxVel)
        ROCK::minVelocity=newMaxVel;
      if (ROCK::maxVelocity>newMaxVel)
        ROCK::maxVelocity=newMaxVel;
      if (ROCK::maxAngularVelocity>newMaxVel/(4*ROCK::scale))
        ROCK::maxAngularVelocity=newMaxVel/(4*ROCK::scale);
     }
 };

inline OptionsMenuHost& playingFieldOptionsMenuHost()
 {// Function-local static: constructed on first use (after main() built the
  // globals the host reads through).
  static PlayingFieldOptionsMenuHost host;
  return host;
 }
#endif

#endif
