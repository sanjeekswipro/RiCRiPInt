/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gsccrdpriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * CRD private data API
 */

#ifndef __GSCCRDPRIV_H__
#define __GSCCRDPRIV_H__


#include "gu_chan.h"  /* DEVICESPACEID */
#include "mps.h"      /* mps_res_t */
#include "gsccrd.h"
#include "context.h"

CLINK *cc_crd_create(GS_CRDinfo         *crdInfo,
                     XYZVALUE           sourceWhitePoint,
                     XYZVALUE           sourceBlackPoint,
                     XYZVALUE           **destWhitePoint,
                     XYZVALUE           **destBlackPoint,
                     XYZVALUE           **destRelativeWhitePoint,
                     DEVICESPACEID      realDeviceSpace,
                     Bool               neutralMappingNeeded,
                     OBJECT             **ppOutputColorSpace,
                     COLORSPACE_ID      *oColorSpace,
                     int32              *colorspacedimension); /* of the output colorspace */

void  cc_destroycrdinfo( GS_CRDinfo **crdInfo ) ;
void  cc_reservecrdinfo( GS_CRDinfo *crdInfo ) ;
Bool cc_arecrdobjectslocal(corecontext_t *corecontext, GS_CRDinfo *crdInfo ) ;
mps_res_t cc_scan_crd( mps_ss_t ss, GS_CRDinfo *crdInfo );

void cc_crd_details(GS_CRDinfo    *crdInfo,
                    int32         *outputDimensions,
                    COLORSPACE_ID *outputSpaceId);

Bool cc_setcolorrendering( GS_COLORinfo *colorInfo,
                           GS_CRDinfo **pcrdInfo,
                           OBJECT *crdObject ) ;

Bool cc_crdpresent(GS_CRDinfo* crdInfo);

/* Log stripped */

#endif /* __GSCCRDPRIV_H__ */
