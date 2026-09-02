// test/vk/mtlmethods.C — task-6 self-diagnostic build for the Metal backend.
// Renders identityScene.H through MTLBackend on the window target and runs
// one frame, proving every draw method family links and executes without
// Metal validation errors. (The full MTL-vs-VK byte-compare is todo 11, which
// needs offscreen render targets from task 8.)
//
// Built by the makefile's obj/MTL/mtlmethods rule. Run from the repo root
// (font + metallib resolution is /proc/self/exe-relative).
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

  SceneMasks masks;
  memset(&masks,0,sizeof masks);

  mtl.beginFrame();
  renderIdentityScene(mtl,tChecker,tContent,tMask,&masks);
  mtl.endFrame();

  // Write a minimal header (framebuffer size + text rects) so the dump
  // format matches the todo-11 comparator; pixel data is empty here (the
  // window drawable is not CPU-readable — offscreen readback is task 8).
  // The window is created at the canonical size (640x512), so the framebuffer
  // size equals it at identity.
  int fw=640,fh=512;
  FILE* f=fopen(argv[1],"wb");
  if (!f)
   {fprintf(stderr,"mtlmethods: FAIL: open %s\n",argv[1]);
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
  fclose(f);
  printf("mtlmethods: wrote %s (%dx%d, %d text rects)\n",argv[1],fw,fh,
         masks.numTextRects);

  mtl.deleteTexture(tChecker);
  mtl.deleteTexture(tContent);
  mtl.deleteTexture(tMask);
  mtl.shutdown();
  printf("mtlmethods: PASS\n");
  return 0;
 }
