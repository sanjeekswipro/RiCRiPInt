/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_thread.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief Multi-threading Handler
 *
 */

#include "pms.h"
#include "pms_platform.h"

#include "pms_thread.h"
#include "pms_engine_simulator.h"

static void StartOILWrapper(void * dummy);

/**
 * \brief Startup OEM Interface Layer on a new thread.
 *
 * Creates a new thread to start StartOIL wrapper routine on this new thread.
 */
void *StartOILThread()
{
  return(PMS_BeginThread(&StartOILWrapper, 0, NULL));
}

/**
 * \brief Startup PMS Output Layer on a new thread.
 *
 * Creates a new thread to start PMSOutput routine on this new thread.
 */
void *StartOutputThread()
{
  return(PMS_BeginThread(&PMSOutput, 0, NULL));
}

/**
 * \brief Thread wrapper to start the OIL.
 *
 * This wrapper routine is provided merely to match the function prototype required for PMS_BeginThread().
 * It just passes on the control to StartOIL()
 */
static void StartOILWrapper(void * dummy)
{
  UNUSED_PARAM(void *, dummy);
  StartOIL();
}


