// test/numeric/angles.C — task 45b numeric lane, components (b)+(c):
//   (c) randomized-angle suite: 500 seeded random angles over the rotator +
//       intersector paths, deterministic under XAST_SEED, golden-hashed.
//   (b) gravity FP guard asserts on constructed same-point / near-same-point
//       states (CalcGravityAcceleration zero-distance guard — currently
//       playingField.H:713-736, guard `distMagSquared ? ... : Vector2d()` at
//       :728/:735; the plan's ":216-228/:222" cite predates the task-36 QA
//       instrumentation block that now occupies those lines).
//
// PORTABLE ACROSS BACKEND COMPILE CONFIGS — this is the X11-vs-GL unit leg:
//   X11 flavor    : -DX11_BACKEND  (real RotVectorData pixmaps; needs DISPLAY)
//   GL-leg flavor : guards-closed, NO backend macro — EXACTLY how the makefile
//                   compiles GAME_OBJECTS into obj/GL (BACKEND_CXXFLAGS=
//                   $(VENDOR_INCS); only XAsteroids.o carries -DGL_BACKEND).
//   Hash equality between the two runs proves the domain float order is
//   identical under both preprocessor configurations.
//
// Why not reuse probe.C for this: probe.C pre-includes the real <X11/Xlib.h>
// before the game web, which conflicts with x11types.H's guards-closed
// anonymous-tag XColor mirror (`typedef struct {...} XColor` vs Xlib's
// tag==typedef-name form). probe.C is the 45a golden-capture artifact and
// stays byte-pristine; this TU instead includes playingField.H FIRST and lets
// x11types.H own the configuration, touching real Xlib only under
// #ifdef X11_BACKEND.
//
// Randomness flows exclusively through the repo's own gary_rand::rand_16()
// (stage.H) after srand(seed); seed = XAST_SEED env override, else the fixed
// default printed on every run. No addresses/time/PID ever printed or hashed.
// Exit code = number of failed asserts.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

using namespace std;

// The game web first — x11types.H resolves the backend configuration and the
// whole pristine header chain (intersection2d.H, movableObject.H, rotator.H,
// liner.H, box.H, vector2d.H, linkedArray.H, ...) comes in with it.
#include "../../gamePlay/playingField.H"

// ---------------------------------------------------------------------------
// SHA-256 (compact standalone implementation; FIPS 180-4) — same construction
// as probe.C's so both lane drivers hash identically-shaped streams.
// ---------------------------------------------------------------------------
class Sha256 {
  uint32_t h[8];
  uint64_t lenBits;
  unsigned char buf[64];
  size_t bufLen;
  static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
  void block(const unsigned char* p) {
    // K table copied BYTE-EXACT from test/numeric/probe.C (the 45a golden
    // construction). NOTE: probe.C's table carries 60 initializers (K[60..63]
    // zero-fill) and deviates from the canonical FIPS 180-4 schedule — it is
    // NOT real SHA-256. Irrelevant to drift detection (capture and diff use
    // the identical construction; the hash only needs determinism), but do
    // not "fix" it here without regenerating every golden.
    static const uint32_t K[64] = {
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,
      0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,
      0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,
      0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0x106aa070,0xf40e3585,0x106aa070,0x19a4c116,
      0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
      0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
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

// Canonical little-endian serialization sink (same conventions as probe.C).
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

// ---------------------------------------------------------------------------
// Fixture geometry: three shapes through the REAL RotVectorData pipeline.
// X11 flavor builds real rotated pixmap tables under DISPLAY; the GL-leg
// (guards-closed) flavor runs the D14 #else data-capture path — same angle
// math, no pixmaps. Hash equality across flavors pins that mirror exactly.
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
// AngleObj — minimal MovableObject fixture over the intersector paths.
// MissScript mirrors the numeric prefix of Ship::MissScript (UpdateAngle +
// UpdateVelocity); HitScript replicates Ship::HitScript's intersector-call
// sequence (RemoveNonPermeable mid-pass + explosion-analog arithmetic +
// AddPermeable) — the REAL intersection2d.H removal machinery executes.
// ---------------------------------------------------------------------------
struct Event { int type; int id; double t; double px, py; }; // type 1=HIT 2=MISS

class AngleObj : public MovableObject {
public:
  int id;
  char klass;            // 'N' nonPermeable, 'S' selfPermeable, 'P' permeable
  bool shipLike;
  bool thrusting;
  AngleObj* thrust;
  AngleObj* explosion;
  Rotator rotator;
  int missCount, hitCount;

  AngleObj(int id_, char klass_, RotatorDisplayData* rdd,
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
    (void)createTime;
    ++missCount;
    gEvents.push_back(Event{2,id,existTime,0,0});
    rotator.UpdateAngle(existTime);
    ObjectLiner().UpdateVelocity(existTime);
  }

  void HitScript(Intersector& intersector, const double createTime,
                 const double existTime, const Vector2d& intersectPoint) override {
    ++hitCount;
    gEvents.push_back(Event{1,id,existTime,intersectPoint.x,intersectPoint.y});
    if (!shipLike) return;
    // ---- replication of Ship::HitScript's numeric prefix ----
    intersector.RemoveNonPermeable(this);
    if (thrust && thrusting) intersector.RemovePermeable(thrust);
    explosion->CurrentBox().MoveBox(ObjectLiner()
                                    .Move(OldBox().Center(), existTime)
                                    - explosion->CurrentBox().Center());
    ObjectLiner().UpdateVelocity(existTime);
    explosion->ObjectLiner().Velocity() = ObjectLiner().Velocity();
    intersector.AddPermeable(explosion, createTime+existTime);
  }

  static vector<Event> gEvents;
};
vector<Event> AngleObj::gEvents;

// Definitions for the game's global object pointers (null; only satisfy weak
// references emitted from the header web compiled into this standalone TU —
// probe.C precedent).
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
ShipGroup* shipGroup = nullptr;

alignas(16) static char pfStorage[sizeof(PlayingField)]; // NEVER constructed;
                                                         // Calc/Set gravity
                                                         // touch no instance
                                                         // members.

static int gAssertsFailed=0;

static void assertFinite(const char* what,double x,double y) {
  const bool ok=isfinite(x)&&isfinite(y);
  if (!ok) ++gAssertsFailed;
  printf("ASSERT %s finite=%d (%.17g,%.17g)\n",what,ok?1:0,x,y);
}

static const uint32_t DEFAULT_SEED=20260825u; // 45b suite pin (distinct from
                                              // 45a's capture seed)
static const int NUM_ANGLES=500;

int main() {
  alarm(120); // safety net: the suite must terminate

#ifdef X11_BACKEND
  Display* dpy=XOpenDisplay(getenv("DISPLAY"));
  if (!dpy) { fprintf(stderr,"angles: cannot open DISPLAY (run under Xvfb)\n"); return 2; }
  Window win=XCreateSimpleWindow(dpy,DefaultRootWindow(dpy),0,0,64,64,0,0,0);
#else
  // Guards-closed (GL-leg domain config): RotVectorData is a pure data
  // capturer — display/window arguments are ignored (unnamed parameters in
  // the #else branch), null/0 are safe.
  Display* dpy=nullptr;
  Window win=0;
#endif
  buildShapes(dpy,win);

  const char* seedEnv=getenv("XAST_SEED");
  uint32_t seed=seedEnv?(uint32_t)strtoul(seedEnv,0,10):DEFAULT_SEED;
  srand(seed);
  printf("seed %u\n",seed);
  printf("flavor %s\n",
#ifdef X11_BACKEND
         "X11"
#else
         "GUARDS-CLOSED(GL-leg-domain-config)"
#endif
         );

  Sink s;
  s.cstr("F500"); s.u32(seed); s.i32(NUM_ANGLES);

  // ===========================================================================
  // SECTION F — 500 seeded random angles over rotator + intersector paths.
  // ===========================================================================
  Rotator sweepRot(shapeRVD[SHAPE_PENT],0.0,PlayingField::maxLinearVelocity);
  char idBuf[32];
  for (int k=0;k<NUM_ANGLES;++k) {
    snprintf(idBuf,sizeof(idBuf),"F%03d",k);
    // --- the random angle itself (repo RNG path exclusively) ---------------
    const double ang=6.28318530717958*(double)gary_rand::rand_16()/RAND_MAX_16;
    const double dirX=cos(ang), dirY=sin(ang);

    // --- rotator path: real rotated-vector tables at this angle ------------
    for (int sh=0;sh<SHAPE_COUNT;++sh) {
      sweepRot.Angle()=ang+(double)sh*0.001;
      const Vector2d* v=sweepRot.GetVecsAtTime(0);
      const int n=sweepRot.GetNumVecs();
      s.f64(sweepRot.Angle()); s.i32(n);
      for (int i=0;i<n;++i) s.vec(v[i]);
    }
    // fmod wrap check at a random time offset.
    const double updT=100.0*(double)gary_rand::rand_16()/RAND_MAX_16;
    sweepRot.Angle()=ang;
    sweepRot.UpdateAngle(updT);
    s.f64(updT); s.f64(sweepRot.Angle());

    // --- intersector path: closing pair along the angle axis ---------------
    AngleObj::gEvents.clear();
    const double r0=10.0+30.0*(double)gary_rand::rand_16()/RAND_MAX_16;
    const double spd= 2.0+18.0*(double)gary_rand::rand_16()/RAND_MAX_16;
    const double perp=(k%5==0)? 0.75*(double)(gary_rand::rand_16()%32)/16.0
                              : 0.0; // occasional near-miss geometry
    const double angVel=-0.2+0.4*(double)gary_rand::rand_16()/RAND_MAX_16;
    const char ka="NNSSP"[k%5];
    const char kb=(k%3==0)?'P':'N';
    const bool shipLike=(k%9==0);
    const bool thrusting=(k%18==0);

    const Vector2d cA( dirX*r0,          dirY*r0+perp);
    const Vector2d cB(-dirX*r0,         -dirY*r0-perp);
    const Vector2d vA(-dirX*spd,-dirY*spd);
    const Vector2d vB( dirX*spd, dirY*spd);

    int capN=0,capS=0,capP=0;
    if (ka=='N') ++capN; else if (ka=='S') ++capS; else ++capP;
    if (kb=='N') ++capN; else if (kb=='S') ++capS; else ++capP;
    if (shipLike) ++capP;                    // explosion analog joins mid-pass
    if (thrusting) ++capP;                   // thrust analog rides permeable
    LinkedArray<MovableObject*> np(capN>0?capN:1),sp(capS>0?capS:1),
                                pm(capP>0?capP:1);
    Intersector isx(np,sp,pm);

    AngleObj* thrustAnalog=nullptr;
    if (thrusting) {
      thrustAnalog=new AngleObj(90,'P',shapeRVD[SHAPE_SQ],
                                Vector2d(-500,-500),Vector2d(),Vector2d(),28,0);
      thrustAnalog->thrusting=true;
    }

    vector<AngleObj*> objs;
    AngleObj* A=new AngleObj(1,ka,shapeRVD[k%SHAPE_COUNT],cA,vA,
                             Vector2d(),28,angVel);
    AngleObj* B=new AngleObj(2,kb,shapeRVD[(k+1)%SHAPE_COUNT],cB,vB,
                             Vector2d(),28,-angVel);
    A->shipLike=shipLike;
    A->thrusting=thrusting;
    if (shipLike) {
      A->thrust=thrustAnalog;
      A->explosion=new AngleObj(101,'P',shapeRVD[SHAPE_TRI],
                                cA,Vector2d(),Vector2d(),28,0);
      objs.push_back(A->explosion);
    }
    objs.push_back(A); objs.push_back(B);
    if (ka=='N') isx.AddNonPermeable(A,0);
    else if (ka=='S') isx.AddSelfPermeable(A,0);
    else isx.AddPermeable(A,0);
    if (kb=='N') isx.AddNonPermeable(B,0);
    else if (kb=='S') isx.AddSelfPermeable(B,0);
    else isx.AddPermeable(B,0);
    if (thrustAnalog) { objs.push_back(thrustAnalog); isx.AddPermeable(thrustAnalog,0); }

    isx.Intersect();

    // --- serialize this case (IX-style stream, one big F-suite sink) -------
    s.cstr(idBuf);
    s.f64(ang); s.f64(r0); s.f64(spd); s.f64(perp); s.f64(angVel);
    s.u8((unsigned)ka); s.u8((unsigned)kb);
    s.i32(k%SHAPE_COUNT); s.i32((k+1)%SHAPE_COUNT);
    s.u8(shipLike?1:0); s.u8(thrusting?1:0);
    s.i32((int32_t)AngleObj::gEvents.size());
    for (const Event& e : AngleObj::gEvents) {
      s.u8((unsigned)e.type); s.i32(e.id);
      s.f64(e.t); s.f64(e.px); s.f64(e.py);
    }
    s.i32((int32_t)objs.size());
    for (AngleObj* o : objs) {
      s.i32(o->id); s.u8((unsigned)o->klass);
      // collided is only initialized for N/S classes (SetRemoveData);
      // permeable objects hash a constant instead of uninitialized memory.
      s.u8(o->klass=='P'?0:(o->collided==MovableObject::hit?1:0));
      s.i32(o->missCount); s.i32(o->hitCount);
      s.vec(o->CurrentBox().Center());
      s.vec(o->ObjectLiner().Velocity());
      s.f64(o->createTime);
      s.f64(o->rotator.Angle());
      assertFinite(idBuf,o->CurrentBox().Center().x,o->CurrentBox().Center().y);
    }

    printf("case %s ang=%.17g events=%zu\n",idBuf,ang,AngleObj::gEvents.size());
    for (AngleObj* o : objs) delete o;
  }

  // ===========================================================================
  // SECTION G — gravity FP guard asserts on constructed states (component b).
  // Real private member function via -fno-access-control on a never-
  // constructed PlayingField storage block (probe.C/README disclosure).
  // ===========================================================================
  {
    PlayingField* pf=reinterpret_cast<PlayingField*>(pfStorage);
    PlayingField::universalGravitationalConst=0.5;
    PlayingField::relativisticMass=PlayingField::off;
    PlayingField::uSecondsPerFrame=62500;
    AngleObj* a=new AngleObj(1,'N',shapeRVD[SHAPE_SQ],
                             Vector2d(10,10),Vector2d(),Vector2d(),28,0);
    // G1 EXACT same-point state: the zero-distance guard must be taken and
    // emit exactly Vector2d() — no NaN/Inf, no divide-by-zero stall.
    AngleObj* b=new AngleObj(2,'P',shapeRVD[SHAPE_PENT],
                             Vector2d(10,10),Vector2d(),Vector2d(),28,0);
    Vector2d g=pf->CalcGravityAcceleration(a,b);
    assertFinite("G1-same-point",g.x,g.y);
    const bool guardTaken=(g.x==0.0)&&(g.y==0.0);
    if (!guardTaken) ++gAssertsFailed;
    printf("ASSERT %s guard-taken=%d\n","G1-same-point",guardTaken?1:0);
    s.u8(guardTaken?1:0); s.vec(g);
    delete b;
    // G2 near-same point (denormal-scale offset): denormal-offset squares to
    // zero → the SAME guard covers it; result must be exactly Vector2d().
    AngleObj* c=new AngleObj(3,'P',shapeRVD[SHAPE_PENT],
                             Vector2d(10+5e-320,10),Vector2d(),Vector2d(),28,0);
    g=pf->CalcGravityAcceleration(a,c);
    assertFinite("G2-denormal-offset",g.x,g.y);
    const bool guardTaken2=(g.x==0.0)&&(g.y==0.0);
    if (!guardTaken2) ++gAssertsFailed;
    printf("ASSERT %s guard-taken=%d\n","G2-denormal-offset",guardTaken2?1:0);
    s.u8(guardTaken2?1:0); s.vec(g);
    delete c;
    // G3 seeded random pairs: every result finite (no NaN/Inf anywhere).
    for (int k=0;k<32;++k) {
      AngleObj* p=new AngleObj(4,'N',shapeRVD[k%SHAPE_COUNT],
                               Vector2d(-200+400.0*gary_rand::rand_16()/RAND_MAX_16,
                                        -160+320.0*gary_rand::rand_16()/RAND_MAX_16),
                               Vector2d(),Vector2d(),28,0);
      AngleObj* q=new AngleObj(5,'N',shapeRVD[(k+1)%SHAPE_COUNT],
                               Vector2d(-200+400.0*gary_rand::rand_16()/RAND_MAX_16,
                                        -160+320.0*gary_rand::rand_16()/RAND_MAX_16),
                               Vector2d(-24+48.0*gary_rand::rand_16()/RAND_MAX_16,
                                        -24+48.0*gary_rand::rand_16()/RAND_MAX_16),
                               Vector2d(),28,0);
      g=pf->CalcGravityAcceleration(p,q);
      assertFinite("G3-random-pair",g.x,g.y);
      s.vec(g);
      delete p; delete q;
    }
    delete a;
  }

  Sha256 sh;
  sh.update(s.b.data(),s.b.size());
  const string digest=sh.hex();
  printf("HASH F1-500-angle-suite %s\n",digest.c_str());
  printf("GOLD F1-500-angle-suite\t%s\n",digest.c_str());

  printf("\nassert-failures %d\n",gAssertsFailed);

#ifdef X11_BACKEND
  XDestroyWindow(dpy,win);
  XCloseDisplay(dpy);
#endif
  return gAssertsFailed?1:0;
}
