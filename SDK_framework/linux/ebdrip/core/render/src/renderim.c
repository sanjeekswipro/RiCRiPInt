/** \file
 * \ingroup renderloop
 *
 * $HopeName: CORErender!src:renderim.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Rendering functions for images.
 */

#include "core.h"

#include "renderim.h"
#include "render.h"     /* outputpage */

#include "often.h"

#include "scanconv.h"
#include "scpriv.h"
#include "spanlist.h"
#include "hqbitops.h"

#include "bitblts.h"
#include "bitblth.h"
#include "blttables.h"
#include "blitcolorh.h"
#include "blitcolors.h"
#include "display.h"
#include "control.h"
#include "pixelLabels.h"
#include "diamondfill.h"
#include "renderfn.h"
#include "dl_image.h"
#include "group.h"
#include "gu_chan.h"
#include "imgblts.h"
#include "dl_bres.h"
#include "htrender.h" /* GET_FORM */
#include "converge.h"
#include "toneblt.h"
#include "rlecache.h"
#include "imageo.h" /* IMAGEOBJECT */
#include "dl_color.h"   /* COLORVALUE_TRANSPARENT */
#include "trap.h"
#include "interrupts.h"
#include "swerrors.h"
#ifdef DEBUG_BUILD
/* Pseudo-metrics counting how many rotated image lines we presented for
   rendering when they were completely clipped out, and the maximum number of
   consecutive rotated image lines that were completely clipped out. */
int32 clipped_rotated_lines = 0 ;
int32 max_clipped_rotated_lines = 0 ;
#endif

/* ---------------------------------------------------------------------- */
/* static prototypes */

static im_rot_row_fn rfill;
static im_rot_row_fn rfill_tiled;

static im_rot_row_fn rmask;
static im_rot_row_fn rmask_tiled;

/* Variants of row-fill functions for orthogonal images, optimised for number
   of channels, image depth, output type, and forward/backward iteration.
 */

static inline im_orth_row_fn mask_orth_generic_row;
static inline im_orth_row_fn mask_orth_generic_col;

/* ---------------------------------------------------------------------- */

static inline im_pixel_run_fn pixels_generic_knockout ;
static inline im_pixel_run_fn pixels_generic_1x8_forward ;
static inline im_pixel_run_fn pixels_generic_Nx8_forward ;
static inline im_pixel_run_fn pixels_generic_1x16_forward ;
static inline im_pixel_run_fn pixels_generic_Nx16_forward ;
static inline im_pixel_run_fn pixels_generic_1x8_backward ;
static inline im_pixel_run_fn pixels_generic_Nx8_backward ;
static inline im_pixel_run_fn pixels_generic_1x16_backward ;
static inline im_pixel_run_fn pixels_generic_Nx16_backward ;
static inline im_pixel_run_fn pixels_mask_forward ;
static inline im_pixel_run_fn pixels_mask_backward ;

static im_pixel_run_fn *const pixel_functions[2][2][2] = {
  { /* 8-bit image */
    { /* forwards */
      pixels_generic_Nx8_forward, pixels_generic_1x8_forward
    },
    { /* backwards */
      pixels_generic_Nx8_backward, pixels_generic_1x8_backward
    }
  },
  { /* 16-bit image */
    { /* forwards */
      pixels_generic_Nx16_forward, pixels_generic_1x16_forward
    },
    { /* backwards */
      pixels_generic_Nx16_backward, pixels_generic_1x16_backward
    }
  }
} ;

/* ---------------------------------------------------------------------- */
/** \fn image_orth_generic_row
    Generic row fill function for orthogonal images. */
#ifndef DOXYGEN_SKIP
#define FUNCTION image_orth_generic_row
#define PIXEL_FN(params) (params->pixel_fn)
#include "imgfillorthrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn image_orth_generic_rows
   Definition of the generic orthogonal line function; this function calls
   the expander defined in the image params. */
#ifndef DOXYGEN_SKIP
#define FUNCTION image_orth_generic_rows
#define EXPAND_FN(params_) ((params_)->expand_fn)
#define ROW_FN(params_) image_orth_generic_row
#include "imgbltorthrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn image_orth_generic_col
    Generic row fill function for orthogonal images. */
#ifndef DOXYGEN_SKIP
#define FUNCTION image_orth_generic_col
#define PIXEL_FN(params) (params->pixel_fn)
#include "imgfillorthcols.h"
#endif /* !DOXYGEN_SKIP */

/** \fn image_orth_generic_cols
   Definition of the generic orthogonal line function; this function calls
   the expander defined in the image params. */
#ifndef DOXYGEN_SKIP
#define FUNCTION image_orth_generic_cols
#define EXPAND_FN(params_) ((params_)->expand_fn)
#define COL_FN(params_) image_orth_generic_col
#include "imgbltorthcols.h"
#endif /* !DOXYGEN_SKIP */

/** \fn mask_orth_generic_rows
   Definition of the generic orthogonal line function; this function calls
   the expander and row fill functions defined in the image params. */
#ifndef DOXYGEN_SKIP
#define FUNCTION mask_orth_generic_rows
#define EXPAND_FN(params_) im_expandread
#define ROW_FN(params_) mask_orth_generic_row
#include "imgbltorthrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn mask_orth_generic_cols
   Definition of the generic orthogonal line function; this function calls
   the expander and row fill functions defined in the image params. */
#ifndef DOXYGEN_SKIP
#define FUNCTION mask_orth_generic_cols
#define EXPAND_FN(params_) im_expandread
#define COL_FN(params_) mask_orth_generic_col
#include "imgbltorthcols.h"
#endif /* !DOXYGEN_SKIP */

/** \fn rotated_lines
   Definition of the generic rotated line function; this function calls
   the expander and row fill functions defined in the image params. */
#ifndef DOXYGEN_SKIP
#define FUNCTION rotated_lines
#define EXPAND_FN(params_) ((params_)->expand_fn)
#define ROW_FN(params_) ((params_)->fill_fn.rotated)
#include "imgbltrot.h"
#endif /* !DOXYGEN_SKIP */

/* ---------------------------------------------------------------------- */
/* Specialised image blit functions for copydot (1:1 device resolution, with
   copy/shift from image expander to output, no screening required). */

/** \fn fill_1to1_charblt1
   Definition of the 1:1 optimised black writing row fill function. */
#ifndef DOXYGEN_SKIP
#define FUNCTION fill_1to1_charblt1
#define CHARBLT_FN charblt1
#include "imgfill1to1.h"
#endif /* !DOXYGEN_SKIP */

/** \fn image_orth_1to1_charblt1
    Specialised image function for 1:1 copydot optimisations, calling
    im_expand1to1 and charblt1 functions directly. */
#ifndef DOXYGEN_SKIP
#define FUNCTION image_orth_1to1_charblt1
#define EXPAND_FN(params_) im_expand1to1
#define ROW_FN(params_) fill_1to1_charblt1
#define NOT_HALFTONED /* solid black or solid white */
#include "imgbltorthrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn fill_1to1_charclip1
   Definition of the 1:1 optimised black writing row fill function
   for complex clipped images. */
#ifndef DOXYGEN_SKIP
#define FUNCTION fill_1to1_charclip1
#define CHARBLT_FN charclip1
#include "imgfill1to1.h"
#endif /* !DOXYGEN_SKIP */

/** \fn image_orth_1to1_charclip1
    Specialised image function for 1:1 copydot optimisations, calling
    im_expand1to1 and charclip1 functions directly. */
#ifndef DOXYGEN_SKIP
#define FUNCTION image_orth_1to1_charclip1
#define EXPAND_FN(params_) im_expand1to1
#define ROW_FN(params_) fill_1to1_charclip1
#define NOT_HALFTONED /* solid black or solid white */
#include "imgbltorthrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn fill_1to1_charblt0
   Definition of the 1:1 optimised writing row fill function. */
#ifndef DOXYGEN_SKIP
#define FUNCTION fill_1to1_charblt0
#define CHARBLT_FN charblt0
#include "imgfill1to1.h"
#endif /* !DOXYGEN_SKIP */

/** \fn image_orth_1to1_charblt0
    Specialised image function for 1:1 copydot optimisations, calling
    im_expand1to1 and charblt0 functions directly. */
#ifndef DOXYGEN_SKIP
#define FUNCTION image_orth_1to1_charblt0
#define EXPAND_FN(params_) im_expand1to1
#define ROW_FN(params_) fill_1to1_charblt0
#define NOT_HALFTONED /* solid black or solid white */
#include "imgbltorthrows.h"
#endif /* !DOXYGEN_SKIP */

/** \fn fill_1to1_charclip0
   Definition of the 1:1 optimised white writing row fill function
   for complex clipped images. */
#ifndef DOXYGEN_SKIP
#define FUNCTION fill_1to1_charclip0
#define CHARBLT_FN charclip0
#include "imgfill1to1.h"
#endif /* !DOXYGEN_SKIP */

/** \fn image_orth_1to1_charclip0
    Specialised image function for 1:1 copydot optimisations, calling
    im_expand1to1 and charclip0 functions directly. */
#ifndef DOXYGEN_SKIP
#define FUNCTION image_orth_1to1_charclip0
#define EXPAND_FN(params_) im_expand1to1
#define ROW_FN(params_) fill_1to1_charclip0
#define NOT_HALFTONED /* solid black or solid white */
#include "imgbltorthrows.h"
#endif /* !DOXYGEN_SKIP */

/* ---------------------------------------------------------------------- */

void imagebltn(render_blit_t *rb,
               imgblt_params_t *params,
               imgblt_callback_fn *callback,
               Bool *result)
{
  HQASSERT(callback, "No image blit callback") ;
  VERIFY_OBJECT(params, IMGBLT_PARAMS_NAME) ;

  if ( params->type == IM_BLIT_KNOCKOUT && params->orthogonal ) {
    /* Fast knockout for orthogonal images. */
    dcoord x1, y1, x2, y2;
    const dbbox_t *clip = &rb->p_ri->clip ;
    const dbbox_t *bbox = &params->image->bbox ;

    INLINE_MAX32(x1, clip->x1, bbox->x1) ;
    INLINE_MAX32(y1, clip->y1, bbox->y1) ;
    INLINE_MIN32(x2, clip->x2, bbox->x2) ;
    INLINE_MIN32(y2, clip->y2, bbox->y2) ;

    /* The bbox shouldn't be degenerate after clipping (the entire image
       would have been clipped out before getting here). The normal bitblit
       asserts will take care of checking this. */

    rb->ylineaddr = BLIT_ADDRESS(theFormA(*rb->outputform),
                                 theFormL(*rb->outputform)*(y1 - theFormHOff(*rb->outputform) - rb->y_sep_position));
    rb->ymaskaddr = BLIT_ADDRESS(theFormA(*rb->clipform),
                                 theFormL(*rb->clipform)*(y1 - theFormHOff(*rb->clipform) - rb->y_sep_position));

    DO_BLOCK(rb, y1, y2, x1, x2);
  } else {
    /* Use the line iteration function already set up to convert the image
       to blocks, via a fill function. */
    *result = (*callback)(rb, params) ;
  }
}

void imageblt1(render_blit_t *rb, imgblt_params_t *params,
               imgblt_callback_fn *callback,
               Bool *result)
{
  HQASSERT(rb != NULL, "No image blit render state") ;
  HQASSERT(callback, "No image blit callback") ;
  VERIFY_OBJECT(params, IMGBLT_PARAMS_NAME) ;

  /* The outputform type test is here so this routine can be used in
     maskimageslice, when we might be outputting to RLE encoded bands. */
  if ( params->orthogonal &&
       params->type != IM_BLIT_KNOCKOUT &&
       (theIOptimize(params->image) & IMAGE_OPTIMISE_1TO1) != 0 &&
       params->geometry->wx != 0 &&
       theFormT(*rb->outputform) != FORMTYPE_BANDRLEENCODED ) {
    render_info_t ri_copy ;
    const dbbox_t *bbox = &params->image->bbox ;

    /* Use the 1:1 expander. The specialised parameter to it has a different
       meaning from im_expandread's. */
    HQASSERT(params->geometry->wx >= 0 || !params->wflip,
             "Cannot be both wflipped and right-left") ;
    params->expand_arg = (params->geometry->wx < 0) ^ params->wflip ;

    RI_COPY_FROM_RB(&ri_copy, rb) ;
    rb = &ri_copy.rb ;

    /* Set the clip limits to match the image width. */
    INLINE_MAX32(ri_copy.clip.x1, bbox->x1, ri_copy.clip.x1) ;
    INLINE_MIN32(ri_copy.clip.x2, ri_copy.clip.x2, bbox->x2) ;

    if ( params->type == IM_BLIT_IMAGE ) {
      /* Since we're doing an image, we need to clear the background. */
      dcoord y1, y2 ;

      INLINE_MAX32(y1, bbox->y1, ri_copy.clip.y1) ;
      INLINE_MIN32(y2, ri_copy.clip.y2, bbox->y2) ;

      rb->ylineaddr = BLIT_ADDRESS(theFormA(*rb->outputform),
                                   (y1 - theFormHOff(*rb->outputform) - rb->y_sep_position) *
                                   theFormL(*rb->outputform)) ;

      if ( rb->clipmode == BLT_CLP_COMPLEX ) {
        rb->ymaskaddr = BLIT_ADDRESS(theFormA(*rb->clipform),
                                     (y1 - theFormHOff(*rb->clipform) - rb->y_sep_position) *
                                     theFormL(*rb->clipform)) ;
        blkclip0(rb, y1, y2, ri_copy.clip.x1, ri_copy.clip.x2) ;
      } else
        blkfill0(rb, y1, y2, ri_copy.clip.x1, ri_copy.clip.x2) ;
    }

    if ( rb->clipmode == BLT_CLP_COMPLEX ) {
      *result = image_orth_1to1_charclip1(rb, params) ;
    } else {
      *result = image_orth_1to1_charblt1(rb, params) ;
    }

    return ;
  }

  imagebltn(rb, params, callback, result) ;
}

void imageblt0(render_blit_t *rb, imgblt_params_t *params,
               imgblt_callback_fn *callback,
               Bool *result)
{
  HQASSERT(rb != NULL, "No image blit render state") ;
  HQASSERT(callback, "No image blit callback") ;
  VERIFY_OBJECT(params, IMGBLT_PARAMS_NAME) ;

  /* This routine is only useful for masks, it doesn't clear the background. */
  if ( params->orthogonal &&
       params->type == IM_BLIT_MASK &&
       params->geometry->wx != 0 &&
       (theIOptimize(params->image) & IMAGE_OPTIMISE_1TO1) != 0 ) {
    render_info_t ri_copy ;
    const dbbox_t *bbox = &params->image->bbox ;

    /* Use the 1:1 expander. The specialised parameter to it has a different
       meaning from im_expandread's. */
    HQASSERT(params->geometry->wx >= 0 || !params->wflip,
             "Cannot be both wflipped and right-left") ;
    params->expand_arg = (params->geometry->wx < 0) ^ params->wflip ;

    RI_COPY_FROM_RB(&ri_copy, rb) ;
    rb = &ri_copy.rb ;

    /* Set the clip limits to match the image width. */
    INLINE_MAX32(ri_copy.clip.x1, bbox->x1, ri_copy.clip.x1) ;
    INLINE_MIN32(ri_copy.clip.x2, ri_copy.clip.x2, bbox->x2) ;

    if ( rb->clipmode == BLT_CLP_COMPLEX ) {
      *result = image_orth_1to1_charclip0(rb, params) ;
    } else {
      *result = image_orth_1to1_charblt0(rb, params) ;
    }

    return ;
  }

  imagebltn(rb, params, callback, result) ;
}

/* ---------------------------------------------------------------------- */

Bool image_dbbox_covering_ibbox(const IMAGEOBJECT *imageobj,
                                const ibbox_t *ibbox,
                                dbbox_t *dbbox)
{
  ibbox_t ibbox_clipped ;
  dbbox_t dbbox_transform ;
  const im_transform_t *geometry ;

  HQASSERT(imageobj != NULL, "No image object") ;
  geometry = &imageobj->geometry ;
  IM_TRANSFORM_ASSERTS(geometry) ;
  HQASSERT(dbbox != NULL, "No device space bbox") ;
  HQASSERT(ibbox != NULL, "No image space bbox") ;

  if ( geometry->cross_sign == 0 ) {
    /* Non-invertible matrix implies no visible area, we'll make the result
       equivalent to an empty bbox. */
    return FALSE ;
  }

  bbox_intersection(&imageobj->imsbbox, ibbox, &ibbox_clipped) ;
  if ( bbox_is_empty(&ibbox_clipped) )
    return FALSE ;

  if ( ibbox_clipped.x1 == 0 && ibbox_clipped.x2 == geometry->w - 1 &&
       ibbox_clipped.y1 == 0 && ibbox_clipped.y2 == geometry->h - 1 ) {
    /* If we're doing the whole image, then the bbox is the image extent. */
    dbbox_transform = geometry->extent ;
  } else {
    image_dda_t x, y, xperu, yperu, xperv, yperv ;
    image_dda_basis_t basis ;
    int32 duv ;

    bbox_clear(&dbbox_transform) ;

    IMAGE_DDA_BASIS(&basis, geometry->w, geometry->h) ;
    IMAGE_DDA_INITIALISE_W(&xperu, basis, geometry->wx) ;
    IMAGE_DDA_INITIALISE_H(&xperv, basis, geometry->hx) ;
    IMAGE_DDA_INITIALISE_W(&yperu, basis, geometry->wy) ;
    IMAGE_DDA_INITIALISE_H(&yperv, basis, geometry->hy) ;

    /* Transform (x1,y1) to device space, adding to dbbox union. Calculate
       the device space coordinates as a delta from the image transform
       point. Add 1 to the image space x2 and y2 coordinates, because we want
       to get the device space range that fully encloses the image space
       bbox. We'll compensate for this addition by subtracting from the
       device space range if doing off-by-one rendering. Combine the points
       into the device space bbox using bbox_union_coordinates because this
       will set both the high and low ranges if necessary. */
    IMAGE_DDA_INITIALISE_XY(&x, basis, geometry->tx) ;
    IMAGE_DDA_INITIALISE_XY(&y, basis, geometry->ty) ;

    IMAGE_DDA_STEP_N(x, xperu, basis, ibbox_clipped.x1, &x) ;
    IMAGE_DDA_STEP_N(y, yperu, basis, ibbox_clipped.x1, &y) ;
    IMAGE_DDA_STEP_N(x, xperv, basis, ibbox_clipped.y1, &x) ;
    IMAGE_DDA_STEP_N(y, yperv, basis, ibbox_clipped.y1, &y) ;
    bbox_union_coordinates(&dbbox_transform, x.i, y.i, x.i, y.i) ;

    if ( !(geometry->wy == 0 && geometry->hx == 0) &&
         !(geometry->wx == 0 && geometry->hy == 0) ) {
      /* If rotated or skewed, calculate the point x1,y2. Don't disrupt x
         and y, we'll calculate x2,y1 and x2,y2 as deltas from it. */
      image_dda_t x1 = x, y2 = y ;
      duv = ibbox_clipped.y2 + 1 - ibbox_clipped.y1 ;
      IMAGE_DDA_STEP_N(x1, xperv, basis, duv, &x1) ;
      IMAGE_DDA_STEP_N(y2, yperv, basis, duv, &y2) ;
      bbox_union_coordinates(&dbbox_transform, x1.i, y2.i, x1.i, y2.i) ;
    }

    /* x2,y1 is calculated as a delta from x1,y1. */
    duv = ibbox_clipped.x2 + 1 - ibbox_clipped.x1 ;
    IMAGE_DDA_STEP_N(x, xperu, basis, duv, &x) ;
    IMAGE_DDA_STEP_N(y, yperu, basis, duv, &y) ;
    bbox_union_coordinates(&dbbox_transform, x.i, y.i, x.i, y.i) ;

    /* x2,y2 is calculated as a delta from x1,y1. */
    duv = ibbox_clipped.y2 + 1 - ibbox_clipped.y1 ;
    IMAGE_DDA_STEP_N(x, xperv, basis, duv, &x) ;
    IMAGE_DDA_STEP_N(y, yperv, basis, duv, &y) ;
    bbox_union_coordinates(&dbbox_transform, x.i, y.i, x.i, y.i) ;

    dbbox_transform.x2 -= 1 ;
    dbbox_transform.y2 -= 1 ;
  }

  /* Intersect with the image bbox which includes clipping. */
  bbox_intersection(&dbbox_transform, &imageobj->bbox, dbbox);

  return !bbox_is_empty(dbbox) ;
}

/* ---------------------------------------------------------------------- */
static Bool setuporthogonal(imgblt_params_t *cb,
                            const IMAGEOBJECT *theimage,
                            const render_info_t *p_ri)
{
  const im_transform_t *geometry ;
  ibbox_t imbb ;

  geometry = &theimage->geometry ;
  HQASSERT((geometry->wx != 0 && geometry->wy == 0 &&
            geometry->hx == 0 && geometry->hy != 0) ||
           (geometry->wx == 0 && geometry->wy != 0 &&
            geometry->hx != 0 && geometry->hy == 0),
           "Non-orthogonal image geometry") ;
  cb->image = theimage ;
  cb->geometry = geometry ;
  cb->orthogonal = TRUE ;
  cb->wflip = (theIOptimize(theimage) & IMAGE_OPTIMISE_XFLIP) != 0 ;
  cb->hflip = (theIOptimize(theimage) & IMAGE_OPTIMISE_YFLIP) != 0 ;
  cb->order = 1 ;
  cb->cross_sign = 1 ;
  cb->tiles = NULL ;
  cb->expand_fn = im_expandread ;
  cb->expand_arg = p_ri->region_set_type == RENDER_REGIONS_BACKDROP;

  if ( !image_ibbox_covering_dbbox(theimage, &p_ri->clip, &imbb) ) {
    HQFAIL("No intersection of image bbox with clip") ;
    return FAILURE(FALSE);
  }

  /* Prepare common image basis for use. All of the image DDAs share a common
     basis, so xperw and xperh are compatible, etc. For orthogonal images, we
     only need an X and Y coordinate for the start of the row or column. */
  IMAGE_DDA_BASIS(&cb->basis, geometry->w, geometry->h) ;
  IMAGE_DDA_INITIALISE_XY(&cb->xs, cb->basis, geometry->tx) ;
  IMAGE_DDA_INITIALISE_XY(&cb->ys, cb->basis, geometry->ty) ;

  if ( geometry->hx == 0 && geometry->wy == 0 ) {
    /* Device-space X start and direction. */
    IMAGE_DDA_INITIALISE_W(&cb->xperwh, cb->basis, geometry->wx) ;
    if ( geometry->wx < 0 &&
         (p_ri->surface->render_order & SURFACE_ORDER_DEVICELR) != 0 ) {
      cb->dcol = -1 ;
      IMAGE_DDA_STEP_N(cb->xs, cb->xperwh, cb->basis, imbb.x2 + 1, &cb->xs) ;
      IMAGE_DDA_NEGATE(cb->xperwh, cb->basis, &cb->xperwh) ;
    } else {
      cb->dcol = 1 ;
      IMAGE_DDA_STEP_N(cb->xs, cb->xperwh, cb->basis, imbb.x1, &cb->xs) ;
    }

    /* Device-space Y start and direction. */
    IMAGE_DDA_INITIALISE_H(&cb->yperhw, cb->basis, geometry->hy) ;
    if ( geometry->hy < 0 &&
         (p_ri->surface->render_order & SURFACE_ORDER_DEVICETB) != 0 ) {
      cb->drow = -1 ;
      IMAGE_DDA_STEP_N(cb->ys, cb->yperhw, cb->basis, imbb.y2 + 1, &cb->ys) ;
      IMAGE_DDA_NEGATE(cb->yperhw, cb->basis, &cb->yperhw) ;
    } else {
      cb->drow = 1 ;
      IMAGE_DDA_STEP_N(cb->ys, cb->yperhw, cb->basis, imbb.y1, &cb->ys) ;
    }
  } else if ( geometry->wx == 0 && geometry->hy == 0 ) {
    /* Device-space X start and direction. */
    IMAGE_DDA_INITIALISE_H(&cb->xperwh, cb->basis, geometry->hx) ;
    if ( geometry->hx < 0 &&
         (p_ri->surface->render_order & SURFACE_ORDER_DEVICELR) != 0 ) {
      cb->drow = -1 ;
      IMAGE_DDA_STEP_N(cb->xs, cb->xperwh, cb->basis, imbb.y2 + 1, &cb->xs) ;
      IMAGE_DDA_NEGATE(cb->xperwh, cb->basis, &cb->xperwh) ;
    } else {
      cb->drow = 1 ;
      IMAGE_DDA_STEP_N(cb->xs, cb->xperwh, cb->basis, imbb.y1, &cb->xs) ;
    }

    /* Device-space Y start and direction. */
    IMAGE_DDA_INITIALISE_W(&cb->yperhw, cb->basis, geometry->wy) ;
    if ( geometry->wy < 0 &&
         (p_ri->surface->render_order & SURFACE_ORDER_DEVICETB) != 0 ) {
      cb->dcol = -1 ;
      IMAGE_DDA_STEP_N(cb->ys, cb->yperhw, cb->basis, imbb.x2 + 1, &cb->ys) ;
      IMAGE_DDA_NEGATE(cb->yperhw, cb->basis, &cb->yperhw) ;
    } else {
      cb->dcol = 1 ;
      IMAGE_DDA_STEP_N(cb->ys, cb->yperhw, cb->basis, imbb.x1, &cb->ys) ;
    }
  } else {
    HQFAIL("Invalid orthogonal image geometry") ;
    return FAILURE(FALSE) ;
  }

  /* These not used for orthogonal images. */
  IMAGE_DDA_INITIALISE_0(&cb->xperhw) ;
  IMAGE_DDA_INITIALISE_0(&cb->yperwh) ;
  IMAGE_DDA_INITIALISE_0(&cb->nxperw) ;
  IMAGE_DDA_INITIALISE_0(&cb->nyperw) ;

  cb->irow = cb->drow < 0 ? imbb.y2 : imbb.y1 ;
  cb->nrows = imbb.y2 - imbb.y1 + 1 ;

  /* Regardless of the X order, the start X position is the same. We adjust
     the expander buffer pointer and read the contents in reverse if
     flipping. */
  cb->ncols = imbb.x2 - imbb.x1 + 1 ;
  cb->lcol = imbb.x1 ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/* Controls the calls to the imaging for orthogonal imagemask.
 */
Bool setupmaskandgo(IMAGEOBJECT *theimage, render_blit_t *rb)
{
  Bool result = TRUE ;
  const render_info_t *p_ri ;
  const im_transform_t *geometry ;
  imgblt_params_t cb ; /* callback data */
  imgblt_callback_fn *cbfn ;

  HQASSERT(RENDER_BLIT_CONSISTENT(rb), "Render context is not consistent") ;
  p_ri = rb->p_ri ;

  /* Not an error if this returns FALSE; it merely means the image doesn't
     intersect the clip box. */
  if ( !setuporthogonal(&cb, theimage, p_ri) )
    return TRUE ;

  cb.expanded_comps = cb.converted_comps = 1 ;
  cb.expanded_to_plane[0] = cb.blit_to_expanded[0] = 0 ;

  cb.type = IM_BLIT_MASK ;
  HQASSERT(!im_16bit_output(p_ri, theimage->ime),
           "Mask should not expand to 16 bits") ;
  cb.out16 = FALSE ;
  cb.on_the_fly = FALSE ;
  cb.one_color_channel = TRUE ;
  cb.ht_params = NULL ; /* Done at a higher level */

  geometry = &theimage->geometry ;
  if ( geometry->wx != 0 ) {
    HQASSERT(geometry->wy == 0 && geometry->hx == 0 && geometry->hy != 0,
             "Invalid orthogonal image geometry") ;
    cbfn = mask_orth_generic_rows ;
  } else {
    HQASSERT(geometry->wy != 0 && geometry->hx != 0 && geometry->hy == 0,
             "Invalid orthogonal image geometry") ;
    cbfn = mask_orth_generic_cols ;
  }

  if ( cb.expanded_comps == 0 ) {
    cb.pixel_fn = pixels_generic_knockout ;
  } else {
    cb.pixel_fn = cb.dcol < 0 ? pixels_mask_backward : pixels_mask_forward ;
  }

  NAME_OBJECT(&cb, IMGBLT_PARAMS_NAME) ;

  DO_IMG(rb, &cb, cbfn, &result) ;

  UNNAME_OBJECT(&cb) ;

  return result ;
}

/* Macros to make lookup table definitions shorter. Lookup tables are used to
   determine the optimal column/row iteration orders because I can't discern
   a sufficiently easy algorithmic encoding of the patterns. */
#define N 0x00        /* Normal state (forward iteration) */
#define C 0x11        /* Columns backward */
#define R 0x44        /* Rows backward */
#define CR (C|R)      /* Columns backward, rows backward */
#define XN (C<<1)     /* Columns either direction, rows forward */
#define NX (R<<1)     /* Columns forward, rows either direction */
#define CX (C|NX)     /* Columns backward, rows either direction */
#define XR (XN|R)     /* Columns either direction, rows backward */
#define X(p_, n_) (((p_) & 0x0f) | ((n_) & 0xf0)) /* Vary by cross product */

/** Lookup table for left-right device rendering order. Values are indexed by
    sign(wy), sign(wx), sign(hy) sign(hx), and then by sign(cross product). */
const static uint8 lr_order[81] = {
  /* wx/wy   -/-      -/0 -/+      0/- 0/0 0/+ +/-      +/0 +/+
     hx/hy                                                          */
  /* -/- */  X(C,R),  CX, CR,      R,  N,  CR, R,       NX, X(CR,N),
  /* -/0 */  XR,      N,  XR,      XR, N,  XR, XR,      N,  XR,
  /* -/+ */  CR,      CX, X(R,C),  CR, N,  R,  X(N,CR), NX, R,
  /* 0/- */  C,       CX, CR,      N,  N,  N,  R,       NX, N,
  /* 0/0 */  N,       N,  N,       N,  N,  N,  N,       N,  N,
  /* 0/+ */  CR,      CX, C,       N,  N,  N,  N,       NX, R,
  /* +/- */  C,       CX, X(CR,N), C,  N,  N,  X(C,R),  NX, N,
  /* +/0 */  XN,      N,  XN,      XN, N,  XN, XN,      N,  XN,
  /* +/+ */  X(N,CR), CX, C,       N,  N,  C,  N,       NX, X(R,C),
} ;

/** Lookup table for top-bottom device rendering order. Values are indexed by
    sign(wy), sign(wx), sign(hy) sign(hx), and then by sign(cross product). */
const static uint8 tb_order[81] = {
  /* wx/wy   -/-      -/0 -/+      0/- 0/0 0/+ +/-      +/0 +/+
     hx/hy                                                          */
  /* -/- */  X(R,C),  R,  R,       CX, N,  NX, CR,      CR, X(N,CR),
  /* -/0 */  C,       N,  R,       CX, N,  NX, CR,      N,  N,
  /* -/+ */  C,       C,  X(R,C),  CX, N,  NX, X(N,CR), N,  N,
  /* 0/- */  XR,      XR, XR,      N,  N,  N,  XR,      XR, XR,
  /* 0/0 */  N,       N,  N,       N,  N,  N,  N,       N,  N,
  /* 0/+ */  XN,      XN, XN,      N,  N,  N,  XN,      XN, XN,
  /* +/- */  CR,      CR, X(CR,N), CX, N,  NX, X(C,R),  R,  R,
  /* +/0 */  CR,      N,  N,       CX, N,  NX, C,       N,  R,
  /* +/+ */  X(CR,N), N,  N,       CX, N,  NX, C,       C,  X(C,R),
} ;

#undef N
#undef CR
#undef CX
#undef XR
#undef X

/* ---------------------------------------------------------------------- */
static Bool setuprotated(imgblt_params_t *cb,
                         const IMAGEOBJECT *theimage,
                         const render_info_t *p_ri)
{
  const im_transform_t *geometry ;
  ibbox_t imbb ;
  uint8 rc = (XN|NX) & 0xf ; /* Row/column order can be overridden */

  if ( !image_ibbox_covering_dbbox(theimage, &p_ri->clip, &imbb) )
    return FALSE ; /* No intersection of image bbox with clip. */

  geometry = &theimage->geometry ;

  cb->image = theimage ;
  cb->geometry = geometry ;
  cb->orthogonal = FALSE ;
  cb->wflip = (theIOptimize(theimage) & IMAGE_OPTIMISE_XFLIP) != 0 ;
  cb->hflip = (theIOptimize(theimage) & IMAGE_OPTIMISE_YFLIP) != 0 ;
  cb->cross_sign = geometry->cross_sign ;
  cb->tiles = theimage->tiles ;
  cb->expand_fn = im_expandread ;
  cb->expand_arg = p_ri->region_set_type == RENDER_REGIONS_BACKDROP;

  /* Unless the surface asks for a particular render order, we'll read and
     rasterise data in the order the image store has it. */
  cb->drow = cb->dcol = 1 ;

  if ( (p_ri->surface->render_order & SURFACE_ORDER_DEVICELR) != 0 ) {
    int index = SIGN32(geometry->wy) + SIGN32(geometry->wx) * 3 +
      SIGN32(geometry->hy) * 9 + SIGN32(geometry->hx) * 27 + 40 ;
    HQASSERT(index >= 0 && index < NUM_ARRAY_ITEMS(lr_order),
             "Index calculation out of bounds") ;
    rc = geometry->cross_sign < 0
      ? (lr_order[index] >> 4)
      : (lr_order[index] & 0xf) ;
  }

  /* If we didn't set both row and column order from device LR, set remainder
     from */
  if ( (p_ri->surface->render_order & SURFACE_ORDER_DEVICETB) != 0 &&
       (rc & ((C<<1)|(R<<1))) != 0 ) {
    uint8 tb ;
    int index = SIGN32(geometry->wy) + SIGN32(geometry->wx) * 3 +
      SIGN32(geometry->hy) * 9 + SIGN32(geometry->hx) * 27 + 40 ;
    HQASSERT(index >= 0 && index < NUM_ARRAY_ITEMS(lr_order),
             "Index calculation out of bounds") ;
    tb = geometry->cross_sign < 0
      ? (tb_order[index] >> 4)
      : (tb_order[index] & 0xf) ;
    if ( rc & (C<<1) )
      rc = (rc & ~C) | (tb & C) ;
    if ( rc & (R<<1) )
      rc = (rc & ~R) | (tb & R) ;
  }

  if ( rc & C )
    cb->dcol = -1 ;

  if ( rc & R )
    cb->drow = -1 ;

  cb->irow = cb->drow < 0 ? imbb.y2 : imbb.y1 ;
  cb->nrows = imbb.y2 - imbb.y1 + 1 ;

  /* Regardless of the X order, the start X position is the same. We adjust
     the expander buffer pointer and read the contents in reverse if
     flipping. */
  cb->ncols = imbb.x2 - imbb.x1 + 1;
  cb->lcol = imbb.x1 ;

  /* Prepare common image basis for use. All of the image DDAs share a common
     basis, so xperw and xperh are compatible, etc. For rotated images, we
     need an X and Y coordinate for the start of the row or column. We also
     keep the negated X and Y steps per column, to assist with clip edge
     adjustment. */
  IMAGE_DDA_BASIS(&cb->basis, geometry->w, geometry->h) ;
  IMAGE_DDA_INITIALISE_XY(&cb->xs, cb->basis, geometry->tx) ;
  IMAGE_DDA_INITIALISE_XY(&cb->ys, cb->basis, geometry->ty) ;

  IMAGE_DDA_INITIALISE_W(&cb->xperwh, cb->basis, geometry->wx) ;
  IMAGE_DDA_INITIALISE_W(&cb->yperwh, cb->basis, geometry->wy) ;
  IMAGE_DDA_NEGATE(cb->xperwh, cb->basis, &cb->nxperw) ;
  IMAGE_DDA_NEGATE(cb->yperwh, cb->basis, &cb->nyperw) ;
  /* No adjustment of tx,ty for column, we keep xs,ys as the row start.
     Slightly different DDA setup for height, because we negate the row step
     in imgbltrot.h rather than keeping both normal and negated steps in the
     params structure. */
  IMAGE_DDA_INITIALISE_H(&cb->xperhw, cb->basis, geometry->hx) ;
  IMAGE_DDA_INITIALISE_H(&cb->yperhw, cb->basis, geometry->hy) ;
  IMAGE_DDA_STEP_N(cb->xs, cb->xperhw, cb->basis, cb->irow, &cb->xs) ;
  IMAGE_DDA_STEP_N(cb->ys, cb->yperhw, cb->basis, cb->irow, &cb->ys) ;

  return TRUE ;
}

#undef C
#undef R
#undef XN
#undef NX

/* ---------------------------------------------------------------------- */
/* Controls the calls to the imaging for rotated imagemask. */

Bool setuprotatedmaskandgo(IMAGEOBJECT *theimage, render_blit_t *rb)
{
  Bool result ;
  const render_info_t *p_ri ;
  imgblt_params_t cb ; /* callback data */

  HQASSERT(RENDER_BLIT_CONSISTENT(rb), "Render context is not consistent") ;
  p_ri = rb->p_ri ;

  /* Not an error if this returns FALSE; it merely means the image doesn't
     intersect the clip box. */
  if ( !setuprotated(&cb, theimage, p_ri) )
    return TRUE ;

  cb.expanded_comps = cb.converted_comps = 1 ;
  cb.expanded_to_plane[0] = cb.blit_to_expanded[0] = 0 ;

  cb.type = IM_BLIT_MASK ;
  HQASSERT(!im_16bit_output(p_ri, theimage->ime),
           "Mask should not expand to 16 bits") ;
  cb.out16 = FALSE ;
  cb.on_the_fly = FALSE ;
  cb.one_color_channel = TRUE ;
  cb.ht_params = NULL ; /* Done at a higher level */
  cb.fill_fn.rotated = cb.tiles ? rmask_tiled : rmask ;

  if ( cb.expanded_comps == 0 ) {
    cb.pixel_fn = pixels_generic_knockout ;
  } else {
    cb.pixel_fn = cb.dcol < 0 ? pixels_mask_backward : pixels_mask_forward ;
  }

  NAME_OBJECT(&cb, IMGBLT_PARAMS_NAME) ;

  DO_IMG(rb, &cb, rotated_lines, &result) ;

  UNNAME_OBJECT(&cb) ;

  return result ;
}

/* ---------------------------------------------------------------------- */
Bool setupimageandgo(IMAGEOBJECT *theimage,
                     render_blit_t *rb,
                     Bool screened)
{
  Bool result = TRUE ;
  const render_info_t *p_ri ;
  const im_transform_t *geometry ;
  imgblt_params_t cb ; /* callback data */
  imgblt_callback_fn *cbfn ;

  HQASSERT(RENDER_BLIT_CONSISTENT(rb), "Render context is not consistent") ;
  p_ri = rb->p_ri ;

  /* Not an error if this returns FALSE; it merely means the image doesn't
     intersect the clip box. */
  if ( !setuporthogonal(&cb, theimage, p_ri) )
    return TRUE ;

  *p_ri->p_rs->cs.p_white_on_white = FALSE;

  /* Generate the two mappings needed, one for the expander, the other for
     the pixel extracter. */
  im_expand_blit_mapping(rb, theimage->ime, cb.expanded_to_plane,
                         &cb.expanded_comps, &cb.converted_comps,
                         cb.blit_to_expanded);

  cb.type = IM_BLIT_IMAGE ;
  cb.out16 = im_16bit_output(p_ri, theimage->ime) ;
  cb.on_the_fly = im_converting_on_the_fly(theimage->ime) ;
  cb.one_color_channel = can_expand_pixels_1xN(rb->color, cb.converted_comps,
                                               cb.blit_to_expanded) ;
  cb.ht_params = screened ? p_ri->ht_params : NULL ;

  geometry = &theimage->geometry ;
  if ( geometry->wx != 0 ) {
    HQASSERT(geometry->wy == 0 && geometry->hx == 0 && geometry->hy != 0,
             "Invalid orthogonal image geometry") ;
    cbfn = image_orth_generic_rows ;
  } else {
    HQASSERT(geometry->wy != 0 && geometry->hx != 0 && geometry->hy == 0,
             "Invalid orthogonal image geometry") ;
    cbfn = image_orth_generic_cols ;
  }

  if ( cb.expanded_comps == 0 ) {
    cb.type = IM_BLIT_KNOCKOUT ;
    cb.expand_fn = im_expandknockout ;
    cb.expand_arg = 0 ;
    cb.pixel_fn = pixels_generic_knockout ;
  } else {
    cb.pixel_fn = pixel_functions
      [cb.out16]
      [cb.dcol < 0]
      [cb.one_color_channel] ;
  }

  NAME_OBJECT(&cb, IMGBLT_PARAMS_NAME) ;

  DO_IMG(rb, &cb, cbfn, &result) ;

  UNNAME_OBJECT(&cb) ;

  return result ;
}

/* The following function is used when we have an orthogonal image, with
   complex clipping which has a single band-height rectangular unclipped
   region. It images the left part, right part, and centre of the image
   separately, using the fastest method for the centre section. */
Bool setup3partimageandgo(IMAGEOBJECT *theimage,
                          render_blit_t *rb,
                          Bool screened)
{
  render_info_t ri_copy ;
  dcoord x1, x2 ;

  HQASSERT(RENDER_BLIT_CONSISTENT(rb), "Render context is not consistent") ;
  HQASSERT(rb == &rb->p_ri->rb, "Blit context changed before blit call") ;

  RI_COPY_FROM_RB(&ri_copy, rb) ;

  HQASSERT(theimage, "No image parameter in setup3partimageandgo") ;

  x1 = theimage->bbox.x1 ;
  x2 = theimage->bbox.x2 ;
  bbox_clip_x(&ri_copy.clip, x1, x2);
  HQASSERT(x2 >= x1, "Grid values in wrong order in setup3partimageandgo") ;

  /* we shouldn't be in here if the mask clips are the wrong way round -
     typically MININT and MAXINT indicating clipping more than one span across
     the band, or similar. This would lead to overflow and wrapping when doing
     arithmetic on them. */
  HQASSERT(ri_copy.x2maskclip >= ri_copy.x1maskclip,
           "maskclips wrong way round - overflow!");

  if ( x1 < ri_copy.x1maskclip ) {
    HQASSERT(ri_copy.x1maskclip > rb->p_ri->clip.x1,
             "No area to left of unmasked") ;

    ri_copy.clip.x2 = ri_copy.x1maskclip - 1;
    ri_copy.rb.clipmode = BLT_CLP_COMPLEX ;
    if ( !setupimageandgo(theimage, &ri_copy.rb, screened) )
      return FALSE ;
  }

  if ( x1 <= ri_copy.x2maskclip && x2 >= ri_copy.x1maskclip ) {
    ri_copy.clip.x1 = ri_copy.x1maskclip;
    ri_copy.clip.x2 = ri_copy.x2maskclip;
    ri_copy.rb.clipmode = BLT_CLP_RECT;
    if ( !setupimageandgo(theimage, &ri_copy.rb, screened) )
      return FALSE ;
  }

  if ( x2 > ri_copy.x2maskclip ) {
    HQASSERT(ri_copy.x2maskclip < rb->p_ri->clip.x2,
             "No area to right of unmasked");

    ri_copy.clip.x1 = ri_copy.x2maskclip + 1;
    ri_copy.clip.x2 = rb->p_ri->clip.x2;
    ri_copy.rb.clipmode = BLT_CLP_COMPLEX ;
    if ( !setupimageandgo(theimage, &ri_copy.rb, screened) )
      return FALSE ;
  }

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
Bool setuprotatedimageandgo(IMAGEOBJECT *theimage, render_blit_t *rb,
                            Bool screened)
{
  Bool result ;
  const render_info_t *p_ri ;
  imgblt_params_t cb ;

  HQASSERT(RENDER_BLIT_CONSISTENT(rb), "Render context is inconsistent") ;
  p_ri = rb->p_ri ;

  /* Not an error if this returns FALSE; it merely means the image doesn't
     intersect the clip box. */
  if ( !setuprotated(&cb, theimage, p_ri) )
    return TRUE ;

  *p_ri->p_rs->cs.p_white_on_white = FALSE;

  /* Generate the two mappings needed, one for the expander, the other for
     the pixel extracter. */
  im_expand_blit_mapping(rb, theimage->ime,
                         cb.expanded_to_plane, &cb.expanded_comps,
                         &cb.converted_comps, cb.blit_to_expanded);

  cb.type = IM_BLIT_IMAGE ;
  cb.out16 = im_16bit_output(p_ri, theimage->ime) ;
  cb.on_the_fly = im_converting_on_the_fly(theimage->ime) ;
  cb.one_color_channel = can_expand_pixels_1xN(rb->color, cb.converted_comps,
                                               cb.blit_to_expanded) ;
  cb.ht_params = screened ? p_ri->ht_params : NULL ;
  cb.fill_fn.rotated = cb.tiles ? rfill_tiled : rfill ;

  if ( cb.expanded_comps == 0 ) {
    /* When knocking out, the erase color is already set up. There is no
       expanded data, so we'll repeatedly use the same original colorvalue
       set up by unpacking the image color. */
    cb.type = IM_BLIT_KNOCKOUT ;
    cb.expand_fn = im_expandknockout ;
    cb.expand_arg = 0 ;
    cb.pixel_fn = pixels_generic_knockout ;
    cb.ht_params = NULL ; /* Done at a higher level in the knockout case. */
  } else {
    cb.pixel_fn = pixel_functions
      [cb.out16]
      [cb.dcol < 0]
      [cb.one_color_channel] ;
  }

  NAME_OBJECT(&cb, IMGBLT_PARAMS_NAME) ;

  DO_IMG(rb, &cb, rotated_lines, &result) ;

  UNNAME_OBJECT(&cb) ;

  return result ;
}

/* ---------------------------------------------------------------------- */
/** Set up a blit color from an 8-bit expander buffer.

    \param[in,out] color        The blit color to fill in.
    \param[in] expanded         The expander data buffer.
    \param[in] blit_to_expanded Mapping from blit color channels to expander
                                indices.
    \param nexpanded            The number of channels of expanded data.
*/
static inline void im_expand_color_8(blit_color_t *color,
                                     const uint8 *expanded,
                                     unsigned int nexpanded,
                                     const int blit_to_expanded[])
{
  channel_index_t index, nchannels ;
  const blit_colormap_t *map ;

  UNUSED_PARAM(unsigned int, nexpanded) ; /* For asserts only */

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(expanded != NULL, "No expansion buffer") ;
  HQASSERT(blit_to_expanded != NULL, "No expander to blit channel mapping") ;
  HQASSERT(nexpanded > 0, "No expanded data channels") ;

  HQASSERT(color->quantised.spotno != SPOT_NO_INVALID,
           "Quantised screen not set for expander color") ;

  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;
  nchannels = map->nchannels ;

  for ( index = 0 ; index < nchannels ; ++index ) {
    int eindex = blit_to_expanded[index] ;
    if ( eindex >= 0 ) {
      HQASSERT((unsigned int)eindex < nexpanded, "Expanded index out of range") ;
      if ( color->quantised.htmax[index] == COLORVALUE_ONE )
        /* Shift up 8 bit values to make 16 bit values. This avoids bloating
           image stores and LUTs when compositing. */
        color->quantised.qcv[index] = expanded[eindex] << 8 ;
      else
        color->quantised.qcv[index] = expanded[eindex] ;
      HQASSERT(expanded[eindex] <= color->quantised.htmax[index],
               "Expanded color is not quantised to valid range") ;
    }
  }

  color->quantised.state = blit_quantise_unknown ;

  if ( blit_to_expanded[index = map->alpha_index] >= 0 ) {
    /* If we expanded alpha, it's now quantised. Map it back to the unpacked
       range, and store it in the appropriate places. */
    COLORVALUE_DIVIDE(color->quantised.qcv[index],
                      color->quantised.htmax[index],
                      color->alpha) ;
    color->unpacked.channel[index].cv = color->alpha ;
  }

  /* Force re-packing of blit color. */
#ifdef ASSERT_BUILD
  color->valid = blit_color_quantised;
#endif
  blit_color_pack(color) ;
}

/** Set up a blit color from a 16-bit expander buffer.

    \param[in,out] color        The blit color to fill in.
    \param[in] expanded         The expander data buffer.
    \param[in] blit_to_expanded Mapping from blit color channels to expander
                                indices.
    \param nexpanded            The number of channels of expanded data.
*/
static inline void im_expand_color_16(blit_color_t *color,
                                      const uint16 *expanded,
                                      unsigned int nexpanded,
                                      const int blit_to_expanded[])
{
  channel_index_t index, nchannels ;
  const blit_colormap_t *map ;

  UNUSED_PARAM(unsigned int, nexpanded) ; /* For asserts only */

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(expanded != NULL, "No expansion buffer") ;
  HQASSERT(blit_to_expanded != NULL, "No expander to blit channel mapping") ;
  HQASSERT(nexpanded > 0, "No expanded data channels") ;

  HQASSERT(color->quantised.spotno != SPOT_NO_INVALID,
           "Quantised screen not set for expander color") ;

  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;
  nchannels = map->nchannels ;

  for ( index = 0 ; index < nchannels ; ++index ) {
    int eindex = blit_to_expanded[index] ;
    if ( eindex >= 0 ) {
      HQASSERT((unsigned int)eindex < nexpanded, "Expanded index out of range") ;
      color->quantised.qcv[index] = expanded[eindex] ;
      HQASSERT(expanded[eindex] <= color->quantised.htmax[index],
               "Expanded color is not quantised to valid range") ;
    }
  }

  color->quantised.state = blit_quantise_unknown ;

  if ( blit_to_expanded[index = map->alpha_index] >= 0 ) {
    /* If we expanded alpha, it's now quantised. Map it back to the unpacked
       range, and store it in the appropriate places. */
    COLORVALUE_DIVIDE(color->quantised.qcv[index],
                      color->quantised.htmax[index],
                      color->alpha) ;
    color->unpacked.channel[index].cv = color->alpha ;
  }

  /* Force re-packing of blit color. */
#ifdef ASSERT_BUILD
  color->valid = blit_color_quantised;
#endif
  blit_color_pack(color) ;
}

/*---------------------------------------------------------------------------*/

Bool can_expand_pixels_1xN(blit_color_t *color,
                           unsigned int nexpanded,
                           const int blit_to_expanded[])
{
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  VERIFY_OBJECT(color->map, BLIT_MAP_NAME) ;

  /* To use the 1x8 or 1x16 expanders, we need one expanded channel, we
     need one blit color channel (so we don't have to search for the right
     channel), and the channel cannot be Alpha (so we don't have to map it
     back to the unpacked alpha). */
  return nexpanded == 1 && blit_to_expanded[0] == 0
    && color->nchannels == 1 && color->map->alpha_index != 0 ;
}

/** Pixel extracter for knockout rotated images. This has very little to do,
    because all of the pixels are the same color, and the color has already
    been setup in the blit color. */
static inline void pixels_generic_knockout(blit_color_t *color,
                                           const void **buffer,
                                           int32 *npixels,
                                           unsigned int nexpanded,
                                           const int blit_to_expanded[])
{
  UNUSED_PARAM(blit_color_t *, color) ;
  UNUSED_PARAM(const void **, buffer) ;
  UNUSED_PARAM(int32 *, npixels) ;
  UNUSED_PARAM(const int *, blit_to_expanded) ;
  UNUSED_PARAM(unsigned int, nexpanded) ;
}

/* ---------------------------------------------------------------------- */
/** \fn pixels_generic_1x8_forward
    Generic pixel extracter for 1 channel, 8 bits, forward row order. */
#ifndef DOXYGEN_SKIP
#define FUNCTION pixels_generic_1x8_forward
#define EXPAND_BITS 8
#define BLIT_COLOR_PACK blit_color_pack
#include "imgpixels1f.h"
#endif /* !DOXYGEN_SKIP */

/** 8-bit forward pixel extracter, for use when no optimised extracter is
    found. */
static inline void pixels_generic_Nx8_forward(blit_color_t *color,
                                              const void **buffer,
                                              int32 *npixels,
                                              unsigned int nexpanded,
                                              const int blit_to_expanded[])
{
  const uint8 *first, *current, *next, *limit ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(buffer != NULL, "Nowhere to find expansion buffer") ;
  HQASSERT(blit_to_expanded != NULL, "No expander to blit channel mapping") ;
  HQASSERT(npixels != NULL, "Nowhere to find number of pixels") ;
  HQASSERT(*npixels > 0, "No pixels to expand") ;
  HQASSERT(nexpanded > 0, "No expanded data channels") ;

  current = first = *buffer ;
  HQASSERT(current != NULL, "No expansion buffer") ;

  /* Expand this color into the blit entry. */
  im_expand_color_8(color, current, nexpanded, blit_to_expanded) ;

  /* Determine how many pixels were the same by raw byte comparison, then
     adjust the number of pixels based on the number of similar bytes. */
  next = current + nexpanded ;
  limit = current + nexpanded * *npixels ;

  if ( nexpanded >= sizeof(uint32)
#ifndef Unaligned_32bit_access
       /* Pointers must be aligned to used this if no unaligned access. */
       /** \todo ajcd 2008-10-17: Not quite true. They need to have the same
           alignment, the first few bytes can be aligned. */
       && (((uintptr_t)current | (uintptr_t)next) & (sizeof(uint32) - 1)) == 0
#endif
       ) {
    const uint8 *limit32 = limit - sizeof(uint32) ;
    while ( next <= limit32 && *(const uint32 *)current == *(const uint32 *)next ) {
      current += sizeof(uint32) ;
      next += sizeof(uint32) ;
    }
  } else if ( nexpanded >= sizeof(uint16)
#ifndef Unaligned_32bit_access
              /* Pointers must be aligned if no unaligned access. */
              && (((uintptr_t)current | (uintptr_t)next) & (sizeof(uint16) - 1)) == 0
#endif
       ) {
    const uint8 *limit16 = limit - sizeof(uint16) ;
    while ( next <= limit16 && *(const uint16 *)current == *(const uint16 *)next ) {
      current += sizeof(uint16) ;
      next += sizeof(uint16) ;
    }
  }

  /* This loop should use as few registers as possible for performance; on
     x86 processors, this makes a noticeable difference. */
  while ( next < limit && *current == *next ) {
    ++current, ++next ;
  }

  *npixels = (int32)((next - first) / nexpanded) ;
  *buffer = first + nexpanded * *npixels ;
}

/** \fn pixels_generic_1x8_backward
    Generic pixel extracter for 1 channel, 8 bits, backward row order. */
#ifndef DOXYGEN_SKIP
#define FUNCTION pixels_generic_1x8_backward
#define EXPAND_BITS 8
#define BLIT_COLOR_PACK blit_color_pack
#include "imgpixels1b.h"
#endif /* !DOXYGEN_SKIP */

/** 8-bit backward pixel extracter, for use when no optimised extracter is
    found. */
static inline void pixels_generic_Nx8_backward(blit_color_t *color,
                                               const void **buffer,
                                               int32 *npixels,
                                               unsigned int nexpanded,
                                               const int blit_to_expanded[])
{
  const uint8 *first, *current, *next, *limit ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(buffer != NULL, "Nowhere to find expansion buffer") ;
  HQASSERT(npixels != NULL, "Nowhere to find number of pixels") ;
  HQASSERT(*npixels > 0, "No pixels to expand") ;
  HQASSERT(blit_to_expanded != NULL, "No expander to blit channel mapping") ;
  HQASSERT(nexpanded > 0, "No expanded data channels") ;

  current = first = *buffer ;
  HQASSERT(current != NULL, "No expansion buffer") ;

  /* Expand this color into the blit entry. */
  im_expand_color_8(color, current, nexpanded, blit_to_expanded) ;

  /* Determine how many pixels were the same by raw byte comparison, then
     adjust the number of pixels based on the number of similar bytes. */
  next = current + nexpanded ;
  limit = next - nexpanded * *npixels ; /* Same as current if *npixels == 1 */

  if ( nexpanded >= sizeof(uint32)
#ifndef Unaligned_32bit_access
       /* Pointers must be aligned if no unaligned access */
       && (((uintptr_t)current | (uintptr_t)next) & (sizeof(uint32) - 1)) == 0
#endif
       ) {
    const uint8 *limit32 = limit + sizeof(uint32) ;
    while ( current >= limit32 &&
            ((const uint32 *)current)[-1] == ((const uint32 *)next)[-1] ) {
      current -= sizeof(uint32) ;
      next -= sizeof(uint32) ;
    }
  } else if ( nexpanded >= sizeof(uint16)
#ifndef Unaligned_32bit_access
              /* Pointers must be aligned if no unaligned access */
              && (((uintptr_t)current | (uintptr_t)next) & (sizeof(uint16) - 1)) == 0
#endif
       ) {
    const uint8 *limit16 = limit + sizeof(uint16) ;
    while ( current >= limit16 &&
            ((const uint16 *)current)[-1] == ((const uint16 *)next)[-1] ) {
      current -= sizeof(uint16) ;
      next -= sizeof(uint16) ;
    }
  }

  /* This loop should use as few registers as possible for performance; on
     x86 processors, this makes a noticeable difference. */
  while ( current > limit && current[-1] == next[-1] ) {
    --current, --next ;
  }

  *npixels = (int32)((first - current) / nexpanded) + 1 ;
  *buffer = first - nexpanded * *npixels ;
}

/* ---------------------------------------------------------------------- */

/** \fn pixels_generic_1x16_forward
    Generic pixel extracter for 1 channel, 16 bits, forward row order. */
#ifndef DOXYGEN_SKIP
#define FUNCTION pixels_generic_1x16_forward
#define EXPAND_BITS 16
#define BLIT_COLOR_PACK blit_color_pack
#include "imgpixels1f.h"
#endif /* !DOXYGEN_SKIP */

/** 16-bit forward pixel extracter, for use when no optimised extracter is
    found. */
static inline void pixels_generic_Nx16_forward(blit_color_t *color,
                                               const void **buffer,
                                               int32 *npixels,
                                               unsigned int nexpanded,
                                               const int blit_to_expanded[])
{
  const uint16 *first, *current, *next, *limit ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(buffer != NULL, "Nowhere to find expansion buffer") ;
  HQASSERT(blit_to_expanded != NULL, "No expander to blit channel mapping") ;
  HQASSERT(npixels != NULL, "Nowhere to find number of pixels") ;
  HQASSERT(*npixels > 0, "No pixels to expand") ;
  HQASSERT(nexpanded > 0, "No expanded data channels") ;

  current = first = *buffer ;
  HQASSERT(current != NULL, "No expansion buffer") ;

  /* Expand this color into the blit entry. */
  im_expand_color_16(color, current, nexpanded, blit_to_expanded) ;

  /* Determine how many pixels were the same by raw byte comparison, then
     adjust the number of pixels based on the number of similar bytes. */
  next = current + nexpanded ;
  limit = current + nexpanded * *npixels ;

  if ( nexpanded >= sizeof(uint32) / sizeof(uint16)
#ifndef Unaligned_32bit_access
       /* Pointers must be aligned if no unaligned access */
       && (((uintptr_t)current | (uintptr_t)next) & (sizeof(uint32) - 1)) == 0
#endif
       ) {
    const uint16 *limit32 = limit - sizeof(uint32) / sizeof(uint16) ;
    while ( next <= limit32 && *(const uint32 *)current == *(const uint32 *)next ) {
      current += sizeof(uint32) / sizeof(uint16) ;
      next += sizeof(uint32) / sizeof(uint16) ;
    }
  }

  /* This loop should use as few registers as possible for performance; on
     x86 processors, this makes a noticeable difference. */
  while ( next < limit && *current == *next ) {
    ++current, ++next ;
  }

  *npixels = (int32)((next - first) / nexpanded) ;
  *buffer = first + nexpanded * *npixels ;
}

/** \fn pixels_generic_1x16_backward
    Generic pixel extracter for 1 channel, 16 bits, backward row order. */
#ifndef DOXYGEN_SKIP
#define FUNCTION pixels_generic_1x16_backward
#define EXPAND_BITS 16
#define BLIT_COLOR_PACK blit_color_pack
#include "imgpixels1b.h"
#endif /* !DOXYGEN_SKIP */

/** Generic backward pixel extracter, for use when no optimised extracter is
    found. */
static inline void pixels_generic_Nx16_backward(blit_color_t *color,
                                                const void **buffer,
                                                int32 *npixels,
                                                unsigned int nexpanded,
                                                const int blit_to_expanded[])
{
  const uint16 *first, *current, *next, *limit ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(buffer != NULL, "Nowhere to find expansion buffer") ;
  HQASSERT(npixels != NULL, "Nowhere to find number of pixels") ;
  HQASSERT(*npixels > 0, "No pixels to expand") ;
  HQASSERT(blit_to_expanded != NULL, "No expander to blit channel mapping") ;
  HQASSERT(nexpanded > 0, "No expanded data channels") ;

  current = first = *buffer ;
  HQASSERT(current != NULL, "No expansion buffer") ;

  /* Expand this color into the blit entry. */
  im_expand_color_16(color, current, nexpanded, blit_to_expanded) ;

  /* Determine how many pixels were the same by raw byte comparison, then
     adjust the number of pixels based on the number of similar bytes. */
  next = current + nexpanded ;
  limit = next - nexpanded * *npixels ; /* Same as current if *npixels == 1 */

  if ( nexpanded >= sizeof(uint32) / sizeof(uint16)
#ifndef Unaligned_32bit_access
       /* Pointers must be aligned if no unaligned access */
       && (((uintptr_t)current | (uintptr_t)next) & (sizeof(uint32) - 1)) == 0
#endif
       ) {
    const uint16 *limit32 = limit + sizeof(uint32) / sizeof(uint16) ;
    while ( current >= limit32 &&
            ((const uint32 *)current)[-1] == ((const uint32 *)next)[-1] ) {
      current -= sizeof(uint32) / sizeof(uint16) ;
      next -= sizeof(uint32) / sizeof(uint16) ;
    }
  }

  /* This loop should use as few registers as possible for performance; on
     x86 processors, this makes a noticeable difference. */
  while ( current > limit && current[-1] == next[-1] ) {
    --current, --next ;
  }

  *npixels = (int32)((first - current) / nexpanded) + 1 ;
  *buffer = first - nexpanded * *npixels ;
}

/* ---------------------------------------------------------------------- */
/* Do the span calculation for imagemask. */

static inline void mask_orth_generic_row(render_blit_t *rb,
                                         const imgblt_params_t *params,
                                         const void *expanded,
                                         dcoord y1, dcoord y2)
{
  dcoord xprev, x1clip, x2clipp1 ;
  image_dda_t u = params->xs ;
  const uint8 *values = expanded ;
  int32 rw = params->ncols ;
  int32 dcol = params->dcol ;

  rb->ylineaddr = BLIT_ADDRESS(theFormA(*rb->outputform),
    (y1 - theFormHOff(*rb->outputform) - rb->y_sep_position) *
     theFormL(*rb->outputform)) ;
  rb->ymaskaddr = BLIT_ADDRESS(theFormA(*rb->clipform),
    (y1 - theFormHOff(*rb->clipform) - rb->y_sep_position) *
     theFormL(*rb->clipform)) ;

  x1clip = rb->p_ri->clip.x1 ;
  x2clipp1 = rb->p_ri->clip.x2 + 1 ;
  INLINE_RANGE32(xprev, u.i, x1clip, x2clipp1) ;

  do {
    int32 npixels ;
    dcoord x1, x2 ;

    /* Ignore the bits we don't want to fill. Avoid dereferencing expander
       buffer if there are no more values to check. */
    npixels = 0 ;
    while ( *values == 0 ) {
      values += dcol ;
      if ( ++npixels == rw ) /* There may be no set pixels in the row. */
        return ;
    }

    if ( npixels > 0 ) {
      rw -= npixels ;
      IMAGE_DDA_STEP_N_LG2(u, params->xperwh, params->basis, npixels) ;
      xprev = u.i ;
    }

    /* Fill as big a block as we can. Avoid dereferencing expander buffer if
       there are not more values to check. */
    npixels = 0 ;
    while ( *values != 0 ) {
      values += dcol ;
      if ( ++npixels == rw )
        break ;
    }

    HQASSERT(npixels > 0, "No pixels to fill") ;
    rw -= npixels ;

    IMAGE_DDA_STEP_N_LG2(u, params->xperwh, params->basis, npixels) ;

    /* We need to test for degenerate pixels anyway, so combine it with a
       directional test to determine whether we may have stepped over the end
       of the clip boundary. */
    if ( u.i > xprev ) {
      x1 = xprev ;
      INLINE_MIN32(x2, x2clipp1, u.i) ;
    } else if ( u.i < xprev ) {
      INLINE_MAX32(x1, x1clip, u.i) ;
      x2 = xprev ;
    } else {
      continue ; /* Degenerate source pixel. */
    }

    DO_BLOCK(rb, y1, y2, x1, x2 - 1) ;

    xprev = u.i ;
  } while ( rw != 0 ) ;
}

static inline void mask_orth_generic_col(render_blit_t *rb,
                                         const imgblt_params_t *params,
                                         const void *expanded,
                                         dcoord x1, dcoord x2)
{
  dcoord yprev, y1clip, y2clipp1 ;
  image_dda_t u = params->ys ;
  const uint8 *values = expanded ;
  int32 rw = params->ncols ;
  int32 dcol = params->dcol ;

  y1clip = rb->p_ri->clip.y1 ;
  y2clipp1 = rb->p_ri->clip.y2 + 1 ;
  INLINE_RANGE32(yprev, u.i, y1clip, y2clipp1) ;

  do {
    int32 npixels ;
    dcoord y1, y2 ;

    /* Ignore the bits we don't want to fill. Avoid dereferencing expander
       buffer if there are no more values to check. */
    npixels = 0 ;
    while ( *values == 0 ) {
      values += dcol ;
      if ( ++npixels == rw ) /* There may be no set pixels in the row. */
        return ;
    }

    if ( npixels > 0 ) {
      rw -= npixels ;
      IMAGE_DDA_STEP_N_LG2(u, params->yperhw, params->basis, npixels) ;
      yprev = u.i ;
    }

    /* Fill as big a block as we can. Avoid dereferencing expander buffer if
       there are not more values to check. */
    npixels = 0 ;
    while ( *values != 0 ) {
      values += dcol ;
      if ( ++npixels == rw )
        break ;
    }

    HQASSERT(npixels > 0, "No pixels to fill") ;
    rw -= npixels ;

    IMAGE_DDA_STEP_N_LG2(u, params->yperhw, params->basis, npixels) ;

    /* We need to test for degenerate pixels anyway, so combine it with a
       directional test to determine whether we may have stepped over the end
       of the clip boundary. */
    if ( u.i > yprev ) {
      y1 = yprev ;
      INLINE_MIN32(y2, y2clipp1, u.i) ;
    } else if ( u.i < yprev ) {
      INLINE_MAX32(y1, y1clip, u.i) ;
      y2 = yprev ;
    } else {
      continue ; /* Degenerate source pixel. */
    }

    rb->ylineaddr = BLIT_ADDRESS(theFormA(*rb->outputform),
      (y1 - theFormHOff(*rb->outputform) - rb->y_sep_position) *
       theFormL(*rb->outputform)) ;
    rb->ymaskaddr = BLIT_ADDRESS(theFormA(*rb->clipform),
      (y1 - theFormHOff(*rb->clipform) - rb->y_sep_position) *
       theFormL(*rb->clipform)) ;

    DO_BLOCK(rb, y1, y2 - 1, x1, x2) ;

    yprev = u.i ;
  } while ( rw != 0 ) ;
}

/* ---------------------------------------------------------------------- */
/* Tiled rotated images. The first inclusion of imgfillrt.h will define the
   support macros and functions needed for the rotated tiled masks, as well
   as defining the rotated tiled image row function. */

/** \fn rfill
    Generic row fill function using diamond_fill for rotated images. */
#ifndef DOXYGEN_SKIP
#define FUNCTION rfill
#define PIXEL_FN(params) (params->pixel_fn)
#define RENDER_IMAGE_TILE NFILL_IMAGE_TILE
#include "imgfillrt.h"
#endif /* !DOXYGEN_SKIP */

/** \fn rfill_tiled
    Generic row fill function using char blits for rotated images. */
#ifndef DOXYGEN_SKIP
#define FUNCTION rfill_tiled
#define PIXEL_FN(params) (params->pixel_fn)
#define RENDER_IMAGE_TILE CHAR_IMAGE_TILE
#include "imgfillrt.h"
#endif /* !DOXYGEN_SKIP */

/* ---------------------------------------------------------------------- */

/** Pixel extracter for masks, forward order. */
static inline void pixels_mask_forward(blit_color_t *color,
                                       const void **buffer,
                                       int32 *npixels,
                                       unsigned int nexpanded,
                                       const int blit_to_expanded[])
{
  const uint8 *current = *buffer ;
  uint8 value = *current ;
  int32 remaining = *npixels ;

  UNUSED_PARAM(blit_color_t *, color) ;
  UNUSED_PARAM(const int *, blit_to_expanded) ;
  UNUSED_PARAM(unsigned int, nexpanded) ;

  do {
    ++current ;
    --remaining ;
  } while ( remaining != 0 && *current == value ) ;

  *buffer = current ;
  *npixels -= remaining ;
}

/** Pixel extracter for masks, backward order. */
static inline void pixels_mask_backward(blit_color_t *color,
                                        const void **buffer,
                                        int32 *npixels,
                                        unsigned int nexpanded,
                                        const int blit_to_expanded[])
{
  const uint8 *current = *buffer ;
  uint8 value = *current ;
  int32 remaining = *npixels ;

  UNUSED_PARAM(blit_color_t *, color) ;
  UNUSED_PARAM(const int *, blit_to_expanded) ;
  UNUSED_PARAM(unsigned int, nexpanded) ;

  do {
    --current ;
    --remaining ;
  } while ( remaining != 0 && *current == value ) ;

  *buffer = current ;
  *npixels -= remaining ;
}

/** \fn rmask
    Generic row fill function using diamond_fill for rotated masks. */
#ifndef DOXYGEN_SKIP
#define FUNCTION rmask
#define DOING_MASK
#define PIXEL_FN(params) (params->pixel_fn)
#define RENDER_IMAGE_TILE NFILL_IMAGE_TILE
#include "imgfillrt.h"
#endif /* !DOXYGEN_SKIP */

/** \fn rmask_tiled
    Generic row fill function using char blits for rotated masks. */
#ifndef DOXYGEN_SKIP
#define FUNCTION rmask_tiled
#define DOING_MASK
#define PIXEL_FN(params) (params->pixel_fn)
#define RENDER_IMAGE_TILE CHAR_IMAGE_TILE
#include "imgfillrt.h"
#endif /* !DOXYGEN_SKIP */

/* Log stripped */
