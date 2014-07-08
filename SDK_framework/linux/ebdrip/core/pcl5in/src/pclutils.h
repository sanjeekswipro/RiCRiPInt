/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pclutils.h(EBDSDK_P.1) $
 * $Id: src:pclutils.h,v 1.5.4.1.1.1 2013/12/19 11:25:02 anon Exp $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Utility functions for PCL interpreter.
 */

#ifndef __PCLUTILS_H__
#define __PCLUTILS_H__  (1)

/* Return int32 of unsigned short from array of any type */
#define READ_SHORT(p)   (((p)[0] << 8) | ((p)[1]))

/**
 * Handle a PS error for PCL. If the error was a VMERROR, FALSE is returned,
 * otherwise the error is cleared and TRUE is returned.
 */
Bool pcl_error_return(struct error_context_t *errcontext);

#endif

/*
* Log stripped */

/* eof */
