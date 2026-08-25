// test/vk/vkpipe.C — task-41 pipeline proof: line/triangle/outline/textured
// pipelines + thick-line quad geometry + dynamic MVP transform.
//
// Phase A (pipeline variants, one frame, multiple binds): thin line (line
//          pipeline), filled triangle + thick line (triangle pipeline),
//          outlined square (outline pipeline), textured checker quad +
//          alpha-0.5 quad + drawTriangles window-space path (textured
//          pipeline). Readback asserts per-shape colors at probe pixels.
// Phase B (thick-line geometry): width-3 horizontal line — cross-section at
//          mid-line spans exactly 3 rows; total lit pixels consistent with
//          a 301x3 butt-capped quad (the GL SC8 behavior class).
// Phase C (transform identity): reference square vs setTransform(0,0,0)
//          draw -> 0 px differ; rotated+translated draw differs; after
//          resetTransform -> 0 px differ again. A 90-degree bar pins the
//          rotate-about-own-origin-THEN-translate placement semantic.
// Phase D (soak regression): ~600 frames with LIVE draws every frame and a
//          forced resize at 2/5 — pipelines must survive the re-bootstrap
//          (one render pass for life) under validation. 0 VUID/UNASSIGNED.
//
// Draws are recorded between beginFrame/endFrame (the command buffer is
// open only there); readback rides the backend QA hooks exactly like
// vkpass.C (after-pass PRESENT_SRC -> TRANSFER_SRC -> copy -> PRESENT_SRC
// inside the frame's own command buffer).

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<time.h>

#include"vkBackend.H"

static VKBackend* g_vk=NULL;

struct Readback
 {VkBuffer buf;
  VkDeviceMemory mem;
  void* mapped;
  VkDeviceSize size;
 };

static double nowSeconds()
 {struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC,&ts);
  return (double)ts.tv_sec+(double)ts.tv_nsec/1e9;
 }

static uint32_t findMemoryType(VkPhysicalDevice pd,uint32_t typeBits,
                               VkMemoryPropertyFlags props)
 {VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(pd,&mp);
  for (uint32_t i=0;i<mp.memoryTypeCount;++i)
    if ((typeBits&(1u<<i))
        &&(mp.memoryTypes[i].propertyFlags&props)==props)
      return i;
  return UINT32_MAX;
 }

static int rbCreate(Readback* r,VkDeviceSize size)
 {r->buf=VK_NULL_HANDLE;r->mem=VK_NULL_HANDLE;r->mapped=NULL;r->size=size;
  VkBufferCreateInfo bci{};
  bci.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bci.size=size;
  bci.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  bci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(g_vk->device,&bci,NULL,&r->buf)!=VK_SUCCESS)
    return 0;
  VkMemoryRequirements mr;
  vkGetBufferMemoryRequirements(g_vk->device,r->buf,&mr);
  VkMemoryAllocateInfo mai{};
  mai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize=mr.size;
  mai.memoryTypeIndex=findMemoryType(g_vk->physicalDevice,
                                     mr.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                     |VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (mai.memoryTypeIndex==UINT32_MAX)
    return 0;
  if (vkAllocateMemory(g_vk->device,&mai,NULL,&r->mem)!=VK_SUCCESS)
    return 0;
  if (vkBindBufferMemory(g_vk->device,r->buf,r->mem,0)!=VK_SUCCESS)
    return 0;
  if (vkMapMemory(g_vk->device,r->mem,0,size,0,&r->mapped)!=VK_SUCCESS)
    return 0;
  return 1;
 }

static void rbDestroy(Readback* r)
 {if (g_vk->device==VK_NULL_HANDLE)
    return;                    // fatal-stall path already tore down the device
  if (r->mapped)
    vkUnmapMemory(g_vk->device,r->mem);
  if (r->mem!=VK_NULL_HANDLE)
    vkFreeMemory(g_vk->device,r->mem,NULL);
  if (r->buf!=VK_NULL_HANDLE)
    vkDestroyBuffer(g_vk->device,r->buf,NULL);
  r->buf=VK_NULL_HANDLE;r->mem=VK_NULL_HANDLE;r->mapped=NULL;
 }

static void presentToTransferBarrier(VkCommandBuffer cb,VkImage image)
 {VkImageMemoryBarrier bar{};
  bar.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  bar.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  bar.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  bar.oldLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  bar.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  bar.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
  bar.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
  bar.image=image;
  bar.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
  bar.subresourceRange.levelCount=1;
  bar.subresourceRange.layerCount=1;
  vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,
                       &bar);
 }

static void transferToPresentBarrier(VkCommandBuffer cb,VkImage image)
 {VkImageMemoryBarrier bar{};
  bar.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  bar.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  bar.dstAccessMask=0;
  bar.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  bar.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  bar.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
  bar.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
  bar.image=image;
  bar.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
  bar.subresourceRange.levelCount=1;
  bar.subresourceRange.layerCount=1;
  vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,NULL,0,
                       NULL,1,&bar);
 }

static void copyForReadbackHook(VkCommandBuffer cb,uint32_t imageIndex,
                                VkImage image,void* ud)
 {Readback* r=(Readback*)ud;
  (void)imageIndex;
  VkImage img=image;
  presentToTransferBarrier(cb,img);
  VkBufferImageCopy region{};
  region.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.layerCount=1;
  region.imageExtent.width=g_vk->swapchainExtent_.width;
  region.imageExtent.height=g_vk->swapchainExtent_.height;
  region.imageExtent.depth=1;
  vkCmdCopyImageToBuffer(cb,img,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         r->buf,1,&region);
  transferToPresentBarrier(cb,img);
 }

static const uint8_t* pixelAt(const Readback* r,uint32_t W,uint32_t x,
                              uint32_t y)
 {return (const uint8_t*)r->mapped+((size_t)y*W+x)*4;
 }

// Swapchain format is B8G8R8A8_UNORM: copied bytes are [B,G,R,A]. All
// color predicates below read through that mapping.
static int chanNear(const uint8_t* p,int chan,int want,int tol)
 {int d=p[chan]-want;
  if (d<0)
    d=-d;
  return d<=tol;
 }
static int isRed(const uint8_t* p)
 {return p[2]>200&&p[1]<60&&p[0]<60;
 }
static int isGreen(const uint8_t* p)
 {return p[2]<60&&p[1]>200&&p[0]<60;
 }
static int isBlue(const uint8_t* p)
 {return p[2]<60&&p[1]<60&&p[0]>200;
 }
static int isCyan(const uint8_t* p)
 {return p[2]<60&&p[1]>200&&p[0]>200;
 }
static int isMagenta(const uint8_t* p)
 {return p[2]>200&&p[1]<60&&p[0]>200;
 }
static int isWhite(const uint8_t* p)
 {return p[0]>240&&p[1]>240&&p[2]>240;
 }
static int isBlack(const uint8_t* p)
 {return p[0]<10&&p[1]<10&&p[2]<10;
 }

static long countMatching(const Readback* r,uint32_t W,
                          uint32_t x0,uint32_t y0,uint32_t x1,uint32_t y1,
                          int (*pred)(const uint8_t*))
 {long n=0;
  for (uint32_t y=y0;y<y1;++y)
    for (uint32_t x=x0;x<x1;++x)
      if (pred(pixelAt(r,W,x,y)))
        ++n;
  return n;
 }

static long diffPixels(const Readback* a,const Readback* b,uint32_t W,
                       uint32_t H)
 {const uint8_t* pa=(const uint8_t*)a->mapped;
  const uint8_t* pb=(const uint8_t*)b->mapped;
  long n=0;
  for (size_t i=0;i<(size_t)W*H*4;++i)
    if (pa[i]!=pb[i])
      ++n;
  return n;
 }

static void clearFrame()
 {g_vk->resetTransform();
  g_vk->setScissorRect(nullptr);
 }

int main()
 {long soakFrames=600;
  if (const char* env=getenv("XAST_VK_SOAK_FRAMES"))
    soakFrames=atol(env);

  VKBackend vk;
  g_vk=&vk;
  vk.setCanonicalLayout(640,512,44);

  int failures=0;
  if (!vk.initWindow("vkpipe"))
   {fprintf(stderr,"vkpipe: FAIL: initWindow failed\n");
    return 1;
   }
  printf("vkpipe: init: %u shader module(s) compiled from source at init, "
         "compiledAtInit=%d\n",vk.shaderModulesLoaded_,
         (int)vk.shadersCompiledAtInit_);
  printf("vkpipe: pipelines: line=%s tri=%s outline=%s tex=%s\n",
         vk.linePipeline!=VK_NULL_HANDLE?"ok":"MISSING",
         vk.triPipeline!=VK_NULL_HANDLE?"ok":"MISSING",
         vk.outlinePipeline!=VK_NULL_HANDLE?"ok":"MISSING",
         vk.texPipeline!=VK_NULL_HANDLE?"ok":"MISSING");
  if (!vk.shadersCompiledAtInit_||vk.shaderModulesLoaded_!=4
      ||!vk.linePipeline||!vk.triPipeline||!vk.outlinePipeline
      ||!vk.texPipeline)
   {fprintf(stderr,"vkpipe: FAIL: pipelines/shaders not ready at init\n");
    return 1;
   }

  const uint32_t W=vk.swapchainExtent_.width;
  const uint32_t H=vk.swapchainExtent_.height;
  Readback rbA,rbB;
  if (!rbCreate(&rbA,(VkDeviceSize)W*H*4)
      ||!rbCreate(&rbB,(VkDeviceSize)W*H*4))
   {fprintf(stderr,"vkpipe: FAIL: readback buffers\n");
    return 1;
   }
  vk.qaAfterRenderPassHook=&copyForReadbackHook;

  // ---- Phase A: all pipeline variants in ONE frame ----------------------
  TextureId bad=vk.createTextureFromRGBA32(NULL,0,0);   // invalid-input guard
  if (bad!=0)
   {fprintf(stderr,"vkpipe: FAIL: invalid createTexture returned %llu\n",
            (unsigned long long)bad);
    ++failures;
   }
  uint8_t texPix[8*8*4];
  for (int y=0;y<8;++y)
    for (int x=0;x<8;++x)
     {uint8_t* p=texPix+(y*8+x)*4;
      uint8_t v=(x<4)?255:0;                 // left half white, right black
      p[0]=v;p[1]=v;p[2]=v;p[3]=255;
     }
  TextureId tex=vk.createTextureFromRGBA32(texPix,8,8);
  if (!tex)
   {fprintf(stderr,"vkpipe: FAIL: texture creation failed\n");
    return 1;
   }

  clearFrame();
  vk.beginFrame();
  vk.drawLine(50,100,250,100,1.0f,0.0f,0.0f,1.0f);        // thin red LINE_LIST
  float tri[6]={300,80, 420,80, 360,180};
  vk.drawPolygon(tri,3,0.0f,1.0f,0.0f,true);              // green TRIANGLE_LIST fill
  float sq[8]={450,250, 550,250, 550,350, 450,350};
  vk.drawPolygon(sq,4,0.0f,0.0f,1.0f,false);              // blue POLYGON_MODE_LINE
  vk.drawLine(60,400,260,400,1.0f,1.0f,1.0f,5.0f);        // white width-5 quad
  vk.drawRect(430,420,80,60,0.0f,1.0f,1.0f,false);        // cyan outline rect
  vk.drawTexture(tex,60,220,120,120,1.0f);                // opaque checker
  vk.drawTexture(tex,220,220,120,120,0.5f);               // half-alpha over black
  float winTri[21]=                                       // 7-float contract
    {400,540, 0.125f,0.125f, 1,0,0,
     500,540, 0.125f,0.125f, 1,0,0,
     450,600, 0.125f,0.125f, 1,0,0};
    vk.drawTriangles(winTri,3,tex);                         // white tex * red tint
  vk.qaAfterRenderPassHookUserData=&rbA;
  memset(rbA.mapped,0xAB,(size_t)rbA.size);
  vk.endFrame();
  vkDeviceWaitIdle(vk.device);

  struct Check
   {const char* name;
    long got;
    long wantMin;
   };
  Check checks[]=
   {// thin line: half of the 200 columns lit somewhere in rows 97..103
    {"thin-line px",countMatching(&rbA,W,50,97,250,104,isRed),100},
    {"tri-fill px",countMatching(&rbA,W,300,80,421,181,isGreen),2000},
    {"outline px",countMatching(&rbA,W,445,245,556,356,isBlue),200},
    {"thick5 interior",isWhite(pixelAt(&rbA,W,160,400)),1},
    {"rect-outline top edge",countMatching(&rbA,W,460,416,481,425,isCyan),1},
    {"rect-outline interior black",isBlack(pixelAt(&rbA,W,470,450)),1},
    {"tex left-half white",isWhite(pixelAt(&rbA,W,90,280)),1},
    {"tex right-half black",isBlack(pixelAt(&rbA,W,150,280)),1},
    {"alpha .5 gray",chanNear(pixelAt(&rbA,W,250,280),0,127,20)
                    &&chanNear(pixelAt(&rbA,W,250,280),1,127,20),1},
    {"drawTriangles red",isRed(pixelAt(&rbA,W,450,560)),1},
   };
  for (unsigned i=0;i<sizeof checks/sizeof checks[0];++i)
   {printf("vkpipe: A %-26s got=%ld\n",checks[i].name,checks[i].got);
    if (checks[i].got<checks[i].wantMin)
     {fprintf(stderr,"vkpipe: FAIL: A %s got=%ld want>=%ld\n",
              checks[i].name,checks[i].got,checks[i].wantMin);
      ++failures;
     }
   }
  if (checks[0].got<100)
   {// line-rasterization debug: where DID the thin line land?
    printf("vkpipe: DEBUG rows 90..110 x 40..260 non-black pixels:\n");
    int shown=0;
    for (uint32_t y=90;y<110&&shown<12;++y)
      for (uint32_t x=40;x<260&&shown<12;++x)
       {const uint8_t* p=pixelAt(&rbA,W,x,y);
        if (!isBlack(p))
         {printf("  (%u,%u) = %02x %02x %02x %02x\n",x,y,p[0],p[1],p[2],p[3]);
          ++shown;
         }
       }
   }

  // ---- Phase B: width-3 thick-line geometry ------------------------------
  clearFrame();
  vk.beginFrame();
  vk.drawLine(100,300,400,300,1.0f,1.0f,1.0f,3.0f);
  vk.qaAfterRenderPassHookUserData=&rbB;
  memset(rbB.mapped,0xAB,(size_t)rbB.size);
  vk.endFrame();
  vkDeviceWaitIdle(vk.device);
  int colRows=0;
  for (uint32_t y=290;y<315;++y)
    if (isWhite(pixelAt(&rbB,W,250,y)))
      ++colRows;
  long total=countMatching(&rbB,W,90,290,410,315,isWhite);
  printf("vkpipe: B width-3 cross-section rows=%d total=%ld "
         "(301x3=903 butt-capped)\n",colRows,total);
  if (colRows!=3)
   {fprintf(stderr,"vkpipe: FAIL: B cross-section %d rows, want exactly 3\n",
            colRows);
    ++failures;
   }
  if (total<850||total>950)
   {fprintf(stderr,"vkpipe: FAIL: B total %ld outside [850,950]\n",total);
    ++failures;
   }

  // ---- Phase C: transform identity + rotator placement -------------------
  clearFrame();
  float box[6]={560,600, 620,600, 590,650};
  vk.beginFrame();
  vk.drawPolygon(box,3,0.0f,1.0f,1.0f,true);
  vk.qaAfterRenderPassHookUserData=&rbA;
  memset(rbA.mapped,0xAB,(size_t)rbA.size);
  vk.endFrame();
  vkDeviceWaitIdle(vk.device);

  // C1: setTransform(0,0,0) == identity reference
  clearFrame();
  vk.beginFrame();
  vk.setTransform(0.0f,0.0f,0.0f);
  vk.drawPolygon(box,3,0.0f,1.0f,1.0f,true);
  vk.qaAfterRenderPassHookUserData=&rbB;
  memset(rbB.mapped,0xAB,(size_t)rbB.size);
  vk.endFrame();
  vkDeviceWaitIdle(vk.device);
  long d1=diffPixels(&rbA,&rbB,W,H);
  printf("vkpipe: C1 setTransform(0,0,0) vs reference: %ld differing byte "
         "values\n",d1);
  if (d1!=0)
   {fprintf(stderr,"vkpipe: FAIL: C1 identity transform not identity\n");
    ++failures;
   }

  // C2: rotate about own origin THEN translate (D2 placement): local bar
  // (0..80, 0..10) under R(90deg) maps to (-10..0, 0..80); translated by
  // (300,300) it must occupy x[290,300] y[300,380] — vertical, LEFT of
  // x=300, extending DOWN from y=300.
  clearFrame();
  vk.beginFrame();
  vk.setTransform(300.0f,300.0f,1.5707963267948966f);
  vk.drawRect(0.0f,0.0f,80.0f,10.0f,1.0f,0.0f,1.0f,true);
  vk.qaAfterRenderPassHookUserData=&rbB;
  memset(rbB.mapped,0xAB,(size_t)rbB.size);
  vk.endFrame();
  vkDeviceWaitIdle(vk.device);
  int magInside=isMagenta(pixelAt(&rbB,W,295,340));
  int magOutsideRight=!isMagenta(pixelAt(&rbB,W,305,340));
  int magAbove=!isMagenta(pixelAt(&rbB,W,295,290));
  printf("vkpipe: C2 R90+translate bar: inside=%d right-clear=%d "
         "above-clear=%d\n",magInside,magOutsideRight,magAbove);
  if (!magInside||!magOutsideRight||!magAbove)
   {fprintf(stderr,"vkpipe: FAIL: C2 rotate-then-translate placement "
                    "wrong\n");
    ++failures;
   }

  // C3: resetTransform restores identity
  clearFrame();
  vk.beginFrame();
  vk.setTransform(30.0f,-15.0f,0.7f);
  vk.resetTransform();
  vk.drawPolygon(box,3,0.0f,1.0f,1.0f,true);
  vk.qaAfterRenderPassHookUserData=&rbB;
  memset(rbB.mapped,0xAB,(size_t)rbB.size);
  vk.endFrame();
  vkDeviceWaitIdle(vk.device);
  long d3=diffPixels(&rbA,&rbB,W,H);
  printf("vkpipe: C3 resetTransform vs reference: %ld differing byte "
         "values\n",d3);
  if (d3!=0)
   {fprintf(stderr,"vkpipe: FAIL: C3 resetTransform not identity\n");
    ++failures;
   }

  // ---- Phase D: soak with live draws + forced re-bootstrap ---------------
  // Readback hook detached: the copy target is sized for the ORIGINAL
  // extent and a resize mid-soak would overflow it (VUID-00183); the
  // re-bootstrap proof here is pipelines surviving with draws every frame.
  vk.deleteTexture(tex);
  vk.qaAfterRenderPassHook=NULL;
  vk.qaAfterRenderPassHookUserData=NULL;
  const unsigned rbBefore=vk.rebootstrapCount_;
  double t0=nowSeconds();
  for (long i=0;i<soakFrames;++i)
   {if (i==soakFrames*2/5)
      glfwSetWindowSize(vk.window,920,640);
    clearFrame();
    vk.beginFrame();
    vk.drawLine((float)(i%400)+50,50.0f,(float)(i%400)+150,150.0f,
                1.0f,0.0f,0.0f,1.0f);
    vk.drawLine(100.0f,200.0f,300.0f,200.0f,0.0f,1.0f,0.0f,3.0f);
    float spin[6]={320,300, 380,300, 350,350};
    vk.setTransform(0.0f,0.0f,(float)i*0.05f);
    vk.drawPolygon(spin,3,0.0f,0.0f,1.0f,true);
    vk.resetTransform();
    vk.drawRect(400.0f,300.0f+(float)(i%100),60.0f,40.0f,
                1.0f,1.0f,0.0f,false);
    vk.endFrame();
    if (vk.fatalStallShutdown_)
     {fprintf(stderr,"vkpipe: FAIL: fatal stall at frame %ld\n",i);
      ++failures;
      break;
     }
   }
  double dt=nowSeconds()-t0;
  printf("vkpipe: D soak: %u/%ld frames in %.2fs (%.1f fps), re-bootstraps "
         "%u->%u, stalls %u\n",vk.frameCount_,soakFrames,dt,
         dt>0.0?(double)vk.frameCount_/dt:0.0,rbBefore,
         vk.rebootstrapCount_,vk.stallSkipCount_);
  if (vk.frameCount_<soakFrames-16)
   {fprintf(stderr,"vkpipe: FAIL: D completed only %u/%ld frames\n",
            vk.frameCount_,soakFrames);
    ++failures;
   }
  if (vk.rebootstrapCount_<=rbBefore)
   {fprintf(stderr,"vkpipe: FAIL: D re-bootstrap never engaged (pipelines "
                    "never proven across swapchain rebuild)\n");
    ++failures;
   }
  else
    printf("vkpipe: D re-bootstrap ENGAGED x%u WITH live draws every frame "
           "(pipelines survived)\n",vk.rebootstrapCount_-rbBefore);

  if (vk.validationErrorCount_!=0)
   {fprintf(stderr,"vkpipe: FAIL: %u validation ERROR(s)\n",
            vk.validationErrorCount_);
    ++failures;
   }
  printf("vkpipe: validation errors (VUID-/UNASSIGNED): %u (layer %s)\n",
         vk.validationErrorCount_,
         vk.validationEnabled_?"LIVE":"absent");

  if (!vk.fatalStallShutdown_)
   {int fw=0,fh=0;
    glfwGetFramebufferSize(vk.window,&fw,&fh);
    if ((uint32_t)fw!=vk.swapchainExtent_.width
        ||(uint32_t)fh!=vk.swapchainExtent_.height)
     {fprintf(stderr,"vkpipe: FAIL: extent %ux%u != framebuffer %dx%d\n",
              vk.swapchainExtent_.width,vk.swapchainExtent_.height,fw,fh);
      ++failures;
     }
   }

  vk.qaAfterRenderPassHook=NULL;
  vk.qaAfterRenderPassHookUserData=NULL;
  rbDestroy(&rbA);
  rbDestroy(&rbB);
  vk.shutdown();

  if (failures)
   {fprintf(stderr,"vkpipe: FAIL: %d assertion(s)\n",failures);
    return 1;
   }
  printf("vkpipe: PASS\n");
  return 0;
 }
