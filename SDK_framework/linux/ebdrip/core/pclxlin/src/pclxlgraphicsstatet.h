/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlgraphicsstatet.h(EBDSDK_P.1) $
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

#ifndef __PCLXLGRAPHICSSTATET_H__
#define __PCLXLGRAPHICSSTATET_H__

#include "pclxltypes.h"
#include "swpfinapi.h"
#include "idhashtable.h"
#include "objectt.h"
#include "matrix.h"
#include "graphics.h"

typedef struct pclxl_graphics_state_struct* PCLXL_GRAPHICS_STATE;

/**
 * \todo the following typedefs are all stub definitions
 * while I find out what is actually required
 */

typedef int16 PCLXL_PatternID;
typedef int   PCLXL_DitherMatrixID;
typedef int   PCLXL_FontID;
typedef int   PCLXL_PaletteID;

/**
 * \brief
 * PCLXL_COLOR_SPACE_DETAILS structure holds details of a PCLXL color space
 * as specified using the SetColorSpace, SetColorDepth, SetColorTreatment and SetColorMapping operators
 *
 * Whenever a SetPenSource or SetBrushSource operator is encountered
 * we must effectively take a snapshot copy of the current color space details
 *
 * However we do this lazily by maintaining a reference count inside
 * the PCLXL_COLOR_SPACE_DETAILS structure
 *
 * When it is initially created but not yet referenced by either
 * a PCLXL_BRUSH_SOURCE_DETAILS or PCLXL_PEN_SOURCE_DETAILS
 * then this reference count will be 1
 *
 * As it is referenced by a either or both of the brush/pen source
 * this reference count is incremented
 *
 * While the reference count remains at 1, any subsequent
 * SetColorSpace, SetColorDepth, SetColorTreatment or SetColorMapping operators
 * can simply update the current color space details in-situ
 *
 * However, if the reference count is greater than 1
 * (i.e. thee current color space details are referenced by either
 * a pen source or brush source) then we must take a copy before
 * applying any [Set]ColorSpace, [Set]ColorTreatment or [Set]ColorMapping change
 *
 * When we "free" a color space we decrement the reference count
 * and only actually perform the underlying memory deallocation
 * when this reference count reaches 0
 */

typedef struct pclxl_color_space_details_struct
{
  PCLXL_ColorSpace        color_space;
  PCLXL_ColorDepth        color_depth;
  /*
   * Note that we always expand (i.e. unpack) raw palette data
   * into (unsigned) byte values for each color entry in the pallet
   *
   * I.e. if we have 5 bytes of 4-bit depth palette data
   * and this therefore represents 10 separate colors
   * we will store this as a 10-byte color palette
   * so the resultant palette can be indexed directly
   * (See pclxl_copy_palette_data)
   */
  uint8*                  color_palette;
  uint32                  color_palette_len;
  PCLXL_ColorMapping      color_mapping;
  PCLXL_ColorTreatment    color_treatment;
  PCLXL_BlackType         raster_black_type;
  PCLXL_BlackType         text_black_type;
  PCLXL_BlackType         vector_black_type;

  uint32                  ref_count;
} PCLXL_COLOR_SPACE_DETAILS_STRUCT, *PCLXL_COLOR_SPACE_DETAILS;

/**
 * \brief PCLXL_BRUSH_SOURCE_STRUCT (and its synonym PCLXL_PEN_SOURCE_STRUCT)
 * holds the details supplied as attributes to
 * PCLXL's SetBrushSource and SetPenSource operators
 *
 * An instance of this structure is then used as the *first* member/field
 * in both the PCLXL_LINE_STYLE_STRUCT and PCLXL_FILL_DETAILS_STRUCT structures
 * but is thereafter followed by a divergent set of subsequent
 * structure members/fields
 *
 * In this way we can inter-convert between a pointer to any of
 * PCLXL_BRUSH_SOURCE_STRUCT, PCLXL_PEN_SOURCE_STRUCT,
 * PCLXL_LINE_STYLE_STRUCT and PCLXL_FILL_DETAILS_STRUCT
 * wherever we need to access only these common fields
 */

typedef struct pclxl_brush_pen_source
{
  /*
   * colour channel levels/intensities
   * always stored as a real number in the range 0.0 to 1.0
   * The color_array_len will be 0 for NullPen/NullBrush
   * But I am not sure whether we need a small fixed size array
   * or whether we need to go to the bother/complexity of
   * a dynamically allocated array.
   */

#ifdef DEBUG_BUILD
  PCLXL_SysVal              color_array[4];
#else
  PCLXL_SysVal              color_array[3];
#endif

  uint32                    color_array_len;
  PCLXL_ColorDepth          color_depth;
  PCLXL_COLOR_SPACE_DETAILS color_space_details;
  Bool                      pattern_enabled;
  PCLXL_PatternID           pattern_id;
  PCLXL_Int32               pattern_angle;
  FPOINT                    pattern_scale;
  FPOINT                    pattern_origin;
  IPOINT                    destination_size;
} PCLXL_BRUSH_SOURCE_STRUCT, *PCLXL_BRUSH_SOURCE,
  PCLXL_PEN_SOURCE_STRUCT, *PCLXL_PEN_SOURCE,
  PCLXL_COLOR_DETAILS_STRUCT, *PCLXL_COLOR_DETAILS;

#define PCLXL_GRAY_CHANNEL    0

#define PCLXL_RED_CHANNEL     0
#define PCLXL_GREEN_CHANNEL   1
#define PCLXL_BLUE_CHANNEL    2

#ifdef DEBUG_BUILD
#define PCLXL_CYAN_CHANNEL    0
#define PCLXL_MAGENTA_CHANNEL 1
#define PCLXL_YELLOW_CHANNEL  2
#define PCLXL_BLACK_CHANNEL   3
#endif

#define PCLXL_LINE_DASH_MAX (128)

typedef struct pclxl_line_style_struct
{
  PCLXL_PEN_SOURCE_STRUCT   pen_source;
  PCLXL_SysVal              pen_width;
  PCLXL_LineCap             line_cap;
  PCLXL_LineJoin            line_join;
  PCLXL_SysVal              miter_limit;
  PCLXL_SysVal              line_dash[PCLXL_LINE_DASH_MAX];
  PCLXL_UByte               line_dash_len;
  int32                     dash_offset;
} PCLXL_LINE_STYLE_STRUCT, *PCLXL_LINE_STYLE;

typedef struct pclxl_fill_details_struct
{
  PCLXL_BRUSH_SOURCE_STRUCT brush_source;
  PCLXL_FillMode            fill_mode;
} PCLXL_FILL_DETAILS_STRUCT, *PCLXL_FILL_DETAILS;

typedef struct pcl5_font_selection_criteria
{
  Bool          pcl5_informed;
  int32         initialized;
  int32         symbol_set;
  int32         spacing;
  float         pitch;
  float         height;
  int32         style;
  int32         weight;
  int32         typeface;
} PCL5_FONT_SELECTION_CRITERIA;

typedef struct pclxl_font_details_struct
{
  /*
   * The current state of the font is tracked using
   * the following enumerated states:
   *
   * PCLXL_FS_NO_CURRENT_FONT
   * PCLXL_FS_PCLXL_FONT_SET
   * PCLXL_FS_PFIN_FONT_SELECTED
   * PCLXL_FS_POSTSCRIPT_FONT_SELECTED
   *
   * Various PCLXL operators modify the above state
   * Either advancing the state onto the next state
   * Or bumping the state back down
   *
   * Then when we come to actually plot some characters
   * behind a Text or TextPath operator we will perform
   * whatever steps (including selecting the default PCLXL font if necessary)
   * to ultimately select the correct Postscript font to use
   */

#define PCLXL_FS_NO_CURRENT_FONT          0
#define PCLXL_FS_PCLXL_FONT_SET           1
#define PCLXL_FS_PFIN_FONT_SELECTED       2
#define PCLXL_FS_POSTSCRIPT_FONT_SELECTED 3

  uint8                       pclxl_font_state;

  /*
   * We also need to record whether the current PCLXL font
   * is actually a named PCLXL font
   * or a PCL5 font selected by a PCLSelectFont specification
   */

#define PCLXL_FT_NO_CURRENT_FONT          0
#define PCLXL_FT_8_BIT_FONT_NAME          1
#define PCLXL_FT_16_BIT_FONT_NAME         2
#define PCLXL_FT_PCL5_SELECT_FONT         3

  uint8                       pclxl_font_type;
  uint8                       pclxl_font_name[(64 + 1)];
  uint8                       pclxl_font_name_len;
  PCLXL_SysVal                pclxl_char_size;

  PCLXL_FontID                font_id;
  uint32                      symbol_set;

  PCL5_FONT_SELECTION_CRITERIA pcl5_font_selection_criteria;

  uint8                       ps_font_name[(32 + 1)];
  uint8                       ps_font_name_len;
  PCLXL_SysVal                ps_font_point_size;
  Bool                        ps_font_is_bitmapped;

  uint8                       original_wmode;
  OBJECT*                     wmode_object;
  uint8                       original_vmode;
  OBJECT*                     vmode_object;
} PCLXL_FONT_DETAILS_STRUCT, *PCLXL_FONT_DETAILS;

typedef struct pclxl_char_details_struct
{
  PCLXL_COLOR_DETAILS_STRUCT* char_color;   /* note that this will always point at fill_details.brush_source
                                             * and is here for convenience only
                                             */
  PCLXL_SysVal                char_angle;
  PCLXL_SysVal_XY             char_scale;
  PCLXL_SysVal_XY             char_shear;
  PCLXL_SysVal                char_boldness;
  PCLXL_WritingMode           writing_mode;
  uint8*                      char_sub_modes;
  uint32                      char_sub_modes_len;

  /*
   * Unfortunately it appears that we must remember to preserve the order
   * in which SetCharAngle, SetCharScale and SetCharShear were called
   * and re-assemble the current character CTM in this call order
   *
   * Therefore, in addition to the font_matrix itself we record
   * an enum that records the order in which this was constructed
   *
   * Then when any of SetCharAngle, SetCharScale or SetCharShear are called
   * this font_matrix_construction_order is reset
   * and then used to reconstruct the font_matrix
   */

  OMATRIX                     font_matrix;

#define PCLXL_FMCO_ANGLE_SCALE_SHEAR 0
#define PCLXL_FMCO_ANGLE_SHEAR_SCALE 1
#define PCLXL_FMCO_SCALE_ANGLE_SHEAR 2
#define PCLXL_FMCO_SCALE_SHEAR_ANGLE 3
#define PCLXL_FMCO_SHEAR_ANGLE_SCALE 4
#define PCLXL_FMCO_SHEAR_SCALE_ANGLE 5

  uint8                       font_matrix_construction_order;

  PCLXL_FONT_DETAILS_STRUCT   current_font;
} PCLXL_CHAR_DETAILS_STRUCT, *PCLXL_CHAR_DETAILS;

typedef struct pclxl_media_details_struct
{
  PCLXL_Orientation         orientation;

  /*
   * There are 3 different representations of "media size"
   * It can be an enumeration value (like eA4Paper or eLetterPaper etc.)
   * Or it can be a media (size) name (like "A4" or "Letter" etc.)
   * Or it can represent a "custom" media size
   * which the user specifies as a pair of numbers (integer or real)
   * an a unit size eInch, eMillimeter or eTenthsOfAMillimeter
   * but we then convert to 1/7200ths of an inch
   *
   * But this means that we need to capture a media size
   * specified in any of these variant forms
   *
   * Therefore we record a bit-mapped value that indicates
   * which of the subsequent media size values are actually valid
   */

#define PCLXL_MEDIA_SIZE_NO_VALUE   0x00
#define PCLXL_MEDIA_SIZE_ENUM_VALUE 0x01
#define PCLXL_MEDIA_SIZE_NAME_VALUE 0x02
#define PCLXL_MEDIA_SIZE_XY_VALUE   0x04

  uint8                     media_size_value_type;
  PCLXL_MediaSize           media_size;
  uint8*                    media_size_name;  /* Note that this can be a pointer rather than an array because
                                               * we will always be pointing it at/into a statically declared array of strings
                                               * if this ever changes, then we probably need to change this to an array.
                                               */
  PCLXL_SysVal_XY           media_size_xy;    /* Note that this is always in PCL5 "internal" units which are 1/7200ths of an inch
                                               * The reason for this is that PCLXL can contain PCL5 "passthrough" data
                                               * and if PCLXL uses the same basic coordinates/scaling
                                               * (albeit with a PCLXL session "user" units scaling over the top of this)
                                               * then it makes the passthrough coordinate handling easier
                                               */
  PCLXL_Measure             custom_media_size_units;

  uint32                    printable_area_margins[4];
                                              /*
                                               * PCLXL sets up a page printable area as the initial clip path
                                               * The default is 1/6th == 12/72nds == 1200/7200ths of an inch
                                               * in from the left, right, top and bottom of the physical media
                                               *
                                               * However we additionally support an additional PCLXLDefaultPrintableArea page device key
                                               * whose value is 4 integral values that represent the clip margins
                                               * specified in points (i.e. 72nds of an inch)
                                               */

  PCLXL_MediaSource         media_source;
  PCLXL_MediaDestination    media_destination;
  uint8                     media_type[128];
  uint32                    media_type_len;
  uint32                    leading_edge;  /* We either get told this via the LeadingEdge dictionary key in the currentpagedevice
                                            * Or we re-derive it from the CTM supplied to us *immediately* after a setpagedevice call.
                                            * We use it, at present, *only* to decide how many additional 90-degree rotations to add to pattern angles
                                            */
  Bool                      duplex;
  PCLXL_SimplexPageMode     simplex_page_side;
  PCLXL_DuplexPageSide      duplex_page_side;
  PCLXL_DuplexPageMode      duplex_binding;

} PCLXL_MEDIA_DETAILS_STRUCT, *PCLXL_MEDIA_DETAILS;

typedef struct pclxl_non_gs_state_struct
{
  PCLXL_Measure             measurement_unit;
  PCLXL_UnitsPerMeasure     units_per_measure;

  PCLXL_MEDIA_DETAILS_STRUCT  requested_media_details;  /* Exactly as *requested* by the last BeginPage operator and supplied *to* the setpagedevice Postscript operator */
  PCLXL_MEDIA_DETAILS_STRUCT  current_media_details;    /* What is actually supported by the device and returned by currentpagedevice Postscript operator */
  PCLXL_MEDIA_DETAILS_STRUCT  previous_media_details;   /* The actual media/page set-up in force for the previous page (if any) */

  int32                     page_copies;
  uint32                    page_number;      /* Starts at 1 and is incremented for each EndPage operator encountered */
  uint32                    duplex_page_side_count;
                                              /* Counts the number of calls to "showpage" (plus 1)
                                               * It is incremented at every successful showpage
                                               * including the "showpage" calls needed to effect duplex pages
                                               * And so is potentially more than the page_number
                                               */
  sw_pfin*                  ufst;             /* something to do with plug-in font selection technology
                                               * we need exactly one of these things
                                               * which we will obtain/initialize at the point of first use
                                               */
  PCLXL_SysVal              min_line_width;   /* Needed to ensure that *all* lines are drawn at a minimum of 1 pixel width
                                               * even if these lines are clipped in half as they run along the edge of a clip path
                                               */

  Bool                      setg_required;    /* Has something happened since the last text command that
                                               * means we need to finishaddchardisplay and ensure a setg is done?
                                               */

  Bool                      text_mode_changed; /* Has there been a char_sub_mode or writing_mode command
                                                * which changed anything since the start of the job? Since the start of the page
                                                * would also be ok for current implementation.
                                                * todo This needs review for trunk.
                                                * todo Review for passthrough.
                                                */

} PCLXL_NON_GS_STATE_STRUCT, *PCLXL_NON_GS_STATE;


typedef struct pclxl_object_halftone_method {
  PCLXL_HalftoneMethod text;
  PCLXL_HalftoneMethod raster;
  PCLXL_HalftoneMethod vector;
} PCLXL_OBJECT_HALFTONE_METHOD;

typedef struct pclxl_graphics_state_struct
{
  /*
   * Note that the graphic state can be saved and restored
   * using the PushGS and PopGS operators.
   *
   * Therefore the graphics state is actually a FILO "stack"
   * of PCLXL_GRAPHICS_STATE structures
   */

  PCLXL_GRAPHICS_STATE        parent_graphics_state;

  /*
   * It is currently envisioned that we actually
   * use the Postscript graphics state.
   *
   * But as there is not necessarily a one-to-one mapping
   * between PCLXL graphics state items and Postscript graphics state items
   * so we retain the exact PCLXL graphics state settings
   * using native PCLXL representations
   */

  Bool                        current_path;
  Bool                        current_point;
  PCLXL_SysVal_XY             current_point_xy;

  /*
   * We record a number of different "current" transformation matrices (CTM)
   * including:
   *
   * 1) The physical media/page CTM pre any page orientation transforms
   * 2) The logical page transform pre any in-page modifications
   *    (this is so we can revert to this CTM when SetDefaultCTM is invoked
   * 3) The genuinely "current" CTM
   * 4) The current *character* CTM which is always the current CTM
   *    with the additional character angle added in
   *
   * Note that (4) is always recalculated and stored whenever
   * (3) is reset by any route.
   */

  OMATRIX                     physical_page_ctm;
  OMATRIX                     logical_page_ctm;

  /*
   * As part of setting up the page device
   * and establishing the logical page CTM
   * that supports the portrait/landscape orientation of the page
   * we also capture the page width and height
   * (swapped around for landscape pages)
   * in "user"/"job" units
   */

  PCLXL_SysVal                page_width;
  PCLXL_SysVal                page_height;

  /*
   * As part of recalculating the character/font matrix
   * we also need to collect the cumulative *page* angle (a.k.a. rotation)
   * and cumulative page scale
   * We must also record which of SetPageScale and SetPageRotation was called first
   * because this affects whether or not the page rotation angle
   * affects the font matrix
   * Specifically we count the number of SetPageScale operations
   * and IFF this is greater than zero, then we count the number of
   * SetPageRotation operations and IFF this later count is non-zero
   * then we begin using the ctm-minus-logical-page-ctm version
   * of the font matrix (re-)calculation
   */

  PCLXL_Int32                 page_angle;
  PCLXL_SysVal_XY             page_scale;
  uint32                      set_page_scale_op_count;
  uint32                      set_page_rotation_op_count;

  /*
   * Note that all the above are set up by (calls to)
   * pclxl_set_page_device() and pclxl_set_default_ctm()
   *
   * And are not the province of pclxl_set_default_graphics_state()
   * but *are* saved and restored as part of pushGS and popGS operations
   *
   * But all the remaining graphics state are reset
   * by pclxl_set_default_graphics_state()
   * one of whose principle operations is to reset the current and character CTMs
   * back to the logical page CTM with a character (rotation) angle of zero
   */

  OMATRIX                     current_ctm;
  Bool                        ctm_is_invertible;

  /*
   * We maintain a "stack" of PCLXL graphics states (this structure)
   * beneath the single "global" (i.e. one per call to pclxlexec_())
   * PCLXL context.
   *
   * The functions: pclxl_push_graphics_state() and pclxl_pop_graphics_state()
   * maintain this stack.
   *
   * But as we "push" and "pop" our own PCLXL_GRAPHICS_STATE structure
   * we do not necessarily perform the same Postscript operation.
   *
   * In fact we do one of the (pairs of) operations:
   *
   *  Push          Pop
   *  ----          ---
   *  No-Op         No-Op
   *  No-Op         switch back to logical page CTM
   *  gsave         grestore
   *  save          restore
   *
   * So when we call pclxl_push_gs()
   * which uses pclxl_create_graphics_state() and pclxl_push_graphics_state()
   * to maintain our own "PCLXL" state
   * we also pass an enumeration value to specify which (pair of)
   * Postscript operations to perform.
   *
   * If we successfully perform the Postscript operation
   * (and create a new PCLXL_GRAPHICS_STATE) then we record this operation
   * (together with a Postscript "save" object) into the new PCLXL graphics state
   * and then push this new PCLXL_GRAPHICS_STATE into our stack.
   *
   * Then when we come to "pop" our graphics_state, we use the enumeration
   * to specify what corresponding Postscript operation to perform
   * to revert to the previous Postscript state
   */

#define PCLXL_PS_NO_OP_NO_OP        0
#define PCLXL_PS_NO_OP_SET_CTM      1
#define PCLXL_PS_GSAVE_GRESTORE     2
#define PCLXL_PS_SAVE_RESTORE       3

  uint8                       postscript_op;
  OBJECT                      ps_save_object;

  /*
   * The following PCLXL-specific graphics state items
   * can all be directly mainpulated by the "set..." operators
   * in the PCLXL data stream.
   *
   * In handling these operators we validate the new setting,
   * store it in the appropriate data item below
   * and then do whatever is necessary to the underlying
   * *Postscript* graphics state to actually *implement* this new state
   */

  PCLXL_COLOR_SPACE_DETAILS   color_space_details;
  PCLXL_FILL_DETAILS_STRUCT   fill_details;
  PCLXL_LINE_STYLE_STRUCT     line_style;
  PCLXL_CHAR_DETAILS_STRUCT   char_details;


  PCLXL_ClipMode              clip_mode;
  CLIPRECORD*                 clip_record;

  PCLXL_SysVal_XY             dither_anchor;
  PCLXL_DitherMatrixID        dither_matrix_ID;
  PCLXL_OBJECT_HALFTONE_METHOD halftone_method;
  PCLXL_PaletteID             palette_id;

  PCLXL_ROP3                  ROP3;

  PCLXL_TxMode                source_tx_mode;
  PCLXL_TxMode                paint_tx_mode;

  /* The pattern cache for this gstate. */
  IdHashTable* pattern_cache;

  int32 device_setg_prev_colorType, 
        device_setg_prev_options ;

} PCLXL_GRAPHICS_STATE_STRUCT, *PCLXL_GRAPHICS_STATE_STACK;

#endif


/* Log stripped */

