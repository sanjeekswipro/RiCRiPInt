/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!src:rcbinit.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Initialisation for recombine compound.
 */

#include "core.h"
#include "coreinit.h"
#include "swstart.h"
#include "rcbinit.h"
#include "rcbmergeinit.h"
#include "rcbadjstinit.h"

/** Sub-initialisation table for DL compound. */
static core_init_t rcbn_init[] = {
  CORE_INIT("recombine adjust", rcbn_adjust_C_globals),
  CORE_INIT("recombine merge", rcbn_merge_C_globals),
} ;

static Bool rcbn_swinit(SWSTART *params)
{
  return core_swinit_run(rcbn_init, NUM_ARRAY_ITEMS(rcbn_init), params) ;
}

static Bool rcbn_swstart(SWSTART *params)
{
  return core_swstart_run(rcbn_init, NUM_ARRAY_ITEMS(rcbn_init), params) ;
}

static Bool rcbn_postboot(void)
{
  return core_postboot_run(rcbn_init, NUM_ARRAY_ITEMS(rcbn_init)) ;
}

static void rcbn_finish(void)
{
  core_finish_run(rcbn_init, NUM_ARRAY_ITEMS(rcbn_init)) ;
}

void rcbn_C_globals(core_init_fns *fns)
{
  core_C_globals_run(rcbn_init, NUM_ARRAY_ITEMS(rcbn_init)) ;

  fns->swinit = rcbn_swinit ;
  fns->swstart = rcbn_swstart ;
  fns->postboot = rcbn_postboot ;
  fns->finish = rcbn_finish ;
}

/* Log stripped */
