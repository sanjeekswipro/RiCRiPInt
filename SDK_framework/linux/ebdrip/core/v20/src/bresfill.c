/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:bresfill.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Filli of an arbitrary n-vertex polygon.
 */

#include "core.h"
#include "swdevice.h"
#include "mm.h"
#include "mmcompat.h"
#include "scanconv.h" /* from CORErender */
#include "fonts.h"    /* from COREfonts */

#include "ripdebug.h"

#include "bitblts.h"
#include "display.h"
#include "dlstate.h"
#include "ndisplay.h"
#include "matrix.h"
#include "graphics.h"
#include "dl_bres.h"
#include "render.h"
#include "often.h"
#include "devops.h"

#include "bresfill.h"
#include "dl_free.h"

/* ----------------------------------------------------------------------------
   function:            bressfill( .. )     author:              Andrew Cave
   creation date:       01-Aug-1989        last modification:   ##-###-####
   arguments:           therule , bressptr .
   description:

   This function does a fill on an n-vertex polygon which is passed in as
   a linked list of sub-paths ( pathptr - which make up a complete path ).
   It uses only integer arithmetic and cohesion in the lines to do a quick
   fill.  The type of fill is defined by therule.

---------------------------------------------------------------------------- */
Bool fillnbressdisplay(DL_STATE *page, int32 therule, NFILLOBJECT *nfill)
{
  charcontext_t *charcontext = char_current_context() ;

  HQASSERT(charcontext, "No character context") ;
  HQASSERT ((therule & (~(ISCLIP|ISFILL|ISRECT|SPARSE_NFILL))) == NZFILL_TYPE ||
            (therule & (~(ISCLIP|ISFILL|ISRECT|SPARSE_NFILL))) == EOFILL_TYPE,
            "'therule' should be NZFILL_TYPE or EOFILL_TYPE");

  if ( nfill != NULL ) { /* Check for degenerate. */
    preset_nfill( nfill ) ;
    HQASSERT(RENDER_BLIT_CONSISTENT(charcontext->rb),
             "Character caching state not consistent") ;
    scanconvert_band(charcontext->rb, nfill,
                     therule & ( CLIPRULE | ISCLIP )) ;
    free_fill( nfill, page ) ;
  }
  return TRUE ;
}

Bool accfillnbressdisplay(DL_STATE *page, int32 therule, NFILLOBJECT *nfill,
                          uint32 flags)
{
  charcontext_t *charcontext = char_current_context() ;

  HQASSERT(charcontext, "No character context") ;
  HQASSERT((flags & SC_FLAG_SWAPXY) == 0,
           "Swap X/Y flag should not be set initially") ;
  HQASSERT((flags & SC_FLAG_ACCURATE) != 0,
           "Accurate rendering flag should be set initially") ;

  if ( nfill != NULL ) { /* Check for degenerate. */
    preset_nfill(nfill) ;
    HQASSERT(RENDER_BLIT_CONSISTENT(charcontext->rb),
             "Character caching state not consistent") ;
    scanconvert_char(charcontext->rb, nfill, therule, flags) ;
    if ( (flags & SC_FLAG_TWOPASS) != 0 ) {
      swapxy_nfill(nfill) ;
      preset_nfill(nfill) ;
      scanconvert_char(charcontext->rb, nfill, therule,
                       flags|SC_FLAG_SWAPXY) ;
    }
    free_fill(nfill, page) ;
  }
  return TRUE ;
}

/* Log stripped */
