/** \file
 * \ingroup halftone
 *
 * $HopeName: COREhalftone!src:htblits.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Halftone form blitting routines.
 */

#ifndef __HTBLITS_H__
#define __HTBLITS_H__ 1

#include "bitbltt.h"  /* FORM */
#include "htrender.h" /* ht_params_t */

void set_cell_bits(FORM *formptr, const ht_params_t *ht_params,
                   int16 *xptr, int16 *yptr, int32 n,
                   int32 R1, int32 R2, int32 R3, int32 R4,
                   int32 *ys, int32 depth,
                   int32 scms, Bool dot_centered,
                   Bool accurate, Bool thresholdScreen);

void bitexpandform( const ht_params_t *ht_params,
                    int32 mxdims, int32 mydims, FORM *newform );

#endif /* protection for multiple inclusion */

/* Log stripped */
