/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:jobcontrol.h(EBDSDK_P.1) $
 * $Id: src:jobcontrol.h,v 1.23.4.1.1.1 2013/12/19 11:25:02 anon Exp $
 *
 * Copyright (C) 2007-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

#ifndef __JOBCONTROL_H__
#define __JOBCONTROL_H__

#include "pcl5context.h"

/* Job Control Environment Features (PCL) */
/* Be sure to update default_job_control() if you change this structure. */
typedef struct JobCtrlInfo {
  /* Explicitly required */
  uint32       num_copies;
  Bool         duplex;
  uint32       binding;
  /* uint32       tray_lock;       N.B. no command found */
  Bool         job_separation;
  /* Bool         manual_feed;     N.B. no command found unless from paper source */
  PCL5Real     left_registration;
  PCL5Real     top_registration;
  uint32       output_bin;

  /* Extra settings */
  Bool         requested_duplex;
  uint32       requested_binding;
} JobCtrlInfo;

/* Get hold of the JobCtrlInfo */
JobCtrlInfo* get_job_ctrl_info(PCL5Context *pcl5_ctxt) ;

/* Initialise, save and restore default job control. */
void default_job_control(JobCtrlInfo* self, PCL5ConfigParams* config_params);
Bool jobcontrol_apply_pjl_changes(PCL5Context *pcl5_ctxt, PCL5ConfigParams* config_params) ;
void save_job_control(PCL5Context *pcl5_ctxt, JobCtrlInfo *to, JobCtrlInfo *from, Bool overlay) ;
void restore_job_control(PCL5Context *pcl5_ctxt, JobCtrlInfo *to, JobCtrlInfo *from) ;

/* Set the required number of copies in userdict */
Bool set_ps_num_copies(PCL5Context *pcl5_ctxt) ;

/* ============================================================================
 * Operator callbacks are below here.
 * ============================================================================
 */

/* ESC % X (Its likely to be a UEL Command) */
Bool pcl5op_percent_X(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_ampersand_b_W(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_E(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_ampersand_l_X(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_ampersand_l_S(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_ampersand_l_U(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_ampersand_l_Z(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_ampersand_a_G(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_ampersand_l_T(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_ampersand_l_G(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

Bool pcl5op_ampersand_u_D(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

/* ============================================================================
* Log stripped */
#endif
