/* Copyright (C) 2011-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: HQNtimer!src:timer.c(EBDSDK_P.1) $
 */

#include "std.h"
#include "rdrapi.h"
#include "apis.h"

#define TIMER_IMPLEMENTOR (1)
#include "timerapi.h"
#include "timerimp.h"
#include "timer.h"

sw_timer_api_20110324  timer_api_20110324 = {
  hqn_timer_create,
  hqn_timer_reset,
  hqn_timer_destroy
};

sw_timer_api_20110324 *timer_api = &timer_api_20110324;

int HQNCALL hqn_timer_init(void *(HQNCALL *alloc)(size_t size),
                           void  (HQNCALL *free)(void* mem))
{
  if (!hqn_timer_system_init(alloc, free)) {
    return(FALSE);
  }

  if (SwRegisterRDR(RDR_CLASS_API, RDR_API_TIMER, 20110324, &timer_api_20110324,
                    sizeof(sw_timer_api_20110324), 0) != SW_RDR_SUCCESS) {
    return(FALSE);
  }

  return(TRUE);
}

void HQNCALL hqn_timer_finish(void)
{
  (void)SwDeregisterRDR(RDR_CLASS_API, RDR_API_TIMER, 20110324,
                        &timer_api_20110324, sizeof(sw_timer_api_20110324));

  hqn_timer_system_finish();
}

