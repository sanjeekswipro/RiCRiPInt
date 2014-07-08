/* Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWptdev!src:ptincs.h(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */
#ifndef __PTINCS_H__
#define __PTINCS_H__  (1)

/**
 * @file
 * @brief Selection of headers for types, memory handling, etc.
 */

/* types */
#include "std.h"

#ifdef INRIP_PTDEV

/* Building as part of Harlequin RIP SDK */

/* memory allocation - skinkit's mem.h */
#include "mem.h"

/* string utilities */
#include "hqnstrutils.h"

#else

/* Building as part of a plugin */

#include <limits.h>

/* memory allocation - use PFI functions */
#include "pfi.h"

#define MemAlloc( _size_, _fZero_, _fExitOnFail_ ) \
  PlgFwMemAlloc( _size_, \
    ( _fZero_ ? PLGFWMEM_INIT_ZEROED : PLGFWMEM_INIT_UNSPECIFIED ) \
    | PLGFWMEM_ALLOC_FAIL_NULL )

#define MemFree( _pbMem_ ) \
  PlgFwMemFree( _pbMem_ )

/* string utilities, to match required parts of hqnstrutils.h */
extern double strToDouble(char* pStr, char** ppEndStr);

#define utl_strdup( _s_ ) \
  PlgFwStrDuplicate( (PlgFwTextString) (_s_) )

#endif /* ! INRIP_PTDEV */

#endif /* !__PTINCS_H__ */


/* EOF ptincs.h */
