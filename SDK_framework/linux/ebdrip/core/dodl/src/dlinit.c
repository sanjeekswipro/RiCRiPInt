/** \file
 * \ingroup dl
 *
 * $HopeName: COREdodl!src:dlinit.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Initialisation for DL compound.
 */

#include "core.h"
#include "coreinit.h"
#include "swstart.h"
#include "dlinit.h"
#include "display.h"
#include "dl_purge.h"
#include "shadex.h"

/** Sub-initialisation table for DL compound. */
static core_init_t dodl_init[] = {
  CORE_INIT("DL pool", dl_pool_C_globals),
  CORE_INIT("DL misc", dl_misc_C_globals),
  CORE_INIT("DL purge", dlpurge_C_globals),
} ;

static Bool dodl_swinit(SWSTART *params)
{
  return core_swinit_run(dodl_init, NUM_ARRAY_ITEMS(dodl_init), params) ;
}

static Bool dodl_swstart(SWSTART *params)
{
  return core_swstart_run(dodl_init, NUM_ARRAY_ITEMS(dodl_init), params) ;
}

static Bool dodl_postboot(void)
{
  return core_postboot_run(dodl_init, NUM_ARRAY_ITEMS(dodl_init)) ;
}

static void dodl_finish(void)
{
  core_finish_run(dodl_init, NUM_ARRAY_ITEMS(dodl_init)) ;
}

/** \todo ajcd 2009-11-23: Approximate list of what will go in COREdodl.
    Split these out into the right places. Recombine will be in a
    sub-directory that can be compiled out. */
IMPORT_INIT_C_GLOBALS( display )
IMPORT_INIT_C_GLOBALS( dl_shade )
IMPORT_INIT_C_GLOBALS( fcache )
IMPORT_INIT_C_GLOBALS( group )
IMPORT_INIT_C_GLOBALS( hdl )
IMPORT_INIT_C_GLOBALS( imtiles )
IMPORT_INIT_C_GLOBALS( pattern )
IMPORT_INIT_C_GLOBALS( patternshape )
IMPORT_INIT_C_GLOBALS( pclAttrib )
IMPORT_INIT_C_GLOBALS( pgbproxy )
IMPORT_INIT_C_GLOBALS( plotops )
IMPORT_INIT_C_GLOBALS( renderom )
IMPORT_INIT_C_GLOBALS( rlecache )
IMPORT_INIT_C_GLOBALS( rollover )
IMPORT_INIT_C_GLOBALS( routedev )

void dodl_C_globals(core_init_fns *fns)
{
  init_C_globals_display() ;
  init_C_globals_dl_shade() ;
  init_C_globals_fcache() ;
  init_C_globals_group() ;
  init_C_globals_hdl() ;
  init_C_globals_imtiles() ;
  init_C_globals_pattern() ;
  init_C_globals_patternshape() ;
  init_C_globals_pclAttrib() ;
  init_C_globals_pgbproxy() ;
  init_C_globals_plotops() ;
  init_C_globals_renderom() ;
  init_C_globals_rlecache() ;
  init_C_globals_rollover() ;
  init_C_globals_routedev() ;

  fns->swinit = dodl_swinit ;
  fns->swstart = dodl_swstart ;
  fns->postboot = dodl_postboot ;
  fns->finish = dodl_finish ;

  core_C_globals_run(dodl_init, NUM_ARRAY_ITEMS(dodl_init)) ;
}

/* Log stripped */
