/** \file
 *
 * Copyright (C) 2011-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * $HopeName: HQNtimer!src:timerimp.h(EBDSDK_P.1) $
 *
 * \brief Defines timed callback API.
 *
 * Defines prototypes for the platform's implementation of the timed callback
 * system.
 */
#ifndef __TIMERIMP_H__
#define __TIMERIMP_H__  (1)

/** \addtogroup hqn_timer
 * \{
 */

/** Initialise the platform's internal timer system.
 * alloc, memory allocation function with malloc() signature
 * free, memory release function with free() signature
 *
 * Returns TRUE if the timer system initialised ok, else FALSE.
 *
 * This function must be called before any timers can be created.
 */
int hqn_timer_system_init(void *(HQNCALL *alloc)(size_t size),
                          void (HQNCALL *free)(void* mem));

/** Finalise the platform's internal timer system.
 *
 * This terminates all current callback timers and release any system resource
 * used by the platform's timer system.
 */
void hqn_timer_system_finish(void);

/** \brief Create a timer to call the callback on a timed basis.
 *
 * If the time before the first call is zero then the callback will be called
 * immediately.  If the time between subsequent calls is zero then the
 * callback will be called just once.
 *
 * \param[in] first
 * Time before first call, in msecs.
 * \param[in] repeat
 * Time between subsequent calls, in msecs.
 * \param[in] callback
 * Function to be called, passed data passed to this function.
 * \param[in] data
 * Pointer passed to callback.
 *
 * \return Timed callback handle, or \c NULL.
 */
hqn_timer_t *HQNCALL hqn_timer_create(
  unsigned int  first,
  unsigned int  repeat,
  hqn_timer_callback_fn *callback,
  void*         data);

/** \brief Set new delay and period for calling a timed callback.
 *
 * This has not yet been implemented.
 *
 * \param[in] timer
 * Handle of the timer to reset.
 * \param[in] next
 * Time delay to the next call, in msecs
 * \param[in] repeat
 * Time delay between subsequent calls, in msecs
 */
void HQNCALL hqn_timer_reset(
  hqn_timer_t* timer,
  unsigned int  next,
  unsigned int  repeat);

/** \brief End timer event.
 *
 * After a call to this function a timed callback will not happen, even if it
 * has not yet been called.
 *
 * \param[in] timer
 * Handle of the timer to destroy.
 */
void HQNCALL hqn_timer_destroy(
  hqn_timer_t* timer);

/** \} */

#endif /* __TIMERIMP_H__ */
