/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:hdl.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of the Hierarchical Display List.
 *
 * The HDL is stored as a number of linked lists, one for each band a one
 * describing the z-order of the entire page.
 *
 * The situation is this: we have a master linked list. We have a set of
 * sub linked lists, each of which contains a sub-set of the objects
 * in the master list. The order of objects in each sub list will match
 * the order in the master list, even though some objects will be absent
 * in the sub list.
 *
 * For example, we may have a master list of:
 *   A, B, C, D, E, F
 * valid sub lists include:
 *   A, C, F
 *   B, D, E
 * but not:
 *   B, D, C
 *
 * What's happening is that some objects in the master list are being
 * deleted, and we need to remove each occurence of these deleted
 * objects in each sub list. Because the each sub-list is only
 * singly-linked, this process would be very slow without some
 * optimisation, and so what we do is hijack the 'bandTails' list and
 * use it as a temporary set of head pointers - each entry points to
 * the current object in a traversal of each sub-list. Once the cleanup
 * process is complete, the band tails list will actually be correct -
 * each entry will point to the last entry on the band.
 *
 * It works like this; the caller scans over the master list deciding
 * which objects to keep and which to delete, calling this function for
 * each object specifying that it should be either deleted or preserved.
 * For each call, we do this for each sub-list:
 *
 * \code
 * if (object is first in list) {
 *   if (delete object) {
 *     The target object MUST (because of the way preservations are handled)
 *     be the first in this band; we'll need to set the head pointer (in
 *     'bands' as well as 'bandTails') to the next entry.
 *   }
 *   else (preserve object) {
 *     Do nothing. From now on the target object will always be
 *     next in the list (if present at all), regardless of how many
 *     deletes/preserves we do in the future.
 *   }
 * }
 * else {
 *   if (object is next in list) {
 *     if (delete object) {
 *       Set the next item to be the item after the object to be deleted.
 *       The head does not change.
 *     }
 *     else (preserve object) {
 *       Set the head to the next object - i.e. the object to be preserved
 *       becomes the head.
 *     }
 *   }
 *   else {
 *     Do nothing; the object is not in this band.
 *   }
 * }
 * \endcode
 *
 * Note that we keep the most recently preserved object at the start of
 * the bandTails list (rather than just skipping over it, which may seem
 * easier) so that we can remove any deleted objects after it from the list;
 * remember the list is only singly-linked.
 *
 * It's a shame to use such obscure code, but this really does need to
 * be efficient; this process only occurs when a partial paint occurs,
 * and at that point we are sure to have a large number of objects on
 * the display list.
 *
 * The process of removal/preservation is split into two distinct
 * functions (for efficiency and because some additional information
 * is required from the remove method) - it is presented as a unified
 * function above for clarity.
 */

#include "core.h"
#include "hdlPrivate.h"
#include "bitblts.h"
#include "bitblth.h"
#include "blttables.h"
#include "clipblts.h"
#include "dl_cleanup.h"
#include "dl_free.h"
#include "dl_store.h"
#include "dl_foral.h"
#include "graphics.h"
#include "gschcms.h"
#include "gu_chan.h"
#include "hqmemcpy.h"
#include "hqmemset.h"
#include "jobmetrics.h"
#include "objnamer.h"
#include "often.h"
#include "plotops.h"
#include "routedev.h"
#include "swerrors.h"
#include "vndetect.h"
#include "vnobj.h"
#include "render.h"
#include "pattern.h"
#include "patternrender.h"
#include "shadex.h"
#include "dl_purge.h"
#include "dl_ref.h"
#include "timing.h"
#include "surface.h"
#include "spotlist.h"

#define HDL_NAME "Hierarchical display list"

/**
 * ID of current HDL. Preincrement before assigning to HDL so that
 * HDL_ID_INVALID is never used.
 */
static uint32 hdlLastId = HDL_ID_INVALID;

/**
 * State of DL banding
 * \todo BMJ 29-Jul-08 : Just starting development of this feature.
 */
enum {
  BANDED_NOW,
  BANDED_LATER,
  BANDED_NEVER
};

/**
 * Structure used to store private HDL data
 */
struct HDL {
  DlSSEntry storeEntry;    /**< Must be first member for DlSSEntry/HDL casting to work */
  uint32 id;               /**< Unique id */
  int32 purpose;           /**< What is this HDL used for */
  DL_STATE *page;          /**< DL page of this instance */
  int32 device;            /**< HDL device type, e.g. banded/null/pattern */
  int32 deviceid;          /**< device ID */
  Bool open;               /**< Is the HDL open or closed? */
  Bool transparent;        /**< Does the HDL include transparent objects? */
  Bool recombined;         /**< Does it contain recombine-intercepted objs? */
  Bool overprint;          /**< Does the HDL include overprint objects? */
  Bool patterned;          /**< Does the HDL contain patterned objects? */
  Bool selfIntersect;      /**< Use self-intersection rendering? */
  int32 banding;           /**< What sort of banding is applied to this HDL */
  int purge_level;         /**< Level to which HDL has been purged */

  dbbox_t bbox;            /**< Bounding box union of all objects in HDL */
  Range usedBands;         /**< Range of bands objects appear in */
  HDL *parent;             /**< parent HDL */
  Group *group;            /**< Group this is the DL of, or NULL */
  uint32 offsetInPageDL;   /**< band offset w.r.t. parent */
  uint32 numBands;         /**< Number of bands, excluding z-order band */
  DLREF **bands;           /**< Array of band DLs */
  DLREF **bandTails;       /**< Array of band DL insertion points */
  Bool snapshotValid;      /**< Is the snapshot valid? */
  DLREF **snapshot;        /**< Snapshot array of band DL insertion points */

  struct {
    uint32 size;           /**< Size of memory chunk in bytes */
    uint8 *storage;        /**< base address of memory */
  } storage;               /**< Chunk of memory used by HDL */

  dl_color_t dlcMerged;   /**< Merged colour of all objects and children */

  OBJECT_NAME_MEMBER       /**< ID handle for this struct */
};

DlSSEntry *hdlStoreEntry(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  return &hdl->storeEntry;
}

uint32 hdlId(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  return hdl->id;
}

/**
 * Returns this HDL's offset into the page DL.
 */
uint32 hdlOffsetIntoPageDL(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  return hdl->offsetInPageDL;
}

/**
 * Return the range of bands used in the HDL.
 */
Range hdlUsedBands(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  return hdl->usedBands;
}

/**
 * Returns this HDL's extent on its parent HDL.
 *
 * Note, extentOnParent is deliberately calculated dynamically, and not stored
 * in hdl, because its value changes when the hdl is closed and then again when
 * the parent is closed.  When the parent closes it is too much effort to find
 * all the child hdls and update their extentOnParent, and recurse on the
 * children's children etc.
 */
Range hdlExtentOnParent(HDL *hdl)
{
  int origin;

  VERIFY_OBJECT(hdl, HDL_NAME);
  origin = hdl->parent == NULL
    ? 0 : hdl->offsetInPageDL - hdl->parent->offsetInPageDL;
  HQASSERT(origin >= 0, "HDL cannot use more bands than parent");
  return rangeNew(CAST_SIGNED_TO_UINT32(origin), hdl->numBands);
}

/**
 * Returns this HDL's merged color
 */
dl_color_t *hdlColor(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  return &hdl->dlcMerged;
}

/**
 * Update the merge color with lobj's color.
 */
Bool hdlMergeColorUpdate(HDL *hdl, LISTOBJECT *lobj)
{
  dl_color_t dlcObject;

  VERIFY_OBJECT(hdl, HDL_NAME);

  dlc_from_lobj_weak(lobj, &dlcObject);

  if ( lobj->opcode == RENDER_gouraud ) {
    /* Gourauds may have max-blit overprint flags which must also be merged.  A
       shfill is required to have unified colorants and overprints (unification
       happens subsequently in the shfill code).  When max-blits have been
       removed, shfills can use the normal dlc_merge_with_action call. */
    if ( !dlc_merge_shfill_with_action(hdl->page->dlc_context,
                                       &hdl->dlcMerged, &dlcObject,
                                       COMMON_COLORANT_AVERAGE) )
      return FALSE;
  } else {
    if ( !dlc_merge_with_action(hdl->page->dlc_context,
                                &hdl->dlcMerged, &dlcObject,
                                COMMON_COLORANT_AVERAGE) )
      return FALSE;
  }

  return TRUE;
}

/**
 * Returns the parent of the HDL.  The base HDL has no parent because
 * it is the bottom of the HDL stack.
 */
HDL *hdlParent(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  return hdl->parent;
}

/** Finds if the hdl contains a single non-self-intersecting object
    (possibly wrapped in layers of dls), and returns it. */
LISTOBJECT *hdlSingleObject(HDL *hdl)
{
  DLREF *dl;

  VERIFY_OBJECT(hdl, HDL_NAME);

  dl = hdlOrderList(hdl);
  if ( dl == NULL || dlref_next(dl) != NULL )
    return NULL;
  if ( dlref_lobj(dl)->opcode == RENDER_hdl )
    return hdlSingleObject(dlref_lobj(dl)->dldata.hdl);
  if ( dlref_lobj(dl)->opcode == RENDER_vignette ) /* can self-intersect */
    return NULL;
  return dlref_lobj(dl);
}

/**
 * Returns the base of the HDL stack.
 */
HDL *hdlBase(HDL *hdl)
{
  for (;;) {
    VERIFY_OBJECT(hdl, HDL_NAME);
    if ( hdl->parent == NULL )
      break;
    hdl = hdl->parent;
  }
  return hdl;
}

/**
 * Returns the target HDL for the gstate and DL state. The target HDL may not be
 * the top of the HDL stack if a setgstate is performed with a previous
 * pagedevice.
 */
HDL *hdlTarget(HDL *target, GSTATE *gs)
{
  VERIFY_OBJECT(target, HDL_NAME);
  HQASSERT(gs, "No gstate for HDL target");

  while ( target != NULL ) {
    if ( (target->device == thegsDeviceType(*gs) ||
          (target->device == DEVICE_BAND &&
           dev_is_bandtype(thegsDeviceType(*gs)))) &&
         target->deviceid == thegsDeviceBandId(*gs) )
      break;

    target = target->parent;
  }

  return target;
}

/**
 * Returns true if HDL contains transparency.
 */
Bool hdlTransparent(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  return hdl->transparent;
}

/** Record that HDL contains transparency. */
void hdlSetTransparent(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);

  if ( !hdl->transparent ) {
    hdl->transparent = TRUE;

    /* Propagate transparent flag up the HDL hierarchy. */
    if ( hdl->parent )
      hdlSetTransparent(hdl->parent);
  }
}

/** Indicates HDL contains recombine-intercepted objects. */
Bool hdlRecombined(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  return hdl->recombined;
}

/** Record that HDL contains recombine-intercepted objects. */
static void hdlSetRecombined(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);

  if ( !hdl->recombined ) {
    hdl->recombined = TRUE;

    /* Propagate recombined flag up the HDL hierarchy. */
    if ( hdl->parent )
      hdlSetRecombined(hdl->parent);
  }
}

/**
 * Returns true if HDL contains objects that overprint
 */
Bool hdlOverprint(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  return hdl->overprint;
}

/**
 * Record that HDL has overprinted objects.  This is used in marking overprinted
 * objects for compositing.
 */
void hdlSetOverprint(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);

  if ( !hdl->overprint ) {
    hdl->overprint = TRUE;

    /* Propagate overprint flag up the HDL hierarchy. */
    if ( hdl->parent )
      hdlSetOverprint(hdl->parent);
  }
}

/** Indicates HDL contains patterned objects. */
Bool hdlPatterned(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  return hdl->patterned;
}

/** Record that HDL contains patterned objects. */
static void hdlSetPatterned(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);

  if ( !hdl->patterned ) {
    hdl->patterned = TRUE;

    /* Propagate patterned flag up the HDL hierarchy. */
    if ( hdl->parent )
      hdlSetPatterned(hdl->parent);
  }
}

/** Set HDL to use self-intersection rendering. */
Bool hdlSetSelfIntersect(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  if ( !dl_reserve_band(hdl->page, RESERVED_BAND_SELF_INTERSECTING) )
    return FALSE ;
  hdl->selfIntersect = TRUE;
  return TRUE ;
}

int hdlPurgeLevel(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  return hdl->purge_level;
}

void hdlSetPurgeLevel(HDL *hdl, int level)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  hdl->purge_level = level;
}

/**
 * HDL purpose query.
 */
int32 hdlPurpose(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  return hdl->purpose;
}

/**
 * Band list query.
 */
DLREF **hdlBands(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  return hdl->bands;
}

/**
 * Band-tail list (current band insertion points) query.
 */
DLREF **hdlBandTails(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  return hdl->bandTails;
}

/**
 * Order list query.
 */
DLREF *hdlOrderList(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  return hdl->bands[hdl->numBands];
}

/**
 * Order list query for last object.
 */
DLREF *hdlOrderListLast(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  return hdl->bandTails[hdl->numBands];
}

/**
 * Is this HDL empty? Erases are ignored (a DL containing nothing but erases
 * is considered empty).
 *
 * We only need to look on the order list band, as anything added will be in
 * that band. The first object on the list is normally an erase, but we check
 * just to be sure.
 */
Bool hdlIsEmpty(HDL *hdl)
{
  DLREF *orderBand;

  VERIFY_OBJECT(hdl, HDL_NAME);

  orderBand = hdlOrderList(hdl);
  if ( orderBand == NULL )
    return TRUE;

  if ( dlref_next(orderBand) != NULL ||
       dlref_lobj(orderBand)->opcode != RENDER_erase )
    return FALSE;

  /* This HDL must be empty (or contain only erases). */
  return TRUE;
}

/**
 * Return the bbox of the given HDL
 */
void hdlBBox(HDL *hdl, dbbox_t *bbox)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  HQASSERT(bbox != NULL, "bbox is null");
  *bbox = hdl->bbox;
}

/** Set the group that this HDL is the DL of */
void hdlSetGroup(HDL *hdl, Group *group)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  hdl->group = group;
}

/**
 * If the HDL is a component of a group then return the group, otherwise return
 * NULL.  Note in general hdl.c does not know about groups, but maintaining an
 * opaque reference makes life considerably easier.
 */
Group *hdlGroup(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  return hdl->group;
}

/** Return the closest enclosing group for any hdl. */
Group *hdlEnclosingGroup(HDL *hdl)
{
  do {
    VERIFY_OBJECT(hdl, HDL_NAME);
    if ( hdl->group != NULL )
      return hdl->group;
    hdl = hdl->parent;
  } while ( hdl != NULL );
  return NULL;
}

/** Returns the nearest banded ancestor for an unbanded HDL. */
static HDL *hdlBandedAncestor(HDL *hdl)
{
  do {
    VERIFY_OBJECT(hdl, HDL_NAME);
    if ( hdl->numBands != 0 )
      return hdl;
    hdl = hdl->parent;
  } while ( hdl != NULL );
  return NULL;
}

/** Returns the extent on the page, works even for unbanded HDL. */
Range hdlExtentOnPage(HDL *hdl)
{
  HDL *banded;

  VERIFY_OBJECT(hdl, HDL_NAME);

  /* Assumes that unbanded groups cover their parent (but allows for completely
     unbanded operation). If the banded hdl is still open, used bands isn't
     final and therefore return the full band range. */
  banded = hdlBandedAncestor(hdl);
  if ( banded != NULL ) {
    return rangeNew(hdlOffsetIntoPageDL(banded),
                    banded->open ? banded->numBands : hdlUsedBands(banded).length);
  } else
    return rangeNew(0, 1);
}

/**
 * Allocate a set of links and bands to used until the HDL is closed (and its
 * true extent is known).
 */
static Bool hdlAllocateTempStorage(HDL *hdl)
{
  uint32 i, bandLists = 2;
  uint32 numAllocBands = hdl->numBands + 1; /* include z-order band */

  /* If this HDL is for a page (or imposed page), then we must allow for band
     snapshots, which requires an additional band list. */
  /** \todo MJ Until every page starts off in a page group, additionally
      create a snapshot in the base HDL. */
  if ( hdl->purpose == HDL_PAGE || hdl->purpose == HDL_BASE )
    bandLists = 3;

  /* Memory allocation is done in a single block to avoid nasty error
     handling. We use (length + 1) to include storage for the order band. */
  hdl->storage.size = sizeof(DLREF *) * bandLists * numAllocBands;
  hdl->storage.storage = dl_alloc(hdl->page->dlpools, hdl->storage.size,
                                  MM_ALLOC_CLASS_HDL);
  if ( hdl->storage.storage == NULL )
    return error_handler(VMERROR);

  /* Assign the two band pointer lists. */
  hdl->bands = (DLREF **)hdl->storage.storage;
  hdl->bandTails = hdl->bands + numAllocBands;

  /* Assign the snapshot list if allocated. */
  hdl->snapshot = NULL;
  /** \todo MJ Until every page starts off in a page group, additionally
      create a snapshot in the hdl HDL. */
  if ( hdl->purpose == HDL_PAGE || hdl->purpose == HDL_BASE )
    hdl->snapshot = hdl->bandTails + numAllocBands;

  for ( i = 0; i < numAllocBands; ++i )
    hdl->bands[i] = hdl->bandTails[i] = NULL;

  return TRUE;
}

/**
 * Calculate the range of bands the given bbox covers in the DL.
 * If the bounds lay outside of the page/parent HDL, FALSE is returned,
 * otherwise TRUE is returned. If the HDL is not banded, TRUE will always
 * be returned, and the range will be set to empty
 */
static Bool hdlCalculateBandRange(HDL *hdl, dbbox_t *bbox, Range *range)
{
  int32 totalBands, lowBand, highBand;

  HQASSERT(range != NULL, "'range' cannot be NULL.");
  HQASSERT(bbox->y2 >= bbox->y1, "coordinates out of order.");

  /* If there is only a Z-order band in the HDL, then everything falls into
     that band. */
  if ( hdl->numBands == 0 ) {
    *range = rangeNew(0, 0);
    return TRUE;
  }

  /* Convert range into band indices.
     highBand adjustment is required for separation imposition.
     It extends objects into the adjacent band, so that when we render
     with a Y offset, we know the band we pick will contain the objects to
     be displayed on the shifted display list. */
  lowBand = bbox->y1 / hdl->page->sizefactdisplayband;
  highBand = (bbox->y2 + guc_getMaxOffsetIntoBand(hdl->page->hr))
             / hdl->page->sizefactdisplayband;

  /* Offset the band range into this HDL. */
  lowBand -= hdl->offsetInPageDL;
  highBand -= hdl->offsetInPageDL;

  /* Get the total number of bands on this HDL. */
  totalBands = hdl->numBands;

  /* Check for the range being completely off this HDL. */
  if ( highBand < 0 || lowBand >= totalBands )
    return FALSE;

  /* Clip the range. */
  if ( lowBand < 0 )
    lowBand = 0;
  if ( highBand >= totalBands )
    highBand = totalBands - 1;

  /* Commit results */
  range->origin = lowBand;
  range->length = (highBand - lowBand) + 1;

  HQASSERT(range->length > 0, "range should not be empty.");

  return TRUE;
}

/** Return band range on the page corresponding to a bbox */
void hdlBandRangeOnPage(DL_STATE *page, dbbox_t *bbox, Range *range)
{
  HDL *hdl = page->currentHdl;

  VERIFY_OBJECT(hdl, HDL_NAME);

  if ( !hdlCalculateBandRange(hdl, bbox, range) )
    *range = rangeNew(0, 0);
  range->origin += hdlOffsetIntoPageDL(hdl);
}

/**
 * Open a new HDL.  The HDL will be inserted into the hierarchy and made the
 * current HDL if the call succeeds.
 */
Bool hdlOpen(DL_STATE *page, Bool banded, int32 purpose, HDL **newHdl)
{
  HDL_LIST *hlist;
  uint32 numBands;
  void *stored;
  HDL *hdl, *parent;

  HQASSERT(page != NULL, "page cannot be NULL");
  HQASSERT(newHdl != NULL, "newHdl is null");
  *newHdl = NULL;

  /* Ensure all objects on the vignette chain are added to the correct HDL */
  if ( !flush_vignette(VD_Default))
    return FALSE;

  if ( !finishaddchardisplay(page, 1) )
    return error_handler(VMERROR);

  parent = page->currentHdl;

  if ( banded ) {
    /* If the HDL is for a banded sub-dl, inherit the parent HDL's default
       banding. */
    if ( parent != NULL ) {
      numBands = parent->numBands;

      /* If try to put a banded HDL inside a Z-order HDL, the inner HDL will be
         converted to a Z-order HDL as well (i.e., the most restrictive set of
         bands applies). */
      if ( numBands == 0 )
        banded = FALSE;
    } else {
      numBands = page->sizefactdisplaylist;
    }
  } else {
    /* Non-banded HDLs have a single Z-order chain. The length is set
       to zero because the Z-order band is normally in addition to the
       other bands. */
    numBands = 0;
  }

  /* Allocate memory for HDL and HDL_LIST. */
  hdl = dl_alloc(page->dlpools, sizeof(HDL) + sizeof(HDL_LIST),
                 MM_ALLOC_CLASS_HDL);
  if ( hdl == NULL )
    return error_handler(VMERROR);
  /* Clear structure (NULL any pointers). */
  HqMemZero(hdl, sizeof(HDL));
  hlist = (HDL_LIST *)(hdl + 1);

  /* Init members. */
  hdl->id = ++hdlLastId;
  hdl->purpose = purpose;
  hdl->page = page;
  hdl->transparent = FALSE;
  hdl->recombined = FALSE;
  hdl->overprint = FALSE;
  hdl->patterned = FALSE;
  hdl->selfIntersect = FALSE;
  hdl->usedBands = rangeNew(0, 0); /* indicates no usage */
  bbox_clear(&hdl->bbox);
  hdl->device = CURRENT_DEVICE();
  hdl->deviceid = thegsDeviceBandId(*gstateptr);
  hdl->parent = page->currentHdl;
  hdl->offsetInPageDL = parent != NULL ? parent->offsetInPageDL : 0;
  hdl->numBands = numBands;
  dlc_get_none(page->dlc_context, &hdl->dlcMerged);
  hdl->snapshotValid = FALSE;
  if ( banded )
    hdl->banding = dlRandomAccess() ? BANDED_NOW : BANDED_LATER;
  else
    hdl->banding = BANDED_NEVER;
  hdl->purge_level = 0;

  NAME_OBJECT(hdl, HDL_NAME);

  /* Allocate and initialise bands. */
  if ( !hdlAllocateTempStorage(hdl) ) {
    hdlDestroy(&hdl);
    return FALSE;
  }

  /* Add to the list of all HDLs for this DL (hdlRemoveFromList assumes
     descending id order) */
  hlist->hdl = hdl;
  hlist->next = page->all_hdls;
  page->all_hdls = hlist;

  /* Insert the HDL into the DL store. HDLs are the only type of object we
     insert into the DL store while they are still under construction; this is
     because their construction is spread out over a long period, and target IDs
     are correlated to HDL ids to avoid having DL pointers in the gstate. After
     this point, we have to be careful about cleanup to avoid leaving dangling
     pointers in the DL store. */
  stored = dlSSInsert(page->stores.hdl, &hdl->storeEntry, FALSE);
  if ( stored == NULL ) {
    hdlDestroy(&hdl);
    return FALSE;
  }
  HQASSERT(stored == hdl, "HDL insertion into DL store failed");

#ifdef METRICS_BUILD
  dl_metrics()->store.hdlCount++;
#endif

  probe_begin(SW_TRACE_DL_HDL, (intptr_t)hdl) ;

  /* We are now a fully-fledged HDL. Install ourselves as the current and
     target HDL (target because we're guaranteed to have the gstate's device
     type and ID). */
  hdl->open = TRUE;
  page->targetHdl = page->currentHdl = hdl;
  *newHdl = hdl;
  return TRUE;
}

/**
 * Once a HDL has been closed, we know its true extent, and thus may be able
 * to reduce the amount of memory allocated for its band lists. This will
 * generally be the case for HDLs created unbounded.
 */
static Bool hdlAllocateFinalStorage(HDL *hdl)
{
  uint8 *finalStorage;
  uint32 finalSize;
  Range range;
  DLREF **finalBands, **finalTails;
  uint32 i;

  VERIFY_OBJECT(hdl, HDL_NAME);

  /* Only trim the band lists if necessary. */
  if ( hdl->usedBands.length == hdl->numBands )
    return TRUE;

  /* Allocate final band pointers. */
  range = hdl->usedBands;

  /* Use (length + 1) as we'll need an extra band to hold the order list.
     Two lists are allocated - one for the band pointers, and one for the
     band tail pointers. The band tail pointers are required even though this
     HDL is closed; someone may still add something (perhaps during
     low-memory handling), and they are required by the partial paint
     mechanism. */
  finalSize = (sizeof(DLREF *) * (range.length + 1)) * 2;
  finalStorage = dl_alloc(hdl->page->dlpools, finalSize, MM_ALLOC_CLASS_HDL);

  if ( finalStorage == NULL )
    return error_handler(VMERROR);

  /* Assign final band pointers (not forgetting to include order list). */
  finalBands = (DLREF **)finalStorage;
  finalTails = finalBands + (range.length + 1);

  /* Copy temp bands into final storage. */
  for ( i = 0; i < range.length; ++i ) {
    finalBands[i] = hdl->bands[range.origin + i];
    finalTails[i] = hdl->bandTails[range.origin + i];
  }

  /* Copy order band. */
  finalBands[i] = hdl->bands[hdl->numBands];
  finalTails[i] = hdl->bandTails[hdl->numBands];

  /* The extent on the parent has been trimmed down to 'range' within our
     original band list. */
  hdl->offsetInPageDL += range.origin;
  hdl->numBands = range.length;

  /* Our master band list has been trimmed to reflect the usedBands range,
     thus the origin of usedBands is now zero. */
  hdl->usedBands.origin = 0;

  /* Commit the final bands to our band list. */
  hdl->bands = finalBands;
  hdl->bandTails = finalTails;

  /* Free temp storage and replace with final storage. */
  dl_free(hdl->page->dlpools, hdl->storage.storage, hdl->storage.size,
          MM_ALLOC_CLASS_HDL);
  hdl->storage.size = finalSize;
  hdl->storage.storage = finalStorage;
  return TRUE;
}

/**
 * Common action required when a HDL is terminated, either by being closed or
 * destroyed while active.
 */
static void hdlCommonTermination(HDL *hdl)
{
  DL_STATE *page;

  VERIFY_OBJECT(hdl, HDL_NAME);
  HQASSERT(hdl->purpose != HDL_BASE, "Base HDL can never be terminated.");

  page = hdl->page;

  if ( page->currentHdl == hdl ) {
    HQASSERT(hdl->parent != NULL, "Must have parent to update currentHdl");

    /* Reset the HDL target. We didn't store the previous target on open, so
       we'll assume that we can reset it using the gstate. */
    page->targetHdl = hdlTarget(hdl->parent, gstateptr);

    /* Reinstate the parent HDL as current on the page DL. */
    page->currentHdl = hdl->parent;
  }
  hdl->open = FALSE;
}

/**
 * Close a HDL - returns TRUE if successful. This causes the band list to be
 * trimmed of excess and the parent HDL is reinstalled as the current HDL in
 * the page. The base HDL (which has no parent) cannot be closed - this is
 * asserted. The called must test if the HDL is empty, and destroy it if
 * desired.
 */
Bool hdlClose(HDL **hdlPointer, Bool success)
{
  HDL *hdl;

  HQASSERT(hdlPointer != NULL, "hdlPointer cannot be NULL");
  hdl = *hdlPointer;
  VERIFY_OBJECT(hdl, HDL_NAME);
  HQASSERT(hdl->open, "Closing already closed HDL");
  HQASSERT(hdl->parent != NULL, "Closing root HDL");

  /* Ensure all objects on the vignette chain are added to the correct HDL */
  if ( !flush_vignette(VD_Default) )
    return FALSE;

  /* This also restores the parent HDL to be the current HDL. */
  hdlCommonTermination(hdl);

  probe_end(SW_TRACE_DL_HDL, (intptr_t)hdl) ;

  if ( !success ) {
    /* If this HDL was aborted, destroy it. Aborting returns false so that
       parents can abort */
    hdlDestroy(hdlPointer);
    return FALSE;
  }

  /* Finish off the HDL by trimming the band lists */
  if ( !hdlAllocateFinalStorage(hdl) ) {
    hdlDestroy(hdlPointer);
    return FALSE;
  }

  return TRUE;
}

/**
 * Prepare for a session of listobject removal (the process of removing a
 * list object from all the bands it appears in).
 */
static void hdlPrepareForObjectRemoval(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);

  /* Copy the band head pointer list into the band-tail pointer list.  This is
     necessary because we are going to use the band-tail list to track the
     removal of listobjects. Since the band tail list is held only for
     optimization, it can easily be regenerated once the object removal is
     complete. */
  HqMemCpy(&hdl->bandTails[hdl->usedBands.origin],
           &hdl->bands[hdl->usedBands.origin],
           hdl->usedBands.length * sizeof(DLREF *));
}

/**
 * Return the number of times the target object was found in our band lists.
 */
static uint32 hdlRemoveObjectInBands(HDL *hdl, LISTOBJECT *lobj)
{
  /* We've hijacked the band tails list for the cleanup process. */
  DLREF **bandCleanupList = hdl->bandTails;
  uint32 first, last, limit;

  VERIFY_OBJECT(hdl, HDL_NAME);
  HQASSERT(lobj != NULL, "lobj cannot be NULL");

  /* Find the first band in which the object appears */
  limit = rangeTop(hdl->usedBands);
  for (first = hdl->usedBands.origin; first < limit; ++first) {
    DLREF *head = bandCleanupList[first];
    if ( head != NULL ) {
      if ( dlref_lobj(head) == lobj )
        break; /* Object is first in list */
      head = dlref_next(head);
      if ( head != NULL && dlref_lobj(head) == lobj )
        break; /* Object follows existing head */
    }
  }

  /* Remove the object from all bands until it isn't found */
  for (last = first; last < limit; ++last) {
    DLREF *head = bandCleanupList[last];

    if ( head == NULL )
      break; /* Must be past the last band in which this object appears */

    if ( dlref_lobj(head) == lobj ) /* Remove first object in list */
      hdl->bands[last] = bandCleanupList[last] = dlref_next(head);
    else {
      DLREF *next = dlref_next(head);

      if ( next == NULL || dlref_lobj(next) != lobj )
        break; /* Not found, so we're past the last occurrence in the band */

      /* Remove object just past current head object */
      dlref_setnext(head, dlref_next(next));
    }
  }

#if defined(ASSERT_BUILD)
  /* Check that the object does not appear in the remaining bands. */
  while ( limit > last ) {
    DLREF *head = bandCleanupList[--limit];

    HQASSERT(head == NULL ||
             (dlref_lobj(head) != lobj &&
              (dlref_next(head) == NULL || dlref_lobj(dlref_next(head)) != lobj)),
             "Object appears in non-contiguous band");
  }
#endif

  /* The difference between the last and first indices is the number of bands
     in which the object appeared. */
  return last - first;
}

/**
 * Destroy hdl band storage.
 */
static void hdlDestroyBandStorage(HDL *hdl)
{
  DLRANGE dlrange;

  /* Separate code for the case that DL is purged to disk */
  /** \todo BMJ 07-Nov-08: Unify the disk and non-disk cases */
  if ( dlpurge_inuse() ) {
    DLREF *dl = hdlOrderList(hdl);

    while ( dl != NULL ) {
      DLREF *next = dl ? dlref_next(dl) : NULL;
      LISTOBJECT *lobj;

      if ( dl->inMemory ) {
        lobj = dlref_lobj(dl);
        if ( lobj )
          free_dl_object(lobj, hdl->page);
      }
      free_n_dlrefs(dl, 1, hdl->page->dlpools);
      dl = next;
    }
    return;
  }

  /* Destroy all objects in the order list, deallocating the DLREFs we allocated
     for each object. */
  hdlPrepareForObjectRemoval(hdl);

  hdlDlrange(hdl, &dlrange);
  for ( dlrange_start(&dlrange); !dlrange_done(&dlrange); ) {
    LISTOBJECT *lobj = dlrange_lobj(&dlrange);
    DLREF *deleteMe = dlrange.current.dlref;
    uint32 linkCount;

    /* Destruction can take a while. */
    SwOftenUnsafe();

    /* We don't actually need to remove the object from our bands (as the bands
       are about to be destroyed) - we call removal function purely to determine
       how many DLREFs were allocated for this object.

       Vignette merging transfers the LISTOBJECTS from one HDL to another
       leaving a husk behind with all the objects nulled out. In this case,
       freeing the husk just involves freeing the dlrefs, and the linkount will
       always be zero (i.e. just an orderlist). */
    if ( lobj != NULL ) {
      linkCount = hdlRemoveObjectInBands(hdl, lobj);
      /* Destroy the listobject. */
      free_dl_object(lobj, hdl->page);
    } else
      linkCount = 0;

    /* Skip over this link before we delete it. */
    dlrange_next(&dlrange);

    /* Free the DLREFs we allocated for this object. We use (linkCount + 1)
       because there was a link allocated for the order list. We know that the
       link in the order list was the first in the allocated block, and thus
       that is the address we free. */
    free_n_dlrefs(deleteMe, linkCount + 1, hdl->page->dlpools);
  }
  dl_free(hdl->page->dlpools, hdl->storage.storage, hdl->storage.size,
          MM_ALLOC_CLASS_HDL);
}

/**
 * Remove \a hdl from all_hdls and null any parent ptrs still referring to \a
 * hdl (which can happen when a child of \a hdl was created for an unused
 * pattern).
 */
static void hdlRemoveFromList(HDL *hdl, DL_STATE *page)
{
  HDL_LIST *curr, **prev ;

  VERIFY_OBJECT(hdl, HDL_NAME);

  /* Any children requiring their parent ptr to be cleared will precede 'hdl',
     because the list is in descending id order. This avoids having to iterate
     over the whole list. */
  for ( prev = &page->all_hdls ; (curr = *prev) != NULL ; prev = &curr->next ) {
    VERIFY_OBJECT(curr->hdl, HDL_NAME);
    if ( hdl == curr->hdl ) {
      *prev = curr->next ;
      return ;
    } else if ( hdl == curr->hdl->parent ) {
      curr->hdl->parent = NULL ;
    }
  }
  HQFAIL("Removing HDL not in list");
}

/**
 * Destructor. The contents of the HDL are also destroyed.
 */
void hdlDestroy(HDL **hdlPointer)
{
  HDL *hdl;

  HQASSERT(hdlPointer != NULL, "hdlPointer cannot be NULL");
  hdl = *hdlPointer;

  if ( hdl == NULL )
    return;

  VERIFY_OBJECT(hdl, HDL_NAME);

  hdlRemoveFromList(hdl, hdl->page);

  /* Destruction may happen part way through construction of a group or other
     HDL subclass. Make sure the HDL is removed from the stack in this case. */
  hdlCommonTermination(hdl);

  /* Invalidate the clients pointer. */
  *hdlPointer = NULL; /* Must be done after hdlCommonTermination in case
                         hdlPointer refers to page->currentHdl */

  /* Destroy band storage. */
  if ( hdl->storage.storage != NULL )
    hdlDestroyBandStorage(hdl);

  dlc_release(hdl->page->dlc_context, &hdl->dlcMerged);
  (void)dlSSRemove(hdl->page->stores.hdl, &hdl->storeEntry);
  UNNAME_OBJECT(hdl);
  dl_free(hdl->page->dlpools, hdl, sizeof(HDL) + sizeof(HDL_LIST),
          MM_ALLOC_CLASS_HDL);
}

/**
 * Add the passed DLREF to this HDL at the indicated index.
 * The link object may be singular, or may be the head of a linked list of
 * DLREFs; the band tail will be set accordingly.
 * It is asserted that the object is being added within our used range,
 * or to the order list.
 */
static void hdlAddToBand(HDL *hdl, DLREF *dlobj, uint32 index)
{
  DLREF *endLink;

  VERIFY_OBJECT(hdl, HDL_NAME);
  HQASSERT(dlobj != NULL, "DL object cannot be NULL");
  HQASSERT(dlref_lobj(dlobj) != NULL, "DL does not contain an object");
  HQASSERT(rangeContains(hdl->usedBands, index) || index == hdl->numBands,
           "Tried adding object without first updating the used bands range");

  if ( dlref_next(dlobj) == NULL ) /* This is a single addition. */
    endLink = dlobj;
  else {
    /* This is a graft - find the end of the list */
    endLink = dlref_next(dlobj);
    while ( dlref_next(endLink) != NULL )
      endLink = dlref_next(endLink);
  }

  if ( hdl->bandTails[index] == NULL ) /* 1st object added to this band. */
    hdl->bands[index] = dlobj;
  else {
    /* Add to the tail of the list. In some circumstances (e.g. recombine) each
       band tail may not actually be the tail of the list, thus the new link is
       actually inserted into the list rather than blindly appended. */
    dlref_setnext(endLink, dlref_next(hdl->bandTails[index]));
    dlref_setnext(hdl->bandTails[index], dlobj);
  }

  /* Set new band tail. */
  hdl->bandTails[index] = endLink;
}

/**
 * Add the given DL objects to the main z-order list and any DL bands
 * it touches, dependent on the current banding mode.
 */
static Bool AddToBands(HDL *hdl, Range range, LISTOBJECT *lobj)
{
  uint32 band, n = hdl->banding == BANDED_NOW ? range.length + 1 : 1;
  DLREF *dlobj;

  if ( (dlobj = alloc_n_dlrefs(n, hdl->page)) == NULL )
    return error_handler(VMERROR);

  /* Add to order list first, and then to each band */
  for ( band = hdl->numBands; n > 0; --n ) {
    DLREF *next;

    dlref_assign(dlobj, lobj);
    next = dlref_next(dlobj);
    dlref_setnext(dlobj, NULL);
    hdlAddToBand(hdl, dlobj, band);
    dlobj = next;
    if ( band == hdl->numBands )
      band = range.origin;
    else
      band++;
  }
  return TRUE;
}

Bool hdlPrepareBanding(DL_STATE *page)
{
  HDL_LIST *hlist;

  /* Iterate over all active HDLs on the page, and then for each element within
     each HDL... */
  for ( hlist = page->all_hdls; hlist != NULL; hlist = hlist->next ) {
    HDL *hdl = hlist->hdl;

    VERIFY_OBJECT(hdl, HDL_NAME);

    if ( hdl->banding == BANDED_LATER ) {
      DLRANGE dlrange;

      hdl->banding = BANDED_NOW;
      hdlDlrange(hdl, &dlrange);

      for ( dlrange_start(&dlrange); !dlrange_done(&dlrange);
            dlrange_next(&dlrange) ) {
        LISTOBJECT *lobj = dlrange_lobj(&dlrange);
        Range range;

        if ( hdlCalculateBandRange(hdl, &lobj->bbox, &range) )
          if ( !AddToBands(hdl, range, lobj) )
            return FALSE;
      }
    }
  }
  return TRUE;
}

/**
 * Add the passed object to the HDL.
 *
 * It is possible to add to a HDL after it has been closed, but generally
 * such additions will be coming from a composite object which is
 * processing the objects on its HDL, rather than from a normal addtodl()
 * calls during interpretation.
 */
Bool hdlAdd(HDL *hdl, LISTOBJECT *lobj)
{
  Range range;

  VERIFY_OBJECT(hdl, HDL_NAME);

  /* Merge the passed bounds with ours. */
  HQASSERT(!bbox_is_empty(&lobj->bbox), "bbox is empty");
  bbox_union(&hdl->bbox, &lobj->bbox, &hdl->bbox);

  if ( !hdlCalculateBandRange(hdl, &lobj->bbox, &range) )
    return TRUE; /* Fell outside band range, add is a no-op */

  HQASSERT(rangeTop(range) <= hdl->numBands, "Range exceeds numBands");

  /* Update the usedBands range. */
  hdl->usedBands = rangeUnion(hdl->usedBands, range);
  HQASSERT(rangeTop(hdl->usedBands) <= hdl->numBands, "usedBands invalid");

#ifdef METRICS_BUILD
  {
    dl_metrics_t *dlmetrics = dl_metrics();

    ++dlmetrics->opcodes[lobj->opcode] ;

    /* Note the current dl state is not valid for groups; their blend modes are
       recorded elsewhere. */
    if ( hdl->page->currentdlstate != NULL &&
         hdl->page->currentdlstate->tranAttrib != NULL &&
         lobj->opcode != RENDER_erase &&
         lobj->opcode != RENDER_void &&
         lobj->opcode != RENDER_group ) {
      updateBlendModeMetrics(hdl->page->currentdlstate->tranAttrib,
                             dlmetrics);
    }
  }
#endif

  if ( (lobj->spflags & RENDER_PATTERN) == 0 ) {
    if ( !hdlMergeColorUpdate(hdl, lobj) )
      return FALSE;
  } else { /* Patterned object. Merge the color of the pattern HDL. */
    HDL *patHdl = patternHdl(lobj->objectstate->patternstate) ;
    if ( patHdl &&
         !dlc_merge_with_action(hdl->page->dlc_context,
                                &hdl->dlcMerged, &patHdl->dlcMerged,
                                COMMON_COLORANT_AVERAGE) )
      return FALSE ;
  }

  if ( !AddToBands(hdl, range, lobj) )
    return FALSE;

  if ( (lobj->marker & MARKER_TRANSPARENT) != 0 )
    hdlSetTransparent(hdl);
  if ( (lobj->spflags & RENDER_RECOMBINE) != 0 )
    hdlSetRecombined(hdl);
  if ( (lobj->spflags & RENDER_KNOCKOUT) == 0 )
    hdlSetOverprint(hdl);
  if ( (lobj->spflags & RENDER_PATTERN) != 0 )
    hdlSetPatterned(hdl);
  hdl->purge_level = 0; /* It might be useful to purge this HDL again. */
  return TRUE;
}

/**
 * Populate the DLRANGE object with references to the start and end of dl
 * contained in the given hdl and for the given band. Create the range foing
 * from start to end, unless 'forward' is true. If requested, skip an erase
 * object (if present) at the start of the dl.
 */
static void hdlDlrangeInternal(HDL *hdl, uint32 band, DLRANGE *dlrange,
                               Bool forwards, Bool skip_erase)
{
  DLREF *start;

  dlrange_init(dlrange);
  dlrange->forwards = forwards;

  if ( (start = hdl->bands[band]) == NULL )
    start = hdl->bands[hdl->numBands];

  if ( skip_erase && start != NULL ) {
    HQASSERT(dlref_lobj(start) , "No LISTOBJECT for first object in band");
    if ( dlref_lobj(start)->opcode == RENDER_erase )
      start = dlref_next(start); /* Skip erase, it's already done */
  }
  dlrange->start.dlref = start;
}

void hdlDlrange(HDL *hdl, DLRANGE *dlrange)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  HQASSERT(dlrange != NULL, "dlrange missing");
  hdlDlrangeInternal(hdl, hdl->numBands, dlrange, TRUE, FALSE);
}

void hdlDlrangeNoErase(HDL *hdl, DLRANGE *dlrange)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  HQASSERT(dlrange != NULL, "dlrange missing");
  hdlDlrangeInternal(hdl, hdl->numBands, dlrange, TRUE, TRUE);
}

void hdlDlrangeBackwards(HDL *hdl, DLRANGE *dlrange)
{
  VERIFY_OBJECT(hdl, HDL_NAME);
  HQASSERT(dlrange != NULL, "dlrange missing");
  hdlDlrangeInternal(hdl, hdl->numBands, dlrange, FALSE, FALSE);
}


DLREF *hdlGetHead(HDL *hdl, uint32 bandi)
{
  DLREF *dl;
  uint32 hdl_bandi; /* band index relative to this HDL */

  HQASSERT(hdl != NULL, "No HDL to get bands from");

  if ( bandi < hdl->offsetInPageDL )
    return NULL;
  hdl_bandi = bandi - hdl->offsetInPageDL;
  if ( hdl_bandi >= hdl->numBands )
    return NULL;
  dl = hdl->bands[hdl->banding == BANDED_NOW ? hdl_bandi : hdl->numBands];
  return dl;
}


/**
 * Render a HDL.
 */
Bool hdlRender(HDL *hdl, render_info_t *renderInfo, TranAttrib *transparency,
               Bool intersect)
{
  uint32 band;
  const render_state_t *p_rs;
  render_info_t *p_ri = renderInfo;
  DLRANGE dlrange;
  Bool do_intersect;

  UNUSED_PARAM(TranAttrib *, transparency);

  VERIFY_OBJECT(hdl, HDL_NAME);
  HQASSERT(renderInfo != NULL, "renderInfo is null");
  HQASSERT(tranAttribIsOpaque(transparency), "cannot do transparency");
  HQASSERT(hdl->parent == NULL || !hdl->open, "Can only render closed HDL");

  p_rs = renderInfo->p_rs;
  HQASSERT(p_rs != NULL, "No render state.");

  do_intersect = (intersect || hdl->selfIntersect || p_rs->cs.fSelfIntersect) ;

  if ( p_rs->band == DL_LASTBAND )
    band = hdl->numBands;
  else {
    if ( (uint32)p_rs->band < hdl->offsetInPageDL
         || (uint32)p_rs->band >=
                              hdl->offsetInPageDL + rangeTop(hdl->usedBands) )
      return TRUE; /* nothing to do */

    band = (uint32)p_rs->band - hdl->offsetInPageDL;
  }

  hdlDlrangeInternal(hdl, band, &dlrange, !do_intersect, TRUE);

  return render_object_list_of_band(p_ri, &dlrange);
}

/**
 * Determine how many bands this HDL has covered. This is done by finding
 * the first and the last bands that are not empty. The returned range is
 * within the savedBands list.
 */
static Range hdlDetermineUsedBands(HDL *hdl)
{
  uint32 first, last;

  VERIFY_OBJECT(hdl, HDL_NAME);
  HQASSERT(hdl->bandTails != NULL,
           "hdlDetermineUsedBands cannot be called after final storage allocation.");

  last = hdl->numBands;

  /* Find last touched band */
  while ( last > 0 )
    if ( hdl->bands[--last] != NULL )
      break;

  /* Find first touched band. */
  for ( first = 0; first < last; ++first )
    if ( hdl->bands[first] != NULL )
      break;

  return rangeNew(first, last - first + 1);
}

/**
 * Maintain the given object in all bands it covers
 */
static void hdlPreserveObjectInBands(HDL *hdl, LISTOBJECT *lobj)
{
  /* We've hijacked the band tails list for the cleanup process. */
  DLREF **bandCleanupList = hdl->bandTails;
  uint32 band, limit;

  VERIFY_OBJECT(hdl, HDL_NAME);
  HQASSERT(lobj != NULL, "lobj cannot be NULL");

  /* Find the first band in which the object appears */
  limit = rangeTop(hdl->usedBands);
  for ( band = hdl->usedBands.origin; band < limit; ++band ) {
    DLREF *head = bandCleanupList[band];
    if ( head != NULL ) {
      if ( dlref_lobj(head) == lobj )
        break; /* Object is first in list */
      head = dlref_next(head);
      if ( head != NULL && dlref_lobj(head) == lobj )
        break; /* Object follows existing head */
    }
  }

  /* Find the last band in which the object appears */
  for ( ; band < limit; ++band ) {
    DLREF *head = bandCleanupList[band];

    if ( head == NULL )
      break; /* Must be past the last band in which this object appears */

    if ( dlref_lobj(head) != lobj ) { /* Move head pointer on to skipped object */
      head = dlref_next(head);

      if ( head == NULL || dlref_lobj(head) != lobj )
        break; /* Not found, so we're past the last occurrence in the band */

      /* Object we are skipping is now last in the band */
      bandCleanupList[band] = head;
    }
  }

#if defined(ASSERT_BUILD)
  /* Check that the object does not appear in the remaining bands. */
  while ( limit > band ) {
    DLREF *head = bandCleanupList[--limit];

    HQASSERT(head == NULL || dlref_lobj(head) != lobj ||
             dlref_next(head) == NULL ||
             dlref_lobj(dlref_next(head)) != lobj,
             "Object appears in non-contiguous band");
  }
#endif
}

/**
 * This function is called after a partial paint has occured; we should
 * deallocate everything that will have been drawn.
 *
 * If the entire contents of the HDL were successfully destroyed, then the HDL
 * itself is destroyed, and TRUE is returned.
 *
 * Often a HDL is used by a more complex composite object; in this case it is
 * intended that this method is being called by such an object, after it has
 * done all it needs to in this situation, and thus no longer depends on the
 * contents of this HDL.
 */
Bool hdlCleanupAfterPartialPaint(HDL **hdlPointer)
{
  DLREF *scan, *before, *lastSurvivingLink = NULL;
  HDL *hdl;

  HQASSERT(hdlPointer != NULL, "hdlPointer cannot be NULL");
  hdl = *hdlPointer;
  VERIFY_OBJECT(hdl, HDL_NAME);
  HQASSERT(hdl->open || hdl->purpose != HDL_BASE, "Base HDL is never closed");

  /* If this is an open HDL, preserve it. Otherwise delete it. We don't delete
     erase objects either, so the base HDL will not be completely removed,
     preserving the parent order of the HDL. */

  /* This is an HDL that should be deleted */
  hdlPrepareForObjectRemoval(hdl);

  /* Try to deallocate each object on the order list. */
  for ( scan = hdl->bands[hdl->numBands], before = NULL; scan; ) {
    LISTOBJECT *lobj = dlref_lobj(scan);

    SwOftenUnsafe(); /* Cleanup can take a while. */

    if ( cleanupDLObjectAfterPartialPaint(lobj, hdl->page) ) {
      /* If the item was removed successfully, remove all reference to
         it in our band lists. */
      uint32 linkCount = hdlRemoveObjectInBands(hdl, lobj);
      DLREF *next = dlref_next(scan);

      /* Unlink the item from the order list. */
      if ( before )
        dlref_setnext(before, next);
      else
        hdl->bands[hdl->numBands] = next;

      /* Because of the way DLREFs are allocated (see hdlAdd()) we know
         that the start of the memory block is the same as the address of the
         link in the order list. Free the DLREFs we allocated for this object.
         We use (linkCount + 1) because there was a link allocated for the
         order list. */
      free_n_dlrefs(scan, linkCount + 1, hdl->page->dlpools);
      scan = next;
    } else {
      /* Preserve the object in our band lists. This call is only
         required because of the optimization method used to remove
         objects from the band lists. */
      hdlPreserveObjectInBands(hdl, lobj);
      lastSurvivingLink = scan;

      /* We need to re-build the spot number list. */
      if ( !spotlist_add(hdl->page, lobj->objectstate->spotno,
                         DISPOSITION_REPRO_TYPE(lobj->disposition)) )
        return FALSE;

      /* Move onto next object in the list. */
      before = scan;
      scan = dlref_next(scan);
    }
  }

  /* If everything in the HDL has been successfully freed, then we can
     destroy the HDL itself, otherwise it must survive. */
  if ( !hdl->open && hdlOrderList(hdl) == NULL ) {
    hdlDestroy(hdlPointer);
    return TRUE;
  }

  /* The bandTails list should now be correct for all bands except the order
     list; each band tail will be either null (for empty bands) or point to the
     last object on the band. Manually update the order list band tail now. */
  hdl->bandTails[hdl->numBands] = lastSurvivingLink;

  /* The contents of this HDL have changed, so we must recalculate the
     usedBands range. */
  hdl->usedBands = hdlDetermineUsedBands(hdl);

  /* We'll leave the bounding box as is. Open HDLs should survive, including
     the base HDL, and the bounding box in this may be used to calculate
     separation placement, so it is probably sensible to preserve it. */

#if defined(ASSERT_BUILD)
  /* Assert that the cleanup worked as expected; i.e. that a band is now empty,
     or that the last object on a band is the same as that in the bandTails
     list. */
  {
    uint32 i;

    /* Use <= length to include the order list. */
    for ( i = 0; i <= hdl->numBands; ++i ) {
      /* Scan for the end of the list */
      for ( scan = hdl->bands[i]; scan != NULL && dlref_next(scan) != NULL;
            scan = dlref_next(scan) )
        EMPTY_STATEMENT();

      HQASSERT(hdl->bandTails[i] == scan,
               "End of display list does not match band tail");
    }
  }
#endif

  /* Preserve all the store entries for everything left on the DL. This is done
     only in the base HDL because the preserve recurses over the DL itself. */
  if ( hdl->purpose == HDL_BASE )
    dlSSPreserve(hdl->page->stores.hdl, &hdl->storeEntry, TRUE);

  /* This HDL was not completely removed, so return FALSE */
  return FALSE;
}

/**
 * Copy the current bandTail list into the snapshot list.
 */
void hdlTakeBandTailSnapshot(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);

  HQASSERT(hdl->snapshot != NULL, "No snapshot to take");
  if ( hdl->snapshot != NULL ) {
    hdl->snapshotValid = TRUE;

    /* Copy the band tail pointers (including the order list) into the
       snapshot array. */
    HqMemCpy(hdl->snapshot, hdl->bandTails,
             (hdl->numBands + 1) * sizeof(DLREF *));
  }
}

/**
 * Copy the current band tail snapshot into the band tail list, and then
 * invalidate the snapshot.
 */
void hdlRestoreBandTails(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);

  if ( hdl->snapshotValid ) {
    /* Copy the snapshot list (including the order list) back into the band
       tails array. Note that this should not affect the deallocation method
       used in hdlCleanupAfterPartialPaint(). */
    HqMemCpy(hdl->bandTails, hdl->snapshot,
             (hdl->numBands + 1) * sizeof(DLREF *));
  }
}

/**
 * snapshot query.
 */
DLREF **hdlBandTailSnapshot(HDL *hdl)
{
  VERIFY_OBJECT(hdl, HDL_NAME);

  HQASSERT(hdl->snapshotValid, "no snapshot available.");
  return hdl->snapshot;
}

/**
 * Remove the first object from the given HDL
 */
void hdlRemoveFirstObject(HDL *hdl)
{
  uint32 first, last, limit;
  DLREF *zorder;
  LISTOBJECT *lobj;

  VERIFY_OBJECT(hdl, HDL_NAME);

  zorder = hdlOrderList(hdl);
  HQASSERT(zorder, "No object in HDL to remove");
  hdl->bands[hdl->numBands] = dlref_next(zorder);

  lobj = dlref_lobj(zorder);
  HQASSERT(lobj != NULL, "No listobject in Z-order link");

  /* Find the first band in which the object appears */
  limit = rangeTop(hdl->usedBands);
  for ( first = hdl->usedBands.origin; first < limit; ++first ) {
    DLREF *head = hdl->bands[first];
    if ( head != NULL && dlref_lobj(head) == lobj )
      break; /* Object is first in list */
  }

  /* Remove the object from all bands until it isn't found */
  for ( last = first; last < limit; ++last ) {
    DLREF *head = hdl->bands[last];

    if ( head == NULL || dlref_lobj(head) != lobj )
      break; /* Must be past the last band in which this object appears */

    /* Remove first object in list */
    hdl->bands[last] = dlref_next(head);
  }

#if defined(ASSERT_BUILD)
  /* Check that the object does not appear in the remaining bands. */
  while ( limit > last ) {
    DLREF *head = hdl->bands[--limit];

    HQASSERT(head == NULL || dlref_lobj(head) != lobj,
             "Object appears in non-contiguous band");
  }
#endif

  free_n_dlrefs(zorder, last - first + 1, hdl->page->dlpools);
}

/**
 * Adjust the range of bands in which an object appears, keeping the
 * deallocation constraints for HDLs consistent. The ranges of bands must
 * overlap; this function is used to expand or contract the bands touched by
 * an object.
 *
 * VERY INEFFICIENT function to adjust the range of bands in which an object
 * appears, keeping the (de)allocation constraints for HDL links consistent.
 *
 * This function only exists to cope with recombine adjustment for shfills,
 * which can reduce the range of bands touched by the shfill object.
 */
Bool hdlAdjustBandRange(HDL *hdl, LISTOBJECT *lobj, Range oldbands,
                        Range newbands)
{
  DLREF *before, *zold, *znew, *dlobj, *add;
  uint32 band;

  VERIFY_OBJECT(hdl, HDL_NAME);
  HQASSERT(oldbands.origin != newbands.origin ||
           oldbands.length != newbands.length,
           "HDL adjust band called with no changes");
  HQASSERT(oldbands.origin < rangeTop(newbands) &&
           newbands.origin < rangeTop(oldbands),
           "HDL band range for adjustment does not intersect");
  HQASSERT(hdl->numBands > 0,
           "Should not be adjusting the band range on a Z-order only HDL");

  /* If we need to expand the used bands to cover the new range, then this
     function needs applying recursively on parent HDLs. We cannot do this
     because we don't know the appropriate LISTOBJECTS, so assert that new
     bands lie within the range of usedBands. We don't shrink usedBands
     either. */
  HQASSERT(newbands.origin >= hdl->usedBands.origin &&
           rangeTop(newbands) <= rangeTop(hdl->usedBands),
           "New band range is outside already used bands");

  /* Find object in existing Z-order band of HDL */
  for ( zold = hdl->bands[hdl->numBands], before = NULL;
        zold != NULL; before = zold, zold = dlref_next(zold) ) {
    if ( dlref_lobj(zold) == lobj )
      break;
  }

  HQASSERT(zold, "Object to adjust not found in HDL Z-order band");

  if ( oldbands.length != newbands.length ) {
    /* Needs a new allocation */
    if ( (znew = alloc_n_dlrefs(newbands.length + 1, hdl->page)) == NULL )
      return error_handler(VMERROR);
  } else /* Lengths are the same, so no new allocation is needed. */
    znew = zold;

  /* Re-link the Z-order band to use the new links */
  znew = dlref_next(add = znew);
  dlref_setnext(add, dlref_next(zold));
  dlref_assign(add, dlref_lobj(zold));
  if ( before )
    dlref_setnext(before, add);
  else
    hdl->bands[hdl->numBands] = add;

  /* Remove object from all bands outside new band range. */
  for ( band = oldbands.origin; band < rangeTop(oldbands); ++band ) {
    if ( band < newbands.origin || band >= rangeTop(newbands) ) {
      for ( dlobj = hdl->bands[band], before = NULL; dlobj != NULL;
            before = dlobj, dlobj = dlref_next(dlobj) ) {
        if ( dlref_lobj(dlobj) == lobj ) {
          if ( before )
            dlref_setnext(before, dlref_next(dlobj));
          else
            hdl->bands[band] = dlref_next(dlobj);
          break;
        }
      }
      HQASSERT(dlobj, "Object to remove not found in old initial band");
    }
  }

  /* Re-link bands in overlapping range using new links */
  for ( band = newbands.origin; band < rangeTop(newbands); ++band ) {
    for ( dlobj = hdl->bands[band], before = NULL; dlobj != NULL;
          before = dlobj, dlobj = dlref_next(dlobj) ) {
      if ( dlref_lobj(dlobj) == lobj ) {
        znew = dlref_next(add = znew);
        dlref_assign(add, lobj);
        dlref_setnext(add, dlref_next(dlobj));
        if ( before )
          dlref_setnext(before, add);
        else
          hdl->bands[band] = add;
        break;
      }
    }
    HQASSERT(dlobj, "Object to adjust not found in overlapping band");
  }

  if ( oldbands.length != newbands.length )
    free_n_dlrefs(zold, oldbands.length + 1, hdl->page->dlpools);

  /* For all bands (including order band ) update band tails */
  for ( band = 0; band <= hdl->numBands; ++band ) {
    dlobj = hdl->bands[band];

    if ( dlobj != NULL )
      while ( dlref_next(dlobj) != NULL )
        dlobj = dlref_next(dlobj);

    hdl->bandTails[band] = dlobj;
  }
  /* Make sure snapshot ptrs are set to valid DLREFs. Since hdlAdjustBandRange
     is called only from recombine adjustment at the end of the page, the
     snapshot values shouldn't matter. */
  if ( hdl->snapshot != NULL )
    hdlTakeBandTailSnapshot(hdl);
  return TRUE;
}

/**
 * Initialise all the C static vairaibles in the HDL module
 */
void init_C_globals_hdl(void)
{
  hdlLastId = HDL_ID_INVALID;
}

/* Log stripped */
