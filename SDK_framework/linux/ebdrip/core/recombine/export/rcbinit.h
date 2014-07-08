/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!export:rcbinit.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Recombine compound initialisation.
 */

#ifndef __RCBINIT_H__
#define __RCBINIT_H__

/** \defgroup recombine Recombination of separated pages.
    \ingroup dl
    \{ */

struct core_init_fns ; /* from SWcore */

/** \brief Initialise the C runtime state for the recombine compound and
    sub-directories. */
void rcbn_C_globals(struct core_init_fns *fns) ;

/** \} */

#endif /* Protection from multiple inclusion */

/* Log stripped */
