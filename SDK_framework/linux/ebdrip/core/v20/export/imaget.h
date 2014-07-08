/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:imaget.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Incomplete typedefs for image structures.
 */

#ifndef __IMAGET_H__
#define __IMAGET_H__

/* Do NOT include any other headers, or make this file depend on any other
   headers */
typedef struct IMAGEARGS IMAGEARGS ;
typedef struct IM_BUFFER IM_BUFFER ;
typedef struct IMAGEDATA IMAGEDATA ;

/* Describe image type by functionality rather than P.S. operator used */
enum {
  TypeImageMask        = 0x00, /**< imagemask */
  TypeImageImage       = 0x01, /**< image + /ImageType 1 or colorimage */
  TypeImageMaskedImage = 0x02, /**< image + /ImageType 3 or 4 (image) */
  TypeImageMaskedMask  = 0x03, /**< image + /ImageType 3 or 4 (mask) */
  TypeImageAlphaImage  = 0x04, /**< image + /ImageType 12 (image) */
  TypeImageAlphaAlpha  = 0x05  /**< image + /ImageType 12 (alpha channel) */
} ;

#define VALID_IMAGETYPE(t) (     \
  (t) == TypeImageMask ||        \
  (t) == TypeImageImage ||       \
  (t) == TypeImageMaskedImage || \
  (t) == TypeImageMaskedMask ||  \
  (t) == TypeImageAlphaImage ||  \
  (t) == TypeImageAlphaAlpha     \
)

/** Image optimise bits. These indicate whether the X and Y are swapped,
    whether it's top to bottom or vice versa, left to right or vice versa,
    rotated, at device resolution, etc. */
enum {
  IMAGE_OPTIMISE_NONE    = 0,    /**< Plain value for case statements */
  IMAGE_OPTIMISE_SWAP    = 0x01, /**< X and Y swapped */
  IMAGE_OPTIMISE_YFLIP   = 0x02, /**< Read rows backwards from store. */
  IMAGE_OPTIMISE_XFLIP   = 0x04, /**< Read columns backwards from store. */
  IMAGE_OPTIMISE_ROTATED = 0x08, /**< Image is not orthogonal */
  IMAGE_OPTIMISE_1TO1    = 0x80, /**< 1-bit copy+shift from expander to raster. */
  IMAGE_OPTIMISE_SWAP4SPEED = 0x100 /**< Swap to get longer image rows. */
} ;

/** An integral type with enough precision to compute device-coordinate cross
    products for images. If we have 64-bit integer arithmetic, perform matrix
    calculations using it. Otherwise use double to get 52-bit mantissa and
    one extra implicit bit (with exact integer representations up to
    2^53). */
#if HQN_INT64_OP_MULDIV
typedef int64 im_dcross_t ;
#define IM_DCROSS_MAX MAXINT64 /**< Max exact integer. */
/**< Min exact integer. This is -MAXINT64 because it can be negated due to
   division by -1 in assertions, which may generate an overflow exception. */
#define IM_DCROSS_MIN (-MAXINT64)
#else
typedef double im_dcross_t ;
#define IM_DCROSS_MAX 9007199254740992.0  /**< Max exact integer. */
#define IM_DCROSS_MIN -9007199254740992.0 /**< Min exact integer. */
#endif

/** \brief Image device-space transformation matrix.

    This is used to store the optimised, device-space matrix for an image.
    The matrix normalised so that the major axis is steeper than the minor
    axis. The major axis corresponds to the image space height, and the minor
    axis corresponds to the image space width. The device-space axes stored
    correspond to the full width and height of the image, so we also store
    the width and height explicitly.

    We also store the minor/major axis cross product, and its sign
    separately, because these are used in transformation between device and
    image space, and degeneracy testing. */
typedef struct im_transform_t {
  int32 w, h ;        /**< Image-space width and height of image. */
  dcoord wx, wy ;     /**< Minor (width) axis; m(0,0) and m(0,1) of matrix. */
  dcoord hx, hy ;     /**< Major (height) axis; m(1,0) and m(1,1) of matrix. */
  dcoord tx, ty ;     /**< m(2,0) and m(2,1) of matrix. */
  dbbox_t extent ;    /**< Unclipped minimal device space extent of image. */
  im_dcross_t cross ; /**< Cross product wx * hy - hx * wy. */
  int cross_sign ;    /**< Sign of cross product. */
} im_transform_t ;

/** Check that an image device-space matrix is normalised. */
#define IM_TRANSFORM_ASSERTS(_transform) MACRO_START \
  dbbox_t _dbbox_ ; \
  HQASSERT((_transform) != NULL, "No image transform") ; \
  HQASSERT((_transform)->w > 0, "Image width should not be degenerate") ; \
  HQASSERT((_transform)->h > 0, "Image height should not be degenerate") ; \
  _dbbox_ = (_transform)->extent ; \
  _dbbox_.x2 += 1 ; _dbbox_.y2 += 1 ; /* Convert to exclusive range */ \
  HQASSERT(bbox_contains_point(&_dbbox_, \
                               (_transform)->tx, (_transform)->ty), \
           "Image translation point not inside extent") ; \
  HQASSERT(bbox_contains_point(&_dbbox_, \
                               (_transform)->tx + (_transform)->wx, \
                               (_transform)->ty + (_transform)->wy), \
           "Image minor axis end not inside extent") ; \
  HQASSERT(bbox_contains_point(&_dbbox_, \
                               (_transform)->tx + (_transform)->hx, \
                               (_transform)->ty + (_transform)->hy), \
           "Image major axis end not inside extent") ; \
  HQASSERT(bbox_contains_point(&_dbbox_, \
                               (_transform)->tx + (_transform)->wx + (_transform)->hx, \
                               (_transform)->ty + (_transform)->wy + (_transform)->hy), \
           "Image anti-translation point not inside extent") ; \
  HQASSERT((im_dcross_t)(_transform)->wx * (im_dcross_t)(_transform)->hy - \
           (im_dcross_t)(_transform)->hx * (im_dcross_t)(_transform)->wy == \
           (_transform)->cross, "Image cross product wrong") ; \
  HQASSERT((_transform)->cross < 0 ? (_transform)->cross_sign == -1 : \
           (_transform)->cross > 0 ? (_transform)->cross_sign == 1 : \
           (_transform)->cross_sign == 0, "Image cross product sign wrong") ; \
MACRO_END


/* Log stripped */
#endif /* protection for multiple inclusion */
