/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!src:backdrop.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 */

#include "core.h"
#include "display.h"
#include "backdroppriv.h"
#include "resource.h"
#include "composite.h"
#include "table.h"
#include "iterator.h"
#include "mm.h"
#include "swerrors.h"
#include "dlstate.h"
#include "group.h"
#include "pixelLabels.h"
#include "params.h"
#include "often.h"
#include "monitor.h"
#include "functns.h"
#include "debugging.h"
#include "namedef_.h"
#include "backdropinit.h"
#include "metrics.h"
#include "backdropmetrics.h"
#include "swstart.h"
#include "cvcolcvt.h"
#include "coreinit.h"
#include "blitcolors.h"
#include "spotlist.h"
#include "swtrace.h" /* SW_TRACE_INVALID */
#include "lowmem.h"
#include "taskres.h"
#include "hdl.h"

/**
 * To save a large amount of memory just for block ptrs, re-use block ptrs for
 * each band.  Need the full set of block ptrs for the page backdrop to allow
 * blocks to be retained or if doing multithreaded compositing.
 */
#define BLOCKPTR_REUSE_OPTIMISATION

#ifdef DEBUG_BUILD
#include "ripdebug.h"       /* register_ripvar */

int32 backdropDebug = 0;
#endif

#ifdef METRICS_BUILD
backdrop_metrics_t backdropMetrics;

static Bool bd_metricsUpdate(sw_metrics_group *metrics)
{
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Backdrop")) )
    return FALSE ;
  SW_METRIC_INTEGER("count", backdropMetrics.nBackdrops);
  SW_METRIC_INTEGER("blocks", backdropMetrics.nBlocks);
  SW_METRIC_INTEGER("blocks_to_disk", backdropMetrics.nBlocksToDisk);
  SW_METRIC_INTEGER("bytes_to_disk", backdropMetrics.nBytesToDisk);
  SW_METRIC_INTEGER("duplicate_colors", backdropMetrics.nDuplicateEntries);
  SW_METRIC_INTEGER("compressed_size_percent", backdropMetrics.compressedSizePercent);
  sw_metrics_close_group(&metrics); /*Backdrop*/
  return TRUE;
}

static void bd_metricsReset(int reason)
{
  backdrop_metrics_t init = { 0 };

  UNUSED_PARAM(int, reason);

  backdropMetrics = init;
}

static sw_metrics_callbacks backdropMetricsHook = {
  bd_metricsUpdate, bd_metricsReset, NULL
} ;
#endif

static void bd_init_C_globals(void)
{
#ifdef DEBUG_BUILD
  backdropDebug = 0;
#endif
#ifdef METRICS_BUILD
  bd_metricsReset(SW_METRICS_RESET_BOOT);
  sw_metrics_register(&backdropMetricsHook);
#endif
}

static low_mem_offer_t *bd_backdropPurgeSolicit(low_mem_handler_t *handler,
                                                corecontext_t *context,
                                                size_t count,
                                                memory_requirement_t *requests);

static Bool bd_backdropPurgeRelease(low_mem_handler_t *handler,
                                    corecontext_t *context,
                                    low_mem_offer_t *offer);

static low_mem_handler_t bd_backdropPurgeHandler = {
  "Backdrop",
  BD_PURGE_COST_TIER, bd_backdropPurgeSolicit, bd_backdropPurgeRelease, TRUE,
  0, FALSE
};

multi_mutex_t backdropLock;

static Bool bd_swinit(SWSTART *params)
{
  UNUSED_PARAM(SWSTART *, params);

  multi_mutex_init(&backdropLock, BACKDROP_LOCK_INDEX, TRUE /*recursive*/,
                   SW_TRACE_COMPOSITE_ACQUIRE, SW_TRACE_COMPOSITE_HOLD);

  resourcePool = NULL;

  if ( !bd_contextInit() ||
       !bd_resourceInit() ||
       !low_mem_handler_register(&bd_backdropPurgeHandler) ) {
    low_mem_handler_deregister(&bd_backdropPurgeHandler);
    bd_resourceFinish();
    bd_contextFinish();
    return FALSE ;
  }

  return TRUE ;
}

static Bool bd_swstart(SWSTART *params)
{
  UNUSED_PARAM(SWSTART *, params);

#ifdef DEBUG_BUILD
  register_ripvar(NAME_debug_bd, OINTEGER, &backdropDebug);
#endif

  return TRUE;
}

static void bd_finish(void)
{
  low_mem_handler_deregister(&bd_backdropPurgeHandler);
  bd_resourceFinish();
  bd_contextFinish();
  multi_mutex_finish(&backdropLock);
}

const mm_cost_t purgeCost = { BD_PURGE_COST_TIER, BD_PURGE_COST_VALUE };

/**
 * bd_stateAlloc/Free are used for backdrop state memory.
 */
void *bd_stateAlloc(const BackdropShared *shared, size_t bytes)
{
  return mm_alloc(shared->statePool, bytes,
                  MM_ALLOC_CLASS_BACKDROP_STATE);
}

void bd_stateFree(const BackdropShared *shared, void *addr, size_t bytes)
{
  mm_free(shared->statePool, (mm_addr_t)addr, bytes);
}

/**
 * bd_dataAlloc/Free are used for backdrop block data that
 * is purgeable in low memory.
 */
void *bd_dataAlloc(const BackdropShared *shared, size_t bytes,
                   mm_cost_t cost)
{
  return mm_alloc_cost(shared->dataPool, bytes, cost,
                       MM_ALLOC_CLASS_BACKDROP_DATA);
}

void bd_dataFree(const BackdropShared *shared, void *addr, size_t bytes)
{
  mm_free(shared->dataPool, (mm_addr_t)addr, bytes);
}

/**
 * bd_dataFreeSize returns the amount of memory currently available
 * for backdrop data.
 */
size_t bd_dataFreeSize(const BackdropShared *shared)
{
  return (mm_no_pool_size(FALSE /* include_reserve */) +
          mm_pool_free_size(shared->dataPool));
}

mm_pool_t resourcePool = NULL;

void *bd_resourceAlloc(size_t bytes)
{
  return mm_alloc(resourcePool, bytes,
                  MM_ALLOC_CLASS_BACKDROP_DATA);
}

void *bd_resourceAllocCost(size_t bytes, mm_cost_t cost)
{
  return mm_alloc_cost(resourcePool, bytes, cost,
                       MM_ALLOC_CLASS_BACKDROP_DATA);
}

void bd_resourceFree(void *addr, size_t bytes)
{
  mm_free(resourcePool, (mm_addr_t)addr, bytes);
}

mm_pool_t bd_resourcePool(const resource_source_t *source)
{
  VERIFY_OBJECT(source, RESOURCE_SOURCE_NAME);
  UNUSED_PARAM(const resource_source_t*, source);
  return resourcePool;
}

static void bd_destroyPools(BackdropShared *shared)
{
  if ( shared->dataPool != NULL )
    mm_pool_destroy(shared->dataPool);
  if ( shared->statePool != NULL )
    mm_pool_destroy(shared->statePool);
  /* resourcePool is never destroyed.
     The memory is managed by the task resource system. */
}

static Bool bd_createPools(BackdropShared *shared)
{
  HQASSERT(shared->statePool == NULL, "Backdrop state pool exists already");
  HQASSERT(shared->dataPool == NULL, "Backdrop data pool exists already");

  if ( resourcePool == NULL ) {
    if ( mm_pool_create(&resourcePool, BDDATA_POOL_TYPE,
                        (size_t)MM_SEGMENT_SIZE /* segment size */,
                        (size_t)2048 /* average size */,
                        (size_t)8 /* alignment */) != MM_SUCCESS )
      return error_handler(VMERROR);
  }

  if ( mm_pool_create(&shared->statePool, BDSTATE_POOL_TYPE,
                      (size_t)MM_SEGMENT_SIZE /* segment size */,
                      (size_t)64 /* average size */,
                      (size_t)8 /* alignment */) != MM_SUCCESS )
    return error_handler(VMERROR);

  if ( mm_pool_create(&shared->dataPool, BDDATA_POOL_TYPE,
                      (size_t)MM_SEGMENT_SIZE /* segment size */,
                      (size_t)2048 /* average size */,
                      (size_t)8 /* alignment */) != MM_SUCCESS ) {
    bd_destroyPools(shared);
    return error_handler(VMERROR);
  }

  return TRUE;
}

/**
 * BackdropShared contains all the information that is common to all backdrops.
 */
Bool bd_sharedNew(DL_STATE *page, uint32 width, uint32 height,
                  uint32 regionHeight, uint32 regionRowsPerBand,
                  Bool multiThreaded, BackdropShared **newShared)
{
  BackdropShared *shared, init = {0};

  HQASSERT(*newShared == NULL, "Backdrop shared already present");
  HQASSERT(regionHeight >= 1, "Invalid regionHeight");
  HQASSERT(regionRowsPerBand >= 1, "Invalid regionRowsPerBand");

  if ( !bd_createPools(&init) )
    return FALSE;

  shared = bd_stateAlloc(&init, sizeof(BackdropShared));
  if ( shared == NULL ) {
    bd_destroyPools(&init);
    return error_handler(VMERROR);
  }

  *shared = init; /* Now safe to just call bd_sharedFree */

  shared->page = page;
  shared->multiThreaded = multiThreaded;

  HQASSERT(width > 0 && height > 0, "Invalid backdrop dimensions");
  shared->width = width;
  shared->height = height;

  /* Don't over allocate backdrop resources to help avoid contention with other
     resources. */
  shared->nLimResources = UserParams.BackdropResourceLimit;

  /* Backdrop blocks are currently limited in height (to restrict the
     block->data allocation to 16K) and therefore multiple rows of blocks may be
     needed to cover the region height.  If multiple rows are required then the
     block height is chosen such that all blocks have the same height to allow
     recycling of memory between blocks and rows.  Potentially this may mean a
     small number of lines in the last row will be wasted. */
  shared->regionHeight = regionHeight;
  shared->regionRowsPerBand = regionRowsPerBand;
  shared->blockWidth = BLOCK_DEFAULT_WIDTH;
  shared->blockRowsPerRegion = ((regionHeight + BLOCK_DEFAULT_HEIGHT - 1)
                                / BLOCK_DEFAULT_HEIGHT);
  shared->blockHeight = ((regionHeight + shared->blockRowsPerRegion - 1)
                         / shared->blockRowsPerRegion);
  shared->xblock = bd_xBlockIndex(shared, shared->width - 1) + 1;
  shared->nTables = shared->blockHeight;

  if ( page->imageParams.LowMemImagePurgeToDisk &&
       !im_filecreatectxt(&shared->imfileCtxt, FALSE) ) {
    bd_sharedFree(&shared);
    return FALSE;
  }
  shared->dynamicResourceAlloc = TRUE;
  shared->allowLineRepeats = TRUE;
#ifdef DEBUG_BUILD
  shared->allowLineRepeats = (backdropDebug & BD_DISABLE_LINE_REPEATS) == 0;
#endif

  /* Allocate a reserve for color conversion per compositing thread. This
     reserve must be available before committing to every region set. */
  shared->backdropReserveSize = UserParams.BackdropReserveSize;

  *newShared = shared;
  return TRUE;
}

/**
 * Provision resources in preparation for compositing.  bd_resourceUpdate can be
 * called whenever new information is known regarding the resource requirements
 * for compositing, such as when adding a transparent object to the DL.  When
 * all the backdrops have been created bd_resourceUpdate should be called to
 * finalise resource provisioning.  In this case inCompsMax and
 * nonisolatedPresent arguments are unused and the function uses the more
 * accurate information gathered when creating the backdrops.
 */
Bool bd_resourceUpdate(BackdropShared *shared, uint32 inCompsMax,
                       Bool nonisolatedPresent, uint32 maxBackdropDepth)
{
  uint32 nMinResources, nMaxResources;
  size_t tableSize;
  Bool final = FALSE;
#if defined( ASSERT_BUILD )
  static Bool debug = FALSE;
#endif

  /* We're Finalising provisioning when backdrops have been created and
     shared->inCompsMax is set, otherwise provisioning is based on the given
     estimates. */
  HQASSERT((shared->inCompsMax > 0) == (shared->backdrops != NULL),
           "Expected shared->inCompsMax to be set by backdrop creation");
  final = shared->backdrops != NULL;
  HQTRACE(debug && final, ("bd_resourceUpdate: finalising"));

  /* nonIsolatedPresent can change back and forth often when estimating.  Avoid
     a significant slowdown by ignoring too many changes. */
  if ( !final && shared->provisioned.nonisolatedPresent )
    nonisolatedPresent = TRUE;

  /* shared->inCompsMax and shared->tableSize are available only after backdrops
     have been created.  If this is not the case just estimate tableSize from the
     given inCompsMax. */
  if ( final ) {
    /* Already have the correct final values in shared. */
    HQASSERT(shared->tableSize > 0, "Expected tableSize to be set");
    inCompsMax = shared->inCompsMax;
    tableSize = shared->tableSize;
  } else {
    /* Don't know final values so estimate. */
    if ( inCompsMax > shared->provisioned.inCompsMax ||
         nonisolatedPresent != shared->provisioned.nonisolatedPresent ) {
      tableSize = bdt_size(nonisolatedPresent ? BDT_NONISOLATED : BDT_ISOLATED,
                           inCompsMax, BACKDROPTABLE_MAXSLOTS);
      shared->provisioned.nonisolatedPresent = nonisolatedPresent;
    } else {
      /* Not less than previous estimate. */
      inCompsMax = shared->provisioned.inCompsMax;
      tableSize = shared->provisioned.tableSize;
    }
  }

  /* maxBackdropDepth may only be an estimate. */
  if ( maxBackdropDepth == 0 )
    maxBackdropDepth = 1;
  if ( maxBackdropDepth > shared->provisioned.maxBackdropDepth )
    shared->provisioned.maxBackdropDepth = maxBackdropDepth;

  /* Number of resources guaranteed to be sufficient for any single region.  New
     estimate can't be less than previous, but final nMinResources may reduce. */
  nMinResources = maxBackdropDepth * shared->blockRowsPerRegion;
  if ( !final && nMinResources < shared->provisioned.nMinResources )
    nMinResources = shared->provisioned.nMinResources;

  /* The max number of resources required to composite the whole band.  But
     there's probably little point in allocating all of these, so throttle the
     number back to a more manageable couple of hundred or so. */
  nMaxResources = (nMinResources * shared->regionRowsPerBand
                   * (bd_xBlockIndex(shared, shared->width - 1) + 1));
  if ( nMaxResources > shared->nLimResources &&
      shared->nLimResources > nMinResources )
    nMaxResources = shared->nLimResources;

  /* Set up the pools for the compositing contexts and backdrop resources. */
  if ( inCompsMax != shared->provisioned.inCompsMax ||
       nMaxResources != shared->provisioned.nMaxResources ) {
    HQTRACE(debug, ("bd_contextPoolGet: inCompsMax %d", inCompsMax));
    if ( !bd_contextPoolGet(shared->page->render_resources, nMaxResources,
                            inCompsMax, shared->width,
                            shared->backdropReserveSize) )
      return FALSE;
    shared->provisioned.inCompsMax = inCompsMax;
  }

  if ( tableSize != shared->provisioned.tableSize ) {
    HQTRACE(debug, ("bd_resourcePoolGet: tableSize %d", tableSize));
    if ( !bd_resourcePoolGet(shared->page->render_resources,
                             shared->nTables, tableSize, shared->blockHeight) )
      return FALSE;
    shared->provisioned.tableSize = tableSize;
    shared->provisioned.nMinResources = 0; /* Force bd_resourcePoolSetMinMax */
  }

  /* Change provision if new estimate is more than previous, or final value
     differs from a previous estimate.  When estimating, the resource
     requirements can keep changing and therefore it's better to wait till for
     the final update to provision the max number of resources required to
     composite the whole band or less. */
  if ( nMinResources > shared->provisioned.nMinResources ||
       (final && (nMinResources != shared->provisioned.nMinResources ||
                  nMaxResources != nMinResources)) ) {

    HQTRACE(debug, ("bd_resourcePoolSetMinMax: nMinResources %d, "
                    "nMaxResources %d, tableSize %d",
                    nMinResources, final ? nMaxResources : nMinResources,
                    bd_resourceSize(shared->nTables, tableSize, shared->blockHeight)));

    if ( !bd_resourcePoolSetMinMax(shared->page->render_resources,
                                   nMinResources,
                                   final ? nMaxResources : nMinResources) )
      return FALSE;
    shared->provisioned.nMinResources = nMinResources;
    shared->provisioned.nMaxResources = nMaxResources;
  }

  return TRUE;
}

static void bd_backdropStats(const BackdropShared *shared);

/**
 * Free all backdrop related memory.
 */
void bd_sharedFree(BackdropShared **freeShared)
{
  BackdropShared *shared = *freeShared;

  if ( shared != NULL ) {
    bd_backdropStats(shared);

    if ( shared->imfileCtxt != NULL )
      im_filedestroyctxt(&shared->imfileCtxt);

    bd_destroyPools(shared);

    *freeShared = NULL;
  }
}

static void bd_backdropLink(Backdrop *backdrop)
{
  HQASSERT(backdrop, "backdrop state missing in backdrop");
  HQASSERT(backdrop->shared, "backdrop shared state missing in backdrop");
  HQASSERT(backdrop->next == NULL, "backdrop already linked");

  /* Prepend. */
  backdrop->next = backdrop->shared->backdrops;
  backdrop->shared->backdrops = backdrop;

  ++backdrop->shared->nBackdrops;

  if ( backdrop->inComps > backdrop->shared->inCompsMax )
    backdrop->shared->inCompsMax = backdrop->inComps;

  if ( backdrop->compositeToPage ) {
    backdrop->nextPage = backdrop->shared->pageBackdrops;
    backdrop->shared->pageBackdrops = backdrop;
  }
}

static void bd_backdropUnlink(Backdrop *backdrop)
{
  Backdrop **link;

  /* Find backdrop in the list. */
  link = &backdrop->shared->backdrops;
  while ( *link != NULL && *link != backdrop ) {
    link = &(*link)->next;
  }

  if ( *link == backdrop ) {
    /* Unlink backdrop. */
    *link = backdrop->next;

    backdrop->next = NULL;

    --backdrop->shared->nBackdrops;

    if ( backdrop->compositeToPage ) {
      /* Find backdrop in the page backdrop list. */
      link = &backdrop->shared->pageBackdrops;
      while ( *link != NULL && *link != backdrop ) {
        link = &(*link)->nextPage;
      }

      if ( *link == backdrop ) {
        /* Unlink backdrop. */
        *link = backdrop->nextPage;

        backdrop->nextPage = NULL;
      }
    }
  }
}

/**
 * Given an x value, returns the block index for the x component.
 */
uint32 bd_xBlockIndex(const BackdropShared *shared, dcoord x)
{
  uint32 bx;

  HQASSERT((uint32)x < shared->width, "x out of range");

  if ( shared->blockWidth == 256 ) {
    bx = x >> 8;
    HQASSERT(bx == (x / shared->blockWidth),
             "bd_xBlockIndex optimisation failed");
  } else {
    HQFAIL("bd_xBlockIndex using slower fallback code");
    bx = x / shared->blockWidth;
  }

  return bx;
}

/**
 * Given a y value, returns the block index for the y component.
 */
uint32 bd_yBlockIndex(const BackdropShared *shared, dcoord y)
{
  uint32 by;

  HQASSERT((uint32)y < shared->height, "y out of range");
  by = (((y % shared->regionHeight) / shared->blockHeight) +
        ((y / shared->regionHeight) * shared->blockRowsPerRegion));
  return by;
}

/**
 * Given an x value, returns the pixel index into the block
 * for the x component.
 */
uint32 bd_xPixelIndex(const BackdropShared *shared, dcoord x)
{
  uint32 xi;

  HQASSERT((uint32)x < shared->width, "x out of range");
  if ( BIT_EXACTLY_ONE_SET((uint32)shared->blockWidth) ) {
    xi = x & (shared->blockWidth - 1);
    HQASSERT(xi == (x % shared->blockWidth),
             "bd_xPixelIndex optimisation failed");
  } else {
    HQFAIL("bd_xPixelIndex using slower fallback code");
    xi = x % shared->blockWidth;
  }
  return xi;
}

/**
 * Given a y value, returns the line index into the block
 * for the y component.
 */
uint32 bd_yPixelIndex(const BackdropShared *shared, dcoord y)
{
  uint32 yi;

  HQASSERT((uint32)y < shared->height, "y out of range");
  yi = ((y % shared->regionHeight) % shared->blockHeight);
  return yi;
}

/**
 * Given a bx and by, returns the index into the block ptrs.  May return
 * different bb values for same bx and by on different backdrops (because not
 * all backdrop have a full set of block ptrs).
 */
uint32 bd_blockIndex(const Backdrop *backdrop, uint32 bx, uint32 by)
{
  BackdropShared *shared = backdrop->shared;
  uint32 bb;

#ifdef BLOCKPTR_REUSE_OPTIMISATION
  if ( !backdrop->compositeToPage && !shared->multiThreaded )
    by = by % (shared->blockRowsPerRegion * shared->regionRowsPerBand);
#endif

  HQASSERT(bx < shared->xblock, "bx out of range");
  HQASSERT(by < backdrop->yblock, "by out of range");

  bb = bx + by * shared->xblock;
  HQASSERT(bb < backdrop->nBlocks, "bb out of range");

  return bb;
}

static Bool bd_copyColorants(Backdrop *backdrop,
                             uint32 srcComps, COLORANTINDEX *srcColorants,
                             COLORANTINDEX **destColorants)
{
  uint32 i;

  *destColorants = bd_stateAlloc(backdrop->shared,
                                 srcComps * sizeof(COLORANTINDEX));
  if ( *destColorants == NULL )
    return error_handler(VMERROR);

  for ( i = 0; i < srcComps; ++i ) {
    (*destColorants)[i] = srcColorants[i];
  }
  return TRUE;
}

/**
 * Build the mapping from input colorants in the current blend space all the way
 * to final device space colorants.  This mapping is used for haltone merging
 * for overprinted objects.
 */
static Bool bd_setDeviceColorantMapping(Backdrop *backdrop, Group *group)
{
  uint32 i;

  HQASSERT(!backdrop->deviceColorantMapping, "deviceColorantMapping already exists");

  backdrop->deviceColorantMapping =
    bd_stateAlloc(backdrop->shared,
                  backdrop->inComps * sizeof(COLORANTINDEX*));
  if ( backdrop->deviceColorantMapping == NULL )
    return error_handler(VMERROR);

  for ( i = 0; i < backdrop->inComps; ++i ) {
    (void)groupColorantToEquivalentRealColorant(group, backdrop->inColorants[i],
                                                &backdrop->deviceColorantMapping[i]);
  }

  return TRUE;
}

/**
 * A backdrop isolated status is determined by the presence or absence of an
 * initialBackdrop.  The group isolated attribute is ignored because group
 * elimination may change the isolated status.  Therefore if an initialBackdrop
 * exists the backdrop is considered to be non-isolated.
 *
 * initialColor/Alpha/Info are used for isolated backdrops only.  Color and
 * alpha are by default set to zero (according to the PDF spec).  Softmasks from
 * luminosity override the initial values by calling bd_setSoftMask.
 *
 * PCL sets initialColor values to one but sets initialAlpha to zero.  PCL
 * ropping requires a white surface but we still want an initial alpha value of
 * zero so that untouched areas of the backdrop are skipped when the backdrop is
 * rendered.  This means initial color for PCL is not premultiplied, but this is
 * ok because PCL has its own compositing rules (it doesn't call
 * bd_compositeColor or bdt_compositeToPage).
 */
static Bool bd_setInitialColor(Backdrop *backdrop, Backdrop *initialBackdrop,
                               int32 spotNo)
{
  if ( initialBackdrop ) {
    /* A non-isolated backdrop. */
#if defined( ASSERT_BUILD )
    /* For non-isolated backdrops initialBackdrop is used for initialisation.
       The blend spaces of the backdrops must match, but initialBackdrop (an
       antecedent of backdrop) is allowed to have extra spot colorants which
       must have been added after backdrop was closed.  The extra colorants are
       always at the end of the colorant array in initialBackdrop. */
    uint32 i;
    HQASSERT(initialBackdrop->inComps >= backdrop->inComps,
             "Initial backdrop must have superset of child backdrop");
    for ( i = 0; i < backdrop->inComps; ++i ) {
      HQASSERT(initialBackdrop->inColorants[i] == backdrop->inColorants[i],
               "Non-isolated parent backdrop colorants must have any "
               "additional colorants at the end of the colorant list");
    }
#endif
    backdrop->initial.backdrop = initialBackdrop;
    backdrop->initial.color = NULL;
    backdrop->initial.alpha = COLORVALUE_INVALID;
    backdrop->initial.info.label = 0;
    backdrop->isolated = FALSE;
  } else {
    /* An isolated backdrop. */    /* was pclGstateIsEnabled() */
    BackdropShared *shared = backdrop->shared;
    COLORVALUE cv = shared->page->opaqueOnly ? COLORVALUE_ONE : COLORVALUE_ZERO;
    uint32 i;

    backdrop->initial.backdrop = NULL;
    backdrop->initial.color =
      bd_stateAlloc(backdrop->shared,
                    backdrop->inComps * sizeof(COLORVALUE));
    if ( backdrop->initial.color == NULL )
      return error_handler(VMERROR);

    for ( i = 0; i < backdrop->inComps; ++i ) {
      backdrop->initial.color[i] = cv;
    }
    backdrop->initial.alpha = COLORVALUE_ZERO;
    backdrop->initial.groupAlpha = COLORVALUE_ZERO;
    COLORINFO_SET(backdrop->initial.info, spotNo, GSC_FILL, REPRO_TYPE_OTHER,
                  REPRO_COLOR_MODEL_GRAY, SW_CMM_INTENT_RELATIVE_COLORIMETRIC,
                  BLACK_TYPE_NONE, TRUE, 0, 0);
    backdrop->isolated = TRUE;
  }

  return TRUE;
}

/**
 * The parent backdrop may have additional colorants not in the child or may
 * require a mapping if the colorants are in a different order.  Backdrops are
 * created with blit colorant order, but after color conversion the colorants
 * are sorted into ascending numerical order by the color chain code.  Create a
 * mapping from backdrop's colorants to parentBackdrop's colorants if necessary.
 */
static Bool bd_setParentBackdrop(Backdrop *backdrop, Backdrop *parentBackdrop)
{
  backdrop->parentBackdrop = parentBackdrop;

  if ( parentBackdrop ) {
    uint32 i;

    HQASSERT(parentBackdrop->inComps >= backdrop->outComps,
             "Parent backdrop must have superset of child backdrop");

    if ( parentBackdrop->inComps == backdrop->outComps ) {
      /* Same number of colorants but are they in the same order? */
      for ( i = 0; i < parentBackdrop->inComps; ++i ) {
        if ( backdrop->outColorants[i] != parentBackdrop->inColorants[i] )
          break;
      }
      if ( i == parentBackdrop->inComps )
        return TRUE; /* Don't need a mapping, the colorants match. */
    }

    /* Build a colorant mapping from backdrop to its parent. */
    backdrop->parentMapping =
      bd_stateAlloc(backdrop->shared,
                    parentBackdrop->inComps * sizeof(int32));
    if ( backdrop->parentMapping == NULL )
      return error_handler(VMERROR);

    for ( i = 0; i < parentBackdrop->inComps; ++i ) {
      int32 j;
      for ( j = backdrop->outComps; --j >= 0; ) {
        if ( backdrop->outColorants[j] == parentBackdrop->inColorants[i] )
          break;
      }
      backdrop->parentMapping[i] = j; /* possibly -1 indicating colorant doesn't
                                         exist in backdrop */
    }
  }

  return TRUE;
}

/**
 * Allocate block ptrs for the backdrop, and initialise the blocks to null.  The
 * blocks are created by bd_blockInit or bd_blockComplete.
 */
static Bool bd_blockPtrsNew(Backdrop *backdrop)
{
  BackdropShared *shared = backdrop->shared;
  uint32 i;

  backdrop->yblock = bd_yBlockIndex(shared, shared->height - 1) + 1;
#ifdef BLOCKPTR_REUSE_OPTIMISATION
  if ( !backdrop->compositeToPage && !shared->multiThreaded )
    backdrop->yblock = shared->blockRowsPerRegion * shared->regionRowsPerBand;
#endif

  backdrop->nBlocks = shared->xblock * backdrop->yblock;
  backdrop->blocks = bd_stateAlloc(shared,
                                   backdrop->nBlocks * sizeof(BackdropBlock*));
  if ( backdrop->blocks == NULL )
    return error_handler(VMERROR);

  for ( i = 0; i < backdrop->nBlocks; ++i ) {
    backdrop->blocks[i] = NULL;
  }

  return TRUE;
}

/**
 * Make the table large enough to hold output as well as input tables
 * and then the tables can be reused to render the final backdop.
 */
static void bd_setTableSize(Backdrop *backdrop)
{
  size_t tableSize, outTableSize;

  tableSize = bdt_size((backdrop->isolated
                        ? (backdrop->trackShape ? BDT_ISOLATED_SHAPE : BDT_ISOLATED)
                        : (backdrop->trackShape ? BDT_NONISOLATED_SHAPE : BDT_NONISOLATED)),
                       backdrop->inComps, BACKDROPTABLE_MAXSLOTS);

  outTableSize = bdt_size(backdrop->outTableType, backdrop->outComps,
                          BACKDROPTABLE_MAXSLOTS);

  if ( outTableSize > tableSize )
    tableSize = outTableSize;

  /* All resources are the same size (to simplify and optimise resource
     sharing).  Resources are allocated only after all the backdrops have been
     created, when largest resource size required is known. */
  if ( tableSize > backdrop->shared->tableSize )
    backdrop->shared->tableSize = tableSize;
}

/**
 * Make a new backdrop.  Converter is used to color convert between blend spaces
 * or to the final device.
 */
Bool bd_backdropNew(BackdropShared *shared, uint32 depth,
                    Backdrop *initialBackdrop, Backdrop *parentBackdrop,
                    Group *group, Bool out16,
                    uint32 inComps, uint32 inProcessComps,
                    COLORANTINDEX *inColorants,
                    uint32 outComps, COLORANTINDEX *outColorants,
                    CV_COLCVT *converter, LateColorAttrib *pageGroupLCA,
                    Backdrop **newBackdrop)
{
  Bool result = FALSE;
  Backdrop *backdrop = NULL, init = {0};
  const GroupAttrs *attrs = groupGetAttrs(group);

  backdrop = bd_stateAlloc(shared, sizeof(Backdrop));
  if ( backdrop == NULL )
    return error_handler(VMERROR);
  *backdrop = init;
#define return USE_goto_cleanup

  backdrop->shared = shared;
  hdlBBox(groupHdl(group), &backdrop->bounds);
  backdrop->depth = depth;

  HQASSERT(inProcessComps <= inComps,
           "inProcessComps cannot be greater than inComps");
  backdrop->inComps = inComps;
  backdrop->inProcessComps = inProcessComps;
  if ( !bd_copyColorants(backdrop, inComps, inColorants,
                         &backdrop->inColorants) )
    goto cleanup;

  backdrop->outComps = outComps;
  if ( !bd_copyColorants(backdrop, outComps, outColorants,
                         &backdrop->outColorants) )
    goto cleanup;

  backdrop->converter = converter;
  backdrop->pageGroupLCA = pageGroupLCA;
  backdrop->softMaskType = attrs->softMaskType;
  backdrop->isolated = (int8)attrs->isolated;
  backdrop->knockout = (int8)attrs->knockout;
  backdrop->knockoutDescendant = (int8)attrs->knockoutDescendant;
  backdrop->compositeToPage = (int8)attrs->compositeToPage;
  backdrop->trackShape = (int8)(attrs->hasShape &&
                                parentBackdrop != NULL &&
                                parentBackdrop->knockoutDescendant);

  /* Initialise blocks from either the initialBackdrop or from
     initialColor/Alpha/Info for isolated backdrops. */
  if ( !bd_setInitialColor(backdrop, initialBackdrop,
                           shared->page->default_spot_no) )
    goto cleanup;

  if ( !bd_setParentBackdrop(backdrop, parentBackdrop) )
    goto cleanup;

  if ( !bd_setDeviceColorantMapping(backdrop, group) )
    goto cleanup;

  /* The backdropLabel is always combined with the result label in compositing.
     This is used when outputting transparency RLE to mark all backdrop rendered
     spans as composited. */
  backdrop->backdropLabel = (DOING_TRANSPARENT_RLE(shared->page)
                             ? SW_PGB_USER_OBJECT : 0);

  if ( !bd_blockPtrsNew(backdrop) )
    goto cleanup;

  if ( backdrop->compositeToPage )
    backdrop->outTableType = out16 ? BDT_OUTPUT16 : BDT_OUTPUT8;
  else if ( backdrop->softMaskType == EmptySoftMask )
    backdrop->outTableType =
      backdrop->trackShape ? BDT_ISOLATED_SHAPE : BDT_ISOLATED;
  else
    backdrop->outTableType = BDT_ALPHA;

  bd_setTableSize(backdrop);

  bd_backdropLink(backdrop);

  *newBackdrop = backdrop;
  result = TRUE;
 cleanup:
  if ( !result && backdrop )
    bd_backdropFree(&backdrop);
#undef return
  return result;
}

/**
 * bd_backdropFree unlinks the backdrop from the list of backdrops and frees off
 * all bits inside.
 */
void bd_backdropFree(Backdrop **freeBackdrop)
{
  Backdrop *backdrop = *freeBackdrop;

  bd_backdropUnlink(backdrop);

  if ( backdrop->inColorants )
    bd_stateFree(backdrop->shared, backdrop->inColorants,
                 backdrop->inComps * sizeof(COLORANTINDEX));
  if ( backdrop->outColorants )
    bd_stateFree(backdrop->shared, backdrop->outColorants,
                 backdrop->outComps * sizeof(COLORANTINDEX));
  if ( backdrop->pageColor )
    bd_stateFree(backdrop->shared, backdrop->pageColor,
                 backdrop->inComps * sizeof(COLORVALUE));
  if ( backdrop->initial.color )
    bd_stateFree(backdrop->shared, backdrop->initial.color,
                 backdrop->inComps * sizeof(COLORVALUE));
  if ( backdrop->deviceColorantMapping )
    bd_stateFree(backdrop->shared, backdrop->deviceColorantMapping,
                 backdrop->inComps * sizeof(COLORANTINDEX*));

  if ( backdrop->blocks )
    bd_stateFree(backdrop->shared, backdrop->blocks,
                 backdrop->nBlocks * sizeof(BackdropBlock*));

  bd_stateFree(backdrop->shared, backdrop, sizeof(Backdrop));

  *freeBackdrop = NULL;
}

/**
 * The final composited backdrop is composited against a completely
 * opaque color made from the erase color.
 */
Bool bd_setPageColor(Backdrop *backdrop, uint32 inComps, COLORVALUE *pageColor)
{
  uint32 i;

  HQASSERT(backdrop->compositeToPage,
           "pageColor only required if compositeToPage is set");
  HQASSERT(backdrop->pageColor == NULL, "pageColor already exists");
  HQASSERT(inComps == backdrop->inComps,
           "pageColor number of components differs from backdrop");

  backdrop->pageColor = bd_stateAlloc(backdrop->shared,
                                      inComps * sizeof(COLORVALUE));
  if ( backdrop->pageColor == NULL )
    return error_handler(VMERROR);

  for ( i = 0; i < inComps; ++i )
    backdrop->pageColor[i] = pageColor[i];

  return TRUE;
}

/**
 * Set the initial color and alpha for a softmask from luminosity backdrop.
 * Softmask from luminosity backdrops are defined to be initialised with a fully
 * opaque color.  Also set the transfer function for a softmask (either from
 * luminosity or alpha).
 */
Bool bd_setSoftMask(Backdrop *backdrop, COLORVALUE *initialColor,
                    FN_INTERNAL *softMaskTransfer)
{
  HQASSERT(backdrop, "backdrop is missing");
  HQASSERT(backdrop->isolated, "Expected isolated flag to be set for soft mask");
  HQASSERT(backdrop->softMaskType != EmptySoftMask,
           "backdrop is not a softmask");

  if ( backdrop->softMaskType == LuminositySoftMask ) {
    HQASSERT(initialColor, "initialColor is null");
    bd_copyColor(backdrop->initial.color, initialColor, backdrop->inComps);
    backdrop->initial.alpha = COLORVALUE_ONE;
    backdrop->initial.groupAlpha = COLORVALUE_ZERO;

    /* initialInfo is first setup to have no pixel label, but this needs
       changing for softmasks from luminosity backdrop, because they are
       initialised to an opaque background. */
    backdrop->initial.info.label = SW_PGB_COMPOSITED_OBJECT;

    /* Create the default backdrop color for a luminosity softmask.  Color
       convert, producing a single colorvalue representing the mask alpha. */
    if ( !cv_colcvt(backdrop->converter, 1, NULL, backdrop->initial.color,
                    &backdrop->defaultMaskAlpha, NULL) )
      return FALSE;
  }

  if ( softMaskTransfer != NULL ) {
    backdrop->softMaskTransfer = softMaskTransfer;

    backdrop->defaultMaskAlpha =
      fn_evaluateColorValue(backdrop->softMaskTransfer,
                            backdrop->defaultMaskAlpha);
  }

  return TRUE;
}

struct BackdropReserve {
  size_t bytes;
  BackdropReserve *next;
};

/**
 * A bit of a hack to make sure we have got the BackdropReserveSize, required
 * since we can't purge images in the renderer so force however much required to
 * disk now.  Another reason is to stop the halftone preload grabbing all the
 * memory.
 */
void bd_allocBackdropReserve(size_t reserveSize, Bool forceAlloc,
                             BackdropReserve **reserveHead)
{
  mm_cost_t cost = { memory_tier_disk, 2.0 }; /* previously, purged images and fonts */
  /** \todo Determine real cost of backdrop reserve. */

  HQASSERT(*reserveHead == NULL, "Backdrop reserve already exists");

  /* Round up to the next multiple of BLOCK_DEFAULT_SIZE. */
  reserveSize = ((reserveSize + BLOCK_DEFAULT_SIZE - 1)
                 / BLOCK_DEFAULT_SIZE) * BLOCK_DEFAULT_SIZE;

#ifdef DEBUG_BUILD
  if ( (backdropDebug & BD_DEBUG_MEMORY) != 0 ) {
    monitorf((uint8*)"Backdrop reserve alloc request: %d\n", reserveSize);
    mm_pool_memstats() ;
    monitorf(( uint8 * )"  Memory not assigned to any pool: %d\n",
             mm_no_pool_size(TRUE)) ;
  }
#endif

  HQASSERT(BLOCK_DEFAULT_SIZE >= sizeof(BackdropReserve),
           "BLOCK_DEFAULT_SIZE smaller than BackdropReserve struct size");

  /* No need to actually allocate if the memory is already available, unless
     forceAlloc is set.  When retaining the page at hi-res the reserve could be
     a large amount of memory, eg 130 MBs.  Allocating the reserve may force
     images to disk which could take a large amount of time, but the alternative
     is a vmerror at some point during compositing. */
  while ( reserveSize > 0 &&
          (forceAlloc ||
           reserveSize > mm_no_pool_size(FALSE /* include_reserve */)) ) {
    BackdropReserve *reserve = bd_resourceAllocCost(BLOCK_DEFAULT_SIZE, cost);
    if ( reserve == NULL )
      break; /* not an error */

    reserve->bytes = BLOCK_DEFAULT_SIZE;
    reserve->next = *reserveHead;
    *reserveHead = reserve;

    reserveSize -= BLOCK_DEFAULT_SIZE;
    SwOftenUnsafe();
  }
}

void bd_freeBackdropReserve(BackdropReserve **reserveHead)
{
  while ( *reserveHead != NULL ) {
    BackdropReserve *reserve = *reserveHead;
    (*reserveHead) = reserve->next;
    bd_resourceFree(reserve, reserve->bytes);
  }
}

/**
 * Ensure there is enough memory available to retain blocks across bands or
 * separations by potentially forcing some image data to disk. This reserve is a
 * one off and is not maintained whilst compositing.
 */
static void bd_prepareForRetainedBlocks(BackdropShared *shared,
                                        Backdrop *pageBackdrop)
{
  size_t reserveSize = 0, blockSizeEstimate;
  BackdropReserve *reserveHead = NULL;

  /* Estimate the size of the non-purgeable part of a backdrop block.  In
     general this excludes the tables which can be purged, but uniform blocks
     have a table with a single entry that is never purged.  Estimate a third of
     the blocks being uniform. */
#define UNIFORM_BLOCK_PROPORTION (0.33) /* just a rough guess */
  blockSizeEstimate = (size_t)(bd_blockSize()
                               + (UNIFORM_BLOCK_PROPORTION
                                  * bdt_size(pageBackdrop->outTableType,
                                             pageBackdrop->outComps, 1)));

  if ( shared->retention == BD_RETAINBAND ) {
    /* Need to hold one band's worth of block headers at a time. */
    reserveSize += blockSizeEstimate * shared->xblock *
                   shared->regionRowsPerBand * shared->blockRowsPerRegion;
  } else if ( shared->retention == BD_RETAINPAGE ) {
    /* Need to hold all the composited block headers between separations. */
    reserveSize += (size_t)(blockSizeEstimate
                            * bitGridRegionsSetProportion(shared->page->regionMap)
                            * pageBackdrop->nBlocks);
  }

  /* Ensure there are reserveSize bytes available. */
  bd_allocBackdropReserve(reserveSize, FALSE, &reserveHead);
  bd_freeBackdropReserve(&reserveHead);
}

/**
 * bd_backdropPrepare ensures there are enough resources to composite this
 * backdrop, at least for the minimum region area.  Specifies backdrop retention
 * required for band or frame/separated output.  For overprinted objects the
 * resulting screen is a combination of foreground and background spots.  This
 * spot merging only applies if we are doing screened output.
 */
Bool bd_backdropPrepare(BackdropShared *shared, uint32 retention,
                        Bool mergeSpots)
{
  HQASSERT(shared->pageBackdrops, "page backdrop is null");
  HQASSERT(shared->nBackdrops > 0, "No backdrops to prepare resources for");
  HQASSERT(shared->tableSize >= 0, "tableSize uninitialised");

  shared->retention = retention;
  shared->mergeSpots = mergeSpots;

  /* Ensure there is enough memory available to retain blocks across bands or
     separations. This reserve is a one off and is not checked whilst
     compositing. */
  if ( shared->retention == BD_RETAINBAND ||
       shared->retention == BD_RETAINPAGE ) {
    Backdrop *pageBackdrop;
    for ( pageBackdrop = shared->pageBackdrops; pageBackdrop != NULL;
          pageBackdrop = pageBackdrop->nextPage ) {
      bd_prepareForRetainedBlocks(shared, pageBackdrop);
    }
  }

#ifdef DEBUG_BUILD
  if ( (backdropDebug & BD_DEBUG_MEMORY) != 0 )
    monitorf((uint8*)"bd_backdropPrepare preallocated %d backdrop resources for each "
             "compositing thread (the minimum for guaranteed success)\n",
             shared->provisioned.nMinResources);
#endif

  return TRUE;
}

void bd_setCurrentPagebackdrop(CompositeContext *context,
                               Backdrop *pageBackdrop)
{
  context->currentPageBackdrop = pageBackdrop;
}

/**
 * bd_requestRegions is called to determine if we have enough resources to
 * composite the region area given by bounds.  A large region area may have to
 * be split into separate regions to ensure compositing can be completed.
 */
Bool bd_requestRegions(const BackdropShared *shared, CompositeContext *context,
                       uint32 backdropDepth, const dbbox_t *regions)
{
  uint32 nRows, nCols, nBlocks, nRequestResources;
  uint32 bx, by ;
  dbbox_t bounds = *regions;

  HQASSERT(context->currentPageBackdrop != NULL,
           "Must have called bd_setCurrentPagebackdrop first");
  bbox_intersection(&bounds, &context->currentPageBackdrop->bounds, &bounds);
  bbox_intersection_coordinates(&bounds, 0, 0, (dcoord)shared->width-1,
                                (dcoord)shared->height-1);

  bd_freeBackdropReserve(&context->backdropReserve);

  /* First time around, check if this region has been composited already.
     If so, only need resources to read the page backdrop. */
  if ( context->nFixedResources == 0 ) {
    if ( !bd_regionRequiresCompositing(context->currentPageBackdrop, &bounds, NULL) )
      backdropDepth = 1;
    context->backdropDepth = backdropDepth;
  }

  bx = bd_xBlockIndex(shared, bounds.x1) ;
  by = bd_yBlockIndex(shared, bounds.y1) ;

  /* The backdrop's region size must be a multiple of the region map's region
     size.  Allowing the two to vary independently is useful for performance.
     For small areas of compositing this may mean not all of the backdrop block
     is actually used; for a large area of compositing the large backdrop blocks
     improve performance. */

  nCols = bd_xBlockIndex(shared, bounds.x2) - bx + 1;

  /* Last row of regions may require fewer rows of blocks. */
  nRows = bd_yBlockIndex(shared, bounds.y2) - by + 1;

  nBlocks = nRows * nCols;
  nRequestResources = nBlocks * context->backdropDepth;

  if ( nRequestResources > shared->provisioned.nMaxResources )
    return FALSE; /* nMaxResources is has been limited. */

  /* A region request of just one must succeed (must already have a minimum
     number of resources to complete one region), but any number of regions
     beyond that may fail owing to low memory.  Must fix a region request above
     the minimum now to ensure the resources don't get purged in low memory.
     Backdrop block width may differ from region width and therefore some
     requests may not require more resources than last time. */
  HQASSERT(nRequestResources <= context->nMaxResources,
           "Requesting too many resources");

  if ( nRequestResources != context->nFixedResources ) {
    resource_id_t *fixedResourceIds ;
    void **fixedResources ;
    uint32 nExtra, id ;

    if ( shared->imfileCtxt == NULL ) {
      /* May have failed to retain all the composited blocks from the last pass
         and therefore some blocks may already be composited and others not. */
      Bool doCompositing, consistent;
      dbbox_t testBounds;

      testBounds = bounds;
      if ( context->nFixedResources > 0 &&
           bounds.x1 == context->requestBounds.x1 &&
           bounds.y1 == context->requestBounds.y1 &&
           bounds.y2 == context->requestBounds.y2 )
        /* Just test the additional area over the previous request. */
        testBounds.x1 = context->requestBounds.x2 + 1;

      HQASSERT(shared->pageBackdrops != NULL && shared->pageBackdrops->nextPage == NULL,
               "This code expects only one page backdrop");
      doCompositing = bd_regionRequiresCompositing(shared->pageBackdrops, &testBounds,
                                                   &consistent);
      if ( !consistent ||
           (context->nFixedResources > 0 &&
            doCompositing != context->doCompositing) )
        return FALSE;

      context->requestBounds = bounds;
      context->doCompositing = doCompositing;
    }

    HQASSERT(nRequestResources > context->nFixedResources,
             "Not extending requested region");

    fixedResourceIds = context->fixedResourceIds + context->nFixedResources ;
    fixedResources = context->fixedResources + context->nFixedResources ;

    /* Resource IDs are the block index multiplied by the group depth, plus
       the current backdrop depth. We arrange it such that it is the index
       into the fixed resource array, so we can */
    for ( id = context->nFixedResources, nExtra = 0 ;
          id < nRequestResources ;
          ++id, ++nExtra ) {
      fixedResourceIds[nExtra] = (resource_id_t)id ;
      fixedResources[nExtra] = NULL ;
    }

    HQASSERT(nExtra + context->nFixedResources == nRequestResources,
             "Fixing wrong number of resources") ;

    if ( !task_resource_fix_n(TASK_RESOURCE_BACKDROP_RESOURCE,
                              nExtra, fixedResourceIds, fixedResources, NULL) )
      return FALSE ;

    context->baseX = bx ;
    context->baseY = by;
    context->xblock = nCols ;
    context->nFixedResources = nRequestResources;
  }

  return TRUE;
}

/**
 * bd_regionRequiresCompositing determines whether the region has already been
 * composited in a previous band, frame or separation.  If it has been
 * composited then obviously we don't need to do it again.  Check for
 * consistency if the arg has been supplied, otherwise assume consistency is
 * required and assert it.
 */
Bool bd_regionRequiresCompositing(const Backdrop *backdrop,
                                  const dbbox_t *bounds,
                                  Bool *consistent)
{
  Bool doCompositing;
  BlockIterator iterator = BLOCKITERATOR_INIT;
  BackdropBlock *block;
  Bool more;

  HQASSERT(backdrop, "backdrop is null");
  HQASSERT(bounds, "bounds is null");

  more = bd_blockIterator(backdrop, bounds, & iterator);
  block = *bd_iteratorLocateBlock(&iterator, backdrop);

  doCompositing = block == NULL || !bd_isComplete(block);

  if ( consistent != NULL
#ifdef ASSERT_BUILD
       || TRUE
#endif
       ) {
    if ( consistent != NULL )
      *consistent = TRUE;

    while ( more ) {
      more = bd_blockIterator(backdrop, bounds, & iterator);
      block = *bd_iteratorLocateBlock(&iterator, backdrop);

      if ( !(doCompositing == (block == NULL || !bd_isComplete(block))) ) {
        if ( consistent != NULL )
          *consistent = FALSE;
        else
          HQFAIL("Region bbox has been used inconsistently");
        break;
      }
    }
  }

  return doCompositing;
}

/**
 * bd_regionInit creates backdrop blocks from resources to cover the region area
 * given by bounds.  The blocks are initialised from the initial color (for
 * isolated backdrops) or from the initial backdrop (for non-isolated
 * backdrops).
 */
Bool bd_regionInit(CompositeContext *context,
                   const Backdrop *backdrop, const dbbox_t *bounds)
{
  BlockIterator iterator = BLOCKITERATOR_INIT;
  Bool more;

  HQASSERT(backdrop, "backdrop is null");
  HQASSERT(bounds, "bounds is null");

  if ( backdrop->compositeToPage ) {
    bd_coalesceInit(context->coalesce);
    bbox_union(&context->bandBounds, bounds, &context->bandBounds);
  } else
    bd_coalesceFlush(context);

  do {
    uint32 bx, by;

    more = bd_blockIterator(backdrop, bounds, &iterator);
    bd_iteratorPosition(&iterator, &bx, &by);

    bd_blockInit(backdrop, context, bx, by);
  } while ( more );

  return TRUE;
}

/**
 * Iterate over the alpha data in the specified line of the passed block.
 * 'iterator' should be set to zero by the client, and then this function should
 * be called repeatedly until it returns FALSE, indicating the no more data is
 * available.
 *
 * The iterated run details will be returned in 'length' and 'alpha'.
 */
static Bool bd_getAlphaRun(BackdropBlock *block, uint32 yi,
                           uint32 *iterator,
                           uint32 *length, uint16 *alpha)
{
  BackdropLine *lines = bd_blockLine(block, 0);

  HQASSERT(bd_isComplete(block), "mask block must be in complete mode");

  yi = bd_findRepeatSource(lines, yi);

  if ( bd_isUniform(block) ) {
    *length = bd_blockWidth(block);
    bdt_getAlpha(bd_uniformTable(block), 0u, alpha);
    return FALSE;
  }
  else {
    uint8 maskIndex;
    uint8 *mdata = bd_blockData(block, yi);
    Bool more = TRUE;

    if ( bd_isRLE(&lines[yi]) ) {
      /* This block is held in RLE already - return the iterated run. */
      HQASSERT(lines[yi].nRuns > 0, "bd_getAlphaRun - block cannot be empty");

      *length = (int32)readRunLen(mdata[(*iterator) * 2]);
      maskIndex = mdata[((*iterator) * 2) + 1];

      (*iterator) ++;
      more = (*iterator) < lines[yi].nRuns;
    } else {
      /* This block is held in bitmap form - just return a single length run. */
      *length = 1;
      maskIndex = mdata[*iterator];

      (*iterator) ++;
      more = (*iterator) < bd_blockWidth(block);
    }

    bdt_getAlpha(lines[yi].table, maskIndex, alpha);
    return more;
  }
}

/**
 * Pass the soft mask data for the passed line, within the passed x-extent
 * (inclusive), to the provided iterator function one span at a time.
 */
Bool bd_readSoftmaskLine(const Backdrop *backdrop, dcoord y, dcoord x1, dcoord x2,
                         AlphaSpanIteratorFunc callback,
                         void *callbackData)
{
  BackdropShared *shared = backdrop->shared;
  dcoord x;
  uint32 yi, bx, by, bb;
  Bool clip = TRUE;

  SwOftenUnsafe();

  bx = bd_xBlockIndex(shared, x1);
  x = bx * shared->blockWidth;

  yi = bd_yPixelIndex(shared, y);
  by = bd_yBlockIndex(shared, y);

  bb = bd_blockIndex(backdrop, bx, by);

  /* Output the required spans. */
  while ( x < x2 && bx < shared->xblock ) {
    Bool more;
    uint32 iterator = 0;
    BackdropBlock *block = backdrop->blocks[bb++];

    bx++;

    /* Iterate over the runs in the block. */
    do {
      uint32 length;
      uint16 alpha;
      more = bd_getAlphaRun(block, yi, &iterator, &length, &alpha);

      if ( x + (dcoord)length > x2 ) {
        /* x2 is inclusive, thus +1 */
        length = x2 - x + 1;
        more = FALSE;
      }

      if ( clip ) {
        if ( x + (dcoord)length > x1 ) {
          /* The first span to be output may straddle the clip boundary, thus
             it's length needs adjusting. */
          if ( !callback(callbackData, alpha, length - (x1 - x)) )
            return FALSE;

          clip = FALSE;
        }
      } else {
        if ( !callback(callbackData, alpha, length) )
          return FALSE;
      }

      x += length;
    } while ( more && x <= x2 );
  }

  return TRUE;
}

static Bool bd_regionReclaim(const Backdrop *backdrop,
                             const dbbox_t *bounds, Bool bandclose,
                             Bool canKeep)
{
  BlockIterator iterator = BLOCKITERATOR_INIT;
  Bool result = TRUE, more;

  HQASSERT(backdrop, "backdrop is null");
  HQASSERT(!bbox_is_empty(bounds),
           "never initialised a region, no blocks to finish");

  do {
    BackdropBlock **pblock;

    more = bd_blockIterator(backdrop, bounds, & iterator);
    pblock = bd_iteratorLocateBlock(&iterator, backdrop);
    HQASSERT(pblock != NULL, "No block position in plane");

    if ( *pblock != NULL &&
         !bd_blockReclaim(backdrop, pblock, bandclose, canKeep) )
      result = FALSE ;
  } while ( more );

  return result;
}

/**
 * bd_regionRelease is called on a single backdrop to release the resources.
 * The resources are returned to the global list ready to be picked up to
 * composite the next region for any backdrop.
 */
Bool bd_regionRelease(const Backdrop *backdrop, const dbbox_t *bounds)
{
  HQASSERT(!bbox_is_empty(bounds), "empty region - nothing to release!");
  return bd_regionReclaim(backdrop, bounds, FALSE /* bandclose */, TRUE);
}

/**
 * bd_regionReleaseAll returns all the resources to the global list that have
 * been used for this region, for all the backdrops.
 */
Bool bd_regionReleaseAll(CompositeContext *context, const BackdropShared *shared,
                         const dbbox_t *bounds, Bool canKeep)
{
  Backdrop *backdrop;
  Bool result = TRUE;
  size_t i;

  HQASSERT(!bbox_is_empty(bounds), "empty region - nothing to release!");

  if ( shared->retention != BD_RETAINNOTHING && shared->imfileCtxt == NULL ) {
    /* We can't flush resources to disk.
       If a retained block in this region set is using a resource then release
       the block and re-composite on the next band/frame. The resource must be
       released or we may try using it in more than one block. We shouldn't need
       to flush the whole region set out, but in practice with multiple rows of
       blocks it is difficult to avoid. */
    BlockIterator preiterator = BLOCKITERATOR_INIT;
    Bool more;

    HQASSERT(shared->pageBackdrops != NULL && shared->pageBackdrops->nextPage == NULL,
             "This code expects only one page backdrop");
    do {
      BackdropBlock **pblock;

      more = bd_blockIterator(shared->pageBackdrops, bounds, &preiterator);
      pblock = bd_iteratorLocateBlock(&preiterator, shared->pageBackdrops);
      HQASSERT(pblock != NULL, "No block position in plane");

      if ( *pblock != NULL ) {
        if ( bd_blockResource(*pblock) != NULL ) {
          canKeep = FALSE;
          break;
        }
      }
    } while ( more );
  }

  /* Release all the resources, and if necessary write blocks to disk. */
  for ( backdrop = shared->backdrops; backdrop; backdrop = backdrop->next ) {
    if ( !bd_regionReclaim(backdrop, bounds, FALSE /* bandclose */,
                           canKeep) )
      result = FALSE;
  }

  /* Unfix the resources that were fixed in bd_requestRegions. */
  for ( i = 0; i < context->nFixedResources; ++i ) {
    if ( context->fixedResources[i] != NULL ) {
      bd_resourceRelease(context->fixedResources[i], i);
      context->fixedResources[i] = NULL;
    }
  }
  context->nFixedResources = 0;

  return result;
}

/**
 * Resets backdrop for the next band for this context.
 */
void bd_bandInit(CompositeContext *context)
{
  bbox_clear(&context->bandBounds);
}

/**
 * Release all the backdrop blocks for this band if band retaining.
 * Also, if result is false, the whole band is cleared out to avoid region set
 * inconsistencies if compositing is retried (and this applies to all output
 * styles).
 */
Bool bd_bandRelease(CompositeContext *context, const BackdropShared *shared,
                    Bool canKeep)
{
  Bool result = TRUE;

  if ( bbox_is_empty(&context->bandBounds) )
    return TRUE;

  for (;;) {
    Backdrop *backdrop;

    for ( backdrop = shared->pageBackdrops; backdrop != NULL;
          backdrop = backdrop->nextPage ) {
      /* Finished with the results of the compositing (they've already
         been rendered), trim the blocks in the band.  This will free
         retained blocks and return any resources to the global list. */
      if ( !bd_regionReclaim(backdrop, &context->bandBounds,
                             TRUE /* bandclose */, canKeep) )
        result = FALSE ;
    }
    if ( result || !canKeep )
      break ;
    /* Do again if result changed to false and we weren't trimming. */
    canKeep = FALSE ;
  }

#if defined(METRICS_BUILD) && defined(DEBUG_BUILD)
  if ( (backdropDebug & BD_DEBUG_MEMORY) != 0 )
    monitorf((uint8*)"Composite bounds (%d, %d, %d, %d) completed; "
             "%u blocks on disk and %Pu bytes to disk\n",
             context->bandBounds.x1, context->bandBounds.y1,
             context->bandBounds.x2, context->bandBounds.y2,
             shared->nBlocksToDisk, shared->nBytesToDisk);
#endif

  return result;
}

/**
 * Returns the output set of colorants in the backdrop.  nComps is optional.
 */
COLORANTINDEX *bd_backdropColorants(const Backdrop *backdrop, uint32 *nComps)
{
  if ( nComps )
    *nComps = backdrop->outComps;
  return backdrop->outColorants;
}

/**
 * Specifies an area for backdrop reading which must be a subset of the current
 * region set.
 */
void bd_readerInit(CompositeContext *context, const dbbox_t *readBounds)
{
  BackdropReader init = {0}, *reader = &context->reader;

  *reader = init;
  if ( bbox_is_empty(readBounds) )
    /* Do something sensible for a daft case. */
    reader->moreBlocks = FALSE;
  else {
    BlockIterator iterInit = BLOCKITERATOR_INIT;

    reader->readBounds = *readBounds;
    reader->blockIterator = iterInit;
    reader->moreBlocks = TRUE;
  }
}

/**
 * bd_readerNext iterates over backdrop blocks in the readBounds supplied to
 * bd_readerInit.
 */
Bool bd_readerNext(CompositeContext *context, const Backdrop *backdrop,
                   Bool *result)
{
  BackdropReader *reader = &context->reader;
  const BackdropShared *shared = backdrop->shared;
  uint32 bx, by;

  HQASSERT(reader->block == NULL || reader->blockFinish,
           "Haven't finished reading the last block");

  *result = TRUE;

  /* Check for a finished block. */
  if ( reader->block != NULL ) {
    if ( reader->wasLoaded ) {
      HQASSERT(bd_blockResource(reader->block) != NULL, "Resource missing");
      /* Release the resource used to load the block off disk. */
      if ( !bd_blockReclaim(backdrop, &reader->block, FALSE, TRUE) ) {
        *result = FALSE;
        return FALSE;
      }
      reader->wasLoaded = FALSE;
    }
    if ( reader->wasPurgeable ) {
      /* Reset purgeable back to previous state. */
      bd_setPurgeable(shared, reader->block, TRUE, NULL);
      reader->wasPurgeable = FALSE;
    }
  }
  HQASSERT(!reader->wasLoaded && !reader->wasPurgeable,
           "Reader block handling flags should all be clear now");

  if ( !reader->moreBlocks )
    /* Finished reading the data for readBounds. */
    return FALSE;

  /* Get the next block for reading. */
  reader->moreBlocks =
    bd_blockIterator(backdrop, &reader->readBounds, &reader->blockIterator);
  reader->block = *bd_iteratorLocateBlock(&reader->blockIterator, backdrop);
  reader->blockStart = TRUE;
  reader->blockFinish = FALSE;

  /* A missing or incomplete block is likely to indicate readBounds lies
     outside of the current region set. */
  HQASSERT(reader->block != NULL, "Must have a block now");
  HQASSERT(bd_isComplete(reader->block), "Block must be complete");

  /* The device coords of the top left pixel in this new block. */
  bd_iteratorPosition(&reader->blockIterator, &bx, &by);
  reader->xTopLeft = (dcoord)(bx * shared->blockWidth);
  reader->yTopLeft = (dcoord)((by / shared->blockRowsPerRegion) * shared->regionHeight
                              + (by % shared->blockRowsPerRegion) * shared->blockHeight);

  /* Stop the block being purged whilst it is being read. */
  bd_setPurgeable(shared, reader->block, FALSE, &reader->wasPurgeable);

  /* If the block is on disk find a resource and read the block back in.
     The resource needs releasing when the block has been fully read. */
  if ( !bd_isStorageMemory(reader->block) &&
       !bd_isUniform(reader->block) ) {
    if ( !bd_blockLoad(backdrop, context, bx, by, reader->block) ) {
      *result = FALSE;
      return FALSE;
    }
    reader->wasLoaded = TRUE;
  }
  return TRUE;
}

static low_mem_offer_t *bd_backdropPurgeSolicit(low_mem_handler_t *handler,
                                                corecontext_t *context,
                                                size_t count,
                                                memory_requirement_t *requests)
{
  static low_mem_offer_t offer;
  BackdropShared *shared = context->page->backdropShared;

  UNUSED_PARAM(low_mem_handler_t*, handler);
  UNUSED_PARAM(size_t, count);
  UNUSED_PARAM(memory_requirement_t*, requests);

  if ( shared == NULL || shared->retention == BD_RETAINNOTHING )
    return NULL;

  if ( !multi_mutex_trylock(&backdropLock) )
    return NULL; /* give up if can't get the lock */

  if ( shared->purgeableBlocks == NULL )
    offer.offer_size = 0;
  else {
    offer.pool = shared->dataPool;
    offer.offer_size = bd_blockPurgeSize(shared->purgeableBlocks);
    offer.offer_cost = BD_PURGE_COST_VALUE;
    offer.next = NULL;
  }

  multi_mutex_unlock(&backdropLock);
  return offer.offer_size > 0 ? &offer : NULL;
}

static Bool bd_backdropPurgeRelease(low_mem_handler_t *handler,
                                    corecontext_t *context,
                                    low_mem_offer_t *offer)
{
  BackdropShared *shared = context->page->backdropShared;
  Bool result = TRUE;

  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(low_mem_offer_t*, offer);

  HQASSERT(offer != NULL, "No offer");
  HQASSERT(offer->next == NULL, "Multiple offers");
  HQASSERT(shared != NULL, "No shared");

  if ( !multi_mutex_trylock(&backdropLock) )
    return TRUE; /* give up if can't get the lock */

  if ( shared->purgeableBlocks != NULL )
    result = bd_blockPurge(shared, &shared->purgeableBlocks);

  multi_mutex_unlock(&backdropLock);
  return result;
}

#ifdef METRICS_BUILD
static Bool bd_collateMetrics(resource_pool_t *pool,
                              resource_entry_t *entry,
                              void *data)
{
  CompositeMetrics *total = data;
  CompositeMetrics *next = entry->resource;

  UNUSED_PARAM(resource_pool_t*, pool);

  total->nUniformBlocks += next->nUniformBlocks;
  total->nRLELines += next->nRLELines;
  total->nRuns += next->nRuns;
  total->nMapLines += next->nMapLines;
  total->nPixels += next->nPixels;
  total->nDuplicateEntries += next->nDuplicateEntries;
  total->nPoachCandidates += next->nPoachCandidates;
  total->nPoachedBlocks += next->nPoachedBlocks;
  return TRUE;
}
#endif

static void bd_backdropStats(const BackdropShared *shared)
{
  if ( shared->pageBackdrops != NULL ) {
#ifdef METRICS_BUILD
    resource_pool_t *pool;
    CompositeMetrics total = {0};
    double compressedSizePercent = 0.0;
    Bool result;

    pool = resource_requirement_get_pool(shared->page->render_resources,
                                         TASK_RESOURCE_COMPOSITE_CONTEXT);
    if ( pool == NULL ) return;
    result = resource_pool_forall(pool, 0, bd_collateMetrics, &total);
    resource_pool_release(&pool);
    if ( !result ) return;

    /* 100 * ( Compressed size / uncompressed size )
       More convenient than compression rate because metrics can only be ints. */
    compressedSizePercent = 100.0 *
      ( (double)(total.nUniformBlocks + total.nRuns + total.nPixels) /
        (double)(shared->width * shared->height) );

    backdropMetrics.nBackdrops += shared->nBackdrops;
    backdropMetrics.nBlocks += shared->pageBackdrops->nBlocks;
    backdropMetrics.nBlocksToDisk += (uint32)shared->nBlocksToDisk;
    backdropMetrics.nBytesToDisk += (uint32)shared->nBytesToDisk;
    backdropMetrics.nDuplicateEntries += (uint32)total.nDuplicateEntries;
    backdropMetrics.compressedSizePercent += (uint32)compressedSizePercent;
#endif /* METRICS_BUILD */

#ifdef DEBUG_BUILD
    if ( (backdropDebug & BD_DEBUG_STATS) != 0 ) {
      static Bool allBackdrops = FALSE;

      monitorf((uint8*)"== Backdrop debug stats ==\n");
      monitorf((uint8*)"Number of backdrops: %u\n", shared->nBackdrops);

      /* Most of the time don't need traces for all the backdrops. */
      if ( allBackdrops ) {
        Backdrop *backdrop;

        for ( backdrop = shared->backdrops; backdrop; backdrop = backdrop->next ) {
          if ( backdrop->compositeToPage )
            monitorf((uint8*)"Page ");
          else if ( backdrop->softMaskType == LuminositySoftMask )
            monitorf((uint8*)"LuminositySoftMask ");
          else if ( backdrop->softMaskType == AlphaSoftMask )
            monitorf((uint8*)"AlphaSoftMask ");
          else
            monitorf((uint8*)"SubGroup ");
          monitorf((uint8*)"backdrop %p\n", backdrop);
        }
      }

      monitorf((uint8*)"Minimum backdrop resources %u; Bytes for min resources %u\n",
               shared->provisioned.nMinResources,
               shared->provisioned.nMinResources
               * bd_resourceSize(shared->nTables, shared->tableSize, shared->blockHeight));

#ifdef METRICS_BUILD
      monitorf((uint8*)"Blocks written to disk: %u of %u; Bytes to disk: %u\n",
               shared->nBlocksToDisk, shared->pageBackdrops->nBlocks,
               shared->nBytesToDisk);
      monitorf((uint8*)"Uniform blocks: %u\n", total.nUniformBlocks);
      monitorf((uint8*)"RLE lines %u; nRuns: %u\n", total.nRLELines, total.nRuns);
      monitorf((uint8*)"Map lines %u; nPixels: %u\n", total.nMapLines, total.nPixels);
      monitorf((uint8*)"Duplicate entries: %u\n", total.nDuplicateEntries);
      monitorf((uint8*)"Poached %u blocks out of %u\n",
               total.nPoachedBlocks, total.nPoachCandidates);
      monitorf((uint8*)"Compressed size: %f%%; Compression ratio: %f to 1\n",
               compressedSizePercent, 100 / compressedSizePercent);
      monitorf((uint8*)"== end of stats ==\n");
#endif /* METRICS_BUILD */
    }
#endif /* DEBUG_BUILD */
  }
}

void backdrop_C_globals(core_init_fns *fns)
{
  bd_init_C_globals();

  fns->swinit = bd_swinit;
  fns->swstart = bd_swstart;
  fns->finish = bd_finish;

  bd_contextGlobals();
  bd_resourceGlobals();
}

/* Log stripped */
