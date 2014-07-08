/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlimage.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * "Image" operator handling functions
 */

#include "core.h"
#include "timing.h"
#include "swerrors.h"
#include "swctype.h"
#include "swdevice.h"
#include "dicthash.h"
#include "dictscan.h"
#include "objects.h"
#include "objstack.h"
#include "graphics.h"
#include "gschcms.h"
#include "gschead.h"
#include "gstate.h"
#include "gu_ctm.h"
#include "mm.h"
#include "namedef_.h"
#include "images.h"
#include "miscops.h"
#include "mmcompat.h"
#include "hqmemset.h"
#include "routedev.h"

#include "pclxltypes.h"
#include "pclxldebug.h"
#include "pclxlcontext.h"
#include "pclxlerrors.h"
#include "pclxloperators.h"
#include "pclxlattributes.h"
#include "pclxlgraphicsstate.h"
#include "pclxlimage.h"
#include "pclxlscan.h"
#include "pclxlpsinterface.h"

/* ============================================================================
 * Utility functions
 * ============================================================================
 */

#define pclxl_zero_seed_row(str_, len_) HqMemZero((str_), (len_))

/* Decode array supports a maximum of 8 planes. */
#define MAX_PLANES 8

/** \todo ajcd 2009-11-27: Rename this define to reflect its purpose. */
#define PS_MAX_STRING_LENGTH MAXPSSTRING

static
Bool pclxl_throwout_remaining_image_data(PCLXL_PARSER_CONTEXT parser_context,
                                         PCLXL_IMAGE_READ_CONTEXT *image_reader)
{
  PCLXLSTREAM* p_stream;
  int32 bytes_skipped;

  HQASSERT(image_reader != NULL, "image_reader is NULL") ;

  p_stream = pclxl_parser_current_stream(parser_context);
  if ( p_stream != NULL && image_reader->embedded_data_len > 0 ) {
    if ( file_skip(p_stream->flptr, image_reader->embedded_data_len, &bytes_skipped) <= 0 ) {
      return(FALSE);
    }
    image_reader->embedded_data_len -= bytes_skipped;
  }

  return(TRUE);
}

static
Bool pclxl_throwout_remaining_threshold_data(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_THRESHOLD_READ_CONTEXT *threshold_reader ;
  PCLXLSTREAM* p_stream;
  int32 bytes_skipped;

  threshold_reader = parser_context->threshold_reader ;
  HQASSERT(threshold_reader != NULL, "image_reader is NULL") ;
  p_stream = pclxl_parser_current_stream(parser_context);

  if ( p_stream != NULL && threshold_reader->embedded_data_len > 0 ) {
    if ( file_skip(p_stream->flptr, threshold_reader->embedded_data_len, &bytes_skipped) <= 0 ) {
      return(FALSE);
    }
    threshold_reader->embedded_data_len -= bytes_skipped;
  }

  return TRUE ;
}


/* See header for doc. */
Bool pclxl_raster_read_context_create(PCLXL_PARSER_CONTEXT parser_context,
                                      PCLXL_IMAGE_READ_CONTEXT **image_reader,
                                      PCLXL_ENUMERATION color_mapping,
                                      PCLXL_ENUMERATION color_depth,
                                      int32 source_width,
                                      int32 source_height,
                                      PCLXL_SysVal_XY destination_size,
                                      Bool  for_an_image)
{
  int32 depth ;
  double width_in_bytes ;
  size_t size ;
  int32 num_components ;
  PCLXL_IMAGE_READ_CONTEXT *new_reader ;
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state ;
  PCLXL_ColorSpace color_space = graphics_state->color_space_details->color_space ;

  HQASSERT(parser_context != NULL, "parser_context is NULL.") ;
  HQASSERT(image_reader != NULL, "image_reader is NULL") ;

  *image_reader = NULL ;

  /* We now need to determine the number of color components per color that the
     image needs to supply to determine the color of each pixel. This is always
     exactly 1 component for eIndexedPixel images because a single value is
     used to access an indexed colorspace.  In the case of patterns, it is not
     necessary for the indexed colorspace already to be set up.

     It has not been investigated whether it is acceptable for the colorspace
     to change between the BeginRastPattern and ReadRastPattern commands in the
     case of a direct pattern.
   */

  if (color_mapping == PCLXL_eDirectPixel) {
    /* Use base colorspace directly. */
    num_components = pclxl_color_space_num_components(color_space);
  } else if (!for_an_image || graphics_state->color_space_details->color_palette_len > 0) {
    num_components = 1 ;
  }
  else {
    /* We have an indexed image but we do not have an indexed colorspace */
    (void) PCLXL_ERROR_HANDLER(parser_context->pclxl_context,
                               PCLXL_SS_IMAGE,
                               PCLXL_PALETTE_UNDEFINED,
                               ("IndexedPixel image requires an indexed colorspace"));
    return FALSE ;
  }

  depth = pclxl_color_depth_bits(color_depth) ;

  HQASSERT(source_width > 0, "Source_width is not greater than zero.") ;

  /* Calculate size of raster row in bytes. */
  width_in_bytes = (double)source_width * (double)num_components * (double)depth / 8.0 ;
  if (width_in_bytes != (int32)width_in_bytes)
    width_in_bytes = (int32)width_in_bytes + 1 ;

  size = sizeof(PCLXL_IMAGE_READ_CONTEXT) + (size_t)width_in_bytes
            + 4 + 1 ; /* Extra space for any padding,
                          plus 1 byte for lastchar filter symantics */

  if ((new_reader = mm_alloc(pclxl_context->memory_pool, size,
                             MM_ALLOC_CLASS_PCLXL_GRAPHICS_STATE)) == NULL) {
    (void) PCLXL_ERROR_HANDLER(parser_context->pclxl_context,
                               PCLXL_SS_IMAGE,
                               PCLXL_INSUFFICIENT_MEMORY,
                               ("Failed to allocate (%d bytes) for image reader",
                                size));
    return FALSE ;
 }

  new_reader->width_in_bytes = (int32)width_in_bytes ;
  new_reader->zee_bytes = (uint8*)new_reader + sizeof(PCLXL_IMAGE_READ_CONTEXT) ;
  new_reader->zee_bytes += 1 ; /* Allow an extra byte for lastchar filter symantics */
  new_reader->alloc_size = size ;
  new_reader->destination_width = destination_size.x ;
  new_reader->destination_height = destination_size.y ;
  new_reader->source_width = source_width ;
  new_reader->source_height = source_height ;
  new_reader->within_PS_imageread_callback = FALSE ;
  new_reader->embedded_data_len = 0 ;
  new_reader->uncompressed_bytes = 0 ;
  new_reader->bytes_remaining_in_scan_line = 0 ;
  new_reader->pad_bytes = 0 ;
  new_reader->start_line = 0 ;
  new_reader->block_height = 0 ;
  new_reader->compress_mode = 0 ;
  new_reader->is_first_decode_segment = TRUE ;
  new_reader->num_rows_seen = 0 ;
  new_reader->start_of_string = new_reader->zee_bytes ;
  new_reader->end_image_seen = FALSE ;
  new_reader->rle_control = 0 ;
  new_reader->rle_out_byte = -1 ;
  new_reader->decode_ok = TRUE ;
  new_reader->jpeg_source = NULL ;

  switch (color_mapping) {
  case PCLXL_eDirectPixel:
    new_reader->is_indexed = FALSE ;
    break ;
  case PCLXL_eIndexedPixel:
    new_reader->is_indexed = TRUE ;
    break ;
  default:
    HQFAIL("Invalid color mapping should not be possible here.") ;
  }

  new_reader->num_components = num_components ;
  new_reader->bits_per_component = depth ;

  oString(new_reader->string_object) = new_reader->zee_bytes ;
  theLen(new_reader->string_object) = 0 ;
  theTags(new_reader->string_object) = OSTRING|UNLIMITED|LITERAL ;
  theMark(new_reader->string_object) = ISLOCAL|ISNOTVM|SAVEMASK ;

  *image_reader = new_reader ;
  return TRUE ;
}

/* Threshold arrays are just arrays of bytes. For time being we will
 * read the data for the threshold arrays in scan lines - this is
 * inefficient, but matches the behaviour of the image handling.
 * Threshold array alignment is always on 4 byte boundaries. */
static
Bool pclxl_threshold_read_context_create( PCLXL_PARSER_CONTEXT parser_context,
                                          PCLXL_THRESHOLD_READ_CONTEXT **threshold_reader,
                                          int32 source_width,
                                          int32 source_height)
{
  size_t size ;
  PCLXL_THRESHOLD_READ_CONTEXT *new_reader ;

  HQASSERT(parser_context != NULL, "parser_context is NULL.") ;
  HQASSERT(threshold_reader != NULL, "image_reader is NULL") ;

  *threshold_reader = NULL ;

  HQASSERT(source_width > 0, "Source_width is not greater than zero.") ;
  HQASSERT(source_height > 0, "Source_height is not greater than zero.") ;

  HQASSERT(source_width <= 256, "Source_width is greater than 256.") ;
  HQASSERT(source_height <= 256, "Source_height is greater than 256.") ;

  /* Calculate size of raster row in bytes. */
  size = sizeof(PCLXL_THRESHOLD_READ_CONTEXT) + (size_t)source_width ;

  if ((new_reader = mm_alloc(parser_context->pclxl_context->memory_pool,
                             size,
                             MM_ALLOC_CLASS_PCLXL_GRAPHICS_STATE)) == NULL) {
    (void)PCLXL_ERROR_HANDLER(parser_context->pclxl_context,
                              PCLXL_SS_KERNEL,
                              PCLXL_INSUFFICIENT_MEMORY,
                              ("Insufficient memory"));
    return FALSE ;
  }

  new_reader->width_in_bytes = source_width ;
  new_reader->zee_bytes = (uint8*)new_reader
                            + sizeof(PCLXL_THRESHOLD_READ_CONTEXT) ;
  new_reader->alloc_size = size ;
  new_reader->threshold_array_width = source_width ;
  new_reader->threshold_array_height = source_height ;
  new_reader->embedded_data_len = 0 ;
  new_reader->uncompressed_bytes = 0 ;
  new_reader->bytes_remaining = 0 ;
  new_reader->pad_bytes = ( 4 - ( source_width % 4 )) % 4;
  new_reader->throwout_byte_count = ( 4 - ( source_width % 4 )) % 4;
  new_reader->start_of_string = new_reader->zee_bytes ;

  oString(new_reader->string_object) = new_reader->zee_bytes ;
  theLen(new_reader->string_object) = 0 ;
  theTags(new_reader->string_object) = OSTRING|UNLIMITED|LITERAL ;
  theMark(new_reader->string_object) = ISLOCAL|ISNOTVM|SAVEMASK ;

  *threshold_reader = new_reader ;
  return TRUE ;
}

static
Bool create_decode_filter(PCLXL_PARSER_CONTEXT parser_context,
                          PCLXL_IMAGE_READ_CONTEXT *image_reader,
                          OBJECT *thefilter)
{
  corecontext_t *context = parser_context->pclxl_context->corecontext;
  Bool currentglobal ;
  Bool ok = TRUE ;
  OBJECT ofiltered = OBJECT_NOTVM_NOTHING ;
  OBJECT ofiltered_rsd = OBJECT_NOTVM_NOTHING ;
  OBJECT ofiltered_dct = OBJECT_NOTVM_NOTHING ;
  PCLXLSTREAM* p_stream = pclxl_parser_current_stream(parser_context);

  HQASSERT(thefilter != NULL, "thefilter is NULL") ;
  HQASSERT(p_stream != NULL, "p_stream is NULL") ;

  /* Create this filter in local memory, it's closed at the end of the
     image anyway. */
  currentglobal = setglallocmode(context, FALSE) ;

  if (image_reader->compress_mode == PCLXL_eJPEGCompressionForPattern) {
    /* For JPEG patterns, layer a DCT decode filter. */
    ok = filter_layer(p_stream->flptr,
                      NAME_AND_LENGTH("DCTDecode"), NULL, &image_reader->jpeg_source) ;

  } else {
    FILELIST *filter;

    ok = filter_layer(p_stream->flptr,
                      NAME_AND_LENGTH("PCLXLImageDecode"), NULL, &filter) ;

    if ( ok ) {
      file_store_object(&ofiltered, filter, LITERAL) ;

      if (image_reader->compress_mode == PCLXL_eJPEGCompression) {
        /* For JPEG images, layer a reusable stream decode and DCT decode
           filter. */
        ok = ok && filter_layer_object(&ofiltered, NAME_AND_LENGTH("ReusableStreamDecode"),
                                       NULL, &ofiltered_rsd) ;

        {
          OBJECT nameobj = OBJECT_NOTVM_NOTHING;
          OBJECT dictobj = OBJECT_NOTVM_NOTHING;
          OBJECT dictobjects[ NDICTOBJECTS(1) ];

          init_dictionary(&dictobj, 1, UNLIMITED,
                          dictobjects, ISNOTVMDICTMARK(SAVEMASK));
          theTags(nameobj) = ONAME;
          oName(nameobj) = &system_names[NAME_CloseSource];
          ok = ok && insert_hash_with_alloc(&dictobj, &nameobj, &tnewobj,
                                            INSERT_HASH_NORMAL,
                                            no_dict_extension, NULL);
          ok = ok && filter_layer_object(&ofiltered_rsd,
                                         NAME_AND_LENGTH("DCTDecode"),
                                         &dictobj, &ofiltered_dct);
        }

        *thefilter = ofiltered_dct ;
        image_reader->jpeg_source = oFile(*thefilter) ;

      } else {
        *thefilter = ofiltered ;
        image_reader->jpeg_source = NULL ;
      }
    }
  }

  setglallocmode(context, currentglobal) ;

  return ok ;
}

/* ========================================================================== */
/* PCLXLImageDecode:
 * The PCL XL image decode filter is a lightweight filter wrapper around
 * the PCL XL image interpreter which reads XL commands in the image
 * stream.  The real work is done by pclxl_image_filter_decode.
 */

static Bool pclxl_image_filter_init(FILELIST *filter, OBJECT *args, STACK *stack)
{
  int32 pop_args = 0 ;

  UNUSED_PARAM(OBJECT*, args);

  HQASSERT( filter , "filter NULL in pclxl_image_filter_init" ) ;
  HQASSERT(args != NULL || stack != NULL,
           "Arguments and stack should not both be empty") ;

  /* Get underlying source/target if we have a stack supplied. */
  if ( stack ) {
    if ( theIStackSize(stack) < pop_args )
      return error_handler(STACKUNDERFLOW) ;

    if ( ! filter_target_or_source(filter, stackindex(pop_args, stack)) )
      return FALSE ;

    ++pop_args ;
  }

  theIBuffer( filter ) = theIPtr( filter ) = NULL ;
  theICount( filter ) = 0 ;
  theIBufferSize( filter ) = 0 ;
  theIFilterState( filter ) = FILTER_INIT_STATE ;
  theIFilterPrivate( filter ) = NULL ;

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE;
}

static void pclxl_image_filter_dispose(FILELIST *filter)
{
  UNUSED_PARAM(FILELIST*, filter);

  HQASSERT(filter, "filter NULL in pclxl_image_filter_dispose");
  HQASSERT(isIInputFile(filter), "PCLXLImageEncode? No such thing!");
  HQASSERT(!theIFilterPrivate( filter ), "Don't expect a private state for PCLXLImageDecode");
}

static Bool pclxl_image_filter_decode(FILELIST *filter, int32 *ret_bytes);

Bool pclxl_image_decode_filter_init(void)
{
  FILELIST *flptr ;

  if ( (flptr = mm_alloc_static(sizeof(FILELIST))) == NULL )
    return FALSE ;

  /* subfile decode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("PCLXLImageDecode") ,
                       FILTER_FLAG | READ_FLAG | DELIMITS_FLAG ,
                       0, NULL , 0 ,
                       FilterFillBuff,                       /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       pclxl_image_filter_init,              /* initfile */
                       FilterCloseFile,                      /* closefile */
                       pclxl_image_filter_dispose,           /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       pclxl_image_filter_decode,            /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;

  filter_standard_add(flptr);

  return TRUE ;
}

/* ========================================================================== */

/* See header for doc. */
Bool pclxl_raster_read_context_block_init(PCLXL_PARSER_CONTEXT parser_context,
                                          PCLXL_IMAGE_READ_CONTEXT *image_reader,
                                          int32 start_line,
                                          int32 block_height,
                                          PCLXL_ENUMERATION compress_mode,
                                          int32 pad_bytes)
{
  int32 whole_number, remainder;

  image_reader->start_line = start_line ;
  image_reader->block_height = block_height ;
  image_reader->compress_mode = compress_mode ;
  image_reader->pad_bytes = pad_bytes ;

  whole_number = (int32)(image_reader->width_in_bytes / image_reader->pad_bytes) ;
  remainder = image_reader->width_in_bytes - (whole_number * image_reader->pad_bytes) ;
  if (remainder > 0) {
    whole_number++ ;
    image_reader->throwout_byte_count = (whole_number * image_reader->pad_bytes) -
                                        image_reader->width_in_bytes ;
  } else {
    image_reader->throwout_byte_count = 0 ;
  }

  /* For patterns we need to create the filter chain here. */
  if (compress_mode == PCLXL_eJPEGCompressionForPattern) {
    OBJECT ofiltered = OBJECT_NOTVM_NOTHING ;

    if (! create_decode_filter(parser_context, image_reader, &ofiltered))
      /** \todo raise XL error */
      return FALSE ;
  }

  return TRUE ;
}

/* See header for doc. */
Bool pclxl_raster_read_context_destroy(PCLXL_PARSER_CONTEXT parser_context,
                                       PCLXL_IMAGE_READ_CONTEXT **image_reader,
                                       Bool consume_remaining_data)
{
  Bool status = TRUE;

  HQASSERT(image_reader != NULL, "image_reader is NULL") ;
  HQASSERT(*image_reader != NULL, "*image_reader is NULL") ;

  if ((*image_reader)->compress_mode == PCLXL_eJPEGCompression ||
      (*image_reader)->compress_mode == PCLXL_eJPEGCompressionForPattern) {
    /* We don't know how much underlying data was consumed so can't flush any
     * remaining. */
    if ((*image_reader)->jpeg_source != NULL) {
      HQASSERT((*image_reader)->jpeg_source->mydisposefile != NULL,
                "image_reader jpeg_source disposefile function is NULL");
      (*image_reader)->jpeg_source->mydisposefile((*image_reader)->jpeg_source);
      (*image_reader)->jpeg_source = NULL;
    }

  } else {
    if ( consume_remaining_data ) {
      status = pclxl_throwout_remaining_image_data(parser_context, *image_reader);
    }
  }

  mm_free(parser_context->pclxl_context->memory_pool, *image_reader,
          (*image_reader)->alloc_size) ;

  *image_reader = NULL ;

  return(status);
}

static
Bool pclxl_threshold_read_context_destroy(PCLXL_PARSER_CONTEXT parser_context,
                                          PCLXL_THRESHOLD_READ_CONTEXT **threshold_reader)
{
  Bool status;

  HQASSERT(threshold_reader != NULL, "image_reader is NULL") ;
  HQASSERT(*threshold_reader != NULL, "*image_reader is NULL") ;

  status = pclxl_throwout_remaining_threshold_data(parser_context) ;

  mm_free(parser_context->pclxl_context->memory_pool, *threshold_reader,
          (*threshold_reader)->alloc_size) ;

  *threshold_reader = NULL ;

  return(status);
}


/* ============================================================================
 * Image decoders
 * ============================================================================
 */

/* They return TRUE on success, FALSE if EOF was found. */

static
Bool pclxl_expand_unencoded(PCLXL_PARSER_CONTEXT parser_context,
                            PCLXL_IMAGE_READ_CONTEXT *image_reader)
{
  PCLXLSTREAM* p_stream ;
  int32 bytes;

  HQASSERT(image_reader != NULL, "image_reader is NULL") ;
  p_stream = pclxl_parser_current_stream(parser_context);
  HQASSERT(p_stream != NULL, "p_stream is NULL") ;

  if ( image_reader->embedded_data_len >= image_reader->width_in_bytes ) {
    INLINE_MIN32(bytes, image_reader->width_in_bytes + image_reader->throwout_byte_count, image_reader->embedded_data_len);
    if ( file_read(p_stream->flptr, image_reader->zee_bytes, bytes, NULL) <= 0 ) {
      return(FALSE);
    }
    image_reader->embedded_data_len -= bytes;

    /* We are always going to expand a full raster row. */
    image_reader->uncompressed_bytes = image_reader->width_in_bytes ;
  } else {
    image_reader->uncompressed_bytes = 0 ;
  }

  return(TRUE);

} /* pclxl_expand_unencoded */


static
Bool pclxl_expand_rle(
  PCLXL_PARSER_CONTEXT  parser_context,
  PCLXL_IMAGE_READ_CONTEXT* image_reader)
{
  PCLXLSTREAM* p_stream;
  uint8*  line_data;
  uint8*  line_end;
  int32 bytes_to_read;
  int32 size;
  int32 rle_control;
  int32 rle_out_byte;
  int32 embedded_data_len;

  HQASSERT((parser_context != NULL),
      "parser_context pointer NULL");
  HQASSERT((image_reader != NULL),
      "image_reader pointer NULL");

  p_stream = pclxl_parser_current_stream(parser_context);
  HQASSERT(p_stream != NULL, "stream pointer is NULL");

  /* Unpack image reader variables to autos so compiler can keep them in
   * registers/stack across function calls */
  embedded_data_len = image_reader->embedded_data_len;
  rle_control = image_reader->rle_control;
  rle_out_byte = image_reader->rle_out_byte;

  bytes_to_read = image_reader->width_in_bytes + image_reader->throwout_byte_count;
  line_data = image_reader->zee_bytes;
  line_end = &image_reader->zee_bytes[image_reader->width_in_bytes];

  do {
    if ( rle_control == 0 ) {
      /* Read next repeat/literal length */
      if ( embedded_data_len == 0 ) {
        break;
      }
      if ( (rle_control = Getc(p_stream->flptr)) == EOF ) {
        return(FALSE);
      }
      embedded_data_len--;
      /* No-op command is just consumed */
      if ( rle_control == 128 ) {
        continue;
      }
      if ( rle_control > 128 ) {
        if ( embedded_data_len == 0 ) {
          break;
        }
        if ( (rle_out_byte = Getc(p_stream->flptr)) == EOF ) {
          return(FALSE);
        }
        embedded_data_len--;
        rle_control = 256 - rle_control;
      } else {
        rle_out_byte = -1;
      }
      rle_control++;
    }

    /* Do run length */
    INLINE_MIN32(size, rle_control, bytes_to_read);
    HQASSERT((size > 0),
        "zero byte run length");
    if ( rle_out_byte >= 0 ) {
      HqMemSet8(line_data, (uint8)rle_out_byte, size);

    } else { /* Limit read to what is left in data stream */
      INLINE_MIN32(size, embedded_data_len, size);
      if ( file_read(p_stream->flptr, line_data, size, NULL) <= 0 ) {
        return(FALSE);
      }
      embedded_data_len -= size;
    }
    line_data += size;
    rle_control -= size;
    bytes_to_read -= size;

    HQASSERT((line_data - image_reader->zee_bytes <= image_reader->width_in_bytes + image_reader->throwout_byte_count),
        "Wrote beyond end of zee_bytes)");

  } while ( (bytes_to_read > 0) && (embedded_data_len > 0) );

  /* If there is not enough data, zero pad. */
  if ( line_end > line_data ) {
    HqMemSet8(line_data, 0, line_end - line_data);
  } else {
    HQASSERT((line_data - image_reader->zee_bytes == image_reader->width_in_bytes + image_reader->throwout_byte_count),
        "Didn't expand as much data as expected");
  }

  /* Always returning a full line of image data */
  image_reader->uncompressed_bytes = image_reader->width_in_bytes;

  /* Repack updated image reader variables ready for next call */
  image_reader->embedded_data_len = embedded_data_len;
  image_reader->rle_control = rle_control;
  image_reader->rle_out_byte = rle_out_byte;

  return(TRUE);

} /* pclxl_expand_rle */


static
Bool pclxl_expand_jpeg(PCLXL_PARSER_CONTEXT parser_context,
                       PCLXL_IMAGE_READ_CONTEXT *image_reader)
{
  PCLXLSTREAM* p_stream;
  PCLXL_CONTEXT pclxl_context;
  int32 num_bytes;
  int32 status;

  HQASSERT(image_reader != NULL, "image_reader is NULL") ;
  HQASSERT(parser_context != NULL, "parser_context is NULL") ;

  pclxl_context = parser_context->pclxl_context;
  p_stream = pclxl_parser_current_stream(parser_context);
  HQASSERT(p_stream != NULL, "p_stream is NULL") ;

  num_bytes = min(image_reader->width_in_bytes, image_reader->embedded_data_len);
  status = file_read(p_stream->flptr, image_reader->zee_bytes, num_bytes, NULL);

  if (status <= 0) {
    if (status == EOF) {
      /* HP4700 reports this as missing data, (not e.g. PREMATURE_EOF) */
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_JPEG,
                                 PCLXL_MISSING_DATA,
                                 ("Unexpected EOF when expanding jpeg"));
    }
    else {
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_JPEG,
                                 PCLXL_INTERNAL_ERROR,
                                 ("Unable to expand jpeg"));
    }
    return(FALSE);
  }

  image_reader->embedded_data_len -= num_bytes;
  image_reader->uncompressed_bytes = num_bytes;
  return(TRUE);

} /* pclxl_expand_jpeg */

static
Bool pclxl_expand_jpeg_for_pattern(PCLXL_IMAGE_READ_CONTEXT *image_reader)
{
  Bool status = TRUE ;
  int32 num_bytes, upto_byte, ch ;

  HQASSERT(image_reader != NULL, "image_reader is NULL") ;

  num_bytes = image_reader->width_in_bytes ;
  upto_byte = 0 ;

  /* Alas we can't easily track how much of the underlying XL stream
     has been read, so we don't update 'embedded_data_len'. */
  while (num_bytes-- > 0) {
    if ((ch = Getc(image_reader->jpeg_source)) == EOF)
      return FALSE ;

    image_reader->zee_bytes[upto_byte] = (uint8)ch ;
    upto_byte++ ;
  }
  image_reader->uncompressed_bytes = upto_byte ;

  return status ;
}


static
Bool pclxl_expand_deltarow(
  PCLXL_PARSER_CONTEXT  parser_context,
  PCLXL_IMAGE_READ_CONTEXT* image_reader)
{
  PCLXLSTREAM* p_stream;
  FILELIST* flptr;
  uint8* delta_data;
  uint8* row_data;
  int32 num_bytes;
  int32 offset;
  int32 bytes_to_read;
  int32 delta_bytes;
  int32 ch;
  int32 embedded_data_len;
  int32 count;
  int32 max_bytes;

  HQASSERT((image_reader != NULL),
      "image_reader pointer NULL");

  image_reader->uncompressed_bytes = 0;

  if ( image_reader->is_first_decode_segment ) {
    pclxl_zero_seed_row(image_reader->zee_bytes, image_reader->width_in_bytes);
  }

  /* Not even enough bytes in the block to read the byte count. Abort job. */
  if ( image_reader->embedded_data_len < 2 ) {
    return(image_reader->embedded_data_len == 0);
  }

  /* Get the 2 byte byte count which indicates the number of bytes to follow the
   * delta row. */
  p_stream = pclxl_parser_current_stream(parser_context);
  HQASSERT((p_stream != NULL), "stream pointer NULL");

  if ( (delta_bytes = Getc(p_stream->flptr)) == EOF ) {
    return(FALSE);
  }
  if ( (ch = Getc(p_stream->flptr)) == EOF ) {
    return(FALSE);
  }
  delta_bytes = ((ch & 0xff) << 8) | (delta_bytes & 0xff);
  image_reader->embedded_data_len -= 2;

  /* A byte count of 0 means repeat last row */
  if ( delta_bytes > 0 ) {
    /* Amount of data including padding to read */
    bytes_to_read = image_reader->width_in_bytes + image_reader->throwout_byte_count;

    /* Use pointers to walk and detect end of delta row data */
    row_data = image_reader->zee_bytes;
    embedded_data_len = image_reader->embedded_data_len;

    flptr = p_stream->flptr;

    do {
      /* Get number of delta bytes and initial offset from the control byte */
      if ( (ch = Getc(flptr)) == EOF ) {
        return(FALSE);
      }
      if ( (--embedded_data_len == 0) || (--delta_bytes == 0) ) {
        break;
      }

      num_bytes = ((ch >> 5) & 0x07) + 1;
      offset = (ch & 0x1f);

      /* Handle offsets > 30 bytes */
      if ( offset == 31 ) {
        INLINE_MIN32(max_bytes, embedded_data_len, delta_bytes);
        count = max_bytes;
        do {
          if ( (ch = Getc(flptr)) == EOF ) {
            return(FALSE);
          }
          offset += (ch & 0xff);
        } while ( (--count > 0) && ((ch & 0xff) == 255) );
        max_bytes -= count;
        if ( ((embedded_data_len -= max_bytes) == 0) || ((delta_bytes -= max_bytes) == 0) ) {
          break;
        }
      }
      bytes_to_read -= offset;

      INLINE_MIN32(num_bytes, num_bytes, delta_bytes);
      INLINE_MIN32(num_bytes, num_bytes, embedded_data_len);
      if ( bytes_to_read > 0 ) {
        /* Still filling raster row - limit to space left */
        INLINE_MIN32(num_bytes, num_bytes, bytes_to_read);
        HQASSERT(((num_bytes > 0) && (num_bytes < 9)),
            "Invalid delta byte count");
        row_data += offset;
        if ( theICount(flptr) > num_bytes ) {
          /* Optimisation - the underlying filter buffer has all the bytes so
           * access the buffer directly.
           */
          delta_data = theIPtr(flptr);
          switch ( num_bytes ) {
            case 8: *row_data++ = *delta_data++;
            case 7: *row_data++ = *delta_data++;
            case 6: *row_data++ = *delta_data++;
            case 5: *row_data++ = *delta_data++;
            case 4: *row_data++ = *delta_data++;
            case 3: *row_data++ = *delta_data++;
            case 2: *row_data++ = *delta_data++;
            case 1: *row_data++ = *delta_data++;
          }
          theIPtr(flptr) = delta_data;
          theICount(flptr) -= num_bytes;

        } else { /* Bytes cross filter buffer - have Getc look after refill */
          count = num_bytes;
          do {
            if ( (ch = Getc(flptr)) == EOF ) {
              return(FALSE);
            }
            *row_data++ = (uint8)(ch & 0xff);
          } while ( --count > 0 );
        }
        HQASSERT((row_data <= &image_reader->zee_bytes[image_reader->width_in_bytes + image_reader->throwout_byte_count]),
            "Overflowed row data array");

        bytes_to_read -= num_bytes;
        HQASSERT((bytes_to_read >= 0),
            "Read more data than needed");

      } else {  /* Row is full, silently consume remaining delta row data */
        count = num_bytes;
        do {
          if ( (ch = Getc(flptr)) == EOF ) {
            return(FALSE);
          }
        } while ( --count > 0 );
      }

      embedded_data_len -= num_bytes;
      delta_bytes -= num_bytes;

      HQASSERT(((embedded_data_len >= 0) && (delta_bytes >= 0)),
          "Read too much delta data");

    } while ( (embedded_data_len > 0) && (delta_bytes > 0) );

    image_reader->embedded_data_len = embedded_data_len;
  }

  /* A full row is always returned. */
  image_reader->uncompressed_bytes = image_reader->width_in_bytes;

  return(TRUE);

} /* pclxl_expand_deltarow */


/* ============================================================================
 * Read the image data and dispatch appropriate decoder
 * ============================================================================
 */
static
Bool pclxl_get_image_block_details(PCLXL_PARSER_CONTEXT parser_context,
                                   PCLXL_IMAGE_READ_CONTEXT *image_reader)
{
  static PCLXL_ATTR_MATCH match[6] = {
#define IMAGEBLOCK_COMPRESS_MODE  (0)
    {PCLXL_AT_CompressMode | PCLXL_ATTR_REQUIRED},
#define IMAGEBLOCK_START_LINE     (1)
    {PCLXL_AT_StartLine | PCLXL_ATTR_REQUIRED},
#define IMAGEBLOCK_BLOCK_HEIGHT   (2)
    {PCLXL_AT_BlockHeight | PCLXL_ATTR_REQUIRED},
#define IMAGEBLOCK_PAD_BYTES_MULTIPLE (3)
    {PCLXL_AT_PadBytesMultiple},
#define IMAGEBLOCK_BLOCK_BYTE_LENGTH  (4)
    {PCLXL_AT_BlockByteLength},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION compress_mode_values[] = {
    PCLXL_eNoCompression,
    PCLXL_eRLECompression,
    PCLXL_eJPEGCompression,
    PCLXL_eDeltaRowCompression,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  int32 block_height;
  int32 start_line;
  int32 pad_bytes;
  uint32 data_length;
  Bool stream_21;
  PCLXL_ENUMERATION compress_mode;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* CompressMode */
  if ( !pclxl_attr_match_enumeration(match[IMAGEBLOCK_COMPRESS_MODE].result, compress_mode_values,
                                     &compress_mode, pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }
  stream_21 = pclxl_stream_min_protocol(pclxl_parser_current_stream(parser_context),
                                        PCLXL_PROTOCOL_VERSION_2_1);
  if ( !stream_21 && (compress_mode == PCLXL_eDeltaRowCompression) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_IMAGE, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                        ("Got delta row compression in pre 21 XL stream"));
    return(FALSE);
  }
  /* StartLine */
  start_line = pclxl_attr_get_int(match[IMAGEBLOCK_START_LINE].result);
  /* BlockHeight */
  block_height = pclxl_attr_get_int(match[IMAGEBLOCK_BLOCK_HEIGHT].result);
  /* PadBytesMultiple */
  pad_bytes = 4;
  if ( match[IMAGEBLOCK_PAD_BYTES_MULTIPLE].result ) {
    pad_bytes = pclxl_attr_get_int(match[IMAGEBLOCK_PAD_BYTES_MULTIPLE].result);
  }
  /* Note: doc is contradictory, say 1 to 4 in ReadImage and 1 to 255 in appendix */
  if ( (pad_bytes < 1) || (pad_bytes > 4) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_IMAGE, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                        ("Image data pad value out of range [1,4]"));
    return(FALSE);
  }
  /* BlockByteLength */
  if ( match[IMAGEBLOCK_BLOCK_BYTE_LENGTH].result ) {
    /* Note - this is overwritten below anyway */
    image_reader->embedded_data_len =
      CAST_UNSIGNED_TO_INT32(pclxl_attr_get_uint(match[IMAGEBLOCK_BLOCK_BYTE_LENGTH].result));
  }

  HQASSERT(image_reader != NULL, "image_reader is NULL") ;

  if ( !pclxl_stream_read_data_length(pclxl_context,
                                      pclxl_parser_current_stream(parser_context),
                                      &data_length) ) {
    return(FALSE);
  }
  image_reader->embedded_data_len = CAST_UNSIGNED_TO_INT32(data_length);

  return(pclxl_raster_read_context_block_init(parser_context, image_reader,
                                              start_line, block_height,
                                              compress_mode, pad_bytes));
}

/* See header for doc. */
Bool pclxl_raster_decode_block_segment(PCLXL_PARSER_CONTEXT parser_context,
                                       PCLXL_IMAGE_READ_CONTEXT *image_reader)
{
  Bool status = TRUE ;

  HQASSERT(image_reader != NULL, "image_reader is NULL") ;

  if (image_reader->num_rows_seen == image_reader->source_height) {
    image_reader->uncompressed_bytes = 0 ;

    if (image_reader->compress_mode == PCLXL_eJPEGCompression ||
        image_reader->compress_mode == PCLXL_eJPEGCompressionForPattern) {
      /* We haven't tracked the number of bytes read for JPEGs, so assume we've
       * read it all. */
      image_reader->embedded_data_len = 0;
    }
  } else {
    switch (image_reader->compress_mode) {
    case PCLXL_eNoCompression:
      image_reader->rle_control = 0 ;
      image_reader->rle_out_byte = -1 ;
      status = pclxl_expand_unencoded(parser_context, image_reader) ;
      break ;
    case PCLXL_eRLECompression:
      status = pclxl_expand_rle(parser_context, image_reader) ;
      break ;
    case PCLXL_eJPEGCompression:
      image_reader->rle_control = 0 ;
      image_reader->rle_out_byte = -1 ;
      status = pclxl_expand_jpeg(parser_context, image_reader) ;
      break ;
    case PCLXL_eJPEGCompressionForPattern:
      image_reader->rle_control = 0 ;
      image_reader->rle_out_byte = -1 ;
      status = pclxl_expand_jpeg_for_pattern(image_reader) ;
      break ;
    case PCLXL_eDeltaRowCompression:
      image_reader->rle_control = 0 ;
      image_reader->rle_out_byte = -1 ;
      status = pclxl_expand_deltarow(parser_context, image_reader) ;
      break ;
    default:
      HQFAIL("Unrecognised compression mode.") ;
    }

    image_reader->is_first_decode_segment = FALSE ;

    if (image_reader->uncompressed_bytes != 0) {
      image_reader->num_rows_seen++ ;
#if 0
      image_reader->block_height-- ;
#endif
    }

#if 0
    if (image_reader->block_height == 0) {
      (void)pclxl_throwout_remaining_image_data(parser_context) ;
      image_reader->rle_control = 0 ;
      image_reader->out_byte = -1 ;
    }
#endif
  }
  return status ;
}

static Bool pclxl_image_filter_decode(FILELIST *filter, int32 *ret_bytes)
{
  PCLXL_PARSER_CONTEXT parser_context ;
  PCLXL_IMAGE_READ_CONTEXT *image_reader ;
  Bool is_jpeg = FALSE ;
  Bool status = TRUE ;

  parser_context = pclxl_get_parser_context() ;
  HQASSERT(parser_context != NULL, "parser_context is NULL") ;

  /* Some protection from invalid use. */
  image_reader = parser_context->image_reader ;
  if (image_reader == NULL) {
    HQFAIL("This operator should never be called outside of a pclxlexec.") ;
    return error_handler(UNDEFINED) ;
  }

  image_reader->within_PS_imageread_callback = TRUE ;

  if (image_reader->compress_mode == PCLXL_eJPEGCompression ||
      image_reader->compress_mode == PCLXL_eJPEGCompressionForPattern) {
    is_jpeg = TRUE;
    status = pclxl_expand_jpeg(parser_context, image_reader) ;

  } else { /* its not a JPEG */

    /* We still have bytes uncompressed in the scan line. */
    if (image_reader->bytes_remaining_in_scan_line > 0) {
      image_reader->start_of_string += PS_MAX_STRING_LENGTH ;
      image_reader->uncompressed_bytes = image_reader->bytes_remaining_in_scan_line ;

    } else {

      image_reader->start_of_string = image_reader->zee_bytes ;

      /* A greater than zero embedded data length implies we are in
         the middle of decoding an image block of data so we should
         not look for a ReadImage operator or we are repeating a byte
         in an RLE decode. */
      if (image_reader->embedded_data_len == 0 &&  image_reader->rle_control == 0) {
        /* We do not have any data to consume so go looking for a
           ReadImage. */
        if (image_reader->end_image_seen) {
          set_exit_parser(parser_context, TRUE) ;
        } else {
          set_exit_parser(parser_context, FALSE) ;
          /* This is how the pclxl_scan() success is tested within
             pclxlexec. */
          if (status)
            status = (pclxl_scan(parser_context) >= EOF) ;
        }

        /* If we have seen an EndImage or had an error, we want to exit the
         * BeginImage pclxlscan(). */
        set_exit_parser(parser_context, (!status || image_reader->end_image_seen));
      }

      /* Decode some data. */
      status = status && pclxl_raster_decode_block_segment(parser_context,
                                                           image_reader) ;

      /* If we have not been able to uncompress any bytes, we want to
         exit the BeginImage pclxlscan(). */
      if (image_reader->uncompressed_bytes == 0) {
        set_exit_parser(parser_context, TRUE) ;
      }

      if (image_reader->uncompressed_bytes > PS_MAX_STRING_LENGTH) {
        image_reader->bytes_remaining_in_scan_line = image_reader->uncompressed_bytes - PS_MAX_STRING_LENGTH ;
        image_reader->uncompressed_bytes = PS_MAX_STRING_LENGTH ;
      } else {
        image_reader->bytes_remaining_in_scan_line = 0 ;
      }

    }
  }

  theIBuffer(filter) = image_reader->start_of_string ;

  *ret_bytes = image_reader->uncompressed_bytes ;

  image_reader->within_PS_imageread_callback = FALSE ;
  image_reader->decode_ok = status;
  if (is_jpeg)
    return status;
  else
    return TRUE ;
}

/** \todo Read multiple rows of threshold data in one go.
 *
 * Threshold data is read in one block only, so don't need a check on rows.
 * The correct amount of data for entire threshold is available at this point.
 *
 * Operation is currently to read only a single row of threshold data, then
 * discard the padding.
 */
Bool pclxl_read_threshold_segment(PCLXL_PARSER_CONTEXT parser_context,
                                PCLXL_THRESHOLD_READ_CONTEXT *threshold_reader)
{
  Bool status = TRUE ;
  PCLXLSTREAM* p_stream ;
  int32 num_bytes, upto_byte, ch ;

  HQASSERT(threshold_reader != NULL, "threshold_reader is NULL") ;

  p_stream = pclxl_parser_current_stream(parser_context);
  HQASSERT(p_stream != NULL, "p_stream is NULL") ;

  num_bytes = threshold_reader->width_in_bytes ;
  upto_byte = 0 ;

  if (threshold_reader->embedded_data_len >= num_bytes) {
    while (num_bytes-- > 0) {
      if ((ch = Getc(p_stream->flptr)) == EOF)
        return FALSE ;
      threshold_reader->embedded_data_len-- ;
      threshold_reader->zee_bytes[upto_byte] = (uint8)ch ;
      upto_byte++ ;
    }

    /* Throw out any pad bytes. */
    num_bytes = threshold_reader->throwout_byte_count ;
    while (num_bytes-- > 0 && threshold_reader->embedded_data_len > 0) {
      if ((ch = Getc(p_stream->flptr)) == EOF)
        return FALSE ;
      threshold_reader->embedded_data_len-- ;
    }

    /* We are always going to expand a full raster row. */
    threshold_reader->uncompressed_bytes = threshold_reader->width_in_bytes ;
  } else {
    threshold_reader->uncompressed_bytes = 0 ;
  }

  return status ;
}

/* threshold arrays need to read similarly to images. */
Bool pclxlthresholdread_(ps_context_t *pscontext)
{
  PCLXL_PARSER_CONTEXT parser_context ;
  PCLXL_THRESHOLD_READ_CONTEXT *threshold_reader ;
  Bool status = TRUE ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  parser_context = pclxl_get_parser_context() ;
  HQASSERT(parser_context != NULL, "parser_context is NULL") ;

  /* Some protection from invalid use. */
  threshold_reader = parser_context->threshold_reader ;
  if (threshold_reader == NULL) {
    HQFAIL("This operator should never be called outside of a pclxlexec.") ;
    return error_handler(UNDEFINED) ;
  }

  /* We still have bytes in the scan line. */
  if (threshold_reader->bytes_remaining > 0)
  {
    threshold_reader->start_of_string += PS_MAX_STRING_LENGTH ;
    threshold_reader->uncompressed_bytes
      = threshold_reader->bytes_remaining ;
  }
  else
  {
    threshold_reader->start_of_string = threshold_reader->zee_bytes ;

    /* read data from the threshold data source, and provided it to the
     * consummer in chunks no bigger than the max length of PS string.
     */
    if (threshold_reader->embedded_data_len != 0)
      status = pclxl_read_threshold_segment(parser_context, threshold_reader) ;
  }

  if (threshold_reader->uncompressed_bytes > PS_MAX_STRING_LENGTH) {
    threshold_reader->bytes_remaining
      = threshold_reader->uncompressed_bytes - PS_MAX_STRING_LENGTH ;
    threshold_reader->uncompressed_bytes = PS_MAX_STRING_LENGTH ;
  } else {
    threshold_reader->bytes_remaining = 0 ;
  }

  oString(threshold_reader->string_object)
    = threshold_reader->start_of_string ;

  if (status) {
    theLen(threshold_reader->string_object)
      = CAST_SIGNED_TO_UINT16(threshold_reader->uncompressed_bytes) ;
  } else {
    theLen(threshold_reader->string_object) = 0 ;
  }

  /* Always return the bytes as a string whether we have a failure or
     not. */
  push(&(threshold_reader->string_object), &operandstack) ;

  return TRUE ;
}

/* ============================================================================
 * Dispatch the rendering of an image via PS
 * ============================================================================
 */

static Bool pclxl_image_args(PCLXL_PARSER_CONTEXT parser_context,
                             PCLXL_IMAGE_READ_CONTEXT *image_reader,
                             USERVALUE decodes[],
                             OBJECT *data_source,
                             IMAGEARGS *imageargs)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  int32 decode_size ;

  HQASSERT(image_reader != NULL, "image_reader is NULL") ;
  HQASSERT(image_reader->is_first_decode_segment,
           "We seem to be creating image args when not decoding first block segment.") ;

  init_image_args(imageargs, GSC_IMAGE) ;
  imageargs->imageop = NAME_image ;
  imageargs->imagetype = TypeImageImage ;
  imageargs->interleave = INTERLEAVE_NONE ;
  imageargs->ncomps = gsc_dimensions(gstateptr->colorInfo, GSC_FILL) ;
  imageargs->image_color_space =
    gsc_getcolorspace(gstateptr->colorInfo, GSC_FILL) ;
  Copy(object_slot_notvm(&imageargs->image_color_object),
       gsc_getcolorspaceobject(gstateptr->colorInfo, GSC_FILL)) ;
  /* Struct copy now OK, the image_color_object is NOTVM. */
  imageargs->colorspace = imageargs->image_color_object ;

  imageargs->width = image_reader->source_width ;
  imageargs->height = imageargs->lines_per_block = image_reader->source_height ;

  imageargs->omatrix.matrix[0][0] = image_reader->source_width ;
  imageargs->omatrix.matrix[1][1] = image_reader->source_height ;
  imageargs->omatrix.opt = MATRIX_OPT_0011 ;

  imageargs->nprocs = 1 ;
  if (! create_decode_filter(parser_context, image_reader, data_source))
    return FALSE ;
  imageargs->data_src = data_source ;
  if ( oType(*data_source) == OFILE ) {
    imageargs->n_src_close = 1 ;
    imageargs->data_src_close = data_source ;
  }

  imageargs->decode = decodes ;
  decode_size = image_reader->num_components ;
  HQASSERT(decode_size <= MAX_PLANES, "Too many channels in PCL image") ;
  if (image_reader->is_indexed) {
    int32 max_decode = (1 << image_reader->bits_per_component) - 1 ;
    HQASSERT(max_decode <= 255, "decode max is too large") ;
    while (--decode_size >= 0) {
      decodes[decode_size * 2] = 0 ;
      decodes[decode_size * 2 + 1] = (USERVALUE)max_decode ;
    }
  } else {
    while (--decode_size >= 0) {
      decodes[decode_size * 2] = 0 ;
      decodes[decode_size * 2 + 1] = 1 ;
    }
  }

  imageargs->bits_per_comp = image_reader->bits_per_component ;

  imageargs->image_pixel_centers = FALSE ;
  imageargs->clean_matrix = FALSE ;

  /* NYI check_image_misc_hooks(). */

  set_image_order(imageargs);

  return filter_image_args(pclxl_context->corecontext, imageargs) ;
}

static
Bool pclxl_dispatch_image(PCLXL_PARSER_CONTEXT parser_context,
                          PCLXL_IMAGE_READ_CONTEXT *image_reader)
{
  PCLXL_CONTEXT pclxl_context ;
  PCLXL_GRAPHICS_STATE graphics_state ;
  PCLXL_NON_GS_STATE non_gs_state ;
  double image_x_scale, image_y_scale, x_factor, y_factor ;
  PCLXL_COLOR_SPACE_DETAILS_STRUCT image_color_space ;

  /* I admit it, I'm paranoid that all these pointers are OK. */
  HQASSERT(image_reader != NULL, "image_reader is NULL") ;
  HQASSERT(parser_context != NULL, "parser_context is NULL") ;
  pclxl_context = parser_context->pclxl_context ;
  HQASSERT(pclxl_context != NULL, "pclxl_context is NULL") ;
  graphics_state = pclxl_context->graphics_state ;
  HQASSERT(graphics_state != NULL, "graphics_state is NULL") ;
  non_gs_state = &pclxl_context->non_gs_state;
  if (!finishaddchardisplay(pclxl_context->corecontext->page, 1)) {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to flush chars to DL"));
    return FALSE;
  }

  (void) pclxl_ps_set_rop3(pclxl_context, graphics_state->ROP3, FALSE);

  x_factor = (double)image_reader->destination_width / (double)image_reader->source_width ;
  y_factor = (double)image_reader->destination_height / (double)image_reader->source_height ;
  image_x_scale = (double)image_reader->source_width ;
  image_y_scale = (double)image_reader->source_height ;
  image_x_scale = image_x_scale * x_factor ;
  image_y_scale = image_y_scale * y_factor ;

  {
    PCLXL_SysVal_XY current_point_xy ;

    /* Install the image transformation. */
    OMATRIX image_ctm = graphics_state->current_ctm ;

    if (! pclxl_ps_get_current_point(&current_point_xy))
      return FALSE ;

    matrix_translate(&image_ctm, current_point_xy.x, current_point_xy.y, &image_ctm) ;
    matrix_scale(&image_ctm, image_x_scale, image_y_scale, &image_ctm) ;

    if (! gs_setctm(&image_ctm, FALSE))
      return FALSE ;
  }

  image_color_space = *graphics_state->color_space_details ;

  if (graphics_state->color_space_details->color_palette_len > 0 &&
      (! image_reader->is_indexed)) {
    image_color_space.color_palette = NULL ;
    image_color_space.color_palette_len = 0 ;
  }

  /* Set the current paint color. Note that this is NOT setting color
     for an image. In fact, we must not used indexed color spaces when
     setting the foreground source. */

  if (
#define PCLXL_SUPPRESS_VECTOR_TONER_BLACK_TYPE_AROUND_IMAGES 0
#if PCLXL_SUPPRESS_VECTOR_TONER_BLACK_TYPE_AROUND_IMAGES
        /*
         * There is (upto 11-Nov-2009) a known problem
         * that setting a shade of gray as the current "brush source"
         * (and in particular certain shades of gray like 64 out of the range 0 to 255)
         * that cause ROPing issues when attempting to use eProcessBlack for images
         * and eTonerBlack for vector objects/line-work
         *
         * Hence this "work-around" that needs to be removed when
         * the underlying problem has been removed
         */
        (image_color_space.vector_black_type == PCLXL_eTonerBlack &&
         !pclxl_ps_set_black_preservation(pclxl_context,
                                          &image_color_space,
                                          TRUE))
       ||
#endif
          !pclxl_ps_set_color(pclxl_context,
                              &graphics_state->fill_details.brush_source, FALSE /* For an image? */)
        /* This captures the current color into the foreground color, as well as
           setting the source flag: */
       || !setPclForegroundSource(pclxl_context->corecontext->page,
                                  PCL_FOREGROUND_IN_PCL_ATTRIB)
     ) {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_IMAGE,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to set PCL color details"));
    return FALSE ;
  }

  /* Set the pattern before the image colorspace as it may change
   * the core colorspace.
   */
  pclxl_ps_set_pattern(pclxl_context,
                       &graphics_state->fill_details.brush_source);

  /* Set up the image colorspace. */
  if (! pclxl_ps_set_colorspace(pclxl_context, &image_color_space))
    return FALSE ;

  {
    IMAGEARGS imageargs ;
    USERVALUE decodes[MAX_PLANES * 2] ;
    OBJECT data_source = OBJECT_NOTVM_NOTHING ;
    Bool ok ;

    /** \todo ajcd 2013-12-03: Do we need to flush_vignette() and test
        DEVICE_INVALID_CONTEXT() here as gs_image does? */

    /** \todo ajcd 2013-12-04: stack isn't used, except for the npop(), which
        asserts if there isn't any stack to remove from. */
    PROBE(SW_TRACE_INTERPRET_IMAGE, 6,
          ok = (pclxl_image_args(parser_context, image_reader,
                                 decodes, &data_source, &imageargs) &&
                DEVICE_IMAGE(pclxl_context->corecontext->page, &operandstack,
                             &imageargs))) ;
    finish_image_args(&imageargs) ;

    if ( !ok ) {
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_IMAGE,
                                 PCLXL_INTERNAL_ERROR,
                                 ("Problem creating PCL image"));
      return FALSE;
    }
  }

#if PCLXL_SUPPRESS_VECTOR_TONER_BLACK_TYPE_AROUND_IMAGES
  /* See comment about this work-around above */
  if ( ! (image_color_space.vector_black_type == PCLXL_eProcessBlack ||
          pclxl_ps_set_black_preservation(pclxl_context,
                                          &image_color_space,
                                          FALSE))) {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_IMAGE,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to set PCL color details"));
     return FALSE;
  }
#endif

  set_exit_parser(parser_context, TRUE) ;

  /* Reset the ctm. */
  {
    OMATRIX existing_ctm = graphics_state->current_ctm ;
    if (! gs_setctm(&existing_ctm, FALSE))
      return FALSE ;
  }

  return pclxl_throwout_remaining_image_data(parser_context, image_reader) ;
}

/* ============================================================================
 * PCL XL operators below here.
 * ============================================================================
 */

/*
 * Tag 0xb0 BeginImage
 */
Bool pclxl_op_begin_image(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[6] = {
#define BEGINIMAGE_COLOR_MAPPING    (0)
    {PCLXL_AT_ColorMapping | PCLXL_ATTR_REQUIRED},
#define BEGINIMAGE_COLOR_DEPTH      (1)
    {PCLXL_AT_ColorDepth | PCLXL_ATTR_REQUIRED},
#define BEGINIMAGE_SOURCE_WIDTH     (2)
    {PCLXL_AT_SourceWidth | PCLXL_ATTR_REQUIRED},
#define BEGINIMAGE_SOURCE_HEIGHT    (3)
    {PCLXL_AT_SourceHeight | PCLXL_ATTR_REQUIRED},
#define BEGINIMAGE_DESTINATION_SIZE (4)
    {PCLXL_AT_DestinationSize | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION color_mapping_values[] = {
    PCLXL_eDirectPixel,
    PCLXL_eIndexedPixel,
    PCLXL_ENUMERATION_END
  };
  static PCLXL_ENUMERATION color_depth_values[] = {
    PCLXL_e1Bit,
    PCLXL_e4Bit,
    PCLXL_e8Bit,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_ENUMERATION color_mapping;
  PCLXL_ENUMERATION color_depth;
  PCLXL_IMAGE_READ_CONTEXT *image_reader;
  PCLXL_SysVal_XY destination_size;
  int32 source_width;
  int32 source_height;
  PCLXLSTREAM*  p_stream ;
  Bool status ;
  Bool more_data = TRUE ;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* ColorMapping */
  if ( !pclxl_attr_match_enumeration(match[BEGINIMAGE_COLOR_MAPPING].result, color_mapping_values,
                                     &color_mapping, pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }
  /* ColorDepth */
  if ( !pclxl_attr_match_enumeration(match[BEGINIMAGE_COLOR_DEPTH].result, color_depth_values,
                                     &color_depth, pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }
  /* SourceWidth */
  source_width = pclxl_attr_get_int(match[BEGINIMAGE_SOURCE_WIDTH].result);
  if ( source_width < 1 ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_IMAGE, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                        ("Image block width < 1"));
    return(FALSE);
  }
  /* SourceHeight */
  source_height = pclxl_attr_get_int(match[BEGINIMAGE_SOURCE_HEIGHT].result);
  if ( source_height < 1 ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_IMAGE, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                        ("Image block height < 1"));
    return(FALSE);
  }
  /* DestinationSize */
  pclxl_attr_get_real_xy(match[BEGINIMAGE_DESTINATION_SIZE].result, &destination_size);
  if ( (destination_size.x == 0) || (destination_size.y == 0)) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_IMAGE, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                        ("Image destination width or height is zero"));
    return(FALSE);
  }

  image_reader = parser_context->image_reader;

  if ( image_reader != NULL ) {
    return(TRUE);
  }

  if ( !pclxl_context->graphics_state->current_point ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_IMAGE, PCLXL_CURRENT_CURSOR_UNDEFINED,
                        ("There is no current position"));
    return(FALSE);
  }

  if ( (color_mapping == PCLXL_eIndexedPixel) &&
       (pclxl_context->graphics_state->color_space_details->color_palette_len == 0) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_RASTER, PCLXL_PALETTE_UNDEFINED,
                        ("IndexedPixel image requires an indexed colorspace"));
    return FALSE ;
  }

  /* The only reason we place a image reader pointer in the parser
   * context is because the operator callbacks get a parser context
   * and we need the image reader. */
  if ( !pclxl_raster_read_context_create(parser_context, &(parser_context->image_reader),
                                         color_mapping, color_depth, source_width,
                                         source_height, destination_size, TRUE)) {
    return FALSE ;
  }

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
              ("BeginImage(ColorMapping = %d, ColorDepth = %d (enum) SourceWidth = %d, SourceHeight = %d, DestinationSize = (%f, %f))",
               color_mapping, color_depth,
               source_width, source_height,
               destination_size.x, destination_size.y));

  /* N.B. EOF is just a convenient way to divide up the results,
   *      pclxl_scan never actually returns EOF.
   */
  PROBE(SW_TRACE_INTERPRET_PCLXL_IMAGE, parser_context->image_reader,
        status = (pclxl_scan(parser_context) >= EOF));

  status = status && parser_context->image_reader->decode_ok;
  /* If we have reached EOF we expect p_stream to be NULL */
  p_stream = pclxl_parser_current_stream(parser_context);

  if (p_stream)
    /* We do not usually want to exit the top level PCL XL interpreter. */
    set_exit_parser(parser_context, FALSE) ;
  else {
    set_exit_parser(parser_context, TRUE) ;
    more_data = FALSE ;
  }


  return(pclxl_raster_read_context_destroy(parser_context, &parser_context->image_reader, (more_data ? TRUE : FALSE)) && status);
}

/*
 * Tag 0xb1 ReadImage
 */
Bool pclxl_op_read_image(PCLXL_PARSER_CONTEXT parser_context)
{
  Bool status = TRUE ;
  PCLXL_IMAGE_READ_CONTEXT *image_reader ;

  image_reader = parser_context->image_reader ;

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS, ("ReadImage(...)"));

  if (image_reader == NULL) {
    uint32 embedded_data_len ;
    int32 ch ;
    PCLXLSTREAM* p_stream = pclxl_parser_current_stream(parser_context);
    /* If image_reader is NULL, this means the PS image machinary had
       enough data from a previous ReadImage but we have another one
       available. So, we need to throw this data out. */
    HQASSERT(p_stream != NULL, "p_stream is NULL") ;
    if ( !pclxl_stream_read_data_length(parser_context->pclxl_context, p_stream, &embedded_data_len) ) {
      return FALSE ;
    }

    while (embedded_data_len-- > 0) {
      if ((ch = Getc(p_stream->flptr)) == EOF)
        return TRUE ;
    }
    return TRUE ;
  }

  HQASSERT(image_reader != NULL, "image_reader is NULL") ;

  if (image_reader->within_PS_imageread_callback) {
    set_exit_parser(parser_context, TRUE) ;

    status = status && pclxl_get_image_block_details(parser_context,
                                                     image_reader) ;
  } else {
    if (image_reader->is_first_decode_segment) {
      status = status && pclxl_get_image_block_details(parser_context,
                                                       image_reader) ;
      status = status && pclxl_dispatch_image(parser_context,
                                              parser_context->image_reader) ;
    } else {
      /* Getting a ReadImage callback which is not from the PS image
         operator pclxlimageread callback means that the PS machinery
         has consumed enough data but it seems we have more in the
         stream. */

      /* Mmm, seems we have left over data from the previous call as
         well. Throw them out before reading details for the current
         block. */
      if (image_reader->embedded_data_len > 0) {
        status = pclxl_throwout_remaining_image_data(parser_context,
                                                     image_reader) ;
      }

      /* Consume and throw out un-needed bytes. */
      status = status && pclxl_get_image_block_details(parser_context, image_reader) ;
      /* Throw out data for this block. */
      status = status && pclxl_throwout_remaining_image_data(parser_context,
                                                             image_reader) ;
    }
  }

  return status ;
}

/*
 * Tag 0xb2 EndImage
 */
Bool pclxl_op_end_image(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  Bool status = TRUE ;
  PCLXL_IMAGE_READ_CONTEXT *image_reader ;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match_empty(parser_context->attr_set, pclxl_context,
                                   PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  image_reader = parser_context->image_reader ;

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS, ("EndImage"));

  if (image_reader != NULL) {
    image_reader->end_image_seen = TRUE ;

    /* If the embedded data length is greater than zero, it means that
       the PS image machinery thinks it has enough data but the stream
       contains more data than is actually needed. So we should throw it
       out. */
    if (image_reader->embedded_data_len > 0) {
      /* Consume and throw out un-needed bytes. */
      status = pclxl_throwout_remaining_image_data(parser_context,
                                                   image_reader) ;
    }

    /* If we get an EndImage but the PS image machinery thinks we need
       more data, it means we are in a recursive scan() interpreter
       call. We need to test this special case as we need to abort
       it. */

    /* If interpreter has been invoked from the pclxlimageread PS
       callback, interrupt that interpreter and return zero bytes
       read. */
    if (image_reader->within_PS_imageread_callback)
      set_exit_parser(parser_context, TRUE) ;

    image_reader->uncompressed_bytes = 0 ;
  }

  return status ;
}

static
Bool build_threshold_dict(
  OBJECT *halftone_dict,
  PCLXL_THRESHOLD_READ_CONTEXT *threshold_reader)
{
  OBJECT nameobj = OBJECT_NOTVM_NOTHING;
  OBJECT value = OBJECT_NOTVM_NOTHING;
  OBJECT ofiltered = OBJECT_NOTVM_NOTHING;
  OBJECT data_source = OBJECT_NOTVM_NOTHING;
  FILELIST *subfilt;
  int32 halftone_type = 6;

  if (! ps_dictionary(halftone_dict, 4))
    return FALSE ;

  /* HalftoneType */
  object_store_name(&nameobj, NAME_HalftoneType, LITERAL) ;
  object_store_integer(&value, halftone_type) ;
  if (! insert_hash(halftone_dict, &nameobj, &value) )
    return FALSE ;

  /* Width */
  object_store_name(&nameobj, NAME_Width, LITERAL) ;
  object_store_integer(&value, threshold_reader->threshold_array_width ) ;
  if (! insert_hash(halftone_dict, &nameobj, &value) )
    return FALSE ;

  /* Height */
  object_store_name(&nameobj, NAME_Height, LITERAL) ;
  object_store_integer(&value, threshold_reader->threshold_array_height) ;
  if (! insert_hash(halftone_dict, &nameobj, &value) )
    return FALSE ;

  /* Threshold */
  object_store_name(&nameobj, NAME_Thresholds, LITERAL) ;

  subfilt = filter_standard_find(NAME_AND_LENGTH("SubFileDecode")) ;
  HQASSERT(subfilt != NULL, "Lost SubFileDecode") ;

  if (! ps_array(&data_source, 1))
    return FALSE ;
  theTags(data_source) |= EXECUTABLE ;
  object_store_operator(&oArray(data_source)[0], NAME_pclxlthresholdread) ;

  oInteger(inewobj) = 0 ;
  theLen(snewobj) = 0 ;
  oString(snewobj) = NULL ;
  if (!push3(&data_source, &inewobj, &snewobj, &operandstack)) {
    return (FALSE);
  }
  if (!filter_create_object(subfilt, &ofiltered, NULL, &operandstack)) {
    npop(3, &operandstack);
    return (FALSE);
  }

  return (insert_hash(halftone_dict, &nameobj, &ofiltered));
}

/* Define some structures for holding the data for processing the
 * threshold array data. The maximum threshold array data is 65536
 * bytes - just 1 over the maximum string size. So go for a filter
 * based implementation, like the image reading. The image reading is
 * a bit too specialized to be easily reused in this context.
 */
static
Bool pclxl_init_threshold_dict(OBJECT *halftone_dict,
                               PCLXL_THRESHOLD_READ_CONTEXT *threshold_reader)
{
  corecontext_t *context = get_core_context_interp();
  Bool currentglobal ;
  Bool ok ;

  /** \todo
   * Might need to clear any errors set in the failure of the functions
   * operators invoked below.
   */

 /* Create objects in local memory, each PCLXL page will have save
    restore around it. */

  currentglobal = setglallocmode(context, FALSE) ;
  ok = build_threshold_dict(halftone_dict, threshold_reader);
  setglallocmode(context, currentglobal) ;
  return ok ;
}

/*
 * Tag 0x6d
 */

/* DeviceMatrix is documented to be a byte datatype but QL FTS T329.bin has it
 * as a UInt16
 */
Bool
pclxl_op_set_halftone_method(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[9] = {
#define SETHTMETHOD_DITHER_ORIGIN   (0)
    {PCLXL_AT_DitherOrigin},
#define SETHTMETHOD_DEVICE_MATRIX   (1)
    {PCLXL_AT_DeviceMatrix},
#define SETHTMETHOD_TEXT_OBJECTS    (2)
    {PCLXL_AT_TextObjects},
#define SETHTMETHOD_VECTOR_OBJECTS  (3)
    {PCLXL_AT_VectorObjects},
#define SETHTMETHOD_RASTER_OBJECTS  (4)
    {PCLXL_AT_RasterObjects},
#define SETHTMETHOD_DITHER_MATRIX_DATA_TYPE (5)
    {PCLXL_AT_DitherMatrixDataType},
#define SETHTMETHOD_DITHER_MATRIX_SIZE (6)
    {PCLXL_AT_DitherMatrixSize},
#define SETHTMETHOD_DITHER_MATRIX_DEPTH (7)
    {PCLXL_AT_DitherMatrixDepth},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION allowed_object_dither_values[] = {
    PCLXL_eHighLPI,
    PCLXL_eMediumLPI,
    PCLXL_eLowLPI,
    PCLXL_ENUMERATION_END
  };
  static PCLXL_ENUMERATION allowed_dither_matrix_data_types[] = {
    PCLXL_eUByte,
    PCLXL_ENUMERATION_END
  };
  static PCLXL_ENUMERATION allowed_device_matrix_values[] = {
    PCLXL_eDeviceBest,
    PCLXL_ENUMERATION_END
  };
  static PCLXL_ENUMERATION allowed_dither_depths[] = {
    PCLXL_e8Bit,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  ps_context_t *pscontext;
  PCLXL_THRESHOLD_READ_CONTEXT* threshold_reader;
  OBJECT halftone_dict = OBJECT_NOTVM_NOTHING;
  PCLXL_SysVal_XY dither_origin;
  PCLXL_UInt32_XY dither_matrix_size;
  Bool stream_30;
  Bool do_change;
  Bool changed;
  Bool status;
  uint32 required_data;
  uint32 embedded_data_length;
  PCLXL_ENUMERATION dither_matrix_data_type;
  PCLXL_ENUMERATION dither_matrix_depth;
  PCLXL_ENUMERATION raster_dither_matrix = 0;
  PCLXL_ENUMERATION text_dither_matrix = 0;
  PCLXL_ENUMERATION vector_dither_matrix = 0;
  PCLXL_ENUMERATION device_matrix;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match_at_least_1(parser_context->attr_set, match, pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* DitherOrigin */
  dither_origin.x = dither_origin.y = 0;
  if ( match[SETHTMETHOD_DITHER_ORIGIN].result ) {
    pclxl_attr_get_real_xy(match[SETHTMETHOD_DITHER_ORIGIN].result, &dither_origin);
  }

  if ( match[SETHTMETHOD_DEVICE_MATRIX].result || match[SETHTMETHOD_TEXT_OBJECTS].result ||
       match[SETHTMETHOD_VECTOR_OBJECTS].result || match[SETHTMETHOD_RASTER_OBJECTS].result ) {
    /* Got at least one attribute for setting internal dither matrices */

    stream_30 = pclxl_stream_min_protocol(pclxl_parser_current_stream(parser_context),
                                          PCLXL_PROTOCOL_VERSION_3_0);

    if ( !stream_30 && (match[SETHTMETHOD_TEXT_OBJECTS].result ||
                        match[SETHTMETHOD_VECTOR_OBJECTS].result ||
                        match[SETHTMETHOD_RASTER_OBJECTS].result) ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_IMAGE, PCLXL_ILLEGAL_ATTRIBUTE,
                          ("Got object dither attributes in pre 30 XL stream"));
      return(FALSE);
    }

    if ( match[SETHTMETHOD_DITHER_MATRIX_DATA_TYPE].result ||
         match[SETHTMETHOD_DITHER_MATRIX_SIZE].result ||
         match[SETHTMETHOD_DITHER_MATRIX_DEPTH].result ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_IMAGE, PCLXL_ILLEGAL_ATTRIBUTE_COMBINATION,
                          ("Got job dither as well as internal dither setting"));
      return(FALSE);
    }

    if ( match[SETHTMETHOD_DEVICE_MATRIX].result ) {
      /* Device best dithering */
      if ( match[SETHTMETHOD_TEXT_OBJECTS].result || match[SETHTMETHOD_VECTOR_OBJECTS].result ||
           match[SETHTMETHOD_RASTER_OBJECTS].result ) {
        PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_IMAGE, PCLXL_ILLEGAL_ATTRIBUTE_COMBINATION,
                            ("Got object and device dither attributes"));
        return(FALSE);
      }

      /* DeviceMatrix */
      if ( !pclxl_attr_match_enumeration(match[SETHTMETHOD_DEVICE_MATRIX].result,
                                         allowed_device_matrix_values,
                                         &device_matrix, pclxl_context, PCLXL_SS_KERNEL) ) {
        return(FALSE);
      }

      PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS, ("SetHalftoneMethod DeviceBest"));

      /* Set dither phase */
      if ( !pclxl_set_device_dither_phase(pclxl_context, graphics_state,
                                          dither_origin.x, dither_origin.y) ) {
        return(FALSE);
      }
      return(pclxl_set_device_matrix_halftone(graphics_state, device_matrix));
    }

    /* Object based dither settings */

    /* TextObjects */
    if ( match[SETHTMETHOD_TEXT_OBJECTS].result ) {
      if ( !pclxl_attr_match_enumeration(match[SETHTMETHOD_TEXT_OBJECTS].result,
                                         allowed_object_dither_values,
                                         &text_dither_matrix, pclxl_context, PCLXL_SS_KERNEL) ) {
        return(FALSE);
      }
    }
    /* VectorObjects */
    if ( match[SETHTMETHOD_VECTOR_OBJECTS].result ) {
      if ( !pclxl_attr_match_enumeration(match[SETHTMETHOD_VECTOR_OBJECTS].result,
                                         allowed_object_dither_values,
                                         &vector_dither_matrix, pclxl_context, PCLXL_SS_KERNEL) ) {
        return(FALSE);
      }
    }
    /* RasterObjects */
    if ( match[SETHTMETHOD_RASTER_OBJECTS].result ) {
      if ( !pclxl_attr_match_enumeration(match[SETHTMETHOD_RASTER_OBJECTS].result,
                                         allowed_object_dither_values,
                                         &raster_dither_matrix, pclxl_context, PCLXL_SS_KERNEL) ) {
        return(FALSE);
      }
    }

    /* Set dither phase */
    if ( !pclxl_set_device_dither_phase(pclxl_context, graphics_state,
                                        dither_origin.x, dither_origin.y) ) {
      return(FALSE);
    }

    do_change = FALSE;
    status = TRUE;

    if ( match[SETHTMETHOD_TEXT_OBJECTS].result ) {
      changed = FALSE;

      PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
                  ("SetHalftoneMethod TextObject %d (enum)", text_dither_matrix));
      status = pclxl_set_object_halftone(graphics_state, PCLXL_AT_TextObjects,
                                         text_dither_matrix, &changed);
      do_change |= changed;
    }

    if ( status && match[SETHTMETHOD_VECTOR_OBJECTS].result ) {
      changed = FALSE;

      PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
                  ("SetHalftoneMethos VectorObject %d (enum)", vector_dither_matrix));
      status = pclxl_set_object_halftone(graphics_state, PCLXL_AT_VectorObjects,
                                         vector_dither_matrix, &changed);
      do_change |= changed;
    }

    if ( status && match[SETHTMETHOD_RASTER_OBJECTS].result ) {
      changed = FALSE;

      PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
                  ("SetHalftoneMethod RasterObject %d (enum)", raster_dither_matrix));
      status = pclxl_set_object_halftone(graphics_state, PCLXL_AT_RasterObjects,
                                         raster_dither_matrix, &changed);
      do_change |= changed;
    }

    return(!do_change || pclxl_ps_object_halftone(graphics_state));
  }

  /* Job supplied dither matrix */
  if ( !match[SETHTMETHOD_DITHER_MATRIX_DATA_TYPE].result ||
       !match[SETHTMETHOD_DITHER_MATRIX_SIZE].result ||
       !match[SETHTMETHOD_DITHER_MATRIX_DEPTH].result ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_IMAGE, PCLXL_MISSING_ATTRIBUTE,
                        ("Did not get all job dither matrix attributes"));
    return(FALSE);
  }

  /* DitherMatrixDataType */
  if ( !pclxl_attr_match_enumeration(match[SETHTMETHOD_DITHER_MATRIX_DATA_TYPE].result,
                                     allowed_dither_matrix_data_types,
                                     &dither_matrix_data_type, pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* DitherMatrixSize */
  pclxl_attr_get_uint_xy(match[SETHTMETHOD_DITHER_MATRIX_SIZE].result, &dither_matrix_size);
#define MAX_DITHER_SIZE (256)
  if ( (dither_matrix_size.x < 1) || (dither_matrix_size.x > MAX_DITHER_SIZE) ||
       (dither_matrix_size.y < 1) || (dither_matrix_size.y > MAX_DITHER_SIZE) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                        ("DitherMatrixSize Illegal Attribute Value"));
    return(FALSE);
  }

  /* DitherMatrixDepth */
  if ( !pclxl_attr_match_enumeration(match[SETHTMETHOD_DITHER_MATRIX_DEPTH].result,
                                     allowed_dither_depths, &dither_matrix_depth,
                                     pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* Process the threshold */
  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
              ("SetHalftoneMethod threshold array, Depth = %d (enum) Width = %d, Height = %d",
               dither_matrix_depth, dither_matrix_size.x, dither_matrix_size.y ));

  /* Calculate the expected dimensions of the the image data. Data
   * is padded out to 4 byte boundaries on each line.
   */
  if ( pclxl_stream_read_data_length(pclxl_context,
                                     pclxl_parser_current_stream(parser_context),
                                     &embedded_data_length) < 0 ) {
    return FALSE;
  }

  required_data = ((dither_matrix_size.x + 3) & ~3) * dither_matrix_size.y;
  if ( required_data != embedded_data_length ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL,
                        required_data < embedded_data_length
                          ? PCLXL_DATA_SOURCE_EXCESS_DATA
                          : PCLXL_MISSING_DATA,
                        ("DitherMatrix size (%d,%d) does not match embedded data length (%d)",
                         dither_matrix_size.x, dither_matrix_size.y, embedded_data_length)) ;
    return FALSE;
  }

  if ( !pclxl_threshold_read_context_create(parser_context,
                                            &(parser_context->threshold_reader),
                                            dither_matrix_size.x,
                                            dither_matrix_size.y)) {
    return FALSE;
  }

  threshold_reader = parser_context->threshold_reader;
  threshold_reader->embedded_data_len = embedded_data_length;

  /* Set dither phase */
  if ( !pclxl_set_device_dither_phase(pclxl_context, graphics_state,
                                      dither_origin.x, dither_origin.y) ) {
    return(FALSE);
  }

  if ( !pclxl_init_threshold_dict(&halftone_dict, threshold_reader) ) {
    return FALSE;
  }

  /* Push the threshold data into the rip halftone mechanism. Create PostScript
   * to do this. Use sub-file filter on the embedded data stream.
   */
  HQASSERT(pclxl_context->corecontext != NULL, "No core context") ;
  pscontext = pclxl_context->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;
  status = push(&halftone_dict, &operandstack) && sethalftone_(pscontext) ;

  /* Destroying the context also disposes of the un-read threshold data. */
  status = status && pclxl_threshold_read_context_destroy(parser_context,
                                                          &parser_context->threshold_reader);

  if ( !status ) {
    /* Don't pass on any PS error. */
    error_clear_context(pclxl_context->corecontext->error);
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_INTERNAL_ERROR,
                        ("Internal error reading threshold data."));
  }

  return status;
}

/******************************************************************************
* Log stripped */
