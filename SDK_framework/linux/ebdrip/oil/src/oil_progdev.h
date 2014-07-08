/* Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_progdev.h(EBDSDK_P.1) $
 * 
 */
/*! \file
 *  \ingroup OIL
 *  \brief This header file defines the parameters for the progress device type.
 *
 */

#ifndef _OIL_PROGDEV_H_
#define _OIL_PROGDEV_H_

#include "std.h"
#include "swdevice.h"

extern DEVICETYPE EBD_Progress_Device_Type;
int oil_progress_init(void);
void oil_progress_finish(void);
#endif /* _OIL_PROGDEV_H_ */

