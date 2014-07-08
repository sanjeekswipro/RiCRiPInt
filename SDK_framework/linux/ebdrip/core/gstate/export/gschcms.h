/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!export:gschcms.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS graphic-state color-management API
 */

#ifndef __GSCHCMS_H__
#define __GSCHCMS_H__

#include "swcmm.h"              /* SW_CMM_N_SW_RENDERING_INTENTS */
#include "objects.h"            /* OBJECT */
#include "gs_color.h"

/* Different kinds of graphic object which may be intercepted differently.
   The RGBImages rendering intent is only for CRD selection - it is not a
   full rendering intent for e.g. rendering different types of graphic
   objects to different separations.  For this purpose rgb image objects
   are treated the same as picture objects.  */
enum {
  REPRO_TYPE_ILLEGAL = 255,

  REPRO_TYPE_PICTURE = 0,
  REPRO_TYPE_TEXT,
  REPRO_TYPE_VIGNETTE,
  REPRO_TYPE_OTHER,
  REPRO_N_TYPES
};

/* Color classes used in object/colorspace overriding of profiles & intents */
typedef uint8   REPRO_COLOR_MODEL;
enum {
  REPRO_COLOR_MODEL_INVALID = 255,

  REPRO_COLOR_MODEL_CMYK = 0,
  REPRO_COLOR_MODEL_RGB,
  REPRO_COLOR_MODEL_GRAY,
  REPRO_COLOR_MODEL_NAMED_COLOR,
  REPRO_COLOR_MODEL_CIE,
  REPRO_N_COLOR_MODELS
};

#define DEFAULT_POOR_SOFT_MASK (TRUE)

Bool gsc_getOverprintPreview(GS_COLORinfo *colorInfo);

Bool gsc_isInvertible(GS_COLORinfo *colorInfo, OBJECT *colorSpace);

OBJECT *gsc_getBlend(GS_COLORinfo   *colorInfo,
                     COLORSPACE_ID  blendSpaceId,
                     uint8          reproType);

Bool gsc_getBlendInfoSet(GS_COLORinfo *colorInfo, COLORSPACE_ID blendSpaceId);

Bool gsc_getColorSpaceOverride(GS_COLORinfo *colorInfo,
                               COLORSPACE_ID colorSpaceId);

OBJECT *gsc_getNamedColorIntercept(GS_COLORinfo *colorInfo);

/* Should only be called during setpagedevice and stores the alternate CMM
 * installed into the pagedevice.
 */
Bool gsc_setAlternateCMM(GS_COLORinfo *colorInfo, OBJECT *cmmName);

Bool gsc_setinterceptcolorspace(GS_COLORinfo *colorInfo, OBJECT *icsDict);
Bool gsc_getinterceptcolorspace(GS_COLORinfo *colorInfo, STACK *stack);

Bool gsc_setPoorSoftMask(GS_COLORinfo *colorInfo, Bool poorSoftMask);
Bool gsc_configPaintingSoftMask(GS_COLORinfo *colorInfo, Bool setOverride);

Bool gsc_setAssociatedProfile(GS_COLORinfo   *colorInfo,
                              COLORSPACE_ID  colorSpaceId,
                              OBJECT         *profile);

Bool gsc_setrenderingintent(GS_COLORinfo *colorInfo, OBJECT *riName);
Bool gsc_getrenderingintent(GS_COLORinfo *colorInfo, STACK *stack);
NAMECACHE *gsc_convertIntentToName(uint8 renderingIntent);
uint8 gsc_getICCrenderingintent(GS_COLORinfo *colorInfo);
Bool gsc_setAdobeRenderingIntent(GS_COLORinfo *colorInfo, Bool adobeRenderingIntent);
Bool gsc_getAdobeRenderingIntent(GS_COLORinfo *colorInfo);

Bool gsc_setreproduction(GS_COLORinfo *colorInfo, OBJECT *reproDict);
Bool gsc_getreproduction(GS_COLORinfo *colorInfo, STACK *stack);

Bool gsc_addOutputIntent(GS_COLORinfo *colorInfo, OBJECT *profileObj);
Bool gsc_pdfxResetIntercepts(GS_COLORinfo *colorInfo);

Bool gsc_setmiscobjectmappings(GS_COLORinfo *colorInfo, OBJECT *objectDict);
Bool gsc_getmiscobjectmappings(GS_COLORinfo *colorInfo, STACK *stack);

uint8 gsc_getTreatOneBitImagesAs(GS_COLORinfo *colorInfo);
uint8 gsc_getTreatSingleRowImagesAs(GS_COLORinfo *colorInfo);

int gsc_reproTypePriority(uint8 reproType);

Bool gsc_setRequiredReproType(
  /*@notnull@*/               GS_COLORinfo *colorInfo,
                              int32 colorType,
                              uint8 reproType);
uint8 gsc_getRequiredReproType(
  /*@notnull@*/                const GS_COLORinfo *colorInfo,
                               int32 colorType);
Bool gsc_resetRequiredReproType(GS_COLORinfo *colorInfo, int32 colorType);
                            /*@notnull@*/
NAMECACHE *gsc_getReproTypeName(int32 reproType);

Bool gsc_setColorModel(GS_COLORinfo *colorInfo, int32 colorType,
                       REPRO_COLOR_MODEL colorModel);
REPRO_COLOR_MODEL gsc_getColorModel(GS_COLORinfo *colorInfo, int32 colorType);
Bool gsc_colorModel(GS_COLORinfo      *colorInfo,
                    OBJECT            *colorSpace,
                    REPRO_COLOR_MODEL *colorModel);


#endif /* __GSCHCMS_H__ */

/* Log stripped */
