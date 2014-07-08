/** \file
 * \ingroup gstate
 *
 * $HopeName: COREgstate!export:gsinit.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Gstate compound initialisation.
 */

#ifndef __GSINIT_H__
#define __GSINIT_H__

struct core_init_fns ;

/** \brief Initialise the C runtime state for the gstate compound and
    sub-directories. */
void gstate_C_globals(struct core_init_fns *fns) ;

#endif /* Protection from multiple inclusion */

/* Log stripped */
