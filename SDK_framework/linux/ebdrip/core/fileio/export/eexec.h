/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!export:eexec.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * eexec encode and decode filter routines
 */

#ifndef __EEXEC_H__
#define __EEXEC_H__


/* Decryption constants. */
#define EEXEC_SEED 55665
#define EEXEC_ADD  22719
#define EEXEC_MULT 52845

void eexec_encode_filter(FILELIST *flptr) ;
void eexec_decode_filter(FILELIST *flptr) ;

/*
Log stripped */
#endif /* protection for multiple inclusion */
