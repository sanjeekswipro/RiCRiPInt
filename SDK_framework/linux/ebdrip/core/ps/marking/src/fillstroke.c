/** \file
 * \ingroup ps
 *
 * $HopeName: COREps!marking:src:fillstroke.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * All PS fill and stroke operators
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "namedef_.h"
#include "mmcompat.h"

#include "stacks.h"
#include "graphics.h"
#include "gstack.h"
#include "gs_color.h"
#include "gu_fills.h"
#include "gu_rect.h"
#include "pathops.h"
#include "pathcons.h"
#include "rectops.h"
#include "vndetect.h"

/* ----------------------------------------------------------------------------
   function:            fill_()            author:              Andrew Cave
   creation date:       11-Nov-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 156.

---------------------------------------------------------------------------- */
Bool fill_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( !dofill(&thePathInfo(*gstateptr), NZFILL_TYPE, GSC_FILL, FILL_NORMAL) )
    return FALSE;

  return gs_newpath();
}


/* ----------------------------------------------------------------------------
   function:            eofill_()          author:              Andrew Cave
   creation date:       11-Nov-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 150.

---------------------------------------------------------------------------- */
Bool eofill_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( !dofill(&thePathInfo(*gstateptr), EOFILL_TYPE, GSC_FILL, FILL_NORMAL) )
    return FALSE;

  return gs_newpath();
}

Bool rectfill_(ps_context_t *pscontext)
{
  Bool result ;
  int32 nrects = STACK_RECTS ;
  RECTANGLE stackrects[ STACK_RECTS ] ;
  RECTANGLE *rects = stackrects ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ! get_rect_op_args( & operandstack , & rects , & nrects , NULL , NULL ))
    return FALSE ;

  if ( nrects == 0 )
    return TRUE ;

  result = dorectfill( nrects , rects , GSC_FILL, RECT_NORMAL ) ;

  if ( rects != stackrects )
    mm_free_with_header(mm_pool_temp,  rects ) ;

  return result ;
}


/* ----------------------------------------------------------------------------
   function:            stroke_()          author:              Andrew Cave
   creation date:       11-Nov-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 229.

---------------------------------------------------------------------------- */

Bool stroke_(ps_context_t *pscontext)
{
  STROKE_PARAMS params ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  set_gstate_stroke(&params, &thePathInfo(*gstateptr), NULL, FALSE) ;

  /* This is the only place where dostroke gets called with dovignettedetection
   * as TRUE. This is because the current vignette detection code can only
   * cope with simple stroked vignettes. Later on may change.
   */
  if ( ! dostroke( & params , GSC_FILL , STROKE_NORMAL )) {
    return FALSE ;
  }

  return gs_newpath() ;
}

/* ----------------------------------------------------------------------------
   function:            rectstroke_()                 author:   Luke Tunmer
   creation date:       21-May-1991        last modification:   ##-###-####
   arguments:           none .
   description:

   Operator described on page 474, PS-2.

---------------------------------------------------------------------------- */
Bool rectstroke_(ps_context_t *pscontext)
{
  RECTANGLE *rects ;
  RECTANGLE stackrects[STACK_RECTS];
  OMATRIX   matrix ;
  int32     number ;
  Bool      result ;
  Bool      got_matrix ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ! flush_vignette( VD_Default ))
    return FALSE ;

  /* get the arguments off the stack, may be prefixed by a matrix operand */
  rects = stackrects ;
  number = STACK_RECTS ;

  if ( ! get_rect_op_args(&operandstack , &rects , &number , & matrix , &got_matrix ))
    return FALSE ;

  if ( number == 0 )
    return TRUE ;

  result = strokerectangles( GSC_FILL , rects , number , got_matrix ? & matrix : NULL ) ;

  if ( rects != stackrects )
    mm_free_with_header(mm_pool_temp,  rects ) ;
  return ( result ) ;
}

/* Log stripped */
