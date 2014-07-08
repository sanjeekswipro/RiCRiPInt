/** \file
 * \ingroup filters
 *
 * $HopeName: SWv20!src:procfilt.h(EBDSDK_P.1) $
 * $Id: src:procfilt.h,v 1.5.10.1.1.1 2013/12/19 11:25:22 anon Exp $
 *
 * Copyright (C) 2001-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Standard procedure filter routines
 */

#ifndef __PROCFILT_H__
#define __PROCFILT_H__

/* Save/restore targets */
int32 checkValidProcFilters(int32 slevel) ;

void procedure_encode_filter(FILELIST *flptr) ;
void procedure_decode_filter(FILELIST *flptr) ;

/*
Log stripped */
#endif /* Protection from multiple inclusion */
