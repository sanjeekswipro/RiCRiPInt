/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:misc.c(EBDSDK_P.1) $
 * $Id: src:misc.c,v 1.27.4.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the PCL5 "Misc" category.
 *
 * I have dropped a pile of operator callbacks into here as a place
 * holder.
 */

#include "core.h"
#include "misc.h"

#include "pcl5context_private.h"
#include "printenvironment_private.h"
#include "pcl5scan.h"
#include "macros.h"
#include "pclutils.h"

#include "fileio.h"
#include "monitor.h"


/** \todo TODO Should this be here or provided by PCL5PrintState handling? */
TroubleShootingInfo* get_trouble_shooting_info(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *print_state ;
  PCL5PrintEnvironment *mpe ;
  TroubleShootingInfo *trouble_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  mpe = print_state->mpe ;
  HQASSERT(mpe != NULL, "mpe is NULL") ;

  trouble_info = &(mpe->trouble_shooting) ;

  return trouble_info ;
}

/* See header for doc. */
void default_trouble_shooting_info(TroubleShootingInfo* self)
{
  self->line_wrap = FALSE;
  self->display_functions = FALSE;
}

/* ============================================================================
 * Operator callbacks are below here.
 * ============================================================================
 */

/* Line Wrap */
Bool pcl5op_ampersand_s_C(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  TroubleShootingInfo *trouble_info ;

  UNUSED_PARAM(int32, explicit_sign) ;

  value.integer = abs(value.integer);
  if ( (value.integer != 0) && (value.integer != 1) ) {
    return(TRUE);
  }

  trouble_info = get_trouble_shooting_info(pcl5_ctxt) ;
  HQASSERT(trouble_info != NULL, "trouble_info is NULL") ;

  /* If it is not definitely on, then it is off!  CET 34_02.bin */
  trouble_info->line_wrap = (value.integer == 0);

  return TRUE ;
}

/* Render algorithm */
Bool pcl5op_ampersand_t_J(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  FILELIST *flptr ;

  UNUSED_PARAM(int32, explicit_sign) ;
  UNUSED_PARAM(PCL5Numeric, value) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  flptr = pcl5_ctxt->flptr ;
  HQASSERT(flptr != NULL, "flptr is NULL") ;

  return TRUE ;
}


/* ============================================================================
* Log stripped */
