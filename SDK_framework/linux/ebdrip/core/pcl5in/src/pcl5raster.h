/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pcl5raster.h(EBDSDK_P.1) $
 * $Id: src:pcl5raster.h,v 1.36.4.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

#ifndef __PCL5RASTER_H__
#define __PCL5RASTER_H__ (1)

#include "pcl5context.h"
#include "cursorpos.h"

typedef struct PCL5_RasterReader PCL5_RasterReader ;

/* Raster encoding mode. */
enum {
  /* The order of the first four MUST not change. */
  PCL5_INDEXED_BY_PLANE = 0,
  PCL5_INDEXED_BY_PIXEL,
  PCL5_DIRECT_BY_PLANE,
  PCL5_DIRECT_BY_PIXEL,
  /* Order from here on does not matter. */
  PCL5_MONOCHROME
} ;


/* Raster compression method. */
enum {
  PCL5_UNENCODED = 0, /* Unencoded. */
  PCL5_RUN_LENGTH,    /* Run-length encoding. */
  PCL5_TIFF,          /* Tagged Imaged File Format (TIFF) rev 4.0 */
  PCL5_DELTA_ROW,     /* Delta row compression. */
  PCL5_ADAPTIVE = 5   /* Adaptive compression. */
} ;

/* Be sure to update default_raster_graphics_info() if you change this structure. */
typedef struct RasterGraphicsInfo {
  /* Explicitly required */
  PCL5Real     left_margin;  /* N.B. The distance from the left or top of the logical page, NOT the coordinate value */
  uint32       resolution;
  uint32       presentation;
  uint32       compression_method;
  Bool         raster_scaling ;
  uint32       orig_source_width;              /* In image pixels */
  uint32       source_width ;
  Bool         explicit_source_width ;
  uint32       orig_source_height;             /* In image pixels */
  uint32       source_height ;
  Bool         explicit_source_height ;
  double       destination_width ;             /* In PCL internal units */
  Bool         explicit_destination_width ;
  double       destination_height ;            /* In PCL internal units */
  Bool         explicit_destination_height ;
  double       x_scale, y_scale ;
  /* These 6 set up via ESC * v 6 W, see pcl5color.c */
  uint8        colorspace ;
  uint8        pixel_encoding_mode ;
  uint8        bits_per_index ;
  uint8        bits_for_component_one ;
  uint8        bits_for_component_two ;
  uint8        bits_for_component_three ;
  /* Extra internal state. */
  uint8        num_components ;
  uint8        ps_bits_per_index ;
  uint8        num_planes ;
  Bool         graphics_started ;              /* Has an implicit or explicit start graphics since the last end graphics? */
  uint32       num_rows ;                      /* How many rows of data have we seen - may be needed when reader is Null */
  CursorPosition orig_cursor_pos ;             /* The original cursor position before moving to the top left of the image */
  CursorPosition start_pos ;                   /* The position of the top left corner of the image */
  PCL5_RasterReader *reader ;
} RasterGraphicsInfo;

/* Get hold of the RasterGraphicsInfo */
RasterGraphicsInfo* get_rast_info(PCL5Context *pcl5_ctxt) ;

/**
 * Initialise default raster graphics info.
 */
void default_raster_graphics_info(RasterGraphicsInfo* self) ;

/**
 * Save and restore RasterGraphicsInfo
 */
void save_raster_graphics_info(PCL5Context *pcl5_ctxt, RasterGraphicsInfo *to, RasterGraphicsInfo *from, Bool overlay) ;
void restore_raster_graphics_info(PCL5Context *pcl5_ctxt, RasterGraphicsInfo *to, RasterGraphicsInfo *from) ;

/* End raster graphics callback */
Bool end_raster_graphics_callback(PCL5Context *pcl5_ctxt, Bool explicit_end) ;

/**
 * Set the left graphics margin, (needed for overlay macros).
 */
void set_left_graphics_margin(PCL5Context *pcl5_ctxt, Bool default_margin) ;

Bool pcl5op_star_b_M(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_b_W(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_b_V(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_b_Y(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_r_A(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_r_B(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_r_C(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_r_F(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_r_S(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_r_T(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_t_H(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_t_K(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_t_R(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_t_V(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

/* ============================================================================
* Log stripped */
#endif
