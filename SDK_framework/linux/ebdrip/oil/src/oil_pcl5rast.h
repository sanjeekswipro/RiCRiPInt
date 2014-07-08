/* Copyright (C) 2006-2011 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_pcl5rast.h(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/**
 * @file
 * @ingroup OIL
 * @brief Raster callback definitions for example PCL5 backend.
 * The 4 PCL5_RASTER_xxx functions listed here must match the 
 * prototypical callback types defined in rasthand.h.
 */

#ifndef __PCL5_RASTER_H__
#define __PCL5_RASTER_H__

#include "std.h"
#include "swdevice.h"   /* DEVICELIST */
#include "oil_rasthand.h"

/* ---- Public variables ---- */

extern uint8* fileformatPCL5;

typedef struct _PCL5_RASTER_handle {
#ifdef WIN32
  HANDLE     fd_out;        /**< Printer handle */
  uint8      *pPrivate;		/**< private data for PCL image */
#else
  FILE 		 *fd_out;  		/**< File descriptor for output PS device. */
  uint8      filename[128]; /**< output filename */                 
  uint8      pPrivate[256]; /**< private data for PCL image */
#endif

} PCL5_RASTER_handle;
/* ---- Public routines ----- */

/** @brief PCL5_RASTER_start */
extern int32 PCL5_RASTER_start(uint8 *filename,
                              RasterDescription * pRD,
                              RASTER_handle *handlep);

/** @brief PCL5_RASTER_write_data */
extern int32 PCL5_RASTER_write_data(RASTER_handle rHandle,
                                   void *data, int32 topline, int32 nLinesThisTime,
                                   int32 nLineWidth, int32 channel);

/** @brief PCL5_RASTER_finish */
extern int32 PCL5_RASTER_finish(RASTER_handle *handlep, int32 result);

/** @brief PCL5_RASTER_job_end */
extern int32 PCL5_RASTER_job_end(RASTER_handle *handlep);



#endif /* __PCL5_RASTER_H__ */
