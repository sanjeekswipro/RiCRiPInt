/** \file
 * \ingroup images
 *
 * $HopeName: SWv20!src:imblock.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * API to image block storage abstraction
 */

#ifndef __IMBLOCK_H__
#define __IMBLOCK_H__

struct core_init_fns ; /* from SWcore */
struct DL_STATE ;

#include "imstore.h"
#include "imstore_priv.h"

void im_block_C_globals(struct core_init_fns *fns) ;

Bool im_blocksetup(IM_STORE *ims, int32 plane, int32 bx, int32 by);
Bool im_blocktrim(IM_STORE *ims, int32 plane, IM_BLOCK **pblock,
                  Bool trimcolumn);
int16 im_blockDefaultAbytes(IM_STORE *ims);
int32 im_blockprealloc(IM_STORE *ims, int32 planei,
                       int32 bb, int32 bx, int32 by);
int32 im_blockwrite(IM_STORE *ims, int32 planei,
                    int32 bb, int32 bx, int32 by, uint8 *buf);
Bool im_blockaddr(IM_STORE *ims, int32 plane, int32 bb, int32 bx,
                  int32 x, int32 y, uint8 **rbuf, int32 *rpixels);
void im_blockfree(IM_STORE *ims, int32 iplane);
void im_blockForbid();
void im_blockAllow();
int32 im_blockpurge(IM_STORE *ims, IM_BLOCK *block, Bool from_blist);
Bool im_blockclose(IM_STORE *ims, IM_PLANE *plane, Bool *incomplete);
Bool im_blockpurgeplane(IM_STORE *ims, IM_PLANE *plane, int32 ty,
                   int32 *purgedBlocks);
void im_blockclear(IM_BLIST *blist, int32 abytes, Bool xxx);
void im_blockrelease(IM_STORE *ims, IM_BLIST *blist, int32 nullblock,
                     IM_PLANE *plane, int32 band);
void im_blockreport(IM_STORE *ims, IM_PLANE *plane);
void im_blocknull(IM_BLOCK *block);
void im_blockcheckNoPurgeable(IM_STORE *ims, IM_PLANE *plane, int32 ty);
void im_blockcheckrelease(IM_BLOCK *block);
IM_BLOCK **im_blocknalloc(IM_SHARED *im_shared, int32 nblocks);
void im_blockerrfree(IM_STORE *ims, IM_PLANE *plane, int32 planei);
Bool im_blockexists(IM_STORE *ims, int32 planei, int32 bb, int32 x,
                    Bool checkComplete);
Bool im_blockgetdata(IM_STORE *ims, int32 planei, int32 bx, int32 bb,
                     uint8 **rbuf , uint32 *rbytes);
Bool im_blockblistalloc(IM_STORE *ims, int32 planei,
                        int32 bb, int32 bx, int32 by, Bool reuse_blists);
void im_blocklock(IM_BLOCK *block);
void im_blockunlock(IM_BLOCK *block, Bool disposable);
uint8 *im_blockyptr(IM_BLOCK *block, int32 y);
void im_blockblistinit(IM_BLIST *blist, Bool doFlags);
int32 im_blockwidth(IM_BLOCK *block);
int32 im_blockheight(IM_BLOCK *block);
void im_blockReadyForInsertion(IM_BLOCK* block);
Bool im_blockUniform(IM_STORE *ims, IM_BLOCK *block, Bool freeData);
uint16 im_blockUniformColor(IM_BLOCK *block);
uint8 *im_blockdata(IM_BLOCK *block, int32 y);
Bool im_blockcheckeol(IM_BLOCK *block, int32 offset);
uint8 im_blockstorage(IM_BLOCK *block);
IM_BLIST *im_blockblist(IM_BLOCK *block);
void im_blockvalidate(IM_BLOCK *block, Bool checkData);
void im_blockreopen(IM_BLOCK *block);
Bool im_blockisusable(IM_BLOCK *block);
Bool im_blockisIMoveable(IM_BLOCK *block);
void im_blocktrim_ycheck(IM_STORE *ims, int32 bx, int32 by);
int32 im_blocksizeof();
void im_block2blist(IM_BLIST *blist, Bool cpBytes);
Bool im_blockcomplete(IM_BLOCK *block);
void im_block_null(IM_BLOCK *block, int id);
void im_block_blist_check(IM_BLOCK *block, IM_BLIST *blist);
void im_block_acheck(IM_BLOCK *block, IM_BLIST *blist);
void im_block_pre_render_assertions(IM_SHARED *im_shared);
Bool im_blockdone(IM_STORE *ims, int32 planei, int32 bb);
void im_blockreadrelease(corecontext_t *context);

#endif /* __IMBLOCK_H__ protection for multiple inclusion */

/* Log stripped */
