/** \file
 * \ingroup ps
 *
 * $HopeName: COREps!marking:export:upcache.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * External declarations for the userpath cache
 */

#ifndef __UPCACHE_H__
#define __UPCACHE_H__

#include "coreinit.h"

int32 upaths_cached(void) ;

struct DL_STATE;
struct PATHINFO;
struct CHARCACHE;

Bool fill_using_charcache(struct DL_STATE *page, struct PATHINFO *path,
                          int32 filltype, struct CHARCACHE **ccptr,
                          SYSTEMVALUE cx, SYSTEMVALUE cy);

void purge_ucache(corecontext_t *context);

void init_C_globals_userpath(core_init_fns *fns);

#endif /* protection for multiple inclusion */

/* Log stripped */
