/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:status.h(EBDSDK_P.1) $
 * $Id: src:status.h,v 1.10.6.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

#ifndef __STATUS_H__
#define __STATUS_H__

#include "pcl5context.h"

/* Be sure to update default_status_readback_info() if you change this structure. */
typedef struct StatusReadBackInfo {
  /* Explicitly required */
  uint32       location_type;
  uint32       location_unit;
} StatusReadBackInfo;

/**
 * Initialise default status readback info.
 */
void default_status_readback_info(StatusReadBackInfo* self);

extern
StatusReadBackInfo* pcl5_get_status_readback(
  PCL5Context*  pcl5_ctxt);

Bool pcl5op_ampersand_r_F(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_s_I(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_s_M(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_s_T(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_s_U(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_s_X(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

/* ============================================================================
* Log stripped */
#endif
