/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:nsidetst.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS path insideness tests
 */

#include "core.h"
#include "mm.h"
#include "mmcompat.h"
#include "swerrors.h"
#include "scanconv.h"

#include "objects.h"
#include "bitblts.h"
#include "matrix.h"
#include "display.h"
#include "graphics.h"
#include "gs_color.h"     /* GSC_ILLEGAL */
#include "gstate.h"
#include "pathcons.h"
#include "pathops.h"
#include "clippath.h"
#include "system.h"
#include "stacks.h"
#include "control.h"

#include "utils.h"
#include "ndisplay.h"
#include "dl_bres.h"
#include "swdevice.h"
#include "render.h"
#include "devops.h"

#include "gu_path.h"

#include "upath.h"
#include "uparse.h"

#include "dl_free.h"
#include "dlstate.h"
#include "nsidetst.h"

#define INFILL 0
#define INSTROKE 1

/* local function declarations */

static Bool in_fill( int32 filltype ) ;
static Bool inupath( int32 matrixok, int32 intype, int32 filltype ) ;
static Bool inpath_common( int32 filltype, PATHLIST *inpath, int32 nargs ) ;

/* Insideness testing operators testing current path */

Bool infill_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return in_fill(NZFILL_TYPE) ;
}

Bool ineofill_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return in_fill(EOFILL_TYPE) ;
}

static Bool in_fill( int32 filltype )
{
  int32    result = FALSE ;
  PATHINFO *lpath = &(thePathInfo(*gstateptr)) ;
  LINELIST *templine = lpath->lastline ;

  /* We don't test if the path is degenerate here, because we need to call
     inpath_common to parse the arguments correctly. It will return from
     point_in_path or path_in_path if the path is degenerate. */
  if ( path_close(TEMPORARYCLOSE, lpath) ) {
    result = inpath_common(filltype, lpath->firstpath, 0) ;

    if ( templine != lpath->lastline ) {    /* restore previous end */
      HQASSERT(templine->next == lpath->lastline,
               "Current path changed");
      templine->next = NULL ;
      free_line( lpath->lastline, mm_pool_temp );
      lpath->lastline = templine ;
    }
  }

  return result ;
}

Bool instroke_(ps_context_t *pscontext)
{
  int32 result ;
  STROKE_PARAMS params ;
  PATHINFO spath ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  set_gstate_stroke(&params, &thePathInfo(*gstateptr), &spath, FALSE) ;

  if ( ! dostroke(&params, GSC_ILLEGAL, STROKE_NOT_VIGNETTE) )
    return FALSE ;

  HQASSERT(! thePath(spath) ||
           theILineType(spath.lastline) == MYCLOSE ||
           theILineType(spath.lastline) == CLOSEPATH,
           "Path from stroked dostroke is NULL") ;

  result = inpath_common(NZFILL_TYPE, thePath(spath), 0) ;

  path_free_list(thePath(spath), mm_pool_temp) ; /* dispose of stroked path */

  return result ;
}

/* Insideness testing operators using userpath. These call inupath, which in
   turn calls inpath_common with the appropriate paths. */

Bool inufill_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return inupath(FALSE, INFILL, NZFILL_TYPE) ;
}

Bool inueofill_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return inupath(FALSE, INFILL, EOFILL_TYPE) ;
}

Bool inustroke_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return inupath(TRUE, INSTROKE, NZFILL_TYPE) ;
}

static Bool inupath( int32 matrixok, int32 intype, int32 filltype )
{
  OMATRIX adjustment_matrix ;
  OBJECT *arg ;
  PATHINFO upath ;
  Bool has_matrix = FALSE ;
  int32 nargs = 0 ;
  Bool result ;

  if ( isEmpty(operandstack) )
    return error_handler(STACKUNDERFLOW) ;

  arg = theTop(operandstack) ;
  if ( matrixok )
    if ( (has_matrix = is_matrix_noerror(arg, & adjustment_matrix)) != (int32)0 )
      nargs++ ;

  if ( theStackSize(operandstack) < nargs )
    return error_handler(STACKUNDERFLOW) ;

  arg = stackindex(nargs++, &operandstack) ;

  if ( ! upath_to_path(arg, UPARSE_CLOSE, &upath) )
    return FALSE ;

  /* don't bother stroking upath if it is degenerate, result is the same */
  if ( intype == INSTROKE && thePath(upath) ) { /* convert to outline */
    STROKE_PARAMS params ;

    set_gstate_stroke(&params, &upath, &upath, FALSE) ; /* replace path */

    if ( has_matrix ) {
      params.usematrix = TRUE ;
      matrix_mult( &adjustment_matrix, &thegsPageCTM(*gstateptr), &params.orig_ctm ) ;
    }

    if ( ! dostroke(&params, GSC_ILLEGAL, STROKE_NOT_VIGNETTE) ) { /* replaces original path */
      path_free_list(thePath(upath), mm_pool_temp) ; /* dispose of original path on error */
      return FALSE ;
    }

    HQASSERT(! thePath(upath) ||
             theILineType(upath.lastline) == MYCLOSE ||
             theILineType(upath.lastline) == CLOSEPATH,
             "Path from stroked dostroke is NULL") ;
  }

  result = inpath_common(filltype, thePath(upath), nargs) ;

  path_free_list(thePath(upath), mm_pool_temp) ; /* dispose of path */

  return result ;
}

/* Common code for stroked insideness testing */

/* Common part of insideness testing for gstate paths and userpaths. Path to
   check inside is passed in as inpath, nargs gives number of operands on
   stack before call. */
static Bool inpath_common( int32 filltype, PATHLIST *inpath, int32 nargs )
{
  OBJECT *arg ;
  int32 type ;
  Bool result ;
  Bool inside ;

  if ( theStackSize(operandstack) < nargs )
    return error_handler(STACKUNDERFLOW) ;

  arg = stackindex(nargs++, &operandstack) ;

  type = oType(*arg) ;

  if ( type == OARRAY || type == OPACKEDARRAY ) { /* aperture intersection */
    PATHINFO appath ;

    if ( ! upath_to_path(arg, UPARSE_CLOSE, &appath) )
      return FALSE ;

    result = path_in_path(thePath(appath), inpath, filltype, &inside) ;

    path_free_list(thePath(appath), mm_pool_temp) ;
  } else { /* point in path */
    SYSTEMVALUE px, py, tx, ty ;

    if ( ! object_get_numeric(arg, &py) )
      return FALSE ;

    if ( theStackSize(operandstack) < nargs )
      return error_handler(STACKUNDERFLOW) ;

    arg = stackindex(nargs++, &operandstack) ;

    if ( ! object_get_numeric(arg, &px) )
      return FALSE ;

    MATRIX_TRANSFORM_XY( px, py, tx, ty, & thegsPageCTM(*gstateptr)) ;

    result = point_in_path(tx, ty, inpath, filltype, &inside) ;
  }

  if ( result ) { /* remove consumed operands */
    npop(nargs, &operandstack) ;
    result = push((inside ? &tnewobj : &fnewobj), &operandstack) ;
  }

  return result ;
}

/* Test if point is inside a path. This is done using the normal
   scan-conversion algorithm and a span function that sets a static variable.
   Note that clipping to the point to be tested means that the span function
   will only be called if the point is in the span, i.e. inside the fill. */

static FORM nsidetstform ; /* dummy form; never actually used */

/*ARGSUSED*/
static void nsidetstspan(render_blit_t *rb,
                         dcoord y , dcoord xs , dcoord xe )
{
  Bool *pinside ;

  UNUSED_PARAM(dcoord, y);
  UNUSED_PARAM(dcoord, xs);
  UNUSED_PARAM(dcoord, xe);

  HQASSERT(rb, "No render context for insideness testing") ;

  GET_BLIT_DATA(rb->blits, BASE_BLIT_INDEX, pinside) ;
  *pinside = TRUE ;
}

Bool point_in_path(SYSTEMVALUE px, SYSTEMVALUE py, PATHLIST *thepath,
                    int32 filltype, Bool *inside)
{
  DL_STATE *page = get_core_context_interp()->page;
  int32 ix, iy ;
  NFILLOBJECT *nfill ;
  render_state_t inside_rs ;
  blit_chain_t inside_blits ;
  render_forms_t inside_forms ;

  static blit_slice_t nsidetstslice = {
    nsidetstspan, invalid_block, invalid_snfill, invalid_char, invalid_imgblt
  } ;

  HQASSERT(filltype == NZFILL_TYPE || filltype == EOFILL_TYPE,
           "point_in_path: rule should be NZFILL_TYPE or EOFILL_TYPE");
  HQASSERT(inside, "point_in_path() called with NULL result pointer");

  *inside = FALSE ; /* default to outside */

  if ( ! thepath )  /* can't be inside a degenerate path */
    return TRUE ;

  SC_C2D_INT( ix , px ) ;
  SC_C2D_INT( iy , py ) ;

  /* Set clipping so that flattening removes all lines completely above,
     below, or to the right of the point */
  cclip_bbox.x1 = cclip_bbox.x2 = ix ;
  cclip_bbox.y1 = cclip_bbox.y2 = iy ;

  /* Flatten path */
  fl_setflat( theFlatness( theLineStyle(*gstateptr))) ;
  if ( ! make_nfill(page, thepath, NFILL_ISFILL, &nfill) )
    return FALSE ;

  if ( nfill == NULL ) /* definitely outside fill, clipped out completely */
    return TRUE;

  preset_nfill( nfill ) ;
  render_state_mask(&inside_rs, &inside_blits, &inside_forms, &invalid_surface,
                    &nsidetstform) ;
  RESET_BLITS(&inside_blits, &nsidetstslice, &nsidetstslice, &nsidetstslice) ;
  SET_BLIT_DATA(&inside_blits, BASE_BLIT_INDEX, inside) ;

  /* Set clipping to the point */
  bbox_store(&inside_rs.ri.clip, ix, iy, ix, iy);

  scanconvert_band(&inside_rs.ri.rb, nfill, filltype) ;

  free_fill( nfill, page ) ;
  return TRUE ;
}

/* Intersect a userpath aperture with a path. Fill rule applied to aperture
   in NZFILL_TYPE, fill type applied to path is filltype. Note that makeconvex
   and cliptoall free their argument paths, so only the flattened paths
   need to be freed */
Bool path_in_path(PATHLIST *appath, PATHLIST *path, int32 filltype,
                  Bool *inside)
{
  PATHINFO apath, fpath ;

  if ( ! appath || ! path) { /* can't be inside a degenerate path */
    *inside = FALSE ;
    return TRUE ;
  }

  /* prepare both paths, using filltype for path and NZFILL for aperture */
  fl_setflat( theFlatness( theLineStyle(*gstateptr))) ;
  if ( ! path_flatten(appath, &apath) ||
       ! normalise_path(&apath, NZFILL_TYPE, FALSE, FALSE) )
    return FALSE ;

  if ( ! thePath(apath) ) {
    *inside = FALSE ;
    return TRUE ;
  }

  if ( ! path_flatten(path, &fpath) ||
       ! normalise_path(&fpath, filltype, FALSE, FALSE) ) {
    path_free_list(thePath(apath), mm_pool_temp) ;
    return FALSE ;
  }

  if ( ! thePath(fpath) ) {
    path_free_list(thePath(apath), mm_pool_temp) ;
    *inside = FALSE ;
    return TRUE ;
  }

  /* intersect aperture and path */
  if ( ! cliptoall(&apath, &fpath, &apath, FALSE) )
    return FALSE ;

  if ( thePath(apath) ) {
    *inside = TRUE ; /*  if it's not degenerate, aperture is painted */
    path_free_list(thePath(apath), mm_pool_temp) ;
  } else
    *inside = FALSE ; /* if clip is degenerate, it's not inside */

  return TRUE ;
}


/* Log stripped */
