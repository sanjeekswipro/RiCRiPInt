/* Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_media.h(EBDSDK_P.1) $
 * 
 */
/*! \file
 *  \ingroup OIL
 *  \brief This header file contains the interface to the OIL used to retrieve
 *   information on the available media input trays.
 *
 */

#ifndef _OIL_MEDIA_H_
#define _OIL_MEDIA_H_

#include "pms_export.h"

#define BOUNDARY_IN_BYTES                     (g_ConfigurableFeatures.nStrideBoundaryBytes)
#define BITS_PER_BYTE                         (8)
#define POINTS_PER_INCH                       (72.0)

#define BOUNDARY_IN_BITS                      (BOUNDARY_IN_BYTES * BITS_PER_BYTE)
#define BOUNDARY_IN_PIXELS(bpp)               (BOUNDARY_IN_BITS / (bpp))
#define CONVERT_POINTS_TO_PIXELS(width, xres) ((int)(((width) * ((float)(xres))) / POINTS_PER_INCH))

#define PADDED_ALIGNEMENT(width, bpp, xres)                                                                           \
         ((float)(  (  (  (int)(((width) * ((float)(xres))) / POINTS_PER_INCH) /* convert points to pixels */                 \
                        + (BOUNDARY_IN_PIXELS(bpp) - 1))                       /* pad bytes to cross alignment boundary */    \
                     & ~(BOUNDARY_IN_PIXELS(bpp) - 1))                         /* trim padding to exact alignment boundary */ \
                  * POINTS_PER_INCH)                                           /* convert back... */                          \
               / (xres))  

/* Global Functions */
void GetTrayInformation (char *pBuf);
void GetOutputInformation (char *pBuf);
/* Temporary string size */
#define OIL_TMPSTR_SIZE         4096

#endif /* _OIL_MEDIA_H_ */
