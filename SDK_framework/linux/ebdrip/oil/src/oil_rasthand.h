/* Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_rasthand.h(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/**
 * @file
 * @ingroup OIL
 *
 * @brief Generic declarations of callback functions which output
 * backends based on this example skin must implement in order to
 * receive raster data from the Harlequin RIP.  
 *
 */

#ifndef __RASTHAND_H__
#define __RASTHAND_H__

#include "skinras.h"
#include "ripcall.h"
#ifdef WIN32
#include <windows.h>
#include <Winspool.h>
#endif
/* Major codes */
#define RASTER_noErr 0
#define RASTER_fileOutputErr -1
#define RASTER_requestErr -2
#define RASTER_fileSeekErr -3
#define RASTER_compressionErr -4

/* ---- Public routines ----- */

/**
 * @brief All the raster callback functions receive an opaque value of
 * this type.  The output backend implementation can use it to ensure
 * any necessary state is available to the callback implementations.
 */
typedef void* RASTER_handle ;

/**
 * @brief A function of this type is called once at the start of each
 * output page, giving preparatory information for handling the page.
 */
typedef int32 (RIPCALL *RASTER_START) (uint8 *filename,
                                       RasterDescription * pRasterDescription,
                                       RASTER_handle *handlep);


/** @brief A function of this type is called multiple times per output
 *  page, with each call containing bands of raster data for the page.
 */
typedef int32 (RIPCALL *RASTER_WRITE_DATA) (RASTER_handle handle,
                                            void *data, int32 topline, int32 nlines,
                                            int32 bytesperline, int32 channel);

/**
 * @brief A function of this type is called once at the end of each
 * output page.  Implementations can use this call to perform tasks at
 * page scope, for example incrementing a page counter.
 */
typedef int32 (RIPCALL *RASTER_FINISH) (RASTER_handle *handlep, int32 result);

/**
 * @brief A function of this type is called once at the end of a job.
 * Implementations can use this call to perform tasks at job scope,
 * such as closing disk files and releasing other platform resources.
 */
typedef int32 (RIPCALL *RASTER_JOB_END) (RASTER_handle *handlep);


#endif /* __RASTHAND_H__ */
