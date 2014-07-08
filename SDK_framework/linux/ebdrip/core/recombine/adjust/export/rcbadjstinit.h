/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!adjust:export:rcbadjstinit.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Recombine compound adjustment sub-directory initialisation.
 */

#ifndef __RCBADJSTINIT_H__
#define __RCBADJSTINIT_H__

struct core_init_fns ; /* from SWcore */

/** \brief Initialise the C runtime state for the recombine adjustment
    sub-directory. */
void rcbn_adjust_C_globals(struct core_init_fns *fns) ;

#endif /* Protection from multiple inclusion */

/* Log stripped */
