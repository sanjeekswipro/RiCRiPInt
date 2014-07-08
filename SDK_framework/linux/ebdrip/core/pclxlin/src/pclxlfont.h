/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlfont.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Additional font-related functions
 */

#ifndef __PCLXLFONT_H__
#define __PCLXLFONT_H__ 1

#include "pclxlcontext.h"
#include "pclxltypes.h"

typedef struct pclxl_font_header_struct
{
  PCLXL_CONTEXT    pclxl_context;
  uint8*           font_name;
  uint32           font_name_len;
  PCLXL_TAG        font_name_type;
  PCLXLSTREAM*     font_data_stream;
  uint8            font_format;
  uint8*           font_header_data;
  uint32           font_header_data_len;
  uint32           font_header_data_alloc_len;
} PCLXL_FONT_HEADER_STRUCT;

typedef struct pclxl_char_data_struct
{
  PCLXL_CONTEXT    pclxl_context;
  uint8*           font_name;
  uint32           font_name_len;
  PCLXL_TAG        font_name_type;
  PCLXLSTREAM*     char_data_stream;
  uint32           char_count;
  uint8*           char_data;
  uint32           char_data_size;
  uint32           char_data_alloc_len;
} PCLXL_CHAR_DATA_STRUCT;

void pclxl_delete_font_header(PCLXL_FONT_HEADER font_header);

void pclxl_delete_char_data(PCLXL_CHAR_DATA char_data);

Bool pclxl_set_courier_weight(PCLXL_CONTEXT pclxl_context,
                              uint32        courier_weight);

Bool pclxl_set_default_font(PCLXL_CONTEXT pclxl_context,
                            uint8*        font_name,
                            uint32        font_name_len,
                            PCLXL_SysVal  point_size,
                            uint32        symbol_set,
                            Bool          outline_char_path);

Bool pclxl_set_pclxl_font(PCLXL_PARSER_CONTEXT parser_context,
                          uint8*               pclxl_font_name,
                          uint32               pclxl_font_name_len,
                          PCLXL_TAG            pclxl_font_name_type,
                          PCLXL_SysVal         char_size,
                          uint32               symbol_set,
                          Bool                 outline_char_path);

extern void
pclxl_record_font_details(PCLXL_GRAPHICS_STATE graphics_state,
                          uint8                pclxl_font_state,
                          uint8                pclxl_font_type,
                          uint8*               pclxl_font_name,
                          uint8                pclxl_font_name_len,
                          PCLXL_SysVal         pclxl_char_size,
                          PCLXL_FontID         font_id,
                          uint32               symbol_set,
                          uint8*               ps_font_name,
                          uint8                ps_font_name_len,
                          PCLXL_SysVal         ps_font_point_size,
                          Bool                 ps_font_is_bitmapped);

extern void
pcl5_record_font_details(PCLXL_GRAPHICS_STATE graphics_state,
                         uint8                pclxl_font_state,
                         uint8                pclxl_font_type,
                         uint8*               pclxl_font_name,
                         uint8                pclxl_font_name_len,
                         PCLXL_SysVal         pclxl_char_size,
                         PCLXL_FontID         font_id,
                         uint32               symbol_set,
                         int32                spacing,
                         float                pitch,
                         float                height,
                         int32                style,
                         int32                weight,
                         int32                typeface,
                         uint8*               ps_font_name,
                         uint8                ps_font_name_len,
                         PCLXL_SysVal         ps_font_point_size,
                         Bool                 ps_font_is_bitmapped);

extern Bool
pclxl_set_font(PCLXL_PARSER_CONTEXT parser_context,
               PCLXL_FONT_DETAILS   font_details,
               Bool                 outline_char_path);

extern Bool
pclxl_remove_soft_fonts(PCLXL_CONTEXT pclxl_context,
                        Bool          include_permanent_soft_fonts);

#endif

/******************************************************************************
* Log stripped */
