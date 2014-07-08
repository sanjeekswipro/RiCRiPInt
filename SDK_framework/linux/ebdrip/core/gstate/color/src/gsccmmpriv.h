/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gsccmmpriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * CMM privata data API
 */

#ifndef __GSCCMMPRIV_H__
#define __GSCCMMPRIV_H__


#include "gschcmspriv.h"        /* MAX_NEXTDEVICE_DICTS */
#include "gs_colorpriv.h"       /* XYZVALUE */
#include "gu_chan.h"            /* DEVICESPACEID */
#include "swcmm.h"              /* sw_cmm_instance */

typedef union {
  void                      *shared;
  CLINKciebaseddefg         *ciebaseddefg;
  CLINKcietableabcd         *cietableabcd;
  CLINKciebaseddef          *ciebaseddef;
  CLINKcietableabc          *cietableabc;
  CLINKciebasedabc          *ciebasedabc;
  CLINKcalrgbg              *calrgb;
  CLINKlab                  *lab;
  CLINKciebaseda            *ciebaseda;
  CLINKcietablea            *cietablea;
  CLINKcalrgbg              *calgray;
  ICC_PROFILE_INFO          *icc;
  CUSTOM_CMM_INFO           *customcmm;
  GS_CRDinfo                *crd;
  HQN_PROFILE_INFO          *hqnprofile;
} TRANSFORM_LINK_INFO_UNION;

struct TRANSFORM_LINK_INFO {
  COLORSPACE_ID               inputColorSpaceId;
  COLORSPACE_ID               outputColorSpaceId;
  TRANSFORM_LINK_INFO_UNION   u;
  uint8                       intent;
  Bool                        blackPointComp;
  TRANSFORM_LINK_INFO         *next;
};


CLINK *cc_cmmxform_create(GS_COLORinfo          *colorInfo,
                          TRANSFORM_LINK_INFO   *transformList[MAX_NEXTDEVICE_DICTS],
                          DEVICESPACEID         aimDeviceSpace,
                          uint8                 currentRenderingIntent,
                          sw_cmm_instance       *alternateCMM,
                          sw_cmm_instance       *wcsCMM,
                          COLORANTINDEX         *pColorantIndexArray,
                          COLORSPACE_ID         *chainColorSpace,
                          int32                 *colorspacedimension,
                          OBJECT                **outputPSColorSpace);

void cc_getCMMRange(CLINK *pLink, int32 index, SYSTEMVALUE range[2]);
CLINK *cc_getFinalXYZLink(CLINK *pLink);

Bool cc_hqnprofile_createInfo(GS_COLORinfo      *colorInfo,
                              OBJECT            *profileObj,
                              HQN_PROFILE_INFO  **profileInfo,
                              int32             *validDimensions,
                              COLORSPACE_ID     *validDeviceSpace);
void cc_destroyhqnprofileinfo(HQN_PROFILE_INFO  **pInfo);
void cc_reservehqnprofileinfo(HQN_PROFILE_INFO  *pInfo);

void cc_initTransformInfo(TRANSFORM_LINK_INFO *transformInfo);
Bool cc_createTransformInfo(GS_COLORinfo        *colorInfo,
                            TRANSFORM_LINK_INFO *pInfo,
                            OBJECT              *PSColorSpace);
void cc_destroyTransformInfo(TRANSFORM_LINK_INFO *pInfo);
void cc_reserveTransformInfo(TRANSFORM_LINK_INFO *pInfo);
Bool cc_sameColorSpaceFromTransformInfo(TRANSFORM_LINK_INFO *info1,
                                        TRANSFORM_LINK_INFO *info2);
mm_size_t cc_sizeofTransformInfo();

TRANSFORM_LINK_INFO *cc_followOnTransformLinkInfo(TRANSFORM_LINK_INFO *linkInfo);

#ifdef METRICS_BUILD
#ifdef ASSERT_BUILD
int cc_countLinksInCMMXform(CLINK *pLink);
#endif    /* ASSERT_BUILD */
#endif    /* METRICS_BUILD */


/* Log stripped */

#endif  /* __GSCCMMPRIV_H__ */
