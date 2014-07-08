/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:areafill.h(EBDSDK_P.1) $
 * $Id: src:areafill.h,v 1.9.6.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

#ifndef __AREAFILL_H__
#define __AREAFILL_H__

#include "pcl5context.h"

/**
 * State required for the rectangular area fill command.
 */
typedef struct {
  /* The dimensions of the area fill. */
  HPGL2Point size;
} AreaFillInfo;

/**
 * Initialise default environment.
 */
void default_area_fill(AreaFillInfo* self);

Bool pcl5op_star_c_A(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_c_B(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_c_G(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_c_H(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_c_P(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_c_V(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

/* ============================================================================
* Log stripped */

#endif
