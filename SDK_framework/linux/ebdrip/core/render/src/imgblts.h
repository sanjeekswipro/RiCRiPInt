/** \file
 * \ingroup images
 *
 * $HopeName: CORErender!src:imgblts.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2010-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image blitter types.
 *
 * These are type definitions that are made available to the image blitters.
 */

#ifndef __IMAGEBLTS_H__
#define __IMAGEBLTS_H__

#include "imtiles.h" /* ROWTILES. */
#include "imexpand.h" /* IM_EXPANDFUNC. */
#include "imagedda.h" /* image_dda_t */
#include "bitbltt.h"
#include "blitcolort.h"
#include "objnamer.h"

struct IMAGEOBJECT ; /* from SWv20/COREdodl */
struct render_blit_t ;
struct ht_params_t; /* from htrender.h */

#ifndef imageinline
/* Allow performance-critical image inlining to be overridden depending on the
   compiler. */
# define imageinline inline
#endif

/** Can we use 1xN pixel expanders? */
Bool can_expand_pixels_1xN(blit_color_t *color,
                           unsigned int nexpanded,
                           const int blit_to_expanded[]) ;

/** Type definition for function that extracts a run of contiguous pixels
    from an image expansion buffer, updating the pointer to the expansion
    buffer and the count of the number of pixels.

    \param[out] color       The blit color entry to be filled in with expander
                            data.
    \param[in,out] buffer   The location of the expansion buffer pointer.
                            On exit, the expansion buffer pointer is updated to
                            point at the next buffer location to be extracted.
    \param[in,out] npixels  A pointer to the number of pixels expanded. On
                            entry, the number of pixels should be set to the
                            maximum number of pixels that should be merged. On
                            exit, the number of pixels is updated to the number
                            of contiguous pixels with the same color value.
    \param nexpanded        The number of channels in the expander buffer.
    \param[in] blit_to_expanded
                            Mapping array from the blit channel indices to
                            expander buffer positions.
*/
typedef void (im_pixel_run_fn)(
        /*@notnull@*/ /*@in@*/ /*@out@*/  struct blit_color_t *color,
        /*@notnull@*/ /*@in@*/ /*@out@*/  const void **buffer,
        /*@notnull@*/ /*@in@*/ /*@out@*/  int32 *npixels,
                                          unsigned int nexpanded,
        /*@notnull@*/ /*@in@*/            const int blit_to_expanded[]) ;

/** Type definition for function that renders a row of a rotated image. On
    entry, the params structure is set up with xedges[0],yedges[0] as the
    row base.

    \param rb         The blit render state.
    \param params     Image blit parameters.
    \param expanded   The expanded image data, set up ready to use the pixel
                      extracter function. (If the image data should be read in
                      reverse order to the render order, this
                      will point just past the end of the data rather than at
                      the start of the data.)
    \param x          The first index in the image row.
    \param ex         The last index in the image row (inclusive).
    \param tiles      An optional pointer to the image tiles.

    \note The condition that determines if the image data is read in the
    opposite direction to the render order depends on \c params->wflip, which
    indicates that image data is stored in the opposite order to the grid
    array, and \c params->order, which determines which order the grid points
    will be read. If both or neither of \c params->order < 0 and
    \c params->wflip are true, the data will be read backwards, otherwise it
    will be read forwards.
*/
typedef void (im_rot_row_fn)(struct render_blit_t *rb,
                             const imgblt_params_t *params,
                             const void *expanded, int32 ncols,
                             const dcoord drx, const dcoord dry,
                             image_dda_t refx, image_dda_t refy,
                             const dbbox_t *inclip);

/** Type definition for function that renders a row of an orthogonal image.

    \param rb         The blit render state.
    \param params     Image blit parameters. The \c params->xedges field is
                      set up so the first entry is the coordinate of the first
                      pixel in the row.
    \param expanded   The expanded image data, set up ready to use the pixel
                      extracter function. (If \c params->wflip is true, this
                      will point just past the end of the data. If
                      \c params->wflip is false, this will point at the start
                      of the data.)
    \param y1         The top coordinate of the image row.
    \param y2         The bottom coordinate of the image row (inclusive).
*/
typedef void (im_orth_row_fn)(struct render_blit_t *rb,
                              const imgblt_params_t *params,
                              const void *expanded,
                              dcoord y1, dcoord y2);

#define IMGBLT_PARAMS_NAME "Image blit parameters"

/** Image blit parameters for rendering, incompletely defined in bitblts.h. */
struct imgblt_params_t {
  enum {
    IM_BLIT_MASK,     /**< Color and blit are setup, alpha is 0/1 */
    IM_BLIT_IMAGE,    /**< Color varies for each pixel, alpha is 1 */
    IM_BLIT_KNOCKOUT  /**< Color and blit are setup, all pixels are same */
  } type ;
  const struct IMAGEOBJECT *image ; /**< The image in question. */
  const struct im_transform_t *geometry ; /**< The image geometry. */
  Bool orthogonal ;        /**< Is this image orthogonal? */
  Bool wflip ;             /**< Are image columns indexed the same order as the grid? */
  Bool hflip ;             /**< Are image rows reversed when reading from store blocks? */
  Bool out16 ;             /**< Is the expansion data in 16-bit chunks? */
  Bool on_the_fly ;        /**< Image colors are converted by expander. */
  Bool one_color_channel ; /**< 1xN optimisations OK. */
  /** HT params if we need to lock halftone, NULL otherwise. */
  struct ht_params_t *ht_params ;
  /** Order in which pixels are rendered along minor axis. This will always
      be +1 for orthogonal images. It will be +1 or -1 for rotated images. */
  int32 order ;
  int32 ncols ;            /**< Number of sample columns. */
  int32 lcol ;             /**< Lowest column sample index. */
  int32 dcol ;     /**< Order in which column samples are extracted (+1/-1). */
  int32 nrows ;            /**< Number of sample rows. */
  int32 irow ;             /**< Initial row sample index */
  int32 drow ;        /**< Order in which row samples are extracted (+1/-1). */
  /** Sign of cross product of major and minor axes. This is used to detect
      whether the roundings for rotated images invert the axes. */
  int32 cross_sign ;

  IMAGETILES *tiles;       /**< Ptr to array of 16 ptrs to tiles. */

  IM_EXPANDFUNC *expand_fn ; /**< Expander function */
  Bool expand_arg ; /**< Multi-purpose parameter for the expand function. */

  /** Image pixel extracter function is preset to a generic function suitable
      for the X rendering order and the image data store order. If the X
      rendering direction is changed, the pixel extracter function must be
      changed. */
  im_pixel_run_fn *pixel_fn ;

  /** Row fill function is preset to a generic function suitable for the X
      rendering direction. If the X rendering direction is changed, the fill
      function must be changed. */
  union {
    im_rot_row_fn *rotated ;
  } fill_fn ;

  image_dda_basis_t basis ;  /**< Shared image DDA basis. */
  image_dda_t xperwh ;       /**< Device space X per Image space W/H. */
  image_dda_t yperhw ;       /**< Device space Y per Image space H/W. */
  image_dda_t xs, ys ;       /**< Device space row/column X, Y start. */
  /* The next four DDA steps are used for rotated images. The negated steps
     are for convenience when adjusting rotated row boundaries. */
  image_dda_t yperwh ;       /**< Device space Y per Image space W/H. */
  image_dda_t xperhw ;       /**< Device space X per Image space H/W. */
  image_dda_t nxperw ;       /**< Negated device space X per Image space W. */
  image_dda_t nyperw ;       /**< Negated device space Y per Image space H. */

  /** Number of expanded components, and converted components. These will only
      differ if converting an image on the fly for one-pass compositing. */
  unsigned int expanded_comps, converted_comps;
  /** Map from blit color order to expander channels. */
  int blit_to_expanded[BLIT_MAX_CHANNELS] ;
  /** Map from expander order to image store plane order. */
  int expanded_to_plane[BLIT_MAX_CHANNELS] ;

  OBJECT_NAME_MEMBER
} ;

#endif

/* Log stripped */
