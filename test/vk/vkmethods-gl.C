// test/vk/vkmethods-gl.C — the GL REFERENCE leg of the task-42 pre-Q11
// identity probe. Renders identityScene.H through glBackend under one Xvfb
// display and dumps the normalized (top-down RGBA) framebuffer to argv[1].
// The VK leg (vkmethods.C) renders the same scene and byte-compares.
//
// Readback happens BEFORE endFrame's swap: glReadPixels on the bound back
// buffer, rows flipped to top-down so both legs share one orientation.
//
// Built by the makefile's obj/VK/vkmethods-gl rule (glad + stbTruetypeImpl;
// no ImGui symbols are odr-used here). Run from the repo root under its own
// Xvfb display (/proc/self/exe-relative font + shader resolution).
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>       // readlink (glBackend.H's font-path resolution)

#include"../../utilities/rendering/glBackend.H"
#include"identityScene.H"

int main(int argc,char** argv)
 {if (argc<2)
   {fprintf(stderr,"usage: vkmethods-gl <out.raw>\n");
    return 2;
   }
  GLBackend gl;
  gl.setCanonicalLayout(640,512,44);
  if (!gl.initWindow("vkmethods-gl"))
   {fprintf(stderr,"vkmethods-gl: FAIL: initWindow\n");
    return 1;
   }

  uint8_t checker[8*8*4],content[16*16*4],mask[16*16];
  buildCheckerPixels(checker);
  buildQuadContentPixels(content);
  buildQuadMaskPixels(mask);
  TextureId tChecker=gl.createTextureFromBitmap(checker,8,8,4);
  TextureId tContent=gl.createTextureFromBitmap(content,16,16,4);
  TextureId tMask=gl.createTextureFromBitmap(mask,16,16,1);
  if (!tChecker||!tContent||!tMask)
   {fprintf(stderr,"vkmethods-gl: FAIL: texture creation\n");
    return 1;
   }

  SceneMasks masks;
  memset(&masks,0,sizeof masks);
  int fw=0,fh=0;
  glfwGetFramebufferSize(gl.window,&fw,&fh);

  gl.beginFrame();
  renderIdentityScene(gl,tChecker,tContent,tMask,&masks);
  glPixelStorei(GL_PACK_ALIGNMENT,1);
  uint8_t* raw=(uint8_t*)malloc((size_t)fw*fh*4);
  if (!raw)
   {fprintf(stderr,"vkmethods-gl: FAIL: alloc\n");
    return 1;
   }
  glReadPixels(0,0,fw,fh,GL_RGBA,GL_UNSIGNED_BYTE,raw);
  gl.endFrame();

  // Flip bottom-up -> top-down; write raw RGBA + the mask rects header.
  uint8_t* out=(uint8_t*)malloc((size_t)fw*fh*4);
  for (int y=0;y<fh;++y)
    memcpy(out+(size_t)y*fw*4,raw+(size_t)(fh-1-y)*fw*4,(size_t)fw*4);

  FILE* f=fopen(argv[1],"wb");
  if (!f)
   {fprintf(stderr,"vkmethods-gl: FAIL: open %s\n",argv[1]);
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
  fwrite(out,(size_t)fw*fh*4,1,f);
  fclose(f);
  printf("vkmethods-gl: wrote %s (%dx%d, %d text rects)\n",argv[1],fw,fh,
         masks.numTextRects);

  free(raw);
  free(out);
  gl.deleteTexture(tChecker);
  gl.deleteTexture(tContent);
  gl.deleteTexture(tMask);
  gl.shutdown();
  printf("vkmethods-gl: PASS\n");
  return 0;
 }
