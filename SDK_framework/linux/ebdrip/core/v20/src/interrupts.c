/** \file
 * \ingroup core
 *
 * $HopeName: SWv20!src:interrupts.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2011-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * The interpreter's interrupt and timeout features.
 */

#include "core.h"

#include "swerrors.h"
#include "taskh.h"
#include "coreinit.h"
#include "interrupts.h"
#include "control.h" /* dosomeaction */


static Bool aninterrupt;
static Bool timedout;

void clear_interrupts(void)
{
  aninterrupt = FALSE;
}

void clear_timeout(void)
{
  timedout = FALSE;
}

Bool interrupts_clear(Bool allow_interrupt)
{
  return !(timedout || (allow_interrupt && aninterrupt) || task_cancelling());
}

Bool user_interrupt(void)
{
  return aninterrupt;
}

void raise_interrupt(void)
{
  aninterrupt = TRUE; dosomeaction = TRUE;
}

void raise_timeout(void)
{
  timedout = TRUE; dosomeaction = TRUE;
}

Bool report_interrupt(Bool allow_interrupt)
{
  int32 error = UNREGISTERED;

  if (aninterrupt && allow_interrupt) {
    error = INTERRUPT;
  } else if (timedout) {
    error = TIMEOUT;
  } else if (task_cancelling()) {
    error = INTERRUPT;
  }
  HQASSERT(error != UNREGISTERED, "no interrupt to report");
  return error_handler(error);
}


void interrupts_C_globals(core_init_fns *fns)
{
  UNUSED_PARAM(core_init_fns*, fns);

  aninterrupt = FALSE;
  timedout = FALSE;
}

/* Log stripped */
