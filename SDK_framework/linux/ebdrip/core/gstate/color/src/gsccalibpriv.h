/** \file
 * \ingroup gstate
 *
 * $HopeName: COREgstate!color:src:gsccalibpriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Calibration private data API
 */

#ifndef __GSCCALIBPRIV_H__
#define __GSCCALIBPRIV_H__

#include "gsccalib.h"
#include "mps.h"          /* mps_ss_t */
#include "graphict.h"     /* GS_CALIBRATIONinfo */
#include "dl_color.h"     /* COLORANTINDEX */
#include "gs_colorpriv.h" /* GS_CALIBRATIONinfo */
#include "dlstate.h" /* DL_STATE */


#define CLID_SIZEcalibration 2

typedef struct gsc_warnings_criteria    GSC_WARNINGS_CRITERIA;

CLINK *cc_calibration_create(int32              nColorants,
                             COLORANTINDEX      *colorants,
                             COLORSPACE_ID      colorSpace,
                             GS_CALIBRATIONinfo *calibrationInfo,
                             GUCR_RASTERSTYLE   *hRasterStyle,
                             Bool               fCompositing);

void  cc_destroycalibrationinfo( GS_CALIBRATIONinfo **calibrationInfo ) ;

void  cc_reservecalibrationinfo( GS_CALIBRATIONinfo *calibrationInfo );
int32 cc_arecalibrationobjectslocal(corecontext_t *corecontext,
                                    GS_CALIBRATIONinfo *calibrationInfo );
mps_res_t cc_scan_calibration( mps_ss_t ss,
                               GS_CALIBRATIONinfo *calibrationInfo );

int32 cc_samecalibrationinfo (void * pvGstateInfo, CLINK * pLink);

int32 cc_applyCalibrationInterpolation(USERVALUE value,
                                       USERVALUE *result,
                                       OBJECT *array );

void cc_note_uncalibrated_screens(GS_COLORinfo *colorInfo,
                                  DL_STATE     *page,
                                  int32         spotno);


/* Log stripped */

#endif /* __GSCCALIBPRIV_H__ */
