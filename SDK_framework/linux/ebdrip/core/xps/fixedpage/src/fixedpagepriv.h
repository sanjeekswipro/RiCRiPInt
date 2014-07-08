/** \file
 * \ingroup fixedpage
 *
 * $HopeName: COREedoc!fixedpage:src:fixedpagepriv.h(EBDSDK_P.1) $
 * $Id: fixedpage:src:fixedpagepriv.h,v 1.23.10.1.1.1 2013/12/19 11:24:46 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * External declarations of callback function tables.
 *
 * The callback function tables are registered from
 * \c xmlcbRegisterFuncts_fixedpage.
 */

#ifndef __FIXEDPAGEPRIV_H__
#define __FIXEDPAGEPRIV_H__

#include "xpspriv.h"
#include "fixedpage.h"

extern xpsElementFuncts imagebrush_functions[] ;
extern xpsElementFuncts gradientbrush_functions[] ;
extern xpsElementFuncts visualbrush_functions[] ;
extern xpsElementFuncts solidbrush_functions[] ;
extern xpsElementFuncts canvas_functions[] ;
extern xpsElementFuncts geometry_functions[] ;
extern xpsElementFuncts glyph_functions[] ;
extern xpsElementFuncts path_functions[] ;
extern xpsElementFuncts documentsequence_functions[] ;
extern xpsElementFuncts fixeddocument_functions[] ;
extern xpsElementFuncts fixed_page_extension_functions[] ;

/** Set an XPS defined color. */
Bool xps_setcolor(/*@notnull@*/ /*@in@*/ xmlDocumentContext *xps_ctxt,
                  int32 colortype,
                  /*@notnull@*/ /*@in@*/ xps_color_designator *color_designator);

/** Are we in a path, and if so is it OK to expand a pattern? */
Bool path_pattern_valid(/*@notnull@*/ /*@in@*/ xpsCallbackState *state) ;

/** Are we in glyphs, and if so is it OK to expand a pattern? */
Bool glyphs_pattern_valid(/*@notnull@*/ /*@in@*/ xpsCallbackState *state) ;

/** Are we in a canvas, and if so is it OK to expand a pattern? */
Bool canvas_pattern_valid(/*@notnull@*/ /*@in@*/ xpsCallbackState *state) ;

/** Direct image handling is only enabled and supported for path fills. */
void allow_direct_image(xmlDocumentContext *xps_ctxt);
void disallow_direct_image(xmlDocumentContext *xps_ctxt);

/** Are we going to draw an image directly? */
Bool drawing_direct_image(xmlDocumentContext *xps_ctxt);

/** Draw the image directly or fallback to a pattern. */
Bool xps_draw_direct_image(xmlDocumentContext *xps_ctxt, Bool direct);

/** Paint style flags (note, these are bit flags)
 */
enum {
  XPS_PAINT_NONE   = 0,
  XPS_PAINT_FILL   = 1,
  XPS_PAINT_STROKE = 2
};

/* ============================================================================
* Log stripped */
#endif
