/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!export:gsccalib.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Calibration color link API.
 */

#ifndef __GSCCALIB_H__
#define __GSCCALIB_H__


#include "objecth.h"

Bool gsc_setcalibration(GS_COLORinfo *colorInfo, OBJECT calibObj);
Bool gsc_getcalibration(GS_COLORinfo *colorInfo, STACK *stack);

Bool gsc_validateCalibrationArray(OBJECT *arrayObject);

Bool gsc_report_uncalibrated_screens(GS_COLORinfo *colorInfo,
                                     DL_STATE     *page);

#endif /* __GSCCALIB_H__ */

/* Log stripped */
