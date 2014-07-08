/* Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_stream.h(EBDSDK_P.1) $
 * 
 */
/*! \file
 *  \ingroup OIL
 *  \brief This header file describes the interface to the 
 *  OIL stream device.
 *
 *  This stream implementation fulfils the requirements of the 
 *  \c HqnReadStream interface, and supports registering the 
 *  stream with the Skin.

 */

#ifndef _OIL_STREAM_H_
#define _OIL_STREAM_H_

#include "std.h"

extern void Stream_Reset(void);
extern int Stream_Register(void);
extern void Stream_Unregister(void);
extern void Stream_SetBytesConsumed( Hq32x2 * pBytesConsumed );

#endif /* _OIL_STREAM_H_ */
