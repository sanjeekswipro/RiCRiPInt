/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gschcmspriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Graphic-state HCMP private data
 */

#ifndef __GSCHCMSPRIV_H__
#define __GSCHCMSPRIV_H__


#include "mps.h"                /* mps_res_t */
#include "swcmm.h"              /* sw_cmm_instance */

#include "gschcms.h"

/* These additional colorModel values are only for use within the color module */
enum {
  REPRO_COLOR_MODEL_CMYK_WITH_SPOTS = REPRO_N_COLOR_MODELS,
  REPRO_COLOR_MODEL_RGB_WITH_SPOTS,
  REPRO_COLOR_MODEL_GRAY_WITH_SPOTS,
  REPRO_COLOR_MODEL_PATTERN,
  REPRO_N_COLOR_MODELS_WITH_SPOTS
};

/* A special rendering intent of None means don't do color management. It's
 * defined here because It shouldn't be exposed as a CMM interface since the CMM
 * won't see it. It's a new value that has to go on the end of the SW intents.
 */
#define GSC_N_RENDERING_INTENTS     (SW_CMM_N_SW_RENDERING_INTENTS + 1)
#define SW_CMM_INTENT_NONE          (SW_CMM_N_SW_RENDERING_INTENTS)

/* This is an initialiser which means to use the currentrenderingintent */
#define SW_CMM_INTENT_DEFAULT       (GSC_N_RENDERING_INTENTS)



typedef struct REPRO_ITERATOR REPRO_ITERATOR;

void cc_destroyhcmsinfo(GS_HCMSinfo **hcmsInfo);
void cc_reservehcmsinfo(GS_HCMSinfo *hcmsInfo);
Bool cc_arehcmsobjectslocal(corecontext_t *corecontext, GS_HCMSinfo *hcmsInfo);
mps_res_t cc_scan_hcms(mps_ss_t ss, GS_HCMSinfo *hcmsInfo);

Bool cc_isInvertible(TRANSFORM_LINK_INFO *interceptInfo);

OBJECT *cc_getIntercept(GS_COLORinfo    *colorInfo,
                        int32           colorType,
                        COLORSPACE_ID   interceptSpaceId,
                        Bool            chainIsColorManaged,
                        Bool            fCompositing,
                        Bool            independentChannels);

TRANSFORM_LINK_INFO cc_getInterceptInfo(GS_COLORinfo    *colorInfo,
                                        int32           colorType,
                                        COLORSPACE_ID  interceptSpaceId,
                                        Bool           chainIsColorManaged,
                                        Bool           fCompositing,
                                        Bool           independentChannels);

TRANSFORM_LINK_INFO cc_getBlendInfo(GS_COLORinfo   *colorInfo,
                                    int32          colorType,
                                    COLORSPACE_ID  blendSpaceId);

TRANSFORM_LINK_INFO cc_getDefaultInfo(GS_COLORinfo   *colorInfo,
                                      COLORSPACE_ID  blendSpaceId);

Bool cc_getColorSpaceOverride(GS_HCMSinfo    *hcmsInfo,
                              COLORSPACE_ID  colorSpaceId,
                              int32          colorType);

Bool cc_getBlackIntercept(GS_HCMSinfo *hcmsInfo, int32 colorType);
USERVALUE cc_getBlackTintIntercept(GS_HCMSinfo *hcmsInfo, int32 colorType);
Bool cc_getBlackTintLuminance(GS_HCMSinfo *hcmsInfo);
Bool cc_getConvertRGBBlack(GS_HCMSinfo *hcmsInfo, int32 colorType);
Bool cc_getMultipleNamedColors(GS_HCMSinfo *hcmsInfo);

int cc_getNumNames(OBJECT *namesObject);
void cc_getNames(OBJECT *namesObject, NAMECACHE **names, int numNames);

Bool cc_lookupNamedColorantStore(GS_COLORinfo *colorInfo,
                                 OBJECT       *namesObject,
                                 OBJECT       **storedAlternateSpace,
                                 OBJECT       **storedTintTransform,
                                 int32        *storedTintTransformIds);
Bool cc_insertNamedColorantStore(GS_COLORinfo *colorInfo,
                                 OBJECT       *namesObject,
                                 OBJECT       *alternateSpace,
                                 OBJECT       *tintTransform,
                                 int32        tintTransformId);
void cc_purgeNamedColorantStore(GS_COLORinfo *colorInfo, int32 saveLevel);
Bool cc_updateNamedColorantStore(GS_COLORinfo *colorInfo);

void cc_initInterceptId(uint32 *id);

uint32 cc_getInterceptId(GS_HCMSinfo *hcmsInfo);

TRANSFORM_LINK_INFO cc_getAssociatedProfile(GS_COLORinfo      *colorInfo,
                                            REPRO_COLOR_MODEL colorModel);

Bool cc_getPaintingSoftMask(GS_COLORinfo *colorInfo);

sw_cmm_instance *cc_getAlternateCMM(GS_HCMSinfo *hcmsInfo);
sw_cmm_instance *cc_getwcsCMM(GS_HCMSinfo *hcmsInfo);

NAMECACHE *gsc_convertIntentToName(uint8 renderingIntent);

uint8 cc_getrenderingintent(GS_COLORinfo *colorInfo);

Bool cc_reproductionIteratorInit(GS_COLORinfo         *colorInfo,
                                 int32                colorType,
                                 TRANSFORM_LINK_INFO  *currentReproSpaceInfo,
                                 REPRO_ITERATOR       **reproIterator);
void cc_reproductionIteratorNext(REPRO_ITERATOR       *iterator,
                                 TRANSFORM_LINK_INFO  *nextReproSpaceInfo,
                                 Bool                 *startNewTransform);
void cc_reproductionIteratorFinish(REPRO_ITERATOR **reproIterator);

uint8 cc_sourceRenderingIntent(GS_COLORinfo   *colorInfo,
                                int32          colorType);

Bool cc_sourceBlackPointComp(GS_COLORinfo   *colorInfo);

Bool cc_invalidateCurrentRenderingIntent(GS_COLORinfo *colorInfo);

Bool cc_findColorModel(GS_COLORinfo       *colorInfo,
                       COLORSPACE_ID      iColorSpaceId,
                       OBJECT             *colorSpace,
                       Bool               fCompositing,
                       REPRO_COLOR_MODEL  *colorModel);

Bool cc_setColorModel(GS_COLORinfo *colorInfo, int32 colorType,
                      REPRO_COLOR_MODEL chainColorModel);

Bool cc_isDevicelink(TRANSFORM_LINK_INFO *linkInfo);

Bool cc_usingObjectBasedColor(GS_COLORinfo *colorInfo);

/* Log stripped */

#endif /* __GSCHCMSPRIV_H__ */
