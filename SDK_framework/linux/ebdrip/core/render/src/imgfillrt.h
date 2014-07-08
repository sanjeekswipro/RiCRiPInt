/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!src:imgfillrt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2010-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This file contains the definition of the optimised row fill functions for
 * rotated tiled images.
 *
 * On inclusion, these macros should be defined:
 *
 * The macro FUNCTION expands to the function name to be defined.
 *
 * The macro PIXEL_FN(params) expands to the pixel extracter function to call.
 *
 * The macro RENDER_IMAGE_TILE expands to a macro or function to call to
 * render an image tile. This will normally be CHAR_IMAGE_TILE or
 * NFILL_IMAGE_TILE.
 *
 * The macro DOING_MASK should be defined if rendering an image mask.
 *
 * The function parameters are:
 *
 *   rb               - The render_blit_t state pointer.
 *   params           - The collected image blit parameters.
 *   expanded         - A pointer to the expanded image data.
 *   ncols            - The number of source image columns to render
 *   drx              - The X coordinate difference between rows
 *   dry              - The Y coordinate difference between rows
 *   refx             - The reference X coordinate of the first source pixel
 *   refy             - The reference Y coordinate of the first source pixel
 *   inclip           - The inner clip box (a bounding box such that if the
 *                      reference point is inside, the source pixel is
 *                      entirely unclipped).
 *
 * This file is included multiple times, so should NOT have a guard around
 * it.
 */

#ifndef CHAR_IMAGE_TILE
/** Character-based routine to blit an image tile in the right location. */
#define CHAR_IMAGE_TILE(rb, params, tiles, tx, ty, drx, dry, dcx, dcy) MACRO_START \
  TILE *_tile_ = ROWTILES_TILE(*tiles, dcx, dcy) ;                      \
  FORM *_tform_ ;                                                       \
  dcoord bx, by ;                                                       \
                                                                        \
  HQASSERT(_tile_ != NULL, "Image tile not found") ;                    \
  HQASSERT(theITileDxMaj(_tile_) == drx, "drx doesn't match tile") ;    \
  HQASSERT(theITileDyMaj(_tile_) == dry, "dry doesn't match tile") ;    \
  HQASSERT(theITileDxMin(_tile_) == dcx, "dcx doesn't match tile") ;    \
  HQASSERT(theITileDyMin(_tile_) == dcy, "dcy doesn't match tile") ;    \
                                                                        \
  bx = tx + theITileDx(_tile_);                                         \
  by = ty + theITileDy(_tile_);                                         \
                                                                        \
  /* plonk tile down at appropriate place */                            \
  _tform_ = theITileForm(_tile_) ;                                      \
  if ( theFormH(*_tform_) ) {                                           \
    switch ( theFormT(*_tform_) ) {                                     \
    case FORMTYPE_CACHERLE1:                                            \
    case FORMTYPE_CACHERLE2:                                            \
    case FORMTYPE_CACHERLE3:                                            \
    case FORMTYPE_CACHERLE4:                                            \
    case FORMTYPE_CACHERLE5:                                            \
    case FORMTYPE_CACHERLE6:                                            \
    case FORMTYPE_CACHERLE7:                                            \
    case FORMTYPE_CACHERLE8:                                            \
      rlechar(rb, _tform_, bx, by) ;                                    \
      break ;                                                           \
    case FORMTYPE_CACHEBITMAP:                                          \
      DO_CHAR(rb, _tform_, bx, by) ;                                    \
      break ;                                                           \
    case FORMTYPE_BLANK:                                                \
      break ;                                                           \
    default:                                                            \
      HQFAIL("Invalid form type rendering image tile") ;                \
    }                                                                   \
  }                                                                     \
MACRO_END
#endif /* !defined(CHAR_IMAGE_TILE) */

#ifndef NFILL_IMAGE_TILE
/** NFill-based routine to blit an image tile in the right location. */
#define NFILL_IMAGE_TILE(rb, params, tiles, tx, ty, drx, dry, dcx, dcy) MACRO_START \
  /* Only draw pixel if not degenerate roundings. */               \
  dcoord cross = dcx * dry - drx * dcy ;                           \
  if ( SIGN32(cross) == params->cross_sign ) {                     \
    diamond_fill(rb, tx, ty, drx, dry, dcx, dcy) ;                 \
  }                                                                \
MACRO_END
#endif /* !defined(NFILL_IMAGE_TILE) */

/* High-speed rotated images; possible source pixel shapes are pre-calculated
   and stored in tile forms. These are blitted into the band in three sections;
   the first section contains tiles which are crossed by the top band boundary
   or which could be affected by clipping, the second section contains tiles
   which are contained entirely within the non-clipped area of the band, and
   the last section contains tiles which are crossed by the bottom band
   boundary or which could be affected by clipping at the right */
static imageinline void FUNCTION(render_blit_t *rb,
                                 const imgblt_params_t *params,
                                 const void *expanded, int32 ncols,
                                 const dcoord drx, const dcoord dry,
                                 image_dda_t refx, image_dda_t refy,
                                 const dbbox_t *inclip)
{
#ifndef DOING_MASK
  const blit_slice_t *slice ;
#endif
  const image_dda_t *stepx, *stepy ;
  uint8 clipmode = rb->clipmode ;
  ROWTILES *tiles ;
  /* Before, in, or after unclipped region? Set this to after if there is
     no unclipped region, so we don't waste time testing. */
  enum {
    Before_UC, In_UC, After_UC
  } state = bbox_is_empty(inclip) ? After_UC : Before_UC ;

  VERIFY_OBJECT(params, IMGBLT_PARAMS_NAME) ;
  HQASSERT(expanded != NULL, "No expanded values") ;
  HQASSERT(ncols > 0, "No columns to render") ;

#ifndef DOING_MASK
  slice = &rb->p_ri->surface->baseblits[0] ;
#endif
  tiles = &IMAGE_ROWTILES(*params->tiles, drx, dry) ;

  /* If iterating the columns in reverse order, start one column further so the
     dcx, dcy is the delta for *this* column, not the next one. */
  if ( params->dcol < 0 ) {
    IMAGE_DDA_STEP_1(refx, params->xperwh, params->basis, &refx) ;
    IMAGE_DDA_STEP_1(refy, params->yperwh, params->basis, &refy) ;
    stepx = &params->nxperw ; stepy = &params->nyperw ;
  } else {
    stepx = &params->xperwh ; stepy = &params->yperwh ;
  }

  do {
    int32 npixels = ncols ;
#ifdef DOING_MASK
    Bool skip = !*(uint8 *)expanded ;
#else
    Bool new_color = TRUE ;
#endif
    PIXEL_FN(params)(rb->color, &expanded, &npixels, params->converted_comps,
                     params->blit_to_expanded) ;
    HQASSERT(npixels > 0 && npixels <= ncols, "Wrong number of pixels expanded") ;
    ncols -= npixels ;

#ifdef DOING_MASK
    if ( skip ) {
      IMAGE_DDA_STEP_N_LG2(refx, *stepx, params->basis, npixels) ;
      IMAGE_DDA_STEP_N_LG2(refy, *stepy, params->basis, npixels) ;
    } else
#endif
    do {
      dcoord dcx, dcy, tx, ty ;

      tx = refx.i ; ty = refy.i ;
      IMAGE_DDA_STEP_1(refx, *stepx, params->basis, &refx) ;
      IMAGE_DDA_STEP_1(refy, *stepy, params->basis, &refy) ;
      if ( params->dcol < 0 ) {
        /* Translation of tile is new position. */
        dcx = tx - refx.i ; tx = refx.i ;
        dcy = ty - refy.i ; ty = refy.i ;
      } else {
        dcx = refx.i - tx ; dcy = refy.i - ty ;
      }

      if ( dcx | dcy ) {
        /* Reset clipmode and blits at transition between unclipped area and
           clipped area. */
        if ( state != After_UC ) {
          if ( bbox_contains_point(inclip, tx, ty) ) {
            if ( state == Before_UC ) {
              rb->clipmode = BLT_CLP_NONE ;
              state = In_UC ;
#ifndef DOING_MASK
              new_color = TRUE ;
#endif
            }
          } else {
            if ( state == In_UC ) {
              rb->clipmode = clipmode ;
              state = After_UC ;
#ifndef DOING_MASK
              new_color = TRUE ;
#endif
            }
          }
        }

#ifndef DOING_MASK
        if ( new_color ) { /* Reset blits for new color. */
          SET_BLITS_CURRENT(rb->blits, BASE_BLIT_INDEX,
                            &slice[BLT_CLP_NONE],
                            &slice[BLT_CLP_RECT],
                            &slice[BLT_CLP_COMPLEX]) ;
          new_color = FALSE ;
        }
#endif

        RENDER_IMAGE_TILE(rb, params, tiles, tx, ty, drx, dry, dcx, dcy) ;
      }
    } while ( --npixels > 0 ) ;
  } while ( ncols > 0 ) ;
  rb->clipmode = clipmode ;
}

#undef FUNCTION
#undef PIXEL_FN
#undef DOING_MASK
#undef RENDER_IMAGE_TILE

/* Log stripped */
