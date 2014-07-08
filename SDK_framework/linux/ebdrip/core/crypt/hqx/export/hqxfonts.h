/** \file
 * \ingroup hqx
 *
 * $HopeName: COREcrypt!hqx:export:hqxfonts.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Encryption program for RIP-ID keyed composite fonts.
 */

#ifndef __HQXFONTS_H__
#define __HQXFONTS_H__


/* --- Macro Definitions --- */

/* Max possible number of keys to try */
#define FONT_MAX_CHECK_KEYS 3  /* Cust#, rip#, generic */

/* --- Exported Functions --- */
Bool hqxNonPSSetup( FILELIST *flptr );
int32 hqxfont_test( DEVICE_FILEDESCRIPTOR fd , DEVICELIST *dev );

/* --- Exported Variables --- */
extern uint32 HqxDepth;         /* nested depth, for "are we encrypted?" */

#endif /* protection for multiple inclusion */

/* Log stripped */
