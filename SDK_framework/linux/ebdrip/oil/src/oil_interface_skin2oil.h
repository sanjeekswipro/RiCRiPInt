/* Copyright (C) 2007-2013 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_interface_skin2oil.h(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup OIL
 *  \brief This header file declares the callback functions which allow
 * the RIP and Skin to pass data to the OIL.
 *
 */

#include "std.h"
#include "skinras.h"
#include "swraster.h"
#include "ripcall.h"

#ifndef _OIL_INTERFACE_SKIN2OIL_H_
#define _OIL_INTERFACE_SKIN2OIL_H_
#ifndef GG_64395

/**
 * \brief A read-only structure providing the main details of a page.
 */
typedef struct _page_spec {
  uint32 width;                       /**< \brief Page width in
                                         pixels. */
  uint32 height;                      /**< \brief Page height in
                                         pixels. */
  uint32 bit_depth;                   /**< \brief Bit depth. */
  uint32 components;                  /**< \brief Number of color
                                         components. */
} PAGE_SPEC;

/**
 * \brief A pointer to RASTER_LAYOUT will be passed as the arg to
 * DEVICELIST_IOCTL() routine when opcode is DeviceIOCtl_RasterLayout.
 * This structure allows the client to specify the layout of memory
 * provided via the DeviceIOCtl_GetBufferForRaster DEVICELIST_IOCTL()
 * routine.
 */
typedef struct _raster_layout {
  PAGE_SPEC page;                      /**< Details of the full page;
                                         this is provided for
                                         information only and should
                                         be considered read-only. */

  int32 valid;                         /**< /brief Defaulted to false,
                                         this should be set to true
                                         if the callee is going to
                                         provide band memory,
                                         otherwise the other values
                                         in this structure are
                                         ignored. */

  uint32 line_bytes;                   /**< /brief The number of bytes
                                         in a single line of raster in
                                         a single colorant. This is
                                         required in addition to the
                                         strides provided below as the
                                         page buffer needs to store
                                         lines in it's own memory. */

  int32 line_stride;                  /**< /brief The number of bytes
                                         between lines of the same
                                         colorant; e.g. the number of
                                         bytes between consecutive
                                         lines of Red data. */

  int32 colorant_stride;              /**< /brief The number of bytes
                                         between the same line in
                                         consecutive colorants; e.g.
                                         the number of bytes between
                                         the Red and Green data for
                                         the same line. This may be
                                         set to zero, which will
                                         cause the rip to use
                                         line_bytes * band height. */
} RASTER_LAYOUT;
#endif

/* RIP callback functions  */
void OIL_MonitorCallback(int nBufLen, unsigned char * pBuffer);
int32 RIPCALL OIL_RasterCallback(void *pJobContext,
                                 RasterDescription * ptRasterDescription,
                                 unsigned char * pBuffer);
int32 RIPCALL OIL_RasterStride(void *pJobContext, unsigned int *puStride);
#ifdef GG_64395
int32 OIL_RasterLayout(RASTER_LAYOUT * pRasterLayout);
#else
int32 RIPCALL OIL_RasterStride(void *pJobContext, unsigned int *puStride);
#endif
int32 RIPCALL OIL_RasterRequirements(void *pJobContext,
                                     RASTER_REQUIREMENTS *pRasterRequirements,
                                     int32 fRenderingStarting);
int32 RIPCALL OIL_RasterDestination(void *pJobContext,
                                    RASTER_DESTINATION * pRasterDestination,
                                    int32 nFrameNumber);


#endif /* _OIL_INTERFACE_SKIN2OIL_H_ */
