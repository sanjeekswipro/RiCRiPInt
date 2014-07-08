/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:imageo.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image objects
 */

#ifndef __IMAGEO_H__
#define __IMAGEO_H__

#include "imaget.h"
#include "imtiles.h" /* IMAGETILES */
#include "displayt.h" /* SpotList */
#include "ndisplay.h" /* NFILLOBJECT */
#include "matrix.h"   /* OMATRIX */


struct IM_STORE;
struct IM_EXPAND;

/** Display list image object. */
struct IMAGEOBJECT {
  dbbox_t bbox ;       /**< Clipped device-space bbox. */
  ibbox_t imsbbox ;    /**< Image-space bbox, after X-Y swapping. */

  im_transform_t geometry ;    /**< Image transform. */

  uint8 optimize ;             /**< Matrix information flags. */

  uint8 sixteenbit_output ;    /**< TRUE if exp buff uses 16 bits per sample. */

  uint8 flags;                 /**< IM_FLAG_ ... */

  IMAGETILES *tiles;           /**< ptr to array of 16 ptrs to tiles. */

  IMAGEOBJECT *mask;           /**< for imagetype == TypeImageMasked. */

  struct IM_STORE *ims ;
  struct IM_EXPAND *ime ;

  /* cv_[f|n]decodes arrays are used to convert from values from the
     COLORVALUE type to color chain input values. */
  uint32 alloc_ncomps;         /**< ncomps allocated for two following arrays */
  float **cv_fdecodes;         /**< Decode array for to map to floats. */
  int32 **cv_ndecodes;         /**< Decode array for Tom's Tables conversion. */

  struct {
    OMATRIX omatrix ;          /**< Image matrix. */
    NFILLOBJECT *nfillm ;      /**< Outline to be used for matching. */
    union {
      int32 adler32 ;          /**< Checksum of imagemask data used for exact/fuzzy matches. */
      int32 nplanes ;          /**< Number of planes of image data merged. */
    } u ;
  } rcb ;

  IMAGEOBJECT *next ;         /**< Double linked list of all active images. */
  IMAGEOBJECT *prev ;

  void  *base_addr ;          /**< Used to free the memory for the object. */
} ;

#define IM_GOTALLPLANES MAXINT32

enum im_flags {
  IM_FLAG_MAJ_DEGEN = 0x01, /**< Is image degenerate in the major axis? */
  IM_FLAG_MIN_DEGEN = 0x02, /**< Is image degenerate in the minor axis? */
  IM_FLAG_PRESEP = 0x04, /**< Is image preseparated (when recombining)? */
  IM_FLAG_PRESEP_KO_PROCESS = 0x08, /**< KOs required in all process colorants. */
  IM_FLAG_COMPOSITE_ALPHA_CHANNEL = 0x10 /**< Image has an alpha channel and must be composited. */
} ;

/* Image lookup table component flags for 1-bit images; these are shifted by
   the process colour component index (0-3). */
#define LUT_SOLID       0x01
#define LUT_INVERTED    0x10    /* reverse polarity of the image */

/* access macros: */
#define theIOptimize(val)      ((val)->optimize)
#define theINPlanes(val)       ((val)->rcb.u.nplanes)
#define theIAdler32(val)       ((val)->rcb.u.adler32)


/* Log stripped */

 #endif /* __IMAGEO_H__ */
