/** \file
 *
 * Copyright (C) 2011-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * $HopeName: HQNtimerapi!timerapi.h(EBDSDK_P.1) $
 *
 * \brief Defines timed callback API versions.
 *
 * The timed callback system must be initialised before the API can be used.
 *
 * \addtogroup hqn_timer
 * \{
 */

#ifndef __TIMERAPI_H__
#define __TIMERAPI_H__  (1)

#include "hqncall.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Handle for a timed callback. */
typedef struct TIMER_HANDLE hqn_timer_t;

/** Callback function for timers. */
typedef void (HQNCALL hqn_timer_callback_fn)(hqn_timer_t *timer, void *data);

/* \brief Timed callback function pointer API version 20110324. */
typedef struct SW_TIMER_20110324 {
  /* See hqn_timer_create(). */
  hqn_timer_t *(HQNCALL *hqn_timer_create)(
    unsigned int  first,
    unsigned int  repeat,
    hqn_timer_callback_fn *callback,
    void         *data);

  /* See hqn_timer_reset() */
  void (HQNCALL *hqn_timer_reset)(
    hqn_timer_t *timer,
    unsigned int         next,
    unsigned int         repeat);

  /* See hqn_timer_destroy(). */
  void (HQNCALL *hqn_timer_destroy)(
    hqn_timer_t* timer);
} sw_timer_api_20110324;

extern sw_timer_api_20110324 *timer_api;

#if !defined(FOOL_INTELLISENSE) && !defined(TIMER_IMPLEMENTOR)

#define hqn_timer_create      timer_api->hqn_timer_create
#define hqn_timer_reset       timer_api->hqn_timer_reset
#define hqn_timer_destroy     timer_api->hqn_timer_destroy

#endif /* !FOOL_INTELLISENSE && !TIMER_IMPLEMENTOR */

#ifdef __cplusplus
}
#endif

/** \} */

#endif /* !__TIMERAPI_H__ */
