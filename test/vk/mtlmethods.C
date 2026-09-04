// test/vk/mtlmethods.C — task-11 identity-gate for the Metal backend.
// Renders identityScene.H to an OFFSCREEN shared-storage render target
// (640x512, BGRA8Unorm), waits for GPU completion, reads back pixels via
// getBytes with BGRA→RGBA swizzle, and writes a full raw dump (128-byte
// header + top-down RGBA pixels) to argv[1] for byte-comparison against
// the GL/VK reference dumps.
//
// The RT is BGRA8Unorm (task 8: matching pipeline pixel format). getBytes
// returns native BGRA layout; the readback loop swaps B↔R to produce the
// standard RGBA dump format used by vkmethods-gl.C and vkmethods.C.
//
// Row-order convention (task 15): the backend's MVP maps logical top to
// NDC +1 (Metal NDC is y-UP), which lands in texture row 0 — Metal texture
// row 0 IS the top of the image — so the raw readback is ALREADY top-down
// and matches the VK reference orientation. No row flip here: the old
// readback-side row flip was a compensation for the backend's missing
// MVP y-flip and was removed with the task-15 fix (a flip left in exactly
// one stage of the pipeline is caught by the orientation self-check below
// and by the cross-backend byte-compare).
//
// Built by the makefile's obj/MTL/mtlmethods rule. Run from the repo root
// (font + metallib resolution is /proc/self/exe-relative — copy the binary
// to the repo root first, or symlink the vendor/ and obj/MTL/ dirs).
#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#include"../../utilities/rendering/mtlBackend.H"
#include"identityScene.H"

int main(int argc,char** argv)
 {if (argc<2)
   {fprintf(stderr,"usage: mtlmethods <out.raw>\n");
    return 2;
   }
  MTLBackend mtl;
  mtl.setCanonicalLayout(640,512,44);
  if (!mtl.initWindow("mtlmethods"))
   {fprintf(stderr,"mtlmethods: FAIL: initWindow\n");
    return 1;
   }

  uint8_t checker[8*8*4],content[16*16*4],mask[16*16];
  buildCheckerPixels(checker);
  buildQuadContentPixels(content);
  buildQuadMaskPixels(mask);
  TextureId tChecker=mtl.createTextureFromBitmap(checker,8,8,4);
  TextureId tContent=mtl.createTextureFromBitmap(content,16,16,4);
  TextureId tMask=mtl.createTextureFromBitmap(mask,16,16,1);
  if (!tChecker||!tContent||!tMask)
   {fprintf(stderr,"mtlmethods: FAIL: texture creation\n");
    return 1;
   }

  const int fw=640,fh=512;
  TextureId rt=mtl.createRenderTarget(fw,fh);
  if (!rt)
   {fprintf(stderr,"mtlmethods: FAIL: createRenderTarget\n");
    return 1;
   }

  SceneMasks masks;
  memset(&masks,0,sizeof masks);

  mtl.beginRenderTo(rt);
  renderIdentityScene(mtl,tChecker,tContent,tMask,&masks);
  mtl.endRenderTo();
  mtl.waitIdle();

  void* texPtr=mtl.getTexturePtr(rt);
  if (!texPtr)
   {fprintf(stderr,"mtlmethods: FAIL: getTexturePtr\n");
    return 1;
   }
  uint8_t* bgra=(uint8_t*)malloc((size_t)fw*fh*4);
  if (!bgra)
   {fprintf(stderr,"mtlmethods: FAIL: alloc\n");
    return 1;
   }
  mtlGetTextureBytes(texPtr,bgra,fw,fh);

  // BGRA→RGBA swizzle only (row order is already top-down — see the file
  // header convention note). The loop visits rows in buffer order.
  uint8_t* rgba=(uint8_t*)malloc((size_t)fw*fh*4);
  if (!rgba)
   {fprintf(stderr,"mtlmethods: FAIL: alloc rgba\n");
    free(bgra);
    return 1;
   }
  for (int y=0;y<fh;++y)
   {const uint8_t* srcRow=bgra+(size_t)y*fw*4;
    uint8_t* dstRow=rgba+(size_t)y*fw*4;
    for (int x=0;x<fw;++x)
     {dstRow[x*4+0]=srcRow[x*4+2];  // R ← B[2]
      dstRow[x*4+1]=srcRow[x*4+1];  // G ← G[1]
      dstRow[x*4+2]=srcRow[x*4+0];  // B ← R[0]
      dstRow[x*4+3]=srcRow[x*4+3];  // A ← A[3]
     }
   }
  free(bgra);

  FILE* f=fopen(argv[1],"wb");
  if (!f)
   {fprintf(stderr,"mtlmethods: FAIL: open %s\n",argv[1]);
    free(rgba);
    return 1;
   }
  uint32_t hdr[2+32]={(uint32_t)fw,(uint32_t)fh};
  for (int i=0;i<masks.numTextRects;++i)
   {hdr[2+i*4+0]=(uint32_t)masks.textRects[i][0];
    hdr[2+i*4+1]=(uint32_t)masks.textRects[i][1];
    hdr[2+i*4+2]=(uint32_t)masks.textRects[i][2];
    hdr[2+i*4+3]=(uint32_t)masks.textRects[i][3];
   }
  hdr[30]=(uint32_t)masks.numTextRects;
  fwrite(hdr,sizeof hdr,1,f);
  fwrite(rgba,(size_t)fw*fh*4,1,f);
  fclose(f);

  // ---- Task 15: asymmetric top-vs-bottom orientation self-check -----------
  // identityScene.H places a cyan marker ONLY at the top edge (10,4,48,12)
  // and a magenta marker ONLY at the bottom edge (10,496,48,12). In a
  // top-down dump (row 0 = logical top, the VK reference convention) the
  // cyan marker must sit in the TOP rows and the magenta in the BOTTOM rows.
  // A vertical flip surviving in exactly one pipeline stage (MVP y-scale OR
  // readback row order) swaps the halves and fails this check — so a mirror
  // can no longer pass the gate silently. The old pre-task-15 state (flipped
  // MVP + flipped readback) canceled out and passed; that double-flip is
  // exactly what this probe plus the pre/after dump invariance compare pin
  // against.
  auto at=[&](int x,int y)->const uint8_t*
   {return rgba+(size_t)y*fw*4+(size_t)x*4;};
  int topCyan  = at(34,10)[0]<60 &&at(34,10)[1]>200&&at(34,10)[2]>200;
  int botMagenta=at(34,502)[0]>200&&at(34,502)[1]<60&&at(34,502)[2]>200;
  int topNotMag=!(at(34,10)[0]>200&&at(34,10)[1]<60&&at(34,10)[2]>200);
  int botNotCyn=!(at(34,502)[0]<60 &&at(34,502)[1]>200&&at(34,502)[2]>200);
  // Per-sprite texture orientation: the masked quad (500,80,64,64) samples
  // the content texture whose quadrants are yellow(TL)/cyan(TR)/magenta(BL)/
  // blue(BR). Top-left of the quad (510,100) must read the texture's TOP
  // (yellow) and bottom-left (510,130) its BOTTOM (magenta) — a per-sprite
  // vertical flip swaps exactly these two.
  const uint8_t* tTop=at(510,100);
  const uint8_t* tBot=at(510,130);
  int texTopYellow = tTop[0]>200&&tTop[1]>200&&tTop[2]<60;
  int texBotMagenta= tBot[0]>200&&tBot[1]<60 &&tBot[2]>200;
  printf("mtlmethods: orientation topCyan=%d botMagenta=%d topNotMagenta=%d "
         "botNotCyan=%d texTopYellow=%d texBotMagenta=%d\n",
         topCyan,botMagenta,topNotMag,botNotCyn,texTopYellow,texBotMagenta);
  if (!topCyan||!botMagenta||!topNotMag||!botNotCyn
      ||!texTopYellow||!texBotMagenta)
   {fprintf(stderr,"mtlmethods: FAIL: orientation probe — the dump is not "
                   "top-down (top-of-canvas content must be in the TOP rows); "
                   "a vertical flip remains in the MVP or the readback\n");
    free(rgba);
    mtl.shutdown();
    return 1;
   }

  free(rgba);
  printf("mtlmethods: wrote %s (%dx%d, %d text rects, %d bytes total)\n",
         argv[1],fw,fh,masks.numTextRects,
         (int)(sizeof hdr+fw*fh*4));

  // ---- Task-13 resize probe (TCC-free): letterbox transform recompute ----
  // The backend's canonical window size is computed by WindowSizeFormula
  // (688x702 here: play area 640x512 + header), NOT the raw 640x512 play
  // area. The letterbox transform uses canonicalWidth_/canonicalHeight_, so
  // the expected ox/oy below are derived from the ACTUAL canonical size via
  // the same scale=min formula the backend implements. We verify the backend
  // returns exactly those values after each glfwSetWindowSize + poll.
  {
   GLFWwindow* win=mtl.glfwWindow();
   if (!win)
    {fprintf(stderr,"mtlmethods: FAIL: no glfw window for resize probe\n");
     return 1;
    }
   // At the initial (canonical) window size the framebuffer equals the
   // canonical size, so read it directly from GLFW before any resize. The
   // backend's recomputePresentTransform_ divides the FRAMEBUFFER size by the
   // LOGICAL canonical size (canonicalWidth_/canonicalHeight_), so read the
   // logical window size for the canonical dims and the framebuffer size for
   // the current dims — this reproduces the backend's scale exactly even on
   // Retina (backing scale 2x).
   int canW=0,canH=0;
   glfwGetWindowSize(win,&canW,&canH);
   float s0;int ox0,oy0;
   mtl.getPresentTransform(s0,ox0,oy0);
   printf("resize: canonical window %dx%d, initial transform scale=%.6f ox=%d oy=%d\n",
          canW,canH,(double)s0,ox0,oy0);

   // Probe 1: 1024x768 (logical). The framebuffer is the backing-scale
   // multiple (Retina 2x here), and recomputePresentTransform_ uses the
   // FRAMEBUFFER size, so read the actual framebuffer after the resize and
   // derive expected values from it.
   glfwSetWindowSize(win,1024,768);
   glfwPollEvents();
   int fbw1=0,fbh1=0;
   glfwGetFramebufferSize(win,&fbw1,&fbh1);
   float s1;int ox1,oy1;
   mtl.getPresentTransform(s1,ox1,oy1);
   float eScale1=((float)fbw1/canW<(float)fbh1/canH)?(float)fbw1/canW:(float)fbh1/canH;
   int eOx1=((int)fbw1-(int)(canW*eScale1))/2;
   int eOy1=((int)fbh1-(int)(canH*eScale1))/2;
   printf("resize: 1024x768 (fb %dx%d) -> scale=%.6f ox=%d oy=%d (expected scale=%.6f ox=%d oy=%d) %s\n",
          fbw1,fbh1,(double)s1,ox1,oy1,(double)eScale1,eOx1,eOy1,
          (ox1==eOx1&&oy1==eOy1)?"MATCH":"MISMATCH");

   // Probe 2: 300x200 (logical)
   glfwSetWindowSize(win,300,200);
   glfwPollEvents();
   int fbw2=0,fbh2=0;
   glfwGetFramebufferSize(win,&fbw2,&fbh2);
   float s2;int ox2,oy2;
   mtl.getPresentTransform(s2,ox2,oy2);
   float eScale2=((float)fbw2/canW<(float)fbh2/canH)?(float)fbw2/canW:(float)fbh2/canH;
   int eOx2=((int)fbw2-(int)(canW*eScale2))/2;
   int eOy2=((int)fbh2-(int)(canH*eScale2))/2;
   printf("resize: 300x200 (fb %dx%d) -> scale=%.6f ox=%d oy=%d (expected scale=%.6f ox=%d oy=%d) %s\n",
          fbw2,fbh2,(double)s2,ox2,oy2,(double)eScale2,eOx2,eOy2,
          (ox2==eOx2&&oy2==eOy2)?"MATCH":"MISMATCH");

   // Restore canonical size so the readback dump above is unaffected (it
   // already ran; this is just tidy).
   glfwSetWindowSize(win,canW,canH);
   glfwPollEvents();
  }

  mtl.deleteTexture(tChecker);
  mtl.deleteTexture(tContent);
  mtl.deleteTexture(tMask);
  mtl.deleteTexture(rt);
  mtl.shutdown();
  printf("mtlmethods: PASS\n");
  return 0;
 }
