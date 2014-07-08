/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!export:xpsconfstate.h(EBDSDK_P.1) $
 */

#ifndef __XPSCONFSTATE_H__
#define __XPSCONFSTATE_H__

#include "std.h"

/**
 * @file
 * @brief Functions relating to usage of XPS PrintTicket device.
 */


/**
 * @brief Allow the XPS PrintTicket device to be disabled.
 *
 * Should be called prior to an XPS job submission.
 *
 * @param  bEnabled  Set to TRUE or FALSE.
 */
void xpspt_setEnabled(HqBool bEnabled);

/**
 * @brief Determine whether the XPS PrintTicket device is enabled.
 *
 * @return TRUE if the device should be enabled.
 */
HqBool xpspt_getEnabled(void);

/**
 * @brief Allow the XPS PrintTicket device to emulate certain PT options
 * by adding information to the output XPS.
 *
 * Should be called prior to an XPS job submission.
 *
 * @param  bEnabled  Set to TRUE or FALSE.
 */
void xpspt_setEmulationEnabled(HqBool bEnabled);

/**
 * @brief Determine whether the XPS PrintTicket device should emulate
 * certain PT options.
 *
 * @return TRUE if emulation is enabled.
 */
HqBool xpspt_getEmulationEnabled(void);


#endif /* __XPSCONFSTATE_H__ */
