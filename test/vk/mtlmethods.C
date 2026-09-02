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

  // BGRA→RGBA swizzle + vertical flip: RT is BGRA8Unorm (task 8 pipeline
  // pixel format), and Metal NDC is y-UP (like GL, unlike VK's y-down), so
  // the raw readback has logical top at framebuffer bottom. Flip rows so the
  // dump is top-down RGBA matching the GL/VK reference orientation.
  uint8_t* rgba=(uint8_t*)malloc((size_t)fw*fh*4);
  if (!rgba)
   {fprintf(stderr,"mtlmethods: FAIL: alloc rgba\n");
    free(bgra);
    return 1;
   }
  for (int y=0;y<fh;++y)
   {const uint8_t* srcRow=bgra+(size_t)(fh-1-y)*fw*4;
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
  free(rgba);
  printf("mtlmethods: wrote %s (%dx%d, %d text rects, %d bytes total)\n",
         argv[1],fw,fh,masks.numTextRects,
         (int)(sizeof hdr+fw*fh*4));

  mtl.deleteTexture(tChecker);
  mtl.deleteTexture(tContent);
  mtl.deleteTexture(tMask);
  mtl.deleteTexture(rt);
  mtl.shutdown();
  printf("mtlmethods: PASS\n");
  return 0;
 }
