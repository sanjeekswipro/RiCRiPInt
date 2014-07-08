/** \file
 * \ingroup cid
 *
 * $HopeName: COREfonts!src:cidfont0.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions for CID Font Type 0 font cache hooks
 */

#ifndef __CIDFONT0_H__
#define __CIDFONT0_H__

struct core_init_fns ; /* from SWcore */

void cid0_C_globals(struct core_init_fns *fns) ;


/** Clear the CID type 0 cache (expect for items in use). */
void cid0_cache_clear(void);


/** A hook for the VM system to remove fonts being restored away. */
void cid0_restore(int32 savelevel) ;

#endif /* protection for multiple inclusion */

/*
Log stripped */
