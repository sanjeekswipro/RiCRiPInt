/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pcl5color.c(EBDSDK_P.1) $
 * $Id: src:pcl5color.c,v 1.72.2.1.1.1 2013/12/19 11:25:02 anon Exp $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the PCL5 "Color" category.
 *
 * Simple Color                    ESC * r # U
 * Configure Image Data            ESC * v # W[data]
 * Color Component One             ESC * v # A
 * Color Component Two             ESC * v # B
 * Color Component Three           ESC * v # C
 * Assign Color Index              ESC * v # I
 * Push/Pop Palette                ESC * p # P
 * Select Palette                  ESC & p # S
 * Palette Control ID              ESC & p # I
 * Palette Control                 ESC & p # C
 * Foreground Color                ESC * v # S
 * Render Algorithm                ESC * t # J
 * Download Dither Matrix          ESC * m # W [data]
 * Color Lookup Tables             ESC * l # W [data]
 * Gamma Correction                ESC * t # I
 * Viewing Illuminant              ESC * i # W [data]
 * Driver Config (Color Treatment) ESC * o # W [data]
 * Monochrome Print Mode           ESC & b # M
 */

#include "core.h"
#include "pcl5color.h"

#include "objects.h"
#include "display.h"
#include "fileio.h"
#include "monitor.h"
#include "namedef_.h"
#include "graphics.h"
#include "gschead.h"
#include "gsc_icc.h"
#include "gschcms.h"
#include "gstate.h"
#include "hqmemcpy.h"
#include "hqmemset.h"
#include "miscops.h"
#include "swcopyf.h"
#include "swerrors.h"
#include "pcl5fonts.h"
#include "hpgl2misc.h"
#include "hpgl2linefill.h"

#include "pcl5context_private.h"
#include "printenvironment_private.h"
#include "pcl5scan.h"
#include "macros.h"
#include "pcl5raster.h"

/**
 * Evaluates to the size of a ColorPalette structure containing the specified
 * number uint8's.
 */
#define COLORPALETTE_SIZE(_n) \
  (offsetof(ColorPalette,values[0])+(_n)*sizeof(uint8))

/* Default values for Black and White (Simple) Color Palette */
static uint8 default_bw_palette_vals[2] =
{ 255, 0} ;                   /* white, black */

/* Default values for 1 and 2 bits per index CID Color Palettes */
static uint8 default_1_bit_rgb_palette_vals[6] =
{ 255, 255, 255,              /* white */
  0, 0, 0                     /* black */
} ;

static uint8 default_1_bit_cmy_palette_vals[6] =
{ 0, 0, 0,                    /* white */
  255, 255, 255               /* black */
} ;

static uint8 default_2_bit_rgb_cmy_palette_vals[12] =
{ 0, 0, 0,                    /* black or white */
  255, 0, 0,                  /* red or cyan */
  0, 255, 0,                  /* green or magenta */
  255, 255, 255               /* white or black */
} ;

/* Default values for Simple Color Palettes, and for first 8 entries
 * for 3 or more bits per index CID Color Palettes.
 */
static uint8 default_3_bit_rgb_cmy_palette_vals[24] =
{ 0, 0, 0,                    /* black or white */
  255, 0, 0,                  /* red or cyan */
  0, 255, 0,                  /* green or magenta */
  255, 255, 0,                /* yellow or blue  */
  0, 0, 255,                  /* blue or yellow */
  255, 0, 255,                /* magenta or green */
  0, 255, 255,                /* cyan or red */
  255, 255, 255               /* white or black */
} ;

/* HPGL palettes are RGB if create by IN. Otherwise, the color model is
 * inherited from the PCL world.
 */

/* Defaults for the 2 Pen HPGL palette */
static uint8 default_1_bit_hpgl2_rgb_palette_vals[6] =
{
  255,255,255,
  0,0,0
} ;

/* Defaults for the 4 Pen HPGL palette */
static uint8 default_2_bit_hpgl2_rgb_palette_vals[12] =
{
  255,255,255,
  0,0,0,
  255,0,0,
  0,255,0
} ;

/* Defaults for the 8+ Pen HPGL palette */
static uint8 default_3_bit_hpgl2_rgb_palette_vals[24] =
{
  255,255,255,
  0,0,0,
  255,0,0,
  0,255,0,
  255,255,0,
  0,0,255,
  255,0,255,
  0,255,255,
} ;


uint8* select_palette_default_values(uint8 creator,
                                    uint8 colorspace,
                                    uint8 bits_per_index,
                                    uint8 type)
{
  if ( creator == HPGL2) {
    HQASSERT(colorspace != PCL5_CS_CMY, "Invalid colorspace");
    HQASSERT(type == CID, "Invalid palette type");

    switch ( bits_per_index ) {
    case 1:
      return default_1_bit_hpgl2_rgb_palette_vals;

    case 2:
      return default_2_bit_hpgl2_rgb_palette_vals;

    default :
      return default_3_bit_hpgl2_rgb_palette_vals;
    }
  }
  else {
    HQASSERT( creator == PCL5, "Invalid palette creator");

    if ( type == BLACK_AND_WHITE ) {
      HQASSERT( bits_per_index == 1, "Invalid bits_per_index");
      return default_bw_palette_vals;
    }
    else {
      /* (sRGB), RGB and CMY */
      switch ( bits_per_index ) {
      case 1:
        if ( colorspace == PCL5_CS_CMY )
          return default_1_bit_cmy_palette_vals;
        else
          return default_1_bit_rgb_palette_vals;

      case 2:
        return default_2_bit_rgb_cmy_palette_vals;

      default:
        return default_3_bit_rgb_cmy_palette_vals;
      }
    }
  }
}

Bool pcl5_color_init(PCL5_RIP_LifeTime_Context *pcl5_rip_context)
{
  UNUSED_PARAM(PCL5_RIP_LifeTime_Context*, pcl5_rip_context) ;
  return TRUE ;
}

void pcl5_color_finish(PCL5_RIP_LifeTime_Context *pcl5_rip_context)
{
  UNUSED_PARAM(PCL5_RIP_LifeTime_Context*, pcl5_rip_context) ;
}

/* ========================================================================= */
ColorState *get_color_state(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *print_state ;
  ColorState *color_state ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  color_state = &(print_state->color_state) ;
  return color_state ;
}

ColorInfo* get_color_info(PCL5Context *pcl5_ctxt)
{
  PCL5PrintEnvironment *mpe ;
  ColorInfo *color_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  mpe = get_current_mpe(pcl5_ctxt) ;
  HQASSERT(mpe != NULL, "mpe is NULL") ;

  color_info = &(mpe->color_info) ;
  return color_info ;
}


/* Setup the default values for foreground color, (white). */
void default_foreground_color(ForegroundColor *self)
{
  self->colorspace = PCL5_CS_GRAY ;
  self->type = BLACK_AND_WHITE ;
  self->values[0] = 0 ;
  self->values[1] = 0 ;
  self->values[2] = 0 ;
}

/* Set the passed ColorInfo to default values */
void default_color_info(ColorInfo *self)
{
  self->palette_control_id = 0 ;
  default_foreground_color(&self->foreground_color) ;
  HQASSERT(self->saved_palette == NULL, "Unexpected saved palette") ;
  self->saved_palette = NULL ;
}

ColorPalette* get_active_palette(PCL5Context *pcl5_ctxt)
{
  ColorState *color_state ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  color_state = get_color_state(pcl5_ctxt) ;
  HQASSERT(color_state != NULL, "color_state is NULL") ;

  return SLL_GET_HEAD(&(color_state->palette_store), ColorPalette, sll) ;
}


/**
 * Initialise a Black and White ColorPalette
 */
void default_color_palette(ColorPalette* self, int32 id)
{
  self->ID = id ;
  self->type = BLACK_AND_WHITE ;  /** \todo or maybe just SIMPLE? */
  self->creator = PCL5 ;
  self->pixel_encoding_mode = PCL5_MONOCHROME ;
  self->bits_per_index = 1 ;
  self->colorspace = PCL5_CS_GRAY;
  self->num_values = 2 ;
  HqMemCpy(self->values, default_bw_palette_vals, 2) ;
}

/**
 * Initialise a Simple RGB ColorPalette
 */
void set_simple_rgb_palette(ColorPalette* self, int32 id)
{
  self->ID = id ;
  self->type = SIMPLE ;
  self->creator = PCL5 ;
  self->pixel_encoding_mode = PCL5_INDEXED_BY_PLANE ;
  self->bits_per_index = 3 ;
  self->colorspace = PCL5_CS_RGB ;
  self->num_values = 24 ;
  HqMemCpy(self->values, default_3_bit_rgb_cmy_palette_vals, 24) ;
}

/**
 * Initialise a Simple CMY ColorPalette
 */
void set_simple_cmy_palette(ColorPalette* self, int32 id)
{
  self->ID = id ;
  self->type = SIMPLE ;
  self->creator = PCL5 ;
  self->pixel_encoding_mode = PCL5_INDEXED_BY_PLANE ;
  self->bits_per_index = 3 ;
  self->colorspace = PCL5_CS_CMY ;
  self->num_values = 24 ;
  HqMemCpy(self->values, default_3_bit_rgb_cmy_palette_vals, 24) ;
}


/* Free the referenced palette, (which must not be NULL) */
void free_palette(ColorPalette **pp_palette)
{
  ColorPalette *p_palette ;

  HQASSERT(pp_palette != NULL, "pp_palette is NULL") ;
  p_palette = *pp_palette ;

  HQASSERT(p_palette != NULL, "p_palette is NULL") ;

  if ( p_palette->pen_widths != NULL ) {
    mm_free(mm_pcl_pool,
    p_palette->pen_widths,
    PENWIDTHTABLE_SIZE(p_palette->bits_per_index));
    p_palette->pen_widths = NULL;
  }

  mm_free(mm_pcl_pool, p_palette, COLORPALETTE_SIZE(p_palette->num_values)) ;
  *pp_palette = NULL ;
}


/* Remove and free the active palette, if it exists */
void destroy_active_palette(PCL5Context *pcl5_ctxt)
{
  ColorState *color_state ;
  ColorPalette *palette ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  color_state = get_color_state(pcl5_ctxt) ;
  HQASSERT(color_state != NULL, "color_state is NULL") ;

  if (! SLL_LIST_IS_EMPTY(&(color_state->palette_store)))
  {
    palette = get_active_palette(pcl5_ctxt) ;
    SLL_REMOVE_HEAD(&(color_state->palette_store));
    free_palette(&palette) ;

    if (SLL_LIST_IS_EMPTY(&color_state->palette_store))
      SLL_RESET_LIST(&(color_state->palette_store)) ;
  }
}

/* Calculate the length of a values array */
static
int32 calculate_num_values(int32 colorspace, uint8 bits_per_index)
{
  int32 num_components, entries, length ;

  num_components = COMPONENTS_IN_COLORSPACE(colorspace);
  entries = VALUES_FOR_BITS(bits_per_index) ;
  length = entries * num_components ;

  return length ;
}

/* Allocate a new palette, store the length of the values array, and reset the link */
ColorPalette* alloc_palette(PCL5Context *pcl5_ctxt, int32 colorspace, uint8 bits_per_index)
{
  ColorPalette *palette = NULL ;
  int32 length ;

  UNUSED_PARAM(PCL5Context *,pcl5_ctxt);

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(colorspace == PCL5_CS_RGB || colorspace == PCL5_CS_CMY ||
           colorspace == PCL5_CS_GRAY,
           "Invalid or unsupported palette colorspace requested") ;

  length = calculate_num_values(colorspace, bits_per_index) ;
  palette = mm_alloc(mm_pcl_pool, COLORPALETTE_SIZE(length), MM_ALLOC_CLASS_PCL_CONTEXT);

  if (palette == NULL)
    (void) error_handler(VMERROR) ;
  else {
    palette->num_values = length ;
    SLL_RESET_LINK(palette, sll) ;

    palette->pen_widths = alloc_pen_width_table(bits_per_index);
    if ( palette->pen_widths == NULL ) {
      free_palette(&palette) ;
      palette = NULL ;
    }
  }

  return palette ;
}

/* Returns TRUE if the two palettes are the same size.
 * Size of pen width table depends on bits_per_index.
 */
static
Bool palette_sizes_match(ColorPalette *palette_1, ColorPalette *palette_2)
{
  return (palette_1->num_values == palette_2->num_values)
   && hpgl2_pen_width_tables_equal_size(palette_1, palette_2) ;
}

/* The caller needs to ensure that the color palettes are same size.
 * Cannot rely on the state of target being set up correctly.
 */
void copy_full_palette(ColorPalette *target, ColorPalette *source)
{
  target->ID = source->ID;
  target->type = source->type;
  target->creator = source->creator;
  target->pixel_encoding_mode = source->pixel_encoding_mode;
  target->colorspace = source->colorspace;
  target->num_values = source->num_values;
  target->bits_per_index = source->bits_per_index;

  copy_palette_data(target, source);
}

void copy_palette_data(ColorPalette *target,
                       ColorPalette *source)
{
  int32 values, bits_per_index;

  values = source->num_values < target->num_values ?
            source->num_values : target->num_values;

  bits_per_index = source->bits_per_index < target->bits_per_index ?
                     source->bits_per_index : target->bits_per_index;

  HqMemCpy(target->values, source->values, values) ;
  hpgl2_copy_pen_width_table(target, source);
}

/* Make a copy of the active palette and give it the specified id */
ColorPalette* copy_active_palette(PCL5Context *pcl5_ctxt, int32 id)
{
  ColorPalette *palette, *new_palette = NULL ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  palette = get_active_palette(pcl5_ctxt) ;
  HQASSERT(palette != NULL, "active palette is NULL") ;

  new_palette = alloc_palette(pcl5_ctxt, palette->colorspace, palette->bits_per_index) ;

  /* Copy the contents and reset the link */
  if (new_palette != NULL) {
    copy_full_palette(new_palette, palette) ;
    new_palette->ID = id ;
    SLL_RESET_LINK(new_palette, sll) ;
  }

  return new_palette ;
}


/**
 * Create a Simple ColorPalette (RGB, CMY or the default B&W palette),
 * and make it the active palette, replacing any existing active palette.
 */
Bool create_simple_palette(PCL5Context *pcl5_ctxt, int32 palette_colorspace)
{
  ColorState *color_state ;
  ColorPalette *new_palette = NULL, *active_palette ;
  uint8 bits_per_index ;
  int32 length_needed, id = 0 ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(palette_colorspace == PCL5_CS_GRAY || palette_colorspace == PCL5_CS_RGB ||
           palette_colorspace == PCL5_CS_CMY,
           "Invalid palette colorspace requested for simple palette") ;

  color_state = get_color_state(pcl5_ctxt) ;
  HQASSERT(color_state != NULL, "color_state is NULL") ;
  active_palette = get_active_palette(pcl5_ctxt) ;

#if defined (DEBUG_BUILD)
  /* There should always be an active palette unless just setting up the
   * default black and white palette.
   */
  if (palette_colorspace != PCL5_CS_GRAY)
    HQASSERT(active_palette != NULL, "active palette is NULL") ;
#endif

  if (active_palette != NULL)
    id = active_palette->ID ;

  bits_per_index = COMPONENTS_IN_COLORSPACE(palette_colorspace);
  length_needed = calculate_num_values(palette_colorspace, bits_per_index) ;

  /** \todo Will need to change this if HPGL2 pen_widths end up being
   *  allocated lazily.
   */
  if (active_palette == NULL ||
      bits_per_index != active_palette->bits_per_index ||
      length_needed != active_palette->num_values) {

    destroy_active_palette(pcl5_ctxt) ;

    /* Create the new palette */
    new_palette = alloc_palette(pcl5_ctxt, palette_colorspace, bits_per_index) ;

    if (new_palette == NULL)
      return FALSE ;

    SLL_ADD_HEAD(&(color_state->palette_store), new_palette, sll);
  }
  else
    new_palette = active_palette ;

  /* Initialise the simple palette */
  switch (palette_colorspace) {
  case PCL5_CS_GRAY:
    default_color_palette(new_palette, id) ;
    break ;

  case PCL5_CS_RGB:
    set_simple_rgb_palette(new_palette, id) ;
    break ;

  case PCL5_CS_CMY:
    set_simple_cmy_palette(new_palette, id) ;
    break ;
  }

  /** \todo Possibly allocate this lazily if needed for HPGL2 */
  hpgl2_set_palette_pen_width(new_palette,
                              HPGL2_ALL_PENS,
                              hpgl2_get_default_pen_width(
                                HPGL2_PEN_WIDTH_METRIC));

  return TRUE ;
}

/**
 * Create a CID (programmable) palette, and make it the active palette,
 * replacing any existing active palette.
 * N.B. This currently handles short form command only.
 */
static
Bool create_cid_palette(PCL5Context *pcl5_ctxt, uint8 colorspace,
                        uint8 pixel_encoding_mode, uint8 bits_per_index)
{
  ColorState *color_state ;
  ColorPalette *new_palette = NULL, *active_palette ;
  int32 length_needed, id ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(colorspace == PCL5_CS_RGB || colorspace == PCL5_CS_CMY ||
           colorspace == PCL5_CS_sRGB,
           "Invalid colorspace requested for CID palette") ;
  HQASSERT(pixel_encoding_mode != PCL5_MONOCHROME,
           "Unexpected pixel encoding mode") ;
  HQASSERT(bits_per_index >= 1 && bits_per_index <= 8,
           "Invalid bits_per_index") ;

  color_state = get_color_state(pcl5_ctxt) ;
  HQASSERT(color_state != NULL, "color_state is NULL") ;

  active_palette = get_active_palette(pcl5_ctxt) ;
  HQASSERT(active_palette != NULL, "active palette is NULL") ;

  /* N.B. sRGB appears to be treated as DeviceRGB for all the cases where a
   * palette is actually created.  (No palette is created for sRGB direct by
   * plane pixel encoding).
   */
  if (colorspace == PCL5_CS_sRGB)
    colorspace = PCL5_CS_RGB ;

  id = active_palette->ID ;

  length_needed = calculate_num_values(colorspace, bits_per_index) ;

  /** \todo Will need to change this if HPGL2 pen_widths end up being
   *  allocated lazily.
   */
  if (bits_per_index != active_palette->bits_per_index ||
      length_needed != active_palette->num_values) {

    destroy_active_palette(pcl5_ctxt) ;

    /* Create the new palette, (all colorspaces have 3 components) */
    new_palette = alloc_palette(pcl5_ctxt, colorspace, bits_per_index) ;

    if (new_palette == NULL)
      return FALSE ;

    SLL_ADD_HEAD(&(color_state->palette_store), new_palette, sll);
  }
  else
    new_palette = active_palette ;

  /* Initialise the palette */
  new_palette->ID = id ;
  new_palette->type = CID ;
  new_palette->creator = PCL5 ;
  new_palette->pixel_encoding_mode = pixel_encoding_mode ;
  new_palette->bits_per_index = bits_per_index ;
  new_palette->colorspace = colorspace ;

  switch (colorspace) {
  case PCL5_CS_RGB:
    if (bits_per_index == 1)
      HqMemCpy(new_palette->values, default_1_bit_rgb_palette_vals, 6) ;
    else if (bits_per_index == 2)
      HqMemCpy(new_palette->values, default_2_bit_rgb_cmy_palette_vals, 12) ;
    else {
      HqMemZero((uint8 *)new_palette->values, new_palette->num_values) ;
      HqMemCpy(new_palette->values, default_3_bit_rgb_cmy_palette_vals, 24) ;
    }
    break ;

  case PCL5_CS_CMY:
    if (bits_per_index == 1)
      HqMemCpy(new_palette->values, default_1_bit_cmy_palette_vals, 6) ;
    else if (bits_per_index == 2)
      HqMemCpy(new_palette->values, default_2_bit_rgb_cmy_palette_vals, 12) ;
    else {
      HqMemSet8((uint8 *)new_palette->values, 255, new_palette->num_values) ;
      HqMemCpy(new_palette->values, default_3_bit_rgb_cmy_palette_vals, 24) ;
    }
    break ;
  }

  /** \todo Possibly allocate this lazily if needed for HPGL2 */
  hpgl2_set_palette_pen_width(new_palette,
                              HPGL2_ALL_PENS,
                              hpgl2_get_default_pen_width(
                                HPGL2_PEN_WIDTH_METRIC));

  return TRUE ;
}


/* Create an indexed colorspace from the active palette.
 * E.g. for an RGB palette:
 * [/Indexed /DeviceRGB hival (string of values in range 0 to 255)]
 */
/** \todo May need to support HPGL2 palettes with 32768 entries.
 *  This will not work at the moment.
 */
Bool create_indexed_space_from_palette(PCL5Context *pcl5_ctxt, OBJECT *indexedSpaceObj)
{
  ColorPalette *palette ;
  int32 entries;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  palette = get_active_palette(pcl5_ctxt) ;
  HQASSERT(palette != NULL, "active palette is NULL") ;

  entries = ENTRIES_IN_PALETTE(palette) ;

  if (! ps_array( indexedSpaceObj, 4 ))
    return FALSE ;

  object_store_name(&oArray(*indexedSpaceObj)[0], NAME_Indexed, LITERAL) ;

  switch (palette->colorspace) {
  case PCL5_CS_GRAY:
    object_store_name(&oArray(*indexedSpaceObj)[1], NAME_DeviceGray, LITERAL) ;
    break ;

  case PCL5_CS_RGB:
    object_store_name(&oArray(*indexedSpaceObj)[1], NAME_DeviceRGB, LITERAL) ;
    break ;

  case PCL5_CS_CMY:
    object_store_name(&oArray(*indexedSpaceObj)[1], NAME_DeviceCMY, LITERAL) ;
    break ;

  default:
    /* At least for now we are not doing CIELab or Luminance-Chrominance spaces */
    HQFAIL("Unexpected palette colorspace") ;
    break ;
  }

  object_store_integer(&oArray(*indexedSpaceObj)[2], (entries - 1));

  /** \todo It would also be nice to have this object point here rather than
   *  copying the data.
   */
  if (! ps_string(&oArray(*indexedSpaceObj)[3], &(palette->values[0]), palette->num_values))
    return FALSE ;

  return TRUE ;
}


/* Set the black and white reference values from bits per component.
 * N.B.   Although these are set up by the CID command, they do not appear
 *        to belong to the palette.
 * N.N.B. The Color Tech Ref seems to hint that the range for sRGB should
 *        always be 0 - 255.  Testing, however, does not bear this out.
 */
void set_color_range_from_bits_per_component(PCL5Context *pcl5_ctxt,
                                             uint8 bits_for_component_one,
                                             uint8 bits_for_component_two,
                                             uint8 bits_for_component_three)
{
  ColorState *color_state ;
  ColorRange *range ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL" ) ;
  color_state = get_color_state(pcl5_ctxt) ;
  HQASSERT(color_state != NULL, "color_state is NULL") ;

  range = &(color_state->color_range[0]) ;

  /* Limit final high vals to 32767.
   * A value of zero bits per component means set
   * to the default for the colorspace, i.e. 0 - 255,
   * for all the ones we handle.
   */
  if (bits_for_component_one > 15)
    bits_for_component_one = 15 ;
  else if (bits_for_component_one == 0)
    bits_for_component_one = 8 ;

  if (bits_for_component_two > 15)
    bits_for_component_two = 15 ;
  else if (bits_for_component_two == 0)
    bits_for_component_two = 8 ;

  if (bits_for_component_three > 15)
    bits_for_component_three = 15 ;
  else if (bits_for_component_three == 0)
    bits_for_component_three = 8 ;

  range[0].lo = range[1].lo = range[2].lo = 0 ;
  range[0].hi = (PCL5Real) VALUES_FOR_BITS(bits_for_component_one) - 1;
  range[1].hi = (PCL5Real) VALUES_FOR_BITS(bits_for_component_two) - 1;
  range[2].hi = (PCL5Real) VALUES_FOR_BITS(bits_for_component_three) - 1;
}

void default_hpgl2_palette_values(ColorPalette *palette)
{
  HQASSERT(palette != NULL, "Null palette");
  HQASSERT(palette->creator == HPGL2, "Bad creator value");
  HQASSERT(palette->type == CID, "Bad palette type");

  HqMemSet8((uint8 *)palette->values, 255, palette->num_values);
  HqMemCpy(palette->values, default_3_bit_hpgl2_rgb_palette_vals, 24) ;
}

Bool create_hpgl2_palette(PCL5Context *pcl5_ctxt, uint8 bits_per_index)
{
  ColorState *color_state ;
  ColorPalette *new_palette = NULL, *active_palette ;
  int32 length_needed, id ;
  HPGL2LineFillInfo *line_info = NULL;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  color_state = get_color_state(pcl5_ctxt) ;
  HQASSERT(color_state != NULL, "color_state is NULL") ;

  active_palette = get_active_palette(pcl5_ctxt) ;
  HQASSERT(active_palette != NULL, "active palette is NULL") ;

  line_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  HQASSERT(line_info != NULL, "No line_fill info");


  HQASSERT( pcl5_ctxt->pcl5c_enabled,
          "Creating HPGL palette but PCL5c unsupported");

  id = active_palette->ID ;

  length_needed = calculate_num_values(PCL5_CS_RGB, bits_per_index) ;

  /** \todo Will need to change this if HPGL2 pen_widths end up being
   *  allocated lazily.
   */
  if (bits_per_index != active_palette->bits_per_index ||
      length_needed != active_palette->num_values) {

    destroy_active_palette(pcl5_ctxt) ;

    new_palette = alloc_palette(pcl5_ctxt, PCL5_CS_RGB, bits_per_index ) ;

    if (new_palette == NULL)
      return FALSE ;

    SLL_ADD_HEAD(&(color_state->palette_store), new_palette, sll);
  }
  else
    new_palette = active_palette ;

  /* Initialise the palette */
  new_palette->ID = id ;
  new_palette->type = CID ;
  new_palette->creator = HPGL2 ;
  new_palette->pixel_encoding_mode = PCL5_INDEXED_BY_PLANE ;
  new_palette->bits_per_index = bits_per_index ;
  new_palette->colorspace = PCL5_CS_RGB ;
  hpgl2_default_pen_values(new_palette, HPGL2_ALL_PENS);
  hpgl2_set_palette_pen_width(new_palette,
                              HPGL2_ALL_PENS,
                              hpgl2_get_default_pen_width(
                                line_info->pen_width_selection_mode));

  /* IN is not supposed to alter the selected pen, but if the pen is out
   * of range, we need to do so. */
  hpgl2_set_current_pen(pcl5_ctxt, line_info->selected_pen, FALSE);
  return TRUE ;
}

/* ============================================================================
 * Palette Store, Palette Stack, and Palette Control functions
 * ============================================================================
 */
static
int32 get_palette_control_id(PCL5Context *pcl5_ctxt)
{
  ColorInfo *color_info ;
  int32 id ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  color_info = get_color_info(pcl5_ctxt) ;
  HQASSERT(color_info != NULL, "color_info is NULL") ;

  id = color_info->palette_control_id ;

  return id ;
}


void set_palette_control_id(PCL5Context *pcl5_ctxt, int32 id)
{
  ColorInfo *color_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(0 <= id && id <= 32767, "Unexpected palette control id") ;

  color_info = get_color_info(pcl5_ctxt) ;
  HQASSERT(color_info != NULL, "color_info is NULL") ;

  color_info->palette_control_id = id ;
}


void free_palette_store(PCL5Context *pcl5_ctxt)
{
  ColorState *color_state ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL" ) ;
  color_state = get_color_state(pcl5_ctxt) ;
  HQASSERT( color_state != NULL, "color_state is NULL");

  while (! SLL_LIST_IS_EMPTY(&(color_state->palette_store))) {
    destroy_active_palette(pcl5_ctxt) ;
  }

  SLL_RESET_LIST(&(color_state->palette_store)) ;
}


/* Initialise the palette store to just contain a single palette,
 * namely, the default B&W palette.
 * The palette control ID is also set to zero.
 */
static
Bool init_palette_store(PCL5Context *pcl5_ctxt)
{
  ColorState *color_state ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL" ) ;
  color_state = get_color_state(pcl5_ctxt) ;
  HQASSERT( color_state != NULL, "color_state is NULL");

  /* Initialise the palette store single linked list to be empty */
  SLL_RESET_LIST(&(color_state->palette_store)) ;

  /* Add the default Black and White ColorPalette to the store */
  if (! create_simple_palette(pcl5_ctxt, PCL5_CS_GRAY))
    return FALSE ;

  return TRUE ;
}

void free_palette_stack(PCL5Context *pcl5_ctxt)
{
  ColorState *color_state ;
  ColorPalette *palette ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL" ) ;
  color_state = get_color_state(pcl5_ctxt) ;
  HQASSERT( color_state != NULL, "color_state is NULL");

  while (! SLL_LIST_IS_EMPTY(&(color_state->palette_stack))) {
    palette = SLL_GET_HEAD(&(color_state->palette_stack), ColorPalette, sll) ;
    SLL_REMOVE_HEAD(&(color_state->palette_stack));
    free_palette(&palette) ;
  }

  SLL_RESET_LIST(&(color_state->palette_stack)) ;
}


/* Delete a palette with the given ID from the store if it exists.
 * (There can be at most one such palette).
 * N.B.   This is safe to call with an empty palette store.
 * N.N.B. Beware - this can delete the active palette!
 */
void delete_palette_from_store(PCL5Context *pcl5_ctxt, int32 id)
{
  ColorState *color_state ;
  ColorPalette *palette, *next_palette ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(id >= 0, "negative palette ID") ;

  color_state = get_color_state(pcl5_ctxt) ;
  HQASSERT(color_state != NULL, "color_state is NULL") ;

  palette = get_active_palette(pcl5_ctxt) ;

  if (palette == NULL)
    return ;

  if (palette->ID == id)
    destroy_active_palette(pcl5_ctxt) ;
  else {
    while (palette != NULL) {
      next_palette = SLL_GET_NEXT(palette, ColorPalette, sll) ;

      if (next_palette != NULL && next_palette->ID == id) {
        /* Found a match */
        SLL_REMOVE_NEXT(palette, sll) ;
        free_palette(&next_palette) ;
        break ;
      }
      palette = next_palette ;
    }
  }
}


/* Reset palette stack and store and create default B&W palette */
Bool color_state_init(PCL5Context *pcl5_ctxt)
{
  ColorState *color_state ;
  int32 i ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL" ) ;
  color_state = get_color_state(pcl5_ctxt) ;
  HQASSERT( color_state != NULL, "color_state is NULL");

  /* Quick initialisation of state */
  HqMemZero((uint8 *)color_state, (int)sizeof(ColorState));

  /* Initialise the color range */
  for (i=0; i<3; i++){
    color_state->color_range[i].lo = 0 ;
    color_state->color_range[i].hi = 255 ;
  }

  if (! init_palette_store(pcl5_ctxt))
    return FALSE ;

  SLL_RESET_LIST(&(color_state->palette_stack)) ;

  return TRUE ;
}

/* Free palette stack and store */
void color_state_finish(PCL5Context *pcl5_ctxt)
{
  ColorState *color_state ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL" ) ;
  color_state = get_color_state(pcl5_ctxt) ;
  HQASSERT( color_state != NULL, "color_state is NULL");

  free_palette_stack(pcl5_ctxt) ;
  free_palette_store(pcl5_ctxt) ;
}

/* Free and reset palette stack and store and create default B&W palette */
Bool color_state_reset(PCL5Context *pcl5_ctxt)
{
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL" ) ;
  color_state_finish(pcl5_ctxt) ;
  return color_state_init(pcl5_ctxt) ;
}

/* ==========================================================================*/
 /* Squirrel away a copy of the active palette when saving MPE.
  * N.B. In fact this does not at the moment need the new ColorInfo, it
  * just happens to be using the old one to hold onto a copy of the
  * active palette.  (So it could be called save color state, or
  * such like).
  */
void save_color_info(PCL5Context *pcl5_ctxt, ColorInfo* to, ColorInfo *from)
{
  ColorPalette *palette, *saved_palette ;

  UNUSED_PARAM(ColorInfo*, to) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(to != NULL, "ColorInfo is NULL") ;
  HQASSERT(from != NULL, "ColorInfo is NULL") ;

  palette = get_active_palette(pcl5_ctxt) ;
  HQASSERT(palette != NULL, "active palette is NULL") ;

  saved_palette = copy_active_palette(pcl5_ctxt, palette->ID) ;
  from->saved_palette = saved_palette ;
}


/* Delete the active palette and replace it with the squirelled away one
 * at the new, lower MPE level.  It is also necessary to delete any
 * other palette in the store with the same ID as the palette that is
 * about to be reinstated.
 *
 * N.B. It probably doesn't actually matter whether the palette being
 * squirrelled away is saved at the new or the old level, (as long as
 * the restore looks in the same place).
 */
void restore_color_info(PCL5Context *pcl5_ctxt, ColorInfo* to, ColorInfo *from)
{
  ColorState *color_state ;

  UNUSED_PARAM(ColorInfo*, from) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(to != NULL, "ColorInfo is NULL") ;
  HQASSERT(from != NULL, "ColorInfo is NULL") ;
  HQASSERT(to->saved_palette != NULL, "Saved palette is NULL") ;

  color_state = get_color_state(pcl5_ctxt) ;
  HQASSERT(color_state != NULL, "color_state is NULL") ;

  destroy_active_palette(pcl5_ctxt) ;

  /* Delete any palette with the same ID as the one about to be reinstated. */
  delete_palette_from_store(pcl5_ctxt, to->saved_palette->ID) ;

  /* Reinstate the saved palette */
  SLL_ADD_HEAD(&(color_state->palette_store), to->saved_palette, sll) ;
  to->saved_palette = NULL ;
}


#define IN_HPGL (gstackptr->gType == GST_PCL5)


static USERVALUE last_pcl5_color[3] = {-2.0, -2.0, -2.0};
static USERVALUE last_hpgl_color[3] = {-2.0, -2.0, -2.0};


static void set_last_hpgl_color(USERVALUE *vals, int32 num_vals)
{
  int32 i;

  HQASSERT(vals != NULL && num_vals > 0 && num_vals <= 3,
           "Unexpected vals when setting last hpgl color");

  for (i = 0; i < num_vals; i++)
    last_hpgl_color[i] = vals[i];
}


static void set_last_pcl5_color(USERVALUE *vals, int32 num_vals)
{
  int32 i;

  HQASSERT(vals != NULL && num_vals > 0 && num_vals <= 3,
           "Unexpected vals when setting last pcl5 color");

  for (i = 0; i < num_vals; i++)
    last_pcl5_color[i] = vals[i];
}


void reset_last_pcl5_and_hpgl_colors(void)
{
  USERVALUE out_of_range = -2.0;

  set_last_hpgl_color(&out_of_range, 1);
  set_last_pcl5_color(&out_of_range, 1);
}


/**
 * Set the current core colorspace using the passed PCL colorspace identifier,
 * which should be one of PCL5_CS_GRAY, PCL5_CS_RGB, etc.
 */
Bool set_ps_colorspace_internal(uint32 id)
{
  COLORSPACE_ID colorspace_id = SPACE_notset ;
  switch (id) {
  case PCL5_CS_GRAY:
    colorspace_id = SPACE_DeviceGray ;
    break ;

  case PCL5_CS_RGB:
    colorspace_id = SPACE_DeviceRGB ;
    break ;

  case PCL5_CS_CMY:
    colorspace_id = SPACE_DeviceCMY ;
    break ;

  default:
    HQFAIL("Unexpected PCL5 colorspace") ;
    break ;
  }

  if (pcl5eModeIsEnabled()) {
    HQASSERT(colorspace_id == SPACE_DeviceGray,
             "Color space not gray in 5E mode.");
  }

  /* Is it the same setcolorspace as is currently set ? */
  if ( gsc_getcolorspace(gstateptr->colorInfo,
       GSC_FILL) == colorspace_id )
    return TRUE;

  if (IN_HPGL)
    last_hpgl_color[0] = -2.0;
  else
    last_pcl5_color[0] = -2.0;

  if (! gsc_setcolorspacedirect( gstateptr->colorInfo, GSC_FILL, colorspace_id))
    return FALSE ;

  return TRUE ;
}

/* ==========================================================================*/
/* Get current color mode, e.g monochrome or RGB and set it as the default */
Bool get_default_color_mode(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *print_state ;
  OBJECT *temp_obj ;
  uint8 buffer[128] ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL" ) ;
  print_state = pcl5_ctxt->print_state ;
  HQASSERT( print_state != NULL, "state pointer is NULL");

  swncopyf(buffer, sizeof(buffer), (uint8*)
          "(%%embedded%%) currentdevparams /GGColorMode get" ) ;

  if (! run_ps_string(buffer))
    return FALSE ;

  temp_obj = theTop(operandstack) ;

  if ( oType(*temp_obj) != OINTEGER )
    return error_handler(TYPECHECK) ;

  print_state->default_color_mode = oInteger(*temp_obj) ;

  /* Also set the current color mode */
  print_state->current_color_mode = print_state->default_color_mode ;

  pop(&operandstack) ;

  return TRUE ;
}

/* ==========================================================================*/
Bool set_ps_colorspace(PCL5Context *pcl5_ctxt)
{
  ColorInfo *color_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL" ) ;
  color_info = get_color_info(pcl5_ctxt) ;
  HQASSERT(color_info != NULL, "color_info is NULL" ) ;

  return set_ps_colorspace_internal(color_info->foreground_color.colorspace) ;
}

Bool set_ps_color_internal(USERVALUE *values)
{
  USERVALUE *last_color = IN_HPGL ? last_hpgl_color : last_pcl5_color;

  /* Are we setting exactly the same color value as last time? */
  switch ( gsc_getcolorspace(gstateptr->colorInfo, GSC_FILL) ) {
    case SPACE_DeviceRGB: case SPACE_DeviceCMY:
      if ( values[0] == last_color[0] && values[1] == last_color[1]
           && values[2] == last_color[2] )
        return TRUE;
      last_color[0] = values[0];
      last_color[1] = values[1];
      last_color[2] = values[2];
      break;
    case SPACE_DeviceGray:
      if ( values[0] == last_color[0] )
        return TRUE;
      last_color[0] = values[0];
      break;
    default:
      last_color[0] = -2.0;
      break;
  }
  return gsc_setcolordirect(gstateptr->colorInfo, GSC_FILL, values);
}

Bool set_ps_color(PCL5Context *pcl5_ctxt)
{
  ColorInfo *color_info ;
  ForegroundColor *foreground ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL" ) ;
  color_info = get_color_info(pcl5_ctxt) ;
  HQASSERT(color_info != NULL, "color_info is NULL" ) ;
  foreground = &(color_info->foreground_color) ;

  return set_ps_color_internal(foreground->values);
}

/* See header for doc. */
int32 get_color_from_palette_index(PCL5Context *pcl5_ctxt,
                                  uint32 index,
                                  uint8* values)
{
  ColorPalette *palette ;
  int32 i, num_components, entries ;

  palette = get_active_palette(pcl5_ctxt) ;

  num_components = COMPONENTS_IN_COLORSPACE(palette->colorspace) ;
  entries = ENTRIES_IN_PALETTE(palette) ;
  index = index % entries ;

  HQASSERT(num_components <= PCL_MAX_COLORANTS, "Too many colorants.") ;
  for (i = 0; i < num_components; i++) {
    values[i] = palette->values[(num_components * index) + i] ;
  }
  return num_components;
}

/* See header for doc. */
Bool set_ps_color_from_palette_index(PCL5Context *pcl5_ctxt, uint32 index)
{
  int32 i, num_components ;
  uint8 values[PCL_MAX_COLORANTS] ;
  USERVALUE color[PCL_MAX_COLORANTS] ;
  ColorPalette *palette ;

  palette = get_active_palette(pcl5_ctxt) ;
  set_ps_colorspace_internal(palette->colorspace) ;
  num_components = get_color_from_palette_index(pcl5_ctxt, index, values) ;

  for (i = 0; i < num_components; i++) {
    /* Scale palette values from [0, 255] to [0, 1] */
    color[i] = values[i] / 255.0f ;
  }

  if (! set_ps_color_internal(color))
    return FALSE ;

  return TRUE ;
}

/* See header for doc. */
Bool set_current_color(PCL5Context *pcl5_ctxt)
{
  /* Set colorspace and color. */
  if (! set_ps_colorspace(pcl5_ctxt) ||
      ! set_ps_color(pcl5_ctxt))
    return FALSE ;

  return setPclForegroundSource(pcl5_ctxt->corecontext->page,
                                PCL_DL_COLOR_IS_FOREGROUND);
}

/* See header for doc. */
Bool foreground_is_white(PCL5Context *pcl5_ctxt)
{
  ColorInfo *color_info = get_color_info(pcl5_ctxt) ;
  ForegroundColor *fg = &color_info->foreground_color ;

  switch (fg->colorspace) {
  default:
    HQFAIL("Unexpected PCL5 colorspace") ;
    return FALSE ;

  case PCL5_CS_GRAY:
    return fg->values[0] == 1 ;

  case PCL5_CS_RGB:
    return fg->values[0] == 1 && fg->values[1] == 1 && fg->values[2] == 1;

  case PCL5_CS_CMY:
    return fg->values[0] == 0 && fg->values[1] == 0 && fg->values[2] == 0;
  }
}

/* See header for doc. */
uint32 classify_palette_entry(PCL5Context *pcl5_ctxt, uint32 index)
{
  uint8 values[PCL_MAX_COLORANTS] ;
  ColorPalette* palette = get_active_palette(pcl5_ctxt) ;

  get_color_from_palette_index(pcl5_ctxt, index, values) ;
  switch (palette->colorspace) {
  default:
    HQFAIL("Unsupported color space.") ;
    return FALSE;

  case PCL5_CS_RGB:
  case PCL5_CS_sRGB:
    if (values[0] == 255 && values[1] == 255 && values[2] == 255)
      return PCL_COLOR_IS_WHITE;
    if (values[0] == 0 && values[1] == 0 && values[2] == 0)
      return PCL_COLOR_IS_BLACK;
    break;

  case PCL5_CS_GRAY:
    if (values[0] == 255)
      return PCL_COLOR_IS_WHITE;
    if (values[0] == 0)
      return PCL_COLOR_IS_BLACK;
    break;

  case PCL5_CS_CMY:
    if (values[0] == 0 && values[1] == 0 && values[2] == 0)
      return PCL_COLOR_IS_WHITE;
    if (values[0] == 255 && values[1] == 255 && values[2] == 255)
      return PCL_COLOR_IS_BLACK;
    break;
  }
  return PCL_COLOR_IS_SHADE;
}

/* See header for doc. */
Bool set_shade(USERVALUE shade)
{
  if (! set_ps_colorspace_internal(PCL5_CS_GRAY))
    return FALSE;

  if (! set_ps_color_internal(&shade))
    return FALSE;

  return TRUE;
}

/* See header for doc. */
Bool set_white(void)
{
  return set_shade(1.0);
}

/* See header for doc. */
Bool set_black(void)
{
  return set_shade(0);
}

/* ============================================================================
 * Operator callbacks are below here.
 * ============================================================================
 */

/* Simple Color Command */
/* N.B. This command is locked out during raster graphics */
Bool pcl5op_star_r_U(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  int32 colorspace ;

  UNUSED_PARAM(int32, explicit_sign) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  switch (value.integer) {
  case (-3):
    colorspace = PCL5_CS_CMY ;
    break ;

  case (1):
    colorspace = PCL5_CS_GRAY ;
    break ;

  case (3):
    colorspace = PCL5_CS_RGB ;
    break ;
  default:
    /* Ignore invalid values */
    return TRUE ;
  }

  if (! create_simple_palette(pcl5_ctxt, colorspace))
    return FALSE ;

  return TRUE ;
}


static Bool discard_CID(PCL5Context *pcl5_ctxt, int32 numbytes)
{
  int32 ch ;
  while (numbytes-- > 0) {
    if ((ch = Getc(pcl5_ctxt->flptr)) == EOF)
      break ;
  }
  return TRUE ;
}

/* Configure Image Data (CID) Command

  ESC *v6W b0 b1 b2 b3 b4 b5

  Where:
  6 = The number of bytes following the W

  b0 = byte 0 The color space
  b1 = byte 1 The Pixel Encoding Mode
  b2 = byte 2 The number of bits per index which implies the
              size of the palette
  b3 = byte 3 The number of bits in color component
              (primary) #1
  b4 = byte 4 The number of bits in color component
              (primary) #2
  b5 = byte 5 The number of bits in color component
              (primary) #3

  N.B. This command is locked out during raster graphics
 */
Bool pcl5op_star_v_W(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  int32 ch ;
  uint8 colorspace, pixel_encoding_mode, bits_per_index, bits_for_component_one,
        bits_for_component_two, bits_for_component_three ;
  int32 numbytes ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* minus and plus signs are ignored */
  value.integer = value.integer < 0 ? - value.integer : value.integer ;
  numbytes = value.integer ;

  if (numbytes-- > 0) { /* Get color space */
    if ((ch = Getc(pcl5_ctxt->flptr)) == EOF)
      return TRUE ;
    switch (ch) {
    case PCL5_CS_RGB:  /* Device Dependent RGB */
    case PCL5_CS_CMY:  /* Device Dependent CMY */
    case PCL5_CS_sRGB: /* Standard RGB (sRGB) */
      colorspace = (uint8)ch ;
      break ;
    default:
      return discard_CID(pcl5_ctxt, numbytes) ;
    }
  } else {
    return TRUE ;
  }

  if (numbytes-- > 0) { /* Get encoding mode */
    if ((ch = Getc(pcl5_ctxt->flptr)) == EOF)
      return TRUE ;
    switch (ch) {
    case PCL5_INDEXED_BY_PLANE:
    case PCL5_INDEXED_BY_PIXEL:
    case PCL5_DIRECT_BY_PLANE:
    case PCL5_DIRECT_BY_PIXEL:
      pixel_encoding_mode = (uint8)ch ;
      break ;
    default:
      return discard_CID(pcl5_ctxt, numbytes) ;
    }
  } else {
    return TRUE ;
  }

  if (numbytes-- > 0) { /* Get number of bits per index */
    if ((ch = Getc(pcl5_ctxt->flptr)) == EOF)
      return TRUE ;
    bits_per_index = (uint8)ch ;
  } else {
    return TRUE ;
  }

  /* Get # of bits for components 1, 2, and 3 */
  if (numbytes-- > 0) { /* Get number of bits per index */
    if ((ch = Getc(pcl5_ctxt->flptr)) == EOF)
      return TRUE ;
    bits_for_component_one = (uint8)ch ;
  } else {
    return TRUE ;
  }
  if (numbytes-- > 0) { /* Get number of bits per index */
    if ((ch = Getc(pcl5_ctxt->flptr)) == EOF)
      return TRUE ;
    bits_for_component_two = (uint8)ch ;
  } else {
    return TRUE ;
  }
  if (numbytes-- > 0) { /* Get number of bits per index */
    if ((ch = Getc(pcl5_ctxt->flptr)) == EOF)
      return TRUE ;
    bits_for_component_three = (uint8)ch ;
  } else {
    return TRUE ;
  }

  /* Read and throw out CID long form for now. */
  while (numbytes-- > 0) {
    if ((ch = Getc(pcl5_ctxt->flptr)) == EOF)
      return TRUE ;
  }

  /* Some more validation. */
  switch (pixel_encoding_mode) {
  case PCL5_INDEXED_BY_PLANE:
    if (bits_per_index > 8)
      return TRUE ;
    break ;
  case PCL5_INDEXED_BY_PIXEL:
    if (bits_per_index != 0 &&
        bits_per_index != 1 && bits_per_index != 2 &&
        bits_per_index != 4 && bits_per_index != 8)
      return TRUE ;
    break ;
  case PCL5_DIRECT_BY_PLANE:
    if (bits_for_component_one != 1 || bits_for_component_two != 1 ||
        bits_for_component_three != 1)
      return TRUE ;
    break ;
  case PCL5_DIRECT_BY_PIXEL:
    if (bits_for_component_one != 8 || bits_for_component_two != 8 ||
        bits_for_component_three != 8)
      return TRUE ;
    break ;
  }

  /* Direct by pixel encoding mode appears to allow any value of bits per
   * index, (including zero), but then always behaves as if there were 8 bits
   * per index.
   *
   * Zero bits per index appear always defaulted to 1, (except for the direct
   * by pixel encoding mode where this is treated as 8).
   */
  if (pixel_encoding_mode == PCL5_DIRECT_BY_PIXEL)
    bits_per_index = 8 ;
  else if (bits_per_index == 0)
    bits_per_index = 1 ;

  /* Create the CID palette.
   * N.B. In the case of sRGB, direct by plane, no palette is created, (i.e.
   * the command is ignored as far as palette creation is concerned).  The
   * color range however, is updated.
   */
  if ((colorspace != PCL5_CS_sRGB) || (pixel_encoding_mode != PCL5_DIRECT_BY_PLANE))  {
    if (! create_cid_palette(pcl5_ctxt, colorspace, pixel_encoding_mode, bits_per_index))
      return FALSE ;
  }

  /* Update the black and white reference values. */
  set_color_range_from_bits_per_component(pcl5_ctxt, bits_for_component_one,
                                          bits_for_component_two, bits_for_component_three) ;

  return TRUE ;
}

/**
 * Set the indexed color component.
 */
static Bool set_color_component(PCL5Context *pcl5_ctxt,
                                uint32 index,
                                PCL5Numeric value)
{
  ColorState *color_state ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  color_state = get_color_state(pcl5_ctxt) ;
  HQASSERT(color_state != NULL, "color_state is NULL") ;

  /* Limit values to -32767 to 32767
   * N.B. Assume limit to range, and limit followed by rounding
   * by analogy with various other commands.
   */
  value.real = pcl5_limit_to_range(value.real, -32767, 32767) ;
  value.real = pcl5_round_to_4d(value.real) ;

  color_state->component_colors[index] = value.real ;

  return TRUE ;

}

/* Color Component One */
/* N.B. This does not appear to be locked out in Simple Color Mode.
 *      It is locked out during raster graphics.
 */
Bool pcl5op_star_v_A(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  UNUSED_PARAM(int32, explicit_sign) ;
  return set_color_component(pcl5_ctxt, 0, value);
}

/* Component Color Two */
/* N.B. This does not appear to be locked out in Simple Color Mode.
 *      It is locked out during raster graphics.
 */
Bool pcl5op_star_v_B(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  UNUSED_PARAM(int32, explicit_sign) ;
  return set_color_component(pcl5_ctxt, 1, value);
}

/* Component Color Three */
/* N.B. This does not appear to be locked out in Simple Color Mode.
 *      It is locked out during raster graphics.
 */
Bool pcl5op_star_v_C(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  UNUSED_PARAM(int32, explicit_sign) ;
  return set_color_component(pcl5_ctxt, 2, value);
}


/* Assign Color Index */
/* N.B. This command is locked out during raster graphics */
Bool pcl5op_star_v_I(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  ColorState *color_state ;
  ColorPalette *palette ;
  ColorRange *range ;
  PCL5Real comp_range, scaled_val, *colors ;
  int32 i, max_index ;
  int32 index = value.integer ;

  UNUSED_PARAM(int32, explicit_sign) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  color_state = get_color_state(pcl5_ctxt) ;
  HQASSERT(color_state != NULL, "color_state is NULL") ;

  palette = get_active_palette(pcl5_ctxt) ;
  HQASSERT(palette != NULL, "active palette is NULL") ;

  /* This command is locked out except for CID palettes */
  if ( palette->type != CID )
    return TRUE ;

  range = &(color_state->color_range[0]) ;
  colors = &(color_state->component_colors[0]) ;

  max_index = ENTRIES_IN_PALETTE(palette) - 1 ;

  if (index <= max_index && index >= 0) {

    /* Need to scale the color component values to the range 0 - 255.
     * N.B.   If more precision than this is wanted, could store PCL5Reals in
     *        the palettes, but would need to use a PS procedure rather than a
     *        string for the indexed colorspaces for images.
     * N.N.B. The Color Tech Ref seems to hint that the range for sRGB palettes
     *        is always 0 - 255.  Testing, however, does not bear this out.
     */

    for (i = 0; i < 3; i++) {

      /* N.B. Only a long form CID command could try to set up a zero range,
       * and currently we treat these as short form, discarding any extra data.
       * Even if we were to handle these, it is questionable whether we would
       * want to allow a zero range to be set up, so for now should be able
       * to assert that the range is non-zero.
       */
      comp_range = range[i].hi - range[i].lo ;

      HQASSERT(comp_range != 0, "Component range is zero") ;

      /* Clip to the range */
      if (colors[i] < range[i].lo)
        colors[i] = range[i].lo ;
      else if (colors[i] > range[i].hi)
        colors[i] = range[i].hi ;

      /* Scale to 0 - 255 */
      scaled_val = 255 * (colors[i] - range[i].lo) / comp_range ;

      /* Round to nearest integer for palette */
      palette->values[(3 * index) + i] = (uint8) (scaled_val + 0.5f) ;
    }
  }

  /* Reset the color components (even if the index was invalid) */
  colors[0] = colors[1] = colors[2] = 0 ;

  return TRUE ;
}


/* Push or Pop Palette */
/* N.B. This command is locked out during raster graphics */
Bool pcl5op_star_p_P(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  ColorState *color_state ;
  ColorPalette *stack_palette, *active_palette ;
  int32 id ;

  UNUSED_PARAM(int32, explicit_sign) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  color_state = get_color_state(pcl5_ctxt) ;
  HQASSERT(color_state != NULL, "color_state is NULL") ;

  /* N.B. Negative values seem to be treated as positive */
  switch (abs(value.integer)) {
  case 0:
    /* Push */
    stack_palette = copy_active_palette(pcl5_ctxt, -1) ;
    if (stack_palette == NULL)
      return FALSE ;

    SLL_ADD_HEAD(&(color_state->palette_stack), stack_palette, sll) ;
    break ;

  case 1:
    /* Pop */
    active_palette = get_active_palette(pcl5_ctxt) ;
    HQASSERT( active_palette != NULL, "active palette is NULL") ;
    id = active_palette->ID ;
    stack_palette = SLL_GET_HEAD(&(color_state->palette_stack), ColorPalette, sll) ;

    /* Attempts to pop from an empty stack are ignored */
    if (stack_palette != NULL) {
      SLL_REMOVE_HEAD(&color_state->palette_stack) ;
      SLL_RESET_LINK(stack_palette, sll) ;
      stack_palette->ID = id ;
      destroy_active_palette(pcl5_ctxt) ;
      SLL_ADD_HEAD(&(color_state->palette_store), stack_palette, sll);
    }
    break ;

  default:
    break ;
  }

  return TRUE ;
}


/* Palette Select Command */
/* N.B. This command is locked out during raster graphics */
Bool pcl5op_ampersand_p_S(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  ColorState *color_state ;
  ColorPalette *palette, *next_palette ;
  int32 id ;

  UNUSED_PARAM(int32, explicit_sign) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  color_state = get_color_state(pcl5_ctxt) ;
  HQASSERT(color_state != NULL, "color_state is NULL") ;

  palette = get_active_palette(pcl5_ctxt) ;
  HQASSERT(palette != NULL, "active palette is NULL") ;

  /* N.B. The PCL5 Color Tech Ref states that the command is ignored for values
   * outside the range [0, 32767].  However, testing implies that negative
   * values are treated as positive, and that values are then limited to this
   * range.
   */
  id = (int32) pcl5_limit_to_range(abs(value.integer), 0, 32767) ;

  if (palette->ID == id)
    return TRUE ;

  /* Find the desired palette and move it to the head of the list,
   * as we define the active palette to be the one at the head.
   * (We could simply point to the active palette, but this has
   * the added benefit of keeping them in MRU order).
   */

  do {
    next_palette = SLL_GET_NEXT(palette, ColorPalette, sll) ;

    if (next_palette != NULL && next_palette->ID == id) {
      /* Found a match */
      SLL_REMOVE_NEXT(palette, sll) ;
      SLL_RESET_LINK(next_palette, sll) ;
      SLL_ADD_HEAD(&(color_state->palette_store), next_palette, sll);
      break ;
    }

    palette = next_palette ;
  } while (palette != NULL) ;

  return TRUE ;
}


/* Palette Control ID */
/* N.B. This command is locked out during raster graphics */
Bool pcl5op_ampersand_p_I(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  int32 id ;

  UNUSED_PARAM(int32, explicit_sign) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* N.B. The PCL5 Color Tech Ref states that the command is ignored for values
   * outside the range [0, 32767].  However, testing implies that negative
   * values are treated as positive, and that values are then limited to this
   * range.
   */
  id = (int32) pcl5_limit_to_range(abs(value.integer), 0, 32767) ;
  set_palette_control_id(pcl5_ctxt, id) ;

  return TRUE ;
}


/* Palette Control Command */
/* N.B. This command is locked out during raster graphics */
Bool pcl5op_ampersand_p_C(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  ColorState *color_state ;
  ColorInfo *color_info ;
  ColorPalette *palette, *active_palette, *next_palette ;
  int32 id ;
  Bool success = TRUE ;
  UNUSED_PARAM(int32, explicit_sign) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  color_state = get_color_state(pcl5_ctxt) ;
  HQASSERT(color_state != NULL, "color_state is NULL") ;

  color_info = get_color_info(pcl5_ctxt) ;
  HQASSERT(color_info != NULL, "color_info is NULL") ;

  switch (value.integer) {
  case 0:
    /* Delete all palettes, (except those in the stack).
     * N.B. palettes squirrelled away at a lower MPE level appear to be
     * unaffected.
     */
    free_palette_store(pcl5_ctxt) ;
    success = init_palette_store(pcl5_ctxt) ;
    break ;

  case 1:
    free_palette_stack(pcl5_ctxt) ;
    break ;

  case 2:
    /* Delete the palette specified by the palette control ID.
     * N.B. Palettes with this ID squirrelled away at a lower MPE level
     * appear to be unaffected.
     */
    id = get_palette_control_id(pcl5_ctxt) ;
    active_palette = get_active_palette(pcl5_ctxt) ;
    HQASSERT(active_palette != NULL, "active palette is NULL") ;

    if (active_palette->ID == id) {
      /* N.B. The active palette is about to be replaced by a palette with
       * ID 0.  So first delete any other palette with ID 0.
       */
      if (id != 0)
        delete_palette_from_store(pcl5_ctxt, 0) ;

      if (! create_simple_palette(pcl5_ctxt, PCL5_CS_GRAY))
        return FALSE ;
      active_palette = get_active_palette(pcl5_ctxt) ;
      HQASSERT(active_palette != NULL, "active palette is NULL") ;
      active_palette->ID = 0 ;
    }
    else
      delete_palette_from_store(pcl5_ctxt, id) ;
    break ;

  case 6:
    /* Copy the active palette to the ID specified by the palette control ID,
     * (ignore the command if the active palette has the palette control ID).
     */
    id = get_palette_control_id(pcl5_ctxt) ;
    active_palette = get_active_palette(pcl5_ctxt) ;
    HQASSERT(active_palette != NULL, "active palette is NULL" ) ;

    if (active_palette->ID != id) {

      /* See if there is a palette with the palette control ID */
      palette = active_palette ;

      do {
        next_palette = SLL_GET_NEXT(palette, ColorPalette, sll) ;

        if (next_palette != NULL && next_palette->ID == id) {
          /* Found a match */
          SLL_REMOVE_NEXT(palette, sll) ;

          if (palette_sizes_match(next_palette, active_palette)) {
            /* Copy the contents, set the id and reset the link */
            copy_full_palette(next_palette, active_palette);
            next_palette->ID = id ;
            SLL_RESET_LINK(next_palette, sll) ;
          }
          else {
            /* Free the palette we just found */
            free_palette(&next_palette) ;
            palette = NULL ;    /* so we copy the active palette below */
          }
          break ;
        }

        palette = next_palette ;
      } while (palette != NULL) ;

      /* Here palette will be NULL if we didn't find a palette with the
       * palette control ID, or if we did find one but freed it because it
       * was the wrong size.
       */
      if (palette == NULL)
        next_palette = copy_active_palette(pcl5_ctxt, id) ;

      /* Place the palette in the store after the active palette.  (It does
       * not particularly have to go there, but this keeps behaviour same
       * regardless of whether a match was found above).
       */
      if (next_palette != NULL)
        SLL_ADD_AFTER(active_palette, next_palette, sll) ;
      else
        success = FALSE ;
    }
    break ;

  default:
    break ;
  }

  return success ;
}


/* Foreground Color */
/* N.B. This command is locked out during raster graphics */
Bool pcl5op_star_v_S(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  ColorState *color_state ;
  ColorPalette *palette ;
  ColorInfo *color_info ;
  ForegroundColor *foreground ;
  int32 index, i , num_components, entries ;

  UNUSED_PARAM(int32, explicit_sign) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  color_state = get_color_state(pcl5_ctxt) ;
  HQASSERT(color_state != NULL, "color_state is NULL") ;

  palette = get_active_palette(pcl5_ctxt) ;
  HQASSERT(palette != NULL, "active palette is NULL") ;

  color_info = get_color_info(pcl5_ctxt) ;
  HQASSERT(color_info != NULL, "color_info is NULL") ;
  foreground = &(color_info->foreground_color) ;

  /* N.B. Strictly have only tested that this is limited to [0, 32767]
   * with palettes created in PCL5, not HPGL2, (but seems unlikely to
   * be different).
   */
  index = (int32) pcl5_limit_to_range(value.integer, 0, 32767) ;

  entries = ENTRIES_IN_PALETTE(palette) ;
  num_components = COMPONENTS_IN_COLORSPACE(palette->colorspace) ;

  /* Copy the colorspace and type. */
  foreground->colorspace = palette->colorspace ;
  foreground->type = palette->type ;

  /* Copy the colorvalues */
  /* N.B. See Color Tech Ref page 3-17 for the PCL5 version.
   * The DesignJet Language Guide Select Pen command gives the formula
   * below for HPGL2 for raster devices, where the request is for a pen
   * number greater than the max index, (or max number of pens in the
   * device).  (A different formula is given for pen plotters, since
   * pens are numbered from 1, not 0).
   */
  if (palette->creator == PCL5 || index <= (entries - 1))
    index = index % entries ;
  else
    index = ((index - 1) % (entries - 1)) + 1 ;

  for (i = 0; i < num_components; i++) {
    /* Scale palette values from [0, 255] to [0, 1] */
    foreground->values[i] = palette->values[(num_components * index) + i] / 255.0f ;
  }

  return TRUE ;
}

/* Render Algorithm */
/* N.B. The Color Tech Ref states that command is locked out during raster graphics,
 *      however, on our reference printers this command ends raster graphics.
 */
Bool pcl5op_star_t_J(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;
  UNUSED_PARAM(int32, explicit_sign) ;
  UNUSED_PARAM(PCL5Numeric, value) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  return TRUE ;
}

/* Download dither matrix */
/* N.B. This ends graphics on our reference printer */
Bool pcl5op_star_m_W(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  int32 numbytes ;

  UNUSED_PARAM(int32, explicit_sign) ;

  numbytes = min(value.integer, 32767);
  if ( numbytes == 0 ) {
    return(TRUE);
  }

  return(file_skip(pcl5_ctxt->flptr, numbytes, NULL) > 0);
}

/* Color lookup table */
/* N.B. This ends graphics on our reference printer */
Bool pcl5op_star_l_W(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  int32 numbytes ;

  UNUSED_PARAM(int32, explicit_sign) ;

  numbytes = min(value.integer, 32767);
  if ( numbytes == 0 ) {
    return(TRUE);
  }

  return(file_skip(pcl5_ctxt->flptr, numbytes, NULL) > 0);
}

/* Gamma correction */
/* N.B. This command does not appear to end graphics on our reference printer */
Bool pcl5op_star_t_I(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;
  UNUSED_PARAM(int32, explicit_sign) ;
  UNUSED_PARAM(PCL5Numeric, value) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  return TRUE ;
}

/* Viewing illuminant */
/* N.B. This ends raster graphics on our reference printer */
Bool pcl5op_star_i_W(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  int32 numbytes ;

  UNUSED_PARAM(int32, explicit_sign) ;

  numbytes = min(value.integer, 32767);
  if ( numbytes == 0 ) {
    return(TRUE);
  }

  return(file_skip(pcl5_ctxt->flptr, numbytes, NULL) > 0);
}

/* Driver function config */
/* N.B. The Color Tech Ref states that command is locked out during raster graphics,
 *      however, on our reference printers this command ends raster graphics.
 */
Bool pcl5op_star_o_W(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  int32 numbytes ;

  UNUSED_PARAM(int32, explicit_sign) ;

  numbytes = min(value.integer, 32767);
  if ( numbytes == 0 ) {
    return(TRUE);
  }

  return(file_skip(pcl5_ctxt->flptr, numbytes, NULL) > 0);
}

/* Monochrome print mode */
/* N.B. This ends raster graphics on our reference printers */
Bool pcl5op_ampersand_b_M(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  uint32 color_mode = (uint32) value.integer ;
  PCL5PrintState *print_state ;
  uint8 buffer[256] ;

  UNUSED_PARAM(int32, explicit_sign) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  print_state = pcl5_ctxt->print_state ;
  HQASSERT( print_state != NULL, "state pointer is NULL");

  /* Only act on this command if there are no marks on the page.
   * The Tech Ref states that the only valid values are 0 and 1,
   * though this has not been confirmed.
   * Also assuming we will always get the requested color mode, so
   * no need to keep track of actual and requested modes, or to
   * repeat the request if we already have the desired mode.
   */
  /** \todo Have not investigated what should happen in XL passthrough
   *  mode.
   */
  /** \todo Probably would ideally not be dependent on hard coded color
   *  mode values.
   */
  if (pcl5_ctxt->pass_through_mode == PCLXL_SNIPPET_JOB_PASS_THROUGH)
    return TRUE ;

  if (!IN_RANGE(color_mode, 0, 1) ||
      (color_mode == 0 && print_state->current_color_mode != 1) ||
      (color_mode == 1 && print_state->current_color_mode == 1) ||
      ! displaylistisempty(pcl5_ctxt->corecontext->page))
    return TRUE ;

  color_mode = (color_mode ? 1 : print_state->default_color_mode) ;

  swncopyf(buffer, sizeof(buffer), (uint8*)
           "mark{%d /HqnEmbedded/ProcSet findresource/HqnEbd_SetColorMode get exec}stopped cleartomark",
           color_mode) ;

  /* Make the PS setpagedevice call and fill in the resulting MPE state */
  if (! handle_pcl5_page_change(pcl5_ctxt, buffer, PCL5_STATE_MAINTENANCE, FALSE))
    return FALSE ;

  /* Also set the current color mode */
  print_state->current_color_mode = color_mode;

  /** \todo Although the page is not being reset here, perhaps because we do
   * not want to change the default cursor position, it seems likely we may
   * want to do the other parts of reset_page here.
   */
  return TRUE;
}


/* ============================================================================
* Log stripped */
