/** \file
 * \ingroup otherdevs
 *
 * $HopeName: SWv20!src:pgbproxy.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definition of pgbproxy device type
 */

#ifndef __PGBPROXY_H__
#define __PGBPROXY_H__

struct DL_STATE ; /* from SWv20/COREdodl */
struct mm_pool_t ; /* from SWmm */

/** Initialise the parameters for a page's PGB proxy, and replace the
    pagebuffer device with PGB proxy. */
Bool pgbproxy_init(struct DL_STATE *page, struct mm_pool_t *pool) ;

/** Finalise the parameters for a page's PGB proxy. */
void pgbproxy_finish(struct DL_STATE *page) ;

/** Discard all saved parameters for this page, a new pagedevice is being
    installed. */
void pgbproxy_reset(struct DL_STATE *page) ;

/** Set the flush state for this page's proxy. */
Bool pgbproxy_setflush(struct DL_STATE *page, Bool flush) ;

#ifdef DEBUG_BUILD
void pgbproxy_debug_init(void) ;
#endif

#endif /* protection from multiple inclusion */

/*
* Log stripped */
