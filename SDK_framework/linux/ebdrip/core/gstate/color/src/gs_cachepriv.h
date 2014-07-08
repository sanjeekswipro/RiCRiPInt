/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gs_cachepriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Graphics-state private cache data
 */

#ifndef __GS_CACHEPRIV_H__
#define __GS_CACHEPRIV_H__

#include "gs_cache.h" /* because users expect it */
#include "gs_colorprivt.h" /* GS_CHAINinfo */
#include "graphict.h" /* GS_COLORinfo */

typedef struct COC_STATE COC_STATE;

typedef struct COC_HEAD COC_HEAD;

Bool coc_start(COC_STATE **cocStateRef);
void coc_finish(COC_STATE **cocStateRef);
void coc_reset(COC_STATE *cocState, Bool release_dlcolors);
Bool coc_head_create(COC_STATE *cocState, GS_CHAINinfo *chain);
void coc_reserve(COC_HEAD *cache);
void coc_release(COC_HEAD *cache);
Bool coc_lookup(GS_CHAINinfo *colorChain, uint32 *return_hash);
void coc_insert(GS_CHAINinfo *chain, uint32 hashkey);
Bool coc_generationNumber(COC_STATE *cocState, GS_CHAINinfo* chain,
                          uint32 *pGeneratioNumber);

/** Color cache initialization and finishing. */
Bool coc_swstart(struct SWSTART *params);
void coc_swfinish(void);

/* Log stripped */

#endif /* __GS_CACHEPRIV_H__ */
