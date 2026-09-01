// xbmDecodeSelfTest.C — golden self-test for xbmDecode.H (rendering-abstraction task 26).
//
// Exercises ALL 28 default-build XBM datasets PLUS the 4 _CORP_LOGO_-only files
// (the compile-time swap in rockGroup.H/shipGroup.H), for both variants:
//
//   default-build 28 (no _CORP_LOGO_):
//     bulletScoringIcon 5x5, eightball 51x51, enemyBulletDecor 3x3,
//     enemyDecor 7x3, ENEMYDecor 13x5, enemyScoringIcon 17x7,
//     ENEMYScoringIcon 31x11, explosionCenter 65x65, explosionEdge 65x65,
//     explosionMiddle 65x65, fortytwo 7x21, NCC1701ADecor 19x55,
//     NCC1701AIcon 19x44, NCC1701AThrustDecor 5x9, NCC1701DDecorBottom 23x39,
//     NCC1701DDecorTop 17x19, NCC1701DIcon 23x30, NCC1701DThrustDecor 17x13,
//     peace 51x59, rockScoringIcon 14x14, ROckScoringIcon 28x28,
//     ROCKScoringIcon 40x40, shipBulletDecor 3x3, starDestroyerIcon 17x31,
//     starDestroyerThrustCenter 17x40, starDestroyerThrustEdge 17x40,
//     starDestroyerThrustMiddle 17x40, yinyang 51x59
//   _CORP_LOGO_-only 4:
//     ROCKDecor1 44x19, ROCKDecor2 47x27, ROCKDecor3 74x13,
//     starDestroyerDecor 7x21
//
// Per dataset it asserts decoded w/h against the .xbm's own _width/_height
// constants (and the derived plane sizes), decodes twice to prove
// determinism, and prints one line:
//     sha256 <name> <w> <h> <hex digest of the RGBA8 buffer>
// Variant distinctness: the 4 swapped pairs must hash differently.
//
// Build & run (host utility — pure std-C++, no backend linkage):
//     make xbm-selftest && ./obj/xbmDecodeSelfTest > qa/xbm-golden/goldens.txt
// The same TU also compiles into the GL/VK object legs via `make BACKEND=<B> objects`
// (as obj/<B>/xbmDecodeSelfTest.o) so the decode unit is proven to build WITHOUT
// -DX11_BACKEND.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstddef>

#include "xbmDecode.H"

// All 32 dataset files. Quoted includes resolve relative to THIS file's
// directory, so the TU is location-independent under -c from the repo root.
#include "../../bitmaps/bulletScoringIcon.xbm"
#include "../../bitmaps/eightball.xbm"
#include "../../bitmaps/enemyBulletDecor.xbm"
#include "../../bitmaps/enemyDecor_7x3.xbm"
#include "../../bitmaps/ENEMYDecor_13x5.xbm"
#include "../../bitmaps/enemyScoringIcon_17x7.xbm"
#include "../../bitmaps/ENEMYScoringIcon_31x11.xbm"
#include "../../bitmaps/explosionCenter.xbm"
#include "../../bitmaps/explosionEdge.xbm"
#include "../../bitmaps/explosionMiddle.xbm"
#include "../../bitmaps/fortytwo.xbm"
#include "../../bitmaps/NCC1701ADecor.xbm"
#include "../../bitmaps/NCC1701AIcon.xbm"
#include "../../bitmaps/NCC1701AThrustDecor.xbm"
#include "../../bitmaps/NCC1701DDecorBottom.xbm"
#include "../../bitmaps/NCC1701DDecorTop.xbm"
#include "../../bitmaps/NCC1701DIcon.xbm"
#include "../../bitmaps/NCC1701DThrustDecor.xbm"
#include "../../bitmaps/peace.xbm"
#include "../../bitmaps/ROCKDecor1.xbm"
#include "../../bitmaps/ROCKDecor2.xbm"
#include "../../bitmaps/ROCKDecor3.xbm"
#include "../../bitmaps/rockScoringIcon_14x14.xbm"
#include "../../bitmaps/ROckScoringIcon_28x28.xbm"
#include "../../bitmaps/ROCKScoringIcon_40x40.xbm"
#include "../../bitmaps/shipBulletDecor.xbm"
#include "../../bitmaps/starDestroyerDecor.xbm"
#include "../../bitmaps/starDestroyerIcon.xbm"
#include "../../bitmaps/starDestroyerThrustCenter.xbm"
#include "../../bitmaps/starDestroyerThrustEdge.xbm"
#include "../../bitmaps/starDestroyerThrustMiddle.xbm"
#include "../../bitmaps/yinyang.xbm"

// ---------------------------------------------------------------------------
// Minimal SHA-256 (FIPS 180-4). Test-local infrastructure only — deliberately
// NOT part of xbmDecode.H, which stays a pure decoder. Correctness is pinned
// below by the two standard digests before any golden is emitted.
// ---------------------------------------------------------------------------

class Sha256
 {public:
    Sha256()
     {
        state[0]=0x6a09e667u; state[1]=0xbb67ae85u; state[2]=0x3c6ef372u; state[3]=0xa54ff53au;
        state[4]=0x510e527fu; state[5]=0x9b05688cu; state[6]=0x1f83d9abu; state[7]=0x5be0cd19u;
        bitLen=0; bufLen=0;
     }
    void update(const uint8_t* const p, const std::size_t n)
     {
        bitLen += static_cast<uint64_t>(n) * 8u;
        std::size_t i=0;
        if (bufLen)
         {
            while (bufLen<64 && i<n) buf[bufLen++]=p[i++];
            if (bufLen==64) compress(buf);
         }
        for (; i+64<=n; i+=64) compress(p+i);
        for (; i<n; ++i) buf[bufLen++]=p[i];
     }
    void finish(uint8_t out[32])
     {
        const uint64_t bits=bitLen;
        static const uint8_t pad[72]={0x80};   // rest zero-initialised
        const std::size_t padLen = (bufLen<56) ? (56-bufLen) : (120-bufLen);
        update(pad, padLen);                   // padding excluded from bitLen on purpose:
        bitLen = bits;                         // restore, then length block
        uint8_t lenBlock[8];
        for (int i=0;i<8;++i) lenBlock[i]=static_cast<uint8_t>(bits>>(56-8*i));
        update(lenBlock, 8);
        for (int i=0;i<8;++i)
         {
            out[i*4+0]=static_cast<uint8_t>(state[i]>>24);
            out[i*4+1]=static_cast<uint8_t>(state[i]>>16);
            out[i*4+2]=static_cast<uint8_t>(state[i]>>8);
            out[i*4+3]=static_cast<uint8_t>(state[i]);
         }
     }
  private:
    static uint32_t rotr(const uint32_t v, const int n) { return (v>>n)|(v<<(32-n)); }
    void compress(const uint8_t* const chunk)
     {
        static const uint32_t K[64]={
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
        uint32_t w[64];
        for (int i=0;i<16;++i)
            w[i]=(static_cast<uint32_t>(chunk[i*4])<<24)|(static_cast<uint32_t>(chunk[i*4+1])<<16)|
                 (static_cast<uint32_t>(chunk[i*4+2])<<8)|static_cast<uint32_t>(chunk[i*4+3]);
        for (int i=16;i<64;++i)
         {
            const uint32_t s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);
            const uint32_t s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);
            w[i]=w[i-16]+s0+w[i-7]+s1;
         }
        uint32_t a=state[0],b=state[1],c=state[2],d=state[3],
                 e=state[4],f=state[5],g=state[6],h=state[7];
        for (int i=0;i<64;++i)
         {
            const uint32_t S1=rotr(e,6)^rotr(e,11)^rotr(e,25);
            const uint32_t ch=(e&f)^((~e)&g);
            const uint32_t t1=h+S1+ch+K[i]+w[i];
            const uint32_t S0=rotr(a,2)^rotr(a,13)^rotr(a,22);
            const uint32_t maj=(a&b)^(a&c)^(b&c);
            const uint32_t t2=S0+maj;
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
         }
        state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
        state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
        bufLen=0;
     }
    uint32_t state[8];
    uint64_t bitLen;
    uint8_t buf[64];
    std::size_t bufLen;
 };

static void sha256Hex(const uint8_t* const data, const std::size_t n, char hex[65])
 {
    Sha256 h;
    h.update(data, n);
    uint8_t digest[32];
    h.finish(digest);
    static const char* const digits="0123456789abcdef";
    for (int i=0;i<32;++i)
     {
        hex[i*2]  =digits[digest[i]>>4];
        hex[i*2+1]=digits[digest[i]&15];
     }
    hex[64]='\0';
 }

// ---------------------------------------------------------------------------

#define CHECK(cond, msg) \
    do { if (!(cond)) { std::fprintf(stderr, "SELFTEST FAIL: %s (line %d)\n", msg, __LINE__); return 1; } } while (0)

struct Dataset
 {public:
    const char* name;
    const unsigned char* bits;
    int w;
    int h;
 };

static const Dataset datasets[]={
    {"bulletScoringIcon",          bulletScoringIcon_bits,          bulletScoringIcon_width,          bulletScoringIcon_height},
    {"eightball",                  eightball_bits,                  eightball_width,                  eightball_height},
    {"enemyBulletDecor",           enemyBulletDecor_bits,           enemyBulletDecor_width,           enemyBulletDecor_height},
    {"enemyDecor",                 enemyDecor_bits,                 enemyDecor_width,                 enemyDecor_height},
    {"ENEMYDecor",                 ENEMYDecor_bits,                 ENEMYDecor_width,                 ENEMYDecor_height},
    {"enemyScoringIcon",           enemyScoringIcon_bits,           enemyScoringIcon_width,           enemyScoringIcon_height},
    {"ENEMYScoringIcon",           ENEMYScoringIcon_bits,           ENEMYScoringIcon_width,           ENEMYScoringIcon_height},
    {"explosionCenter",            explosionCenter_bits,            explosionCenter_width,            explosionCenter_height},
    {"explosionEdge",              explosionEdge_bits,              explosionEdge_width,              explosionEdge_height},
    {"explosionMiddle",            explosionMiddle_bits,            explosionMiddle_width,            explosionMiddle_height},
    {"fortytwo",                   fortytwo_bits,                   fortytwo_width,                   fortytwo_height},
    {"NCC1701ADecor",              NCC1701ADecor_bits,              NCC1701ADecor_width,              NCC1701ADecor_height},
    {"NCC1701AIcon",               NCC1701AIcon_bits,               NCC1701AIcon_width,               NCC1701AIcon_height},
    {"NCC1701AThrustDecor",        NCC1701AThrustDecor_bits,        NCC1701AThrustDecor_width,        NCC1701AThrustDecor_height},
    {"NCC1701DDecorBottom",        NCC1701DDecorBottom_bits,        NCC1701DDecorBottom_width,        NCC1701DDecorBottom_height},
    {"NCC1701DDecorTop",           NCC1701DDecorTop_bits,           NCC1701DDecorTop_width,           NCC1701DDecorTop_height},
    {"NCC1701DIcon",               NCC1701DIcon_bits,               NCC1701DIcon_width,               NCC1701DIcon_height},
    {"NCC1701DThrustDecor",        NCC1701DThrustDecor_bits,        NCC1701DThrustDecor_width,        NCC1701DThrustDecor_height},
    {"peace",                      peace_bits,                      peace_width,                      peace_height},
    {"ROCKDecor1",                 ROCKDecor1_bits,                 ROCKDecor1_width,                 ROCKDecor1_height},
    {"ROCKDecor2",                 ROCKDecor2_bits,                 ROCKDecor2_width,                 ROCKDecor2_height},
    {"ROCKDecor3",                 ROCKDecor3_bits,                 ROCKDecor3_width,                 ROCKDecor3_height},
    {"rockScoringIcon",            rockScoringIcon_bits,            rockScoringIcon_width,            rockScoringIcon_height},
    {"ROckScoringIcon",            ROckScoringIcon_bits,            ROckScoringIcon_width,            ROckScoringIcon_height},
    {"ROCKScoringIcon",            ROCKScoringIcon_bits,            ROCKScoringIcon_width,            ROCKScoringIcon_height},
    {"shipBulletDecor",            shipBulletDecor_bits,            shipBulletDecor_width,            shipBulletDecor_height},
    {"starDestroyerDecor",         starDestroyerDecor_bits,         starDestroyerDecor_width,         starDestroyerDecor_height},
    {"starDestroyerIcon",          starDestroyerIcon_bits,          starDestroyerIcon_width,          starDestroyerIcon_height},
    {"starDestroyerThrustCenter",  starDestroyerThrustCenter_bits,  starDestroyerThrustCenter_width,  starDestroyerThrustCenter_height},
    {"starDestroyerThrustEdge",    starDestroyerThrustEdge_bits,    starDestroyerThrustEdge_width,    starDestroyerThrustEdge_height},
    {"starDestroyerThrustMiddle",  starDestroyerThrustMiddle_bits,  starDestroyerThrustMiddle_width,  starDestroyerThrustMiddle_height},
    {"yinyang",                    yinyang_bits,                    yinyang_width,                    yinyang_height},
};

static bool buffersEqual(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
 {return a.size()==b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size())==0);}

int main()
 {
    // SHA-256 implementation pinned to the standard digests first.
    char hex[65];
    sha256Hex(reinterpret_cast<const uint8_t*>(""), 0, hex);
    CHECK(std::strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")==0, "sha256 empty-string vector");
    sha256Hex(reinterpret_cast<const uint8_t*>("abc"), 3, hex);
    CHECK(std::strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")==0, "sha256 'abc' vector");

    const int n=static_cast<int>(sizeof(datasets)/sizeof(datasets[0]));
    CHECK(n==32, "dataset count is 32 (28 default-build + 4 _CORP_LOGO_-only)");

    std::printf("# xbmDecodeSelfTest goldens — sha256 <name> <w> <h> <rgba8 digest>\n");
    for (int i=0;i<n;++i)
     {
        const Dataset& d=datasets[i];

        const DecodedXBM a=decodeXBM(d.bits, d.w, d.h);
        CHECK(a.w==d.w && a.h==d.h, "decoded dimensions equal the .xbm constants");
        CHECK(a.rgba8.size()==static_cast<std::size_t>(d.w)*d.h*4u, "rgba8 plane size");
        CHECK(a.mask1bit.size()==static_cast<std::size_t>(XBRowStride(d.w))*d.h, "mask1bit plane size");

        const DecodedXBM b=decodeXBM(d.bits, d.w, d.h);
        CHECK(buffersEqual(a.rgba8, b.rgba8) && buffersEqual(a.mask1bit, b.mask1bit), "decode is deterministic");

        sha256Hex(a.rgba8.data(), a.rgba8.size(), hex);
        std::printf("sha256 %s %d %d %s\n", d.name, a.w, a.h, hex);
     }

    // Both _CORP_LOGO_ variants decode DISTINCTLY: each swapped pair must differ.
    struct Pair {const char* a; const char* b;};
    static const Pair pairs[]={
        {"ROCKDecor1",   "eightball"},
        {"ROCKDecor2",   "peace"},
        {"ROCKDecor3",   "yinyang"},
        {"starDestroyerDecor", "fortytwo"},
    };
    for (int i=0;i<4;++i)
     {
        DecodedXBM tmpA, tmpB;
        bool foundA=false, foundB=false;
        for (int j=0;j<n;++j)
         {
            if (std::strcmp(datasets[j].name, pairs[i].a)==0) { tmpA=decodeXBM(datasets[j].bits, datasets[j].w, datasets[j].h); foundA=true; }
            if (std::strcmp(datasets[j].name, pairs[i].b)==0) { tmpB=decodeXBM(datasets[j].bits, datasets[j].w, datasets[j].h); foundB=true; }
         }
        CHECK(foundA && foundB, "variant pair members present");
        CHECK(!buffersEqual(tmpA.rgba8, tmpB.rgba8), "_CORP_LOGO_ variant pair decodes distinctly");
     }

    std::printf("datasets %d ok\n", n);
    return 0;
 }
