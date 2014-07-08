/** \file
 * \ingroup gstate
 *
 * $HopeName: COREgstate!color:src:gscblackevaluate.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2010-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Black preservation: First (Black evaluate) phase.
 */

#ifndef __GSCBLACKEVALUATE_H__
#define __GSCBLACKEVALUATE_H__


#include "gs_colorpriv.h"       /* CLINK */

Bool cc_blackevaluate_create(OBJECT             *colorSpace,
                             COLORSPACE_ID      colorSpaceId,
                             int32              nColorants,
                             Bool               f100pcBlackRelevant,
                             Bool               fBlackTintRelevant,
                             REPRO_COLOR_MODEL  chainColorModel,
                             GUCR_RASTERSTYLE   *hRasterStyle,
                             Bool               compositing,
                             GSC_BLACK_TYPE     chainBlackType,
                             OBJECT             *excludedSeparations,
                             CLINK              **blackLink,
                             int32              *blackPosition);

GSC_BLACK_TYPE cc_blackInput(Bool              f100pcBlackRelevant,
                             Bool              fBlackTintRelevant,
                             REPRO_COLOR_MODEL colorModel,
                             USERVALUE         *pColorValues,
                             int32             nColorValues,
                             int32             blackPosition,
                             int32             rgbPositions[3]);


/* $Log */

#endif /* __GSCBLACKEVALUATE_H__ */
