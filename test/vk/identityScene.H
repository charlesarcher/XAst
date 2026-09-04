// test/vk/identityScene.H — the deterministic cross-backend scene for the
// task-42 pre-Q11 GL-vs-VK identity probe. One draw script, parameterized
// over RenderingEngine& so glBackend and vkBackend execute BYTE-IDENTICAL
// API call sequences (the D8 canonical order). Texture contents are built
// here too, so both legs upload identical bytes.
//
// The scene covers every task-42 method family on the window target:
//   thin/thick lines, filled + outlined polygons, rects (41 pipelines),
//   opaque/half-alpha textured quads, a 45-degree ROTATED textured quad
//   (D2 texture regime), a masked content+R8-mask draw (D6), a NonRot-style
//   static-texture draw issued with ZERO setTransform calls (m13), and two
//   text runs (title + score fonts — the documented text mask class).
//
// Text bounding boxes are recorded in SceneMasks so the comparison phase can
// exclude them (the plan's Tier-2 regime compares NON-TEXT pixels; text is
// judged via its own mask class).
#ifndef _XAST_IDENTITYSCENE_H
#define _XAST_IDENTITYSCENE_H

#include"../../utilities/rendering/renderingEngine.H"
#include<string.h>
#include<math.h>

struct SceneMasks
 {float textRects[8][4];      // x,y,w,h in logical client coords
  int numTextRects;
 };

// 8x8 checker: left half white, right half black, fully opaque.
inline void buildCheckerPixels(uint8_t* p)
 {for (int y=0;y<8;++y)
   for (int x=0;x<8;++x)
    {uint8_t v=(x<4)?255:0;
     uint8_t* q=p+(y*8+x)*4;
     q[0]=v;q[1]=v;q[2]=v;q[3]=255;
    }
 }

// 16x16 RGBA content: red/green/blue/yellow quadrants, opaque.
inline void buildQuadContentPixels(uint8_t* p)
 {for (int y=0;y<16;++y)
   for (int x=0;x<16;++x)
    {uint8_t* q=p+(y*16+x)*4;
     q[0]=(x<8)?255:0;
     q[1]=(y<8)?255:0;
     q[2]=((x<8)==(y<8))?0:255;
     q[3]=255;
    }
 }

// 16x16 R8 coverage mask: left half on, right half off (the discard half).
inline void buildQuadMaskPixels(uint8_t* p)
 {for (int y=0;y<16;++y)
   for (int x=0;x<16;++x)
     p[y*16+x]=(x<8)?255:0;
 }

inline void recordTextRect(SceneMasks* m,float x,float y,float w,float h)
 {if (!m||m->numTextRects>=8)
   return;
  float* r=m->textRects[m->numTextRects];
  r[0]=x;r[1]=y;r[2]=w;r[3]=h;
  ++m->numTextRects;
 }

inline void renderIdentityScene(RenderingEngine& e,TextureId checker,
                                TextureId content,TextureId mask,
                                SceneMasks* masks)
 {// -- primitives (the GL task-32 scene family) --------------------------
  e.drawLine(50.0f,100.0f,250.0f,100.0f,1.0f,0.0f,0.0f,1.0f);
  float tri[6]={300,80, 420,80, 360,180};
  e.drawPolygon(tri,3,0.0f,1.0f,0.0f,true);
  float sq[8]={450,250, 550,250, 550,350, 450,350};
  e.drawPolygon(sq,4,0.0f,0.0f,1.0f,false);
  e.drawLine(60.0f,400.0f,260.0f,400.0f,1.0f,1.0f,1.0f,5.0f);
  e.drawRect(430.0f,420.0f,80.0f,60.0f,0.0f,1.0f,1.0f,false);
  // -- textures ------------------------------------------------------------
  e.drawTexture(checker,60.0f,220.0f,120.0f,120.0f,1.0f);
  e.drawTexture(checker,220.0f,220.0f,120.0f,120.0f,0.5f);
  // -- D2 rotation regime: wireframe MVP + rotated textured quad -----------
  e.setTransform(320.0f,560.0f,0.7853981633974483f);   // 45 degrees
  e.drawLine(-40.0f,0.0f,40.0f,0.0f,1.0f,0.0f,0.0f,1.0f);
  e.drawTexture(checker,-60.0f,-60.0f,120.0f,120.0f,1.0f);
  e.resetTransform();
  // -- D6 masked draw -------------------------------------------------------
  e.drawTextureMasked(content,mask,500.0f,80.0f,64.0f,64.0f);
// -- m13 NonRot degenerate path: static texture, NO transform calls ------
   e.drawTexture(checker,600.0f,600.0f,40.0f,40.0f,1.0f);
   // -- Task 15: asymmetric top-vs-bottom orientation markers ---------------
   // One solid quad exists ONLY at the top edge (cyan, rows 4..16) and one
   // ONLY at the bottom edge (magenta, rows 496..508) of the 640x512 canvas;
   // neither overlaps any other scene content. A vertical flip remaining in
   // exactly one stage of the pipeline (MVP y-scale or readback row order)
   // swaps the two halves and this breaks both the cross-backend byte-compare
   // and the MTL probe's self-check. Both gate legs render these through the
   // shared drawRect, so the markers are backend-neutral.
   e.drawRect(10.0f,4.0f,48.0f,12.0f,0.0f,1.0f,1.0f,true);     // top: cyan
   e.drawRect(10.0f,496.0f,48.0f,12.0f,1.0f,0.0f,1.0f,true);   // bottom: magenta
  // -- text (recorded as the text mask class) --------------------------------
  if (masks)
   {const char* t1="VKGL";
    const char* t2="0123456789";
    float w1=e.measureText(t1,0);
    float w2=e.measureText(t2,3);
    FontMetrics fm1,fm2;
    e.getFontMetrics(0,fm1);
    e.getFontMetrics(3,fm2);
    e.drawStringTransparent(t1,60.0f,70.0f,0,1.0f,1.0f,1.0f);
    e.drawStringOpaque(t2,420.0f,690.0f,3,1.0f,1.0f,1.0f,0.0f,0.0f,0.5f);
    recordTextRect(masks,60.0f,70.0f-fm1.max_bounds.ascent,w1,
                   fm1.max_bounds.ascent+fm1.max_bounds.descent);
    recordTextRect(masks,420.0f,690.0f-fm2.max_bounds.ascent,w2,
                   fm2.max_bounds.ascent+fm2.max_bounds.descent);
   }
 }
#endif
