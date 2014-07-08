/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlgraphicsstate.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * As the RIP process a PCLXL data stream
 * it maintains a "graphics context"
 * which we represent/hold in the structure defined in this file.
 * We also maintain a "stack" of graphic states beneath the PCLXL "context"
 */

#include "core.h"
#include "pclxlgraphicsstate.h"

#include "mm.h"
#include "gstack.h"
#include "gstate.h"
#include "system.h"    /* gs_reservecliprec */
#include "graphics.h"
#include "hqmemcmp.h"

#include "pclxlerrors.h"
#include "pclxlpsinterface.h"
#include "pclxlattributes.h"
#include "pclxlpage.h"
#include "pclxldebug.h"
#include "pclxlpattern.h"
#include "pclxlfont.h"

#ifdef DEBUG_BUILD
#include "pclxltest.h"
#endif

void
matrix_set_shear(OMATRIX* matrix, SYSTEMVALUE sx, SYSTEMVALUE sy)
{
  matrix->matrix[0][0] = 1;

  /**
   * \todo I am not sure whether I have got these
   * two shear factors the correct way round
   */

  matrix->matrix[1][0] = (- sx);
  matrix->matrix[0][1] = (- sy);

  matrix->matrix[1][1] = 1;
  matrix->matrix[2][0] = 0;
  matrix->matrix[2][1] = 0;

  MATRIX_SET_OPT_BOTH(matrix);
}

/*
 * pclxl_recalculate_font_matrix() takes an existing/current page CTM
 * and character angle, scale and shear factors
 * and then calculates a resultant character CTM
 *
 * Note that it explicitly does not know about the graphics state itself
 * It is upto the caller to retrieve the character angle, scale and shear
 * from the graphics if they are the values required.
 * And it is upto the caller whether they then want the resultant character CTM
 * to be stored back into a graphics state.
 */

OMATRIX*
pclxl_recalculate_font_matrix(PCLXL_CONTEXT pclxl_context,
                              PCLXL_GRAPHICS_STATE graphics_state,
                              PCLXL_CHAR_DETAILS char_details,
                              OMATRIX* font_matrix)
{
  static OMATRIX font_size;
  static OMATRIX page_transform, page_transform_inverse;
  static OMATRIX char_scale, char_shear, char_rotate;
  static OMATRIX* transforms[6][6] =
  {
    { &font_size, &page_transform, &char_rotate, &char_scale,  &char_shear,  &page_transform_inverse },
    { &font_size, &page_transform, &char_rotate, &char_shear,  &char_scale,  &page_transform_inverse },
    { &font_size, &page_transform, &char_scale,  &char_rotate, &char_shear,  &page_transform_inverse },
    { &font_size, &page_transform, &char_scale,  &char_shear,  &char_rotate, &page_transform_inverse },
    { &font_size, &page_transform, &char_shear,  &char_rotate, &char_scale,  &page_transform_inverse },
    { &font_size, &page_transform, &char_shear,  &char_scale,  &char_rotate, &page_transform_inverse }
  };

  OMATRIX char_ctm = { 1, 0, 0, -1, 0, 0, 1};

  uint8 i;

  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;

  PCLXL_SysVal char_size = (char_details->current_font.pclxl_char_size > 0.0 ?
                            char_details->current_font.pclxl_char_size :
                            (char_details->current_font.ps_font_point_size *
                             non_gs_state->units_per_measure.res_y /
                             pclxl_ps_units_per_pclxl_uom(non_gs_state->measurement_unit)));


  /*
   * It appears that the HP4700 makes some adjustment
   * to the requested character size that is
   * proportional to the character size
   *
   * However this adjustment seems to be "quantized" in some way
   *
   * The following character-size adjustment has been empirically derived
   * on the basis of the output produced by the HP4700
   */

  /*
   * We now need to "quantize" this char size into
   * a number of discrete ranges
   * and where it is within a particular range
   */

  uint32 mod_25 = ((char_size >= 1.0) ? (((uint32) char_size - 1) % 25) : 0);
  uint32 sub_range_length = (mod_25 >= 12 ? 13 : 12);
  uint32 sub_range_index = (mod_25 >= 12 ? (mod_25 - 12) : mod_25);
  uint32 sub_range_pos = (sub_range_index + 1);

  /*
   * We now need the "magic" correction factor
   * which is apparently a small fixed size
   * which is 0.01 mm
   */

  PCLXL_SysVal char_size_adj_unit = (
                (0.01 * pclxl_ps_units_per_pclxl_uom(PCLXL_eMillimeter)) *
                non_gs_state->units_per_measure.res_y /
                pclxl_ps_units_per_pclxl_uom(non_gs_state->measurement_unit));

  PCLXL_SysVal char_size_adjustment = (char_size_adj_unit * (1 + ((sub_range_length - sub_range_pos) / (PCLXL_SysVal) (sub_range_length - 1))));

  if (char_size > char_size_adjustment)
  {
    char_size = (char_size - char_size_adjustment);
  }

  /*
   * It appears that the HP4700 *sometimes* uses
   * both the current page scale *and* the current page rotation
   * (i.e. the CTM minus the initial media orientation)
   * in its glyph transform (a.k.a. font matrix) calculation
   * But on other occasions it uses only the current page scale
   * *without* the page rotation
   *
   * I have coded-up both versions of these two font matrix (re-)calculation below
   * and which one gets used depends upon the value of the current PCLXL graphics state's
   * set_page_rotation_op_count
   *
   * This count is only incremented when the corresponding set_page_scale_op_count is itself non-zero
   *
   * The purpose of these two "counts" is to allow all SetPageRotation operations
   * that are performed *before* the first SetPageScale operation
   * to effectively be "ignored" or treated as if they were actually just part
   * of establishing the page *orientation*
   *
   * See pclxl_create_graphics_state(), pclxl_set_page_scale(),
   * pclxl_set_page_rotation(), and pclxl_default_graphics_state()
   * for where these two counts are incremented and/or reset
   */

  if ( graphics_state->set_page_rotation_op_count > 0 )
  {
    OMATRIX current_ctm = graphics_state->current_ctm;
    OMATRIX logical_page_ctm = graphics_state->logical_page_ctm;

    MATRIX_20(&logical_page_ctm) = 0;
    MATRIX_21(&logical_page_ctm) = 0;

    MATRIX_20(&current_ctm) = 0;
    MATRIX_21(&current_ctm) = 0;

    if ( pclxl_ps_is_matrix_invertible(&logical_page_ctm) )
    {
      OMATRIX logical_page_ctm_inverse;

      matrix_inverse(&logical_page_ctm, &logical_page_ctm_inverse);

      matrix_mult(&graphics_state->current_ctm,
                  &logical_page_ctm_inverse,
                  &page_transform);
    }
    else
    {
      matrix_set_scale(&page_transform,
                       graphics_state->page_scale.x,
                       graphics_state->page_scale.y);
    }

    MATRIX_20(&page_transform) = 0;
    MATRIX_21(&page_transform) = 0;
  }
  else
  {
    matrix_set_scale(&page_transform,
                     graphics_state->page_scale.x,
                     graphics_state->page_scale.y);
  }

  if ( pclxl_ps_is_matrix_invertible(&page_transform) )
  {
    matrix_inverse(&page_transform, &page_transform_inverse);
  }
  else
  {
    page_transform_inverse = identity_matrix;
  }

  matrix_set_scale(&font_size,
                   char_size,
                   char_size);

  if ( char_details->current_font.ps_font_is_bitmapped ) {
    char_scale = char_shear = char_rotate = identity_matrix;
  } else {
    matrix_set_scale(&char_scale,
                   char_details->char_scale.x,
                   char_details->char_scale.y);

    matrix_set_shear(&char_shear,
                   char_details->char_shear.x,
                   char_details->char_shear.y);

  /*
   * Note that PCLXL interprets angles as positive meaning anti-clockwise
   * and negative meaning clockwise.
   *
   * Postscript does the same *BUT* ...
   *
   * Postscript uses a positive-y-axis to mean up the page
   * but PCLXL uses positive-y-axis to down the page
   *
   * So we need to negate the supplied angle when calculating
   * the rotation affect upon the CTM
   */

    matrix_set_rotation(&char_rotate,
                      (- char_details->char_angle));
  }
  /*
   * Then we combine each of these separate transformations
   * in a very specific order into a single font transformation matrix
   * because it has been empirically found to work in most cases
   * (i.e. *all* cases encountered so far).
   *
   * However there are still occasionally some obscure job
   * whose character transformation is going wrong.
   *
   * Hence the above un-rolled loop that is used to try variant
   * matrix transformation orders/permutations
   */

  for (i = 0 ;
       i < NUM_ARRAY_ITEMS(transforms) ;
       i++)
  {
    matrix_mult(&char_ctm,
                transforms[char_details->font_matrix_construction_order][i],
                &char_ctm);
  }

  /*
   * We can now return the resultant
   * scaled then sheared then rotated
   * character CTM
   *
   * Which I currently believe will be
   * used as a font matrix within pclxl_ps_set_font()
   *
   * Note that having included the page scale and rotate
   * transforms into the font matrix
   * we must remember to temporarily revert to the logical page CTM
   * for the duration of the call to plotchar()
   */

  *font_matrix = char_ctm;

  PCLXL_DEBUG((PCLXL_DEBUG_PAGE_CTM | PCLXL_DEBUG_FONTS),
              ("Font Matrix = [ %f, %f, %f, %f, %f, %f ]",
               MATRIX_00(&char_ctm),
               MATRIX_01(&char_ctm),
               MATRIX_10(&char_ctm),
               MATRIX_11(&char_ctm),
               MATRIX_20(&char_ctm),
               MATRIX_21(&char_ctm)));


  if ( char_details->current_font.pclxl_font_state > PCLXL_FS_PFIN_FONT_SELECTED )
    char_details->current_font.pclxl_font_state = PCLXL_FS_PFIN_FONT_SELECTED;

  return font_matrix;
}

#define PCLXL_FONT_MATRIX_SET_CHAR_ANGLE 0
#define PCLXL_FONT_MATRIX_SET_CHAR_SCALE 1
#define PCLXL_FONT_MATRIX_SET_CHAR_SHEAR 2

static uint8
pclxl_new_font_matrix_construction_order(PCLXL_CHAR_DETAILS char_details,
                                         uint8              font_matrix_operation)
{
  static uint8 next_font_matrix_construction_order[6][3] =
  {
    { /* PCLXL_FMCO_ANGLE_SCALE_SHEAR */
      PCLXL_FMCO_SCALE_SHEAR_ANGLE,
      PCLXL_FMCO_ANGLE_SHEAR_SCALE,
      PCLXL_FMCO_ANGLE_SCALE_SHEAR
    },
    { /* PCLXL_FMCO_ANGLE_SHEAR_SCALE */
      PCLXL_FMCO_SHEAR_SCALE_ANGLE,
      PCLXL_FMCO_ANGLE_SHEAR_SCALE,
      PCLXL_FMCO_ANGLE_SCALE_SHEAR
    },
    { /* PCLXL_FMCO_SCALE_ANGLE_SHEAR */
      PCLXL_FMCO_SCALE_SHEAR_ANGLE,
      PCLXL_FMCO_ANGLE_SHEAR_SCALE,
      PCLXL_FMCO_SCALE_ANGLE_SHEAR
    },
    { /* PCLXL_FMCO_SCALE_SHEAR_ANGLE */
      PCLXL_FMCO_SCALE_SHEAR_ANGLE,
      PCLXL_FMCO_SHEAR_ANGLE_SCALE,
      PCLXL_FMCO_SCALE_ANGLE_SHEAR
    },
    { /* PCLXL_FMCO_SHEAR_ANGLE_SCALE */
      PCLXL_FMCO_SHEAR_SCALE_ANGLE,
      PCLXL_FMCO_SHEAR_ANGLE_SCALE,
      PCLXL_FMCO_ANGLE_SCALE_SHEAR
    },
    { /* PCLXL_FMCO_SHEAR_SCALE_ANGLE */
      PCLXL_FMCO_SHEAR_SCALE_ANGLE,
      PCLXL_FMCO_SHEAR_ANGLE_SCALE,
      PCLXL_FMCO_SCALE_ANGLE_SHEAR
    }
  };

  return next_font_matrix_construction_order[char_details->font_matrix_construction_order][font_matrix_operation];
}

Bool
pclxl_reset_current_point(PCLXL_CONTEXT pclxl_context,
                          PCLXL_SysVal_XY* previous_point,
                          OMATRIX* previous_ctm,
                          OMATRIX* new_ctm,
                          PCLXL_SysVal_XY* new_point)
{
  if ( !pclxl_context->graphics_state->ctm_is_invertible )
  {
    pclxl_context->graphics_state->current_point_xy.x =
      pclxl_context->graphics_state->current_point_xy.y = 0.0;

    pclxl_context->graphics_state->current_path =
      pclxl_context->graphics_state->current_point = FALSE;
  }
  else if ( !pclxl_ps_transform_point(previous_point,
                                      previous_ctm,
                                      new_ctm,
                                      new_point) )
  {
    return FALSE;
  }
  else if ( !pclxl_ps_moveto(pclxl_context,
                             new_point->x,
                             new_point->y) )
  {
    return FALSE;
  }

#ifdef DEBUG_BUILD

  else if ( pclxl_context->config_params.debug_pclxl & PCLXL_DEBUG_CURSOR_POSITION )
  {
    (void) pclxl_dot(pclxl_context,
                     new_point->x,
                     new_point->y,
                     3, NULL);
  }

#endif

  return TRUE;
}

Bool
pclxl_set_page_scale(PCLXL_CONTEXT pclxl_context,
                     PCLXL_SysVal_XY* scale_factors)
{
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  if ( !pclxl_ps_scale(pclxl_context, scale_factors->x, scale_factors->y) )
  {
    return FALSE;
  }
  else
  {
    OMATRIX existing_ctm = graphics_state->current_ctm;

    graphics_state->page_scale.x *= scale_factors->x;
    graphics_state->page_scale.y *= scale_factors->y;

    graphics_state->set_page_scale_op_count++;

    (void) pclxl_get_current_ctm(pclxl_context,
                                 &graphics_state->current_ctm,
                                 &graphics_state->ctm_is_invertible);

    /*
     * It appears that we must also re-evaluate
     * the current position (if any)
     * in terms of this new CTM
     *
     * To derive a new user-coordinate under this new CTM
     * that is equivalent to the existing user-coordiate
     * under the previous CTM
     */

    if ( graphics_state->current_point )
    {
      return pclxl_reset_current_point(pclxl_context,
                                       &graphics_state->current_point_xy,
                                       &existing_ctm,
                                       &graphics_state->current_ctm,
                                       &graphics_state->current_point_xy);
    }
    else
    {
      return TRUE;
    }
  }
}

Bool
pclxl_set_page_rotation(PCLXL_CONTEXT pclxl_context,
                        PCLXL_Int32 rotation_angle)
{
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  /*
   * We must attempt to translate the page origin to this new position
   * and if successful we must record the new current CTM
   *
   * Note that because the PCLXL origin is top-left
   * with <y> increasing down the page
   * and a positive angle means a counter-clockwise rotation in PCLXL terms
   * But Postscript's native coordiate system has its origin bottom-left
   * with <y> increasing up the page (so we have applied a vertical inversion)
   * So we need to negate the angle
   */

  if ( !pclxl_ps_rotate(pclxl_context, (- rotation_angle)) )
  {
    return FALSE;
  }
  /*
   * It appears that we must also re-evaluate
   * the current position (if any)
   * in terms of this new CTM
   */
  else if ( (graphics_state->current_point) &&
            (!pclxl_ps_moveto(pclxl_context,
                              graphics_state->current_point_xy.x,
                              graphics_state->current_point_xy.y)) )
  {
    return FALSE;
  }
  else
  {
    /*
     * We *definitely* need to track the resultant page rotation angle
     * (and we appear to be able to do this in an *integer* data type
     * because rotations by an integral number of degrees)
     * because, whilst any (integral) number of degrees is supported
     * various other parts of the PCLXL PDL only accept "orthogonal" angles
     * (i.e. -360, -270, -180, -90, 0, 90, 180, 270, 360 etc.)
     */

    OMATRIX existing_ctm = graphics_state->current_ctm;

    graphics_state->page_angle += rotation_angle;

    if ( graphics_state->set_page_scale_op_count > 0 )
      graphics_state->set_page_rotation_op_count++;

#ifdef DEBUG_BUILD

    if ( pclxl_context->config_params.debug_pclxl & PCLXL_DEBUG_PAGE_CTM )
      (void) pclxl_debug_origin(pclxl_context);

#endif

    /*
     * I am not sure whether we need to recalculate and update
     * the current *character* CTM here also?
     * If it is to be updated, then we supply its location
     * Otherwise we supply a NULL location
     */

    (void) pclxl_get_current_ctm(pclxl_context,
                                 &graphics_state->current_ctm,
                                 &graphics_state->ctm_is_invertible);

    /*
     * It appears that we must also re-evaluate
     * the current position (if any)
     * in terms of this new CTM
     *
     * To derive a new user-coordinate under this new CTM
     * that is equivalent to the existing user-coordiate
     * under the previous CTM
     */

    if ( graphics_state->current_point )
    {
      return pclxl_reset_current_point(pclxl_context,
                                       &graphics_state->current_point_xy,
                                       &existing_ctm,
                                       &graphics_state->current_ctm,
                                       &graphics_state->current_point_xy);
    }
    else
    {
      return TRUE;
    }
  }

  /*NOTREACHED*/
}

/*
 * pclxl_create_color_space_details() creates an entirely new
 * PCLXL_COLOR_SPACE_DETAILS[_STRUCT] and initializes it as
 * either a default PCLXL color space
 * or as a clone of an existing graphics state
 *
 * Note that the initial "reference count" of this new
 * PCLXL_COLOR_SPACE_DETAILS will be 1
 *
 * It is anticipated that it will be incremented
 * when it is actually referenced by a PCLXL_PEN_SOURCE and/or PCLXL_BRUSH_SOURCE
 * and decremented as these structures are deallocated
 */

static PCLXL_COLOR_SPACE_DETAILS
pclxl_create_color_space_details(PCLXL_CONTEXT pclxl_context,
                                 PCLXL_COLOR_SPACE_DETAILS existing_color_space_details)
{
  PCLXL_COLOR_SPACE_DETAILS new_color_space_details;

  if ( ((new_color_space_details = mm_alloc(pclxl_context->memory_pool,
                                            sizeof(PCLXL_COLOR_SPACE_DETAILS_STRUCT),
                                            MM_ALLOC_CLASS_PCLXL_COLOR)) == NULL) )
  {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INSUFFICIENT_MEMORY,
                               ("Failed to allocate new color space details struct"));

    return NULL;
  }
  else if ( existing_color_space_details )
  {
    /*
     * We will do a quick'n'dirty copy of the non-NULL existing color space details
     * And then do a proper copy of the color palette array if any
     * And adjust the parent graphics_state pointer
     */

    *new_color_space_details = *existing_color_space_details;

    new_color_space_details->color_palette = NULL;

    new_color_space_details->color_palette_len = 0;

    pclxl_copy_color_palette(pclxl_context, new_color_space_details,
                             existing_color_space_details->color_palette,
                             existing_color_space_details->color_palette_len,
                             /*
                              * Note that we always specify e8Bit
                              * because the existing palette has already been unpacked
                              * so we actually always want a byte-for-byte copy
                              * with no unpacking
                              */
                             PCLXL_e8Bit);

    new_color_space_details->ref_count = 1;

    return new_color_space_details;
  }
  else
  {
#ifdef DEBUG_BUILD
    /* Catch use of uninitialised values */
    HqMemSet8((uint8 *)new_color_space_details, 0xff,
              sizeof(PCLXL_COLOR_SPACE_DETAILS_STRUCT));
#endif /* DEBUG_BUILD */

    new_color_space_details->color_palette_len = 0;

    new_color_space_details->color_space = PCLXL_eRGB;

    new_color_space_details->raster_black_type = PCLXL_eProcessBlack;
    new_color_space_details->text_black_type = PCLXL_eTonerBlack;
    new_color_space_details->vector_black_type = PCLXL_eTonerBlack;

    new_color_space_details->ref_count = 1;

    return new_color_space_details;
  }
}

/*
 * pclxl_get_writable_color_space_details()
 * *always* (well subject to memory allocation issues) returns
 * a PCLXL_COLOR_SPACE_DETAILS that is not being referenced
 * by a pen/brush source or indeed by more than 1 graphics state
 *
 * Therefore, by definition we can write to the returned PCLXL_COLOR_SPACE_DETAILS
 */

static PCLXL_COLOR_SPACE_DETAILS
pclxl_get_writable_color_space_details(PCLXL_CONTEXT pclxl_context,
                                       PCLXL_COLOR_SPACE_DETAILS existing_color_space_details)
{
  if ( !existing_color_space_details )
  {
    /*
     * We have been asked to create a writable color space (details)
     * when the existing color space details is NULL
     */

    PCLXL_COLOR_SPACE_DETAILS new_color_space_details =
      pclxl_create_color_space_details(pclxl_context, NULL);

    PCLXL_DEBUG(PCLXL_DEBUG_COLOR,
                ("Created new color space details 0x%08x",
                 new_color_space_details));

    return new_color_space_details;
  }
  else if ( existing_color_space_details->ref_count <= 1 )
  {

    PCLXL_DEBUG(PCLXL_DEBUG_COLOR,
                ("Updating color space 0x%08x in-situ because the reference count is %d",
                 existing_color_space_details,
                 existing_color_space_details->ref_count));

    return existing_color_space_details;
  }
  else
  {
    PCLXL_COLOR_SPACE_DETAILS new_color_space_details =
      pclxl_create_color_space_details(pclxl_context,
                                       existing_color_space_details);

    PCLXL_DEBUG(PCLXL_DEBUG_COLOR,
                ("Copying existing color space 0x%08x to new color space 0x%08x because the existing one has %d references",
                 existing_color_space_details,
                 new_color_space_details,
                 existing_color_space_details->ref_count));

    return new_color_space_details;
  }
}

static void
pclxl_delete_color_space_details(PCLXL_CONTEXT context,
                                 PCLXL_COLOR_SPACE_DETAILS color_space_details)
{
  if ( color_space_details )
  {
    /*
     * Note that under the new lazy/late copy of a color space details
     * we actually maintain a reference count inside each
     * PCLXL_COLOR_SPACE_DETAILS structure.
     *
     * This count is initialized to 1 and then incremented
     * each time some PCLXL_PEN_SOURCE or PCLXL_BRUSH_SOURCE references it.
     * It is then decremented again as each of these ceases referencing it
     *
     * We only actually free the allocated memory
     * when the *last* reference is deleted
     */

    if ( color_space_details->ref_count-- <= 1 )
    {
      pclxl_free_color_palette(context, color_space_details);

      mm_free(context->memory_pool, color_space_details,
              sizeof(PCLXL_COLOR_SPACE_DETAILS_STRUCT));
    }
  }
}

void
pclxl_set_color_depth(PCLXL_CONTEXT        pclxl_context,
                      PCLXL_GRAPHICS_STATE graphics_state,
                      PCLXL_ColorDepth     color_depth)
{
  PCLXL_COLOR_SPACE_DETAILS color_space_details;

  if ( ((color_space_details =
         pclxl_get_writable_color_space_details(pclxl_context,
                                                graphics_state->color_space_details)) == NULL) )
  {
    HQFAIL("Failed to obtain a writable (copy of) an existing color space");

    return;
  }
  else if ( graphics_state->color_space_details != color_space_details )
  {
    pclxl_delete_color_space_details(pclxl_context,
                                     graphics_state->color_space_details);

    graphics_state->color_space_details = color_space_details;
  }

  color_space_details->color_depth = color_depth;

  /**
   * \todo this new color depth
   * needs to be propagated into
   * Postscript graphics state
   */
}

void pclxl_set_color_mapping(PCLXL_CONTEXT        pclxl_context,
                             PCLXL_GRAPHICS_STATE graphics_state,
                             PCLXL_ColorMapping   color_mapping)
{
  PCLXL_COLOR_SPACE_DETAILS color_space_details;

  if ( ((color_space_details =
         pclxl_get_writable_color_space_details(pclxl_context,
                                                graphics_state->color_space_details)) == NULL) )
  {
    HQFAIL("Failed to obtain a writable (copy of) an existing color space");

    return;
  }
  else if ( graphics_state->color_space_details != color_space_details )
  {
    pclxl_delete_color_space_details(pclxl_context,
                                     graphics_state->color_space_details);

    graphics_state->color_space_details = color_space_details;
  }

  color_space_details->color_mapping = color_mapping;

  /**
   * \todo this new color mapping
   * needs to be propagated into
   * Postscript graphics state
   */
}

uint8*
pclxl_color_space_name(PCLXL_ColorSpace color_space)
{
  static uint8* color_space_names[] =
  {
    (uint8*) "0 (undefined)",
    (uint8*) "Gray",
    (uint8*) "RGB",
#ifdef DEBUG_BUILD
    (uint8*) "CMYK",
#else
    (uint8*) "3 (undefined)",
#endif
    (uint8*) "4 (undefined)",
    (uint8*) "5 (undefined)",
    (uint8*) "sRGB"
  };

  return color_space_names[color_space];
}

/* See header for doc. */
uint8 pclxl_color_space_num_components(PCLXL_ColorSpace color_space)
{
  switch (color_space) {
    case PCLXL_eGray:
      return 1 ;

    case PCLXL_eRGB:
    case PCLXL_eSRGB:
      return 3 ;

#ifdef DEBUG_BUILD
    case PCLXL_eCMYK:
      return 4;
#endif
  }

  HQFAIL("Invalid color space.") ;
  return 0 ;
}

Bool
pclxl_set_default_color(PCLXL_CONTEXT pclxl_context,
                        PCLXL_COLOR_SPACE_DETAILS color_space_details,
                        PCLXL_COLOR_DETAILS color_details)
{
  uint8 i;

  if (! pclxl_copy_color_space_details(pclxl_context,
                                       color_space_details,
                                       &color_details->color_space_details)) {
    return FALSE;
  }

  for ( i = 0 ;
        i < (sizeof(color_details->color_array) /
             sizeof(color_details->color_array[0])) ;
        i++ )
  {
    color_details->color_array[i] = 0.0;
  }

  color_details->color_array_len =
    pclxl_color_space_num_components(color_space_details->color_space);

  color_details->pattern_enabled = FALSE;

  return TRUE;
}

static PCLXL_COLOR_SPACE_DETAILS
pclxl_add_ref_to_color_space_details(PCLXL_CONTEXT             pclxl_context,
                                     PCLXL_COLOR_SPACE_DETAILS existing_color_space_details)
{
  UNUSED_PARAM(PCLXL_CONTEXT, pclxl_context);

  if ( existing_color_space_details ) existing_color_space_details->ref_count++;

  return existing_color_space_details;
}

/*
 * pclxl_copy_color_space_details() (now) performs
 * a "lazy copy" of an existing color space into a new location
 *
 * This "lazy copy" basically involves deleting any reference to any other color space details
 * that the "new color space details" may currently point to
 * and then simply pointing the "new color space details" (location)
 * at the existing color space details,
 * incrementing the existing color space details' reference count as it does so
 */

Bool
pclxl_copy_color_space_details(PCLXL_CONTEXT pclxl_context,
                               PCLXL_COLOR_SPACE_DETAILS  existing_color_space_details,
                               PCLXL_COLOR_SPACE_DETAILS* new_color_space_details)
{
  if ( *new_color_space_details != NULL )
  {
    pclxl_delete_color_space_details(pclxl_context,
                                     *new_color_space_details);

    *new_color_space_details = NULL;
  }

  *new_color_space_details =
    pclxl_add_ref_to_color_space_details(pclxl_context,
                                         existing_color_space_details);

  return (*new_color_space_details != NULL);
}

Bool
pclxl_set_color_space(PCLXL_CONTEXT pclxl_context,
                      PCLXL_GRAPHICS_STATE graphics_state,
                      PCLXL_ColorSpace color_space)
{
  PCLXL_COLOR_SPACE_DETAILS color_space_details;

  if ( ((color_space_details =
         pclxl_get_writable_color_space_details(pclxl_context,
                                                graphics_state->color_space_details)) == NULL) )
  {
    HQFAIL("Failed to obtain a writable (copy of) an existing color space");

    return FALSE;
  }
  else if ( graphics_state->color_space_details != color_space_details )
  {
    pclxl_delete_color_space_details(pclxl_context,
                                     graphics_state->color_space_details);

    graphics_state->color_space_details = color_space_details;
  }

  /*
   * We have an interesting problem here
   * because PCLXL provides a SetColorSpace operator
   * that can supply either a "direct" color space
   * or an "indexed" color space
   *
   * So if we have previously been supplied with an indexed color space
   * but are *now* supplied with a direct color space
   * we must clear out the indexed color palette
   */

  pclxl_free_color_palette(pclxl_context, color_space_details);

  color_space_details->color_space = color_space;

  /*
   * However we do *not* directly install this new color space
   * with Postscript, not least because PCLXL may assume
   * that some previous color (in another color space) is still in effect
   * but also because we are just about to follow this call to
   * pclxl_set_color_space() with a call to pclxl_set_color_depth(),
   * pclxl_set_color_treatment() and/or a call to pclxl_copy_color_palette()
   */

  /*
   * The PCLXL Protocol Class Specification 2.0
   * mandates that whenever the color space is changed
   * then the current pen and brush "source" colors
   * must be changed back to black.
   *
   * However it is quite clear from both
   * the Quality Logic PCLXL FTS test "t305.bin" reference output
   * *and* from the output produced by an hp4700n for this same test file
   * that the color is *not* changed
   * (and indeed that it is possible for a color that is not even valid
   * under the new color space to persist beyond this SetColorSpace operation)
   *
   * Therefore we only perform the reset-to-black
   * if we are adhering strictly to the protocol class specification
   */

  if ( pclxl_context->config_params.strict_pclxl_protocol_class )
  {
    return (
             pclxl_set_default_color(pclxl_context,
                                     color_space_details,
                                     &graphics_state->line_style.pen_source)
             &&
             pclxl_set_default_color(pclxl_context,
                                     color_space_details,
                                     &graphics_state->fill_details.brush_source)
           );
  }
  else
  {
    return TRUE;
  }
}

/* See header for doc. */
PCLXL_ColorSpace pclxl_get_colorspace(PCLXL_GRAPHICS_STATE graphics_state)
{
  return graphics_state->color_space_details->color_space;
}

void pclxl_set_color_treatment(PCLXL_CONTEXT        pclxl_context,
                               PCLXL_GRAPHICS_STATE graphics_state,
                               PCLXL_ColorTreatment color_treatment)
{
  PCLXL_COLOR_SPACE_DETAILS color_space_details;

  if ( ((color_space_details =
         pclxl_get_writable_color_space_details(pclxl_context,
                                                graphics_state->color_space_details)) == NULL) )
  {
    HQFAIL("Failed to obtain a writable (copy of) an existing color space");

    return;
  }
  else if ( graphics_state->color_space_details != color_space_details )
  {
    pclxl_delete_color_space_details(pclxl_context,
                                     graphics_state->color_space_details);

    graphics_state->color_space_details = color_space_details;
  }

  color_space_details->color_treatment = color_treatment;

  /**
   * \todo this new color treatment
   * needs to be propagated into
   * Postscript graphics state
   */
}

Bool
pclxl_set_black_types(PCLXL_CONTEXT         pclxl_context,
                      PCLXL_GRAPHICS_STATE  graphics_state,
                      PCLXL_BlackType       raster_black_type,
                      PCLXL_BlackType       text_black_type,
                      PCLXL_BlackType       vector_black_type)
{
  PCLXL_COLOR_SPACE_DETAILS color_space_details;

  if ( ((color_space_details =
        pclxl_get_writable_color_space_details(pclxl_context,
                                               graphics_state->color_space_details)) == NULL) )
  {
    HQFAIL("Failed to obtain a writable (copy of) an existing color space");

    return FALSE;
  }
  else if ( graphics_state->color_space_details != color_space_details )
  {
    pclxl_delete_color_space_details(pclxl_context,
                                     graphics_state->color_space_details);

    graphics_state->color_space_details = color_space_details;
  }

  color_space_details->raster_black_type = raster_black_type;
  color_space_details->text_black_type = text_black_type;
  color_space_details->vector_black_type = vector_black_type;

  return pclxl_ps_set_black_preservation(pclxl_context,
                                         color_space_details,
                                         FALSE);
}

Bool
pclxl_ps_set_line_dash(PCLXL_CONTEXT pclxl_context,
                       PCLXL_GRAPHICS_STATE graphics_state)
{
  if ( !gs_storedashlist(&gstateptr->thestyle,
                         graphics_state->line_style.line_dash,
                         graphics_state->line_style.line_dash_len) )
  {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               pclxl_context->parser_context,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to set line dash"));

    return FALSE;
  }

  gstateptr->thestyle.dashoffset = (USERVALUE)graphics_state->line_style.dash_offset;

  PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS, ("PS: setlinedash"));

  return TRUE;
}

Bool
pclxl_set_line_dash(PCLXL_CONTEXT pclxl_context,
                    PCLXL_GRAPHICS_STATE graphics_state,
                    PCLXL_ATTRIBUTE line_dash_style_attr,
                    int32 dash_offset)
{
  PCLXL_LINE_STYLE line_style = &graphics_state->line_style;

  HQASSERT(!line_dash_style_attr ||
           line_dash_style_attr->array_length == 0 ||
           line_dash_style_attr->data_type == PCLXL_DT_UByte_Array ||
           line_dash_style_attr->data_type == PCLXL_DT_UInt16_Array ||
           line_dash_style_attr->data_type == PCLXL_DT_Int16_Array,
           "Unexpected line dash style");

  if ( (line_dash_style_attr == NULL) ||
       (line_dash_style_attr->array_length == 0) )
  {
    /*
     * A NULL or zero-lengthed array of dash+gaps
     * means that we should be drawing solid lines
     */

    line_style->line_dash_len = 0;
  }
  else
  {
    PCLXL_SysVal *line_dash = line_style->line_dash;
    int32 total = 0;

    if ( line_dash_style_attr->array_length > PCLXL_LINE_DASH_MAX ) {
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 pclxl_context->parser_context,
                                 PCLXL_ILLEGAL_ARRAY_SIZE,
                                 ("Line dash size (%d) exceeds maximum (%d)",
                                  line_dash_style_attr->array_length,
                                  PCLXL_LINE_DASH_MAX));
      return FALSE;
    }

    switch ( line_dash_style_attr->data_type )
    {
     case PCLXL_DT_UByte_Array:
      {
        size_t i;
        for ( i = 0 ; i < line_dash_style_attr->array_length; ++i ) {
          line_dash[i] = (PCLXL_SysVal) line_dash_style_attr->value.v_ubytes[i];
          total += line_dash_style_attr->value.v_ubytes[i];
        }
      }
      break;

     case PCLXL_DT_UInt16_Array:
      {
        size_t i;
        for ( i = 0 ; i < line_dash_style_attr->array_length; ++i ) {
          line_dash[i] = (PCLXL_SysVal) line_dash_style_attr->value.v_uint16s[i];
          total += line_dash_style_attr->value.v_uint16s[i];
        }
      }
      break;

     case PCLXL_DT_Int16_Array:
      {
        size_t i;
        for ( i = 0 ; i < line_dash_style_attr->array_length; ++i ) {
          if ( line_dash_style_attr->value.v_int16s[i] < 0 ) {
            (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                       pclxl_context->parser_context,
                                       PCLXL_ILLEGAL_DATA_VALUE,
                                       ("Line dash style has negative values"));

            return FALSE;
          }
          total += line_dash_style_attr->value.v_int16s[i];
          line_dash[i] = (PCLXL_SysVal) line_dash_style_attr->value.v_int16s[i];
        }
      }
      break;

     default:
       HQFAIL("Unexpected line dash element type");
    }

    if ( total == 0 ) {
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 pclxl_context->parser_context,
                                 PCLXL_ILLEGAL_DATA_VALUE,
                                 ("Line dash style has all zero values"));

      return FALSE;
    }

    line_style->line_dash_len = CAST_SIGNED_TO_UINT8(line_dash_style_attr->array_length);

  }

  /* Spec states dash offset must be a +number but QL FTS job T318 specifies -ve
     dash offsets which are duly printed by the HP printers. */
  if ( pclxl_context->config_params.strict_pclxl_protocol_class &&
       dash_offset < 0 ) {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               pclxl_context->parser_context,
                               PCLXL_ILLEGAL_DATA_VALUE,
                               ("Line dash offset must be positive"));

    return FALSE;
  }
  line_style->dash_offset = dash_offset;

  return pclxl_ps_set_line_dash(pclxl_context, graphics_state);
}

Bool
pclxl_set_line_cap(PCLXL_GRAPHICS_STATE graphics_state,
                   PCLXL_LineCap        line_cap)
{
  graphics_state->line_style.line_cap = line_cap;

  return pclxl_ps_set_line_cap(graphics_state, line_cap);
}

Bool
pclxl_ps_set_line_join(PCLXL_GRAPHICS_STATE graphics_state)
{
  switch ( graphics_state->line_style.line_join ) {
  default:
    HQFAIL("Unexpected line join");
    /* fall thru */
  case PCLXL_eMiterJoin:
    gstateptr->thestyle.linejoin = MITER_JOIN;
    break;
  case PCLXL_eRoundJoin:
    gstateptr->thestyle.linejoin = ROUND_JOIN;
    break;
  case PCLXL_eBevelJoin:
    gstateptr->thestyle.linejoin = BEVEL_JOIN;
    break;
  case PCLXL_eNoJoin:
    gstateptr->thestyle.linejoin = NONE_JOIN;
    break;
  }

  PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS, ("PS: setlinejoin"));

  return TRUE;
}

Bool
pclxl_set_line_join(PCLXL_GRAPHICS_STATE graphics_state,
                    PCLXL_LineJoin       line_join)
{
  graphics_state->line_style.line_join = line_join;

  return pclxl_ps_set_line_join(graphics_state);
}

Bool
pclxl_set_miter_limit(PCLXL_CONTEXT pclxl_context,
                      PCLXL_GRAPHICS_STATE graphics_state,
                      PCLXL_SysVal miter_limit)
{
  graphics_state->line_style.miter_limit = miter_limit;
  return pclxl_ps_set_miter_limit(pclxl_context,
                                  graphics_state->line_style.miter_limit);
}

Bool
pclxl_set_pen_width(PCLXL_NON_GS_STATE   non_gs_state,
                    PCLXL_GRAPHICS_STATE graphics_state,
                    PCLXL_SysVal         pen_width)
{
  /* graphics_state->line_style.pen_width = pclxl_ps_min_line_width(non_gs_state, pen_width); */

  UNUSED_PARAM(PCLXL_NON_GS_STATE, non_gs_state);

  graphics_state->line_style.pen_width = pen_width;

  return pclxl_ps_set_line_width(pen_width);
}

Bool
pclxl_set_fill_mode(PCLXL_GRAPHICS_STATE graphics_state,
                    PCLXL_FillMode       fill_mode)
{
  graphics_state->fill_details.fill_mode = fill_mode;

  /* Fill mode is not stored in gstateptr, instead
     it is used as a parameter to dofill. */

  return TRUE;
}

/*
 * The following "convenience" macros
 * allow us to clarify the initialization
 * of the default graphics state
 */

#define PCLXL_INITIALIZE_CTM(_MATRIX_)                  \
{                                                       \
  MATRIX_00(_MATRIX_) = 1.0; MATRIX_01(_MATRIX_) = 0.0; \
  MATRIX_10(_MATRIX_) = 0.0; MATRIX_11(_MATRIX_) = 1.0; \
  MATRIX_20(_MATRIX_) = 0.0; MATRIX_21(_MATRIX_) = 0.0; \
  (_MATRIX_)->opt = 0;                                  \
}

#define PCLXL_INITIALIZE_XY(_XY_, _X_, _Y_) \
{ (_XY_)->x = (_X_); (_XY_)->y = (_Y_); }

/**
 * \brief carefully (re-)sets an existing graphics state structure
 * back to the default (whatever that may mean?) graphics state.
 * Note that this will (probably) involve removing any current path,
 * line dash style and resetting the current clip path back to the
 * imageable page area
 */

Bool
pclxl_set_default_graphics_state(PCLXL_CONTEXT pclxl_context,
                                 PCLXL_GRAPHICS_STATE graphics_state)
{
  PCLXL_CHAR_DETAILS char_details = &graphics_state->char_details;

  graphics_state->current_path = graphics_state->current_point = FALSE;
  PCLXL_INITIALIZE_XY(&graphics_state->current_point_xy, 0.0, 0.0);

  graphics_state->page_angle = 0;
  PCLXL_INITIALIZE_XY(&graphics_state->page_scale, 1.0, 1.0);
  graphics_state->set_page_scale_op_count = 0;
  graphics_state->set_page_rotation_op_count = 0;

  if ( !pclxl_set_current_ctm(graphics_state, &graphics_state->logical_page_ctm) )
    return FALSE;

  char_details->char_angle = 0.0;

  PCLXL_INITIALIZE_XY(&char_details->char_scale, 1.0, 1.0);
  PCLXL_INITIALIZE_XY(&char_details->char_shear, 0.0, 0.0);

  char_details->current_font.pclxl_font_state = PCLXL_FS_PCLXL_FONT_SET;

  char_details->char_boldness = 0.0;

  char_details->writing_mode = PCLXL_eHorizontal;

  if ( !pclxl_set_char_sub_modes(pclxl_context, char_details, NULL, 0) )
    return FALSE;

  char_details->font_matrix_construction_order = PCLXL_FMCO_SCALE_SHEAR_ANGLE;

  if ( !pclxl_recalculate_font_matrix(pclxl_context, graphics_state,
                                       char_details,
                                       &char_details->font_matrix) )
    return FALSE;

  /**
   * \todo the default should be "no font defined"
   * But does this still apply when a font is defined on one page
   * and we complete this page and move on to the next?
   */

  pcl5_record_font_details(graphics_state,
                           PCLXL_FS_NO_CURRENT_FONT,
                           PCLXL_FT_NO_CURRENT_FONT,
                           NULL, 0,               /* PCLXL font name + length */
                           0.0,                   /* PCLXL character size */
                           0,                     /* PCLXL/PCL5 soft font ID */
                           0,                     /* symbol set */
                           0, 0.0, 0.0, 0, 0, 0,  /* other PCL5 font selection criteria */
                           NULL, 0,               /* Postscript font name + length */
                           0.0,                   /* Postscript font point size */
                           FALSE);                /* is bitmap font */

  graphics_state->clip_mode = PCLXL_eNonZeroWinding;
  graphics_state->clip_record = NULL;

  /**
   * \todo I am sure that these following "defaults"
   * need to be taken from some printer-specific
   * configuration dictionary.
   *
   * Note that we have just such a dictionary in
   * context->init_params_dict :-)
   *
   * But I am equally sure that some of them should be carried
   * forward from a previous page's settings
   */

  if ( !pclxl_set_color_space(pclxl_context, graphics_state, PCLXL_eRGB) )
    return FALSE;

  pclxl_set_color_depth(pclxl_context, graphics_state, PCLXL_e8Bit);
  pclxl_set_color_mapping(pclxl_context, graphics_state, PCLXL_eDirectPixel);
  pclxl_set_color_treatment(pclxl_context, graphics_state, PCLXL_eNoTreatment);

  if ( !pclxl_set_black_types(pclxl_context,
                              graphics_state,
                              PCLXL_eProcessBlack,
                              PCLXL_eTonerBlack,
                              PCLXL_eTonerBlack) )
    return FALSE;

  if ( !pclxl_set_default_color(pclxl_context,
                                graphics_state->color_space_details,
                                &graphics_state->line_style.pen_source) )
    return FALSE;

  if ( !pclxl_set_default_color(pclxl_context,
                                graphics_state->color_space_details,
                                &graphics_state->fill_details.brush_source) )
    return FALSE;

  char_details->char_color = &graphics_state->fill_details.brush_source;

  /*
   * When the (Postscript) page "device" is set up
   * an initial clip path that defines the current (printable area of the) page
   * is automatically set up.
   */

  /*
   * We need separate concepts of current "point" and current "path"
   * because operators like PaintPath need to know if there is a path
   * (possibly containing several sub-paths) to be painted
   *
   * But various path construction operations need to know whether
   * there is a current *point* to draw a line *from*
   * or whether we are at the start of a new sub-path and thus
   * need to move to rather than line to the starting point of
   * the new sub-path
   */

  graphics_state->current_path = FALSE;
  graphics_state->current_point = FALSE;
  PCLXL_INITIALIZE_XY(&graphics_state->current_point_xy, 0.0, 0.0);
  PCLXL_INITIALIZE_XY(&graphics_state->dither_anchor, 0.0, 0.0);

  /** \todo the default should be "none" */
  graphics_state->dither_matrix_ID = 0;

  /* set the default screening. */
  if ( !pclxl_set_device_matrix_halftone(graphics_state, PCLXL_eDeviceBest) )
    return FALSE;

  if ( !pclxl_set_fill_mode(graphics_state, PCLXL_eNonZeroWinding) )
    return FALSE;

  /*
   * Line dash can be specified as an array of dash + gap lengths
   * But the default is a solid line
   * which is typically represented by an empty dash (style) array
   */

  if (! pclxl_set_line_dash(pclxl_context, graphics_state, NULL, 0) ||
      ! pclxl_set_line_cap(graphics_state, PCLXL_eButtCap) ||
      ! pclxl_set_line_join(graphics_state, PCLXL_eMiterJoin) ||
      ! pclxl_set_miter_limit(pclxl_context, graphics_state, 10.0) ||
      ! pclxl_set_pen_width(&pclxl_context->non_gs_state, graphics_state, 1.0)) {
    return FALSE;
  }

  graphics_state->source_tx_mode = PCLXL_eOpaque;
  graphics_state->paint_tx_mode = PCLXL_eOpaque;
  graphics_state->ROP3 = PCLXL_ROP3_PSo;

  /*
   * Having set the above 3 "PCL5" related graphics state values
   * in the PCLXL graphics state
   * we need to reflect these changes into the PCL5 graphics state itself
   *
   * We can cheekily do this using a call to
   * "restore" the PCL5 graphics state
   */

  pclxl_pcl_grestore(pclxl_context, graphics_state);

  /** \todo supposed to default to "none" */
  graphics_state->palette_id = 0;

  return TRUE;
}

/**
 * \brief pclxl_get_indexed_color()
 * is passed an array of bytes that represents a color palette
 * (along with the byte array length), a color bit depth (using PCLXL ColorDepth enumeration)
 * and an (integer) color index.
 *
 * Given the color bit depth it calculates which byte contains the color value
 * and then, if dictated by a color bit depth less than 8, it masks
 * the byte value to extract the color value
 */

uint8
pclxl_get_indexed_color(uint8*             color_palette_array,
                        size_t             color_palette_array_length,
                        PCLXL_ColorDepth   color_depth,
                        size_t             color_index)
{
  switch ( color_depth )
  {
  case PCLXL_e8Bit:

    {
      uint8 color = color_palette_array[(color_index % color_palette_array_length)];

      return color;
    }

    break;

  case PCLXL_e4Bit:

    {
      uint8 color_containing_byte = color_palette_array[((color_index / 2) % color_palette_array_length)];

      uint8 color_mask = ((color_index % 2) ? 0x0f : 0xf0);

      uint8 color = color_containing_byte & color_mask;

      return color;
    }

    break;

  case PCLXL_e1Bit:

    {
      uint8 color_containing_byte = color_palette_array[((color_index / 8) % color_palette_array_length)];

      uint8 color_bit = (uint8) (color_index % 8);

      uint8 color_mask = (0x01 << (7 - color_bit));

      uint8 color = color_containing_byte & color_mask;

      return color;
    }

    break;
  }

  HQFAIL("Should never reach this code line. Internal error - illegal color depth");

  return 0;
}

void
pclxl_free_color_palette(PCLXL_CONTEXT context,
                         PCLXL_COLOR_SPACE_DETAILS color_space_details)
{
  HQASSERT(color_space_details, "Cannot free a color palette array from beneath a NULL color space details");

  if ( color_space_details )
  {
    if ( color_space_details->color_palette_len > 0 )
    {
      mm_free(context->memory_pool,
              color_space_details->color_palette,
              color_space_details->color_palette_len);

      color_space_details->color_palette = NULL;
      color_space_details->color_palette_len = 0;
    }
  }
}

uint8
pclxl_color_depth_bits(PCLXL_ColorDepth color_depth)
{
  uint8 bit_depths[] = { 1, 4, 8 };

  HQASSERT(((color_depth == PCLXL_e1Bit) ||
            (color_depth == PCLXL_e4Bit) ||
            (color_depth == PCLXL_e8Bit)),
           "PCLXL ColorDepth must be one of e1Bit (0), e4Bit (1) or e8Bit (2)");

  return bit_depths[color_depth];
}

/**
 * \brief pclxl_copy_color_palette() takes a new
 * color palette raw data (or an existing "expanded" color palette)
 * and arranges to replace an existing color palette (if any)
 * with an unpacked copy of this new palette data.
 *
 * The "unpack" is required because the raw palette data
 * may contain 1-bit or 4-bit colours packed into each byte
 * This is indicated by the palette_color_depth, which is a PCLXL enumeration value
 */

Bool
pclxl_copy_color_palette(PCLXL_CONTEXT pclxl_context,
                         PCLXL_COLOR_SPACE_DETAILS color_space_details,
                         uint8*                    palette_data,
                         uint32                    palette_data_len,
                         PCLXL_ColorDepth          palette_color_depth)
{
  static uint8 palette_length_multipliers[] = { 8, 2, 1 };

  /*
   * We must calculate how much space the unpacked raw palette data
   * expands into and see if we already have exactly this much space allocated
   */

  uint32 color_palette_len = (palette_data_len * palette_length_multipliers[palette_color_depth]);

  uint32 i;

  /*
   * If there is an existing palette data array
   * and it is of the wrong length
   * then we must free it (before potentially allocating a new lengthed array)
   */

  if ( (color_space_details->color_palette_len > 0) &&
       (color_space_details->color_palette_len != color_palette_len) )
  {
    pclxl_free_color_palette(pclxl_context, color_space_details);
  }

  /*
   * If the current color palette is zero-lengthed
   * and the new palette data is non-zero-lengthed
   * then we will attempt to allocate space
   */

  if ( (color_space_details->color_palette_len == 0) &&
       (color_palette_len > 0) &&
       ((color_space_details->color_palette = mm_alloc(pclxl_context->memory_pool,
                                                       color_palette_len,
                                                       MM_ALLOC_CLASS_PCLXL_COLOR)) == NULL) )
  {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INSUFFICIENT_MEMORY,
                               ("Failed to allocate new PaletteData array"));

    return FALSE;
  }
  else
  {
    color_space_details->color_palette_len = color_palette_len;
  }

  for ( i = 0 ; i < color_palette_len ; i++ )
  {
    color_space_details->color_palette[i] =
      pclxl_get_indexed_color(palette_data,
                              palette_data_len,
                              palette_color_depth,
                              i);
  }

  return TRUE;
}

static Bool
pclxl_copy_char_details(PCLXL_CONTEXT pclxl_context,
                        PCLXL_GRAPHICS_STATE existing_graphics_state,
                        PCLXL_GRAPHICS_STATE new_graphics_state)
{
  PCLXL_CHAR_DETAILS new_char_details = &new_graphics_state->char_details;
  PCLXL_CHAR_DETAILS existing_char_details = &existing_graphics_state->char_details;

  /*
   * We have already performed a "shallow" copy of the whole
   * of the existing graphics state
   * (which includes a shallow copy of the char details)
   *
   * But this means that the "char_color" needs to be correctly
   * fixed up to the new graphics state fill_details->brush_source
   * and we need to take an independent copy of the "char_sub_modes"
   */

  new_graphics_state->char_details.char_color = &new_graphics_state->fill_details.brush_source;

  new_char_details->char_sub_modes = NULL;
  new_char_details->char_sub_modes_len = 0;

  return pclxl_set_char_sub_modes(pclxl_context,
                                  new_char_details,
                                  existing_char_details->char_sub_modes,
                                  existing_char_details->char_sub_modes_len);
}

/*
 * pclxl_scale_integer_color() rescales an integer colour value
 * in the range 0 to MAXINT(colour-bit-depth)
 * to be a floating point value in the range 0.0 to 1.0
 *
 * Which then makes this colour (intensity) value directly usable
 * as a Postscript colour channel intensity
 *
 * Note that a negative returned value means that the input
 * (integer) value was invalid for the specified bit depth
 */

static PCLXL_SysVal
pclxl_scale_integer_color(PCLXL_CONTEXT pclxl_context,
                          uint32               target_color,
                          PCLXL_ColorDepth     color_depth)
{
  static uint8 max_color_values[] = { 1, 15, 255 };

  uint32 max_color = max_color_values[color_depth];

  if ( target_color > max_color )
  {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                               ("Integer colour %d must be in the range 0 to %d, i.e. within the range defined by a color-depth %d (enum)",
                                target_color,
                                max_color,
                                color_depth));

    return -1.0;
  }
  else
  {
    PCLXL_SysVal scaled_target_color =
      ((PCLXL_SysVal) target_color /
       (PCLXL_SysVal) max_color);

    PCLXL_DEBUG(PCLXL_DEBUG_COLOR,
                ("Integer colour %d within the range 0 to %d results in scaled colour of %f",
                 target_color,
                 max_color,
                 scaled_target_color));

    return scaled_target_color;
  }
}

/**
 * \brief pclxl_copy_color_array() takes a PCLXL_COLOR_DETAIL structure
 * and a "Primary[Color]Array" from one of a number of different source
 * including a (UByte) PrimaryArray attribute,
 a (Real32_Array) PrimaryAttribute
 * or indeed from another PCLXL_COLOR_DETAIL's color_array
 *
 * This means a straight copy for Real32 and SysVal arrays
 * but means unpacking UByte arrays according to the specified color-depth
 */

Bool
pclxl_copy_color_array(PCLXL_CONTEXT pclxl_context,
                       PCLXL_GRAPHICS_STATE graphics_state,
                       PCLXL_COLOR_DETAILS  color_details,
                       PCLXL_UByte*         primary_array_ubyte_values,
                       PCLXL_ColorDepth     color_depth,
                       PCLXL_Real32*        primary_array_real32_values,
                       PCLXL_SysVal*        primary_array_sysval_values,
                       uint32               primary_array_len)
{
  PCLXL_ColorSpace color_space = graphics_state->color_space_details->color_space;
  uint8 allowed_color_level_count = pclxl_color_space_num_components(color_space);

  if ( (primary_array_real32_values || primary_array_sysval_values) &&
       (primary_array_len > 0) &&
       (primary_array_len != allowed_color_level_count) &&
       (pclxl_context->config_params.strict_pclxl_protocol_class) )
  {
    /*
     * A primary array length does not match
     * the length allowed for the current color space
     *
     * Unfortunately it appears that although the
     * PCLXL Protocol Class Specification regards this as an error
     * the hp4700n at least does not seem to complain in the slightest
     *
     * So we only actually enter this branch if we are
     * strictly adhering to the PCLXL Protocol Class Specification
     */

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               pclxl_context->parser_context,
                               PCLXL_COLOR_SPACE_MISMATCH,
                               ("Color specification (which consists of %d color component%s) must match current color space %d (which requires %d component%s)",
                                primary_array_len,
                                (primary_array_len > 1 ? "s" : ""),
                                graphics_state->color_space_details->color_space,
                                pclxl_color_space_num_components(color_space),
                                (pclxl_color_space_num_components(color_space) > 1 ? "s" : "")));

    return FALSE;
  }
  else
  {
    uint32 i;

    color_details->color_array_len = allowed_color_level_count;

    for ( i = 0, color_details->color_array_len = primary_array_len ;
          i < primary_array_len ;
          i++ )
    {
      if ( primary_array_ubyte_values )
      {
        uint8 ubyte_color_value = pclxl_get_indexed_color(primary_array_ubyte_values,
                                                          primary_array_len,
                                                          color_depth,
                                                          i);
        PCLXL_SysVal real_color_value;

        if ( (real_color_value = pclxl_scale_integer_color(pclxl_context,
                                                           ubyte_color_value,
                                                           color_depth)) < 0.0 )
        {
          return FALSE;
        }
        else
        {
          color_details->color_array[i] = real_color_value;
        }
      }
      else if ( primary_array_real32_values )
      {
        if ( (primary_array_real32_values[i] < 0.0) ||
             (primary_array_real32_values[i] > 1.0) )
        {
          (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                     PCLXL_SS_KERNEL,
                                     PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                                     ("Real32 color level %f should lie in the range 0.0 and 1.0",
                                      primary_array_real32_values[i]));

          return FALSE;
        }
        else
        {
          color_details->color_array[i] = primary_array_real32_values[i];
        }
      }
      else if ( primary_array_sysval_values )
      {
        color_details->color_array[i] = primary_array_sysval_values[i];
      }
    }

    return TRUE;
  }

  /*NOTREACHED*/
}

Bool
pclxl_copy_color(PCLXL_CONTEXT pclxl_context,
                 PCLXL_COLOR_DETAILS  existing_color_details,
                 PCLXL_COLOR_DETAILS  new_color_details)
{
  if ( new_color_details->color_space_details != NULL )
  {
    pclxl_delete_color_space_details(pclxl_context,
                                     new_color_details->color_space_details);

    new_color_details->color_space_details = NULL;
  }

  *new_color_details = *existing_color_details;

  new_color_details->color_space_details =
    pclxl_add_ref_to_color_space_details(pclxl_context,
                                         existing_color_details->color_space_details);

  return (new_color_details->color_space_details != NULL);
}

/**
 * \brief creates a new PCLXL graphics state
 * optionally as a clone/copy of an existing graphics state.
 * \todo Should this result in empty current and clip paths and/or current position?
 */

PCLXL_GRAPHICS_STATE
pclxl_create_graphics_state(PCLXL_GRAPHICS_STATE existing_graphics_state,
                            PCLXL_CONTEXT pclxl_context)
{
  PCLXL_GRAPHICS_STATE new_graphics_state;
  PCLXL_GRAPHICS_STATE_STRUCT init = {0};

  if ( (new_graphics_state = mm_alloc(pclxl_context->memory_pool,
                                      sizeof(PCLXL_GRAPHICS_STATE_STRUCT),
                                      MM_ALLOC_CLASS_PCLXL_GRAPHICS_STATE)) == NULL ) {
    /*
     * We failed to allocate a new graphics state
     * So we cannot populate it
     * (And the caller must not attempt to push it onto the graphics state stack)
     */

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INSUFFICIENT_MEMORY,
                               ("Failed to allocate new Graphics State structure"));

    return NULL;
  }

  /* Zero-init members. */
  *new_graphics_state = init;

  if ( existing_graphics_state != NULL )
  {
    OBJECT onothing = OBJECT_NOTVM_NOTHING;

    /*
     * We have been supplied with an existing graphics state
     * which is to be used as the initial settings in this new graphics state
     *
     * Note that we can *nearly* do a straight copy/clone of the
     * entire structure contents.
     * However there are a couple of fields that must be replicated
     * rather more carefully because they are themselves dynamically allocated
     *
     * First the direct copy/clone:
     */

    *new_graphics_state = *existing_graphics_state;

    /*
     * After this "quick'n'dirty" copy of the entire existing graphics state structure
     * We must fix-up any incorrectly copied fields
     * So that any subsequent delete of this new structure does the correct thing
     */

    new_graphics_state->parent_graphics_state = NULL;

    new_graphics_state->postscript_op = PCLXL_PS_NO_OP_NO_OP;

    new_graphics_state->ps_save_object = onothing;

    /*
     * I am not sure whether we should reset these two counts
     * merely because we have pushed a copy of the current PCLXL graphics state
     * onto the graphics state stack.
     *
     * In fact I have empirical evidence that we explicitly do *not*
     * change the font matrix re-calculation in this case
     *
    new_graphics_state->set_page_scale_op_count = 0;
    new_graphics_state->set_page_rotation_op_count = 0;
     */

    new_graphics_state->color_space_details = NULL;

    new_graphics_state->line_style.pen_source.color_space_details = NULL;

    new_graphics_state->fill_details.brush_source.color_space_details = NULL;

    if ( (!pclxl_copy_color_space_details(pclxl_context,
                                          existing_graphics_state->color_space_details,
                                          &new_graphics_state->color_space_details)) ||
         (!pclxl_copy_color(pclxl_context,
                            &existing_graphics_state->line_style.pen_source,
                            &new_graphics_state->line_style.pen_source)) ||
         (!pclxl_copy_color(pclxl_context,
                            &existing_graphics_state->fill_details.brush_source,
                            &new_graphics_state->fill_details.brush_source)) ||
         (!pclxl_copy_char_details(pclxl_context, existing_graphics_state,
                                   new_graphics_state))
      )
    {
      pclxl_delete_graphics_state(pclxl_context, new_graphics_state);

      return NULL;
    }

    if ( new_graphics_state->clip_record != NULL )
      gs_reservecliprec(new_graphics_state->clip_record);
  }
  else
  {
    /*
     * We have been asked to create a new graphics state and to then initialize
     * it without reference to another graphics state
     *
     * In this case we initialize it to the BeginPage default graphics state (as
     * specified in the PCLXL Feature Reference Protocol Class)
     *
     * Note that pclxl_set_default_graphics_state() explicitly does not touch
     * <postscript_op> and <ps_save_object> fields because it is expected that
     * only pclxl_push_gs() and pclxl_pop_gs() touch these fields
     */

    if ( !pclxl_set_default_graphics_state(pclxl_context, new_graphics_state) )
    {
      /*
       * We have failed to reset the default graphics state An appropriate error
       * has been logged/handled So we must delete/free this incorrectly reset
       * graphics state and return NULL
       */

      pclxl_delete_graphics_state(pclxl_context, new_graphics_state);

      return NULL;
    }

    /**
     * \todo is there *anything* that is not (re-)set by
     * pclxl_set_default_graphics_state()?  like maybe the measurement_units or
     * the units_per_measure, for instance?  Because, if so, then we need to set
     * these to suitable initial defaults here.
     */
  }

  /* Create the pattern cache for this gstate. */
  if (! pclxl_pattern_gstate_created(pclxl_context, new_graphics_state)) {
    pclxl_delete_graphics_state(pclxl_context, new_graphics_state);
    return NULL;
  }

  return new_graphics_state;
}

/**
 * \brief deletes/frees a graphics state,
 * typically one that has just been "popped" from the
 * PCLXL context's graphics state stack
 */

void
pclxl_delete_graphics_state(PCLXL_CONTEXT pclxl_context,
                            PCLXL_GRAPHICS_STATE graphics_state)
{
  HQASSERT(graphics_state != NULL, "Cannot delete NULL graphics state");
  HQASSERT(pclxl_context != NULL, "NULL context - need access to memory pool to delete graphics state");

  pclxl_pattern_gstate_deleted(pclxl_context, graphics_state);

  pclxl_delete_color_space_details(pclxl_context,
                                   graphics_state->color_space_details);
  pclxl_delete_color_space_details(pclxl_context,
                                   graphics_state->line_style.pen_source.color_space_details);
  pclxl_delete_color_space_details(pclxl_context,
                                   graphics_state->fill_details.brush_source.color_space_details);

  pclxl_set_char_sub_modes(pclxl_context, &graphics_state->char_details, NULL, 0);

  if ( graphics_state->clip_record )
    pclxl_free_clip_record(graphics_state);

  mm_free(pclxl_context->memory_pool, graphics_state,
          sizeof(PCLXL_GRAPHICS_STATE_STRUCT));
}

/**
 * \brief pushes a graphics state
 * (typically one that has just been created)
 * onto the top of a graphics state stack.
 */

void
pclxl_push_graphics_state(PCLXL_GRAPHICS_STATE new_graphics_state,
                          PCLXL_GRAPHICS_STATE_STACK* graphics_state_stack)
{
  HQASSERT((new_graphics_state != NULL), "Cannot push NULL graphics state");
  HQASSERT((graphics_state_stack != NULL), "Cannot push graphics state into NULL graphics state stack");
  HQASSERT((new_graphics_state != *graphics_state_stack), "Cannot push graphics state onto stack again as this would corrupt the stack");

  /*
   * Interesting question here:
   * What happens if we try and push a new graphics state onto the stack but:
   *
   * a) The new graphics state's parent graphics state is not NULL
   *    (and not already pointing at the top of the stack)?
   *
   * b) The "new" graphics state is somehow already in the stack?
   */

  HQASSERT(((new_graphics_state->parent_graphics_state == NULL) ||
              (new_graphics_state->parent_graphics_state == *graphics_state_stack)),
             "New graphics state \"parent\" graphics state already points somewhere else");

  new_graphics_state->parent_graphics_state = *graphics_state_stack;

  *graphics_state_stack = new_graphics_state;
}

/**
 * \brief "pops" the top graphics state from the top of a graphics state stack
 * \return returns the "popped" graphics state.
 * It is upto the caller to delete/free this popped graphics state if desired.
 */

PCLXL_GRAPHICS_STATE
pclxl_pop_graphics_state(PCLXL_GRAPHICS_STATE_STACK* graphics_state_stack)
{
  HQASSERT((graphics_state_stack != NULL), "Cannot pop graphics state from a NULL graphics state stack");

  if ( *graphics_state_stack != NULL )
  {
    PCLXL_GRAPHICS_STATE popped_graphics_state = *graphics_state_stack;

    *graphics_state_stack = popped_graphics_state->parent_graphics_state;

    popped_graphics_state->parent_graphics_state = NULL;

    return popped_graphics_state;
  }
  else
  {
    (void) PCLXL_ERROR_HANDLER(pclxl_get_context(),
                               PCLXL_SS_KERNEL,
                               PCLXL_GRAPHICS_STATE_STACK_ERROR,
                               ("Tried to pop a graphics state from an empty graphics state stack"));
    return NULL;
  }

  /*NOTREACHED*/
}

/* search for a pfin module, prioritize UFST over FontFusion */
/* \todo this function needs renaming now multiple PFIN modules avavilable */
static sw_pfin*
pclxl_get_ufst(PCLXL_CONTEXT pclxl_context)
{
  OBJECT ufstname = OBJECT_NOTVM_STRING("UFST");

  sw_pfin* ufst = pfin_findpfin(&ufstname);

  if (!ufst) {
    OBJECT ffname = OBJECT_NOTVM_STRING("FF");
    ufst = pfin_findpfin(&ffname);
  }

  if ( !ufst )
  {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Pailed to initialize font selection interface"));
  }

  return ufst;
}

Bool
pclxl_get_default_media_size(PCLXL_CONTEXT    pclxl_context,
                             uint8*           p_media_size_value_type,
                             PCLXL_MediaSize* p_media_size_enum,
                             uint8**          p_media_size_name,
                             PCLXL_SysVal_XY* p_media_size_xy)
{
  PCLXL_MEDIA_DETAILS_STRUCT dummy_page_device;

  HqMemZero((uint8 *)&dummy_page_device, sizeof(dummy_page_device));

  if ( !pclxl_ps_currentpagedevice(pclxl_context, &dummy_page_device) )
  {
    return FALSE;
  }
  else
  {
    *p_media_size_value_type = PCLXL_MEDIA_SIZE_ENUM_VALUE;
    *p_media_size_enum = dummy_page_device.media_size;
    *p_media_size_name = (uint8*) "Default";
    p_media_size_xy->x = dummy_page_device.media_size_xy.x;
    p_media_size_xy->y = dummy_page_device.media_size_xy.y;

    PCLXL_DEBUG(PCLXL_DEBUG_ATTRIBUTES,
                ("Default media size name = \"%s\", enum = %d, X = %f, Y = %f",
                 (*p_media_size_name),
                 (*p_media_size_enum),
                 p_media_size_xy->x,
                 p_media_size_xy->y));

    return TRUE;
  }
}

Bool
pclxl_get_default_media_details(PCLXL_CONTEXT       pclxl_context,
                                PCLXL_MEDIA_DETAILS media_details)
{
  media_details->orientation = pclxl_context->config_params.default_orientation;

  pclxl_get_default_media_size(pclxl_context,
                               &media_details->media_size_value_type,
                               &media_details->media_size,
                               &media_details->media_size_name,
                               &media_details->media_size_xy);

  media_details->media_source = pclxl_context->config_params.default_media_source;
  media_details->media_destination = pclxl_context->config_params.default_media_destination;
  HqMemZero((uint8 *)media_details->media_type,
            sizeof(media_details->media_type));
  (void) strncpy((char*) media_details->media_type,
                 (char*) pclxl_context->config_params.default_media_type,
                 sizeof(media_details->media_type));
  media_details->duplex = FALSE;
  media_details->simplex_page_side = PCLXL_eSimplexFrontSide;
  media_details->duplex_page_side = PCLXL_eFrontMediaSide;
  media_details->duplex_binding = PCLXL_eDuplexVerticalBinding;
  media_details->leading_edge = 0;

  return TRUE;
}

PCLXL_NON_GS_STATE
pclxl_init_non_gs_state(PCLXL_CONTEXT pclxl_context,
                        PCLXL_NON_GS_STATE non_gs_state)
{
  non_gs_state->measurement_unit = PCLXL_eInch;

  /*
   * Note that the initial "resolution"
   * specified here is completely arbitary,
   * because each PCLXL "session" specifies
   * its own "UnitsPerMeasure" which will promptly
   * override these values
   */

  non_gs_state->units_per_measure.res_x = 0;
  non_gs_state->units_per_measure.res_y = 0;

  (void) pclxl_get_default_media_details(pclxl_context,
                                         &non_gs_state->previous_media_details);

  non_gs_state->requested_media_details = non_gs_state->previous_media_details;

  (void) pclxl_get_current_media_details(pclxl_context,
                                         &non_gs_state->requested_media_details,
                                         &non_gs_state->current_media_details);

  non_gs_state->page_copies = (PCLXL_UInt16) pclxl_context->config_params.default_page_copies;
  non_gs_state->page_number = 1; /* Starts at 1 so that it is the correct (show)page number from the start of the session */
  non_gs_state->duplex_page_side_count = 1;

  non_gs_state->ufst = pclxl_get_ufst(pclxl_context);

  non_gs_state->min_line_width = 0.0;

  non_gs_state->setg_required = TRUE ;

  non_gs_state->text_mode_changed = FALSE ;

  return non_gs_state;
}

/**
 * \brief retrieves the current path and current point details
 * from the (Postscript) Core RIP and updates the supplied graphics state
 * accordingly
 */

void
pclxl_get_current_path_and_point(PCLXL_GRAPHICS_STATE graphics_state)
{
  graphics_state->current_path = graphics_state->current_point =
      pclxl_ps_get_current_point(&graphics_state->current_point_xy);
}

/**
 * \brief
 * pclxl_get_current_ctm() is passed the graphics_state
 * and the location of an OMATRIX
 * which is typically be the graphics_state's "current_ctm" field,
 * and a optional pointer to a boolean which again typically points to the
 * graphics state's "ctm_is_invertible" field.
 *
 * It gets the Current *Postscript* Trnasformation Matrix (CTM)
 * and whether or not it is invertible.
 *
 * It places this updated CTM and its invertibility, into the supplied location and
 * It also returns the CTM for the possible benefit of the caller
 * (although I think all the current callers currently (void) the result)
 */

void
pclxl_get_current_ctm(PCLXL_CONTEXT pclxl_context,
                      OMATRIX* current_ctm,
                      Bool* p_invertible)
{
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  OMATRIX* postscript_ctm = &thegsPageCTM(*gstateptr);

  HQASSERT(pclxl_context->graphics_state != NULL, "Cannot obtain CTM for a NULL PCLXL graphics state");
  HQASSERT(current_ctm != NULL, "Cannot store an updated CTM into a NULL OMATRIX location");

  if ( !MATRIX_EQ(current_ctm, postscript_ctm) )
  {
    /*
     * The current Postscript CTM differs from our record of it
     * So we must update our record it
     */

    *current_ctm = *postscript_ctm;

    /*
     * We also calculate whether this changed CTM is invertible
     * Because if it is not then we must avoid calling numerous
     * other core Rip functions that would fail when attempting to invert the CTM
     */

    if ( p_invertible )
      *p_invertible = pclxl_ps_is_matrix_invertible(postscript_ctm);

    /*
     * We must also ensure that the current Postscript font is re-selected
     * if/when we need to use it, so that we take into account this CTM change
     */

    if ( graphics_state->char_details.current_font.pclxl_font_state > PCLXL_FS_PFIN_FONT_SELECTED )
    {
      graphics_state->char_details.current_font.pclxl_font_state = PCLXL_FS_PFIN_FONT_SELECTED;
    }
  }
}

/**
 * \brief
 * pclxl_set_current_ctm() is passed a CTM
 * which it a) stores into the graphics state
 * and b) sets this same CTM as the current Postscript CTM
 */

Bool
pclxl_set_current_ctm(PCLXL_GRAPHICS_STATE graphics_state,
                      OMATRIX*             current_ctm)
{
  if ( pclxl_ps_set_current_ctm(current_ctm) )
  {
    if ( current_ctm != &graphics_state->current_ctm )
    {
      graphics_state->current_ctm = *current_ctm;
      graphics_state->ctm_is_invertible = pclxl_ps_is_matrix_invertible(current_ctm);

      /*
       * If we have changed the current CTM in any way
       * it is almost certain that the current font matrix is now wrong
       * So we reset its state so that it gets recalculated
       * if-and-when some text is drawn
       */

      if ( graphics_state->char_details.current_font.pclxl_font_state > PCLXL_FS_PFIN_FONT_SELECTED )
        graphics_state->char_details.current_font.pclxl_font_state = PCLXL_FS_PFIN_FONT_SELECTED;
    }

    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

Bool
pclxl_set_char_angle(PCLXL_GRAPHICS_STATE graphics_state,
                     PCLXL_SysVal         char_angle)
{
  PCLXL_CHAR_DETAILS char_details = &graphics_state->char_details;

  char_details->char_angle = char_angle;

  char_details->font_matrix_construction_order =
    pclxl_new_font_matrix_construction_order(char_details,
                                             PCLXL_FONT_MATRIX_SET_CHAR_ANGLE);


  /*
   * The current Postscript font_matrix is invalid
   * and so needs to be recalculated before next use
   */

  if ( char_details->current_font.pclxl_font_state > PCLXL_FS_PFIN_FONT_SELECTED )
    char_details->current_font.pclxl_font_state = PCLXL_FS_PFIN_FONT_SELECTED;

  return TRUE;
}

Bool
pclxl_set_char_scale(PCLXL_GRAPHICS_STATE graphics_state,
                     PCLXL_SysVal_XY*     char_scale)
{
  PCLXL_CHAR_DETAILS char_details = &graphics_state->char_details;

  char_details->char_scale = *char_scale;

  char_details->font_matrix_construction_order =
    pclxl_new_font_matrix_construction_order(char_details,
                                             PCLXL_FONT_MATRIX_SET_CHAR_SCALE);

  if ( char_details->current_font.pclxl_font_state > PCLXL_FS_PFIN_FONT_SELECTED )
    char_details->current_font.pclxl_font_state = PCLXL_FS_PFIN_FONT_SELECTED;

  return TRUE;
}

Bool
pclxl_set_char_shear(PCLXL_GRAPHICS_STATE graphics_state,
                     PCLXL_SysVal_XY*     char_shear)
{
  PCLXL_CHAR_DETAILS char_details = &graphics_state->char_details;

  char_details->char_shear = *char_shear;

  char_details->font_matrix_construction_order =
    pclxl_new_font_matrix_construction_order(char_details,
                                             PCLXL_FONT_MATRIX_SET_CHAR_SHEAR);

  if ( char_details->current_font.pclxl_font_state > PCLXL_FS_PFIN_FONT_SELECTED )
    char_details->current_font.pclxl_font_state = PCLXL_FS_PFIN_FONT_SELECTED;

  return TRUE;
}

Bool
pclxl_set_char_boldness(PCLXL_GRAPHICS_STATE graphics_state,
                        PCLXL_SysVal         char_boldness)
{
  PCLXL_CHAR_DETAILS char_details = &graphics_state->char_details;

  char_details->char_boldness = char_boldness;

  if ( char_details->current_font.pclxl_font_state > PCLXL_FS_PCLXL_FONT_SET )
    char_details->current_font.pclxl_font_state = PCLXL_FS_PCLXL_FONT_SET;

  return TRUE;
}

Bool
pclxl_set_char_attributes(PCLXL_CONTEXT pclxl_context,
                          PCLXL_GRAPHICS_STATE graphics_state,
                          PCLXL_WritingMode    writing_mode)
{
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;
  PCLXL_CHAR_DETAILS char_details = &graphics_state->char_details;

  if (char_details->writing_mode != writing_mode)
    non_gs_state->text_mode_changed = TRUE;

  char_details->writing_mode = writing_mode;

  if ( char_details->current_font.pclxl_font_state > PCLXL_FS_PFIN_FONT_SELECTED )
    char_details->current_font.pclxl_font_state = PCLXL_FS_PFIN_FONT_SELECTED;

  return TRUE;
}

Bool
pclxl_set_char_sub_modes(PCLXL_CONTEXT pclxl_context,
                         PCLXL_CHAR_DETAILS char_details,
                         uint8* char_sub_modes,
                         uint32 char_sub_modes_len)
{
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;

  /*
   * We store the char_sub_modes in a dynamically allocated array
   * which we must remember to re-size/re-use as necessary
   */

  if ( char_details->char_sub_modes_len != char_sub_modes_len )
  {
    /*
     * The existing char_sub_modes array is not the same length
     * as the new array.
     *
     * So we will free the existing array
     * and set the length to zero
     */

    if ( (char_details->char_sub_modes != NULL) &&
         (char_details->char_sub_modes_len != 0) )
    {
      mm_free(pclxl_context->memory_pool, char_details->char_sub_modes,
              char_details->char_sub_modes_len);

      char_details->char_sub_modes = NULL;

      char_details->char_sub_modes_len = 0;
    }

    if ( (char_sub_modes_len > 0) &&
         ((char_details->char_sub_modes = mm_alloc(pclxl_context->memory_pool,
                                                   char_sub_modes_len,
                                                   MM_ALLOC_CLASS_PCLXL_CHAR_DATA)) == NULL) )

    {
      (void) PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL,
                                 PCLXL_INSUFFICIENT_MEMORY,
                                 ("Failed to allocate new CharSubMode array"));

      return FALSE;
    }
    else
    {
      char_details->char_sub_modes_len = char_sub_modes_len;
    }

    non_gs_state->text_mode_changed = TRUE ;
  }

  if ( char_sub_modes_len > 0 )
  {
     if (! non_gs_state->text_mode_changed)
      if (HqMemCmp(char_details->char_sub_modes, char_sub_modes_len,
                   char_sub_modes, char_sub_modes_len) != 0)
        non_gs_state->text_mode_changed = TRUE ;

    (void) memcpy(char_details->char_sub_modes,
                  char_sub_modes,
                  char_sub_modes_len);
  }

  if ( char_details->current_font.pclxl_font_state > PCLXL_FS_PFIN_FONT_SELECTED )
    char_details->current_font.pclxl_font_state = PCLXL_FS_PFIN_FONT_SELECTED;

  return TRUE;
}

/**
 * Adjust the object based halftone selection parameters.
 */
Bool
pclxl_set_object_halftone(PCLXL_GRAPHICS_STATE graphics_state,
                          PCLXL_ATTR_ID attrib,
                          PCLXL_HalftoneMethod method,
                          Bool *change)
{
  *change = FALSE;

  switch (attrib)
  {
    case PCLXL_AT_TextObjects:
      if (graphics_state->halftone_method.text == method )
        return TRUE;

      graphics_state->halftone_method.text = method;

      break;

    case PCLXL_AT_VectorObjects:
      if (graphics_state->halftone_method.vector == method)
        return TRUE;

      graphics_state->halftone_method.vector = method;
      break;

    case PCLXL_AT_RasterObjects:
      if (graphics_state->halftone_method.raster == method)
        return TRUE;

      graphics_state->halftone_method.raster = method;
      break;

    case PCLXL_AT_AllObjectTypes:
      if (graphics_state->halftone_method.text == method
          && graphics_state->halftone_method.raster == method
          && graphics_state->halftone_method.vector == method)
        return TRUE;

      graphics_state->halftone_method.text = method;
      graphics_state->halftone_method.raster = method;
      graphics_state->halftone_method.vector = method;
      break;

    default:
      return FALSE;
      break;
  }

  *change = TRUE;

  /* Setting any object based halftone turns off any existing
   * job defined threshold array.
   * PCL compatibility : this behaviour seems to vary amongst printers.*/
  graphics_state->dither_matrix_ID = 0;
  return TRUE;
}

/* We take the v2.1 and lower operation to map into the default
 * setting for the object based screening.
 */
/**
 * \todo @@@ TODO need to check this assumption that the default
 * LPI should be used.
 */
Bool
pclxl_set_device_matrix_halftone(PCLXL_GRAPHICS_STATE graphics_state,
                                PCLXL_DitherMatrix method)
{
  if ( method == PCLXL_eDeviceBest )
  {
    graphics_state->halftone_method.raster = PCLXL_eLowLPI;
    graphics_state->halftone_method.vector = PCLXL_eMediumLPI;
    graphics_state->halftone_method.text   = PCLXL_eHighLPI;
    return pclxl_ps_object_halftone(graphics_state);
  }
  return FALSE;
}

Bool
pclxl_set_device_dither_phase(PCLXL_CONTEXT pclxl_context,
                              PCLXL_GRAPHICS_STATE graphics_state,
                              SYSTEMVALUE new_origin_x,
                              SYSTEMVALUE new_origin_y)
{
  int32 dev_origin_x, dev_origin_y;

  MATRIX_TRANSFORM_XY(new_origin_x,
                      new_origin_y,
                      new_origin_x,
                      new_origin_y,
                      &thegsPageCTM(*gstateptr));

  SC_C2D_INT(dev_origin_x, new_origin_x);
  SC_C2D_INT(dev_origin_y, new_origin_y);

  /* only update origin in PS world if it is different to request. Dither
   * anchor is set in device pixels so should be in whole pixels.
   */
  if ( dev_origin_x != graphics_state->dither_anchor.x
       || dev_origin_y != graphics_state->dither_anchor.y )
  {
    graphics_state->dither_anchor.x = dev_origin_x;
    graphics_state->dither_anchor.y = dev_origin_y;

    if ( !pclxl_ps_set_device_dither_origin(graphics_state) )
    {
      (void)PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL,
                                PCLXL_INTERNAL_ERROR,
                                ("Internal error setting halftone phase"));
      return FALSE;
    }
    return TRUE;
  }
  return TRUE;
}

/******************************************************************************
* Log stripped */
