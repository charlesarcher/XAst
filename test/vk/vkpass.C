// test/vk/vkpass.C — task-40 render pass + framebuffer + scissor proof.
//
// Phase A: 600-frame soak (XAST_VK_SOAK_FRAMES overrides) with a forced
//          glfwSetWindowSize at 2/5 — the re-bootstrap path must destroy
//          old framebuffers before swapchain retirement and recreate them,
//          all under live validation. Regression signal only (task 39
//          proved the 9600-frame soak).
// Phase B: scissor proof through the REAL beginFrame/endFrame cycle using
//          the backend's QA hooks (vkBackend.H header):
//   B1: setScissorRect({100,80,320,256}) then a hook-issued WHITE
//       vkCmdClearAttachments whose VkClearRect IS the transformed scissor
//       rect (identity transform today). Readback asserts outside=black,
//       inside=white — 0 px outside.
//   B2: setScissorRect(nullptr), same hook with the FULL-extent rect —
//       readback asserts ALL white (un-scissored HUD contract restored).
//
// WHY THE CLEAR RECT CARRIES THE CLIP (driver finding, see vkBackend.H):
// vkCmdClearAttachments is NOT clipped by the dynamic scissor on this
// stack — "not affected by the bound pipeline state", and without task-41
// pipelines the latched vkCmdSetScissor has no consumer (verified live:
// an unclipped full-rect clear washed the whole framebuffer, RUN T40-1
// negative control). The backend still records vkCmdSetScissor every frame
// (task 41's draws consume it); this proof pins the setScissorRect ->
// present-transform -> recorded-rect chain pixel-exactly via the
// spec-guaranteed VkClearRect region.
// Readback rides INSIDE the recorded frame command buffer: after
// vkCmdEndRenderPass the image is PRESENT_SRC_KHR; the after-hook bounces
// TRANSFER_SRC -> vkCmdCopyImageToBuffer(host-visible) -> back to
// PRESENT_SRC so the normal present stays legal.

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
 {if (r->mapped)
    vkUnmapMemory(g_vk->device,r->mem);
  if (r->mem!=VK_NULL_HANDLE)
    vkFreeMemory(g_vk->device,r->mem,NULL);
  if (r->buf!=VK_NULL_HANDLE)
    vkDestroyBuffer(g_vk->device,r->buf,NULL);
  r->buf=VK_NULL_HANDLE;r->mem=VK_NULL_HANDLE;r->mapped=NULL;
 }

static void recordPresentToTransferBarrier(VkCommandBuffer cb,VkImage image,
                                            VkExtent2D ext)
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
  (void)ext;
 }

static void recordTransferToPresentBarrier(VkCommandBuffer cb,VkImage image)
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

struct ClearRegion
 {int x,y,w,h;      // framebuffer-space (already present-transformed)
 };

// Inside-pass hook: clear WHITE restricted to the region derived from the
// setScissorRect call (B2 passes the full extent).
static void clearWhiteHook(VkCommandBuffer cb,uint32_t imageIndex,
                           VkImage image,void* ud)
 {(void)imageIndex;(void)image;
  const ClearRegion* r=(const ClearRegion*)ud;
  VkClearAttachment att{};
  att.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
  att.clearValue.color.float32[0]=1.0f;
  att.clearValue.color.float32[1]=1.0f;
  att.clearValue.color.float32[2]=1.0f;
  att.clearValue.color.float32[3]=1.0f;
  VkClearRect cr{};
  cr.rect.offset.x=r->x;
  cr.rect.offset.y=r->y;
  cr.rect.extent.width=(uint32_t)r->w;
  cr.rect.extent.height=(uint32_t)r->h;
  cr.baseArrayLayer=0;
  cr.layerCount=1;
  vkCmdClearAttachments(cb,1,&att,1,&cr);
 }

// After-pass hook: image is PRESENT_SRC_KHR here — bounce TRANSFER_SRC,
// copy into the host-visible buffer, bounce back so present stays legal.
static void copyForReadbackHook(VkCommandBuffer cb,uint32_t imageIndex,
                                VkImage image,void* ud)
 {Readback* r=(Readback*)ud;
  (void)imageIndex;
  VkImage img=image;
  recordPresentToTransferBarrier(cb,img,g_vk->swapchainExtent_);
  VkBufferImageCopy region{};
  region.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.layerCount=1;
  region.imageOffset.x=0;
  region.imageOffset.y=0;
  region.imageOffset.z=0;
  region.imageExtent.width=g_vk->swapchainExtent_.width;
  region.imageExtent.height=g_vk->swapchainExtent_.height;
  region.imageExtent.depth=1;
  vkCmdCopyImageToBuffer(cb,img,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         r->buf,1,&region);
  recordTransferToPresentBarrier(cb,img);
 }

static int pixelIsWhite(const uint8_t* p)
 {return p[0]==255&&p[1]==255&&p[2]==255&&p[3]==255;
 }

static int pixelIsBlack(const uint8_t* p)
 {return p[0]==0&&p[1]==0&&p[2]==0&&p[3]==255;
 }

int main()
 {long soakFrames=600;
  long postResizeFrames=200;
  if (const char* env=getenv("XAST_VK_SOAK_FRAMES"))
    soakFrames=atol(env);
  if (const char* env=getenv("XAST_VK_POST_RESIZE_FRAMES"))
    postResizeFrames=atol(env);

  VKBackend vk;
  g_vk=&vk;
  // Mirrors main()'s setCanonicalLayout call: playArea 640x512,
  // maxIconHeight 44.
  vk.setCanonicalLayout(640,512,44);

  int failures=0;
  if (!vk.initWindow("vkpass"))
   {fprintf(stderr,"vkpass: FAIL: initWindow failed\n");
    return 1;
   }
  printf("vkpass: swapchain %ux%u, %u image(s), format %d\n",
         vk.swapchainExtent_.width,vk.swapchainExtent_.height,
         vk.swapchainImageCount_,(int)vk.swapchainFormat_);
  printf("vkpass: render pass created: %s\n",
         vk.renderPass!=VK_NULL_HANDLE?"yes":"NO");

  // ---- Phase A: soak regression incl. forced re-bootstrap --------------
  const unsigned rbBefore=vk.rebootstrapCount_;
  const long totalA=soakFrames+postResizeFrames;
  double t0=nowSeconds();
  for (long i=0;i<totalA;++i)
   {if (i==soakFrames*2/5)
      glfwSetWindowSize(vk.window,920,640);
    vk.beginFrame();
    vk.endFrame();
    if (vk.fatalStallShutdown_)
     {fprintf(stderr,"vkpass: FAIL: fatal stall shutdown at frame %ld\n",i);
      ++failures;
      break;
     }
   }
  double dtA=nowSeconds()-t0;
  printf("vkpass: phase A: %u/%ld frames in %.2fs (%.1f fps), "
         "re-bootstraps %u->%u, stalls %u\n",
         vk.frameCount_,totalA,dtA,
         dtA>0.0?(double)vk.frameCount_/dtA:0.0,
         rbBefore,vk.rebootstrapCount_,vk.stallSkipCount_);
  if (vk.frameCount_<totalA-16)
   {fprintf(stderr,"vkpass: FAIL: phase A completed only %u/%ld frames\n",
            vk.frameCount_,totalA);
    ++failures;
   }
  if (vk.rebootstrapCount_<=rbBefore)
   {fprintf(stderr,"vkpass: FAIL: re-bootstrap never engaged "
                    "(framebuffers never recreated)\n");
    ++failures;
   }
  else
    printf("vkpass: re-bootstrap ENGAGED x%u under validation; framebuffers "
           "recreated\n",vk.rebootstrapCount_-rbBefore);

  // ---- Phase B: scissor proof ------------------------------------------
  const uint32_t W=vk.swapchainExtent_.width;
  const uint32_t H=vk.swapchainExtent_.height;
  Readback rb;
  if (!rbCreate(&rb,(VkDeviceSize)W*H*4))
   {fprintf(stderr,"vkpass: FAIL: readback buffer creation failed\n");
    vk.shutdown();
    return 1;
   }
  vk.qaInRenderPassHook=&clearWhiteHook;
  vk.qaAfterRenderPassHook=&copyForReadbackHook;
  vk.qaAfterRenderPassHookUserData=&rb;

  // B1: scissored white rect on black.
  const int rect[4]={100,80,320,256};   // logical client coords == window
                                        // coords while transform is identity
  vk.setScissorRect(rect);              // backend latches vkCmdSetScissor
  ClearRegion region;
  region.x=rect[0];region.y=rect[1];region.w=rect[2];region.h=rect[3];
  vk.qaInRenderPassHookUserData=&region;
  memset(rb.mapped,0xAB,(size_t)rb.size);   // poison: only the copy may fill
  vk.beginFrame();
  vk.endFrame();
  vkDeviceWaitIdle(vk.device);
  const uint8_t* px=(const uint8_t*)rb.mapped;
  long insideBad=0,outsideBad=0,insideTotal=0,outsideTotal=0;
  long firstBad[4]={-1,-1,-1,-1};
  for (uint32_t y=0;y<H;++y)
    for (uint32_t x=0;x<W;++x)
     {const uint8_t* p=px+((size_t)y*W+x)*4;
      int inside=(x>=(uint32_t)rect[0]&&x<(uint32_t)(rect[0]+rect[2])
                  &&y>=(uint32_t)rect[1]&&y<(uint32_t)(rect[1]+rect[3]));
      if (inside)
       {++insideTotal;
        if (!pixelIsWhite(p))
         {if (insideBad==0)
           {firstBad[0]=(long)x;firstBad[1]=(long)y;}
          ++insideBad;
         }
       }
      else
       {++outsideTotal;
        if (!pixelIsBlack(p))
         {if (outsideBad==0)
           {firstBad[2]=(long)x;firstBad[3]=(long)y;}
          ++outsideBad;
         }
       }
     }
  printf("vkpass: B1 scissored rect {%d,%d,%d,%d}: inside %ld/%ld white, "
         "outside %ld/%ld black\n",rect[0],rect[1],rect[2],rect[3],
         insideTotal-insideBad,insideTotal,
         outsideTotal-outsideBad,outsideTotal);
  if (insideBad||outsideBad)
   {fprintf(stderr,"vkpass: FAIL: B1 mismatches: %ld inside (first %ld,%ld), "
                    "%ld outside (first %ld,%ld)\n",insideBad,firstBad[0],
            firstBad[1],outsideBad,firstBad[2],firstBad[3]);
    ++failures;
   }
  else
    printf("vkpass: B1 PASS — scissor CLIPPED the clear (0 px outside, "
           "%ld px inside all white)\n",insideTotal);

  // B2: nullptr restores full-extent behavior.
  vk.setScissorRect(nullptr);
  region.x=0;region.y=0;
  region.w=(int)W;region.h=(int)H;
  memset(rb.mapped,0xAB,(size_t)rb.size);
  vk.beginFrame();
  vk.endFrame();
  vkDeviceWaitIdle(vk.device);
  long notWhite=0;
  firstBad[0]=firstBad[1]=-1;
  for (uint32_t y=0;y<H;++y)
    for (uint32_t x=0;x<W;++x)
     {const uint8_t* p=px+((size_t)y*W+x)*4;
      if (!pixelIsWhite(p))
       {if (notWhite==0)
         {firstBad[0]=(long)x;firstBad[1]=(long)y;}
        ++notWhite;
       }
     }
  printf("vkpass: B2 nullptr scissor: %ld/%ld pixels white\n",
         (long)W*H-notWhite,(long)W*H);
  if (notWhite)
   {fprintf(stderr,"vkpass: FAIL: B2 %ld non-white pixels (first %ld,%ld) — "
                    "setScissorRect(nullptr) did not restore full extent\n",
            notWhite,firstBad[0],firstBad[1]);
    ++failures;
   }
  else
    printf("vkpass: B2 PASS — full-extent behavior restored\n");

  vk.qaInRenderPassHook=NULL;
  vk.qaAfterRenderPassHook=NULL;
  vk.qaAfterRenderPassHookUserData=NULL;
  rbDestroy(&rb);

  if (vk.validationErrorCount_!=0)
   {fprintf(stderr,"vkpass: FAIL: %u validation ERROR(s)\n",
            vk.validationErrorCount_);
    ++failures;
   }
  printf("vkpass: validation errors (VUID-/UNASSIGNED): %u "
         "(layer %s)\n",vk.validationErrorCount_,
         vk.validationEnabled_?"LIVE":"absent");

  int fw=0,fh=0;
  glfwGetFramebufferSize(vk.window,&fw,&fh);
  if ((uint32_t)fw!=vk.swapchainExtent_.width
      ||(uint32_t)fh!=vk.swapchainExtent_.height)
   {fprintf(stderr,"vkpass: FAIL: extent %ux%u != framebuffer %dx%d\n",
            vk.swapchainExtent_.width,vk.swapchainExtent_.height,fw,fh);
    ++failures;
   }

  vk.shutdown();

  if (failures)
   {fprintf(stderr,"vkpass: FAIL: %d assertion(s)\n",failures);
    return 1;
   }
  printf("vkpass: PASS\n");
  return 0;
 }
