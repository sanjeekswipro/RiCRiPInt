/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:compress.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1995-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Routines which interface the core-rip with the %compress% device.  Indeed,
 * the renderer is the only client of this interface.
 */

#include "core.h"
#include "swdevice.h"
#include "swstart.h"
#include "objecth.h"

#include "devices.h"
#include "bitblts.h"
#include "display.h"
#include "render.h"
#include "dlstate.h"
#include "params.h"

#include "compress.h"


/* copy_pagebuff_param()
 * Internal function used by setup_compress_device().
 * Copies a single pagebuffer parameter to the %compress% device
 */
static void copy_pagebuff_param( DEVICELIST *s_compress_device,
                                 DEVICELIST *pgbdev,
                                 uint8 *pszName, int32 nType )
{
  DEVICEPARAM param;
  int32       nResult;

  theDevParamName(param)    = pszName;
  theDevParamNameLen(param) = strlen_int32( (char *)pszName );
  theDevParamType(param)    = nType;

  nResult = (*theIGetParam(pgbdev))( pgbdev, &param );

  HQASSERT(nResult == ParamAccepted, "Error getting pagebuffer device parameter");
  HQASSERT(theDevParamType(param) == nType, "Type error in copy_pagebuff_param");

  nResult = (*theISetParam(s_compress_device))( s_compress_device, &param );

  HQASSERT(nResult == ParamAccepted, "Error setting compresion device parameter");
}


/* open_compress_device
 * ====================
 *
 * Open a channel to the compression device.
 */
DEVICE_FILEDESCRIPTOR open_compress_device(
  DEVICELIST *s_compress_device, DEVICELIST *pgbdev, int32 color_factor,
  int32 pagebuffer_band_size )
{
  DEVICEPARAM param;
  int32 nResult;
  float minCompressRatio;

  static uint8 *pszScratchSize = (uint8 *) "ScratchSize";

  if (s_compress_device == NULL) {
    return -1;
  }

  /* get MinBandCompressRatio */
  theDevParamName(param)    = (uint8 *)"MinBandCompressRatio";
  theDevParamNameLen(param) = strlen_int32( (char *)theDevParamName(param) );
  nResult = (*theIGetParam(pgbdev))( pgbdev, &param );
  HQASSERT(nResult == ParamAccepted, "Error getting MinBandCompressRatio");
  HQASSERT(theDevParamType(param) == ParamFloat,
           "Type error in open_compress_device");
  minCompressRatio = theDevParamFloat(param);
  /* Send it to compression device. */
  nResult = (*theISetParam(s_compress_device))( s_compress_device, &param );

  HQASSERT(nResult == ParamAccepted, "Error setting compresion device parameter") ;

 /* Copy relevant parameters from the pagebuffer device to the compression *
  * device. */
  copy_pagebuff_param( s_compress_device, pgbdev,
                       (uint8*) "CompressBands", ParamInteger );
  copy_pagebuff_param( s_compress_device, pgbdev,
                       (uint8*) "BandWidth", ParamInteger );

  theDevParamName(param)    = (uint8 *) "ColorFactor" ;
  theDevParamNameLen(param) = strlen_int32( (char *)theDevParamName(param) );
  theDevParamType(param)    = ParamInteger ;
  theDevParamInteger(param) = color_factor ;

  nResult = (*theISetParam(s_compress_device))( s_compress_device, &param );

  HQASSERT(nResult == ParamAccepted, "Error setting compresion device parameter") ;

  /* Let the %compress% device know how large a scratch buffer it'll
   * need to allocate for this page.
   */
  theDevParamName(param) = pszScratchSize;
  theDevParamNameLen(param) = strlen_int32((char *)pszScratchSize);
  theDevParamType(param) = ParamInteger;
  theDevParamInteger(param) = (int32)(pagebuffer_band_size * minCompressRatio);

  nResult = (*theISetParam(s_compress_device))( s_compress_device, &param );

  HQASSERT(nResult == ParamAccepted, "Error setting device parameter in open_compress_device");

  /* Open the device */
  return (*theIOpenFile(s_compress_device))(s_compress_device,
                                            (uint8*) "CompressBitmap",
                                            SW_WRONLY );
}


/* close_compress_device
 * =====================
 *
 * Close the compress device.
 */
void close_compress_device( DEVICELIST *s_compress_device, DEVICE_FILEDESCRIPTOR file_id )
{
  if (s_compress_device != NULL && file_id >= 0)
    (*theICloseFile(s_compress_device))( s_compress_device, file_id );
}


/* compress_band
 * =============
 *
 * Compress the given band in place. Returns the new size of the band if
 * the compression was successful. If the band couldn't be compressed, the
 * return is < 0. If an error occurred, the return is zero.
 */
int32 compress_band( DEVICELIST *s_compress_device,
                     DEVICE_FILEDESCRIPTOR file_id, uint8* pbBand, int32 cb )
{
  HQASSERT( s_compress_device != NULL, "No compression device in compress_band" );
  HQASSERT( file_id >= 0, "Invalid compress file descriptor in compress_band" );

  if ((*theIWriteFile(s_compress_device))( s_compress_device, file_id, pbBand, cb) < 0)
    return 0;

  return (*theIReadFile(s_compress_device))( s_compress_device, file_id, pbBand, cb );
}


/* Log stripped */
