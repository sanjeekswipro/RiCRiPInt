/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:dl_shade.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Display list manipulation and rendering functions for smooth shading
 */

#include "core.h"
#include "swerrors.h" /* VMERROR */
#include "swoften.h"
#include "debugging.h"
#include "timing.h"
#include "interrupts.h"
#include "often.h"
#include "graphics.h"
#include "gu_chan.h"
#include "display.h"  /* dlBackdropRender */
#include "routedev.h" /* DEVICE_* */
#include "dl_color.h" /* p_ncolor_t etc */
#include "vndetect.h" /* VIGNETTEOBJECT etc */
#include "gschcms.h"  /* REPRO_TYPE_VIGNETTE */
#include "dl_free.h"  /* free_dl_object */
#include "control.h"  /* interrupts_clear */
#include "rcbadjst.h" /* rcba_doing_color_adjust (DEBUG) */
#include "trap.h"     /* isTrappingActive */
#include "hdlPrivate.h" /* HDL functionality */
#include "gs_tag.h"   /* TAG_BYTES */
#include "dl_purge.h" /* TAG_BYTES */
#include "shadex.h"
#include "dl_shade.h"
#include "surface.h"
#include "shadesetup.h"
#include "preconvert.h"


/** Calculates the bounding box of the triangle mesh. */
void gouraudbbox(GOURAUDOBJECT *gouraud, const dbbox_t* clip, dbbox_t* bbox)
{
  dcoord xo, x1, x2, y1, y2 ;

  HQASSERT(gouraud != NULL, "gouraud null") ;
  HQASSERT(clip != NULL, "gouraud clip null") ;
  HQASSERT(bbox != NULL, "gouraud bbox null") ;

  y1 = gouraud->coords[1] ;
  y2 = gouraud->coords[5] ;
  x1 = gouraud->coords[0] ;
  x2 = gouraud->coords[4] ;
  xo = gouraud->coords[2] ;
  if ( x1 > x2 ) { /* Make sure order is correct */
    dcoord tmp = x1 ; x1 = x2 ; x2 = tmp ;
  }
  if ( xo < x1 ) /* replace high or low if necessary */
    x1 = xo ;
  else if ( xo > x2 )
    x2 = xo ;

  if ( x1 < clip->x1 )
    x1 = clip->x1 ;
  if ( x2 > clip->x2 )
    x2 = clip->x2 ;
  if ( y1 < clip->y1 )
    y1 = clip->y1 ;
  if ( y2 > clip->y2 )
    y2 = clip->y2 ;

  bbox_store(bbox, x1, y1, x2, y2) ;
}

/** Add gouraud object to display list. */
Bool addgourauddisplay(DL_STATE *page, GOURAUDOBJECT *gouraud)
{
  LISTOBJECT *lobj = NULL ;
  dbbox_t bbox;
  Bool added;

  HQASSERT(gouraud, "No gouraudobject") ;
  HQASSERT(gouraud->gsize > sizeof(GOURAUDOBJECT), "Bad gouraudobject size");

#if 0
  /* I'd like to use this assert, to reduce sorting when the linear
     interpolated bitblit is implemented, but it doesn't work because of the
     integer subdivision; for example, point 2 can be slightly above and
     right of point 3, but when subdivided the new points 2 and 3 are
     truncated to the same line, with the x-coordinates now out of order. */
  HQASSERT((gouraud->coords[1] < gouraud->coords[3] ||
            (gouraud->coords[1] == gouraud->coords[3] &&
             gouraud->coords[0] <= gouraud->coords[2])) &&
           (gouraud->coords[3] < gouraud->coords[5] ||
            (gouraud->coords[3] == gouraud->coords[5] &&
             gouraud->coords[2] <= gouraud->coords[4])),
           "Triangle gouraud points out of order") ;
  /* Weaker assert follows: */
#endif
  HQASSERT(gouraud->coords[1] <= gouraud->coords[3] &&
           gouraud->coords[3] <= gouraud->coords[5],
           "Triangle gourarud points out of order") ;

  if ( !finishaddchardisplay(page, 1) )
    return FALSE;

  /* Get Bounding box of triangle patch */
  gouraudbbox(gouraud, &cclip_bbox, &bbox);

  if ( !make_listobject(page, RENDER_gouraud, &bbox, &lobj) )
    return FALSE ;
  lobj->dldata.gouraud = gouraud;

  if ( !add_listobject(page, lobj, &added) )
    return FALSE;
  if ( added ) {
    /** \todo @@@ TODO FIXME: make this test more specific. Since we've
     * added to the HDL and have therefore updated the region map, we should
     * be able to determine if this gouraud is likely to require compositing.
     * The hard case is for "floating" HDLs, which may be used for patterns in
     * future, since we only know if we require compositing when these are
     * attached to the state of an object which requires compositing.
     */
    if ( !dl_reserve_band(page, RESERVED_BAND_SELF_INTERSECTING) )
      return FALSE ;
  }
  return TRUE;
}

/** Test to ensure that cross product stays within 32-bit integer range. */
Bool cross_check(int32 x0, int32 y0, int32 x1, int32 y1, int32 x2, int32 y2)
{
  int32 ax, bx, ay, by, maxab ;

  /* Assume (but assert) that coordinate/colour differences fit in int32. */
  HQASSERT((x1 ^ x0) >= 0 ||
           (x1 >= 0 && x1 <= x0 + MAXINT32) ||
           (x1 < 0 && x1 >= x0 - MAXINT32),
           "x0..x1 cross product coordinate overflow") ;
  ax = x1 - x0 ;

  HQASSERT((y1 ^ y0) >= 0 ||
           (y1 >= 0 && y1 <= y0 + MAXINT32) ||
           (y1 < 0 && y1 >= y0 - MAXINT32),
           "y0..y1 cross product coordinate overflow") ;

  ay = y1 - y0 ;

  HQASSERT((x2 ^ x0) >= 0 ||
           (x2 >= 0 && x2 <= x0 + MAXINT32) ||
           (x2 < 0 && x2 >= x0 - MAXINT32),
           "x0..x2 cross product coordinate overflow") ;
  bx = x2 - x0 ;

  HQASSERT((y2 ^ y0) >= 0 ||
           (y2 >= 0 && y2 <= y0 + MAXINT32) ||
           (y2 < 0 && y2 >= y0 - MAXINT32),
           "y0..y2 cross product coordinate overflow") ;

  by = y2 - y0 ;

  /* Cross product is bx * ay - ax * by. Test that parts are well formed,
     and that whole is within range. */
  maxab = MAXINT32 ;
  if ( bx > 0 )
    maxab /= bx ;
  else if ( bx < 0 )
    maxab /= -bx ;

  if ( ay > maxab || -ay > maxab )
    return FALSE ;

  maxab = MAXINT32 ;
  if ( ax > 0 )
    maxab /= ax ;
  else if ( ax < 0 )
    maxab /= -ax ;

  if ( by > maxab || -by > maxab )
    return FALSE ;

  /* Components are within range, check complete cross product. */
  bx = bx * ay ;
  ax = ax * by ;
  if ( (bx ^ ax) < 0 ) {
    if ( bx >= 0 ) {
      if ( bx > ax + MAXINT32 )
        return FALSE ;
    } else {
      if ( bx < ax - MAXINT32 )
        return FALSE ;
    }
  }

  /* Cross product is within MAXINT32 range. This is sufficient to prevent
     overflow in DDAs, because the fractional components are held as uint32.
     There is one spare bit so that DDAs can be added before renormalisation,
     and for carries during subtract or divide. */
  return TRUE ;
}


/* Helper function and struct definition for Gouraud DL color iterator */

typedef struct {
  Bool (*callback)(p_ncolor_t *, void *data) ;
  void *callback_data ;
  p_ncolor_t *nextcolor ; /* Next colorvalue in decomposition */
  uintptr_t flags ;       /* Current flags */
  int32 bits ;            /* Number of bits left in current flags */
#if defined( ASSERT_BUILD )
  int32 linear_triangles ; /* Number of triangles seen */
#endif
} GOURAUD_ITERATE ;

static Bool gouraud_iterate_sub(GOURAUD_ITERATE *iter)
{
  uintptr_t mask ;

  HQASSERT(iter, "No gfill argument to render_gouraud_sub") ;
  HQASSERT(iter->bits >= 0 && iter->bits <= sizeof(uintptr_t) * 8,
           "Strange number of bits left in flagptr") ;

  SwOftenUnsafe();
  if ( !interrupts_clear(allow_interrupt) )
    return report_interrupt(allow_interrupt);

  if ( --iter->bits < 0 ) { /* Get a new flag word if no more bits */
    iter->bits = sizeof(uintptr_t) * 8 - 1 ;
    iter->flags = *((uintptr_t *)iter->nextcolor) ;
    HQASSERT(sizeof(uintptr_t) == sizeof(p_ncolor_t),
             "flag size is not the same as p_ncolor_t") ;
    iter->nextcolor += 1 ;
  }

  mask = (uintptr_t)1 << iter->bits ;

  if ( iter->flags & mask ) { /* Subdivide and try again */
    if ( ! (*iter->callback)(&iter->nextcolor[0], iter->callback_data) ||
         ! (*iter->callback)(&iter->nextcolor[1], iter->callback_data) ||
         ! (*iter->callback)(&iter->nextcolor[2], iter->callback_data) )
      return FALSE ;

    iter->nextcolor += 3 ;

    /* Recursively call four times, for decomposed triangles. Since we're
       just iterating DL colours we don't need to pass any information about
       which sub-triangle is being checked; each decomposition introduces
       three new dl colors to examine. */
    if ( !gouraud_iterate_sub(iter) ||
         !gouraud_iterate_sub(iter) ||
         !gouraud_iterate_sub(iter) ||
         !gouraud_iterate_sub(iter) )
      return FALSE ;
  }
#if defined( ASSERT_BUILD )
  else {
    ++iter->linear_triangles ;
  }
#endif


  return TRUE ;
}

/** Iterator for Gouraud's DL colors, for vignette overprint unification and
   the like. Callback function is called for each colour, with pointer to
   p_ncolor so that colour can be released and altered if necessary. Callback
   should return TRUE if successful, FALSE for error to abort iteration. Void
   data pointer is passed through to callback. */

Bool gouraud_iterate_dlcolors(LISTOBJECT *lobj,
                              Bool (*callback)(p_ncolor_t *color, void *data),
                              void *data)
{
  GOURAUDOBJECT *gour = (GOURAUDOBJECT *)load_dldata(lobj);
  GOURAUD_ITERATE iter ;

  iter.callback = callback ;
  iter.callback_data = data ;
  iter.nextcolor = (p_ncolor_t *)(gour + 1) ;
  iter.flags = gour->flags ;
  iter.bits = sizeof(uintptr_t) * 8 ;
#if defined( ASSERT_BUILD )
  iter.linear_triangles = 0 ;
#endif

  if ( ! (*iter.callback)(&iter.nextcolor[0], iter.callback_data) ||
       ! (*iter.callback)(&iter.nextcolor[1], iter.callback_data) ||
       ! (*iter.callback)(&iter.nextcolor[2], iter.callback_data) )
    return FALSE ;

  iter.nextcolor += 3 ;

  if ( !gouraud_iterate_sub(&iter) )
    return FALSE ;

  HQASSERT(iter.linear_triangles == *(int32 *)iter.nextcolor,
           "Mismatch in number of triangles decomposed and iterated") ;
  rewrite_dldata(lobj);
  return TRUE ;
}


/* Functions for HDL object diversion for Shfill. */

static LISTOBJECT *shfill_lobj = NULL ;

static int32 addobjecttoshfilldl(DL_STATE *page, LISTOBJECT *lobj)
{
  HQASSERT(!analyzing_vignette(), "shouldn't be analyzing a vignette");
  HQASSERT(lobj->opcode == RENDER_shfill_patch ||
           lobj->opcode == RENDER_fill || lobj->opcode == RENDER_rect ||
           lobj->opcode == RENDER_quad ||
           lobj->opcode == RENDER_image || lobj->opcode == RENDER_gouraud,
           "Unexpected shfill object type");
  HQASSERT(!bbox_is_empty(&(lobj->bbox)), "bbox is empty");

  if ( dl_is_none(lobj->p_ncolor) )
    return DL_NotAdded;

  /* If we are in the recombine adjustment phase, we have to use the object
     state of the shfill object to make sure that clipping is set up properly.
     We also have to ensure that the gstags data is correct with respect to
     this object, so we re-allocate the LISTOBJECT (making a copy of the
     shfill LISTOBJECT), and alter the appropriate fields. The return of
     DL_Added is somewhat suspect here, since clients might still access and
     modify the LISTOBJECT parameter. I will at least make it likely that a
     debug build will fail if this happens by clearing the fields before
     freeing the listobject. */
  if ( lobj->objectstate != shfill_lobj->objectstate ) {
    /* Do we need to re-allocate the whole object? */
    if ( TAG_BYTES(lobj->objectstate) != TAG_BYTES(shfill_lobj->objectstate) ) {
      LISTOBJECT *new_lobj;

      if ( !make_listobject_copy(page, shfill_lobj, &new_lobj) ) /* RENDER_shfill */
        return DL_Error;
      new_lobj->opcode = lobj->opcode;
      new_lobj->marker = lobj->marker;
      new_lobj->spflags = lobj->spflags;
      new_lobj->disposition = lobj->disposition;
      new_lobj->dldata.shade = lobj->dldata.shade;
      dl_release(page->dlc_context, &new_lobj->p_ncolor);
      dl_copy_release(&new_lobj->p_ncolor, &lobj->p_ncolor);
      if ( (lobj->spflags & RENDER_RECOMBINE) != 0 && lobj->attr.planes != NULL) {
        dl_release(page->dlc_context, &new_lobj->attr.planes);
        dl_copy_release(&new_lobj->attr.planes, &lobj->attr.planes);
      } else
        new_lobj->attr.planes = lobj->attr.planes;

      free_listobject(lobj, page);
      lobj = new_lobj;
    } else { /* gstagstructure matches; don't need to re-allocate object */
      lobj->objectstate = shfill_lobj->objectstate;
    }
  }

  /* We may need the self-intersection band to render this shfill if it's
     transparent. */
  if ( !dl_reserve_band(page, RESERVED_BAND_SELF_INTERSECTING) )
    return DL_Error ;

  if ( !hdlAdd(shfill_lobj->dldata.shade->hdl, lobj) )
    return DL_Error;

  return DL_Added;
}

/** Methods to control interception of objects. */
Bool setup_shfill_dl(DL_STATE *page, LISTOBJECT *lobj)
{
  SHADINGOBJECT *shade;

  HQASSERT(shfill_lobj == NULL, "Shfill LISTOBJECT should be NULL at start");
  HQASSERT(lobj != NULL, "LISTOBJECT parameter invalid");
  HQASSERT(lobj->opcode == RENDER_shfill, "Existing object is not a shfill") ;

  /* Allocate SHADINGOBJECT now, will get populated later */
  shade = (SHADINGOBJECT *)dl_alloc(page->dlpools, sizeof(SHADINGOBJECT),
                                    MM_ALLOC_CLASS_SHADING);
  if ( ( lobj->dldata.shade = shade ) == NULL )
    return error_handler(VMERROR);

  shade->colorWorkspace = shade->colorWorkspace_base = NULL;
  shade->info = NULL;
  shade->hdl = NULL;

  if ( !hdlOpen(page, TRUE, HDL_SHFILL, &lobj->dldata.shade->hdl) )
    return FALSE;

  device_current_addtodl = addobjecttoshfilldl;
  shfill_lobj = lobj;

  probe_begin(SW_TRACE_DL_SHFILL, (intptr_t)lobj) ;

  return TRUE;
}

/** Create workspace for rendering the shfill. The workspace may need
    reallocating after the shfill has been preconverted. */
static Bool reset_shfill_workspace(DL_STATE *page, SHADINGinfo *sinfo,
                                   LISTOBJECT *lobj, GS_COLORinfo *colorInfo)
{
  SHADINGOBJECT *shadeobj = lobj->dldata.shade;
  dl_color_t dlc;
  int32 nchannels;
  size_t workSize = 0;

  HQASSERT(shadeobj != NULL, "Lost DL shading object");

  /* The shfill code may be asked to decompose every channel at once (for color
     RLE, compositing, trapping, retained raster, pixel-interleaved output etc),
     so make sure workspace is big enough.  The shfill may be rendered in blend
     space or device space depending on whether it's composited or not. */
  nchannels = dl_num_channels(lobj->p_ncolor);
  if ( !sinfo->preseparated ) {
    dlc_from_lobj_weak(lobj, &dlc);
    if ( !preconvert_probe(page->currentGroup, GSC_SHFILL,
                           sinfo->spotno, REPRO_TYPE_VIGNETTE, &dlc,
                           sinfo->lca) )
      return FALSE;
    if ( DLC_NUM_CHANNELS(dlc_currentcolor(page->dlc_context)) > nchannels )
      nchannels = DLC_NUM_CHANNELS(dlc_currentcolor(page->dlc_context));
  }
  if ( nchannels < 1 ) nchannels = 1;

  /* Include alpha and type channels. */
  if ( guc_backdropRasterStyle(gsc_getTargetRS(colorInfo)) )
    ++nchannels; /* include alpha channel when inside a group */
  ++nchannels; /* always include type channel */

  /* If preconverting the shfill, may already have workspace. */
  if ( shadeobj->colorWorkspace_base != NULL ) {
    if ( shadeobj->nchannels == nchannels )
      return TRUE; /* already have the right sized workspace */

    dl_free(page->dlpools, shadeobj->colorWorkspace_base,
            gouraud_dda_size(shadeobj->nchannels) + 7,
            MM_ALLOC_CLASS_SHADING);
    shadeobj->colorWorkspace_base = shadeobj->colorWorkspace = NULL;
  }

  /* Now safe to update nchannels. */
  shadeobj->nchannels = nchannels ;

  if ( nchannels > RENDERSH_MAX_FIXED_CHANNELS )
    /* Then we need some space for calculating the intermediate colors. We
       statically allocate enough for small numbers of colorants, but for the
       much rarer case of many simultaneous colorants we do this on the fly. At
       present this only applies to color rle (which is pixel interleaved, and
       only pixel interleaved needs more than 1 colorant at once). If we
       implement arbitrary numbers of contone pixel interleaved colorants, we
       would need to do this then also. The 7's are to align this memory on a
       doubleword boundary */
    workSize = gouraud_dda_size(nchannels) + 7;

  if ( workSize > 0 ) {
    shadeobj->colorWorkspace_base =
      dl_alloc(page->dlpools, workSize, MM_ALLOC_CLASS_SHADING);
    if ( shadeobj->colorWorkspace_base == NULL )
      return error_handler(VMERROR);

    shadeobj->colorWorkspace =
      (void *)DWORD_ALIGN_UP(uintptr_t, shadeobj->colorWorkspace_base);
    HqMemZero(shadeobj->colorWorkspace, workSize & ~7);
  }

  return TRUE;
}

Bool reset_shfill_dl(DL_STATE *page, SHADINGinfo *sinfo,
                     Bool result, uint16 smoothnessbands,
                     USERVALUE noise, uint16 noisesize,
                     dbbox_t *bbox)
{
  HDL **hdlPtr ;

  HQASSERT(bbox, "Nowhere to store shfill bbox") ;
  bbox_clear(bbox) ;

  HQASSERT(shfill_lobj, "Invalid saved LISTOBJECT") ;

  hdlPtr = &shfill_lobj->dldata.shade->hdl ;
  HQASSERT(hdlPtr && *hdlPtr, "No HDL in saved LISTOBJECT") ;

  device_current_addtodl = device_table_addtodl[thegsDeviceType(*gstateptr)] ;

  if ( result ) { /* Add constructed object to this DL */

    /* At this point, hdlColor is a merge of the colorants in the shfill.  We
       need the colorant value to reflect whether the channel is all clear, all
       set, or some combination or intermediate value.  If recombine colour
       adjustment used the colour at setg(), and it was clear but the shading
       contained intermediate levels, it might accidentally remove a channel
       believing it to be an overprint.  N.B., Clear/set/intermediate is only
       valid for the current colorspace; if the shfill is converted to another
       (including halftone levels), the merge must be recomputed. */
    if ( hdlOrderList(*hdlPtr) != NULL ) {
      dl_color_t dlc_merged;

      dlc_clear(&dlc_merged);
      result = dlc_copy(page->dlc_context, &dlc_merged, hdlColor(*hdlPtr));
      if ( result ) {
        /* Successful so far; now populate a SHADINGOBJECT and colour
           workspace for the shfill. */
        SHADINGOBJECT *shadeobj;

        dl_release(page->dlc_context, &shfill_lobj->p_ncolor) ;
        dlc_to_lobj_release(shfill_lobj, &dlc_merged) ;

        shadeobj = shfill_lobj->dldata.shade;
        HQASSERT(shadeobj, "Lost DL shading object");
        shadeobj->mbands = smoothnessbands ;
        shadeobj->noise = noise ;
        shadeobj->noisesize = noisesize ;

        result = reset_shfill_workspace(page, sinfo, shfill_lobj,
                                        gstateptr->colorInfo);
      } else {
        dlc_release(page->dlc_context, &dlc_merged);
      }
    }
  }

  result = hdlClose(hdlPtr, result) ;

  if ( result && *hdlPtr )
    hdlBBox(*hdlPtr, bbox) ;

  probe_end(SW_TRACE_DL_SHFILL, (intptr_t)shfill_lobj) ;

  shfill_lobj = NULL ;

  return result ;
}

void init_C_globals_dl_shade(void)
{
  shfill_lobj = NULL ;
}

/* Log stripped */
