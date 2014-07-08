/** \file
 * \ingroup paths
 *
 * $HopeName: SWv20!src:clipops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Clipping operators.
 */

#include "core.h"
#include "swerrors.h"

#include "bitblts.h"
#include "mm.h"
#include "mmcompat.h"
#include "matrix.h"
#include "display.h"
#include "devops.h"   /* doing_mirrorprint. Yuck. */
#include "graphics.h"

#include "ndisplay.h"
#include "routedev.h"
#include "control.h"

#include "swdevice.h"
#include "dlstate.h"
#include "render.h"

#include "gu_path.h"
#include "gu_fills.h"
#include "gstate.h"
#include "system.h"
#include "pathcons.h"
#include "fileio.h"
#include "clipops.h"
#include "namedef_.h"
#include "pathops.h"
#include "idlom.h"
#include "vndetect.h"

#include "params.h"   /* SystemParams */
#include "clippath.h" /* clippath_internal */
#include "swpdfout.h"
#include "ripdebug.h"

#include "gschead.h"

#ifdef DEBUG_BUILD
static dbbox_t debug_clip = {MINDCOORD, MINDCOORD, MAXDCOORD, MAXDCOORD} ;
#endif

static void addcliprecord(/*@notnull@*/ /*@in@*/ CLIPRECORD *cliprec,
                          /*@notnull@*/ /*@in@*/ CLIPPATH *clippath);

/** \brief Reset a CLIPPATH structure to the initial state for the
    pagedevice in a target gstate. */
static void reset_clip_for_gs(/*@notnull@*/ /*@out@*/ CLIPPATH *clippath,
                              /*@notnull@*/ /*@in@*/ GSTATE *gs) ;

static CLIPRECORD *reverse_cliprecord_list(CLIPRECORD **c_recp) ;

CLIPPATH impositionclipping ;

/** Current clipping ID. This is pre-incremented when put into the clipnumber
    of CLIPRECORDs. */
int32 clipid = CLIPID_INVALID;

/** File runtime initialisation */
void init_C_globals_clipops(void)
{
  CLIPPATH init_clip = { 0 } ;
  impositionclipping = init_clip ;
  clipid = CLIPID_INVALID ;
}

void init_clip_debug(void)
{
#ifdef DEBUG_BUILD
  register_ripvar(NAME_debug_clip_x1, OINTEGER, &debug_clip.x1) ;
  register_ripvar(NAME_debug_clip_y1, OINTEGER, &debug_clip.y1) ;
  register_ripvar(NAME_debug_clip_x2, OINTEGER, &debug_clip.x2) ;
  register_ripvar(NAME_debug_clip_y2, OINTEGER, &debug_clip.y2) ;
#endif
}

/* ----------------------------------------------------------------------------
   function:            clip_()            author:              Andrew Cave
   creation date:       05-Jan-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 128.

---------------------------------------------------------------------------- */
Bool clip_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gs_addclip( NZFILL_TYPE ,
                     & thePathInfo( *gstateptr ) ,
                     TRUE ) ;
}

Bool iclip_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Harlequin extension: inverted clipping */
  return gs_addclip( NZFILL_TYPE | CLIPINVERT ,
                     & thePathInfo( *gstateptr ) ,
                     TRUE ) ;
}

Bool eoclip_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gs_addclip( EOFILL_TYPE ,
                     & thePathInfo( *gstateptr ) ,
                     TRUE ) ;
}

/* ----------------------------------------------------------------------------
   function:            addclip(..)        author:              Andrew Cave
   creation date:       05-Jan-1987        last modification:   ##-###-####
   arguments:           thepath .
   description:

   This procedure adds a new clip record onto the current clipping path.

  Y 29Jul97 add pdfout hook
---------------------------------------------------------------------------- */
Bool gs_addclip(int32 cliptype, PATHINFO *lpath, Bool copyclip)
{
  corecontext_t *context = get_core_context_interp();
  CLIPRECORD *cliprec ;
  LINELIST *templine ;
  Bool result = FALSE, cacheOk = FALSE ;
  int32 cacheId = 0 ;

  if ( ! flush_vignette( VD_AddClip ))
    return FALSE;

  templine = lpath->lastline ;

  if ( ! templine ) {
    /* If there is no currentpath, create a degenerate path statically */
    path_fill_four( 0.0 , 0.0 , 0.0 , 0.0 , 0.0 , 0.0 , 0.0 , 0.0) ;
    cliptype |= CLIPISDEGN ;
    lpath = &i4cpath ;
    templine = i4cpath.lastline ;
    copyclip = TRUE ;
  } else {
    /* If a path was passed in, make sure that it is closed. */
    if ( ! path_close( TEMPORARYCLOSE, lpath ))
      return FALSE ;
  }

  switch (IDLOM_NEWCLIPPATH(lpath, cliptype, &cacheOk, &cacheId)) {
   case NAME_Discard:           /* just pretending */
     result = TRUE;
     /*@fallthrough@*/
   case NAME_false:             /* PS error in IDLOM callbacks */
     goto cleanup_and_return;
   default:                     /* NAME_add, others may be introduced later */
     ;
  }

  /* Get memory in which to store cliprecord. */
  if ( ( cliprec = get_cliprec(mm_pool_temp)) == NULL ) {
    result = error_handler( VMERROR ) ;
    goto cleanup_and_return;
  }

  /* A clip seems to clear the path bbox! */
  lpath->bboxtype = BBOX_NOT_SET ;

  if ( copyclip ) {
    if ( ! path_copy( &theClipPath(*cliprec) , lpath, mm_pool_temp )) {
      free_cliprec( cliprec, mm_pool_temp ) ;
      result = error_handler( VMERROR ) ;
      goto cleanup_and_return;
    }
  } else {
    /* Steal the path. */
    theClipPath(*cliprec) = *lpath ;
    /* Clear out old path. */
    path_init( lpath ) ;
  }

  /* Set up clip record. */
  theClipRefCount(*cliprec) = 1 ;
  theClipType(*cliprec) = (uint8)cliptype ;
  theClipFlat(*cliprec) = 0.0f ;

  thegsPageBaseID(*cliprec) = pageBaseMatrixId ;

  switch ( IDLOM_NEWCLIP(cliprec,cacheOk,cacheId) ) {
  case NAME_false:              /* PS error in IDLOM callbacks */
    path_free_list(thePath(theClipPath(*cliprec)), mm_pool_temp);
    free_cliprec( cliprec, mm_pool_temp ) ;
    goto cleanup_and_return;
  case NAME_Discard:            /* just pretending */
    path_free_list(thePath(theClipPath(*cliprec)), mm_pool_temp);
    free_cliprec( cliprec, mm_pool_temp ) ;
    result = TRUE;
    goto cleanup_and_return;
  default:                      /* NAME_add, others may be introduced later */
    ;
  }

  if (cacheOk) {
    if (! IDLOM_CACHECLIP( &theClipPath(*cliprec), cacheId )) {
      path_free_list(thePath(theClipPath(*cliprec)), mm_pool_temp);
      free_cliprec( cliprec, mm_pool_temp ) ;
      goto cleanup_and_return;
    }
  }

  if ( pdfout_enabled() &&
       ! pdfout_pushclip(context->pdfout_h, cliprec) ) {
    path_free_list(thePath(theClipPath(*cliprec)), mm_pool_temp);
    free_cliprec( cliprec, mm_pool_temp ) ;
    goto cleanup_and_return;
  }

  addcliprecord(cliprec, &thegsPageClip(*gstateptr)) ;

  if ( context->systemparams->PoorClippath[8] && cliprec->next != NULL ) {
    /* Collapse all clips after the initial clip into one cliprecord, and
       mark it as normalised. There are too many assumptions in the RIP that
       an initial clip record exists to be able to collapse all clip records
       into just one record. */
    int32 cliptype ;
    CLIPRECORD *baseclip, *nextclip ;

    /* Find the base clip record, we need to save it. */
    for ( baseclip = cliprec->next ;
          (nextclip = baseclip->next) != NULL ;
          baseclip = nextclip )
      EMPTY_STATEMENT() ;

    HQASSERT(baseclip, "No base clip record") ;

    if ( !clippath_internal(context, &theClipPath(*cliprec), &cliptype) ) {
      path_free_list(thePath(theClipPath(*cliprec)), mm_pool_temp);
      free_cliprec(cliprec, mm_pool_temp) ;
      goto cleanup_and_return;
    }

    HQASSERT((cliptype & CLIPINVERT) == 0, "Clippath result should not be inverted") ;

    theClipType(*cliprec) = CAST_TO_UINT8(cliptype|CLIPNORMALISED) ;

    /* Remove the intermediate clip records, leaving the base clip record,
       then add back this clip record again, to set the clip type flags and
       bounds correctly. The combined path contains all of the details in
       the intermediate records. */
    gs_reservecliprec(baseclip) ;
    gs_freecliprec(&cliprec->next) ;
    theClipRecord(thegsPageClip(*gstateptr)) = baseclip ;

    addcliprecord(cliprec, &thegsPageClip(*gstateptr)) ;
  }

  result = TRUE ;

cleanup_and_return:

  if ( copyclip && templine != lpath->lastline ) {    /* restore previous end */
    HQASSERT(templine->next == lpath->lastline,
             "Current path changed");
    templine->next = NULL ;
    free_line( lpath->lastline, mm_pool_temp );
    lpath->lastline = templine ;
  }

  return result ;
}

static void clip_bounds_from_rbounds(CLIPRECORD *cliprec, CLIPPATH *clippath)
{
  dcoord t1, t2 ;
  SYSTEMVALUE dt1, dt2 ;

  HQASSERT(cliprec != NULL, "No clip record") ;
  HQASSERT(clippath != NULL, "No clip chain") ;

  dt1 = theXd1Clip(*clippath) ;
  dt2 = theXd2Clip(*clippath) ;

  if ( dt1 > dt2 ) {
    t1 = t2 = 0 ;
    theClipType(*cliprec) |= CLIPISDEGN ;
  } else {
    SC_C2D_INT( t1 , dt1 ) ;
    SC_C2D_INT( t2 , dt2 ) ;
  }

  if ( theClipRecord(*clippath) != NULL ) {
    if ( t1 > theX1Clip(*clippath) )
      theX1Clip(*clippath) = t1 ;
    if ( t2 < theX2Clip(*clippath) )
      theX2Clip(*clippath) = t2 ;
  } else {
    /* We reduce the upper bound by one when adding the device clip record to
       convert from the device clip path to the inclusive device pixel set. */
    if ( t2 > t1 ) t2 -= 1 ;
    theX1Clip(*clippath) = t1 ;
    theX2Clip(*clippath) = t2 ;
  }

  dt1 = theYd1Clip(*clippath) ;
  dt2 = theYd2Clip(*clippath) ;

  if ( dt1 > dt2 ) {
    t1 = t2 = 0 ;
    theClipType(*cliprec) |= CLIPISDEGN ;
  } else {
    SC_C2D_INT( t1 , dt1 ) ;
    SC_C2D_INT( t2 , dt2 ) ;
  }

  if ( theClipRecord(*clippath) != NULL ) {
    if ( t1 > theY1Clip(*clippath) )
      theY1Clip(*clippath) = t1 ;
    if ( t2 < theY2Clip(*clippath) )
      theY2Clip(*clippath) = t2 ;
  } else {
    /* We reduce the upper bound by one when adding the device clip record to
       convert from the device clip path to the inclusive device pixel set. */
    if ( t2 > t1 ) t2 -= 1 ;
    theY1Clip(*clippath) = t1 ;
    theY2Clip(*clippath) = t2 ;
  }
}

static void addcliprecord(CLIPRECORD *cliprec, CLIPPATH *clippath)
{
  CLIPRECORD *prevrec ;

  HQASSERT(cliprec != NULL, "No clip record") ;
  HQASSERT(clippath != NULL, "No clip chain") ;

  theClipNo(*cliprec) = ++clipid ;
  prevrec = theClipRecord(*clippath) ;

  /* Propagate degeneracy flag from previous clip record. */
  if ( prevrec != NULL && (theClipType(*prevrec) & CLIPISDEGN) )
    theClipType(*cliprec) |= CLIPISDEGN ;

  /* the clip rectangle doesn't change if its inverted clipping */
  if ( ! (theClipType(*cliprec) & CLIPINVERT) ) {
    sbbox_t *bbox = path_bbox(&theClipPath(*cliprec), NULL,
                              BBOX_IGNORE_ALL|BBOX_SAVE|BBOX_LOAD) ;
    SYSTEMVALUE dt1 , dt2 ;

    /* Mark a single non-inverted rectangle as a special rect clip. These
       just reduce the clip bounds during rendering, they don't get
       rasterised into a clip mask. */
    if ( path_rectangles(thePath(theClipPath(*cliprec)), FALSE, NULL) == 1 )
      theClipType(*cliprec) |= CLIPISRECT ;

    /* Real-value clip bounds define the bounds of the clip path for this
       pagedevice target. Integer clip bounds define the inclusive set of
       pixels touched for this pagedevice target. Neither bounds take into
       account the imposition clipping, they are relative to the current
       target. Clip records are updated during setg() to take into account
       the intersection of the imposition clip and the current clip. */
    if ( prevrec != NULL ) {
      dt1 = theXd1Clip(*clippath) ;
      dt2 = theXd2Clip(*clippath) ;
      if ( dt1 < bbox->x1 )
        dt1 = bbox->x1 ;
      if ( dt2 > bbox->x2 )
        dt2 = bbox->x2 ;
    } else {
      dt1 = bbox->x1 ;
      dt2 = bbox->x2 ;
    }
    theXd1Clip(*clippath) = dt1 ;
    theXd2Clip(*clippath) = dt2 ;

    if ( prevrec != NULL ) {
      dt1 = theYd1Clip(*clippath) ;
      dt2 = theYd2Clip(*clippath) ;
      if ( dt1 < bbox->y1 )
        dt1 = bbox->y1 ;
      if ( dt2 > bbox->y2 )
        dt2 = bbox->y2 ;
    } else {
      dt1 = bbox->y1 ;
      dt2 = bbox->y2 ;
    }
    theYd1Clip(*clippath) = dt1 ;
    theYd2Clip(*clippath) = dt2 ;

    clip_bounds_from_rbounds(cliprec, clippath) ;
  } else {
    HQASSERT(prevrec != NULL,
             "Cannot use inverted clipping in device clip record") ;
  }

  /* these should be set here rather than in addclip so that they are updated
     properly for imposition clipping */
  cliprec->bounds = clippath->bounds ;

  /* Add the new clipping path record onto the current list. */
  cliprec->next = prevrec ;
  theClipRecord(*clippath) = cliprec ;

  HQASSERT(!cliprec->next ||
           theClipRefCount(*cliprec->next) >= theClipRefCount(*cliprec),
           "Reference count of next clip is less than reference count of current clip") ;
}

/*
 * Return TRUE if the rectangle pointed at by rectfill is at least
 * as large as the current (possibly imposed) page. This is used by
 * recombining to determine that an application is trying to clear the
 * page by using eithere rectfill_ or fill_
 */

Bool is_pagesize(DL_STATE *page, dbbox_t *rectfill, int32 colorType)
{
  GSTATE *gs ;
  int32 device = CURRENT_DEVICE() ;
  int32 x1 , y1 , x2 , y2 ;

  gs = gstateptr ;

  /* If the device IS the frame and the color space is not a pattern
   * then we can continue.
   */
  if ( ! CURRENT_DEVICE_SUPPRESSES_STATE() &&
       device != DEVICE_CHAR &&
       device != DEVICE_PATTERN1 &&
       device != DEVICE_PATTERN2 &&
       clippingiscomplex( gs , FALSE ) == NULL &&
       displaylistisempty(page) &&
       gsc_getcolorspace( gs->colorInfo , colorType ) != SPACE_Pattern ) {

    /* If the imposition is set then extract the tl, br point, otherwise
     * get it from the device.
     */
    if ( theClipRecord(impositionclipping) != NULL ) {
      x1 = theX1Clip(impositionclipping) ;
      y1 = theY1Clip(impositionclipping) ;
      x2 = theX2Clip(impositionclipping) ;
      y2 = theY2Clip(impositionclipping) ;
    }
    else {
      x1 = 0 ;
      y1 = 0 ;
      x2 = thegsDeviceW(*gs) - 1 ;
      y2 = thegsDeviceH(*gs) - 1 ;
    }

    /* If the rectangle of the path is larger than the imposition
     * or the device then we're doing the equivalent of an erase to
     * some arbitrary color.
     */
    if ( rectfill->x1 <= x1 &&
         rectfill->y1 <= y1 &&
         rectfill->x2 >= x2 &&
         rectfill->y2 >= y2 )
      return TRUE ;
  }
  return FALSE ;
}

/**
 * Test to see if current path is made-up of one or more disjoint rectangles.
 *
 * Checks if the path is one or more disjoint rectangles. Ignores trailing
 * movetos. Return the number of disjoint sub-path rectangles there are in
 * the given path, or zero if any sub-path is not a rectangle or if the
 * rectangles overlap.  Set *rectfill to to extent of the rectangles.
 * "nzfill" tells us if we are being called from the context of a
 * non-zero fill, in which case we can ignore the overlaping condition,
 * provided all the paths have the same direction.
 */
int32 path_rectangles(PATHLIST *thepath, Bool nzfill, dbbox_t *rectfill)
{
  Bool clockwise = FALSE, first_cw = FALSE, all_same_cw = TRUE;
  int32 nrects;
  dbbox_t bbox, nbbox;

  bbox_clear(&nbbox);
  for ( nrects = 0; thepath; thepath = thepath->next )
  {
    struct { dcoord x, y; } prev = {0, 0}, curr = {0, 0};
    int32 npoints = 0, nsides = 0, prev_horiz = 0, prev_vert = 0;
    LINELIST *pt;
    Bool doubleback = FALSE;

    bbox_clear(&bbox); /* quiet compiler warnings */
    for ( pt = thepath->subpath; pt; pt = pt->next, npoints++ )
    {
      HQASSERT((npoints == 0 && (pt->type == MOVETO || pt->type == MYMOVETO)) ||
               (npoints != 0 && (pt->type != MOVETO && pt->type != MYMOVETO)),
                "Corrupt moveto in path");

      if ( TEST4HUGE(pt->point.x) || TEST4HUGE(pt->point.y) )
        return 0;

      SC_C2D_INT(curr.x, pt->point.x);
      SC_C2D_INT(curr.y, pt->point.y);

      switch ( pt->type )
      {
        case MYMOVETO:
        case MOVETO:
          bbox_store(&bbox, curr.x, curr.y, curr.x, curr.y);
          break;

        case MYCLOSE:
          if ( npoints == 1 )
          {
            /* Note that a sub-path of the form MOVETO MYCLOSE like this can
             * only occur as the last sub-path, in which case it is either on
             * its own (when we need to say it is a fill so the fill code to
             * ignore such degenerate paths operates) or is at the end of a
             * pile of sub-paths, when it can be ignored, and the already
             * determined state of whether the rest forms a rectangle can
             * stand.
             *
             * Now we deal with multiple rectangles in a different manner,
             * it is better to just push all such paths through the generic
             * fill code to stop behaviour varying.
             */
            HQASSERT(thepath->next == NULL,
              "a MOVETO followed by a MYCLOSE is not the last subpath");
            return 0;
          }
          /* fallthru */
        case CLOSEPATH:
        case LINETO:
          if ( curr.x != prev.x && curr.y != prev.y ) /* no diagonal lines */
            return 0;
          if ( curr.x != prev.x || curr.y != prev.y ) /* ignore co-incidence */
          {
            if ( curr.x != prev.x ) /* horizontal line */
            {
              if ( prev_horiz != 0 )
              {
                if ( (curr.x - prev.x) * prev_horiz < 0 )
                {
                  if ( doubleback )
                    return 0;
                  doubleback = TRUE;
                }
              }
              else
              {
                if ( ++nsides > 4 ) /* max rectangle sides ! */
                  return 0;
                prev_horiz = (curr.x > prev.x)?1:-1;
                if ( nsides == 2 )
                  clockwise = (prev_vert*prev_horiz < 0);
                prev_vert  =  0;
              }
            }
            else /* vertical line */
            {
              if ( prev_vert != 0 )
              {
                if ( (curr.y - prev.y) * prev_vert < 0 )
                {
                  if ( doubleback )
                    return 0;
                  doubleback = TRUE;
                }
              }
              else
              {
                if ( ++nsides > 4 ) /* max rectangle sides ! */
                  return 0;
                prev_vert  = (curr.y > prev.y)?1:-1;
                if ( nsides == 2 )
                  clockwise = (prev_vert*prev_horiz > 0);
                prev_horiz =  0;
              }
            }
            if ( nsides > 2 )
            {
              if ( !bbox_contains_point(&bbox, curr.x, curr.y) )
                return 0;
            }
            bbox_union_point(&bbox, curr.x, curr.y);
          }
          break;

        default: /* anything else and it can't be a rectangle */
          return 0;
      }
      prev = curr;
    }
    if ( doubleback && nsides > 2 )
      return 0;
    nrects++;
    if ( nrects == 1 ) /* First rectangle */
    {
      nbbox = bbox;
      first_cw = clockwise;
    }
    else /* 2nd or later rectangles */
    {
      /*
       * For multiple rects, we are OK if
       *   a) We are nzfill and they all have the same clocckwise-ness
       *   b) None of them overlap each other.
       */
      if ( nzfill && all_same_cw && clockwise == first_cw )
        ; /* nzfill overlaps are OK provided all same direction */
      else
      {
        all_same_cw = FALSE;
        if ( bbox_intersects(&bbox, &nbbox) )
          return 0;
      }
      bbox_union(&bbox, &nbbox, &nbbox);
    }
  }
  /* Fill out the rectfill it is not NULL with the bounds of all the rects */
  if ( rectfill )
    *rectfill = nbbox;
  return nrects;
}

/* ----------------------------------------------------------------------------
   function:            initclip()         author:              Andrew Cave
   creation date:       05-Jan-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 172.

---------------------------------------------------------------------------- */
Bool initclip_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gs_initclip(gstateptr) ;
}

Bool clip_device_new(GSTATE *gs)
{
  CLIPRECORD *cliprec ;
  CLIPPATH *pageclip ;

  HQASSERT(gs != NULL, "No gstate for target pagedevice") ;

  pageclip = &thegsPageClip(*gs) ;
  HQASSERT(theClipRecord(*pageclip) == NULL, "Clip record already exists") ;

  /* Get memory in which to store cliprecord. */
  if ( ( cliprec = get_cliprec(mm_pool_temp)) == NULL )
    return error_handler(VMERROR) ;

  /* Set up device clip record. */
  theClipRefCount(*cliprec) = 1 ;
  theClipType(*cliprec) = NZFILL_TYPE ;
  theClipFlat(*cliprec) = 0.0f ;
  thegsPageBaseID(*cliprec) = pageBaseMatrixId ;

  /* Store the rect in the cliprecord path. */
  if ( !doing_mirrorprint ) {
    if (! path_add_four(&theClipPath(*cliprec),
                        theXd1Clip(*pageclip), theYd1Clip(*pageclip),
                        theXd2Clip(*pageclip), theYd1Clip(*pageclip),
                        theXd2Clip(*pageclip), theYd2Clip(*pageclip),
                        theXd1Clip(*pageclip), theYd2Clip(*pageclip)) ) {
      free_cliprec(cliprec, mm_pool_temp) ;
      return FALSE ;
    }
  } else {
    if (! path_add_four(&theClipPath(*cliprec),
                        theXd2Clip(*pageclip), theYd1Clip(*pageclip),
                        theXd1Clip(*pageclip), theYd1Clip(*pageclip),
                        theXd1Clip(*pageclip), theYd2Clip(*pageclip),
                        theXd2Clip(*pageclip), theYd2Clip(*pageclip)) ) {
      free_cliprec(cliprec, mm_pool_temp) ;
      return FALSE ;
    }
  }

  addcliprecord(cliprec, pageclip) ;

  /* Determine if we should re-use the same clip number, so we should end up
     with the same CLIPOBJECT. */
  if ( gs->thePDEVinfo.initcliprec != NULL ) {
    CLIPRECORD *initcliprec = gs->thePDEVinfo.initcliprec ;
    if ( thegsPageBaseID(*cliprec) == thegsPageBaseID(*initcliprec) &&
         bbox_equal(&cliprec->bounds, &initcliprec->bounds) &&
         path_compare(&theClipPath(*cliprec), &theClipPath(*initcliprec),
                      PATH_COMPARE_NORMAL, 0.0) ) {
      HQASSERT(theClipType(*cliprec) == theClipType(*initcliprec),
               "Initclip record clip type modified") ;
      HQASSERT(theClipFlat(*cliprec) == theClipFlat(*initcliprec),
               "Initclip record clip flat modified") ;
      /* Set the clip number of the new clip record to that of the old clip
         record, they refer to the same area. This makes the clip DL state
         lookup match the same object, and so avoid swapping clips
         unnecessarily and avoid polluting the DL clip store. */
      HQASSERT(theClipRefCount(*cliprec) == 1,
               "Too many references to clip record to change its number") ;
      theClipNo(*cliprec) = theClipNo(*initcliprec) ;
    }

    gs_freecliprec(&gs->thePDEVinfo.initcliprec) ;
  }

  gs_reservecliprec(theClipRecord(*pageclip)) ;
  gs->thePDEVinfo.initcliprec = theClipRecord(*pageclip) ;

  return TRUE ;
}

Bool clip_device_correct(CLIPPATH *clippath, GSTATE *gs)
{
  CLIPRECORD *tgtdevclip, *newdevclip ;
  Bool result = TRUE ;

  HQASSERT(clippath != NULL, "No clip details") ;
  HQASSERT(gs != NULL, "No gstate for target pagedevice") ;

  /* Find the device clip record for the new clip details. */
  newdevclip = theClipRecord(*clippath) ;
  HQASSERT(newdevclip != NULL, "No clip record in clip details") ;
  while ( newdevclip->next != NULL )
    newdevclip = newdevclip->next ;

  /* Find the device clip record for the old clip details. */
  tgtdevclip = theClipRecord(thegsPageClip(*gs)) ;
  HQASSERT(tgtdevclip != NULL, "No clip record in clip details") ;
  while ( tgtdevclip->next != NULL )
    tgtdevclip = tgtdevclip->next ;

  /* The device clip should be the same as the initial clip record for the
     target page device. */
  if ( theClipNo(*newdevclip) != theClipNo(*tgtdevclip) &&
       (thegsPageBaseID(*newdevclip) != thegsPageBaseID(*tgtdevclip) ||
        !bbox_equal(&newdevclip->bounds, &tgtdevclip->bounds) ||
        !path_compare(&theClipPath(*newdevclip), &theClipPath(*tgtdevclip),
                      PATH_COMPARE_NORMAL, 0.0)) ) {
    int32 device = thegsDeviceType(*gs) ;
    CLIPRECORD *cliprec, *cliplist ;
    CLIPPATH newclip = { 0 } ;

    /* Start again with the imposition clipping bounds, or a fresh clip
       state. */
    if (doing_imposition &&
        device != DEVICE_CHAR &&
        device != DEVICE_PATTERN1 &&
        device != DEVICE_PATTERN2 &&
        device != DEVICE_NULL ) {
      newclip = impositionclipping ;
    } else {
      reset_clip_for_gs(&newclip, gs) ;
    }

    /* Since we're not going to use addcliprecord() to re-add the target
       device clip record, we need to duplicate the bounds narrowing it
       performs. */
    theClipRecord(newclip) = NULL ; /* Force device boundary calculation */
    clip_bounds_from_rbounds(tgtdevclip, &newclip) ;

    /* The bounds of the tgtdevclip record started out the same as newclip,
       but may have been narrowed by prepare_dl_clipping() to reflect the
       actual pixels touched. */
    HQASSERT(theX1Clip(newclip) <= theX1Clip(*tgtdevclip) &&
             theY1Clip(newclip) <= theY1Clip(*tgtdevclip) &&
             theX2Clip(newclip) >= theX2Clip(*tgtdevclip) &&
             theY2Clip(newclip) >= theY2Clip(*tgtdevclip),
             "Target device clip record exceeds device clip bounds") ;

    /* Build a new CLIPPATH with the target gstate's device clip record first,
       followed by copies of the clip records from clippath. */
    thegsPageBaseID(newclip) = thegsPageBaseID(*clippath) ;
    theClipRefCount(newclip) = theClipRefCount(*clippath) ;
    theClipStack(newclip) = theClipStack(*clippath) ;

    /* From now on, we have to clean up the new clip records */
#define return DO_NOT_return_FALL_THROUGH_INSTEAD

    /* We deliberately don't use addcliprecord() to put the existing device
       clip record on the start of the new chain, we don't want to reset its
       clip number and attributes. */
    gs_reservecliprec(tgtdevclip) ;
    theClipRecord(newclip) = tgtdevclip ;

    cliplist = reverse_cliprecord_list(&theClipRecord(*clippath)) ;
    HQASSERT(cliplist != NULL, "No reversed clip path records") ;
    HQASSERT(theClipRecord(*clippath) == NULL,
             "Clip records not reversed correctly") ;

    /* Similar to clippath, we only add the device clip for the chain we're
       installing to the new chain if it is the only clip in the chain. We've
       already set up a device clip record for the new chain, so the
       installed device clip record will just become a normal clip record. */
    for ( cliprec = cliplist->next ? cliplist->next : cliplist ;
          cliprec != NULL ; cliprec = cliprec->next ) {
      CLIPRECORD *newcliprec ;

      /* Get memory in which to store cliprecord. */
      if ( ( newcliprec = get_cliprec(mm_pool_temp)) == NULL ) {
        result = error_handler(VMERROR) ;
        break ;
      }

      /* Set up device clip record. */
      theClipRefCount(*newcliprec) = 1 ;
      theClipType(*newcliprec) = (theClipType(*cliprec)  &
                                  ~(CLIPISRECT|CLIPISDEGN|CLIPNORMALISED)) ;
      theClipFlat(*newcliprec) = theClipFlat(*cliprec) ;
      thegsPageBaseID(*newcliprec) = thegsPageBaseID(*cliprec) ;

      if ( !path_copy(&theClipPath(*newcliprec), &theClipPath(*cliprec),
                      mm_pool_temp) ) {
        free_cliprec(newcliprec, mm_pool_temp) ;
        result = FALSE ;
        break ;
      }

      addcliprecord(newcliprec, &newclip) ;
    }

    theClipRecord(*clippath) = reverse_cliprecord_list(&cliplist) ;
    HQASSERT(cliplist == NULL, "Clip records not reversed correctly") ;

    if ( result ) {
      gs_freecliprec(&theClipRecord(*clippath)) ;
      *clippath = newclip ;
    } else {
      gs_freecliprec(&theClipRecord(newclip)) ;
    }
#undef return
  }

  return result ;
}

Bool gs_initclip(GSTATE *gs)
{
  CLIPPATH *pageclip = &thegsPageClip(*gs) ;
  int32 device ;

  gs_freecliprec(&theClipRecord(*pageclip)) ;

  reset_clip_for_gs(pageclip, gs) ;

  device = thegsDeviceType(*gs) ;
  if (device != DEVICE_CHAR &&
      device != DEVICE_PATTERN1 &&
      device != DEVICE_PATTERN2 ) {
    if ( !doing_imposition )
      gs_freecliprec(&theClipRecord(impositionclipping)) ;
    if ( doing_imposition && device != DEVICE_NULL )
      imposition_update_rectangular_clipping(gs) ;
  }

  return clip_device_new(gs) ;
}

/* This routine MUST be immediately followed by a routine that calls
   addcliprecord(). */
static void reset_clip_for_gs(CLIPPATH *clippath, GSTATE *gs)
{
  HQASSERT(clippath != NULL, "No gstate") ;
  HQASSERT(gs != NULL, "No gstate for target pagedevice") ;

  theX1Clip(*clippath) = 0 ;
  theY1Clip(*clippath) = 0 ;

  if ( thegsDeviceType(*gs) == DEVICE_NULL ||
       thegsDeviceType(*gs) == DEVICE_ERRBAND ) {
    theX2Clip(*clippath) = 0 ;
    theY2Clip(*clippath) = 0 ;
  } else {
    theX2Clip(*clippath) = thegsDeviceW(*gs) ;
    theY2Clip(*clippath) = thegsDeviceH(*gs) ;
#ifdef DEBUG_BUILD
    if ( thegsDeviceType(*gs) == DEVICE_BAND ||
         thegsDeviceType(*gs) == DEVICE_SUPPRESS ) {
      if ( theX1Clip(*clippath) < debug_clip.x1 )
        theX1Clip(*clippath) = debug_clip.x1 ;
      if ( theY1Clip(*clippath) < debug_clip.y1 )
        theY1Clip(*clippath) = debug_clip.y1 ;
      if ( theX2Clip(*clippath) - 1 > debug_clip.x2 )
        theX2Clip(*clippath) = debug_clip.x2 + 1 ;
      if ( theY2Clip(*clippath) - 1 > debug_clip.y2 )
        theY2Clip(*clippath) = debug_clip.y2 + 1 ;
    }
#endif
  }

  theXd1Clip(*clippath) = theX1Clip(*clippath) ;
  theYd1Clip(*clippath) = theY1Clip(*clippath) ;
  theXd2Clip(*clippath) = theX2Clip(*clippath) ;
  theYd2Clip(*clippath) = theY2Clip(*clippath) ;

  HQASSERT(theClipRecord(*clippath) == NULL,
           "Clip record should not exist when resetting clip") ;
}

static CLIPRECORD *reverse_cliprecord_list(CLIPRECORD **c_recp)
{
  CLIPRECORD *c_rec = *c_recp ;
  CLIPRECORD *previous = NULL ;

  while ( c_rec != NULL ) {
    CLIPRECORD *next = c_rec->next ;

    c_rec->next = previous ;
    previous = c_rec ;
    c_rec = next ;
  }

  *c_recp = NULL ;

  return previous ;
}

void imposition_update_rectangular_clipping( GSTATE *gs )
{
  int32 device = thegsDeviceType(*gs) ;
  CLIPRECORD *userclip ;
  CLIPPATH *pageclip = &thegsPageClip(*gs) ;

  thegsPageBaseID(*pageclip) = pageBaseMatrixId ;

  userclip = reverse_cliprecord_list(&theClipRecord(*pageclip)) ;
  HQASSERT(theClipRecord(*pageclip) == NULL, "Gstate clipping not cleared") ;

  reset_clip_for_gs(pageclip, gs) ;

  if ( device != DEVICE_NULL &&
       device != DEVICE_CHAR &&
       device != DEVICE_PATTERN1 &&
       device != DEVICE_PATTERN2 ) {
    CLIPRECORD *imposeclip =
      reverse_cliprecord_list(&theClipRecord(impositionclipping)) ;

    HQASSERT(theClipRecord(impositionclipping) == NULL,
             "Imposition clipping not cleared") ;

    while ( imposeclip ) {
      CLIPRECORD *tmpclip = imposeclip ;
      imposeclip = imposeclip->next ;

      theClipType(*tmpclip) &= ~(CLIPISRECT|CLIPISDEGN|CLIPNORMALISED) ;
      addcliprecord(tmpclip, pageclip) ;
    }

    /* The clip chain was built up in pageclip. Steal the imposition clip
       from it again. */
    impositionclipping = *pageclip ;
    theClipStack(impositionclipping) = NULL ;
    theClipRecord(*pageclip) = NULL ;
  }

  while ( userclip ) {
    CLIPRECORD *tmpclip = userclip ;
    userclip = userclip->next ;

    theClipType(*tmpclip) &= ~(CLIPISRECT|CLIPISDEGN|CLIPNORMALISED) ;
    addcliprecord(tmpclip, pageclip) ;
  }
}

Bool clippingisdegenerate(GSTATE *gs)
{
  CLIPRECORD *c_rec ;
  int32 device ;

  /* Use of CURRENT_DEVICE_SUPPRESSES_STATE() requires current gstate: */
  HQASSERT(gs == gstateptr,
           "Degenerate clipping test only valid for current gstate") ;

  if ( CURRENT_DEVICE_SUPPRESSES_STATE() )
    return TRUE ;

  c_rec = theClipRecord(thegsPageClip(*gs)) ;
  if ( c_rec )
    if ( theClipType(*c_rec) & CLIPISDEGN )
      return TRUE ;

  device = thegsDeviceType(*gs) ;
  if (device == DEVICE_CHAR ||
      device == DEVICE_PATTERN1 ||
      device == DEVICE_PATTERN2 )
    return FALSE ;

  if ( theClipRecord(impositionclipping) != NULL ) {
    if ( theClipType(*theClipRecord(impositionclipping)) & CLIPISDEGN )
      return TRUE ;
    /* If top clip and imposition clip don't intersect, it's degenerate. */
    if ( c_rec != NULL &&
         !bbox_intersects(&c_rec->bounds,
                          &theClipRecord(impositionclipping)->bounds) )
      return TRUE ;
  }

  return FALSE ;
}

CLIPRECORD *clippingiscomplex(GSTATE *gs, Bool useimpositionclipping)
{
  int32 device ;
  CLIPRECORD *c_rec ;

  /* Use of CURRENT_DEVICE_SUPPRESSES_STATE() requires current gstate: */
  HQASSERT(gs == gstateptr,
           "Complex clipping test only valid for current gstate") ;

  if ( CURRENT_DEVICE_SUPPRESSES_STATE() )
    return NULL ;

  for (c_rec = theClipRecord(thegsPageClip(*gs)) ;
       c_rec ;
       c_rec = c_rec->next) {
    if ( ! (theClipType(*c_rec) & CLIPISRECT) )
      return c_rec ;
  }

  device = thegsDeviceType(*gs) ;
  if (device == DEVICE_CHAR ||
      device == DEVICE_PATTERN1 ||
      device == DEVICE_PATTERN2 )
    return NULL ;

  if ( ! useimpositionclipping )
    return NULL ;

  for (c_rec = theClipRecord(impositionclipping) ;
       c_rec ;
       c_rec = c_rec->next) {
    if ( ! (theClipType(*c_rec) & CLIPISRECT) )
      return c_rec ;
  }

  return NULL ;
}

/*
Log stripped */

