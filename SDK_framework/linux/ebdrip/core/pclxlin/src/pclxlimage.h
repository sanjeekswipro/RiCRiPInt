/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlimage.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2010 Global Graphics Software Ltd. All rights
 * reserved.  Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 */

#ifndef __PCLXLIMAGE_H__
#define __PCLXLIMAGE_H__ 1

#include "pclxlimaget.h"
#include "pclxlparsercontext.h"


/**
 * Create a new raster read context in the passed parser_context; this is
 * required before raster blocks can be read.
 */
Bool pclxl_raster_read_context_create(PCLXL_PARSER_CONTEXT parser_context,
                                      PCLXL_IMAGE_READ_CONTEXT **image_reader,
                                      PCLXL_ENUMERATION color_mapping,
                                      PCLXL_ENUMERATION color_depth,
                                      int32 source_width,
                                      int32 source_height,
                                      PCLXL_SysVal_XY destination_size,
                                      Bool  for_an_image);

/**
 * Destructor.
 * \param consume_remaining_data When TRUE, any known remaining image data will
 *        be consumed from the parser stream.
 */
Bool pclxl_raster_read_context_destroy(PCLXL_PARSER_CONTEXT parser_context,
                                       PCLXL_IMAGE_READ_CONTEXT **image_reader,
                                       Bool consume_remaining_data);

/**
 * Perform per-block initialisation.
 */
Bool pclxl_raster_read_context_block_init(PCLXL_PARSER_CONTEXT parser_context,
                                          PCLXL_IMAGE_READ_CONTEXT *image_reader,
                                          int32 start_line,
                                          int32 block_height,
                                          PCLXL_ENUMERATION compress_mode,
                                          int32 pad_bytes);

/**
 * Read a portion of a block of image data; multiple calls may be required to
 * read a complete block. This will make data available in the passed image
 * reader structure thus:
 *
 * image_reader->uncompressed_bytes - the amount of data available.
 * image_reader->start_of_string - the data.
 */
Bool pclxl_raster_decode_block_segment(PCLXL_PARSER_CONTEXT parser_context,
                                       PCLXL_IMAGE_READ_CONTEXT *image_reader);

/**
 * Add the PCL XL image decode filter to the list of available filter.
 */
Bool pclxl_image_decode_filter_init(void);

#endif /* __PCLXLIMAGE_H__ */

/******************************************************************************
* Log stripped */
