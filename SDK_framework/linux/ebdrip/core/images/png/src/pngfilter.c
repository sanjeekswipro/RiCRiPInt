/** \file
 * \ingroup png
 *
 * $HopeName: COREpng!src:pngfilter.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * PNG filter wrapper functions. These functions manage a filter to
 * decode PNG data; the implementation is provided by the libpng
 * library.
 */

#include "core.h"
#include "coreinit.h"
#include "swerrors.h"
#include "mmcompat.h"
#include "png.h"
#include "fileio.h"
#include "filterinfo.h"
#include "objects.h"
#include "objnamer.h"
#include "objstack.h"
#include "hqmemcpy.h"
#include "hqmemset.h"
#include "namedef_.h"

#define PNGSTATE_NAME "PNG filter state"

/**
 * Private data for PNG image filter.
 */
typedef struct PNGSTATE {
  png_structp png;

  png_infop info;
  uint32 width;
  uint32 height;
  int32 bit_depth;
  int32 color_type;
  int32 interlace_type;
  int32 alpha_state;
  uint32 ncomps; /* excludes alpha channel */
  USERVALUE xresolution, yresolution ;

  uint32 row_index;
  uint32 rowbytes_data;
  uint32 rowbytes_alloc;
  uint8* row_pointer; /* for non-interleaved PNGs */
  uint8** row_pointers; /* for interleaved PNGs */

  OBJECT_NAME_MEMBER
} PNGSTATE;

#if defined( ASSERT_BUILD )
static Bool png_debug = TRUE;
static Bool png_scribble = FALSE;
#define ROW_SCRIBBLE_VAL (0x33)
#define ROW_SCRIBBLE(_row, _n) \
MACRO_START \
  if (png_scribble) { \
    uint32 _i_; \
    for (_i_ = 0; _i_ < (_n); ++_i_) \
      (_row)[_i_] = ROW_SCRIBBLE_VAL; \
  } \
MACRO_END
#else
#define ROW_SCRIBBLE(_row, _n) EMPTY_STATEMENT()
#endif

#if defined( ASSERT_BUILD )
/* Check libpng isn't writing off the end of the row buffers. */
#define ROW_END_MARKER (0x55)
#define ROW_END_SET(_row, _i) ((_row)[(_i)]) = ROW_END_MARKER;
#define ROW_END_ASSERT(_row, _i) HQASSERT(((_row)[(_i)]) == ROW_END_MARKER, \
                                 "libpng has written beyond row memory!");
#else
#define ROW_END_SET(_row, _i) EMPTY_STATEMENT()
#define ROW_END_ASSERT(_row, _i) EMPTY_STATEMENT()
#endif

/**
 * Description of the alpha channel in the PNG image
 */
enum
{
  PNG_UNK_INFO = -2,      /* not known yet - called from contextinfo */
  PNG_UNK_READ = -1,      /* not known yet - called from read */
  PNG_NO_ALPHA = 1,       /* No alpha channel present */
  PNG_FULL_ALPHA = 2,     /* Values across the range 0...255 */
  PNG_MASK_ALPHA = 3,     /* Just 0 and 255 */
};

/**
 * Callback function to allocate libpng memory from the temp pool.
 * \param[in] png   handle to PNG data
 * \param[in] size  number of bytes to allocate
 * \return          pointer to allocated memory
 */
static void* png_malloc_callback(png_structp png, png_size_t size)
{
  void *alloc;

  UNUSED_PARAM(png_structp, png);

  alloc = mm_alloc_with_header(mm_pool_temp, size, MM_ALLOC_CLASS_PNG_LIBPNG);
  if (alloc == NULL)
    (void) error_handler(VMERROR);

  return alloc;
}

/**
 * Callback function to free libpng memory back to the temp pool.
 * \param[in] png   handle to PNG data
 * \param[in] mem   Pointer to memory to be freed
 */
static void png_free_callback(png_structp png, png_voidp mem)
{
  UNUSED_PARAM(png_structp, png);

  mm_free_with_header(mm_pool_temp, mem);
}

/**
 * Callback function to read libpng data from a \c FILELIST.
 * \param[in]      png    handle to PNG data
 * \param[in,out]  data   Pointer to buffer into which data is read
 * \param[in]      length size of buffer in bytes
 */
static void PNGAPI png_read_callback(png_structp png,
                                     png_bytep data,
                                     png_size_t length)
{
  FILELIST* flptr = png->io_ptr;

  HQASSERT(length <= MAXINT32, "length is too large") ;

  if ( file_read(flptr, data, (int32)length, NULL) <= 0 )
    png_error(png, "PNG read callback failure") ;
}

/**
 * Callback function to report libpng errors in assert builds.
 * \param[in] png       handle to PNG data
 * \param[in] error_msg string containing error message
 */
static void PNGAPI png_error_callback(png_structp png,
                                      png_const_charp error_msg)
{
  UNUSED_PARAM(png_structp, png);
  UNUSED_PARAM(png_const_charp, error_msg);

  /* @@@ internationalise msg or just debug? */
  HQTRACE(png_debug, ("PNG ERROR: %s", error_msg));
}

/**
 * Callback function to report libpng warnings in assert builds.
 * \param[in] png         handle to PNG data
 * \param[in] warning_msg string containing warning message
 */
static void PNGAPI png_warning_callback(png_structp png,
                                        png_const_charp warning_msg)
{
  UNUSED_PARAM(png_structp, png);
  UNUSED_PARAM(png_const_charp, warning_msg);

  /* @@@ internationalise msg or just debug? */
  HQTRACE(png_debug, ("PNG WARNING: %s", warning_msg));
}

/**
 * Create a context into the png library for accessing the given image
 * \param[in] filter    Input stream containing PNG data
 * \param[in] pngstate  Container of all PNG state information
 * \return              Success status
 */
Bool png_create_context(FILELIST *filter, PNGSTATE *pngstate)
{
  /* Make sure jmp_buf is defined as the *very* first local. This
     ensures alignment on the stack after the function. On some 64
     processors it seems that the alignment of the argument to
     setjmp/longjmp *MUST* be 16 byte aligned. See request 61836. */
  jmp_buf aligned_jmpbuf ;

  HQASSERT(pngstate && pngstate->png == NULL,"Problem creating png context");

  /* Create png context and override libpng's I/O, memory and error handling */

  pngstate->png = png_create_read_struct_2(PNG_LIBPNG_VER_STRING,
                                           NULL /* user_error_ptr */,
                                           png_error_callback,
                                           png_warning_callback,
                                           NULL /* user_mem_ptr */,
                                           png_malloc_callback,
                                           png_free_callback);
  if (pngstate->png == NULL)
    return FALSE;

  /* libpng longjump's back to here on error. */

  if (setjmp(aligned_jmpbuf)) {
    png_destroy_read_struct(&(pngstate->png), &(pngstate->info), NULL);
    return error_handler(IOERROR);
  } else {
    memcpy(png_jmpbuf(pngstate->png), aligned_jmpbuf, sizeof(jmp_buf));
  }

  /* Override default libpng read with png_read_callback. */
  png_set_read_fn(pngstate->png, filter->underlying_file, png_read_callback);

  return TRUE;
}

/**
 * Destroy the context created for accessing the given image
 * \param[in] pngstate  Container of all PNG state information
 * \return              Success status
 */
Bool png_destroy_context(PNGSTATE *pngstate)
{
  /* Make sure jmp_buf is defined as the *very* first local. This
     ensures alignment on the stack after the function. On some 64
     processors it seems that the alignment of the argument to
     setjmp/longjmp *MUST* be 16 byte aligned. See request 61836. */
  jmp_buf aligned_jmpbuf ;

  /* libpng longjump's back to here on error. */
  if (setjmp(aligned_jmpbuf))
  {
    /* In case png_destroy_read_struct fails. */
    return error_handler(IOERROR);
  }
  else
  {
    memcpy(png_jmpbuf(pngstate->png), aligned_jmpbuf, sizeof(jmp_buf));
    png_destroy_read_struct(&(pngstate->png), &(pngstate->info), NULL);
  }
  return TRUE;
}


/**
 * Return a chunk of memory big enough to hold a single row of
 * expanded png data. To prevent repeated alloc/frees, retain and
 * re-use the last allocated row, when suitable.
 * \param[in] pngstate  Container of all PNG state information
 * \return              Pointer to a row of memory
 */
static struct { uint8 *ptr; uint32 bytes; } last = { NULL, 0 };
static uint8 *png_row_mem(PNGSTATE *pngstate)
{
  uint32 bytes = png_get_rowbytes(pngstate->png, pngstate->info);

  if ( bytes == 0 )
    return NULL;

  bytes = (bytes & ~1023)+1024; /* round up to 1k boundary */

  if ( last.ptr )
  {
    if ( bytes <= last.bytes )
      return last.ptr;

    mm_free(mm_pool_temp, last.ptr, last.bytes);
    last.bytes = 0;
  }

  if ( (last.ptr = mm_alloc(mm_pool_temp, bytes,
                            MM_ALLOC_CLASS_PNG_BUFFER)) == NULL )
    (void) error_handler(VMERROR);
  else
    last.bytes = bytes;

  return last.ptr;
}

/**
 * Read all the file information up to the actual image data and
 * unpack into pngstate.
 *
 * Many XPS PNG images claim to include transparency, but it turns out
 * that the alpha channel only in fact contains opaque values. This slows
 * down rip processing dramatically. If we can pre-determine that the the
 * alpha is in fact redundant, we can greatly speed up ripping times.
 * So it is worth pre-scanning the PNG to check for such unnecessary alpha.
 *
 * The normal course of events when using a PNG filter is
 *   stream        imagecontextopen
 *    keys         imagecontextinfo => values
 *   stream+values image
 *                 imagecontextclose
 * i.e. a single image call is wrapped by an imagecontextopen/close, and it
 * uses the same stream (though possibly re-wound) as was used to open the
 * image context.
 * There is no reason this has to be the case. There could be multiple calls
 * to image inside an imageconext, and they don't have to use the same stream.
 * Or the imagecontexts could be nested etc.
 *
 * So it would be nice to avoid having to do this alpha pre-scan twice per
 * image (once for the imagecontextinfo, and once for the actual image call).
 * But it turns out, because there is no architectural link between the two,
 * this is not easily possible.
 *
 * So the "imagecontextinfo" pre-scans the whole image checking the alpha
 * values, and leaves the filter at the end of the stream (as the imagecontext
 * operator manages the rewind of the stream for next image call). We then get
 * the actual "image" call, and the first filter decode re-scans the whole
 * image again, checking alpha state, and then has to seek back to the
 * beginning so that the image data can be read by the image operator properly.
 *
 * This is hugely ineffecient (the image is being read three times in all), but
 * this loss is dwarfed by the advantage we get in avoiding unneeded
 * transparency processing. If it becomes an issue, this triple image read will
 * have to be looked at again, but for the moment leave it like that as it is
 * the most architecturally straight-forward, and the performance gain is
 * high.
 *
 * \param[in] filter            Input stream containing PNG data
 * \param[in] pngstate          Container of all PNG state information
 * \param[in] start_alpha_state What we know about the alpha in the image
 * \return                      Success status
 */
static Bool png_decode_read_info(FILELIST* filter,  PNGSTATE* pngstate,
                                 int32 start_alpha_state)
{
  /* Make sure jmp_buf is defined as the *very* first local. This
     ensures alignment on the stack after the function. On some 64
     processors it seems that the alignment of the argument to
     setjmp/longjmp *MUST* be 16 byte aligned. See request 61836. */
  jmp_buf aligned_jmpbuf ;
  png_uint_32 width, height, xres = 0, yres = 0;
  int unit_type = 0;
  uint32 nchannels;
  Bool alpha_peek = FALSE, read_row = FALSE;

  /* libpng longjump's back to here on error. */
  if (setjmp(aligned_jmpbuf)) {
    return error_handler(IOERROR);
  } else {
    memcpy(png_jmpbuf(pngstate->png), aligned_jmpbuf, sizeof(jmp_buf));
  }

  pngstate->info = png_create_info_struct(pngstate->png);
  if (pngstate->info == NULL)
    return error_handler(VMERROR);

  png_read_info(pngstate->png, pngstate->info);
  pngstate->color_type = png_get_color_type(pngstate->png, pngstate->info);
  /* Fill-in the remaining pngstate attributes. */
  (void)png_get_IHDR(pngstate->png, pngstate->info,
                     & width, & height, & pngstate->bit_depth,
                     & pngstate->color_type, & pngstate->interlace_type,
                     NULL /* compression_type */, NULL /* filter_method */);
  /* This is necessary to avoid a compiler warning, without casting it away.
     I prefer to use core types asap. */
  HQASSERT(sizeof(png_uint_32) == sizeof(uint32),
           "png_uint_32 should be same size as uint32");
  pngstate->width = width;
  pngstate->height = height;

  switch ( pngstate->color_type ) /* palette, color, alpha */
  {
    case PNG_COLOR_TYPE_GRAY:
    case PNG_COLOR_TYPE_RGB:
      /* Non-palette images can have tRNS chunks too. Just assume they
       * would not go to the trouble one if there was not real transparency,
       * so don't bother doing the alpha search.
       */
      if ( png_get_valid(pngstate->png, pngstate->info, PNG_INFO_tRNS) )
      {
        png_set_tRNS_to_alpha(pngstate->png);
        pngstate->alpha_state = PNG_FULL_ALPHA;
      }
      else
        pngstate->alpha_state = PNG_NO_ALPHA;
      break;
    case PNG_COLOR_TYPE_GRAY_ALPHA:
      pngstate->alpha_state = PNG_FULL_ALPHA;
      break;
    case PNG_COLOR_TYPE_PALETTE:
      {
        png_bytep trans;
        int i, num_trans;
        png_color_16p trans_values;

        pngstate->alpha_state = PNG_NO_ALPHA; /* assume no alpha */
        if ( png_get_valid(pngstate->png, pngstate->info, PNG_INFO_tRNS) &&
             png_get_tRNS(pngstate->png,pngstate->info,&trans,&num_trans,
                           &trans_values) != 0 && trans != NULL )
        {
          alpha_peek = TRUE; /* searching for alpha */
          for (i=0;i<num_trans;i++)
          {
            /*
             * Transparency palette value = 255 means opaque
             * for indexed images : see tRNS section of PNG spec.
             */
            if ( trans[i] == 0x00 )
            {
              pngstate->alpha_state = PNG_MASK_ALPHA;
            }
            else if (trans[i] != 0xff)
            {
              pngstate->alpha_state = PNG_FULL_ALPHA;
              break;
            }
          }
        }
        /* Expand paletted PNGs into rgb
         * (otherwise we'll have palette indices in the rows).
         */
        png_set_palette_to_rgb(pngstate->png);
      }
      if ( pngstate->alpha_state != PNG_FULL_ALPHA )
        break; /* all done if we have found only limited alpha */
      /*
       * Searching through the palette, we found some real alpha entries
       * so its probably a transparent PNG. But those palette entries may
       * not actually get used, so its still worth search the entire image
       * just to double check, as it will avoid the huge workload of
       * compositing.
       */
      if ( start_alpha_state == PNG_UNK_INFO ||
           start_alpha_state == PNG_UNK_READ )
        png_read_update_info(pngstate->png, pngstate->info);
      /* And fall through to search the image for alpha .... */
    case PNG_COLOR_TYPE_RGB_ALPHA:
      pngstate->alpha_state = PNG_FULL_ALPHA;
      /**
       * Not sure we believe there is alpha present, so take a peek.
       * If the PNG data includes and alpha channel, read all the alpha values
       * to see if they are restricted set, and therefore can be reduced to
       * a case which the rip can process faster
       * (e.g. ignore alpha channel if it is all solid ).
       * Only bother with non-interleaved data for now.
       * Also don't try peeking if we are being called recursively to reset
       * all the structures after a previous alpha peek.
       */
      if ( start_alpha_state != PNG_UNK_INFO &&
           start_alpha_state != PNG_UNK_READ )
      {
        alpha_peek = TRUE;
        pngstate->alpha_state = start_alpha_state;
      }
      else if ( pngstate->interlace_type == PNG_INTERLACE_NONE )
      {
        FILELIST *ufptr = theIUnderFile(filter);
        Hq32x2 pos;
        uint8 *row;
        uint32 x, y;

        /* Pre-check the alpha data in the PNG image.
         * If the filter is rewindable, then it is safe to pre-read all of the
         * alpha channel, test for special cases, and then re-wind the stream
         * back to the start of the image.
         */
        if ( file_seekable(ufptr) &&
             (*theIMyFilePos(ufptr))(ufptr, &pos) != EOF )
        {
          if ( (row = png_row_mem(pngstate)) == NULL )
            return FALSE;

          alpha_peek = TRUE;
          read_row = TRUE;
          pngstate->alpha_state = PNG_NO_ALPHA;
          for ( y = 0; y < pngstate->height; y++)
          {
            png_read_row(pngstate->png, row, NULL); /* returns RGBa data */
            for ( x = 0; x < pngstate->width; x++ )
            {
              if ( row[4*x+3] == 0x00 )
                pngstate->alpha_state = PNG_MASK_ALPHA;
              else if ( row[4*x+3] != 0xFF )
              {
                pngstate->alpha_state = PNG_FULL_ALPHA;
                break;
              }
            }
            if ( pngstate->alpha_state == PNG_FULL_ALPHA )
              break;
          }
          /* As we read the png, need to reset the filter so it can be read
           * again properly.
           * But don't bother if we have been called from imagecontextinfo,
           * as its going to get reset anyway.
           */
          if ( start_alpha_state == PNG_UNK_READ )
          {
            Hq32x2FromUint32(&pos,0u);

            /* Rewind and reset the filter
             * What should we be doing on error ?
             */
            if ( (*theIMyResetFile(ufptr))(ufptr) == EOF ||
                 (*theIMySetFilePos(ufptr))(ufptr, &pos) == EOF )
              return FALSE;

            /*
             * Have read the image data to peek at the alpha, and now want
             * to reset things so we can read it again properly in "image".
             * But libpng does not seem to support a read reset of any kind.
             * So we end up having to destroy and re-create the PNG context
             * ready for the image to be read again.
             * Have to be very careful, as this involves calling this function
             * again, and need to prevent infinite recursion. Hence the
             * "start_alpha_state" passed in to stop us going round and round.
             */
            start_alpha_state = pngstate->alpha_state;
            if ( !png_destroy_context(pngstate) ||
                 !png_create_context(filter, pngstate) ||
                 !png_decode_read_info(filter, pngstate, start_alpha_state) )
              return FALSE;
          }
        }
      }
      break;
    default:
      png_error(pngstate->png, "unkown PNG color type") ;
  }
  if ( alpha_peek )
  {
    /*
     * If we peeked at the alpha, then what we discovered may be that we do
     * not actually need to treat it as a transparent PNG. Depending on what
     * we found, adjust the libpng library so that it will deliver the data
     * in the way we need.
     */
    if ( pngstate->alpha_state == PNG_NO_ALPHA )
      png_set_strip_alpha(pngstate->png);
    else if ( pngstate->alpha_state == PNG_FULL_ALPHA )
      png_set_tRNS_to_alpha(pngstate->png);
    else if ( pngstate->alpha_state == PNG_MASK_ALPHA )
    {
      /*
       * PNG image contains an alpha channel, but we have examined all
       * the data and found it can be replaced by a mask instead.
       * Do this by inverting the alpha ( mask = !alpha) and then swapping
       * the data 4-tuple from being RGBa to mRGB, as this is the interleaved
       * format PS expects mask data in.
       */
      png_set_invert_alpha(pngstate->png);
      png_set_swap_alpha(pngstate->png);
    }
  }

  /* libpng may need to expand images with a bit_depth < 8 up to 8-bits to
   * handle palette and transparency expansion.  png_read_update_info ensures
   * bit_depth etc take account of the transformations requested above.
   * Then re-get header information.
   */
  if ( !read_row ) /* Don't need to update if we have read a row */
    png_read_update_info(pngstate->png, pngstate->info);
  (void)png_get_IHDR(pngstate->png, pngstate->info,
                     & width, & height, & pngstate->bit_depth,
                     & pngstate->color_type, & pngstate->interlace_type,
                     NULL /* compression_type */, NULL /* filter_method */);

  if ((pngstate->color_type & PNG_COLOR_MASK_COLOR) != 0)
    pngstate->ncomps = 3;
  else
    pngstate->ncomps = 1;

  nchannels = pngstate->ncomps;
  if ( pngstate->alpha_state != PNG_NO_ALPHA )
    ++nchannels;

  if ( png_get_pHYs(pngstate->png, pngstate->info, &xres, &yres, &unit_type) )
  {
    /* Metres are the only valid resolution value as of 28th January 2005 */
    if ( unit_type == PNG_RESOLUTION_METER ) {
      if ( xres != 0 )
        pngstate->xresolution = (USERVALUE)(xres * 0.0254) ;

      if ( yres != 0 )
        pngstate->yresolution = (USERVALUE)(yres * 0.0254) ;
    }
  }

  pngstate->row_index = 0u;

  /* rowbytes_data must be precise to ensure the right quantity of data is read.
   * png_get_rowbytes is slightly larger to allow sanity checking by libpng (I
   * also do end of row checking to catch buffer overruns -- belt and braces!).
   */
  pngstate->rowbytes_data = ((pngstate->width * nchannels *
                              pngstate->bit_depth) + 7u) >> 3u;

  /* Need to allocate an extra byte for FilterFillBuff lastchar shenanigans. */
  pngstate->rowbytes_alloc = png_get_rowbytes(pngstate->png, pngstate->info)+1u;
  HQASSERT(pngstate->rowbytes_alloc > pngstate->rowbytes_data,
           "rowbytes_alloc is too small");

#if defined( ASSERT_BUILD )
  /* Add another byte for the ROW_END_MARKER to catch buffer overruns. */
  pngstate->rowbytes_alloc += 1u;
#endif

  return TRUE;
}

/**
 * Initialise the PNG image decode filter.
 * \param[in] filter   Input stream containing PNG data
 * \param[in] args     Optional filter initialisation arguments
 * \param[in] stack    Stack containing filter configuration options
 * \return             Success status
 */
static Bool png_decode_init(FILELIST *filter, OBJECT *args, STACK *stack)
{
  PNGSTATE *pngstate;
  int32 pop_args = 0;

  UNUSED_PARAM(OBJECT*, args);

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
    if ( ! FilterCheckArgs(filter, args) )
      return FALSE ;
    OCopy(theIParamDict(filter), *args) ;
  } else {
    args = NULL ;
  }

  /* Get underlying source/target if we have a stack supplied. */
  if (stack) {
    if ( theIStackSize(stack) < pop_args)
      return error_handler(STACKUNDERFLOW);

    if (! filter_target_or_source(filter, stackindex(pop_args, stack)))
      return FALSE;

    ++pop_args;
  }

  filter->buffer = NULL;
  filter->ptr = NULL;
  filter->count = 0;
  filter->buffersize = 0;
  filter->readsize = 0;
  filter->filter_state = FILTER_INIT_STATE;

  pngstate = mm_alloc(mm_pool_temp, sizeof(PNGSTATE), MM_ALLOC_CLASS_PNG_STATE);
  if (pngstate == NULL)
    return error_handler(VMERROR);

  HqMemZero(pngstate, sizeof(PNGSTATE));

  NAME_OBJECT(pngstate, PNGSTATE_NAME);

  theIFilterPrivate(filter) = pngstate;

  if ( !png_create_context(filter, pngstate) )
    return FALSE;

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack");
  if (pop_args > 0)
    npop(pop_args, stack);

  return TRUE;
}

/**
 * Destroy allocations for a PNG image filter.
 * \param[in] filter   Input stream containing PNG data
 */
static void png_decode_dispose(FILELIST* filter)
{
  PNGSTATE *pngstate;

  HQASSERT(filter, "filter is null");

  pngstate = theIFilterPrivate(filter);

  if (pngstate) {
    VERIFY_OBJECT(pngstate, PNGSTATE_NAME);

    /* row_pointer only exists if the image was not interlaced. */
    if (pngstate->row_pointer) {
      mm_free(mm_pool_temp, pngstate->row_pointer-1, pngstate->rowbytes_alloc);
    }

    /* row_pointers only exists if the image was interlaced. */
    if (pngstate->row_pointers) {
      uint32 row;
      for (row = 0u; row < pngstate->height; ++row) {
        if (pngstate->row_pointers[row])
          mm_free(mm_pool_temp, pngstate->row_pointers[row]-1,
                  pngstate->rowbytes_alloc);
      }
      mm_free(mm_pool_temp, pngstate->row_pointers,
              pngstate->height * sizeof(uint8*));
    }

    (void)png_destroy_context(pngstate);
    /* Don't return on error yet -- must free PNGSTATE. */

    theIBuffer(filter) = theIPtr(filter) = NULL;

    UNNAME_OBJECT(pngstate);
    mm_free(mm_pool_temp, pngstate, sizeof(PNGSTATE));
    theIFilterPrivate(filter) = NULL;
  }
}

/**
 * Decode a buffer of data from a PNG file.
 * \param[in]  filter     Input stream containing PNG data
 * \param[out] ret_bytes  Number of bytes decoded
 * \return                Success status
 */
static Bool png_decode_buffer(FILELIST* filter, int32* ret_bytes)
{
  /* Make sure jmp_buf is defined as the *very* first local. This
     ensures alignment on the stack after the function. On some 64
     processors it seems that the alignment of the argument to
     setjmp/longjmp *MUST* be 16 byte aligned. See request 61836. */
  jmp_buf aligned_jmpbuf ;
  PNGSTATE* pngstate;

  HQASSERT(filter, "filter is null");

  pngstate = theIFilterPrivate(filter);
  VERIFY_OBJECT(pngstate, PNGSTATE_NAME);

  if (! pngstate->info)
  {
    /* WARNING: png_decode_read_info() makes a call to setjmp(). Be
       careful it does NOT clobber any setjmp calls in this
       function. */

    if (! png_decode_read_info(filter, pngstate, PNG_UNK_READ))
      return FALSE;
  }

  /* libpng longjump's back to here on error. */
  if (setjmp(aligned_jmpbuf)) {
    return error_handler(IOERROR);
  } else {
    memcpy(png_jmpbuf(pngstate->png), aligned_jmpbuf, sizeof(jmp_buf));
  }

  if (pngstate->interlace_type == PNG_INTERLACE_NONE) {
    /* Not a progressive PNG -- read row-by-row. */

    if (! pngstate->row_pointer) {
      pngstate->row_pointer = mm_alloc(mm_pool_temp, pngstate->rowbytes_alloc,
                                       MM_ALLOC_CLASS_PNG_BUFFER);
      if (pngstate->row_pointer == NULL)
        return error_handler(VMERROR);

      ++pngstate->row_pointer;

      ROW_SCRIBBLE(pngstate->row_pointer, pngstate->rowbytes_data);
      ROW_END_SET(pngstate->row_pointer, pngstate->rowbytes_data);
    }

    png_read_row(pngstate->png, pngstate->row_pointer, NULL);

    ROW_END_ASSERT(pngstate->row_pointer, pngstate->rowbytes_data);
    filter->buffer = pngstate->row_pointer;
    filter->buffersize = (int32)pngstate->rowbytes_alloc;

  } else {
    /* A progressive PNG -- read the whole image into a buffer. */

    if (! pngstate->row_pointers) {
      /* This must be the first row.  Allocate rows and row ptrs for the
         entire image and read the image in now.  png_read_image does the
         multiple read/writes to handle the progressive PNG for us. */
      uint32 row;

      HQASSERT(pngstate->row_index == 0, "Expected this to be first row");

      pngstate->row_pointers = mm_alloc(mm_pool_temp,
                                        pngstate->height * sizeof(uint8*),
                                        MM_ALLOC_CLASS_PNG_BUFFER);
      if (pngstate->row_pointers == NULL)
        return error_handler(VMERROR);

      for (row = 0u; row < pngstate->height; ++row) {
        pngstate->row_pointers[row] = NULL;
      }

      for (row = 0u; row < pngstate->height; ++row) {
        pngstate->row_pointers[row] = mm_alloc(mm_pool_temp,
                                               pngstate->rowbytes_alloc,
                                               MM_ALLOC_CLASS_PNG_BUFFER);
        if (pngstate->row_pointers[row] == NULL)
          return error_handler(VMERROR);

        ++pngstate->row_pointers[row];

        ROW_SCRIBBLE(pngstate->row_pointers[pngstate->row_index],
                     pngstate->rowbytes_data);
        ROW_END_SET(pngstate->row_pointers[row], pngstate->rowbytes_data);
      }

      png_read_image(pngstate->png, pngstate->row_pointers);

    } else {
      /* Finished reading the previous row -- may as well free it now. */
      HQASSERT(pngstate->row_index > 0, "Expected row_index > 0");
      mm_free(mm_pool_temp, pngstate->row_pointers[pngstate->row_index-1]-1,
              pngstate->rowbytes_alloc);
      pngstate->row_pointers[pngstate->row_index - 1] = NULL;
    }

    ROW_END_ASSERT(pngstate->row_pointers[pngstate->row_index],
                   pngstate->rowbytes_data);
    filter->buffer = pngstate->row_pointers[pngstate->row_index];
    filter->buffersize = (int32)pngstate->rowbytes_alloc;
  }

  HQASSERT(pngstate->rowbytes_data <= MAXINT32,
           "rowbytes_data overflowed int32");
  *ret_bytes = (int32)pngstate->rowbytes_data;

  ++pngstate->row_index;

  if (pngstate->row_index == pngstate->height) {
    /* Negation signifies that this is the last buffer. */
    *ret_bytes = -(*ret_bytes);

#if defined( PNG_DO_READ_END )
    /* We'll skip this for now. */
    png_read_end(pngstate->png, NULL /* end_info */);
#endif
  }

  return TRUE;
}

/**
 * Extract required information from PNG file and report to the caller.
 * \param[in] filter   Input stream containing PNG data
 * \param[in] match    Callback to report keys extracted from PNG file
 * \return                Success status
 */
static Bool png_decode_info(FILELIST* filter, imagefilter_match_t *match)
{
  /* Make sure jmp_buf is defined as the *very* first local. This
     ensures alignment on the stack after the function. On some 64
     processors it seems that the alignment of the argument to
     setjmp/longjmp *MUST* be 16 byte aligned. See request 61836. */
  jmp_buf aligned_jmpbuf ;
  PNGSTATE* pngstate;
  OBJECT value = OBJECT_NOTVM_NOTHING ;
  Bool result = FALSE ;

  HQASSERT(filter, "filter is null");
  HQASSERT(match, "No image decode match");

  pngstate = theIFilterPrivate(filter);
  VERIFY_OBJECT(pngstate, PNGSTATE_NAME);

  if (! pngstate->info)
  {
    /* WARNING: png_decode_read_info() makes a call to setjmp(). Be
       careful it does NOT clobber any setjmp calls in this
       function. */
    if (! png_decode_read_info(filter, pngstate, PNG_UNK_INFO))
      return FALSE;
  }

  object_store_integer(&value, pngstate->width) ;
  if ( filter_info_callback(match, NAME_Width, &value, &result) )
    return result ;

  object_store_integer(&value, pngstate->height) ;
  if ( filter_info_callback(match, NAME_Height, &value, &result) )
    return result ;

  object_store_integer(&value, pngstate->bit_depth) ;
  if ( filter_info_callback(match, NAME_BitsPerComponent, &value, &result) )
    return result ;

  object_store_name(&value,
                    pngstate->ncomps == 1u ? NAME_DeviceGray : NAME_DeviceRGB,
                    LITERAL) ;
  if ( filter_info_callback(match, NAME_ColorSpace, &value, &result) )
    return result ;

  /* Set the Imagetype depending on the type of alpha data present */
  if ( pngstate->alpha_state == PNG_MASK_ALPHA )
    object_store_integer(&value, 3);
  else if ( pngstate->alpha_state == PNG_FULL_ALPHA )
    object_store_integer(&value, 12);
  else
    object_store_integer(&value, 1);
  if ( filter_info_callback(match, NAME_ImageType, &value, &result) )
    return result ;

  /* Sample interleaved if alpha present, none otherwise. */
  if ( pngstate->alpha_state == PNG_NO_ALPHA )
    object_store_integer(&value, 0);
  else
    object_store_integer(&value, 1);
  if ( filter_info_callback(match, NAME_InterleaveType, &value, &result) )
    return result ;

  if ( filter_info_callback(match, NAME_PreMult, &fnewobj, &result) )
    return result ;

  if ( filter_info_callback(match, NAME_MultipleDataSources,
                            &fnewobj, &result) )
    return result ;

  file_store_object(&value, filter, LITERAL) ;
  if ( filter_info_callback(match, NAME_DataSource, &value, &result) )
    return result ;

  /* [Width 0 0 -Height 0 Height] */
  if ( filter_info_ImageMatrix(match,
                               (SYSTEMVALUE)pngstate->width, 0,
                               0, -(SYSTEMVALUE)pngstate->height,
                               0, (SYSTEMVALUE)pngstate->height, &result) )
    return result ;

  if ( filter_info_Decode(match, pngstate->ncomps, 0.0f, 1.0f, &result) )
    return result ;

  object_store_name(&value, NAME_EmbeddedICCProfile, LITERAL) ;
  if (filter_info_match(&value, match) != NULL )
  {
    /* we only check profiles if caller is asking for profiles */
    if (pngstate->info->iccp_proflen && pngstate->info->iccp_profile) {
      OBJECT *icc_profile_array, *entry;
      uint32 remaining = pngstate->info->iccp_proflen ;
      uint32 offset = 0 ;
      uint32 len = (remaining + MAXPSSTRING - 1) / MAXPSSTRING;
      Bool done;

      icc_profile_array = mm_alloc(mm_pool_temp,
                                   sizeof(OBJECT)*len,
                                   MM_ALLOC_CLASS_PNG_LIBPNG);
      if (icc_profile_array == NULL)
        return error_handler(VMERROR);

      theTags(value) = OARRAY|LITERAL|UNLIMITED; /* Prepare to hold an array */
      theLen(value) = CAST_TO_UINT16(len) ; /* local array length */
      oArray(value) = icc_profile_array ;

      /* now put the result into the array */
      for ( entry = oArray(value) ; remaining > 0 ; ++entry )
      {
        uint32 datalen = min(MAXPSSTRING, remaining);

        (void)object_slot_notvm(entry) ;
        theTags(*entry) = OSTRING | LITERAL | READ_ONLY ;
        theLen(*entry) =  CAST_TO_UINT16(datalen) ;
        oString(*entry) = (uint8 *)&pngstate->info->iccp_profile[offset];

        offset += datalen;
        remaining -= datalen ;
      }

      HQASSERT(entry - oArray(value) == theLen(value),
               "ICC chunks calculation was wrong for size") ;

      done = filter_info_callback(match, NAME_EmbeddedICCProfile, &value,
                                  &result);

      mm_free(mm_pool_temp, icc_profile_array, sizeof(OBJECT)*len);

      if ( done )
        return result;
    }
  }

  /* libpng longjump's back to here on error. */
  if (setjmp(aligned_jmpbuf)) {
    return error_handler(IOERROR);
  } else {
    png_uint_32 xres = 0, yres = 0 ;
    int unit_type = 0 ;

    memcpy(png_jmpbuf(pngstate->png), aligned_jmpbuf, sizeof(jmp_buf));

    if (png_get_pHYs(pngstate->png, pngstate->info, &xres, &yres, &unit_type))
    {
      /* Metres are the only valid resolution value as of 28th January 2005 */
      if ( unit_type == PNG_RESOLUTION_METER ) {
        if ( xres != 0 ) {
          object_store_numeric(&value, xres * 0.0254) ;
          if ( filter_info_callback(match, NAME_XResolution, &value, &result) )
            return result ;
        }

        if ( yres != 0 ) {
          object_store_numeric(&value, yres * 0.0254) ;
          if ( filter_info_callback(match, NAME_YResolution, &value, &result) )
            return result ;
        }
      }
    }
  }

  return TRUE ;
}

/**
 * Test if a file stream is a PNG image without advancing the file
 * position. This just looks at the file signature of a file stream.
 * \param[in]    filter   Input stream containing PNG data
 * \return                Is this a PNG file ?
 */
Bool png_signature_test(FILELIST *filter)
{
  if ( isIIOError(filter) ||
       !isIInputFile(filter) ||
       !isIOpenFile(filter) ||
       !EnsureNotEmptyFileBuff(filter) )
    return FALSE ; /* These are all reasons for it not to be a PNG file */

  return png_sig_cmp(theIPtr(filter), 0, theICount(filter)) == 0 ;
}

/**
 * Create PNG filter
 * \param[in]    filter   Input stream containing PNG data
 */
static void png_decode_filter(FILELIST* filter)
{
  HQASSERT(filter, "No filter to initialise");

  /* png decode filter */
  init_filelist_struct(filter,
                       NAME_AND_LENGTH("PNGDecode"),
                       FILTER_FLAG | READ_FLAG | EXPANDS_FLAG,
                       0, NULL, 0,
                       FilterFillBuff,                   /* fillbuff */
                       FilterFlushBufError,              /* flushbuff */
                       png_decode_init,                  /* initfile */
                       FilterCloseFile,                  /* closefile */
                       png_decode_dispose,               /* disposefile */
                       FilterBytes,                      /* bytesavail */
                       FilterReset,                      /* resetfile */
                       FilterPos,                        /* filepos */
                       FilterSetPos,                     /* setfilepos */
                       FilterFlushFile,                  /* flushfile */
                       FilterEncodeError,                /* filterencode */
                       png_decode_buffer,                /* filterdecode */
                       FilterLastError,                  /* lasterror */
                       -1, NULL, NULL, NULL);

  /* A minimal decode to determine values contained in DecodeInfo. */
  filter->decodeinfo = png_decode_info;
}

/**
 * Initialise C globals for PNG filter code
 */
static void init_C_globals_pngfilter(void)
{
#if defined( ASSERT_BUILD )
  png_debug = TRUE;
  png_scribble = FALSE;
#endif
  last.ptr = NULL ;
  last.bytes = 0 ;
}

/**
 * Initialise PNG module
 * \param[in]  params  Configuration parameters
 * \return             Success status
 */
static Bool png_swstart(struct SWSTART *params)
{
  FILELIST *flptr ;

  UNUSED_PARAM(struct SWSTART *, params) ;

  if ( (flptr = mm_alloc_static(sizeof(FILELIST))) == NULL )
    return FALSE ;

  png_decode_filter(&flptr[0]) ;
  filter_standard_add(&flptr[0]) ;

  return TRUE ;
}

void png_C_globals(core_init_fns *fns)
{
  init_C_globals_pngfilter() ;
  fns->swstart = png_swstart ;
}

/* =============================================================================
* Log stripped */
