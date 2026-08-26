// X11 leg of the task-27 5-frame composite assertion probe.
//
// Runs the REAL guarded CompositePixmap pipeline (this file is compiled with
// the backend macro, so compositePixmap.C's guarded implementations are the
// ones linked) against a short-lived Xvfb server, building the same 5
// explosion frames with the same stack specification as probeCPU.C. Each
// frame is read back with XGetImage and normalized to R,G,B,A bytes through
// the visual's channel masks (alpha = painted vs background pixel), then
// dumped in probeCPU's exact format for byte comparison in run.sh.
//
// Usage: DISPLAY=:100 probeX11 <output-dir>

#include <X11/Xlib.h>
#include <X11/Xutil.h>   // XGetImage/XGetPixel/XDestroyImage (was transitive pre-row-47)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include"../../utilities/pixmaps/composite/compositePixmap.H"
#include"../../bitmaps/explosionEdge.xbm"
#include"../../bitmaps/explosionMiddle.xbm"
#include"../../bitmaps/explosionCenter.xbm"

static void dumpImage(const char* const dir, const char* const name,
                      Display* display, const Pixmap& pixmap,
                      const int width, const int height)
 {XImage* image=XGetImage(display,pixmap,0,0,width,height,AllPlanes,ZPixmap);
  if (!image)
   {fprintf(stderr,"XGetImage failed for %s\n",name);
    exit(2);
   }
  const Visual* const visual=DefaultVisual(display,DefaultScreen(display));
  const unsigned long masks[3]={visual->red_mask,visual->green_mask,visual->blue_mask};
  int shifts[3], bits[3];
  for(int c=0;c<3;++c)
   {unsigned long m=masks[c];
    int s=0;
    while (m && !(m&1u))
     {++s;
      m>>=1u;
     }
    shifts[c]=s;
    int b=0;
    for(;m;m>>=1u)
      if (m&1u)
        ++b;
    bits[c]=b;
   }

  const unsigned long backgroundPixel=BlackPixel(display,DefaultScreen(display));
  std::vector<uint8_t> pixels(static_cast<std::size_t>(width)*height*4u);
  for(int j=0;j<height;++j)
    for(int i=0;i<width;++i)
     {const unsigned long pixel=XGetPixel(image,i,j);
       uint8_t* const dst=&pixels[(static_cast<std::size_t>(j)*width+i)*4u];
       for(int c=0;c<3;++c)
        {const unsigned long maxval=(1ul<<bits[c])-1ul;
         const unsigned long v=(pixel&masks[c])>>shifts[c];
         dst[c]=static_cast<uint8_t>((v*255ul+maxval/2ul)/maxval);
        }
       dst[3]=(pixel!=backgroundPixel)?255u:0u;
     }
  XDestroyImage(image);

  char path[512];
  snprintf(path,sizeof(path),"%s/%s.bin",dir,name);
  FILE* f=fopen(path,"wb");
  if (!f)
   {perror(path);
    exit(2);
   }
  int32_t dims[2]={width,height};
  fwrite(dims,sizeof(dims),1,f);
  fwrite(pixels.data(),1,pixels.size(),f);
  fclose(f);
 }


// Task 47 API adaptation (final-wave remediation): the D14 pipeline
// constructors take the injected engine and source their server handles from
// its nativeHandle() — same seam test/numeric/probe.C uses. This stub
// surfaces the probe's own X11 connection; the CompositePixmap server
// requests are unchanged.
namespace {
struct ProbeEngine final : RenderingEngine {
  Display* dpy; Window win;
  ProbeEngine(Display* d, Window w): dpy(d), win(w) {}
  bool initWindow(const char*) override { return true; }
  void shutdown() override {}
  X11NativeHandle nativeHandle() const override { return { (void*)dpy, (unsigned long)win }; }
  void beginFrame() override {} void endFrame() override {}
  int pollEvents(GameEvent*, int) override { return 0; }
  void setScissorRect(const int*) override {}
  RenderTargetId createRenderTarget(int,int) override { return 0; }
  void beginRenderTo(RenderTargetId) override {} void endRenderTo() override {}
  void getPresentTransform(float& s,int& ox,int& oy) override { s=1.0f; ox=0; oy=0; }
  void clear() override {}
  void drawLine(float,float,float,float,float,float,float,float) override {}
  void drawPolygon(const float*,int,float,float,float,bool) override {}
  void drawRect(float,float,float,float,float,float,float,bool) override {}
  void drawTriangles(const float*,int,TextureId) override {}
  void drawStringOpaque(const char*,float,float,int,float,float,float,float,float,float) override {}
  void drawStringTransparent(const char*,float,float,int,float,float,float) override {}
  float measureText(const char*,int) override { return 0.0f; }
  void getFontMetrics(int,FontMetrics&) override {}
  TextureId createTextureFromBitmap(const uint8_t*,int,int,int) override { return 0; }
  void drawTexture(TextureId,float,float,float,float,float) override {}
  void drawTextureMasked(TextureId,TextureId,float,float,float,float) override {}
  void deleteTexture(TextureId) override {}
  TextureId createTextureFromRGBA32(const uint8_t*,int,int) override { return 0; }
  void setTransform(float,float,float) override {} void resetTransform() override {}
};
}

int main(int argc, char** argv)
 {if (argc!=2)
   {fprintf(stderr,"usage: probeX11 <output-dir>\n");
    return 2;
   }
  const char* const dir=argv[1];

  const char* const displayName=getenv("DISPLAY") ? getenv("DISPLAY") : ":100";
  Display* const display=XOpenDisplay(displayName);
  if (!display)
   {fprintf(stderr,"probeX11: cannot open display %s\n",displayName);
    return 2;
   }
  const int screen=DefaultScreen(display);

  const RotColor hottest={65535,65535,65535},
                 hotter={65535,65535,0},
                 hot={65535,42405,0};
  ProbeEngine engine(display,RootWindow(display,screen));

  // Same construction sequence as the explosion graphic's guarded block:
  // every canvas is the edge bitmap's size; layers added in its order.
  {CompositePixmap f0(engine,explosionEdge_width,explosionEdge_height);
   f0.AddBitmapData(explosionCenter_bits,explosionCenter_width,explosionCenter_height,hottest);
   dumpImage(dir,"x11_frame0",display,f0.GetPixmap(),explosionEdge_width,explosionEdge_height);
  }
  {CompositePixmap f1(engine,explosionEdge_width,explosionEdge_height);
   f1.AddBitmapData(explosionMiddle_bits,explosionMiddle_width,explosionMiddle_height,hotter)
     .AddBitmapData(explosionCenter_bits,explosionCenter_width,explosionCenter_height,hottest);
   dumpImage(dir,"x11_frame1",display,f1.GetPixmap(),explosionEdge_width,explosionEdge_height);
  }
  {CompositePixmap f2(engine,explosionEdge_width,explosionEdge_height);
   f2.AddBitmapData(explosionEdge_bits,explosionEdge_width,explosionEdge_height,hot)
     .AddBitmapData(explosionMiddle_bits,explosionMiddle_width,explosionMiddle_height,hotter)
     .AddBitmapData(explosionCenter_bits,explosionCenter_width,explosionCenter_height,hottest);
   dumpImage(dir,"x11_frame2",display,f2.GetPixmap(),explosionEdge_width,explosionEdge_height);
  }
  {CompositePixmap f3(engine,explosionEdge_width,explosionEdge_height);
   f3.AddBitmapData(explosionEdge_bits,explosionEdge_width,explosionEdge_height,hot)
     .AddBitmapData(explosionMiddle_bits,explosionMiddle_width,explosionMiddle_height,hotter)
     .AddBitmapData(explosionCenter_bits,explosionCenter_width,explosionCenter_height,hottest);
   dumpImage(dir,"x11_frame3",display,f3.GetPixmap(),explosionEdge_width,explosionEdge_height);
  }
  {CompositePixmap f4(engine,explosionEdge_width,explosionEdge_height);
   f4.AddBitmapData(explosionMiddle_bits,explosionMiddle_width,explosionMiddle_height,hot)
     .AddBitmapData(explosionCenter_bits,explosionCenter_width,explosionCenter_height,hotter);
   dumpImage(dir,"x11_frame4",display,f4.GetPixmap(),explosionEdge_width,explosionEdge_height);
   }

  XCloseDisplay(display);
  printf("probeX11: dumped 5 frames to %s\n",dir);
  return 0;
 }
