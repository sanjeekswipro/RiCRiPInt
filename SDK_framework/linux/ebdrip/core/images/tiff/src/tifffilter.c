/** \file
 * \ingroup tiff
 *
 * $HopeName: SWv20tiff!src:tifffilter.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * TIFF Decode filter.
 *
 * The TIFF filter structure is quite complex, because of the way TIFF files
 * are structured. TIFF files may have samples interleaved, or may be
 * pre-separated, and may be banded into "strips". The compression methods
 * applied to TIFF are applied to each strip independently.
 *
 * Creating a TIFFDecode filter automatically builds multiple filter chains,
 * and layers them under the TIFFDecode. The filters in the chain are:
 *
 * original
 *     The original file or filter over which the filter chain is built.
 * RSD (optional)
 *     If the original file or filter is not seekable, or is not positioned
 *     at its start, then an RSD is layered over it.
 * %TIFFbase
 *     The TIFF base filter manages the TIFF contexts, parsing the tags, and
 *     seeking in the underlying file to fulfill channel requests.
 *
 * There are then one of the following for each separated colour channel:
 *
 * %TIFFselect
 *     The TIFF select filter handles seeking for the strips for a particular
 *     channel, buffering the raw data from the TIFF strips for it, and
 *     byte-ordering of that data. Its main reason for existing is because we
 *     may not be able to read all of the data for a strip at once, so the
 *     state of the decompression filters on top of this must be maintained,
 *     be able to read an entire strip in small chunks, interleaved with
 *     reads on other channels. This filter returns EOF at the end of each
 *     raw strip; the decompression filters on top of it will be closed at
 *     the end of the strip. The %TIFFchannel at the top of the channel's
 *     filter chain will then seek on this filter to the next strip and
 *     re-initialised the decompression filter chain. The TIFF select filter
 *     has intimate knowledge of its underlying %TIFFbase filter.
 *
 * Each %TIFFselect may have one of the following decompression filters on
 * top of it:
 *
 * CCITTFaxDecode (for CCITT, CCITT4 and CCITT6 compression)
 * RunLengthDecode (for Packbits)
 * FlateDecode
 * LZWDecode
 * DCTDecode
 *     The filter params dict for the decompression filter is allocated in
 *     TIFF memory and is created and destroyed by the %TIFFselect filter.
 *
 * Each channel filter chain has the following at the top of it:
 *
 * %TIFFchannel
 *     The TIFF channel filter manages the channel filter chain, making a
 *     series of strip decompressions appear as one single decompressed data
 *     stream. It seeks the appropriate strip on the %TIFFselect filter, reads
 *     each strip to EOF, and then re-initialises the decompression filters for
 *     the next strip.
 *
 * The final filter in the chains is:
 *
 * TIFFDecode
 *     All of the %TIFFchannel filters are referenced from the state structure
 *     of TIFFDecode, and TIFFDecode has no direct underlying file. When the
 *     imagecontext interface is used, TIFFDecode will return an array of the
 *     %TIFFchannel filters through the DataSource key. Reads are not normally
 *     performed on TIFFDecode directly, but if they are required, TIFFDecode
 *     will interleave data from the underlying sources.
 *
 * If the TIFF data is presented as sample-interleaved rather than as
 * separated, then only one filter chain will be set up.
 *
 * The TIFF filter does not yet take into account TIFF-IT files. When these
 * are included, tiffexec_ will be able to be re-written as a simple
 * application of the image context interface.
 */


#include "core.h"
#include "swerrors.h"
#include "hqmemcmp.h"
#include "hqmemcpy.h"
#include "hqmemset.h"
#include "objects.h"
#include "objstack.h"
#include "dictscan.h"
#include "fileio.h"
#include "filterinfo.h"
#include "mmcompat.h"
#include "objnamer.h"
#include "namedef_.h"
#include "tables.h"
#include "caching.h"
#include "swcopyf.h"

#include "tiftypes.h"
#include "ifdreadr.h"
#include "tifftags.h"
#include "tiffmem.h"
#include "tifcntxt.h"
#include "tifffile.h"
#include "tifreadr.h"
#include "t6params.h"
#include "tiffclsp.h"

#if defined(ASSERT_BUILD)
static int32 debug_tifffilter = 0 ;
enum {
  DEBUG_TIFF_F_BASE = 1,
  DEBUG_TIFF_F_SELECT = 2,
  DEBUG_TIFF_F_CHANNEL = 4,
  DEBUG_TIFF_F_DECODE = 8,
  DEBUG_TIFF_F_DECOMPRESS = 16
} ;
#endif

/* TIFF base filter */

#define TIFF_BASE_BUFFSIZE 16384
#define TIFF_BASE_NAME "%TIFFbase filter state"


/** Private data for %TIFFbase image filter. */
typedef struct {
  /*@owned@*/
  tiff_context_t *context ;       /**< TIFF context. */
  uint32 subimage ;               /**< IFD selected. */
  tiff_image_data_t image_data ;  /**< Expanded image details. */

  OBJECT_NAME_MEMBER
} tiff_base_state_t ;

/** Initialise the private state for a %TIFFbase filter. */
static Bool tiff_base_init(/*@notnull@*/ /*@in@*/ FILELIST *filter,
                           /*@notnull@*/ /*@in@*/ OBJECT *args,
                           /*@notnull@*/ /*@in@*/ STACK *stack)
{
  int32 pop_args = 0 ;
  tiff_base_state_t *state ;

  enum {
    tiff_SubImage,
    tiff_dummy
  };
  static NAMETYPEMATCH tiffmatch[tiff_dummy + 1] = {
    { NAME_SubImage | OOPTIONAL , 1, { OINTEGER }},
    DUMMY_END_MATCH
  };

  HQASSERT(args != NULL || stack != NULL,
           "Arguments and stack should not both be empty") ;
  if ( ! args && !isEmpty(*stack) ) {
    args = theITop(stack) ;
    if ( oType(*args) == ODICTIONARY )
      pop_args = 1 ;
  }

  if ( args && oType(*args) == ODICTIONARY ) {
    if ( ! oCanRead(*oDict(*args)) &&
         ! object_access_override(oDict(*args)) )
      return error_handler(INVALIDACCESS) ;

    if ( ! dictmatch(args, tiffmatch) )
      return FALSE ;

    if ( ! FilterCheckArgs(filter, args) )
      return FALSE ;

    OCopy(theIParamDict(filter), *args) ;
  } else {
    args = NULL;
  }

  /* Get underlying source/target if we have a stack supplied. */
  if ( stack ) {
    FILELIST *flptr ;
    Hq32x2 filepos ;

    if ( theIStackSize(stack) < pop_args )
      return error_handler(STACKUNDERFLOW) ;

    if ( ! filter_target_or_source(filter, stackindex(pop_args, stack)) )
      return FALSE ;

    /* Layer an RSD over the underlying file. We don't need to do this if
       we already have one, and it's positioned at its start. */
    flptr = theIUnderFile(filter) ;
    if ( !file_seekable(flptr) ||
         (*theIMyFilePos(flptr))(flptr, &filepos) == EOF ||
         !Hq32x2IsZero(&filepos) ) {
      if ( !filter_layer(flptr,
                         NAME_AND_LENGTH("ReusableStreamDecode"),
                         NULL, &flptr) )
        return FALSE ;

      /* Transfer the CloseSource/CloseTarget flag from this filter to the RSD
         filter, and turn on the CloseSource flag on this filter so the RSD is
         closed automatically. */
      if ( isICST(filter) )
        SetICSTFlag(flptr) ;
      else
        ClearICSTFlag(flptr) ;

      theIUnderFile(filter) = flptr ;
      theIUnderFilterId(filter) = theIFilterId(flptr) ;
      SetICSTFlag(filter) ;
    }

    ++pop_args ;
  }

  /* allocate a state structure */
  state = mm_alloc(mm_pool_temp,
                   sizeof(tiff_base_state_t),
                   MM_ALLOC_CLASS_TIFF_FILTER_STATE) ;
  if ( state == NULL )
    return error_handler(VMERROR) ;

  theIFilterPrivate(filter) = state ;

  /* quick dirty initialization */
  HqMemZero((uint8 *)state, sizeof(tiff_base_state_t));

  NAME_OBJECT(state, TIFF_BASE_NAME);

  if (args) { /* Do whatever is needed with args */
    OBJECT *theo ;

    if ( (theo = tiffmatch[tiff_SubImage].result) != NULL ) {
      if ( oInteger(*theo) < 0 )
        return error_handler(RANGECHECK) ;

      state->subimage = (uint32)oInteger(*theo) ;
    }
  }

  /* %TIFFbase requires an underlying filter to initialise properly */
  if ( theIUnderFile(filter) != NULL ) {
    corecontext_t *corecontext = get_core_context();
    FILELIST *uflptr = theIUnderFile(filter) ;
    tiff_file_t *tifffile ;
    tiff_context_t *context ;

    if ( !tiff_new_context(corecontext, &context) )
      return FALSE ;

    state->context = context ;

    if ( !tiff_new_file(uflptr, context->mm_pool, &tifffile) )
      return FALSE ;

    if ( !tiff_new_reader(corecontext, tifffile, context->mm_pool, &context->p_reader) ) {
      tiff_free_file(&tifffile) ;
      return FALSE ;
    }

    if ( !tiff_read_header(corecontext, context->p_reader) ||
         !ifd_read_ifds(ifd_reader_from_tiff(context->p_reader), &context->number_images) )
      return FALSE ;

    if ( context->number_images == 0 )
      return error_handler(RANGECHECK) ;

    if ( !tiff_set_image(context->p_reader, state->subimage + 1) ||
         !tiff_check_tiff6(corecontext, context->p_reader) ||
         !tiff_get_image_data(corecontext, context->p_reader, &state->image_data) ||
         !tiff_setup_read_data(corecontext, context->p_reader, &state->image_data) )
      return FALSE ;
  } else /* No underlying filter, can't initialise context. */
    return error_handler(UNDEFINED) ;

  /* The %TIFFbase filter uses its own fillbuff routine, which sneakily
     returns the underlying file or filter's buffer as if it were its own. */
  theIPtr(filter) = theIBuffer(filter) = NULL ;
  theIBufferSize(filter) = theICount(filter) = 0 ;
  theIFilterState(filter) = FILTER_EMPTY_STATE ;

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  HQTRACE((debug_tifffilter & DEBUG_TIFF_F_BASE),
          ("    %%TIFFbase %p CREATE", filter)) ;

  return TRUE ;
}

/** Destroy the private data for a %TIFFbase filter. */
static void tiff_base_dispose(/*@notnull@*/ /*@in@*/ FILELIST* filter)
{
  tiff_base_state_t *state ;

  state = theIFilterPrivate(filter) ;
  if ( state != NULL ) {
    tiff_context_t *context = state->context ;

    VERIFY_OBJECT(state, TIFF_BASE_NAME) ;
    UNNAME_OBJECT(state) ;

    if ( context != NULL ) {
      tiff_reader_t *reader = context->p_reader ;

      if ( reader != NULL )
        tiff_free_image_data(reader, &state->image_data) ;

      tiff_free_context(&state->context) ;
    }

    mm_free(mm_pool_temp, state, sizeof(tiff_base_state_t)) ;
    theIFilterPrivate(filter) = NULL ;
  }

  HQTRACE((debug_tifffilter & DEBUG_TIFF_F_BASE),
          ("    %%TIFFbase %p DISPOSE", filter)) ;
}

/** Read a buffer of data from a TIFF base file. Note that this is a
    FILTER_FILLBUFF routine, not a decode routine, so that seeks on the
    filter don't confuse the filter LASTCHAR mechanism. */
static int32 tiff_base_fill(/*@notnull@*/ /*@in@*/ FILELIST *filter)
{
  FILELIST *uflptr = theIUnderFile(filter) ;
  uint8 *uflbuf ;
  tiff_base_state_t *state ;
  int32 bytes ;

  HQTRACE((debug_tifffilter & DEBUG_TIFF_F_BASE),
          ("    %%TIFFbase %p FILL", filter)) ;

  state = theIFilterPrivate(filter) ;
  VERIFY_OBJECT(state, TIFF_BASE_NAME) ;

  if ( !GetFileBuff(uflptr, TIFF_BASE_BUFFSIZE, &uflbuf, &bytes) ) {
    if ( isIIOError(filter) )
      return ioerror_handler(filter) ;

    SetIEofFlag(filter) ;
    theIFilterState(filter) = FILTER_EOF_STATE ;
    return EOF ;
  }

  theIBuffer(filter) = uflbuf ;
  theIPtr(filter) = uflbuf + 1 ;
  theIBufferSize(filter) = theIReadSize(filter) = bytes ;
  theICount(filter) = bytes - 1 ;

  HQTRACE((debug_tifffilter & DEBUG_TIFF_F_BASE),
          ("    %%TIFFbase %p FILL size %d first 0x%x state %d",
           filter, theIReadSize(filter), uflbuf[0], theIFilterState(filter))) ;

  return uflbuf[0] ;
}

/** Seek to a location in a TIFF base file. */
static int32 tiff_base_seek(/*@notnull@*/ /*@in@*/ FILELIST *filter,
                            /*@notnull@*/ /*@in@*/ const Hq32x2 *offset)
{
  FILELIST *uflptr = theIUnderFile(filter) ;

  HQTRACE((debug_tifffilter & DEBUG_TIFF_F_BASE),
          ("    %%TIFFbase %p SEEK to %u", filter, offset->low)) ;

  /* This is a dumb trampoline routine to forward the seek to the underlying
     file or filter. We rely on the underlying file/filter being smart enough
     to retain the cached buffer if possible. */
    if (
#if NEEDMYRESETFILE
       theIMyResetFile(uflptr)(uflptr) == EOF ||
#endif
       theIMySetFilePos(uflptr)(uflptr, offset) == EOF )
    return ioerror_handler(filter) ;

  return 0 ;
}

/** Return the memory pool used for TIFF objects. */
static mm_pool_t tiff_base_pool(/*@notnull@*/ /*@in@*/ FILELIST *filter)
{
  tiff_base_state_t *state ;
  tiff_context_t *context ;

  state = theIFilterPrivate(filter) ;
  VERIFY_OBJECT(state, TIFF_BASE_NAME) ;

  context = state->context ;
  HQASSERT(context != NULL, "No TIFF context in %%TIFFbase") ;

  return context->mm_pool ;
}

/** Return the number of channels in the output data, including the alpha
    channel (if any). */
static uint32 tiff_base_nchannels(/*@notnull@*/ /*@in@*/ FILELIST *filter)
{
  tiff_base_state_t *state ;
  tiff_image_data_t *data ;

  state = theIFilterPrivate(filter) ;
  VERIFY_OBJECT(state, TIFF_BASE_NAME) ;

  data = &state->image_data ;

  /* For interleaved data, return the number of channels. */
  if ( data->planar_config == PLANAR_CONFIG_CHUNKY )
    return 1 ;

  /* Ignore all extra samples except for alpha channel */
  return data->samples_per_pixel - data->extra_samples + data->alpha ;
}

/** Find the appropriate TIFF channel for the output sample n*/
static uint32 tiff_base_mapped_channel(/*@notnull@*/ /*@in@*/ FILELIST *filter,
                                       uint32 index)
{
  tiff_base_state_t *state ;
  tiff_image_data_t *data ;

  state = theIFilterPrivate(filter) ;
  VERIFY_OBJECT(state, TIFF_BASE_NAME) ;

  data = &state->image_data ;

  /* Normal samples indices are returned directly */
  if ( index < data->samples_per_pixel - data->extra_samples )
    return index ;

  /** \todo @@@ TODO FIXME ajcd 2005-03-04: Extra samples use alpha channel */
  return index ;
}

/** Extract TIFF image data from TIFF base filter state.. */
static tiff_image_data_t *tiff_base_data(/*@notnull@*/ /*@in@*/ FILELIST *filter)
{
  tiff_base_state_t *state ;

  state = theIFilterPrivate(filter) ;
  VERIFY_OBJECT(state, TIFF_BASE_NAME) ;

  return &state->image_data ;
}

/** Return byte range in TIFF file for selected strip of a channel. */
static void tiff_base_strip(/*@notnull@*/ /*@in@*/ FILELIST *filter,
                            uint32 channel, uint32 strip,
                            /*@notnull@*/ /*@out@*/ Hq32x2 *start,
                            /*@notnull@*/ /*@out@*/ Hq32x2 *end)
{
  tiff_base_state_t *state ;
  tiff_image_data_t *data ;
  Hq32x2 stripsize ;

  state = theIFilterPrivate(filter) ;
  VERIFY_OBJECT(state, TIFF_BASE_NAME) ;
  data = &state->image_data ;

  HQASSERT(strip < data->number_strips, "Strip out of range") ;
  HQASSERT(channel < data->samples_per_pixel, "Channel out of range") ;

  strip += channel * data->number_strips ;
  HQASSERT(strip < data->strip_count && strip < data->strip_bytecount,
           "Strip index out of range") ;

  Hq32x2FromUint32(start, data->strip_offset[strip]) ;
  Hq32x2FromUint32(&stripsize, data->strip_bytes[strip]) ;
  Hq32x2Add(end, start, &stripsize) ;

  HQTRACE((debug_tifffilter & DEBUG_TIFF_F_BASE),
          ("    %%TIFFbase %p STRIP channel %u strip %u start %u end %u",
           filter, channel, strip, start->low, end->low)) ;
}

/** Build a prototypical %TIFFbase filter in the memory supplied. */
static void tiff_base_filter(/*@notnull@*/ /*@in@*/ FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* flate encode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("%TIFFbase") ,
                       FILTER_FLAG | READ_FLAG,
                       0, NULL, 0,
                       tiff_base_fill,                       /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       tiff_base_init,                       /* initfile */
                       FilterCloseFile,                      /* closefile */
                       tiff_base_dispose,                    /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       tiff_base_seek,                       /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       FilterDecodeError,                    /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}

/****************************************************************************/
/* TIFF select filter */

#define TIFF_SELECT_BUFFSIZE 16384
#define TIFF_SELECT_NAME "%TIFFselect filter state"

/** Private data for the %TIFFselect filter. */
typedef struct {
  uint32 channel ;            /**< Selected channel. */
  Hq32x2 current ;            /**< Current offset in %TIFFbase. */
  Hq32x2 end ;                /**< End offset of strip in %TIFFbase. */
  OBJECT decompress_params ;  /**< Param dictionary for decompress filter. */
  OBJECT decompress_params_sfd ;  /**< Param dictionary for sub file decode filter
                                        (used in conjunstion with runlength). */
  Bool update_rows ;          /**< Update Rows in decompress dict? */
  Bool update_eodcount ;      /**< Update EODCount in decompress dict? */
  Bool update_eodcount_sfd ;  /**< Update EODCount in subfiledecode dict? */
  Bool flipbits ;             /**< Flip bit-order. */

  OBJECT_NAME_MEMBER
} tiff_select_state_t ;

/** Initialise the private state for a %TIFFselect filter. */
static Bool tiff_select_init(/*@notnull@*/ /*@in@*/ FILELIST *filter,
                             /*@notnull@*/ /*@in@*/ OBJECT *args,
                             /*@notnull@*/ /*@in@*/ STACK *stack)
{
  tiff_select_state_t *state ;
  FILELIST *uflptr ;
  tiff_image_data_t *data ;

  UNUSED_PARAM(OBJECT *, args) ;
  UNUSED_PARAM(STACK *, stack) ;

  uflptr = theIUnderFile(filter) ;
  HQASSERT(uflptr != NULL &&
           HqMemCmp(uflptr->clist, uflptr->len, NAME_AND_LENGTH("%TIFFbase")) == 0,
           "Underlying filter of %TIFFselect is not %TIFFbase") ;

  /* allocate a state structure */
  state = mm_alloc(mm_pool_temp,
                   sizeof(tiff_select_state_t),
                   MM_ALLOC_CLASS_TIFF_FILTER_STATE) ;
  if ( state == NULL )
    return error_handler(VMERROR) ;

  theIFilterPrivate(filter) = state ;

  /* quick dirty initialization */
  HqMemZero((uint8 *)state, sizeof(tiff_select_state_t));

  state->decompress_params =
    state->decompress_params_sfd = onull ; /* Struct copy to set slot properties */

  data = tiff_base_data(uflptr) ;

  state->flipbits = (data->fill_order == FILLORDER_LSB_TO_MSB) ;

  NAME_OBJECT(state, TIFF_SELECT_NAME);

  /* Delay creation of a buffer until decode routine, because channel isn't
     known yet. This allows us to allocate a buffer of the maximum size
     required for the channel's strips. */
  theIFilterState(filter) = FILTER_INIT_STATE ;

  HQTRACE((debug_tifffilter & DEBUG_TIFF_F_SELECT),
          ("   %%TIFFselect %p CREATE", filter)) ;

  return TRUE ;
}

/** Destroy the private data for a %TIFFselect filter. */
static void tiff_select_dispose(/*@notnull@*/ /*@in@*/ FILELIST* filter)
{
  tiff_select_state_t *state ;

  state = theIFilterPrivate(filter) ;
  if ( state != NULL ) {
    VERIFY_OBJECT(state, TIFF_SELECT_NAME) ;
    UNNAME_OBJECT(state) ;

    if ( oType(state->decompress_params) == ODICTIONARY ) {
      mm_pool_t pool = tiff_base_pool(theIUnderFile(filter)) ;
      tiff_free_psdict(pool, &state->decompress_params) ;
    }

    if ( oType(state->decompress_params_sfd) == ODICTIONARY ) {
      mm_pool_t pool = tiff_base_pool(theIUnderFile(filter)) ;
      tiff_free_psdict(pool, &state->decompress_params_sfd) ;
    }

    mm_free(mm_pool_temp, state, sizeof(tiff_select_state_t)) ;
    theIFilterPrivate(filter) = NULL ;
  }

  if ( theIBuffer(filter) ) {
    mm_free(mm_pool_temp, theIBuffer(filter) - 1, theIBufferSize(filter) + 1) ;
    theIBuffer(filter) = NULL ;
  }

  HQTRACE((debug_tifffilter & DEBUG_TIFF_F_SELECT),
          ("   %%TIFFselect %p DISPOSE", filter)) ;
}

/** Seek to a particular strip number in the selected channel of a TIFF
    file. */
static int32 tiff_select_seek(/*@notnull@*/ /*@in@*/ FILELIST *filter,
                              /*@notnull@*/ /*@in@*/ const Hq32x2 *offset)
{
  tiff_select_state_t *state ;
  tiff_image_data_t *data ;
  FILELIST *uflptr ;
  int32 result ;
  uint32 strip ;

  HQTRACE((debug_tifffilter & DEBUG_TIFF_F_SELECT),
          ("   %%TIFFselect %p SEEK to strip %u", filter, offset->low)) ;

  if ( !Hq32x2ToUint32(offset, &strip) )
    return EOF ;

  state = theIFilterPrivate(filter) ;
  VERIFY_OBJECT(state, TIFF_SELECT_NAME) ;

  uflptr = theIUnderFile(filter) ;
  HQASSERT(uflptr != NULL &&
           HqMemCmp(uflptr->clist, uflptr->len, NAME_AND_LENGTH("%TIFFbase")) == 0,
           "Underlying filter of %TIFFselect is not %TIFFbase") ;

  data = tiff_base_data(uflptr) ;

  if ( strip >= data->number_strips ) /* Seek beyond last strip */
    return EOF ;

  /* Get strip start and end offsets in TIFFbase. */
  tiff_base_strip(uflptr, state->channel, strip, &state->current, &state->end) ;

  if ( (result = theIMyResetFile(uflptr)(uflptr)) == 0 &&
       (result = theIMySetFilePos(uflptr)(uflptr, &state->current)) == 0 ) {
    uint32 rows_this_strip = data->rows_per_strip ;

    if ( data->number_strips > 1 &&
         strip + 1 == data->number_strips ) /* last strip, update rows */
      rows_this_strip = data->image_length - (data->rows_per_strip * strip);

    if ( state->update_eodcount ) {
      /* Seek on %TIFFbase OK. Update EODCount and Rows in compression
         dictionary if requested. */
      oInteger(inewobj) = (int32)(data->bytes_per_row * rows_this_strip) ;

      HQASSERT(!state->update_eodcount_sfd && !state->update_rows,
               "EODCount and row updates should be mutually exclusive") ;
      HQASSERT(oInteger(inewobj) > 0, "EODCount overflowed") ;
      HQASSERT(oType(state->decompress_params) == ODICTIONARY,
               "Decompression parameters not dictionary") ;

      if ( !tiff_insert_hash(&state->decompress_params, NAME_EODCount, &inewobj) )
        return ioerror_handler(filter) ;
    } else if ( state->update_eodcount_sfd ) {
      /* Seek on %TIFFbase OK. Update EODCount and Rows in SubFileDecode
         dictionary if requested. */
      oInteger(inewobj) = (int32)(data->bytes_per_row * rows_this_strip) ;
      HQASSERT(!state->update_eodcount && !state->update_rows,
               "EODCount and row updates should be mutually exclusive") ;
      if ( !tiff_insert_hash(&state->decompress_params_sfd, NAME_EODCount, &inewobj) )
        return ioerror_handler(filter) ;
    }

    if ( state->update_rows ) {
      oInteger(inewobj) = (int32)rows_this_strip ;

      HQASSERT(!state->update_eodcount && !state->update_eodcount_sfd,
               "EODCount and row updates should be mutually exclusive") ;
      HQASSERT(oInteger(inewobj) > 0, "Number of Rows overflowed") ;
      HQASSERT(oType(state->decompress_params) == ODICTIONARY,
               "Decompression parameters not dictionary") ;

      if ( !tiff_insert_hash(&state->decompress_params, NAME_Rows, &inewobj) )
        return ioerror_handler(filter) ;
    }
  }

  /* After a seek, any existing last char data is invalid. */
  theIFilterState(filter) = FILTER_INIT_STATE ;
  return result ;
}

/** Read a buffer of data from the selected channel of a TIFF file. Note that
    this is a FILTER_FILLBUFF routine, not a decode routine, to avoid the
    automatic close on filter LASTCHAR state, since we need to retain the
    filter state after returning EOF for each strip end. */
static int32 tiff_select_fill(/*@notnull@*/ /*@in@*/ FILELIST *filter)
{
  FILELIST *uflptr ;
  uint8 *uflbuf, *to ;
  int32 count ;
  Hq32x2 remains ;
  tiff_select_state_t *state ;
  tiff_image_data_t *data ;

  HQTRACE((debug_tifffilter & DEBUG_TIFF_F_SELECT),
          ("   %%TIFFselect %p DECODE state %d",
           filter, theIFilterState(filter))) ;

  state = theIFilterPrivate(filter) ;
  VERIFY_OBJECT(state, TIFF_SELECT_NAME) ;

  uflptr = theIUnderFile(filter) ;
  HQASSERT(uflptr != NULL &&
           HqMemCmp(uflptr->clist, uflptr->len, NAME_AND_LENGTH("%TIFFbase")) == 0,
           "Underlying filter of %TIFFselect is not %TIFFbase") ;

  data = tiff_base_data(uflptr) ;

  Hq32x2Subtract(&remains, &state->end, &state->current) ;
  if ( !Hq32x2ToInt32(&remains, &count) ) {
    HQFAIL("Current position greatly differs from end position in %TIFFbase") ;
    return ioerror_handler(filter) ;
  }
  HQASSERT(count >= 0, "%TIFFbase current position greater than end") ;

  /* If at EOF for this strip, return EOF, but don't the state or mark the
     filter with the EOF flags, because that would cause FilterFillBuff to
     auto-close this filter. */
  if ( count == 0 )
    return EOF ;

  /* We MUST seek to the position desired in %TIFFbase, because other
     %TIFFselect filters may have read from %TIFFbase since our last read.
     The %TIFFbase seek routine is smart enough to be able to re-use buffers
     after a reset and seek. */
  if ( theIMyResetFile(uflptr)(uflptr) == EOF ||
       theIMySetFilePos(uflptr)(uflptr, &state->current) == EOF )
    return ioerror_handler(filter) ;

  /* We know the channel once we're called to fill a buffer, so make sure the
     buffer is allocated. */
  if ( theIBuffer(filter) == NULL ) {
    /* Buffer size does not need to exceed maximum strip size for channel. */
    tiff_image_data_t *data ;
    uint8 * buff ;
    uint32 max_strip, *first, *last ;

    data = tiff_base_data(uflptr) ;

    first = &data->strip_bytes[state->channel * data->number_strips] ;
    last = first + data->number_strips ;
    max_strip = *first ;
    while ( ++first < last ) {
      if ( *first > max_strip )
        max_strip = *first ;
    }

    if ( max_strip > TIFF_SELECT_BUFFSIZE )
      max_strip = TIFF_SELECT_BUFFSIZE ;

    buff = mm_alloc(mm_pool_temp,
                    max_strip + 1 ,
                    MM_ALLOC_CLASS_TIFF_BUFFER) ;
    if ( buff == NULL )
      return error_handler(VMERROR) ;

    theIBuffer(filter) = buff + 1 ;
    theIPtr(filter) = theIBuffer(filter) ;
    theIBufferSize(filter) = (int32)max_strip ;
    theICount(filter) = 0 ;
  }

  if ( theIBufferSize(filter) < count )
    count = theIBufferSize(filter) ;

  if ((data->bits_per_sample == 16) && (data->pmi == PMI_CIE_L_a_b_))
                                          /* Ask for an even number of bytes. */
    count &= ~1 ;

  /* Read data. Byte- and word- flip if necessary. */
  if ( !GetFileBuff(uflptr, count, &uflbuf, &count) || count == 0 )
    return ioerror_handler(filter) ;

  /* Update current position */
  Hq32x2AddInt32(&state->current, &state->current, count) ;

  to = theIBuffer(filter) ;
  theIPtr(filter) = to + 1 ;
  theIReadSize(filter) = count ;
  theICount(filter) = count - 1 ;
  theIFilterState(filter) = FILTER_EMPTY_STATE ;

  if (state->flipbits) {

    uint8* from = uflbuf;
    int32 nbytes = count;

    /* Flip bits, but not words */
    for ( ; nbytes >= 8 ; nbytes -= 8, to += 8, from +=8) {
      PENTIUM_CACHE_LOAD(to + 7) ;
      to[0] = reversed_bits_in_byte[from[0]] ;
      to[1] = reversed_bits_in_byte[from[1]] ;
      to[2] = reversed_bits_in_byte[from[2]] ;
      to[3] = reversed_bits_in_byte[from[3]] ;
      to[4] = reversed_bits_in_byte[from[4]] ;
      to[5] = reversed_bits_in_byte[from[5]] ;
      to[6] = reversed_bits_in_byte[from[6]] ;
      to[7] = reversed_bits_in_byte[from[7]] ;
    }

    switch ( nbytes ) {
    case 7:
      to[6] = reversed_bits_in_byte[from[6]] ;
      /*@fallthrough@*/
    case 6:
      to[5] = reversed_bits_in_byte[from[5]] ;
      /*@fallthrough@*/
    case 5:
      to[4] = reversed_bits_in_byte[from[4]] ;
      /*@fallthrough@*/
    case 4:
      to[3] = reversed_bits_in_byte[from[3]] ;
      /*@fallthrough@*/
    case 3:
      to[2] = reversed_bits_in_byte[from[2]] ;
      /*@fallthrough@*/
    case 2:
      to[1] = reversed_bits_in_byte[from[1]] ;
      /*@fallthrough@*/
    case 1:
      to[0] = reversed_bits_in_byte[from[0]] ;
      break ;
    }
  }
  else
    HqMemCpy(to, uflbuf, count) ;

  HQTRACE((debug_tifffilter & DEBUG_TIFF_F_SELECT),
          ("   %%TIFFselect %p FILL size %d first %d state %0x",
           filter, theIReadSize(filter), theIBuffer(filter)[0],
           theIFilterState(filter))) ;

  return theIBuffer(filter)[0] ;
}

/** Build a prototypical %TIFFselect filter in the memory supplied. */
static void tiff_select_filter(/*@notnull@*/ /*@in@*/ FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* flate encode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("%TIFFselect") ,
                       FILTER_FLAG | READ_FLAG,
                       0, NULL, 0,
                       tiff_select_fill,                     /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       tiff_select_init,                     /* initfile */
                       FilterCloseFile,                      /* closefile */
                       tiff_select_dispose,                  /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       tiff_select_seek,                     /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       FilterDecodeError,                    /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}

/****************************************************************************/
/* TIFF channel filter */

#define TIFF_CHANNEL_BUFFSIZE 65536 /* Maximum size for channel data */
#define TIFF_CHANNEL_NAME "%TIFFchannel filter state"

/** Private data for %TIFFchannel filter. */
typedef struct {
  uint32 channel ;     /**< Selected channel. */
  FILELIST *tiffbase ; /**< Reference to underlying %tiffbase. */
  uint32 strip ;       /**< Current strip. */
  Bool eof ;           /**< EOF seen on current strip? */
  uint32 strip_remaining ; /** Bytes left to read in current strip */
  Bool flip16 ;        /**< Flip 16-bit word bytes. */
  uint32 cie_lab_offset; /**< Lab Channel component offset. */
  OBJECT_NAME_MEMBER
} tiff_channel_state_t ;

/** Initialise the private state for a %TIFFchannel filter. */
static Bool tiff_channel_init(/*@notnull@*/ /*@in@*/ FILELIST *filter,
                              /*@notnull@*/ /*@in@*/ OBJECT *args,
                              /*@notnull@*/ /*@in@*/ STACK *stack)
{
  tiff_channel_state_t *state ;
  tiff_image_data_t *data ;
  FILELIST *tiffbase ;
  uint8 *buff ;
  uint32 bufsize ;

  UNUSED_PARAM(OBJECT *, args) ;
  UNUSED_PARAM(STACK *, stack) ;

  tiffbase = filter ;
  do { /* Find underlying %TIFFbase so we can extract expanded strip size. */
    if ( (tiffbase = theIUnderFile(tiffbase)) == NULL ) {
      HQFAIL("No %TIFFbase filter under new %TIFFchannel") ;
      return error_handler(UNDEFINED) ;
    }
  } while ( HqMemCmp(tiffbase->clist, tiffbase->len,
                     NAME_AND_LENGTH("%TIFFbase")) != 0 ) ;

  /* Channel buffer size comes from decompressed strip size, or the linesize
     if the strip size is too big. */
  data = tiff_base_data(tiffbase) ;
  bufsize = data->bytes_per_row * data->rows_per_strip ;
  if ( bufsize > TIFF_CHANNEL_BUFFSIZE ) { /* Strip is unreasonably large */
    bufsize = data->bytes_per_row ;
    if ( bufsize > TIFF_CHANNEL_BUFFSIZE ) /* Line is unreasonably large */
      bufsize = TIFF_CHANNEL_BUFFSIZE ;
  }

  /* allocate a state structure */
  state = mm_alloc(mm_pool_temp,
                   sizeof(tiff_channel_state_t),
                   MM_ALLOC_CLASS_TIFF_FILTER_STATE) ;
  if ( state == NULL )
    return error_handler(VMERROR) ;

  theIFilterPrivate(filter) = state ;

  state->tiffbase = tiffbase ;
  state->strip = 0 ;  /* Next strip to fetch */
  state->eof = TRUE ; /* Require a seek to start */
  state->strip_remaining = 0 ; /* Bytes left to read (not yet setup a strip) */
  state->flip16 = data->shortswap ;
  state->cie_lab_offset = 0 ;

  NAME_OBJECT(state, TIFF_CHANNEL_NAME);

  buff = mm_alloc(mm_pool_temp, bufsize + 1, MM_ALLOC_CLASS_TIFF_BUFFER) ;
  if ( buff == NULL )
    return error_handler(VMERROR) ;

  theIBuffer(filter) = buff + 1 ;
  theIPtr(filter) = theIBuffer(filter) ;
  theIBufferSize(filter) = bufsize ;
  theICount(filter) = 0 ;
  theIFilterState(filter) = FILTER_INIT_STATE ;

  HQTRACE((debug_tifffilter & DEBUG_TIFF_F_CHANNEL),
          (" %%TIFFchannel %p CREATE", filter)) ;

  return TRUE ;
}

/** Destroy the private data for a %TIFFchannel filter. */
static void tiff_channel_dispose(/*@notnull@*/ /*@in@*/ FILELIST* filter)
{
  tiff_channel_state_t *state ;

  state = theIFilterPrivate(filter) ;
  if ( state != NULL ) {
    VERIFY_OBJECT(state, TIFF_CHANNEL_NAME) ;
    UNNAME_OBJECT(state) ;

    mm_free(mm_pool_temp, state, sizeof(tiff_channel_state_t)) ;
    theIFilterPrivate(filter) = NULL ;
  }

  if ( theIBuffer(filter) ) {
    mm_free(mm_pool_temp, theIBuffer(filter) - 1, theIBufferSize(filter) + 1) ;
    theIBuffer(filter) = NULL ;
  }

  HQTRACE((debug_tifffilter & DEBUG_TIFF_F_CHANNEL),
          (" %%TIFFchannel %p DISPOSE", filter)) ;
}

/** Recursive function to find %TIFFselect underlying filter and seek to next
    strip. Overlying decompression filters are then re-opened with
    appropriate parameters. */
static Bool tiff_channel_strip(/*@notnull@*/ /*@in@*/ FILELIST *filter,
                               uint32 strip,
                               /*@notnull@*/ /*@out@*/ Bool *ateof)
{
  HQASSERT(filter != NULL, "No filter for %TIFFchannel strip seek") ;

  if ( HqMemCmp(filter->clist, filter->len, NAME_AND_LENGTH("%TIFFselect")) == 0 ) {
    Hq32x2 pos ;

    Hq32x2FromUint32(&pos, strip) ;
    if ( theIMyResetFile(filter)(filter) == EOF ||
         theIMySetFilePos(filter)(filter, &pos) == EOF ) {
      HQTRACE((debug_tifffilter & DEBUG_TIFF_F_SELECT),
              ("   %%TIFFselect %p EOF state %d", filter, theIFilterState(filter))) ;
      *ateof = TRUE ;
      return FALSE ;
    }

    *ateof = FALSE ;
    return TRUE ;
  }

  if ( !tiff_channel_strip(theIUnderFile(filter), strip, ateof) )
    return FALSE ;

  /* Close decompression filter if currently open, avoiding automatic close
     of underlying filter. */
  if ( isIOpenFile(filter) ) {
    Bool presCST = isICST(filter) ;
    ClearICSTFlag(filter) ;
    HQTRACE((debug_tifffilter & DEBUG_TIFF_F_DECOMPRESS),
            ("  Filter %.*s %p CLOSE", filter->len, filter->clist, filter)) ;
    (void)theIMyCloseFile(filter)(filter, CLOSE_EXPLICIT) ;
    if ( presCST )
      SetICSTFlag(filter) ;
    ClearIOpenFlag(filter) ;
    SetIEofFlag(filter) ;
  }

  /* Re-initialise decompression filter, possibly using new parameters in
     dictionary. */
  HQTRACE((debug_tifffilter & DEBUG_TIFF_F_DECOMPRESS),
          ("  Filter %.*s %p INIT", filter->len, filter->clist, filter)) ;
  if ( !theIMyInitFile(filter)(filter, &theIParamDict(filter), NULL) )
    return theIFileLastError(filter)(filter) ;

  SetIOpenFlag(filter) ;
  ClearIEofFlag(filter) ;

  return TRUE ;
}

static void do_shifts_and_swaps( tiff_image_data_t *data,
                                 tiff_channel_state_t *state,
                                 FILELIST *filter)
{
  HQASSERT(data , "data NULL in do_shifts_and_swaps") ;
  HQASSERT(state, "state NULL in do_shifts_and_swaps") ;
  HQASSERT(filter, "filter NULL in do_shifts_and_swaps") ;

  if ( state->flip16 ) {
    uint8 *to = theIBuffer(filter);
    int32 count = theIBufferSize(filter);

    HQASSERT(to , "No destination buffer in do_shifts_and_swaps") ;

    HQASSERT((count & 1) == 0, "Odd number of bytes in 16-bit input") ;
    BYTE_SWAP16_BUFFER(to, to, count) ;
  }

  if (data->pmi == PMI_CIE_L_a_b_) {
    /* if the image is a lab color we need to munge the data */
    tiff_Lab_shift(data->bits_per_sample,
                   theIBuffer(filter), theIBufferSize(filter),
                   &state->cie_lab_offset,
                   (data->planar_config == PLANAR_CONFIG_CHUNKY));
  }
}

/** Buffer decode for %TIFFchannel. This reads the strip decompression filter
    until it reaches EOF, then increments the %TIFFselect strip, resets the
    filter chain, and continues. If the seek to the next strip returns EOF,
    then this routine returns no bytes. */
static Bool tiff_channel_decode(/*@notnull@*/ /*@in@*/ FILELIST *filter,
                                /*@notnull@*/ /*@in@*/ int32 *ret_bytes)
{
  tiff_image_data_t *data ;
  tiff_channel_state_t *state ;
  FILELIST *uflptr ;
  uint8 *uflbuf, *buffer ;
  int32 remaining ;

  HQTRACE((debug_tifffilter & DEBUG_TIFF_F_CHANNEL),
          (" %%TIFFchannel %p DECODE state %d",
           filter, theIFilterState(filter))) ;

  uflptr = theIUnderFile(filter) ;
  HQASSERT(uflptr != NULL, "No underlying file for %TIFFchannel") ;

  state = theIFilterPrivate(filter) ;
  VERIFY_OBJECT(state, TIFF_CHANNEL_NAME) ;

  data = tiff_base_data(state->tiffbase) ;

  *ret_bytes = 0 ;
  buffer = theIBuffer(filter) ;
  remaining = theIBufferSize(filter) ;

  do {
    int32 gotbytes ;

    if ( state->eof || state->strip_remaining == 0 ) {
      /* If already seen eof or read all the strip data anyway, increment strip,
         seek to next strip and re-initialise decompression filters. */
      if ( !tiff_channel_strip(uflptr, state->strip++, &state->eof) ) {
        if ( state->eof ) {
          do_shifts_and_swaps(data,state,filter);
          *ret_bytes = -*ret_bytes ;
          return TRUE ;
        }
        return error_handler(IOERROR) ;
      }
      state->strip_remaining = data->bytes_per_row * data->rows_per_strip;
    }

    if ( GetFileBuff(uflptr, remaining, &uflbuf, &gotbytes) ) {
      HqMemCpy(buffer, uflbuf, gotbytes) ;
      *ret_bytes += gotbytes ;
      buffer += gotbytes ;
      remaining -= gotbytes ;
      state->strip_remaining -= gotbytes ;
    } else {
      if ( isIIOError(uflptr) )
        return error_handler(IOERROR) ;

      HQASSERT(gotbytes == 0, "EOF on underlying filter but bytes returned") ;

      /* EOF on underlying filter */
      state->eof = TRUE ;
    }
  } while ( remaining > 0 ) ;

  do_shifts_and_swaps(data,state,filter);

  HQTRACE((debug_tifffilter & DEBUG_TIFF_F_CHANNEL),
          (" %%TIFFchannel %p DECODE size %d state %d",
           filter, *ret_bytes, theIFilterState(filter))) ;
  return TRUE ;
}

/** Build a prototypical %TIFFchannel filter in the memory supplied. */
static void tiff_channel_filter(/*@notnull@*/ /*@in@*/ FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* flate encode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("%TIFFchannel") ,
                       FILTER_FLAG | READ_FLAG,
                       0, NULL, 0,
                       FilterFillBuff,                       /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       tiff_channel_init,                    /* initfile */
                       FilterCloseFile,                      /* closefile */
                       tiff_channel_dispose,                 /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       tiff_channel_decode,                  /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}

/****************************************************************************/
/** Initialise the filter chain for a given channel of a TIFF file, layering
    the %TIFFselect, decompression filters and %TIFFchannel filters on top of
    %TIFFbase. Note that this function relies on knowledge of the %TIFFselect
    filter state. */
static Bool tiff_decode_channel_init(/*@notnull@*/ /*@out@*/ FILELIST **channel,
                                     /*@notnull@*/ /*@out@*/ FILELIST **select,
                                     uint32 index,
                                     /*@notnull@*/ /*@in@*/ FILELIST *tiffbase)
{
  FILELIST prototype ;
  mm_pool_t pool = tiff_base_pool(tiffbase) ;
  OBJECT noparams = OBJECT_NOTVM_NULL ;
  tiff_select_state_t *select_st ;
  tiff_image_data_t *data ;
  tiff_channel_state_t *channel_state;
  Bool bigendian;

  HQASSERT(channel != NULL, "No where to put TIFF channel filter chain") ;
  HQASSERT(*channel == NULL, "TIFF channel filter chain aleady initialised") ;
  HQASSERT(tiffbase != NULL, "No %TIFFbase filter") ;

  /* Put select filter at start of chain, just above %TIFFbase. */
  tiff_select_filter(&prototype) ;
  theIUnderFile(&prototype) = tiffbase ;
  theIUnderFilterId(&prototype) = theIFilterId(tiffbase) ;

  if ( !filter_create(&prototype, channel, &noparams, NULL) )
    return FALSE ;

  /* Set the channel we want to extract. */
  *select = *channel;
  select_st = theIFilterPrivate(*channel) ;
  VERIFY_OBJECT(select_st, TIFF_SELECT_NAME) ;
  select_st->channel = index ;

  data = tiff_base_data(tiffbase) ;

  switch ( data->compression ) {
    uint32 eodcount ;
  case COMPRESS_CCITT:
    select_st->update_rows = (data->number_strips > 1) ;
    if ( !tiff_ccitt_filter(pool, data, data->rows_per_strip,
                            &select_st->decompress_params) ||
         !filter_layer(*channel,
                       NAME_AND_LENGTH("CCITTFaxDecode"),
                       &select_st->decompress_params, channel) )
      return FALSE ;
    break ;
  case COMPRESS_CCITT_T4:
    select_st->update_rows = (data->number_strips > 1) ;
    if ( !tiff_ccitt4_filter(pool, data, data->rows_per_strip,
                             &select_st->decompress_params) ||
         !filter_layer(*channel,
                       NAME_AND_LENGTH("CCITTFaxDecode"),
                       &select_st->decompress_params, channel) )
      return FALSE ;
    break ;
  case COMPRESS_CCITT_T6:
    if ( !tiff_ccitt6_filter(pool, data, &select_st->decompress_params) ||
         !filter_layer(*channel,
                       NAME_AND_LENGTH("CCITTFaxDecode"),
                       &select_st->decompress_params, channel) )
      return FALSE ;
    break ;
  case COMPRESS_LZW:
    if ( get_core_context()->tiff6params->f_strict ) {
      /* Have LZW decoder look for EOD as per spec */
      eodcount = 0;
    } else { /* Read upto but not including the EOD */
      select_st->update_eodcount = (data->number_strips > 1) ;
      eodcount = data->bytes_per_row * data->rows_per_strip;
    }
    bigendian = !data->shortswap;
    if ( !tiff_lzw_filter(pool, data, eodcount, bigendian, &select_st->decompress_params) ||
         !filter_layer(*channel,
                       NAME_AND_LENGTH("LZWDecode"),
                       &select_st->decompress_params, channel) )
      return FALSE ;
    break ;
  case COMPRESS_JPEG_OLD:
  case COMPRESS_JPEG:
    /* Experimentally, the most widely-used TIFF library doesn't flip bits for
       JPEG compression. */
    select_st->flipbits = FALSE ;
    if ( !tiff_jpeg_filter(pool, data, &select_st->decompress_params,
                           data->compression == COMPRESS_JPEG_OLD) ||
         !filter_layer(*channel,
                       NAME_AND_LENGTH("DCTDecode"),
                       &select_st->decompress_params, channel) )
      return FALSE ;
    break ;
  case COMPRESS_FLATE:
  case COMPRESS_FLATE_TIFFLIB:
    if ( data->predictor != 1 &&
         !tiff_flate_filter(pool, data, &select_st->decompress_params) )
      return FALSE ;

    if ( !filter_layer(*channel,
                       NAME_AND_LENGTH("FlateDecode"),
                       &select_st->decompress_params, channel) )
      return FALSE ;

    break ;
  case COMPRESS_Packbits:
    select_st->update_eodcount_sfd = TRUE;
    if ( !filter_layer(*channel,
                       NAME_AND_LENGTH("RunLengthDecode"),
                       &select_st->decompress_params, channel) )
      return FALSE ;
    if (!tiff_subfiledecode_filter(pool,&select_st->decompress_params_sfd))
      return FALSE ;
    if ( !filter_layer(*channel,
                       NAME_AND_LENGTH("SubFileDecode"),
                       &select_st->decompress_params_sfd, channel) )
      return FALSE ;
    break ;
  default:
    HQFAIL("Unknown TIFF compression scheme") ;
    /*@fallthrough@*/
  case COMPRESS_None:
    break ;
  }

  /* Put channel filter on top to control strip re-building. */
  tiff_channel_filter(&prototype) ;
  theIUnderFile(&prototype) = *channel ;
  theIUnderFilterId(&prototype) = theIFilterId(*channel) ;

  if ( !filter_create(&prototype, channel, &noparams, NULL) )
    return FALSE ;

  /* Set the channel we want to extract. */
  channel_state = theIFilterPrivate(*channel) ;
  VERIFY_OBJECT(channel_state, TIFF_CHANNEL_NAME) ;
  channel_state->channel = index ;

  return TRUE ;
}

/****************************************************************************/
/* TIFFDecode filter */

#define TIFF_DECODE_BUFFSIZE 16384
#define TIFF_DECODE_NAME "TIFFDecode filter state"

/** Private data for TIFFDecode image filter. */
typedef struct {
  /*@observer@*/
  FILELIST *tiffbase ;  /**< Reference to underlying %tiffbase. */
  uint32 nchannels ;    /**< Number of channels */
  /*@owned@*/
  FILELIST **channels ; /**< Array of tiffchannel pointers. */
  FILELIST **selects ; /**< Array of tiffselect pointers. */

  OBJECT_NAME_MEMBER
} tiff_decode_state_t ;

/** Initialise the private state for a TIFFDecode filter. */
static Bool tiff_decode_init(/*@notnull@*/ /*@in@*/ FILELIST *filter,
                             /*@notnull@*/ /*@in@*/ OBJECT *args,
                             /*@notnull@*/ /*@in@*/ STACK *stack)
{
  FILELIST prototype, *tiffbase ;
  tiff_decode_state_t *state ;
  uint32 i, bufsize ;
  uint8 *buff ;

  tiff_base_filter(&prototype) ;

  /* args and stack are handed over to %TIFFbase filter. */
  if ( !filter_create(&prototype, &tiffbase, args, stack) )
    return FALSE ;

  /* allocate a state structure */
  state = mm_alloc(mm_pool_temp,
                   sizeof(tiff_decode_state_t),
                   MM_ALLOC_CLASS_TIFF_FILTER_STATE) ;
  if ( state == NULL ) {
    (void)theIMyCloseFile(tiffbase)(tiffbase, CLOSE_EXPLICIT) ;
    return error_handler(VMERROR) ;
  }

  theIFilterPrivate(filter) = state ;

  /* quick dirty initialization */
  HqMemZero((uint8 *)state, sizeof(tiff_decode_state_t));
  NAME_OBJECT(state, TIFF_DECODE_NAME);

  state->tiffbase = tiffbase ;
  state->nchannels = tiff_base_nchannels(tiffbase) ;
  if ( (state->channels = mm_alloc(mm_pool_temp,
                                   sizeof(FILELIST) * state->nchannels,
                                   MM_ALLOC_CLASS_TIFF_FILTER_STATE)) == NULL ) {
    return error_handler(VMERROR) ;
  }
  HqMemZero((uint8 *)state->channels, sizeof(FILELIST) * state->nchannels) ;
  if ( (state->selects = mm_alloc(mm_pool_temp,
                                   sizeof(FILELIST) * state->nchannels,
                                   MM_ALLOC_CLASS_TIFF_FILTER_STATE)) == NULL ) {
    return error_handler(VMERROR) ;
  }
  HqMemZero((uint8 *)state->selects, sizeof(FILELIST) * state->nchannels) ;

  for ( i = 0 ; i < state->nchannels ; ++i ) {
    if ( !tiff_decode_channel_init(&state->channels[i],
                                   &state->selects[i],
                                   tiff_base_mapped_channel(tiffbase, i),
                                   tiffbase) )
      return FALSE ;
  }

  /* Buffer size need be no larger than the number of channels times the
     channel buffer sizes. */
  bufsize = theIBufferSize(state->channels[0]) * state->nchannels ;
  if ( bufsize > TIFF_DECODE_BUFFSIZE )
    bufsize = TIFF_DECODE_BUFFSIZE ;

  buff = mm_alloc(mm_pool_temp,
                  bufsize + 1 ,
                  MM_ALLOC_CLASS_TIFF_BUFFER) ;
  if ( buff == NULL )
    return error_handler(VMERROR) ;

  theIBuffer(filter) = buff + 1 ;
  theIPtr(filter) = theIBuffer(filter) ;
  theIBufferSize(filter) = bufsize ;
  theICount(filter) = 0 ;
  theIFilterState(filter) = FILTER_EMPTY_STATE ;

  HQTRACE((debug_tifffilter & DEBUG_TIFF_F_DECODE),
          ("TIFFDecode %p CREATE", filter)) ;

  return TRUE ;
}

/** Destroy the private data for a TIFFDecode filter. */
static void tiff_decode_dispose(/*@notnull@*/ /*@in@*/ FILELIST* filter)
{
  tiff_decode_state_t *state ;
  FILELIST *channel, *under_channel;
  FILELIST *select, *under_select;
  uint32 i ;

  state = theIFilterPrivate(filter) ;
  if ( state != NULL ) {
    VERIFY_OBJECT(state, TIFF_DECODE_NAME) ;
    UNNAME_OBJECT(state) ;

    /* we have here a belt and braces method of tearing down the filter chain
       We can't just walk it, as some of the intermediate filters may have
       been closed (or at worst case re-used).  It's OK to walk the chain starting
       at tiffselect, and then walk it again starting at tiffchannel.  This
       ensures everything gets closed exactly once */

    if ( state->selects != NULL ) {

      /* first close all the filters starting at tiffselect */
      for ( i = 0 ; i < state->nchannels ; ++i ) {
        select = state->selects[i] ;
        if ( select && isIOpenFile(select) ) {
          while ( select != NULL && select != state->tiffbase ) {
            under_select = theIUnderFile(select);
            if ( under_select && (!isIOpenFileFilterById( theIUnderFilterId( select ) , under_select )))
              under_select = NULL;
            if ( isIOpenFile(select) ) {
              (void)theIMyCloseFile(select)(select, CLOSE_EXPLICIT) ;
            }
            select = under_select ;
          }
        }
      }
    }

    if ( state->channels != NULL ) {
      /* now close the rest */
      for ( i = 0 ; i < state->nchannels ; ++i ) {
        channel = state->channels[i] ;
        while ( channel != NULL && channel != state->tiffbase ) {
          under_channel = theIUnderFile(channel);
          if ( under_channel && (!isIOpenFileFilterById( theIUnderFilterId( channel ) , under_channel )))
            under_channel = NULL;
          if ( isIOpenFile(channel) )
          {
            (void)theIMyCloseFile(channel)(channel, CLOSE_EXPLICIT) ;
          }
          channel = under_channel ;
        }
      }
    }
    if (state->channels != NULL)
      mm_free(mm_pool_temp, state->channels, sizeof(FILELIST) * state->nchannels ) ;
    if (state->selects != NULL)
      mm_free(mm_pool_temp, state->selects, sizeof(FILELIST) * state->nchannels ) ;

    if ( state->tiffbase != NULL )
      (void)theIMyCloseFile(state->tiffbase)(state->tiffbase, CLOSE_EXPLICIT) ;

    mm_free(mm_pool_temp, state, sizeof(tiff_decode_state_t)) ;
    theIFilterPrivate(filter) = NULL ;

  }
  if ( theIBuffer(filter) ) {
    mm_free(mm_pool_temp, theIBuffer(filter) - 1, theIBufferSize(filter) + 1) ;
    theIBuffer(filter) = NULL ;
  }

  HQTRACE((debug_tifffilter & DEBUG_TIFF_F_DECODE),
          ("TIFFDecode %p DISPOSE", filter)) ;
}

/** Fill buffer for TIFFDecode. This routine will not be called when using
    tiffexec or the image context, so the inefficiency of interleaving the
    channels only to de-interleave them into the image store is not a
    problem. Note that this is a FILELIST_FILLBUFF routine, not a filter
    decode routine, because TIFFDecode does not have a direct underlying
    file. */
static int32 tiff_decode_fill(/*@notnull@*/ /*@in@*/ FILELIST *filter)
{
  tiff_decode_state_t *state ;
  uint32 i ;
  int32 readsize = 0, result = 0 ;
  tiff_image_data_t *data ;

  HQTRACE((debug_tifffilter & DEBUG_TIFF_F_DECODE),
          ("TIFFDecode %p FILL state %0x",
           filter, theIFilterState(filter))) ;

  state = theIFilterPrivate(filter) ;
  VERIFY_OBJECT(state, TIFF_DECODE_NAME) ;

  data = tiff_base_data(state->tiffbase) ;

  if ( state->nchannels == 1 ) {
    /* Interleaved or single channel data doesn't need re-interleaving, and
       so can be returned as-is (as long as there are not extra samples to skip). */
    FILELIST *channel = state->channels[0] ;
    uint8 *from;

    if ( !GetFileBuff(channel, theIBufferSize(filter), &from, &readsize) ) {
      if ( isIIOError(channel) )
        (void)ioerror_handler(filter) ;
      result = EOF ;
    } else {
      HqMemCpy(theIBuffer(filter), from, readsize) ;
    }
  } else {
    /* Read from each of the underlying sources, and interleave the data. If
       we have an alpha channel, we could try to interleave it by scanline,
       but it's not worth the extra complexity, so we don't. The maximum
       chunk size we will read is determined by the number of channels we
       need to interleave into the output buffer. */
    int32 chunksize = theIBufferSize(filter) / state->nchannels ;
    Bool is16bit = (data->bits_per_sample == 16) ;

    /* Request an even number of bytes for 16-bit data. */
    if ( is16bit )
      chunksize &= ~1 ;

    for ( i = 0 ; i < state->nchannels ; ++i ) {
      FILELIST *channel = state->channels[i] ;
      uint8 *from ;
      int32 gotbytes ;

      if ( !GetFileBuff(channel, chunksize, &from, &gotbytes) ) {
        if ( isIIOError(channel) )
          (void)ioerror_handler(filter) ;
        result = EOF ;
      }

      /* Need the same number of bytes in each channel; scan all channels
         before returning with EOF, in case one is longer than the others. */
      if ( gotbytes != chunksize ) {
        if ( i > 0 )
          result = ioerror_handler(filter) ;
        chunksize = gotbytes ;
      }

      if ( result != EOF ) {
        readsize += gotbytes ;

        if ( is16bit ) { /* 16-bit interleaving */
          register uint32 stride = (state->nchannels << 1) ;
          register uint8 *to = theIBuffer(filter) + (i << 1) ;

          for ( ; gotbytes >= 2 ; gotbytes -= 2 ) {
            to[0] = from[0] ;
            to[1] = from[1] ;
            from += 2 ;
            to += stride ;
          }

          if ( gotbytes != 0 ) {
            uint8 lastch = from[0];
            int32 nextch = Getc(channel) ;

            /* The buffer we read was an odd size. Pushing back the byte
               before requesting a new buffer probably won't do any good,
               we'll get just that byte back in the next buffer. Instead, we
               read the next character. */
            if ( nextch != EOF ) {
              to[0] = lastch ;
              to[1] = CAST_TO_UINT8(nextch) ;
              ++readsize ;
            } else
              result = ioerror_handler(filter) ;
          }
        } else { /* 8-bit interleaving */
          register uint32 stride = state->nchannels ;
          register uint8 *to = theIBuffer(filter) + i ;

          for ( ; gotbytes > 0 ; --gotbytes ) {
            *to = *from ;
            ++from ;
            to += stride ;
          }
        }
      }
    }
  }

  if ( readsize == 0 ) { /* At EOF */
    SetIEofFlag(filter) ;
    theIFilterState(filter) = FILTER_EOF_STATE ;
    result = EOF ;
  }

  if ( result == EOF ) { /* EOF or error. */
    theIReadSize(filter) = 0 ;
    return EOF ;
  }

  theIPtr(filter) = theIBuffer(filter) + 1 ;
  theIReadSize(filter) = readsize ;
  theICount(filter) = readsize - 1 ;

  HQTRACE((debug_tifffilter & DEBUG_TIFF_F_DECODE),
          ("TIFFDecode %p FILL size %d first %d state %0x",
           filter, theIReadSize(filter), theIBuffer(filter)[0],
           theIFilterState(filter))) ;

  return theIBuffer(filter)[0] ; /* Return first character */
}

/** Seek to a location in a TIFF base file. This is only usable for
    completely rewinding the filter before any strip data is read (this is
    needed by the image context code). */
static int32 tiff_decode_seek(/*@notnull@*/ /*@in@*/ FILELIST *filter,
                              /*@notnull@*/ /*@in@*/ const Hq32x2 *offset)
{
  tiff_decode_state_t *state ;

  HQTRACE((debug_tifffilter & DEBUG_TIFF_F_DECODE),
          ("TIFFDecode %p SEEK to %u", filter, offset->low)) ;

  state = theIFilterPrivate(filter) ;
  VERIFY_OBJECT(state, TIFF_DECODE_NAME) ;

  /* Rewind TIFFbase. */
  if ( !Hq32x2IsZero(offset) ||
       theIMyResetFile(state->tiffbase)(state->tiffbase) == EOF ||
       theIMySetFilePos(state->tiffbase)(state->tiffbase, offset) == EOF )
    return ioerror_handler(filter) ;

  return 0 ;
}

/** Return info for TIFF file, deferring implementation to %TIFFbase. */
static Bool tiff_decode_info(/*@notnull@*/ /*@in@*/ FILELIST *filter,
                             /*@null@*/ /*@in@*/ imagefilter_match_t *match)
{
  tiff_decode_state_t *state ;
  tiff_image_data_t *data ;
  USERVALUE d0 = 0.0f, d1 = 1.0f ;
  SYSTEMVALUE xres, yres ;
  OBJECT value = OBJECT_NOTVM_NOTHING ;
  Bool result = FALSE, need_Decode = TRUE ;

  state = theIFilterPrivate(filter) ;
  VERIFY_OBJECT(state, TIFF_DECODE_NAME) ;

  data = tiff_base_data(state->tiffbase) ;

  object_store_integer(&value, (int32)data->image_width) ;
  if ( filter_info_callback(match, NAME_Width, &value, &result) )
    return result ;

  object_store_integer(&value, (int32)data->image_length) ;
  if ( filter_info_callback(match, NAME_Height, &value, &result) )
    return result ;

  object_store_integer(&value, (int32)data->bits_per_sample) ;
  if ( filter_info_callback(match, NAME_BitsPerComponent, &value, &result) )
    return result ;

  object_store_integer(&value, data->alpha ? 12 : 1) ;
  if ( filter_info_callback(match, NAME_ImageType, &value, &result) )
    return result ;

  object_store_bool(&value, data->premult) ;
  if ( filter_info_callback(match, NAME_PreMult, &value, &result) )
    return result ;

  object_store_name(&value, NAME_EmbeddedICCProfile, LITERAL) ;
  if (filter_info_match(&value, match) != NULL) {
      /* We don't bother with the icc profile if the caller isn't interested,
        as this is likely to be an expensive operation when we return the profile
        data rather than the offsets and lengths we do at present */
    if (data->icc_length && data->icc_offset) {
      OBJECT *entry;
      uint32 remaining = data->icc_length ;
      Hq32x2 pos;
      Bool done;

      Hq32x2FromUint32(&pos, data->icc_offset) ;

      /* seek to data->icc_offset in tiffbase */
      if ( theIMyResetFile(state->tiffbase)(state->tiffbase) == EOF ||
           theIMySetFilePos(state->tiffbase)(state->tiffbase, &pos) == EOF )
        return error_handler(IOERROR) ;

      if ( !tiff_alloc_icc_profile(mm_pool_temp, &value, remaining) )
        return FALSE ;

      /* now put the result into the offsets array */
      for ( entry = oArray(value) ; remaining > 0; ++entry)
      {
        uint8 *str;
        uint32 datalen = min(MAXPSSTRING, remaining);

        if ( NULL == (str = (uint8*)mm_alloc(mm_pool_temp,
                                             datalen,
                                             MM_ALLOC_CLASS_IMAGE_CONTEXT))) {
          tiff_free_icc_profile(mm_pool_temp, &value);
          return error_handler(VMERROR) ;
        }

        theTags(*entry) = OSTRING | LITERAL | UNLIMITED ;
        theLen(*entry) = CAST_TO_UINT16(datalen) ;
        oString(*entry) = str;

        if ( file_read(state->tiffbase, str, (int32)datalen, NULL) <= 0 ) {
          /* Chuck error if EOF before got all bytes, or another read error */
          tiff_free_icc_profile(mm_pool_temp, &value);
          return error_handler(IOERROR) ;
        }

        remaining -= datalen ;
      }

      HQASSERT(entry - oArray(value) == theLen(value),
               "ICC chunks calculation was wrong for size") ;

      done = filter_info_callback(match, NAME_EmbeddedICCProfile, &value, &result);
      /* free all our allocated data */
      tiff_free_icc_profile(mm_pool_temp, &value);
      if ( done )
        return result;
    }
  }

  switch ( data->pmi ) {
  case PMI_WhiteIsZero:
    d0 = 1.0f ;
    d1 = 0.0f ;
    /*@fallthrough@*/
  case PMI_BlackIsZero:
    if ( data->compression == COMPRESS_CCITT ||
         data->compression == COMPRESS_CCITT_T4 ||
         data->compression == COMPRESS_CCITT_T6 ) {
      /* CCITT filter reverses if necessary to ensure that black is zero. */
      /** \todo @@@ TODO FIXME ajcd 2005-03-19: when tiffexec is
          re-implemented using TIFFDecode, this case (and the setting of
          BlackIs1 in CCITT filter decode dictionaries) can be removed. */
      d0 = 1.0f ;
      d1 = 0.0f ;
    }
  /*@fallthrough@*/
  case PMI_RGB:
  /* Rather counter-intuitively we treat RGB and grey almost the same as CMYK Separated.
      This is becuse we can have Grey+ spot & RGB + spot */
  /*@fallthrough@*/
  case PMI_Separated:
    {
      int32 ncomps,i;
      uint32 imax;
      USERVALUE  step;


      if (data->pmi == PMI_Separated)
      {
        /* Have to set up decode array for CMYK color space based on dot range */
        step = 1.0f / (data->dot_range[1] - data->dot_range[0]);
        d0 = -(step*(data->dot_range[0]));
        imax = (1<<data->bits_per_sample) - 1;
        d1 = step*(imax - data->dot_range[1]) + 1;
      }
      if ( data->f_plain ) {
        if (( data->pmi == PMI_BlackIsZero ) || ( data->pmi == PMI_WhiteIsZero ))
          object_store_name(&value, NAME_DeviceGray, LITERAL) ;
        else if ( data->pmi == PMI_RGB )
          object_store_name(&value, NAME_DeviceRGB, LITERAL) ;
        else
          object_store_name(&value, NAME_DeviceCMYK, LITERAL) ;

        if ( filter_info_callback(match, NAME_ColorSpace, &value, &result) )
          return result ;
      } else {
        OBJECT cspace = OBJECT_NOTVM_NOTHING ;
        mm_pool_t pool = tiff_base_pool(state->tiffbase) ;

        /* Though there could be up to 65535 channels in a TIFF, let's call
           16 a reasonable limit until someone proves otherwise. We'll fail
           with a RANGECHECK if we get more than this, which is better than
           corrupting the stack as we used to. [66184] */
#define MAX_TIFF_CHANNELS 16
        OBJECT decodearray[MAX_TIFF_CHANNELS*2];

        if ( !tiff_init_noncmyk_colorspace(data,pool))
          return FALSE ;

        if ( !tiff_get_noncmyk_colorspace(data, &cspace)) {
          tiff_destroy_noncmyk_colorspace(data);
          return FALSE;
        }

        HQASSERT(oType(cspace) == OARRAY,
                 "Constructed TIFF colorspace is not array") ;
        HQASSERT(theLen(cspace) >= 2,
                 "Constructed TIFF colorspace array is too short") ;

        if ( filter_info_callback(match, NAME_ColorSpace, &cspace, &result) ) {
          tiff_destroy_noncmyk_colorspace(data);
          return result;
        }

        ncomps  = data->samples_per_pixel - data->extra_samples + data->premult;
        HQASSERT(ncomps <= MAX_TIFF_CHANNELS, "Too many channels in TIFF colorspace") ;
        if (ncomps > MAX_TIFF_CHANNELS)
          return error_handler(RANGECHECK) ;

        /* construct a decode array based upon the names in the DN space returned above */
        HQASSERT(oType(oArray(cspace)[0]) == ONAME,
                 "Constructed TIFF colorspace doesn't start with name") ;
        if (oNameNumber(oArray(cspace)[0]) == NAME_DeviceN) {
          OBJECT *dn_names ;

          HQASSERT(oType(oArray(cspace)[1]) == OARRAY,
                   "DeviceN space names not in an array") ;
          HQASSERT(theLen(oArray(cspace)[1]) == ncomps,
                   "Number of DeviceN colour names did not match components") ;
          dn_names = oArray(oArray(cspace)[1]) ;

          for ( i = 0 ; i < ncomps; ++i )
          {
            HQASSERT(oType(dn_names[i]) == ONAME, "DeviceN colour not an ONAME") ;

            switch ( oNameNumber(dn_names[i]) )
            {
            case NAME_Cyan:
            case NAME_Magenta:
            case NAME_Yellow:
            case NAME_Black:
              if (data->pmi != PMI_RGB) {
                object_store_real(object_slot_notvm(&decodearray[2*i]), d0);
                object_store_real(object_slot_notvm(&decodearray[2*i+1]), d1);
                break;
              }
              /*@fallthrough@*/
            default:
              if ((data->pmi == PMI_RGB) || (data->photoshopinks != e_photoshopinks_none))
              {
                object_store_real(object_slot_notvm(&decodearray[2*i]), d1);
                object_store_real(object_slot_notvm(&decodearray[2*i+1]), d0);
              } else {
                object_store_real(object_slot_notvm(&decodearray[2*i]), d0);
                object_store_real(object_slot_notvm(&decodearray[2*i+1]), d1);
              }
              break;
            }
          }
        } else { /* Not DeviceN */
          for (i=0;i<ncomps;i++)
          {
            object_store_real(object_slot_notvm(&decodearray[2*i]), d0) ;
            object_store_real(object_slot_notvm(&decodearray[2*i+1]), d1) ;
          }
        }

        theTags(value) = OARRAY | LITERAL | UNLIMITED ;  /* Prepare temp to hold an array */
        theLen(value) = CAST_TO_UINT16(2*ncomps) ; /* local array of length 2 */
        oArray(value) = decodearray ;

        /* now free the colorspace we constructed */
        tiff_destroy_noncmyk_colorspace( data );

        if ( filter_info_callback(match, NAME_Decode, &value, &result))
          return result ;

        need_Decode = FALSE;
      }
    }
    break ;
  case PMI_Palette:
  {
    /* construct an indexed colorspace & decode array */
    OBJECT colorspace[4] = {
      OBJECT_NOTVM_NOTHING, OBJECT_NOTVM_NOTHING,
      OBJECT_NOTVM_NOTHING, OBJECT_NOTVM_NOTHING,
    } ;
    OBJECT decodearray[2] = {
      OBJECT_NOTVM_INTEGER(0), OBJECT_NOTVM_NOTHING,
    } ;
    int32  lookup_table_length;

    lookup_table_length  = 1 << data->bits_per_sample;

    object_store_name(&colorspace[0], NAME_Indexed, LITERAL) ;
    object_store_name(&colorspace[1], NAME_DeviceRGB, LITERAL) ;
    object_store_integer(&colorspace[2], lookup_table_length - 1) ; /* usually 255 */

    theTags(colorspace[3]) = OSTRING | LITERAL | READ_ONLY ;
    theLen(colorspace[3]) = CAST_TO_UINT16(lookup_table_length * 3) ;
    oString(colorspace[3]) = data->colormap ;

    theTags(value) = OARRAY | LITERAL | UNLIMITED ;
    theLen(value) = 4 ; /* local array of length 4 */
    oArray(value) = colorspace ;

    if ( filter_info_callback(match, NAME_ColorSpace, &value, &result) )
      return result ;

    /* decodearray[0] set by initialiser */
    object_store_integer(&decodearray[1], lookup_table_length - 1);

    theTags(value) = OARRAY | LITERAL | UNLIMITED ;
    theLen(value) = 2 ; /* local array of length 2 */
    oArray(value) = decodearray ;

    if ( filter_info_callback(match, NAME_Decode, &value, &result))
      return result ;

    need_Decode = FALSE;
    break ;
  }
  case PMI_CIE_L_a_b_:
  case PMI_ICCLab:
  {
    /* We calculate the reference x and z values from the following
       formulae that converts from 1931 CIE chormaticity to the whitepoint:

       refWhite_X = whitePoint_x / whitePoint_y
       refWhite_Y = 1.0
       refWhite_Z = (1.0 - (whitePoint_x + whitePoint_y)) / whitePoint_y
    */
    SYSTEMVALUE xX = ((SYSTEMVALUE)data->whitepoint_x[RATIONAL_NUMERATOR] /
                      (SYSTEMVALUE)data->whitepoint_x[RATIONAL_DENOMINATOR]) ;
    SYSTEMVALUE yY = ((SYSTEMVALUE)data->whitepoint_y[RATIONAL_NUMERATOR] /
                      (SYSTEMVALUE)data->whitepoint_y[RATIONAL_DENOMINATOR]) ;
    SYSTEMVALUE x = xX / yY ;
    SYSTEMVALUE z = (1.0 - (xX + yY)) / yY ;

    if ( filter_info_Lab_CSD(match,
                             (USERVALUE)x, 1.0f, (USERVALUE)z, /* Whitepoint */
                             -128, 127, -128, 127, /* min & max values */
                             &result) )
      return result ;

    /* Decode was handled by the Lab callback. */
    need_Decode = FALSE;

    break;
  }
  default:
    HQFAIL("Unsupported TIFF decode info colorspace") ;
    break ;
  }

  if ( need_Decode ) {
    /* Don't include extra channels in Decode, but PreMult alpha images need an
       extra Decode pair, and doesn't support planar data. */
    if ( filter_info_Decode(match, data->samples_per_pixel - data->extra_samples + data->premult,
                            d0, d1, &result))
      return result ;
  }


  xres = data->x_resolution;
  yres = data->y_resolution;

  if ((xres > 0.0)&&(yres > 0.0) &&  !data->defaulted_resolution)
  {
    switch ( data->res_unit ) {
    default:
      HQFAIL("Unrecognised TIFF unit size") ;
      /*@fallthrough@*/
    case RESUNIT_NONE: /* No units, just pixel sizes */
      break ;
    case RESUNIT_CM: /* dpcm */
      xres *= 2.54 ;
      yres *= 2.54 ;
      /*@fallthrough@*/
    case RESUNIT_INCH: /* dpi */
      object_store_numeric(&value, xres) ;
      if ( filter_info_callback(match, NAME_XResolution, &value, &result) )
        return result ;

      object_store_numeric(&value, yres) ;
      if ( filter_info_callback(match, NAME_YResolution, &value, &result) )
        return result ;

      break ;
    }
  }
  {
    /* ImageMatrix orientation depends on TIFF Orientation tag. */
    SYSTEMVALUE m00, m01, m10, m11, m20, m21 ;

    m20 = m21 = 0.0 ;
    if ( data->orientation < 5 ) { /* Normal rows and columns */
      m00 = data->image_width ;
      m11 = data->image_length ;
      m01 = m10 = 0.0 ;
    } else { /* Swap rows and columns */
      m01 = data->image_length ;
      m10 = data->image_width ;
      m00 = m11 = 0.0 ;
    }

    if ( (data->orientation & 2) != 0 ) { /* Flip X */
      m20 = m00 + m10 ;
      m00 = -m00 ;
      m10 = -m10 ;
    }

    /* Flip Y: normal ImageMatrix orientation has flipped Y for PostScript
       userspace, so this test is the inverse of that implied by the TIFF
       spec. */
    if ( ((data->orientation + 1) & 2) != 0 ) {
      m21 = m01 + m11 ;
      m01 = -m01 ;
      m11 = -m11 ;
    }

    if ( filter_info_ImageMatrix(match, m00, m01, m10, m11, m20, m21, &result) )
      return result ;
  }

  object_store_bool(&value, state->nchannels != 1) ;
  if ( filter_info_callback(match, NAME_MultipleDataSources, &value, &result) )
    return result ;

  /* Separate sources if planar, sample interleaved otherwise. */
  object_store_integer(&value, state->nchannels == 1 ? 1 : 3) ;
  if ( filter_info_callback(match, NAME_InterleaveType, &value, &result) )
    return result ;

  if ( state->nchannels == 1 ) {
    file_store_object(&value, filter, LITERAL) ;
    if ( filter_info_callback(match, NAME_DataSource, &value, &result) )
      return result ;
  } else {
    imagefilter_match_t *sourcematch ;

    object_store_name(&value, NAME_DataSource, LITERAL) ;
    if ( (sourcematch = filter_info_match(&value, match)) != NULL ) {
      OBJECT sources[5] ;
      Bool done ;
      uint32 i ;

      HQASSERT(!data->premult, "Planar premultiplied alpha is not supported by image operator") ;

      /* Set up DataSource array for planar TIFFs. Note that we DO include the
         alpha channel in the DataSource array, we don't have a way of
         returning it separately. */
      theTags(value) = OARRAY | LITERAL | READ_ONLY ;
      theLen(value) = CAST_UNSIGNED_TO_UINT16(state->nchannels) ;
      oArray(value) = &sources[0] ;

      if ( theLen(value) > NUM_ARRAY_ITEMS(sources) &&
           (oArray(value) = mm_alloc(mm_pool_temp,
                                     sizeof(OBJECT) * theLen(value),
                                     MM_ALLOC_CLASS_IMAGE_CONTEXT)) == NULL )
        return error_handler(VMERROR) ;

      for ( i = 0 ; i < theLen(value) ; ++i ) {
        file_store_object(object_slot_notvm(&oArray(value)[i]),
                          state->channels[i], LITERAL) ;
      }

      done = filter_info_callback(sourcematch, NAME_DataSource, &value, &result) ;

      if ( oArray(value) != &sources[0] ) {
        mm_free(mm_pool_temp, oArray(value), sizeof(OBJECT) * theLen(value)) ;
      }

      if ( done )
        return result ;
    }
  }

  return TRUE ;
}

/** Build a prototypical TIFFDecode filter in the memory supplied. */
void tiff_decode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* flate encode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("TIFFDecode") ,
                       FILTER_FLAG | READ_FLAG,
                       0, NULL, 0,
                       tiff_decode_fill,                     /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       tiff_decode_init,                     /* initfile */
                       FilterCloseFile,                      /* closefile */
                       tiff_decode_dispose,                  /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       tiff_decode_seek,                     /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       FilterDecodeError,                    /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;

  flptr->decodeinfo = tiff_decode_info ;
}

/****************************************************************************/
/* Utility functions */

Bool tiff_signature_test(FILELIST *flptr)
{
  static const uint8 tiff_le[4] = {
    0x49, 0x49, 42, 0
  };

  static const uint8 tiff_be[4] = {
    0x4d, 0x4d, 0, 42
  };

  if ( isIIOError(flptr) ||
       !isIInputFile(flptr) ||
       !isIOpenFile(flptr) ||
       !EnsureNotEmptyFileBuff(flptr) ||
       theICount(flptr) < NUM_ARRAY_ITEMS(tiff_le) )
    return FALSE ; /* These are all reasons for it not to be a TIFF file */

  return (HqMemCmp(theIPtr(flptr), NUM_ARRAY_ITEMS(tiff_le),
                   tiff_le, NUM_ARRAY_ITEMS(tiff_le)) == 0 ||
          HqMemCmp(theIPtr(flptr), NUM_ARRAY_ITEMS(tiff_be),
                   tiff_be, NUM_ARRAY_ITEMS(tiff_be)) == 0) ;
}

/*
Log stripped */
