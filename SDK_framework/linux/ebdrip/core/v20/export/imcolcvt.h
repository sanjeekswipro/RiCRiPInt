/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:imcolcvt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Code for unpacking input data, interleaving it, applying decode arrays
 * and color converting resultant pixels.
 */

#ifndef __IMCOLCVT_H__
#define __IMCOLCVT_H__

/* imcolcvt.h */

#include "dl_color.h"
#include "mm.h" /* required since mm_pool_t is an opaque struct */

/**< Enum types indicating method of color-conversion. */
enum {
  GSC_NO_EXTERNAL_METHOD_CHOICE     = -1,
  GSC_USE_PS_PROC                   = 0,
  GSC_USE_INTERPOLATION             = 1,
  GSC_USE_FASTRGB2GRAY              = 2,
  GSC_QUANTISE_ONLY                 = 3,
  GSC_USE_FASTRGB2CMYK              = 4
};

typedef struct IM_COLCVT IM_COLCVT ;

/**
 * Make an image color-convert object to be used for front-end or back-end image
 * color conversion.  justDecode indicates doing an alpha channel, or just want
 * the result of applying the decodes rather than a full image color conversion.
 */
IM_COLCVT *im_colcvtopen( GS_COLORinfo *colorInfo , mm_pool_t *pools ,
                          int32 *ndecodes[] , int32 incomps , int32 nconv ,
                          Bool out16 , int32 method , int32 colorType ,
                          Bool justDecode ) ;

void im_colcvtfree( IM_COLCVT *imc ) ;

/**
 * Applies image decodes to unpacked image values in ubuf and color-converts
 * data into rbuf.
 */
Bool im_colcvt( IM_COLCVT *imc , int32 *ubuf , uint8 **rbuf ) ;

void im_coldecode( IM_COLCVT *imc , int32 *ubuf ) ;

/**
 * A variation on im_colcvt() for image filtering.
 */
Bool im_colcvt_n( IM_COLCVT *imc , int32 *ubuf , uint8 **rbuf , uint32 nsamples ) ;

void im_decode( int32 *ndecodes[] ,
                int32 *unpack , int32 *decode ,
                int32 incomps , int32 nconv ) ;


#endif /* protection for multiple inclusion */

/* Log stripped */
