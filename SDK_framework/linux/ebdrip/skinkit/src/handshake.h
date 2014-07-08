/* Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWskinkit!src:handshake.h(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/**
 * @file
 *
 * @brief Client-side security initialization interface.
 */

#ifndef __HANDSHAKE_H__
#define __HANDSHAKE_H__

#include "lesec.h"

/* ---- Public routines ----- */

/**
 * @brief Provides initialization paramater values to pass to RIP via SwSecInit().
 *
 * @param[in] pParam
 * Structure for client to fill in with initialization parameter values.
 */
extern void SecInit( SwSecInitStruct * pParam );



#endif /* __HANDSHAKE_H__ */
