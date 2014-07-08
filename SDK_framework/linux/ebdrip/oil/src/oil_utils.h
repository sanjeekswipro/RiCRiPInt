/* Copyright (C) 2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_utils.h(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup OIL
 *  \brief This header file defines convienient utilities for OIL.
 *
 */


#ifndef _OIL_UTILS_H_
#define _OIL_UTILS_H_

typedef unsigned int ras32;
extern ras32 l_aConvHigh4to32[];
extern ras32 l_aConvLow4to32[];

/* Use 64 bit types for 1 to 8 bpp conversion */
#define USE_64
#ifdef USE_64
typedef long long ras64;
extern ras64 l_aConv8to64[];
#endif




#endif /* _OIL_UTILS_H_ */
