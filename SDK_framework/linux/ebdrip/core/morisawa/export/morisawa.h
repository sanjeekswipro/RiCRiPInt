/** \file
 * \ingroup morisawa
 *
 * $HopeName: SWmorisawa!export:morisawa.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions for decrypting Morisawa fonts. This is a separately-compiled part
 * of the Core RIP. Two functions are exported: MPS_decrypt() converts a
 * Morisawa-encrypted charstring to a normal T1 encoded charstring. Non Morisawa
 * customers get a dummy version of this functions which does nothing.
 * MPS_supported() returns a value indicating if Morisawa fonts are supported by
 * this RIP.
 */

#ifndef __MORISAWA_H__
#define __MORISAWA_H__

/** \defgroup morisawa Morisawa MPS font decryption.
    \ingroup crypt
    \{ */

Bool MPS_decrypt(uint8 *from, int32 len, uint8 *to) ;
Bool MPS_supported(void) ;

struct core_init_fns ; /* from SWcore */
void morisawa_C_globals(struct core_init_fns *fns) ;

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
