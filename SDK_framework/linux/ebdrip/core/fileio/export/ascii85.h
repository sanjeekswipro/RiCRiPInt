/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!export:ascii85.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * ASCII85 filter routines and defines
 */

#ifndef __ASCII85_H__
#define __ASCII85_H__


/* ----------------------------------------------------------------------------
   file:                ASCII 85          author:              Luke Tunmer
   creation date:       14-Aug-1991       last modification:   ##-###-####

---------------------------------------------------------------------------- */

/* This file is required by both the ascii85 filter, and the ascii85
 * scanner code.
 */

#define POWER4 52200625
#define POWER3 614125
#define POWER2 7225
#define POWER1 85
#define MAXHIGH4BYTES 1377252876

#ifdef highbytefirst
#define BYTE_INDEX( i )  ( i )
#else
#define BYTE_INDEX( i )  ( 3 - ( i ) )
#endif

/** \addtogroup filters */
/** \{ */

void ascii85_encode_filter(FILELIST *flptr) ;
void ascii85_decode_filter(FILELIST *flptr) ;

/** \} */

#endif /* protection for multiple inclusion */


/* Log stripped */
