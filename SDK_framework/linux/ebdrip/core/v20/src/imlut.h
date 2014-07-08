/** \file
 * \ingroup images
 *
 * $HopeName: SWv20!src:imlut.h(EBDSDK_P.1) $
 * $Id: src:imlut.h,v 1.4.2.1.1.1 2013/12/19 11:25:25 anon Exp $
 *
 * Copyright (C) 2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 *
 * imlut - the image LUT cache
 */

#ifndef __IMLUT_H__
#define __IMLUT_H__

#include "displayt.h" /* DL_STATE */
#include "mm.h" /* mm_size_t */
#include "graphict.h" /* GS_COLORinfo */

typedef struct imlut_t imlut_t;

void imlut_destroy(DL_STATE *page);

Bool imlut_lookup(GS_COLORinfo *colorInfo, int32 colorType,
                  float *decode_array, void *decode_for_adjust,
                  int32 ncomps, int32 bpp, Bool expanded,
                  uint8 ***lut);

void imlut_add(DL_STATE *page,
               GS_COLORinfo *colorInfo, int32 colorType,
               float *decode_array, void *decode_for_adjust,
               int32 ncomps, int32 bpp, Bool expanded,
               uint8 **lut, Bool *added);

Bool imlut_convert(GS_COLORinfo *colorInfo, int32 colorType);

#endif /* protection for multiple inclusion */

/* Log stripped */
