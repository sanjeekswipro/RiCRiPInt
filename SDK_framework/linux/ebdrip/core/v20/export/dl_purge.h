/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:dl_purge.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * API for purging DL elements to disk.
 */

#ifndef __DL_PURGE_H__
#define __DL_PURGE_H__

struct core_init_fns ; /* from SWcore */

void dlpurge_C_globals(struct core_init_fns *fns) ;

void dlpurge_reset(void);
Bool dlpurge_inuse(void);

void *load_dldata(LISTOBJECT *lobj);
void rewrite_dldata(LISTOBJECT *lobj);
Bool dlref_readfromdisk(DLREF *dlref, uint32 index, LISTOBJECT *lobj);
Bool dlref_rewrite(DLREF *dlref, uint32 index, LISTOBJECT *lobj);

#endif /* protection for multiple inclusion */

/*
* Log stripped */
