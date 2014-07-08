/* Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * $HopeName: HQNjpeg!export:jpegapi.h(trunk.2) $
 */

#ifndef __JPEGAPI_H__
#define __JPEGAPI_H__

#include "apis.h"
#include "rdrapi.h"

/** Callback to read the underlying source. */
typedef int (HQNCALL *source_callback)(void *source_data,
                                       size_t *bytes_read,
                                       const uint8 **buffer);

/** JPEG api structure.

   Allow alternate JPEG implementations to be registered via RDR, and then used
   instead of the HQN JPEG code.

   Currently used just for decompression.
 */
typedef struct {
  int (HQNCALL *decompress_init)(void **p_priv,
                                 source_callback source_cb,
                                 void *source_data,
                                 int *p_bufsize);
  void (HQNCALL *decompress_read)(void *priv,
                                  uint8 *buffer,
                                  int *p_ret_bytes);
  void (HQNCALL *decompress_close)(void **p_priv);
} sw_jpeg_api_20140317;

#endif /* __JPEGAPI_H__ */

/* Log stripped */
