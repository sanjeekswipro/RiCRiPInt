/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pcl5color.h(EBDSDK_P.1) $
 * $Id: src:pcl5color.h,v 1.33.4.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

#ifndef __PCL5COLOR_H__
#define __PCL5COLOR_H__

#include "pcl5context.h"
#include "printenvironment.h"


#define HACK 1     /* Only C99 likes [] as the struct hack, so [1] it must be */

#define PCL_COLOR_IS_WHITE 1
#define PCL_COLOR_IS_BLACK 2
#define PCL_COLOR_IS_SHADE 3

/* PCL5 color space. */
enum {
  /* The order of the first three MUST not change. */
  PCL5_CS_RGB,
  PCL5_CS_CMY,
  PCL5_CS_sRGB,
  /* Order from here on does not matter. */
  PCL5_CS_GRAY
} ;

/* Palette Types */
enum {
  BLACK_AND_WHITE = 0,
  SIMPLE = 1,
  CID = 2
};

/* Palette Creator */
enum {
  PCL5 = 0,
  HPGL2
};

/* The black and white reference values, or vice versa */
typedef struct ColorRange {
  PCL5Real     lo ;
  PCL5Real     hi ;
} ColorRange ;

typedef struct ColorState {
  sll_list_t palette_store ;   /* single linked list of ColorPalettes */
  sll_list_t palette_stack ;   /* single linked list of ColorPalettes */
  ColorRange color_range[3] ;  /* black and white refs for 3 color components */
  PCL5Real component_colors[3] ;  /* colors to set for the 3 color components */
} ColorState ;


/* Be sure to update default_color_palette() if you change this structure.
 * N.B. The pen_widths can be allocated if required, (either automatically
 *      for HPGL2, or if perhaps only if pen widths differ for different
 *      colors).
 */
typedef struct ColorPalette {
  /** List links - more efficient if first. */
  sll_link_t     sll;
  int32          ID ;
  uint8          type ;
  uint8          creator ;
  uint8          pixel_encoding_mode ;
  uint8          bits_per_index ;
  uint8          colorspace ;
  HPGL2Real      *pen_widths ;
  int32          num_values ;
  uint8          values[HACK] ;
} ColorPalette ;

/**
 * Evaluates to the number of components in the specified color space, which
 * must be one of PCL5_CS_RGB, PCL5_CS_CMY, etc.
 */
#define COMPONENTS_IN_COLORSPACE(_space) (((_space) == PCL5_CS_GRAY) ? 1 : 3)

/**
 * Evaluates to the number of values representable by the passed number of bits.
 */
#define VALUES_FOR_BITS(_bits) (1 << (_bits))

/**
 * Evaluates to the number of entries in the passed palette.
 */
#define ENTRIES_IN_PALETTE(_pal) VALUES_FOR_BITS((_pal)->bits_per_index)

/**
 * The maximum number of colorants expected in a PCL color.
 */
#define PCL_MAX_COLORANTS 4

/* calculate number of pens for table with given number of bits per index */
#define PENWIDTHTABLE_SIZE(_bpi) (((size_t)1 <<_bpi) * sizeof(HPGL2Real))


/* Be sure to update default_foreground_color if you change this structure. */
typedef struct ForegroundColor {
  uint8 colorspace ;

  /* The type of palette the color was chosen from, e.g. CID, SIMPLE, etc. */
  uint8 type ;

  USERVALUE values[3] ;
} ForegroundColor ;


/* Be sure to update default_color_info if you change this structure. */
typedef struct ColorInfo {
  int32           palette_control_id ;
  ForegroundColor foreground_color ;
  ColorPalette    *saved_palette ;
} ColorInfo ;


/**
 * Initialise the ColorInfo element of the MPE.
 */
void default_color_info(ColorInfo *self) ;

/**
 * Initialise the Palette Control Id.
 */
void default_palette_control_id(int32 *self) ;

/**
 * Get hold of the ColorState.
 */
ColorState* get_color_state(PCL5Context *pcl5_ctxt) ;

/**
 * Get hold of the ColorInfo.
 */
ColorInfo* get_color_info(PCL5Context *pcl5_ctxt) ;

/**
 * Get hold of the active palette.
 */
ColorPalette *get_active_palette(PCL5Context *pcl5_ctxt) ;

/**
 * Create an /Indexed colorspace from the active palette.
 */
Bool create_indexed_space_from_palette(PCL5Context *pcl5_ctxt, OBJECT *indexedSpaceObj) ;

/**
 * Reset palette store and stack, and create default B&W palette.
 */
Bool color_state_init(PCL5Context *pcl5_ctxt) ;

/**
 * Free palette store and palette stack.
 */
void color_state_finish(PCL5Context *pcl5_ctxt) ;

/**
 * Free and reset the palette store and stack, and create default B&W palette.
 */
Bool color_state_reset(PCL5Context *pcl5_ctxt) ;

/**
 * Squirrel away a copy of the active palette when saving MPE.
 */
void save_color_info(PCL5Context *pcl5_ctxt, ColorInfo *to, ColorInfo *from) ;

/**
 * Replace the active palette with the squirrelled away one when restoring MPE.
 */
void restore_color_info(PCL5Context *pcl5_ctxt, ColorInfo* to, ColorInfo *from) ;

/**
 * Get the color mode, e.g. monochrome, RGB, and set it as the default.
 */
Bool get_default_color_mode(PCL5Context *pcl5_ctxt) ;

/**
 * Set up the PS colorspace from the foreground color. Note that setting a new
 * colorspace is a no-op if the new space is the same as the old; thus it is not
 * expensive call this repeatedly.
 */
Bool set_ps_colorspace(PCL5Context *pcl5_ctxt) ;

/**
 * Set up the PS color values from the foreground color.
 */
Bool set_ps_color(PCL5Context *pcl5_ctxt) ;

/**
 * Set the indexed color in the current palette in the core. This will first set
 * the current colorspace in the core to that of the palette. This will not
 * affect the current PCL color state.
 */
Bool set_ps_color_from_palette_index(PCL5Context *pcl5_ctxt, uint32 index);

/**
 * Return the indexed entry from the active palette.
 *
 * \param values Pointer to an array (which should be PCL_MAX_COLORANTS long)
 *        where the color values will be stored.
 * \return The number of components stored in 'values'.
 */
int32 get_color_from_palette_index(PCL5Context *pcl5_ctxt,
                                   uint32 index,
                                   uint8* values);

/**
 * Classify the specified as either white, black or a shade.
 *
 * \return One of: PCL_COLOR_IS_WHITE, PCL_COLOR_IS_BLACK, PCL_COLOR_IS_SHADE.
 */
uint32 classify_palette_entry(PCL5Context *pcl5_ctxt, uint32 index);

/**
 * Set the current color settings in the core. This is the same calling:
 *
 * set_ps_colospace(pcl5_ctxt)
 * set_ps_color(pcl5_ctxt)
 * setPclForegroundSource(PCL_FOREGROUND_IN_DL_COLOR)
 */
Bool set_current_color(PCL5Context *pcl5_ctxt);

/**
 * Returns true if the current foreground color is white.
 */
Bool foreground_is_white(PCL5Context *pcl5_ctxt);

/**
 * Set the core color to the shade of DeviceGray;  this will not affect the
 * current PCL color state.
 */
Bool set_shade(USERVALUE shade);

/**
 * Set the core color to white; this will not affect the current PCL color
 * state.
 */
Bool set_white(void);

/**
 * Set the core color to black; this will not affect the current PCL color
 * state.
 */
Bool set_black(void);

Bool pcl5_color_init(PCL5_RIP_LifeTime_Context *pcl5_rip_context) ;
void pcl5_color_finish(PCL5_RIP_LifeTime_Context *pcl5_rip_context) ;

/** Force the creation of a completely new HPGL palette. The palette will
 *  have a pen width table installed.
 */
Bool create_hpgl2_palette(PCL5Context *pcl5_ctxt, uint8 bits_per_index);

/**
 * Allocate a bare palette.
 */
ColorPalette* alloc_palette(PCL5Context *pcl5_ctxt,
                            int32 colorspace,
                            uint8 bits_per_index);

/**
 *  Copy color component data and pen width data from source to target until
 *  either all source copied or targer is full.
 *  Caller must ensure that all required memory is allocated prior to
 *  calling this function.
 */
void copy_palette_data(ColorPalette *target,
                       ColorPalette *source);

/**
 * Destroys the currently active palette.
 */
void destroy_active_palette(PCL5Context *pcl5_ctxt);

/**
 * Copy the active palette.
 */
ColorPalette* copy_active_palette(PCL5Context *pcl5_ctxt, int32 id) ;

Bool create_simple_palette(PCL5Context *pcl5_ctxt, int32 palette_colorspace) ;

/**
 * Return array of uint8s, giving the default color component assignments
 * for color palettes. The data is pixel interleaved.
 */
uint8* select_palette_default_values(uint8 creator,
                                    uint8 colorspace,
                                    uint8 bits_per_index,
                                    uint8 type);

/**
 *  Set gstate colorspace from PCL colorspace.
 */
Bool set_ps_colorspace_internal(uint32 id);

/**
 *  Set gstate color from PCL color.
 */
Bool set_ps_color_internal(USERVALUE *values);

/**
 * Reset the last-used PCL & HPGL color values to out-of-range values.
 */
void reset_last_pcl5_and_hpgl_colors(void);

/* ============================================================================
 * Operator callbacks are below here.
 * ============================================================================
 */

Bool pcl5op_ampersand_b_M(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_ampersand_p_C(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_ampersand_p_I(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_ampersand_p_S(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_i_W(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_l_W(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_m_W(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_o_W(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_p_P(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_r_U(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_t_I(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_t_J(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_v_A(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_v_B(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_v_C(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_v_I(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_v_S(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_star_v_W(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

/* ============================================================================
* Log stripped */
#endif
