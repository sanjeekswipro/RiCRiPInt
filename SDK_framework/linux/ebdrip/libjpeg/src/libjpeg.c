/* Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * $HopeName: HQNlibjpeg!src:libjpeg.c(trunk.5) $
 */

#include <stdio.h>
#include <setjmp.h>
#include "std.h"
#include "apis.h"
#include "rdrapi.h"
#include "jpegapi.h"
#include "jpeglib.h"
#include "libjpeg.h"

typedef struct {
  struct jpeg_error_mgr pub;
  jmp_buf setjmp_buffer;
} libjpeg_error_mgr;

typedef struct {
  struct jpeg_decompress_struct cinfo;
  libjpeg_error_mgr jerr;
  struct jpeg_source_mgr src;
  int row_stride, block_size;
  source_callback source_cb;
  void *source_data;
} libjpeg_data_t;

static void error_exit(j_common_ptr cinfo)
{
  /* cinfo->err really points to a libjpeg_error_mgr struct, so coerce pointer */
  libjpeg_error_mgr *error_mgr = (libjpeg_error_mgr*) cinfo->err;

#ifdef DEBUG_BUILD
  char buffer[JMSG_LENGTH_MAX];

  /* Create the message */
  (*cinfo->err->format_message) (cinfo, buffer);

  HQTRACE(TRUE, ("libjpeg error: %s", buffer));
#endif

  /* Return control to the setjmp point */
  longjmp(error_mgr->setjmp_buffer, 1);
}

static void init_source(j_decompress_ptr cinfo)
{
  UNUSED_PARAM(j_decompress_ptr, cinfo);
}

static boolean resync_to_restart(j_decompress_ptr cinfo, int desired)
{
  UNUSED_PARAM(j_decompress_ptr, cinfo);
  UNUSED_PARAM(int, desired);
  return TRUE;
}

static void term_source(j_decompress_ptr cinfo)
{
  UNUSED_PARAM(j_decompress_ptr, cinfo);
}

static boolean fill_input_buffer(j_decompress_ptr cinfo)
{
  libjpeg_data_t *data = (libjpeg_data_t*)cinfo->client_data;
  struct jpeg_source_mgr *src = cinfo->src;

  return (boolean)data->source_cb(data->source_data,
                                  &src->bytes_in_buffer,
                                  &src->next_input_byte);
}

static void skip_input_data(j_decompress_ptr cinfo, long num_bytes)
{
  struct jpeg_source_mgr *src = cinfo->src;

  while ( num_bytes > 0 ) {
    if ( (long)src->bytes_in_buffer >= num_bytes ) {
      src->bytes_in_buffer -= num_bytes;
      src->next_input_byte += num_bytes;
      num_bytes = 0;
    } else {
      num_bytes -= src->bytes_in_buffer;
      (void)fill_input_buffer(cinfo);
    }
  }
}

static int HQNCALL libjpeg_decompress_init(void **p_priv,
                                           source_callback source_cb,
                                           void *source_data,
                                           int *p_bufsize)
{
  /* Make sure jmp_buf is defined as the *very* first local. This
     ensures alignment on the stack after the function. On some 64
     processors it seems that the alignment of the argument to
     setjmp/longjmp *MUST* be 16 byte aligned. See request 61836. */
  jmp_buf aligned_jmpbuf;
  static int lowerquality = TRUE;
  static int downscale = FALSE;
  libjpeg_data_t *data;

  if ( (data = malloc(sizeof(libjpeg_data_t))) == NULL )
    return FALSE;

  data->src.next_input_byte = NULL;
  data->src.bytes_in_buffer = 0;
  data->src.init_source = init_source;
  data->src.fill_input_buffer =  fill_input_buffer;
  data->src.skip_input_data = skip_input_data;
  data->src.resync_to_restart = resync_to_restart;
  data->src.term_source = term_source;
  data->cinfo.err = jpeg_std_error(&data->jerr.pub);
  data->jerr.pub.error_exit = error_exit;
  if (setjmp(aligned_jmpbuf)) {
    jpeg_destroy_decompress(&data->cinfo);
    free(data);
    return FALSE;
  } else {
    memcpy(data->jerr.setjmp_buffer, aligned_jmpbuf, sizeof(jmp_buf));
  }
  data->cinfo.client_data = data;
  data->source_cb = source_cb;
  data->source_data = source_data;
  jpeg_create_decompress(&data->cinfo);
  data->cinfo.src = &data->src;
  jpeg_read_header(&data->cinfo, TRUE);

  if ( lowerquality ) {
    data->cinfo.dither_mode = JDITHER_NONE;
    data->cinfo.dct_method = JDCT_FASTEST;
    data->cinfo.do_block_smoothing = FALSE;
    data->cinfo.do_fancy_upsampling = FALSE;
  }

  if ( downscale ) {
    data->cinfo.scale_num = 6;
    data->cinfo.scale_denom = 8;
  }

  jpeg_start_decompress(&data->cinfo);

  data->row_stride = data->cinfo.output_width * data->cinfo.output_components;
  data->block_size = data->cinfo.block_size;

  *p_priv = (void*)data;
  *p_bufsize = data->row_stride * data->block_size;

  return TRUE;
}

static void HQNCALL libjpeg_decompress_read(void *priv,
                                            uint8 *buffer,
                                            int *p_ret_bytes)
{
  /* Make sure jmp_buf is defined as the *very* first local. This
     ensures alignment on the stack after the function. On some 64
     processors it seems that the alignment of the argument to
     setjmp/longjmp *MUST* be 16 byte aligned. See request 61836. */
  jmp_buf aligned_jmpbuf;
  libjpeg_data_t *data = (libjpeg_data_t*)priv;
  int total_read = 0;

  if (setjmp(aligned_jmpbuf)) {
    *p_ret_bytes = 0;
    return;
  } else {
    memcpy(data->jerr.setjmp_buffer, aligned_jmpbuf, sizeof(jmp_buf));
  }

  while ( total_read < data->block_size  &&
          data->cinfo.output_scanline < data->cinfo.output_height ) {
    uint8 *lineptr = buffer + data->row_stride * total_read;
    JDIMENSION num_read = jpeg_read_scanlines(&data->cinfo, &lineptr,
                                              1 /* Can't use data->block_size here;
                                                   results in a crash in libjpeg */);
    if ( num_read == 0 ) {
      jpeg_finish_decompress(&data->cinfo);
      break;
    }
    total_read += num_read;
  }

  *p_ret_bytes = data->row_stride * total_read;
}

static void HQNCALL libjpeg_decompress_close(void **p_priv)
{
  /* Make sure jmp_buf is defined as the *very* first local. This
     ensures alignment on the stack after the function. On some 64
     processors it seems that the alignment of the argument to
     setjmp/longjmp *MUST* be 16 byte aligned. See request 61836. */
  jmp_buf aligned_jmpbuf;
  libjpeg_data_t *data = (libjpeg_data_t*)*p_priv;

  if (setjmp(aligned_jmpbuf)) {
    free(data);
    *p_priv = NULL;
    return;
  } else {
    memcpy(data->jerr.setjmp_buffer, aligned_jmpbuf, sizeof(jmp_buf));
  }

  jpeg_destroy_decompress(&data->cinfo);
  free(data);
  *p_priv = NULL;
}

const sw_jpeg_api_20140317 libjpeg_api = {
  libjpeg_decompress_init,
  libjpeg_decompress_read,
  libjpeg_decompress_close,
};

int libjpeg_register( void )
{
  if (SwRegisterRDR(RDR_CLASS_API, RDR_API_JPEG, 20140317,
                    (void*)&libjpeg_api, sizeof(libjpeg_api), 0)
      != SW_RDR_SUCCESS)
    return FALSE;

  return TRUE;
}

void libjpeg_deregister( void )
{
  (void)SwDeregisterRDR(RDR_CLASS_API, RDR_API_JPEG, 20140317,
                        (void*)&libjpeg_api, sizeof(libjpeg_api));
}

/* Log stripped */
