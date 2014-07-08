/** \file
 * \ingroup images
 *
 * $HopeName: SWv20!src:imblist.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * API to image blist abstraction
 */

#ifndef __IMBLIST_H__
#define __IMBLIST_H__

#include "imstore.h"
#include "imstore_priv.h"
#include "dlstate.h"
#include "bandtable.h" /* bandnum_t */


Bool im_blist_start(IM_SHARED *im_shared);
void im_blist_finish(IM_SHARED *im_shared);

IM_BLIST *blist_create(IM_SHARED *im_shared, int32 bx, Bool global,
                       IM_BLOCK *block, int32 abytes, mm_cost_t cost);
void blist_destroy(IM_SHARED *im_shared, IM_BLIST *blist, Bool followingBlock);
void blist_linkGlobal(IM_SHARED *im_shared, IM_BLIST *blist, Bool linkToHead);
void blist_unlinkGlobal(IM_SHARED *im_shared, IM_BLIST *blist);
void blist_link(IM_BLIST *blist, IM_PLANE *plane);
void blist_unlink(IM_BLIST *blist, IM_PLANE *plane);
void blist_checkGlobal(IM_SHARED *im_shared);
void blist_checkLocal(IM_BLIST* blist, int32 nblists);
void blist_purgeGlobal(IM_SHARED *im_shared);
IM_BLIST *blist_findGlobal(IM_SHARED *im_shared, int32 abytes, int32 bx);
IM_BLIST *blist_find(IM_STORE *ims, int32 abytes, int32 planei, int32 bx,
                     Bool lookInGlobal, Bool rendering);
void blist_release(IM_STORE *ims, int32 band, int32 nullblock);
IM_BLIST *blist_freeall(IM_SHARED *im_shared, IM_BLIST *blist);
void blist_unchain(IM_SHARED *im_shared, IM_BLIST *blist, IM_BLIST **chain);
void blist_add2chain(IM_BLIST *blist, IM_BLIST **chain);
int32 blist_purge(IM_SHARED *im_shared, IM_PLANE *plane, int32 minblists);
void blist_setdata(IM_BLIST *blist, uint8 *data, int16 abytes);
uint8 *blist_getdata(IM_BLIST *blist, int16 *abytes);
size_t blist_sizeof();
void blist_setblock(IM_BLIST *blist, IM_BLOCK *block);
IM_BLOCK *blist_getblock(IM_BLIST *blist);
void blist_xyswap(IM_STORE *ims, IM_PLANE *plane);
void blist_reassign(IM_STORE *ims, IM_PLANE *plane);
void blist_purge2disk(IM_STORE *ims, IM_PLANE *plane);
void blist_pre_render_assertions(IM_BLIST *blist);
void blist_ccheck(IM_BLIST *blist, int32 *ccheck, int32 maxbx);
Bool blist_global(IM_BLIST *blist);
Bool blist_wasGlobal(IM_BLIST *blist);
void blist_setbx(IM_BLIST *blist, int32 bx);
int32 blist_getbx(IM_BLIST *blist);

Bool blist_toomany(IM_SHARED *im_shared, size_t margin);
size_t blist_required(IM_SHARED *im_shared);
void blist_add_extent(IM_STORE *ims, bandnum_t band1, bandnum_t band2);

void blist_globalreport(IM_SHARED *im_shared);

#endif /* __IMBLIST_H__ protection for multiple inclusion */

/* Log stripped */
