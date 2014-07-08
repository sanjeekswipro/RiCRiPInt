/** \file
 * \ingroup multi
 *
 * $HopeName: SWmulti!export:ripmulti.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Rip multithreading API
 */

#ifndef __RIPMULTI_H__
#define __RIPMULTI_H__ 1

extern unsigned int multi_nthreads; /* number of threads used (incl. interpreter) */

#define NUM_THREADS() (multi_nthreads)

/** \todo ajcd 2011-01-25: Move these definitions to taskh.h or somewhere more
    appropriate. */

/** Try to constrain the system to running a single thread.
 *
 * Naturally, this is only going to succeed if this is the only thread running.
 * \return Whether it succeeded.
 */
Bool multi_constrain_to_single(void);


/** Lift the constraint of only running a single thread.
 *
 * Must match an earlier call to \c multi_constrain_to_single(), and the
 * constraint is only really lifted when the number of unconstrain calls
 * matches the number of successful constrain calls.
 */
void multi_unconstrain_to_single(void);


struct core_init_fns ; /* from SWcore */

void multi_render_C_globals(struct core_init_fns *fns) ;

#endif

/* Log stripped */
