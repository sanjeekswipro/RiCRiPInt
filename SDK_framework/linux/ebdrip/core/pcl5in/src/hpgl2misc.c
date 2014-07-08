/** \file
 * \ingroup hpgl2
 *
 * $HopeName: COREpcl_pcl5!src:hpgl2misc.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the HPGL2 "Misc" category.
 * (TL, XT and YT are unsupported)
 *
 * Dual Context Extensions
 *  ESC-%#A   % Enter PCL
 *  ESC-E     % Printer Reset
 *
 * Palette Extension
 *  CR        % Color range
 *  NP        % Number of pens
 *  PC        % Pen color assignment
 */

#include "core.h"
#include "hpgl2misc.h"

#include "pcl5context_private.h"
#include "hpgl2scan.h"
#include "pcl5color.h"
#include "hqmemcpy.h"
#include "hpgl2config.h"

#include "objects.h"
#include "fileio.h"
#include "swerrors.h"
#include "graphics.h"
#include "gschead.h"
#include "gstate.h"

void hpgl2_default_pen_values(ColorPalette *palette, HPGL2Integer pen);

void default_HPGL2_palette_extension_info(
    HPGL2PaletteExtensionInfo *hpgl2_palette_extension)
{
  HPGL2Real *CR_black = NULL;
  HPGL2Real *CR_white = NULL;

  CR_black = hpgl2_palette_extension->CR_black;
  CR_black[0] = CR_black[1] = CR_black[2] = 0.0;
  CR_white = hpgl2_palette_extension->CR_white;
  CR_white[0] = CR_white[1] = CR_white[2] = 255.0;
}

void hpgl2_set_default_palette_extension_info(
    HPGL2PaletteExtensionInfo *hpgl2_palette_extension, Bool init)
{
  HPGL2Real *CR_black = NULL;
  HPGL2Real *CR_white = NULL;

  if ( init ) {
    CR_black = hpgl2_palette_extension->CR_black;
    CR_black[0] = CR_black[1] = CR_black[2] = 0.0;
    CR_white = hpgl2_palette_extension->CR_white;
    CR_white[0] = CR_white[1] = CR_white[2] = 255.0;
  }
}

HPGL2Real* alloc_pen_width_table(uint32 bits_per_index)
{
  HPGL2Real *pen_widths = NULL;

  pen_widths = mm_alloc(mm_pcl_pool,
                        sizeof(HPGL2Real) * ((size_t)1 << bits_per_index),
                        MM_ALLOC_CLASS_PCL_CONTEXT);

  if ( pen_widths == NULL )
    error_handler(VMERROR);

  return pen_widths ;
}

void free_pen_width_table(HPGL2Real *table, int32 bits_per_index)
{
  mm_free(mm_pcl_pool, table, sizeof(HPGL2Real) * ((size_t)1 << bits_per_index));
}

/* copy "count" entries from source table to target table. */
void copy_pen_width_table(HPGL2Real *target,
                          HPGL2Real *source,
                          int32 bits_per_index)
{
  HqMemCpy(target, source, ((size_t)1 << bits_per_index) * sizeof(HPGL2Real));
}

/* Memory for the pen width tables must have been allocated previously.
 * Any required default values must have been set previously. This
 * function copies data from source to targer until target is full, or
 * source is exhausted. It is not an error for source and target to be
 * different sizes. */
void hpgl2_copy_pen_width_table(ColorPalette *target,
                                ColorPalette *source)
{
  uint32 bits_per_index;

  HQASSERT( target != NULL, "NULL ColorPalette target");
  HQASSERT( source != NULL, "NULL ColorPalette source");

  bits_per_index = ( source->bits_per_index < target->bits_per_index ?
                       source->bits_per_index : target->bits_per_index ) ;

  HQASSERT(source->pen_widths != NULL, "Source pen_widths is NULL");
  HQASSERT(target->pen_widths != NULL, "Target pen_widths is NULL");

  if ( source->pen_widths != NULL && target->pen_widths != NULL ) {
    HqMemCpy(target->pen_widths,
             source->pen_widths,
             PENWIDTHTABLE_SIZE(bits_per_index));
  }

}

/* Trivial place holder for size equality test on pen width tables. */
Bool hpgl2_pen_width_tables_equal_size(ColorPalette *a, ColorPalette *b)
{
  return a->bits_per_index == b->bits_per_index ;
}

/* ---- HPGL2 Operators --- */


/* Is CR even needed for our implementation? Is it supported by our
 * reference printers? What use is it when we have only 255 values for the
 * component levels.
 * Does CR change the interpretation of black, white etc? I.e are definition
 * of pens 0 and 1 tied to this.
 */
Bool hpgl2op_CR(PCL5Context *pcl5_ctxt)
{
  uint8 terminator;
  HPGL2Real c1_b,c1_w,c2_b,c2_w,c3_b,c3_w;
  HPGL2PaletteExtensionInfo *palette_extension = NULL;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  palette_extension = get_hpgl2_palette_extension(pcl5_ctxt);

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {
    c1_b = c2_b = c3_b = 0.0;
    c1_w = c2_w = c3_w = 255.0;
  }
  else {
    (void) hpgl2_scan_separator(pcl5_ctxt);
    if ( hpgl2_scan_clamped_real(pcl5_ctxt, &c1_b) > 0
        && hpgl2_scan_separator(pcl5_ctxt) > 0
        && hpgl2_scan_clamped_real(pcl5_ctxt, &c1_w) > 0
        && hpgl2_scan_separator(pcl5_ctxt) > 0
        && hpgl2_scan_clamped_real(pcl5_ctxt, &c2_b) > 0
        && hpgl2_scan_separator(pcl5_ctxt) > 0
        && hpgl2_scan_clamped_real(pcl5_ctxt, &c2_w) > 0
        && hpgl2_scan_separator(pcl5_ctxt) > 0
        && hpgl2_scan_clamped_real(pcl5_ctxt, &c3_b) > 0
        && hpgl2_scan_separator(pcl5_ctxt) > 0
        && hpgl2_scan_clamped_real(pcl5_ctxt, &c3_w) > 0 ) {

      (void) hpgl2_scan_terminator(pcl5_ctxt, &terminator);

      c1_b = clamp_hpgl2real(c1_b, -32768.0, 32768.0);
      c1_w = clamp_hpgl2real(c1_w, -32768.0, 32768.0);
      c2_b = clamp_hpgl2real(c2_b, -32768.0, 32768.0);
      c2_w = clamp_hpgl2real(c2_w, -32768.0, 32768.0);
      c3_b = clamp_hpgl2real(c3_b, -32768.0, 32768.0);
      c3_w = clamp_hpgl2real(c3_w, -32768.0, 32768.0);
    }
    else {
      /* syntax error */
      return TRUE;
    }
  }

  /* palette colors themselves are normalised 0 to 255 so need no adjustment
   * on change to color range.
   */
  palette_extension->CR_black[0] = c1_b;
  palette_extension->CR_black[1] = c2_b;
  palette_extension->CR_black[2] = c3_b;
  palette_extension->CR_white[0] = c1_w;
  palette_extension->CR_white[1] = c2_w;
  palette_extension->CR_white[2] = c3_w;

  return TRUE ;
}

Bool hpgl2op_NP(PCL5Context *pcl5_ctxt)
{
  uint8 terminator;
  uint8 log = 0;
  HPGL2Integer pen_count = 0;
  ColorPalette *current_palette = NULL,
               *new_palette = NULL;
  ColorState *color_state = NULL;
  HPGL2LineFillInfo *line_info = NULL;

  HQASSERT(pcl5_ctxt != NULL, "PCL5Context is NULL");

  color_state = get_color_state(pcl5_ctxt) ;
  HQASSERT(color_state != NULL, "color_state is NULL");

  current_palette = get_active_palette(pcl5_ctxt);
  HQASSERT(current_palette != NULL, "Current palette is NULL");

  line_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  HQASSERT( line_info != NULL, "No line fill info");

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 )
    pen_count = HPGL2_DEFAULT_PEN_COUNT ;
  else if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &pen_count) <= 0 )
    return TRUE ; /* syntax error, just bail on it */

  (void)hpgl2_scan_terminator(pcl5_ctxt, &terminator);


  if ( !pcl5_ctxt->pcl5c_enabled )
    return TRUE;

  /* NP cannot alter simple palettes */
  if ( current_palette->type != CID )
    return TRUE;

  /* spec says pen count clamped to 2 to 32678, but also says values less
   * than 2 are ignored. The actual maximum number of pens is device
   * dependent.
   */
  if ( pen_count < 2 )
    return TRUE;

  pen_count = clamp_hpgl2int(pen_count, 2, HPGL2_MAX_PENS) ;

  log = 1;

  /* find lowest power of 2 greater than or equal to, the pen count. */
  while ( ( 1 << log ) < pen_count )
    log++;

  HQASSERT( (1 << log) <= HPGL2_MAX_PENS, "Bad pen count" );

  if ( log == current_palette->bits_per_index )
    return TRUE;

  /* As we are in a HPGL operator, there must be a pen width table */
  HQASSERT(current_palette->pen_widths != NULL, "No pen width table");
  new_palette = alloc_palette(pcl5_ctxt, current_palette->colorspace, log);
  if ( NULL == new_palette )
    return FALSE; /* must be a VMERROR */

  /* NP involves resizing a palette, so cannot use copying at the moment. */
  /** \todo
   * Need a neater method than this; integrate with the PCL palette copying. */
  new_palette->ID = current_palette->ID;
  new_palette->colorspace = current_palette->colorspace;
  new_palette->type = current_palette->type;
  new_palette->creator = current_palette->creator;
  new_palette->bits_per_index = log;
  new_palette->pixel_encoding_mode = current_palette->pixel_encoding_mode;
  new_palette->num_values = 3 * ( 1 << log );

  hpgl2_set_palette_pen_width(new_palette,
                              HPGL2_ALL_PENS,
                              hpgl2_get_default_pen_width(
                                line_info->pen_width_selection_mode));
  hpgl2_default_pen_values(new_palette, HPGL2_ALL_PENS);

  /* copy as much from source as possible. */
  copy_palette_data(new_palette, current_palette);

  /* Add the new palette as the active one. */
  destroy_active_palette(pcl5_ctxt);
  SLL_RESET_LINK(new_palette, sll);
  SLL_ADD_HEAD(&(color_state->palette_store), new_palette, sll);

  return TRUE;
}


/* It is not clear from specification whether HPGL needs to account for PCL
 * color range specifications.
 */
/** \todo Assume that it is safe for HPGL to do its own mapping to
 * device color values.
 */

/** \todo
 * The color component manipulation functions contain too much internal
 * knowledge of the PCL color palette. Refactor to share functionality with PCL
 */
void hpgl2_map_color_comp_range(PCL5Context *pcl5_ctxt,
                           HPGL2Real in[],
                           HPGL2Real out[] )
{
  HPGL2PaletteExtensionInfo *palette_extension = NULL;
  HPGL2Real *CR_black;
  HPGL2Real *CR_white;

  palette_extension = get_hpgl2_palette_extension(pcl5_ctxt);
  CR_black = palette_extension->CR_black;
  CR_white = palette_extension->CR_white;

  /* clamp to range. */
  out[0] = clamp_hpgl2real(in[0], CR_black[0], CR_white[0]);
  out[1] = clamp_hpgl2real(in[1], CR_black[1], CR_white[1]);
  out[2] = clamp_hpgl2real(in[2], CR_black[2], CR_white[2]);

  /* map linearly into range 0 to 255 for palette */
  out[0] = (out[0] - CR_black[0]) * (255.0 / ( CR_white[0] - CR_black[0])) ;
  out[1] = (out[1] - CR_black[1]) * (255.0 / ( CR_white[1] - CR_black[1])) ;
  out[2] = (out[2] - CR_black[2]) * (255.0 / ( CR_white[2] - CR_black[2])) ;

  HQASSERT( out[0] >= 0.0 && out[0] <= 255.0, "Bad color component");
  HQASSERT( out[1] >= 0.0&& out[1] <= 255.0, "Bad color component");
  HQASSERT( out[2] >= 0.0 && out[2] <= 255.0, "Bad color component");

}

/* colors always have 3 components. */
void hpgl2_set_pen_components(ColorPalette *palette,
                             HPGL2Integer pen,
                             HPGL2Real* values)
{
  if ( pen < ( 1 << palette->bits_per_index) ) {
    int32 offset = 3 * pen;

    palette->values[offset] = (uint8) (values[0] + 0.5f);
    palette->values[offset+1] = (uint8) (values[1] + 0.5f);
    palette->values[offset+2] = (uint8) (values[2] + 0.5f);
  }
}

uint32 hpgl2_palette_black_index(ColorPalette *palette)
{
  if ( palette->creator == HPGL2 )
    return 1; /* HPGL pen 1 is black . */
  else {
    if ( palette->colorspace == PCL5_CS_CMY )
      return 7;
     else
       return 0;
  }
}

/* palettes with more than 8 entries only have 8 default colors */
#define MAX_DEFAULTED_PEN (7)

void hpgl2_default_pen_values(ColorPalette *palette, HPGL2Integer pen)
{
  uint8 * def_comps = NULL;
  uint32 palette_size;
  uint32 default_index;

  HQASSERT(palette != NULL, "Invalid palette");

  def_comps = select_palette_default_values(palette->creator,
                                           palette->colorspace,
                                           palette->bits_per_index,
                                           palette->type);

  palette_size = 1 << palette->bits_per_index;
  default_index =  hpgl2_palette_black_index(palette);

  if ( def_comps == NULL ) {
    HQFAIL("No defaults founds");
    return;
  }

  if ( pen == HPGL2_ALL_PENS ) {

    while (palette_size > 8 ) {
      --palette_size;
      palette->values[ (3 * palette_size) ] =
        def_comps[ 3 * default_index + 0];
      palette->values[ (3 * palette_size) + 1] =
        def_comps[ 3 * default_index + 1];
      palette->values[ (3 * palette_size) + 2] =
        def_comps[ 3 * default_index + 2];
    }

    while ( palette_size > 0 ) {
      --palette_size;
      palette->values[ (3*palette_size)] = def_comps[(3*palette_size)];
      palette->values[ (3*palette_size)+1] = def_comps[(3*palette_size)+1];
      palette->values[ (3*palette_size)+2] = def_comps[(3*palette_size)+2];
    }

  }
  else {

    if ( pen < MAX_DEFAULTED_PEN )
      default_index = pen;

    palette->values[ (3*pen)] = def_comps[(3*default_index)];
    palette->values[ (3*pen)+1] = def_comps[(3*default_index)+1];
    palette->values[ (3*pen)+2] = def_comps[(3*default_index)+2];
  }
}

Bool hpgl2op_PC(PCL5Context *pcl5_ctxt)
{
  HPGL2Integer pen;
  HPGL2Real comp[3], adj_comp[3];
  ColorPalette *current_palette = NULL;
  uint8 terminator;
  HPGL2LineFillInfo *line_info = NULL;

  HQASSERT(pcl5_ctxt != NULL, "NULL PCL5Context");

  current_palette = get_active_palette(pcl5_ctxt);
  HQASSERT(current_palette != NULL, "No current palette");

  line_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  HQASSERT(line_info != NULL, "NULL line fill info");

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator ) > 0 ) {
    hpgl2_default_pen_values(current_palette, HPGL2_ALL_PENS);
    return TRUE;
  }

  if ( hpgl2_scan_integer(pcl5_ctxt, &pen) <= 0 )
    return TRUE; /* syntax error */

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {
    /* default a particular pen, according to its palette type */
    if ( pcl5_ctxt->pcl5c_enabled )
      hpgl2_default_pen_values(current_palette, pen);
  }
  else if ( hpgl2_scan_separator(pcl5_ctxt) > 0
            && hpgl2_scan_real(pcl5_ctxt, &(comp[0])) > 0
            && hpgl2_scan_separator(pcl5_ctxt) > 0
            && hpgl2_scan_real(pcl5_ctxt, &(comp[1])) > 0
            && hpgl2_scan_separator(pcl5_ctxt) > 0
            && hpgl2_scan_real(pcl5_ctxt, &(comp[2])) > 0 ) {

    (void)hpgl2_scan_terminator(pcl5_ctxt, &terminator);

    if ( pen >= ( 1 << current_palette->bits_per_index) )
      return TRUE; /* pen out of range. */

    if ( pcl5_ctxt->pcl5c_enabled && ( current_palette->type == CID ) ) {
      hpgl2_map_color_comp_range(pcl5_ctxt, comp, adj_comp);
      hpgl2_set_pen_components(current_palette, pen, adj_comp);
      if ( pen == line_info->selected_pen )
        hpgl2_sync_pen_color(pcl5_ctxt, FALSE);
    }
  }
  /* else, syntax error; just return */

  return TRUE ;
}

/* ============================================================================
* Log stripped */
