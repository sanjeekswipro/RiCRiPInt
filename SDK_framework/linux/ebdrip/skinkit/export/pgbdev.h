/* Copyright (C) 2008-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!export:pgbdev.h(EBDSDK_P.1) $
 */
#ifndef __PGBDEV_H__
#define __PGBDEV_H__

/**
 * @file
 * @brief Page buffer device functions.
 */

#include "skinkit.h"

/**
 * \brief Records that the user of the application has, from the
 * command line, overridden the PageBuffer type default value by
 * requesting "None", i.e. that output should be discarded.
 */
extern void SwLePgbSetOverrideToNone();

/**
 * \brief Allows the default value of MultipleCopies
 * to be overriden.
 *
 * \param[in] fFlag The desired value of MultipleCopies, TRUE or FALSE.
 */
extern void SwLePgbSetMultipleCopies( int32 fFlag );

/**
 * \brief Sets a callback functions for the named pagebuffer device parameter.
 *
 * If a callback function is registered correctly, it will be called whenever
 * the value of the pagebuffer device parameter is \e changed. There is
 * exactly one callback for each parameter; setting the callback will
 * override the previous callback definition. To remove a callback hook, call
 * this function with a \c NULL function pointer.
 *
 * \param paramname The parameter name to monitor.
 *
 * \param paramtype The type of the parameter name to monitor. The callback
 * functions use a generic pointer to pass the changed parameter value.
 * Passing the expected type in with the hook function lets the skinkit check
 * that the hook function will be passed the type it expects.
 *
 * \param pfnPGBCallback Pointer to a function though which the RIP informs
 * the skin of pagebuffer type changes. The skin may call \c SwLeSetCallbacks
 * in this routine, to route callbacks to different backends.
 *
 * \retval FALSE If the callback could not be set (either the name or type of
 * the PGB param is incorrect).
 *
 * \retval TRUE If the callback was set.
 */
int32 SwLePgbSetCallback(const char *paramname,
                         int32 paramtype,
                         SwLeParamCallback *pfnPGBCallback) ;
/**
 * \brief Turns on testing of the framebuffer mode in the pgb device.
 * We allocate a singleton framebuffer for the whole page.
 * We deliberately allocate memory outside the rip's memory pool to emphasise
 * the testing nature of this function.
 *
 * \param fFlag TRUE to turn the test on. Default is FALSE.
 */
void SwLePgbUseFrameBuffer(int32 fFlag);


#endif
