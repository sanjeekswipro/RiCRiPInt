/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlimaget.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2010 Global Graphics Software Ltd. All rights
 * reserved.  Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Types for PCLXL image handling.
 */

#ifndef __PCLXLIMAGET_H__
#define __PCLXLIMAGET_H__

#include "pclxlgraphicsstatet.h"

/* Image read context. */
typedef struct PCLXL_IMAGE_READ_CONTEXT {
  size_t alloc_size ; /* Allocation size of this struct with data. */
  PCLXL_COLOR_SPACE_DETAILS_STRUCT image_color_details ;
  Bool within_PS_imageread_callback ; /* Are we within the
                                         pclxlimageread callback? */
  int32 num_components ; /* Number of color components. Always 1 for
                            indexed. */
  Bool is_indexed ; /* TRUE if indexed, FALSE if direct by pixel. */
  int32 embedded_data_len ; /* As per spec. Read from the stream. */
  int32 pad_bytes ; /* From ReadImage attributes. */
  int32 throwout_byte_count ; /* Number of bytes per scan line we need to ignore. */
  int32 start_line ; /* From ReadImage attributes. */
  int32 block_height ; /* From ReadImage attributes. */
  int32 compress_mode ; /* From ReadImage attributes. */
  int32 bits_per_component ; /* 1, 4 or 8 */
  int32 source_width ; /* > 0 */
  int32 source_height ; /* > 0 */
  int32 num_rows_seen ; /* Number of raster rows we have seen. */
  int32 rle_control ; /* Cached RLE control byte. */
  int32 rle_out_byte ; /* Cached RLE outbyte for repeated byte requests. */
  SYSTEMVALUE destination_width ; /* > 0 */
  SYSTEMVALUE destination_height ; /* > 0 */
  Bool is_first_decode_segment ; /* TRUE if its the first decode segment. */
  Bool end_image_seen ; /* TRUE if we have seen an end image. */
  OBJECT string_object ; /* String object for DataSource to return. */
  int32 bytes_remaining_in_scan_line ; /* Number of bytes remaining in
                                          the decoded scan line. */
  uint8 *start_of_string ; /* Pointer to the start of the string to
                              pass back to PS. */
  int32 uncompressed_bytes ; /* How many bytes did we uncompress. */
  int32 width_in_bytes ; /* Num bytes in a single scan line. */
  uint8 *zee_bytes ; /* Width in bytes is its size. */
  Bool  decode_ok; /* Image decoding went ok */

  /* When reading a jpeg image, the decoding is performed by a DCT decode filter
   * layered on top of the PCLXL stream. */
  FILELIST* jpeg_source;
} PCLXL_IMAGE_READ_CONTEXT;

/* threshold array read context. */
typedef struct {
  size_t alloc_size;

  int32 embedded_data_len ;   /* As per spec. Read from the stream. */
  int32 pad_bytes ;           /* From ReadImage attributes. */
  int32 throwout_byte_count ; /* Number of bytes per row we need to ignore. */

  int32 threshold_array_width ;  /* [1,256] */
  int32 threshold_array_height ; /* [1,256] */

  OBJECT string_object ;  /* String object for DataSource to return. */
  int32 bytes_remaining ; /* Number of bytes remaining in
                             the decoded scan line. */
  uint8 *start_of_string ;   /* Pointer to the start of the string to
                                pass back to PS. */
  int32 uncompressed_bytes ; /* How many bytes did we uncompress. */
  int32 width_in_bytes ;     /* Num bytes in a single scan line. */
  uint8 *zee_bytes ;         /* Width in bytes is its size. */
} PCLXL_THRESHOLD_READ_CONTEXT;

#endif

/* Log stripped */
