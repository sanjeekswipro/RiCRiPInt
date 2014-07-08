/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pclutils.c(EBDSDK_P.1) $
 * $Id: src:pclutils.c,v 1.5.4.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Utility functions for PCL interpreter.
 */

#include "core.h"
#include "pclutils.h"
#include "swerrors.h"

/* See header for doc. */
Bool pcl_error_return(struct error_context_t *errcontext)
{
  if ( error_latest_context(errcontext) == VMERROR ) {
    return FALSE;
  }
  error_clear_context(errcontext);
  return TRUE;
}

/*
* Log stripped */

/* eof */
