/** \file
 * \ingroup bandmgmt
 *
 * $HopeName: CORErender!src:bandtable.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software.  All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Bandtable management
 */

#include "core.h"
#include "coreinit.h"
#include "coremaths.h"
#include "bandtable.h"
#include "bandtablepriv.h"
#include "basemap.h"

#include "swerrors.h"
#include "params.h" /* SystemParams */
#include "render.h" /* render_forms_t */
#include "dlstate.h"  /* DLSTATE */
#include "swrle.h" /* RLE_BLOCK_SIZE_WORDS */
#include "mm.h" /* mm_pool_create */
#include "mmcompat.h" /* mm_alloc_static */
#include "gu_htm.h" /* htm_GetFirstHalftoneRef */
#include "ripmulti.h" /* max_simultaneous_tasks() */
#include "swdevice.h"
#include "swraster.h"
#include "graphict.h" /* GUCR_RASTERSTYLE */
#include "gu_chan.h" /* gucr_rasterDepth */
#include "control.h" /* interrupts_clear */
#include "often.h" /* SwOftenSafe */
#include "tables.h" /* count_bits_set_in_byte */
#include "htrender.h" /* ht_anyInRIPHalftonesUsed */
#include "pclGstate.h"
#include "timing.h" /* probe_other */
#include "taskh.h"
#include "monitor.h"
#include "uvms.h"
#include "mlock.h"
#include "swevents.h"
#include "eventapi.h"
#include "swtimelines.h"
#include "riptimeline.h"
#include "irr.h"

/** Maximums for reserved bands/forms, see render_forms_t and
    alloc_all_bands. */
#define MASK_BAND_MAX 7

static resource_source_t framebuffer_band_source ;
static resource_source_t output_band_source ;
static resource_source_t mask_band_source ;
static resource_source_t basemap_band_source ;

float band_factor;


/* ----------- Basemap ------------ */


static mm_pool_t basemap_pool = NULL;

static struct {
  void *mem;
  size_t size;
  uint32 semaphore, count;
} basemap ;

#define BASEMAP_SEMA_START 0xFEEDFACE

/**
 * Define the semaphore functions for basemap1.  This semaphore is to ensure
 * that while parts of the rip other than the renderer may use the basemap
 * memory for their own purposes (generally as a large scratch pad), we can
 * easily detect any conflict in its use.
 */

/**
 * Adjust the size of the basemap, reallocating if necessary.
 */
static Bool alloc_basemap(size_t new_basemap_size)
{
  HQASSERT(basemap.semaphore == BASEMAP_SEMA_START,
           "Trying to resize a basemap currently in use");
  if (new_basemap_size != basemap.size) {
    if ( basemap.size != 0 )
      mm_free(basemap_pool, basemap.mem, basemap.size);

    basemap.mem = mm_alloc(basemap_pool, new_basemap_size, MM_ALLOC_CLASS_BAND);
    if (basemap.mem == NULL) {
      /* New size allocation failed. Try to re-instate the old basemap size, so
         that we don't lose the memory to other pools. This should at least
         allow error recovery and continuation. */
      basemap.mem = mm_alloc(basemap_pool, basemap.size, MM_ALLOC_CLASS_BAND);
      if ( basemap.mem == NULL ) {
        HQFAIL("Could not reallocate old basemap") ;
        basemap.size = 0 ;
      }
      return error_handler(VMERROR);
    }
    basemap.size = new_basemap_size;
  }

  return TRUE ;
}


uint32 get_basemap_semaphore(void **memptr, uint32 *memsize)
{
  HQASSERT(memptr, "No return pointer for basemap") ;
  HQASSERT(memsize, "No return size for basemap") ;

  if (basemap.semaphore != BASEMAP_SEMA_START || basemap.mem == NULL) {
    *memptr = basemap_band_source.data = NULL;
    *memsize = 0;
    return (0);
  }

  /* Count the number of requests */
  basemap.count++;

  *memptr = basemap_band_source.data = basemap.mem ;
  *memsize = (uint32)basemap.size;

  /* perturb the semaphore value */
  return (basemap.semaphore ^= basemap.count);
}


void free_basemap_semaphore(uint32 semaphore)
{
#if ! defined( ASSERT_BUILD )
  UNUSED_PARAM(uint32, semaphore);
#endif

  /* the semaphore has to match to the base */
  HQASSERT(semaphore == basemap.semaphore, "Bad basemap semaphore free");

  basemap_band_source.data = NULL ;

  /* restore the semaphore value */
  basemap.semaphore ^= basemap.count ;
}


#if defined( ASSERT_BUILD )
/* Use this function in asserts to check multi-phase protocols still have the
   semaphore */
Bool got_basemap_semaphore(uint32 semaphore)
{
  return (semaphore == basemap.semaphore) ;
}
#endif


/* ----------- band extension for RLE ----------- */
Bool alloc_band_extension(resource_pool_t *pool, band_t *pband)
{
  resource_entry_t entry = {
    TASK_RESOURCE_FREE,
    TASK_RESOURCE_ID_INVALID /*id*/,
    NULL /*owner*/,
    NULL /*resource*/
  } ;
  band_t *extra ;

  HQASSERT(pband != NULL, "No band descriptor entry") ;

  if ( !pool->alloc(pool, &entry, mm_cost_none) )
    return FALSE ;

  if ( (extra = entry.resource) == NULL || extra->mem == NULL ) {
    pool->free(pool, &entry) ;
    return FALSE ;
  }

  /* Push on head of extras list to make this easy to find. */
  extra->next = pband->next ;
  pband->next = extra ;

  return TRUE ;
}

void free_band_extensions(resource_pool_t *pool, band_t *pband)
{
  resource_entry_t entry = {
    TASK_RESOURCE_FREE,
    TASK_RESOURCE_ID_INVALID /*id*/,
    NULL /*owner*/,
    NULL /*resource*/
  } ;
  band_t *extra ;

  HQASSERT(pband != NULL, "No band descriptor entry") ;

  while ( (extra = pband->next) != NULL ) {
    pband->next = extra->next ;
    entry.resource = extra ;
    pool->free(pool, &entry) ;
  }
}

/* ----------- PGB interface ----------- */


/** Call to allow the pagebuffer device to increase the offset between
    raster line starts. */
static Bool call_pagebuffer_raster_stride(DEVICELIST *pgbdev, uint32 *stride)
{
  Bool result = FALSE;

  if ( pgbdev == NULL || /* Ignore if no PGB device */
       theIIoctl(pgbdev) == NULL)
    return TRUE;

  multi_mutex_lock(&pagebuffer_mutex);

  if ( (*theIIoctl(pgbdev))(pgbdev,
                            (DEVICE_FILEDESCRIPTOR)-1,
                            DeviceIOCtl_RasterStride,
                            (intptr_t)stride) >= 0 ||
       (*theILastErr(pgbdev))(pgbdev) == DeviceNoError ) {
    result = TRUE;
  }

  multi_mutex_unlock(&pagebuffer_mutex);
  return result;
}


Bool call_pagebuffer_raster_requirements(DEVICELIST *pgbdev,
                                         Bool rendering_starting,
                                         DL_STATE *page,
                                         GUCR_RASTERSTYLE *rs,
                                         unsigned minimum_bands,
                                         size_t scratch_size,
                                         void *scratch_band)
{
  Bool result = FALSE;
  RASTER_REQUIREMENTS req;
  COLORANTINDEX *cis;
  DEVICESPACEID deviceSpace;
  uint32 colorantCount;
  int32 deviceColorants;

  if ( pgbdev == NULL || /* Ignore if no PGB device */
       theIIoctl(pgbdev) == NULL)
    return TRUE;

  guc_deviceColorSpace(rs, &deviceSpace, &deviceColorants);
  gucr_colorantCount(rs, &colorantCount);
  if ( colorantCount == 0 )
    return detail_error_handler(CONFIGURATIONERROR,
                                "No channels mapped to colorants") ;
  cis = mm_alloc(mm_pool_temp, sizeof(COLORANTINDEX)*colorantCount,
                 MM_ALLOC_CLASS_BAND);
  if (cis == NULL)
    return error_handler(VMERROR);
  /* colorantCount is the total number of colorants, req.components excludes
     duplicates.  cis is required to get req.components. */
  gucr_colorantIndices(rs, cis, &req.components);
  mm_free(mm_pool_temp, (mm_addr_t)cis, sizeof(COLORANTINDEX)*colorantCount);
  req.components = max(req.components, (uint32)deviceColorants);

  /* Sadly, the interpreter calls happen before the eraseno is incremented. */
  req.eraseno = (int32)page->eraseno + (rendering_starting ? 0 : 1);
  req.page_width = page->page_w;
  req.page_height = page->page_h;
  req.bit_depth = gucr_rasterDepth(rs);
  req.interleavingStyle = gucr_interleavingStyle(rs);
  req.nChannels = gucr_framesChannelsTotal(gucr_framesStart(rs));
  req.components_per_band =
    req.interleavingStyle == GUCR_INTERLEAVINGSTYLE_BAND
    && !DOING_RUNLENGTH(page)
    ? req.nChannels : 1;
  req.minimum_bands = (uint32)minimum_bands;
  req.band_height = page->band_lines;
  req.line_bytes = (uint32)page->band_l;
  req.band_width = req.line_bytes << 3;
  req.band_size = req.band_height * req.line_bytes * req.components_per_band;
  req.have_framebuffer = FALSE;
  req.scratch_size = scratch_size;
  req.scratch_band = scratch_band;
  req.handled = FALSE;

  multi_mutex_lock(&pagebuffer_mutex);
  /* PGBdev can use DeviceNotReady to ask us to spin, taking the opportunity to
     call tickles, and then call again. */
  for (;;) {
    if ( (*theIIoctl(pgbdev))(pgbdev,
                              (DEVICE_FILEDESCRIPTOR)-1,
                              rendering_starting ?
                              DeviceIOCtl_RenderingStart :
                              DeviceIOCtl_RasterRequirements,
                              (intptr_t)&req) >= 0 ) {
      if ( req.handled ) {
        result = TRUE;
        break;
      }
    } else {
      if ( (*theILastErr(pgbdev))(pgbdev) == DeviceNoError )
        result = TRUE ;
      break;
    }

    SwOftenSafe();
  }
  multi_mutex_unlock(&pagebuffer_mutex);

  if ( !result )
    return error_handler(CONFIGURATIONERROR);
  if ( rendering_starting ) {
    /* The have_framebuffer flag must match the call at the start of the
       page, or we may have saved too little space in the basemap. */
    if ( req.have_framebuffer != page->framebuffer )
      return error_handler(CONFIGURATIONERROR);
  } else {
    page->framebuffer = req.have_framebuffer;
    page->scratch_band_size = req.scratch_size;
  }
  return TRUE;
}


/** Call to ask the pagebuffer device for the appropriate band buffer
    for the current DL band to be rendered into. Like \c
    call_pagebuffer_raster_requirements, it can use DeviceNotReady to
    ask us to delay, presumably because the consumer's data FIFO
    leading into the next stage of its pipeline is full. */
static Bool call_pagebuffer_raster_destination(DEVICELIST *pgbdev,
                                               RASTER_DESTINATION *dest)
{
  Bool result = FALSE;

  if ( pgbdev == NULL || /* Ignore if no PGB device */
       theIIoctl(pgbdev) == NULL)
    return TRUE;

  multi_mutex_lock(&pagebuffer_mutex);

  for (;;) {
    if ( (*theIIoctl(pgbdev))(pgbdev,
                              (DEVICE_FILEDESCRIPTOR)-1,
                              DeviceIOCtl_GetBufferForRaster,
                              (intptr_t)dest) >= 0 ) {
      if (dest->handled) {
        result = TRUE;
        break;
      }
    } else {
      if ( (*theILastErr(pgbdev))(pgbdev) == DeviceNoError ) /* IOCTL not implemented */
        result = TRUE ;
      break;
    }

    SwOftenSafe();
  }

  multi_mutex_unlock(&pagebuffer_mutex);
  return result;
}


#define SET_PGB_PARAM_I(page_, name_, value_) { \
  DEVICEPARAM p_; \
  theDevParamName(p_)    = (uint8 *)("" name_ ""); \
  theDevParamNameLen(p_) = (int32)sizeof("" name_ "") - 1; \
  theDevParamType(p_)    = ParamInteger; \
  theDevParamInteger(p_) = (value_); \
  if ( !send_pagebuff_param(page_, &p_)) return FALSE; \
}


static Bool tell_pagebuffer_rsize(DL_STATE *page, int32 nc,
                                  int32 bw, int32 bh, int32 w, int32 h)
{
  SET_PGB_PARAM_I(page, "NumChannels", nc);
  SET_PGB_PARAM_I(page, "BandWidth", bw);
  SET_PGB_PARAM_I(page, "BandHeight", bh);
  SET_PGB_PARAM_I(page, "ImageWidth", w);
  SET_PGB_PARAM_I(page, "ImageHeight", h);
  return TRUE;
}


/* ----------- Bandtable ------------ */


/* Integer division, rounding up */
#define CEILING(dividend, divisor) (((dividend) + (divisor) - 1) / (divisor))

static size_t printer_buffer_warned;
static int32 printer_buffer_warned_job ;


/** Factor to estimate RLE compression wrt. a raster. */
#define RLE_COMPRESSION_FACTOR 4


/*
 * Determine number of lines in a band.
 */
Bool determine_band_size(DL_STATE *page, GUCR_RASTERSTYLE *rs,
                         int32 width, int32 height,
                         int32 ResamplingFactor, int32 BandHeight)
{
  SYSTEMPARAMS *systemparams = get_core_context_interp()->systemparams;
  int32 maxonebandmemory; /* maximum size of a single (raster) band */
  int32 lines;
  unsigned int components; /* # of color components */
  int32 depth; /* # of bits per component */
  size_t lbytes;
  size_t lmaskbytes;
  size_t linebytes; /* size of a single line in bytes */
  size_t contonebytes; /* size of a single line of a contone band in bytes */
  unsigned int num_contone_bands;
  int32 nsimultaneous = max_simultaneous_tasks() ;
  Bool ok ;

  if (DOING_RUNLENGTH(page)) {
    /* For RLE, we expect that process colorants often do appear together, spots
       typically only singly, so pretend we have only the process channels. */
    int32 n_process;
    guc_deviceColorSpace(rs, NULL, &n_process);
    components = (unsigned int)n_process;
  } else {
    components = gucr_framesChannelsTotal(gucr_framesStart(rs));
    HQASSERT (components > 0, "no channels in frame");
  }
  depth = gucr_rasterDepth(rs);

  switch (gucr_interleavingStyle(rs)) {
  case GUCR_INTERLEAVINGSTYLE_MONO:
    break;
  case GUCR_INTERLEAVINGSTYLE_PIXEL:
    depth *= components; components = 1; break;
  case GUCR_INTERLEAVINGSTYLE_BAND:
    HQASSERT(!DOING_RUNLENGTH(page), "Not providing for band-interleaved RLE");
    break;
  case GUCR_INTERLEAVINGSTYLE_FRAME:
    components = 1; break;
  default:
    HQFAIL ("unrecognised interleaving style"); break;
  }
  /* lbytes is the number of bytes required to hold a single line
     of pixels at the appropriate depth */
  if (DOING_RUNLENGTH(page))
    lbytes = FORM_LINE_BYTES(max(width * depth / RLE_COMPRESSION_FACTOR,
                                 /* Must have enough for one RLE block */
                                 32 * RLE_BLOCK_SIZE_WORDS));
  else
    lbytes = FORM_LINE_BYTES(width * depth);

  /* lmaskbytes is the number of bytes required to store a single line of
     one-bit pixels for patterns and clipping etc. */
  lmaskbytes = FORM_LINE_BYTES(width);
  if (lbytes == 0 || height == 0) {
    lines = 0; linebytes = 0;
    goto bandSizeReturn;
  }
  contonebytes = FORM_LINE_BYTES(width * 16); /* could need 16-bit */
  if (htm_GetFirstModule() != NULL /* if there are halftone modules */
      && gucr_halftoning(rs) && ! pcl5eModeIsEnabled())
    num_contone_bands = 2; /* contone and object map */
  else
    num_contone_bands = 0;

  {
    uint32 stride = CAST_SIZET_TO_UINT32(lbytes);

    if (!call_pagebuffer_raster_stride(page->pgbdev, &stride)) {
      return FALSE;
    }

    if (stride < lbytes) {
      return detail_error_handler(CONFIGURATIONERROR,
                                  "Stride length less than raster width.");
    }
    else {
      lbytes = stride;
    }
  }

  if (!DOING_RUNLENGTH(page)) {
    linebytes = lbytes * (components + (page->output_object_map ? 1 : 0));
  } else {
    linebytes = lbytes; /* no object map for RLE */
  }

  /* Maximum memory for one band. */
  if (! get_pagebuff_param(page, (uint8 *) "MaxBandMemory", &maxonebandmemory,
                           ParamInteger, 0))
    return FALSE;

  if ( maxonebandmemory > MAXINT32 / 1024 || maxonebandmemory <= 0 )
    maxonebandmemory = MAXINT32 ;
  else
    maxonebandmemory *= 1024; /* is given in kB */

  if (BandHeight > 0) {
    lines = min(BandHeight, height);
    if ( lines * linebytes > (size_t)maxonebandmemory )
      return detail_error_handler(CONFIGURATIONERROR,
                                  "BandHeight doesn't fit into printer buffer");
  } else {
    int32 nbands, nbands_adjusted_for_threads, band_threads ;
    double maxtotalbandmemory = systemparams->MaxBandMemory * 1024.0 * 1024.0 ;
    size_t bytes_for_1_line = (linebytes +
                               lmaskbytes * MASK_BAND_MAX +
                               contonebytes * num_contone_bands) ;
    Bool dynamic_bands = systemparams->DynamicBands ;

    /** \todo ajcd 2011-09-24: This request from the PGB device is at the
        wrong time, make_band_requests() is called during interpretation. The
        GUI PGB device can change whether dynamic bands are allowed by
        changing throughput mode, but fortunately that is modal; it only
        happens between jobs. */
    if ( dynamic_bands &&
         !get_pagebuff_param(page, (uint8 *) "DynamicBands",
                             &dynamic_bands, ParamBoolean, TRUE) )
      return FALSE ;

    /* SystemParams.MaxBandMemory is total to divide up between renderers,
       including the mask bands. In the GUI RIP, it's initialised to the max
       number of threads times the renderer memory per thread setting. This
       calculates the maximum height of a band, assuming we can use one
       active thread per band. We allow an extra band if we're going to allow
       DynamicBands, so the plugin can be working on a band simultaneously. */
    /** \todo ajcd 2011-09-24: Should we set aside DynamicBandLimit extra
        bands? */
    band_threads = nsimultaneous + (dynamic_bands ? 1 : 0) ;
    lines = (int32)(maxtotalbandmemory / (bytes_for_1_line * band_threads)) ;

    if ( lines <= 0 ) {
      /* We don't have enough total memory to run all of the threads
         simultaneously. Work out what that smaller number of threads is. */
      band_threads = (int32)(maxtotalbandmemory / bytes_for_1_line) ;
      HQASSERT(band_threads <= nsimultaneous, "Too many threads") ;
      if ( band_threads > 0 )
        lines = (int32)(maxtotalbandmemory / (bytes_for_1_line * band_threads)) ;
    }

    if ( lines * linebytes > (size_t)maxonebandmemory ) {
      /* The maximum number of lines exceeds the memory available for one
         band, so we're going to reduce the band height to fit. If we're
         going to allow extra DynamicBands, then don't complain if we can use
         the extra memory. We'll accept the reduction in band size without
         complaining if the base space */
      size_t printer_buffer_needed = lines * linebytes * 2;
      int32 dynamic_bands_ceil = 0 ;

      lines = maxonebandmemory / CAST_SIZET_TO_INT32(linebytes);

      /* Use one more than the number of dynamic bands to test whether the
         new limit doesn't fill the total band memory available. */
      if ( dynamic_bands ) {
        dynamic_bands_ceil = systemparams->DynamicBandLimit ;

        if ( dynamic_bands_ceil == 0 ) {
          dynamic_bands_ceil = MAXINT32 ;
        } else if ( dynamic_bands_ceil < 0 ) {
          dynamic_bands_ceil = 0 ;
        } else {
          dynamic_bands_ceil += 1 ;
        }
      }

      if ( maxtotalbandmemory
           - bytes_for_1_line * lines * nsimultaneous
           - linebytes * lines * (double)dynamic_bands_ceil > 0 &&
           printer_buffer_warned < printer_buffer_needed &&
           printer_buffer_warned_job != page->job_number ) {
        monitorf(UVM("Warning: Band size reduced to fit printer buffer.  Needed %0.2f MiB.\n"),
                 printer_buffer_needed / (1024.0 * 1024.0));
        printer_buffer_warned = printer_buffer_needed;
        printer_buffer_warned_job = page->job_number ;
      }
    }

    if (lines <= 0)
      return detail_error_handler(CONFIGURATIONERROR,
                                  "MaxBandMemory limit too small");

    /* Adjust lines to give each thread about the same amount of work. */
    nbands = CEILING(height, lines);
    nbands_adjusted_for_threads = SIZE_ALIGN_UP(nbands, nsimultaneous);
    lines = CEILING(height, nbands_adjusted_for_threads);

    if (BandHeight < 0) {
      /* Round down to nearest modulo (-BandHeight). See request 12772. */
      if ( lines < -BandHeight ) {
        /* We don't have enough lines to make it a multiple of BandHeight. */
        monitorf(UVS("Warning: Unable to make band size a multiple of BandHeight. Not enough band memory available.\n"));
      } else if (ResamplingFactor > 1) {
        /* Both BandHeight and ResamplingFactor need to be used to
           calculate new band height. */
        int32 modulo = (int32)ulcm((unsigned int)ResamplingFactor,
                                   (unsigned int)(-BandHeight));

        if (lines < modulo) {
          monitorf(UVS("Warning: Unable to make band size a multiple of ResamplingFactor and BandHeight. Not enough band memory available.\n"));
        } else {
          lines = SIZE_ALIGN_DOWN(lines, modulo);
        }
      } else {
        lines = SIZE_ALIGN_DOWN(lines, -BandHeight);
      }
    } else {
      /* Align to ResamplingFactor. Round down as we know we will have
         enough memory. */
      if (lines < ResamplingFactor) {
        /* We don't have enough lines to make it a multiple of
           ResamplingFactor. */
        monitorf(UVS("Warning: Unable to make band size a multiple of ResamplingFactor. Not enough band memory available.\n"));
      } else {
        lines = SIZE_ALIGN_DOWN(lines, ResamplingFactor);
      }
    }

    HQASSERT(lines > 0, "Ended up with 0 lines per band");
  }

 bandSizeReturn:
  page->page_w = width; page->page_h = height;
  page->band_l = lbytes; page->band_l1 = lmaskbytes;
  page->band_lc = 0 ; /* MHT only, should be back-end */
  page->band_lines = (int32)lines;

  /* Swap in the IRR pgb device if in the generating phase.  This must be done
     after the band dimensions have been set with the normal pgb device. */
  if ( !irr_pgb_install(page) )
    return FALSE;

  /* Call the pagebuffer device to see if the consumer wants to
     allocate bands itself. */
  if ( lines != 0 ) {
    if ( !call_pagebuffer_raster_requirements(page->pgbdev, FALSE,
                                              page, rs,
                                              nsimultaneous, 0, NULL) )
      return FALSE;
    /** \todo jonw 20090718: Maybe here I can enable, disable or even eagerly
       prefer partial paint, depending on whether the device is fast enough.
       Maybe another field in req indicating that the device has a disk or
       something similar. */
  }

  ok = alloc_basemap(systemparams->BaseMapSize) ;
  if ( ok ) {
    /* Tell the pagebuffer device how big the band is going to be. */
    if ( !tell_pagebuffer_rsize(page,
                                gucr_framesChannelsTotal(gucr_framesStart(rs)),
                                CAST_SIZET_TO_INT32(lbytes << 3),
                                page->band_lines, width, height) )
      ok = FALSE;
  }
  return ok ;
}


/** \brief Data structure used to return bands owned by the PGB device. */
typedef struct band_notify_t {
  sw_tl_extent frame_and_line ; /**< The frame and line number we've reached. */
  int32 bands_freed ;    /**< Count of bands after returning. */
} band_notify_t ;

/** \brief Callback used to return bands owned by the PGB device.

    \param pool  Resource pool pointer (not used by this callback function).
    \param entry Resource entry to examine.
    \param data  Opaque pointer to a \c band_notify_t data structure.

    \return This callback always returns TRUE.
*/
static Bool band_output_notify(resource_pool_t *pool,
                               resource_entry_t *entry,
                               void *data)
{
  band_notify_t *done = data ;

  HQASSERT(done != NULL, "No data in band pool done iteration callback") ;
  HQASSERT(entry != NULL,
           "No resource entry in band pool done iteration callback") ;

#if 0
  /** \todo ajcd 2012-10-27: Unless we can get a better solution to RLE
      RunLineComplete, we can't assert this. RunLineComplete may output the
      same line in several PGB write calls. Each band will have the same
      resource ID, because they all finish on the same line. We need
      timeline to issue the lines copied events, so we increment the progress
      by a fractional amount for each of these lines. */
  HQASSERT(data->frame_and_line == (int32)data->frame_and_line,
           "Band progress is not an integer") ;
#endif

  if ( entry->state == TASK_RESOURCE_DETACHED &&
       entry->id < done->frame_and_line ) {
    /* If the entry had previously been detached and the task group had
       ended, the entry will be owned by the pool. In this case, the pool's
       detach counter was increased when the group was deprovisioned, we
       need to update it. */
    probe_other(entry->owner == pool
                ? SW_TRACE_RESOURCE_RETURN
                : SW_TRACE_RESOURCE_FIX,
                SW_TRACETYPE_MARK, entry->resource) ;

    /* Get rid of band extensions regardless of band's fate. We won't be
       using it any more. */
    free_band_extensions(pool, entry->resource) ;

    if ( entry->owner == pool ) {
      --pool->ndetached ;
      entry->owner = NULL ;
      entry->state = TASK_RESOURCE_FREE ;
      entry->id = TASK_RESOURCE_ID_INVALID ;
      ++done->bands_freed ;
    } else {
      HQASSERT(entry->owner != NULL, "Resource already freed") ;
      /* Entry is still owned by the group. Re-fix it, so that the deprovision
         operation can account for the pool provisions correctly. */
      entry->state = TASK_RESOURCE_FIXED ;
    }
  }

  return TRUE ;
}

int32 bands_copied_notify(resource_pool_t *pool, sw_tl_extent frame_and_line)
{
  band_notify_t data ;

  data.frame_and_line = frame_and_line ;
  data.bands_freed = 0 ;

  (void)resource_pool_forall(pool, 0, band_output_notify, &data) ;

  return data.bands_freed ;
}


void fix_reserved_band_resources(render_forms_t *forms, uint8 band_flags)
{
  if ( (band_flags & RESERVED_BAND_CLIPPING) != 0 ) {
    forms->clippingform.addr = task_resource_fix(TASK_RESOURCE_BAND_1,
                                                 RESERVED_BAND_CLIPPING,
                                                 NULL);
    HQASSERT(forms->clippingform.addr != NULL, "No clipping form resource") ;
  }

  if ( (band_flags & RESERVED_BAND_PATTERN) != 0 ) {
    forms->patternform.addr = task_resource_fix(TASK_RESOURCE_BAND_1,
                                                RESERVED_BAND_PATTERN,
                                                NULL);
    HQASSERT(forms->patternform.addr != NULL, "No pattern form resource") ;
  }

  if ( (band_flags & RESERVED_BAND_PATTERN_SHAPE) != 0 ) {
    forms->patternshapeform.addr = task_resource_fix(TASK_RESOURCE_BAND_1,
                                                     RESERVED_BAND_PATTERN_SHAPE,
                                                     NULL);
    HQASSERT(forms->patternshapeform.addr != NULL, "No pattern shape form resource") ;
  }

  if ( (band_flags & RESERVED_BAND_PATTERN_CLIP) != 0 ) {
    forms->patternclipform.addr = task_resource_fix(TASK_RESOURCE_BAND_1,
                                                    RESERVED_BAND_PATTERN_CLIP,
                                                    NULL);
    HQASSERT(forms->patternclipform.addr != NULL, "No pattern clip form resource") ;
  }

  if ( (band_flags & RESERVED_BAND_MASKED_IMAGE) != 0 ) {
    forms->maskedimageform.addr = task_resource_fix(TASK_RESOURCE_BAND_1,
                                                    RESERVED_BAND_MASKED_IMAGE,
                                                    NULL);
    HQASSERT(forms->maskedimageform.addr != NULL, "No masked image form resource") ;
  }

  if ( (band_flags & RESERVED_BAND_SELF_INTERSECTING) != 0 ) {
    forms->intersectingclipform.addr = task_resource_fix(TASK_RESOURCE_BAND_1,
                                                         RESERVED_BAND_SELF_INTERSECTING,
                                                         NULL);
    HQASSERT(forms->intersectingclipform.addr != NULL, "No intersecting clip form resource") ;
  }

  if ( (band_flags & RESERVED_BAND_MODULAR_SCREEN) != 0 ) {
    forms->htmaskform.addr = task_resource_fix(TASK_RESOURCE_BAND_1,
                                               RESERVED_BAND_MODULAR_SCREEN,
                                               NULL);
    HQASSERT(forms->htmaskform.addr != NULL, "No MHT mask form resource") ;
    forms->contoneform.addr = task_resource_fix(TASK_RESOURCE_BAND_CT,
                                                RESERVED_BAND_MODULAR_SCREEN,
                                                NULL);
    HQASSERT(forms->contoneform.addr != NULL, "No MHT tone form resource") ;
    if ( (band_flags & RESERVED_BAND_MODULAR_MAP) != 0 ) {
      forms->objectmapform.addr = task_resource_fix(TASK_RESOURCE_BAND_CT,
                                                    RESERVED_BAND_MODULAR_MAP,
                                                    NULL);
      HQASSERT(forms->objectmapform.addr != NULL, "No MHT object map form resource") ;
    }
  }

  HQASSERT((band_flags & ~(RESERVED_BAND_CLIPPING |
                           RESERVED_BAND_PATTERN  |
                           RESERVED_BAND_PATTERN_SHAPE |
                           RESERVED_BAND_PATTERN_CLIP  |
                           RESERVED_BAND_MASKED_IMAGE  |
                           RESERVED_BAND_SELF_INTERSECTING |
                           RESERVED_BAND_MODULAR_SCREEN |
                           RESERVED_BAND_MODULAR_MAP)) == 0,
           "Unrecognised reserved band" ) ;

  forms->halftonebase = task_resource_fix(TASK_RESOURCE_LINE_OUT, 0, NULL);
  HQASSERT(forms->halftonebase != NULL, "No halftone base resource") ;
  forms->maxbltbase = task_resource_fix(TASK_RESOURCE_LINE_OUT, 1, NULL);
  HQASSERT(forms->maxbltbase != NULL, "No maxblit base resource") ;
  forms->clippingbase = task_resource_fix(TASK_RESOURCE_LINE_1, 2, NULL);
  HQASSERT(forms->clippingbase != NULL, "No clipping base resource") ;
}


Bool max_basemap_band_height(DL_STATE *page)
{
  size_t line_size, height, basebands;

  HQASSERT(page != NULL, "max_basemap_band_height: NULL page pointer");
  HQASSERT(IS_INTERPRETER(), "Called from non-interpreter task") ;

  basebands = page->band_l1 * 3; /* halftonebase, maxbltbase, clippingbase */
  /* Calculate number of words needed for a single line for all the mask
     bands needed for creating shape masks. Shape masks do not use
     modular screening. */
  line_size = page->band_l1
    * count_bits_set_in_byte[page->reserved_bands &
                             ~(RESERVED_BAND_MODULAR_SCREEN|
                               RESERVED_BAND_MODULAR_MAP)];

  if ( basebands + line_size > basemap.size ) {
    /* Enlarge the basemap to accomodate at least one line. */
    if ( !alloc_basemap(basebands + line_size) )
      return detailf_error_handler(CONFIGURATIONERROR,
               "Not enough space for a huge pattern. Enlarge "
               "the BaseMapSize system parameter to %u.",
               basebands + line_size);
  }
  /* Calculate maximum band height */
  height = (basemap.size - basebands) / line_size;
  HQASSERT(height > 0 && basemap.size > basebands, "Miscalculated basemap size");
  page->band_lines = min((int32)height, page->page_h) ;
  return TRUE ;
}


Bool mask_bands_from_basemap(render_forms_t *forms, DL_STATE *page)
{
  blit_t *bptr, *blimit;
  size_t line_words ;
  uint8 band_flags ;

  HQASSERT(IS_INTERPRETER(), "Called from non-interpreter task") ;
  HQASSERT(forms != NULL, "No forms to fill in");
  HQASSERT(page != NULL, "No page for patternshape bands");

  band_flags = page->reserved_bands;

  line_words = page->band_l1 >> BLIT_SHIFT_BYTES ;
  HQASSERT(line_words != 0, "No words for mask line") ;

  bptr = basemap.mem ;
  blimit = BLIT_ALIGN_DOWN((uint8 *)bptr + basemap.size) ;
  forms->halftonebase = bptr ; bptr += line_words ;
  forms->maxbltbase   = bptr ; bptr += line_words ;
  forms->clippingbase = bptr ; bptr += line_words ;

  line_words *= (unsigned int)page->band_lines ;

  if ( (band_flags & RESERVED_BAND_CLIPPING) != 0 ) {
    forms->clippingform.addr = bptr ;
    bptr += line_words ;
  }

  if ( (band_flags & RESERVED_BAND_PATTERN) != 0 ) {
    forms->patternform.addr = bptr ;
    bptr += line_words ;
  }

  if ( (band_flags & RESERVED_BAND_PATTERN_SHAPE) != 0 ) {
    forms->patternshapeform.addr = bptr ;
    bptr += line_words ;
  }

  if ( (band_flags & RESERVED_BAND_PATTERN_CLIP) != 0 ) {
    forms->patternclipform.addr = bptr ;
    bptr += line_words ;
  }

  if ( (band_flags & RESERVED_BAND_MASKED_IMAGE) != 0 ) {
    forms->maskedimageform.addr = bptr ;
    bptr += line_words ;
  }

  if ( (band_flags & RESERVED_BAND_SELF_INTERSECTING) != 0 ) {
    forms->intersectingclipform.addr = bptr ;
    bptr += line_words ;
  }

  if ( bptr > blimit )
    return error_handler(CONFIGURATIONERROR) ;

  return TRUE;
}

Bool band_resource_pools(DL_STATE *page)
{
  resource_pool_t *pool ;
  resource_requirement_t *req ;
  int32 colorants_per_band, nSheetNumber ;

  HQASSERT(IS_INTERPRETER(), "Setting up band resource pools too late") ;
  HQASSERT(page != NULL, "No DL to update band pool") ;
  req = page->render_resources ;
  HQASSERT(req != NULL, "No resource requirement to update band pool") ;
  HQASSERT(!resource_requirement_is_ready(req),
           "Resource requirement cannot be updated when ready") ;

  /** \todo ajcd 2011-09-12: This assumes that the number of colorants per
      band won't change for each sheet. In future, this may not be a valid
      assumption (once the full blitter capabilities are exposed so the
      raster output can request any output type). We'd then need different
      output pools for each sheet, or we'd have to create the pools at render
      time, which would make minimum provisioning of the pool tricky. */
  if ( !gucr_framesStartOfSheet(gucr_framesStart(page->hr),
                                &colorants_per_band, &nSheetNumber) )
    return error_handler(UNREGISTERED) ;

  if ( page->output_object_map )
    colorants_per_band += 1 ;

  if (/* Output bands. */
      (pool = resource_pool_get(page->framebuffer
                                ? &framebuffer_band_source
                                : &output_band_source,
                                TASK_RESOURCE_BAND_OUT,
                                page->band_l * page->band_lines * colorants_per_band)) == NULL ||
      !resource_requirement_set_pool(req, TASK_RESOURCE_BAND_OUT, pool) ||
      /* Output lines. */
      (pool = resource_pool_get(&mask_band_source,
                                TASK_RESOURCE_LINE_OUT,
                                page->band_l)) == NULL ||
      !resource_requirement_set_pool(req, TASK_RESOURCE_LINE_OUT, pool) ||
      /* Mask bands. */
      (pool = resource_pool_get(&mask_band_source,
                                TASK_RESOURCE_BAND_1,
                                page->band_l1 * page->band_lines)) == NULL ||
      !resource_requirement_set_pool(req, TASK_RESOURCE_BAND_1, pool) ||
      /* Mask lines. */
      (pool = resource_pool_get(&mask_band_source,
                                TASK_RESOURCE_LINE_1,
                                page->band_l1)) == NULL ||
      !resource_requirement_set_pool(req, TASK_RESOURCE_LINE_1, pool) )
    return FALSE ;

  if (/* Scratch bands. */
      (pool = resource_pool_get(&mask_band_source,
                                TASK_RESOURCE_BAND_SCRATCH,
                                page->scratch_band_size)) == NULL ||
      !resource_requirement_set_pool(req, TASK_RESOURCE_BAND_SCRATCH, pool) )
    return FALSE;

  return TRUE ;
}

Bool mht_band_resources(DL_STATE *page)
{
  resource_pool_t *pool ;
  resource_requirement_t *req ;

  HQASSERT(IS_INTERPRETER(), "Setting up MHT resource pools too late") ;
  HQASSERT(page != NULL, "No DL to update band pool") ;
  req = page->render_resources ;
  HQASSERT(req != NULL, "No resource requirement to update band pool") ;
  HQASSERT(!resource_requirement_is_ready(req),
           "Resource requirement cannot be updated when ready") ;

  if (/* MHT contone bands. */
      (pool = resource_pool_get(&mask_band_source,
                                TASK_RESOURCE_BAND_CT,
                                page->band_lc * page->band_lines)) == NULL ||
      !resource_requirement_set_pool(req, TASK_RESOURCE_BAND_CT, pool) )
    return FALSE ;

  return TRUE ;
}

/** Handler for BandsCopied from PGB device. */
static sw_event_result HQNCALL bands_copied_handler(void *context,
                                                    sw_event *ev)
{
  SWMSG_TIMELINE *msg = ev->message;

  UNUSED_PARAM(void *, context);

  if (msg == NULL || ev->length < sizeof(SWMSG_TIMELINE))
    return SW_EVENT_CONTINUE;

  if (msg->type == SWTLT_PGB ) {
    void *owner ;

    if ( (owner = SwTimelineGetContext(msg->ref, SWTLC_BAND_OWNER)) != NULL ) {
      int32 nfreed = bands_copied_notify(owner, msg->progress) ;
      probe_other(SW_TRACE_LINES_COPIED, SW_TRACETYPE_AMOUNT, (intptr_t)msg->progress);

      if ( nfreed > 0 ) {
        task_group_resources_signal() ;
      }
    }
  }

  return SW_EVENT_CONTINUE;
}

static sw_event_handlers handlers[] = {
  { bands_copied_handler, NULL, 0, EVENT_TIMELINE_PROGRESS, SW_EVENT_NORMAL }
};


/** Generic method for sources storing mm_pool in data field */
static mm_pool_t band_source_mm_pool(const resource_source_t *source)
{
  VERIFY_OBJECT(source, RESOURCE_SOURCE_NAME) ;
  return source->data ;
}

/* Methods for mask band pool *********************************************/
static Bool mask_band_alloc(resource_pool_t *pool, resource_entry_t *entry,
                            mm_cost_t cost)
{
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;
  VERIFY_OBJECT(pool->source, RESOURCE_SOURCE_NAME) ;
  HQASSERT(entry != NULL, "No resource entry") ;

  if ( (entry->resource = mm_alloc_cost(pool->source->data, pool->key,
                                        cost, MM_ALLOC_CLASS_BAND)) == NULL )
    return FALSE ;

  return TRUE ;
}

static void mask_band_free(resource_pool_t *pool, resource_entry_t *entry)
{
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;
  VERIFY_OBJECT(pool->source, RESOURCE_SOURCE_NAME) ;
  HQASSERT(entry != NULL, "No resource entry") ;

  mm_free(pool->source->data, entry->resource, pool->key) ;
}

static size_t mask_band_size(resource_pool_t *pool,
                             const resource_entry_t *entry)
{
  UNUSED_PARAM(const resource_entry_t *, entry) ;
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;
  return (size_t)pool->key ;
}

/** \brief Factory for mask bands allocating from mm_pool_basemap. */
static Bool mask_band_make_pool(resource_source_t *source,
                                resource_pool_t *pool)
{
  UNUSED_PARAM(resource_source_t *, source) ;
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;

  pool->alloc = mask_band_alloc ;
  pool->free = mask_band_free ;
  pool->entry_size = mask_band_size ;

  return TRUE ;
}

/* Methods for output band pool *******************************************/
static Bool output_band_alloc(resource_pool_t *pool, resource_entry_t *entry,
                              mm_cost_t cost)
{
  band_t *band ;
  mm_pool_t mm_pool ;

  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;
  VERIFY_OBJECT(pool->source, RESOURCE_SOURCE_NAME) ;
  HQASSERT(entry != NULL, "No resource entry") ;

  mm_pool = pool->source->data ;
  if ( (band = mm_alloc_cost(mm_pool, sizeof(*band), cost,
                             MM_ALLOC_CLASS_BAND)) == NULL )
    return FALSE ;

  band->fCompressed = FALSE ;
  band->fDontOutput = FALSE ;
  band->size_to_write = CAST_SIZET_TO_INT32(pool->key) ;
  band->next = NULL ;
  if ( (band->mem = mm_alloc_cost(mm_pool, pool->key, cost,
                                  MM_ALLOC_CLASS_BAND)) == NULL ) {
    mm_free(mm_pool, band, sizeof(*band)) ;
    return FALSE ;
  }

  entry->resource = band ;

  return TRUE ;
}

static void output_band_free(resource_pool_t *pool, resource_entry_t *entry)
{
  band_t *band ;
  mm_pool_t mm_pool ;

  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;
  VERIFY_OBJECT(pool->source, RESOURCE_SOURCE_NAME) ;
  HQASSERT(entry != NULL, "No resource entry") ;

  band = entry->resource ;
  mm_pool = pool->source->data ;
  mm_free(mm_pool, band->mem, pool->key) ;
  mm_free(mm_pool, band, sizeof(*band)) ;
}

static void output_band_fix(resource_pool_t *pool, resource_entry_t *entry)
{
  band_t *band ;

  UNUSED_PARAM(resource_pool_t *, pool) ;

  HQASSERT(entry->state == TASK_RESOURCE_FIXING,
           "Resource entry not in fixing state") ;
  band = entry->resource ;
  HQASSERT(band != NULL && band->mem, "No band to fix") ;
  band->fCompressed = FALSE ;
  band->fDontOutput = FALSE ;
  band->size_to_write = CAST_SIZET_TO_INT32(pool->key) ;
  HQASSERT(band->next == NULL, "Output band should not have extensions") ;
}

static size_t output_band_size(resource_pool_t *pool,
                               const resource_entry_t *entry)
{
  UNUSED_PARAM(const resource_entry_t *, entry) ;
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;
  return (size_t)pool->key + sizeof(band_t) ;
}

/** \brief Factory for output bands allocating from mm_pool_basemap. */
static Bool output_band_make_pool(resource_source_t *source,
                                  resource_pool_t *pool)
{
  UNUSED_PARAM(resource_source_t *, source) ;
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;

  pool->alloc = output_band_alloc ;
  pool->free = output_band_free ;
  pool->fix = output_band_fix ;
  pool->entry_size = output_band_size ;

  return TRUE ;
}

/* Methods for framebuffer band pool *****************************************/
static Bool framebuffer_band_alloc(resource_pool_t *pool,
                                   resource_entry_t *entry, mm_cost_t cost)
{
  band_t *band ;
  mm_pool_t mm_pool ;

  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;
  VERIFY_OBJECT(pool->source, RESOURCE_SOURCE_NAME) ;
  HQASSERT(entry != NULL, "No resource entry") ;

  mm_pool = pool->source->data ;
  if ( (band = mm_alloc_cost(mm_pool, sizeof(*band), cost,
                             MM_ALLOC_CLASS_BAND)) == NULL )
    return FALSE ;

  band->fCompressed = FALSE ;
  band->fDontOutput = FALSE ;
  band->size_to_write = CAST_SIZET_TO_INT32(pool->key) ;
  band->mem = NULL ;
  band->next = NULL ;

  entry->resource = band ;

  return TRUE ;
}

static void framebuffer_band_free(resource_pool_t *pool, resource_entry_t *entry)
{
  band_t *band ;
  mm_pool_t mm_pool ;

  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;
  VERIFY_OBJECT(pool->source, RESOURCE_SOURCE_NAME) ;
  HQASSERT(entry != NULL, "No resource entry") ;

  band = entry->resource ;
  mm_pool = pool->source->data ;
  mm_free(mm_pool, band, sizeof(*band)) ;
}

static void framebuffer_band_fix(resource_pool_t *pool, resource_entry_t *entry)
{
  RASTER_DESTINATION dest;
  band_t *band ;
  DL_STATE *page = CoreContext.page ;

  UNUSED_PARAM(resource_pool_t *, pool) ;

  HQASSERT(entry->state == TASK_RESOURCE_FIXING,
           "Resource entry not in fixing state") ;
  band = entry->resource ;
  HQASSERT(band != NULL, "No band to fix") ;

  /* Convert frame and line number to frame and band number combined. */
  dest.bandnum = ((int32)entry->id / page->page_h) * page->sizedisplaylist +
    ((int32)entry->id % page->page_h) / page->band_lines ;
  dest.memory_base = NULL;
  dest.memory_ceiling = NULL;
  dest.handled = FALSE;

  /* Could put page into pool data? */
  if ( !call_pagebuffer_raster_destination(page->pgbdev, &dest) )
    HQFAIL("Can't get raster destination for band");

  HQASSERT(dest.memory_base, "No memory from raster destination call.") ;

  band->mem = (void *)dest.memory_base ;
  band->fCompressed = FALSE ;
  band->fDontOutput = FALSE ;
  band->size_to_write = CAST_SIZET_TO_INT32(pool->key) ;
  HQASSERT(band->next == NULL, "Output band should not have extensions") ;
}

/** \brief Factory for bands allocating from framebuffer. */
static Bool framebuffer_band_make_pool(resource_source_t *source,
                                       resource_pool_t *pool)
{
  UNUSED_PARAM(resource_source_t *, source) ;
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;

  pool->alloc = framebuffer_band_alloc ;
  pool->free = framebuffer_band_free ;
  pool->fix = framebuffer_band_fix ;
  /* No low-memory reclaimation. */

  return TRUE ;
}

/* Methods for basemap band pool *********************************************/
static Bool basemap_band_alloc(resource_pool_t *pool, resource_entry_t *entry,
                               mm_cost_t cost)
{
  uint8 *next, *limit ;

  UNUSED_PARAM(mm_cost_t, cost) ;

  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;
  VERIFY_OBJECT(pool->source, RESOURCE_SOURCE_NAME) ;
  HQASSERT(entry != NULL, "No resource entry") ;

  HQASSERT((uint8 *)pool->source->data >= (uint8 *)basemap.mem &&
           (uint8 *)pool->source->data <= (uint8 *)basemap.mem + basemap.size,
           "Basemap band source base wrong") ;

  next = pool->source->data ;
  next += pool->key ;
  next = (void *)BLIT_ALIGN_UP(next) ;

  limit = basemap.mem ;
  limit += basemap.size ;

  if ( next > limit )
    return FALSE ;

  entry->resource = pool->source->data ;
  HQASSERT(entry->resource == BLIT_ALIGN_UP(entry->resource),
           "Basemap band is not blit aligned") ;
  pool->source->data = next ;

  return TRUE ;
}

static void basemap_band_free(resource_pool_t *pool, resource_entry_t *entry)
{
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;
  VERIFY_OBJECT(pool->source, RESOURCE_SOURCE_NAME) ;
  HQASSERT(entry != NULL, "No resource entry") ;

  HQASSERT((uint8 *)entry->resource >= (uint8 *)basemap.mem &&
           (uint8 *)entry->resource + pool->key <= (uint8 *)basemap.mem + basemap.size,
           "Basemap band source base wrong") ;

  /* No freeing of basemap resources. They're reset each time the basemap
     semaphore is acquired. */
  UNUSED_PARAM(resource_pool_t *, pool) ;
  UNUSED_PARAM(resource_entry_t *, entry) ;
}

/** \brief Factory for band pools allocating from the basemap. */
static Bool basemap_band_make_pool(resource_source_t *source,
                                   resource_pool_t *pool)
{
  UNUSED_PARAM(resource_source_t *, source) ;
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME) ;

  pool->alloc = basemap_band_alloc ;
  pool->free = basemap_band_free ;
  /* No low-memory reclaimation. */

  return TRUE ;
}

/* Initialisation ************************************************************/

static void bandtable_finish(void) ;

static Bool bandtable_swinit(struct SWSTART *params)
{
  SYSTEMPARAMS *systemparams = get_core_context()->systemparams;

  UNUSED_PARAM(struct SWSTART *, params) ;

  probe_other(SW_TRACE_LINES_COPIED, SW_TRACETYPE_OPTION, SW_TRACEOPTION_AFFINITY);

  /* Individual allocations from this pool are ONLY EVER used for bands, so
     the alignment requirement can be increased substantially. We would like
     to align to the cache line size for performance. The front end "basemap"
     is itself sub-allocated, and there is only one of them allocated, so the
     increased alignment won't hurt it either. */
  /** \todo ajcd 2011-09-03: The cache line size can be worked out from:

      * GetLogicalProcessorInformation() (Windows),
      * /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size (Linux),
      * sysctlbyname("hw.cachelinesize",...) (MacOS). */
  /** \todo ajcd 2011-09-04: Why not use default 64K segments? What should
      the average size be set to? */
  if ( mm_pool_create(&basemap_pool, BAND_POOL_TYPE,
                      MM_SEGMENT_SIZE, (size_t)(4 * 1024) /*avgsize*/,
                      (size_t)64 /*should be cache line size*/) != MM_SUCCESS ) {
    HQFAIL("Basemap pool creation failed");
    return FAILURE(FALSE) ;
  }

  if ( !alloc_basemap(systemparams->BaseMapSize) ) {
    mm_pool_destroy(basemap_pool);
    return FAILURE(FALSE) ;
  }

  if ( SwRegisterHandlers(handlers, NUM_ARRAY_ITEMS(handlers)) != SW_RDR_SUCCESS) {
    bandtable_finish();
    return FAILURE(FALSE) ;
  }

  output_band_source.data = basemap_pool ;
  mask_band_source.data = basemap_pool ;
  framebuffer_band_source.data = basemap_pool ;

  if ( !resource_source_low_mem_register(&output_band_source) ||
       !resource_source_low_mem_register(&mask_band_source) ||
       !resource_source_low_mem_register(&framebuffer_band_source) ) {
    bandtable_finish();
    return FAILURE(FALSE) ;
  }

  return TRUE ;
}

static void bandtable_finish(void)
{
  resource_source_low_mem_deregister(&output_band_source) ;
  resource_source_low_mem_deregister(&mask_band_source) ;
  resource_source_low_mem_deregister(&framebuffer_band_source) ;

  (void)SwDeregisterHandlers(handlers, NUM_ARRAY_ITEMS(handlers)) ;

  if ( basemap.size != 0 )
    mm_free(basemap_pool, basemap.mem, basemap.size);

  mm_pool_destroy(basemap_pool);

  HQASSERT(basemap_band_source.refcount == 1 &&
           output_band_source.refcount == 1 &&
           mask_band_source.refcount == 1 &&
           framebuffer_band_source.refcount == 1,
           "Incorrect reference count for band source") ;
  UNNAME_OBJECT(&basemap_band_source) ;
  UNNAME_OBJECT(&output_band_source) ;
  UNNAME_OBJECT(&mask_band_source) ;
  UNNAME_OBJECT(&framebuffer_band_source) ;
}

/** File runtime initialisation */
static void init_C_globals_bandtable(void)
{
  resource_source_t source_init = { 0 } ;

  band_factor = 0.0f ;

  basemap_pool = NULL ;
  basemap.mem = NULL;
  basemap.size = 0;
  basemap.semaphore = BASEMAP_SEMA_START;
  basemap.count = 0;

  output_band_source = source_init ;
  output_band_source.refcount = 1 ; /* Never release this source */
  output_band_source.make_pool = output_band_make_pool ;
  output_band_source.mm_pool = band_source_mm_pool ;
  NAME_OBJECT(&output_band_source, RESOURCE_SOURCE_NAME) ;

  mask_band_source = source_init ;
  mask_band_source.refcount = 1 ; /* Never release this source */
  mask_band_source.make_pool = mask_band_make_pool ;
  mask_band_source.mm_pool = band_source_mm_pool ;
  NAME_OBJECT(&mask_band_source, RESOURCE_SOURCE_NAME) ;

  framebuffer_band_source = source_init ;
  framebuffer_band_source.refcount = 1 ; /* Never release this source */
  framebuffer_band_source.make_pool = framebuffer_band_make_pool ;
  framebuffer_band_source.mm_pool = band_source_mm_pool ;
  NAME_OBJECT(&framebuffer_band_source, RESOURCE_SOURCE_NAME) ;

  basemap_band_source = source_init ;
  basemap_band_source.refcount = 1 ; /* Never release this source */
  basemap_band_source.make_pool = basemap_band_make_pool ;
  NAME_OBJECT(&basemap_band_source, RESOURCE_SOURCE_NAME) ;

  printer_buffer_warned = 0;
  printer_buffer_warned_job = 0;
}

void bandtable_C_globals(core_init_fns *fns)
{
  init_C_globals_bandtable() ;

  fns->swinit = bandtable_swinit ;
  fns->finish = bandtable_finish ;
}

/* Log stripped */
