/** \file
 * \ingroup hpgl2
 *
 * $HopeName: COREpcl_pcl5!src:hpgl2linefill.h(EBDSDK_P.1) $
 * $Id: src:hpgl2linefill.h,v 1.33.1.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the HPGL2 "Polygon Group" category.
 */

#ifndef __HPGL2LINEFILL_H__
#define __HPGL2LINEFILL_H__

#include "pcl5context.h"

struct PATHINFO;

/* pens */
/* pens can be in the range 0 to 32678. */
enum {
  HPGL2_NO_PEN = -2,
  HPGL2_ALL_PENS = -1,
  HPGL2_PEN_WHITE = 0,    /* white */
  HPGL2_PEN_BLACK        /* black */
} ;

/* line caps */
enum {
  HPGL2_LINE_END_BUTT = 1,
  HPGL2_LINE_END_SQUARE,
  HPGL2_LINE_END_TRIANGLE,
  HPGL2_LINE_END_ROUND
} ;

/* line joins */
enum {
  HPGL2_LINE_JOIN_MITRED = 1,
  HPGL3_LINE_JOIN_MITRED_BEVELED,
  HPGL2_LINE_JOIN_TRIANGULAR,
  HPGL2_LINE_JOIN_ROUNDED,
  HPGL2_LINE_JOIN_BEVELED,
  HPGL2_LINE_JOIN_NONE
} ;

/* pen width selection modes */
enum {
  HPGL2_PEN_WIDTH_METRIC = 0,
  HPGL2_PEN_WIDTH_RELATIVE,
} ;

typedef struct HPGL2LineTypeInfo {
  int32     type ;
  HPGL2Real pattern_length ;
  int32     mode ;
  HPGL2Real residue ;
  HPGL2Real user_linetype[8][21] ; /* First slot in 21 item array is length */
} HPGL2LineTypeInfo ;

enum {
    HPGL2_FILL_SOLID_1 = 1,
    HPGL2_FILL_SOLID_2 = 2,
    HPGL2_FILL_HATCH = 3,
    HPGL2_FILL_CROSSHATCH = 4,
    HPGL2_FILL_SHADING = 10,
    HPGL2_FILL_USER = 11,
    HPGL2_FILL_PCL_CROSSHATCH = 21,
    HPGL2_FILL_PCL_USER = 22
};

enum {
  HPGL2_SV_SOLID = 0,
  HPGL2_SV_SHADING = 1,
  HPGL2_SV_USER = 2,
  HPGL2_SV_PCL_CROSSHATCH = 21,
  HPGL2_SV_PCL_USER = 22
};

enum {
  HPGL2_HATCH_SPACING_DEFAULT = 0,
  HPGL2_HATCH_SPACING_USER,
  HPGL2_HATCH_SPACING_PLOTTER
};

enum {
  HPGL2_LINETYPE_SOLID = 999
} ;

typedef struct HPGL2FillHatching
{
  HPGL2Real spacing;
  HPGL2Real angle; /* +ve angle, in radians. */
  uint8 spacing_type;
} HPGL2FillHatching;

typedef struct HPGL2FillShading
{
  HPGL2Real level;
} HPGL2FillShading;

typedef struct HPGL2FillUser
{
  HPGL2Integer index;
  HPGL2Integer pen_choice;
} HPGL2FillUser;

typedef struct HPGL2FillSVUser
{
  HPGL2Integer index;
  HPGL2Integer pen_choice;
} HPGL2FillSVUser;

typedef struct HPGL2FillPCLHatch
{
  HPGL2Integer type;
} HPGL2FillPCLHatch;

typedef struct HPGL2FillPCLUser
{
  HPGL2Integer pattern_id;
} HPGL2FillPCLUser;

/* We are obliged to retain the previously specified parameters, to return
 * to them if the options for a fill type are not given.
 */
typedef struct HPGL2FillParams {
  uint8 fill_type;
  HPGL2FillHatching hatch;
  HPGL2FillHatching cross_hatch;
  HPGL2FillShading shading;
  HPGL2FillUser user;
  HPGL2FillPCLHatch pcl_hatch;
  HPGL2FillPCLUser pcl_user;
} HPGL2FillParams;

/* screened vectors are basically a subset of the fill params. */
typedef struct HPGL2SVParams {
  uint8 fill_type;
  HPGL2FillShading shading;
  HPGL2FillSVUser user;
  HPGL2FillPCLHatch pcl_hatch;
  HPGL2FillPCLUser pcl_user;
} HPGL2SVParams;

/* Be sure to update default_HPGL2_line_fill_info() if you change this structure. */
typedef struct HPGL2LineFillInfo {
  HPGL2LineTypeInfo line_type ;
  Bool              previous_line_type_valid ;
  HPGL2LineTypeInfo previous_line_type ;
  /* PCL COMPATIBILITY.
   * HP4250 / HP4650 printers don't seem to require record of pen positions for
   * LT99 processing.
   * HPGL2Point        linetype99_point ;
   */
  uint8             line_end;
  uint8             line_join;
  HPGL2Real         miter_limit;
  HPGL2Integer      pen_turret;
  HPGL2Real         black_pen_width;
  HPGL2Real         white_pen_width;
  HPGL2Integer      pen_width_selection_mode;
  HPGL2Integer      selected_pen;
  uint16            symbol_mode_char ; /* A value of NUL indicates symbol mode is off. */
  HPGL2Point        anchor_corner;
  HPGL2Point        job_anchor_corner;
  HPGL2FillParams   fill_params;
  uint8             transparency;
  HPGL2SVParams     screened_vectors;
} HPGL2LineFillInfo;

/**
 * Initialise default line fill info.
 */
void default_HPGL2_line_fill_info(HPGL2LineFillInfo* self);

/** changes to scale need to re-set line width */
void hpgl2_sync_linewidth(PCL5Context *pcl5_ctxt);

void hpgl2_set_pen_width(PCL5Context *pcl5_ctxt, HPGL2Integer pen, HPGL2Real pen_width) ;

HPGL2Real hpgl2_get_pen_width(PCL5Context *pcl5_ctxt, HPGL2Integer pen) ;

void hpgl2_set_pen_width_mode(PCL5Context *pcl5_ctxt, HPGL2Integer pen_width_mode) ;

HPGL2Integer hpgl2_get_pen_width_mode(PCL5Context *pcl5_ctxt) ;

Bool pen_is_selected(PCL5Context *pcl5_ctxt);

void hpgl2_linetype_clear(HPGL2LineFillInfo *linefill_info) ;

/**
 * Reset the default values, depending on whether the context is
 * IN or DF.
 */
void hpgl2_set_default_linefill_info(HPGL2LineFillInfo *line_info,
                                     Bool initialize,
                                     HPGL2Point *default_anchor_corner);

/** Synchronise the gsate and the HPGL line fill info. Pen state is synced
 * in preference to fill state, where there is a clash. */
void hpgl2_sync_linefill_info(PCL5Context *pcl5_ctxt,
                              Bool initialize);

/* Stroking and filling affect overlapping parts of the core state, so
 * will need to be able to choose which components to sync.
 */
void hpgl2_sync_pen_color(PCL5Context *pcl5_ctxt, Bool force_solid);

/* Sync the fill mode state to the gstate, force_pen_solid allows the
 * pen to be forced to be solid colors, required for some character fill
 * operations, but normally false.
 */
void hpgl2_sync_fill_mode(PCL5Context *pcl5_ctxt, Bool force_pen_solid);

void hpgl2_sync_transparency(HPGL2LineFillInfo *line_info);

void hpgl2_set_current_pen(PCL5Context *pcl5_ctxt, HPGL2Integer pen,
                           Bool force_solid);

/* Early drop rop D */
void hpgl2_ensure_pen_in_palette_range(PCL5Context *pcl5_ctxt);

HPGL2Integer hpgl2_get_current_pen(PCL5Context *pcl5_ctxt) ;

Bool fix_all_hatch_spacing(PCL5Context *pcl5_ctxt);

/* Hatch fill the given path. The path coordinates will be drawn in the current
 * gstate coordinate system. fill rule applies to erasure of the path before
 * hatching.
 */
Bool hpgl2_hatchfill_path(PCL5Context *pcl5_ctxt,
                          struct PATHINFO *the_path,
                          int32 fill_rule,
                          Bool do_copypath);


HPGL2Real hpgl2_get_default_pen_width(HPGL2Integer mode);

struct ColorPalette;

void hpgl2_set_palette_pen_width(struct ColorPalette *palette,
                                 HPGL2Integer pen,
                                 HPGL2Real pen_width);

HPGL2Real hpgl2_get_palette_pen_width(struct ColorPalette *palette,
                                 HPGL2Integer pen);

/* Set the current line type, and resync it to gstate. Sets up saved LT info
 * for LT99 operator, if appropriate.
 */
Bool LT_internal(PCL5Context *pcl_ctxt,
                 HPGL2Integer type,
                 HPGL2Real pattern_length,
                 HPGL2Integer mode,
                 HPGL2Real residue);

/* LT99 operator implementation. */
Bool restore_previous_linetype_internal(PCL5Context *pcl5_ctxt,
                                        Bool force_non_adaptive);

/* LT; operator implementation. */
Bool LT_default(PCL5Context *pcl5_ctxt);

struct LINESTYLE ;

/* Use butt cap and no join for lines 0.35mm or less. */
void hpgl2_override_thin_line_attributes(PCL5Context *pcl5_ctxt,
                                         struct LINESTYLE *linestyle);

struct PATHINFO;
/* define a general fill path routine for HPGL. */
Bool hpgl2_fill_path(PCL5Context *pcl5_ctxt,
                     struct PATHINFO *the_path,
                     int32 fill_mode);

/* Fill the current gstate path according to HPGL state. A wrapper for
 * hpgl2_fill_path. */
Bool hpgl2_fill(PCL5Context *pcl5_ctxt, int32 fill_mode, Bool newpath);

/* Reinterpret the last given coordinates for an AC command in whatever user
 * scaled space is currently in force. */
Bool hpgl2_redo_AC(PCL5Context *pcl5_ctxt);

/* --- HPGL2 operators --- */
Bool hpgl2op_AC(PCL5Context *pcl5_ctxt) ;

Bool hpgl2op_FT(PCL5Context *pcl5_ctxt) ;

Bool hpgl2op_LA(PCL5Context *pcl5_ctxt) ;

Bool hpgl2op_LT(PCL5Context *pcl5_ctxt) ;

Bool hpgl2op_PW(PCL5Context *pcl5_ctxt) ;

Bool hpgl2op_RF(PCL5Context *pcl5_ctxt) ;

Bool hpgl2op_SM(PCL5Context *pcl5_ctxt) ;

Bool hpgl2op_SP(PCL5Context *pcl5_ctxt) ;

Bool hpgl2op_SV(PCL5Context *pcl5_ctxt) ;

Bool hpgl2op_TR(PCL5Context *pcl5_ctxt) ;

Bool hpgl2op_UL(PCL5Context *pcl5_ctxt) ;

Bool hpgl2op_WU(PCL5Context *pcl5_ctxt) ;

/* ============================================================================
* Log stripped */
#endif
