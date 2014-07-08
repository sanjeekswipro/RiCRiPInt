/* Copyright (C) 1999-2011 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWskinkit!src:kit.h(EBDSDK_P.1) $
 *
 * Exports to other skinkit files from skinkit.c
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

#ifndef __KIT_H__
#define __KIT_H__

/** \file
 * \ingroup skinkit
 * \brief Exports to other skinkit files from skinkit.c */

#include "skinkit.h"


/**
 * \brief This method is called from the RIP thread by the config
 * device to determine how much config data, if any is available.
 * This data would then be obtained by the RIP via KGetConfigData().
 *
 * \return \c the number of bytes of config data available.
 */
extern int32 KGetConfigAvailable(void);


/**
 * \brief This method is called from the RIP thread by the config
 * device when it is ready for the config data for next job.  Config
 * data is provided by the app via SwLeJobStart().
 *
 * \param cbMax The maximum amount of data which should be provided
 * \param ppData Where to copy the data to
 * \param pcbData The actual amount of data copied
 * \return \c TRUE if some data was provided, and \c FALSE otherwise.
 */
extern int32 KGetConfigData(int32 cbMax, uint8 ** ppData, uint32 * pcbData);


/**
 * \brief This method is called from the RIP thread by the monitor
 * device when it is ready for data from the current job.  This data
 * is provided by the app via SwLePs().
 *
 * \param cbMax The maximum amount of data which should be provided
 * \param ppData Where to copy the data to
 * \param pcbData The actual amount of data copied
 * \return \c TRUE if some data was provided, and \c FALSE otherwise.
 */
extern int32 KGetJobData(int32 cbMax, uint8 ** ppData, uint32 * pcbData);


/**
 * \brief This function hands a band of raster data generated from the
 * current job page to the raster callback implementation, if any,
 * that the application has registered.
 *
 * \param rasterDescription Description of the raster data.
 *
 * \param pBuffer The raster data itself.
 *
 * \return The result of handing the raster data off to the registered
 * callback function; \c TRUE if both no raster callback was
 * registered and this function was called correctly in sequence; and
 * \c FALSE otherwise.
 *
 */
extern int32 KCallRasterCallback(RasterDescription * rasterDescription, uint8 * pBuffer);


/**
 * \brief A callback function which allows the skin to increase the
 * raster stride (byte offset between the start address of successive
 * raster lines).
 *
 * \param puStride Pointer to an unsigned integer which is set on
 * entry to the raster line length in bytes which the RIP is set to
 * use. The skin may increase this value to better suit its
 * requirements, for example to force line start addresses to coincide
 * with cache lines or DMA ranges. Of course, any increase will
 * necessarily mean a larger memory requirement to hold the raster.
 *
 * This function will almost always return 0, meaning success, but a
 * skin implementation might return -1 signifying an error if the
 * stride value is outside of an acceptable range.
 */
extern int32 KCallRasterStride(uint32 *puStride);

/**
 * \brief A callback function which gives the skin the details of the
 * raster it's about to be handed, and allocate memory to contain it.
 *
 * \param pRasterRequirements Structure providing details of the
 * raster which the RIP will be providing, and some parameters for the
 * skin to indicate to the RIP how it will proceed.
 *
 * \param fRenderingStarting A flag which is FALSE when this function
 * is being called as a result of a change to the page device which
 * means a difference in the raster structure, or TRUE when no further
 * such changes are possible and rendering is beginning very soon.
 */
extern int32 KCallRasterRequirements(RASTER_REQUIREMENTS * pRasterRequirements,
                                     int32 fRenderingStarting);

/**
 * \brief A callback function which asks the skin to provide a memory
 * address range into which to render.
 *
 * \param pRasterDestination Structure providing the band number the
 * RIP is about to render, and pointers to the memory range that the
 * RIP should use.
 *
 * \param nFrameNumber The frame (separation) number.
 */
extern int32 KCallRasterDestination(RASTER_DESTINATION * pRasterDestination,
                                    int32 nFrameNumber);

/**
 * \brief This function hands a monitor information string generated
 * by the RIP to the monitor callback implementation, if any, that the
 * application has registered.
 *
 * \param cbData The length, in bytes, of a NUL-terminated monitor
 * information string.
 *
 * \param pszMonitorData The NUL-terminated monitor information
 * string.
 */
extern void KCallMonitorCallback(int32 cbData, uint8 * pszMonitorData);


/**
 * \brief Called as part of the RIP's exit procedure.
 *
 * This function releases any remaining resources allocated during RIP
 * initialization.
 */
extern void KSwLeStop(void);

#endif
