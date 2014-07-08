/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWskinkit!src:devutils.h(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/**
 * \file
 * \brief Contains functions which can be shared between multiple devices.
 */
#ifndef __DEVUTILS_H__
#define __DEVUTILS_H__

#include "std.h"

/**
 * \brief Provides a mapping from the skin platform error codes to their
 * nearest equivalent RIP device code.
 *
 * \param[in] pkError Skin error code.
 *
 * \return Equivalent error code for the RIP device interface.
 */
int32 KMapPlatformError(int32 pkError);


#endif /* __DEVUTILS_H__ */
