/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlpsinterface.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Contains various wrappers around Postscript APIs
 * provided by the Core RIP including:
 *
 *  save()
 *  restore()
 *  run_ps_string()
 */

#ifndef __PCLXLPSINTERFACE_H__
#define __PCLXLPSINTERFACE_H__ 1

#include "pclxlparsercontext.h"

Bool pclxl_ps_save(PCLXL_CONTEXT pclxl_context, OBJECT* save_location);

Bool pclxl_ps_restore(PCLXL_CONTEXT pclxl_context, OBJECT* saved_state);

Bool pclxl_ps_run_ps_string(PCLXL_CONTEXT pclxl_context,
                            uint8* postscript_string,
                            size_t postscript_string_len);

PCLXL_SysVal pclxl_ps_units_per_pclxl_uom(uint8 pclxl_uom);

/**
 * Initialise the core for PCL5 rendering; enable backdrop rendering,
 * PCL gstate application, etc.
 *
 * NOTE: pclxl_ps_finish_core() must be called once PCLXL processing is
 * finished, otherwise rendering of subsequent jobs may be corrupt.
 */
Bool pclxl_ps_init_core(PCLXL_CONTEXT pclxl_context);

/**
 * Reset core state. This MUST be called if pclxl_ps_init_core() has been
 * called.
 */
void pclxl_ps_finish_core(PCLXL_CONTEXT pclxl_context);

Bool pclxl_ps_currentpagedevice(PCLXL_CONTEXT pclxl_context,
                                PCLXL_MEDIA_DETAILS current_media_details);

Bool pclxl_ps_setpagedevice(PCLXL_CONTEXT pclxl_context,
                            PCLXL_MEDIA_DETAILS existing_page_device,
                            PCLXL_MEDIA_DETAILS requested_page_device);

Bool pclxl_ps_set_page_scaling(PCLXL_CONTEXT pclxl_context,
                               PCLXL_SysVal scale_x,
                               PCLXL_SysVal scale_y);

Bool pclxl_ps_showpage(PCLXL_CONTEXT pclxl_context,
                       uint32 page_copies);

Bool pclxl_ps_rotate(PCLXL_CONTEXT pclxl_context,
                     PCLXL_SysVal rotation_degrees);

Bool pclxl_ps_scale(PCLXL_CONTEXT pclxl_context,
                    PCLXL_SysVal scale_x,
                    PCLXL_SysVal scale_y);

Bool pclxl_ps_translate(PCLXL_CONTEXT pclxl_context,
                        PCLXL_SysVal translate_x,
                        PCLXL_SysVal translate_y);

Bool pclxl_ps_get_current_point(PCLXL_SysVal_XY* p_xy);

Bool pclxl_ps_is_matrix_invertible(OMATRIX* matrix);

Bool pclxl_ps_set_current_ctm(OMATRIX* p_ctm);

Bool pclxl_ps_transform_point(PCLXL_SysVal_XY* existing_point,
                              OMATRIX*         existing_ctm,
                              OMATRIX*         new_ctm,
                              PCLXL_SysVal_XY* new_point);

Bool pclxl_ps_moveto(PCLXL_CONTEXT pclxl_context,
                     PCLXL_SysVal moveto_x,
                     PCLXL_SysVal moveto_y);

Bool pclxl_ps_moveif(PCLXL_CONTEXT pclxl_context,
                     PCLXL_SysVal moveto_x,
                     PCLXL_SysVal moveto_y);

Bool pclxl_ps_lineto(PCLXL_CONTEXT pclxl_context,
                     PCLXL_SysVal lineto_x,
                     PCLXL_SysVal lineto_y);

Bool pclxl_ps_rlineto(PCLXL_CONTEXT pclxl_context,
                      PCLXL_SysVal lineto_x,
                      PCLXL_SysVal lineto_y);

Bool pclxl_ps_curveto(PCLXL_CONTEXT pclxl_context,
                      PCLXL_SysVal control_point_1_x,
                      PCLXL_SysVal control_point_1_y,
                      PCLXL_SysVal control_point_2_x,
                      PCLXL_SysVal control_point_2_y,
                      PCLXL_SysVal curveto_x,
                      PCLXL_SysVal curveto_y);

Bool pclxl_ps_rcurveto(PCLXL_CONTEXT pclxl_context,
                       PCLXL_SysVal rcontrol_point_1_x,
                       PCLXL_SysVal rcontrol_point_1_y,
                       PCLXL_SysVal rcontrol_point_2_x,
                       PCLXL_SysVal rcontrol_point_2_y,
                       PCLXL_SysVal rcurveto_x,
                       PCLXL_SysVal rcurveto_y);

Bool pclxl_ps_plot_char(PCLXL_CONTEXT pclxl_context,
                        uint16 character,
                        Bool outline_char_path);

Bool pclxl_ps_show_string(PCLXL_CONTEXT pclxl_context,
                          PCLXL_SysVal x,
                          PCLXL_SysVal y,
                          uint8* string,
                          PCLXL_SysVal* x_advance);

Bool pclxl_ps_fill(PCLXL_CONTEXT pclxl_context);

Bool pclxl_ps_stroke(PCLXL_CONTEXT pclxl_context);

/**
 * Set the current colorspace in the core.
 */
Bool pclxl_ps_set_colorspace(PCLXL_CONTEXT pclxl_context,
                             PCLXL_COLOR_SPACE_DETAILS color_space_details);

/**
 * Set the current colorspace in the core using a specific color type.
 * Most clients should not use this method; rather pclxl_ps_set_colorspace()
 * should be used, as the same color type is used for almost all PCLXL color.
 * Only use this method if you require a specific color type to be used.
 */
Bool pclxl_ps_set_colorspace_explicit(PCLXL_CONTEXT pclxl_context,
                                      PCLXL_COLOR_SPACE_DETAILS color_space_details,
                                      int32 color_type);

Bool pclxl_ps_set_color(PCLXL_CONTEXT pclxl_context,
                        PCLXL_COLOR_DETAILS color_details,
                        Bool for_an_image);

/**
 * Set the passed shade in DeviceGray.
 */
Bool pclxl_ps_set_shade(USERVALUE shade);

Bool pclxl_ps_set_pattern(PCLXL_CONTEXT pclxl_context,
                          PCLXL_COLOR_DETAILS color_details);

Bool
pclxl_ps_set_black_preservation(PCLXL_CONTEXT             pclxl_context,
                                PCLXL_COLOR_SPACE_DETAILS color_space_details,
                                Bool                      override_vector_objects_setting);

Bool pclxl_ps_set_line_width(PCLXL_SysVal line_width);

Bool pclxl_ps_set_miter_limit(PCLXL_CONTEXT pclxl_context,
                              PCLXL_SysVal miter_limit);

Bool pclxl_ps_select_font(PCLXL_CONTEXT pclxl_context, Bool for_error_page);

Bool pclxl_ps_set_char_mode(PCLXL_GRAPHICS_STATE graphics_state, uint8 wmode, uint8 vmode);

Bool pclxl_ps_init_pcl_gstate(PCLXL_NON_GS_STATE non_gs_state);

Bool pclxl_ps_enable_pcl_gstate(PCLXL_NON_GS_STATE non_gs_state);

Bool pclxl_ps_set_rop3(PCLXL_CONTEXT pclxl_context, PCLXL_ROP3 rop3, Bool stroke_choke);

void pclxl_pcl_set_source_transparency(PCLXL_CONTEXT pclxl_context, Bool transparent);

void pclxl_pcl_set_paint_transparency(PCLXL_CONTEXT pclxl_context, Bool transparent);

void pclxl_pcl_grestore(PCLXL_CONTEXT pclxl_context, PCLXL_GRAPHICS_STATE graphics_state);

Bool pclxl_ps_clip(PCLXL_CONTEXT pclxl_context, Bool ext_clip, Bool eo_clip);

Bool pclxl_ps_set_line_cap(PCLXL_GRAPHICS_STATE graphics_state,
                           PCLXL_LineCap linecap);

Bool pclxl_ps_object_halftone(PCLXL_GRAPHICS_STATE graphics_state);

Bool pclxl_ps_set_device_dither_origin(PCLXL_GRAPHICS_STATE graphics_state);

void pclxl_free_clip_record(PCLXL_GRAPHICS_STATE graphics_state);

void pclxl_save_clip_record(PCLXL_CONTEXT pclxl_context);

Bool pclxl_restore_clip_record(PCLXL_CONTEXT pclxl_context);

Bool pclxl_ps_set_page_clip(PCLXL_CONTEXT pclxl_context,
                            OMATRIX* pcl5_ctm,
                            uint32 pcl5_page_clip_offsets[4]);

#endif /* __PCLXLPSINTERFACE_H__ */

/******************************************************************************
* Log stripped */
