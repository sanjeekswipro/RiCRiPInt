/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!color:src:gscpresp.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Pre-separated color chain API
 */

#ifndef __GSCPRESP_H__
#define __GSCPRESP_H__

#include "graphict.h" /* GS_COLORinfo */
#include "gs_colorpriv.h" /* GS_CHAINinfo */
#include "gs_color.h"       /* CLINK */


CLINK* cc_preseparation_create(GS_COLORinfo         *colorInfo,
                               int32                colorType,
                               GS_CONSTRUCT_CONTEXT *context,
                               GS_CHAINinfo         *colorChain,
                               CLINK                *pHeadLink);

#endif /* !__GSCPRESP_H__ */


/* Log stripped */
