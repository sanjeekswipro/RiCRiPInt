/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:irr.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2012-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * - The Internal Retained Raster band store.
 * - The IRR page buffer device used to create the band store.
 * - The IRR state and related callbacks to control IRR store generating,
 *   replaying and caching.
 */

#include "core.h"
#include "irr.h"
#include "coreinit.h"
#include "devices.h"
#include "devs.h"
#include "swraster.h"
#include "swerrors.h"
#include "swevents.h"
#include "swtimelines.h"
#include "mm.h"
#include "mmcompat.h"
#include "hqmemcpy.h"
#include "gu_chan.h"
#include "hdl.h"
#include "display.h"
#include "corejob.h"
#include "objnamer.h"
#include "zlib.h"
#include "params.h"
#include "lowmem.h"
#include "swstart.h"
#include "imfile.h"
#include "mlock.h"
#include "swtrace.h"
#include "spdetect.h"
#include "bandtable.h"
#include "pgbproxy.h"

#if defined( ASSERT_BUILD )
/* Report store footprint and band reads, writes and purges. */
static Bool irr_debug = FALSE;
#endif

/* Mutex for low-memory purging. */
static multi_mutex_t irr_mutex;

/** Band storage for page elements in IRR.  When replaying, the bands are copied
    back in the output band to save repeatedly rendering the same content on
    multiple pages.  Bands may be compressed and may be in memory or on disk. */

typedef struct irr_band_t {
  int32 size; /**< Non-zero for bands intersecting bbox. */
  uint8 *mem; /**< Allocated lazily, and null if band purged to disk. */
  Bool locked; /**< Set when reading or writing band, to stop it being purged. */
  IM_FILES *file; /**< Present when the band has been purged to disk. */
  int32 fileoffset; /**< File offset for this band into file. */
} irr_band_t;

struct irr_store_t {
  dbbox_t bbox; /**< Only interested in storing bands within this area. */
  mm_pool_t pool; /**< For the store and its bands. */
  uint32 ncomps_alloc, ncomps; /**< ncomps excludes duplicates. */
  COLORANTINDEX *colorants; /**< Array of colorant indices in the store. */
  int32 num_separations; /**< Number of separations. */
  int32 sep_map_len; /**< Length of sep_map. */
  int32 *sep_map; /**< Mapping from renderer sep index to store sep index. */
  int32 frames_per_sheet; /**< Number of frames per sheet. */
  int32 bands_per_frame; /**< Number of bands per frame. */
  int32 nbands; /**< Number of bands allocated. */
  irr_band_t *bands; /**< [nbands] */
  int32 compression_level; /**< Optional flate compression level for bands. */
  int32 iband_purge; /**< Next band to purge in low memory. */
  IM_FILE_CTXT *purge_ctxt; /**< List of files containing the bands on disk. */
  uint8 *temp_band; /**< Temp band required when reading compressed band from disk. */
  int32 temp_band_size; /**< Yep, you guessed it. */
  Bool page_is_composite, page_is_separations, multiple_separations;
    /**< Separation detection flags. */
  Bool *omits; /**< Blank separation omit flags (per colorant). */
};

void irr_store_free(irr_store_t **free_store)
{
  irr_store_t *store = *free_store;

  if ( store == NULL )
    return;
  if ( store->colorants != NULL )
    mm_free(store->pool, store->colorants,
            store->ncomps_alloc * sizeof(*store->colorants));
  if ( store->sep_map != NULL )
    mm_free(store->pool, store->sep_map,
            store->sep_map_len * sizeof(*store->sep_map));
  if ( store->bands != NULL ) {
    int32 iband;
    for ( iband = 0; iband < store->nbands; ++iband ) {
      irr_band_t *band = &store->bands[iband];
      if ( band->mem != NULL )
        mm_free(store->pool, band->mem, band->size);
    }
    mm_free(store->pool, store->bands, store->nbands * sizeof(*store->bands));
  }
  im_filedestroyctxt(&store->purge_ctxt);
  if ( store->temp_band != NULL )
    mm_free(store->pool, store->temp_band, store->temp_band_size);
  if ( store->omits != NULL )
    mm_free(store->pool, store->omits,
            store->ncomps_alloc * sizeof(*store->omits));
  mm_free(store->pool, store, sizeof(*store));
  *free_store = NULL;
}

static Bool irr_store_create(DL_STATE *page, mm_pool_t pool, dbbox_t *bbox,
                             irr_store_t **new_store)
{
  irr_store_t *store;
  uint32 i;
  int32 j;
  GUCR_CHANNEL *hf;

  store = *new_store = mm_alloc(pool, sizeof(*store), MM_ALLOC_CLASS_IRR);
  if ( store == NULL )
    return error_handler(VMERROR);

  store->bbox = *bbox;
  store->pool = pool;
  gucr_colorantCount(page->hr, &store->ncomps_alloc);
  store->ncomps = store->ncomps_alloc;
  store->colorants = NULL;
  store->num_separations = -1;
  store->sep_map_len = 0;
  store->sep_map = NULL;
  store->frames_per_sheet = 0;
  store->bands_per_frame = 0;
  store->nbands = 0;
  store->bands = NULL;
  store->compression_level = UserParams.RetainedRasterCompressionLevel;
  store->iband_purge = 0;
  store->purge_ctxt = NULL;
  store->temp_band = NULL;
  store->temp_band_size = 0;
  store->page_is_composite = FALSE;
  store->page_is_separations = FALSE;
  store->multiple_separations = FALSE;
  store->omits = NULL;

  if ( (store->colorants = mm_alloc(store->pool,
                                    store->ncomps_alloc * sizeof(*store->colorants),
                                    MM_ALLOC_CLASS_IRR)) == NULL ||
       (store->omits = mm_alloc(store->pool,
                                store->ncomps_alloc * sizeof(*store->omits),
                                MM_ALLOC_CLASS_IRR)) == NULL )
    return irr_store_free(new_store), error_handler(VMERROR);

  gucr_colorantIndices(page->hr, store->colorants, &store->ncomps);

  /* Separation omission flags for the invariant data are set up later. */
  for ( i = 0; i < store->ncomps; ++i ) {
    store->omits[i] = FALSE;
  }

  /* For mapping separation indices from the renderer on to store indices.  The
     indices from the renderer may be sparse after separation omission. */
  for ( hf = gucr_framesStart(page->hr); gucr_framesMore(hf);
        gucr_framesNext(&hf) ) {
    int32 sheet_number;

    if ( gucr_framesStartOfSheet(hf, NULL, &sheet_number) ) {
      if ( sheet_number + 1 > store->sep_map_len )
        store->sep_map_len = sheet_number + 1;
    }
  }

  if ( (store->sep_map
        = mm_alloc(store->pool,
                   store->sep_map_len * sizeof(*store->sep_map),
                   MM_ALLOC_CLASS_IRR)) == NULL )
    return irr_store_free(new_store), error_handler(VMERROR);

  for ( j = 0; j < store->sep_map_len; ++j ) {
    store->sep_map[j] = -1;
  }

  if ( !im_filecreatectxt(&store->purge_ctxt, FALSE) )
    return irr_store_free(new_store), FALSE;

  return TRUE;
}

/** Footprint is returned in KB, because size_t may overflow at high resolutions
    when storing bytes.  The footprint is valid only when all bands have been
    written and compressed. */
size_t irr_store_footprint(irr_store_t *store)
{
  size_t footprint;
  int32 iband;

  footprint = (sizeof(irr_store_t)
               + store->ncomps_alloc * sizeof(*store->colorants)
               + store->ncomps_alloc * sizeof(*store->omits)
               + store->nbands * sizeof(*store->bands)) >> 10;

  for ( iband = 0; iband < store->nbands; ++iband ) {
    footprint += store->bands[iband].size >> 10;
  }
  footprint += store->temp_band_size >> 10;
  return footprint;
}

static void *zlib_alloc(void *opaque, uint32 items, uint32 size)
{
  UNUSED_PARAM(void *, opaque);
  /* vmerrors are handled in the client */
  return mm_alloc_with_header(mm_pool_temp, items * size,
                              MM_ALLOC_CLASS_IRR);
}

static void zlib_free(void *opaque, void *p)
{
  UNUSED_PARAM(void *, opaque);
  mm_free_with_header(mm_pool_temp, p);
}

static int32 band_compress(irr_store_t *store, irr_band_t *band,
                           uint8 *buff, int32 len)
{
  z_stream z_state;
  int z_result;
  int32 compressedsize;

  z_state.zalloc = zlib_alloc;
  z_state.zfree = zlib_free;
  z_state.opaque = Z_NULL;
  z_state.next_in = buff;
  z_state.avail_in = len;
  z_state.next_out = band->mem;
  z_state.avail_out = CAST_SIGNED_TO_UINT32(band->size);

  if ( deflateInit(&z_state, store->compression_level) != Z_OK )
    return devices_set_last_error(DeviceIOError), -1;

  z_result = deflate(&z_state, Z_FINISH);

  compressedsize = band->size - CAST_UNSIGNED_TO_INT32(z_state.avail_out);
  (void)deflateEnd(&z_state);

  if ( z_result != Z_STREAM_END ) {
    if ( z_result == Z_MEM_ERROR )
      return devices_set_last_error(DeviceVMError), -1;
    return devices_set_last_error(DeviceIOError), -1;
  }

  HQASSERT(z_state.avail_in == 0,
           "Should have compressed it all in one go");

  mm_truncate(store->pool, band->mem, band->size, compressedsize);

  band->size = compressedsize;

  return 0;
}

static Bool band_decompress(irr_store_t *store, irr_band_t *band,
                             uint8 *buff, int32 len)
{
  z_stream z_state;
  int z_result;

  HQASSERT(len <= store->temp_band_size, "temp_band too small");
  z_state.zalloc = zlib_alloc;
  z_state.zfree = zlib_free;
  z_state.opaque = Z_NULL;
  z_state.next_in = band->mem != NULL ? band->mem : store->temp_band;
  z_state.avail_in = CAST_SIGNED_TO_UINT32(band->size);
  z_state.next_out = buff;
  z_state.avail_out = len;

  if ( inflateInit(&z_state) != Z_OK )
    return error_handler(CONFIGURATIONERROR);

  z_result = inflate(&z_state, Z_FINISH);

  if ( z_result != Z_STREAM_END ) {
    if (z_result == Z_MEM_ERROR)
      return error_handler(VMERROR);
    return error_handler(CONFIGURATIONERROR);
  }

  (void)inflateEnd(&z_state);

  HQASSERT(z_state.avail_in == 0 && z_state.avail_out == 0,
           "Should have decompressed it all in one go");
  return TRUE;
}

/** Cost of IRR stores purges.

  @@@@ Currently really determined by mmlow.c.
 */
static mm_cost_t irr_store_purge_cost = { memory_tier_ram, 2.0 };

static irr_store_t *irr_get_store(DL_STATE *page);

static low_mem_offer_t *irr_store_purge_solicit(low_mem_handler_t *handler,
                                                corecontext_t *corecontext,
                                                size_t count,
                                                memory_requirement_t *requests)
{
  static low_mem_offer_t offer;
  irr_store_t *store = irr_get_store(corecontext->page);

  UNUSED_PARAM(low_mem_handler_t*, handler);
  UNUSED_PARAM(size_t, count);
  UNUSED_PARAM(memory_requirement_t*, requests);

  if ( !multi_mutex_trylock(&irr_mutex) )
    return NULL; /* give up if can't get the lock */

  offer.pool = NULL;
  offer.offer_size = (store != NULL &&
                      store->iband_purge < store->nbands &&
                      !store->bands[store->iband_purge].locked &&
                      store->bands[store->iband_purge].mem != NULL
                      ? store->bands[store->iband_purge].size : 0);
  offer.offer_cost = irr_store_purge_cost.value;
  offer.next = NULL;

  multi_mutex_unlock(&irr_mutex);

  return offer.offer_size > 0 ? &offer : NULL;
}

static Bool irr_store_purge_release(low_mem_handler_t *handler,
                                    corecontext_t *corecontext,
                                    low_mem_offer_t *offer)
{
  Bool purged = FALSE;
  irr_store_t *store = irr_get_store(corecontext->page);

  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(low_mem_offer_t*, offer);

  if ( !multi_mutex_trylock(&irr_mutex) )
    return purged; /* give up if can't get the lock */

  if ( store != NULL &&
       store->iband_purge < store->nbands &&
       !store->bands[store->iband_purge].locked &&
       store->bands[store->iband_purge].mem != NULL ) {
    irr_band_t *band = &store->bands[store->iband_purge];
    IM_FILES *file = NULL;
    int32 fileoffset = 0;

    if ( im_fileoffset(store->purge_ctxt, 0, band->size, &file, &fileoffset) &&
         im_fileseek(file, fileoffset) &&
         im_filewrite(file, band->mem, band->size) ) {
      /* Band successfully written to disk. */
      band->file = file;
      band->fileoffset = fileoffset;

      mm_free(store->pool, band->mem, band->size);
      band->mem = NULL;

      HQTRACE(irr_debug, ("IRR purge: band %d, free size %d",
                          store->iband_purge, band->size));
      purged = TRUE;
    } else {
      /* Something went wrong, but only trying to purge band so ignore error. */
      error_clear_context(corecontext->error);
    }

    store->iband_purge = (store->iband_purge + 1 < store->nbands
                          ? store->iband_purge + 1 : 0);
  }

  multi_mutex_unlock(&irr_mutex);

  return purged;
}

static low_mem_handler_t irr_store_purge_handler = {
  "IRR store",
  memory_tier_ram, irr_store_purge_solicit, irr_store_purge_release, TRUE,
  0, FALSE
};

static Bool irr_store_swinit(SWSTART *params)
{
  UNUSED_PARAM(SWSTART *, params);

  if ( !low_mem_handler_register(&irr_store_purge_handler) )
    return FALSE;

  multi_mutex_init(&irr_mutex, IRR_LOCK_INDEX, TRUE /* recursive */,
                   SW_TRACE_IRR_ACQUIRE, SW_TRACE_IRR_HOLD);

  return TRUE;
}

static void irr_store_finish(void)
{
  multi_mutex_finish(&irr_mutex);

  low_mem_handler_deregister(&irr_store_purge_handler);
}

/** Called during replaying, to read directly from the IRR store, when there is
    no IRR page buffer device, and also on the second render pass by the IRR
    page buffer device.  do_copy (replaying only) is for band-interleaved output
    which calls the read back for each component, but all components are read
    back on the first call and subsequent calls must not write to buff. */
Bool irr_read_back_band(irr_store_t *store,
                        int32 separation, int32 frame_and_bandnum,
                        uint8 *buff, int32 len, Bool do_copy, Bool *do_erase)
{
  Bool result = FALSE, mutex_locked = FALSE;
  int32 iband;
  irr_band_t *band;

  *do_erase = TRUE;

  if ( store == NULL )
    return TRUE; /* Not replaying, normal erase required. */

  if ( separation >= store->sep_map_len || store->sep_map[separation] == -1 )
    return TRUE; /* Separation not stored, normal erase required. */

  iband = store->sep_map[separation] * store->bands_per_frame
          * store->frames_per_sheet + frame_and_bandnum;
  HQASSERT(0 <= iband && iband < store->nbands, "band out of range");
  band = &store->bands[iband];

  /* Ignore bands which were never written. */
  if ( band->mem == NULL && band->file == NULL )
    return TRUE; /* Band not stored, normal erase required. */

  if ( !do_copy ) {
    *do_erase = FALSE;
    return TRUE;
  }

  /* Need to hold the irr_mutex while setting the band locked flag and while we
     do any disk I/O, or decompression into shared temp_band.  Don't need the
     mutex to clear the locked flag. */
  multi_mutex_lock(&irr_mutex);
  HQASSERT(!band->locked, "Band already locked");
  band->locked = mutex_locked = TRUE;
#define return USE_goto_cleanup

  /* If the band is uncompressed read straight into buff and skip the copy;
     otherwise read into temp_band before decompressing. */
  if ( band->mem == NULL ) {
    if ( !im_fileseek(band->file, band->fileoffset) ||
         !im_fileread(band->file,
                      store->compression_level == 0 ? buff : store->temp_band,
                      band->size) )
      goto cleanup;
  }

  /* Relinquish lock early if not using temp_band, so decompression or copy can
     happen in parallel for different bands. */
  if ( band->mem != NULL || store->compression_level == 0 ) {
    multi_mutex_unlock(&irr_mutex);
    mutex_locked = FALSE;
  }

  if ( store->compression_level > 0 ) {
    if ( !band_decompress(store, band, buff, len) )
      goto cleanup;
  } else if ( band->mem != NULL ) {
    HQASSERT(band->size == len, "IRR band is the wrong size");
    HqMemCpy(buff, band->mem, len);
  }

  HQTRACE(irr_debug, ("IRR read back band: %s, separation %d, "
                      "frame_and_bandnum %d, len %d, size %d",
                      band->mem != NULL ? "in memory" : "on disk",
                      separation, frame_and_bandnum, len, band->size));

  /* Band stored and read back, skip erase. */
  *do_erase = FALSE;

  result = TRUE;
 cleanup:
  if ( mutex_locked )
    multi_mutex_unlock(&irr_mutex);
  band->locked = FALSE;
#undef return
  return result;
}

/* -------------------------------------------------------------------------- */

#define IRR_PGB_DEVICE_TYPE 38

#define IRR_PGB_NAME "IRR page buffer"

typedef struct {
  DEVICELIST *real_pgb; /**< The real page buffer is reinstated by irr_finish. */
  int32 imageheight, bandheight, bandwidth, nchannels, interleavingstyle,
    separation, band_lines; /**< Set by set_param. */
  int32 frame_and_line; /**< Set by seek_file. */
  irr_store_t *store; /**< The thing we're making. */
  OBJECT_NAME_MEMBER
} irr_pgb_t;

static irr_pgb_t *irr_pgb_get(DEVICELIST *dev)
{
  irr_pgb_t *pgb = (irr_pgb_t*)dev->private_data;
  HQASSERT(theIDevTypeNumber(theIDevType(dev)) == IRR_PGB_DEVICE_TYPE,
           "Expected IRR pgb device");
  VERIFY_OBJECT(pgb, IRR_PGB_NAME);
  return pgb;
}

static int32 RIPCALL irr_pgb_init_device(DEVICELIST *dev)
{
  irr_pgb_t *pgb = (irr_pgb_t*)dev->private_data;

  pgb->real_pgb = NULL;
  pgb->nchannels = 0;
  pgb->interleavingstyle = 0;
  pgb->imageheight = 0;
  pgb->bandheight = 0;
  pgb->bandwidth = 0;
  pgb->separation = -1;
  pgb->band_lines = 0;
  pgb->frame_and_line = 0;
  pgb->store = NULL;
  NAME_OBJECT(pgb, IRR_PGB_NAME); /* Now ok to call irr_pgb_get */

#ifdef DEBUG_BUILD
  /* Debug, we don't care if it's not registered properly. */
  (void)SwRegisterRDR(RDR_CLASS_TIMELINE, TL_DEBUG_TYPE_NAME,
                      SWTLT_PGB, STRING_AND_LENGTH(IRR_PGB_NAME),
                      SW_RDR_NORMAL);
#endif

  devices_set_last_error(DeviceNoError);
  return 0;
}

static inline int32 irr_frame_and_band(irr_pgb_t *pgb)
{
  return (pgb->frame_and_line / pgb->imageheight) * pgb->store->bands_per_frame +
    (pgb->frame_and_line % pgb->imageheight) / pgb->bandheight;
}

static DEVICE_FILEDESCRIPTOR RIPCALL irr_pgb_open_file(DEVICELIST *dev,
                                                       uint8 *filename,
                                                       int32 openflags)
{
  irr_pgb_t *pgb = irr_pgb_get(dev);
  irr_store_t *store = pgb->store;
  sw_tl_ref tl;

  UNUSED_PARAM(int32, openflags);

  /* Only create the band store on the first sheet of the page. */
  if ( store->bands == NULL ) {
    int32 iband;
    int32 n_channels_in_band = 1;
    int32 n_frames_per_sheet = 1;
    int32 band_size = (pgb->bandwidth / 8) * pgb->bandheight;

    switch (pgb->interleavingstyle) {
    case 1: /* mono */  break;
    case 2: /* pixel */ break;
    case 3: /* band */
      n_channels_in_band = pgb->nchannels;
      band_size *= n_channels_in_band;
      break;
    case 4: /* frame */
      n_frames_per_sheet = pgb->nchannels;
      break;
    default:
      HQFAIL("unrecognized interleaving style");
    }

    store->bands_per_frame = ((pgb->imageheight + pgb->bandheight - 1)
                              / pgb->bandheight);
    store->frames_per_sheet = n_frames_per_sheet;

    HQASSERT(store->num_separations != -1,
             "NumSeparations pgb param must be set by now");
    store->nbands = store->bands_per_frame * store->frames_per_sheet
      * store->num_separations;

    if ( (store->bands = mm_alloc(store->pool,
                                  store->nbands * sizeof(*store->bands),
                                  MM_ALLOC_CLASS_IRR)) == NULL ) {
      devices_set_last_error(DeviceVMError);
      return -1;
    }

    for ( iband = 0; iband < store->nbands; ++iband ) {
      irr_band_t *band = &store->bands[iband];

      /* Bands are allocated lazily in irr_pgb_write_file. */
      band->size = 0;
      band->mem = NULL;
      band->locked = FALSE;
      band->file = NULL;
      band->fileoffset = -1;
    }

    /* A temp band is required to read a band from disk before decompressing. */
    if ( store->compression_level > 0 ) {
      store->temp_band_size = band_size;
      if ( (store->temp_band = mm_alloc(store->pool, store->temp_band_size,
                                        MM_ALLOC_CLASS_IRR)) == NULL ) {
        devices_set_last_error(DeviceVMError);
        return -1;
      }
    }
  }

  /** \todo ajcd 2011-07-22: We'd like this to be a child of the RENDER_PAGE
      timeline from the core. How can we do this? Probably use an event
      handler to capture RENDER_PAGE start/end into a variable. */
  if ( (tl = SwTimelineStart(SWTLT_PGB, SW_TL_REF_INVALID,
                             0 /*start*/,
                             /** \todo ajcd 2011-07-22: We know how
                                 many bands are output, use it. */
                             SW_TL_EXTENT_INDETERMINATE,
                             SW_TL_UNIT_BANDS, SW_TL_PRIORITY_NORMAL,
                             NULL /*context*/,
                             filename, filename ? strlen((const char *)filename) : 0))
       == SW_TL_REF_INVALID ) {
    devices_set_last_error(DeviceVMError);
    return -1;
  }

  devices_set_last_error(DeviceNoError);
  return tl;
}

static int32 RIPCALL irr_pgb_read_file(DEVICELIST *dev,
                                       DEVICE_FILEDESCRIPTOR descriptor,
                                       uint8 *buff, int32 len)
{
  irr_pgb_t *pgb = irr_pgb_get(dev);
  Bool dummy_do_erase;

  if ( (sw_tl_ref)descriptor == SW_TL_REF_INVALID )
    return devices_set_last_error(DeviceIOError), -1;

  if ( !irr_read_back_band(pgb->store, pgb->separation,
                           irr_frame_and_band(pgb),
                           buff, len, TRUE, &dummy_do_erase) )
    return devices_set_last_error(DeviceIOError), -1; /** \todo translate error code? */

  devices_set_last_error(DeviceNoError);
  return len;
}

static int32 RIPCALL irr_pgb_write_file(DEVICELIST *dev,
                                        DEVICE_FILEDESCRIPTOR descriptor,
                                        uint8 *buff, int32 len)
{
  irr_pgb_t *pgb = irr_pgb_get(dev);
  sw_tl_ref tl = (sw_tl_ref)descriptor;
  irr_store_t *store = pgb->store;
  int32 iband;
  irr_band_t *band;

  if ( tl == SW_TL_REF_INVALID )
    return devices_set_last_error(DeviceIOError), -1;

  iband = (store->sep_map[pgb->separation] * store->frames_per_sheet
           * store->bands_per_frame + irr_frame_and_band(pgb));
  HQASSERT(0 <= iband && iband < store->nbands, "band out of range");
  band = &store->bands[iband];

  multi_mutex_lock(&irr_mutex);
  HQASSERT(!band->locked, "Band already locked");
  band->locked = TRUE;
#define return USE_goto_cleanup

  /* Bands are allocated lazily, but a band may exist from a previous render
     pass.  If the band was compressed it is likely to need reallocating. */
  if ( band->size != len && band->mem != NULL ) {
    mm_free(store->pool, band->mem, band->size);
    band->mem = NULL;
  }
  if ( band->mem == NULL ) {
    if ( (band->mem = mm_alloc(store->pool, len,
                               MM_ALLOC_CLASS_IRR)) == NULL ) {
      devices_set_last_error(DeviceVMError); len = -1; goto cleanup;
    }
    band->size = len;
  }

  if ( store->compression_level > 0 ) {
    if ( band_compress(store, band, buff, len) == -1 ) {
      len = -1; goto cleanup;
    }
  } else {
    HQASSERT(band->size == len, "IRR band is the wrong size");
    HqMemCpy(band->mem, buff, len);
  }

  (void)SwTimelineSetProgress(tl, (sw_tl_extent)(pgb->frame_and_line +
                                                 pgb->band_lines));

  store->iband_purge = iband;

  HQTRACE(irr_debug, ("IRR page buffer write: separation %d, "
                      "frame_and_line %d, lines %d, len %d, size %d",
                      pgb->separation, pgb->frame_and_line, pgb->band_lines,
                      len, band->size));

  devices_set_last_error(DeviceNoError);

 cleanup:
  band->locked = FALSE;
  multi_mutex_unlock(&irr_mutex);
#undef return
  return len;
}

static int32 RIPCALL irr_pgb_close_file(DEVICELIST *dev,
                                        DEVICE_FILEDESCRIPTOR descriptor)
{
  sw_tl_ref tl = (sw_tl_ref)descriptor;

  UNUSED_PARAM(DEVICELIST *, dev);

  if ( tl == SW_TL_REF_INVALID )
    return devices_set_last_error(DeviceIOError), -1;

  (void)SwTimelineEnd(tl);

  devices_set_last_error(DeviceNoError);
  return 0;
}

static int32 RIPCALL irr_pgb_abort_file(DEVICELIST *dev,
                                        DEVICE_FILEDESCRIPTOR descriptor)
{
  sw_tl_ref tl = (sw_tl_ref)descriptor;

  UNUSED_PARAM(DEVICELIST *, dev);

  if ( tl != SW_TL_REF_INVALID )
    (void)SwTimelineAbort(tl, 0);

  devices_set_last_error(DeviceNoError);
  return 0;
}

static int32 RIPCALL irr_pgb_seek_file(DEVICELIST *dev,
                                       DEVICE_FILEDESCRIPTOR descriptor,
                                       Hq32x2 *destination, int32 flags)
{
  irr_pgb_t *pgb = irr_pgb_get(dev);
  int32 offset;

  if ( (sw_tl_ref)descriptor == SW_TL_REF_INVALID )
    return devices_set_last_error(DeviceIOError), FALSE;

  if ( !Hq32x2ToInt32(destination, &offset) )
    return devices_set_last_error(DeviceLimitCheck), FALSE;

  switch ( flags ) {
  case SW_SET:
    break;
  case SW_INCR:
    offset += pgb->frame_and_line;
    break;
  case SW_XTND:
    offset += pgb->imageheight * pgb->store->frames_per_sheet;
    break;
  default:
    devices_set_last_error(DeviceIOError);
    return FALSE;
  }

  /* We allow seeks to any line. */
  pgb->frame_and_line = offset;

  Hq32x2FromInt32(destination, offset);

  devices_set_last_error(DeviceNoError);
  return TRUE;
}

static int32 RIPCALL irr_pgb_bytes_file(DEVICELIST *dev,
                                        DEVICE_FILEDESCRIPTOR descriptor,
                                        Hq32x2 *bytes, int32 reason)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);

  if ( reason == SW_BYTES_AVAIL_REL ) {
    devices_set_last_error(DeviceNoError);
    Hq32x2FromInt32(bytes, 0);
    return TRUE;
  }

  devices_set_last_error(DeviceIOError);
  return FALSE;
}

static int32 RIPCALL irr_pgb_status_file(DEVICELIST *dev, uint8 *filename,
                                         STAT *statbuff)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, filename);
  UNUSED_PARAM(STAT *,statbuff);

  devices_set_last_error(DeviceIOError);
  return -1;
}

static void *RIPCALL irr_pgb_start_list(DEVICELIST *dev, uint8 *pattern)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, pattern);

  devices_set_last_error(DeviceNoError);
  return NULL;
}

static int32 RIPCALL irr_pgb_next_list(DEVICELIST *dev, void **handle,
                                       uint8 *pattern, FILEENTRY *entry)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(void **, handle);
  UNUSED_PARAM(uint8 *, pattern);
  UNUSED_PARAM(FILEENTRY *, entry);

  return FileNameNoMatch;
}

static int32 RIPCALL irr_pgb_end_list(DEVICELIST *dev, void *handle)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(void *, handle);

  devices_set_last_error(DeviceNoError);
  return 0;
}

static int32 RIPCALL irr_pgb_rename_file(DEVICELIST *dev,
                                         uint8 *file1, uint8 *file2)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, file1);
  UNUSED_PARAM(uint8 *, file2);

  devices_set_last_error(DeviceIOError);
  return 0;
}

static int32 RIPCALL irr_pgb_delete_file(DEVICELIST *dev, uint8 *filename)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, filename);

  devices_set_last_error(DeviceIOError);
  return -1;
}

#define PARAM_EQ(param_, string_) \
  ((param_)->paramnamelen == sizeof("" string_ "") - 1 && \
   strncmp((char *)(param_)->paramname, string_, sizeof("" string_ "") - 1) == 0)

static int32 RIPCALL irr_pgb_set_param(DEVICELIST *dev, DEVICEPARAM *param)
{
  int32 result = ParamIgnored;

  if ( param->type == ParamInteger ) {
    irr_pgb_t *pgb = irr_pgb_get(dev);
    irr_store_t *store = pgb->store;

    if ( PARAM_EQ(param, "Separation") ) {
      pgb->separation = param->paramval.intval - 1;
      HQASSERT(pgb->separation < store->sep_map_len, "Unexpected separation index");
      if ( store->sep_map[pgb->separation] == -1 ) {
        /* Find the next free separation index in the store. */
        int32 i, mapped_sep = 0;
        for ( i = 0; i < store->sep_map_len; ++i ) {
          if ( store->sep_map[i] != -1 )
            ++mapped_sep;
        }
        store->sep_map[pgb->separation] = mapped_sep;
      }
      result = ParamAccepted;
    } else if ( PARAM_EQ(param, "BandLines") ) {
      pgb->band_lines = param->paramval.intval;
      result = ParamAccepted;
    } else if ( PARAM_EQ(param, "NumSeparations") ) {
      HQASSERT(store->num_separations == -1 ||
               store->num_separations == param->paramval.intval,
               "Number of separations changed between sheets");
      store->num_separations = param->paramval.intval;
      result = ParamAccepted;
    } else if ( PARAM_EQ(param, "BandHeight") ) {
      pgb->bandheight = param->paramval.intval;
      result = ParamAccepted;
    } else if ( PARAM_EQ(param, "BandWidth") ) {
      pgb->bandwidth = param->paramval.intval;
      result = ParamAccepted;
    } else if ( PARAM_EQ(param, "ImageHeight") ) {
      pgb->imageheight = param->paramval.intval;
      result = ParamAccepted;
    } else if ( PARAM_EQ(param, "InterleavingStyle") ) {
      pgb->interleavingstyle = param->paramval.intval;
      result = ParamAccepted;
    } else if ( PARAM_EQ(param, "NumChannels") ) {
      pgb->nchannels = param->paramval.intval;
      result = ParamAccepted;
    }
  }

  devices_set_last_error(DeviceNoError);
  return result;
}

static int32 RIPCALL irr_pgb_start_param(DEVICELIST *dev)
{
  UNUSED_PARAM(DEVICELIST *, dev);

  devices_set_last_error(DeviceNoError);
  return 0;
}

static int32 RIPCALL irr_pgb_get_param(DEVICELIST *dev, DEVICEPARAM *param)
{
  int32 len = param->paramnamelen;
  int32 result = ParamIgnored;

  UNUSED_PARAM(DEVICELIST *, dev);

  /* Intercept the RIPCanCompress get param request and turn compression off.
     The IRR page buffer device handles compression for itself. */
  if ( param->type == ParamBoolean &&
       len == strlen_int32("RIPCanCompress") &&
       strncmp((char*)param->paramname, "RIPCanCompress", len) == 0 ) {
    param->paramval.boolval = FALSE;
    result = ParamAccepted;
  }

  devices_set_last_error(DeviceNoError);
  return result;
}

static int32 RIPCALL irr_pgb_status_device(DEVICELIST *dev, DEVSTAT *devstat)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVSTAT *, devstat);
  devices_set_last_error(DeviceNoError);
  return 0;
}

static int32 RIPCALL irr_pgb_dismount_device(DEVICELIST *dev)
{
  UNUSED_PARAM(DEVICELIST *, dev);
#ifdef DEBUG
  (void)SwDeregisterRDR(RDR_CLASS_TIMELINE, TL_DEBUG_TYPE_NAME,
                        SWTLT_PGB, STRING_AND_LENGTH(IRR_PGB_NAME));
#endif
  return 0;
}

static int32 RIPCALL irr_pgb_buffer_size(DEVICELIST *dev)
{
  UNUSED_PARAM(DEVICELIST *, dev);

  devices_set_last_error(DeviceIOError);
  return -1;
}

static int32 RIPCALL irr_pgb_ioctl_device(DEVICELIST *dev,
                                          DEVICE_FILEDESCRIPTOR descriptor,
                                          int32 opcode, intptr_t arg)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);
  HQASSERT(descriptor == (DEVICE_FILEDESCRIPTOR)-1,
           "File descriptor for IOCtl unexpected");

  switch ( opcode ) {
  case DeviceIOCtl_RasterStride:
  case DeviceIOCtl_GetBufferForRaster:
    HQFAIL("Don't expect to see these opcodes in the IRR pbg device");
    break;
  case DeviceIOCtl_RenderingStart:
  case DeviceIOCtl_RasterRequirements:
  default: {
    RASTER_REQUIREMENTS *req = (RASTER_REQUIREMENTS*)arg;
    req->have_framebuffer = FALSE;
    req->handled = TRUE;
    break;
   }
  }

  devices_set_last_error(DeviceNoError);
  return 0;
}

static int32 RIPCALL irr_pgb_spare(void)
{
  return 0;
}

DEVICETYPE IRR_PGB_Device_Type = {
  IRR_PGB_DEVICE_TYPE,     /**< The device ID number */
  DEVICEWRITABLE,          /**< Flags to indicate specifics of device */
  sizeof(irr_pgb_t),       /**< The size of the private data */
  0,                       /**< Minimum ticks between tickle functions */
  NULL,                    /**< Procedure to service the device */
  devices_last_error,      /**< Return last error for this device */
  irr_pgb_init_device,     /**< Call to initialize device */
  irr_pgb_open_file,       /**< Call to open file on device */
  irr_pgb_read_file,       /**< Call to read data from file on device */
  irr_pgb_write_file,      /**< Call to write data to file on device */
  irr_pgb_close_file,      /**< Call to close file on device */
  irr_pgb_abort_file,      /**< Call to abort action on the device */
  irr_pgb_seek_file,       /**< Call to seek file on device */
  irr_pgb_bytes_file,      /**< Call to get bytes avail on an open file */
  irr_pgb_status_file,     /**< Call to check status of file */
  irr_pgb_start_list,      /**< Call to start listing files */
  irr_pgb_next_list,       /**< Call to get next file in list */
  irr_pgb_end_list,        /**< Call to end listing */
  irr_pgb_rename_file,     /**< Rename file on the device */
  irr_pgb_delete_file,     /**< Remove file from device */
  irr_pgb_set_param,       /**< Call to set device parameter */
  irr_pgb_start_param,     /**< Call to start getting device parameters */
  irr_pgb_get_param,       /**< Call to get the next device parameter */
  irr_pgb_status_device,   /**< Call to get the status of the device */
  irr_pgb_dismount_device, /**< Call to dismount the device */
  irr_pgb_buffer_size,     /**< Call to return buffer size */
  irr_pgb_ioctl_device,    /**< ioctl slot */
  irr_pgb_spare,           /**< Spare slot */
};

static Bool irr_pgb_postboot(void)
{
  return device_type_add(&IRR_PGB_Device_Type);
}

void irr_pgb_C_globals(core_init_fns *fns)
{
  fns->swinit = irr_store_swinit;
  fns->finish = irr_store_finish;
  fns->postboot = irr_pgb_postboot;
}

/* -------------------------------------------------------------------------- */

struct irr_state_t {
  /** Memory pool from which all non-workspace allocations must come. */
  mm_pool_t pool;

  /** Bounding box. */
  dbbox_t bbox;

  /** An opaque reference to the context which owns this IRR queue item. */
  void *priv_context;

  /** An opaque reference to a key by which this item is identified within its
      owning context. */
  uintptr_t priv_id;

  /** \todo This is just so the store can be purged in low memory.  There should
      probably be a list of stores obtained from the IRR cache instead. */
  irr_store_t *store;

  /** Hook to call once the IRR band store has been completed. */
  Bool (*callback)(void *priv_context, uintptr_t priv_id, irr_store_t *store);
};

Bool irr_pgb_install(DL_STATE *page)
{
  DEVICELIST *irr_dev;
  irr_pgb_t *pgb;

  if ( !page->irr.generating )
    return TRUE;

  /** \todo Band indices are different for RLE output and IRR pgbdev would need
     to handle write appends, overflow etc. RLE output in general is currently
     under review anyway. */
  HQASSERT(!DOING_RUNLENGTH(page), "IRR pgbdev does not support RLE output");

  if ( (irr_dev = device_alloc(STRING_AND_LENGTH(IRR_PGB_NAME))) == NULL )
    return FALSE;

  if ( !device_connect(irr_dev, IRR_PGB_DEVICE_TYPE,
                       (char*)theIDevName(irr_dev),
                       0, TRUE) ) {
    device_free(irr_dev);
    return FALSE;
  }

  pgb = irr_pgb_get(irr_dev);

  /* Swap in the IRR page buffer device. */
  pgb->real_pgb = page->pgbdev;
  page->pgbdev = irr_dev;

  pgb->bandheight = page->band_lines;
  pgb->bandwidth = CAST_SIZET_TO_INT32(page->band_l << 3);
  pgb->imageheight = page->page_h;
  pgb->interleavingstyle = gucr_interleavingStyle(page->hr);

  return TRUE;
}

void irr_pgb_remove(DL_STATE *page)
{
  DEVICELIST *irr_dev;
  irr_pgb_t *pgb;

  if ( theIDevTypeNumber(theIDevType(page->pgbdev)) != IRR_PGB_DEVICE_TYPE )
    return;

  irr_dev = page->pgbdev;
  pgb = irr_pgb_get(irr_dev);

  /* Put the original page buffer device back. */
  page->pgbdev = pgb->real_pgb;
  pgbproxy_setflush(page, FALSE);

  if ( (*theIDevDismount(irr_dev))(irr_dev) == -1 )
    HQFAIL("IRR pgbdev dismount cannot fail");
  device_free(irr_dev);
}

Bool irr_render_complete(DL_STATE *page, Bool result)
{
  irr_state_t *state = page->irr.state;
  irr_store_t *store;

  if ( theIDevTypeNumber(theIDevType(page->pgbdev)) != IRR_PGB_DEVICE_TYPE )
    return result;

  /* renderbands incremented pages_output even though the IRR page buffer didn't
     output anything.  To correct pgb numbering, sheets_output must be
     decremented to its previous value. */
  --page->job->pages_output;

  store = irr_pgb_get(page->pgbdev)->store;

  HQTRACE(irr_debug, ("IRR store final footprint %d MB",
                      store != NULL ? irr_store_footprint(store) >> 10 : 0));

  /* Pass the band store back to the client if it was created successfully. */
  return (result &&
          (state->callback)(state->priv_context, state->priv_id, store));
}

void irr_sepdetect_flags(DL_STATE *page, Bool *page_is_composite,
                         Bool *page_is_separations, Bool *multiple_separations)
{
  if ( page->irr.generating ) {
    irr_store_t *store = irr_pgb_get(page->pgbdev)->store;

    /* Generating phase - record the sep-detection flags. */
    store->page_is_composite = *page_is_composite;
    store->page_is_separations = *page_is_separations;
    store->multiple_separations = *multiple_separations;
  } else if ( page->irr.store != NULL ) {
    irr_store_t *store = page->irr.store;

    /* Replaying the store - include the store in the sep-detection flags. */
    *page_is_composite |= store->page_is_composite;
    *page_is_separations |= store->page_is_separations;
    *multiple_separations |= store->multiple_separations;
  }
}

void irr_omit_blank_separations(DL_STATE *page)
{
  if ( page->irr.generating ) {
    irr_store_t *store = page->irr.state->store;
    uint32 i;

    /* Retain omits for the invariant data and use when replaying. */
    for ( i = 0; i < store->ncomps; ++i ) {
      store->omits[i] = guc_fOmittingSeparation(page->hr, store->colorants[i]);
    }
  } else if ( page->irr.store != NULL ) {
    irr_store_t *store = page->irr.store;
    uint32 i;

    /* When replaying, take account of the IRR store when deciding omits. */
    for ( i = 0; i < store->ncomps; ++i ) {
      if ( !store->omits[i] )
        (void)guc_dontOmitSeparation(page->hr, store->colorants[i]);
    }
  }
}

/** Setup an IRR state for this page.  A state is used for IRR store generation
    and in this case a callback is supplied to pass the band store back to the
    IRR cache.  A state is also used for replaying. */
Bool irr_setdestination(DL_STATE *page, mm_pool_t pool, dbbox_t *bbox,
                        void *priv_context, uintptr_t priv_id,
                        Bool (*callback)(void *priv_context,
                                         uintptr_t priv_id,
                                         irr_store_t *store))
{
  irr_state_t *state = page->irr.state;

  HQASSERT(state == NULL, "More than one IRR state per page!");

  state = mm_alloc(mm_pool_temp, sizeof(*state), MM_ALLOC_CLASS_IRR);
  if ( state == NULL )
    return error_handler(VMERROR);

  state->pool = pool;
  if ( bbox != NULL )
    state->bbox = *bbox;
  else
    bbox_clear(&state->bbox);
  state->priv_context = priv_context;
  state->priv_id = priv_id;
  state->callback = callback;
  state->store = NULL;
  page->irr.state = state;

  HQASSERT((callback != NULL) == page->irr.generating,
           "IRR callback should exist only when generating");

  if ( page->irr.generating ) {
    irr_pgb_t *pgb = irr_pgb_get(page->pgbdev);

    HQASSERT(pgb != NULL, "IRR generating, but IRR pgb device is missing");

    if ( !irr_store_create(page, state->pool, &state->bbox, &pgb->store) )
      return FALSE;

    /** \todo This is just so the store can be purged in low memory.  There
        should probably be a list of stores obtained from the IRR cache
        instead. */
    state->store = pgb->store;
  }

  return TRUE;
}

void irr_free(DL_STATE *page)
{
  irr_state_t *state = page->irr.state;

  if ( state != NULL ) {
    mm_free(mm_pool_temp, state, sizeof(*state));
    page->irr.state = NULL;
  }
  page->irr.store = NULL; /* The store is managed by the PDF IRR cache (pdfirrc.c) */
}

uintptr_t irr_getprivid(irr_state_t *state)
{
  return state->priv_id;
}

void irr_addtodl(DL_STATE *page, irr_store_t *store)
{
  HQASSERT(hdlIsEmpty(page->currentHdl),
           "IRR won't work if objects are present before the IRR store is added");
  HQASSERT(page->irr.store == NULL, "Only one IRR store can be added per page");
  page->irr.store = store;
}

/** \todo This is just so the store can be purged in low memory.  There should
    probably be a list of stores obtained from the IRR cache instead. */
static irr_store_t *irr_get_store(DL_STATE *page)
{
  return page->irr.state != NULL ? page->irr.state->store : NULL;
}

/* Log stripped */
