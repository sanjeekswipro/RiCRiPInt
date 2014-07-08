/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!export:gscparams.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2011-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Color params.
 */

#ifndef __GSCPARAMS_H__
#define __GSCPARAMS_H__

struct DL_STATE ; /* from SWv20/COREdodl */

#include "objects.h"

/** The modularised color system params. None of them can be accessed from back
 * end processes.
 */
typedef struct COLOR_SYSTEM_PARAMS {
  Bool      Overprint;
  Bool      ImmediateRepro;
  Bool      UseAllSetScreen;
  Bool      AdobeCurrentHalftone;
  Bool      TableBasedColor;
  Bool      ForcePositive;
} COLOR_SYSTEM_PARAMS;

/** The modularised color user params.
 * Some of these are implemented as gstate params, while some are implemented as
 * though they were pagedevice keys. This is unsatisfactory, but it will have to
 * do for the moment.
 */
typedef struct COLOR_USER_PARAMS {
  /* This set of params are treated as effectively gstate params because copies
   * are made into the gstate which can be changed independently.
   * NB. Overprint is a front end process and the use of the overprint values is
   * largley protected from the back end.
   */
  Bool      OverprintProcess;
  Bool      OverprintBlack;
  Bool      OverprintGray;
  Bool      OverprintGrayImages;
  Bool      OverprintWhite;
  Bool      IgnoreOverprintMode;
  Bool      TransformedSpotOverprint;
  Bool      OverprintICCBased;
  int       EnableColorCache;

  /** These are real userparams, but restricting access to them from only the
   * front end may be difficult to maintain because the body of code is shared
   * with the back end. The expedient solution is to also implement them as
   * gstate attributes.
   */
  OBJECT    ExcludedSeparations;
  Bool      PhotoshopInput;
  Bool      AdobeProcessSeparations;

  /* This set of params are treated as pagedevice params. They are all copied
   * into the DL_STATE at setpagedevice.
   */
  Bool      AbortOnBadICCProfile;
  int16     DuplicateColorants;
  Bool      NegativeJob;
  Bool      IgnoreSetTransfer;
  Bool      IgnoreSetBlackGeneration;
  OBJECT    TransferFunction;
  int       TransferFunctionId;     /* unique id for above. */
  Bool      UseAllSetTransfer;

  int       FasterColorMethod;
  int       FasterColorGridPoints;
  USERVALUE FasterColorSmoothness;
  Bool      HalftoneColorantMapping;
  Bool      UseFastRGBToCMYK;
  int       RGBToCMYKMethod;
} COLOR_USER_PARAMS;

typedef struct COLOR_PAGE_PARAMS {
  int       exposure;
  Bool      negativePrint;
  int       contoneMask;
  Bool      halftoning;

  Bool      overprintsEnabled;
  Bool      immediateRepro;
  Bool      useAllSetScreen;
  Bool      adobeCurrentHalftone;
  Bool      tableBasedColor;
  Bool      forcePositive;

  Bool      abortOnBadICCProfile;
  int16     duplicateColorants;
  Bool      negativeJob;
  Bool      ignoreSetTransfer;
  Bool      ignoreSetBlackGeneration;
  struct {
    Bool cached;
    union {
      struct CALLPSCACHE *cpsc;
      OBJECT psfunc;
    } func;
    int id; /* unique id for above. */
  } transfer;
  Bool      useAllSetTransfer;

  int       fasterColorMethod;
  int       fasterColorGridPoints;
  USERVALUE fasterColorSmoothness;
  Bool      halftoneColorantMapping;
} COLOR_PAGE_PARAMS;


mps_res_t MPS_CALL gsc_scanColorUserParams(mps_ss_t ss, void *p, size_t s);

Bool gsc_pagedevice(corecontext_t *context,
                    struct DL_STATE *page,
                    GS_COLORinfo  *colorInfo,
                    OBJECT        *pagedeviceDict);

/* Log stripped */

#endif /* __GSCPARAMS_H__ */
