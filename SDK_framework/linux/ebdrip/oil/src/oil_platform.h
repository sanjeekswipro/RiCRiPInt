/* Copyright (C) 2007-2010 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_platform.h(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup OIL
 *  \brief Header file for platform specific code.
 *
 */

#ifndef _OIL_PLATFORM_H_
#define _OIL_PLATFORM_H_

#include "std.h"

#ifdef highbytefirst
#define HIGHBYTEFIRST
#else
#define LOWBYTEFIRST
#endif

void OIL_RelinquishTimeSlice(void);

void OIL_Delay(int nMilliSeconds);

unsigned long OIL_TimeInMilliSecs(void);

int OIL_SwapBytesInWord(unsigned char * pBuffer, int nTotalWords);
/* Returns TRUE on success, otherwise FALSE which should be treated as
   a fatal error. */
HqBool OIL_Init_platform(void);


#endif /* _OIL_PLATFORM_H_ */
