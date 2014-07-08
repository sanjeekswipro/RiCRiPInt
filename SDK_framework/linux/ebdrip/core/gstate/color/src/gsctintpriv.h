/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!color:src:gsctintpriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * The tint transform color link.
 */

#ifndef __GSCTINTPRIV_H__
#define __GSCTINTPRIV_H__


#include "objects.h"         /* OBJECT */
#include "gscsmpxformpriv.h" /* GSC_SIMPLE_TRANSFORM */
#include "dl_color.h"        /* COLORANTINDEX */
#include "gs_color.h"        /* CLINK */

#include "gsctint.h"

typedef struct GS_TINT_STATE GS_TINT_STATE;

CLINK *cc_tinttransform_create(int32                nColorants,
                               COLORANTINDEX        *colorants,
                               COLORSPACE_ID        colorSpaceId,
                               GS_COLORinfo         *colorInfo,
                               OBJECT               *colorSpaceObject,
                               Bool                 fCompositing,
                               OBJECT               **alternativeSpaceObject,
                               Bool                 allowNamedColorIntercept,
                               Bool                 *pf_allowColorManagement);

CLINK *cc_allseptinttransform_create(int32            nColorants,
                                     COLORANTINDEX    *colorants,
                                     COLORSPACE_ID    colorSpaceId,
                                     int32            nOutputColorants,
                                     int32            convertAllSeparation);

Bool cc_tinttransformiscomplex(CLINK *pLink);

GSC_SIMPLE_TRANSFORM *cc_tintSimpleTransform(CLINK *pLink);

Bool cc_tintStateCreate(GS_TINT_STATE **tintStateRef);
void cc_tintStateDestroy(GS_TINT_STATE **tintStateRef);

int32 cc_nextTintTransformId(GS_TINT_STATE *tintState);


#endif /* __GSCTINTPRIV_H__ */

/* Log stripped */
