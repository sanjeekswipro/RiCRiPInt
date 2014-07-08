/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gscicc.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Modular colour chain processor for ICC profiles.
 */

#ifndef __GSCICC_H__
#define __GSCICC_H__

#include "objecth.h"            /* OBJECT */
#include "gs_colorpriv.h"       /* CLINK (& COLORSPACE_ID) */
#include "swblobapi.h"          /* sw_blob_instance */

#include "gsc_icc.h"            /* gsc_startICCCache */
#include "icmini.h"             /* ICC_PROFILE_INFO */


Bool cc_iccbased_create(ICC_PROFILE_INFO  *pInfo,
                        COLOR_STATE    *colorState,
                        uint8          desiredIntent,
                        CLINK          **pNextLink,
                        COLORSPACE_ID  *oColorSpace,
                        int32          *dimensions,
                        OBJECT         **nextColorSpaceObject,
                        XYZVALUE       **sourceWhitePoint,
                        XYZVALUE       **sourceBlackPoint,
                        XYZVALUE       **sourceRelativeWhitePoint,
                        XYZVALUE       **sourceRelativeBlackPoint);

Bool cc_outputtransform_create(ICC_PROFILE_INFO *pInfo,
                               COLOR_STATE    *colorState,
                               uint8          desiredIntent,
                               OBJECT         **nextColorSpaceObject,
                               CLINK          **pNextLink,
                               COLORSPACE_ID  *oColorSpace,
                               int32          *dimensions,
                               XYZVALUE       **destWhitePoint,
                               XYZVALUE       **destBlackPoint,
                               XYZVALUE       **destRelativeWhitePoint,
                               XYZVALUE       **destRelativeBlackPoint);

Bool cc_get_icc_is_conversion_profile( ICC_PROFILE_INFO *pInfo,
                                       int32 *is_conversion );

Bool cc_get_icc_is_scRGB( ICC_PROFILE_INFO *pInfo,
                          int32 *is_scRGB );

Bool cc_get_icc_details( ICC_PROFILE_INFO *pInfo,
                         Bool onlyIfValid,
                         int32 *dimensions,
                         COLORSPACE_ID *deviceSpace,
                         COLORSPACE_ID *pcsSpace );

Bool cc_valid_icc_profile( ICC_PROFILE_INFO *pInfo );

Bool cc_get_icc_DeviceN( ICC_PROFILE_INFO *iccbasedInfo,
                         OBJECT *colorSpaceObject,
                         OBJECT **nextColorSpaceObject );

Bool cc_get_iccbased_profile_info( GS_COLORinfo *colorInfo,
                                   OBJECT *iccbasedspace,
                                   ICC_PROFILE_INFO **iccbasedInfo,
                                   int32 *dimensions,
                                   COLORSPACE_ID *deviceSpace,
                                   COLORSPACE_ID *pcsSpace );

Bool cc_icc_availableModes(ICC_PROFILE_INFO *pInfo,
                           Bool *inputTablePresent,
                           Bool *outputTablePresent,
                           Bool *devicelinkTablePresent);

int32 cc_iccbased_nOutputChannels(ICC_PROFILE_INFO *iccbasedInfo);

Bool cc_get_icc_output_profile_info( GS_COLORinfo *colorInfo,
                                     OBJECT *iccFileObj,
                                     ICC_PROFILE_INFO **iccbasedInfo,
                                     int32 *dimensions,
                                     COLORSPACE_ID *deviceSpace,
                                     COLORSPACE_ID *pcsSpace );

Bool cc_iccSaveLevel(ICC_PROFILE_INFO *pInfo);

sw_blob_instance *cc_get_icc_blob(ICC_PROFILE_INFO *iccInfo);

void cc_getICCBasedRange( CLINKiccbased *iccInfo,
                                 int32 index,
                                 SYSTEMVALUE range[2] );

void cc_getICCInfoRange( ICC_PROFILE_INFO *iccbasedInfo,
                         int32 index,
                         SYSTEMVALUE range[2] );

Bool cc_iccbased_init(void);
void cc_iccbased_finish(void);

void cc_purgeICCProfileInfo( COLOR_STATE *colorState, int32 savelevel );

CLINK *cc_createInverseICCLink(CLINK *pLink, COLORSPACE_ID *deviceSpace);

#endif

/* Log stripped */
