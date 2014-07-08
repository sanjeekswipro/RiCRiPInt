/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!export:gscfastrgb2gray.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Fast RGB to gray conversion.
 */

#ifndef __GSCFASTRGB2GRAY_H__
#define __GSCFASTRGB2GRAY_H__

#include "graphict.h" /* GS_COLORinfo */

/* API for the fast path rgb to gray color conversion.
 * To use the API, initialize the internal LUTs by calling gsc_fastrgb2gray_prep
 * then convert colors with gsc_fastrgb2gray_do. gsc_fastrgb2gray_do must only
 * be invoked on a colorInfo/colorType pair previously subject to a call to
 * gsc_fastrgb2gray_prep.
 * The fast path is intended for use with images, hence the decode arrays passed
 * as parameters to the prep function. These are the decode arrays calculated
 * for the imagedata.
 * gsc_fastrgb2gray_prep must be called for each individual image to which the
 * fast path conversion is to apply.
 * The fast path can deal with images up to 12 bits per component.
 * The precision of color values used in the construction of LUTs is
 * given below. 12 bits is plenty sufficient for mono output.
 *
 */

Bool gsc_fastrgb2gray_prep(GS_COLORinfo *colorInfo, int32 colorType,
                           uint32 bpc, float *r_decode, float *b_decode,
                           float *g_decode);

Bool gsc_fastrgb2gray_do(GS_COLORinfo *colorInfo, int32 colorType,
                         int32 *piColorValues, COLORVALUE *poColorValues,
                         int32 ncolors);

Bool gsc_fastrgb2cmyk_prep(GS_COLORinfo *colorInfo, uint32 bpc,
                           COLORVALUE *r_decode, COLORVALUE *b_decode,
                           COLORVALUE *g_decode);

Bool gsc_fastrgb2cmyk_do(GS_COLORinfo *colorInfo, int32 colorType,
                         int32 *piColorValues, COLORVALUE *poColorValues,
                         int32 ncolors);

/* Log stripped */

#endif /* __GSCFASTRGB2GRAY_H__ */
