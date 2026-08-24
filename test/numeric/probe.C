// test/numeric/probe.C — task 45a numeric GOLDEN CAPTURE (pre-task-24 tree).
//
// PURE OBSERVER over the PRISTINE game sources. Zero game-source files are
// modified; this probe is a standalone translation unit that links the real
// repo code (utilities/intersection2d.H, objects/movableObject.H,
// utilities/{vector2d,box,liner,linkedArray}.H, utilities/pixmaps/rotated/*,
// gamePlay/playingField.H web) and hashes the observable numeric state of:
//
//   (1) swept-intersect sort      intersection2d.H:754-763  (Intersect() loop)
//   (2) mid-pass removal semantics shipGroup.H:253-275     (Ship::HitScript
//       intersector-call sequence + :261-265 explosion arithmetic — replicated
//       line-for-line in the fixture because Ship itself is X-bound; see README)
//   (3) pass-count snapshot        intersection2d.H:766-784  (post-pass Miss loops;
//       NOTE: plan text says "shipGroup.H ~:766-784" but shipGroup.H ends at 742 —
//       the region is intersection2d.H's Miss sweep)
//   (4) gravity FP paths           playingField.H:217-240    (CalcGravityAcceleration,
//       zero-distance guard at :232/:239 `distMagSquared ? ... : Vector2d()`)
//       + SetGravityAcceleration() list-summation order (:242-293)
//
// WHITE-BOX ACCESS, disclosed: CalcGravityAcceleration/SetGravityAcceleration are
// private members of PlayingField and a real PlayingField cannot be constructed
// headless (its ctor needs fonts/window/pixmap). This TU compiles the PRISTINE
// headers with `#define private public` (access control only — Itanium ABI lays
// data members out in declaration order regardless of access, and this is a
// single self-consistent TU). Every libstdc++/X11/Motif header is pre-included
// CLEANLY before the define, so no system header is ever parsed with access
// opened. CalcGravityAcceleration reads only its parameters and static members
// (verified by inspection of playingField.H:217-240), so it is invoked on an
// aligned storage block that is never constructed.
//
// Hash = SHA-256 over a canonical little-endian byte serialization (layout
// documented in test/numeric/README.md). Doubles are hashed as raw IEEE-754
// bit patterns — bit-exact drift is what this lane exists to catch.
//
// Determinism: fixed seed (XAST_SEED env override honored, printed), no
// addresses/time/PID ever printed. Two runs must be byte-identical.

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <math.h>
#include <new>
#include <pwd.h>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <vector>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

using namespace std;

// ---- WHITE-BOX ACCESS (probe TU only; game sources untouched) -------------
// Compiled with -fno-access-control (see README): CalcGravityAcceleration is a
// private member of PlayingField and a real PlayingField cannot be constructed
// headless. This include pulls the whole pristine game web: intersection2d.H,
// movableObject.H, rotator.H, liner.H, box.H, ...
#include "../../gamePlay/playingField.H"

// ---------------------------------------------------------------------------
// SHA-256 (compact standalone implementation; FIPS 180-4).
// ---------------------------------------------------------------------------
class Sha256 {
  uint32_t h[8];
  uint64_t lenBits;
  unsigned char buf[64];
  size_t bufLen;
  static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
  void block(const unsigned char* p) {
    static const uint32_t K[64] = {
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t w[64], a,b,c,d,e,f,g,hh,t1,t2;
    for (int i=0;i<16;++i)
      w[i]=(uint32_t)p[i*4]<<24|(uint32_t)p[i*4+1]<<16|(uint32_t)p[i*4+2]<<8|p[i*4+3];
    for (int i=16;i<64;++i) {
      uint32_t s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);
      uint32_t s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);
      w[i]=w[i-16]+s0+w[i-7]+s1;
    }
    a=h[0];b=h[1];c=h[2];d=h[3];e=h[4];f=h[5];g=h[6];hh=h[7];
    for (int i=0;i<64;++i) {
      t1=hh+(rotr(e,6)^rotr(e,11)^rotr(e,25))+((e&f)^(~e&g))+K[i]+w[i];
      t2=(rotr(a,2)^rotr(a,13)^rotr(a,22))+((a&b)^(a&c)^(b&c));
      hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
  }
 public:
  Sha256(): lenBits(0), bufLen(0) {
    h[0]=0x6a09e667;h[1]=0xbb67ae85;h[2]=0x3c6ef372;h[3]=0xa54ff53a;
    h[4]=0x510e527f;h[5]=0x9b05688c;h[6]=0x1f83d9ab;h[7]=0x5be0cd19;
  }
  void update(const void* data, size_t n) {
    const unsigned char* p=(const unsigned char*)data;
    lenBits+=(uint64_t)n*8;
    while (n) {
      size_t take=64-bufLen; if (take>n) take=n;
      memcpy(buf+bufLen,p,take); bufLen+=take; p+=take; n-=take;
      if (bufLen==64) { block(buf); bufLen=0; }
    }
  }
  string hex() {
    unsigned char pad=0x80;
    update(&pad,1);
    lenBits-=8;
    unsigned char z=0;
    while (bufLen!=56) update(&z,1);
    unsigned char lb[8];
    uint64_t lbv=lenBits;
    for (int i=0;i<8;++i) lb[i]=(unsigned char)(lbv>>(56-8*i));
    update(lb,8);
    char out[65];
    for (int i=0;i<8;++i) snprintf(out+i*8,9,"%08x",h[i]);
    return string(out,64);
  }
};

// ---------------------------------------------------------------------------
// Canonical little-endian serialization sink.
// ---------------------------------------------------------------------------
struct Sink {
  string b;
  void u8 (unsigned v)          { b.push_back((char)(v&0xff)); }
  void u32(uint32_t v)          { for (int i=0;i<4;++i) b.push_back((char)((v>>(8*i))&0xff)); }
  void i32(int32_t v)           { u32((uint32_t)v); }
  void f64(double d)            { uint64_t x; memcpy(&x,&d,8);
                                  for (int i=0;i<8;++i) b.push_back((char)((x>>(8*i))&0xff)); }
  void cstr(const char* s)      { b.append(s,strlen(s)+1); } // bytes + NUL
  void vec(const Vector2d& v)   { f64(v.x); f64(v.y); }
};

static double bitsOf(double d) { return d; } // hashing uses raw f64 bits anyway

// ---------------------------------------------------------------------------
// Fixture geometry: three shapes built through the REAL RotVectorData pipeline
// (real X pixmaps + rotated vector tables under Xvfb).
// ---------------------------------------------------------------------------
static const Vector2d triVecs[3]  = { Vector2d(0,-12), Vector2d(11,7), Vector2d(-11,7) };
static const Vector2d sqVecs[4]   = { Vector2d(-9,-9), Vector2d(9,-9), Vector2d(9,9), Vector2d(-9,9) };
static const Vector2d pentVecs[5] = { Vector2d(0,-14), Vector2d(13,-4), Vector2d(8,11),
                                      Vector2d(-8,11), Vector2d(-13,-4) };
enum Shape { SHAPE_TRI=0, SHAPE_SQ=1, SHAPE_PENT=2, SHAPE_COUNT=3 };
static RotVectorData* shapeRVD[SHAPE_COUNT];

static void buildShapes(Display* dpy, Window win) {
  XColor color;
  memset(&color,0,sizeof(color));
  color.flags=DoRed|DoGreen|DoBlue;
  color.red=color.green=color.blue=65535;
  shapeRVD[SHAPE_TRI]  = new RotVectorData(dpy,win,color,triVecs,3);
  shapeRVD[SHAPE_SQ]   = new RotVectorData(dpy,win,color,sqVecs,4);
  shapeRVD[SHAPE_PENT] = new RotVectorData(dpy,win,color,pentVecs,5);
}

// ---------------------------------------------------------------------------
// ProbeObj — minimal MovableObject fixture.
//  - MissScript mirrors the NUMERIC prefix of Ship::MissScript
//    (shipGroup.H:237-238: UpdateAngle + UpdateVelocity) minus XCopyArea.
//  - HitScript replicates the intersector-call sequence of Ship::HitScript
//    (shipGroup.H:256-268): RemoveNonPermeable(this) [mid-pass removal],
//    optional companion RemovePermeable(thrust), the :261-265 explosion box/
//    velocity arithmetic on an explosion analog, then AddPermeable(explosion).
//    The removal machinery executed is the REAL intersection2d.H code.
//  - Everything recorded goes into the per-case hash stream; nothing drawn.
// ---------------------------------------------------------------------------
struct Event { int type; int id; double t; double px, py; }; // type 1=HIT 2=MISS

class ProbeObj : public MovableObject {
public:
  int id;
  char klass;            // 'N' nonPermeable, 'S' selfPermeable, 'P' permeable
  bool shipLike;         // replicate Ship::HitScript sequence when hit
  bool thrusting;
  ProbeObj* thrust;      // companion permeable removed on hit (thrust analog)
  ProbeObj* explosion;   // permeable analog added on hit
  Rotator rotator;
  int missCount, hitCount;

  ProbeObj(int id_, char klass_, RotatorDisplayData* rdd,
           const Vector2d& center, const Vector2d& vel, const Vector2d& acc,
           double maxVel, double angVel)
    : MovableObject(center, 2*rdd->GetRadius(), 2*rdd->GetRadius(), vel, maxVel, acc),
      id(id_), klass(klass_), shipLike(false), thrusting(false),
      thrust(nullptr), explosion(nullptr),
      rotator(rdd, angVel, PlayingField::maxLinearVelocity),
      missCount(0), hitCount(0) {}

  Rotator& ObjectRotator() override { return rotator; }

  void MissScript(Intersector&, const double createTime,
                  const double existTime) override {
    ++missCount;
    gEvents.push_back(Event{2,id,existTime,0,0});
    rotator.UpdateAngle(existTime);          // shipGroup.H:237
    ObjectLiner().UpdateVelocity(existTime); // shipGroup.H:238
  }

  void HitScript(Intersector& intersector, const double createTime,
                 const double existTime, const Vector2d& intersectPoint) override {
    ++hitCount;
    gEvents.push_back(Event{1,id,existTime,intersectPoint.x,intersectPoint.y});
    if (!shipLike) return;
    // ---- replication of shipGroup.H:256-268 (Ship::HitScript) ----
    intersector.RemoveNonPermeable(this);                    // :256 mid-pass removal
    if (thrust && thrusting) intersector.RemovePermeable(thrust); // :257-258
    explosion->CurrentBox().MoveBox(ObjectLiner()            // :261-263
                                    .Move(OldBox().Center(), existTime)
                                    - explosion->CurrentBox().Center());
    ObjectLiner().UpdateVelocity(existTime);                 // :264
    explosion->ObjectLiner().Velocity() = ObjectLiner().Velocity(); // :265
    intersector.AddPermeable(explosion, createTime+existTime); // :268
    // (hyper/dead/score side effects of :259-260/:269-274 are plain integer/
    //  flag assignments with no FP content — not hashed; documented in README.)
  }

  static vector<Event> gEvents;
};
vector<Event> ProbeObj::gEvents;

// ---------------------------------------------------------------------------
// Intersector case runner.
// ---------------------------------------------------------------------------
struct ObjCfg {
  char klass; int shape;
  double cx,cy,vx,vy,ax,ay,maxVel,angVel;
  bool shipLike, thrusting;
};

static uint32_t caseSeedEcho;

static string runIntersectCase(const char* caseId, const vector<ObjCfg>& cfgs,
                               bool withThrustAnalog) {
  ProbeObj::gEvents.clear();
  vector<ProbeObj*> objs;
  int nextId=1;
  int capN=0,capS=0,capP=0;
  for (const ObjCfg& c : cfgs) {
    if (c.klass=='N') ++capN; else if (c.klass=='S') ++capS; else ++capP;
    if (c.shipLike) ++capP;                  // explosion analog joins mid-pass
  }
  if (withThrustAnalog) ++capP;              // thrust analog rides permeable
  LinkedArray<MovableObject*> np(capN>0?capN:1), sp(capS>0?capS:1), pm(capP>0?capP:1);

  ProbeObj* thrustAnalog=nullptr;
  if (withThrustAnalog) {
    thrustAnalog=new ProbeObj(90,'P',shapeRVD[SHAPE_SQ],
                              Vector2d(-500,-500),Vector2d(),Vector2d(),28,0);
    thrustAnalog->thrusting=true;
  }

  Intersector isx(np,sp,pm);

  for (const ObjCfg& c : cfgs) {
    ProbeObj* o=new ProbeObj(nextId++,c.klass,shapeRVD[c.shape],
                             Vector2d(c.cx,c.cy),Vector2d(c.vx,c.vy),
                             Vector2d(c.ax,c.ay),c.maxVel,c.angVel);
    o->shipLike=c.shipLike;
    o->thrusting=c.thrusting;
    if (o->shipLike) {
      o->thrust=thrustAnalog;
      o->explosion=new ProbeObj(100+o->id,'P',shapeRVD[SHAPE_TRI],
                                Vector2d(c.cx,c.cy),Vector2d(),Vector2d(),28,0);
      objs.push_back(o->explosion);
    }
    objs.push_back(o);
    if (c.klass=='N')         isx.AddNonPermeable(o,0);
    else if (c.klass=='S')    isx.AddSelfPermeable(o,0);
    else                      isx.AddPermeable(o,0);
  }
  if (thrustAnalog) {
    objs.push_back(thrustAnalog);
    isx.AddPermeable(thrustAnalog,0);
  }

  isx.Intersect();

  // ---- serialize: inputs, event log (sort/removal/pass order), final state --
  Sink s;
  s.cstr("IX"); s.cstr(caseId); s.u32(caseSeedEcho);
  s.i32((int32_t)cfgs.size());
  for (const ObjCfg& c : cfgs) {
    s.u8((unsigned)c.klass); s.i32(c.shape);
    s.f64(c.cx); s.f64(c.cy); s.f64(c.vx); s.f64(c.vy);
    s.f64(c.ax); s.f64(c.ay); s.f64(c.maxVel); s.f64(c.angVel);
    s.u8(c.shipLike?1:0); s.u8(c.thrusting?1:0);
  }
  s.u8(withThrustAnalog?1:0);
  s.i32((int32_t)ProbeObj::gEvents.size());
  for (const Event& e : ProbeObj::gEvents) {
    s.u8((unsigned)e.type); s.i32(e.id);
    s.f64(e.t); s.f64(e.px); s.f64(e.py);
  }
  s.i32((int32_t)objs.size());
  for (ProbeObj* o : objs) {
    s.i32(o->id); s.u8((unsigned)o->klass);
    // collided is only initialized by SetRemoveData (N/S classes); permeable
    // objects would hash uninitialized memory, so they hash a constant.
    s.u8(o->klass=='P'?0:(o->collided==MovableObject::hit?1:0));
    s.i32(o->missCount); s.i32(o->hitCount);
    s.vec(o->CurrentBox().Center());
    s.vec(o->ObjectLiner().Velocity());
    s.f64(o->createTime);
    s.f64(o->rotator.Angle());
  }
  Sha256 sh;
  sh.update(s.b.data(),s.b.size());
  string digest=sh.hex();

  // ---- human-readable detail -------------------------------------------------
  printf("case %s objects=%d events=%zu\n", caseId,(int)cfgs.size(),
         ProbeObj::gEvents.size());
  for (const Event& e : ProbeObj::gEvents)
    printf("  ev %s id=%d t=%.17g pt=(%.17g,%.17g)\n",
           e.type==1?"HIT":"MISS",e.id,e.t,e.px,e.py);
  for (ProbeObj* o : objs) {
    Vector2d vel=o->ObjectLiner().Velocity(); // LinerVec -> Vector2d conversion
    printf("  st id=%d %c collided=%d miss=%d hit=%d c=(%.17g,%.17g)"
           " v=(%.17g,%.17g)\n",
           o->id,o->klass,
           o->klass=='P'?0:(o->collided==MovableObject::hit?1:0),
           o->missCount,o->hitCount,
           o->CurrentBox().Center().x,o->CurrentBox().Center().y,vel.x,vel.y);
  }
  printf("HASH %s %s\n",caseId,digest.c_str());
  printf("GOLD %s\t%s\n",caseId,digest.c_str());

  for (ProbeObj* o : objs) delete o;
  return digest;
}

// ---------------------------------------------------------------------------
// Gravity helpers (playingField.H:217-240 / :242-293).
// ---------------------------------------------------------------------------
alignas(16) static char pfStorage[sizeof(PlayingField)]; // NEVER constructed;
                                                         // Calc/Set gravity touch
                                                         // no instance members.
// Definitions for the game's global object pointers. The probe never touches
// them (all null); they exist only to satisfy weak vtable references emitted
// from the pristine header web compiled into this standalone TU.
Button* button = nullptr;
Score* score = nullptr;
ShipYard* shipYard = nullptr;
Stage* stage = nullptr;
PlayingField* playingField = nullptr;
EnemyBulletGroup* enemyBulletGroup = nullptr;
EnemyGroup* enemyGroup = nullptr;
ExplosionGraphic* explosionGraphic = nullptr;
RockGroup* rockGroup = nullptr;
ShipBulletGroup* shipBulletGroup = nullptr;
ShipGroup* shipGroup = nullptr; // SetGravityAcceleration-style comparisons hit
                                // only the STATIC ShipGroup::ship (null ->
                                // else-branch always).

static int gAssertsFailed=0;

static void assertFinite(const char* caseId,const char* what,double x,double y) {
  bool ok=isfinite(x)&&isfinite(y);
  if (!ok) ++gAssertsFailed;
  printf("ASSERT %s %s finite=%d (%.17g,%.17g)\n",caseId,what,ok?1:0,x,y);
}

// ---------------------------------------------------------------------------
static const uint32_t DEFAULT_SEED=20260823u; // fixed golden-capture seed

int main() {
  alarm(120); // safety net: no case may hang the capture

  Display* dpy=XOpenDisplay(getenv("DISPLAY"));
  if (!dpy) { fprintf(stderr,"probe: cannot open DISPLAY (run under Xvfb)\n"); return 2; }
  Window win=XCreateSimpleWindow(dpy,DefaultRootWindow(dpy),0,0,64,64,0,0,0);
  buildShapes(dpy,win);

  const char* seedEnv=getenv("XAST_SEED");
  uint32_t seed=seedEnv?(uint32_t)strtoul(seedEnv,0,10):DEFAULT_SEED;
  srand(seed);
  caseSeedEcho=seed;
  printf("seed %u\n",seed);
  printf("sha256 over canonical LE serialization; doubles = raw IEEE-754 bits\n");

  // ===========================================================================
  // SECTION A — swept-intersect sort (:754-763) + mid-pass removal
  //             (shipGroup.H:253-275 replicated) + pass snapshot (:766-784)
  // ===========================================================================
  vector<pair<string,string>> goldens;

  // A1 tangent contact: centers separated by exactly 2*radius (the SAME
  // double value RotatorDisplayData computed), zero velocity -> pins the
  // centerDistMag>radiusSum boundary decision itself.
  {
    double r=shapeRVD[SHAPE_SQ]->GetRadius();
    vector<ObjCfg> c={
      {'N',SHAPE_SQ, r,0, 0,0, 0,0, 28,0, false,false},
      {'N',SHAPE_SQ,-r,0, 0,0, 0,0, 28,0, false,false}};
    goldens.push_back({"A1-tangent-head-on",runIntersectCase("A1-tangent-head-on",c,false)});
  }
  // A1b tangent at t=0 plus symmetric closing drift.
  {
    double r=shapeRVD[SHAPE_SQ]->GetRadius();
    vector<ObjCfg> c={
      {'N',SHAPE_SQ, r,0, -4,0, 0,0, 28,0, false,false},
      {'N',SHAPE_SQ,-r,0,  4,0, 0,0, 28,0, false,false}};
    goldens.push_back({"A1b-tangent-drift",runIntersectCase("A1b-tangent-drift",c,false)});
  }
  // A2 near-miss: separation radiusSum+eps, slow closing -> swept window expires.
  {
    double rSum=2*shapeRVD[SHAPE_SQ]->GetRadius();
    vector<ObjCfg> c={
      {'N',SHAPE_SQ,  rSum/2+1e-6,0,-2,0, 0,0, 28,0, false,false},
      {'N',SHAPE_SQ, -rSum/2,     0, 2,0, 0,0, 28,0, false,false}};
    goldens.push_back({"A2-near-miss",runIntersectCase("A2-near-miss",c,false)});
  }
  // A3 coincident spawn, zero velocity: unit path from state 0.
  {
    vector<ObjCfg> c={
      {'N',SHAPE_TRI,0,0, 0,0, 0,0, 28,0, false,false},
      {'N',SHAPE_TRI,0,0, 0,0, 0,0, 28,0, false,false}};
    goldens.push_back({"A3-coincident-zero-vel",runIntersectCase("A3-coincident-zero-vel",c,false)});
  }
  // A4 zero-relative-velocity parallel drift (boxes overlap in sweep, never close).
  {
    vector<ObjCfg> c={
      {'N',SHAPE_PENT,-30,3, 10,0, 0,0, 28,0, false,false},
      {'N',SHAPE_PENT, 30,3, 10,0, 0,0, 28,0, false,false}};
    goldens.push_back({"A4-zero-rel-vel",runIntersectCase("A4-zero-rel-vel",c,false)});
  }
  // A5 staggered chain: sort order decides which pair fires first; mid-pass
  //    removal of the middle object perturbs iteration (ship-like B removes
  //    itself + adds an explosion analog mid-loop).
  {
    vector<ObjCfg> c={
      {'N',SHAPE_TRI,-26,0, 24,0, 0,0, 28,0, false,false},
      {'N',SHAPE_TRI,  0,0,  0,0, 0,0, 28,0, true, true },
      {'N',SHAPE_TRI, 26,0,-24,0, 0,0, 28,0, false,false}};
    goldens.push_back({"A5-staggered-chain",runIntersectCase("A5-staggered-chain",c,true)});
  }
  // A6 near-simultaneous equal-time hits: qsort tie handling pinned by goldens.
  {
    vector<ObjCfg> c={
      {'N',SHAPE_SQ,-14,  0, 16,0, 0,0, 28,0, false,false},
      {'N',SHAPE_SQ, 14,  0,-16,0, 0,0, 28,0, false,false},
      {'N',SHAPE_SQ,  0,-14, 0,16, 0,0, 28,0, false,false},
      {'N',SHAPE_SQ,  0, 14, 0,-16,0,0, 28,0, false,false}};
    goldens.push_back({"A6-equal-time-ties",runIntersectCase("A6-equal-time-ties",c,false)});
  }
  // A7 deceleration miss: closing pair brakes hard inside CircleIntersector.
  {
    vector<ObjCfg> c={
      {'N',SHAPE_SQ, 22,0,-18,0, 7,0, 28,0, false,false},
      {'N',SHAPE_SQ,-22,0, 18,0,-7,0, 28,0, false,false}};
    goldens.push_back({"A7-decel-miss",runIntersectCase("A7-decel-miss",c,false)});
  }
  // A8 self-permeable pair (bullets-vs-owner class) + permeable bystander.
  {
    vector<ObjCfg> c={
      {'N',SHAPE_TRI,  0,0,  0,0, 0,0, 28,0, false,false},
      {'S',SHAPE_TRI, 14,0, -4,0, 0,0, 28,0, false,false},
      {'P',SHAPE_SQ, 90,90,  0,0, 0,0, 28,0, false,false}};
    goldens.push_back({"A8-selfperm-mix",runIntersectCase("A8-selfperm-mix",c,false)});
  }
  // A9..A30 seeded random configurations (positions/velocities/accelerations/
  // shapes/classes via the REAL gary_rand::rand_16 path, srand(seed) above).
  char idBuf[32];
  for (int k=0;k<22;++k) {
    snprintf(idBuf,sizeof(idBuf),"R%02d-random-intersect",k);
    int n=2+(int)(gary_rand::rand_16()%4); // 2..5 objects
    vector<ObjCfg> c;
    for (int i=0;i<n;++i) {
      ObjCfg o;
      o.klass="NNSSP"[gary_rand::rand_16()%5];
      o.shape=(int)(gary_rand::rand_16()%SHAPE_COUNT);
      o.cx=-40+80.0*gary_rand::rand_16()/RAND_MAX_16;
      o.cy=-30+60.0*gary_rand::rand_16()/RAND_MAX_16;
      o.vx=-18+36.0*gary_rand::rand_16()/RAND_MAX_16;
      o.vy=-18+36.0*gary_rand::rand_16()/RAND_MAX_16;
      o.ax=-6+12.0*gary_rand::rand_16()/RAND_MAX_16;
      o.ay=-6+12.0*gary_rand::rand_16()/RAND_MAX_16;
      o.maxVel=28;
      o.angVel=-0.2+0.4*gary_rand::rand_16()/RAND_MAX_16;
      o.shipLike=(i==0&&k%3==0);
      o.thrusting=o.shipLike&&(k%2==0);
      c.push_back(o);
    }
    goldens.push_back({idBuf,runIntersectCase(idBuf,c,k%4==0)});
  }

  // ===========================================================================
  // SECTION B — pass-count snapshot across consecutive passes (:766-784):
  // every surviving non/self-permeable object gets EXACTLY ONE Miss per pass;
  // permeable objects get exactly one Miss per pass unconditionally.
  // ===========================================================================
  {
    vector<ObjCfg> base={
      {'N',SHAPE_PENT,-120,0, 12,0, 0,0, 28,.2, false,false},
      {'N',SHAPE_PENT, 120,0,-12,0, 0,0, 28,-.2, false,false},
      {'S',SHAPE_SQ,   300,40, 0,0, 0,0, 28,0, false,false},
      {'P',SHAPE_TRI,  -300,-40, 0,0, 0,0, 28,0, false,false}};
    // Re-run one configuration through THREE passes by replaying the same
    // geometry shifted per pass index (Intersect() mutates objects; a fresh
    // case per pass keeps each hash independent while pinning per-pass counts).
    for (int pass=0;pass<3;++pass) {
      snprintf(idBuf,sizeof(idBuf),"B%d-passcount-p%d",pass,pass);
      vector<ObjCfg> c=base;
      for (ObjCfg& o : c) { o.cx+=pass*37.0; o.cy-=pass*11.0; }
      goldens.push_back({idBuf,runIntersectCase(idBuf,c,false)});
    }
  }

  // ===========================================================================
  // SECTION C — gravity FP paths, pairwise (CalcGravityAcceleration direct).
  // ===========================================================================
  {
    PlayingField* pf=reinterpret_cast<PlayingField*>(pfStorage);
    auto gravCase=[&](const char* cid,double G,bool rel,int uspf,
                      ProbeObj& a,ProbeObj& b){
      PlayingField::universalGravitationalConst=G;
      PlayingField::relativisticMass=rel?PlayingField::on:PlayingField::off;
      PlayingField::uSecondsPerFrame=uspf;
      Vector2d g=pf->CalcGravityAcceleration(&a,&b);
      Sink s; s.cstr("GR"); s.cstr(cid); s.u32(seed);
      s.u8(rel?1:0); s.i32(uspf); s.f64(G);
      s.f64(a.CurrentBox().Center().x); s.f64(a.CurrentBox().Center().y);
      s.f64(b.CurrentBox().Center().x); s.f64(b.CurrentBox().Center().y);
      s.f64(a.ObjectRotator().Radius()); s.f64(a.ObjectRotator().Area());
      s.f64(b.ObjectRotator().Radius()); s.f64(b.ObjectRotator().Area());
      s.f64(b.ObjectLiner().VelocityMagnitude());
      s.f64(g.x); s.f64(g.y);
      Sha256 sh; sh.update(s.b.data(),s.b.size());
      string digest=sh.hex();
      printf("case %s G=%.17g rel=%d uspf=%d g=(%.17g,%.17g)\n",
             cid,G,rel?1:0,uspf,g.x,g.y);
      assertFinite(cid,"gravity",g.x,g.y);
      printf("HASH %s %s\n",cid,digest.c_str());
      printf("GOLD %s\t%s\n",cid,digest.c_str());
      goldens.push_back({cid,digest});
    };
    auto mk=[&](int id,int shape,double cx,double cy,double vx=0,double vy=0){
      return new ProbeObj(id,'N',shapeRVD[shape],Vector2d(cx,cy),
                          Vector2d(vx,vy),Vector2d(),28,0);
    };

    // C1 zero-distance guard (:232/:239): SAME point -> exact Vector2d().
    {
      ProbeObj* a=mk(1,SHAPE_SQ,10,10); ProbeObj* b=mk(2,SHAPE_PENT,10,10);
      gravCase("C1-zero-distance-guard",0.5,false,62500,*a,*b);
      Vector2d g=pf->CalcGravityAcceleration(a,b);
      bool guardTaken=(g.x==0.0)&&(g.y==0.0);
      if (!guardTaken) ++gAssertsFailed;
      printf("ASSERT C1 guard-taken=%d\n",guardTaken?1:0);
      delete a; delete b;
    }
    // C2 near-same point (denormal-scale offset) — overlapping branch.
    {
      ProbeObj* a=mk(1,SHAPE_SQ,10,10); ProbeObj* b=mk(2,SHAPE_PENT,10+5e-320,10);
      gravCase("C2-denormal-offset",0.5,false,62500,*a,*b);
      delete a; delete b;
    }
    // C3 overlapping branch (dist < radiusAverage).
    {
      ProbeObj* a=mk(1,SHAPE_SQ,0,0); ProbeObj* b=mk(2,SHAPE_PENT,5,3);
      gravCase("C3-overlap-branch",0.5,false,62500,*a,*b);
      delete a; delete b;
    }
    // C4 boundary dist == radiusAverage (strict `<` sends it inverse-square).
    {
      ProbeObj* a=mk(1,SHAPE_SQ,0,0);
      double rAvg=.5*(a->ObjectRotator().Radius()+shapeRVD[SHAPE_PENT]->GetRadius());
      ProbeObj* b=mk(2,SHAPE_PENT,rAvg,0);
      gravCase("C4-boundary-dist",0.5,false,62500,*a,*b);
      delete a; delete b;
    }
    // C5 far inverse-square branch.
    {
      ProbeObj* a=mk(1,SHAPE_SQ,0,0); ProbeObj* b=mk(2,SHAPE_PENT,180,-95,3,4);
      gravCase("C5-inverse-square",0.5,false,62500,*a,*b);
      delete a; delete b;
    }
    // C6 relativistic mass variant of the overlap branch.
    {
      ProbeObj* a=mk(1,SHAPE_SQ,0,0); ProbeObj* b=mk(2,SHAPE_PENT,5,3,20,7);
      gravCase("C6-relativistic-overlap",0.5,true,62500,*a,*b);
      delete a; delete b;
    }
    // C7 frame-rate scaling variant (uSecondPerFrameRatio = 2).
    {
      ProbeObj* a=mk(1,SHAPE_SQ,0,0); ProbeObj* b=mk(2,SHAPE_PENT,60,8);
      gravCase("C7-uspf-x2",0.5,false,125000,*a,*b);
      delete a; delete b;
    }
    // C8..C15 seeded random pairs.
    for (int k=0;k<8;++k) {
      snprintf(idBuf,sizeof(idBuf),"R%02d-random-gravity",k);
      ProbeObj* a=mk(1,(int)(gary_rand::rand_16()%SHAPE_COUNT),
                     -200+400.0*gary_rand::rand_16()/RAND_MAX_16,
                     -160+320.0*gary_rand::rand_16()/RAND_MAX_16);
      ProbeObj* b=mk(2,(int)(gary_rand::rand_16()%SHAPE_COUNT),
                     -200+400.0*gary_rand::rand_16()/RAND_MAX_16,
                     -160+320.0*gary_rand::rand_16()/RAND_MAX_16,
                     -24+48.0*gary_rand::rand_16()/RAND_MAX_16,
                     -24+48.0*gary_rand::rand_16()/RAND_MAX_16);
      gravCase(idBuf,0.125+gary_rand::rand_16()%2,
               (gary_rand::rand_16()&1)!=0,
               62500*(1+gary_rand::rand_16()%3),*a,*b);
      delete a; delete b;
    }
  }

  // ===========================================================================
  // SECTION D — SetGravityAcceleration full summation (order-sensitive +=).
  // ===========================================================================
  {
    PlayingField* pf=reinterpret_cast<PlayingField*>(pfStorage);
    auto sumCase=[&](const char* cid,double G,vector<ObjCfg> cfgs){
      ProbeObj::gEvents.clear();
      int capN=0,capS=0;
      for (auto& c : cfgs){ if(c.klass=='N')++capN; else ++capS; }
      LinkedArray<MovableObject*> np(capN>0?capN:1),sp(capS>0?capS:1),
                                  pm(1);
      Intersector isx(np,sp,pm); // unused here; lists shared by address only
      vector<ProbeObj*> objs;
      int id=1;
      for (auto& c : cfgs) {
        ProbeObj* o=new ProbeObj(id++,c.klass,shapeRVD[c.shape],
                                 Vector2d(c.cx,c.cy),Vector2d(c.vx,c.vy),
                                 Vector2d(c.ax,c.ay),c.maxVel,c.angVel);
        objs.push_back(o);
        if (c.klass=='N') isx.AddNonPermeable(o,0); else isx.AddSelfPermeable(o,0);
      }
      PlayingField::universalGravitationalConst=G;
      PlayingField::relativisticMass=PlayingField::off;
      PlayingField::uSecondsPerFrame=62500;
      pf->SetGravityAcceleration();
      Sink s; s.cstr("GS"); s.cstr(cid); s.u32(seed); s.f64(G);
      s.i32((int32_t)objs.size());
      printf("case %s G=%.17g n=%d\n",cid,G,(int)objs.size());
      for (ProbeObj* o : objs) {
        const Vector2d& acc=o->ObjectLiner().Acceleration();
        s.i32(o->id); s.vec(acc);
        assertFinite(cid,"summed-accel",acc.x,acc.y);
        printf("  acc id=%d (%.17g,%.17g)\n",o->id,acc.x,acc.y);
      }
      Sha256 sh; sh.update(s.b.data(),s.b.size());
      string digest=sh.hex();
      printf("HASH %s %s\n",cid,digest.c_str());
      printf("GOLD %s\t%s\n",cid,digest.c_str());
      goldens.push_back({cid,digest});
      for (ProbeObj* o : objs) delete o;
    };
    sumCase("D1-triangle-sum",0.5,{
      {'N',SHAPE_SQ,  -80,  0, 0,0, 0,0, 28,0, false,false},
      {'N',SHAPE_PENT, 80,  0, 0,0, 0,0, 28,0, false,false},
      {'N',SHAPE_TRI,   0, 80, 0,0, 0,0, 28,0, false,false}});
    sumCase("D2-five-random-sum",0.75,{
      {'N',SHAPE_TRI,-150,-90, 3, 1, 0,0, 28,.1, false,false},
      {'N',SHAPE_SQ,  110, 40,-2, 4, 0,0, 28,0,  false,false},
      {'N',SHAPE_PENT, 30,-130, 5,-2, 0,0, 28,-.1,false,false},
      {'S',SHAPE_SQ,  -60, 140, 1, 3, 0,0, 28,0,  false,false},
      {'N',SHAPE_PENT,190, 120, -4,-1, 0,0, 28,0,  false,false}});
    sumCase("D3-mixed-list-order",0.5,{
      {'S',SHAPE_TRI,   0,  0, 0,0, 0,0, 28,0, false,false},
      {'N',SHAPE_SQ,   20,  0, 0,0, 0,0, 28,0, false,false},
      {'N',SHAPE_PENT,-20,  0, 0,0, 0,0, 28,0, false,false},
      {'S',SHAPE_SQ,    0, 20, 0,0, 0,0, 28,0, false,false}});
  }

  // ===========================================================================
  // SECTION E — rotator angle sweep (m24 flavor): REAL rotated-vector tables
  // (RotVectorData::GetVecs) + UpdateAngle fmod, over 64 seeded angles.
  // ===========================================================================
  {
    Rotator r(shapeRVD[SHAPE_PENT],0.0,PlayingField::maxLinearVelocity);
    Sink s; s.cstr("ROT"); s.cstr("E1-angle-sweep"); s.u32(seed);
    for (int k=0;k<64;++k) {
      double a=k*(6.28318530717958/64.0)
              +0.001*gary_rand::rand_16()/RAND_MAX_16;
      r.Angle()=a;
      const Vector2d* v=r.GetVecsAtTime(0);
      int n=r.GetNumVecs();
      s.f64(a); s.i32(n);
      for (int i=0;i<n;++i) s.vec(v[i]);
      printf("case E1 angle[%02d]=%.17g v0=(%.17g,%.17g)\n",k,a,v[0].x,v[0].y);
    }
    for (double t : {0.1,1.7,100.0}) {
      r.Angle()=10.0;
      r.UpdateAngle(t);
      s.f64(t); s.f64(r.Angle());
      printf("case E1 update-angle t=%.17g -> %.17g\n",t,r.Angle());
    }
    Sha256 sh; sh.update(s.b.data(),s.b.size());
    string digest=sh.hex();
    printf("HASH E1-angle-sweep %s\n",digest.c_str());
    printf("GOLD E1-angle-sweep\t%s\n",digest.c_str());
    goldens.push_back({"E1-angle-sweep",digest});
  }

  // ===========================================================================
  printf("\nassert-failures %d\ncases %zu\n",gAssertsFailed,goldens.size());
  printf("--- goldens.txt lines ---\n");
  for (auto& g : goldens) printf("%s\t%s\n",g.first.c_str(),g.second.c_str());

  XDestroyWindow(dpy,win);
  XCloseDisplay(dpy);
  return gAssertsFailed?1:0;
}
