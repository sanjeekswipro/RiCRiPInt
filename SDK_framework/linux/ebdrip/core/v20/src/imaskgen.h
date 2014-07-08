/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:imaskgen.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Routines to separate the mask and image data from a scanline of data
 * received while processing masked images containing interleaved mask
 * and image data.
 */

#ifndef	__IMASKGEN_H__
#define	__IMASKGEN_H__


/* imask_split_samples:
   Used for Type 3 Masked Images.  Mask channel is written out as
   1-bit, regardless of original mask bit depth.
 */
extern void imask_split_samples( uint8 *sptr, int32 samples,
                                 uint8 *mptr, int32 mbits,
                                 uint8 *iptr, int32 ibits ) ;

/* alpha_split_samples:
   Used for separating alpha channel (up to 16-bit alpha) in PNG and TIFF
   images.
   - alpha_last indicates alpha occurs after the color values (RGBA,RGBA,...).
   - premultiplied indicates color values are premultiplied by the alpha.
 */
extern void alpha_split_samples( Bool alpha_last, Bool premultiplied,
                                 uint8 *sptr, int32 samples,
                                 uint8 *mptr, int32 mbits,
                                 uint8 *iptr, int32 ibits ) ;

#endif  /* !__IMASKGEN_H__ */

/* EOF */

/* Log stripped */
