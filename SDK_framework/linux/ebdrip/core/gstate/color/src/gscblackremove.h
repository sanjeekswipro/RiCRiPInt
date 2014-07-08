/** \file
 * \ingroup gstate
 *
 * $HopeName: COREgstate!color:src:gscblackremove.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2010-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Black preservation: Second (Black remove) phase.
 */

#ifndef __GSCBLACKREMOVE_H__
#define __GSCBLACKREMOVE_H__


#include "gs_colorpriv.h"       /* CLINK */

Bool cc_blackremove_create(GS_CHAIN_CONTEXT   *chainContext,
                           OBJECT             *colorSpace,
                           COLORSPACE_ID      colorSpaceId,
                           int32              nColorants,
                           Bool               f100pcBlackRelevant,
                           Bool               fBlackTintRelevant,
                           Bool               fBlackTintLuminance,
                           USERVALUE          blackTintThreshold,
                           REPRO_COLOR_MODEL  chainColorModel,
                           GUCR_RASTERSTYLE   *hRasterStyle,
                           OBJECT             *excludedSeparations,
                           CLINK              **blackLink);

Bool cc_getBlackPosition(OBJECT            *colorspace,
                         COLORSPACE_ID     iColorSpace,
                         int32             n_iColorants,
                         GUCR_RASTERSTYLE  *hRasterStyle,
                         REPRO_COLOR_MODEL chainColorModel,
                         OBJECT            *excludedSeparations,
                         int32             *blackPosition);
Bool cc_getBlackPositionInList(int32            nColorants,
                               COLORANTINDEX    *iColorants,
                               GUCR_RASTERSTYLE *hRasterStyle,
                               int32            *blackPosition);
Bool cc_getRGBPositions(OBJECT            *colorspace,
                        int32             n_iColorants,
                        GUCR_RASTERSTYLE  *hRasterStyle,
                        REPRO_COLOR_MODEL chainColorModel,
                        OBJECT            *excludedSeparations,
                        int32             rgbPositions[3]);
Bool cc_getRgbPositionsInList(int32            nColorants,
                              COLORANTINDEX    *iColorants,
                              GUCR_RASTERSTYLE *hRasterStyle,
                              int32            *rgbPositions);


/* Log stripped */

#endif /* __GSCBLACKREMOVE_H__ */
