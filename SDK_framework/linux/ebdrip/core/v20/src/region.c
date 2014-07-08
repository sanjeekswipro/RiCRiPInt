/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:region.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * The page is notionally split up into regions.  Regions are the same
 * rectangular size, apart from the last row and column which may be smaller.
 * Each region can be composited or direct rendered and this decision is made
 * based on the DL objects that intersect each region.
 *
 * A region is marked for compositing if it contains transparency or if it
 * overprints another object.  Since compositing is expensive the code tries
 * avoid marking regions for compositing.  For overprinted objects which paint
 * in virtual colorants (i.e. spots which will eventually be converted to
 * process) the background is tracked to determine if anything non-white has
 * been painted (on a per-colorant basis).  This optimisation was particularly
 * beneficial for some preseparated jobs.
 *
 * Softmask images are prevalent in photobook jobs and often large areas of the
 * image are completely transparent and do not need compositing.
 */

#include "core.h"
#include "region.h"
#include "swerrors.h"
#include "dlstate.h"
#include "hdlPrivate.h"
#include "pclAttrib.h"
#include "params.h"
#include "group.h"
#include "groupPrivate.h"
#include "dl_image.h"
#include "rcbcntrl.h"
#include "backdrop.h"
#include "jobmetrics.h"
#include "gu_chan.h"
#include "dl_foral.h"
#include "gstack.h"
#include "graphics.h"
#include "trap.h"
#include "pattern.h"
#include "preconvert.h"
#include "imexpand.h" /* im_expand_ims_alpha et. al. */
#include "imstore.h"
#include "vndetect.h"

/**
 * Region dimensions.
 *
 * Backdrop actually makes blocks that are 256 wide, but we can still vary
 * the region width independently to ensure small areas requiring compositing
 * don't end up requiring too much wasted compositing. Having backdrop use
 * 256 wide blocks means large areas of compositing, made up of consecutive
 * regions, can benefit from the reduction in the overhead with each block
 * (bigger blocks means fewer blocks). The region width and height are set by
 * the surface definition, and may configurable, but is normally set to the
 * band height.
 */
#define REGION_WIDTH (128)

/** Set the region height, limited by MaxBackdropHeight. */
void set_region_size(DL_STATE *page)
{
  int32 band_height = page->band_lines ;
  int32 max_height = page->max_region_height ;

  HQASSERT(max_height >= 0, "Max region height should not be negative") ;

  /** \todo ajcd 2009-12-09: Set the region width. This is taken from the
      transparency surface, if it is found. We default to the original
      backdrop region width if no transparency surface is found, just so
      nothing explodes. If the pagedevice parameter MaxBackdropWidth is set,
      it only applies if the backdrop has a variable width. */
  page->region_width = REGION_WIDTH ;
  if ( page->surfaces != NULL ) {
    const transparency_surface_t *transurf ;
    if ( (transurf = surface_find_transparency(page->surfaces)) != NULL ) {
      HQASSERT(transurf->region_width >= 0,
               "Transparency surface region width should not be negative") ;
      if ( transurf->region_width == 0 ) {
        page->region_width = page->page_w ;
        if ( page->max_region_width > 0 &&
             page->region_width > page->max_region_width )
          page->region_width = page->max_region_width ;
      } else
        page->region_width = transurf->region_width ;
    }
  }

  if ( band_height > max_height && max_height > 0 ) {
    /* If the band height is very large, we try to limit the region size. We
       try to find an exact factor of the band height as close as possible to
       the max region height. This algorithm just uses trial division for
       the factorisation, because we don't expect MaxBackdropHeight to be set
       very large. If there is only one band, then we can select the exact
       max backdrop height we wanted. */
    if ( page->sizedisplaylist > 1 ) {
      while ( max_height > 1 ) {
        if ( band_height % max_height == 0 ) {
          page->region_height = max_height ;
          return ;
        }
        --max_height ;
      }
      /* We've run out of possibilities. Since we can't exceed max_height,
         we'll have to use a region height of 1. This will probably cause
         trouble. */
      HQTRACE(TRUE, ("Setting region height to 1 is likely to be very inefficient"));
    }

    page->region_height = max_height ;
    return ;
  }

  page->region_height = band_height ;
}

/**
 * Region maps are used to indicate which regions to composite or on a
 * per-colorant basis to indicate where objects have been painted (these
 * region maps are used to minimise the amount of compositing).
 */
static Bool region_map_create(DL_STATE *page, BitGrid **regionMap)
{
  dbbox_t pageFrame;
  Size2d regionSize;

  bbox_store(&pageFrame, 0, 0, page->page_w - 1, page->page_h - 1);

  regionSize = size2dNew(page->region_width, page->region_height);

  return bitGridNewMapped(&pageFrame, regionSize, regionMap) ;
}

/**
 * Region information - one per colorant of the virtual device raster style.
 * Used to track the background for optimising overprinted objects.
 */
typedef struct RegionInfo RegionInfo;
struct RegionInfo {
  COLORANTINDEX   ci;                 /**< Region info is for this colorant index. */
  Bool            processColorant;    /**< Colorant is process. */
  Bool            virtualColorant;    /**< Spot colorant that must eventually be
                                           converted to process. */
  Bool            currentObject;      /**< ci is present in the dl color of the
                                           current object. */
  Bool            currentObjectWhite; /**< The associated value for ci is white
                                           in the current object. */
  BitGrid        *bitGrid;            /**< Bit grid to track white and non-white
                                           regions. */
  RegionInfo     *next;               /**< List of RegionInfos. */
};

/**
 * Create a region info object for tracking the background.
 */
static Bool region_info_create(DL_STATE *page, RegionInfo **head,
                               COLORANTINDEX ci,
                               Bool processColorant, Bool virtualColorant)
{
  RegionInfo *regionInfo;

  /* Only need region maps for process colorants and any virtual spot colorants.
     The regionInfos are used to avoid compositing for overprinting. */

  regionInfo = mm_alloc(mm_pool_temp, sizeof(RegionInfo),
                        MM_ALLOC_CLASS_REGION);
  if ( regionInfo == NULL )
    return error_handler(VMERROR);

  regionInfo->ci = ci;
  regionInfo->processColorant = processColorant;
  regionInfo->virtualColorant = virtualColorant;
  regionInfo->currentObject = FALSE;
  regionInfo->currentObjectWhite = FALSE;
  regionInfo->bitGrid = NULL;
  regionInfo->next = *head;

  if ( !region_map_create(page, &regionInfo->bitGrid) ) {
    mm_free(mm_pool_temp, regionInfo, sizeof(RegionInfo));
    return FALSE;
  }

  *head = regionInfo;

  return TRUE;
}

/**
 * Free the list of region infos which are used for tracking the background for
 * overprint region marking.
 */
static void region_info_destroy(RegionInfo **regionInfo)
{
  while ( *regionInfo != NULL ) {
    RegionInfo *tmp = *regionInfo;

    *regionInfo = (*regionInfo)->next;

    bitGridDestroy(&tmp->bitGrid);
    mm_free(mm_pool_temp, tmp, sizeof(RegionInfo));
  }
}

/**
 * Create a region info for each process and virtual colorant.  These are used
 * for tracking the background to avoid compositing for colorimetric overprints.
 * Spot colorants that are being output do not need compositing for overprinting
 * and are ignored.
 */
static Bool region_info_for_overprinting(DL_STATE *page, GUCR_RASTERSTYLE *rs,
                                         RegionInfo **regionInfo)
{
  GUCR_CHANNEL *hf;

  for ( hf = gucr_framesStart(rs); gucr_framesMore(hf); gucr_framesNext(&hf) ) {
    GUCR_COLORANT *hc;
    for ( hc = gucr_colorantsStart(hf);
          gucr_colorantsMore(hc, GUCR_INCLUDING_PIXEL_INTERLEAVED);
          gucr_colorantsNext(&hc) ) {
      /* Ignore spot colorants that are being output, because compositing is not
         required to overprint them. */
      const GUCR_COLORANT_INFO *colorantInfo;
      if ( gucr_colorantDescription(hc, &colorantInfo) &&
           (colorantInfo->colorantType == COLORANTTYPE_PROCESS ||
            !guc_equivalentRealColorantIndex(rs, colorantInfo->colorantIndex, NULL)) ) {
        if ( !region_info_create(page, regionInfo, colorantInfo->colorantIndex,
                                 colorantInfo->colorantType == COLORANTTYPE_PROCESS,
                                 colorantInfo->colorantType != COLORANTTYPE_PROCESS) )
          return FALSE;
      }
    }
  }
  return TRUE;
}

/**
 * State passed in through the dl_forall callback.
 */
typedef struct {
  RegionInfo     *regionInfo;     /**< For overprint marking. */
  Bool            markOverprints; /**< Set if the object may composite for overprinting. */
} RegionMarkingData;

/**
 * Update the region map for overprinting.  The background is tracked
 * per-colorant because compositing isn't required for an overprinted object
 * painting on an untouched background.
 */
static void mark_region_map_for_overprints(DL_STATE *page,
                                           RegionMarkingData *data,
                                           LISTOBJECT *lobj, dbbox_t *bounds)
{
  Bool overprint = ((lobj->spflags & RENDER_KNOCKOUT) == 0);
  Bool overprintSimplify = preconvert_overprint_simplify(lobj);
  Bool patternedObject = ((lobj->spflags & RENDER_PATTERN) != 0);
  Bool processPresent = FALSE, virtualPresent = FALSE;
  Bool virtualColorantsAllWhite = TRUE;
  RegionInfo *regionInfo;
  dl_color_t dlc;
  dlc_tint_t tint;
  COLORVALUE cv;

  dlc_from_dl_weak(lobj->p_ncolor, &dlc);

  tint = dlc_check_black_white(&dlc);
  switch (tint) {
  default:
    HQFAIL("Unexpected dl color tint value");
  case DLC_TINT_BLACK:
    cv = 0;
    break;
  case DLC_TINT_WHITE:
  case DLC_TINT_OTHER:
    cv = COLORVALUE_MAX;
    break;
  }

  /* Determine various characteristics of the dl color for use in deciding
     how best to deal with this object. */
  for ( regionInfo = data->regionInfo; regionInfo != NULL;
        regionInfo = regionInfo->next ) {
    COLORANTINDEX ci = regionInfo->ci;

    if ( ci != COLORANTINDEX_NONE &&
         (tint != DLC_TINT_OTHER ||
          (dlc_set_indexed_colorant(& dlc, ci) &&
           dlc_get_indexed_colorant(& dlc, ci, & cv))) ) {
      regionInfo->currentObject = TRUE;
      regionInfo->currentObjectWhite = cv == COLORVALUE_ONE;

      if ( regionInfo->processColorant )
        processPresent = TRUE;
      else {
        virtualPresent = TRUE;
        if ( cv != COLORVALUE_ONE )
          virtualColorantsAllWhite = FALSE;
      }
    } else {
      regionInfo->currentObject = FALSE;
      regionInfo->currentObjectWhite = TRUE;
    }
  }

  /* Only interested in process or virtual colorants; ignore objects with just
     spot colorants that are being output. */
  if ( !processPresent && !virtualPresent )
    return;

  for ( regionInfo = data->regionInfo; regionInfo != NULL;
        regionInfo = regionInfo->next ) {
    if ( overprint && data->markOverprints ) {
      Bool compositeIntersection = FALSE;

      /* An object with all white virtual colorants probably comes from a failed
         recombine of knockouts.  If the background in this colorant is still
         white, then the knockout can be ignored (if overprintSimplify is on),
         otherwise it must be composited. */
      if ( overprintSimplify && !processPresent && virtualColorantsAllWhite ) {
        if ( regionInfo->virtualColorant && regionInfo->currentObject )
          compositeIntersection = TRUE;
      }
      /* Color converting virtual colorants or color converting a subset of
         process colorants with an intercept introduces process colorants.  In
         these cases we need to composite where the introduced process colorant
         interacts with the background.  Compositing is also required when an
         object with a virtual colorant overlaps another object with a different
         virtual colorant.

         Check if this colorant is missing from the object, and determine if
         color converting the other colorants of the object means compositing is
         required to handle the interaction with the background correctly.

         For patterned objects we can't say if the colorant is present or not,
         because the patterned object contains the union of the colorants of the
         pattern DL.  In this case err on the side of caution and say the
         colorant might not be present in one of the pattern DL objects. */
      else if ( !regionInfo->currentObject || patternedObject )
        compositeIntersection = TRUE;

      /* Mark the region map for compositing where the bounding box of the
         current object intersects with this colorant's background. */
      if ( compositeIntersection )
        bitGridSetIntersectionMapped(page->regionMap, regionInfo->bitGrid,
                                     bounds, TRUE);
    }

    /* Update the background tracking for this colorant. */
    if ( !regionInfo->currentObjectWhite )
      bitGridSetBoxMapped(regionInfo->bitGrid, bounds, TRUE);
  }
}

/**
 * Search for alpha in a softmask
 */
static Bool find_softmaskimage_alpha(LISTOBJECT *lobj, IMAGEOBJECT **imageobj,
                                     IM_STORE **ims)
{
  IMAGEOBJECT *l_imageobj ;
  IM_STORE *l_ims ;

  if ( lobj->opcode != RENDER_image )
    return FALSE ;

  l_imageobj = lobj->dldata.image ;

  if ( (l_imageobj->flags & IM_FLAG_COMPOSITE_ALPHA_CHANNEL) == 0 )
    return FALSE ;

  if ( !l_imageobj->ime )
    return FALSE ;

  l_ims = im_expand_ims_alpha(l_imageobj->ime) ;
  if ( !l_ims )
    return FALSE ;

  *imageobj = l_imageobj ;
  *ims = l_ims ;
  return TRUE ;
}

/**
 * Now find all the image blocks that intersect this region.  If all the
 * intersecting blocks are 100% transparent or opaque then don't set the
 * region map.  Admittably this is a brute force method for finding
 * intersecting blocks but the slow uniform test is only actually done
 * once per block.
 */
static void mark_softmask_for_region(DL_STATE *page, RegionMarkingData *data,
                                     dbbox_t *region,
                                     LISTOBJECT *lobj, IMAGEOBJECT *imageobj,
                                     IM_STORE *ims, COLORVALUE optimise_color)
{
  enum { STATE_UNKNOWN, STATE_FULLYOPAQUE,
         STATE_FULLYTRANSPARENT, STATE_MIXED };
  int32 state = STATE_UNKNOWN ;
  int32 i = 0 ;
  Bool more ;

  do {
    ibbox_t ibbox ;
    dbbox_t dbbox ;
    Bool uniform ;
    uint16 color ;

    more = im_store_uniformbox_iterator(ims, &i, &ibbox, &uniform, &color) ;

    /* Convert the block pixel indices into a device coord bbox. */
    if ( image_dbbox_covering_ibbox(imageobj, &ibbox, &dbbox) ) {
      /* Intersect the softmask block with the current region. */
      bbox_intersection(&dbbox, region, &dbbox) ;

      if ( !bbox_is_empty(&dbbox) ) {
        int32 new_state = STATE_MIXED ;

        if ( uniform && color == optimise_color ) {
          if ( color == COLORVALUE_ZERO )
            new_state = STATE_FULLYTRANSPARENT ;
          else if ( color == COLORVALUE_ONE )
            new_state = STATE_FULLYOPAQUE ;
        }

        if ( state == STATE_UNKNOWN )
          state = new_state ;

        if ( new_state == STATE_MIXED || new_state != state ) {
          state = STATE_MIXED ;
          bitGridSetBoxMapped(page->regionMap, &dbbox, TRUE) ;
          more = FALSE ; /* that's it for this region */
        }
      }
    }
  } while ( more ) ;

  /* All the blocks for this region were opaque, so the object
     isn't transparent everywhere. */
  if ( state == STATE_FULLYOPAQUE ) {
    lobj->marker &= ~MARKER_OMNITRANSPARENT ;

    /* Still need to call mark_region_map in case compositing is required for
       overprinting. */
    if ( data->regionInfo != NULL )
      mark_region_map_for_overprints(page, data, lobj, region);
  }
}

/**
 * Choose whether to allow direct rendering of completely transparent blocks or
 * completely opaque blocks.  Cannot do both together in one region set without
 * image block rendering.
 */
static COLORVALUE softmask_optimise_color(IM_STORE *ims)
{
  int count_opaque = 0, count_transparent = 0, i = 0;
  Bool more ;

  do {
    ibbox_t ibbox ;
    Bool uniform ;
    uint16 color ;

    more = im_store_uniformbox_iterator(ims, &i, &ibbox, &uniform, &color) ;

    if ( uniform ) {
      if ( color == COLORVALUE_ZERO )
        ++count_transparent ;
      else if ( color == COLORVALUE_ONE )
        ++count_opaque ;
    }
  } while ( more ) ;

  return count_transparent >= count_opaque ? COLORVALUE_ZERO : COLORVALUE_ONE ;
}

/**
 * Find an alpha channel from a softmask image.  The alpha channel must
 * be the only source of transparency for the object - otherwise we'll
 * composite the whole object.  If these conditions are met we set the
 * region map on a per-image-block basis often resulting in much less
 * compositing.
 */
static Bool try_marking_per_region(DL_STATE* page, RegionMarkingData *data,
                                   Group *group, LISTOBJECT *lobj,
                                   dbbox_t *bounds)
{
  IMAGEOBJECT *imageobj ;
  IM_STORE *ims ;
  COLORVALUE optimise_color ;
  Size2d size = bitGridCellSize(page->regionMap) ;
  dbbox_t region ;

  /* Give up if the object is transparent all over. */
  if ( (lobj->marker & MARKER_OMNITRANSPARENT) != 0 )
    return FALSE ;

  /* For now indicate the object is transparent all over; if opaque
     regions are found the flag will be cleared. */
  lobj->marker |= MARKER_OMNITRANSPARENT ;

  /* This test must be after MARKER_OMNITRANSPARENT has been set. */
#ifdef DEBUG_BUILD
  if ( (backdrop_render_debug & BR_DISABLE_SOFTMASK_PER_BLOCK) != 0 )
    return FALSE ;
#endif

  /* Give up if some containing group is a knockout group - we'll still
     need to composite the completely transparent areas. */
  if ( groupGetAttrs(group)->knockoutDescendant )
    return FALSE ;

  if ( !find_softmaskimage_alpha(lobj, &imageobj, &ims) )
    return FALSE ;

  /* Choose whether to optimise completely transparent blocks or completely
     opaque blocks (can't do both without image block rendering). */
  optimise_color = softmask_optimise_color(ims) ;

  /* Iterate over all of the regions covered by this object. */

  for ( region.y1 = (bounds->y1 / size.height) * size.height,
          region.y2 = region.y1 + size.height - 1 ;
        region.y1 <= bounds->y2 ;
        region.y1 += size.height, region.y2 += size.height ) {

    for ( region.x1 = (bounds->x1 / size.width) * size.width,
            region.x2 = region.x1 + size.width - 1 ;
          region.x1 <= bounds->x2 ;
          region.x1 = region.x2 + 1, region.x2 += size.width ) {

      if ( bitGridGetBoxMapped(page->regionMap, &region) != BGAllSet )
        mark_softmask_for_region(page, data, &region, lobj,
                                 imageobj, ims, optimise_color) ;
    }
  }

  return TRUE ;
}

/**
 * Determine if the object is guaranteed to be composited completely, otherwise
 * it may be direct rendered.  This function may return false-negatives because
 * objects in pattern dls are not straightforward to test.  This function should
 * only be called after dlregion_mark.
 */
Bool lobj_fullycompositing(LISTOBJECT *lobj, Group *group, HDL *hdl)
{
  BitGrid *regionMap = groupPage(group)->regionMap;
  Bool insidepattern = groupInsidePattern(group);

  return ( (lobj->marker & MARKER_OMNITRANSPARENT) != 0 ||
           (insidepattern && hdlTransparent(hdl)) ||
           dl_is_none(lobj->p_ncolor) ||
           (regionMap != NULL &&
            !insidepattern && /* bbox not appropriate if inside pattern */
            bitGridGetBoxMapped(regionMap, &lobj->bbox) == BGAllSet) );
}

/**
 * Determine if the object may be compositing, for transparency or overprinting.
 * This function may return false-positives because it doesn't use the region
 * map, or the per-colorant background region maps for overprinting, but it does
 * mean it can be called at any time.
 */
Bool lobj_maybecompositing(LISTOBJECT *lobj, Group *group,
                           Bool maxblts_supported)
{
  dl_color_t dlc ;

  dlc_from_dl_weak(lobj->p_ncolor, &dlc) ;

  /* Can't do compositing without a group. */
  if ( group == NULL )
    return FALSE;

  if ( (lobj->marker & MARKER_TRANSPARENT) != 0 ||
       groupMustComposite(group) ||
       /* For eHVD, when the plugin doesn't know about max-blitting. */
       (!maxblts_supported && dlc_doing_maxblt_overprints(&dlc)) ||
       /* OverprintPreview composites overprints; if OP is off then we do
          device-dependent overprinting, possibly using max-blits. */
       ((lobj->spflags & RENDER_KNOCKOUT) == 0 &&
        gsc_getOverprintPreview(gstateptr->colorInfo)) )
    return TRUE;

  /** \todo If we're doing color management in the back-end, and the object has
      a subset of process colorants or spot colorants that will be converted to
      process, then the object will require compositing.  Also any subset of
      process will require compositing if any blend space transformation changes
      the process space. */

  return FALSE;
}

/**
 * The elements of Groups and HDLs are marked, but not the Groups/HDLs
 * themselves.  A Group itself can apply transparency which affects all the
 * elements and this is handled by hdlMustComposite() elsewhere. Also
 * don't bother marking the erase.
 */
static Bool requires_marking(LISTOBJECT *lobj)
{
  switch ( lobj->opcode ) {
  case RENDER_erase:
  case RENDER_hdl:
  case RENDER_group:
    return FALSE;
  default:
    return TRUE;
  }
}

/**
 * PCL overrides the usual reasons for marking regions for backdrop rendering
 * (transparency, overprinting etc), and instead marks regions based on source,
 * texture, destination and ROP.  Note, pclUpdateRegionMap needs to be called
 * even if regionMap is already set.
 */
static Bool dlregion_pcl_callback(DL_FORALL_INFO *info)
{
  RegionMarkingData *data = info->data;

  if ( requires_marking(info->lobj) &&
       info->lobj->objectstate->pclAttrib ) {
    HQASSERT(info->lobj == dlref_lobj(info->dlrange.current.dlref),
             "lobj and dlref's lobj not equal");
    pclUpdateRegionMap(info->page,
                       info->dlrange.current.dlref, data->regionInfo->bitGrid,
                       info->page->regionMap);
  }
  return TRUE;
}

/**
 * The normal region marking callback to turn on compositing for regions
 * containing transparency and colorimetric overprints.
 */
static Bool dlregion_callback(DL_FORALL_INFO *info)
{
  RegionMarkingData *data = info->data;
  DL_STATE *page = info->page;
  Group *group = hdlEnclosingGroup(info->hdl);
  LISTOBJECT *lobj = info->lobj;
  HDL *patHdl = patternHdl(lobj->objectstate->patternstate);

  HQASSERT((lobj->spflags & RENDER_RECOMBINE) == 0,
           "Do not expect objects with recombine pseudo colorants");

  /* If changing page groups, update region infos if overprint marking. */
  if ( lobj->opcode == RENDER_group &&
       groupGetUsage(lobj->dldata.group) == GroupPage ) {
    /* Free any region infos from previous page group. */
    region_info_destroy(&data->regionInfo);

    /* Check if there are any overprinted objects within this page group. */
    if ( hdlOverprint(info->hdl) ) {
      /* Create region infos for tracking the background for overprints.
         This should be the virtual device rs. */
      GUCR_RASTERSTYLE *virtualRS = groupInputRasterStyle(lobj->dldata.group);
      if ( !region_info_for_overprinting(page, virtualRS, &data->regionInfo) )
        return FALSE;
    }
  }

  /* Only DL objects inside a group can be composited. Watermarks and traps are
     added straight into the HDL base. */
  if ( group == NULL || !requires_marking(lobj) ||
       bitGridGetBoxMapped(page->regionMap, &lobj->bbox) == BGAllSet ) {
    if ( lobj->opcode == RENDER_image )
      /* Preconversion reserve not needed. */
      im_store_release_reserves(lobj->dldata.image->ims);
    return TRUE;
  }

  /* Mark overprints for compositing providing the containing group's blend
     space is not device space.  If compositing for overprints is wanted for all
     groups then OverprintPreview should be enabled. */
  data->markOverprints = (data->regionInfo != NULL &&
                          guc_backdropRasterStyle(groupInputRasterStyle(group)));

  if ( /* Check if the enclosing Group applies transparency. */
       groupMustComposite(group) ||
       /* Composite if the object is transparent everywhere. */
       ((lobj->marker & MARKER_TRANSPARENT) != 0 &&
        (lobj->marker & MARKER_OMNITRANSPARENT) != 0) ||
       /* Composite if the object had a recombine-intercepted object on the
          pattern DL. */
       (patHdl != NULL && hdlRecombined(patHdl)) )
    /* Composite the whole object. */
    bitGridSetBoxMapped(page->regionMap, &lobj->bbox, TRUE);
  else if ( (lobj->marker & MARKER_TRANSPARENT) != 0 ) {
    /* The object might not be transparent all over (e.g. a softmask image).  In
       this case we may only need to set a subset of regions. */
    if ( !try_marking_per_region(page, data, group, lobj, &lobj->bbox) ) {
      /* That failed - composite the whole object. */
      bitGridSetBoxMapped(page->regionMap, &lobj->bbox, TRUE);
      if ( lobj->opcode == RENDER_image )
        /* Preconversion reserve not needed. */
        im_store_release_reserves(lobj->dldata.image->ims);
    }
  } else if ( data->regionInfo != NULL )
    /* May need to composite the object owing to colorimetric overprinting. */
    mark_region_map_for_overprints(page, data, lobj, &lobj->bbox);
  return TRUE;
}

/**
 * Create a region map and mark it according to the DL.  If LCM is enabled then
 * overprinted process colorants may need compositing for correct results.
 */
Bool dlregion_mark(DL_STATE *page)
{
  Bool result = FALSE;
  RegionMarkingData data = {0};
  DL_FORALL_INFO info = {0};

  HQASSERT(page->regionMap == NULL, "Region map already exists");
  HQASSERT(page->region_width > 0 && page->region_height > 0,
           "Region dimensions not set");
  if ( !region_map_create(page, &page->regionMap) )
    return FALSE;
#define return USE_goto_cleanup

  /* Do not mark the region map for pattern DL objects, only patterned objects.
     The pattern DL objects are defined in pattern space and could also be
     tiled. */
  info.page = page;
  info.inflags = DL_FORALL_USEMARKER|DL_FORALL_GROUP;
  /** \todo Vignette detection fails to unify KO flags through the vignette
      elements and therefore we need to mark elements individually. Don't need
      to do this for shfills, and once vignette detection is removed we can also
      remove this hack (also remove include "vndetect.h"). */
  if ( vd_detect() )
    info.inflags |= DL_FORALL_SHFILL;
  info.hdl = dlPageHDL(page);
  info.data = &data;

#if defined( DEBUG_BUILD )
  /* Decide whether to force the whole page to be composited. */
  if ( (backdrop_render_debug & BR_DEBUG_ALL_TO_BACKDROP) != 0 ) {
    dbbox_t pageFrame;
    bbox_store(&pageFrame, 0, 0, page->page_w - 1, page->page_h - 1);
    bitGridSetBoxMapped(page->regionMap, &pageFrame, TRUE);
  } else
#endif
  if ( pclGstateIsEnabled() ) {
    /* PCL uses a single region info to track the background. */
    if ( !region_info_create(page, &data.regionInfo, COLORANTINDEX_UNKNOWN,
                             TRUE, FALSE) )
      goto cleanup;

    /* Do region marking according to PCL rules. */
    if ( !dl_forall(&info, dlregion_pcl_callback) )
      goto cleanup;
  } else {
    /* Do region marking for transparency and overprinting. */
    if ( !dl_forall(&info, dlregion_callback) )
      goto cleanup;
  }

#ifdef METRICS_BUILD
  {
    dl_metrics_t *dlmetrics = dl_metrics();
    Size2d size = bitGridSize(page->regionMap);
    uint32 x, y;

    for ( y = 0; y < size.height; ++y ) {
      for ( x = 0; x < size.width; ++x ) {
        ++dlmetrics->regions.total;
        if ( bitGridGet(page->regionMap, x, y) )
          ++dlmetrics->regions.backdropRendered;
        else
          ++dlmetrics->regions.directRendered;
      }
    }
  }
#endif

  result = TRUE;
 cleanup:
  region_info_destroy(&data.regionInfo);
  /* Don't need regionMap if error or all clear. */
  if ( page->regionMap != NULL &&
       (!result || bitGridGetAll(page->regionMap) == BGAllClear) )
    bitGridDestroy(&page->regionMap);
#undef return
  return result;
}

/**
 * DL first region function for use with region iterator
 */
static Bool first_region(DL_STATE *page, surface_handle_t handle,
                         CompositeContext *context,
                         dbbox_t *region, int32 currentbackdrops,
                         int32 regionTypes, DLRegionIterator *iterator,
                         dbbox_t *bounds, Bool *backdropRender)
{
  BGMultiState state;
  int32 regionWidth, regionHeight ;

  state = page->regionMap != NULL
    ? bitGridGetBoxMapped(page->regionMap, region) : BGAllClear;

  switch (state) {
  case BGAllClear:
    /* Every region in the band is clear */
    *bounds = *region ;
    *backdropRender = FALSE;
    return FALSE; /* no more regions */
  default:
    HQFAIL("Unrecognised bitGrid state");
    /*@fallthrough@*/
  case BGAllSet:
    /* Every region in the band is set, but may still need to break band
       up into regions to avoid requiring excessive amounts of memory */
    if ( (regionTypes & RENDER_REGIONS_BACKDROP) == 0 ) {
      /* If we're not being asked to backdrop render, and all of the regions
         are set, we can bail out quickly. The caller will not render
         any of these regions. */
      *bounds = *region ;
      *backdropRender = TRUE ;
      return FALSE ;
    }
    /*@fallthrough@*/
  case BGMixed:
    /* Got a mixed band, consider region by region. Set the initial iterator
       position to the block including the top-left corner of the region,
       and call the iterator recursively to extend the region as far as
       possible. */
    regionWidth = page->region_width;
    regionHeight = page->region_height;

    *iterator = (region->y1 / regionHeight) *
      ((page->page_w + regionWidth - 1) / regionWidth) +
      (region->x1 / regionWidth) ;

    HQASSERT(*iterator != DL_REGION_ITERATOR_INIT,
             "Region iterator still initial value after rounding start") ;

    return dlregion_iterator(page, handle, context,
                             region, currentbackdrops, regionTypes,
                             iterator, bounds, backdropRender);
  }
}

/**
 * DL region iterator function
 */
Bool dlregion_iterator(DL_STATE *page, surface_handle_t handle,
                       CompositeContext *context,
                       dbbox_t *originalRegion, int32 currentbackdrops,
                       int32 regionTypes, DLRegionIterator *iterator,
                       dbbox_t *bounds, Bool *backdropRender)
{
  int32 regionWidth, regionHeight;
  int32 index, regionsPerBand, nRegions, maxRegions ;
  Bool state;
  dbbox_t region, nextBounds;
  const transparency_surface_t *transurf ;

  HQASSERT(originalRegion, "No original region to iterate over") ;
  HQASSERT(iterator, "No region iterator pointer") ;
  HQASSERT(bounds, "No output region bounds for iteration") ;
  HQASSERT(bounds != originalRegion,
           "Input and output region bounds cannot be same") ;
  HQASSERT(backdropRender, "No backdrop rendering flag location") ;

  transurf = surface_find_transparency(page->surfaces) ;
  HQASSERT(transurf, "No transparency surface in DL region iterator") ;

  /* Clip the passed region to the page. */
  region = *originalRegion;

  bbox_intersection_coordinates(&region, 0, 0, page->page_w-1, page->page_h-1);

  HQASSERT(!bbox_is_empty(&region),
           "Cannot iterate over empty region") ;

  index = *iterator;

  /* First time around, see if the whole band has the same state */
  if (index == DL_REGION_ITERATOR_INIT)
    return first_region(page, handle, context, &region, currentbackdrops,
                        regionTypes, iterator, bounds, backdropRender);

  regionWidth = page->region_width;
  regionHeight = page->region_height;
  regionsPerBand = (page->page_w + regionWidth - 1) / regionWidth ;

  /* Repeat the process of collecting a set of blocks until we get a set
     that matches the region types we want to return, or we run out of
     blocks. */
  do {
    /* Initialise bounding box to return and clip to the region boundaries. */
    bounds->x1 = (index % regionsPerBand) * regionWidth ;
    bounds->x2 = bounds->x1 + regionWidth - 1 ;
    bounds->y1 = (index / regionsPerBand) * regionHeight ;
    bounds->y2 = bounds->y1 + regionHeight - 1 ;
    bbox_intersection(bounds, &region, bounds) ;

    /* Region box from index must intersect the original region box */
    HQASSERT(bbox_intersects(bounds, &region),
             "Iterator region does not intersect iterated region") ;

    state = bitGridGetMapped(page->regionMap, bounds->x1, bounds->y1);

    HQASSERT(bitGridGetMapped(page->regionMap, bounds->x2,
                              bounds->y2) == state,
             "Region corners do not have same map status") ;

    maxRegions = MAXINT32 ;
    nRegions = 0 ;
    if ( !state || (regionTypes & RENDER_REGIONS_BACKDROP) == 0 ||
         currentbackdrops == 0 ||
         (*transurf->region_request)(handle, page->backdropShared,
                                     context, currentbackdrops,
                                     bounds) )
      ++nRegions ;
    HQASSERT(nRegions == 1, "Must be able to do at least one region");

    /* Extend region horizontally, until we either find a region in a different
       state, we reach the limit of regions we can do at once, or we reach
       the edge of the bbox. */
    nextBounds.x1 = bounds->x1 ;
    nextBounds.x2 = bounds->x2 + regionWidth ;
    if ( nextBounds.x2 > region.x2 )
      nextBounds.x2 = region.x2 ;
    nextBounds.y1 = bounds->y1 ;
    nextBounds.y2 = bounds->y2 ;

#if defined(DEBUG_BUILD)
    if ( (backdrop_render_debug & BR_DEBUG_RENDER_INDIVIDUALLY) == 0 )
#endif
      while ( bounds->x2 < region.x2 &&
              nRegions < maxRegions &&
              bitGridGetMapped(page->regionMap, bounds->x2 + 1,
                               bounds->y1) == state &&
              (!state || (regionTypes & RENDER_REGIONS_BACKDROP) == 0 ||
               currentbackdrops == 0 ||
               (*transurf->region_request)(handle, page->backdropShared,
                                           context, currentbackdrops,
                                           &nextBounds)) ) {
        bounds->x2 = nextBounds.x2 ;
        nextBounds.x2 += regionWidth ;
        if ( nextBounds.x2 > region.x2 )
          nextBounds.x2 = region.x2 ;
        ++nRegions ;
        ++index ;
      }

    /* Extend region vertically. We can only try this if we cover the full bbox
       width, otherwise we wouldn't know where to set the iterator on the next
       pass. */
#if defined(DEBUG_BUILD)
    if ( (backdrop_render_debug & BR_DEBUG_RENDER_INDIVIDUALLY) == 0 )
#endif
      if ( bounds->x1 <= region.x1 && bounds->x2 >= region.x2 ) {
        int32 regionsPerRow = nRegions ;
        BGMultiState rowstate = state ? BGAllSet : BGAllClear ;

        nextBounds.x1 = bounds->x1 ;
        nextBounds.x2 = bounds->x2 ;
        nextBounds.y1 = bounds->y1 ;
        nextBounds.y2 = bounds->y2 + regionHeight ;
        if ( nextBounds.y2 > region.y2 )
          nextBounds.y2 = region.y2 ;

        while ( bounds->y2 < region.y2 &&
                nRegions + regionsPerRow < maxRegions &&
                bitGridGetBoxMapped(page->regionMap, &nextBounds) == rowstate &&
                (!state || (regionTypes & RENDER_REGIONS_BACKDROP) == 0 ||
                 currentbackdrops == 0 ||
                 (*transurf->region_request)(handle, page->backdropShared,
                                             context, currentbackdrops,
                                             &nextBounds)) ) {
          bounds->y2 = nextBounds.y2 ;
          nRegions += regionsPerRow ;
          index += regionsPerBand ;

          nextBounds.y2 += regionHeight ;
          if ( nextBounds.y2 > region.y2 )
            nextBounds.y2 = region.y2 ;
        }
      }

    /* index now points at the last region in the box we'll use. Increment it
       by one, being careful to wrap around if we're at the right hand edge, so
       it points to the next region we want to consider. */
    if ( bounds->x2 >= region.x2 ) {
      if ( bounds->y2 >= region.y2 ) {
        index = DL_REGION_ITERATOR_INIT ; /* Done with this iteration */
        /* No more blocks, so return whatever we had and let the caller deal
           with it if it's the wrong type. */
        break ;
      } else {
        /* Set index to row below left bottom corner. Make sure we use
           region's left coordinate because there may have been other regions
           to the left of this one on the same line. */
        index = ((bounds->y2 / regionHeight) * regionsPerBand +
                 (region.x1 / regionWidth)) + regionsPerBand ;
      }
    } else { /* There are more regions to consider at the right of bounds */
      ++index ;
    }
  } while ( state /* loop if backdrop or direct block that we don't want. */
            ? (regionTypes & RENDER_REGIONS_BACKDROP) == 0
            : (regionTypes & RENDER_REGIONS_DIRECT) == 0 ) ;

#if defined( ASSERT_BUILD )
  {
    /* Verify the group of regions is in fact consistent */
    BGMultiState bgState = bitGridGetBoxMapped(page->regionMap, bounds);
    HQASSERT(bgState == BGAllSet || bgState == BGAllClear,
             "bounds bbox should not have a mixed bitGrid state");
  }
#endif

  *iterator = index;
  *backdropRender = state;

  /* Are there more regions to consider? */
  return (index != DL_REGION_ITERATOR_INIT);
}

#if defined(DEBUG_BUILD)
void dlregion_clip(DL_STATE *page, dbbox_t *bbox, int32 left, int32 right)
{
  int32 regionWidth = page->region_width ;

  HQASSERT(regionWidth != 0, "Region size has not been set") ;

  if ( bbox->x1 / regionWidth < left )
    bbox->x1 = left * regionWidth ;

  if ( (bbox->x2 + regionWidth - 1) / regionWidth > right )
    bbox->x2 = right * regionWidth + regionWidth - 1 ;
}
#endif

/* Log stripped */
