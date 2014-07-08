/** \file
 * \ingroup gstate
 *
 * $HopeName: COREgstate!color:src:gscdevcipriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Private device color data
 */

#ifndef __GSCDEVCIPRIV_H__
#define __GSCDEVCIPRIV_H__


#include "graphict.h"           /* GUCR_RASTERSTYLE et al */
#include "gs_color.h"           /* CLINK */
#include "gs_colorprivt.h"      /* GS_COLORinfo */
#include "gschcms.h"            /* REPRO_COLOR_MODEL */
#include "gscparams.h"          /* COLOR_PAGE_PARAMS */
#include "gscdevci.h"

enum {
  DC_TYPE_none             = 1, /* for blend space to blend space */
  DC_TYPE_normal           = 2, /* transfers and calibration */
  DC_TYPE_halftone_only    = 3, /* dummy transfers */
  DC_TYPE_transfer_only    = 4, /* interpreting objects to a blend space */
  DC_TYPE_calibration_only = 5  /* converting final backdrop */
};
typedef int32   DEVICECODE_TYPE;

enum {
  DC_TRANSFORMEDSPOT_ILLEGAL   = 1,
  DC_TRANSFORMEDSPOT_NONE      = 2, /* no spot colors */
  DC_TRANSFORMEDSPOT_NORMAL    = 3, /* normal tint transform */
  DC_TRANSFORMEDSPOT_INTERCEPT = 4  /* intercept tint transform for color
                                       management of process colorants */
};
typedef int32   TRANSFORMEDSPOT;

/* Bits in the overprintProcess field of CLINK.
 * OP_DISABLED says that overpinting is disabled for this invocation of the
 * colour chain. Introduced for Separation All that won't give different
 * output with or without overprinting.
 * OP_CMYK_OVERPRINT_MASK is the mask for the level 1 setcmykoveprint flags.
 * OP_CMYK_OVERPRINT_BITS is defined for the color cache which needs to do
 * some bit manipulation to compress data.
 */
#define OP_DISABLED               (0x80)
#define OP_CMYK_OVERPRINT_MASK    (0x0F)
#define OP_CMYK_OVERPRINT_BITS    (4)

typedef struct DCILUT DCILUT ;

Bool cc_devicecode_populate( CLINK *clink , CLINKblock *cblock ) ;

CLINK *cc_devicecode_create(GS_COLORinfo          *colorInfo,
                            GUCR_RASTERSTYLE      *hRasterStyle,
                            int32                 colorType,
                            GS_CONSTRUCT_CONTEXT  *context,
                            COLORSPACE_ID         jobColorSpace,
                            Bool                  fIntercepting,
                            Bool                  fCompositing,
                            CLINK                 *pHeadLink,
                            DEVICECODE_TYPE       deviceCodeType,
                            uint8                 patternPaintType,
                            REPRO_COLOR_MODEL     chainColorModel,
                            int32                 headLinkBlackPos,
                            OBJECT                *illegalTintTransform,
                            Bool                  *fApplyMaxBlts);

CLINK *cc_nonintercept_create(GS_COLORinfo          *colorInfo,
                              GUCR_RASTERSTYLE      *hRasterStyle,
                              int32                 colorType,
                              GS_CONSTRUCT_CONTEXT  *context,
                              COLORSPACE_ID         colorSpace,
                              GS_DEVICECODEinfo     *devicecodeInfo,
                              CLINK                 *pHeadLink,
                              CLINK                 *devicecodeLink,
                              REPRO_COLOR_MODEL     chainColorModel,
                              int32                 headLinkBlackPos);

void cc_deviceGetOutputColors( CLINK *pLink,
                               COLORVALUE **oColorValues,
                               COLORANTINDEX **oColorants,
                               int32 *nColors,
                               Bool *fOverprinting );
void cc_deviceGetOutputColorSpace( CLINK *pLink, OBJECT **colorSpaceObj ) ;

/* Shfill decomposition creates colors by interpolation instead of color
   chain invocation and therefore requires a specialist routine to update
   the halftone cache. */
void cc_deviceUpdateHTCacheForShfillDecomposition(CLINK* pLink);

void cc_destroydevicecodeinfo( GS_DEVICECODEinfo   **devicecodeInfo );
void cc_reservedevicecodeinfo( GS_DEVICECODEinfo *devicecodeInfo );

Bool cc_samedevicecodehalftoneinfo (void * pvGS_halftoneinfo, CLINK * pLink);
Bool cc_samedevicecodetransferinfo (void * pvGS_transferinfo, CLINK * pLink);
Bool cc_samedevicecodecalibrationinfo (void * pvGS_calibrationinfo, CLINK * pLink);

Bool cc_isblockoverprinting(GS_BLOCKoverprint *blockOverprint) ;
void cc_destroyblockoverprints(GS_BLOCKoverprint **pBlockOverprint) ;

Bool cc_isoverprintblackpossible(GS_COLORinfo *colorInfo, int32 colorType,
                                 Bool fCompositing);
Bool cc_overprintsEnabled(GS_COLORinfo *colorInfo, COLOR_PAGE_PARAMS *colorPageParams);

#ifdef METRICS_BUILD
#ifdef ASSERT_BUILD
int cc_countLinksInDeviceCode(CLINK *pLink);
#endif    /* ASSERT_BUILD */
#endif    /* METRICS_BUILD */


/* Log stripped */

#endif /* __GSCDEVCIPRIV_H__ */
