/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gscalternatecmmpriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * API allowing selection of alternate Color Management Module (CMM)
 */

#ifndef __GSCCMMXFORMPRIV_H__
#define __GSCCMMXFORMPRIV_H__


#include "swcmm.h"              /* sw_cmm_instance */
#include "objectt.h"            /* OBJECT */

sw_cmm_instance *cc_findAlternateCMM(OBJECT *cmmName);

Bool cc_cmmSupportInputProfiles(sw_cmm_instance *alternateCMM);
Bool cc_cmmSupportOutputProfiles(sw_cmm_instance *alternateCMM);
Bool cc_cmmSupportDevicelinkProfiles(sw_cmm_instance *alternateCMM);
Bool cc_cmmSupportColorspaceProfiles(sw_cmm_instance *alternateCMM);
Bool cc_cmmSupportAbstractProfiles(sw_cmm_instance *alternateCMM);
Bool cc_cmmSupportNamedColorProfiles(sw_cmm_instance *alternateCMM);
Bool cc_cmmSupportICCv4Profiles(sw_cmm_instance *alternateCMM);
Bool cc_cmmSupportBlackPointCompensation(sw_cmm_instance *alternateCMM);
Bool cc_cmmSupportExtraAbsoluteIntents(sw_cmm_instance *alternateCMM);
uint32 cc_cmmMaxInputChannels(sw_cmm_instance *alternateCMM);
uint32 cc_cmmMaxOutputChannels(sw_cmm_instance *alternateCMM);
Bool cc_cmmAllowRetry(sw_cmm_instance *alternateCMM);

CLINK *cc_alternatecmm_create(TRANSFORM_LINK_INFO   *transformList,
                              sw_cmm_instance            *alternateCMM,
                              COLORSPACE_ID         *oColorSpace,
                              int32                 *colorspacedimension);

CLINK *cc_customcmm_create(CUSTOM_CMM_INFO  *customcmmInfo,
                           COLORSPACE_ID    *oColorSpace,
                           int32            *colorspacedimension,
                           OBJECT           **outputPSColorSpace);

Bool cc_createcustomcmminfo(CUSTOM_CMM_INFO   **customcmmInfo,
                            COLORSPACE_ID     *outputColorSpaceId,
                            GS_COLORinfo      *colorInfo,
                            OBJECT            *colorSpaceObject,
                            sw_cmm_instance   *alternateCMM);

void cc_destroycustomcmminfo(CUSTOM_CMM_INFO **customcmmInfo);

void cc_reservecustomcmminfo(CUSTOM_CMM_INFO *customcmmInfo);

int32 cc_customcmm_nOutputChannels(CUSTOM_CMM_INFO *customcmmInfo);


/* Log stripped */

#endif /* __GSCCMMXFORMPRIV_H__ */
