/** \file
 * \ingroup crypt
 *
 * $HopeName: COREcrypt!src:cryptinit.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Initialisation for COREcrypt compound.
 */

#include "core.h"
#include "swstart.h"
#include "coreinit.h"
#include "cryptinit.h"
#include "hqxcrypt.h"
#include "morisawa.h"

/** Initialisation table for COREcrypt, recursively calls compound
    init functions. */
static core_init_t crypt_init[] =
{
  CORE_INIT("hqx", hqx_C_globals),
  CORE_INIT("morisawa", morisawa_C_globals),
} ;

static Bool crypt_swinit(struct SWSTART *params)
{
  return core_swinit_run(crypt_init, NUM_ARRAY_ITEMS(crypt_init), params) ;
}

static Bool crypt_swstart(struct SWSTART *params)
{
  return core_swstart_run(crypt_init, NUM_ARRAY_ITEMS(crypt_init), params) ;
}

static Bool crypt_postboot(void)
{
  return core_postboot_run(crypt_init, NUM_ARRAY_ITEMS(crypt_init)) ;
}

static void crypt_finish(void)
{
  core_finish_run(crypt_init, NUM_ARRAY_ITEMS(crypt_init)) ;
}

void crypt_C_globals(core_init_fns *fns)
{
  fns->swinit = crypt_swinit ;
  fns->swstart = crypt_swstart ;
  fns->postboot = crypt_postboot ;
  fns->finish = crypt_finish ;
  core_C_globals_run(crypt_init, NUM_ARRAY_ITEMS(crypt_init)) ;
}

/* Log stripped */
