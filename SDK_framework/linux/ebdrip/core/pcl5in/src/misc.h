/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:misc.h(EBDSDK_P.1) $
 * $Id: src:misc.h,v 1.13.4.1.1.1 2013/12/19 11:25:02 anon Exp $
 *
 * Copyright (C) 2007-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the PCL5 "Misc" category.
 *
 * I have dropped a pile of operator callbacks into here as a place
 * holder.
 */

#ifndef __MISC_H__
#define __MISC_H__

#include "pcl5context.h"

/* Be sure to update default_trouble_shooting_info() if you change this structure. */
typedef struct TroubleShootingInfo {
  /* Explicitly required */
  Bool         line_wrap;
  Bool         display_functions;
} TroubleShootingInfo;

TroubleShootingInfo* get_trouble_shooting_info(PCL5Context *pcl5_ctxt) ;

/**
 * Initialise default trouble-shooting info.
 */
void default_trouble_shooting_info(TroubleShootingInfo* self);

/* ============================================================================
 * Operator callbacks are below here.
 * ============================================================================
 */
Bool pcl5op_ampersand_s_C(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_ampersand_t_J(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

/* ============================================================================
* Log stripped */

#endif
