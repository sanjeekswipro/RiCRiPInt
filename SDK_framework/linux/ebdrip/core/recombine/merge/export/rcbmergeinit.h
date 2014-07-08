/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!merge:export:rcbmergeinit.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Recombine compound initialisation.
 */

#ifndef __RCBMERGEINIT_H__
#define __RCBMERGEINIT_H__

struct core_init_fns ; /* from SWcore */

/** \brief Initialise the C runtime state for the recombine merge
    sub-directory. */
void rcbn_merge_C_globals(struct core_init_fns *fns) ;

#endif /* Protection from multiple inclusion */

/* Log stripped */
