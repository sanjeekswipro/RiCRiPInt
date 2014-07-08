/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!color:src:gscdevcn.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS deviceN Color API
 */

#ifndef __GSCDEVCN_H__
#define __GSCDEVCN_H__

#include "graphict.h" /* GS_COLORinfo */
#include "gs_colorprivt.h" /* GS_CHAINinfo */
#include "gu_chan.h" /* DEVICESPACEID */
#include "gschcms.h" /* REPRO_COLOR_MODEL */
#include "gscsmpxformpriv.h" /* GSC_SIMPLE_TRANSFORM */

struct OBJECT;


#define IDEVN_POSN_UNKNOWN  (-1)

Bool cc_interceptdevicen_create(GS_COLORinfo      *colorInfo,
                                int32             colorType,
                                GUCR_RASTERSTYLE  *hRasterStyle,
                                int32             nColorants,
                                COLORANTINDEX     *colorants,
                                DEVICESPACEID     aimDeviceSpace,
                                int32             nDeviceColorants,
                                GS_CHAINinfo      *colorChain,
                                Bool              fColorManage,
                                struct OBJECT     *PSColorSpace,
                                CLINK             **ppLink,
                                struct OBJECT     **oColorSpace,
                                Bool              *intercepted,
                                Bool              *renderable,
                                Bool              *applyTintTransform);

Bool cc_devicenCachable(CLINK *pLink);

GSC_SIMPLE_TRANSFORM *cc_devicenSimpleTransform(CLINK *pLink);

GS_CHAINinfo *cc_getCMMChain(CLINK             *xformContainerLink,
                             REPRO_COLOR_MODEL chainColorModel);

#ifdef METRICS_BUILD
#ifdef ASSERT_BUILD
int cc_countLinksInDeviceN(CLINK *pLink);
int cc_countSubChainsInDeviceN(CLINK *pLink);
#endif    /* ASSERT_BUILD */
#endif    /* METRICS_BUILD */


#endif /* !__GSCDEVCN_H__ */


/* Log stripped */
