/** \file
 *
 * Copyright (C) 2011-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * $HopeName: HQNtimerapi!timer.h(EBDSDK_P.1) $
 *
 * \brief Defines functions to start and end the timed callback system.
 *
 * The timer callback system must be initialised before creating any timed
 * callback timers.
 *
 * \defgroup hqn_timer Timed callback system.
 * \{
 */

#ifndef __TIMER_H__
#define __TIMER_H__  (1)

#include "hqncall.h"

/** \brief Initialise the timed callback system.
 *
 * \param[in] alloc
 * Memory allocation function pointer.
 * \param[in] free
 * Memory release function pointer.
 *
 * \return \c TRUE if the timed callback system has been initialised
 * successfully, else \c FALSE.
 */
int HQNCALL hqn_timer_init(void *(HQNCALL *alloc)(size_t size),
                           void  (HQNCALL *free)(void *mem));

/** \brief Shutdown the timed callback system.
 *
 * The timed callback system is shutdown but any existing callback timers are
 * not destroyed. It is the client's responsibility to ensure all callback
 * timers are destroyed to prevent a resource leak.
 */
void HQNCALL hqn_timer_finish(void);

/** \} */

#endif /* !__TIMER_H__ */
