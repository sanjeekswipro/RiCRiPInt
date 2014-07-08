/** \file
 * \ingroup gstate
 *
 * $HopeName: COREgstate!color:src:gscheadpriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Color chain private data
 */

#ifndef __GSCHEADPRIV_H__
#define __GSCHEADPRIV_H__


#include "gs_table.h"           /* GSC_TABLE */
#include "gs_colorpriv.h"       /* CLINK */
#include "gschead.h"

struct COC_HEAD;

#define MAX_CSA_LENGTH    (5)   /* The max length of a color space array */

#define LUM_TABLE_SIZE    (0x100)
#define LUM_TABLE_notset  (-1.0f)

struct GS_CHAINinfo { /* NOTE: When changing, update SWcore!testsrc:swaddin:swaddin.cpp. */
  cc_counter_t      refCnt;
  size_t            structSize;
  COLORSPACE_ID     iColorSpace;
  uint8             chainStyle;
  uint8             overprintProcess;
  Bool              fPresepColorIsGray;
  Bool              fSoftMaskLuminosityChain; /* soft mask alpha is being derived from luminosity */
  Bool              fCompositing;             /* this chain converts colors from a virtual raster style */
  uint8             patternPaintType;
  GSC_BLACK_TYPE    inBlackType;              /* Always BLACK_TYPE_NONE when interpreting, set when compositing */
  Bool              fromVirtualDevice;        /* If the source of this color is the virtual device group */
  Bool              prevIndependentChannels;  /* was there colorant mixing in the previous chain of transparency stack */

  OBJECT            colorspace;
  int               n_iColorants;
  OBJECT            pattern;
  REPRO_COLOR_MODEL chainColorModel;

  int               saveLevel;

  /* Volatile data that may change for each color invoke without rebuilding the chain */
  USERVALUE         *iColorValues;

  GS_CHAIN_CONTEXT  *context;
};

struct GS_CHAIN_CONTEXT {
  cc_counter_t      refCnt;
  CLINK             *pnext;                 /* The main chain */
  CLINK             *pSimpleLink;           /* A simple chain for deriving color managed overprints */
  CLINK             *pCurrentCMYK;          /* A simple chain for deriving currentcmykcolor */
  CLINK             *pCurrentRGB;           /* A simple chain for deriving currentrgbcolor */
  CLINK             *pCurrentGray;          /* A simple chain for deriving currentcolor for gray */

  struct COC_HEAD   *pCache;
  FASTRGB2GRAY_LUT  *devCodeLut;
  GSC_TABLE         *tomsTable;
  int32             cacheFlags;

  USERVALUE         *blockTmpArray ;       /* Holds intermediate values during invokeBlock */
  mm_size_t         blockTmpArraySize ;

  OBJECT            finalDevicenCSAmain[MAX_CSA_LENGTH];
  OBJECT            finalDevicenCSAsmp[MAX_CSA_LENGTH];
  OBJECT            illegalTintTransform;

  /* Volatile data that may change for each color invoke without rebuilding the chain */
  GS_BLOCKoverprint *blockOverprint ;

  /* These items are ones that are set during chain construction */
  Bool              fIntercepting;
  Bool              fApplyMaxBlts;
  int32             blackPosition;
};

/* Possible values for chainStyle in GS_CHAININFO. */
/* NOTE: When changing, update SWcore!testsrc:swaddin:swaddin.cpp. */
typedef uint8 CHAINSTYLE;
#define CHAINSTYLE_COMPLETE           (0)
#define CHAINSTYLE_DUMMY_FINAL_LINK   (2)

/* Functions for manipulating colorchains:
 *
 * cc_createChainHead creates and populates a GS_CHAINinfo structure with the
 * colorspace specified and sets up the default color. Its reference count
 * is set to 1.
 *
 * cc_copyChainHead creates a separate copy of the GS_CHAINinfo structure,
 * with a reference count of 1. Since it shares the clink chain from the
 * original head, it increments the reference count for the head clink.
 *
 * cc_updateChainHead checks whether a GS_CHAINinfo structure has only one
 * reference, and creates a new copy if not.
 *
 * cc_destroyChainHead decrements the reference count for the GS_CHAINinfo
 * structure, freeing the memory used by it if there are no more references.
 *
 *    Returns: TRUE   - success
 *             FALSE  - failure
 */

Bool cc_getpatternbasespace( GS_COLORinfo   *colorInfo,
                             OBJECT         *theo,
                             COLORSPACE_ID  *colorspaceID,
                             int32          *dimension );

Bool cc_createChainHead( GS_CHAINinfo   **colorChain,
                         COLORSPACE_ID  iColorSpace,
                         int32          colorspaceDimension,
                         OBJECT         *colorspace );

Bool cc_initChains(GS_COLORinfo *colorInfo);

Bool cc_updateChain( GS_CHAINinfo **oldHeadLink );

void cc_destroyChain( GS_CHAINinfo **oldHeadLink );

Bool cc_createChainContextIfNecessary(GS_CHAIN_CONTEXT **newContext,
                                      GS_COLORinfo     *colorInfo);

void cc_invalidateTransformChains(GS_CHAIN_CONTEXT *chainContext);

void cc_destroyChainContext(GS_CHAIN_CONTEXT **oldContext);

Bool cc_arechainobjectslocal(corecontext_t *corecontext,
                             GS_COLORinfo *colorInfo, int32 colorType );

mps_res_t cc_scan_chain( mps_ss_t ss, GS_COLORinfo *colorInfo, int32 colorType );

OBJECT *cc_getbasecolorspaceobject( OBJECT *colorSpace );

void cc_getCieRange( CLINK *pLink, int32 index, SYSTEMVALUE range[2] );

Bool cc_sameColorSpaceObject(GS_COLORinfo   *colorInfo,
                             OBJECT         *colorSpace_1,
                             COLORSPACE_ID  colorspaceId_1,
                             OBJECT         *colorSpace_2,
                             COLORSPACE_ID  colorspaceId_2);

Bool cc_updateChainForNewColorSpace(GS_COLORinfo  *colorInfo,
                                    int32         colorType,
                                    COLORSPACE_ID colorspace_id,
                                    int32         colorspacedimension,
                                    OBJECT        *theo,
                                    CHAINSTYLE    chainStyle,
                                    Bool          fCompositing);


/* Log stripped */

#endif /* __GSCHEADPRIV_H__ */
