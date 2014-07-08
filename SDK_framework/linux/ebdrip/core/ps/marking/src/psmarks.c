/** \file
 * \ingroup ps
 *
 * $HopeName: COREps!marking:src:psmarks.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Initialisation for PostScript marking operators.
 */

#include "core.h"
#include "psmarks.h"
#include "upcache.h"

IMPORT_INIT_C_GLOBALS( shows )

/** Compound runtime initialisation */
void ps_marking_C_globals(core_init_fns *fns)
{
  init_C_globals_shows() ;
  init_C_globals_userpath(fns);
}

/* Log stripped */
