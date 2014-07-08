/** \file
 * \ingroup core
 *
 * $HopeName: SWv20!export:asyncps.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2011-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Asynchronous PS API.
 */

#ifndef __ASYNCPS_H__
#define __ASYNCPS_H__ (1)

struct core_init_fns ; /* SWcore */

extern int32 async_action_level;

/* Claim async PS reserve memory. */
void init_async_memory(struct SYSTEMPARAMS *systemparams);

/* Check if any async PS actions are pending. */
Bool async_ps_pending(void);

/* Run currently pending async PS actions. */
void do_pending_async_ps(void);

void asyncps_C_globals(struct core_init_fns *fns);

#endif /* !__ASYNCPS_H__ */

/* Log stripped */
