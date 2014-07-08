/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!export:gsc_icc.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * External interface of modular colour chain processor for ICC profiles.
 */

#ifndef __GSC_ICC_H__
#define __GSC_ICC_H__

#include "objectt.h"
#include "gs_color.h"

struct ICC_PROFILE_INFO_CACHE;

void gsc_purgeInactiveICCProfileInfo(COLOR_STATE *colorState);

void gsc_protectICCCache(int id, void icc_callback(void *, int), void *data);

Bool gsc_geticcbasedintent(GS_COLORinfo *colorInfo, STACK *operandstack);

Bool gsc_get_iccbased_intent(GS_COLORinfo *colorInfo,
                             OBJECT *iccbasedspace, NAMECACHE **intentName);

Bool gsc_geticcbasedinfo(GS_COLORinfo *colorInfo, STACK *operandstack);

Bool gsc_get_iccbased_dimension( GS_COLORinfo *colorInfo,
                                 OBJECT *iccbasedspace, int32 *N );

Bool gsc_geticcbased_is_scRGB(GS_COLORinfo *colorInfo, STACK *operandstack);

Bool gsc_compare_md5s( GS_COLORinfo *colorInfo,
                       OBJECT *iccbasedspace_1,
                       OBJECT *iccbasedspace_2,
                       Bool *match );

Bool gsc_get_icc_output_profile_device_space(GS_COLORinfo *colorInfo,
                                             OBJECT *iccFileObj,
                                             COLORSPACE_ID *deviceSpace);

struct FILELIST ;
Bool gsc_icc_check_for_wcs(struct FILELIST *iccp,
                           Bool *found, uint32 *offset, uint32 *size) ;

Bool gsc_startICCCache(COLOR_STATE *colorState);
void gsc_finishICCCache(COLOR_STATE *colorState);
Bool gsc_ICCCacheTransfer(DL_STATE *page);

#define NUM_CSA_SIZE (2)

/** dstColorSpaceArray must be at least NUM_CSA_SIZE */
void gsc_safeBackendColorSpace(OBJECT *dstColorSpace,
                               OBJECT *srcColorSpace,
                               OBJECT *dstColorSpaceArray);

#endif


/* Log stripped */
