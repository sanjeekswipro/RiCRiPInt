/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!export:gs_cache.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS gstate color-cache API
 */

#ifndef __GS_CACHE_H__
#define __GS_CACHE_H__

struct imlut_t;

#include "gs_color.h"           /* GS_COLORinfo */

/* These bit values in the EnableColorCache userparam control individual caches */
#define GSC_COLOR_CACHE_FIRST_USE               (0x0001)
#define GSC_ENABLE_COLOR_CACHE                  (0x0002)
#define GSC_ENABLE_CHAIN_CACHE                  (0x0004)
#define GSC_ENABLE_3_COMP_TABLE_POINTER_CACHE   (0x0008)
#define GSC_ENABLE_4_COMP_TABLE_POINTER_CACHE   (0x0010)
#define GSC_ENABLE_INTERCEPT_DEVICEN_CACHE      (0x0020)
#define GSC_ENABLE_NAMED_COLOR_CACHE            (0x0040)
#define GSC_ENABLE_CHAIN_CACHE_PSVM             (0x0080)
#define GSC_ENABLE_ALL_COLOR_CACHES             (0xFFFF)

struct imlut_t* coc_imlut_get(GS_COLORinfo *colorInfo, int32 colorType);
Bool coc_imlut_set(GS_COLORinfo *colorInfo, int32 colorType, struct imlut_t *imlut);

#endif /* __GS_CACHE_H__ */

/* Log stripped */
