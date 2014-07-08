/** \file
 * \ingroup fonts
 *
 * $HopeName: SWv20!src:showops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Show operator variants for PostScript.
 */

#include "core.h"
#include "showops.h"

#include "swerrors.h"
#include "mm.h"
#include "mmcompat.h"
#include "objects.h"
#include "dicthash.h"
#include "fonth.h"
#include "namedef_.h"

#include "constant.h"     /* EPSILON */
#include "control.h"
#include "matrix.h"
#include "graphics.h"
#include "gs_color.h"     /* GSC_FILL */
#include "gstate.h"
#include "gu_path.h"
#include "pathops.h"
#include "gu_ctm.h"
#include "ndisplay.h"     /* for routedev.h */
#include "routedev.h"
#include "stacks.h"
#include "system.h"
#include "miscops.h"
#include "fcache.h"
#include "gu_cons.h"
#include "rectops.h"
#include "utils.h"
#include "display.h" /* dl_safe_recursion */
#include "vndetect.h"
#include "cidfont.h"
#include "cmap.h"
#include "charsel.h"
#include "swpdfout.h"
#include "params.h"
#include "idlom.h"

/*
   Exported variables
   ==================
*/
int32       fid_count = 0;  /* Stores the maximum fid count - if > 0, ==> user defined font */

void init_C_globals_showops(void)
{
  fid_count = 0;
}

/* ----------------------------------------------------------------------------
   function:            rootfont()
   creation date:       23-Apr-1990
   arguments:           none .
   description:

   returns the rootfont of the current composite font.

   See Composite Font Extensions, page 10

   [Note that, despite the appearance on page 11 of a modified currentfont
   operator, the code in fact needs no changes because this is how the
   current font is indeed set up while interpreting a composite font.]
   -except for the fact that LWs are different, so this is not true-
---------------------------------------------------------------------------- */
Bool rootfont_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push(&theFontInfo(*gstateptr).rootfont, &operandstack) ;
}

/* ----------------------------------------------------------------------------
   function:            currfont_()        author:              Andrew Cave
   creation date:       16-Oct-1987        last modification:   ##-###-####
   function references: push_opd(..) .
   arguments:           none.
   description:

   See PostScript reference manual page 136.

---------------------------------------------------------------------------- */
Bool currfont_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push( & ( theMyFont( theFontInfo( *gstateptr ))) , & operandstack ) ;
}

/* ------------------------------------------------------------------------- */
Bool init_charpath(int32 stroke )
{
  CHARPATHS *chpath ;
  PATHINFO *lpath = &(thePathInfo(*gstateptr)) ;
  LINELIST *theline = lpath->lastline ;
  PATHLIST *ppath, *cpath ;

  if ( ! path_moveto( theX(thePoint(*theline)) ,
                      theY(thePoint(*theline)) ,
                      MYMOVETO, lpath )) {
    return FALSE ;
  }

  if ( NULL == ( chpath = get_charpaths()))
    return error_handler( VMERROR ) ;

  if ( pdfout_enabled() &&
       !pdfout_begincharpath(get_core_context_interp()->pdfout_h, lpath) ) {
    free_charpaths() ;
    return FALSE ;
  }

  ppath = lpath->firstpath ;
  if ( (cpath = ppath->next) == NULL ) {
    thePathOf(*chpath) = & lpath->firstpath ;
  } else {
    PATHLIST *npath ;
    while (( npath = cpath->next) != NULL) {
      ppath = cpath ;
      cpath = npath ;
    }
    thePathOf(*chpath) = & ppath->next ;
  }

  thePathInfoOf(*chpath) = lpath ;
  doStrokeIt(*chpath) = stroke ;

  /* less protection at level2: */
  if ( isEncrypted(theFontInfo(*gstateptr)) &&
       !font_protection_override(isEncrypted(theFontInfo(*gstateptr)),
                                 FONT_OVERRIDE_CHARPATH) ) {
    if ( ! thePathInfo(*gstateptr).protection )
      thePathInfo(*gstateptr).protection = isEncrypted(theFontInfo(*gstateptr)) ;
    else if ( isEncrypted(theFontInfo(*gstateptr)) != (thePathInfo(*gstateptr)).protection )
      thePathInfo(*gstateptr).protection = PROTECTED_BLANKET;
  }

  return TRUE ;
}

/* ------------------------------------------------------------------------- */
Bool end_charpath(Bool result)
{

  if ( pdfout_enabled() ) {
    result = pdfout_endcharpath(get_core_context_interp()->pdfout_h, result) ;
  }

  free_charpaths() ;

  return result ;
}

/**
  Character selector providers.

  The composite selector performs font encoding manipulations, traversing
  the font hierarchy as necessary, and extracting characters from a string
  object.

  The base selector extracts a single character from a base font.

  The ancestor selector is used for CID descendents of cshow calls; it returns
  the selector used by the parent call. The transitive selector is similar to
  the ancestor selector, but does not install the Type 3 saved CID into the
  selector's CID field. */

Bool char_base_selector(void *data, char_selector_t *selector, Bool *eod)
{
  OBJECT *stringo = data ;
  int32 fonttype ;

  HQASSERT(stringo, "No string to extract selector from") ;
  HQASSERT(selector, "No destination for character selector") ;
  HQASSERT(eod, "No end of data pointer") ;

  fonttype = theFontType(theFontInfo(*gstateptr)) ;
  HQASSERT(fonttype != FONTTYPE_0, "Don't call base selector for composite font") ;

  if ( FONT_IS_CID(fonttype) )
    return error_handler(INVALIDFONT) ;

  if ( theLen(*stringo) == 0 ) {
    *eod = TRUE ;
    return TRUE ;
  }

  oString(selector->string) = oString(*stringo)++ ;
  if (--theLen(*stringo) == 0)
    oString(*stringo) = NULL;

  theLen(selector->string) = 1 ;
  theTags(selector->string) = OSTRING|LITERAL ;
  OCopy(selector->complete, selector->string) ;
  selector->index = selector->cid = *oString(selector->string) ;
  selector->name = NULL ;
  OCopy(selector->font, theMyFont(theFontInfo(*gstateptr))) ;
  theTags(selector->cmap) = ONULL ;
  theTags(selector->parent) = ONULL ;
  selector->type3cid = -1 ;

  *eod = FALSE ;

  return TRUE ;
}

Bool char_ancestor_selector(void *data, char_selector_t *selector, Bool *eod)
{
  char_selector_t **parent = (char_selector_t **)data ;

  HQASSERT(parent, "No previous selector address pointer") ;
  HQASSERT(selector, "No destination for character selector") ;
  HQASSERT(eod, "No end of data pointer") ;

  if ( *parent == NULL ) {
    *eod = TRUE ;
  } else {
    *selector = **parent ; /* Return previous selector */
    OCopy(selector->font, theMyFont(theFontInfo(*gstateptr))) ;
    if ( selector->type3cid >= 0 ) {
      selector->cid = selector->type3cid ;
      selector->name = NULL ;
    }
    *parent = NULL ;       /* But only do it once */
    *eod = FALSE ;
  }

  return TRUE ;
}

Bool char_transitive_selector(void *data, char_selector_t *selector, Bool *eod)
{
  char_selector_t **parent = (char_selector_t **)data ;

  HQASSERT(parent, "No previous selector address pointer") ;
  HQASSERT(selector, "No destination for character selector") ;
  HQASSERT(eod, "No end of data pointer") ;

  if ( *parent == NULL ) {
    *eod = TRUE ;
  } else {
    *selector = **parent ; /* Return previous selector */
    OCopy(selector->font, theMyFont(theFontInfo(*gstateptr))) ;
    selector->name = NULL ;
    *parent = NULL ;       /* But only do it once */
    *eod = FALSE ;
  }

  return TRUE ;
}

/*
Log stripped */
