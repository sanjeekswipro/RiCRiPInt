/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!export:runlen.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Run Length filter encode and decode routines
 */

#ifndef __RUNLEN_H__
#define __RUNLEN_H__


/** \addtogroup filters */
/** \{ */

/*  runlen.h   Created 20:04:93, Dave Emmerson, as part of spring clean */

#define EOD 128
#define RUNLENGTHBUFFSIZE 1024

void runlen_encode_filter(FILELIST *flptr) ;
void runlen_decode_filter(FILELIST *flptr) ;

#endif /* protection for multiple inclusion */

/** \} */

/* Log stripped */
