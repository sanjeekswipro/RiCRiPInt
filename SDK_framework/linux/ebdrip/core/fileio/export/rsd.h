/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!export:rsd.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Reusable Stream Decode (RSD) filter routines
 */

#ifndef __RSD_H__
#define __RSD_H__

#include "fileioh.h" /* FILELIST */

/** \addtogroup filters */
/** \{ */

void rsdSetCircularFlag( FILELIST *filter , int32 value ) ;

void rsd_decode_filter(FILELIST *filter) ;

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
