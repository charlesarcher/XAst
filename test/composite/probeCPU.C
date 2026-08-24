// CPU leg of the task-27 5-frame composite assertion probe.
//
// Builds the 5 explosion composite frames with the CPU compositing path
// (compositePixmap.H's `#else` engine branch, compiled guards-closed) using
// the exact stack specification of the explosion graphic: 65x65 canvases,
// layers edge/middle/center with colors hot/hotter/hottest per frame
// (frame 4 swaps the middle/center colors). Dumps each frame as
//   int32 width, int32 height, then width*height*4 bytes R,G,B,A
// plus the three expanded R8 mask planes. The X11 twin (probeX11.C) dumps
// the identical format from the real guarded pipeline; run.sh byte-compares.
//
// Usage: probeCPU <output-dir>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include"../../utilities/pixmaps/composite/compositePixmap.H"
#include"../../bitmaps/explosionEdge.xbm"
#include"../../bitmaps/explosionMiddle.xbm"
#include"../../bitmaps/explosionCenter.xbm"

static void dumpFrame(const char* const dir, const char* const name,
                      const CompositeFrameRGBA8& frame)
 {char path[512];
  snprintf(path,sizeof(path),"%s/%s.bin",dir,name);
  FILE* f=fopen(path,"wb");
  if (!f)
   {perror(path);
    exit(2);
   }
  int32_t dims[2]={frame.width,frame.height};
  fwrite(dims,sizeof(dims),1,f);
  fwrite(frame.rgba8.data(),1,frame.rgba8.size(),f);
  fclose(f);
 }

static void dumpMaskR8(const char* const dir, const char* const name,
                       const unsigned char* const bits,
                       const int width, const int height)
 {std::vector<uint8_t> expanded;
  compositeMaskExpandR8(bits,width,height,expanded);
  char path[512];
  snprintf(path,sizeof(path),"%s/%s.bin",dir,name);
  FILE* f=fopen(path,"wb");
  if (!f)
   {perror(path);
    exit(2);
   }
  int32_t dims[2]={width,height};
  fwrite(dims,sizeof(dims),1,f);
  fwrite(expanded.data(),1,expanded.size(),f);
  fclose(f);
 }

int main(int argc, char** argv)
 {if (argc!=2)
   {fprintf(stderr,"usage: probeCPU <output-dir>\n");
    return 2;
   }
  const char* const dir=argv[1];

  const unsigned short hottestR=65535u, hottestG=65535u, hottestB=65535u;
  const unsigned short hotterR=65535u,  hotterG=65535u,  hotterB=0u;
  const unsigned short hotR=65535u,     hotG=42405u,     hotB=0u;

  // Frame stacks exactly as the explosion graphic wires them; every canvas
  // is the edge bitmap's size.
  const int W=explosionEdge_width,
            H=explosionEdge_height;
  CompositeLayerSpec stacks[5][3]=
   {{{explosionCenter_bits,explosionCenter_width,explosionCenter_height,hottestR,hottestG,hottestB},
     {0,0,0,0,0,0},
     {0,0,0,0,0,0}},
    {{explosionMiddle_bits,explosionMiddle_width,explosionMiddle_height,hotterR,hotterG,hotterB},
     {explosionCenter_bits,explosionCenter_width,explosionCenter_height,hottestR,hottestG,hottestB},
     {0,0,0,0,0,0}},
    {{explosionEdge_bits,explosionEdge_width,explosionEdge_height,hotR,hotG,hotB},
     {explosionMiddle_bits,explosionMiddle_width,explosionMiddle_height,hotterR,hotterG,hotterB},
     {explosionCenter_bits,explosionCenter_width,explosionCenter_height,hottestR,hottestG,hottestB}},
    {{explosionEdge_bits,explosionEdge_width,explosionEdge_height,hotR,hotG,hotB},
     {explosionMiddle_bits,explosionMiddle_width,explosionMiddle_height,hotterR,hotterG,hotterB},
     {explosionCenter_bits,explosionCenter_width,explosionCenter_height,hottestR,hottestG,hottestB}},
    {{explosionMiddle_bits,explosionMiddle_width,explosionMiddle_height,hotR,hotG,hotB},
     {explosionCenter_bits,explosionCenter_width,explosionCenter_height,hotterR,hotterG,hotterB},
     {0,0,0,0,0,0}}};
  const int layerCount[5]={1,2,3,3,2};

  for(int i=0;i<5;++i)
   {CompositeFrameRGBA8 frame;
    compositeFrameStack(frame,W,H,stacks[i],layerCount[i]);
    char name[32];
    snprintf(name,sizeof(name),"cpu_frame%d",i);
    dumpFrame(dir,name,frame);
   }

  dumpMaskR8(dir,"cpu_mask_center",explosionCenter_bits,explosionCenter_width,explosionCenter_height);
  dumpMaskR8(dir,"cpu_mask_middle",explosionMiddle_bits,explosionMiddle_width,explosionMiddle_height);
  dumpMaskR8(dir,"cpu_mask_edge",explosionEdge_bits,explosionEdge_width,explosionEdge_height);

  printf("probeCPU: dumped 5 frames + 3 masks to %s\n",dir);
  return 0;
 }
