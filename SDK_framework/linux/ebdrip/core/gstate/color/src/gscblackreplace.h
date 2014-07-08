/** \file
 * \ingroup gstate
 *
 * $HopeName: COREgstate!color:src:gscblackreplace.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2010-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Black preservation: Third (Black replace) phase.
 */

#ifndef __GSCBLACKREPLACE_H__
#define __GSCBLACKREPLACE_H__


#include "gs_colorpriv.h"       /* CLINK */

Bool cc_blackreplace_create(int32             nColorants,
                            COLORANTINDEX     *colorants,
                            COLORSPACE_ID     colorSpace,
                            Bool              f100pcBlackRelevant,
                            Bool              fBlackTintRelevant,
                            GUCR_RASTERSTYLE  *hRasterStyle,
                            CLINK             **blackLink);

Bool cc_getBlackPositionInOutputList(int32            nColorants,
                                     COLORANTINDEX    *iColorants,
                                     GUCR_RASTERSTYLE *hRasterStyle,
                                     int32            *blackPosition);

Bool cc_blackPresentInOutput(GUCR_RASTERSTYLE *hRasterStyle, Bool for100pcBlack);

/* Log stripped */

#endif /* __GSCBLACKREPLACE_H__ */
