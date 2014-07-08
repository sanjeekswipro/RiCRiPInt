/** \file
 * \ingroup images
 *
 * $HopeName: SWv20!src:imstore.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS image storage implementation
 */

#include "core.h"
#include "coreinit.h"

#include "devices.h"            /* find_device */
#include "display.h"            /* DL_LASTBAND */
#include "graphics.h"           /* theY1Clip */
#include "imageo.h"             /* IMAGEOBJECT */
#include "jobmetrics.h"         /* JobStats */
#include "mlock.h"              /* multi_mutex_* */
#include "swtrace.h"
#include "mm.h"
#include "mmcompat.h"           /* mm_alloc_static */
#include "monitor.h"            /* monitorf */
#include "namedef_.h"           /* NAME_debug_imstore */
#include "often.h"              /* SwOftenUnsafe */
#include "params.h"             /* SystemParams */
#include "render.h"             /* inputpage */
#include "ripdebug.h"           /* register_ripvar */
#include "ripmulti.h"           /* IS_INTERPRETER */
#include "swcopyf.h"            /* swcopyf */
#include "swerrors.h"           /* VMERROR */
#include "lowmem.h"             /* low_mem_offer_t */
#include "dl_image.h"           /* image_dbbox_covering_ibox */

#include "imb32.h"              /* imb32_init */
#include "imblist.h"            /* blist_init */
#include "imblock.h"            /* im_blockinit */
#include "imfile.h"             /* im_filedestroy */
#include "imstore_priv.h"


#if defined(DEBUG_BUILD)
int32 debug_imstore = 0;
#endif

/**
 * Lock to prevent simultaneous access to the shared image-store lists.
 *
 * All of the threads share image-store data through the IM_SHARED structure
 * in the page pl state. Use of this mutex ensures only one thread at a time
 * is allowed to access or update this shared information.
 */
multi_mutex_t im_mutex;


static int32 im_storePurgeSome(IM_STORE *ims, int32 purgeLimit);

static void im_updatePlanes(IM_STORE *ims, IM_PLANE **planes, int16 nplanes);
static IM_STORE_NODE *im_storeNodeCreate(IM_SHARED *im_shared, int32 abytes);
static void im_setblocksize(IM_STORE *ims, const ibbox_t *imsbbox, int32 bpp);

#ifdef ASSERT_BUILD
static void im_checknode(IM_STORE_NODE *imsNode, IM_STORE *ims, int32 action);
static void im_checkAllStores(IM_SHARED *im_shared);
#else
#define im_checknode(imsNode, ims, action)    EMPTY_STATEMENT()
#define im_checkAllStores(im_shared)    EMPTY_STATEMENT()
#endif
static void reportStore(IM_STORE *ims, char *name, int32 bx, int32 by);
static void im_destroy_blockpools(IM_SHARED *im_shared);

static inline Bool ims_typeof(IM_STORE *ims, uint32 typ)
{
  return ((ims->flags & typ) != 0);
}

/**
 * Start the image store module for this page.
 */
Bool im_shared_start(DL_STATE *page)
{
  IM_SHARED *im_shared, init = {0};

  HQASSERT(page->im_shared == NULL, "im_shared already exists");

  im_shared = dl_alloc(page->dlpools, sizeof(IM_SHARED),
                       MM_ALLOC_CLASS_IMAGE);
  if ( im_shared == NULL )
    return error_handler(VMERROR);

  *im_shared = init;

  page->im_shared = im_shared;

  im_shared->page = page;

  if ( !im_blist_start(im_shared) ||
       !im_filecreatectxt(&im_shared->imfile_ctxt, TRUE) ) {
    im_shared_finish(page);
    return FALSE;
  }

  return TRUE;
}

/**
 * Finish the image store module for this page.
 */
void im_shared_finish(DL_STATE *page)
{
  IM_SHARED *im_shared = page->im_shared;

  if ( im_shared == NULL )
    return;

  im_destroy_blockpools(im_shared);

  im_blist_finish(im_shared);

  im_filedestroyctxt(&im_shared->imfile_ctxt);

  reportStore(NULL, "finish", -1, -1);

  dl_free(page->dlpools, im_shared, sizeof(IM_SHARED), MM_ALLOC_CLASS_IMAGE);
  page->im_shared = NULL;
}

Bool im_shared_filecloseall(IM_SHARED *im_shared)
{
  return im_filecloseall(im_shared->imfile_ctxt);
}

/**
 * Function reporting we have had incomplete image data.
 * Put it near the top of the file to stop the line number
 * moving and upsetting regressions.
 */
static void incomplete_image_data(void)
{
  /* some parts of the rip will expect to have incomplete images,
   * striping and clipped image optimisation in particular. Allow
   * this warning to be quietened down in those parts.
   * A rip var will allow us to force the warning.
   */
#ifdef DEBUG_BUILD
  if ((debug_imstore & IMSDBG_REPORT_INCOMPLETE) != 0)
    monitorf(( uint8 *) "Image with incomplete data");
#endif /* DEBUG_BUILD */
}

/**
 * Which pool should we allocate a block of the given size from?
 */
static inline mm_pool_t im_datapool(IM_SHARED *im_shared, size_t abytes)
{
  if (abytes == IM_BLOCK_DEFAULT_SIZE)
    return im_shared->mm_pool_imbfix;
  else
    return im_shared->mm_pool_imbvar;
}

mm_addr_t im_dataalloc(IM_SHARED *im_shared, size_t abytes, mm_cost_t cost)
{
  return mm_alloc_cost(im_datapool(im_shared, abytes), abytes, cost,
                       MM_ALLOC_CLASS_IMAGE_DATA);
}

void im_datafree(IM_SHARED *im_shared, void *data, size_t abytes)
{
  HQASSERT(data != NULL, "data is NULL");
  mm_free(im_datapool(im_shared, abytes), data, abytes);
}

#ifdef METRICS_BUILD


struct im_store_metrics {
  size_t imbfix_pool_max_size;
  int32 imbfix_pool_max_objects;
  size_t imbfix_pool_max_frag;
  size_t imbvar_pool_max_size;
  int32 imbvar_pool_max_objects;
  size_t imbvar_pool_max_frag;
} im_store_metrics;

static Bool im_store_metrics_update(sw_metrics_group *metrics)
{
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("MM")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("ImageBlockFixed")) )
    return FALSE;
  SW_METRIC_INTEGER("PeakPoolSize",
                    (int32)im_store_metrics.imbfix_pool_max_size);
  SW_METRIC_INTEGER("PeakPoolObjects",
                    im_store_metrics.imbfix_pool_max_objects);
  SW_METRIC_INTEGER("PeakPoolFragmentation",
                    (int32)im_store_metrics.imbfix_pool_max_frag);
  sw_metrics_close_group(&metrics);
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("ImageBlockVariable")) )
    return FALSE;
  SW_METRIC_INTEGER("PeakPoolSize",
                    (int32)im_store_metrics.imbvar_pool_max_size);
  SW_METRIC_INTEGER("PeakPoolObjects",
                    im_store_metrics.imbvar_pool_max_objects);
  SW_METRIC_INTEGER("PeakPoolFragmentation",
                    (int32)im_store_metrics.imbvar_pool_max_frag);
  sw_metrics_close_group(&metrics);
  sw_metrics_close_group(&metrics);
  return TRUE;
}

static void im_store_metrics_reset(int reason)
{
  struct im_store_metrics init = { 0 };

  UNUSED_PARAM(int, reason);
  im_store_metrics = init;
}

static sw_metrics_callbacks im_store_metrics_hook = {
  im_store_metrics_update,
  im_store_metrics_reset,
  NULL
};

/**
 * Track peak memory allocated in fixed pool.
 */
static void im_jobstats_fix(IM_SHARED *im_shared)
{
  size_t max_size = 0, max_frag = 0;
  int32 max_objects;
  mm_pool_t pool;

  pool = im_shared->mm_pool_imbfix;

  mm_debug_total_highest(pool, &max_size, &max_objects, &max_frag);

  if (im_store_metrics.imbfix_pool_max_size < max_size) {
    im_store_metrics.imbfix_pool_max_size = max_size;
    im_store_metrics.imbfix_pool_max_objects = max_objects;
  }
  if (im_store_metrics.imbfix_pool_max_frag < max_frag)
    im_store_metrics.imbfix_pool_max_frag = max_frag;
}

/**
 * Track peak memory allocated in variable pool.
 */
static void im_jobstats_var(IM_SHARED *im_shared)
{
  size_t max_size = 0, max_frag = 0;
  int32 max_objects;
  mm_pool_t pool;

  pool = im_shared->mm_pool_imbvar;

  mm_debug_total_highest(pool, &max_size, &max_objects, &max_frag);

  if ( im_store_metrics.imbvar_pool_max_size < max_size) {
    im_store_metrics.imbvar_pool_max_size = max_size;
    im_store_metrics.imbvar_pool_max_objects = max_objects;
  }
  if (im_store_metrics.imbvar_pool_max_frag < max_frag)
    im_store_metrics.imbvar_pool_max_frag = max_frag;
}
#endif /* METRICS_BUILD */

/**
 * Destroy the memory pools that were used by image storage objects
 */
static void im_destroy_blockpools(IM_SHARED *im_shared)
{
  /* Destroy image block pools (if created) */
  if ( im_shared->mm_pool_imbfix ) {
#ifdef METRICS_BUILD
    im_jobstats_fix(im_shared);
#endif
    mm_pool_destroy(im_shared->mm_pool_imbfix);
    im_shared->mm_pool_imbfix = NULL;
  }
  if ( im_shared->mm_pool_imbvar ) {
#ifdef METRICS_BUILD
    im_jobstats_var(im_shared);
#endif
    mm_pool_destroy(im_shared->mm_pool_imbvar);
    im_shared->mm_pool_imbvar = NULL;
  }
}

/** Create the memory pools that will be used by image storage objects.
 */
static Bool im_create_blockpools(IM_SHARED *im_shared)
{
  /* If we haven't already encountered an image, then create mm pools for image
     blocks. */
  if ( im_shared->mm_pool_imbfix != NULL ) {
    HQASSERT(im_shared->mm_pool_imbvar != NULL,
             "imbfix created without imbvar");
    return TRUE;
  }
  HQASSERT(im_shared->mm_pool_imbvar == NULL,
           "imbvar created without imbfix");

  if ( mm_pool_create(&im_shared->mm_pool_imbfix, IMBFIX_POOL_TYPE,
                      (size_t)1 * IM_BLOCK_DEFAULT_SIZE,
                      (size_t)IM_BLOCK_DEFAULT_SIZE / 1,
                      (size_t)IM_BLOCK_MIN) != MM_SUCCESS )
    return error_handler(VMERROR);

  if ( mm_pool_create(&im_shared->mm_pool_imbvar, IMBVAR_POOL_TYPE,
                      (size_t)1 * IM_BLOCK_DEFAULT_SIZE,
                      (size_t)IM_BLOCK_DEFAULT_SIZE / 4,
                      (size_t)IM_BLOCK_VAR_MIN) != MM_SUCCESS ) {
    im_destroy_blockpools(im_shared);
    return error_handler(VMERROR);
  }
  return TRUE;
}

/** Solicit method of the imstore low-memory handler.
 *
 * This is required to be quick to call. The old algorithm did a
 * self-throttling descent down the various lists of blocks, which would
 * be too slow for the solicit to step through. So just make an estimate
 * based on the number of blocks in the various shared lists, assuming all
 * blocks are of the default size.
 */
static low_mem_offer_t *imstore_lowmem_solicit(low_mem_handler_t *handler,
                                               corecontext_t *context,
                                               size_t count,
                                               memory_requirement_t* requests)
{
  static low_mem_offer_t offer;
  DL_STATE *page;
  IM_SHARED *im_shared;
  int32 nblocks = 0;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  HQASSERT(requests != NULL, "No requests");
  UNUSED_PARAM(size_t, count);
  UNUSED_PARAM(memory_requirement_t*, requests);

  page = context->page;
  HQASSERT(page, "No DL page");
  if ( !page->im_purge_allowed )
    return NULL;

  im_shared = page->im_shared;
  if ( im_shared == NULL )
    return NULL;

  /* bail-out early if purging is not allowed */

  if ( !im_shared->page->imageParams.LowMemImagePurgeToDisk &&
       handler->tier == memory_tier_disk )
    return NULL;
  if ( !im_shared->page->imageParams.CompressImageSource &&
       handler->tier == memory_tier_ram )
    return NULL;

  /* Must lock, to prevent livelock between im_linkims() and this handler. */
  if ( !multi_mutex_trylock(&im_mutex) )
    return NULL; /* give up if can't get the lock */

  if ( handler->tier == memory_tier_ram )
    nblocks = im_shared->im_nBlocks[IM_ACTION_COMPRESSION];
  if ( handler->tier == memory_tier_disk )
    nblocks += im_shared->im_nBlocks[IM_ACTION_WRITETODISK];

  multi_mutex_unlock(&im_mutex);

  if ( nblocks > 0 ) {
    offer.pool = im_shared->mm_pool_imbfix;
    if ( handler->tier == memory_tier_disk )
      offer.offer_size = IM_BLOCK_DEFAULT_SIZE * nblocks;
    else
      offer.offer_size = IM_BLOCK_DEFAULT_SIZE * nblocks/2;
    offer.offer_cost = 1.0;
    offer.next = NULL;
  }

#ifdef DEBUG_BUILD
  if ( debug_imstore & IMSDBG_LOWMEM )
    monitorf((uint8 *)"imstore:solicit %d kbytes for %s\n", nblocks == 0 ? 0 :
      offer.offer_size/1024, handler->tier == memory_tier_disk ? "disk":"comp");
#endif

  return nblocks == 0 ? NULL : &offer;
}


#ifdef DEBUG_BUILD
/** Print debug info about all image stores we have kept track of. */
static void ims_reportall(IM_SHARED *im_shared)
{
  if ( debug_imstore & IMSDBG_REPORT ) {
    IM_STORE_NODE *node;
    monitorf((uint8*)"==== All image stores... ====\n");
    for ( node = im_shared->im_list; node != NULL; node = node->next ) {
      uint8 action;
      for ( action = IM_ACTION_OPEN_FOR_WRITING;
            action != IM_NUM_ACTIONS; action++ ) {
        IM_STORE *ims;
        for ( ims = node->ims[action]; ims != NULL ; ims = ims->next ) {
          reportStore(ims, "Allstores", -1, -1);
        }
      }
    }
    monitorf((uint8*)"==== End all image stores ====\n");
  }
}
#endif


static IM_STORE *im_purgelocatelargest(IM_SHARED *im_shared, uint8 action);


/** Release method of the imstore low-memory handler. */
static Bool imstore_lowmem_release(low_mem_handler_t *handler,
                                   corecontext_t *context,
                                   low_mem_offer_t *offer)
{
  DL_STATE *page;
  Bool result = FALSE;
  int32 purgedBlocks = 0;
  IM_STORE *ims;
  IM_SHARED *im_shared;
  int32 maxBlocks;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  HQASSERT(offer != NULL, "No offer");
  HQASSERT(offer->next == NULL, "Multiple offers");

  page = context->page;
  HQASSERT(page, "No DL page");

  im_shared = page->im_shared;
  if ( im_shared == NULL )
    return TRUE;

#ifdef DEBUG_BUILD
  if ( debug_imstore & IMSDBG_LOWMEM )
    monitorf((uint8 *)"imstore:release %d kbytes for %s\n",
      offer->taken_size/1024,
      handler->tier == memory_tier_disk ? "disk":"comp");
#endif

  /* In order to make the code thread-safe, there is a high-level mutex
     restricting access to any of the shared data structures (the various lists
     hanging off im_shared). This is not very fine-grained, but should not
     matter for so gross an action as image purging. */
  if ( !multi_mutex_trylock(&im_mutex) )
    return TRUE; /* give up if can't get the lock */

  maxBlocks = (int32)max(offer->taken_size/IM_BLOCK_DEFAULT_SIZE, 1);

  if ( handler->tier == memory_tier_ram ) {
    while ( purgedBlocks < maxBlocks) {
      ims = im_purgelocatelargest(im_shared, IM_ACTION_COMPRESSION);
      if (ims == NULL)
        break;
      HQASSERT(ims->abytes >= IM_MIN_COMPRESSION_SIZE,
               "Compressing a block too small to be efficient");
      purgedBlocks += im_storePurgeSome(ims, maxBlocks - purgedBlocks);
      result = result || purgedBlocks > 0;
    }
  }

  if ( im_shared->im_nStores[IM_ACTION_SHAREBLISTS_1] >
       im_shared->im_nStoresTotal * IM_MAX_PURGED_STORE_FRACTION ) {
    blist_purgeGlobal(im_shared);
  }
  if ( blist_toomany(im_shared, IM_MAX_BLISTS_TO_PURGE) ) {
    int32 purgedData = im_purgeglobal(im_shared);
    result = result || purgedData;
  }

  if ( handler->tier == memory_tier_disk ) {
    while ( purgedBlocks < maxBlocks ) {
      int nb;
      ims = im_purgelocatelargest(im_shared, IM_ACTION_WRITETODISK);
      if (ims == NULL)
        break;
      HQASSERT(ims->abytes >= IM_MIN_WRITETODISK_SIZE,
               "Writing to disk a block too small to be efficient");
      nb = im_storePurgeSome(ims, maxBlocks - purgedBlocks);
      purgedBlocks += nb;
      result = result || nb > 0;
    }
  }
  if (!result) {
    blist_purgeGlobal(im_shared);
    result = im_purgeglobal(im_shared);
  }

#ifdef DEBUG_BUILD
  if ( !result )
    ims_reportall(im_shared);

  if ( debug_imstore & IMSDBG_LOWMEM )
    monitorf((uint8 *)"imstore:released %d kbytes\n",
        purgedBlocks * IM_BLOCK_DEFAULT_SIZE/1024);
#endif

  multi_mutex_unlock(&im_mutex);
  return TRUE;
}

/** The imstore compress low-memory handler. */
static low_mem_handler_t im_comp_lowmem_handler = {
  "Image store compress",
  memory_tier_ram, imstore_lowmem_solicit, imstore_lowmem_release, TRUE,
  0, FALSE
};

/** The imstore to-disk low-memory handler. */
static low_mem_handler_t im_disk_lowmem_handler = {
  "Image store to disk",
  memory_tier_disk, imstore_lowmem_solicit, imstore_lowmem_release, TRUE,
  0, FALSE
};


static Bool im_store_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

#if defined(DEBUG_BUILD)
  register_ripvar(NAME_debug_imstore, OINTEGER, &debug_imstore);
#endif
  multi_mutex_init(&im_mutex, IMSTORE_LOCK_INDEX, FALSE,
                   SW_TRACE_IM_STORE_ACQUIRE, SW_TRACE_IM_STORE_HOLD);
  if ( !low_mem_handler_register(&im_comp_lowmem_handler)
       || !low_mem_handler_register(&im_disk_lowmem_handler) ) {
    multi_mutex_finish(&im_mutex) ;
    return FALSE ;
  }

  return TRUE ;
}

static void im_store_finish(void)
{
  multi_mutex_finish(&im_mutex);
  low_mem_handler_deregister(&im_comp_lowmem_handler);
  low_mem_handler_deregister(&im_disk_lowmem_handler);
}

void im_store_C_globals(core_init_fns *fns)
{
  fns->swstart = im_store_swstart;
  fns->finish  = im_store_finish;
}


/**
 * Image store open has failed, so raise a VMERROR if required, free
 * the partially created imstore data structure, and return NULL
 */
static IM_STORE *im_storefail(IM_STORE *ims, Bool vmerr)
{
  if ( vmerr )
    (void)error_handler(VMERROR);

  if ( ims->swapmem != NULL )
    im_datafree(ims->im_shared, ims->swapmem, ims->abytes);

  if (ims->planes != NULL) {
    int32 i;

    for ( i = 0; i < ims->nplanes; ++i ) {
      if ( ims->planes[i] != NULL )
        im_planefree(ims, i);
    }
    dl_free(ims->im_shared->page->dlpools, (mm_addr_t)ims->planes,
            ims->nplanes * sizeof(IM_PLANE *), MM_ALLOC_CLASS_IMAGE_DATA);
  }

  if ( ims->row_repeats != NULL ) {
    int32 ysize = ims->obbox.y2 - ims->obbox.y1 + 1;
    dl_free(ims->im_shared->page->dlpools, ims->row_repeats,
            BITVECTOR_SIZE_BYTES(ysize), MM_ALLOC_CLASS_IMAGE_DATA);
  }

  dl_free(ims->im_shared->page->dlpools, (mm_addr_t)ims, sizeof(IM_STORE),
          MM_ALLOC_CLASS_IMAGE_DATA);
  return NULL;
}

#ifdef DEBUG_BUILD
/**
 * Debug funtion to return a string describing the given action
 */
static char *ims_action(uint8 action)
{
  switch ( action ) {
  case IM_ACTION_OPEN_FOR_WRITING:
    return "Open for Writing";
  case IM_ACTION_COMPRESSION:
    return "Compressing";
  case IM_ACTION_SHAREBLISTS_1:
    return "Share Blists 1";
  case IM_ACTION_WRITETODISK:
    return "Writing to disk";
  case IM_ACTION_SHAREBLISTS_2:
    return "Share Blists 2";
  case IM_ACTION_NOTHINGMORE:
    return "Nothing more";
  default:
    return "Unknown action ?";
  }
}

#endif /* DEBUG_BUILD */

/**
 * Write out a pretty text graphic to the monitor illustrating the storage
 * of each block in a plane. In order to use this, it will be best to change
 * 'nplanes' in a debugger on occassions when the illustration is required
 * because it is far too verbose to use all the time. It is though,
 * very useful.
 * \param[in] ims       The image store object
 * \param[in] name      The name to be printed
 * \param[in] bx        x index of block being processed
 * \param[in] by        y index of block being processed
 */
static void reportStore(IM_STORE *ims, char *name, int32 bx, int32 by)
{
#ifdef DEBUG_BUILD
  if ( debug_imstore & IMSDBG_REPORT ) {
    char xystr[32] = { '\0' };

    if ( bx >= 0 && by >= 0 )
      swcopyf((uint8 *)xystr, (uint8 *)"(%d,%d)", bx, by);

    monitorf((uint8*)"== imstore@0x%08x[%s%s] ==\n", ims, name, xystr);

    if ( ims != NULL ) {
      int32 planei;

      monitorf((uint8*)"%d/%d/(%d,%d-%d,%d)/%d*%d/%d+%d/%s\n",
               ims->bpp, ims->nplanes,
               ims->obbox.x1, ims->obbox.y1, ims->obbox.x2, ims->obbox.y2,
               ims->xblock, ims->yblock,
               ims->stdblocks, ims->extblocks, ims_action(ims->action));

      for ( planei = 0; planei < ims->nplanes; ++planei ) {
        if ( ims->planes[planei] != NULL )
          im_blockreport(ims, ims->planes[planei]);
        monitorf((uint8*)"====\n");
      }
      blist_globalreport(ims->im_shared);
    }
    monitorf((uint8*)"== End ==\n");
  }
#else
  UNUSED_PARAM(IM_STORE *, ims);
  UNUSED_PARAM(char *, name);
  UNUSED_PARAM(int32 , bx);
  UNUSED_PARAM(int32 , by);
#endif
}

/**
 * Create a new image store with the given characteristics
 */
IM_STORE *im_storeopen(IM_SHARED *im_shared, const ibbox_t *imsbbox,
                       int32 nplanes, int32 bpp, uint32 flags)
{
  int32 i, ysize;
  IM_STORE *ims;
  IM_PLANE **planes;

  HQASSERT(imsbbox != NULL, "No image space bbox for store");
  HQASSERT(imsbbox->x2 >= imsbbox->x1, "xsize should be > 0");
  HQASSERT(imsbbox->y2 >= imsbbox->y1, "ysize should be > 0");
  HQASSERT(nplanes > 0, "nplanes should be > 0");
  HQASSERT(bpp > 0, "bpp should be > 0");
  HQASSERT(bpp == 1 || bpp == 2 || bpp == 4 || bpp == 8 || bpp == 16 ||
           bpp == 32, "Unknown bpp");

  /* This call only creates pools for the first image of the page. They get
     destroyed in erase page handling - don't destroy them in error handling
     in this function. */
  if ( !im_create_blockpools(im_shared) )
    return FALSE;

  ims = dl_alloc(im_shared->page->dlpools, sizeof(IM_STORE),
                 MM_ALLOC_CLASS_IMAGE_DATA);
  if ( ims == NULL ) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  ims->im_shared = im_shared;
  ims->flags  = flags;
  ims->swapmem = NULL;
  ims->action = IM_ACTION_NOTHINGMORE; /* Until we've written some data  */
  ims->nplanes = CAST_SIGNED_TO_INT16(nplanes);
  ims->planes = NULL;
  ims->stdblocks = 0;
  ims->extblocks = 0;
  ims->reserves = NULL;
  ims->row_repeats = NULL;

  ims->blockWidth = IM_BLOCK_DEFAULT_WIDTH;
  ims->blockHeight = IM_BLOCK_DEFAULT_HEIGHT;
  im_setblocksize(ims, imsbbox, bpp);
  ims->abytes = im_blockDefaultAbytes(ims);

  ims->purged = FALSE;
  ims->blistPurgeRow = FALSE;

  ims->band = DL_LASTBAND;
  ims->next = NULL;
  ims->prev = NULL;

  if ( ims_typeof(ims, IMS_XYSWAP) ) {
    ims->swapmem = im_dataalloc(im_shared, ims->abytes, mm_cost_normal);
    if ( ims->swapmem == NULL )
      return im_storefail(ims, TRUE);
  }

  planes = dl_alloc(im_shared->page->dlpools, nplanes * sizeof(IM_PLANE *),
                    MM_ALLOC_CLASS_IMAGE_DATA );
  if ( planes == NULL )
    return im_storefail(ims, TRUE);
  ims->planes = planes;

  for ( i = 0; i < nplanes; ++i )
    planes[ i ] = NULL;

  ysize = ims->obbox.y2 - ims->obbox.y1 + 1;
  ims->row_repeats = dl_alloc(im_shared->page->dlpools,
                              BITVECTOR_SIZE_BYTES(ysize),
                              MM_ALLOC_CLASS_IMAGE_DATA);
  if ( ims->row_repeats == NULL )
    return im_storefail(ims, TRUE);

  /* Start off with rows marked as repeats, but clear the first row in each
     block to isolate testing into rows of blocks. More rows will be cleared
     as data is tested when completing blocks. */
  BITVECTOR_SET_ELEMENTS(ims->row_repeats, ysize, BITVECTOR_ELEMENT_ONES);
  for ( i = 0; i < ysize; i += ims->blockHeight ) {
    BITVECTOR_CLEAR(ims->row_repeats, i);
  }

  multi_mutex_lock(&im_mutex);
  if ( !im_linkims(ims, IM_ACTION_OPEN_FOR_WRITING) ) {
    multi_mutex_unlock(&im_mutex);
    return im_storefail(ims, FALSE);
  }
  multi_mutex_unlock(&im_mutex);
  ims->openForWriting = TRUE;

  reportStore(ims, "open", -1, -1);
  return ims;
}

/**
 * Pre-allocate blocks for the specified plane of the given image store
 * We always try and pre-allocate the first row of blocks.
 */
Bool im_storeprealloc(IM_STORE *ims, int32 planei, int32 nconv)
{
  int32 bb, bx= 0, by = 0, nplanes;
  Bool xyswap = ims_typeof(ims, IMS_XYSWAP);

  /* Note that the data is always (must be) planar (on input). */
  HQASSERT(ims, "im_storeprealloc: ims NULL");
  HQASSERT(nconv > 0, "im_storeprealloc: nconv should only be > 0");

  bb = im_bb_get_and_check(ims, 0, 0, planei);
  nconv = ((nconv << (ims->bpps)) + 7) >> 3;
  nplanes = (planei == IMS_ALLPLANES ? ims->nplanes : planei);

  do {
    int32 tplanei = (planei == IMS_ALLPLANES ? 0 : planei);
    int32 bytes;

    do {
      if ( (bytes = im_blockprealloc(ims, tplanei, bb, bx, by)) < 0 )
        return FALSE;
      HQASSERT(bytes <= nconv, "bytes out of range of nconv");
    } while ((++tplanei) < nplanes );

    xyswap ? ++by : ++bx;
    bb += ( xyswap ? ims->xblock : 1);
    nconv -= bytes;
  } while ( nconv > 0 );

  reportStore(ims, "prealloc", -1, -1);
  return TRUE;
}

Bool im_storewrite(IM_STORE *ims, int32 col, int32 row, int32 planei,
                   uint8 *buf, int32 wbytes)
{
  Bool xyswap = ims_typeof(ims, IMS_XYSWAP);
  int32 bb, bx, by, x, y;
  int32 offset;

  HQASSERT(ims && buf && wbytes > 0, "NULL imagestore write");
  HQASSERT(ims->openForWriting, "writing image when not allowed");
  HQASSERT(((col - (xyswap ? ims->obbox.y1 : ims->obbox.x1)) % ims->blockWidth) == 0,
           "Column must be at the start of a block");

  /* Coordinates are pre-swapped space. */
  if ( xyswap ) {
    x = row ; y = col ;
  } else {
    x = col ; y = row ;
  }

  HQASSERT(x >= ims->obbox.x1 && x <= ims->obbox.x2 &&
           y >= ims->obbox.y1 && y <= ims->obbox.y2,
           "Writing outside image store bounds");
  x -= ims->obbox.x1 ;
  y -= ims->obbox.y1 ;

  x = ( x << (ims->bpps) ) >> 3;
  offset = wbytes = ((wbytes << ims->bpps) + 7 ) >> 3;

  bx = im_index_bx(ims, x);
  by = im_index_by(ims, y);
  bb = im_bb_get_and_check(ims, bx, by, planei);

  do {
    int32 written = 0;

    if ( planei != IMS_ALLPLANES ) {
      written = im_blockwrite(ims, planei, bb, bx, by, buf);
      if ( written < 0 )
        return FAILURE(FALSE) ;
    } else {
      int32 pi;

      for ( pi = 0; pi < ims->nplanes; pi++ ) {
        written = im_blockwrite(ims, pi, bb, bx, by, buf + pi*offset);
        if ( written < 0 )
          return FAILURE(FALSE) ;
      }
    }
    HQASSERT(written <= wbytes, "written more bytes than imstore will hold");
    buf += written;

    wbytes -= written;

    if ( wbytes > 0 ) { /* Still some to write, move to next block */
      xyswap ? ++by : ++bx;
      bb += (xyswap ? ims->xblock : 1);
    }
  } while ( wbytes > 0 );

  if ( im_blockdone(ims, planei, bb) ) {
    reportStore(ims, "write", bx, by);

    /* If we are already in low memory, and we filled-up the last block,
     * then that implies the entire row is full. So purge what we have
     * just written as far as is possible.
     */
    if ( ims->blistPurgeRow )
      (void)im_storepurgeOne(ims);
  }
  return TRUE;
}

Bool im_storeread(IM_STORE *ims, int32 x, int32 y, int32 planei,
                  uint8 **rbuf, int32 *rpixels)
{
  int32 bb, bx, by;

  HQASSERT(ims != NULL, "No image store to read") ;
  HQASSERT(rbuf && rpixels, "null args to imstore read function");

  HQASSERT(x >= ims->tbbox.x1 && x <= ims->tbbox.x2 &&
           y >= ims->tbbox.y1 && y <= ims->tbbox.y2,
           "Image store read out of trimmed area") ;
  x -= ims->obbox.x1 ;
  y -= ims->obbox.y1 ;

  x = ( x << (ims->bpps) ) >> 3;
  bx = im_index_bx(ims, x);
  by = im_index_by(ims, y);

  bb = im_bb_get_and_check(ims, bx, by, planei);

  /* reportStore(ims, "read", bx, by); */
  return im_blockaddr(ims, planei, bb, bx, x, y, rbuf, rpixels);
}

int32 im_storeread_nrows(IM_STORE *ims, int32 y)
{
  int32 nrows = 1;

  HQASSERT(y >= ims->obbox.y1 && y <= ims->obbox.y2, "y out of range");

  if ( ims->row_repeats != NULL ) {
    while ( y < ims->obbox.y2 &&
            BITVECTOR_IS_SET(ims->row_repeats, y - ims->obbox.y1 + 1) ) {
      y++;
      nrows++;
    }
  }
  return nrows;
}

Bool im_is_row_repeat(IM_STORE *ims, int32 y)
{
  HQASSERT(y >= ims->obbox.y1 && y <= ims->obbox.y2, "y out of range");
  return ims->row_repeats != NULL &&
         BITVECTOR_IS_SET(ims->row_repeats, y - ims->obbox.y1);
}

/**
 * For recombination:
 * Used to determine if a colorant has image data or is a knockout
 */
Bool im_storeplaneexists(const IM_STORE *ims, int32 plane)
{
  HQASSERT(ims != NULL, "im_storeplaneexists: ims NULL");
  HQASSERT(plane >= 0, "im_storeplaneexists: plane < 0");
  HQASSERT(ims->planes != NULL, "im_storeplaneexists: ims->planes NULL");
  return plane < ims->nplanes && ims->planes[plane] != NULL;
}

int32 im_storebpp(const IM_STORE *ims )
{
  HQASSERT(ims != NULL, "im_storebpp: ims NULL");
  HQASSERT(ims->bpp ==  1 || ims->bpp ==  2 || ims->bpp ==  4 ||
           ims->bpp ==  8 || ims->bpp == 16,
           "im_storebpp: bpp not one of 1,2,4,8,16" );
  return ims->bpp;
}

/**
 * Used for recombine. Reorders the planes in the image store so that they
 * are mapped according to the supplied colorant index mapping.
 */
Bool im_storereorder(IM_STORE *ims, const COLORANTINDEX current[],
                     const COLORANTINDEX order[], int32 nplanes)
{
  IM_PLANE** planes;
  int32 i;

  HQASSERT(ims, "ims is null");
  HQASSERT(ims->planes, "ims->planes is null");
  HQASSERT(order, "order array is missing");
  HQASSERT(nplanes > 0 && nplanes <= ims->nplanes, "nplanes is invalid");

  /* Create a new planes array to hold the re-ordered list. */
  planes = dl_alloc(ims->im_shared->page->dlpools, nplanes * sizeof(IM_PLANE*),
                    MM_ALLOC_CLASS_IMAGE_DATA);
  if (planes == NULL)
    return error_handler(VMERROR);

  for (i = 0; i < nplanes; ++i) {
    int32 j;
    planes[i] = NULL ;
    for (j = 0; j < ims->nplanes; ++j) {
      if (current[j] == order[i]) {
        /* Found colorant for this slot */
        planes[i] = ims->planes[j] ;
        ims->planes[j] = NULL ;
        break ;
      }
    }
    HQASSERT(j < ims->nplanes, "No colorant for image store reordering") ;
  }

  /* For safety's safe, free any planes which were not found in the new plane
     set. */
  for (i = 0 ; i < ims->nplanes ; ++i ) {
    if ( ims->planes[i] != NULL ) {
      /** \todo ajcd 2013-11-19: Find an assert that will work here. We're
          now using this function to deliberately delete planes when
          converting multi-channel images to LUTs. */
      HQASSERT(nplanes == 1, "Image store plane not used after reordering") ;
      im_planefree(ims, i) ;
    }
  }

  dl_free(ims->im_shared->page->dlpools, ims->planes, ims->nplanes * sizeof(IM_PLANE*),
          MM_ALLOC_CLASS_IMAGE_DATA);

  im_updatePlanes(ims, planes, CAST_SIGNED_TO_INT16(nplanes));

  return TRUE;
}

/**
 * Hierarchical double-link list management for efficient control of low
 * memory handling of image stores and global blists of images.
 * The top level is a linked list of store nodes which are
 * differentiated by 'abytes', the size of the data blocks allocated for
 * the store. For most images, these will be the default size of 16 kB
 * and will all get associated with the one node. Some images may be
 * smaller in one dimension and these will require a store node for a
 * different value of abytes.
 * Each store node contains a linked list of image stores and another
 * linked list of global blists. Every list is doubly linked to aid
 * insertion and navigation (which can happen in both directions). The
 * end of all lists is NULL.
 * A new store node is created as appropriate during by a storeopen.
 * - store states
 * - including the openforwriting state
 * - a list of stores for each state
 */
Bool im_linkims(IM_STORE *ims, uint8 action)
{
  IM_SHARED *im_shared;
  IM_STORE *ims_next;
  IM_STORE *ims_prev;
  IM_STORE_NODE *imsNode;

  HQASSERT(ims != NULL, "ims is NULL");
  HQASSERT(action == IM_ACTION_OPEN_FOR_WRITING ||
           action == IM_ACTION_COMPRESSION      ||
           action == IM_ACTION_SHAREBLISTS_1    ||
           action == IM_ACTION_WRITETODISK      ||
           action == IM_ACTION_SHAREBLISTS_2    ||
           action == IM_ACTION_NOTHINGMORE,
           "action should be a known value");
  HQASSERT(ims->next == NULL && ims->prev == NULL,
           "Image is already linked in list");

  /* Although we have separate actions to purge the shared blists after
   * compression and writing to disk, the ims's go on the same node
   * structure to do the actual purging. This simplifies having to merge
   * 2 lists when deciding which stores should have blists to move to
   * the global store.
   */
  if (action == IM_ACTION_SHAREBLISTS_2)
    action = IM_ACTION_SHAREBLISTS_1;

  im_shared = ims->im_shared;
  HQASSERT(im_shared != NULL, "No shared info for image store") ;

  HQASSERT(multi_mutex_is_locked(&im_mutex), "Image store mutex not locked");

  /* Find the store node for the appropriate abytes */
  imsNode = im_shared->im_list;
  while ( imsNode != NULL && imsNode->abytes > ims->abytes )
    imsNode = imsNode->next;

  if (imsNode == NULL || imsNode->abytes != ims->abytes) {
    /* If this is the first store encountered for this value of abytes then
     * create a new node and insert it into the chain of store nodes.
     */
    imsNode = im_storeNodeCreate(im_shared, ims->abytes);
  }

  if ( imsNode != NULL ) {
    /* Insert the ims into the list for this node/action. Organise the node
     * list by the size of the store. This is solely because it is more
     * efficient to purge large stores first.
     */
    ims_prev = NULL;
    ims_next = imsNode->ims[action];
    while (ims_next != NULL && ims_next->nblocks > ims->nblocks) {
      ims_prev = ims_next;
      ims_next = ims_next->next;
    }

    ims->next = ims_next;
    ims->prev = ims_prev;

    if ( ims->prev != NULL )
      ims->prev->next = ims;
    if ( ims->next != NULL )
      ims->next->prev = ims;

    if (ims->next == imsNode->ims[action])
      imsNode->ims[action] = ims;

    imsNode->nStores[action]++;
    imsNode->nBlocks[action] += ims->nblocks * ims->nplanes;

    im_shared->im_nStores[action]++;
    im_shared->im_nBlocks[action] += ims->nblocks * ims->nplanes;

    im_shared->im_nStoresTotal++;
    im_shared->im_nBlocksTotal += ims->nblocks * ims->nplanes;

    im_checknode(imsNode, ims, action);

  }
  return (imsNode != NULL);
}

/**
 * Unlink an image from its list, and remove it if it is attached to
 * this list
 */
void im_unlinkims(IM_STORE *ims, uint8 action)
{
  IM_SHARED *im_shared;
  IM_STORE_NODE *imsNode;

  HQASSERT(ims, "ims is NULL");
  HQASSERT(action == IM_ACTION_OPEN_FOR_WRITING ||
           action == IM_ACTION_COMPRESSION      ||
           action == IM_ACTION_SHAREBLISTS_1    ||
           action == IM_ACTION_WRITETODISK      ||
           action == IM_ACTION_SHAREBLISTS_2    ||
           action == IM_ACTION_NOTHINGMORE,
           "action should be a known value");

  im_shared = ims->im_shared;
  HQASSERT(im_shared != NULL, "No shared info for image store") ;

  /* See comment in im_linkims wrt the corresponding work */
  if (action == IM_ACTION_SHAREBLISTS_2)
    action = IM_ACTION_SHAREBLISTS_1;

  /* Find the store node for the value of abytes. It must exist if called
   * correctly.
   */
  HQASSERT(multi_mutex_is_locked(&im_mutex), "Image store mutex not locked");

  imsNode = im_shared->im_list;
  HQASSERT(imsNode != NULL, "Should be at least one node");
  while (imsNode != NULL && ims->abytes != imsNode->abytes)
    imsNode = imsNode->next;
  HQASSERT(imsNode != NULL, "Store should be linked to a node");

  im_checknode(imsNode, ims, action);

  /* Unlink the store from the list for the node/action */
  if (imsNode->ims[action] == ims)
    imsNode->ims[action] = ims->next;

  if (ims->next != NULL)
    ims->next->prev = ims->prev;
  if (ims->prev != NULL)
    ims->prev->next = ims->next;

  ims->prev = ims->next = NULL;

  imsNode->nStores[action]--;
  imsNode->nBlocks[action] -= ims->nblocks * ims->nplanes;

  im_shared->im_nStores[action]--;
  im_shared->im_nBlocks[action] -= ims->nblocks * ims->nplanes;

  im_shared->im_nStoresTotal--;
  im_shared->im_nBlocksTotal -= ims->nblocks * ims->nplanes;
  /* NB. We don't bother removing ims nodes when nStores == 0. This is
   * because the existing node will most probably be reused at some point
   * in the page.
   */
}

/**
 * Unlink an image from its current list, and link it to the list for
 * the new action.  This is more than just a convenience function
 * because relinking is guaranteed to succeed because store node should
 * exist and creating it is the only failure of im_linkims.
 */
static void do_relinkims(IM_STORE *ims, uint8 action, Bool lowmem)
{
  if ( !lowmem )
    multi_mutex_lock(&im_mutex);

  if (ims->openForWriting)
    im_unlinkims(ims, IM_ACTION_OPEN_FOR_WRITING);
  else
    im_unlinkims(ims, ims->action);
  if (!im_linkims(ims, action))
    HQFAIL("relinking a store shouldn't fail");

  if ( !lowmem )
    multi_mutex_unlock(&im_mutex);
}

void im_relinkims(IM_STORE *ims, uint8 action)
{
  do_relinkims(ims, action, TRUE);
}

Bool im_storeclose(IM_STORE *ims)
{
  int32 plane;
  Bool incompleteImageData = FALSE;
  int32 ysize = ims->obbox.y2 - ims->obbox.y1 + 1;

  HQASSERT(ims, "ims NULL in im_storeclose");

  for ( plane = 0; plane < ims->nplanes; ++plane ) {
    if ( ims->planes[ plane ] != NULL ) {
      if ( !im_blockclose(ims, ims->planes[plane], &incompleteImageData) )
        return FALSE;
    }
  }

  if (incompleteImageData) {
    /* If the image data source ran out of data we will render what we have
     * anyway. If we are also in low memory purge the row that has just been
     * written as far as possible to do the job that the *_storewrite
     * functions would have done.
     */
    if (ims->blistPurgeRow)
      (void)im_storepurgeOne(ims);
    incomplete_image_data();
  }

  if (ims->swapmem != NULL) {
    im_datafree(ims->im_shared, ims->swapmem, ims->abytes);
    ims->swapmem = NULL;
  }

  ims->blistPurgeRow = FALSE;

  /* Link this ims into appropriate list */
  ims_set_action(ims, FALSE);

  do_relinkims(ims, ims->action, FALSE);
  ims->openForWriting = FALSE;

  HQASSERT(ims->action == IM_ACTION_COMPRESSION ||
           ims->action == IM_ACTION_SHAREBLISTS_1 ||
           ims->action == IM_ACTION_WRITETODISK ||
           ims->action == IM_ACTION_SHAREBLISTS_2 ||
           ims->action == IM_ACTION_NOTHINGMORE,
           "Unexpected action");

  im_storerelease(ims, DL_LASTBAND, FALSE);
  reportStore(ims, "close", -1, -1);

  /* If rowrepeats is configured to do 'nearly the same' row analysis,
   * make sure we do not have too many consecutive rows marked. Enforce
   * a maximum number of nearly the same rows to stop the build-up of
   * small row differences.
   */
  if ( (ims->flags & IMS_ROWREPEATS_NEAR) != 0 ) {
    int32 nrows = (ims->flags & IMS_ROWREPEATS_2ROWS) != 0 ? 2 : 4;
    int32 y, rr = 0;
    for ( y = 0; y < ysize; y++ ) {
      if ( BITVECTOR_IS_SET(ims->row_repeats, y) ) {
        rr++;
        if ( rr >= nrows ) {
          BITVECTOR_CLEAR(ims->row_repeats, y);
          rr = 0;
        }
      } else {
        rr = 0;
      }
    }
  }
#if 0
  if ( ims->row_repeats != NULL ) {
    int32 y, nrows = 0;
    for ( y = 0; y < ysize; ++y )
      if ( BITVECTOR_IS_SET(ims->row_repeats, y) )
        ++nrows;
    monitorf((uint8*)"ims row_repeats: nrows %d, ysize %d, percent %f\n",
             nrows, ysize, nrows*100/(float)ysize);
  }
#endif
  return TRUE;
}

/**
 * im_planefree is normally called from im_storefree, but may also be
 * called when recombining and when detected that the plane is blank
 */
void im_planefree(IM_STORE *ims, int32 iplane)
{
  IM_PLANE *plane;

  HQASSERT(ims != NULL, "im_planeFree: ims null");
  HQASSERT(iplane >= 0, "im_planeFree: iplane < 0");

  if ( (plane = ims->planes[iplane]) != NULL ) {
    /* Free the image plane blocks. */
    im_blockfree(ims, iplane);

    /* And finally the planes. */
    dl_free(ims->im_shared->page->dlpools, (mm_addr_t)plane, sizeof(IM_PLANE),
            MM_ALLOC_CLASS_IMAGE_DATA);

    ims->planes[iplane] = NULL;
  }
}

/**
 * @@@; If we've written some image data out to disk, and we can reclaim this
 *      disk space then do so.
 */
void im_storefree(IM_STORE *ims)
{
  int32 i;

  HQASSERT(ims, "ims NULL in im_storefree");

  reportStore(ims, "free", -1, -1);

  multi_mutex_lock(&im_mutex);
  if (ims->openForWriting)
    im_unlinkims(ims, IM_ACTION_OPEN_FOR_WRITING);
  else
    im_unlinkims(ims, ims->action);
  multi_mutex_unlock(&im_mutex);

  im_storerelease( ims, DL_LASTBAND, TRUE );

  for ( i = 0; i < ims->nplanes; ++i )
    im_planefree(ims, i);

  dl_free(ims->im_shared->page->dlpools, (mm_addr_t)ims->planes,
          ims->nplanes * sizeof(IM_PLANE *), MM_ALLOC_CLASS_IMAGE_DATA);
  if ( ims->row_repeats != NULL ) {
    int32 ysize = ims->obbox.y2 - ims->obbox.y1 + 1;
    dl_free(ims->im_shared->page->dlpools, ims->row_repeats,
            BITVECTOR_SIZE_BYTES(ysize), MM_ALLOC_CLASS_IMAGE_DATA);
  }
  if ( ims->swapmem != NULL )
    im_datafree(ims->im_shared, ims->swapmem, ims->abytes);
  im_store_release_reserves(ims);

  dl_free(ims->im_shared->page->dlpools, (mm_addr_t)ims, sizeof(IM_STORE),
          MM_ALLOC_CLASS_IMAGE_DATA);
}


/**
 * Unlink and re-link the store around changing the number of planes to keep
 * the nBlocks bookkeeping up to date in the store node.
 * NB. I know that many of these planes will be NULL so strictly this count
 * won't be accurate. This doesn't matter because the primary purpose of the
 * nBlocks is for low memory handling and approximate numbers are good enough.
 */
static void im_updatePlanes(IM_STORE *ims, IM_PLANE **planes, int16 nplanes)
{
  multi_mutex_lock(&im_mutex);

  im_unlinkims(ims, ims->action);

  ims->planes = planes;
  ims->nplanes = nplanes;

  if (!im_linkims(ims, ims->action))
    HQFAIL("relinking a store shouldn't fail");

  multi_mutex_unlock(&im_mutex);
}

static IM_STORE_NODE *im_storeNodeCreate(IM_SHARED *im_shared, int32 abytes)
{
  int32 i;
  IM_STORE_NODE *imsNode;
  IM_STORE_NODE *imsNode_next;
  IM_STORE_NODE *imsNode_prev;

  /* Create a new node and insert it into the chain of store nodes */
  imsNode = dl_alloc(im_shared->page->dlpools, sizeof (IM_STORE_NODE),
                     MM_ALLOC_CLASS_IMAGE_DATA);
  if (imsNode == NULL) {
    (void) error_handler(VMERROR);
  } else {
    imsNode->abytes = abytes;
    for (i = 0; i < IM_NUM_ACTIONS; i++) {
      imsNode->ims[i] = NULL;
      imsNode->nStores[i] = 0;
      imsNode->nBlocks[i] = 0;
    }

    imsNode->blistHead = NULL;
    imsNode->blistTail = NULL;
    imsNode->nBlists = 0;

    /* Organise the list by size, put this ims behind any larger stores,
     * organised first by block size, then by number of blocks in the store
     */
    imsNode_prev = NULL;
    imsNode_next = im_shared->im_list;
    while (imsNode_next != NULL && imsNode_next->abytes > abytes) {
      imsNode_prev = imsNode_next;
      imsNode_next = imsNode_next->next;
    }

    imsNode->prev = imsNode_prev;
    imsNode->next = imsNode_next;

    if ( imsNode->prev != NULL )
      imsNode->prev->next = imsNode;
    if ( imsNode->next != NULL )
      imsNode->next->prev = imsNode;

    if (imsNode->prev == NULL)
      im_shared->im_list = imsNode;
  }

  im_checkAllStores(im_shared);

  return imsNode;
}

#ifdef ASSERT_BUILD
static void im_checknode(IM_STORE_NODE *imsNode, IM_STORE *ims, int32 action)
{
  IM_STORE  *ims_iter;
  int32     nStores = 0, nBlocks = 0;
  Bool      found = FALSE;

  HQASSERT(imsNode != NULL, "imsNode is NULL");
  HQASSERT(imsNode->ims[action] == NULL || imsNode->ims[action]->prev == NULL,
           "Head store isn't at the head of the store list");
  HQASSERT(action >= 0 && action < IM_NUM_ACTIONS, "Invalid action");

  /* This is an expensive function for huge lists, reduce the checking for large
     lists. */
  if ((imsNode->nStores[action] > 1000   &&
       imsNode->nStores[action] % 100   != 0) ||
      (imsNode->nStores[action] > 10000  &&
       imsNode->nStores[action] % 1000  != 0) ||
      (imsNode->nStores[action] > 100000 &&
       imsNode->nStores[action] % 10000 != 0)) {
    return;
  }

  ims_iter = imsNode->ims[action];
  while (ims_iter != NULL) {
    HQASSERT(ims == NULL || imsNode->abytes == ims->abytes,
             "Inconsistent abytes");
    if (ims_iter == ims)
      found = TRUE;

    nBlocks += ims_iter->nblocks * ims_iter->nplanes;
    nStores++;
    ims_iter = ims_iter->next;
  }
  HQASSERT(imsNode->nBlocks[action] == nBlocks,
           "Inconsistent count of blocks in this node");
  HQASSERT(imsNode->nStores[action] == nStores,
           "Inconsistent count of stores in this node");
  HQASSERT(ims == NULL || found, "Should have found the store on this list");
}

static void im_checkAllStores(IM_SHARED *im_shared)
{
  IM_STORE_NODE *imsNode;
  int32 action;
  int32 nBlocksTotal = 0;
  int32 nStoresTotal = 0;

  for ( action = 0; action < IM_NUM_ACTIONS; ++action ) {
    int32 nBlocks = 0;
    int32 nStores = 0;
    imsNode = im_shared->im_list;
    HQASSERT(imsNode == NULL || imsNode->prev == NULL,
             "Head node isn't at the head of the node list");

    while ( imsNode != NULL ) {
      im_checknode(imsNode, NULL, action);
      HQASSERT(imsNode->next == NULL || imsNode->abytes >
               imsNode->next->abytes, "Inconsistent abytes");

      nBlocks += imsNode->nBlocks[action];
      nStores += imsNode->nStores[action];
      imsNode = imsNode->next;
    }
    HQASSERT(nBlocks == im_shared->im_nBlocks[action],
             "Inconsistent count of blocks for one action");
    HQASSERT(nStores == im_shared->im_nStores[action],
             "Inconsistent count of stores for one action");
    nBlocksTotal += im_shared->im_nBlocks[action];
    nStoresTotal += im_shared->im_nStores[action];
  }
  HQASSERT(nBlocksTotal == im_shared->im_nBlocksTotal,
           "Inconsistent count of all blocks");
  HQASSERT(nStoresTotal == im_shared->im_nStoresTotal,
           "Inconsistent count of all stores");
}
#endif

/**
 * Given a DL object for an image, work out the extent of the image in
 * output y values.
 */
static void im_extent(LISTOBJECT *lobj, dcoord *y1, dcoord *y2)
{
  CLIPOBJECT *clipstate;
  IMAGEOBJECT *imageobj;

  imageobj = lobj->dldata.image;
  HQASSERT(imageobj, "image object somehow NULL");

  clipstate = lobj->objectstate->clipstate;
  HQASSERT(clipstate, "clip state somehow NULL");

  *y1 = imageobj->bbox.y1;
  *y2 = imageobj->bbox.y2;

  if ( theY1Clip(*clipstate) > (*y1) )
    (*y1) = theY1Clip(*clipstate);
  if ( theY2Clip(*clipstate) < (*y2) )
    (*y2) = theY2Clip(*clipstate);
}


/** \brief When adding an image to the display list, work out the minimum number
 *  of global blists required for rendering.
 *
 * \param[in] im_shared  The im_shared being added to.
 * \param[in] lobj  The image list object being added.
 *
 * NB. This may be an overestimate because an assumption is currently being made
 * that recombine will preserve all image planes even though it destroys image
 * objects that by then should contain no planes. Also, we don't account for
 * images that might be destoyed by other means.
 */
void im_addextent(LISTOBJECT *lobj, int32 sizefactdisplaylist,
                  int32 sizefactdisplayband)
{
  IMAGEOBJECT *image;
  dcoord y1, y2;
  int32 band1, band2;

  HQASSERT(lobj, "lobj NULL in im_add_extent");

  image = lobj->dldata.image;
  HQASSERT(image, "image somehow NULL in im_add_extent");

  /* Find extent of image. */
  im_extent( lobj, & y1, & y2 );

  /* Convert the extent in lines to the extent in bands */
  band1 = y1 / sizefactdisplayband;
  if (band1 < 0)
    band1 = 0;
  band2 = y2 / sizefactdisplayband;
  if (band2 >= sizefactdisplaylist)
    band2 = sizefactdisplaylist - 1;

  blist_add_extent(image->ims, band1, band2);
  image->ims->band = band2;

  if ( image->mask != NULL ) {
    blist_add_extent(image->mask->ims, band1, band2);
    image->mask->ims->band = band2;
  }
}


/* Returns TRUE iff data has been purged. */
Bool im_purgeglobal(IM_SHARED *im_shared)
{
  Bool result = FALSE;
  IM_STORE_NODE *imsNode;
  int32 nFreed = 0;

  /* Trim off a maximum of IM_MAX_BLISTS_TO_PURGE excess global shared blists.
   * Do this by walking over the store nodes and retaining the blists in the
   * largest nodes. This is essential for rendering where we have to guarantee
   * that a blist of appropriate size will exist for all images.
   * As a result, this function may have to be called many times to completely
   * purge the global blist list down to its minimum size.
   */
  if ( blist_toomany(im_shared, 0) ) {
    int32   i = 0;
    size_t required = blist_required(im_shared);

    imsNode = im_shared->im_list;
    while (imsNode != NULL) {
      if ( (size_t)(i + imsNode->nBlists) <= required )
        i += imsNode->nBlists;
      else {
        int32 nToFree;
        IM_BLIST *blist;

        if ( required == 0 )
          nToFree = imsNode->nBlists;
        else {
          nToFree = i + imsNode->nBlists - CAST_UNSIGNED_TO_INT32(required);
          if (nToFree > imsNode->nBlists)
            nToFree = imsNode->nBlists;
          if (nFreed + nToFree > IM_MAX_BLISTS_TO_PURGE)
            nToFree = IM_MAX_BLISTS_TO_PURGE - nFreed;
        }

        i += imsNode->nBlists;

        for ( blist = imsNode->blistHead
              ; blist != NULL && nToFree > 0 && blist_toomany(im_shared, 0) ; ) {
          result = TRUE;
          nFreed++;
          nToFree--;
          blist = blist_freeall(im_shared, blist);
        }
      }
      imsNode = imsNode->next;
    }
  }

  blist_checkGlobal(im_shared);

  return result;
}

/**
 * Attempt to compress or purge enough image blocks to be
 * a) useful to the (unknown) low memory action in the sense of purging enough
 *    to not require calling this function too often,
 * b) not purging too much which may slow down both interpretation and
 *    rendering,
 * c) purging blocks to disk in the most effective order for rendering
 *    performance.
 *
 * Normally whole image rows are purged at a time to allow a row of blists to be
 * reused for the next row.  However xyswapped images are likely to have
 * incomplete rows during interpretation because they are written in columns,
 * but it is still more efficient to purge in (partial) rows for best locality
 * when rendering.
 *
 * For closed stores try to purge at least MIN_BLOCKS per plane to avoid
 * excessive function calling.
 */
static void im_purgesomedata(IM_STORE *ims, int32 *purgedBlocks, uint8 action)
{
  Bool ok = TRUE;
  int32 i, minPerPlane;

  /* Minimum blocks to purge per plane in one call to the purge function. */
#define MIN_BLOCKS (10)

  HQASSERT(ims->action == action, "Inconsistent action for image store");
  HQASSERT(action == IM_ACTION_COMPRESSION || action == IM_ACTION_WRITETODISK,
           "Action should be compress or write to disk");

  *purgedBlocks = 0;

  /* If the store is open then we want to purge a single row and setting
     minPerPlane to 1 means we don't do more than this.  For open and xyswapped
     stores we are unlikely to be able to purge in full rows since data is
     written in columns, so it is better to purge little and often to make it
     more likely that longer rows are purged.  For closed stores try to purge at
     least MIN_BLOCKS per plane to avoid excessive function calling. */
  minPerPlane = ims->openForWriting ? 1 : MIN_BLOCKS;

  for ( i = 0; i < ims->nplanes && ok; ++i ) {
    IM_PLANE *plane = ims->planes[i];

    if ( plane != NULL ) {
      int32 purgedThisPlane = 0, y;

      /* Don't bother looking in rows that are fully purged. */
      y = action == IM_ACTION_COMPRESSION ? plane->yCompressed : plane->yPurged;

      /* Purge as much of the row as possible, but if necessary purge more rows
         to get at least minPerPlane. */
      for ( ; y < ims->yblock && purgedThisPlane < minPerPlane && ok; ++y ) {
        ok = im_blockpurgeplane(ims, plane, y, &purgedThisPlane);
        SwOftenUnsafe();
      }

      *purgedBlocks += purgedThisPlane;
    }
  }

  if ( *purgedBlocks > 0 )
    ims->purged = TRUE;

  reportStore(ims, "purge", -1, -1);
}

#if defined( ASSERT_BUILD )
static void im_checkNoPurgeableBlocks(IM_STORE *ims)
{
  int32 planei, ty;

  /* Walk through the image store looking for purgeable data */
  for ( ty = 0; ty < ims->yblock; ++ty ) {
    for ( planei = 0; planei < ims->nplanes; ++planei ) {
      IM_PLANE *plane = ims->planes[planei];

      if ( plane != NULL )
        im_blockcheckNoPurgeable(ims, plane, ty);
    }
  }
}
#else
#define im_checkNoPurgeableBlocks(ims)    EMPTY_STATEMENT()
#endif

/* Returns the largest store available for purging at the requested action */
static IM_STORE *im_purgelocatelargest(IM_SHARED *im_shared, uint8 action)
{
  IM_STORE_NODE *imsNode;

  /* First consider image stores closed for writing */
  imsNode = im_shared->im_list;
  while (imsNode != NULL) {
    IM_STORE  *ims = imsNode->ims[action];
    if (ims != NULL) {
      HQASSERT(!ims->openForWriting, "Store should be non-writeable");
      HQASSERT(ims->action == action, "Invalid action type");
      return ims;
    }
    imsNode = imsNode->next;
  }

  /* Didn't find one, try to find a writeable store */
  imsNode = im_shared->im_list;
  while (imsNode != NULL) {
    IM_STORE *ims_iter;
    IM_STORE *ims_next;
    for (ims_iter = imsNode->ims[IM_ACTION_OPEN_FOR_WRITING];
         ims_iter != NULL;
         ims_iter = ims_next) {
      HQASSERT(ims_iter->openForWriting, "Store should be writeable");
      ims_next = ims_iter->next;
      if (ims_iter->action == IM_ACTION_NOTHINGMORE)
        im_checkNoPurgeableBlocks(ims_iter);
      if (ims_iter->action == action )
        return ims_iter;
    }
    imsNode = imsNode->next;
  }

  return NULL;
}

/* Attempt to purge data from one store with the next available action */
static int32 im_storePurgeSome(IM_STORE *ims, int32 purgeLimit)
{
  int32 purgedBlocks = 0;
  int32 totalPurged = 0;

  HQASSERT(ims != NULL, "ims is NULL" );

  if (ims->action == IM_ACTION_COMPRESSION) {
    do {
      purgedBlocks = 0;
      im_purgesomedata(ims, &purgedBlocks, IM_ACTION_COMPRESSION);
      if (purgedBlocks == 0) {
        if (ims->openForWriting)
          ims_set_action(ims, FALSE);
        else {
          do_relinkims(ims, IM_ACTION_SHAREBLISTS_1, TRUE);
          ims->action = IM_ACTION_SHAREBLISTS_1;
        }
      }
      totalPurged += purgedBlocks;
    } while ( purgedBlocks > 0 && totalPurged < purgeLimit );
  }

  else if (ims->action == IM_ACTION_WRITETODISK) {
    do {
      purgedBlocks = 0;
      im_purgesomedata(ims, &purgedBlocks, IM_ACTION_WRITETODISK);
      if (purgedBlocks == 0) {
        if (ims->openForWriting)
          ims->action = IM_ACTION_NOTHINGMORE;
        else {
          do_relinkims(ims, IM_ACTION_SHAREBLISTS_2, TRUE);
          ims->action = IM_ACTION_SHAREBLISTS_2;
        }
      }
      totalPurged += purgedBlocks;
    } while ( purgedBlocks > 0 && totalPurged < purgeLimit );
  }

  else if (ims->action == IM_ACTION_NOTHINGMORE) {
    im_checkNoPurgeableBlocks(ims);
  }

  if (totalPurged > 0) {
    int32 planei;

    /* Walk over the blists to potentially reassign blocks if the current
     * associations aren't movable. This may happen particularly when
     * compressing images in low memory and for xyswapped images.
     */
    for (planei = 0; planei < ims->nplanes; ++planei) {
      IM_PLANE *plane = ims->planes[planei];

      if (plane != NULL && plane->blisth != NULL)
        blist_reassign(ims, plane);
    }
  }
  return totalPurged;
}


/** Purge one row of image blocks. */
int32 im_storepurgeOne( IM_STORE *ims )
{
  return im_storePurgeSome(ims, 0);
}


/* Merge action flags appropriately when combining store data */
static void im_mergeAction( IM_STORE *ims_src, IM_STORE *ims_dst )
{
  uint8 newAction = ims_dst->action;

  HQASSERT(ims_src != NULL, "ims_src is NULL");
  HQASSERT(ims_dst != NULL, "ims_dst is NULL");

  /* Merge the action flag */
  switch (ims_dst->action) {
  case IM_ACTION_NOTHINGMORE:
    if (ims_src->action == IM_ACTION_COMPRESSION)
      newAction = IM_ACTION_COMPRESSION;
    /* Drop through */
  case IM_ACTION_COMPRESSION:
    if (ims_src->action == IM_ACTION_SHAREBLISTS_1)
      newAction = IM_ACTION_SHAREBLISTS_1;
    /* Drop through */
  case IM_ACTION_SHAREBLISTS_1:
    if (ims_src->action == IM_ACTION_WRITETODISK)
      newAction = IM_ACTION_WRITETODISK;
    /* Drop through */
  case IM_ACTION_WRITETODISK:
    if (ims_src->action == IM_ACTION_SHAREBLISTS_2)
      newAction = IM_ACTION_SHAREBLISTS_2;
    /* Drop through */
  case IM_ACTION_SHAREBLISTS_2:
    break;
  default:
    HQFAIL("Unexpected ims->action");
    break;
  }

  if (newAction != ims_dst->action) {
    do_relinkims(ims_dst, newAction, FALSE);
    ims_dst->action = newAction;
  }
}

#if defined( ASSERT_BUILD )
static Bool good_ims(IM_STORE *dst, IM_STORE *src)
{
  if (src == NULL || dst == NULL )
    return FALSE;
  if ( src->bpp != dst->bpp )
    return FALSE;
  if ( src->bpps != dst->bpps )
    return FALSE;
  if ( !bbox_equal(&src->obbox, &dst->obbox) )
    return FALSE;
  if ( src->flags != dst->flags )
    return FALSE;
  if ( src->xblock != dst->xblock )
    return FALSE;
  if ( src->yblock != dst->yblock )
    return FALSE;
  if ( src->nblocks != dst->nblocks )
    return FALSE;
  if ( src->abytes != dst->abytes )
    return FALSE;
  if ( src->openForWriting || dst->openForWriting )
    return FALSE;
  return TRUE;
}
#endif /* ASSERT_BUILD */

Bool im_storemerge(IM_STORE *ims_src, IM_STORE *ims_dst)
{
  int32 i;
  int16 nplanes;
  IM_PLANE **planes;

  HQASSERT(good_ims(ims_dst, ims_src), "Problem appending to image store");

  nplanes = ims_src->nplanes;
  planes  = ims_src->planes;

  /* Enlarge the number of planes if we need to. */
  if ( nplanes > ims_dst->nplanes ) {
    planes = dl_alloc(ims_src->im_shared->page->dlpools, nplanes * sizeof(IM_PLANE *),
                       MM_ALLOC_CLASS_IMAGE_DATA );
    if ( planes == NULL )
      return error_handler(VMERROR);
    for ( i = 0; i < nplanes; ++i )
      planes[ i ] = NULL;
    for ( i = 0; i < ims_dst->nplanes; ++i )
      planes[ i ] = ims_dst->planes[ i ];
    dl_free(ims_src->im_shared->page->dlpools, (mm_addr_t)ims_dst->planes, ims_dst->nplanes *
            sizeof(IM_PLANE *), MM_ALLOC_CLASS_IMAGE_DATA);

    im_updatePlanes(ims_dst, planes, nplanes);
  }

  ims_dst->stdblocks += ims_src->stdblocks;
  ims_dst->extblocks += ims_src->extblocks;
  ims_src->stdblocks = 0;
  ims_src->extblocks = 0;

  /* Merge planes from src into dst. */
  for ( i = 0; i < nplanes; ++i ) {
    HQASSERT(ims_dst->planes[ i ] == NULL ||
             ims_src->planes[ i ] == NULL,
             "recombine planes should be mutually exclusive");
    if ( ims_src->planes[ i ] ) {
      ims_dst->planes[ i ] = ims_src->planes[ i ];
      ims_src->planes[ i ] = NULL;
    }
  }

  im_mergeAction(ims_src, ims_dst);

  return TRUE;
}

/* Update ims->tbbox after trimming a column. */
static void trimmed_column(IM_STORE *ims, Bool left, int32 bx)
{
  int32 i, bb, width;

  /* Find the first non-empty plane. */
  for ( i = 0; i < ims->nplanes && !ims->planes[i]; ++i ) {
    EMPTY_STATEMENT();
  }

  bb = im_bb_get_and_check(ims, bx, 0, i);
  width = im_blockwidth(ims->planes[i]->blocks[bb]);

  if ( left )
    ims->tbbox.x1 += width;
  else
    ims->tbbox.x2 -= width;

  HQASSERT(ims->tbbox.x1 >= ims->obbox.x1 && ims->tbbox.x2 <= ims->obbox.x2,
           "Trimmed store bbox is invalid");
  HQASSERT(!bbox_is_empty(&ims->tbbox),
           "Image shouldn't be completely trimmed away");
}

/* Update ims->tbbox after trimming a row. */
static void trimmed_row(IM_STORE *ims, Bool top, int32 by)
{
  int32 i, bb, height;

  /* Find the first non-empty plane. */
  for ( i = 0; i < ims->nplanes && !ims->planes[i]; ++i ) {
    EMPTY_STATEMENT();
  }

  bb = im_bb_get_and_check(ims, 0, by, i);
  height = im_blockheight(ims->planes[i]->blocks[bb]);

  if ( top )
    ims->tbbox.y1 += height;
  else
    ims->tbbox.y2 -= height;

  HQASSERT(ims->tbbox.y1 >= ims->obbox.y1 && ims->tbbox.y2 <= ims->obbox.y2,
           "Trimmed store bbox is invalid");
  HQASSERT(!bbox_is_empty(&ims->tbbox),
           "Image shouldn't be completely trimmed away");
}

static void im_storetrim_x(IM_STORE *ims, int32 x1, int32 x2)
{
  int32 planei, nplanes, yblock, bb, bx, by, bx1, bx2, tx1, tx2 ;
  Bool ftrimming;

  HQASSERT(ims != NULL, "im_storetrim_x: ims NULL");
  HQASSERT(x1 >= 0 && x1 <= x2, "im_storetrim_x: bad x1, x2 values");

  nplanes = ims->nplanes;
  yblock = ims->yblock;

  bx1 = im_index_bx(ims, (( x1 << ims->bpps ) >> 3 ));
  bx2 = im_index_bx(ims, (( x2 << ims->bpps ) >> 3 ));
  HQASSERT(bx1 >= 0 && bx2 < ims->xblock, "bad bx1, bx2 values");

  /* Current trim bounds, expressed as blocks. */
  tx1 = im_index_bx(ims, ((ims->tbbox.x1 - ims->obbox.x1) << ims->bpps) >> 3) ;
  tx2 = im_index_bx(ims, ((ims->tbbox.x2 - ims->obbox.x1) << ims->bpps) >> 3) ;

  /* Trim any columns on the LHS of x1 */
  ftrimming = (bx1 > tx1) ;
  for ( bx = bx1 - 1; ftrimming && bx >= 0; --bx ) {
    for ( by = 0; ftrimming && by < yblock; ++by ) {
      bb = im_bb_get_and_check(ims, bx, by, 0);
      for ( planei = 0; ftrimming && planei < nplanes; ++planei ) {
        IM_PLANE *plane = ims->planes[planei];

        ftrimming = (plane == NULL ||
                     im_blocktrim(ims, planei, &(plane->blocks[bb]), TRUE));
      }
    }
    if ( ftrimming )
      trimmed_column(ims, TRUE, bx);
  }

  /* Trim any columns on the RHS of x2 */
  ftrimming = (bx2 < tx2) ;
  for ( bx = bx2 + 1; ftrimming && bx < ims->xblock; ++bx ) {
    for ( by = 0; ftrimming && by < yblock; ++by ) {
      bb = im_bb_get_and_check(ims, bx, by, 0);
      for ( planei = 0; ftrimming && planei < nplanes; ++planei ) {
        IM_PLANE *plane = ims->planes[planei];

        ftrimming = (plane == NULL ||
                     im_blocktrim(ims, planei, &(plane->blocks[bb]), TRUE));
      }
    }
    if ( ftrimming )
      trimmed_column(ims, FALSE, bx);
  }
}

#if defined( ASSERT_BUILD )
/* Checks that im_storetrim_x has already been called with the same
 * x1/x2 values and trimmed the appropriate blocks; im_storetrim_y can
 * therefore optimise out the columns of blocks on the left of x1 and
 * on the right of x2.
 */
static void im_storetrim_ycheck(IM_STORE *ims, int32 bx1, int32 bx2)
{
  int32 yblock, bx, by;

  HQASSERT(ims != NULL, "im_storetrim_ycheck: ims NULL");

  yblock = ims->yblock;

  HQASSERT(bx1 >= 0 && bx2 < ims->xblock, "bad bx1, bx2 values");

  /* Trim any columns on the LHS of x1 */
  for ( bx = bx1 - 1; bx >= 0; --bx )
    for ( by = 0; by < yblock; ++by )
      im_blocktrim_ycheck(ims, bx, by);

  /* Trim any columns on the RHS of x2 */
  for ( bx = bx2 + 1; bx < ims->xblock; ++bx )
    for ( by = 0; by < yblock; ++by )
      im_blocktrim_ycheck(ims, bx, by);
}

#else
#define im_storetrim_ycheck( ims, x1, x2 ) EMPTY_STATEMENT()
#endif

/* Assumes that im_storetrim_x will have already been called with the
 * same x1/x2 values and trimmed the appropriate blocks; can therefore
 * optimise out the columns of blocks on the left of x1 and on the
 * right of x2 (this is checked by im_storetrim_ycheck).
 */
static void im_storetrim_y(IM_STORE *ims, int32 x1, int32 x2, int32 y1, int32 y2)
{
  int32 yblock;
  int32 by1, by2, ty1, ty2;

  HQASSERT(ims != NULL, "im_storetrim_y: ims NULL");
  HQASSERT(x1 >= 0 && x1 <= x2, "im_storetrim_y: bad x1, x2 values");
  HQASSERT(y1 >= 0 && y1 <= y2, "im_storetrim_y: bad y1, y2 values");

  yblock = ims->yblock;

  by1 = im_index_by(ims, y1);
  by2 = im_index_by(ims, y2);
  HQASSERT(by1 >= 0 && by2 < yblock, "im_storetrim_y: bad by1, by2 values");

  /* Current trim bounds, expressed as blocks. */
  ty1 = im_index_by(ims, ims->tbbox.y1 - ims->obbox.y1) ;
  ty2 = im_index_by(ims, ims->tbbox.y2 - ims->obbox.y1) ;

  if ( by1 > ty1 || by2 < ty2 ) {
    int32 planei, nplanes;
    int32 bb, bx, bx1, bx2, by;
    Bool ftrimming;

    nplanes = ims->nplanes;

    bx1 = im_index_bx(ims, (( x1 << ims->bpps ) >> 3 ));
    bx2 = im_index_bx(ims, (( x2 << ims->bpps ) >> 3 ));
    HQASSERT(bx1 >= 0 && bx2 < ims->xblock,
             "im_storetrim_y: bad bx1, bx2 values");

    /* Check columns on LHS of bx1 and RHS of bx2 have been trimmed */
    im_storetrim_ycheck( ims, bx1, bx2 );

    /* Trim any rows below y1 */
    ftrimming = (by1 > ty1) ;
    for ( by = by1 - 1; ftrimming && by >= 0; --by ) {
      for ( bx = bx1; ftrimming && bx <= bx2; ++bx ) {
        bb = im_bb_get_and_check(ims, bx, by, 0);
        for ( planei = 0; ftrimming && planei < nplanes; ++planei ) {
          IM_PLANE *plane = ims->planes[planei];

          ftrimming = (plane == NULL ||
                       im_blocktrim(ims, planei, &(plane->blocks[bb]), FALSE));
        }
      }
      if ( ftrimming )
        trimmed_row(ims, TRUE, by);
    }

    /* Trim any rows above y2 */
    ftrimming = (by2 < ty2) ;
    for ( by = by2 + 1; ftrimming && by < yblock; ++by ) {
      for ( bx = bx1; ftrimming && bx <= bx2; ++bx ) {
        bb = im_bb_get_and_check(ims, bx, by, 0);
        for ( planei = 0; ftrimming && planei < nplanes; ++planei ) {
          IM_PLANE *plane = ims->planes[planei];

          ftrimming = (plane == NULL ||
                       im_blocktrim(ims, planei, &(plane->blocks[bb]), FALSE));
        }
      }
      if ( ftrimming )
        trimmed_row(ims, FALSE, by);
    }
  }
}

void im_storetrim(IM_STORE *ims, const ibbox_t *ibbox)
{
  int32 x1, x2 ;

  HQASSERT(ims != NULL, "No image store to trim") ;
  HQASSERT(ibbox != NULL, "No bounding box to trim image store") ;
  HQASSERT(!bbox_is_empty(ibbox), "Trim bounding box is empty") ;
  HQASSERT(bbox_contains(&ims->tbbox, ibbox),
           "Trim bounding box not inside current trim box") ;

  /* Convert image-space coordinates to store-relative coordinates. */
  x1 = ibbox->x1 - ims->obbox.x1 ;
  x2 = ibbox->x2 - ims->obbox.x1 ;

  if ( ibbox->x1 > ims->tbbox.x1 || ibbox->x2 < ims->tbbox.x2 )
    im_storetrim_x(ims, x1, x2) ;
  if ( ibbox->y1 > ims->tbbox.y1 || ibbox->y2 < ims->tbbox.y2 )
    im_storetrim_y(ims, x1, x2, ibbox->y1 - ims->obbox.y1, ibbox->y2 - ims->obbox.y1) ;
}

Bool im_planenew(IM_STORE *ims, int32 planei)
{
  int32 i;
  IM_PLANE *plane;
  IM_SHARED *im_shared ;

  HQASSERT(ims, "ims NULL in im_planenew");
  HQASSERT(planei >= 0, "planei should be >= 0");
  HQASSERT(ims->planes, "somehow lost planes");
  HQASSERT(planei < ims->nplanes, "planei out of range");

  im_shared = ims->im_shared ;
  HQASSERT(im_shared != NULL, "No shared info for image store") ;

  if ( (plane = dl_alloc(im_shared->page->dlpools, sizeof(IM_PLANE),
                         MM_ALLOC_CLASS_IMAGE_DATA)) == NULL )
    return error_handler(VMERROR);

  if ( (plane->blocks = im_blocknalloc(im_shared, ims->nblocks)) == NULL ) {
    dl_free(im_shared->page->dlpools, (mm_addr_t)plane, sizeof(IM_PLANE),
            MM_ALLOC_CLASS_IMAGE_DATA);
    return FALSE;
  }
  plane->blisth = NULL;
  plane->yCompressed = 0;
  plane->yPurged = 0;

  /* Determine the number of blists required during both interpretation and
   * rendering. Don't bother doing this for undersized blocks if we already
   * have a substantial number because it can be a performance drain in
   * low memory.
   */
  plane->nBlists = 0;

  plane->nDesiredBlists = NUM_INTERPRETATION_BLISTS(ims);
  if (plane->nDesiredBlists < NUM_RENDERING_BLISTS(ims))
    plane->nDesiredBlists = NUM_RENDERING_BLISTS(ims);
  if (plane->nDesiredBlists > ims->nblocks)
    plane->nDesiredBlists = ims->nblocks;

  if ( !ims_can_compress(ims) && !ims_can_write2disk(ims) )
    plane->nDesiredBlists = 0;
  else {
    IM_STORE_NODE *imsNode;
    size_t nBlists = 0;

    multi_mutex_lock(&im_mutex);
    imsNode = im_shared->im_list;
    while (imsNode != NULL && imsNode->abytes >= ims->abytes) {
      nBlists += imsNode->nBlists;
      imsNode = imsNode->next;
    }
    if ( nBlists >= blist_required(im_shared)
                    + (size_t)ims->nplanes * plane->nDesiredBlists )
      plane->nDesiredBlists = 0;
    multi_mutex_unlock(&im_mutex);
  }
  ims->planes[planei] = plane;

  for ( i = 0; i < ims->nblocks; ++i )
    plane->blocks[i] = NULL;

  {
    int32 bx = 0, by = 0;

    for ( i = 0; i < ims->nblocks; ++i ) {
      if ( !im_blocksetup(ims, planei, bx, by) ) {
        im_blockerrfree(ims, plane, planei);
        return FALSE;
      }
      ++bx;
      if (bx == ims->xblock) {
        bx = 0;
        ++by;
      }
    }
  }
  return TRUE;
}

/**
 * Can recycle an image store when image adjusting instead of creating a
 * new store.  When converting an RGB image to CMYK an additional plane
 * is added.
 */
Bool im_storerecycle(IM_STORE *ims, int32 nplanes, Bool *recycled)
{
  IM_PLANE **planes;
  int32 i, old_nplanes;

  HQASSERT(ims, "ims is null");

  *recycled = FALSE;

  old_nplanes = ims->nplanes;
  planes = ims->planes;

  /* Can't recycle store if the new store requires fewer planes than the old. */
  if ( nplanes < old_nplanes )
    return TRUE;

  /* Can't recycle store if some of the old planes are missing.  Check all the
     blocks are in memory.  Blocks on disk would make re-using the image store
     for image adjustment tricky... */
  for ( i = 0; i < old_nplanes; ++i ) {
    if ( planes[i] == NULL )
      return TRUE;
    else {
      IM_BLOCK **blocks = planes[i]->blocks;
      int32 bi;

      for ( bi = 0; bi < ims->nblocks; ++bi ) {
        uint8 storage = im_blockstorage(blocks[bi]);
        if ( storage != IM_STORAGE_MEMORY )
          return TRUE;
      }
    }
  }

  /* Extend the planes array as necessary.  Blocks are allocated lazily. */
  if ( nplanes > old_nplanes ) {
    Bool allow_purge = ims->im_shared->page->im_purge_allowed;

    /* Switch off image purging for this allocation otherwise the above
       invariant for blocks being in memory may be invalidated. */
    ims->im_shared->page->im_purge_allowed = FALSE;
    planes = dl_alloc(ims->im_shared->page->dlpools, nplanes * sizeof(IM_PLANE *),
                      MM_ALLOC_CLASS_IMAGE_DATA);
    ims->im_shared->page->im_purge_allowed = allow_purge;
    if ( planes == NULL )
      return error_handler(VMERROR);

    for ( i = 0; i < old_nplanes; ++i )
      planes[ i ] = ims->planes[ i ];
    for ( ; i < nplanes; ++i )
      planes[ i ] = NULL;

    dl_free(ims->im_shared->page->dlpools, ims->planes, old_nplanes * sizeof(IM_PLANE *),
            MM_ALLOC_CLASS_IMAGE_DATA);
    im_updatePlanes(ims, planes, CAST_SIGNED_TO_INT16(nplanes));
  }

  /* Re-open the image store ready for writing (for image adjustment). */
  do_relinkims(ims, IM_ACTION_OPEN_FOR_WRITING, FALSE);
  ims->openForWriting = TRUE;
  ims->flags |= IMS_RECYCLED;

  for ( i = 0; i < old_nplanes; ++i ) {
    IM_BLOCK **blocks = planes[i]->blocks;
    int32 bi;

    /* Reopen the block for writing which clears IM_BLOCKFLAG_WRITE_COMPLETE
       and stops the block from being purged. */
    for ( bi = 0; bi < ims->nblocks; ++bi )
      im_blockreopen(blocks[bi]);
  }

  *recycled = TRUE;
  return TRUE;
}


const ibbox_t *im_storebbox_trimmed(IM_STORE *ims)
{
  return &ims->tbbox;
}

const ibbox_t *im_storebbox_original(IM_STORE *ims)
{
  return &ims->obbox;
}

uint32 im_storegetflags(IM_STORE *ims)
{
  HQASSERT(ims, "ims is null");

  return ims->flags;
}

void im_storesetflags(IM_STORE *ims, uint32 flags)
{
  HQASSERT(ims, "ims is null");

  ims->flags = flags;
}

static void im_setblocksize(IM_STORE *ims, const ibbox_t *imsbbox, int32 bpp)
{
  int32 tbpp, bpps, bx, by, xsize, ysize ;

  HQASSERT(ims, "ims NULL in im_setblocksize");
  HQASSERT(imsbbox, "ims bbox NULL");

  xsize = imsbbox->x2 - imsbbox->x1 + 1 ;
  HQASSERT(xsize > 0, "xsize should be > 0");
  ysize = imsbbox->y2 - imsbbox->y1 + 1 ;
  HQASSERT(ysize > 0, "xsize should be > 0");

#if defined( ASSERT_BUILD )
  { /* We don't want to make blocks too large overwise so we run
       into problems allocating that much memory.  We certainly
       don't want to overflow int16 abytes. */
    int16 abytes = (int16)(ims->blockHeight * ims->blockWidth);
    HQASSERT((int32)abytes == (ims->blockWidth * ims->blockHeight),
             "required size of abytes overflows int16");
  }
#endif

  bpps = 0;
  tbpp = bpp;
  while ( tbpp != 1 ) {
    tbpp >>= 1;
    ++bpps;
  }

  ims->bpp   = ( uint8 )bpp;
  ims->bpps  = ( uint8 )bpps;

  ims->obbox = ims->tbbox = *imsbbox;

  xsize = (( xsize << bpps ) + 7 ) >> 3;
  bx = im_index_bx(ims, xsize - 1) + 1;
  by = im_index_by(ims, ysize - 1) + 1;

  ims->xblock = bx;
  ims->yblock = by;

  ims->nblocks = bx * by;
}

/* Given an x value, returns the block index for the x component. */
int32 im_index_bx(IM_STORE* ims, int32 x)
{
  int32 bx;

  if ( ims->blockWidth == 128 ) {
    bx = x >> 7;
    HQASSERT(bx == (x / ims->blockWidth), "im_index_bx optimisation failed");
  } else {
    /* Fallback case is slower */
    HQFAIL("Unexpected blockWidth");
    bx = (x / ims->blockWidth);
  }

  return bx;
}

/* Given an y value, returns the block index for the y component. */
int32 im_index_by(IM_STORE* ims, int32 y)
{
  int32 by;

  if ( ims->blockHeight == 128 ) {
    by = y >> 7;
    HQASSERT(by == (y / ims->blockHeight), "im_index_by optimisation failed");
  } else {
    /* Fallback case is slower */
    HQFAIL("Unexpected blockHeight");
    by = y / ims->blockHeight;
  }

  return by;
}

/* Given an x value, returns the pixel index into the block
   for the x component. */
int32 im_index_xi(IM_STORE* ims, int32 x)
{
  int32 xi;

  if ( BIT_EXACTLY_ONE_SET((uint32)ims->blockWidth) ) {
    xi = x & (ims->blockWidth - 1);
    HQASSERT(xi == (x % ims->blockWidth), "im_index_xi optimisation failed");
  }
  else {
    xi = x % ims->blockWidth;
  }

  return xi;
}

/* Given an y value, returns the line index into the block
   for the y component. */
int32 im_index_yi(IM_STORE* ims, int32 y)
{
  int32 yi;

  if ( BIT_EXACTLY_ONE_SET((uint32)ims->blockHeight) ) {
    yi = y & (ims->blockHeight - 1);
    HQASSERT(yi == (y % ims->blockHeight), "im_index_yi optimisation failed");
  }
  else {
    yi = y % ims->blockHeight;
  }

  return yi;
}


Bool im_preconvert_prealloc(IM_STORE *ims)
{
  size_t size, remaining;
  mm_pool_t pool;

  HQASSERT(ims != NULL, "ims NULL");

  /** \todo Assuming RGB->CMYK preconversion, better to scan the group stack to
      count how many extra image planes are needed. */
  size = (sizeof(IM_BLOCK *) + im_blocksizeof() + ims->abytes) * ims->nblocks;
  pool = im_datapool(ims->im_shared, ims->abytes);
  remaining = size;
  while ( remaining > 0 ) {
    im_reserve_chunk_t *ptr;

    /* Take chunks of default seg size, even though image pools don't use it, so
       it can be reused by other pools. */
    size_t alloc = remaining >= MM_SEGMENT_SIZE ? MM_SEGMENT_SIZE
                   : min(remaining, IM_BLOCK_DEFAULT_SIZE);

    ptr = mm_alloc(pool, alloc, MM_ALLOC_CLASS_IMAGE_DATA);
    if ( ptr == NULL )
      return error_handler(VMERROR);
    ptr->size = alloc;
    ptr->next = ims->reserves; ims->reserves = ptr;
    remaining -= alloc;
  }
  return TRUE;
}


void im_store_release_reserves(IM_STORE *ims)
{
  mm_pool_t pool;
  im_reserve_chunk_t *chunk, *next_chunk;

  pool = im_datapool(ims->im_shared, ims->abytes);
  for ( chunk = ims->reserves ; chunk != NULL ; chunk = next_chunk ) {
    next_chunk = chunk->next;
    mm_free(pool, chunk, chunk->size);
  }
  ims->reserves = NULL;
}


#ifdef ASSERT_BUILD
Bool im_store_have_reserves(IM_STORE *ims)
{
  return ims->reserves != NULL;
}
#endif


void im_storerelease(IM_STORE *ims, int32 band, Bool nullblock)
{
  HQASSERT(ims, "ims NULL in im_storerelease");

  /* if this is a image filter cache store, we need to do back up the chain
     freeing all previous data */

  /** \todo ajcd 2011-02-11: Can we get rid of the
      im_blockForbid/im_blockAllow? These are the only instances outside of
      imblock.c, it would be nice to make the mutex control local to that
      file. */
  im_blockForbid();
  blist_release(ims, band, nullblock);
  im_blockAllow();

  reportStore(ims, "release", -1, -1);
}

void im_store_pre_render_assertions(IM_SHARED *im_shared)
{
  im_block_pre_render_assertions(im_shared);
}

void im_storereadrelease(corecontext_t *context)
{
  if (!context) {
    context = get_core_context();
  }
  im_blockreadrelease(context);
}

int32 im_bb_get_and_check(IM_STORE *ims, int32 bx, int32 by, int32 planei)
{
  int32 bb;

  UNUSED_PARAM(int32, planei);
#if defined(DEBUG_BUILD)
  if ( IS_INTERPRETER() ) {
    HQTRACE((debug_imstore & IMSDBG_BLOCKMEM) != 0,
            ("Memory stats: %d %d\n",
            mm_no_pool_size(TRUE /* include_reserve */),
            mm_pool_free_size(mm_pool_temp)));
  }
#endif /* DEBUG_BUILD */

  HQASSERT(ims, "ims NULL");
  HQASSERT(bx >= 0 && bx < ims->xblock, "bx out of range");
  HQASSERT(by >= 0 && by < ims->yblock, "by out of range");
  HQASSERT(planei == IMS_ALLPLANES ||
           (planei >= 0 && planei < ims->nplanes),
           "plane out of range");

  bb = bx + by * ims->xblock;
  HQASSERT(bb >= 0 && bb < ims->nblocks, "block out of range");

  return bb;
}

/**
 * Block compression is only possible if params allow for normal images and if
 * the block size is above a threshold.
 */
Bool ims_can_compress(IM_STORE *ims)
{
  DL_STATE *page = ims->im_shared->page;
  return ( ims->abytes >= IM_MIN_COMPRESSION_SIZE &&
           page->imageParams.CompressImageSource &&
           (page->imageParams.CompressImageParms & ( 1 << ( ims->bpp - 1 ))) != 0 );
}

/**
 * Block purging to disk requires the block size to be above a threshold
 */
Bool ims_can_write2disk(IM_STORE *ims)
{
  return ( ims->abytes >= IM_MIN_WRITETODISK_SIZE &&
           ims->im_shared->page->imageParams.LowMemImagePurgeToDisk );
}

/**
 * Update the image-store action field
 */
void ims_set_action(IM_STORE *ims, Bool close)
{
  if ( close ) {
    /* Since we've written more data to this store, there is a possibility that
       it can be compressed. */
    if ( ims_can_compress(ims) )
      ims->action = IM_ACTION_COMPRESSION;
    else if ( ims_can_write2disk(ims) )
      ims->action = IM_ACTION_WRITETODISK;
  }
  else {
    if ( ims->action == IM_ACTION_COMPRESSION ) {
      if ( ims_can_write2disk(ims) )
        ims->action = IM_ACTION_WRITETODISK;
      else
        ims->action = IM_ACTION_NOTHINGMORE;
    }
    else if ( ims->action == IM_ACTION_NOTHINGMORE ) {
      if ( ims_can_compress(ims) )
        ims->action = IM_ACTION_SHAREBLISTS_1;
      else if ( ims_can_write2disk(ims) )
        ims->action = IM_ACTION_SHAREBLISTS_2;
    }
  }
}

/* Determines if the store is uniform in color (each pixel in a plane is the
   same). */
Bool im_store_is_uniform(IM_STORE *ims, int32 iplane, uint16 *uniformcolor)
{
  int32 bx, by;
  Bool got_uniformcolor = FALSE;

  *uniformcolor = 0; /* arbitrarily */

  for ( by = 0; by < ims->yblock; ++by ) {
    for ( bx = 0; bx < ims->xblock; ++bx ) {
      int32 bb = im_bb_get_and_check(ims, bx, by, iplane);
      IM_BLOCK *block = ims->planes[iplane]->blocks[bb];

      if ( block ) { /* null blocks result from trimming */
        uint16 cv;

        /* If the block is uniform the block memory is not freed.
           This is because the image adjustment code may reuse the
           image store thus requiring the block data memory to be
           present.  This restriction can be removed when we have
           image adjustment by block. */
        if ( !im_blockUniform(ims, block, FALSE /* don't free */) )
          return FALSE;

        cv = im_blockUniformColor(block);

        /* For less than 8 bpp, im_blockUniformColor returns a uniform
           color packing multiple samples to fill up a byte.  We need
           to pick out just one sample and check all samples within the
           byte match. */
        if ( ims->bpp < 8 ) {
          COLORVALUE cv_single = cv & ((1 << ims->bpp) - 1), cv_repack = cv_single;

          switch ( ims->bpp ) {
          case 1 : cv_repack = cv_repack << 1 | cv_repack; /* fall through */
          case 2 : cv_repack = cv_repack << 2 | cv_repack; /* fall through */
          case 4 : cv_repack = cv_repack << 4 | cv_repack;
          }
          if ( cv_repack != cv )
            return FALSE;

          cv =  cv_single;
        }

        if ( !got_uniformcolor ) {
          got_uniformcolor = TRUE;
          *uniformcolor = cv;
        }
        else if ( *uniformcolor != cv ) {
          return FALSE;
        }
      }
    }
  }

  return TRUE;
}

Bool im_store_uniformbox_iterator(IM_STORE *ims, int *iblock, ibbox_t *ibbox,
                                  Bool *uniform, uint16 *color)
{
  IM_PLANE *plane ;
  IM_BLOCK *block ;
  int irow, icol ;

  HQASSERT(*iblock >= 0 && *iblock < ims->nblocks, "iblock is out of range") ;
  HQASSERT(ibbox != NULL, "Nowhere to store uniform bbox") ;
  HQASSERT(ims->nplanes == 1, "im_store_uniformbox_iterator expects only 1 image plane") ;

  plane = ims->planes[0] ;
  HQASSERT(plane, "Missing image plane") ;
  HQASSERT(plane->blocks, "Missing image blocks") ;

  block = plane->blocks[*iblock] ;

  /** \todo don't free a uniform block's data because then a blist will be
      required for subsequent rendering.  If images were rendered by block
      instead of scanline then uniform blocks could be handled much more
      efficiently. */
  if ( im_blockUniform(ims, block, FALSE /* im_blockblist(block) == NULL */) ) {
    *uniform = TRUE ;
    *color = im_blockUniformColor(block) ;
  } else {
    *uniform = FALSE ;
    *color = 0 ; /* should not be used, but clear value anyway */
  }

  irow = *iblock / ims->xblock;
  icol = *iblock % ims->xblock;

  /* Use the first block for x1,y1 calculation as 'block' may not
     not be full width and height. */
  ibbox->x1 = ims->obbox.x1 + icol * im_blockwidth(plane->blocks[0]) ;
  ibbox->y1 = ims->obbox.y1 + irow * im_blockheight(plane->blocks[0]) ;
  ibbox->x2 = ibbox->x1 + im_blockwidth(block) - 1 ;
  ibbox->y2 = ibbox->y1 + im_blockheight(block) - 1 ;

  return ++(*iblock) < ims->nblocks ;
}

int32 im_storenplanes(const IM_STORE* ims)
{
  return ims->nplanes;
}

void init_C_globals_imstore(void)
{
#if defined(DEBUG_BUILD)
  debug_imstore = 0;
#endif
#ifdef METRICS_BUILD
  im_store_metrics_reset(SW_METRICS_RESET_BOOT) ;
  sw_metrics_register(&im_store_metrics_hook);
#endif
}

#include "swflt.h"

/*
 * Image filtering code has been removed. So just leave a stub that returns
 * hard coded fail until filtering is re-implemented.
 */
sw_api_result RIPCALL SwRegisterFLT(sw_flt_api *api)
{
  UNUSED_PARAM(sw_flt_api *, api);
  return FAILURE(SW_API_INCOMPLETE);
}


/* Log stripped */
