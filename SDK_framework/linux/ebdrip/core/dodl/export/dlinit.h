/** \file
 * \ingroup dl
 *
 * $HopeName: COREdodl!export:dlinit.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Display list compound initialisation.
 */

#ifndef __DLINIT_H__
#define __DLINIT_H__

struct core_init_fns ; /* from SWcore */

/** \brief Initialise the C runtime state for the DL compound and
    sub-directories. */
void dodl_C_globals(struct core_init_fns *fns) ;

#endif /* Protection from multiple inclusion */

/* Log stripped */
