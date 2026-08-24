#ifndef _XAST_WINDOWSIZE_H
#define _XAST_WINDOWSIZE_H

// Shared initial-window-size formula (D15 pass 2; extracted verbatim from the
// pre-task-12 stage.H:147-163 math). ONE formula, two callers, no divergence:
// X11Backend::initWindow pass 2 and the Stage ctor both call Compute() with
// identical inputs, so the backend-created window and Stage's layout math can
// never disagree on the canonical client size.
//
// Layering note (task 12): the plan sketch places this static in stage.H, but
// the backend cannot include stage.H (gamePlay dependency inversion), so the
// single definition lives in this neutral header; Stage calls it from stage.H.
// The file is whitelist-clean (F1): pure FontMetrics arithmetic, zero X11.
//
// It also hosts the engine font-id enumeration: renderingEngine.H is frozen
// verbatim from the plan sketch (task 11) and carries no font ids, so the
// transitional home for them is here until a later task re-homes them.

#include"renderingEngine.H"

// Engine font ids (order = the pre-task-12 stage.H:138-142 load order).
enum EngineFontId
 {EngineFont_Title=0,
  EngineFont_Button,
  EngineFont_HiScore,
  EngineFont_Score,
  EngineFont_Error};

struct WindowSizeFormula
 {
  // titleTextWidth: advance width of the title string ("Asteroids"). The X11
  // caller passes the font's advance-width query result for that exact string
  // so the number feeding the formula is bit-identical to the pre-extraction
  // math (stage.H:147); Stage passes (int)engine->measureText(...), which on
  // X11 performs that same query. maxIconHeight: ShipGroup::maxIconHeight.
  // All FontMetrics fields are small integers held exactly in float, so every
  // cast below is exact and reproduces the original int/short arithmetic
  // bit-for-bit.
  static inline void Compute(const FontMetrics& titleFm,
                             const FontMetrics& hiScoreFm,
                             const FontMetrics& scoreFm,
                             int titleTextWidth,
                             int playAreaWidth, int playAreaHeight,
                             int maxIconHeight,
                             int& outWidth, int& outHeight)
   {int titleWidth=titleTextWidth+2*(int)titleFm.max_bounds.width,
         hiScoreWidth=10*(int)hiScoreFm.max_bounds.width,
         scoreWidth=10*(int)scoreFm.max_bounds.width,
         headerWidth=titleWidth+2*(hiScoreWidth>scoreWidth
                                   ? hiScoreWidth
                                   : scoreWidth),
         headerHeight;
    outWidth=(headerWidth>playAreaWidth?headerWidth:playAreaWidth)+30;
    headerHeight=2*((int)hiScoreFm.max_bounds.ascent+(int)hiScoreFm.max_bounds.descent)+4;
    if ((int)titleFm.max_bounds.ascent+(int)titleFm.max_bounds.descent>headerHeight)
      headerHeight=(int)titleFm.max_bounds.ascent+(int)titleFm.max_bounds.descent;
    if ((int)scoreFm.max_bounds.ascent+(int)scoreFm.max_bounds.descent>headerHeight)
      headerHeight=(int)scoreFm.max_bounds.ascent+(int)scoreFm.max_bounds.descent;
    outHeight=headerHeight+2*maxIconHeight+playAreaHeight+60;
   }
 };

#endif
