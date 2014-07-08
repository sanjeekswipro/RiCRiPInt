/** \file
 * \ingroup ps
 *
 * $HopeName: COREps!src:psinit.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Initialisation for PostScript interpreter.
 */

#include "core.h"
#include "coreinit.h"
#include "psinit.h"
#include "psmarks.h"
#include "swstart.h"
#include "v20start.h"

/** \todo ajcd 2009-11-25: This is ugly, but will have to wait until COREps
    is separated from SWv20. */
extern void ps_context_C_globals(core_init_fns *fns);
extern void std_files_C_globals(core_init_fns *fns);
extern void ps_boot_C_globals(core_init_fns *fns);
extern void ps_vm_C_globals(core_init_fns *fns) ;
extern void ps_stacks_C_globals(core_init_fns *fns) ;
extern void ps_dicts_C_globals(core_init_fns *fns) ;
extern void ps_scanner_C_globals(core_init_fns *fns) ;
extern void ps_fontops_C_globals(core_init_fns *fns) ;
extern void ps_fontprivate_C_globals(core_init_fns *fns) ;
extern void ps_idlefonts_C_globals(core_init_fns *fns) ;
extern void idiom_C_globals(core_init_fns *fns) ;

/** The sub initialisation table for PostScript. Using an initialisation
    table assumes that the PostScript interpreter is a singleton. In the
    (very) long run, it would be nice to move all of the interpreter state
    into the PostScript context, in which case the init table should be
    localised on the C stack of the interpreter thread, or dynamically
    allocated in the pscontext. In the meantime, the modularity advantage of
    the automatic cleanup mechanism outweighs any long-term disadvantage. */
static core_init_t postscript_init[] = {
  /* Must be first. Sets up PostScript per-thread context structure: */
  CORE_INIT("pscontext", ps_context_C_globals),
  CORE_INIT("standard files", std_files_C_globals),
  CORE_INIT("%boot% device", ps_boot_C_globals),
  CORE_INIT("psvm", ps_vm_C_globals),
  CORE_INIT("stacks", ps_stacks_C_globals), /* Must be before dicts */
  CORE_INIT("dicts", ps_dicts_C_globals),
  CORE_INIT("scanner", ps_scanner_C_globals),
  CORE_INIT("marking", ps_marking_C_globals),
  CORE_INIT("fontops", ps_fontops_C_globals),
  CORE_INIT("fontpriv", ps_fontprivate_C_globals),
  CORE_INIT("idlefonts", ps_idlefonts_C_globals),
  CORE_INIT("idiom", idiom_C_globals),
} ;

static Bool postscript_swinit(SWSTART *params)
{
  return core_swinit_run(postscript_init, NUM_ARRAY_ITEMS(postscript_init),
                         params) ;
}

static Bool postscript_swstart(SWSTART *params)
{
  return core_swstart_run(postscript_init, NUM_ARRAY_ITEMS(postscript_init),
                          params) ;
}

static Bool postscript_postboot(void)
{
  return core_postboot_run(postscript_init, NUM_ARRAY_ITEMS(postscript_init)) ;
}

static void postscript_finish(void)
{
  core_finish_run(postscript_init, NUM_ARRAY_ITEMS(postscript_init)) ;
}

/** \todo ajcd 2009-11-23: Approximate list of what will go in COREps. Split
    these out into the right places. */
IMPORT_INIT_C_GLOBALS( binfile )
IMPORT_INIT_C_GLOBALS( binscan )
IMPORT_INIT_C_GLOBALS( clipops )
IMPORT_INIT_C_GLOBALS( control )
IMPORT_INIT_C_GLOBALS( devices )
IMPORT_INIT_C_GLOBALS( devops )
IMPORT_INIT_C_GLOBALS( fileops )
IMPORT_INIT_C_GLOBALS( genhook )
IMPORT_INIT_C_GLOBALS( miscops )
IMPORT_INIT_C_GLOBALS( randops )
IMPORT_INIT_C_GLOBALS( shading )
IMPORT_INIT_C_GLOBALS( showops )
IMPORT_INIT_C_GLOBALS( std_file )

void postscript_C_globals(core_init_fns *fns)
{
  init_C_globals_binfile() ;
  init_C_globals_binscan() ;
  init_C_globals_clipops() ;
  init_C_globals_control() ;
  init_C_globals_devices() ;
  init_C_globals_devops() ;
  init_C_globals_fileops() ;
  init_C_globals_genhook() ;
  init_C_globals_miscops() ;
  init_C_globals_randops() ;
  init_C_globals_shading() ;
  init_C_globals_showops() ;
  init_C_globals_std_file() ;

  fns->swinit = postscript_swinit ;
  fns->swstart = postscript_swstart ;
  fns->postboot = postscript_postboot ;
  fns->finish = postscript_finish ;

  core_C_globals_run(postscript_init, NUM_ARRAY_ITEMS(postscript_init)) ;
}

/* Log stripped */
