/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_control!swvalues.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This header file simply defines some value limits
 */

#ifndef values_SW_header
#define values_SW_header 1

#include "hqtypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The range macros and values in this file refer to the range of integers and
 * reals representable in a PostScript object, not in any underlying system
 * type. These definitions should only be changed if the ranges representable
 * in PostScript change.
 */
#define BIGGEST_INTEGER    (( SYSTEMVALUE ) 2147483648.0 )

#define intrange(val)      (( -BIGGEST_INTEGER - 1.0 < (val)) && ((val) < BIGGEST_INTEGER ))

/**
 * USERVALUEs are for values stored in, and derived from, PostScript objects
 * (i.e. they came from the user). The definitions here for the range of
 * USERVALUE assume the use of the IEEE format for (4 byte) floats.
 */
typedef float    USERVALUE ;
#define BIGGEST_REAL  3.402823466e+38F
#define SMALLEST_REAL 1.175494351e-38F

#define realrange(val)     (( -BIGGEST_REAL < (val)) && ((val) < BIGGEST_REAL ))
#define realprecision(val) (!(( -SMALLEST_REAL < (val)) && ((val) < SMALLEST_REAL )))

/**
 * SYSTEMVALUEs are for internal calculations performed by the RIP. They must
 * be able to represent values at least as large and as precisely as both
 * integers and reals derived from the PostScript world (i.e. int32 and
 * USERVALUE).
 */
#ifdef macplusMaybeButTooBig
typedef extended SYSTEMVALUE ;
#else
typedef double   SYSTEMVALUE ;
#endif

/**
 * This is the longest file name allowed by the Core RIP. If the operating
 * system on which the RIP is running does not support filenames this long, then
 * filename mapping ought to be supported. It is this big because not all
 * devices map onto a real file system (for example SOAR's host device).
 */
#define LONGESTFILENAME   2048

#ifdef __cplusplus
}
#endif


#endif /* of #ifndef values_SW_header */
