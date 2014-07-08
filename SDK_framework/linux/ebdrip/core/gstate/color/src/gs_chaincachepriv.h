/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gs_chaincachepriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Color chain cache interface - stores and retrieves color chains
 */

#ifndef __GS_CHAINCACHEPRIV_H__
#define __GS_CHAINCACHEPRIV_H__

#include "gs_color.h" /* COLOR_STATE */
#include "gs_colorprivt.h" /* GS_CHAINinfo */
#include "graphict.h" /* GS_COLORinfo */

typedef struct GS_CHAIN_CACHE_STATE GS_CHAIN_CACHE_STATE;

Bool cc_startChainCache(GS_CHAIN_CACHE_STATE **stateRef);

void cc_stopChainCache(GS_CHAIN_CACHE_STATE **stateRef);

/** Create chain caches for the given color info. */
Bool cc_initChainCache(GS_COLORinfo *colorInfo);

/** Initialize the chain cache module. */
Bool cc_chainCacheSWStart(void);

/** Finish the chain cache module. */
void cc_chainCacheSWFinish(void);

void cc_destroyChainCache(GS_COLORinfo *colorInfo, int32 colorType);

void cc_reserveChainCache(GS_CHAIN_CACHE *chainCache);

Bool cc_addChainCacheEntry(GS_COLORinfo *colorInfo,
                           int32        colorType,
                           Bool         notIfCacheOwner,
                           Bool         potentiallyUnsafe);

Bool cc_findChainCacheEntry(GS_COLORinfo        *colorInfo,
                            int32               colorType,
                            Bool                potentiallyUnsafe,
                            GS_CHAINinfo        **chain,
                            GS_CHAIN_CACHE_INFO *info,
                            Bool                *suitableForCaching);

Bool cc_invalidateChainCache(GS_COLORinfo *colorInfo, int32 colorType);

void cc_chainCacheRestore(int32 saveLevel);

void cc_chainCachePurgeSpaceCache(GS_CHAIN_CACHE_STATE *chainCacheState);

#ifdef METRICS_BUILD
#ifdef ASSERT_BUILD
void cc_countChainsinChainCache(double *chainCnt, double *linkCnt);
#endif
#endif

/* Log stripped */

#endif /* __GS_CHAINCACHEPRIV_H__ */
