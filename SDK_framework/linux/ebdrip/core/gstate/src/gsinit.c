/** \file
 * \ingroup gstate
 *
 * $HopeName: COREgstate!src:gsinit.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Initialisation for gstate compound.
 */

#include "core.h"
#include "swstart.h"
#include "coreinit.h"
#include "gsinit.h"
#include "gscinit.h"
#include "gstack.h"
#include "system.h"

/** \todo ajcd 2009-11-25: These initialisation functions are called through
   the COREgstate initialisation table to make cleanup easier and more
   regular. They should probably be moved into the appropriate source files
   when COREgstate files are split from SWv20. */
static Bool path_sac_swstart(SWSTART *params)
{
  UNUSED_PARAM(SWSTART *, params) ;
  return initSystemMemoryCaches(mm_pool_temp) ;
}

static void path_sac_finish(void)
{
  clearSystemMemoryCaches(mm_pool_temp);
}

/** The sub-module initialisation table for COREgstate. */
static core_init_t gstate_init[] = {
  CORE_INIT("gstack", gstack_C_globals),
  CORE_INIT("color", color_C_globals),
  CORE_INIT_LOCAL("paths", NULL, path_sac_swstart, NULL, path_sac_finish),
} ;

static Bool gstate_swinit(SWSTART *params)
{
  return core_swinit_run(gstate_init, NUM_ARRAY_ITEMS(gstate_init), params) ;
}

static Bool gstate_swstart(SWSTART *params)
{
  return core_swstart_run(gstate_init, NUM_ARRAY_ITEMS(gstate_init), params) ;
}

static Bool gstate_postboot(void)
{
  return core_postboot_run(gstate_init, NUM_ARRAY_ITEMS(gstate_init)) ;
}

static void gstate_finish(void)
{
  core_finish_run(gstate_init, NUM_ARRAY_ITEMS(gstate_init)) ;
}

/** \todo ajcd 2009-11-23: Approximate list of what will go in COREgstate.
    Split these out into the right places. */
IMPORT_INIT_C_GLOBALS( blends )
IMPORT_INIT_C_GLOBALS( clippath )
IMPORT_INIT_C_GLOBALS( gstate )
IMPORT_INIT_C_GLOBALS( gu_chan )
IMPORT_INIT_C_GLOBALS( gu_cons )
IMPORT_INIT_C_GLOBALS( gu_ctm )
IMPORT_INIT_C_GLOBALS( gu_path )
IMPORT_INIT_C_GLOBALS( matrix )
IMPORT_INIT_C_GLOBALS( panalyze )
IMPORT_INIT_C_GLOBALS( shadecev )
IMPORT_INIT_C_GLOBALS( stroker )
IMPORT_INIT_C_GLOBALS( system )
IMPORT_INIT_C_GLOBALS( tensor )

void gstate_C_globals(core_init_fns *fns)
{
  init_C_globals_clippath() ;
  init_C_globals_blends() ;
  init_C_globals_gstate() ;
  init_C_globals_gu_chan() ;
  init_C_globals_gu_cons() ;
  init_C_globals_gu_ctm() ;
  init_C_globals_gu_path() ;
  init_C_globals_matrix() ;
  init_C_globals_panalyze() ;
  init_C_globals_shadecev() ;
  init_C_globals_stroker() ;
  init_C_globals_system() ;
  init_C_globals_tensor() ;

  fns->swinit = gstate_swinit ;
  fns->swstart = gstate_swstart ;
  fns->postboot = gstate_postboot ;
  fns->finish = gstate_finish ;

  core_C_globals_run(gstate_init, NUM_ARRAY_ITEMS(gstate_init)) ;
}

/* Log stripped */
