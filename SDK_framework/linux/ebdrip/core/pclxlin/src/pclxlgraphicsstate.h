/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlgraphicsstate.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 */

#ifndef __PCLXLGRAPHICSSTATE_H__
#define __PCLXLGRAPHICSSTATE_H__

#include "pclxlgraphicsstatet.h"
#include "pclxlcontext.h"

/**
 * \brief at the start of each PCLXL job
 * and at the start of each page in in the job
 * we re-establish the default graphics state
 *
 * We *may* do this by "pushing" and "popping"
 * a copy of a parent graphics state.
 * Or we may do this by resetting the current graphics state in-situ
 *
 * Which ever way this is done, this function
 * (re)sets the supplied graphics state structure
 * (back) to this default.
 *
 * I.e. this function may be called just once at the start of the job
 * or may be called before each and every page.
 */

extern Bool
pclxl_set_default_graphics_state(PCLXL_CONTEXT pclxl_context,
                                 PCLXL_GRAPHICS_STATE graphics_state);

/**
 * \brief creates a new PCLXL graphics state instance
 * copying the contents of an existing graphics state if one is supplied.
 * If no existing graphics state is supplied then suitable "initial"
 * graphics state values are established
 */

extern PCLXL_GRAPHICS_STATE
pclxl_create_graphics_state(PCLXL_GRAPHICS_STATE existing_graphics_state,
                            PCLXL_CONTEXT pclxl_context);

extern void
pclxl_delete_graphics_state(PCLXL_CONTEXT pclxl_context,
                            PCLXL_GRAPHICS_STATE graphics_state);

extern void
pclxl_push_graphics_state(PCLXL_GRAPHICS_STATE graphics_state,
                          PCLXL_GRAPHICS_STATE_STACK* graphics_state_stack);

extern PCLXL_GRAPHICS_STATE
pclxl_pop_graphics_state(PCLXL_GRAPHICS_STATE_STACK* graphics_state_stack);

extern PCLXL_NON_GS_STATE
pclxl_init_non_gs_state(PCLXL_CONTEXT pclxl_context,
                        PCLXL_NON_GS_STATE non_gs_state);

extern Bool
pclxl_get_default_media_size(PCLXL_CONTEXT    pclxl_context,
                             uint8*           p_media_size_value_type,
                             PCLXL_MediaSize* p_media_size_enum,
                             uint8**          p_media_size_name,
                             PCLXL_SysVal_XY* p_media_size_xy);

extern Bool
pclxl_get_default_media_details(PCLXL_CONTEXT       pclxl_context,
                                PCLXL_MEDIA_DETAILS media_details);

void
pclxl_get_current_path_and_point(PCLXL_GRAPHICS_STATE graphics_state);

extern OMATRIX*
pclxl_recalculate_font_matrix(PCLXL_CONTEXT pclxl_context,
                              PCLXL_GRAPHICS_STATE graphics_state,
                              PCLXL_CHAR_DETAILS char_details,
                              OMATRIX* font_matrix);

extern Bool
pclxl_reset_current_point(PCLXL_CONTEXT pclxl_context,
                          PCLXL_SysVal_XY* previous_point,
                          OMATRIX* previous_ctm,
                          OMATRIX*  new_ctm,
                          PCLXL_SysVal_XY* new_point);

extern void
pclxl_get_current_ctm(PCLXL_CONTEXT pclxl_context,
                      OMATRIX* current_ctm,
                      Bool* is_invertible);

extern Bool
pclxl_set_page_rotation(PCLXL_CONTEXT pclxl_context,
                        PCLXL_Int32 rotation_angle);

extern Bool
pclxl_set_page_scale(PCLXL_CONTEXT pclxl_context,
                     PCLXL_SysVal_XY* scale_factors);

extern Bool
pclxl_set_current_ctm(PCLXL_GRAPHICS_STATE graphics_state,
                      OMATRIX*             current_ctm);

extern Bool
pclxl_set_pen_width(PCLXL_NON_GS_STATE   non_gs_state,
                    PCLXL_GRAPHICS_STATE graphics_state,
                    PCLXL_SysVal         pen_width);

Bool
pclxl_set_miter_limit(PCLXL_CONTEXT pclxl_context,
                      PCLXL_GRAPHICS_STATE graphics_state,
                      PCLXL_SysVal miter_limit);

Bool
pclxl_set_color_space(PCLXL_CONTEXT        pclxl_context,
                      PCLXL_GRAPHICS_STATE graphics_state,
                      PCLXL_ColorSpace     color_space);

/**
 * Return the current color space.
 */
PCLXL_ColorSpace
pclxl_get_colorspace(PCLXL_GRAPHICS_STATE graphics_state);

extern uint8*
pclxl_color_space_name(PCLXL_ColorSpace color_space);

/**
 * Return the number of components in the passed color space.
 */
uint8
pclxl_color_space_num_components(PCLXL_ColorSpace color_space);

void
pclxl_set_color_depth(PCLXL_CONTEXT        pclxl_context,
                      PCLXL_GRAPHICS_STATE graphics_state,
                      PCLXL_ColorDepth     color_depth);

Bool
pclxl_set_black_types(PCLXL_CONTEXT         pclxl_context,
                      PCLXL_GRAPHICS_STATE  graphics_state,
                      PCLXL_BlackType       raster_black_type,
                      PCLXL_BlackType       text_black_type,
                      PCLXL_BlackType       vector_black_type);

void
pclxl_free_color_palette(PCLXL_CONTEXT pclxl_context,
                         PCLXL_COLOR_SPACE_DETAILS color_space_details);

extern uint8
pclxl_color_depth_bits(PCLXL_ColorDepth color_depth);

extern Bool
pclxl_copy_color_palette(PCLXL_CONTEXT pclxl_context,
                         PCLXL_COLOR_SPACE_DETAILS color_space_details,
                         uint8*                    palette_data,
                         uint32                    palette_data_len,
                         PCLXL_ColorDepth          color_depth);

extern Bool
pclxl_copy_color_space_details(PCLXL_CONTEXT pclxl_context,
                               PCLXL_COLOR_SPACE_DETAILS  existing_color_space_details,
                               PCLXL_COLOR_SPACE_DETAILS* new_color_space_details);

extern Bool
pclxl_copy_color_array(PCLXL_CONTEXT pclxl_context,
                       PCLXL_GRAPHICS_STATE graphics_state,
                       PCLXL_COLOR_DETAILS  color_details,
                       PCLXL_UByte*         primary_array_ubyte_values,
                       PCLXL_ColorDepth     color_depth,
                       PCLXL_Real32*        primary_array_real32_values,
                       PCLXL_SysVal*        primary_array_sysval_values,
                       uint32               primary_array_len);

extern Bool
pclxl_copy_color(PCLXL_CONTEXT pclxl_context,
                 PCLXL_COLOR_DETAILS  existing_color_details,
                 PCLXL_COLOR_DETAILS  new_color_details);

extern Bool
pclxl_set_default_color(PCLXL_CONTEXT pclxl_context,
                        PCLXL_COLOR_SPACE_DETAILS color_space,
                        PCLXL_COLOR_DETAILS       color_details);

extern Bool
pclxl_set_char_angle(PCLXL_GRAPHICS_STATE graphics_state,
                     PCLXL_SysVal         char_angle);


extern Bool
pclxl_set_char_scale(PCLXL_GRAPHICS_STATE graphics_state,
                     PCLXL_SysVal_XY*     char_scale);


extern Bool
pclxl_set_char_shear(PCLXL_GRAPHICS_STATE graphics_state,
                     PCLXL_SysVal_XY*     char_shear);

extern Bool
pclxl_set_char_boldness(PCLXL_GRAPHICS_STATE graphics_state,
                        PCLXL_SysVal         char_boldness);

extern Bool
pclxl_set_char_attributes(PCLXL_CONTEXT pclxl_context,
                          PCLXL_GRAPHICS_STATE graphics_state,
                          PCLXL_WritingMode    writing_mode);

extern Bool
pclxl_set_char_sub_modes(PCLXL_CONTEXT pclxl_context,
                         PCLXL_CHAR_DETAILS char_details,
                         uint8* char_sub_modes,
                         uint32 char_sub_modes_len);

Bool
pclxl_set_fill_mode(PCLXL_GRAPHICS_STATE graphics_state,
                    PCLXL_FillMode       fill_mode);

Bool
pclxl_set_line_cap(PCLXL_GRAPHICS_STATE graphics_state,
                   PCLXL_LineCap        line_cap);

Bool
pclxl_set_line_join(PCLXL_GRAPHICS_STATE graphics_state,
                    PCLXL_LineJoin       line_join);

Bool
pclxl_set_line_dash(PCLXL_CONTEXT pclxl_context,
                    PCLXL_GRAPHICS_STATE graphics_state,
                    PCLXL_ATTRIBUTE line_dash_style_attr,
                    int32 dash_offset);

/**
 * Adjust the object based halftone selection parameters.
 */
Bool
pclxl_set_object_halftone(PCLXL_GRAPHICS_STATE graphics_state,
                          PCLXL_ATTR_ID attrib,
                          PCLXL_HalftoneMethod method,
                          Bool *change);
/**
 * Set device halftone.
 */
Bool
pclxl_set_device_matrix_halftone(PCLXL_GRAPHICS_STATE graphics_state,
                                PCLXL_DitherMatrix method);

/**
 * Set the PCLXL dither anchor.
 */
Bool
pclxl_set_device_dither_phase(PCLXL_CONTEXT pclxl_context,
                              PCLXL_GRAPHICS_STATE graphics_state,
                              SYSTEMVALUE new_origin_x,
                              SYSTEMVALUE new_origin_y);

/* This function is now redundant */
void pclxl_device_setg_reset(PCLXL_GRAPHICS_STATE graphics_state);

#endif /* __PCLXLGRAPHICSSTATE_H__ */

/******************************************************************************
* Log stripped */
