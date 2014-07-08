/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlpen.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions that handle individual PCLXL SetPen... operators.
 * These functions are referenced by the pclxl_operator_details[] array
 * and are exclusively accessed via this array by pclxl_handle_operator()
 */

#include "core.h"

#include "pclxltypes.h"
#include "pclxldebug.h"
#include "pclxlcontext.h"
#include "pclxlerrors.h"
#include "pclxloperators.h"
#include "pclxlattributes.h"
#include "pclxlgraphicsstate.h"
#include "pclxlpsinterface.h"
#include "pclxlpattern.h"

/*
 * Tag 0x6a SetColorSpace
 */

Bool
pclxl_op_set_color_space(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[4] = {
#define SETCOLORSPACE_COLORSPACE    (0)
    {PCLXL_AT_ColorSpace | PCLXL_ATTR_REQUIRED},
#define SETCOLORSPACE_PALETTE_DEPTH (1)
    {PCLXL_AT_PaletteDepth},
#define SETCOLORSPACE_PALETTE_DATA  (2)
    {PCLXL_AT_PaletteData},
    PCLXL_MATCH_END
  };
  static PCLXL_ColorSpace allowed_color_spaces[] = {
    PCLXL_eGray,
    PCLXL_eRGB,
#ifdef DEBUG_BUILD
    PCLXL_eCMYK,
#endif
    PCLXL_eSRGB,
    PCLXL_ENUMERATION_END
  };
  static PCLXL_ColorDepth allowed_palette_depths[] = {
    PCLXL_e1Bit,
    PCLXL_e4Bit,
    PCLXL_e8Bit,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_ATTRIBUTE palette_data_attr;
  PCLXL_ColorSpace color_space;
  PCLXL_ColorDepth palette_depth;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }
  
  /* ColorSpace */
  if ( !pclxl_attr_match_enumeration(match[SETCOLORSPACE_COLORSPACE].result,
                                     allowed_color_spaces, &color_space, pclxl_context,
                                     PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* Both Palette attributes must be present or missing */
  if ( (match[SETCOLORSPACE_PALETTE_DEPTH].result != NULL) ^ (match[SETCOLORSPACE_PALETTE_DATA].result != NULL) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_MISSING_ATTRIBUTE,
                        ("Found only one of PaletteData and PaletteDepth"));
    return(FALSE);
  }

  if ( match[SETCOLORSPACE_PALETTE_DEPTH].result ) {
    /* Palette data attributes are present */
    static uint16 expected_palette_data_lengths[7][3] = {
      { 0, 0, 0 },
      { 2, 16, 256 },
      { 6, 48, 768 },
#ifdef DEBUG_BUILD
      { 8, 64, 1024 },
#else
      { 0, 0, 0 },
#endif
      { 0, 0, 0 },
      { 0, 0, 0 },
      { 6, 48, 768 }
    };
    uint16 expected_palette_data_length;

    /* PaletteDepth */
    if ( !pclxl_attr_match_enumeration(match[SETCOLORSPACE_PALETTE_DEPTH].result,
                                       allowed_palette_depths, &palette_depth, pclxl_context,
                                       PCLXL_SS_KERNEL) ) {
      return(FALSE);
    }
    /* PaletteData */
    palette_data_attr = match[SETCOLORSPACE_PALETTE_DATA].result;
    expected_palette_data_length = expected_palette_data_lengths[color_space][palette_depth];
    if ( palette_data_attr->array_length > expected_palette_data_length ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ARRAY_SIZE,
                          ("Illegal PaletteData Array Length. Expected (Maximum) Data Length = %d, Supplied Data Length = %d",
                           expected_palette_data_length, palette_data_attr->array_length));
      return(FALSE);
    }

    PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_COLOR),
                ("SetColorSpace(ColorSpace = e%s, PaletteDepth = %d (enum), PaletteData[length = %d])",
                 pclxl_color_space_name(color_space),
                 palette_depth,
                 palette_data_attr->array_length));

    if ( !pclxl_set_color_space(pclxl_context, graphics_state, color_space) ) {
      return(FALSE);
    }
    pclxl_set_color_depth(pclxl_context, graphics_state, palette_depth);

    /* Note that setting the color depth (above) will already have ensured that
     * we have a shiny new writeable color space details
     *
     * So we can go ahead and copy this color pallet into this new color space
     * details *without* having to explicitly ensure that the color space
     * details are indeed writeable here
     */
    return(pclxl_copy_color_palette(pclxl_context,
                                    graphics_state->color_space_details,
                                    palette_data_attr->value.v_ubytes,
                                    palette_data_attr->array_length,
                                    palette_depth));
  }

  /* Just a ColorSpace given so set this color space in the "pen" and
   * "brush" sources (And in doing so we reset the current color back to
   * black)
   */
  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_COLOR),
              ("SetColorSpace(ColorSpace = e%s)",
               pclxl_color_space_name(color_space)));

  /* Note that we must clear out any previous palette or pattern color space
   * settings - this is done automatically by pclxl_set_color_space()
   */
  return(pclxl_set_color_space(pclxl_context, graphics_state, color_space));
}

static Bool
pclxl_set_gray_level_source(PCLXL_PARSER_CONTEXT parser_context,
                            PCLXL_ATTRIBUTE      gray_level_attr,
                            PCLXL_COLOR_DETAILS  color_details)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;

  HQASSERT((gray_level_attr != NULL), "GrayLevel PCLXL attribute must not be NULL");
  HQASSERT((color_details != NULL), "Cannot store RGBColor into NULL color details struct");

  /** \todo Don't believe this can ever return FALSE! */
  if ( !pclxl_copy_color_space_details(pclxl_context,
                                       graphics_state->color_space_details,
                                       &color_details->color_space_details) ) {
    return(FALSE);
  }

  if ( gray_level_attr->data_type == PCLXL_DT_Real32 ) {
    if ( !pclxl_copy_color_array(pclxl_context, graphics_state, color_details,
                                 NULL, PCLXL_e8Bit, &gray_level_attr->value.v_real32,
                                 NULL, 1) ) {
      return FALSE;
    }

  } else if ( gray_level_attr->data_type == PCLXL_DT_UByte ) {
    if ( !pclxl_copy_color_array(pclxl_context, graphics_state, color_details,
                                 &gray_level_attr->value.v_ubyte, PCLXL_e8Bit, NULL,
                                 NULL, 1) ) {
      return FALSE;
    }
  }

  PCLXL_DEBUG(PCLXL_DEBUG_COLOR,
              ("Set GrayLevel [ %f ], bit-depth = %d",
               color_details->color_array[PCLXL_GRAY_CHANNEL],
               pclxl_color_depth_bits(graphics_state->color_space_details->color_depth)));

  return TRUE;
}

static Bool
pclxl_set_rgb_color_source(PCLXL_PARSER_CONTEXT parser_context,
                           PCLXL_ATTRIBUTE      rgb_color_attr,
                           PCLXL_COLOR_DETAILS  color_details)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;

  HQASSERT((rgb_color_attr != NULL), "RGBColor PCLXL attribute must not be NULL");
  HQASSERT((color_details != NULL), "Cannot store RGBColor into NULL color details struct");

  /*
   * We know that the RGBColor attribute is not NULL
   * But we don't yet know whether it is a valid data type
   * nor whether the data values are valid either
   *
   * So we need to check that it is an array of 3 reals or 3 ubytes
   * and report an error or update the current graphics state line_style.pen_source
   * appropriately
   */

  /** \todo don't believe this ever returns FALSE! */
  if ( !pclxl_copy_color_space_details(pclxl_context,
                                       graphics_state->color_space_details,
                                       &color_details->color_space_details) ) {
    return(FALSE);
  }

  if ( rgb_color_attr->data_type == PCLXL_DT_Real32_Array ) {
    if ( !pclxl_copy_color_array(pclxl_context, graphics_state, color_details,
                                 NULL, PCLXL_e8Bit, rgb_color_attr->value.v_real32s,
                                 NULL, rgb_color_attr->array_length) ) {
      return FALSE;
    }

  } else if ( rgb_color_attr->data_type == PCLXL_DT_UByte_Array ) {
    if ( !pclxl_copy_color_array(pclxl_context, graphics_state, color_details,
                                 rgb_color_attr->value.v_ubytes, PCLXL_e8Bit, NULL,
                                 NULL, rgb_color_attr->array_length) ) {
      return FALSE;
    }
  }

  PCLXL_DEBUG(PCLXL_DEBUG_COLOR,
              ("Set RGBColor [ %f, %f, %f ], bit-depth = %d",
               color_details->color_array[PCLXL_RED_CHANNEL],
               color_details->color_array[PCLXL_GREEN_CHANNEL],
               color_details->color_array[PCLXL_BLUE_CHANNEL],
               pclxl_color_depth_bits(graphics_state->color_space_details->color_depth)));

  return TRUE;
}

static Bool
pclxl_set_primary_array_source(PCLXL_PARSER_CONTEXT parser_context,
                               PCLXL_ATTRIBUTE      primary_array_attr,
                               PCLXL_ColorDepth     color_depth,
                               PCLXL_COLOR_DETAILS  color_details)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;

  /** \todo Don't believe this can ever return FALSE! */
  if ( !pclxl_copy_color_space_details(pclxl_context,
                                       graphics_state->color_space_details,
                                       &color_details->color_space_details)) {
    return(FALSE);
  }

  if ( primary_array_attr->data_type == PCLXL_DT_Real32_Array ) {
    if ( !pclxl_copy_color_array(pclxl_context, graphics_state, color_details,
                                 NULL, color_depth, primary_array_attr->value.v_real32s,
                                 NULL, primary_array_attr->array_length) ) {
      return FALSE;
    }

  } else if ( primary_array_attr->data_type == PCLXL_DT_UByte_Array ) {
    if ( !pclxl_copy_color_array(pclxl_context, graphics_state, color_details,
                                 primary_array_attr->value.v_ubytes, color_depth, NULL,
                                 NULL, primary_array_attr->array_length) ) {
      return FALSE;
    }
  }

  PCLXL_DEBUG(PCLXL_DEBUG_COLOR,
              ("Set PrimaryArray %d color channel intensities, bit-depth = %d",
               color_details->color_array_len,
               pclxl_color_depth_bits(graphics_state->color_space_details->color_depth)));

  return TRUE;
}

static Bool
pclxl_set_pattern_source(PCLXL_PARSER_CONTEXT parser_context,
                         PCLXL_ATTRIBUTE      pat_sel_id_attr,
                         PCLXL_ATTRIBUTE      pat_origin_attr,
                         PCLXL_ATTRIBUTE      new_dest_size_attr,
                         PCLXL_COLOR_DETAILS  color_details)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PclXLPattern *pattern;

  color_details->pattern_id = pat_sel_id_attr->value.v_int16;

  pattern = pclxl_pattern_find(pclxl_context, color_details->pattern_id);
  if ( !pattern ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_PATMGR, PCLXL_BAD_PATTERN_ID,
                        ("Undefined pattern id %d", color_details->pattern_id));
    return FALSE;
  }

  /* Pattern angle of rotation and scaling must be store at pen/brush setting time. */
  color_details->pattern_angle = graphics_state->page_angle;
  color_details->pattern_scale.x = ( graphics_state->current_ctm.matrix[0][0] != 0
                                     ? fabs(graphics_state->current_ctm.matrix[0][0])
                                     : fabs(graphics_state->current_ctm.matrix[0][1]) );
  color_details->pattern_scale.y = ( graphics_state->current_ctm.matrix[1][0] != 0
                                     ? fabs(graphics_state->current_ctm.matrix[1][0])
                                     : fabs(graphics_state->current_ctm.matrix[1][1]) );

  /* The spec states patterns with default rotation are offset from the
     bottom of the physical page, but patterns are offset from the page
     origin (SetPageOrigin) for all angles of rotation on the printers. */
  if ( pat_origin_attr ) {
    PCLXL_SysVal x, y;

    x = pat_origin_attr->value.v_int16s[0];
    y = pat_origin_attr->value.v_int16s[1];

    MATRIX_TRANSFORM_XY(x, y, x, y, &graphics_state->current_ctm);

    SETXY(color_details->pattern_origin, x, y);
  } else {
    SETXY(color_details->pattern_origin, graphics_state->current_ctm.matrix[2][0],
          graphics_state->current_ctm.matrix[2][1]);
  }

  /* A pattern destination size of 0,0 is not legal so use 0,0 internally to
     mean that destination size the same as pattern size. */
  SETXY(color_details->destination_size, 0, 0);

  if ( new_dest_size_attr ) {
    SETXY(color_details->destination_size, new_dest_size_attr->value.v_uint16s[0],
          new_dest_size_attr->value.v_uint16s[1]);

    if ( color_details->destination_size.x == 0 ||
         color_details->destination_size.y == 0 ) {
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_KERNEL,
                                 PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                                 ("NewDestinationSize values must not be zero"));
      return FALSE;
    }
  }

  /* The pattern requires a palette but we do not have one */
  if (! pattern->direct && ! (graphics_state->color_space_details->color_palette_len > 0)) {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_IMAGE,
                               PCLXL_PALETTE_UNDEFINED,
                               ("IndexedPixel pattern requires an indexed colorspace"));
    return FALSE ;
  }

  color_details->pattern_enabled = TRUE;

  /* Set color_array_len to something non-zero to ensure pclxl_ps_set_color
     is invoked (color_array_len may be zero if prior brush was null). */
  color_details->color_array_len = 1;
  color_details->color_array[0] = 0; /* arbitrary; not used */

  pclxl_copy_color_space_details(pclxl_context,
                                 graphics_state->color_space_details,
                                 &color_details->color_space_details);

  return TRUE;
}

/*
 * SetBrushSource and SetPenSource
 * both take one of the following "sources"
 *
 * 1) RGB colour intensities
 *    (reals in the range 0.0 to 1.0
 *     or integers in the range 0 to MAXINT for that integer type)
 *
 * 2) Gray level
 *    (again 0.0 to 1.0 or 0 to 255)
 *
 * 3) An array of colour channel intensities
 *    (each 0.0 to 1.0 or 0 to MAXINT)
 *    for all the channels in the current colour space (RGB or CMYK etc.)
 *
 * 4) A pattern select ID (positive integer)
 *    and optional pattern origin (default (0,0))
 *    new destination size
 *
 * 5) A special Null pen
 *    (i.e. pen "source" and "pattern ID" both equal "0" perhaps ;-)
 *
 * Therefore pclxl_handle_source() performs the handling
 * of these sources on behalf of both of these operators
 */

#define PEN_SOURCE    (0)
#define BRUSH_SOURCE  (1)

static Bool
pclxl_handle_source(PCLXL_PARSER_CONTEXT      parser_context,
                    int32                     source,
                    PCLXL_COLOR_DETAILS       color_details)
{
  static PCLXL_ATTR_MATCH match[10] = {
#define SOURCE_RGBCOLOR             (0)
    {PCLXL_AT_RGBColor},
#define SOURCE_GRAY_LEVEL           (1)
    {PCLXL_AT_GrayLevel},
#define SOURCE_PRIMARY_ARRAY        (2)
    {PCLXL_AT_PrimaryArray},
#define SOURCE_PRIMARY_DEPTH        (3)
    {PCLXL_AT_PrimaryDepth},
#define SOURCE_NULL_PEN             (4)
    {PCLXL_AT_NullPen},
#define SOURCE_NULL_BRUSH           (5)
    {PCLXL_AT_NullBrush},
#define SOURCE_PATTERN_SELECT_ID    (6)
    {PCLXL_AT_PatternSelectID},
#define SOURCE_PATTERN_ORIGIN       (7)
    {PCLXL_AT_PatternOrigin},
#define SOURCE_NEW_DESTINATION_SIZE (8)
    {PCLXL_AT_NewDestinationSize},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION primary_depth_values[] = {
    PCLXL_e1Bit,
    PCLXL_e4Bit,
    PCLXL_e8Bit,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_UInt32_XY new_destination_size;
  PCLXL_ColorSpace color_space;
  PCLXL_Real32* rgb;
  double color_level;
  Bool seen_primary;
  Bool seen_pattern;
  int32 null;
  int32 i;
  PCLXL_ColorDepth color_depth;
#if DEBUG_BUILD
  static uint8* op_name[2] = {
    (uint8*)"SetPenSource",
    (uint8*)"SetBrushSource"
  };
#endif /* DEBUG_BUILD */

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match_at_least_1(parser_context->attr_set, match,
                                        pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  seen_primary = match[SOURCE_PRIMARY_ARRAY].result || match[SOURCE_PRIMARY_DEPTH].result;
  seen_pattern = match[SOURCE_PATTERN_SELECT_ID].result || match[SOURCE_PATTERN_ORIGIN].result ||
                  match[SOURCE_NEW_DESTINATION_SIZE].result;

  /* Small hoop to jump through to cope with common code for two operators */
  if ( source == PEN_SOURCE ? match[SOURCE_NULL_BRUSH].result : match[SOURCE_NULL_PEN].result ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_COMBINATION,
                        ("Got multiple color source attributes"));
    return(FALSE);
  }
  null = (source == PEN_SOURCE) ? SOURCE_NULL_PEN : SOURCE_NULL_BRUSH;

  color_space = pclxl_context->graphics_state->color_space_details->color_space;

  /* RGBColor */
  if ( match[SOURCE_RGBCOLOR].result ) {
    if ( match[SOURCE_GRAY_LEVEL].result || seen_primary || match[null].result || seen_pattern ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_COMBINATION,
                          ("Got multiple color source attributes"));
      return(FALSE);
    }

    if ( match[SOURCE_RGBCOLOR].result->array_length != 3 ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ARRAY_SIZE,
                          ("RGB array of wrong size"));
      return(FALSE);
    }

    if ( match[SOURCE_RGBCOLOR].result->data_type == PCLXL_DT_Real32 ) {
      rgb = match[SOURCE_RGBCOLOR].result->value.v_real32s;
      for ( i = 0; i < 3; i++ ) {
        if ( (rgb[i] < 0) || (rgb[i] > 1) ) {
          PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                              ("Real rgb level out of range [0,1]"));
          return(FALSE);
        }
      }
    }

    if ( pclxl_color_space_num_components(color_space) != 3 ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_STATE, PCLXL_COLOR_SPACE_MISMATCH,
                          ("Expect number of color components %d does not match the %s color space number of components %d",
                          3, pclxl_color_space_name(color_space), pclxl_color_space_num_components(color_space)));
      return(FALSE);
    }

    PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_COLOR),
                ("%s(RGBColor)", op_name[source]));

    color_details->pattern_enabled = FALSE;
    return(pclxl_set_rgb_color_source(parser_context, match[SOURCE_RGBCOLOR].result, color_details));
  }

  /* GrayLevel */
  if ( match[SOURCE_GRAY_LEVEL].result ) {
    if ( seen_primary || match[null].result || seen_pattern ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_COMBINATION,
                          ("Got multiple color source attributes"));
      return(FALSE);
    }

    if ( match[SOURCE_GRAY_LEVEL].result->data_type == PCLXL_DT_Real32 ) {
      color_level = pclxl_attr_get_real(match[SOURCE_GRAY_LEVEL].result);
      if ( (color_level < 0) || (color_level > 1) ) {
        PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                            ("Real gray level out of range [0,1]"));
        return(FALSE);
      }
    }

    if ( pclxl_color_space_num_components(color_space) != 1 ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_STATE, PCLXL_COLOR_SPACE_MISMATCH,
                          ("Expect number of color components %d does not match the %s color space number of components %d",
                          1, pclxl_color_space_name(color_space), pclxl_color_space_num_components(color_space)));
      return(FALSE);
    }

    PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_COLOR),
                ("%s(GrayLevel)", op_name[source]));

    color_details->pattern_enabled = FALSE;
    return(pclxl_set_gray_level_source(parser_context, match[SOURCE_GRAY_LEVEL].result, color_details));
  }

  /* PrimaryArray/PrimaryDepth */
  if ( seen_primary ) {
    if ( match[null].result || seen_pattern ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_COMBINATION,
                          ("Got multiple color source attributes"));
      return(FALSE);
    }

    if ( !match[SOURCE_PRIMARY_ARRAY].result || !match[SOURCE_PRIMARY_DEPTH].result ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_MISSING_ATTRIBUTE,
                          ("Did not get both primary color data attributes"));
      return(FALSE);
    }

    /* PrimaryArray */
    if ( pclxl_color_space_num_components(color_space) !=
           match[SOURCE_PRIMARY_ARRAY].result->array_length ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_STATE, PCLXL_COLOR_SPACE_MISMATCH,
                          ("Expect number of color components %d does not match the %s color space number of components %d",
                          match[SOURCE_PRIMARY_ARRAY].result->array_length,
                          pclxl_color_space_name(color_space), pclxl_color_space_num_components(color_space)));
      return(FALSE);
    }

    /* PrimaryDepth */
    if ( !pclxl_attr_match_enumeration(match[SOURCE_PRIMARY_DEPTH].result,
                                       primary_depth_values, &color_depth, pclxl_context,
                                       PCLXL_SS_KERNEL) ) {
      return(FALSE);
    }

    PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_COLOR),
                ("%s(PrimaryArray)", op_name[source]));

    pclxl_set_color_depth(pclxl_context, graphics_state, color_depth);
    color_details->pattern_enabled = FALSE;
    return(pclxl_set_primary_array_source(parser_context,
                                          match[SOURCE_PRIMARY_ARRAY].result,
                                          color_depth, color_details));
  }

  /* NullBrush/Pen */
  if ( match[null].result ) {
    if ( seen_pattern ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_COMBINATION,
                          ("Got multiple color source attributes"));
      return(FALSE);
    }

    if ( pclxl_attr_get_uint(match[null].result) != 0 ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                          ("Invalid Null source attribute value"));
      return(FALSE);
    }

    PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_COLOR),
                ("%s(Null%s)", op_name[source], (source == PEN_SOURCE ? "Pen" : "Brush")));

    color_details->pattern_enabled = FALSE;
    color_details->color_array_len = 0;
    HqMemZero((uint8*)&color_details->color_array, sizeof(color_details->color_array));

    return(TRUE);
  }

  /* PatternSelectID */
  if ( !match[SOURCE_PATTERN_SELECT_ID].result ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_MISSING_ATTRIBUTE,
                        ("Did not get both pattern id"));
    return(FALSE);
  }

  /* NewDestinationSize */
  if ( match[SOURCE_NEW_DESTINATION_SIZE].result ) {
    pclxl_attr_get_uint_xy(match[SOURCE_NEW_DESTINATION_SIZE].result, &new_destination_size);
    if ( (new_destination_size.x == 0) || (new_destination_size.y == 0) ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                          ("New destination cannot be zero in x or y"));
      return(FALSE);
    }
  }

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_COLOR),
              ("%s(PatternSelectID)", op_name[source]));

  color_details->pattern_enabled = FALSE;
  return(pclxl_set_pattern_source(parser_context, match[SOURCE_PATTERN_SELECT_ID].result,
                                  match[SOURCE_PATTERN_ORIGIN].result,
                                  match[SOURCE_NEW_DESTINATION_SIZE].result, color_details));
}

/*
 * Tag 0x63 SetBrushSource
 * Takes same attributes as SetPenSource (below)
 * See pclxl_handle_source() above for details
 */

Bool
pclxl_op_set_brush_source(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_COLOR_DETAILS fill_color_details = &graphics_state->fill_details.brush_source;
  
  return pclxl_handle_source(parser_context, BRUSH_SOURCE, fill_color_details);

  /*
   * Note that we do not have to actually set the
   * underlying Postscript color at this stage
   * All we have to do is record the color to be used
   * when a subsequent operation needs to use it
   */
}

/*
 * Tag 0x79 SetPenSource
 * Takes same attributes as SetBrushSource (above)
 * See pclxl_handle_source() above for details
 */

Bool
pclxl_op_set_pen_source(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_COLOR_DETAILS line_color_details = &graphics_state->line_style.pen_source;

  return pclxl_handle_source(parser_context, PEN_SOURCE, line_color_details);

  /*
   * Note that we do not have to actually set the
   * underlying Postscript color at this stage
   * All we have to do is record the color to be used
   * when a subsequent operation needs to use it
   */
}

/*
 * Tag 0x7a SetPenWidth takes a ubyte or uint16 value
 */

Bool
pclxl_op_set_pen_width(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define SETPENWIDTH_PEN_WIDTH (0)
    {PCLXL_AT_PenWidth | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  int32 pen_width;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* PenWidth */
  pen_width = pclxl_attr_get_int(match[SETPENWIDTH_PEN_WIDTH].result);

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
              ("SetPenWidth(%d)", pen_width));

  return(pclxl_set_pen_width(&pclxl_context->non_gs_state,
                             pclxl_context->graphics_state,
                             pen_width));
}

/******************************************************************************
* Log stripped */
