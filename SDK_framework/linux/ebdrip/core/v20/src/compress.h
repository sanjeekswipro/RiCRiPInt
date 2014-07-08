/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:compress.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1995-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface with %compress% device
 */

#ifndef __COMPRESS_H__
#define __COMPRESS_H__


#include "devices.h"


/** Performs an 'OpenFile' on the %compress% device after having first set a
    parameter indicating the band size (which it uses for allocating a
    scratch buffer). Returns -1 if error, else return the file id required
    for calls to close_compress_device() and compress_band().
 */
DEVICE_FILEDESCRIPTOR open_compress_device(
  DEVICELIST *cmpdev, DEVICELIST *pgbdev, int32 color_factor,
  int32 pagebuffer_band_size );

/** Closes the channel to the %compress% device.
 */
void close_compress_device( DEVICELIST *cmpdev, DEVICE_FILEDESCRIPTOR file_id );

/** Invokes the %compress% device to compress 'cb' bytes of the (band) buffer
    at address 'pbBand'. The device must first have been initialised and
    opened (hence the 'file_id'). Returns 0 for fatal error, -ve if
    compression failed (but the band buffer has been left with its original
    contents, of (if +ve) the new size of the compressed buffer.
 */
int32 compress_band( DEVICELIST *cmpdev, DEVICE_FILEDESCRIPTOR file_id, uint8* pbBand, int32 cb );

#endif  /* Protect against multiple inclusion */


/* Log stripped */
