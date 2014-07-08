/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:upcache.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Userpath caching functions.
 */

#include "core.h"
#include "swerrors.h"
#include "swdevice.h"
#include "mm.h"
#include "mmcompat.h"
#include "objects.h"
#include "fontcache.h"    /* free_ccache */
#include "scanconv.h"

#include "pscontext.h"
#include "bitblts.h"
#include "matrix.h"
#include "display.h"
#include "graphics.h"
#include "gstate.h"
#include "pathcons.h"
#include "pathops.h"
#include "clippath.h"
#include "system.h"
#include "stacks.h"
#include "gu_path.h"
#include "devops.h"
#include "ndisplay.h"
#include "routedev.h"
#include "rlecache.h"
#include "dlstate.h"
#include "render.h"
#include "plotops.h"
#include "dl_bres.h"
#include "dl_free.h"
#include "clipops.h"
#include "fcache.h"
#include "gs_color.h"     /* GSC_ILLEGAL */

#include "params.h"
#include "swmemory.h"
#include "namedef_.h"     /* mark_ */

#include "trap.h"         /* isTrapEnabled() */

#include "upath.h"
#include "uparse.h"

#include "upcache.h"

/* Userpath caching.
   Userpaths are cached as RLE forms, using whichever RLE storage method is
   most efficient. The userpath cache is a multi-level structure:

   At the top level is an array of pointers to UPATHCACHE structures. This
   array is indexed by masking the low order bits of the userpath checksum.
   Each pointer points to a list of UPATHCACHES; the first step is to check if
   the checksums match.

   Each UPATHCACHE has a list of UMATRIXCACHE structures hanging off it; these
   contain the first four components of the matrix which the userpaths
   under it relate to (i.e. not the translation, which is irrelevant). The
   UMATRIXCACHE structure has a pointer to a list of USTROKECACHE structures,
   and two lists of UFILLCACHE structures, one for fills and one for eofills.

   The USTROKECACHE and UFILLCACHE structures contain all of the gstate
   information relevant to the userpath. For fills, this is just the
   flatness, but for strokes it is all of the line style parameters as well.
   Each USTROKECACHE or UFILLCACHE structure contains a pointer to a CHARCACHE
   structure; if this pointer is NULL, this indicates that an attempt has been
   made to cache the userpath, but it couldn't be done for some reason. This is
   used to indicate when it is a waste of time trying to cache the userpath.

   The CHARCACHE structure contains the information needed to render the
   userpath; the RLE form, the offsets of the form, and the page number
   information which is used to tell if the userpath can be flushed from the
   cache.

   The RLE data is generated directly by the Bresenham filling routines, called
   with the span function set to rle_dospan. This means that only one fill can
   be used to generate the points, because the dospan function relies on the
   scanlines being done in turn. This in turn means that strokes are generated
   by a strokepath/fill combination, to make them happen as one fill.

   Note that the RLE form data is allocated separately from the form header,
   and so must be freed separately.

   The userpath cache is a separate cache in its own right. If a userpath
   overflows the size of the cache, it will not be cached. In contrast, if
   caching a character in a font overflows the font cache limit, the
   character is cached and the purge flag is set.
 */

Bool setucacheparams_(ps_context_t *pscontext)
{
  int32 count = 1 ;
  Bool retval;

  for ( ; ; count++ ) {
    OBJECT *obj ;
    int32 otype;

    if ( isEmpty(operandstack) ) {
      retval = error_handler(UNMATCHEDMARK) ;
      break;
    }

    obj = theTop(operandstack) ;

    if ((otype = ( oType(*obj))) == OMARK) {
      pop( &operandstack ) ;
      retval = TRUE ;
      break;
    } else if (otype != OINTEGER) {
      retval = error_handler(TYPECHECK) ;
      break;
    }

    if ( count == 1 ) {
      int32 blimit = oInteger(*obj) ;

      if ( blimit < 0 ) {
        retval =  error_handler(RANGECHECK) ;
        break;
      }

      ps_core_context(pscontext)->userparams->MaxUPathItem = blimit ;
    }
    pop( &operandstack ) ;
  }

  return (retval) ;
}

Bool ucachestatus_(ps_context_t *pscontext)
{
  corecontext_t *context = pscontext->corecontext ;
  return (mark_(pscontext) &&
          stack_push_integer(context->systemparams->CurUPathCache, &operandstack) &&
          stack_push_integer(context->systemparams->MaxUPathCache, &operandstack) &&
          stack_push_integer(upaths_cached(), &operandstack) &&
          stack_push_integer(MAXINT32, &operandstack) &&
          stack_push_integer(context->userparams->MaxUPathItem, &operandstack));
}

/** The ucache operator is a no-op, which is only used for pattern matching */
Bool ucache_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return TRUE;
}


/* Log stripped */
