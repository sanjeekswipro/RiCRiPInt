/** \file
 * \ingroup rendering
 *
 * $HopeName: SWv20!src:render.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software.  All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Rendering setup and outer loops.
 */

#include "core.h"
#include "coreinit.h"
#include "hqmemcpy.h"
#include "hqmemset.h"
#include "hqatomic.h"
#include "render.h"
#include "bitblth.h"
#include "bitblts.h"
#include "blitcolorh.h"
#include "blitcolors.h"
#include "blttables.h"
#include "surface.h"
#include "md5.h"
#include "swstart.h" /* SwThreadIndex() */
#include "taskh.h"
#include "taskres.h"
#include "riptimeline.h"
#include "tables.h" /* count_bits_set_in_byte */
#include "bandtable.h"

#include "debugging.h"
#include "timing.h"
#include "swerrors.h"
#include "swdevice.h"
#include "swraster.h"
#include "swcopyf.h"
#include "objects.h"
#include "mm.h"
#include "monitor.h"
#include "devices.h"
#include "namedef_.h"
#include "basemap.h"
#include "vndetect.h"
#include "swevents.h"
#include "corejob.h"

#include "often.h"

#include "matrix.h"
#include "params.h"
#include "stacks.h"
#include "graphics.h"
#include "halftone.h" /* ht_export_screens */
#include "htrender.h" /* ht_start_sheet */
#include "asyncps.h"
#include "progupdt.h"
#include "dlstate.h"
#include "rleblt.h" /* RLESTATE */
#include "swrle.h" /* POINTER_SIZE_IN_WORDS */
#include "patternrender.h" /* PATTERN_OFF */
#include "control.h"
#include "gstate.h" /* invalidate_gstate_screens */
#include "statops.h"
#include "progress.h"
#include "renderom.h" /* omit_blank_separations */

#include "mlock.h"
#include "ripmulti.h" /* IS_INTERPRETER */
#include "swtrace.h"
#include "spdetect.h"
#include "rcbcntrl.h"
#include "gs_color.h"
#include "gsccalib.h"  /* gsc_report_uncalibrated_screens */
#include "trap.h"      /* trap_context_t */
#include "ripdebug.h"
#include "pclGstate.h"
#include "pclAttrib.h" /* pclPartialPaintSafe */
#include "fonth.h" /* char_doing_cached */
#include "dl_purge.h"
#include "genhook.h"

#include "compress.h"
#include "dicthash.h" /* systemdict */
#include "groupPrivate.h" /* groupNewBackdrops */
#include "hdlPrivate.h"
#include "display.h"
#include "gu_htm.h" /* MODHTONE_REF */
#include "swhtm.h" /* sw_htm_dohalftone_request */
#include "gu_chan.h" /* SPECIALHANDLING_OBJECTMAP */
#include "backdrop.h" /* bd_bandRelease, bd_backdropCloseAll */
#include "rleColorantMapping.h"
#include "xpswatermark.h"
#include "spotlist.h"
#include "jobmetrics.h"
#include "objnamer.h"
#include "preconvert.h"
#include "region.h"
#include "imstore.h"
#include "imcontext.h" /* im_context */
#include "interrupts.h"
#include "gschtone.h" /* gsc_getSpotno */
#include "pgbproxy.h" /* pgbproxy_setflush() */
#include "forms.h"
#include "v20start.h"
#include "irr.h"
#include "surfacecb.h"
#include "tranState.h"
#include "imexpand.h" /* IM_EXPBUF */
#include "pscontext.h"

/** The partial painting mechanism of writing out partially painted bands
   to disk before reading them back in again for the next stage of rendering
   is used for partial painting proper (i.e. low-memory) and also for the
   two-pass method of compositing (transparency) followed by a final render
   (non-transparency).  The following values help distinguish, where needed,
   exactly what we're doing. */
enum {
  PAINT_TYPE_PARTIAL,
  PAINT_TYPE_COMPOSITE,
  PAINT_TYPE_FINAL,
  N_PAINT_TYPES
} ;


#define FORM_UPDATE( _macro, _forms, _retval, _conval, _clipval ) MACRO_START \
  _macro ( (_forms)->retainedform ) = (_retval); \
  _macro ( (_forms)->contoneform ) = \
    _macro ( (_forms)->objectmapform ) = (_conval); \
  _macro ( (_forms)->clippingform ) = \
    _macro ( (_forms)->patternform ) = \
      _macro ( (_forms)->patternclipform ) = \
        _macro ( (_forms)->patternshapeform ) = \
          _macro ( (_forms)->maskedimageform ) = \
            _macro ( (_forms)->intersectingclipform ) = \
              _macro ( (_forms)->htmaskform ) = (_clipval); \
MACRO_END


/**
  The render_base_t structure contains more global state as well as
  render_state_t as a member.  High-up rendering functions refer to
  render_base_t while lower-level functions only get render_state_t/render_info_t.
  The idea is that when render_object_list_of_band() (which is a fairly low-
  level function but called from higher functions which would not know how to
  initialise the variables in render_base_t) is called, then it and the
  functions below it in the call chain have no visibility of render_base_t.
*/
typedef struct render_base_t {
  render_state_t rs;         /**< Render state information */
  render_forms_t forms;      /**< Forms to render into. */
} render_base_t ;

/** Arguments for asynchronous render page driver. */
struct page_data_t {
  hq_atomic_counter_t refcount ; /**< Atomically-accessed reference count. */
  int paint_type ;    /**< partial/retained/final render. */
  int32 numcopies ;   /**< Number of passes to make. */
  Bool page_is_composite ; /**< From spdetect.c */
  Bool page_is_separations ; /**< From spdetect.c */
  Bool multiple_separations ; /**< Interpreter detected multiple seps. */
  BGMultiState pageState ; /**< Region use */
  surface_handle_t surface_handle ; /**< Page-level surface handle. */
  Bool surface_started ; /**< Did surface begin succeed? */
  trap_context_t *trap_context ; /**< Trapping context. */
  int32 transparency_strategy ; /**< TransparencyStrategy systemparam. */
  int32 compress_bands ; /**< CompressBands systemparam. */
  Bool detect_separation ; /**< DetectSeparation systemparam. */
  Bool never_render ;      /**< NeverRender userparam. */
  Bool dynamic_bands ;      /**< DynamicBands systemparam. */
  int32 dynamic_band_limit ; /**< DynamicBandLimit systemparam. */
  Bool use_screen_cache_name ; /**< UseScreenCacheName userparam. */
  Bool screen_dot_centered ; /**< ScreenDotCentered systemparam. */
  task_group_t *trap_group ; /**< Task group for all trapping subtasks. */
  dl_color_t dlc_watermark ; /**< DL color for watermarking. */
  SPOTNO spotno_watermark ; /**< Spotno for watermarking. */
  Bool fail_job ;           /**< Does a render failure fail the job? */
  void *scratch_band; /**< Scratch band to give to PGB. */

  OBJECT_NAME_MEMBER
} ;

#define PAGE_DATA_NAME "Render page data"

/** Arguments for asynchronous render pass driver. */
struct pass_data_t {
  hq_atomic_counter_t refcount ; /**< Atomically-accessed reference count. */
  page_data_t *page_data ;   /**< Page-variant data. */
  int32 paint_type ;         /**< Paint type for this render pass. */

  /** trim_start_band and trim_end_band are always set in calculateTrim() to
     indicate the first and last bands containing new information They are
     used to prevent unecessary reads and writes from and to %pagebuffer%
     during partial and final paints when the %pagebuffer% devparam
     WriteAllOutput is not true. Note that if the page has no marks then
     trim_end_band is -1. */
  bandnum_t trim_startband;  /**< Number of first band with any content */
  bandnum_t trim_endband;    /**< Number of last band with any content  */

  surface_handle_t surface_handle ; /**< Page-level surface handle */
  int32 region_types ;       /**< Region types for this render pass. */
  int32 trim_would_start ;   /**< Trim start from partial painting. */
  int32 trim_would_end ;     /**< Trim end from partial painting. */
  Bool trim_to_page ;        /**< Trim enabled for this page. */
  Bool core_might_compress;  /**< Will core compress, if allowed by skin? */
  Bool allowMultipleThreads ; /**< Should this pass allow multiple threads? */
  Bool writealloutput ;      /**< Do we need to send empty bands? */
  Bool screen_all_bands; /**< Does modular screening need all bands? */
  Bool serialize_mht; /**< Does modular screening need to be serialized? */

  struct sw_htm_render_info *mht_info; /**< Page-wide data for modular hts. */
  unsigned int mht_max_latency ; /**< Max MHT latency on the page. */

  IM_EXPBUF **im_expbufs;    /**< All image expander buffers. */

  /* etc? Easily inited or calculated should be done on spec. */

  OBJECT_NAME_MEMBER
} ;

#define PASS_DATA_NAME "Pass-variant render data"

struct sheet_data_t {
  pass_data_t *pass_data ; /**< Pass-variant data. */
  /** Number of (non-omitted) colorants per band, including object map */
  int32 colorants_per_band;
  int32 nSheetNumber ;     /**< Sheet number. */
  Bool fCanCompress ;      /**< Can the RIP compress data? */
  GUCR_CHANNEL *hf ;       /**< Start channel of first frame in sheet. */
  Bool fUnloadChForms ;    /**< Halftone chforms need unloading? */
  /** \todo ajcd 2010-12-11: Should this be in frame_data rather than
      sheet_data? ht_export_screens() loops over colorants of the frame,
      rather than all frames of sheet: */
  Bool fScreensExported;   /**< RLE screens exported? */
  enum printerstate_e {
    SHEET_OK,          /**< Sheet completed correctly. */
    SHEET_CANCEL,      /**< Cancel this sheet, continue with next. */
    SHEET_REOUTPUT,    /**< Cancel this sheet, repeat sheet graph. */
    SHEET_REOUTPUTPGB, /**< Cancel this sheet, read/write PGB for sheet. */
    SHEET_ABORT        /**< Cancel all sheets. */
  } *printerstate ;
  Bool surface_started ;    /**< Has the surface begin happened? */
  surface_handle_t surface_handle ; /**< Sheet-level surface handle. */
  DEVICE_FILEDESCRIPTOR pgbfd ;
  resource_pool_t *band_pool ; /**< Resource pool for output bands. */
  task_group_t *task_group ; /**< Sheet's task group. */

  OBJECT_NAME_MEMBER
} ;

#define SHEET_DATA_NAME "Sheet-variant render data"

struct frame_data_t {
  hq_atomic_counter_t refcount ; /**< Atomically-accessed reference count. */
  sheet_data_t *sheet_data ;     /**< Sheet-variant data. */
  GUCR_CHANNEL *hf ;             /**< Start channel of frame. */
  int32 nFrameNumber ;           /**< Frame number. */
  Bool surface_started ;         /**< Has the surface begin happened? */
  surface_handle_t surface_handle ;      /**< Frame-level surface handle. */
  task_t *render_start_task ;    /**< Frame's render start task. */
  task_t *render_end_task ;      /**< Frame's render end task. */

  OBJECT_NAME_MEMBER
} ;

#define FRAME_DATA_NAME "Frame-variant render data"

struct band_data_t {
  hq_atomic_counter_t refcount ; /**< Atomically-accessed reference count. */
  dbbox_t bbox ;             /**< BBox of band as total frame+line. */
  dcoord y_allocated ;       /**< Height of allocated band memory. */
  intptr_t surface_pass ;    /**< Surface pass number. */
  frame_data_t *frame_data ; /**< Frame-variant data. */
  /** \todo ajcd 2011-09-08: We could avoid storing this, and use
      task_resource_fix(band->bbox.y2) in each band task that needs to find
      it. */
  band_t *p_band ;           /**< The band entry assigned for this band. */
  task_t *output_task ;      /**< Band's output task, needed for retry dependencies. */
  Bool incomplete ;          /**< True if there will be another render pass over this y1,y2 extent. */

  OBJECT_NAME_MEMBER
} ;

#define BAND_DATA_NAME "Band-variant render data"

/** Args structure for construction of a sheet graph. */
typedef struct render_graph_t {
  task_vector_t *mht ;    /**< Compression hold tasks for MHT latency. */
  task_t *prev_render ;   /**< Previous render task if needed for MHT. */
  task_t *last_render ;   /**< A convenience whilst building sheets/frames. */
  task_t *last_output ;   /**< A convenience whilst building sheets/frames. */
  DL_STATE *page ;        /**< Page for which we're building the graph. */

  OBJECT_NAME_MEMBER
} render_graph_t ;

#define RENDER_GRAPH_NAME "Render graph builder"

#if defined(DEBUG_BUILD)
int32 debug_render ;
int32 debug_render_firstband ;
int32 debug_render_lastband ;
#endif

static Bool printerstatus(DL_STATE *page) ;
static Bool printerupset(DL_STATE *page, sheet_data_t *sheet);

/*---------------------------------------------------------------------------*/
GUCR_CHANNEL *sheet_rs_handle(const sheet_data_t *sheet)
{
  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;
  HQASSERT(sheet->hf != NULL, "No channel handle in sheet") ;
  return sheet->hf ;
}

pass_data_t *sheet_pass(const sheet_data_t *sheet)
{
  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;
  HQASSERT(sheet->pass_data != NULL, "No pass handle in sheet") ;
  return sheet->pass_data ;
}

Bool pass_doing_transparency(const pass_data_t *pass)
{
  VERIFY_OBJECT(pass, PASS_DATA_NAME) ;
  return (pass->region_types & RENDER_REGIONS_BACKDROP) != 0 ;
}

Bool pass_is_final(const pass_data_t *pass)
{
  VERIFY_OBJECT(pass, PASS_DATA_NAME) ;
  return pass->paint_type == PAINT_TYPE_FINAL ;
}

/*---------------------------------------------------------------------------*/

static int32 band_threads_warned_job ;

Bool trim_to_page;

multi_mutex_t pagebuffer_mutex;


/** Convert a band index within a page to a line number within the page. */
static inline dcoord band_to_line(int32 band, const DL_STATE *page)
{
  if ( band <= 0 )
    return 0 ;
  if ( band >= page->sizedisplaylist )
    return page->page_h ;

  return band * page->band_lines ;
}

/** Convert a combined frame and line number to a frame index. */
static inline int32 frameandline_to_frame(dcoord y, const DL_STATE *page)
{
  return y / page->page_h ;
}

/** Convert a combined frame and line number to a line number. */
static inline dcoord frameandline_to_line(dcoord y, const DL_STATE *page)
{
  return y % page->page_h ;
}

/** Convert a combined frame and line number to a band index within a page. */
static inline int32 frameandline_to_band(dcoord y, const DL_STATE *page)
{
  return frameandline_to_line(y, page) / page->band_lines ;
}

/** Convert a combined frame and line number to a combined frame and band. */
static inline int32 frameandline_to_frameandband(dcoord y, const DL_STATE *page)
{
  return frameandline_to_frame(y, page) * page->sizedisplaylist +
    frameandline_to_band(y, page) ;
}

/*---------------------------------------------------------------------------*/

static Bool pagebuffer_swinit(SWSTART *params)
{
  UNUSED_PARAM(SWSTART *, params) ;

  multi_mutex_init(&pagebuffer_mutex, PGB_LOCK_INDEX, FALSE,
                   SW_TRACE_PAGEBUFFER_ACQUIRE, SW_TRACE_PAGEBUFFER_HOLD);

  return TRUE ;
}

static void pagebuffer_finish(void)
{
  multi_mutex_finish(&pagebuffer_mutex);
}

void pagebuffer_C_globals(core_init_fns *fns)
{
  fns->swinit = pagebuffer_swinit ;
  fns->finish = pagebuffer_finish ;
}


Bool send_pagebuff_param(DL_STATE *page, DEVICEPARAM *param)
{
  if ( page->pgbdev == NULL ) /* Ignore if no PGB device */
    return TRUE ;

  switch ( (*theISetParam(page->pgbdev))(page->pgbdev, param) ) {
  case ParamIgnored:
  case ParamAccepted:
    break;
  case ParamTypeCheck:
    return error_handler( TYPECHECK );
  case ParamRangeCheck:
    return error_handler( RANGECHECK );
  case ParamConfigError:
    return error_handler( CONFIGURATIONERROR ) ;
  case ParamError:
  default: /* return with the last error */
    return device_error_handler(page->pgbdev) ;
  }
  return TRUE;
}


/* Short cuts for setting pagebuffer parameters. Float parameters have a
   variant that uses non-constant strings, all others are constant. */

#define SET_PGB_PARAM_I(page_, name_, value_) { \
  DEVICEPARAM p_; \
  theDevParamName(p_)    = (uint8 *)("" name_ ""); \
  theDevParamNameLen(p_) = (int32)sizeof("" name_ "") - 1; \
  theDevParamType(p_)    = ParamInteger; \
  theDevParamInteger(p_) = (value_); \
  if ( !send_pagebuff_param(page_, &p_)) return FALSE; \
}

#define SET_PGB_PARAM_B(page_, name_, value_) { \
  DEVICEPARAM p_; \
  theDevParamName(p_)    = (uint8 *)("" name_ ""); \
  theDevParamNameLen(p_) = (int32)sizeof("" name_ "") - 1; \
  theDevParamType(p_)    = ParamBoolean; \
  theDevParamBoolean(p_) = (value_); \
  if ( !send_pagebuff_param(page_, &p_)) return FALSE; \
}

#define SET_PGB_PARAM_S(page_, name_, value_, length_) { \
  DEVICEPARAM p_; \
  theDevParamName(p_)    = (uint8 *)("" name_ ""); \
  theDevParamNameLen(p_) = (int32)sizeof("" name_ "") - 1; \
  theDevParamType(p_)    = ParamString; \
  theDevParamString(p_)  = (uint8 *) (value_); \
  theDevParamStringLen(p_) = (length_); \
  if ( !send_pagebuff_param(page_, &p_)) return FALSE; \
}

#define SET_PGB_PARAM_F(page_, name_, value_) { \
  DEVICEPARAM p_; \
  theDevParamName(p_)    = (uint8 *)("" name_ ""); \
  theDevParamNameLen(p_) = (int32)sizeof("" name_ "") - 1; \
  theDevParamType(p_)    = ParamFloat; \
  theDevParamFloat(p_)   = (value_); \
  if ( !send_pagebuff_param(page_, &p_)) return FALSE; \
}

#define SET_PGB_PARAM_F_varname(page_, name_, value_) { \
  DEVICEPARAM p_; \
  uint8 * name__ = (uint8 *)(name_); \
  theDevParamName(p_)    = (name__); \
  theDevParamNameLen(p_) = (int32) strlen((char *)name__); \
  theDevParamType(p_)    = ParamFloat; \
  theDevParamFloat(p_)   = (value_); \
  if ( !send_pagebuff_param(page_, &p_)) return FALSE; \
}


/** Eek: SET_PGB_PARAM_* macros automatically do return FALSE; if the
 * set fails.  That's not the right thing here, so we wrap them in a function.
 */
static Bool band_set_compressed(DL_STATE *page, Bool is_compressed)
{
  SET_PGB_PARAM_B(page, "BandIsCompressed", is_compressed);
  return TRUE ;
}

static Bool band_set_lines(DL_STATE *page, int32 lines)
{
  SET_PGB_PARAM_I(page, "BandLines", lines);
  return TRUE ;
}

static Bool separation_set_id(DL_STATE *page, int32 sepid)
{
  SET_PGB_PARAM_I(page, "SeparationId", sepid);
  return TRUE ;
}

/** Retrieve a band from the page buffer device (if partial painting).
 */
static Bool read_back_band(DL_STATE *page, band_data_t *band, int32 band_size)
{
  Bool result = TRUE ;
  sheet_data_t *sheet ;
  frame_data_t *frame ;
  band_t *pband ;

  VERIFY_OBJECT(band, BAND_DATA_NAME) ;
  frame = band->frame_data ;
  VERIFY_OBJECT(frame, FRAME_DATA_NAME) ;
  sheet = frame->sheet_data ;
  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;

  pband = band->p_band ;
  HQASSERT(pband, "No band yet allocated") ;

  band_size *= sheet->colorants_per_band ;

  multi_mutex_lock(&pagebuffer_mutex);

  if (sheet->pgbfd >= 0) {
    HQASSERT(!DOING_RUNLENGTH(page), "Should not be reading back RLE") ;
    if ( !band_set_lines(page, band->bbox.y2 - band->bbox.y1 + 1) ) {
      result = error_handler(IOERROR);
    } else {
      do {
        Hq32x2 pgbPos ;
        int32 frame_and_line = /* Line number within frame. */
          gucr_getRenderIndex(frame->hf) * page->page_h +
          frameandline_to_line(band->bbox.y1, page) ;

        HQASSERT(gucr_getRenderIndex(frame->hf) >= 0, "Render index is not set");

        Hq32x2FromInt32(&pgbPos, frame_and_line);
        if ( (*theISeekFile(page->pgbdev))(page->pgbdev, sheet->pgbfd,
                                           &pgbPos, SW_SET) ) {
          break; /* the seek succeeded */
        }
        if ( !printerupset(page, sheet) )
          result = FALSE;
      } while ( result ) ;

      while ( result ) {
        if ( (*theIReadFile(page->pgbdev))(page->pgbdev, sheet->pgbfd,
                                           (uint8 *)pband->mem,
                                           band_size) == band_size )
          break;
        if ( !printerupset(page, sheet) )
          result = FALSE ;
      }

      pband->size_to_write = band_size ;
    }
  } else {
    /* some other thread has closed it, due to error */
    result = error_handler(IOERROR) ;
  }

  multi_mutex_unlock(&pagebuffer_mutex);

  return result ;
}

/** Opens the pagebuffer device "file". If a partial paint has occurred, the
  access mode is read/write (as bands will need to be read-back), otherwise
  write-only.
*/
static Bool open_pagebuffer_device(DL_STATE *page, sheet_data_t *sheet,
                                   Bool reoutput)
{
  pass_data_t *pass ;
  int32 flags;
  uint8 *name ;

  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;
  pass = sheet->pass_data ;
  VERIFY_OBJECT(pass, PASS_DATA_NAME) ;

  HQASSERT(pass->paint_type >= 0 && pass->paint_type < N_PAINT_TYPES,
           "Paint type invalid") ;

  if ( !reoutput ) {
    static uint8* filenames[N_PAINT_TYPES] = {
      (uint8*)"PartialPaint",
      (uint8*)"Compositing",
      (uint8*)"Painting"
    };

    name = filenames[pass->paint_type] ;
    flags = (page->rippedtodisk
             ? SW_RDWR
             : (SW_WRONLY|SW_CREAT|SW_TRUNC));
  } else {
    name = (uint8 *)"Outputting" ;
    flags = SW_RDWR ;
  }

  /* Track the highest separation number we've attempted to open, which
     therefore may have a PGB file created on disk. */
  if (sheet->nSheetNumber > page->highest_sheet_number)
    page->highest_sheet_number = sheet->nSheetNumber;

  multi_mutex_lock(&pagebuffer_mutex);
  do {
    /** \todo ajcd 2010-12-17: Maybe change to scheme using
        phase-jobnum-pagenum-sepnum for filename, with wildcards implied for
        truncated jobnum/pagenum/sepnum so deletion of PGBs can match all
        from a page, job, sheet, etc. */
    sheet->pgbfd = (*theIOpenFile(page->pgbdev))(page->pgbdev, name, flags);
  } while ( sheet->pgbfd < 0 && printerupset(page, sheet) ) ;
  multi_mutex_unlock(&pagebuffer_mutex);

  if ( sheet->pgbfd < 0 )
    return FALSE ;

  probe_begin(SW_TRACE_PAGEBUFFER, sheet->pgbfd) ;

  if ( pass->core_might_compress )
    if (!get_pagebuff_param(page, (uint8 *)"RIPCanCompress",
                            &sheet->fCanCompress, ParamBoolean, FALSE) )
      return FALSE;
  SET_PGB_PARAM_B(page, "CompressedByRIP", sheet->fCanCompress);

  /* We happen to know that the FD returned by the PGB device is actually a
     timeline reference. The BandsCopied event callback needs to be able to
     find the resource pool used. */
  (void)SwTimelineSetContext((sw_tl_ref)sheet->pgbfd, SWTLC_BAND_OWNER,
                             sheet->band_pool) ;

  return TRUE;
}

/* ----------------------------------------------------------------------------
   Function:  close_pagebuffer_device()
*/
static Bool close_pagebuffer_device(DL_STATE *page, sheet_data_t *sheet)
{
  Bool result = TRUE ;

  HQASSERT(multi_mutex_is_locked(&pagebuffer_mutex), "PGB mutex not locked") ;

  if ( sheet->pgbfd >= 0 ) {
    DEVICE_FILEDESCRIPTOR outputfd = sheet->pgbfd ;

    /* Note we've tried really hard to close the PGB device, so we don't
       retry in the cleanup actions. */
    sheet->pgbfd = -1 ;
    probe_end(SW_TRACE_PAGEBUFFER, outputfd) ;

    while ( result &&
            (*theICloseFile(page->pgbdev))(page->pgbdev, outputfd) < 0 ) {
      if ( !printerupset(page, sheet) )
        result = FALSE ;
    }

    probe_other(SW_TRACE_LINES_COPIED, SW_TRACETYPE_AMOUNT, 0);
  }

  return result ;
}

/** Ensure that the pagebuffer device is aborted and closed, if it is left
    open by an early rendering termination. This should be called with
    pagebuffer_mutex locked. */
static void abort_pagebuffer_device(DL_STATE *page, sheet_data_t *sheet)
{
  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;

  HQASSERT(multi_mutex_is_locked(&pagebuffer_mutex), "PGB mutex not locked") ;

  if (sheet->pgbfd >= 0) {
    /* Remove the band pool owner from the PGB timeline before losing the
       timeline reference so that any delayed band notifications won't refer
       to invalid sheet objects. */
    (void)SwTimelineSetContext((sw_tl_ref)sheet->pgbfd, SWTLC_BAND_OWNER, NULL) ;

    /* Abort the PGB device. If we get a DeviceNotReady error during the
       abort, we have to retry. */
    while ( (*theIAbortFile(page->pgbdev))(page->pgbdev, sheet->pgbfd) < 0 &&
            (*theILastErr(page->pgbdev))(page->pgbdev) == DeviceNotReady )
      (void)printerstatus(page) ;

    probe_end(SW_TRACE_PAGEBUFFER, sheet->pgbfd) ;
    sheet->pgbfd = -1 ;

    probe_other(SW_TRACE_LINES_COPIED, SW_TRACETYPE_AMOUNT, 0);
  }
}

/* ---------------------------------------------------------------------- */
static Bool output_rle_to_pagebuffer(DL_STATE *page, sheet_data_t *sheet,
                                     band_data_t *band)
{
  dcoord linenumber ;

  VERIFY_OBJECT(band, BAND_DATA_NAME) ;
  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;

  for ( linenumber = band->bbox.y1 ; linenumber <= band->bbox.y2 ; ++linenumber ) {
    uint32 *block;

    for (;;) {
      Hq32x2 pgbPos;
      Hq32x2FromInt32(&pgbPos, linenumber);
      if ((*theISeekFile(page->pgbdev))(page->pgbdev, sheet->pgbfd,
                                        &pgbPos, SW_SET)) {
        break; /* the seek succeeded */
      }
      if ( !printerupset(page, sheet) )
        return FALSE ;
    }

    if ( page->rle_flags & RLE_LINE_OUTPUT ) {
      block = rle_block_first(linenumber, band->bbox.y1) ;
      SET_PGB_PARAM_B(page, "RunLineComplete", !band->incomplete);
      /* The size of this write doesn't actually matter. The recipient of
         the write uses the block pointer as the start of the chain of the
         RLE blocks to write. */
      while ( (*theIWriteFile(page->pgbdev))(page->pgbdev, sheet->pgbfd, (uint8 *)block,
                                             POINTER_SIZE_IN_WORDS * 4) != POINTER_SIZE_IN_WORDS * 4 ) {
        if ( !printerupset(page, sheet) )
          return FALSE ;
      }
    } else {
      /* for all blocks in scanline ... */
      for (block = rle_block_first(linenumber, band->bbox.y1);
           block != NULL;) {
        uint8* content = (uint8*)RLEBLOCK_GET_CONTENTS(block);
        int32 blockSize = RLEBLOCK_GET_SIZE(block);
        block = RLEBLOCK_GET_NEXT(block) ;
        SET_PGB_PARAM_B(page, "RunLineComplete",
                        !band->incomplete && block == NULL);
        while ( (*theIWriteFile(page->pgbdev))(page->pgbdev, sheet->pgbfd,
                                               content, blockSize) != blockSize ) {
          if ( !printerupset(page, sheet) )
            return FALSE ;
        }
      }
    }
  }

  return TRUE;
}

Bool get_pagebuff_param(DL_STATE *page, uint8 *name, int32 *value,
                        int32 type, int32 default_value)
{
  DEVICEPARAM param;

  *value = default_value;

  if ( page->pgbdev == NULL ) /* Default if no PGB device */
    return TRUE ;

  theDevParamName(param)    = name;
  theDevParamNameLen(param) = strlen_int32((char *) name);
  theDevParamType(param)    = type;
  theDevParamInteger(param) = 0;

  switch ( (*theIGetParam(page->pgbdev))(page->pgbdev, &param) ) {
  case ParamIgnored:
    break;
  case ParamAccepted:
    switch (type) {
    case ParamInteger:
    case ParamBoolean:
      *value = theDevParamInteger(param);
      break;
    case ParamFloat:
      /* While this function is not designed to get a float (and retains
         the (int32 *) type for that reason), it has been enhanced to
         set floats, as needed by devops.c. */
      *((float *) value) = theDevParamFloat(param);
      break;
    default:
      return error_handler( TYPECHECK );
    }
    break;

  case ParamTypeCheck:
  case ParamRangeCheck:
  case ParamConfigError:
    HQFAIL( "Illegal return value from get_param" ) ;
    return error_handler( UNREGISTERED ) ;
  case ParamError:
  default:
    /* return with the last error */
    return device_error_handler(page->pgbdev) ;
  }
  return TRUE;
}


void erase_page_buffers(DL_STATE *page)
{
  if ( page->pgbdev != NULL ) { /* Ignore if no PGB device */
    int32 i;

    for ( i = 0 ; i <= page->highest_sheet_number ; ++i ) {
      uint8 filename[LONGESTFILENAME] ;
      /* The Separation PGB parameter is set to the sheet number plus one,
         so we need to make the same adjustment here. */
      swcopyf(filename, (uint8 *)"%d", i + 1) ;
      /* The return value's not checked because not all separations need have a
         page buffer; even if they do, we don't want to error. Delete is
         interpreted by the device as remove the page buffer for the
         Separation (sheet) numbered by the filename. */
      (void)(*theIDeleteFile(page->pgbdev))(page->pgbdev, filename);
    }
  }

  page->highest_sheet_number = -1;
} /* Function erase_page_buffers */


/* ---------------------------------------------------------------------- */
static Bool printererror(DL_STATE *page, int32 errorno,
                         uint8 *message, int32 message_len)
{
  SWMSG_ERROR msg = {0} ;

  /* Issue a printer error event */
  msg.timeline = page->timeline ;
  msg.page_number = page->pageno ;
  msg.fail_job = FALSE ;
  msg.suppress_handling = FALSE ;
  msg.error_name.length = message_len ;
  msg.error_name.string = message ;
#define PRINTER_COMMAND "printerupset" /* Command reported by printer error */
  msg.command.length = sizeof(PRINTER_COMMAND) - 1;
  msg.command.string = (uint8 *)PRINTER_COMMAND ;
  msg.error_number = errorno ;

  /** \todo ajcd 2011-04-27: Capture and propagate detail through C
      call stack. */
  msg.detail = NULL ;

  if ( SwEvent(SWEVT_PRINTER_ERROR, &msg, sizeof(msg)) >= SW_EVENT_ERROR )
    HQTRACE(TRUE,
            ("Error from SwEvent(SWEVT_PRINTER_ERROR, 0x%p, %u)",
             &msg, sizeof(msg))) ;

  if ( msg.fail_job )
    page->job->failed = TRUE ;

  if ( msg.suppress_handling ) {
    /* Skin says to ignore this error. Returning FALSE here is interpreted by
       printerupset() as equivalent to changing the error to
       DeviceCancelPage. */
    /** \todo ajcd 2011-12-02: This is new behaviour, as of HMR 3.0r1. It's
        sort of equivalent to the old PostScript code returning TRUE when the
        interpreter call failed. Decide whether this is the right action, or
        if we should repeat the page, or something else. */
    return FALSE ;
  }

  /* Normal actions for printer error. */
  /* If enabled, ring the progress device bell. */
  if ( progressdev != NULL ) {
    DEVICEPARAM bell = {
      STRING_AND_LENGTH("RingBell"), ParamBoolean, { NULL }
    } ;

    theDevParamBoolean(bell) = TRUE ;
    (void)theISetParam(progressdev)(progressdev, &bell) ;
  }

  monitorf((uint8 *)"%%%%[ PrinterError: %.*s ]%%%%\n",
           message_len, message) ;

  /** \todo ajcd 2011-12-02: The PostScript code this was translated from set
      the printer status in statusdict /jobname, from where it is passed to
      the %config% device by /sendprinterstate calls. We can't do that
      directly here, we may be able to save the message for later
      propagation. */

  return TRUE ;
}

/* This is a C translation of the former statusdict /printerupset
   procedure. Implementing this in C is pipeline compatible, but does not
   allow it to be dynamically re-defined, so we use a printererror event to
   allow the skin some amount of control over reporting the error. */
static Bool printerstatus(DL_STATE *page)
{
  int32 status ;

  (void)get_pagebuff_param(page, (uint8 *)"PrinterStatus",
                           &status, ParamInteger, 0) ;
#if 0
  /** \todo ajcd 2011-12-02: The PostScript code that used to do this
      included the pagedevice InputAttributes in the printer status.
      However, it's not actually used by any of the code except the
      fallback error message, so I haven't plumbed through the code to
      extract the media selection from the page device at start time. */
  status |= page->input_attributes ;
#endif

  if ( status == -1 ) {
    if ( !printererror(page, status,
                       STRING_AND_LENGTH("printer turned off")))
      return FALSE ;

    /** \todo ajcd 2011-12-02: The PostScript code in pseudpss.pss puts out
        a message about resetting the printer, then has a callout to
        /resetprinter which always returns TRUE. Presumably resetprinter
        could be overridden to do device specific functions. */
  } else if ( (status & 0x7fffff80) != 0x7ff80000 ) {
    /* The magic numbers above indicate that the printer error is not
       DERR(DETYPE_CONTINUE, DERR_NONE). Definitions for these are in the
       plugin kit, so can't be used by core code. */
    DEVICEPARAM param = {
      STRING_AND_LENGTH("PrinterMessage"), ParamString, { NULL }
    };
    uint8 hex_string[32] ;

    if ( page->pgbdev == NULL ||
         theIGetParam(page->pgbdev)(page->pgbdev, &param) != ParamAccepted ||
         theDevParamStringLen(param) == 0 ) {
      swcopyf(hex_string, (uint8 *)"%x", status) ;
      theDevParamString(param) = hex_string ;
      theDevParamStringLen(param) = strlen_int32((char *)hex_string) ;
    }

    if ( !printererror(page, status,
                       theDevParamString(param), theDevParamStringLen(param)) )
      return FALSE ;
  }

  return TRUE ;
}

/** \brief Translate pagebuffer errors to PS errors.
 *
 * \param page    The display list for the current output sheet.
 * \param sheet   The sheet data for the current output sheet.
 *
 * \retval FALSE  The printer is upset; abort the current sheet, set
 *                printerstatus in the sheet structure to indicate what to
 *                do about it.
 * \retval TRUE   The printer was not ready. Retry the operation.
 */
static Bool printerupset(DL_STATE *page, sheet_data_t *sheet)
{
  int32 errornumber, newstate ;

  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;

  /* We may call abort_pagebuffer_device() */
  HQASSERT(multi_mutex_is_locked(&pagebuffer_mutex), "PGB mutex not locked") ;

  switch ( (*theILastErr(page->pgbdev))(page->pgbdev) ) {
  case DeviceReOutput:
    newstate = printerstatus(page) ? SHEET_REOUTPUT : SHEET_CANCEL ;
    errornumber = NOT_AN_ERROR ;
    break;
  case DeviceReOutputPageBuffer:
    newstate = printerstatus(page) ? SHEET_REOUTPUTPGB : SHEET_CANCEL ;
    errornumber = NOT_AN_ERROR ;
    break;
  case DeviceNotReady:
    /* The PGB device will be expecting a retry, so don't allow an error event
       handler to override that. */
    (void)printerstatus(page) ;
    return TRUE;
  case DeviceCancelPage:
    newstate = SHEET_CANCEL ;
    errornumber = NOT_AN_ERROR ;
    break;
  case DeviceIOError:
    newstate = SHEET_ABORT ;
    errornumber = IOERROR;
    break;
  case DeviceInterrupted:
    newstate = SHEET_ABORT ;
    errornumber = INTERRUPT;
    break;
  case DeviceVMError:
    /** \todo ajcd 2010-12-17: Setting VMERROR because of a device VM error
        is somewhat dubious, because the device probably can't fix it. The
        task that signalled this will retry, and probably fail again. We
        need a way of signalling to the task API that this is a non-retryable
        VMERROR. */
    newstate = SHEET_ABORT ;
    errornumber = VMERROR;
    break;
  default:
    /* No excuse for having untrapped errors. Die horribly. */
    newstate = SHEET_ABORT ;
    errornumber = UNREGISTERED ;
    break ;
  }

  *sheet->printerstate = newstate ;

  /* Should we abort the PGB device? */
  if ( newstate == SHEET_ABORT || newstate == SHEET_CANCEL )
    abort_pagebuffer_device(page, sheet) ;

  /* Explicit cancel of the sheet group avoids us having to wait for the
     error to propagate back through the render task graph all the way to
     the sheet level, whilst the interpreter creates new sheet tasks that
     will never be executed, and the other render tasks attempt to output
     bands that will never be seen. */
  task_group_cancel(sheet->task_group, errornumber) ;
  return error_handler(errornumber) ;
}

/* trim_would_start _end are static for partial paints. They are used so
   we can return the outermost band numbers over all paints despite
   changes to the display list. */
static int32 trim_would_start = 0;
static int32 trim_would_end = 0;

/** This function calculates the numbers of the first and last bands with any
 *  real content taking into account separation shifts (i.e. separation
 *  imposition). This function is called before any of the render loops, but
 *  for both partial paints and final renders.
 *
 *  The start and end band numbers are returned via the pass_data structure.
 */
static void calculateTrim(DL_STATE *page, pass_data_t *pass)
{
  int32       i;
  int32       offset;
  int32       fact = page->sizedisplayfact;
  int32       dl_size = page->sizedisplaylist;
  Bool fExit;
  DLREF* dlobjErase;
  LISTOBJECT* lobj;
  GUCR_CHANNEL* hf;
  GUCR_COLORANT* hc;
  GUCR_RASTERSTYLE *hr = page->hr ;

  pass->trim_startband = 0;
  pass->trim_endband = dl_size - 1;
  pass->trim_to_page = trim_to_page ;

  /* Find first band with something in it, taking account of
     separation shifts. Trim is only calculated per page not per
     output film, so this is worse than need be */

  dlobjErase = dl_get_orderlist(page);
  lobj = dlref_lobj(dlobjErase);

  i = 0;
  if ( !lobj->dldata.erase.with0 &&  /* check for non-zero background */
       page->irr.store == NULL ) {

    /* Loop over all dl bands starting from the top */
    for ( fExit = FALSE; i < dl_size; i++ ) {

      for ( hf = gucr_framesStart(hr);
            ! fExit && gucr_framesMore(hf);
            gucr_framesNext(&hf) ) {
        for ( hc = gucr_colorantsStart(hf);
              ! fExit && gucr_colorantsMore(hc, GUCR_INCLUDING_PIXEL_INTERLEAVED);
              gucr_colorantsNext(&hc) ) {
          const GUCR_COLORANT_INFO *colorantInfo;

          /* Find actual band for start of colorant */
          if ( gucr_colorantDescription(hc, &colorantInfo) ) {
            offset = i + colorantInfo->offsetBand;

            if ( offset < 0 ) {              /* Force looking at first band */
              offset = 0;
            } else if ( offset >= dl_size ) { /* Force looking at last band */
              offset = dl_size - 1;
            }

            dlobjErase = dl_get_head(page, offset/fact);
            fExit = (dlref_next(dlobjErase) != NULL);
          }
        }
      }
      if (fExit)
        break;
    }
  }

  pass->trim_startband = i;

  /* Retain the first band across paints */
  if ( !page->rippedtodisk ) {
    trim_would_start = i;
  } else {
    if ( i < trim_would_start ) {
      trim_would_start = i;
    }
  }


  /* Repeat the above to find the last band with something on it. */
  dlobjErase = dl_get_orderlist(page);
  lobj       = dlref_lobj(dlobjErase);
  i = dl_size - 1;

  if ( !lobj->dldata.erase.with0 &&  /* check for non-zero background */
       page->irr.store == NULL ) {

    /* Loop over all dl bands */
    for ( fExit = FALSE; i >= 0; i-- ) {

      for ( hf = gucr_framesStart(hr);
            ! fExit && gucr_framesMore(hf);
            gucr_framesNext(&hf) ) {
        for ( hc = gucr_colorantsStart(hf);
              ! fExit && gucr_colorantsMore(hc, GUCR_INCLUDING_PIXEL_INTERLEAVED);
              gucr_colorantsNext(&hc) ) {
          const GUCR_COLORANT_INFO *colorantInfo;

          /* Find actual band for start of colorant */
          if ( gucr_colorantDescription(hc, &colorantInfo) ) {
            offset = i + colorantInfo->offsetBand;

            if ( offset < 0 ) {              /* Force looking at first band */
              offset = 0;
            } else if ( offset >= dl_size ) { /* Force looking at last band */
              offset = dl_size - 1;
            }

            dlobjErase = dl_get_head(page, offset/fact);
            fExit = (dlref_next(dlobjErase) != NULL);
          }
        }
      }
      if (fExit)
        break;
    }
  }

  pass->trim_endband = i;

  /* Retain the last band across paints */
  if ( !page->rippedtodisk ) {
    trim_would_end = i;
  } else {
    if ( i > trim_would_end ) {
      trim_would_end = i;
    }
  }


  /* If the %pagebuffer% device can't cope with 'holy' pages, force
     all white or unmodified bands at the top and bottom of the current
     display list to be written.
     Note: only bands from trim_start to trim_end are rendered, even
     if writealloutput is TRUE, so we need to ensure they include all
     bands which may be output directly from %pagebuffer% */
  if ( pass->writealloutput ) {
    if ( trim_to_page ) {
      pass->trim_startband = trim_would_start;
      pass->trim_endband   = trim_would_end;
    } else {
      pass->trim_startband = 0;
      pass->trim_endband   = dl_size - 1;
    }
  }

  pass->trim_would_start = trim_would_start ;
  pass->trim_would_end = trim_would_end ;
}

/** Check colorants x offset does not cause bbox of dl to render outside
 * image area.
 *
 * \param thepage  The output dl to check bbox against
 *
 * \retval TRUE if all colorants are known and positioned ok
 * \retval FALSE otherwise.
 */
static Bool check_colorant_positions(DL_STATE *thepage)
{
  int32   page_width;
  GUCR_CHANNEL* hf;
  GUCR_COLORANT* hc;
  GUCR_RASTERSTYLE* hr = thepage->hr ;

  page_width = thepage->page_w;

  for ( hf = gucr_framesStart(hr) ; gucr_framesMore(hf); gucr_framesNext(&hf) ) {

    for ( hc = gucr_colorantsStart(hf);
          gucr_colorantsMore(hc, GUCR_INCLUDING_PIXEL_INTERLEAVED);
          gucr_colorantsNext(&hc) ) {
      const GUCR_COLORANT_INFO *colorantInfo;

      if ( gucr_colorantDescription(hc, &colorantInfo) ) {
        dbbox_t bbox ;

        if ( !colorantInfo->fBackground )
          bbox = thepage->page_bb ;
        else
          hdlBBox(dlPageHDL(thepage), &bbox);

        if ( DOING_TRANSPARENT_RLE(thepage) ) {
          if ( colorantInfo->offsetX != 0 || colorantInfo->offsetY != 0 )
            return detailf_error_handler(RANGECHECK,
                                         "Separation positioning and RunTransparency are not valid together.");
        } else if ( colorantInfo->offsetX >= 0 ) {
          if ( colorantInfo->offsetX + bbox.x2 > page_width )
            return detailf_error_handler(RANGECHECK,
                                         "Separation '%.*s' is too far right.",
                                         theINLen(colorantInfo->name),
                                         theICList(colorantInfo->name));
        } else if ( colorantInfo->offsetX + bbox.x1 < 0 ) {
          return detailf_error_handler(RANGECHECK,
                                       "Separation '%.*s' is too far left.",
                                       theINLen(colorantInfo->name),
                                       theICList(colorantInfo->name));
        }
      }
    }
  }

  /* All colorants known and in position */
  return TRUE;
} /* Function check_colorant_positions */

/**
 * Is Separation Imposition enabled ?
 *
 * Test by enumerating all colorants of all frames and seeing if any of them
 * has been position with a non-zero offset.
 */
static Bool sep_imposition(DL_STATE *page)
{
  GUCR_RASTERSTYLE *hr = page->hr;
  GUCR_CHANNEL *hf;
  GUCR_COLORANT *hc;

  for ( hf = gucr_framesStart(hr) ; gucr_framesMore(hf);
        gucr_framesNext(&hf) ) {
    for ( hc = gucr_colorantsStart(hf);
          gucr_colorantsMore(hc, GUCR_INCLUDING_PIXEL_INTERLEAVED);
          gucr_colorantsNext(&hc) ) {
      const GUCR_COLORANT_INFO *colorantInfo;

      if ( gucr_colorantDescription(hc, &colorantInfo) ) {
        if ( colorantInfo->offsetX != 0 || colorantInfo->offsetY != 0 )
          return TRUE;
      }
    }
  }
  return FALSE;
}

static Bool add_wm_objects(DL_STATE *page, page_data_t *page_data)
{
  UNUSED_PARAM(DL_STATE *, page);
  UNUSED_PARAM(page_data_t *, page_data);
  return TRUE;
}

/** Determine if we should allow extra dynamic bands. */
static Bool setup_dynamic_bands(DL_STATE *page, page_data_t *page_data)
{
  VERIFY_OBJECT(page_data, PAGE_DATA_NAME) ;
  HQASSERT(page != NULL, "No DL page") ;

  /* Do we need any output bands? Only if we're going to actually render it. */
  if ( !page_data->never_render ) {
    Bool dynamic_bands = page_data->dynamic_bands ;

    /* We require both the pagebuffer (if it specifies DynamicBands) and
       the systemparam both set to alter the band allocation. */
    if ( dynamic_bands &&
         !get_pagebuff_param(page, (uint8 *) "DynamicBands",
                             &dynamic_bands, ParamBoolean, TRUE) )
      return FALSE ;

    if ( dynamic_bands ) {
      /* Dynamic bands are spare bands that we allocate on demand so that the
         plugin can output asynchronously. We only add the dynamic bands on
         if the Plugin or PGB device can output asynchronously. */
      int32 dbl = page_data->dynamic_band_limit ;
      resource_requirement_t *req ;
      requirement_node_t *expr ;

      req = page->render_resources ;
      HQASSERT(req != NULL, "No page resource request") ;

      expr = requirement_node_find(req, REQUIREMENTS_PAGEADD) ;
      HQASSERT(expr != NULL, "No page extra resource expression") ;

      if ( dbl == 0 ) /* Use as many dynamic bands as we want. */
        dbl = page->sizedisplaylist ;

      /* Ignore the return value, we're setting dynamic bands as an
         optimization. If we're in low memory and cannot allocate the lookup
         table for the number of bands required, we can just make do with
         what we've got. */
      (void)requirement_node_setmax(expr, TASK_RESOURCE_BAND_OUT, dbl) ;
    }
  }

  return TRUE ;
}


static Bool init_dl_render(corecontext_t *context, DL_STATE *page, page_data_t *page_data,
                           dl_erase_t spawn_type)
{
  DL_STATE *nextpage ;
  Bool ok = TRUE;
  resource_requirement_t *req ;
  requirement_node_t *expr ;
  resource_pool_t *pool ;
  unsigned int bandmemlimit ;

  HQASSERT(context->is_interpreter, "Not in interpreter") ;
  HQASSERT(context->page == page, "page out of sync") ;
  VERIFY_OBJECT(page_data, PAGE_DATA_NAME) ;

  req = page->render_resources ;
  HQASSERT(req != NULL, "No page resource request") ;

  expr = requirement_node_find(req, REQUIREMENTS_PAGELIMIT) ;
  HQASSERT(expr != NULL, "No page limit resource expression") ;

  /* Apply memory limit. */
  pool = resource_requirement_get_pool(req, TASK_RESOURCE_BAND_OUT) ;
  HQASSERT(pool != NULL, "No output band pool") ;
  bandmemlimit = pool->entry_size != NULL
    ? (unsigned int)(context->systemparams->MaxBandMemory * 1024.0 * 1024.0 /
                     pool->entry_size(pool, NULL))
    : 0 ;
  resource_pool_release(&pool) ;

  /* Regardless of the value here, the request update will force the
     maximum to be larger than the band out minimum. */
  if ( !requirement_node_minmax(expr, TASK_RESOURCE_BAND_OUT, bandmemlimit) )
    return FALSE ;

 /* If we attempted to render a page which had a sheet, and were not
     suppressing rendering, then we mark the job as having output. This
     only affects whether the job number is incremented, so it doesn't
     matter if the render fails. */
  /** \todo ajcd 2011-03-18: This doesn't take into account separation
      omission. However, it must be done synchronously in the interpreter, so
      we can't wait until the rendering to make this determination. Does it
      matter? */
  if ( !page_data->never_render && gucr_framesStart(page->hr) )
    page->job->has_output = TRUE ;

  /* May have a page group that needs closing. */
  if ( page->currentGroup != NULL ) {
    HQASSERT(groupGetUsage(page->currentGroup) == GroupPage,
             "Expected only a page group to be open");
    /* This is the last operation that needs to know about recombine. */
    if ( !groupClosePageGroup(&page->currentGroup,
                              spawn_type == DL_ERASE_PRESERVE ||
                              spawn_type == DL_ERASE_COPYPAGE, TRUE) )
      return FALSE;
  }
  { /* If it's a separation, patch the true colorant onto Black. */
    NAMECACHE *sep_name = get_separation_name(FALSE);
    COLORANTINDEX ci_sep = guc_colorantIndexReserved(page->hr, sep_name);
    COLORANTINDEX ci_black = guc_colorantIndex(page->hr,
                                               &system_names[NAME_Black]);

    if ( ci_sep != COLORANTINDEX_UNKNOWN && ci_black != COLORANTINDEX_UNKNOWN
         && ci_sep != ci_black ) {
      /* In case it's a spot color without screens (so ht_patchSpotnos()
         is a no-op), patch /Default onto Black first. */
      if ( !ht_patchSpotnos(COLORANTINDEX_NONE, ci_black,
                            &system_names[NAME_Black], TRUE) )
        return FALSE;
      if ( !ht_patchSpotnos(ci_sep, ci_black,
                            &system_names[NAME_Black], TRUE) )
        return FALSE;
      invalidate_gstate_screens();
    }
    if ( !update_erase(page) )
      return FALSE;
  }
  if ( !add_wm_objects(page, page_data) )
    return FALSE;

  if ( gucr_halftoning(page->hr) ) {
    MODHTONE_REF *mhtone = htm_first_halftone_ref(page->eraseno);
    /* This is a last-ditch attempt to reserve the resources required for
       modular halftoning. If an MHT screen is used by the erase colour, and
       nothing else on the page, then update_erase() will be the first time
       the screen is marked used. The screen cache update calls ignore the
       return values of htm_ReserveResources() in them, so this will catch
       any failed resource allocations. */
    while ( mhtone != NULL ) {
      if ( !htm_ReserveResources(page, mhtone) )
        return FALSE;
      mhtone = htm_next_halftone_ref(page->eraseno, mhtone);
    }
    if ( gucr_interleavingStyle(page->hr) == GUCR_INTERLEAVINGSTYLE_PIXEL )
      return detail_error_handler(CONFIGURATIONERROR,
        "Can't do modular halftones pixel-interleaved.") ;
  }

  /* Warn user if imposed separations do not fit properly within the page. */
  if ( !check_colorant_positions(page) )
    return FALSE;

  if ( !gsc_report_uncalibrated_screens(gstateptr->colorInfo, page) )
    /* Unacceptable mis-match between job and calset screens - abort */
    return FALSE;

  /* Force an update before we start painting... */
  updateReadFileProgress();

  if ( !dlPrepareBanding(page) )
    return FALSE ;

  if ( !dlregion_mark(page) )
    return FALSE;

  /* Trapping preparation needs to be done in the front end of the rip.
   * Init_dl_render is about as late as it can be. */
  /** \todo rogerg: Trapping not supported for single pass rendering yet. */
  if ( page_data->paint_type == PAINT_TYPE_FINAL &&
       (page->regionMap == NULL ||
        page_data->transparency_strategy == 2) ) {
    /* Call trapPrepare after groups have been prepared and the region map has
       been created and marked.  If trapPrepare succeeds, then from here on tc
       must be disposed of properly before returning, otherwise there'll be a
       very considerable memory leak. Like all the trapping entry points called
       here, it's a cheap no-op if trapping isn't on.

       At this point, there is no surface handle, since we haven't yet got
       as far as calling the begin page of the surface set.
    */
    if ( !trapPrepare(context->pscontext, page, &page_data->trap_context,
                      NULL /*page_data->trap_complete_task*/) )
      return FALSE ;

    HQASSERT(isTrappingActive(page) || page_data->trap_context == NULL,
              "Got a trapping context but trapping not active");
  }

  /** \page render-handoff

     This is where the handoff from front-end to back-end happens. The handoff
     is dependent on the page being able to be rendered asynchronously. This
     can be done if:

     1) The operation that threw the page was a showpage; it's not useful for
        a partial paint, where we're trying to save memory and don't want to
        start building the next DL; it's not possible for an LL2 copypage,
        where we will continue to build on the same DL, so we need to complete
        rendering synchronously.
     2) The display list can be discarded in its entirety after the render.
        This is not possible if partial painting with a vignette candidate, or
        a showpage was issued whilst inside a nested HDL context (e.g., a
        form).
     3) The RIP is configured with multiple threads, and
     4) The pipeline depth is greater than 1.

     For now, we're deliberately not taking conditions #3 and #4 into account
     when preparing the page to handover, so we get maximum test coverage of
     new code paths. We may add back those tests at a later date, because the
     handoff process requires some allocation itself. A similar set of
     conditions control what the DL erase task set up in
     spawn_all_passes_of_page() actually does:

     1) If a full handoff to the render pipeline was performed, the contents
        of the DL pools are discarded, all referenced objects in the DL state
        are released or freed as appropriate.
     2) If a handoff couldn't be done because of a copypage, the DL is left
        as it is, with virtually no modification.
     3) If a handoff couldn't be done because of a partial paint with an active
        vignette candidate, the DL objects used by the vignette candidate are
        retained, and all other objects are discarded from the DL pools.
     4) If a handoff couldn't be done because of a partial paint without a
        vignette candidate, the contents of the DL pools are discarded, but
        the job object and any state required to construct the next page is
        retained.

     These possibilities are enumerated in the DL_ERASE_* values.

     If a full handoff can be done, it prepares the next slot in the
     dl_states array for the next display list to be build, by transferring
     the rasterstyle reference, acquiring job references, and copying other
     fields as appropriate. The interpreter context's page pointer and
     inputpage are then pointed at this slot. If the handoff fails, we leave
     the inputpage as it was, and the DL will be destroyed when we hit an
     erasepage.

     The burden of error handling from this point is higher, because we have
     to be careful about what happens to the next page. */
  if ( !dl_handoff_prepare(&nextpage, spawn_type) )
    return FALSE ;
#define return DO_NOT_return_SET_ok_INSTEAD!

  /** \todo ajcd 2011-03-24: If the color state moves to the page, move
      these calls to dl_handoff_prepare(). */
  /* Load any ICC profiles required for the back-end transforms. */
  if ( ok && page->colorState != NULL )
    ok = gsc_colorStateTransfer(page, frontEndColorState, page->colorState);

  /** \todo ajcd 2011-08-24: Before committing the handoff, provision
      the resource sources from the resource requests. This is a maxmin
      operation on the existing source w.r.t, pages in the pipeline, and the
      new page.

      \todo ajcd 2011-08-24: Should we wait for memory to become available
      if we cannot provision the sources?

      \todo ajcd 2011-08-24: Should some of this be done in the render tasks?
  */

  /* Create all the backdrops in one go, after all the groups are closed and
     group elimination is finalised. A non-null region map indicates
     compositing is required. This must be done after dl_handoff(), in case
     the rasterstyle is copied. */
  if ( ok && page->regionMap != NULL ) {
    page_data->pageState = bitGridGetAll(page->regionMap) ;
    PROBE(SW_TRACE_DL_PREPARE, page, ok = groupNewBackdrops(page));
  } else if ( page->backdropShared != NULL ) {
    bd_sharedFree(&page->backdropShared); /* Not needed, not compositing */
  }

  /* Close any Read/Write image store files so they can be re-opened readonly
   * during rendering. Do this last because it stops image purging. */
  ok = ok && im_shared_filecloseall(page->im_shared);

  /* The final handoff of the interpreter page to the next page. Do NOT put
     any code after this. */
  dl_handoff_commit(nextpage, spawn_type, ok) ;

#undef return
  return ok ;
}

static Bool init_render_pgbs(DL_STATE *page, page_data_t *page_data)
{
  VERIFY_OBJECT(page_data, PAGE_DATA_NAME) ;

  SET_PGB_PARAM_I(page, "CompressBands", page_data->compress_bands);
  SET_PGB_PARAM_B(page, "MSBLeft", AONE != 1);
  /* The width in bits into which the blit data is packed. Data is packed
     high bit first into each unit. */
  SET_PGB_PARAM_I(page, "PackingUnitBits", page->surfaces->packing_unit_bits) ;
  page_data->scratch_band = page->scratch_band_size != 0 ?
    task_resource_fix(TASK_RESOURCE_BAND_SCRATCH, 0, NULL)
    : NULL;
  return TRUE ;
}


static Bool init_partial_render(DL_STATE *page)
{
  SET_PGB_PARAM_B(page, "PrintPage", FALSE);

  /* The render omit flags will not have been set yet, so there is no point in
     checking them while positioning partial paint separations. */
  guc_positionSeparations(page->hr, FALSE) ;

  return TRUE ;
}

static Bool init_final_render(DL_STATE *page, page_data_t *page_data)
{
  GUCR_RASTERSTYLE *hr = page->hr ;

  VERIFY_OBJECT(page_data, PAGE_DATA_NAME) ;

  /* Check for invalid combinations of color jobs & output color mode. */
  if ( page_data->detect_separation &&
       !page_data->page_is_composite &&
       gucr_separationStyle(hr) == GUCR_SEPARATIONSTYLE_SEPARATIONS ) {
    DEVICESPACEID deviceSpace ;
    guc_deviceColorSpace(hr, &deviceSpace, NULL) ;
    if ( deviceSpace == DEVICESPACE_CMYK ||
         deviceSpace == DEVICESPACE_RGBK ) {
      if ( !guc_abortSeparations(hr, page_data->page_is_separations) ||
           !guc_omitSeparations(hr, page_data->page_is_separations) )
        return FALSE ;
    }
  }

  /* The DL walk for separation omission. */
  if ( !omit_blank_separations(page) )
    return FALSE ;

  /* An internal retained raster may contribute separations. */
  irr_omit_blank_separations(page);

  /* Transfer the separation omission marks into the rasterstyle, then
     prepare for the final render. We must omit separations before
     positioning and trimming of the page. */
  if ( !omit_black_transfer(page->omit_data) ||
       !guc_markBlankSeparations(hr, page->rippedtodisk) )
    return FALSE ;

  if ( ! DOING_RUNLENGTH(page) &&
       gucr_interleavingStyle(hr) == GUCR_INTERLEAVINGSTYLE_PIXEL ) {
    /* Check we have enough channels left for pixel interleaved output - note:
       nChannels is only assigned to sensible value when !DOING_RUNLENGTH */
    /** \todo ajcd 2011-02-14: This test is too general. It should use a
        surface property rather than DOING_RUNLENGTH() above to determine
        whether the surface supports extra channels beyond those marked as
        required by the page device configuration PostScript. The generic
        n-bit blitters support any number of channels. */
    GUCR_CHANNEL* hf = gucr_framesStart(hr);
    int32 nChannelsLeft = gucr_framesChannelsLeft(hf);
    if ( nChannelsLeft < gucr_framesChannelsTotal(hf) && nChannelsLeft > 0) {
      /* Would have rendered different number of channels than plugin expects - but note
         that we can successfully omit all of them, since then we omit the enitre page */
      return detail_error_handler(CONFIGURATIONERROR,
               "Too few channels for pixel interleaved after blank separation omission.");
    }
  }

  SET_PGB_PARAM_B(page, "PrintPage", TRUE);

  guc_positionSeparations(hr, TRUE) ;

  return TRUE ;
}

/** Inits rendering params for current page.
 */
static Bool init_page_render(DL_STATE *page, pass_data_t *pass)
{
  GUCR_CHANNEL* hf;
  int32 nMediaSheetsMax;
  page_data_t *page_data ;
  MODHTONE_REF *mhtone;

  VERIFY_OBJECT(pass, PASS_DATA_NAME) ;
  page_data = pass->page_data ;
  VERIFY_OBJECT(page_data, PAGE_DATA_NAME) ;

  /* Count number of media sheets to output and pass this count to the
     page buffer device as the number of separations. */
  nMediaSheetsMax = 0;

  for ( hf = gucr_framesStart(page->hr); gucr_framesMore(hf); gucr_framesNext(&hf) ) {
    if ( gucr_framesStartOfSheet(hf, NULL, NULL) ) {
      /* Found start of new sheet - bump count */
      nMediaSheetsMax++;
    }
  }

  SET_PGB_PARAM_I(page, "NumSeparations", nMediaSheetsMax);

  /* Now figure out writealloutput. */
  { /* check if ProcessEmptyBands overrides */
    mhtone = htm_first_halftone_ref(page->eraseno);
    for ( ; mhtone != NULL ;
          mhtone = htm_next_halftone_ref(page->eraseno, mhtone) )
      if ( htm_ProcessEmptyBands(mhtone) ) {
        pass->screen_all_bands = TRUE; break;
      }
  }
  if ( !DOING_RUNLENGTH(page) ) {
    if (!get_pagebuff_param(page, (uint8 *) "WriteAllOutput",
                             &pass->writealloutput, ParamBoolean, TRUE ))
      return FALSE;
    if ( pass->screen_all_bands )
      pass->writealloutput = TRUE;
  } else {
    SET_PGB_PARAM_B(page, "RunLineOutput",
                    (page->rle_flags & RLE_LINE_OUTPUT) != 0);
    pass->writealloutput = TRUE;
  }

  mhtone = htm_first_halftone_ref(page->eraseno);
  for ( ; mhtone != NULL ;
        mhtone = htm_next_halftone_ref(page->eraseno, mhtone) )
    if ( !mhtone->instance.implementation->reentrant
         || mhtone->instance.implementation->band_ordering
            != SW_HTM_BAND_ORDER_ANY ) {
      pass->serialize_mht = TRUE; break;
    }

  SET_PGB_PARAM_I(page, "JobNumber", page->job_number);


  return TRUE;
} /* Function init_page_render */


#define ENTRIES_IN_COLORANT_DICT 7

/** Setup the passed device parameter's names and types for PGB colorant
    specification; no actual values for the parameters are set here.
*/
static void setupDeviceParameterStructures(DEVICEPARAM* dictparam,
                                           DEVICEPARAM dictentry[ENTRIES_IN_COLORANT_DICT],
                                           DEVICEPARAM sRGBarray[3],
                                           DEVICEPARAM CMYKarray[4])
{
  static uint8 aszColorant[] = "Colorant";
  static uint8 aszChannel[] = "Channel";
  static uint8 aszColorantName[] = "ColorantName";
  static uint8 aszSpecialHandling[] = "SpecialHandling";
  static uint8 aszsRGB[] = "sRGB";
  static uint8 aszCMYK[] = "CMYK";
  static uint8 aszNeutralDensity[] = "NeutralDensity";

  setDevParamTypeAndName(*dictparam, ParamDict, aszColorant, sizeof(aszColorant) - 1);
  theDevParamDict(*dictparam) = dictentry;
  theDevParamDictLen(*dictparam) = ENTRIES_IN_COLORANT_DICT;

  setDevParamTypeAndName(dictentry[0], ParamInteger, aszColorant, sizeof(aszColorant) - 1);
  theDevParamInteger(dictentry[0]) = 0; /* replaced by nColorant on each iteration */
  theDevParamStringLen(dictentry[0]) = 0; /* for safety */

  setDevParamTypeAndName(dictentry[1], ParamString, aszColorantName, sizeof(aszColorantName) - 1);
  theDevParamString(dictentry[1]) = NULL; /* replaced by the colorant name */
  theDevParamStringLen(dictentry[1]) = 0; /* for now */

  setDevParamTypeAndName(dictentry[2], ParamInteger, aszChannel, sizeof(aszChannel) - 1);
  theDevParamInteger(dictentry[2]) = 0; /* replaced by nChannel on each iteration */
  theDevParamStringLen(dictentry[2]) = 0; /* for safety */

  setDevParamTypeAndName(dictentry[3], ParamArray, aszsRGB, sizeof(aszsRGB) - 1);
  theDevParamArrayLen(dictentry[3]) = 3;
  theDevParamArray(dictentry[3]) = sRGBarray ;

  setDevParamTypeAndName(dictentry[4], ParamInteger, aszSpecialHandling, sizeof(aszSpecialHandling) - 1);
  theDevParamInteger(dictentry[4]) = 0; /* replaced on each iteration */
  theDevParamStringLen(dictentry[4]) = 0; /* for safety */

  setDevParamTypeAndName(dictentry[5], ParamArray, aszCMYK, sizeof(aszCMYK) - 1);
  theDevParamArrayLen(dictentry[5]) = 4;
  theDevParamArray(dictentry[5]) = CMYKarray ;

  setDevParamTypeAndName(dictentry[6], ParamFloat, aszNeutralDensity, sizeof(aszNeutralDensity) - 1);
  theDevParamFloat(dictentry[6]) = 0; /* replaced on each iteration */
  theDevParamStringLen(dictentry[6]) = 0; /* for safety */


  setDevParamTypeAndName(sRGBarray[0], ParamFloat, NULL, 0);
  setDevParamTypeAndName(sRGBarray[1], ParamFloat, NULL, 0);
  setDevParamTypeAndName(sRGBarray[2], ParamFloat, NULL, 0);

  setDevParamTypeAndName(CMYKarray[0], ParamFloat, NULL, 0);
  setDevParamTypeAndName(CMYKarray[1], ParamFloat, NULL, 0);
  setDevParamTypeAndName(CMYKarray[2], ParamFloat, NULL, 0);
  setDevParamTypeAndName(CMYKarray[3], ParamFloat, NULL, 0);
}

/** Setup the passed device parameter's name to be 'GroupColorant'.
*/
static void setupDeviceParameterStructuresForGroupColorants(DEVICEPARAM* dictparam)
{
  static uint8 groupC[] = "GroupColorant";

  setDevParamTypeAndName(*dictparam, ParamDict, groupC, sizeof(groupC) - 1);
}

/** Setup a single colorant using the passed device parameter dictionaries. If
    'useBlack' is true, black will be used for the sRGB and CMYK alternates.

    NOTE: The passed DEVICEPARAM structures should have previously been setup
    with type information via a call to setupDeviceParameterStructures().
*/
static void setupPGBColorant(DEVICEPARAM dict[ENTRIES_IN_COLORANT_DICT],
                             DEVICEPARAM sRGBarray[3], DEVICEPARAM CMYKarray[4],
                             const GUCR_COLORANT_INFO *colorantInfo,
                             int32 colorant, int32 channel, Bool useBlack)
{
  theDevParamInteger(dict[0]) = colorant;
  theDevParamString(dict[1]) = theICList(colorantInfo->name);
  theDevParamStringLen(dict[1]) = theINLen(colorantInfo->name);
  theDevParamInteger(dict[2]) = channel;
  theDevParamInteger(dict[4]) = colorantInfo->specialHandling;
  theDevParamFloat(dict[6]) = colorantInfo->neutralDensity;

  if (useBlack) {
    theDevParamFloat(sRGBarray[0]) = 0.0f;
    theDevParamFloat(sRGBarray[1]) = 0.0f;
    theDevParamFloat(sRGBarray[2]) = 0.0f;
    theDevParamFloat(CMYKarray[0]) = 0.0f;
    theDevParamFloat(CMYKarray[1]) = 0.0f;
    theDevParamFloat(CMYKarray[2]) = 0.0f;
    theDevParamFloat(CMYKarray[3]) = 1.0f;
  } else {
    theDevParamFloat(sRGBarray[0]) = colorantInfo->sRGB[0];
    theDevParamFloat(sRGBarray[1]) = colorantInfo->sRGB[1];
    theDevParamFloat(sRGBarray[2]) = colorantInfo->sRGB[2];
    theDevParamFloat(CMYKarray[0]) = colorantInfo->CMYK[0];
    theDevParamFloat(CMYKarray[1]) = colorantInfo->CMYK[1];
    theDevParamFloat(CMYKarray[2]) = colorantInfo->CMYK[2];
    theDevParamFloat(CMYKarray[3]) = colorantInfo->CMYK[3];
  }
}

#define COLORANT_NAME_LENGTH_MAX 1024

/** Construct PGB header paramters, such as the list of active colorants and
    screening values, and pass them to the pgb device.
*/
static Bool set_pagebuffer_params(DL_STATE *page, sheet_data_t *sheet)
{
  uint8 colorName[COLORANT_NAME_LENGTH_MAX];
  USERVALUE colors[4];
  GUCR_RASTERSTYLE *hr = page->hr;
  GUCR_CHANNEL *hf ;
  uint32 nColorant = 0, nGroupColorant = 0;
  Bool doingRle = DOING_RUNLENGTH(page);
  pass_data_t *pass ;
  page_data_t *page_data ;

  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;
  pass = sheet->pass_data ;
  VERIFY_OBJECT(pass, PASS_DATA_NAME) ;
  page_data = pass->page_data ;
  VERIFY_OBJECT(page_data, PAGE_DATA_NAME) ;

  /* Pity we can't derive hr from hf. */
  hf = sheet->hf;

  SET_PGB_PARAM_I(page, "PageNumber",
                  CAST_UNSIGNED_TO_INT32(page->job->pages_output + 1));
  SET_PGB_PARAM_I(page, "Separation", sheet->nSheetNumber + 1) ;

  /* Tell the pagebuffer what the omission-invariant separation ID is. */
  (void)separation_set_id(page, guc_getSeparationId(sheet->hf)) ;

  /* Regardless of whether the TrimPage flag is set
     trim_would_start and trim_would_end always indicate where we would trim for
     the current page (including previous partial paints) if we were
     going to - this allows the TrimPage option for this page to be
     changed after the page has been rendered but before it is output
     for example in the THROUGHPUT system */
  SET_PGB_PARAM_B(page, "TrimPage", pass->trim_to_page);
  SET_PGB_PARAM_I(page, "TrimStart",
                  band_to_line(pass->trim_would_start, page));
  SET_PGB_PARAM_I(page, "TrimEnd",
                  band_to_line(pass->trim_would_end+1, page)-1);
#if 0
  {
    requirement_node_t *expr = requirement_node_find(page->render_resources,
                                                     REQUIREMENTS_ROOT) ;
    HQASSERT(expr != NULL, "No sheet group resources") ;
    SET_PGB_PARAM_I(page, "NumBands",
                    (int32)requirement_node_maximum(expr, TASK_RESOURCE_BAND_OUT));
  }
#else
    SET_PGB_PARAM_I(page, "NumBands", page->sizefactdisplaylist);
#endif

  {
    /* setup the colorant names and other information for the output (initially
    just names). Colorant is a dictionary parameter which is keyed by things
    like "ColorantName" and "Channel", as they appear in the PGB, plus an index
    so it can distinguish them; NumColorants tells it how many there are (so we
    won't be bothered by any left over from last time. The frame handle hf is
    positioned at the start of a sheet. We want to issue all the colorants of
    the sheet, i.e. up to the start of the next sheet or the end of the frames
    if there isn't one. */
    int32 nChannel = 0;
    Bool fNonZeroOffset = FALSE;
    Bool fSingleColorantPerSheet;
    Bool fPixelInterleaved;
    NAMECACHE * pColorantName;

    DEVICEPARAM dictparam;
    DEVICEPARAM dictentry[ENTRIES_IN_COLORANT_DICT];
    DEVICEPARAM sRGBarray[3];
    DEVICEPARAM CMYKarray[4];

    ColorantListIterator rleIterator = { 0 } ;
    Bool groupColorant = FALSE;
    const GUCR_COLORANT_INFO *rleColorantInfo =
      rle_sheet_colorants(sheet, sheet->surface_handle, &rleIterator, &groupColorant);

    /* Assert we're not trying to partial paint whilst producing transparent
       RLE - we can't currently support new device colorants being added after
       the list of group colorants. */
    HQASSERT(pass->paint_type != PAINT_TYPE_PARTIAL ||
             !DOING_TRANSPARENT_RLE(page),
             "partial painting is not supported when producing transparent RLE.");

    setupDeviceParameterStructures(&dictparam, dictentry, sRGBarray, CMYKarray);

    fSingleColorantPerSheet = FALSE; /* until we hear otherwise */
    fNonZeroOffset = FALSE; /* until we hear otherwise */
    pColorantName = NULL;

    fPixelInterleaved = (gucr_interleavingStyle(hr) == GUCR_INTERLEAVINGSTYLE_PIXEL);

    do {
      GUCR_COLORANT* hc;

      for (hc = gucr_colorantsStart(hf);
           gucr_colorantsMore(hc, GUCR_INCLUDING_PIXEL_INTERLEAVED);
           gucr_colorantsNext(&hc)) {
        const GUCR_COLORANT_INFO *colorantInfo;

        if ( gucr_colorantDescription(hc, &colorantInfo) ) {
          HQASSERT(!groupColorant, "unexpected group colorant.");

          /* Are there any colorants before this one in the mapping list that
             have been omitted this time? If so we need to output them, but
             marked as being omitted (by giving them a channel index of -1). */
          while ( rleColorantInfo != NULL ) {
            const GUCR_COLORANT_INFO *thisColorantInfo = rleColorantInfo;

            rleColorantInfo = colorantListGetNext(&rleIterator, &groupColorant);

            /* If we matched the colorant, we've already advanced the iterator
               and so are ready for the next colorant loop. Otherwise, we'll
               output an omitted colorant info. */
            if ( thisColorantInfo->name == colorantInfo->name )
              break ;

            setupPGBColorant(dictentry, sRGBarray, CMYKarray, thisColorantInfo,
                             nColorant, -1, (thisColorantInfo->sRGB[0] < 0.0f ||
                                             page_data->multiple_separations));
            nColorant ++;

            if ( !send_pagebuff_param(page, &dictparam))
              return FALSE;

            if (groupColorant) {
              /* We shouldn't encounter a group colorant here; when ripping a
                 transparent job all colorants should have been present in the
                 partial render, which would have populated the RLE colorant
                 list; therefore the final render should only contain a subset
                 of the colorants in the RLE colorant list. */
              HQFAIL("unexpected group colorant.");
              rleColorantInfo = NULL;
            }
          }

          setupPGBColorant(dictentry, sRGBarray, CMYKarray, colorantInfo,
                           nColorant, doingRle ? nColorant : nChannel,
                           /* Unknown sRGB or more than one so we don't know
                              what the correct color is: set to black */
                           (colorantInfo->sRGB[0] < 0.0f ||
                            page_data->multiple_separations));

          if ( !send_pagebuff_param(page, &dictparam))
            return FALSE;

          if (page->output_object_map && nColorant == 0) {
            GUCR_COLORANT_INFO pseudoInfo = *colorantInfo;
            COLORSPACE_ID colorSpaceId;
            USERVALUE rgb, cmy, k;

            HQASSERT(!doingRle, "Object maps don't make sense for RLE");
            HQASSERT(!fPixelInterleaved, "No object maps for pixel-interleaved");
            nColorant++; nChannel++;

            /* Modify this colorantInfo into an object map */
            pseudoInfo.name = &system_names[NAME_ObjectMap];
            pseudoInfo.originalName = pseudoInfo.name;
            pseudoInfo.specialHandling = SPECIALHANDLING_OBJECTMAP;
            guc_deviceToColorSpaceId(hr, &colorSpaceId);
            if (colorSpaceId == SPACE_DeviceRGB) {
              rgb = 1.0f; cmy = 0.0f; k = 0.0f; /* use white on RGB */
            } else {
              rgb = 0.0f; cmy = 0.0f; k = 1.0f;
            }
            pseudoInfo.sRGB[0] = pseudoInfo.sRGB[1] = pseudoInfo.sRGB[2] = rgb;
            pseudoInfo.CMYK[0] = pseudoInfo.CMYK[1] = pseudoInfo.CMYK[2] = cmy;
            pseudoInfo.CMYK[3] = k;
            setupPGBColorant(dictentry, sRGBarray, CMYKarray, &pseudoInfo,
                             nColorant, nChannel, FALSE);
            if ( !send_pagebuff_param(page, &dictparam))
              return FALSE;
          }

          fNonZeroOffset = (fNonZeroOffset ||
                            colorantInfo->offsetX != 0 ||
                            colorantInfo->offsetY != 0);
          fSingleColorantPerSheet = pColorantName == NULL ||
            (fSingleColorantPerSheet && pColorantName == colorantInfo->name);
          pColorantName = colorantInfo->name;

          colors[0] = colorantInfo->CMYK[0];
          colors[1] = colorantInfo->CMYK[1];
          colors[2] = colorantInfo->CMYK[2];
          colors[3] = colorantInfo->CMYK[3];
          nColorant++;
        } /* if gucr_colorantDescription */

        if (gucr_colorantsBandChannelAdvance(hc) || fPixelInterleaved)
          nChannel++;
      } /* for colorants */

      if (! fPixelInterleaved)
        nChannel++;

      gucr_framesNext(&hf);
    } while (gucr_framesMore(hf) && !gucr_framesStartOfSheet(hf, NULL, NULL)) ;

    /* Output any remaining colorants that have been omitted. */
    while (rleColorantInfo != NULL && !groupColorant) {
      setupPGBColorant(dictentry, sRGBarray, CMYKarray, rleColorantInfo,
                       nColorant, -1, (rleColorantInfo->sRGB[0] < 0.0f ||
                                       page_data->multiple_separations));
      nColorant ++;

      if ( !send_pagebuff_param(page, &dictparam))
        return FALSE;

      rleColorantInfo = colorantListGetNext(&rleIterator, &groupColorant);
    }

    SET_PGB_PARAM_I(page, "NumColorants", nColorant);
    SET_PGB_PARAM_I(page, "NumChannels", doingRle ? nColorant : nChannel);

    if (rleColorantInfo != NULL) {
      setupDeviceParameterStructuresForGroupColorants(&dictparam) ;

      do {
        HQASSERT(groupColorant, "Not handling group colorants") ;
        HQASSERT(rleColorantInfo->sRGB[0] >= 0.0f, "colorant should be known.");
        setupPGBColorant(dictentry, sRGBarray, CMYKarray, rleColorantInfo,
                         nGroupColorant, nColorant, FALSE);
        if ( !send_pagebuff_param(page, &dictparam))
          return FALSE;

        nColorant ++;
        nGroupColorant ++;
        rleColorantInfo = colorantListGetNext(&rleIterator, &groupColorant);
      } while (rleColorantInfo != NULL) ;
    }

    /* Always set the group colorant count; this will be zero if we're not
       outputting transparent RLE. */
    SET_PGB_PARAM_I(page, "NumGroupColorants", nGroupColorant);

    if ( doingRle ) {
      /* Export screens needed for RLE after colorants have been numbered. */

      /** \todo ajcd 2010-12-11: Is this the right place for this?
          ht_export_screens loops over colorants of the frame. It doesn't
          mention anything about the sheet. This function is only called
          for the first frame in the sheet. */
      sheet->fScreensExported = ht_export_screens(page, hr, sheet->hf,
                                                  rle_sheet_colorantmap(sheet, sheet->surface_handle),
                                                  page_data->use_screen_cache_name,
                                                  page_data->screen_dot_centered);
      if ( !sheet->fScreensExported ) {
        /* Failed to set new screens - abort */
        return FALSE;
      }
    }

    /* Determine colorName in a simple fashion:

       Use the colorant name if we find only one unique colorant name.
       Otherwise, if we're imposing separations then use "Separations-n" where
       n is the sheet number.
       Otherwise, use "Composite".

       Note that for a monochrome page this gives us "Gray" and for a
       preseparated page that gets intercepted this gives us the color of
       that preseparated page, for example "Cyan". */
    if (gucr_separationStyle(hr) == GUCR_SEPARATIONSTYLE_PROGRESSIVES) {
      swcopyf(colorName, (uint8*)"Progressive-%d", sheet->nSheetNumber);
    } else if (fSingleColorantPerSheet && ! page_data->multiple_separations) {
      HQASSERT(pColorantName != NULL, "single colorant per sheet, but we dont know its name");
      HQASSERT(theINLen(pColorantName) < COLORANT_NAME_LENGTH_MAX - 1,
               "colorant name too long.");

      swcopyf(colorName, (uint8*)"%.*s",
              theINLen(pColorantName), theICList(pColorantName));
    } else if (page_data->multiple_separations || fNonZeroOffset) {
      swcopyf(colorName, (uint8*)"Separations-%d", sheet->nSheetNumber);
    } else {
      swcopyf(colorName, (uint8*)"Composite");
    }
    SET_PGB_PARAM_S(page, "ColorName", colorName, strlen_int32((char *) colorName));

    /* Determine colors. */
    if (gucr_interleavingStyle(hr) != GUCR_INTERLEAVINGSTYLE_MONO) {
      /* Then colors are actually the order of the output colors. */
      int32 iChannel;
      for (iChannel = 0; iChannel < 4; ++iChannel)
        colors[iChannel] = 0.0f;

      for (iChannel = 0; iChannel < 4; ++iChannel) {
        int32 nChannel = gucr_getChannelAssignment(hr, iChannel);
        HQASSERT(nChannel < 4 , "channel is >= 4 from gucr_getChannelAssignment");
        if (nChannel >= 0) /* it may be -1 if there are no more */
          colors[nChannel] = (USERVALUE)(1 + iChannel);
      }
    } else {
      /* If we've got multiple imposed colorants, or multiple imposed seps
       * then simply mark it as black aka [0, 0, 0, 1].
       */
      if (! fSingleColorantPerSheet || page_data->multiple_separations ||
          colors[0] < 0.0f || colors[1] < 0.0f ||
          colors[2] < 0.0f || colors[3] < 0.0f) {
        colors[0] = colors[1] = colors[2] = 0.0f;
        colors[3] = 1.0f;
      }
    }
  }

  SET_PGB_PARAM_F_varname(page, primary_colors[0], colors[0]);
  SET_PGB_PARAM_F_varname(page, primary_colors[1], colors[1]);
  SET_PGB_PARAM_F_varname(page, primary_colors[2], colors[2]);
  SET_PGB_PARAM_F_varname(page, primary_colors[3], colors[3]);

  return TRUE;
}


static Bool check_pagebuffer_device(DL_STATE *page, sheet_data_t *sheet)
{
  pass_data_t *pass ;
  page_data_t *page_data ;
  int32 nthreads ;

  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;
  pass = sheet->pass_data ;
  VERIFY_OBJECT(pass, PASS_DATA_NAME) ;
  page_data = pass->page_data ;
  VERIFY_OBJECT(page_data, PAGE_DATA_NAME) ;

  nthreads = pass->allowMultipleThreads ? max_simultaneous_tasks() : 1 ;
  if ( nthreads > page->sizedisplaylist
       && band_threads_warned_job != page->job_number ) {
    monitorf(UVM("Warning: Bands are so high (band %d : page %d) that "
                 "there aren't enough bands for every rendering thread.\n"),
             page->band_lines, page->page_h);
    band_threads_warned_job = page->job_number ;
  }

  /* Call the pagebuffer device to allow the consumer to allocate bands
     itself. */
  return call_pagebuffer_raster_requirements(
           page->pgbdev, TRUE, page, page->hr,
           nthreads, page->scratch_band_size, page_data->scratch_band);
}

/*--------------------------------------------------------------------------*/
/** Render the objects in a band.
 *
 * \param p_rs Render state
 * \param dl_bandnum This is the index of the band on the display list from
 *                   which objects will be read. NOTE: This index should be an
 *                   absolute band index; the current band scaling factor will
 *                   be applied by this function.
 */
static Bool render_objects_of_band(render_state_t* p_rs, int32 dl_bandnum)
{
  DLREF* dlobj;
  render_info_t *p_ri = &p_rs->ri ;
  Bool result = TRUE ;
  DL_STATE *page = p_rs->page ;

  /* no clipping for the first (erase) object, but initialise DL state */
  RESET_BLITS(p_ri->rb.blits, &invalid_slice, &invalid_slice, &invalid_slice) ;

  /* Convert the band index into a factored band index. */
  dl_bandnum /= page->sizedisplayfact;

  /* Skip the erase object */
  dlobj = dl_get_head(page, dl_bandnum);
  HQASSERT(dlobj != NULL, "No erase in this band");
  HQASSERT(dlref_lobj(dlobj)->opcode == RENDER_erase,
           "Expected erase DL object");
  dlobj = dlref_next(dlobj);

  if (dlobj != NULL) {
    /** \todo @@@ TODO FIXME ajcd 2002-12-30: We'll call the HDL render
       function on the base HDL for the moment. To accommodate partial
       painting, we should actually be calling a recursive rendering function
       to render each open HDL (depth-first). */
    HDL *hdl = dlPageHDL(page) ;

    p_rs->band = dl_bandnum;
    p_ri->clip = p_ri->bounds = p_rs->cs.bandlimits;

    if ( !clip_context_begin(p_ri) )
      return FALSE ;
#define return DO_NOT_return!

    /* Render the top-level HDL or group, with a parent pattern of NULL. */
    result = hdlRender(hdl, p_ri, NULL, FALSE /* self-intersecting */);

#if defined(DEBUG_BUILD)
    if ( (debug_render & DEBUG_RENDER_SHOW_BANDS) != 0 )
      render_band_debug_marks(p_rs, dl_bandnum);
#endif

    clip_context_end(p_ri) ;
#undef return
  }
  return result;
} /* Function render_objects_of_band */


/** Render objects between start_lobj and end_lobj inclusive on the
   final (z-order) band, to record their shapes or colors.
*/
Bool render_objects_of_z_order_band(
  /*@notnull@*/ /*@in@*/        DLRANGE *dlrange,
  /*@notnull@*/ /*@in@*/        render_state_t *p_rs,
                                dcoord ystart,
                                dcoord yend )
{
  Bool result = TRUE;
  int32 y1c ;
  render_state_t rs ;
  ht_params_t ht_params = { 0 };

  HQASSERT(p_rs, "No render state for Z order band") ;
  RS_COPY_FROM_RS(&rs, p_rs) ;

  RESET_BLITS(rs.ri.rb.blits, &invalid_slice, &invalid_slice, &invalid_slice) ;

  /* Call recursive renderer, with recursion counter of 0, parent
     pattern of NULL and current DL. */

  y1c = ystart - ( ystart % rs.forms->retainedform.rh );

  while ( result && ( y1c <= yend )) {
    render_tracker_t renderTracker ;

    render_tracker_init(&renderTracker) ;

    /* Initialise the DL clipping state. */
    FORM_UPDATE( theFormHOff, rs.forms, y1c, y1c, y1c );

    rs.cs.bandlimits.x1 = 0 ;
    rs.cs.bandlimits.y1 = max( rs.forms->retainedform.hoff , ystart );
    rs.cs.bandlimits.x2 = rs.forms->retainedform.w - 1;
    rs.cs.bandlimits.y2 = min( yend , ( rs.forms->retainedform.hoff +
                                        rs.forms->retainedform.rh - 1 ));
    rs.cs.renderTracker = &renderTracker ;

    rs.ri.clip = rs.ri.bounds = rs.cs.bandlimits ;
    rs.ri.ht_params = &ht_params;

    if ( !clip_context_begin(&rs.ri) )
      return FALSE ;
#define return DO_NOT_return!

    if ( rs.ri.surface->assign != NULL )
      result = (*rs.ri.surface->assign)(rs.surface_handle, &rs) ;

    if ( result )
      result = render_object_list_of_band(&rs.ri, dlrange);

    clip_context_end(&rs.ri) ;
#undef return

    y1c += rs.forms->retainedform.rh;
  }

  FORM_UPDATE( theFormHOff, rs.forms, 0, 0, 0 );

  rs.cs.renderTracker = NULL ; /* For safety, to catch calling chain errors */
  rs.ri.ht_params = NULL;

  return result ;
} /* Function render_objects_of_z_order_band */



/** Condvar for waiting for asynchronous screening. */
static multi_condvar_t mht_condvar;


/** Mutex for asynchronous screening state. */
static multi_mutex_t mht_mutex;


/** Subclass of sw_htm_dohalftone_request, used via a downcast to save
   results from DoneHalftone callback. */
typedef struct {
  sw_htm_dohalftone_request super ; /* Superclass MUST be first entry. */
  Bool synchronous; /* for avoiding the synchronisation when possible */
  volatile Bool done ; /* volatile so the two tests below will be distinct */
  sw_htm_result result ;
} rip_dohalftone_request ;


static void RIPCALL done_halftone_callback(
         const sw_htm_dohalftone_request *request,
         sw_htm_result result )
{
  /* Downcast from const superclass. */
  rip_dohalftone_request *rc = (rip_dohalftone_request *)request;

  rc->result = result;
  rc->done = TRUE;
  HqMemoryBarrierStoreLoad();
  /* If rc->synchronous is TRUE, this is either within the call to DoHalftone,
     or on another thread before rc->synchronous is reset by the renderer
     thread, so before it tests rc->done. */
  if ( !rc->synchronous ) {
    multi_mutex_lock(&mht_mutex);
    /* As setting rc->done is outside the lock, the broadcast could go to waste,
       but that's harmless. It's outside, because it must go before testing
       rc->synchronous. */
    multi_condvar_broadcast(&mht_condvar);
    multi_mutex_unlock(&mht_mutex);
  }
}


/** render_objects_of_band with modular halftones */
static Bool render_objs_of_band_mod_ht(render_state_t *p_rs,
                                       band_data_t *band, int32 dl_bandnum)
{
  Bool fOk;
  Bool dummy_ripped_something, dummy_do_modular_erase;
  Bool white_on_white;
  MODHTONE_REF *mhtone;
  rip_dohalftone_request request ;
  sw_htm_raster_ptr srcs[1], dsts[1];
  render_state_t rs;
  render_tracker_t render_tracker;
  ht_params_t ht_params = { 0 };
  MODHTONE_REF *the_only_mhtone;
  Bool non_omitted_band = FALSE; /* to pacify compiler */
  blit_color_t erase_color ;
  blit_color_t knockout_color ;
  const surface_t *surface ;
  COLORANTINDEX ci;
  frame_data_t *frame ;
  sheet_data_t *sheet ;

  VERIFY_OBJECT(band, BAND_DATA_NAME) ;
  frame = band->frame_data ;
  VERIFY_OBJECT(frame, FRAME_DATA_NAME) ;
  sheet = frame->sheet_data ;
  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;

  HQASSERT(!p_rs->fPixelInterleaved, "Can't do pixel-interleaved mod halftones");
  HQASSERT(p_rs->htm_info != NULL, "Should be using modular halftones") ;
  HQASSERT(p_rs->cs.selected_mht == NULL && !p_rs->cs.selected_mht_is_erase,
           "Modular halftone should not be selected yet") ;

  surface = p_rs->ri.surface ;
  HQASSERT(surface != NULL, "No output surface") ;
  ci = blit_map_sole_index(p_rs->cs.blitmap);

  /* render in-RIP halftones first */
  if ( ht_anyInRIPHalftonesUsed(p_rs->page->eraseno) ) {
    HQASSERT(p_rs->cs.fIsHalftone && surface->screened,
             "In-RIP halftone pass should be marked as halftoned") ;
    if ( !render_objects_of_band(p_rs, dl_bandnum) )
      return FALSE ;
  }

  RS_COPY_FROM_RS(&rs, p_rs) ;
  rs.cs.erase_color = &erase_color ;
  rs.cs.knockout_color = &knockout_color ;
  rs.cs.p_white_on_white = &white_on_white;
  /* do contone band, switching temporarily to contone */
  if ( rs.htm_info->src_bit_depth == 8 ) {
    rs.ri.surface = surface_find(rs.page->surfaces, SURFACE_MHT_CONTONE_FF) ;
  } else {
    rs.ri.surface = surface_find(rs.page->surfaces, SURFACE_MHT_CONTONE_FF00);
  }

  if ( rs.ri.surface == NULL )
    return detail_error_handler(UNREGISTERED,
                                "Modular halftoning surface not present in this build") ;

  rs.cs.fIsHalftone = rs.ri.surface->screened ;
  rs.ri.generate_object_map = (rs.page->reserved_bands & RESERVED_BAND_MODULAR_MAP) != 0 ;

  /* MUST destroy blitmap if this succeeds. */
  fOk = blit_colormap_create(rs.page, &rs.cs.blitmap, mm_pool_temp,
                             rs.ri.surface, rs.cs.hc,
                             /* Override with colour depth. */
                             1 << rs.htm_info->src_bit_depth,
                             rs.ri.generate_object_map,
                             FALSE, /* Don't force masking */
                             FALSE, /* Not compositing */
                             FALSE /* Don't force pixel interleaved. */) ;
  if ( fOk ) {
    render_tracker_init(&render_tracker);
    rs.cs.renderTracker = &render_tracker;
    /* ht_params are not used in contone, so ignore */
    rs.ri.rb.outputform = &rs.forms->contoneform;

    if ( rs.ri.surface->assign != NULL )
      fOk = (*rs.ri.surface->assign)(rs.surface_handle, &rs) ;

    if ( fOk ) {
      render_erase_prepare(&rs, frameandline_to_band(band->bbox.y1, rs.page)) ;
      fOk = render_erase_of_band(&rs, &dummy_ripped_something,
                                 &dummy_do_modular_erase);
      if ( fOk ) {
        /* Render objs if render_colorants_of_band() would for non-modular */
        non_omitted_band =
          (dl_bandnum >= 0 && dl_bandnum < rs.page->sizedisplaylist)
          && (rs.cs.bandlimits.y1 < rs.forms->retainedform.h
              && rs.cs.bandlimits.y2 >= 0);
        if ( non_omitted_band )
          fOk = render_objects_of_band(&rs, dl_bandnum);
      }
    }
    blit_colormap_destroy(&rs.cs.blitmap, mm_pool_temp) ;
  }
  if ( !fOk ) /* Blitmap or render failed */
    return FALSE ;

  /* Ignore white-on-white here, because modules can still paint something,
   * and might have to be called for empty bands. */

  /* prefill DoHalftone request */
  request.super.render_info = rs.htm_info;
  request.super.DoneHalftone = done_halftone_callback;
  request.super.separation_number = sheet->nSheetNumber + 1;
  request.super.band_num = frameandline_to_band(band->bbox.y1, rs.page);
  request.super.first_line_y =
    rs.forms->contoneform.hoff + rs.ri.rb.y_sep_position;
  request.super.num_lines = rs.cs.bandlimits.y2 - rs.cs.bandlimits.y1 + 1;
  HQASSERT(rs.cs.bandlimits.y2 >= rs.cs.bandlimits.y1, "No lines to screen");
  request.super.channel_id = rs.nChannel;
  request.super.msk_bitmap =
    (sw_htm_raster_ptr)rs.forms->htmaskform.addr;
  request.super.object_props_map =
    (sw_htm_raster_ptr)rs.forms->objectmapform.addr;
  request.super.num_src_channels = 1; request.super.src_channels = srcs;
  srcs[0] = (sw_htm_raster_ptr)rs.forms->contoneform.addr;
  request.super.num_dst_channels = 1; request.super.dst_channels = dsts;
  dsts[0] = (sw_htm_raster_ptr)rs.forms->retainedform.addr;
  request.super.thread_index = SwThreadIndex();
  HQASSERT(request.super.thread_index < rs.htm_info->max_render_threads,
           "Thread numbering not within modular screening spec");

  /* set up for mask rendering */
  rs.ri.surface = surface_find(rs.page->surfaces, SURFACE_MHT_MASK) ;
  HQASSERT(rs.ri.surface != NULL, "No modular halftone mask") ;
  rs.cs.fIsHalftone = rs.ri.surface->screened ;
  rs.ri.generate_object_map = FALSE;
  rs.ri.ht_params = &ht_params;
  rs.ri.rb.outputform = &rs.forms->htmaskform;

  /* check if there's only one ht for this ci */
  HQASSERT(p_rs->cs.blitmap->nrendered == 1,
           "Should only have a single channel rendered") ;
  if ( non_omitted_band ) {
    the_only_mhtone = NULL;
    if ( !ht_anyInRIPHalftonesUsed(p_rs->page->eraseno) ) {
      for ( mhtone = htm_first_halftone_ref(rs.page->eraseno) ;
            mhtone != NULL ;
            mhtone = htm_next_halftone_ref(rs.page->eraseno, mhtone) )
        if ( ht_modularHalftoneUsedForColorant(mhtone, rs.page->eraseno, ci) ) {
          if ( the_only_mhtone != NULL ) {
            the_only_mhtone = NULL; break;
          } else
            the_only_mhtone = mhtone;
        }
    }
  } else { /* on an omitted band, there's only the erase to do */
    HQASSERT(ci != COLORANTINDEX_UNKNOWN,
             "Trying to modular erase a blank channel");
    the_only_mhtone =
      ht_getModularHalftoneRef(rs.lobjErase->objectstate->spotno,
                               REPRO_TYPE_OTHER, ci);
    HQASSERT(rs.lobjErase->dldata.erase.newpage,
             "Asked to modular erase a partial-painted band");
  }

  for ( mhtone = htm_first_halftone_ref(rs.page->eraseno) ;
        mhtone != NULL ;
        mhtone = htm_next_halftone_ref(rs.page->eraseno, mhtone) )
    if ( ht_modularHalftoneUsedForColorant(mhtone, rs.page->eraseno, ci) ) {
      /* generate mask */
      if ( mhtone == the_only_mhtone && rs.lobjErase->dldata.erase.newpage ) {
        /* all pixels must be handled by this ht, except after p.p. */
        white_on_white = FALSE;
        area1fill(&rs.forms->htmaskform);
        request.super.msk_hint = SW_HTM_MASKHINT_ALL_ON;
      } else {
        HQASSERT(non_omitted_band, "Somehow rendering on omitted band");
        render_tracker_init(&render_tracker);
        ht_params.spotno = SPOT_NO_INVALID;
        rs.cs.selected_mht = mhtone;

        /* MUST destroy blitmap if this succeeds. */
        fOk = blit_colormap_create(rs.page, &rs.cs.blitmap, mm_pool_temp, rs.ri.surface,
                                   rs.cs.hc,
                                   2, /* Override color to 0/1. */
                                   FALSE, /* type */
                                   TRUE, /* force masking */
                                   FALSE, /* Not compositing */
                                   FALSE /* don't force pixel interleaved. */) ;
        if ( fOk ) {
          if ( rs.ri.surface->assign != NULL )
            fOk = (*rs.ri.surface->assign)(rs.surface_handle, &rs) ;

          if ( fOk ) {
            render_erase_prepare(&rs, frameandline_to_band(band->bbox.y1, rs.page)) ;
            fOk = (render_erase_of_band(&rs, &dummy_ripped_something,
                                        &dummy_do_modular_erase)
                   && render_objects_of_band(&rs, dl_bandnum));
          }

          blit_colormap_destroy(&rs.cs.blitmap, mm_pool_temp) ;
        }
        if ( !fOk ) /* Blitmap or render failed */
          return FALSE ;

        request.super.msk_hint = white_on_white ? SW_HTM_MASKHINT_ALL_OFF
                                          : SW_HTM_MASKHINT_NORMAL;
      }
      if ( !white_on_white || htm_ProcessEmptyBands(mhtone) ) {
        /* render modular halftone */
        request.synchronous = TRUE; request.done = FALSE;
        if ( !htm_DoHalftone(mhtone, &request.super) )
          /* Doesn't return an error code, guess misconfigured. */
          return error_handler(CONFIGURATIONERROR);
        request.synchronous = FALSE;
        if ( !request.done ) { /* wait for async module to finish */
          multi_mutex_lock(&mht_mutex);
          while ( !request.done ) {
            multi_condvar_wait(&mht_condvar);
            if ( !interrupts_clear(allow_interrupt) ) {
              multi_mutex_unlock(&mht_mutex);
              htm_AbortHalftone(mhtone, &request.super);
              /* If the module cannot abort, there's nothing we can do. */
              if ( !request.done )
                monitorf((uint8 *)UVS("%%%%[ Error: Couldn't abort screening module: The RIP could be unstable! ]%%%%\n"));
              return report_interrupt(allow_interrupt);
            }
          }
          multi_mutex_unlock(&mht_mutex);
        } else
          request.synchronous = FALSE;
        /* Propagate white_on_white to the original render state. */
        *p_rs->cs.p_white_on_white &= white_on_white;
        if ( request.result != SW_HTM_SUCCESS )
          return error_handler(htm_MapHtmResultToSwError(request.result));
      }
    } /* if instance used for this colorant */
  /* end loop over modular halftones */
  return TRUE;
} /* function render_objs_of_band_mod_ht */


/*
 * Function:  render_colorants_of_band
 */

static Bool render_colorants_of_band(render_base_t *p_base, band_data_t *band,
                                     Bool writealloutput)
{
  Bool fOk = TRUE ;
  render_state_t *p_rs = &p_base->rs;
  FORM *retainedform = &p_rs->forms->retainedform ;
  band_t *pband ;
  DL_STATE *page = p_rs->page ;

  VERIFY_OBJECT(band, BAND_DATA_NAME) ;
  pband = band->p_band ;
  HQASSERT(pband, "No band table entry") ;

  pband->fDontOutput = !writealloutput; /* nothing on the band yet */

  if ( retainedform->h != 0 ) {
    blit_packed_t overprint_mask ;
    blit_packed_t maxblit_mask ;
    blit_color_t erase_color ;
    blit_color_t knockout_color ;
    blit_chain_t render_blits = { 0 } ;
    int32 dl_bandnum;
    int32 band_offset;
    /* First time through will need an erase */
    Bool fNeedErase = TRUE;
    Bool skip_om = FALSE;
    /* Remember original form address in case map to last band of page */
    blit_t *pFormAddressOrig = retainedform->addr;
    const surface_set_t *surfaces = page->surfaces ;
    const surface_t *surface = surface_find(surfaces, SURFACE_OUTPUT) ;
    frame_data_t *frame ;
    sheet_data_t *sheet ;

    frame = band->frame_data ;
    VERIFY_OBJECT(frame, FRAME_DATA_NAME) ;
    sheet = frame->sheet_data ;
    VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;

    /* First time, generate object map, if needed for output */
    p_rs->ri.generate_object_map = page->output_object_map;

    p_rs->cs.overprint_mask = &overprint_mask ;
    p_rs->cs.maxblit_mask = &maxblit_mask ;

    /* Initialise colormap with NULL, so we can detect early exit */
    p_rs->cs.blitmap = NULL ;
    p_rs->cs.erase_color = &erase_color ;
    p_rs->cs.knockout_color = &knockout_color ;
    /* Reset the halftone flag, because it can be modified by
       render_objs_of_band_mod_ht(). */
    p_rs->cs.fIsHalftone = surface->screened;

    p_rs->ri.rb.blits = &render_blits ;
    RESET_BLITS(p_rs->ri.rb.blits, &invalid_slice, &invalid_slice, &invalid_slice) ;

#define return DO_NOT_RETURN_goto_rendered_colorant_INSTEAD!

    /* Loop over colorants for frame */
    for ( p_rs->cs.hc = gucr_colorantsStart(p_rs->hf);
          gucr_colorantsMore(p_rs->cs.hc, !GUCR_INCLUDING_PIXEL_INTERLEAVED);
          gucr_colorantsNext(&p_rs->cs.hc) ) {
      const GUCR_COLORANT_INFO *colorantInfo;
      render_tracker_t renderTracker ;
      ht_params_t ht_params = { 0 };
      Bool white_on_white = TRUE;
      Bool do_modular_erase = FALSE;

      SwOftenSafe();

      if ( !interrupts_clear(allow_interrupt) ) {
        /* Got an interrupt - abort rendering */
        fOk = report_interrupt(allow_interrupt) ;
        break;
      }

      fOk = blit_colormap_create(page, &p_rs->cs.blitmap, mm_pool_temp,
                                 surface, p_rs->cs.hc,
                                 0, /* Do not override depth */
                                 p_rs->ri.generate_object_map /* type */
                                 || (DOING_RUNLENGTH(page)
                                     && (page->rle_flags & RLE_OBJECT_TYPE)),
                                 FALSE, /* Don't force masking */
                                 FALSE, /* Not compositing */
                                 FALSE /* don't force pixel interleaved.*/) ;

      if ( !fOk )
        break ;

      /* Get colorant index, position and band info. Note that the colorant
         may be non-rendered, but required because of pixel interleaving. We
         still want to get the remaining information for it. We MUST use the
         index set by gucr_colorantsContone above, rather than the one in
         the colorantInfo setup here. */
      (void)gucr_colorantDescription(p_rs->cs.hc, &colorantInfo) ;
      p_rs->cs.bg_separation = colorantInfo->fBackground ;
      p_rs->ri.rb.x_sep_position = colorantInfo->offsetX ;
      p_rs->ri.rb.y_sep_position = colorantInfo->offsetY ;
      band_offset = colorantInfo->offsetBand ;

      /* Clear tracking variables for each band. */
      render_tracker_init(&renderTracker) ;
      p_rs->cs.renderTracker = &renderTracker ;
      p_rs->cs.p_white_on_white = &white_on_white;
      p_rs->cs.fSelfIntersect = FALSE ;
      p_rs->cs.selected_mht = NULL ;
      p_rs->cs.selected_mht_is_erase = FALSE ;

      ht_params.spotno = SPOT_NO_INVALID;
      p_rs->ri.ht_params = &ht_params;

      { /* Set up band limits, incorporating Y separation offset. */
        dcoord y1 = retainedform->hoff + colorantInfo->offsetY ;
        dcoord y2 = y1 + retainedform->rh - 1 ;

        if ( y1 < 0 )
          y1 = 0 ;

        if ( y2 >= retainedform->h )
          y2 = retainedform->h - 1 ;

        bbox_store(&p_rs->cs.bandlimits, band->bbox.x1, y1, band->bbox.x2, y2);
      }

      p_rs->ri.bounds = p_rs->ri.clip = p_rs->cs.bandlimits ;
      fOk = clip_context_begin(&p_rs->ri) ;
      if ( !fOk )
        break ;

      if ( surface->assign != NULL ) {
        fOk = (*surface->assign)(p_rs->surface_handle, p_rs) ;
        if ( !fOk )
          break ;
      }

      /* Calculate actual band number for colorant rendering. 'band_offset' is
         a whole number of bands, offset from the current raster band number,
         for the current colorant, and is non-zero only in the case of
         separation imposition! */
      dl_bandnum = frameandline_to_band(band->bbox.y1, page) + band_offset;
      if (dl_bandnum == page->sizedisplaylist ) {
        /* The offset band is one beyond end of DL due to the way
           band offsets are calculated for separation imposition. */
        dl_bandnum--;
      }

      /* This function should always be called, even if we're not actually
         blitting the erase for a colorant, because it contains operations
         dependent on the blit colormap and the colorant index. These include
         preparing the erase color, and working out if the colorant is a
         modular halftone for the erase. */
      render_erase_prepare(p_rs, frameandline_to_band(band->bbox.y1, page)) ;

      if ( fNeedErase ) {
        /* Moved onto new band; Need to erase it regardless of colorant
           even if unknown. */
        Bool ripped_something = FALSE; /* Did erase change the band? */
        int32 source_band = dl_bandnum / page->sizedisplayfact;
        LISTOBJECT *lobjErase = p_rs->lobjErase ;
        HQASSERT(lobjErase != NULL, "Did not extract erase object correctly") ;

#ifdef DEBUG_BUILD
        if ( dl_bandnum < debug_render_firstband ||
             dl_bandnum > debug_render_lastband ) {
          /* Outside range of debug bands. Set band to white and don't do
             anything to it. */
          area0fill(p_rs->ri.rb.outputform);
          *p_rs->cs.p_white_on_white = TRUE ;
        } else
#endif
        /* If not forcing all bands to be written and there's nothing to render,
           optimise out the erase (if all pixels will be zero) or the
           read-back. */
        if ( !writealloutput
             && page->irr.store == NULL
             && ( !lobjErase->dldata.erase.with0
                  || !lobjErase->dldata.erase.newpage /* read-back */ )
             && ((p_rs->cs.blitmap->nrendered == 0
                  /* Blitmap depends on colorant, make sure can rely on it. */
                  && !gucr_colorantsMultiple(p_rs->cs.hc))
                 || (source_band < 0
                     || source_band >= page->sizefactdisplaylist)
                 || !bandSignificantObjectsToRip(page, source_band)) ) {
          ripped_something = FALSE;
          *p_rs->cs.p_white_on_white = TRUE;
          fNeedErase = FALSE;
          goto done_rendering;
        } else {
          /* OK, have to erase/read-back. Check for read-back first. */
          if ( !lobjErase->dldata.erase.newpage &&
               gucr_getRenderIndex(p_rs->hf) != -1 ) {
            /* There's been a paint on this sep, read the band back. */
            fNeedErase = FALSE;
            /* Check if this is the beginning of the band, and skip the
               read-back for subsequent portions. The read-back is done for the
               whole band at once (all colorants at once). */
            if ( retainedform->addr == pFormAddressOrig ) {
              /* RLE merges when writing to PGBRDWR */
              if ( !DOING_RUNLENGTH(page) ) {
                fOk = read_back_band(page, band, retainedform->size);
                if ( !fOk )
                  break;
              }
            }
            /* Whether we read back something this time around or not,
               we did read at the beginning, so set the flags always. */
            /** \todo Some read-backs do a zero fill, so white_on_white could
                be TRUE, if we knew. */
            *p_rs->cs.p_white_on_white = FALSE;
            ripped_something = TRUE;
          } else {
            /* Check for an internal retained raster pre-rendered band and if
               present the erase is not required. */
            fOk = irr_read_back_band(page->irr.store, sheet->nSheetNumber,
                                     frameandline_to_frameandband(band->bbox.y1, page),
                                     (uint8*)pband->mem,
                                     retainedform->size * sheet->colorants_per_band,
                                     retainedform->addr == pFormAddressOrig,
                                     &fNeedErase);
            if ( !fOk )
              break;

            if ( !fNeedErase ) { /* Did IRR read-back. */
              *p_rs->cs.p_white_on_white = FALSE;
              ripped_something = TRUE;
            }
          }
        } /* if can skip erase */
        /* If for one reason or another we still need an erase, then do it */
        if ( fNeedErase ) {
          fOk = render_erase_of_band(p_rs, &ripped_something,
                                     &do_modular_erase);
          if ( !fOk )
            break;
          fNeedErase = FALSE ;
        }
        pband->fDontOutput &= !ripped_something;
      }

#ifdef DEBUG_BUILD
      if ( dl_bandnum < debug_render_firstband ||
           dl_bandnum > debug_render_lastband ) {
        EMPTY_STATEMENT() ;
      } else
#endif
      if ( do_modular_erase ) {
        fOk = render_objs_of_band_mod_ht(p_rs, band, dl_bandnum);
      } else if ( (dl_bandnum >= 0 && dl_bandnum < page->sizedisplaylist)
                  || sheet->pass_data->screen_all_bands ) {
        if ( p_rs->cs.blitmap->nrendered != 0 ) {
          /* Got at least one channel to render */
          if ( p_rs->cs.bandlimits.y1 < retainedform->h &&
               p_rs->cs.bandlimits.y2 >= 0 ) {
            if ( p_rs->htm_info != NULL ) {
              fOk = render_objs_of_band_mod_ht(p_rs, band, dl_bandnum);
            } else {
              if (p_rs->ri.generate_object_map) {
                HQFAIL("Object map generation currently broken") ;
                /* Generate the object map, then do the actual colorant */
                fOk = render_objects_of_band(p_rs, dl_bandnum);
                if ( !fOk )
                  break;
                p_rs->ri.generate_object_map = FALSE; skip_om = TRUE;
              }
              fOk = render_objects_of_band(p_rs, dl_bandnum);
            }
            if ( !fOk )
              break; /* Failed to render all objects - abort rendering */
          } /* if bandlimits intersect page. */
        } /* if valid colorants */
      } /* if band not off page */

      /* If not white_on_white, we've ripped something that needs outputting. */
      pband->fDontOutput &= white_on_white;

    done_rendering:
      clip_context_end(&p_rs->ri) ;

      if ( gucr_colorantsBandChannelAdvance(p_rs->cs.hc) ) {
        /* Moving to new band that needs erasing - update band memory address */
        ++p_rs->nChannel; fNeedErase = TRUE;
        if (skip_om) {
          retainedform->addr =
            BLIT_ADDRESS(retainedform->addr,
                         retainedform->size + p_rs->forms->objectmapform.size);
          skip_om = FALSE;
        } else
          retainedform->addr =
            BLIT_ADDRESS(retainedform->addr, retainedform->size);
        /* Note: on the last band, size is the actually used portion not the
           whole band: assert this just to be sure */
        HQASSERT(retainedform->size == retainedform->l * band->y_allocated,
                 "render_colorants_of_band: size of retained form isnt L*(H - HOFF) on last band or L*RH on another");
      }

      blit_colormap_destroy(&p_rs->cs.blitmap, mm_pool_temp) ;
    } /* For over frame colorants */

    if ( p_rs->cs.blitmap != NULL ) /* In case of early exit */
      blit_colormap_destroy(&p_rs->cs.blitmap, mm_pool_temp) ;
#undef return

    /* For safety, to catch call chain errors: */
    p_rs->cs.erase_color = NULL ;
    p_rs->cs.knockout_color = NULL ;
    p_rs->cs.renderTracker = NULL ;
    p_rs->cs.p_white_on_white = NULL ;
    p_rs->ri.ht_params = NULL;
    p_rs->ri.rb.blits = NULL;

    /* Reset form address */
    retainedform->addr = pFormAddressOrig;
  }

  return fOk;
} /* Function render_colorants_of_band */

/* Band callbacks for surface.
 *
 * We go through this bother so that the surface can have a stack frame
 * interpolated into the rendering stack, as a convenient place to put
 * band-local data. The callback typedefs and callback system allows us to
 * alter the parameters of render_colorants_of_band() without affecting the
 * surface definitions (or even having to recompile them).
 */

/** \brief Default thread callback function. */
static Bool default_band_localiser(surface_handle_t *handle,
                                   const render_state_t *rs,
                                   render_band_callback_fn *callback,
                                   render_band_callback_t *data,
                                   surface_bandpass_t *bandpass)
{
  UNUSED_PARAM(surface_handle_t *, handle) ;
  UNUSED_PARAM(const render_state_t *, rs) ;
  UNUSED_PARAM(surface_bandpass_t *, bandpass) ;

  HQASSERT(callback != NULL, "No band callback function") ;
  HQASSERT(data != NULL, "No band callback data") ;

  return (*callback)(data) ;
}

/** \brief Render band callback data name. */
#define RENDER_BAND_CALLBACK_NAME "Render band callback"

/** \brief Parameters for render band callback function. */
struct render_band_callback_t {
  render_base_t *base ; /**< Parameter for render_colorants_of_band(). */
  band_data_t *band ;   /**< Parameter for render_colorants_of_band(). */
  Bool writealloutput ; /**< Parameter for render_colorants_of_band(). */
  int called ;          /**< Number of times the callback was called. */
  OBJECT_NAME_MEMBER
} ;

static Bool render_band_callback(render_band_callback_t *data)
{
  VERIFY_OBJECT(data, RENDER_BAND_CALLBACK_NAME) ;

  HQASSERT(data->called == 0, "Render band callback has been called already") ;
  ++data->called ;
  return render_colorants_of_band(data->base, data->band, data->writealloutput) ;
}

static Bool render_band_localised(render_base_t *base,
                                  band_data_t *band,
                                  Bool writealloutput,
                                  const surface_set_t *surfaces,
                                  surface_bandpass_t *bandpass)
{
  render_band_callback_t callback_data ;
  surface_band_localiser_fn *localiser_fn ;
  Bool result ;

  HQASSERT(base != NULL, "No render base") ;
  VERIFY_OBJECT(band, BAND_DATA_NAME) ;
  HQASSERT(surfaces != NULL, "No surface set") ;

  callback_data.base = base ;
  callback_data.band = band ;
  callback_data.writealloutput = writealloutput ;
  callback_data.called = 0 ;
  NAME_OBJECT(&callback_data, RENDER_BAND_CALLBACK_NAME) ;

  localiser_fn = surfaces->band_localiser ;
  if ( localiser_fn == NULL )
    localiser_fn = &default_band_localiser ;

  /* We don't need to save and restore the surface handle, there is only one
     call of render_band_localised for each construction of a render_base_t. */
  result = (*localiser_fn)(&base->rs.surface_handle, &base->rs,
                           &render_band_callback, &callback_data,
                           bandpass) ;

  /* Either we had a failure before the callback, or we should have been
     called exactly once */
  HQASSERT((!result && callback_data.called == 0) ||
           callback_data.called == 1,
           "Render band callback was not called back correctly") ;
  UNNAME_OBJECT(&callback_data) ;

  return result ;
}

static Bool render_one_band_of_frame(render_base_t *p_base, band_data_t *band,
                                     surface_bandpass_t *bandpass)
{
  Bool result;
  dcoord hoff, rh;
  render_forms_t *forms = p_base->rs.forms ;
  DL_STATE *page = p_base->rs.page ;
  const surface_set_t *surfaces = page->surfaces ;
  frame_data_t *frame ;
  sheet_data_t *sheet ;
  pass_data_t *pass ;
  band_t *pband ;

  VERIFY_OBJECT(band, BAND_DATA_NAME) ;
  frame = band->frame_data ;
  VERIFY_OBJECT(frame, FRAME_DATA_NAME) ;
  sheet = frame->sheet_data ;
  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;
  pass = sheet->pass_data ;
  VERIFY_OBJECT(pass, PASS_DATA_NAME) ;
  pband = band->p_band ;
  HQASSERT(pband, "No band table entry allocated") ;

#if defined(DEBUG_BUILD) /* Show which thread is doing which band */
  if ( (debug_render & DEBUG_RENDER_SHOW_THREADS) != 0 ) {
    monitorf((uint8 *)"R1B:%d,%c,%d\n",
             frameandline_to_band(band->bbox.y1, page),
             p_base->rs.pass_region_types == RENDER_REGIONS_BACKDROP ? 'B' : 'D',
             SwThreadIndex());
  }
#endif

  /* Get band raster offset from band height */
  hoff = frameandline_to_line(band->bbox.y1, page);
  FORM_UPDATE( theFormHOff, forms, hoff, hoff, hoff );
  rh = frameandline_to_line(band->bbox.y2, page) - hoff + 1;
  FORM_UPDATE( theFormRH, forms, rh, rh, rh );
  FORM_UPDATE( theFormS, forms,
               band->y_allocated * forms->retainedform.l,
               band->y_allocated * forms->contoneform.l,
               band->y_allocated * forms->clippingform.l );

  /* Pick up next band to use. */
  forms->retainedform.addr = pband->mem;
#if 0
  /** \todo ajcd 2012-10-10:?
      task_resource_fix(TASK_RESOURCE_MAP_OUT, band->bbox.y2, NULL) */
  if (page->output_object_map)
    forms->objectmapform.addr =
      BLIT_ADDRESS(pband->mem, forms->retainedform.size);
#endif

  if ( page->backdropShared != NULL )
    bd_bandInit(p_base->rs.composite_context);

  result = render_band_localised(p_base, band, pass->writealloutput, surfaces,
                                 bandpass);

  if ( page->backdropShared != NULL ) {
    /* Force the release of all blocks if the pass failed, or if we're in a
       sub-divided band. The backdrop code cannot cope with constructing the
       blocks in multiple passes yet. This will cause subsequent passes to
       re-composite the band. */
    Bool can_keep = (result && bandpass->action == SURFACE_PASS_DONE &&
                     band->y_allocated == rh) ;
    if ( !bd_bandRelease(p_base->rs.composite_context,
                         page->backdropShared, can_keep) )
      result = FALSE ;
  }

  /* Remember how big the band is for band_{compress,output}_task */
  pband->size_to_write = forms->retainedform.size * sheet->colorants_per_band;

  return result;
}

/*****************************************************************************/

/** Context specialiser for the compositing tasks. */
void render_task_specialise(corecontext_t *context,
                            void *args,
                            task_specialise_private *data)
{
  /* Renderers shouldn't get access to the interpreter state.  Clear various
     params so the renderer provokes asserts or crashes if they are used. */

  /** \todo ajcd 2011-02-07: assert that page pointer is valid pipeline page.
      We can't guarantee it will be inputpage or outputpage, because
      outputpage is moved on in dl_erase_finalise(), but the next page's tasks
      start running as soon as the wait dependency is released. */

  /* context->error is thread-specialised */
  HQASSERT(!context->is_interpreter, "Shouldn't be interpreter thread") ;
  /* context->thread_index is thread-specialised */
  context->page = args ;
  context->savelevel = -1 ;
  context->glallocmode = -1 ;
  /* context->mm_context is thread-specialised */
  HQASSERT(!context->between_operators, "Shouldn't be between operators") ;
  /* context->ht_context is thread-specialised */
  /* context->im_context is thread-specialised */
  /* context->taskcontext is thread-specialised */
  context->pscontext = NULL ;
  context->mischookparams = NULL ;
  context->userparams = NULL ;
  context->systemparams = NULL ;
  context->charcontext = NULL ;
  context->devicesparams = NULL ;
  context->fileioparams = NULL ;
  context->color_systemparams = NULL ;
  context->color_userparams = NULL ;
  context->pdfparams = NULL ;
  context->tiff6params = NULL ;
  context->wmpparams = NULL ;
  context->pdfin_h = NULL ;
  context->pdfout_h = NULL ;
  context->fontsparams = NULL ;
  context->xmlparams = NULL ;
  context->xpsparams = NULL ;
  context->trapping_systemparams = NULL ;
  context->userparamlist = NULL ;
  context->systemparamlist = NULL ;
  context->xmlparamlist = NULL ;

  task_specialise_done(context, args, data) ;
}

/* Page-level tasks **********************************************************/

static void page_data_cleanup(corecontext_t *context, void *args)
{
  page_data_t *page_data = args ;
  hq_atomic_counter_t after ;

  VERIFY_OBJECT(page_data, PAGE_DATA_NAME) ;

  HqAtomicDecrement(&page_data->refcount, after) ;
  HQASSERT(after >= 0, "Page data already released") ;
  if ( after == 0 ) {
    DL_STATE *page = context->page ;

    /* Here as well as composite_bands() for single pass */
    bd_sharedFree(&page->backdropShared);

    /* Finished with the region map. */
    bitGridDestroy(&page->regionMap);

    dlc_release(page->dlc_context, &page_data->dlc_watermark) ;

    /* Cancel and join trapping tasks before destroying trapping context. */
#if 0
    if ( page_data->trap_group != NULL ) {
      task_group_cancel(page_data->trap_group, INTERRUPT) ;
      (void)task_group_join(page_data->trap_group, NULL) ;
      task_group_release(&page_data->trap_group) ;
    }
#endif

    if ( page_data->trap_context != NULL )
      trapDispose(&page_data->trap_context) ;

    HQASSERT(!page_data->surface_started,
             "Surface still open when cleaning up page data") ;

    UNNAME_OBJECT(page_data) ;
    mm_free(mm_pool_temp, page_data, sizeof(*page_data)) ;
  }
}

/* Pass-level tasks **********************************************************/

/** \brief Cleanup function for page render tasks. */
static void pass_data_cleanup(corecontext_t *context, void *args)
{
  pass_data_t *pass = args ;
  hq_atomic_counter_t after ;

  VERIFY_OBJECT(pass, PASS_DATA_NAME) ;

  HqAtomicDecrement(&pass->refcount, after) ;
  HQASSERT(after >= 0, "Pass data already released") ;
  if ( after == 0 ) {
    DL_STATE *page = context->page ;

    if ( pass->mht_info != NULL ) {
      htm_RenderCompletion(pass->mht_info, page->eraseno, TRUE /*aborting*/);
      mm_free(mm_pool_temp, pass->mht_info, sizeof(sw_htm_render_info)) ;
      pass->mht_info = NULL ;
    }

    if ( pass->im_expbufs != NULL )
      im_freeexpbuf(page, pass->im_expbufs);

    args = pass->page_data ;
    UNNAME_OBJECT(pass) ;
    mm_free(mm_pool_temp, pass, sizeof(pass_data_t)) ;
    page_data_cleanup(context, args) ;
  }
}

/* Sheet-level tasks *********************************************************/

/** \brief Cleanup function for sheet render tasks. */
static void sheet_data_cleanup(corecontext_t *context, void *args)
{
  sheet_data_t *sheet = args ;
  pass_data_t *pass ;
  DL_STATE *page = context->page ;
  const surface_set_t *surfaces = page->surfaces ;
  GUCR_CHANNEL *hf ;
  int32 nFrameNumber ;

  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;
  pass = sheet->pass_data ;
  VERIFY_OBJECT(pass, PASS_DATA_NAME) ;

  if (sheet->surface_started &&
      gucr_separationStyle(page->hr) == GUCR_SEPARATIONSTYLE_SEPARATIONS) {
    /* Notify finished rendering separation */
    (void)timeline_pop(&page->timeline, SWTLT_RENDER_SEPARATION,
                       *sheet->printerstate == SHEET_OK ||
                       *sheet->printerstate == SHEET_CANCEL) ;
  }

  /* If we didn't already abort the PGB device, do it now. */
  multi_mutex_lock(&pagebuffer_mutex);
  abort_pagebuffer_device(page, sheet) ;
  multi_mutex_unlock(&pagebuffer_mutex);

  if ( sheet->fScreensExported ) { /* Well, delete them! */
    sheet->fScreensExported = FALSE;
    ht_delete_export_screens();
  }

  /* Find the highest frame index. */
  for ( hf = sheet->hf, nFrameNumber = 0 ;
        gucr_framesMore(hf) ;
        gucr_framesNext(&hf), ++nFrameNumber )
    EMPTY_STATEMENT() ;

  /* Return all bands sent to the PGB and not yet reclaimed. */
  (void)bands_copied_notify(sheet->band_pool, nFrameNumber * page->page_h) ;
  resource_pool_release(&sheet->band_pool) ;

  /* Might have been cancelled before construction of sheet's task group. */
  if ( sheet->task_group != NULL )
    task_group_release(&sheet->task_group) ;

  /* Free up any spare memory allocated to h/t FORMS. */
  if ( sheet->fUnloadChForms ) {
    sheet->fUnloadChForms = FALSE;
    ht_end_sheet(page->eraseno, sheet->hf, FALSE);
  }

  if ( sheet->surface_started && surfaces->sheet_end != NULL ) {
    sheet->surface_started = FALSE ;
    /** \todo ajcd 2010-12-16: What to do with the retcode? */
    (void) (*surfaces->sheet_end)(sheet->surface_handle, sheet, FALSE) ;
  }

  args = sheet->pass_data ;
  UNNAME_OBJECT(sheet) ;
  mm_free(mm_pool_temp, sheet, sizeof(sheet_data_t)) ;
  pass_data_cleanup(context, args) ;
}

/** \brief Worker function to prepare the surface to render a sheet. */
static Bool sheet_start(corecontext_t *context, void *args)
{
  DL_STATE* page = context->page ;
  sheet_data_t *sheet = args ;
  pass_data_t *pass ;
  const surface_set_t *surfaces ;

  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;
  pass = sheet->pass_data ;
  VERIFY_OBJECT(pass, PASS_DATA_NAME) ;
  surfaces = page->surfaces ;

  sheet->surface_handle = pass->surface_handle ;
  if ( surfaces->sheet_begin != NULL &&
       !(*surfaces->sheet_begin)(&sheet->surface_handle, sheet) )
    return FALSE ;

  sheet->surface_started = TRUE ;

  if (gucr_separationStyle(page->hr) == GUCR_SEPARATIONSTYLE_SEPARATIONS) {
    /* Notify rendering a separation */
    CHECK_TL_VALID(timeline_push(&page->timeline, SWTLT_RENDER_SEPARATION,
                                 0 /*end*/, SW_TL_UNIT_NONE, page, NULL, 0));
  }

  if ( surface_find(surfaces, SURFACE_OUTPUT)->screened ||
       DOING_RUNLENGTH(page) ) {
    /* Pre-loads form caches for each h/t screen on this sheet. */
    sheet->fUnloadChForms = ht_start_sheet(context, page->eraseno, sheet->hf);
    if ( !sheet->fUnloadChForms )
      return FALSE;
  }

  /* We could split this function into separate tasks right here, the surface
     setup and the PGB opening are logically separate, however it doesn't
     help that much. We still have to put a dependency from opening the PGB
     to the render chain, so that partial paint and compositing can read
     back band data. */

  return (set_pagebuffer_params(page, sheet) &&
          check_pagebuffer_device(page, sheet) &&
          open_pagebuffer_device(page, sheet, FALSE /*not reoutputting*/)) ;
}

/** \brief Worker function to note that sheet rendering succeeded. */
static Bool sheet_render_done(corecontext_t *context, void *args)
{
  sheet_data_t *sheet = args ;
  pass_data_t *pass ;
  const surface_set_t *surfaces ;
  Bool result = TRUE ;

  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;
  pass = sheet->pass_data ;
  VERIFY_OBJECT(pass, PASS_DATA_NAME) ;
  surfaces = context->page->surfaces ;

  if ( sheet->surface_started && surfaces->sheet_end != NULL ) {
    sheet->surface_started = FALSE ;
    if ( !(*surfaces->sheet_end)(sheet->surface_handle, sheet, TRUE) )
      result = FALSE ;
  }

  /* Free up any spare memory allocated to h/t FORMS. */
  if ( sheet->fUnloadChForms ) {
    sheet->fUnloadChForms = FALSE;
    ht_end_sheet(context->page->eraseno, sheet->hf,
                 pass->paint_type != PAINT_TYPE_COMPOSITE);
  }

  return result ;
}

/** \brief Worker function to note that sheet output succeeded. */
static Bool sheet_output_done(corecontext_t *context, void *args)
{
  sheet_data_t *sheet = args ;
  pass_data_t *pass ;
  Bool result ;

  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;
  pass = sheet->pass_data ;
  VERIFY_OBJECT(pass, PASS_DATA_NAME) ;

  multi_mutex_lock(&pagebuffer_mutex);
  result = close_pagebuffer_device(context->page, sheet) ;
  multi_mutex_unlock(&pagebuffer_mutex);

  if ( sheet->fScreensExported ) { /* Well, delete them! */
    sheet->fScreensExported = FALSE;
    ht_delete_export_screens();
  }

  /* failure of close_pagebuffer_device() can request re-output, the
     dependent task will not run in that case. */

  return result ;
}

/** \brief Worker function to note that whole sheet succeeded. */
static Bool sheet_done(corecontext_t *context, void *args)
{
  sheet_data_t *sheet = args ;

  UNUSED_PARAM(corecontext_t *, context) ;

  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;
  *sheet->printerstate = SHEET_OK ;

  return TRUE ;
}

/** \brief Worker function to prepare the surface to reoutput a sheet. */
static Bool sheet_reoutput_start(corecontext_t *context, void *args)
{
  return open_pagebuffer_device(context->page, args, TRUE /*reoutputting*/) ;
}

/* Frame-level tasks *********************************************************/

/** \brief Create and initialise the frame data. */
static frame_data_t *frame_data_create(sheet_data_t *sheet, GUCR_CHANNEL *hf,
                                       int32 nFrameNumber)
{
  frame_data_t *frame ;

  if ( (frame = mm_alloc(mm_pool_temp, sizeof(frame_data_t),
                         MM_ALLOC_CLASS_FRAME_DATA)) == NULL )
    return error_handler(VMERROR), NULL ;

  frame->refcount = 1 ;
  frame->sheet_data = sheet ;
  frame->hf = hf ;
  frame->nFrameNumber = nFrameNumber ;
  frame->surface_started = FALSE ;
  frame->surface_handle.frame = NULL ;
  frame->render_start_task = NULL ;
  frame->render_end_task = NULL ;
  NAME_OBJECT(frame, FRAME_DATA_NAME) ;

  return frame ;
}

/** \brief Data cleanup function for frame group tasks.

    \param[in] context  The thread context executing the frame completion task.
    \param[in] args     The band data object.

    This runs as the finaliser of the frame group tasks. */
static void frame_data_cleanup(corecontext_t *context, void *args)
{
  frame_data_t *frame = args ;
  hq_atomic_counter_t after ;

  VERIFY_OBJECT(frame, FRAME_DATA_NAME) ;

  /* NB. When frames can render in parallel, this must be done higher up. */
  context->page->im_purge_allowed = TRUE;

  HqAtomicDecrement(&frame->refcount, after) ;
  HQASSERT(after >= 0, "Frame data already released") ;
  if ( after == 0 ) {
    if ( frame->surface_started ) {
      const surface_set_t *surfaces = context->page->surfaces ;

      frame->surface_started = FALSE ;
      if ( surfaces->frame_end != NULL ) {
        /** \todo ajcd 2010-12-16: What to do with the retcode? */
        (void)(*surfaces->frame_end)(frame->surface_handle, frame, FALSE) ;
      }
    }

    if ( frame->render_start_task != NULL )
      task_release(&frame->render_start_task) ;

    if ( frame->render_end_task != NULL )
      task_release(&frame->render_end_task) ;

    UNNAME_OBJECT(frame) ;
    mm_free(mm_pool_temp, frame, sizeof(*frame)) ;
  }
}

/** \brief Worker function to prepare the surface to render a frame.

    \param[in] context  The thread context executing the frame start task.
    \param[in] args     The frame data object.

    Note that this function is not called when re-outputting a PGB in response
    to a sheet reoutput error. */
static Bool frame_render_start(corecontext_t *context, void *args)
{
  frame_data_t *frame = args ;
  sheet_data_t *sheet ;
  const surface_set_t *surfaces = context->page->surfaces ;

  VERIFY_OBJECT(frame, FRAME_DATA_NAME) ;
  sheet = frame->sheet_data ;
  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;

  frame->surface_handle = sheet->surface_handle ;
  if ( surfaces->frame_begin != NULL &&
       !(*surfaces->frame_begin)(&frame->surface_handle, frame) )
    return FALSE ;

  frame->surface_started = TRUE ;

  im_store_pre_render_assertions(context->page->im_shared);
  context->page->im_purge_allowed = FALSE ;

  return TRUE ;
}

/** \brief Worker function to note that frame rendering succeeded.

    \param[in] context  The thread context executing the frame completion task.
    \param[in] args     The frame data object.

    Frame completion has both a worker and finaliser function so that the
    dependency ensures that frame tasks can be joined before the start of the
    next frame.

    \note This function is not used when re-outputting a PGB in response to a
    sheet reoutput error.
 */
static Bool frame_render_done(corecontext_t *context, void *args)
{
  frame_data_t *frame = args ;

  VERIFY_OBJECT(frame, FRAME_DATA_NAME) ;

  /* Set the frame index for the band read back in the next render pass
     (must be done after frame_render_graph). */
  gucr_setRenderIndex(frame->hf, frame->nFrameNumber);

  if ( frame->surface_started ) {
    const surface_set_t *surfaces = context->page->surfaces ;

    frame->surface_started = FALSE ;
    if ( surfaces->frame_end != NULL  &&
         !(*surfaces->frame_end)(frame->surface_handle, frame, TRUE) )
      return FALSE ;
  }

  return TRUE ;
}

/* Band-level tasks *********************************************************/

static Bool band_render_graph(corecontext_t *context,
                              frame_data_t *frame,
                              const dbbox_t *bbox, dcoord yalloc,
                              intptr_t surface_pass,
                              Bool serialize,
                              task_t **prev_render, task_vector_t *mht,
                              Bool beforeop, task_t *op) ;

/** \brief Get initial bounds for a band from a band and frame number. */
static void band_extent(DL_STATE *page, int32 framenum, bandnum_t bandnum,
                        dbbox_t *bbox)
{
  dcoord y1, yalloc ;

  HQASSERT(bandnum >= 0 && bandnum < page->sizedisplaylist,
           "Initial band number out of range") ;
  y1 = bandnum * page->band_lines ;
  if ( y1 + page->band_lines > page->page_h ) {
    yalloc = page->page_h - y1 ;
  } else {
    yalloc = page->band_lines ;
  }
  y1 += framenum * page->page_h ;

  bbox->y1 = y1 ;
  bbox->y2 = y1 + yalloc - 1 ;
  bbox->x1 = 0 ;
  bbox->x2 = page->page_w - 1 ;
}

/** \brief Create and initialise the band data. */
static band_data_t *band_data_create(frame_data_t *frame,
                                     const dbbox_t *bbox, dcoord yalloc,
                                     intptr_t surface_pass)
{
  band_data_t *band ;
  hq_atomic_counter_t before ;

  /* Create a common args structure for all tasks on this band. */
  if ( (band = mm_alloc(mm_pool_temp, sizeof(band_data_t),
                        MM_ALLOC_CLASS_BAND_DATA)) == NULL )
    return error_handler(VMERROR), NULL ;

  HQASSERT(bbox != NULL, "No band bounding box") ;
  HQASSERT(yalloc >= bbox->y2 - bbox->y1 + 1, "Not enough lines in band") ;

  band->refcount = 1 ;
  band->bbox = *bbox ;
  band->y_allocated = yalloc ;
  band->surface_pass = surface_pass ;
  HqAtomicIncrement(&frame->refcount, before) ;
  HQASSERT(before > 0, "Frame data should have previously been referenced") ;
  band->frame_data = frame ;
  band->p_band = NULL ;
  band->output_task = NULL ;
  band->incomplete = FALSE ;
  NAME_OBJECT(band, BAND_DATA_NAME) ;

  return band ;
}

/** \brief Data cleanup function for band group tasks.

    \param[in] context  The thread context executing the band completion task.
    \param[in] args     The band data object.

    This runs as the finaliser of the band group tasks. */
static void band_data_cleanup(corecontext_t *context, void *args)
{
  band_data_t *band = args ;
  hq_atomic_counter_t after ;

  VERIFY_OBJECT(band, BAND_DATA_NAME) ;

  HqAtomicDecrement(&band->refcount, after) ;
  HQASSERT(after >= 0, "Band data already released") ;
  if ( after == 0 ) {
    if ( band->output_task != NULL )
      task_release(&band->output_task) ;
    args = band->frame_data ;
    UNNAME_OBJECT(band) ;
    mm_free(mm_pool_temp, band, sizeof(*band)) ;
    frame_data_cleanup(context, args) ;
  }
}

/** \brief Band renderer worker function. */
static Bool band_render(corecontext_t *context, void *args)
{
  band_data_t *band = args ;
  frame_data_t *frame ;
  sheet_data_t *sheet ;
  pass_data_t *pass ;
  page_data_t *page_data ;
  Bool result ;
  unsigned int resource_tid = 0 ;
  const surface_t *surface ;
  DL_STATE *page = context->page ;
  render_base_t render_base ;
  render_state_t *prs = &render_base.rs ;

  VERIFY_OBJECT(band, BAND_DATA_NAME) ;
  frame = band->frame_data ;
  VERIFY_OBJECT(frame, FRAME_DATA_NAME) ;
  sheet = frame->sheet_data ;
  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;
  pass = sheet->pass_data ;
  VERIFY_OBJECT(pass, PASS_DATA_NAME) ;
  page_data = pass->page_data ;
  VERIFY_OBJECT(page_data, PAGE_DATA_NAME) ;

  surface = surface_find(page->surfaces, SURFACE_OUTPUT) ;
  HQASSERT(surface, "No output surface found") ;

#define return DO_NOT_return_SET_result_instead!

  /** \todo ajcd 2010-12-20: Replace this with a dynamic resource acquisition
      system. Index for expander buffer/composite context/imstore file
      etc. */
  if ( pass->allowMultipleThreads )
    resource_tid = SwThreadIndex() ;

  do {
    COLORSPACE_ID colorSpaceId;
    surface_bandpass_t bandpass = SURFACE_BANDPASS_INIT ;

    /* Band resource ID is last line in total frame set. */
    band->p_band = task_resource_fix(TASK_RESOURCE_BAND_OUT, band->bbox.y2,
                                     NULL /*cachehit*/) ;
    HQASSERT(band->p_band != NULL,
             "Couldn't get output band from resource pool") ;
    HQASSERT(band->p_band->next == NULL,
             "Band shouldn't have extensions after initial fix") ;

    result = FALSE ;

    /* Initialise base render state. */
    prs->forms = &render_base.forms;
    init_forms(prs->forms, page, pass->mht_info != NULL);
    fix_reserved_band_resources(prs->forms, page->reserved_bands);

    prs->page = page ;
    prs->hr = page->hr ;
    prs->hf = frame->hf ;
    if ( gucr_interleavingStyle(prs->hr) == GUCR_INTERLEAVINGSTYLE_BAND )
      prs->nChannel = 0 ;
    else
      prs->nChannel = frame->nFrameNumber ;

    /* Set up in render_erase_prepare(): */
    prs->lobjErase = NULL ;

    prs->composite_context = NULL;
    if ( page->backdropShared &&
         (pass->region_types & RENDER_REGIONS_BACKDROP) != 0 ) {
      prs->composite_context =
        task_resource_fix(TASK_RESOURCE_COMPOSITE_CONTEXT, resource_tid, NULL);
    }

    /* The current backdrop (changes per group). */
    prs->backdrop = NULL;

    gucr_colorantCount(prs->hr, &prs->nCi);

    /* Initialised in render_colorants_of_band(), taking into account separation
       imposition offsets: */
    prs->band = -1 ;

    prs->pass_region_types = pass->region_types;
    prs->fPixelInterleaved = (gucr_interleavingStyle(prs->hr) == GUCR_INTERLEAVINGSTYLE_PIXEL);

    guc_deviceToColorSpaceId(prs->hr, &colorSpaceId);
    prs->maxmode = (colorSpaceId == SPACE_DeviceRGB) ^ page->forcepositive
      ? BLT_MAX_MIN : BLT_MAX_MAX ;
    /* Coloured patterns can be merged unless group is non-knockout */
    prs->fMergePatterns = TRUE ;
    prs->dopatterns = TRUE ;
    prs->htm_info = pass->mht_info ;
    prs->surface_handle = frame->surface_handle ;
    dlc_clear(&prs->dlc_watermark);
    prs->spotno_watermark = page_data->spotno_watermark ;
    prs->trap_backdrop_context = page_data->trap_context;

#if 0
    if (page->output_object_map) {
      /* Store colorant information for om_do_knockout */
      prs->cis = dl_alloc(page->dlpools,
                          sizeof(COLORANTINDEX) * prs->nCi,
                          MM_ALLOC_CLASS_GUC_RASTERSTYLE);
      if (prs->cis == NULL) {
        result = error_handler(VMERROR);
        break;
      }

      gucr_colorantIndices(hr, prs->cis, &prs->nCiUnique);
    }
#endif

    /* Clear per-colorant state, for safety's sake. */
    prs->cs.overprint_mask = NULL ;
    prs->cs.maxblit_mask = NULL ;
    prs->cs.blitmap = NULL ;
    prs->cs.erase_color = NULL ;
    prs->cs.knockout_color = NULL ;
    prs->cs.renderTracker = NULL ;
    prs->cs.hc = NULL ;
    prs->cs.p_white_on_white = NULL ;
    prs->cs.bandlimits = band->bbox ; /* N.B. this does not include colorant Y offset */
    prs->cs.fIsHalftone = surface->screened;
    prs->cs.bg_separation = FALSE ;
    prs->cs.fSelfIntersect = FALSE ;
    prs->cs.selected_mht = NULL ;
    prs->cs.selected_mht_is_erase = FALSE ;

    /* Remember the raster handle, reset last spot no used, and contone blt,
       and refer rs to itself */
    prs->ri.p_rs = prs;
    prs->ri.lobj = NULL ;
    prs->ri.pattern_state = PATTERN_OFF;
    prs->ri.clip = prs->cs.bandlimits ;
    prs->ri.x1maskclip = MAXDCOORD ;
    prs->ri.x2maskclip = MINDCOORD ;
    prs->ri.region_set_type = RENDER_REGIONS_DIRECT;
    prs->ri.fSoftMaskInPattern = FALSE ;
    prs->ri.overrideColorType = GSC_UNDEFINED;
    prs->ri.generate_object_map = FALSE;
    prs->ri.bounds = prs->cs.bandlimits ;
    prs->ri.ht_params = NULL;
    prs->ri.group_tracker = NULL ;
    prs->ri.surface = surface ;

    prs->ri.rb.p_ri = &prs->ri;
    prs->ri.rb.outputform = &prs->forms->retainedform;
    prs->ri.rb.ylineaddr = NULL;
    prs->ri.rb.clipform = &prs->forms->clippingform;
    prs->ri.rb.ymaskaddr = NULL;
    prs->ri.rb.x_sep_position = 0;
    prs->ri.rb.y_sep_position = 0;
    prs->ri.rb.p_painting_pattern = NULL;
    prs->ri.rb.clipmode = BLT_CLP_RECT;
    prs->ri.rb.depth_shift = 0;
    prs->ri.rb.maxmode = BLT_MAX_NONE ;
    prs->ri.rb.opmode = BLT_OVP_NONE ;
    prs->ri.rb.color = NULL ;
    /* prs->ri.rb.blits set up in render_colorants_of_band: */
    prs->ri.rb.blits = NULL ; /* defensive programming */

    /** \todo ajcd 2010-12-17: Proper resource API for image expansion
        buffers. */
    context->im_context->expbuf = pass->im_expbufs[resource_tid] ;

    bandpass.current_pass = band->surface_pass ;
    result = render_one_band_of_frame(&render_base, band, &bandpass) ;
    band->surface_pass = bandpass.current_pass ;

    if ( !result || bandpass.action == SURFACE_PASS_DONE )
      break ;

    if ( bandpass.action == SURFACE_PASS_YSPLIT ) {
      probe_other(SW_TRACE_RENDER_SPLIT_Y, SW_TRACETYPE_MARK,
                  band->bbox.y2 - band->bbox.y1 + 1) ;

      if ( band->bbox.y2 > band->bbox.y1 ) {
        task_t *render = task_current() ;
        dbbox_t bbox = band->bbox ;

        /* Unfix the resource, because if we're retried we'll have a different
           band height. */
        free_band_extensions(sheet->band_pool, band->p_band) ;
        task_resource_unfix(TASK_RESOURCE_BAND_OUT, band->bbox.y2,
                            band->p_band) ;
        band->p_band = NULL ;

        /* If not repeating the same band, append a band graph after this band.
           Note that this won't work for MHT, there is no MHT gate vector,
           however we don't expect MHT to be used with any surface that
           requires redo or subdivision of bands. */
        band->bbox.y2 = (band->bbox.y2 + band->bbox.y1) / 2 ;
        bbox.y1 = band->bbox.y2 + 1 ;
        HQASSERT(bbox.y1 <= bbox.y2 && band->bbox.y1 <= band->bbox.y2,
                 "Band Y split invalid") ;
        result = band_render_graph(context, frame,
                                   &bbox, band->y_allocated,
                                   bandpass.next_pass, bandpass.serialize,
                                   NULL /*no MHT prev render*/,
                                   NULL /*no MHT vector*/,
                                   FALSE /*after output*/, band->output_task) ;
        task_release(&render) ;
      } else {
        result = error_handler(LIMITCHECK) ;
      }
    } else if ( bandpass.action == SURFACE_PASS_XSPLIT ) {
      probe_other(SW_TRACE_RENDER_SPLIT_X, SW_TRACETYPE_MARK,
                  band->bbox.x2 - band->bbox.x1 + 1) ;

      if ( band->bbox.x2 > band->bbox.x1 ) {
        task_t *render = task_current() ;
        dbbox_t bbox = band->bbox ;

        band->bbox.x2 = (band->bbox.x2 + band->bbox.x1) / 2 ;
        bbox.x1 = band->bbox.x2 + 1 ;
        HQASSERT(bbox.x1 <= bbox.x2 && band->bbox.x1 <= band->bbox.x2,
                 "Band X split invalid") ;
        result = band_render_graph(context, frame,
                                   &bbox, band->y_allocated,
                                   bandpass.next_pass, bandpass.serialize,
                                   NULL /*no MHT prev render*/,
                                   NULL /*no MHT vector*/,
                                   FALSE /*after output*/, band->output_task) ;
        task_release(&render) ;

        /* Mark that the current band is incomplete. */
        band->incomplete = TRUE ;
      } else {
        result = error_handler(LIMITCHECK) ;
      }
    } else {
      HQFAIL("Invalid band pass action") ;
    }
  } while ( result ) ;

  context->im_context->expbuf = NULL ;

#undef return
  return result ;
}

/** \brief Band compression worker function.

     Compression is only performed if the interpreter has previously
     determined that compression is feasible. If compression is not feasible,
     or if compression fails in some (recoverable) way, the raster band can
     still be used and output to the page buffer device so long as we tell it
     the band is not compressed - this is achieved via the band's fCompress
     flag. */
static Bool band_compress(corecontext_t *context, void *args)
{
  band_data_t *band = args ;
  frame_data_t *frame ;
  sheet_data_t *sheet ;
  Bool result = FALSE ;
  DEVICELIST *compress_dev;
  band_t *pband ;
  DL_STATE *page ;

  VERIFY_OBJECT(band, BAND_DATA_NAME) ;
  frame = band->frame_data ;
  VERIFY_OBJECT(frame, FRAME_DATA_NAME) ;
  sheet = frame->sheet_data ;
  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;
  pband = band->p_band ;
  HQASSERT(pband, "No band table entry") ;

  if ( pband->fDontOutput || !sheet->fCanCompress )
    return TRUE;

  page = context->page ;
  HQASSERT(page != NULL, "No output page") ;

  /** \todo ajcd 2010-12-10: Maybe re-factor back to sharing a small set of
      compress device clones for the whole page, and storing a pointer to the
      compression device in the args. For now, we'll do the allocation, setup
      and deallocation every time and measure if it is a significant drain
      before creating a compress device pool. */

  /* Create a new instance of the %compress% device for just this band. */
  compress_dev = device_alloc(STRING_AND_LENGTH("compress"));
  if ( compress_dev == NULL )
    return FALSE ;

  if ( device_connect(compress_dev, COMPRESS_DEVICE_TYPE,
                      (char*)compress_dev->name, 0, TRUE) ) {
    /** \todo ajcd 2011-01-03: I don't like recalculating the band size, and
        I don't like the page reference. We should probably store the
        line range in the band_data_t, or some such. */
    int32 size_uncompressed = CAST_SIZET_TO_INT32(page->band_l) *
      band->y_allocated ;
    DEVICE_FILEDESCRIPTOR compress_fd =
      open_compress_device(compress_dev,
                           page->pgbdev, sheet->colorants_per_band,
                           size_uncompressed);

    if ( compress_fd >= 0 ) {
      int32 size_if_compressed = compress_band(compress_dev, compress_fd,
                                               (uint8 *)pband->mem,
                                               pband->size_to_write / sheet->colorants_per_band) ;

      if (size_if_compressed == 0) { /* Unrecoverable error */
        pband->fCompressed = FALSE;
        result = device_error_handler(compress_dev) ;
      } else if (size_if_compressed > 0) {
        /* A -ve return indicates that the band could not be compressed */
        pband->fCompressed = TRUE;
        pband->size_to_write = size_if_compressed ;
        result = TRUE ;
      } else {
        pband->fCompressed = FALSE;
        result = TRUE ;
      }

      close_compress_device(compress_dev, compress_fd) ;
    } else /* open_compress_device failed */
      return device_error_handler(compress_dev) ;
  } else /* device_connect failed */
    return device_error_handler(compress_dev) ;

  if ( (*theIDevDismount(compress_dev))(compress_dev) == -1 )
    result = device_error_handler(compress_dev);
  device_free(compress_dev) ;

  return result ;
}

/** \brief Band output worker function. */
static Bool band_output(corecontext_t *context, void *args)
{
  band_data_t *band = args ;
  DL_STATE *page = context->page;
  frame_data_t *frame ;
  sheet_data_t *sheet ;
  band_t *pband ;
  Bool result ;

  /** \todo ajcd 2011-02-11: How often, and in which tasks, should we check
      for interrupt? At the moment, it's before potentially lengthy read or
      write operations only. Should the task system do this for us? If so,
      how can it avoid cancelling really important tasks, like joiners? */
  if ( !interrupts_clear(allow_interrupt) )
    return report_interrupt(allow_interrupt) ;

  VERIFY_OBJECT(band, BAND_DATA_NAME) ;
  frame = band->frame_data ;
  VERIFY_OBJECT(frame, FRAME_DATA_NAME) ;
  sheet = frame->sheet_data ;
  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;

  pband = band->p_band ;
  HQASSERT(pband, "No band table entry") ;

  /* No need to release the band resource, it'll revert at the end of the
     band group anyway. */
  if ( pband->fDontOutput )
    return TRUE ;

  result = TRUE ;

#define return DO_NOT_return_SET_result_INSTEAD!
  /* Detach this band from the task group, so it can survive destruction of
     the group. */
  task_resource_detach(TASK_RESOURCE_BAND_OUT, band->bbox.y2, pband) ;

  multi_mutex_lock(&pagebuffer_mutex);

  if (sheet->pgbfd >= 0) { /* make sure some other thread hasn't closed it */
    /** \todo ajcd 2011-02-12: If reoutputting the PGB, we don't want to
        re-interpret the RLE, so use a band flag to tell whether we're
        rendering RLE to PGB. */
    if ( !band_set_compressed(context->page, pband->fCompressed) ||
         !band_set_lines(context->page,
                         DOING_RUNLENGTH(context->page)
                         ? 1
                         : band->bbox.y2 - band->bbox.y1 + 1) ) {
      result = error_handler(IOERROR);
    } else if ( DOING_RUNLENGTH(context->page) ) { /* Doing RLE - finish it off */
      result = output_rle_to_pagebuffer(page, sheet, band) ;
    } else {
      /* write the data to the output device - seek then write */
      do {
        Hq32x2 pgbPos ;
        Hq32x2FromInt32(&pgbPos, band->bbox.y1);
        if ( (*theISeekFile(page->pgbdev))(page->pgbdev, sheet->pgbfd,
                                           &pgbPos, SW_SET) ) {
          break; /* the seek succeeded */
        }
        if ( !printerupset(page, sheet) )
          result = FALSE;
      } while ( result ) ;

      while ( result ) {
        /* Need to load this into a variable before writing to PGB device
           because of race condition with PGB notification callback, which
           can make band free before returning from write. */
        int32 size_to_write = pband->size_to_write ;

        if ( (*theIWriteFile(page->pgbdev))(page->pgbdev, sheet->pgbfd,
                                            (uint8 *)pband->mem,
                                            size_to_write) == size_to_write )
          break ;
        if ( !printerupset(page, sheet) )
          result = FALSE ;
      }
    }
  } else /* sheet->pgbfd < 0 */
    result = error_handler( IOERROR ); /* if it's closed, abort */

  multi_mutex_unlock(&pagebuffer_mutex);

  /* Return the detached band to the RIP's control if we failed to output it. */
  if ( !result ) {
    free_band_extensions(sheet->band_pool, band->p_band) ;
    task_resource_unfix(TASK_RESOURCE_BAND_OUT, band->bbox.y2, band->p_band) ;
    band->p_band = NULL ;
  }

  /* else the band remained detached, and won't be returned to the band pool
     until bands_copied_notify() is called. */

#undef return
  return result ;
}

/** \brief Band readback worker function. */
static Bool band_readback(corecontext_t *context, void *args)
{
  band_data_t *band = args ;
  frame_data_t *frame ;
  DL_STATE *page = context->page ;

  /** \todo ajcd 2011-02-11: How often, and in which tasks, should we check
      for interrupt? At the moment, it's before potentially lengthy read or
      write operations only. Should the task system do this for us? If so,
      how can it avoid cancelling really important tasks, like joiners? */
  if ( !interrupts_clear(allow_interrupt) )
    return report_interrupt(allow_interrupt) ;

  VERIFY_OBJECT(band, BAND_DATA_NAME) ;
  frame = band->frame_data ;
  VERIFY_OBJECT(frame, FRAME_DATA_NAME) ;

  /* Band resource ID is first last in band, counted across the frame set. */
  band->p_band = task_resource_fix(TASK_RESOURCE_BAND_OUT, band->bbox.y2,
                                   NULL /*cachehit*/) ;
  HQASSERT(band->p_band != NULL,
           "Couldn't get output band from resource pool") ;
  HQASSERT(band->p_band->next == NULL,
           "Band shouldn't have extensions after initial fix") ;

  return read_back_band(page, band, band->y_allocated * (int32)page->band_l) ;
}

/* Render graph construction *************************************************/

/** \brief Construct the task graph for rendering a band.

    \param context  A core context.
    \param frame    The frame data for the frame containing the new band.
    \param bbox     The bounding box of the portion of the band to render.
    \param yalloc   The size of the output band, expressed as the
                    number of lines of data that the form can hold. When
                    sub-dividing bands for RLE, we retain the full band size
                    for the sub-divided bands.
    \param surface_pass  The surface pass data cookie. This is initialised to
                    zero for the first pass, if the render surface requests
                    further passes, it can also set a cookie that can be used
                    by the surface to control the next pass.
    \param serialize FALSE if the band group should be inserted in such a
                    way that rendering and compression of bands can occur in
                    parallel with other bands, TRUE if they must be sequential.
    \param prev_render  If non-NULL, this is initialised to a task that the new
                    band's render task will depend on, and updated to contain
                    the new band's render task. These dependencies are added
                    during initial graph construction when modular halftoning
                    latency is used, to prevent render tasks from touching the
                    pixels whilst the modular halftone process is running. If
                    NULL, no such dependencies are added.
    \param mht      If non-null, a vector of tasks used to delay compression of
                    previous bands. These are used to prevent compression of
                    bands while they may still be required by modular
                    halftones. The tasks in the vector are shuffled down each
                    time, with the first task being made ready to run, and a
                    new task added at the end of the vector.
    \param beforeop TRUE if the new band will be inserted before the \a op
                    task, FALSE if inserted after. We insert after an existing
                    band when splitting bands, before the sheet output
                    completion when constructing the initial graph. (We cannot
                    insert after a previous output when constructing the initial
                    graph, because the band may run asynchronously and
                    request a split before we can construct the next band. This
                    would leave the output tasks in an incomplete order.)
    \param op       An output task to either insert the band before or after,
                    according to \a beforeop.

    \retval TRUE    If the new band graph was constructed correctly.
    \retval FALSE   If the new band graph was not constructed correctly. Note
                    that the return value does not indicate whether the band
                    graph has or will run successfully.

    This function deliberately does not use the \c render_graph_t structure
    to construct the band graph, because band graphs may also be constructed
    on the fly by render tasks themselves, when splitting bands into multiple
    parts.

    \note When used for band splitting, this function is not as general as we
    would like. We cannot construct correct render graphs for split bands
    which include modular halftone latency dependencies, and the method used
    for RLE compositing splits produces sub-optimal render graphs, the
    compression and output tasks cannot run in parallel with the next split's
    render task as we would like.
*/
static Bool band_render_graph(corecontext_t *context, frame_data_t *frame,
                              const dbbox_t *bbox, dcoord yalloc,
                              intptr_t surface_pass,
                              Bool serialize,
                              task_t **prev_render, task_vector_t *mht,
                              Bool beforeop, task_t *op)
{
  band_data_t *band = NULL ;
  task_group_t *band_group = NULL ;
  Bool result = FALSE ;
  sheet_data_t *sheet ;
  pass_data_t *pass ;
  task_t *render_task = NULL, *compress_task = NULL ;
  hq_atomic_counter_t before ;

  VERIFY_OBJECT(frame, FRAME_DATA_NAME) ;
  sheet = frame->sheet_data ;
  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;
  pass = sheet->pass_data ;
  VERIFY_OBJECT(pass, PASS_DATA_NAME) ;
  VERIFY_OBJECT(pass->page_data, PAGE_DATA_NAME) ;

  /* Create a common args structure for all tasks on this band. */
  if ( (band = band_data_create(frame, bbox, yalloc, surface_pass)) == NULL )
    return FALSE ;

  /* All of the band tasks are put into the band task group, which is a direct
     descendent of the sheet task group. */
  if ( !task_group_create(&band_group, TASK_GROUP_BAND,
                          sheet->task_group, NULL) )
    goto fail ;

#define return DO_NOT_return_goto_fail_INSTEAD!

  /* Convert band group into a nested group join. This will propagate errors
     to the sheet group automatically. If this fails, this task has to join
     the group. */
  if ( !task_group_set_joiner(band_group, NULL, NULL) ) {
    task_group_cancel(band_group, context->error->old_error) ;
    (void)task_group_join(band_group, NULL) ;
    goto fail ;
  }

  /* Task to render the band. */
  HqAtomicIncrement(&band->refcount, before) ;
  HQASSERT(before > 0, "Band data should have previously been referenced") ;
  if ( !task_create(&render_task, &render_task_specialise, context->page,
                    &band_render, band, band_data_cleanup, band_group,
                    pass->region_types == RENDER_REGIONS_BACKDROP
                    ? SW_TRACE_COMPOSITE_BAND
                    : SW_TRACE_RENDER_BAND) ) {
    band_data_cleanup(context, band) ;
    goto fail ;
  }

  if ( serialize ) {
    /* If we're forcing serialisation, insert the render task into the
       output chain early. We're going to use task_replace() to construct the
       dependencies between the tasks in the band, so the outgoing dependency
       from the render task will be eventually transferred to the output
       task of this band. We'll end up with a long dependency chain through
       all of the render/compress/output tasks in turn for all bands. */
    if ( !task_replace(op,
                       beforeop ? render_task : op,
                       beforeop ? op : render_task) )
      goto fail ;
  }

  /* We cannot make the render task ready until the band group is ready, and
     we cannot do that until the output task is inserted in the correct place
     in the task order. We could create the output task first, but it
     probably wouldn't help, because we still won't be allowed to make the
     task ready until the band group is ready. */

  /* Task to compress a band. */
  /** \todo ajcd 2011-09-21: Can we determine ahead of time whether this will
      be needed in the render graph? fCanCompress is read by
      open_pagebuffer_device(), which is rather late. */
  HqAtomicIncrement(&band->refcount, before) ;
  HQASSERT(before > 0, "Band data should have previously been referenced") ;
  if ( !task_create(&compress_task, &render_task_specialise, context->page,
                    &band_compress, band, band_data_cleanup,
                    band_group, SW_TRACE_COMPRESS_BAND) ) {
    band_data_cleanup(context, band) ;
    goto fail ;
  }

  /* Add render task as dependency for compress. Use task_replace() in case
     we're forcing single-threading, it will transfer outgoing dependencies
     from the render task to the compress task and construct a dependency
     between them. The outgoing dependencies will then be transferred to the
     output task in the next step. */
  if ( !task_replace(render_task, render_task, compress_task) )
    goto fail ;

  /* Task to output the band. */
  HqAtomicIncrement(&band->refcount, before) ;
  HQASSERT(before > 0, "Band data should have previously been referenced") ;
  if ( !task_create(&band->output_task, &render_task_specialise, context->page,
                    &band_output, band, band_data_cleanup,
                    band_group, SW_TRACE_OUTPUT_BAND) ) {
    band_data_cleanup(context, band) ;
    goto fail ;
  }

  /* Add compress task as dependency for output. Use task_replace() in case
     we're forcing single-threading, it will transfer outgoing dependencies
     from the compress task to the output task and construct a dependency
     between them. */
  if ( !task_replace(compress_task, compress_task, band->output_task) )
    goto fail ;

  if ( !serialize ) {
    /* If we're multi-threading, insert the output task into the output
       chain. When constructing the initial band graph, we insert before the
       final output, because we don't want to inherit all of the outgoing
       dependencies of the sheet start task. When sub-dividing bands, we
       insert after the existing band, because we do want to inherit outgoing
       dependencies of the current band's output task, and we don't have a
       reference to the next band's output to insert before anyway. When
       forcing single threading, we inserted the render task in the output
       chain early, and transferred the outgoing dependencies along using
       task_replace(). */
    if ( !task_replace(op,
                       beforeop ? band->output_task : op,
                       beforeop ? op : band->output_task) )
      goto fail ;
  }

  /* Now set up the other dependencies for rendering and compression. We
     delayed them until this point to make the task_replace() usage easy to
     understand. */

  /* If we have a predecessor task for the render task, add a dependency to
     it and replace the predecessor with this render task. This is used to
     set up a chain of render to render dependencies for MHT latency and for
     multiple surface passes, so we don't have two render calls accessing the
     same band buffer simultaneously. */
  if ( prev_render != NULL ) {
    if ( *prev_render != NULL ) {
      if ( !task_depends(*prev_render, render_task) )
        goto fail ;
      task_release(prev_render) ;
    }
    *prev_render = task_acquire(render_task) ;
  }

  /* We need to delay rendering of the band until after the frame is started,
     and we need to wait until all render tasks are done before ending frame
     rendering successfully. */
  if ( !task_depends(frame->render_start_task, render_task) ||
       !task_depends(render_task, frame->render_end_task) )
    goto fail ;

  if ( mht != NULL ) {
    unsigned int i = task_vector_length(mht) ;
    task_t *gate ;

    HQASSERT(i > 0, "MHT gate task vector length is zero") ;

    /* Create a pure synchronisation MHT gate task. This task delays
       compression of the band until after all of the render tasks that need
       to run to handle the MHT latency. The task is made ready when we have
       constructed all of the band groups for those render tasks. */
    if ( !task_create(&gate, &render_task_specialise, context->page,
                      NULL /*no worker*/, NULL /*no args*/, NULL /*no finaliser*/,
                      band_group, SW_TRACE_MHT_GATE) )
      goto fail ;

    /* The new gate is a precursor of the compress task. */
    if ( !task_depends(gate, compress_task) ) {
      task_release(&gate) ;
      goto fail ;
    }

    /* Shuffle down the gate array, pushing the new gate into the last slot.
       The render task is made a precursor to all previous MHT gates. */
    do {
      task_t *prev = mht->element[--i].task ;
      mht->element[i].task = gate ;
      gate = prev ;
      if ( gate != NULL && !task_depends(render_task, gate) ) {
        task_release(&gate) ;
        goto fail ;
      }
    } while ( i > 0 ) ;

    /* The final gate that we shuffled out can have no more precursors, so
       we can make it ready before releasing the reference. */
    if ( gate != NULL ) {
      task_ready(gate) ;
      task_release(&gate) ;
    }
  }

  /* Close the band group, and make the tasks ready. */
  task_group_close(band_group) ;
  task_ready(render_task) ;
  task_ready(compress_task) ;
  task_ready(band->output_task) ;

  result = TRUE ;

 fail:
  if ( band_group != NULL ) {
    /* Most construction failures will have cancelled the band group anyway,
       so this is belt and braces just in case they didn't. If it's not been
       cancelled already, it will at least be nested join group, so we don't
       need to call task_group_join(). */
    if ( !result )
      task_group_cancel(band_group, context->error->old_error) ;
    task_group_release(&band_group) ;
  }

  if ( render_task != NULL )
    task_release(&render_task) ;

  if ( compress_task != NULL )
    task_release(&compress_task) ;

  /* Clean up our own reference to this band. */
  band_data_cleanup(context, band) ;

#undef return
  return result ;
}

/** Construct the task graph for rendering a frame. */
static Bool frame_render_graph(corecontext_t *context, render_graph_t *graph,
                               frame_data_t *frame)
{
  sheet_data_t *sheet ;
  pass_data_t *pass ;

  VERIFY_OBJECT(graph, RENDER_GRAPH_NAME) ;
  VERIFY_OBJECT(frame, FRAME_DATA_NAME) ;
  sheet = frame->sheet_data ;
  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;
  pass = sheet->pass_data ;
  VERIFY_OBJECT(pass, PASS_DATA_NAME) ;

  /** \todo ajcd 2010-12-18: Why do we delay trim checking to here? We've set
      up the render start/end for the sheet already, they won't do anything
      useful without bands. */
  if (pass->trim_endband >= pass->trim_startband) {
    bandnum_t bandnum ;
    hq_atomic_counter_t before ;

    VERIFY_OBJECT(pass->page_data, PAGE_DATA_NAME) ;

    HqAtomicIncrement(&frame->refcount, before) ;
    HQASSERT(before > 0, "Frame data should have previously been referenced") ;
    if ( !task_create(&frame->render_start_task,
                      &render_task_specialise, graph->page,
                      &frame_render_start, frame, frame_data_cleanup,
                      sheet->task_group, SW_TRACE_RENDER_FRAME_START) ) {
      frame_data_cleanup(context, frame) ;
      return FALSE ;
    }

    /* Insert the frame render start task just before the sheet render end. */
    if ( !task_replace(graph->last_render, frame->render_start_task, graph->last_render) )
      return FALSE ;

    task_ready(frame->render_start_task) ;

    /* Construct frame render end task before creating band graph, but don't
       insert it in place or make it ready until band graph is complete. */
    HqAtomicIncrement(&frame->refcount, before) ;
    HQASSERT(before > 0, "Frame data should have previously been referenced") ;
    if ( !task_create(&frame->render_end_task,
                      &render_task_specialise, graph->page,
                      &frame_render_done, frame, &frame_data_cleanup,
                      sheet->task_group, SW_TRACE_RENDER_FRAME_DONE) ) {
      frame_data_cleanup(context, frame) ;
      return FALSE ;
    }

    /* Iterate over bands, constructing task graph for each band, making it
       ready as soon as possible. */
    for ( bandnum = pass->trim_startband ;
          bandnum <= pass->trim_endband ;
          ++bandnum ) {
      dbbox_t bbox ;

      band_extent(graph->page, frame->nFrameNumber, bandnum, &bbox) ;
      if ( !band_render_graph(context, frame, &bbox, bbox.y2 - bbox.y1 + 1,
                              0 /*initial surface pass*/,
                              FALSE /*Don't serialize bands*/,
                              (graph->mht || pass->serialize_mht)
                              ? &graph->prev_render : NULL,
                              graph->mht /*MHT gates*/,
                              TRUE /*output before*/, graph->last_output) )
        return FALSE ;
    }

    /* Insert the frame render end task just before the sheet render end. */
    if ( !task_replace(graph->last_render, frame->render_end_task, graph->last_render) )
      return FALSE ;

    task_ready(frame->render_end_task) ;
  }

  return TRUE ;
}

/** \brief Construct a task graph for a sheet.

    Where possible, we'll make the tasks we're creating ready immediately, so
    they can run in parallel with the graph creation. */
static Bool sheet_render_graph(corecontext_t *context, render_graph_t *graph,
                               sheet_data_t *sheet)
{
  GUCR_CHANNEL *hf ;
  int32 nFrameNumber ;
  task_t *task ;

  VERIFY_OBJECT(graph, RENDER_GRAPH_NAME) ;
  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;

  /* Create the sheet start and end render and output tasks, and link them
     up together. We'll use task_replace() to insert render and output tasks
     in between these. */

  if ( !task_create(&graph->last_render, &render_task_specialise, graph->page,
                    &sheet_render_done, sheet, NULL /*no finaliser*/,
                    sheet->task_group, SW_TRACE_SHEET_RENDER_DONE) )
    return FALSE ;

  if ( !task_create(&graph->last_output, &render_task_specialise, graph->page,
                    &sheet_output_done, sheet, NULL /*no finaliser*/,
                    sheet->task_group, SW_TRACE_SHEET_OUTPUT_DONE) )
    return FALSE ;

  if ( !task_create(&task, &render_task_specialise, graph->page,
                    &sheet_done, sheet, NULL /*no finaliser*/,
                    sheet->task_group, SW_TRACE_SHEET_DONE) )
    return FALSE ;

  /* Link render done and output done to sheet done. This link is only
     necessary to propagate success state for the entire sheet. */
  if ( !task_depends(graph->last_render, task) ||
       !task_depends(graph->last_output, task) ) {
    task_release(&task) ;
    return FALSE ;
  }

  task_ready(task) ;
  task_release(&task) ;

  /* Now construct the sheet start task, combining render and output start
     into one (because we need the PGB device open for readback when
     rendering anyway). */
  if ( !task_create(&task, &render_task_specialise, graph->page,
                    &sheet_start, sheet, NULL /*no finaliser*/,
                    sheet->task_group, SW_TRACE_SHEET_START) )
    return FALSE ;

  if ( !task_depends(task, graph->last_render) ||
       !task_depends(task, graph->last_output) ) {
    task_release(&task) ;
    return FALSE ;
  }

  task_ready(task) ;
  task_release(&task) ;

  /* Loop over all of sheet's frames, constructing graphs for each of them. */
  for ( hf = sheet->hf, nFrameNumber = 0 ;
        gucr_framesMore(hf) ;
        gucr_framesNext(&hf), ++nFrameNumber ) {
    frame_data_t *frame ;
    task_vector_t *mht ;
    Bool ok ;

    if ( (frame = frame_data_create(sheet, hf, nFrameNumber)) == NULL )
      return FALSE ;

    ok = frame_render_graph(context, graph, frame) ;
    frame_data_cleanup(context, frame) ;

    /* Make any remaining MHT tasks ready, and release them. */
    if ( (mht = graph->mht) != NULL ) {
      unsigned int i = task_vector_length(mht) ;
      HQASSERT(i > 0, "MHT gate task vector length is zero") ;
      do {
        task_t *task ;
        if ( (task = mht->element[--i].task) != NULL ) {
          task_ready(task) ;
          task_release(&mht->element[i].task) ;
        }
      } while ( i > 0 ) ;
    }

    if ( !ok )
      return FALSE ;

    if ( gucr_framesEndOfSheet(hf) )
      break ;
  }

  /* The sheet done task has all its dependencies set up. */
  task_ready(graph->last_render) ;
  task_ready(graph->last_output) ;

  return TRUE ;
}

/** Construct the task graph for reoutputting a band. */
static Bool band_reoutput_graph(corecontext_t *context, frame_data_t *frame,
                                const dbbox_t *bbox, dcoord yalloc,
                                task_t *op)
{
  band_data_t *band = NULL ;
  task_group_t *band_group = NULL ;
  Bool result = FALSE ;
  sheet_data_t *sheet ;
  pass_data_t *pass ;
  task_t *readback_task = NULL ;
  hq_atomic_counter_t before ;

  VERIFY_OBJECT(frame, FRAME_DATA_NAME) ;
  sheet = frame->sheet_data ;
  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;
  pass = sheet->pass_data ;
  VERIFY_OBJECT(pass, PASS_DATA_NAME) ;
  VERIFY_OBJECT(pass->page_data, PAGE_DATA_NAME) ;

  /* Create a common args structure for all tasks on this band. */
  if ( (band = band_data_create(frame, bbox, yalloc, 0)) == NULL )
    return FALSE ;

  /* All of the band tasks are put into the band task group, which is a direct
     descendent of the sheet task group. */
  if ( !task_group_create(&band_group, TASK_GROUP_BAND,
                          sheet->task_group, NULL) )
    goto fail ;

#define return DO_NOT_return_goto_fail_INSTEAD!

  /* Convert band group into a nested group join. This will propagate errors
     to the sheet group automatically. If this fails, this task has to join
     the group. */
  if ( !task_group_set_joiner(band_group, NULL, NULL) ) {
    task_group_cancel(band_group, context->error->old_error) ;
    (void)task_group_join(band_group, NULL) ;
    goto fail ;
  }

  /* Task to read back the band. */
  HqAtomicIncrement(&band->refcount, before) ;
  HQASSERT(before > 0, "Band data should have previously been referenced") ;
  if ( !task_create(&readback_task, &render_task_specialise, context->page,
                    &band_readback, band, band_data_cleanup,
                    band_group, SW_TRACE_READBACK_BAND) ) {
    band_data_cleanup(context, band) ;
    goto fail ;
  }

  /* We need to delay reading back the band until after the pagebuffer is
     opened. */
  if ( !task_depends(frame->render_start_task, readback_task) )
    goto fail ;

  /** \todo ajcd 2012-09-05: Single-threaded operation uses the output task
      order to provision band groups. There is no need to explicitly force
      inter-band output to render dependencies. */

  /* Task to output the band. */
  HqAtomicIncrement(&band->refcount, before) ;
  HQASSERT(before > 0, "Band data should have previously been referenced") ;
  if ( !task_create(&band->output_task, &render_task_specialise, context->page,
                    &band_output, band, band_data_cleanup,
                    band_group, SW_TRACE_OUTPUT_BAND) ) {
    band_data_cleanup(context, band) ;
    goto fail ;
  }

  /* Add readback task as dependency for output. */
  if ( !task_depends(readback_task, band->output_task) )
    goto fail ;

  /* Insert the output task into the output chain. When constructing the
     initial band graph, we insert before the final output. */
  if ( !task_replace(op, band->output_task, op) )
    goto fail ;

  /* Close the band group, and make the tasks ready. */
  task_group_close(band_group) ;
  task_ready(readback_task) ;
  task_ready(band->output_task) ;

  result = TRUE ;

 fail:
  if ( band_group != NULL ) {
    /* Most construction failures will have cancelled the band group anyway,
       so this is belt and braces just in case they didn't. If it's not been
       cancelled already, it will at least be nested join group, so we don't
       need to call task_group_join(). */
    if ( !result )
      task_group_cancel(band_group, context->error->old_error) ;
    task_group_release(&band_group) ;
  }

  if ( readback_task != NULL )
    task_release(&readback_task) ;

  /* Clean up our own reference to this band. */
  band_data_cleanup(context, band) ;

#undef return
  return result ;
}

static Bool frame_reoutput_graph(corecontext_t *context, render_graph_t *graph,
                                 frame_data_t *frame)
{
  sheet_data_t *sheet ;
  pass_data_t *pass ;

  VERIFY_OBJECT(graph, RENDER_GRAPH_NAME) ;
  VERIFY_OBJECT(frame, FRAME_DATA_NAME) ;
  sheet = frame->sheet_data ;
  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;
  pass = sheet->pass_data ;
  VERIFY_OBJECT(pass, PASS_DATA_NAME) ;

  /** \todo ajcd 2010-12-18: Why do we delay trim checking to here? We've set
      up the render start/end for the sheet already, they won't do anything
      useful without bands. */
  if (pass->trim_endband >= pass->trim_startband) {
    bandnum_t bandnum ;

    VERIFY_OBJECT(pass->page_data, PAGE_DATA_NAME) ;

    /* Iterate over bands, constructing task graph for each band, making it
       ready as soon as possible. */
    for ( bandnum = pass->trim_startband ;
          bandnum <= pass->trim_endband ;
          ++bandnum ) {
      dbbox_t bbox ;

      band_extent(graph->page, frame->nFrameNumber, bandnum, &bbox) ;
      if ( !band_reoutput_graph(context, frame, &bbox, bbox.y2 - bbox.y1 + 1,
                                graph->last_output) )
        return FALSE ;
    }
  }

  return TRUE ;
}


/** Following a printer "UPSET" state requesting bands to be re-written
 * through the page buffer device, this function does just that. Each band is
 * first read from and then written back to the page buffer device. Why? This
 * is the best we can do while pretending to interface to a file device. We
 * really just want the PGB device to output the bands from its band storage
 * to the actual output device.
 *
 * \param context Context for thread constructing graph.
 * \param graph  Graph construction data
 * \param sheet  The sheet data to output
 *
 * \retval TRUE if graph was constructed ok
 * \retval FALSE otherwise
 */
static Bool sheet_reoutputpgb_graph(corecontext_t *context,
                                    render_graph_t *graph, sheet_data_t *sheet)
{
  GUCR_CHANNEL *hf ;
  int32 nFrameNumber ;
  task_t *task = NULL ;
  Bool result = FALSE ;

  VERIFY_OBJECT(graph, RENDER_GRAPH_NAME) ;
  VERIFY_OBJECT(sheet, SHEET_DATA_NAME) ;

  /* Create the sheet start and end output tasks, and link them up together.
     We'll use task_replace() to insert output tasks in between these. */

  if ( !task_create(&graph->last_output, &render_task_specialise, graph->page,
                    &sheet_output_done, sheet, NULL /*no finaliser*/,
                    sheet->task_group, SW_TRACE_SHEET_OUTPUT_DONE) )
    return FALSE ;

  if ( !task_create(&task, &render_task_specialise, graph->page,
                    &sheet_done, sheet, NULL /*no finaliser*/,
                    sheet->task_group, SW_TRACE_SHEET_DONE) )
    return FALSE ;

#define return DO_NOT_return_GOTO_fail_INSTEAD!
  /* Link render done and output done to sheet done. This link is only
     necessary to propagate success state for the entire sheet. */
  if ( !task_depends(graph->last_output, task) )
    goto fail ;

  task_ready(task) ;
  task_release(&task) ;

  /* Now construct the sheet start task, and retain a reference because we
     need the PGB device open for readback anyway. */
  if ( !task_create(&task, &render_task_specialise, graph->page,
                    &sheet_reoutput_start, sheet, NULL /*no finaliser*/,
                    sheet->task_group, SW_TRACE_SHEET_START) )
    goto fail ;

  if ( !task_depends(task, graph->last_output) )
    goto fail ;

  task_ready(task) ;

  /* Loop over all of sheet's frames, constructing graphs for each of them. */
  for ( hf = sheet->hf, nFrameNumber = 0 ;
        gucr_framesMore(hf) ;
        gucr_framesNext(&hf), ++nFrameNumber ) {
    frame_data_t *frame ;
    Bool ok ;

    if ( (frame = frame_data_create(sheet, hf, nFrameNumber)) == NULL )
      goto fail ;

    /* All of the readback tasks depend on sheet start, so the PGB is open. */
    frame->render_start_task = task_acquire(task) ;

    ok = frame_reoutput_graph(context, graph, frame) ;
    frame_data_cleanup(context, frame) ;

    if ( !ok )
      goto fail ;

    /* Just finished last frame of this sheet. */
    if ( gucr_framesEndOfSheet(hf) )
      break ;
  }

  /* The sheet done task has all its dependencies set up. */
  task_ready(graph->last_output) ;

  result = TRUE ;

 fail:
  if ( task != NULL )
    task_release(&task) ;

#undef return
  return result ;
}

/** \brief Generate task(s) for rendering a sheet.

    This function is reponsible for constructing a sub-graph for
    the frames/bands in the sheet, executing it, and monitoring the result.

    The reason a separate sub-graph is constructed and run by this function
    is to handle re-output. If an output task indicates that the sheet should
    be re-output, the sheet monitor is responsible for cancelling any
    remaining sheet tasks and constructing a new graph to perform the
    re-output. The sheet monitor will only signal sheet completion when the
    re-output succeeds. */
static Bool render_tasks_sheet(corecontext_t *context, pass_data_t *pass,
                               GUCR_CHANNEL *hf, int32 colorants_per_band,
                               int32 nSheetNumber)
{
  error_context_t errcontext = ERROR_CONTEXT_INIT ;
  enum printerstate_e printerstate ;
  DL_STATE *page = context->page ;
  resource_requirement_t *req = page->render_resources ;

  VERIFY_OBJECT(pass, PASS_DATA_NAME) ;
  VERIFY_OBJECT(pass->page_data, PAGE_DATA_NAME) ;

  do {
    sheet_data_t *sheet ;
    hq_atomic_counter_t before ;
    Bool result = FALSE ;
    task_group_t *render_tasks ;

    /* Create sheet data for this sheet. */
    if ( (sheet = mm_alloc(mm_pool_temp, sizeof(sheet_data_t),
                           MM_ALLOC_CLASS_SHEET_DATA)) == NULL ) {
      return error_handler(VMERROR) ;
    }

    /* Reset printer state for each iteration to abort unless we complete
       rendering successfully. */
    printerstate = SHEET_ABORT ;

    sheet->colorants_per_band = colorants_per_band +
      (page->output_object_map ? 1 : 0) ;
    sheet->nSheetNumber = nSheetNumber ;
    sheet->fCanCompress = FALSE;
    sheet->hf = hf ;
    sheet->fUnloadChForms = FALSE ;
    sheet->fScreensExported = FALSE ;
    sheet->printerstate = &printerstate ;
    sheet->surface_started = FALSE ;
    sheet->surface_handle.sheet = NULL ;
    sheet->pgbfd = -1 ;
    sheet->band_pool = resource_requirement_get_pool(req, TASK_RESOURCE_BAND_OUT) ;
    HqAtomicIncrement(&pass->refcount, before) ;
    HQASSERT(before > 0, "Pass data should have previously been referenced") ;
    sheet->pass_data = pass ;
    sheet->task_group = NULL ;
    NAME_OBJECT(sheet, SHEET_DATA_NAME) ;

    /* We need to clear the error context every time around the loop, to
       clear the NOT_AN_ERROR that is signalled if a resend is requested. */
    error_clear_context(&errcontext) ;

    /* Use task_group_current() to find the page group dynamically, it's not
       stored anywhere convenient. We must release this reference to the
       render group. */
    render_tasks = task_group_current(TASK_GROUP_RENDER) ;

    /* Create data for construction of a graph. */
    /* The sheet group is joined by render_all_frames_of_dl(). We use reference
       counting to clean up the sheet data after all sheet tasks are done. It
       would be possible to re-structure the sheet using groups so that the
       sheet done was the join task for sheet tasks, and could clean up its
       data, but that's more of a step that I care to make for the HMR3 beta. */
    HQASSERT(sheet->task_group == NULL, "Already have a sheet group") ;
    if ( task_group_create(&sheet->task_group, TASK_GROUP_SHEET,
                           render_tasks, NULL) ) {
      render_graph_t graph ;

      graph.mht = NULL ;
      graph.prev_render = NULL ;
      graph.last_render = NULL ;
      graph.last_output = NULL ;
      graph.page = page ;
      NAME_OBJECT(&graph, RENDER_GRAPH_NAME) ;

      /* The sheet group is joined by this task, so it can be provisioned
         immediately. */
      task_group_ready(sheet->task_group) ;

      if ( pass->mht_max_latency == 0 ||
           task_vector_create(&graph.mht, pass->mht_max_latency) ) {
        /* Build the sheet graph. */
        if ( printerstate != SHEET_REOUTPUTPGB ) {
          result = sheet_render_graph(context, &graph, sheet) ;
        } else {
          result = sheet_reoutputpgb_graph(context, &graph, sheet) ;
        }

#ifdef DEBUG_BUILD
        if ( (debug_render & DEBUG_RENDER_GRAPH_TASKS) != 0 )
          debug_graph_tasks() ;
#endif

        /* Destroy the task vector used to track band task dependencies. */
        if ( graph.mht != NULL )
          task_vector_release(&graph.mht) ;
        if ( graph.prev_render )
          task_release(&graph.prev_render) ;
        if ( graph.last_render )
          task_release(&graph.last_render) ;
        if ( graph.last_output )
          task_release(&graph.last_output) ;
      }

      UNNAME_OBJECT(&graph) ;

      /* If we failed to build the graph successfully, cancel any
         remaining tasks. */
      if ( result ) {
        task_group_close(sheet->task_group) ;
      } else {
        task_group_cancel(sheet->task_group, context->error->old_error) ;
      }

      /* Wait for all graph tasks to actually complete. This also allows the
         sheet output done to signal failure by cancelling the group. Also
         capture the error into a local error context which we can later
         propagate to this task's context if we decide to abort the sheet. */
      (void)task_group_join(sheet->task_group, &errcontext) ;
    }
    task_group_release(&render_tasks) ;

    /** \todo ajcd 2011-02-28: Sheet object cleanup will become
        sheet_join_and_cleanup when sheet finaliser is turned into a
        continuation function. For now, we can explicitly destroy the sheet
        data because we've joined the sheet's tasks above. */
    sheet_data_cleanup(context, sheet) ;

    /* Was an error signalled in *this* task, or was it in running the graph?
       If the latter, we can try again. */
    if ( context->error->old_error > 0 )
      return FALSE ;
  } while ( printerstate == SHEET_REOUTPUT ||
            printerstate == SHEET_REOUTPUTPGB ) ;

  if ( printerstate == SHEET_ABORT ) {
    /* Propagate the error that we captured from the task. */
    /** \todo ajcd 2011-02-10: Handle detail error propagation here. */
    HQASSERT(errcontext.old_error > 0, "No real error from sheet abort") ;
    return error_handler(errcontext.old_error) ;
  }

  /* Either SHEET_OK or SHEET_CANCEL are success results. */
  HQASSERT(printerstate == SHEET_OK || printerstate == SHEET_CANCEL,
           "Printer not in expected state") ;

  return TRUE ;
}

/*****************************************************************************/

Backdrop *render_state_backdrop(const render_state_t* rs)
{
  HQASSERT(rs != NULL, "rs is null");
  return rs->backdrop;
}

void init_forms(render_forms_t *forms, DL_STATE *page, Bool doing_mht)
{
  forms->retainedform.type = FORMTYPE_BANDBITMAP;
  forms->retainedform.w = page->page_w;
  forms->retainedform.h = page->page_h;
  forms->retainedform.l = CAST_SIZET_TO_INT32(page->band_l);
  forms->retainedform.rh = page->band_lines;
  forms->retainedform.size = forms->retainedform.l * forms->retainedform.rh;
  forms->retainedform.addr = NULL;
  if ( doing_mht ) {
    forms->contoneform = forms->retainedform;
    forms->contoneform.l = CAST_SIZET_TO_INT32(page->band_lc);
    forms->contoneform.size = forms->contoneform.l * forms->contoneform.rh;
    forms->contoneform.addr = NULL;
    forms->objectmapform = forms->contoneform;
  } else {
    forms->objectmapform = forms->contoneform = forms->retainedform;
  }
  forms->clippingform = forms->retainedform;
  forms->clippingform.l = CAST_SIZET_TO_INT32(page->band_l1);
  forms->clippingform.size = forms->clippingform.l * forms->clippingform.rh;
  forms->clippingform.addr = NULL;
  forms->patternform = forms->patternshapeform = forms->patternclipform
    = forms->maskedimageform = forms->intersectingclipform
    = forms->htmaskform = forms->clippingform;
}


/* Prepare a render info to blit into a mask band. We initialise just enough
   details to let the render functions work. */

/* static blit information used by render_mask code */
static blit_colormap_t mask_blitmap ;
static blit_color_t mask_erase_color ;
static blit_color_t mask_knockout_color ;


void render_state_mask(render_state_t *rs, blit_chain_t *blits,
                       render_forms_t *forms, surface_t *surface,
                       FORM *to)
{
  static Bool dummy_white_on_white = FALSE;

  HQASSERT(rs, "No render state") ;
  HQASSERT(to, "No mask output form") ;
  HQASSERT(surface, "No mask surface") ;
  HQASSERT(!dummy_white_on_white, "White on white optimisation should be disabled") ;
  /* Base blit must be set, others may be set as well. */
  HQASSERT(blits, "Nowhere for mask blits") ;
  HQASSERT(forms, "No forms for mask output form") ;

  RESET_BLITS(blits,
              &surface->baseblits[BLT_CLP_NONE],
              &surface->baseblits[BLT_CLP_RECT],
              &surface->baseblits[BLT_CLP_COMPLEX]) ;

  /* Clipform must be set, because some calculations are performed on the
     form address and length without checking it first. We'll set it to a
     form passed in by the caller, but copy the size of the output form and
     put a NULL address pointer in it. Hopefully any ymaskaddr generated from
     this will be invalid if dereferenced. (Char masks set up a valid address
     in the clipform, which is why we don't just use a bogus static form.) */
  forms->clippingform = *to ;
  theFormA(forms->clippingform) = NULL ;
  /* for safety: */
  forms->clippingbase = NULL ;
  forms->halftonebase = NULL ;
  forms->maxbltbase = NULL ;
  theFormA(forms->retainedform) = theFormA(forms->patternform) =
    theFormA(forms->patternclipform) = theFormA(forms->patternshapeform) =
    theFormA(forms->maskedimageform) = theFormA(forms->intersectingclipform) =
    theFormA(forms->contoneform) = theFormA(forms->objectmapform) =
    theFormA(forms->htmaskform) = NULL ;

#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
  HqMemSet8((uint8 *)rs, 0xff, sizeof(rs)) ;
#endif

  rs->page = CoreContext.page;
  rs->forms = forms;
  rs->hr = NULL ;
  rs->hf = NULL ;
  rs->nChannel = 0 ;
  rs->lobjErase = NULL ;
  rs->composite_context = NULL ;
  rs->backdrop = NULL ;
  rs->band = DL_LASTBAND;
  rs->pass_region_types = RENDER_REGIONS_NONE;
  rs->fPixelInterleaved = FALSE ;
  rs->maxmode = BLT_MAX_NONE;
  rs->fMergePatterns = FALSE;
  rs->dopatterns = FALSE;
  rs->htm_info = NULL ;
  rs->surface_handle.page = NULL ;
  dlc_clear(&rs->dlc_watermark) ;
  rs->spotno_watermark = SPOT_NO_INVALID ;
  rs->trap_backdrop_context = NULL ;

  rs->cs.overprint_mask = NULL ;
  rs->cs.maxblit_mask = NULL ;
  rs->cs.blitmap = &mask_blitmap ;
  rs->cs.erase_color = &mask_erase_color ;
  rs->cs.knockout_color = &mask_knockout_color ;
  rs->cs.renderTracker = NULL ;
  rs->cs.hc = NULL ;
  rs->cs.p_white_on_white = &dummy_white_on_white;
  bbox_store(&rs->cs.bandlimits, 0, theFormHOff(*to),
             theFormW(*to) - 1, theFormHOff(*to) + theFormRH(*to) - 1) ;
  rs->ri.surface = surface ;
  rs->cs.fIsHalftone = surface->screened ;
  rs->cs.bg_separation = FALSE ;
  rs->cs.fSelfIntersect = FALSE ;
  rs->cs.selected_mht = NULL ;
  rs->cs.selected_mht_is_erase = FALSE ;

  rs->ri.p_rs = rs ;
  rs->ri.lobj = NULL;
  rs->ri.clip = rs->ri.bounds = rs->cs.bandlimits;
  rs->ri.region_set_type = RENDER_REGIONS_DIRECT;
  rs->ri.overrideColorType = GSC_UNDEFINED ;
  rs->ri.fSoftMaskInPattern = FALSE ;
  rs->ri.generate_object_map = FALSE;
  rs->ri.ht_params = NULL;
  rs->ri.group_tracker = NULL ;
  rs->ri.pattern_state = PATTERN_OFF;

  rs->ri.rb.p_ri = &rs->ri ;
  rs->ri.rb.clipmode = BLT_CLP_RECT ;
  rs->ri.rb.depth_shift = 0; /* can be removed once the users do surface->prepare */
  rs->ri.rb.outputform = to ;
  rs->ri.rb.ylineaddr = NULL ;
  rs->ri.rb.clipform = &forms->clippingform ;
  rs->ri.rb.ymaskaddr = NULL ;
  rs->ri.rb.x_sep_position = 0 ;
  rs->ri.rb.y_sep_position = 0 ;
  rs->ri.rb.p_painting_pattern = NULL;
  rs->ri.rb.color = rs->cs.erase_color ;
  rs->ri.rb.blits = blits ;

  HQASSERT(RENDER_STATE_CONSISTENT(rs), "New mask state inconsistent") ;
}


/** Loop through all frames for rendering.  A "frame" is effectively either a
  separation (separate PGB files) or an entire page of a single colorant in
  frame interleaved mode in which all frames (i.e. all colorants) end up in
  the one PGB.  However, in the sense used here of "looping through all frames",
  it means looping over all sheets & channels for either separations and frame
  i/l mode, while for band and pixel i/l modes it simply means doing the one
  sheet (as there will only be one GUCR_CHANNEL per GUCR_SHEET).
*/
static Bool render_all_frames_of_dl(corecontext_t *context,
                                    page_data_t *page_data, int paint_type)
{
  Bool retcode;

  VERIFY_OBJECT(page_data, PAGE_DATA_NAME) ;

  /** \todo ajcd 2011-02-28: Why do we leave NeverRender processing quite
      this late? */
  retcode = page_data->never_render;
  if ( !retcode ) {    /* Only bother if actual need to render */
    DL_STATE *page = context->page ;
    GUCR_RASTERSTYLE *hr = page->hr ;
    GUCR_CHANNEL *hf ;
    const surface_set_t *surfaces ;
    const surface_t *surface ;
    pass_data_t *pass ;
    hq_atomic_counter_t before ;

    if ( (pass = mm_alloc(mm_pool_temp, sizeof(*pass),
                          MM_ALLOC_CLASS_PASS_DATA)) == NULL )
      return error_handler(VMERROR) ;

    HqMemZero(pass, sizeof(pass_data_t)) ;

#define return DO_NOT_RETURN-goto_cleanup_instead!
    /* Extract output surface */
    surfaces = page->surfaces ;
    HQASSERT(surfaces != NULL, "No selected surface set") ;

    surface = surface_find(surfaces, SURFACE_OUTPUT) ;
    HQASSERT(surface != NULL, "No output surface") ;

    /* Capture page data */
    pass->refcount = 1 ; /* Refcount set so this is never released. */
    pass->paint_type = paint_type ;
    pass->surface_handle = page_data->surface_handle ;

    /* Set type of regions to be rendered according to the 'paint' type */
    switch (paint_type) {
    default:
      HQFAIL( "Invalid paint_type to render_all_frames_of_dl" );
      /* FALL THRU */
    case PAINT_TYPE_FINAL:
    case PAINT_TYPE_PARTIAL:
      pass->region_types =
        page_data->transparency_strategy == 1
        ? RENDER_REGIONS_DIRECT | RENDER_REGIONS_BACKDROP
        : RENDER_REGIONS_DIRECT;
      break;
    case PAINT_TYPE_COMPOSITE:
      pass->region_types = RENDER_REGIONS_BACKDROP;
      break;
    }

    /* Determine if we can use multiple threads for this page. Must be done
       before init_page_render(). */
    pass->allowMultipleThreads = (NUM_THREADS() > 1 && !sep_imposition(page)) ;

    /* If single-threading, might as well get skin to do it, as it's more
       efficient. */
    pass->core_might_compress = (!DOING_RUNLENGTH(page) &&
                                 page_data->compress_bands >= 0 &&
                                 pass->allowMultipleThreads) ;

    /* Defer setting up MHT info until pass data is named, because cleanup
       action requires it to be complete. */
    pass->mht_info = NULL ;
    pass->mht_max_latency = 0 ;
    /* ditto for im_expbufs */
    pass->im_expbufs = NULL ;

    /* These set properly in init_page_render(): */
    pass->writealloutput = FALSE; pass->screen_all_bands = FALSE;
    pass->serialize_mht = FALSE;

    HqAtomicIncrement(&page_data->refcount, before) ;
    pass->page_data = page_data ;
    HQASSERT(before > 0, "Page data should have previously been referenced") ;

    NAME_OBJECT(pass, PASS_DATA_NAME) ;

    /* init_page_render() needs pass_region_types. calculateTrim() must come
       after init_page_render() to use writealloutput. If we abort,
       render_sheet_shutdown() needs hr, and fUnloadChForms. */
    if ( !init_page_render(page, pass) )
      goto cleanup ;

    calculateTrim(page, pass) ;

    /* Set up page-specific MHT information and pass it to the screening
       modules. */
    if ( surface->screened ) {
      MODHTONE_REF *mhtone = htm_first_halftone_ref(page->eraseno);
      unsigned int max_latency ;

      if ( mhtone != NULL ) {
        /* set up modular render info */
        sw_htm_result htmres;
        sw_htm_render_info *mod_info;

        if ( (mod_info = mm_alloc(mm_pool_temp, sizeof(sw_htm_render_info),
                                  MM_ALLOC_CLASS_HTM_RENDER_INFO)) == NULL ) {
          (void)error_handler(VMERROR) ;
          goto cleanup ;
        }

        mod_info->width = page->page_w;
        mod_info->height = page->page_h;
        mod_info->src_bit_depth = htm_SrcBitDepth(mhtone);
        mod_info->dst_bit_depth = gucr_rasterDepth(hr);
        mod_info->msk_linebytes = (int)page->band_l1 ;
        mod_info->src_linebytes = (int)page->band_lc ;
        mod_info->dst_linebytes = (int)page->band_l ;
        mod_info->partial_paint = (paint_type != PAINT_TYPE_FINAL);
        mod_info->max_render_threads =
          !pass->allowMultipleThreads ? 1 : max_simultaneous_tasks();
        mod_info->job_number = page->job_number;
        mod_info->page_number = page->pageno;

        pass->mht_info = mod_info;

        htmres = htm_RenderInitiation(mod_info, FALSE, page->eraseno);
        if ( htmres != SW_HTM_SUCCESS ) {
          (void)error_handler(htm_MapHtmResultToSwError(htmres));
          goto cleanup ;
        }
      }

      for ( max_latency = 0 ; mhtone != NULL ;
            mhtone = htm_next_halftone_ref(page->eraseno, mhtone) )
        pass->mht_max_latency = max(pass->mht_max_latency, htm_Latency(mhtone));
    }

    pass->im_expbufs = im_createexpbuf(page, pass->allowMultipleThreads);
    if ( pass->im_expbufs == NULL )
      goto cleanup ;

    for ( hf = gucr_framesStart(hr) ;
          gucr_framesMore(hf) ;
          gucr_framesNext(&hf) ) {
      int32 colorants_per_band, nSheetNumber ;

      if ( gucr_framesStartOfSheet(hf, &colorants_per_band, &nSheetNumber) ) {
        if ( !render_tasks_sheet(context, pass, hf, colorants_per_band,
                                 nSheetNumber) )
          goto cleanup ;
      }
    }

    HQASSERT(!retcode, "Should still be returing FALSE here") ;
    retcode = TRUE ;

    if ( paint_type == PAINT_TYPE_FINAL )
      ++page->job->pages_output;

    if ( pass->mht_info != NULL ) {
      htm_RenderCompletion(pass->mht_info, page->eraseno, FALSE /*not aborting*/);
      mm_free(mm_pool_temp, pass->mht_info, sizeof(sw_htm_render_info)) ;
      pass->mht_info = NULL ;
    }

  cleanup:
    pass_data_cleanup(context, pass) ;
  }

#undef return
  return retcode;
}

/*****************************************************************************/

/** If the job contains/ regions which require compositing (eg, transparency or
   recombine) rendering is split into two passes.  The first pass renders only
   those regions requiring compositing (backdrop regions), the second pass for
   the other regions using the standard opaque painting model (direct regions).
   Two pass rendering is handled by the partial painting mechanism.  On the
   second rendering pass, to render the direct regions, the bands are read-back
   before rendering the direct regions.  Thus, the whole thing works in a
   similar way to partial painting with the difference that the display list is
   not changed in between the two paints.  In between the two passes, color
   conversion to final device codes and separation omission can be performed.
   Sep. omission can't be performed before compositing due to the way final
   colors are not known until the compositing has been completed.  Don't scan
   the display list for separation omission before the partial render, because
   it will not all be in device space.  The compositing partial paint will set
   separation omission flags for the colorants used in backdrop regions.
 */
static Bool composite_bands(corecontext_t *context, page_data_t *page_data)
{
  Bool ok;
  LISTOBJECT *eraselobj ;
  DL_STATE *page = context->page ;

  VERIFY_OBJECT(page_data, PAGE_DATA_NAME) ;

  eraselobj = dlref_lobj(dl_get_orderlist(page));
  HQASSERT(page->regionMap != NULL &&
           bitGridGetAll(page->regionMap) != BGAllClear,
           "No compositing required");
  HQASSERT(page_data->transparency_strategy == 2,
           "Single-pass compositing shouldn't call composite_bands");

  CHECK_TL_VALID(timeline_push(&page->timeline, SWTLT_COMPOSITE_PAGE,
                               0 /*end*/, SW_TL_UNIT_NONE, page, NULL, 0));

  PROBE(SW_TRACE_COMPOSITE, (intptr_t)page->eraseno,
        ok = (omit_backdrop_prepare(page) &&
              init_partial_render(page) &&
              render_all_frames_of_dl(context, page_data, PAINT_TYPE_COMPOSITE)));

  CHECK_TL_VALID(timeline_pop(&page->timeline, SWTLT_COMPOSITE_PAGE, ok)) ;

  if ( !ok )
    return FALSE;

  bd_sharedFree(&page->backdropShared);

  /* Mark the erase object so that the partial paint mechanism knows it's got to
     do a read-back as and when the final rendering pass is performed. */
  HQASSERT(eraselobj->opcode == RENDER_erase, "Expected erase object");
  eraselobj->dldata.erase.newpage = 0;

  /* The 'rippedtodisk' flag used to just mean that a partial paint has
     occurred.  While this is not exactly synonymous with having done a
     composite-render, it's as good as for this flag. */
  /** \todo ajcd 2011-03-18: What about page->rippedsomethingsignificant? */
  page->rippedtodisk = TRUE;

  spotlist_trace(page);

  return TRUE;
}

static Bool render_all_passes_of_page(corecontext_t *context, void *args) ;

/** \brief Spawn the render passes for a display list. */
static Bool spawn_all_passes_of_page(corecontext_t *context, int32 numcopies,
                                     int paint_type)
{
  page_data_t *page_data ;
  DL_STATE *page = context->page ;
  dl_erase_t spawn_type ;
  Bool ok ;

  HQASSERT(IS_INTERPRETER(), "Not spawning render task from interpreter") ;
  HQASSERT(page->pageno > 0, "Rendering page from config job") ;

  if ( page->erase_type == DL_ERASE_BEGIN ||
       page->erase_type == DL_ERASE_GONE ) {
    int32 errorno = UNREGISTERED;
    corejob_t *job = page->job;

    if ( job != NULL && task_group_is_cancelled(job->task_group) )
      errorno = task_group_error(job->task_group);
    ok = error_handler(errorno);
    dl_clear_page(page);
  } else if ( (page_data = mm_alloc(mm_pool_temp, sizeof(*page_data),
                                    MM_ALLOC_CLASS_PAGE_DATA)) == NULL ) {
    ok = error_handler(VMERROR) ;
    dl_clear_page(page) ;
  } else {
    /* See the comment in init_dl_render() about the erase type.

    If we are in a nested HDL context (a Form, Char, Shfill, imposed page,
    etc), or half way through analyzing a vignette, we cannot destroy the
    DL memory without deleting open HDLs, and getting the calling routines
    confused, but allow destruction for a page group inside an HDL. */
    if ( analyzing_vignette() || in_execform() ) {
      HQASSERT(rendering_in_progress == PAGEDEVICE_DO_PARTIAL,
               "Still analyzing vignette but not in partial paint") ;
      spawn_type = DL_ERASE_PRESERVE ;
    } else if ( page->currentHdl != NULL &&
                hdlPurpose(page->currentHdl) != HDL_BASE &&
                hdlPurpose(page->currentHdl) != HDL_PAGE ) {
      /** \todo ajcd 2011-02-07: See HOMEangus_pstests!pagedev:erasepage.ps */
      spawn_type = DL_ERASE_PRESERVE ;
    } else if ( rendering_in_progress == PAGEDEVICE_DO_SHOWPAGE ) {
      if ( (NUM_THREADS() > 1 && dl_pipeline_depth > 1)
#ifdef DEBUG_BUILD
           || (debug_dl & DEBUG_DL_PIPELINE_SINGLE) != 0
#endif
           )
        spawn_type = DL_ERASE_ALL ;
      else
        spawn_type = DL_ERASE_CLEAR ;
    } else if ( rendering_in_progress == PAGEDEVICE_DO_COPYPAGE ) {
      spawn_type = DL_ERASE_COPYPAGE ;
    } else { /* Partial render continues with this page, but no DL objects. */
      spawn_type = DL_ERASE_PARTIAL ;
    }

    page_data->refcount = 1 ;
    page_data->paint_type = paint_type ;
    page_data->numcopies = numcopies ;
    page_data->page_is_composite = page_is_composite() ;
    page_data->page_is_separations = page_is_separations() ;
    page_data->multiple_separations = detected_multiple_seps() ;
    page_data->pageState = BGAllClear ;
    page_data->surface_handle.page = page->sfc_page ;
    page_data->surface_started = FALSE ;
    page_data->trap_context = NULL ;
    page_data->transparency_strategy = context->systemparams->TransparencyStrategy ;
    page_data->compress_bands = context->systemparams->CompressBands ;
    page_data->detect_separation = context->systemparams->DetectSeparation ;
    page_data->dynamic_bands = context->systemparams->DynamicBands ;
    page_data->dynamic_band_limit = context->systemparams->DynamicBandLimit ;
    page_data->use_screen_cache_name = context->userparams->UseScreenCacheName ;
    page_data->screen_dot_centered = context->systemparams->ScreenDotCentered ;
    page_data->never_render = context->userparams->NeverRender ;
    page_data->trap_group = NULL ;
    page_data->fail_job = (spawn_type == DL_ERASE_ALL) ;
    dlc_clear(&page_data->dlc_watermark) ;
    page_data->spotno_watermark = SPOT_NO_INVALID ;
    NAME_OBJECT(page_data, PAGE_DATA_NAME) ;

    /* Pipelining doesn't yet manage all the memory properly with resources.
       init_dl_render can often demand quite a bit of memory.  Also, the
       resource system itself erroneously allocates resources during
       resource_requirement_set_state().  Short-term solution is to flush other
       pages here. */
    dl_pipeline_flush(1, TRUE);

#define return DO_NOT_return_GO_TO_page_begin_failed_INSTEAD!
    PROBE(SW_TRACE_RENDER_INIT, (intptr_t)page->eraseno,
          ok = init_dl_render(context, page, page_data, spawn_type)) ;
    if ( ok ) {
      task_t *render ;
      task_group_t *render_tasks = NULL ;
      resource_requirement_t *requirement_copy = NULL ;
      int32 depth ;
      Bool new_page = (spawn_type == DL_ERASE_ALL ||
                       spawn_type == DL_ERASE_CLEAR ||
                       /** \todo ajcd 2011-07-08: Should copypage really note a new page? */
                       spawn_type == DL_ERASE_COPYPAGE) ;
      if ( new_page )
        corejob_page_end(page, TRUE /* Going to render page */);

#ifdef DEBUG_BUILD
      /* This should happen after banding the DL (the last operation of
         init_dl_render(). */
      if ( (debug_dl & DEBUG_DL_VERBOSITY) != 0 )
        debug_print_dl(page, debug_dl & DEBUG_DL_VERBOSITY);
#endif

      /* If we're going to re-use the same DL, save a copy of the
         requirements so that we can add new resources to them after the
         render. We need to do this before making the requirements ready,
         because that may remove resource pool references from the
         requirements that we'll need later. */
      if ( spawn_type == DL_ERASE_PRESERVE ||
           spawn_type == DL_ERASE_COPYPAGE ) {
        if ( (requirement_copy =
              resource_requirement_copy(page->render_resources,
                                        REQUIREMENT_NEVER)) == NULL )
          ok = FALSE ;
      }

      /* We need to use this particular requirement tree for this page,
         because it's the one that's attached to the render task group. */
      if ( ok )
        ok = resource_requirement_set_state(page->render_resources,
                                            REQUIREMENT_NOW) ;

      /* Create a group to put the render task in. */
      if ( ok )
        ok = task_group_create(&render_tasks, TASK_GROUP_RENDER,
                               page->all_tasks, page->render_resources) ;

      /* Convert render group into a nested group join. This will propagate
         errors to the page group automatically. */
      if ( ok )
        ok = task_group_set_joiner(render_tasks, NULL, NULL) ;

      if ( ok ) {
        /* Provision this task group when the requirement is ready */
        task_group_ready(render_tasks) ;
      } else if ( render_tasks != NULL ) {
        /* There's nothing in the group, we can join it immediately. We
           need to explicitly join it, because we failed to hand off
           responsibility to the all_tasks group. */
        task_group_join(render_tasks, NULL) ;
      }

      /* Create the main render task for this page, and hand over our
         page_data_t reference to it. Some back-end fields are wrongly
         tracked through the page, rather than page_data, but are cleaned up
         in page_data_cleanup(). Release our reference to it here (we won't
         need it again), so joining the render task can clean up these
         fields. */
      if ( ok &&
           task_create(&render, render_task_specialise, page,
                       &render_all_passes_of_page, page_data,
                       &page_data_cleanup, render_tasks, SW_TRACE_RENDER) ) {
        /* Make render dependent on the previous DL task, and let that task
           proceed. */
        ok = dl_pending_task(page, &render) ;

        /* That's the final (only) task pushed into the render group,
           we can now provision it and use task helpers to run it.
           render_all_passes_of_page() may create sub-groups or
           continuations, that's OK in a completed group. */
        task_group_close(render_tasks) ;
      } else {
        /* We also need to set the core context's page to the page
           temporarily, because the page isn't stored in page_data and we may
           have changed it when handing off the page in init_dl_render(). All
           other callers of page_data_cleanup() are from render specialised
           functions, which will have this page in the context anyway. */
        DL_STATE *oldpage = context->page ;
        context->page = page ;
        page_data_cleanup(context, page_data) ;
        context->page = oldpage ;
        ok = FALSE ;
      }

      if ( render_tasks != NULL )
        task_group_release(&render_tasks) ;

      /* We have no further use of the page's next task. Make it ready, maybe
         initiating asynchronous rendering. If an asynchronous erase task is
         created successfully, then the reference will be transferred to the
         job's "previous" task, so the next page can be made dependent on
         successfully rendering this page; if an asynchronous erase task is
         not set up successfully (or when it wasn't even attempted), the
         presence of the next task reference in the DL is used by
         dl_pipeline_flush() to indicate whether the page needs erasing
         immediately. */
      task_ready(page->next_task) ;

      if ( spawn_type != DL_ERASE_ALL ) {
        /* If we're doing a synchronous page (copypage or partial paint),
           then we pass a pipeline depth of zero. This indicates that we
           should flush (and erase) the whole pipeline including inputpage,
           and then erase inputpage synchronously. */
        depth = 0 ;
      } else if ( !dl_pending_erase(page) ) {
        /* If we couldn't create an asynchronous erase task then we flush all
           pages in the render queue: all except for the new inputpage will
           also be erased. */
        depth = 1 ;
      } else {
        /* We have an async erase task, so pass the maximum number of pages
           we want in flight. If we've only one thread to play with, then
           there is no point trying to pipeline pages. */
        HQASSERT(page->next_task == NULL,
                 "Still have next task after async erase setup") ;
        HQASSERT(dl_pipeline_depth > 0, "Pipeline depth is too shallow") ;
        depth = (NUM_THREADS() == 1
#ifdef DEBUG_BUILD
                 && (debug_dl & DEBUG_DL_PIPELINE_SINGLE) == 0
#endif
                 ? 1
                 : dl_pipeline_depth) ;
      }

      ok = dl_pipeline_flush(depth, TRUE /* no messages */) && ok ;

      /* If we saved a copy of the requirements to continue the current DL,
         swap them into place now. */
      if ( requirement_copy != NULL ) {
        HQASSERT(page == context->page,
                 "Copypage/preserve should have flushed DL pipeline") ;
        if ( !resource_requirement_set_state(requirement_copy, REQUIREMENT_FUTURE) )
          ok = FALSE ;
        if ( page->render_resources != NULL )
          resource_requirement_release(&page->render_resources) ;
        page->render_resources = requirement_copy ;
      }

      /* Input page may have changed, reload it. */
      page = context->page ;

      /* If the spawn type indicated that we are really throwing a final page,
         increment the interpreting page number. */
      if ( new_page )
        corejob_page_begin(page);
    } else { /* init_dl_render failed */
      page_data_cleanup(context, page_data) ;
      dl_clear_page(page) ;
    }
  }

  ok = dl_begin_page(page) && ok ;

#undef return
  return ok ;
}

/** \brief Add render page number content to render page timeline. */
static
sw_event_result HQNCALL render_tl_start(
  void*     context,
  sw_event* event)
{
  SWMSG_TIMELINE* msg = event->message;

  UNUSED_PARAM(void*, context);

  HQASSERT(event->type == EVENT_TIMELINE_START,
           "Unexpected event type");

  if (msg == NULL || event->length < sizeof(SWMSG_TIMELINE)) {
    return (SW_EVENT_CONTINUE);
  }

  if (msg->type == SWTLT_RENDER_CACHE ||
      msg->type == SWTLT_RENDER_PAGE) {
    /* Add render page number context to render cache and render page
       timelines */
    DL_STATE* page = SwTimelineGetContext(msg->ref, 0);
    if (SwTimelineSetContext(msg->ref, SW_RENDER_PAGE_NUMBER_CTXT,
                             (void*)(intptr_t)page->pageno) != SW_TL_SUCCESS) {
      return (SW_EVENT_ERROR);
    }
  }

  return (SW_EVENT_CONTINUE);
}

/** \brief Render all of the passes for a display list. */
static Bool render_all_passes_of_page(corecontext_t *context, void *args)
{
  page_data_t *page_data = args ;
  Bool result = FALSE, ok ;
  DL_STATE *page = context->page ;
  sw_tl_type tl_type = SWTLT_RENDER_PAGE ;

  VERIFY_OBJECT(page_data, PAGE_DATA_NAME) ;

  HQASSERT(page_data->paint_type == PAINT_TYPE_FINAL ||
           page_data->paint_type == PAINT_TYPE_PARTIAL,
           "Invalid paint type for render") ;

  if ( page_data->paint_type == PAINT_TYPE_PARTIAL ) {
    tl_type = SWTLT_RENDER_PARTIAL ;
  }
  else if ( page->irr.state != NULL && page->irr.generating ) {
    tl_type = SWTLT_RENDER_CACHE ;
  }

  CHECK_TL_VALID(timeline_push(&page->timeline, tl_type, 0 /*end*/,
                               SW_TL_UNIT_NONE, page, NULL, 0)) ;

  /* We're now in control of the real PGB device, so flush the proxy's
     stored parameters to it. */
  (void)pgbproxy_setflush(page, TRUE);

  /* Record or update sep-detection flags if doing IRR generating or
     replaying. */
  irr_sepdetect_flags(page, &page_data->page_is_composite,
                      &page_data->page_is_separations,
                      &page_data->multiple_separations);

#define return DO_NOT_return_GO_TO_cleanup_passes_INSTEAD!
  if ( page->surfaces->render_begin != NULL &&
       !(*page->surfaces->render_begin)(&page_data->surface_handle, page_data) )
    goto cleanup_passes ;

  page_data->surface_started = TRUE ;

  if ( !init_render_pgbs(page, page_data) ||
       !setup_dynamic_bands(page, page_data) )
    goto cleanup_passes ;

  /* Do compositing in a separate render pass and set up the erase ready for a
     partial-paint read-back in the final render. */
  if ( page_data->pageState != BGAllClear &&
       page_data->transparency_strategy == 2 ) {
    if ( !composite_bands(context, page_data) )
      goto cleanup_passes ;
  }

  if ( page_data->pageState != BGAllSet ) {
    IMAGEOBJECT *im = page->im_list;

    while ( im != NULL ) {
      if ( im->ims != NULL )
        im_store_release_reserves(im->ims);
      im = im->next;
    }
    /* Convert DL to device colors, ready to render the direct regions. */
    PROBE(SW_TRACE_DL_PRECONVERT, page,
          ok = preconvert_dl(page, page_data->transparency_strategy));
    if ( !ok )
      goto cleanup_passes ;

#ifdef DEBUG_BUILD
    if ( (debug_dl & DEBUG_DL_PRECONVERT_VERBOSITY) != 0 )
      debug_print_dl(page, (debug_dl & DEBUG_DL_PRECONVERT_VERBOSITY) >> 4) ;
#endif
  }
#ifdef ASSERT_BUILD
  else {
    IMAGEOBJECT *im = page->im_list;

    while ( im != NULL ) {
      HQASSERT(im->ims == NULL || !im_store_have_reserves(im->ims),
               "Have preconversion reserves, but not preconverting.");
      im = im->next;
    }
  }
#endif
  /* Prepare for output by marking omitted separations, and setting up the
     PGB device. */
  if ( page_data->paint_type == PAINT_TYPE_FINAL ) {
    /* Generate traps from the data prepared in init_dl_render(). */
    if ( !trapGenerate(page_data->trap_context) )
      goto cleanup_passes ;

    /* Ensure trapping complete before final render using the trap
     * completion task, dependent on all trapping sub-tasks. */
#if 0
    if ( page_data->trap_group != NULL ) {
      if ( !task_group_join(page_data->trap_group, context->error) )
        goto cleanup_passes ;
    }
#endif

    PROBE(SW_TRACE_RENDER_INIT_FINAL, (intptr_t)page->eraseno,
          ok = init_final_render(page, page_data)) ;
  } else {
    PROBE(SW_TRACE_RENDER_INIT_PARTIAL,
          (intptr_t)page->eraseno,
          ok = (omit_blank_separations(page) &&
                init_partial_render(page))) ;
  }

  if ( !ok )
    goto cleanup_passes ;

#if defined(DEBUG_BUILD)
  if ( (debug_render & DEBUG_RENDER_RASTERSTYLE) != 0 )
    debug_print_gucr_rasterstyle(page->hr, FALSE) ;
#endif

  if ( gucr_framesStart(page->hr) != NULL ) {
    int32 numcopies = page_data->numcopies ;
    while ( ok && --numcopies >= 0 ) {
      ok = render_all_frames_of_dl(context, page_data, page_data->paint_type) ;
    }
  }
  if ( !ok )
    goto cleanup_passes ;

  /* If we got here without skipping to the cleanup, all is good. */
  HQASSERT(!result, "'result' should remain FALSE up to here.  Use 'ok' instead!");
  result = TRUE ;

 cleanup_passes:
  if ( page_data->paint_type == PAINT_TYPE_FINAL &&
       !guc_resetOmitSeparations(page->hr) )
    result = FALSE;

  if ( page_data->trap_context != NULL )
    trapDispose(&page_data->trap_context) ;

  if ( page_data->surface_started ) {
    if ( page->surfaces->render_end != NULL &&
         !(*page->surfaces->render_end)(page_data->surface_handle,
                                        page_data, result) )
      result = FALSE ;
    page_data->surface_started = FALSE ;
  }

  if ( page_data->paint_type == PAINT_TYPE_FINAL )
    if ( !irr_render_complete(page, result) )
      result = FALSE ;

  if ( !result || page_data->paint_type == PAINT_TYPE_FINAL ) {
    /* Either we encountered an error, or that was the final pass. Remove the
       PGB device's temporary files. This must be done synchronously, because
       the PGB device is not pipeline-aware. */
    erase_page_buffers(page) ;
  }

  if ( result ) {
    /* Remember if we've ripped anything important for this page. */
    page->rippedsomethingsignificant |= dlSignificantObjectsToRip(page);
    page->rippedtodisk = TRUE;
  } else {
    /* An asynchronously rendered page failed. Issue an event to signal
       the failure, and if the skin indicates that the job should fail,
       mark the job object as a failure. */
    SWMSG_ERROR msg = {0} ;
    error_context_t *error = context->error ;

    /** \todo ajcd 2011-04-28: Should we make the whole event issuance
        dependent on the spawn type (page_data->fail_job) instead of just
        setting the fail flag? */
    msg.timeline = page->timeline ;
    msg.page_number = page->pageno ;
    msg.fail_job = page_data->fail_job ;
    msg.suppress_handling = FALSE ;
    if ( error->old_error == 0 ) {
      msg.error_name.length = 0 ;
      msg.error_name.string = (uint8 *)"" ;
    } else {
      NAMECACHE *name = &system_names[NAME_dictfull + error->old_error - DICTFULL] ;
      msg.error_name.length = theINLen(name) ;
      msg.error_name.string = theICList(name) ;
    }
#define RENDER_COMMAND "renderbands" /* Command reported by render error */
    msg.command.length = sizeof(RENDER_COMMAND) - 1;
    msg.command.string = (uint8 *)RENDER_COMMAND ;
    msg.error_number = error->old_error ;

    /** \todo ajcd 2011-04-27: Capture and propagate detail through C
        call stack. */
    msg.detail = NULL ;

    if ( SwEvent(SWEVT_RENDER_ERROR, &msg, sizeof(msg)) >= SW_EVENT_ERROR )
      HQTRACE(TRUE,
              ("Error from SwEvent(SWEVT_RENDER_ERROR, 0x%p, %u)",
               &msg, sizeof(msg))) ;

    if ( msg.fail_job )
      page->job->failed = TRUE ;

    if ( msg.suppress_handling )
      result = TRUE ; /* Skin says to ignore this error. */
  }

  (void)pgbproxy_setflush(page, FALSE);

  timeline_pop(&page->timeline, tl_type, result) ;

#undef return
  return result ;
}

/** This is the main entry point to the renderer.  When the interpretation of a
   page completes, and the display list is completed, the job calls 'showpage'
   which in turn calls back out to some SW postscript which then calls
   renderbands as a PostScript operator.  On the stack is the number of copies
   of the page to produce.

   If the job contains regions which require compositing (eg, transparency or
   recombine) rendering is split into two passes.  The first pass renders only
   those regions requiring compositing (backdrop regions), the second pass for
   the other regions using the standard opaque painting model (direct regions).
*/
Bool renderbands_(ps_context_t *pscontext)
{
  corecontext_t *context = ps_core_context(pscontext);
  int32 numcopies ;
  OBJECT *theo ;

  HQASSERT(rendering_in_progress != PAGEDEVICE_NOT_ACTIVE,
           "renderbands called but not from render procedure") ;

  if ( EmptyStack( theStackSize( operandstack )))
    return error_handler( STACKUNDERFLOW ) ;

  /* Get number of numcopies to output */
  theo = theTop( operandstack ) ;
  if ( oType(*theo) != OINTEGER )
    return error_handler( TYPECHECK ) ;

  numcopies = oInteger(*theo) ;
  if ( numcopies < 0 )
    return error_handler(RANGECHECK) ;

  pop( & operandstack ) ;

  /* Don't need to render anything */
  if ( numcopies == 0 )
    return TRUE ;

  pclIdiomFlush(context->page) ;

  /* Semi-transparent (was XPS) watermark. I know doing it this way
     around means the watermark will itself get trapped, but trying to
     pretend it's not there will confuse TrapPro no end. */
  if ( context->systemparams->ApplyWatermark ) {
    if ( ! xps_draw_watermarks())
      return FALSE ;
  }

  return spawn_all_passes_of_page(context, numcopies, PAINT_TYPE_FINAL) ;
}


Bool partial_paint_allowed(corecontext_t *context)
{
  return dl_safe_recursion == 0
    && !char_doing_cached()
    && !rcbn_enabled()
    && !isTrappingActive(context->page)
    && !DOING_TRANSPARENT_RLE(context->page)
    && tsOpaque(gsTranState(gstateptr), TsStrokeAndNonStroke, gstateptr->colorInfo)
    && dlAllowPartialPaint(context->page)
    && pclPartialPaintSafe(context->page)
    && rendering_in_progress == PAGEDEVICE_NOT_ACTIVE
    && !dlpurge_inuse()
    && dlSignificantObjectsToRip(context->page);
}


/** Rips all of the bands to the disk.  This function is called from
   the low memory handling routines. The idea is that when memory is
   very low, one strategy is to render what's on the display list so
   far, saving the rasters to disk, and then free the display list
   before continuing to interpret the rest of the job.
*/
static Bool real_rip_to_disk(corecontext_t *context)
{
  Bool result ;

  /* Following var controls paint to disks.  We only allow a subsequent
   * paint to disk IF the adderasedisplay has been called.  This happens
   * normally, but if an error occurs, we stop the paint to disk going
   * into a loop as memory stays low...
   */
  HQASSERT(partial_paint_allowed(context), "Disallowed partial paint");
  HQASSERT(IS_INTERPRETER(), "Not in interpreter") ;

  /* Ensure any cached dl characters in progress get flushed to output page.
   * This should only ever do anything when we do a partial paint in the
   * middle of a show. It's hard to assert this. The call should be safe
   * in all other situations, including when the current device is not the
   * page device. Ideally, we'd have a dl management module, which would
   * automatically handle stuff like this. One day...
   */
  if ( !finishaddchardisplay(context->page, 1) )
    return FALSE ;

  pclIdiomFlush(context->page) ;

  if (! runHooks(& thegsDevicePageDict(*gstateptr), GENHOOK_StartPartialRender))
    return FALSE;

  if ( ! finalise_sep_detection(gstateptr->colorInfo))
    return FALSE ;

  HQASSERT(rendering_in_progress == PAGEDEVICE_NOT_ACTIVE,
           "Already rendering in rip_to_disk()") ;
  rendering_in_progress = PAGEDEVICE_DO_PARTIAL ;
  result = spawn_all_passes_of_page(context, 1 /*numcopies*/,
                                    PAINT_TYPE_PARTIAL) ;
  rendering_in_progress = PAGEDEVICE_NOT_ACTIVE ;

  return result ;
}

Bool rip_to_disk(corecontext_t *context)
{
  Bool ok;
  ok = real_rip_to_disk(context);
  return ok;
}

static char *md5_hex(uint8 id[MD5_OUTPUT_LEN], char buffer[33])
{
  int i ;
  char *end = buffer ;

  for ( i = 0 ; i < 16 ; ++i ) {
    *end++ = (char)nibble_to_hex_char[id[i] >> 4] ;
    *end++ = (char)nibble_to_hex_char[id[i] & 15] ;
  }
  *end++ = '\0' ;

  return buffer ;
}

Bool external_retained_raster(corecontext_t *context,
                              uint8 id[MD5_OUTPUT_LEN],
                              int32 usecount)
{
  char buffer[33] ;
  DL_STATE *page = context->page;

  SET_PGB_PARAM_S(page, "OptimizedPDFId",
                  id != NULL ? md5_hex(id, buffer) : "",
                  id != NULL ? 32 : 0);
  SET_PGB_PARAM_I(page, "OptimizedPDFUsageCount", usecount) ;
  return TRUE ;
}

/*----------------------------------------------------*/
#if defined(DEBUG_BUILD)
void init_render_debug(void)
{
  register_ripvar(NAME_debug_render, OINTEGER, &debug_render);
  register_ripvar(NAME_debug_render_firstband, OINTEGER, &debug_render_firstband);
  register_ripvar(NAME_debug_render_lastband, OINTEGER, &debug_render_lastband);
}
#endif

/* The handler to add the render page number context must be high priority so
 * that it is called before any other handlers in case they need to know what
 * the render page number is */
static
sw_event_handlers handlers[] = {
  {render_tl_start, NULL, 0, EVENT_TIMELINE_START, SW_EVENT_OVERRIDE}
};

static
Bool render_postboot(void)
{
  multi_mutex_init(&mht_mutex, PGB_LOCK_INDEX, FALSE,
                   SW_TRACE_MHT_ACQUIRE, SW_TRACE_MHT_HOLD);
  multi_condvar_init(&mht_condvar, &mht_mutex, SW_TRACE_MHT_WAIT);
  return (SwRegisterHandlers(handlers, NUM_ARRAY_ITEMS(handlers)) == SW_RDR_SUCCESS);
}

static
void render_finish(void)
{
  multi_condvar_finish(&mht_condvar);
  multi_mutex_finish(&mht_mutex);
  (void)SwDeregisterHandlers(handlers, NUM_ARRAY_ITEMS(handlers));
}


void v20render_C_globals(
  struct core_init_fns* fns)
{
#if defined(DEBUG_BUILD)
  debug_render = 0;
  debug_render_firstband = 0 ;
  debug_render_lastband = MAXINT32 ;
#endif

  band_threads_warned_job = -1 ;

  trim_to_page = FALSE;

  trim_would_start = 0;
  trim_would_end = 0;

  /* Initialise the static blit stuff used by render_mask code */
  blit_colormap_mask(&mask_blitmap) ;
  blit_color_init(&mask_erase_color, &mask_blitmap) ;
  blit_color_mask(&mask_erase_color, TRUE /*white*/) ;
  blit_color_init(&mask_knockout_color, &mask_blitmap) ;
  blit_color_mask(&mask_knockout_color, TRUE /*white*/) ;

  fns->postboot = render_postboot;
  fns->finish = render_finish;
}

/*
Log stripped */
