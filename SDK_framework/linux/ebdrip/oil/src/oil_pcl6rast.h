/* Copyright (C) 2006-2011 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_pcl6rast.h(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/**
 * @file
 * @ingroup OIL
 * @brief Raster callback declarations for PCL6 backend.
 * The 4 PCL6_RASTER_xxx functions listed here must match the 
 * prototypical callback types defined in rasthand.h.
 */

#ifndef __PCL6RAST_H__
#define __PCL6RAST_H__

#include "std.h"
#include "swdevice.h"   /* DEVICELIST */
#include "oil_rasthand.h"

/* ---- Public variables ---- */

extern uint8* fileformatPCL6;

typedef struct _PCL6_RASTER_handle {
#ifdef WIN32
  HANDLE     fd_out;            /**< Printer handle */     
  uint8      *pPrivate;         /**< private data for PCL image */
#else
  FILE 		 *fd_out;
  uint8      filename[128];      /**< output filename */                 
  uint8      pPrivate[256];      /**< private data for PCL image */
#endif
  uint32     fPcl6BlockModeCompression ; /**< Tells the RIP not to send PCL6 
                                       device single lines */ 
} PCL6_RASTER_handle;
/* ---- Public routines ----- */

/** @brief PCL6_RASTER_start */
extern int32 PCL6_RASTER_start(uint8 *ps_filename,
                              RasterDescription * pRD,
                              RASTER_handle *handlep);

/** @brief PCL6_RASTER_write_data */
extern int32 PCL6_RASTER_write_data(RASTER_handle rHandle,
                                   void *data, int32 topline, int32 nLinesThisTime,
                                   int32 nLineWidth, int32 channel);

/** @brief PCL6_RASTER_finish */
extern int32 PCL6_RASTER_finish(RASTER_handle *handlep, int32 result);

/** @brief PCL6_RASTER_job_end */
extern int32 PCL6_RASTER_job_end(RASTER_handle *handlep);


#endif /* __PCL6RAST_H__ */
