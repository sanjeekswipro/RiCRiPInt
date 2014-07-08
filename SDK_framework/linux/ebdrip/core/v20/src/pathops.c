/** \file
 * \ingroup paths
 *
 * $HopeName: SWv20!src:pathops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS path operators
 */

#include "core.h"
#include "pathops.h"
#include "swerrors.h"
#include "objects.h"
#include "namedef_.h"
#include "fonts.h"   /* charcontext_t */
#include "swpdfout.h"

#include "matrix.h"
#include "routedev.h"
#include "graphics.h"
#include "gstate.h"
#include "stacks.h"
#include "gu_path.h"
#include "pathcons.h"
#include "clipops.h"
#include "gu_ctm.h"
#include "system.h"
#include "gu_fills.h"
#include "plotops.h"
#include "adobe1.h"
#include "fcache.h"
#include "trap.h"
#include "tranState.h"
#include "render.h"
#include "pathops.h"
#include "params.h"
#include "display.h"
#include "idlom.h"
#include "gschead.h"
#include "vndetect.h"
#include "rcbcntrl.h"
#include "implicitgroup.h"
#include "stroker.h"
#include "wclip.h"


/** Apply the ForceStrokeAdjust SystemParam to the gstate strokeadjust value,
    and return whether or not to strokeadjust.

    N.B. If we can get rid of the compabibility parameter and case then this
    could be a more general tri-state with NO_FORCE meaning honour the gstate
    and FORCE_TRUE and FORCE_FALSE meaning override it.
*/
Bool ForceStrokeAdjustApply(Bool compatibility)
{
  Bool strokeadjust = (Bool) thegsDeviceStrokeAdjust(*gstateptr);

  HQASSERT ( SystemParams.ForceStrokeAdjust <= COMPATIBILITY_FALSE,
            "Invalid ForceStrokeAdjust value" );

  switch ((int32) SystemParams.ForceStrokeAdjust) {

    case NO_FORCE: /* Honour the gstate value */
    case COMPATIBILITY_FALSE:
      break;

    /* Override the gstate value unless doing compatibility stuff */
    case FORCE_TRUE:
      strokeadjust = TRUE;
      break;

    case FORCE_FALSE:
      strokeadjust = FALSE;
      break;

    case COMPATIBILITY_TRUE:
      if (! compatibility)
        strokeadjust = TRUE;
      break;
  }

  return strokeadjust;
}

/** Set the ForceStrokeAdjust SystemParam from the OBJECT passed in */
Bool set_ForceStrokeAdjust( struct SYSTEMPARAMS *systemparams, OBJECT *theo )
{
  HQASSERT (theo != NULL, "Object is null in set_ForceStrokeAdjust");

  if ( oType(*theo) == OBOOLEAN ) {
    if ( oBool(*theo) == FALSE )
       systemparams->ForceStrokeAdjust = COMPATIBILITY_FALSE;
    else
       systemparams->ForceStrokeAdjust = COMPATIBILITY_TRUE;
  }
  else if ( oType(*theo) == ONAME) {

    switch ( (oName(*theo))->namenumber ) {
      case NAME_Default:
        systemparams->ForceStrokeAdjust = NO_FORCE;
        break;

      case NAME_ForceTrue:
        systemparams->ForceStrokeAdjust = FORCE_TRUE;
        break;

      case NAME_ForceFalse:
        systemparams->ForceStrokeAdjust = FORCE_FALSE;
        break;

      default:
        return error_handler(RANGECHECK);
    }
  }
  else
    return error_handler(TYPECHECK);

  return TRUE;
}

/** Get the ForceStrokeAdjust SystemParam value and fill in result object
    appropriately for currentsystemparams. */
void get_ForceStrokeAdjust( struct SYSTEMPARAMS *systemparams, OBJECT *result)
{
  HQASSERT(result, "No object for systemparam result");
  HQASSERT(systemparams->ForceStrokeAdjust <= COMPATIBILITY_FALSE,
           "Unexpected value for SystemParam ForceStrokeAdjust");

  switch ((int32) systemparams->ForceStrokeAdjust) {
    case NO_FORCE:
      object_store_name(result, NAME_Default, LITERAL);
      break;

    case FORCE_TRUE:
      object_store_name(result, NAME_ForceTrue, LITERAL);
      break;

    case FORCE_FALSE:
      object_store_name(result, NAME_ForceFalse, LITERAL);
      break;

    case COMPATIBILITY_FALSE:
      object_store_bool(result, FALSE);
      break;

    case COMPATIBILITY_TRUE:
      object_store_bool(result, TRUE);
      break;
  }
}

/** Add a path segment to the char paths. Copy the path rather than patching it
   in if copycharpath is true. */
Bool add_charpath(PATHINFO *path, Bool copycharpath)
{
  PATHLIST *ppath, *npath, *tpath ;
  PATHINFO *cpath ;

  HQASSERT( path_assert_valid( path ) , "validation of path failed in add_charpath" ) ;

  HQASSERT( path, "argument pathinfo struct pointer NULL in add_charpath") ;
  HQASSERT( thecharpaths, "charpath struct pointer NULL in add_charpath") ;

  cpath = thePathInfoOf(*thecharpaths) ;

  HQASSERT( cpath, "charpath pathinfo struct pointer NULL in add_charpath") ;

  if ( NULL == (ppath = path->firstpath) )       /* Nothing to add? */
    return TRUE ;

  cpath->curved |= path->curved ;
  if (path->protection) {
    if ( ! cpath->protection )
      /* assign if not already protected */
      cpath->protection = path->protection;
    else if ( cpath->protection != path->protection )
      /* if already protected differently, blanket protection */
      cpath->protection = PROTECTED_BLANKET;
  }

  if ( cpath->bboxtype != BBOX_NOT_SET ) {
    /* enlarge bounding box (this is what Adobe do) */
    sbbox_t *bbox = & cpath->bbox ;
    sbbox_t bboxn ;

    (void)path_bbox(path, & bboxn, BBOX_IGNORE_LEVEL2|BBOX_SAVE|BBOX_LOAD) ;

    bbox_union(bbox, &bboxn, bbox) ;
  }

  if ( copycharpath ) { /* can't just patch in path passed to us */
    if ( ! path_copy_list(path->firstpath, &ppath,
                          NULL, NULL, mm_pool_temp) )
      return FALSE ;
  } else {
    path_init(path) ;   /* patch in path directly; clear original pathinfo */
  }

  tpath = *thePathOf(*thecharpaths) ; /* save tail of path */
  *thePathOf(*thecharpaths) = ppath ; /* patch in new path */

  HQASSERT( tpath, "No tail of destination path in add_charpath") ;

  while ( (npath = ppath->next) != NULL )
    ppath = npath ;

  ppath->next = tpath ;         /* restore tail of path */
  thePathOf(*thecharpaths) = & ppath->next ;

  return TRUE ;
}

Bool setbbox_(ps_context_t *pscontext)
{
  SYSTEMVALUE args[ 4 ] ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ! stack_get_numeric(&operandstack, args, 4) )
    return FALSE ;

  if ( ! set_bbox(args, & thePathInfo(*gstateptr)) )
    return FALSE ;

  npop(4, &operandstack);
  return TRUE ;
}

/** set_bbox sets the bounding box according to the array passed in.
   NOTE: This function leaves the values in the argument array unmodified. */
Bool set_bbox(SYSTEMVALUE args[ 4 ], PATHINFO *path)
{
  sbbox_t *bbox ;
  register int32 index, loop ;

  HQASSERT(path, "No path argument to set_bbox") ;
  bbox = & path->bbox ;

  if ( args[0] > args[2] || args[1] > args[3] )
    return error_handler(RANGECHECK);

  if ( path->bboxtype == BBOX_NOT_SET ) { /* initialise box */
    if ( path->lastline ) {         /* merge with gstate path box */
      (void)path_bbox(path, bbox, BBOX_IGNORE_NONE|BBOX_SAVE) ;
    } else {
      bbox_store(bbox, OINFINITY_VALUE, OINFINITY_VALUE,
                -OINFINITY_VALUE, -OINFINITY_VALUE) ;
    }
  }

  /* Transform args to device space, swapping llx and urx after the inner
     loop; this covers the points (urx,lly) and (llx,ury) as well, and
     leaves the values in the args array to their original values. */
  for ( loop = 0 ; loop < 2 ; loop++ ) {
    register SYSTEMVALUE temp, *argptr ;
    for ( index = 0, argptr = args ; index < 2 ; index++ ) {
      register SYSTEMVALUE x = *argptr++ ;
      register SYSTEMVALUE y = *argptr++ ;
      SYSTEMVALUE dx, dy ;

      MATRIX_TRANSFORM_XY( x, y, dx, dy, & thegsPageCTM(*gstateptr)) ;

      /* Do not use bbox_union_point because it assumes the bbox is non-empty
         to start. */
      bbox_union_coordinates(bbox, dx, dy, dx, dy) ;
    }
    temp = args[0] ; args[0] = args[2] ; args[2] = temp ;
  }

  path->bboxtype = BBOX_SETBBOX ;

  return TRUE;
}

/** transform_bbox transforms a bounding box to a different coordinate space,
   as specified by OMATRIX argument. It is safe to use same bbox for input
   and output. */
void bbox_transform(const sbbox_t *ibbox, sbbox_t *obbox, OMATRIX *mat)
{
  int32 i, maxdim ;
  SYSTEMVALUE x1 , x2 , y1 , y2 ;
  SYSTEMVALUE args[ 8 ] ;

  bbox_load(ibbox, x1, y1, x2, y2) ;

  if ( mat->opt != MATRIX_OPT_BOTH ) {
    MATRIX_TRANSFORM_XY( x1, y1, args[ 0 ], args[ 1 ], mat ) ;
    MATRIX_TRANSFORM_XY( x2, y2, args[ 2 ], args[ 3 ], mat ) ;
    maxdim = 4 ;
  }
  else {
    MATRIX_TRANSFORM_XY( x1, y1, args[ 0 ], args[ 1 ], mat ) ;
    MATRIX_TRANSFORM_XY( x2, y1, args[ 2 ], args[ 3 ], mat ) ;
    MATRIX_TRANSFORM_XY( x1, y2, args[ 4 ], args[ 5 ], mat ) ;
    MATRIX_TRANSFORM_XY( x2, y2, args[ 6 ], args[ 7 ], mat ) ;
    maxdim = 8 ;
  }

  x1 = x2 = args[ 0 ] ;
  y1 = y2 = args[ 1 ] ;
  for ( i = 2 ; i < maxdim ; i += 2 ) {
    SYSTEMVALUE tmp ;

    tmp = args[ i ] ;
    if ( tmp < x1 )
      x1 = tmp ;
    else if ( tmp > x2 )
      x2 = tmp ;

    tmp = args[ i + 1 ] ;
    if ( tmp < y1 )
      y1 = tmp ;
    else if ( tmp > y2 )
      y2 = tmp ;
  }

  bbox_store(obbox, x1, y1, x2, y2) ;
}

/* ----------------------------------------------------------------------------
   function:            pathbbox_()        author:              Andrew Cave
   creation date:       11-Nov-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 191.

---------------------------------------------------------------------------- */
Bool pathbbox_(ps_context_t *pscontext)
{
  sbbox_t bbox ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ! CurrentPoint )
    return error_handler( NOCURRENTPOINT ) ;

  /* Transform points by the inverse of the CTM. */
  SET_SINV_SMATRIX( & thegsPageCTM(*gstateptr) , NEWCTM_ALLCOMPONENTS ) ;
  if ( SINV_NOTSET( NEWCTM_ALLCOMPONENTS ) )
    return error_handler( UNDEFINEDRESULT ) ;

  /* We could use path_bbox_transform here, which would return a smaller bbox
     for non-orthogonal transformations, but would not be able to cache and
     re-use the results. */
  (void)path_bbox(&thePathInfo(*gstateptr), &bbox, BBOX_IGNORE_LEVEL2|BBOX_SAVE|BBOX_LOAD) ;

  bbox_transform(&bbox, &bbox, &sinv) ;

  return (stack_push_real( bbox.x1, &operandstack ) &&
          stack_push_real( bbox.y1, &operandstack ) &&
          stack_push_real( bbox.x2, &operandstack ) &&
          stack_push_real( bbox.y2, &operandstack )) ;
}

/* ----------------------------------------------------------------------------
   function:            flattenpath_()     author:              Andrew Cave
   creation date:       11-Nov-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 191.

---------------------------------------------------------------------------- */
Bool flattenpath_(ps_context_t *pscontext)
{
  PATHINFO newpath ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Return instantly if there isn't a path or its already flattened */
  if ( ! CurrentPoint || ! (thePathInfo(*gstateptr)).curved )
    return TRUE ;

  fl_setflat( theFlatness( theLineStyle(*gstateptr))) ;
  if ( ! path_flatten(thePath(thePathInfo(*gstateptr)), &newpath) )
    return FALSE ;
  path_free_list( thePath(thePathInfo(*gstateptr)), mm_pool_temp ) ;

  /* update path, leaving protection bits and bbox alone */
  thePath(thePathInfo(*gstateptr)) = thePath(newpath) ;
  (thePathInfo(*gstateptr)).lastpath = newpath.lastpath ;
  (thePathInfo(*gstateptr)).lastline = newpath.lastline ;
  (thePathInfo(*gstateptr)).curved = FALSE ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            reversepath_()     author:              Andrew Cave
   creation date:       03-Dec-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 203.

---------------------------------------------------------------------------- */
Bool reversepath_(ps_context_t *pscontext)
{
  PATHINFO *lpath = &(thePathInfo(*gstateptr)) ;
  register LINELIST *theline, *nextline ;
  Bool trailing_moveto = TRUE ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ! CurrentPoint )
    return TRUE ;

  if ( ! path_close( MYCLOSE, lpath ))
    return FALSE ;

  path_reverse_linelists(lpath->firstpath, NULL);

  /* Drop MYCLOSE off end to restore currentpoint */
  HQASSERT( lpath->lastpath , "No last path!") ;

  theline = theSubPath(*lpath->lastpath) ;
  HQASSERT( theline, "No points in subpath!") ;

  while ((( nextline = theline->next ) != NULL ) &&
          theLineType(*nextline) != MYCLOSE ) {
    if ( theLineType(*theline) != MOVETO ) {
      trailing_moveto = FALSE ;
    }
    theline = nextline ;
  }

  if ( trailing_moveto && lpath->lastpath != lpath->firstpath) {
    /* We have a trailing moveto on a path which has more than one
       subpath: in these circumstances, we match brain-dead Adobe
       behaviour by dropping the trailing moveto. */

    PATHLIST *newlast = lpath->firstpath ;

    while ( newlast->next != lpath->lastpath) {
      newlast = newlast->next ;
    }

    path_free_list( newlast->next, mm_pool_temp ) ;
    newlast->next = NULL ;

    lpath->lastpath = newlast ;
    theline = theSubPath(*newlast) ;

    while ((( nextline = theline->next ) != NULL ) &&
           theLineType(*nextline) != MYCLOSE ) {
      theline = nextline ;
    }
  }

  /* If nextline is not NULL, then it is a MYCLOSE node, and can be
     removed */
  if ( nextline ) {
    theline->next = NULL ;
    free_line( nextline, mm_pool_temp ) ;
  }
  lpath->lastline = theline ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            strokepath_()      author:              Andrew Cave
   creation date:       11-Nov-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 229.

---------------------------------------------------------------------------- */
Bool strokepath_(ps_context_t *pscontext)
{
  STROKE_PARAMS params ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  set_gstate_stroke(&params, &thePathInfo(*gstateptr), &thePathInfo(*gstateptr), FALSE) ;

  if ( ! dostroke( & params , GSC_FILL , STROKE_NOT_VIGNETTE ))
    return FALSE ;      /* leaves original path intact in error condition */

  return TRUE ;
}

/**
 * Parameterised stroke. The information used by dostroke and its child
 * functions is held in the argument structure. If the input and output
 * pathinfo pointers are the same, and the input path was not statically
 * allocated (determined by the doCopyPath flag), the input path is freed
 * before being replaced.
 */
static Bool dostroke_internal(STROKE_PARAMS *sp, int32 colorType,
                              STROKE_OPTIONS options)
{
  Bool dovignettedetection = !(options & STROKE_NOT_VIGNETTE);
  Bool do_vd = VD_DETECT(dovignettedetection &&
                         char_current_context() == NULL);
  Bool do_hdlt = ((options & STROKE_NO_HDLT) == 0 &&
                  isHDLTEnabled(*gstateptr));
  Bool do_pdfout = ((options & STROKE_NO_PDFOUT) == 0 &&
                     pdfout_enabled());
  LINELIST *templine;
  Bool result = FALSE;
  PATHLIST *firstpath ;

  HQASSERT(sp, "No stroke parameter structure passed to dostroke");
  HQASSERT(sp->thepath, "No input pathinfo in dostroke");

  /*
   * For hdlt we can only generate one dl object, so we must try and
   * guarantee this.
   */
  if ( do_hdlt && !check_analyze_vignette_s(sp) )
    do_vd = FALSE;

  if ( sp->thepath->lastline == NULL ) { /* No path to do */

    /* Note, this used to be the else of a huge if statement encapsulating
       the following stroke functionality, but it's clearer to get the trivial
       case out of the way first */

    if ( sp->strokedpath != NULL ) {
      /* Strokepath with no input, so make result a null path (i.e the input) */
      *(sp->strokedpath) = *(sp->thepath);

      if ( dovignettedetection && !flush_vignette(VD_Default) )
        return FALSE ;
    }

    return TRUE ;
  }

  /* We only get here if we've got a path */

  if ( sp->strokedpath ) {

    /* Again, get the trivial case out of the way */

    HQASSERT( !dovignettedetection , "no need to flush vignette" );
    path_init(&sp->outputpath);

  } else {

    /* The CHARPATH case is trapped here so that ustroke in a type 3
       character will work; note that this case shouldn't be stroked,
       because doIStrokeIt should only be true for PaintType 1 (stroked)
       fonts, which aren't supported by Adobe in Level 2, whereas userpaths
       are a Level 2 feature. But we support it anyway. */
    if ( char_doing_charpath() ) {
      if ( thecharpaths->strokeit ) { /* going recursive... */
        PATHINFO spath;

        path_init(&spath) ;
        sp->strokedpath = &spath;

        /* stroke outline */
        if ( !dostroke_internal(sp, colorType, options|STROKE_NOT_VIGNETTE) )
          return FALSE;

        return add_charpath(&spath, FALSE);

      } else {

        if ( !path_close( MYCLOSE, sp->thepath ) )
          return FALSE;

        /* Note that we don't need to restore any end-of-path here.
         * This is because either:
         *  a) the path is moved (!copied) and needs to retain it's
         *     close'ness.
         *  b) the gs_closepath was a no-op because either it was a
         *     statically added path or one from the userpath cache,
         *     both which are guaranteed to be closed.
         *  c) the path is going to be destroyed anyway
         *     (eg stroke does a newpath).
         */
        return add_charpath(sp->thepath, sp->copypath);
      }
    } /* if char_doing_path() */

    if ( CURRENT_DEVICE_SUPPRESSES_MARKS() && !do_hdlt && !do_pdfout )
      return TRUE;

    if ( (options & STROKE_NO_SETG) == 0 &&
         !DEVICE_SETG(sp->page, colorType , DEVICE_SETG_NORMAL) )
      return FALSE;

    if ( degenerateClipping && !do_hdlt && !do_pdfout )
      return TRUE;
  } /* if !sp->strokedpath */

  /* Temporary close */
#define return Do not use return, goto return_result instead!
  firstpath = sp->thepath->firstpath ;

  templine = sp->thepath->lastline;
  if ( !path_close(TEMPORARYCLOSE, sp->thepath) )
    goto return_result ;

  if ( !do_vd ) {
    /* Use extra test of dovignettedetection to ignore non-painting cases.
     * All other painting cases (ustroke, rectstroke,...)
     * have already done flush.
     */
    if ( dovignettedetection && !flush_vignette(VD_Default) )
      goto return_result ;

    if ( sp->strokedpath == NULL ) { /* No callbacks for strokepath */

      if ( do_pdfout && !pdfout_dostroke(get_core_context_interp()->pdfout_h,
                                         sp, colorType) )
        goto return_result ;

      if ( do_hdlt ) {
        /* need to account for any stroke specified matrix adjust. */
        int32 rval;
        OMATRIX savCTM;

        MATRIX_COPY( & savCTM, & thegsPageCTM(*gstateptr));
        if (sp->usematrix)
          MATRIX_COPY( & thegsPageCTM(*gstateptr), & sp->orig_ctm );
        rval = IDLOM_STROKE(colorType, sp, NULL);
        MATRIX_COPY( & thegsPageCTM(*gstateptr), & savCTM);

        switch (rval) {
        case NAME_Discard:    /* just pretending */
          result = TRUE;
          /* drop through */
        case NAME_false:      /* PS error in callbacks */
          goto return_result ;
        default:              /* only add, for now */
          ;
        }
      }
    }

  } else { /* Doing vignette detection */
    setup_analyze_vignette();
  }

  result = TRUE;
  if ( sp->strokedpath != NULL || !degenerateClipping )
    result = dostroke_draw(sp);

  if ( do_vd ) {
    reset_analyze_vignette();
    if ( result )
      result = analyze_vignette_s(sp, colorType);
  }

  if ( templine != sp->thepath->lastline &&
       sp->thepath->lastline ) { /* restore previous end */

    HQASSERT(templine->next == sp->thepath->lastline,
             "Current path changed");
    templine->next = NULL;
    free_line( sp->thepath->lastline, mm_pool_temp );
    sp->thepath->lastline = templine;
  }

  if ( !result ) {
    if ( sp->strokedpath != NULL )
      path_free_list(sp->outputpath.firstpath, mm_pool_temp);
    goto return_result ;
  }

  if ( sp->strokedpath != NULL ) { /* return stroked path */
    uint8 protection = sp->thepath->protection ;

    if ( sp->thepath == sp->strokedpath && !sp->copypath ) {
      /* dispose of previous path */

      path_free_list(sp->strokedpath->firstpath, mm_pool_temp);
    }

    *(sp->strokedpath) = sp->outputpath;  /* copy stroked path */
    sp->strokedpath->protection = protection ;
  }

  result = TRUE ;

return_result:
#undef return

  return result ;
}

Bool dostroke( STROKE_PARAMS *params , int32 colorType , STROKE_OPTIONS options )
{
  Bool result ;
  Group* group = NULL ;
  int32 gid = GS_INVALID_GID ;

  /*
   * Don't need (in theory) the Implcit group around stroke anymore, as new
   * stroke algorithm creates a single atomic NFILL struct, and so cannot
   * self-composite any more. But in reality, get problems due to other bugs,
   * so only disable group if explicitly by EnableStroker[] setting.
   * Also will always need group if PoorStrokePath is enabled.
   */
  if ( SystemParams.EnableStroker[2] && (!SystemParams.PoorStrokepath) )
    return dostroke_internal(params, colorType, options) ;

  /* Create an implicit KO group around this stroke when the stroke is
     transparent and when it will be handled by oldstrokethepath.  The group
     is required to avoid double compositing artifacts on line caps, corners
     and self-intersecting paths, which can happen with oldstrokethepath. We
     should not create an implicit group when extracting the stroked outline
     of a path.
  */
  /** \todo @@@ TODO FIXME MJ 18/10/2005
     Artifacts can also happen with newstrokethepath.
   */
  if ( params->strokedpath == NULL && thePathInfo(*params)->lastline ) {
    Bool transparent_pattern = FALSE;
    Bool did_early_setg = FALSE ;

    if ( gsc_getcolorspace(gstateptr->colorInfo, colorType) == SPACE_Pattern &&
         !char_doing_charpath() ) {
      /* Need to do the setg now in case the stroke has a transparent pattern
         on it; if this is the case an implicit group is required. */
      if ( (options & STROKE_NO_SETG) == 0 ) {
        if ( !DEVICE_SETG(params->page, colorType, DEVICE_SETG_NORMAL) )
          return FALSE ;
        did_early_setg = TRUE ;
      }

      transparent_pattern = ( params->page->currentdlstate->patternstate &&
                              params->page->currentdlstate->patternstate->backdrop );
    }

    if ( !openImplicitGroup(params->page, &group, &gid, IMPLICIT_GROUP_STROKE,
                            transparent_pattern) )
      return FALSE ;

    /* Only need to do another setg in dostroke_internal if an implicit group
       was created (groupOpen does its own setg, which invalidates this one). */
    if ( did_early_setg && !group )
      options |= STROKE_NO_SETG;
  }

  result = dostroke_internal(params, colorType, options) ;

  /* Close implicit group. The group may be NULL because alpha may be 1. */
  if ( params->strokedpath == NULL &&
       !closeImplicitGroup(&group, gid, result) )
    result = FALSE ;

  return result ;
}

/**
 * Structure for passing all the various strokeadjust parameters back
 * and forth to the child routines.
 */
typedef struct STROKEA_P {
  STROKE_PARAMS *sp;   /**< Stroke parameters controlling strokeadjust */
  int32 mlw_x;         /**< Enforced minimum x linewidth */
  int32 mlw_y;         /**< Enforced minimum y linewidth */
  SYSTEMVALUE rx;      /**< Ratio in x between input and adjusted linewidth */
  SYSTEMVALUE ry;      /**< Ratio in y between input and adjusted linewidth */
  int32 wx;            /**< Adjusted linewidth in x direction */
  int32 wy;            /**< Adjusted linewidth in y direction */
  Bool zerolw;         /**< Can we fall back to using the zero-linewidth code */
  Bool forcemin;       /**< Force the linewidth to the minimum */
  SYSTEMVALUE snap_x;  /**< Snap end-points to this fraction x position */
  SYSTEMVALUE snap_y;  /**< Snap end-points to this fraction y position */
} STROKEA_P;

/**
 * Calculate the line-weight along the given device-space axis
 */
static void calculatelineweight(Bool in_x, SYSTEMVALUE dxy, STROKEA_P *sap)
{
  SYSTEMVALUE adxy, mdxy;
  SYSTEMVALUE shrink = 2.0 * ( in_x ? sap->snap_x : sap->snap_y);
  int32 gsa = ForceStrokeAdjustApply(TRUE);

  dxy = fabs(dxy); /* Create absolute values. */
  adxy = ((SYSTEMVALUE)((int32 )(gsa ? dxy - 0.5 : dxy + 0.5 )) + shrink);
  mdxy = (SYSTEMVALUE)(in_x ? sap->mlw_x : sap->mlw_y);
  mdxy = ((SYSTEMVALUE)((int32)(mdxy-0.5)) + shrink);
  if ( mdxy > adxy || sap->forcemin )
    adxy = mdxy;
  adxy -= (0.1 * shrink);    /* Fudge-factor; compensate for off-by-one. */
  sap->zerolw = sap->zerolw && (adxy < 1.0);
  /* Scaling for x,y is now the ratio of adx/dx,ady/dy. */
  if ( dxy == 0.0 )
    dxy = adxy;
  if ( in_x )
  {
    sap->wx = (int32)adxy + 1;
    sap->rx = adxy/dxy;
  }
  else /* in_y */
  {
    sap->wy = (int32)adxy + 1;
    sap->ry = adxy/dxy;
  }
}

/**
 * Calculate lineadjustment to help in determining strokeadjust settings.
 *
 * Following code base on:
 * We want to adjust the line width to achieve constant line widths in x & y.
 * Since that involves two dimensions, but the line width only gives us control
 * over one, we can't modify that, but have to work out modifications to the CTM
 * independantly in x & y.
 *
 * Consider the following:
 *    |-----------|  ^                /-----------/  ^
 *    |           |  | dy            /           /   | dy
 *    |-----------|  v              /-----------/    v
 *                <> dx(==0)                    <-> dx(!=0)
 * In both cases it is the actualy height (dy) of the linewidth that we care
 * about, not the length of the vector (dx,dy). The only way we know the value
 * of dy is to take the actual stroke outline of a horizontal vector in x.
 * To do this you need to take a horizontal vector in x, transform this into
 * user space, take the perpendicular to this, normalise it and scale up by
 * the linewidth, transform this back into device space and that gives you
 * dx,dy. You can then work out the adjustment that needs to be made to the
 * CTM vertically. The same thing applies horizontally.
 */
static void calculatelineadjust(Bool vertical, STROKEA_P *sap)
{
  SYSTEMVALUE tux, tuy, ux, uy, len, dx, dy;

  /* Take a vector in device space and map into user space. */
  if ( vertical )
    MATRIX_TRANSFORM_DXY(0.0, 1.0, tux, tuy, &(sap->sp->orig_inv));
  else /* horizontal */
    MATRIX_TRANSFORM_DXY(1.0, 0.0, tux, tuy, &(sap->sp->orig_inv));

  /* Take the 90 degree rotation of this vector. */
  ux = -tuy;
  uy = tux;
  /* Normalise and Scale up by lby2. */
  len = sqrt(ux * ux + uy * uy);
  if ( len == 0.0 )
    len = 1.0;
  ux *= sap->sp->lby2*2.0/len;
  uy *= sap->sp->lby2*2.0/len;
  /* Transform to device space. */
  MATRIX_TRANSFORM_DXY(ux, uy, dx, dy, &(sap->sp->orig_ctm));

  calculatelineweight(TRUE, dx, sap);
  calculatelineweight(FALSE, dy, sap);
}

/**
 * Now, figure out whether we're doing stroke adjustment, and fill the
 * sadj_ fields based on that (to either CTM copies, or adjusted matrices)
 *
 * See Red Book pg 322 bottom Note; if line width is small,
 * then use 0 setlinewidth
 */
void setup_strokeadjust(STROKE_PARAMS *sp)
{
  STROKEA_P sap;

  /* Very small (1e-38) linewidths can cause FP underflows, so force to zero
   * Add a safety margin of a factor of ten to try and avoid other possible
   * underflows.
   */
  HQASSERT(sp->linestyle.linewidth >= 0.0, "Negative linewidth");
  if ( sp->linestyle.linewidth < 10*SMALLEST_REAL )
    sp->lby2 = 0.0;
  else
    sp->lby2 = sp->linestyle.linewidth/(USERVALUE)2.0;

  sap.mlw_x    = SystemParams.MinLineWidth[0];
  sap.mlw_y    = SystemParams.MinLineWidth[1];
  sap.forcemin = FALSE;
  sap.sp       = sp;
  sap.zerolw   = TRUE;
  /*
   * Note that these two must be less than 1/4. They could in fact be
   * slightly larger up to 1/2 but in that case the calculatelineweight
   * macro in pathops.c would need to change [AC; 96/02/02].
   */
  /**
   * \todo BMJ 03-Feb-09 : Did some analysis on altering these values, and
   * it appears we can get better quality and less possibility of asymmetry
   * with different values. But will require a lot of quality checking so
   * don't do it right now.
   */
  sap.snap_x = 0.125;
  sap.snap_y = 0.250;

  MATRIX_COPY(&(sp->sadj_ctm), &(sp->orig_ctm));
  MATRIX_COPY(&(sp->sadj_inv), &(sp->orig_inv));
  sp->sc_rndx1 = sp->sc_rndy1 = sp->sc_rndx2 = sp->sc_rndy2 = SC_PIXEL_ROUND;
  if ( sp->strokedpath != NULL ) /* Outline path */
    sp->sc_rndx2 = sp->sc_rndy2 = 0.0;

  if (( sap.mlw_x > 1 || sap.mlw_y > 1 ))
  {
    sp->strokeadjust = TRUE;
    if ( sp->lby2 == 0.0f )
    {
      /* Note that we do need some special case code here even though the
       * routines calculatelineadjust/calculatelineweight would cope with
       * lby2 being 0.0. This is because in this case we can't leave lby2
       * as being 0.0 because the actual stroke code in gu_[c]line.c needs
       * some nominal line width to actually produce an outline (and stop
       * some divides by zero). Therefore forcemin does need to exist and
       * we choose this nominal line width to be 1.0. The matrix adjustments
       * that get calculated in calculatelineadjust (et al) then end up
       * essentially modiying this nominal linewidth to that min requested.
       */
      sp->lby2 = 1.0f;
      sap.forcemin = TRUE;
    }
  }

  if ( sp->strokeadjust )
  {
    sp->sc_rndx2 += sap.snap_x;
    sp->sc_rndy2 += sap.snap_y;

    if ( sp->lby2 != 0.0f ) /* Calculate the adjusted line width... */
    {
      /* Calculate the adjustment for the width of a vertical line. */
      calculatelineadjust(TRUE, &sap);
      if (( sp->sadj_ctm.opt & MATRIX_OPT_0011 ) != 0 )
      {
        sp->sadj_ctm.matrix[0][0] *= sap.rx;
        sp->sadj_inv.matrix[0][0] /= sap.rx;
      }
      if (( sp->sadj_ctm.opt & MATRIX_OPT_1001 ) != 0 )
      {
        sp->sadj_ctm.matrix[1][0] *= sap.rx;
        sp->sadj_inv.matrix[0][1] /= sap.rx;
      }
      if (( sap.wx & 1 ) == 0 )
        sp->sc_rndx1 += 0.5;

      /* Calculate the adjustment for the width of a horizontal line. */
      calculatelineadjust(FALSE, &sap);
      if (( sp->sadj_ctm.opt & MATRIX_OPT_0011 ) != 0 )
      {
        sp->sadj_ctm.matrix[1][1] *= sap.ry;
        sp->sadj_inv.matrix[1][1] /= sap.ry;
      }
      if (( sp->sadj_ctm.opt & MATRIX_OPT_1001 ) != 0 )
      {
        sp->sadj_ctm.matrix[0][1] *= sap.ry;
        sp->sadj_inv.matrix[1][0] /= sap.ry;
      }
      if (( sap.wy & 1 ) == 0 )
        sp->sc_rndy1 += 0.5;

#if defined( ASSERT_BUILD )
      {
        OMATRIX inverse;

        HQASSERT(matrix_inverse(&(sp->sadj_ctm), &(inverse)) &&
          MATRIX_REQ_EPSILON(&(inverse), &(sp->sadj_inv)), "bad inverse");
      }
#endif
      if ( sap.zerolw )
        sp->lby2 = 0.0;
    }
  }
}

/*
 * Try to determine if the dashes are degenerate; i.e., there are no
 * gaps which will show and which will make a difference. This fix is
 * here because of an Illustrator 8 file which used a dash pattern of
 * [0.0009 0.0005]; i.e., gap lengths of 0.000007 inch. We cycle through
 * the dash list, starting at the dash offset, until we find a gap large
 * enough to make the line join or cap style visible.
 *
 * Restricting this test to join and cap styles in which no difference
 * could be seen (i.e. low width, or round join & round cap, or where
 * projecting caps would create a mitre) is not done, because the problem
 * job uses butt caps and mitre joins (the default).
 *
 * Note that we also don't make any attempt to remove tiny gaps in dash
 * patterns that have larger than minimum size gaps.
 */
void check_for_degenerate_dashing(STROKE_PARAMS *sp)
{
  if ( sp->linestyle.dashlistlen )
  {
    int32 i;
    Bool degenerate = TRUE;
    SYSTEMVALUE length, minsize, dx, dy;

    if ( sp->linestyle.dashmode == DASHMODE_PERCENTAGE ) {
      /* Dashes are a percentage of each line segment, so the checks below
         don't apply. */
      return ;
    }

    /* Find out the userspace equivalent lengths of a half pixel device space
       vector. The smaller of these lengths sets the minimum useful gap size
       for dashed lines. */

    MATRIX_TRANSFORM_DXY(0.5, 0.0, dx, dy, &sp->orig_inv);
    minsize = dx * dx + dy * dy;
    MATRIX_TRANSFORM_DXY(0.0, 0.5, dx, dy, &sp->orig_inv);
    length = dx * dx + dy * dy;
    if ( length < minsize )
      minsize = length;

    for ( i = 0; degenerate && i < sp->linestyle.dashlistlen; i++ )
    {
      /*
       * Just look at the gaps, which are the odd index dashes (unless
       * the total length is odd, in which case it will be the odd ones
       * the first time round, and then then even ones. i.e.
       * all of them eventually ).
       */
      if ( (i&1) || (sp->linestyle.dashlistlen&1) )
      {
        length = sp->linestyle.dashlist[i];
        if ( length * length > minsize )
          degenerate = FALSE;
      }
    }
    /* The dash array is owned by the gstate linestyle, therefore we only
       need to zero it in the sp linestyle copy. */
    if ( degenerate )
      sp->linestyle.dashlistlen = 0;
  }
}

/**
 * This routine actually draws the stroke. When the full stroke
 * parameterisation is done, all of the unpacking here will disappear.
 * DO NOT call this routine to stroke an outline; it doesn't set it up
 * properly and doesn't return the right values - call dostroke instead.
 */
Bool dostroke_draw(STROKE_PARAMS *sp)
{
  PATHINFO *inpath = sp->thepath;
  Bool result;

  HQASSERT(!(sp->strokedpath && sp->outputpath.firstpath),
           "dostroke_draw called directly when stroking an outline");

  fl_setflat(sp->linestyle.flatness);

  if ( sp->usematrix )
    newctmin |= NEWCTM_RCOMPONENTS;
  else
  {
    HQASSERT(MATRIX_REQ_EPSILON(&thegsPageCTM(*gstateptr), &sp->orig_ctm),
             "theIgsPageCTM not eq to thegsPageCTM() when not provided");
  }

  SET_SINV_SMATRIX( & sp->orig_ctm , NEWCTM_RCOMPONENTS );
  if ( SINV_NOTSET( NEWCTM_RCOMPONENTS ) )
  {
    /* The only way this can be true is if the matrix is degenerate.
     * In this case we do (as Adobe) do.
     * That is we extract the largest component and propogate.
     * Note that Adobe actually crash if you have a dash in force,
     * but we won't...
     */
     int32 i , j;
     SYSTEMVALUE maxval = 0.0;
     for ( i = 0; i < 2; ++i )
       for ( j = 0; j < 2; ++j )
         if ( fabs(sp->orig_ctm.matrix[i][j]) > maxval )
           maxval = fabs(sp->orig_ctm.matrix[i][j]);
    MATRIX_COPY(&sp->orig_ctm, &identity_matrix);
    if ( maxval == 0.0 )
      sp->linestyle.linewidth  = (USERVALUE )maxval;
    else
    {
      sp->orig_ctm.matrix[0][0] = maxval;
      sp->orig_ctm.matrix[1][1] = maxval;
    }
    SET_SINV_SMATRIX(&sp->orig_ctm, NEWCTM_RCOMPONENTS);
    HQASSERT(!SINV_NOTSET(NEWCTM_RCOMPONENTS), "should be an inverse here");
    newctmin = NEWCTM_ALLCOMPONENTS;
  }

  MATRIX_COPY(&sp->orig_inv, &sinv);
  setup_strokeadjust(sp);
  check_for_degenerate_dashing(sp);

  /*
   * Call the code that generates the stroked outline.
   * This code replaces three old routines which have now been removed, namely
   *   strokethepath(), newstrokethepath() and faststroke();
   *
   * Don't need to pre-flatten the beziers, as the stroker code does it
   * itself in-line.
   */
  result = stroker(sp, inpath->firstpath);

  /* reset matrix inverse flag because we've changed the inverse matrix */
  if ( sp->usematrix )
    newctmin = NEWCTM_ALLCOMPONENTS;

  return result;
}

/** set_gstate_stroke fills in a stroke_params structure appropriate for
 * stroking the gstate path. The argument determines if the outline or the
 * path is stroked.
 */

void set_gstate_stroke( STROKE_PARAMS *params , PATHINFO *input ,
                        PATHINFO *output , uint8 copycharpath )
{
  params->page = get_core_context_interp()->page ;
  thePathInfo(*params) = input ;
  params->strokedpath = output ;
  MATRIX_COPY( & params->orig_ctm , & thegsPageCTM(*gstateptr)) ;
  params->linestyle = theLineStyle(*gstateptr) ;
  params->strokeadjust = ( uint8 ) ForceStrokeAdjustApply(FALSE) ;
  params->usematrix = FALSE ;
  params->copypath = copycharpath ;
}

/* set_font_stroke fills in a stroke_params structure appropriate for
   stroking a character from a font. The argument determines if the outline or
   the path is stroked. */
void set_font_stroke(DL_STATE *page, STROKE_PARAMS *params, PATHINFO *input, PATHINFO *output)
{
  params->page = page ;
  thePathInfo(*params) = input ;
  params->strokedpath = output ;
  MATRIX_COPY(& params->orig_ctm, & theFontMatrix(theFontInfo(*gstateptr))) ;
  params->usematrix = TRUE ;
  theLineWidth(params->linestyle) = theStrokeWidth(theFontInfo(*gstateptr)) ;
  theFlatness(params->linestyle) = fcache_flatness(params->page) ;
  theMiterLimit(params->linestyle) = 10.0f ;
  theStartLineCap(params->linestyle) = 0 ;              /* butt caps */
  theEndLineCap(params->linestyle) = 0 ;                /* butt caps */
  theDashLineCap(params->linestyle) = 0 ;               /* butt caps */
  theLineJoin(params->linestyle) = 0 ;          /* mitre joins */
  theDashListLen(params->linestyle) = 0 ;               /* solid line */
  theDashOffset(params->linestyle) = 0.0f ;
  theDashPattern(params->linestyle) = onull ; /* Struct copy to set slot properties */
  params->strokeadjust = TRUE; /* Isolate from ForceStrokeAdjust [62846] */
  params->copypath = FALSE ;
}

/** dostrokefill:
   inpathfill is only required when the fill and stroke paths are different,
   as indicated by the bool use_inpathfill.  This can only happen for XPS.

   stroke_opacity and fill_opacity are used in XPS when stroke and fill
   supply their own opacities and they work cumutively with the gstate
   alphas.  For PDF these values should be set to 1.0 - only the gstate
   alphas are required.

   usage is either IMPLICIT_GROUP_PDF or IMPLICIT_GROUP_XPS and indicates
   which kind of implicit group is required.
*/
Bool dostrokefill(PATHINFO *inpath, PATHINFO *inpathfill, Bool use_inpathfill,
                  USERVALUE stroke_opacity, USERVALUE fill_opacity,
                  IMPLICIT_GROUP_USAGE usage, Bool closepath, int32 filltype)
{
  PATHINFO flatpathinfo ;
  PATHLIST *firstpath ;
  STROKE_PARAMS params ;
  Group* group = NULL ;
  int32 gid ;
  Bool result = FALSE ;

  HQASSERT(inpath, "No path for combined stroke/fill") ;

  HQASSERT((stroke_opacity == 1.0 && fill_opacity == 1.0) ||
           usage == IMPLICIT_GROUP_XPS,
           "Read the comment before dostrokefill which explains stroke_opacity and fill_opacity");

  /* Stroke/fill of an empty path is a no-op */
  if ( ! inpath->lastline || ( use_inpathfill && ! inpathfill->lastline ) )
    return TRUE;

  if ( closepath ) {
    if ( ! path_close( CLOSEPATH, inpath ) )
      return FALSE ;
    if ( use_inpathfill ) {
      if ( ! path_close( CLOSEPATH, inpathfill ) )
        return FALSE ;
    }
  }

  if ( ! flush_vignette( VD_Default ))
    return FALSE ;

  /* By doing any necessary flattening now, we can avoid doing it twice:
   * ie once in dofill and once in dostroke. Only do it if HDLT is not
   * enabled as we need to return the unflattened path. However, there's
   * no advantage if stroke and fill paths are different. */
  firstpath = NULL ;
  if ( inpath->curved && !isHDLTEnabled( *gstateptr ) && !use_inpathfill ) {
    PATHINFO reducedpath;
    Bool wasHuge = FALSE;

    /* Initialize bbox, protection and flags of flatpathinfo */
    flatpathinfo.bboxtype = BBOX_NOT_SET ;
    flatpathinfo.protection = inpath->protection ;
    flatpathinfo.flags = inpath->flags;

    fl_setflat(theFlatness(theLineStyle(*gstateptr))) ;

    firstpath = inpath->firstpath ;

    if ( is_huge_path(firstpath) ) {
      wasHuge = TRUE;
      set_gstate_stroke(&params, inpath, NULL, TRUE) ;

      if ( !clip_huge_path(firstpath, &reducedpath, &params) )
        return FALSE ;
      firstpath = thePath( reducedpath ) ;
      if ( firstpath == NULL ) /* clipping means nothing left */
        return TRUE;
    }

    if ( ! path_flatten( firstpath , & flatpathinfo ) )
      return FALSE ;
    if ( wasHuge )
      path_free_list( firstpath, mm_pool_temp ) ;
    firstpath = thePath( flatpathinfo ) ;
    inpath = & flatpathinfo ;
  }

  /* Open an implicit group. */
  if ( openImplicitGroup(get_core_context_interp()->page, &group, &gid, usage, FALSE)) {

    if ( usage == IMPLICIT_GROUP_XPS ) {
      /* Now the non-knockout group has been established, with the gstate
         alpha applied to the group, set the additonal stroke and fill
         opacities inside the group.  If no group was created, because the
         gstate alpha are opaque, then the opacities are applied to the
         stroke and fill paths directly. */
      if ( stroke_opacity < 1.0 )
        tsSetConstantAlpha(gsTranState(gstateptr), GSC_STROKE,
                           stroke_opacity, gstateptr->colorInfo);
      if ( fill_opacity < 1.0 )
        tsSetConstantAlpha(gsTranState(gstateptr), GSC_FILL,
                           fill_opacity, gstateptr->colorInfo);
    }

    /* Object cannot be a vignette since we're doing a stroke and a fill,
     * therefore switch the vignette detection off. */
    if ( dofill(use_inpathfill ? inpathfill : inpath, filltype, GSC_FILL,
                FILL_NOT_VIGNETTE|FILL_NOT_ERASE|FILL_COPYCHARPATH) ) {
      set_gstate_stroke(&params, inpath, NULL, TRUE) ;

      result = dostroke(&params, GSC_STROKE, STROKE_NOT_VIGNETTE) ;
    }

    /* Close implicit group. The group may be NULL because alpha may be 1. */
    if ( !closeImplicitGroup(&group, gid, result) )
      result = FALSE ;
  }

  /* Finished with the flat version. */
  if ( firstpath )
    path_free_list( firstpath, mm_pool_temp ) ;

  return result ;
}



/* Log stripped */
