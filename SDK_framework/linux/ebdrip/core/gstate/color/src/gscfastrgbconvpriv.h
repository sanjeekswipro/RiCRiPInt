/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gscfastrgb2graypriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Fast RGB to gray conversion.
 */

#ifndef __GSCFASTRGBCONVPRIV_H__
#define __GSCFASTRGBCONVPRIV_H__

#include "graphict.h" /* GS_COLORinfo */

typedef struct GS_FASTRGB2GRAY_STATE GS_FASTRGB2GRAY_STATE;
typedef struct GS_FASTRGB2CMYK_STATE GS_FASTRGB2CMYK_STATE;

/* Fast rgb to gray color conversion for images requires a LUT to
 * emulate the device code link of a (RGB->Gray ) color chain.
 * The table maps scaled, integer gray color values to device codes.
 * Size of this table will be dependent on the precision choosen for
 * the color conversion.
 */
typedef struct FASTRGB2GRAY_LUT {
  GS_FASTRGB2GRAY_STATE *fastrgb2grayState;
  COLORVALUE *devCodes;
} FASTRGB2GRAY_LUT;

Bool cc_fastrgb2gray_create(GS_FASTRGB2GRAY_STATE **stateRef);

void cc_fastrgb2gray_destroy(GS_FASTRGB2GRAY_STATE **stateRef);

Bool cc_fastrgb2cmyk_create(GS_FASTRGB2CMYK_STATE **stateRef);

void cc_fastrgb2cmyk_destroy(GS_FASTRGB2CMYK_STATE **stateRef);

void cc_fastrgb2gray_freelut(FASTRGB2GRAY_LUT *dev_code);

/* Log stripped */

#endif /* __GSCFASTRGBCONVPRIV_H__ */
