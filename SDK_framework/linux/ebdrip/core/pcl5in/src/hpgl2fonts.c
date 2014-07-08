/** \file
 * \ingroup hpgl2
 *
 * $HopeName: COREpcl_pcl5!src:hpgl2fonts.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the HPGL2 "Character Group" category.
 *
 *   AD    % Alternate Font Definition
 *   CF    % Character Fill Mode
 *   CP    % Character Plot
 *   DI    % Absolute Direction
 *   DR    % Relative Direction
 *   DT    % Define Label Terminator
 *   DV    % Define Variable Text Path
 *   ES    % Extra Space
 *   FI    % Select Primary Font
 *   FN    % Select Secondary Font
 *   LB    % Label
 *   LO    % Label Origin
 *   SA    % Select Alternate Font
 *   SB    % Scalable or Bitmap Fonts
 *   SD    % Standard Font Definition
 *   SI    % Absolute Character Size
 *   SL    % Character Slant
 *   SR    % Relative Character Size
 *   SS    % Select Standard font
 *   TD    % Transparent Data
 */

#include "core.h"
#include "hpgl2fonts.h"

#include "pcl5context_private.h"
#include "pcl5scan.h"
#include "hpgl2scan.h"
#include "macros.h"
#include "fontselection.h"
#include "pagecontrol.h"
#include "printenvironment_private.h"
#include "printmodel.h"
#include "hpgl2vector.h"
#include "stickarc.h"

#include "objects.h"
#include "ascii.h"
#include "fileio.h"
#include "swcopyf.h"
#include "graphics.h"
#include "pathcons.h"
#include "gu_cons.h"
#include "gstack.h"
#include "objstack.h"
#include "stacks.h"
#include "miscops.h"
#include "monitor.h"
#include "hqmemcpy.h"
#include "gu_path.h"
#include "charsel.h"
#include "fcache.h"
#include "vndetect.h"
#include "fontops.h"
#include "display.h"
#include "routedev.h"
#include "idlom.h"
#include "namedef_.h"
#include "showops.h"
#include "gu_ctm.h"
#include "mathfunc.h"
#include "plotops.h"

#define MAX_KINDS (7)

/* 1016 plotter units equals 1 inch (1 point is 1/72 of an inch). */
#define HPGL2_POINT_TO_PLOTTER_UNITS (1016.0 / 72.0)

/* 1016 plotter units equals 1 inch as do 7200 PCL5 internal units. */
#define HPGL2_PLOTTER_UNIT_TO_PCL5_INTERNAL_UNITS (7200.0 / 1016.0)

/* 72 points equal 1 inch as do 25.4 mm */
#define HPGL2_POINT_TO_MMS (25.4 / 72.0)

/* The spec simply states that edge pen thickness automatically increases in
   proportion to point size.  Unfortunately that proportion isn't given, so I
   picked a number that look closet to the observed output on the HP
   printers. */
#define HPGL2_MAGIC_EDGE_PROPORTION (0.004)

/* A made up number to get things looking reasonably ok... */
#define HPGL2_ARC_PEN_WIDTH (0.019)

/* Number came from CET job 30_20 which caused problems with coords
   greater than this. */
#define HPGL2_PEN_POSITION_LIMIT (1e10)

/* The spec refers to the nominal character width and height in reference to SI
   (absolute character width and height).  The nominal character is unspecified,
   unknown and could be anything.  After testing it appears that nominal
   character height is cap height and nominal character width is just some
   proportion of that.
 */
#define HPGL2_NOMINAL_CHAR_PROPORTION (0.74)
#define HPGL2_ARC_NOMINAL_CHAR_PROPORTION (0.54)

/* Text direction */
enum {
  RIGHT = 0,
  DOWN  = 1,
  LEFT  = 2,
  UP    = 3
} ;

/* Transparent data */
enum {
  NORMAL      = 0,
  TRANSPARENT = 1
} ;

/* Direction mode */
enum {
  DEFAULT  = 0,
  ABSOLUTE = 1,
  RELATIVE = 2
} ;


#define STICK (48)
#define ARC   (50)

/* Symbol set values allowed with Stick fonts.  The values are from
   Appendix C of the PCL 5 Comparison Guide. */
#define ASCII     (21)
#define ROMAN_8   (277)
#define ROMAN_EXT (5)

typedef uint16 Glyph ;

#define CAST_SIGNED_TO_GLYPH CAST_SIGNED_TO_UINT16

/* Some debug code for checking the bounding box. --johnk */
#undef DRAW_BOUNDING_BOX

#if defined(DEBUG_BUILD) && defined(DRAW_BOUNDING_BOX)
Bool draw_bounding_box(sbbox_t bbox)
{
  uint8 buffer[544] ;
  HPGL2Real current_point[2] ;
  Bool status ;

  if ( !gs_currentpoint(&gstateptr->thepath, &current_point[0], &current_point[1]) )
    return FALSE ;

  swncopyf(buffer, sizeof(buffer), (uint8*)
           "10 setlinewidth "
           "newpath "
           "%lf %lf moveto "
           "%lf %lf lineto "
           "%lf %lf lineto "
           "%lf %lf lineto "
           "closepath "
           "stroke ",

           bbox.x1, bbox.y1,
           bbox.x2, bbox.y1,
           bbox.x2, bbox.y2,
           bbox.x1, bbox.y2) ;

  status = run_ps_string(buffer) ;

  if ( !gs_moveto(TRUE /* absolute */, current_point, &gstateptr->thepath) )
    return FALSE ;

  return status ;
}
#endif /* DEBUG_BUILD */

/* Function predefines */
static Bool glyph_bbox(PCL5Context *pcl5_ctxt, Glyph glyph, sbbox_t *bbox) ;


static void hpgl2_default_font_sel_info(FontSelInfo* font_sel_info)
{
  font_sel_info->symbol_set = ROMAN_8 ;
  font_sel_info->spacing = 0 ;
  font_sel_info->pitch = DEFAULT_STICK_PITCH ;
  font_sel_info->height = 11.5 ;
  font_sel_info->style = 0 ;
  font_sel_info->stroke_weight = 0 ;
  font_sel_info->typeface = STICK ;
  font_sel_info->exclude_bitmap = 0 ;
}

/* Setup a default stick or arc font */
static void hpgl2_set_default_font(HPGL2CharacterInfo *char_info, int32 which_font)
{
  FontInfo *font_info = &char_info->font_info ;
  FontSelInfo* font_sel_info = get_font_sel_info(font_info, which_font) ;
  Font *font = get_font(font_info, which_font) ;
  Font default_font = { 0 } ;

  *font = default_font ; /* hmi et al are unused */

  /* Font spacing takes priority over typeface. */
  if ( font_sel_info->spacing == 0 ) {
    /* fixed spacing => Stick font */
    font->name[0] = 'S' ;
    font->name[1] = 't' ;
    font->name[2] = 'i' ;
    font->name[3] = 'c' ;
    font->name[4] = 'k' ;
    font->name_length = 5 ;
    if ( font_sel_info->pitch > EPSILON )
      font->size = STICK_FONT_SIZE_FROM_CPI(font_sel_info->pitch) ;
    else
      font->size = MAXINT32 ;
    /* Ascii and Roman Extension symbol sets are converted to Roman 8 in next_label. */
    font->symbolset_type = 1 ;
    font->spacing = 0 ;
    font->bitmapped = FALSE ;
    font->is_stick_font = TRUE ;
  } else {
    /* proportional spacing => Arc font */
    font->name[0] = 'A' ;
    font->name[1] = 'r' ;
    font->name[2] = 'c' ;
    font->name_length = 3 ;
    font->size = font_sel_info->height ;
    /* Ascii and Roman Extension symbol sets are converted to Roman 8 in next_label. */
    font->symbolset_type = 1 ;
    font->spacing = 1 ;
    font->bitmapped = FALSE ;
    font->is_stick_font = TRUE ;
  }

  font->valid = TRUE ;
  font->stability = UNSTABLE ;
  font->ps_font_validity = PS_FONT_INVALID ;
}


/* See header for doc. */
void default_HPGL2_character_info(HPGL2CharacterInfo* char_info)
{
  default_font_info(&char_info->font_info) ;
  hpgl2_default_font_sel_info(&char_info->font_info.primary) ;
  hpgl2_default_font_sel_info(&char_info->font_info.secondary) ;
  hpgl2_set_default_font(char_info, PRIMARY) ;
  hpgl2_set_default_font(char_info, SECONDARY) ;

  char_info->font_info.scaling_x =
    char_info->font_info.scaling_y = HPGL2_POINT_TO_PLOTTER_UNITS ;

  char_info->label_terminator = ETX ;
  char_info->label_terminator_mode = 1 ;
  char_info->label_origin = 1 ;
  char_info->text_direction = RIGHT ;
  char_info->linefeed_direction = 0 ;
  char_info->transparent_data_mode = NORMAL ;
  char_info->direction_mode = DEFAULT ;
  char_info->char_size_mode = DEFAULT ;
  char_info->scalable_bitmap = 0 ;
  char_info->fill_mode = 0 ;
  char_info->edge_pen = 0 ;
  char_info->edge_pen_specified = TRUE ;
  char_info->extra_space_width = 0 ;
  char_info->extra_space_height = 0 ;
  char_info->direction_run = 1 ;
  char_info->direction_rise = 0 ;
  char_info->char_size_width = 0 ;
  char_info->char_size_height = 0 ;
  char_info->slant_angle = 0 ;
  char_info->degenerate_matrices = FALSE ;
  char_info->glyphs_matrix = identity_matrix ;
  char_info->advance_matrix = identity_matrix ;
  char_info->lineend_matrix = identity_matrix ;
  char_info->lineend_inverse = identity_matrix ;
  char_info->left_margin[0] = 0 ;
  char_info->left_margin[1] = 0 ;
  char_info->label_mode = 0;
  char_info->label_row = 0;
}

static
void do_SB1(PCL5Context *pcl5_ctxt)
{
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  char_info->scalable_bitmap = 1 ;

  set_exclude_bitmap(pcl5_ctxt, &char_info->font_info, 0 /* exclude_bitmap */, PRIMARY) ;
  set_exclude_bitmap(pcl5_ctxt, &char_info->font_info, 0 /* exclude_bitmap */, SECONDARY) ;
}

static
void do_SB0(PCL5Context *pcl5_ctxt)
{
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  char_info->scalable_bitmap = 0 ;
  set_exclude_bitmap(pcl5_ctxt, &char_info->font_info, 1 /* exclude_bitmap */, PRIMARY) ;
  set_exclude_bitmap(pcl5_ctxt, &char_info->font_info, 1 /* exclude_bitmap */, SECONDARY) ;
}

void hpgl2_set_default_character_info(HPGL2CharacterInfo *char_info,
                                      Bool initialize)
{
  UNUSED_PARAM(Bool, initialize);
  default_HPGL2_character_info(char_info);
  return;
}

void hpgl2_sync_character_info(HPGL2CharacterInfo *char_info, Bool initialize)
{
  UNUSED_PARAM(HPGL2CharacterInfo*, char_info);
  UNUSED_PARAM(Bool, initialize);
  return;
}

/* Set the HPGL2 and PS fonts. */
Bool hpgl2_set_font(PCL5Context *pcl5_ctxt, FontInfo *font_info)
{
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  FontSelInfo* font_sel_info = get_font_sel_info(font_info, font_info->active_font) ;
  Font* font = get_font(font_info, font_info->active_font) ;

  if (font->valid) {
    if (!font->is_stick_font)
      return set_fontselection(pcl5_ctxt, font_info) ;  /* Set the PS font */
    else {
      set_ps_font_match(pcl5_ctxt, font_info) ;
      return TRUE ;
    }
  }

  /* This is most certainly an incorrect hack but Stick must be
     considered when selecting a font via criteria from HPGL2 along
     with all the other fonts. This work has yet to be done. See
     request 63846. Because weight has a higher scoring than typeface,
     ignore typeface if weight is greater than 3 - which seems to work
     OK for QL FST 5c 1530 & 1730. */
  if ( (font_sel_info->stroke_weight < 4 &&
        font_sel_info->typeface != STICK &&
        font_sel_info->typeface != ARC) ||

       /* Looking through the printer internal font list, Albertus is
          the only font which has a weight > 3. As it happens its 4 so
          special case this. Again this is a hack which will work
          99.9% of the time but is not correct as we should be scoring
          the Stick font along with all others. */
       (font_sel_info->typeface == 4362 /* Albertus */ &&
        font_sel_info->stroke_weight == 4) ) {
    return set_fontselection(pcl5_ctxt, font_info) ;
  }

  /* Italic posture may cause a switch to a different font.  On an HP printer,
     Stick upright and Stick alternate italic come out the same.  Stick italic,
     Arc italic and Arc alternate italic switch to a different font. */
  if ( (font_sel_info->spacing == 0 && font_sel_info->style == 1) ||
       (font_sel_info->spacing == 1 && font_sel_info->style != 0) ) {
    return set_fontselection(pcl5_ctxt, font_info) ;
  }

  /* Only these three symbol sets are allowed for Stick fonts.  If any
     other symbol set is specified switch to a different font. */
  if ( font_sel_info->symbol_set != ASCII &&
       font_sel_info->symbol_set != ROMAN_8 &&
       font_sel_info->symbol_set != ROMAN_EXT &&
       font_sel_info->symbol_set != 522 ) {
    return set_fontselection(pcl5_ctxt, font_info) ;
  }

  hpgl2_set_default_font(char_info, font_info->active_font) ;
  font->stability = STABLE_BY_CRITERIA ;
  set_ps_font_match(pcl5_ctxt, font_info) ;

  return TRUE ;
}


/* Round number of HPGL2 plotter units to the nearest (PCL5) PCL Unit.
 * N.B. This function rounds a negative number of HPGL2 plotter units to
 * the negative of the number that the same positive number of units would
 * be rounded to.
 */
static HPGL2Real round_hpgl2_plotter_units_to_pcl5_unit(PCL5Context *pcl5_ctxt,
                                                        HPGL2Real plotter_units)
{
  HPGL2Real pcl_internal_units ;
  int32 rounded_pcl_internal_units ;
  HPGL2Real rounded_plotter_units ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  pcl_internal_units = plotter_units * HPGL2_PLOTTER_UNIT_TO_PCL5_INTERNAL_UNITS ;
  rounded_pcl_internal_units = round_pcl_internal_to_pcl_unit(pcl5_ctxt, (PCL5Real) pcl_internal_units) ;
  rounded_plotter_units = rounded_pcl_internal_units / HPGL2_PLOTTER_UNIT_TO_PCL5_INTERNAL_UNITS ;

  return rounded_plotter_units ;
}

static Bool hpgl2_notdef(char_selector_t *selector,
                         int32 type, int32 char_count,
                         FVECTOR *advance, void *data)
{
  char_selector_t selector_copy ;

  UNUSED_PARAM(void *, data) ;

  HQASSERT(selector, "No char selector for PS notdef character") ;
  /* Note: cid > 0 in this assertion, because we shouldn't be notdef mapping
     the notdef cid (value 0) */
  HQASSERT(selector->cid > 0, "HPGL2 notdef char selector is not a defined CID") ;

  selector_copy = *selector ;

  /* No CMap lookup for notdef. Use CID 0 (notdef) in current font instead */
  selector_copy.cid = 0 ;

  return plotchar(&selector_copy, type, char_count, NULL, NULL, advance, CHAR_NORMAL) ;
}

/* Unlike a normal plotchar, hpgl2_plotchar returns the advance in user space.
   apply_font_matrix is for Stick fonts which must not combine the font matrix
   into the CTM or line widths and line caps will be affected.
 */
/** \todo Could the rounding to PCL Units take place here instead of in the
 *  various calling functions?
 */
static Bool hpgl2_plotchar(PCL5Context *pcl5_ctxt, HPGL2CharacterInfo *char_info,
                           Glyph glyph, Bool stroke, int32 showtype, Bool hatchfill,
                           Bool apply_font_matrix, FVECTOR *advance /* user space */)
{
  Bool result = FALSE ;
  Font* font = get_font(&char_info->font_info, char_info->font_info.active_font) ;

  HQASSERT(ps_font_matches_valid_pcl(&char_info->font_info),
           "Expected valid HPGL and PS fonts") ;

  /* Hatched fills for characters require special consideration. Currently
     we must fill the character path manually as hatched fills are not
     available to the rip directly. */
  if ( hatchfill )
    showtype = DOCHARPATH ;

  /* This should not be needed for a normal plotchar as plotchar will do it
   * itself.
   * \todo check whether it is needed for stickarc_plotchar
   */
  if (font->is_stick_font)
    if (! DEVICE_SETG(pcl5_ctxt->corecontext->page, GSC_FILL , DEVICE_SETG_NORMAL))
      return FALSE ;

  if ( showtype == DOCHARPATH ) {
    if ( !init_charpath(stroke) )
      return FALSE ;
  }


#define return DO_NOT_RETURN_goto_cleanup_INSTEAD!

  if ( font->is_stick_font ) {
    Font* font = get_font(&char_info->font_info, char_info->font_info.active_font) ;

    HQASSERT(font->valid, "Invalid font");

    if ( !stickarc_plotchar(glyph,
                            (apply_font_matrix ? &char_info->glyphs_matrix : &identity_matrix),
                            font->spacing == 0, showtype,
                            font->size * HPGL2_POINT_TO_PLOTTER_UNITS, advance) )
      goto cleanup ;
  } else {
    char_selector_t char_selector = { 0 } ;

    char_selector.index = char_selector.cid = glyph ;
    Copy(&char_selector.font, &theMyFont(theFontInfo(*gstateptr))) ;

    /* \todo Check wether we could use CHAR_NO_SETG */
    if ( !plotchar(&char_selector, showtype, 1, hpgl2_notdef, NULL, advance,
                   CHAR_NORMAL) )
      goto cleanup ;
  }

  /* Check inverse ctm */
  SET_SINV_SMATRIX(&thegsPageCTM(*gstateptr), NEWCTM_ALLCOMPONENTS) ;
  if ( SINV_NOTSET(NEWCTM_ALLCOMPONENTS) )
    goto cleanup ;

  /* Back to user space */
  MATRIX_TRANSFORM_DXY(advance->x, advance->y, advance->x, advance->y,
                       &sinv) ;
  if ( apply_font_matrix )
    MATRIX_TRANSFORM_DXY(advance->x, advance->y, advance->x, advance->y,
                         &char_info->glyphs_inverse) ;

  if ( hatchfill ) {
    if ( !hpgl2_hatchfill_path(pcl5_ctxt, &gstateptr->thepath,
                               EOFILL_TYPE, FALSE) )
      goto cleanup ;
  }

  result = TRUE ;
 cleanup :
  if ( showtype == DOCHARPATH )
    result = end_charpath(result) ;

#undef return
  return result ;
}

static Bool parse_font_definition_pair(PCL5Context *pcl5_ctxt, int32 which_font)
{
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  FontInfo *font_info = &char_info->font_info ;
  int32 kind ;
  HPGL2Real value ;

  if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &kind) <= 0 )
    return FALSE ;

  if ( hpgl2_scan_separator(pcl5_ctxt) <= 0 )
    return FALSE ;

  if ( hpgl2_scan_clamped_real(pcl5_ctxt, &value) <= 0 )
    return FALSE ;

  switch ( kind ) {
  case 1 : /* Symbol Set */ {
    int32 symbolset = (int32)value ;

    set_symbolset(pcl5_ctxt, font_info, symbolset, which_font) ;
    break ;
   }
  case 2 : /* Font Spacing */ {
    int32 spacing = (int32)value ;

    if ( spacing != 0 && spacing != 1 )
      return FALSE ;

    set_spacing(pcl5_ctxt, font_info, spacing, which_font) ;
    break ;
   }
  case 3 : /* Pitch */
    if ( value < 0 || value > 32767.9999 )
      return FALSE ;

    set_pitch(pcl5_ctxt, font_info, value, which_font) ;
    break ;

  case 4 : /* Height */
    if ( value < 0 || value > 32767.9999 )
      return FALSE ;

    set_height(pcl5_ctxt, font_info, value, which_font) ;
    break ;

  case 5 : /* Posture */ {
    int32 style = (int32)value ;

    if ( style < 0 || style > 2 )
      return FALSE ;

    set_style(pcl5_ctxt, font_info, style, which_font) ;
    break ;
   }
  case 6 : /* Stroke Weight */ {
    int32 weight = (int32)value ;

    if ( weight < -7 || (weight > 7 && weight != 9999) )
      return FALSE ;

    set_weight(pcl5_ctxt, font_info, weight, which_font) ;
    break ;
   }
  case 7 : /* Typeface */ {
    int32 typeface = (int32)value ;

    set_typeface(pcl5_ctxt, font_info, typeface, which_font) ;
    break ;
   }
  default :
    /* Bad 'kind', ignore */
    return FALSE ;
  }

  return TRUE ;
}


/* Find out a typical bounding box for this font */
/** \todo
 * 1) Find out exact requirements for these sizes, e.g. should the horizontal
 *    size be the whole size of the character advance, or strictly the bounding
 *    box of a 'typical' glyph?  Or, do we need different values for different
 *    callers?  Should this depend on horizontal vs vertical text, or text
 *    direction?
 *
 * 2) Store these values somewhere, (e.g. in the font), so that we don't keep
 *    calling plotchar to find out the same information.  How do they relate to
 *    e.g. font hmi, font height?
 *
 * 3) So far this is just for stick fonts.  Extend it to other font types, e.g.
 *    for bitmap soft fonts the PCL Tech Ref, p11-35 suggests a default cap
 *    height of 0.7087 * Em of the font.  For scalable soft fonts, cap height
 *    data should be provided, (as it may be for bitmap fonts).
 *
 * 4) At least the x-values should almost certainly be rounded to nearest PCL
 *    unit, (unless we can change hpgl2_plotchar to do this), though stick
 *    fonts are an exception, (meaning the advances are not rounded, so it
 *    doesn't seem sensible to round the values here either).
 */
static Bool characteristic_bbox(PCL5Context *pcl5_ctxt, sbbox_t *bbox)
{
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  HPGL2Real currentpos[2] ;
  Font *font ;

  /* Ensure the HPGL and PS fonts are valid */
  if (! ps_font_matches_valid_pcl(&char_info->font_info)) {
    /* One or other of these is invalid */
    if (!hpgl2_set_font(pcl5_ctxt, &char_info->font_info))
      return FALSE ;
  }

  font = get_font(&char_info->font_info, char_info->font_info.active_font) ;

  /** \todo Remove this when we can do something better */
  if ( !font->is_stick_font || ! is_fixed_spacing(font))
    return glyph_bbox(pcl5_ctxt, 'M', bbox) ;

  if ( !gs_currentpoint(&gstateptr->thepath, &currentpos[0], &currentpos[1]) )
    return FALSE ;

  /* This calculates the width and height of the characteristic character body,
   * corresponding to the 32*32 grid, (i.e. not the 48 grid unit advance).
   */
  bbox->x1 = currentpos[0] ;
  bbox->y1 = currentpos[1] ;
  bbox->x2 = bbox->x1 + STICK_CHARACTER_BODY_WIDTH(HPGL2_POINT_TO_PLOTTER_UNITS * font->size) ;
  bbox->y2 = bbox->y1 + STICK_CHARACTER_BODY_HEIGHT(HPGL2_POINT_TO_PLOTTER_UNITS * font->size) ;

  return TRUE ;
}


/* Get a bounding box for this glyph */
/** \todo 
 * 1) Find out exact requirements for these sizes, e.g. should the horizontal
 *    size be the whole size of the character advance, or strictly the bounding
 *    box of the glyph?  If we could use DOSTRINGWIDTH rather than DOCHARPATH,
 *    this would eliminate probably unwanted variation due to the exact number
 *    of pixels involved varying with currentpoint.  Or do we need different
 *    values for different callers? Or for horizontal vs vertical text, or text
 *    direction?
 *
 * 2) Are we calling this when we really only want e.g. the width for
 *    this glyph, but could use the characteristic_bbox height?
 *
 * 3) At least the x-values should almost certainly be rounded to nearest PCL
 *    unit, (unless we can change hpgl2_plotchar to do this).
 */
static Bool glyph_bbox(PCL5Context *pcl5_ctxt, Glyph glyph, sbbox_t *bbox)
{
  Bool result = FALSE ;
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  Font *font ;
  USERVALUE linewidth = gstateptr->thestyle.linewidth ;
  HPGL2Real currentpos[2] ;
  FVECTOR advance ;
  uint32 i ;
  SYSTEMVALUE *bbindex ;

  /* Ensure the HPGL and PS fonts are valid */
  if (! ps_font_matches_valid_pcl(&char_info->font_info)) {
    /* One or other of these is invalid */
    if (!hpgl2_set_font(pcl5_ctxt, &char_info->font_info))
      return FALSE ;
  }

  font = get_font(&char_info->font_info, char_info->font_info.active_font) ;

  /* N.B. The characteristic bbox is so far only implemented for Stick fonts
   *      and calculates a character size rather than an advance.  However, it
   *      has not been established that this is what all callers of glyph_bbox
   *      (and characteristic_bbox) actually require.
   */
  if ( font->is_stick_font && is_fixed_spacing(font) )
    return characteristic_bbox(pcl5_ctxt, bbox) ;

  if ( !gs_currentpoint(&gstateptr->thepath, &currentpos[0], &currentpos[1]) )
    return FALSE ;

  /* Glyph bbox is invariant to pen width. */
  gstateptr->thestyle.linewidth = 0 ;

#define return DO_NOT_RETURN_goto_cleanup_INSTEAD!

  if ( !hpgl2_plotchar(pcl5_ctxt, char_info, glyph, FALSE /* stroke */,
                       DOCHARPATH, FALSE /* hatchfill */,
                       FALSE /* apply_font_matrix */, &advance) )
    goto cleanup ;

  if ( !pathbbox_(pcl5_ctxt->corecontext->pscontext) )
    goto cleanup ;

  bbox_as_indexed(bbindex, bbox) ;
  for ( i = 0 ; i < 4 ; ++i ) {
    OBJECT *obj = TopStack(operandstack, operandstack.size) ;
    if ( !object_get_numeric(obj, &bbindex[3 - i]) )
      goto cleanup ;
    pop(&operandstack) ;
  }

  if ( !gs_newpath() )
    goto cleanup ;

  result = TRUE ;
 cleanup :
  /* Reinstate current point. */
  if ( !gs_moveto(TRUE /* absolute */, currentpos, &gstateptr->thepath) )
    result = FALSE ;
  gstateptr->thestyle.linewidth = linewidth ;

#undef return
  return result ;
}

static Bool glyphs_stringwidth(PCL5Context *pcl5_ctxt, Glyph *glyphs, uint32 num_glyphs,
                               HPGL2Real *width, HPGL2Real *height) ;

static Bool glyph_stringwidth_BS_HT(PCL5Context *pcl5_ctxt, Glyph glyph,
                                    HPGL2Real *width, FVECTOR *advance)
{
  Glyph spaceglyph = SPACE ;
  HPGL2Real current_point_x = (width ? *width : 0) ;
  HPGL2Real spacewidth ;

  if ( !glyphs_stringwidth(pcl5_ctxt, &spaceglyph, 1, &spacewidth, &advance->y) )
    return FALSE ;

  if ( glyph == BS ) {
    /* Move one column left unless at left margin, in which case no action is taken. */
    advance->x = 0 ;
    if ( current_point_x > spacewidth )
      advance->x -= spacewidth ;
  } else {
    /* Move to the next horizontal tab stop.  The tab stops are at the left margin,
       and every eight columns to the right of the left margin. */
    if ( current_point_x < 0 ) {
      advance->x = -current_point_x ;
    } else {
      HPGL2Real tabwidth = 8 * spacewidth ;
      HPGL2Real num_tabs = (current_point_x / tabwidth) + EPSILON ;
      advance->x = ((int32)num_tabs + 1) * tabwidth - current_point_x ;
    }
  }

  return TRUE ;
}

static Bool glyphs_stringwidth(PCL5Context *pcl5_ctxt, Glyph *glyphs, uint32 num_glyphs,
                               HPGL2Real *width, HPGL2Real *height)
{
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  Font *font ;
  FontSelInfo *dummy ;
  HPGL2Real plotter_units = 0, rounded_hmi = 0 ;

  uint32 i ;

  if ( width ) *width = 0 ;
  if ( height ) *height = 0 ;

  /* Ensure the HPGL and PS fonts are valid */
  if ( !ps_font_matches_valid_pcl(&char_info->font_info)) {
    /* One or other of these is invalid */
    if ( !hpgl2_set_font(pcl5_ctxt, &char_info->font_info))
      return FALSE ;
  }

  /* Find the hmi rounded to the nearest PCL5 unit */
  if ( !get_active_font(pcl5_ctxt, &(char_info->font_info), &font, &dummy) )
    return FALSE ;

  HQASSERT(font != NULL, "font is NULL") ;

  /* N.B. Stick font advance is not rounded to PCL5 Units */
  /** \todo By analogy with stick fonts we assume that the advance
   *        for arc fonts is not rounded, but we should check this.
   */
  if ( !font->is_stick_font) {
    plotter_units = (HPGL2Real) (font->hmi * HPGL2_POINT_TO_PLOTTER_UNITS) ;
    rounded_hmi = round_hpgl2_plotter_units_to_pcl5_unit(pcl5_ctxt, plotter_units) ;
  }

  for ( i = 0 ; i < num_glyphs ; ++i ) {
    Glyph glyph = glyphs[i] ;
    FVECTOR advance ;

    switch ( glyph ) {
    case HT :
    case BS :
      if ( !glyph_stringwidth_BS_HT(pcl5_ctxt, glyph, width, &advance) )
        return FALSE ;
      break ;
    default :
      if ( !hpgl2_plotchar(pcl5_ctxt, char_info, glyph, FALSE /* stroke */,
                           DOSTRINGWIDTH, FALSE /* hatchfill */,
                           FALSE /* apply_font_matrix */, &advance) )
        return FALSE ;

      /* N.B. Stick font advance is not rounded to PCL5 Units */
      /** \todo By analogy with stick fonts we assume that the advance
       *        for arc fonts is not rounded, but we should check this.
       */
      if ( !font->is_stick_font) {
        if ( !font->spacing && font->hmi ) {
          /** \todo Should this just do the same as below? */
          advance.x = rounded_hmi ;
        }
        else {
          /* Round to nearest PCL5 Unit */
          advance.x = round_hpgl2_plotter_units_to_pcl5_unit(pcl5_ctxt, advance.x) ;
        }

        /* Round to nearest PCL5 Unit */
        advance.y = round_hpgl2_plotter_units_to_pcl5_unit(pcl5_ctxt, advance.y) ;
      }
      break ;
    }

    /** \todo Should rounding be postponed until after extra space and/or repeated ? */
    if ( width ) *width += advance.x + (char_info->extra_space_width * advance.x) ;
    if ( height ) *height += advance.y + (char_info->extra_space_height * advance.y) ;
  }

  return TRUE ;
}

/* Get the character advance for vertical text.
 * N.B.   Due to the factors which have had to be added to the calculation
 *        for the true linefeed below, this is no longer the same thing.
 * N.N.B. It is up to the caller to ensure that this value is only used
 *        in the case of vertical text.
 */
/** \todo Review this function and its use. */
static HPGL2Real get_vertical_char_advance(HPGL2CharacterInfo *char_info)
{
  Font *font = get_font(&char_info->font_info, char_info->font_info.active_font) ;
  HPGL2Real pointsize = font->size * HPGL2_POINT_TO_PLOTTER_UNITS ;

  HQASSERT(font->valid, "Invalid font");

  /* Something else not mentioned in the spec is that apparently the
     advance is related to pointsize when going up or downwards */
  if ( font->is_stick_font ) {
    if ( is_fixed_spacing(font) )
      return STICK_CHARACTER_HEIGHT(pointsize) ;
    else
      /** \todo This is an arbitrary factor which looks close - needs more investigation. */
      return 0.95 * pointsize ;
  }
  else {
    /** \todo This is an arbitrary factor which looks close - needs more investigation. */
    return 0.9 * pointsize ;
  }
}


static HPGL2Real get_linefeed(HPGL2CharacterInfo *char_info)
{
  Font *font = get_font(&char_info->font_info, char_info->font_info.active_font) ;
  HPGL2Real pointsize = font->size * HPGL2_POINT_TO_PLOTTER_UNITS ;

  HQASSERT(font->valid, "Invalid font");

  switch ( char_info->text_direction ) {
  default :
  case RIGHT :
  case LEFT :
    if ( font->is_stick_font ) {
      if ( is_fixed_spacing(font) )
        /* The linefeed is 64 units where the grid height is 48 */
        return ((4.0 / 3.0) * STICK_CHARACTER_HEIGHT(pointsize)) ;
      else
        /** \todo Should this be along similar lines to above? */
        return 1.33 * pointsize ;
    } else {
      return 1.2 * pointsize ;
    }
  case DOWN :
  case UP :
    /* Something else not mentioned in the spec is that apparently
       linefeed is pointsize when going up or downwards */
    if ( font->is_stick_font ) {
      if ( is_fixed_spacing(font) )
        /* The linefeed is 64 units where the grid width is 48 */
        return ((4.0 / 3.0) * STICK_CHARACTER_ADVANCE(pointsize)) ;
      else
        /** \todo Should this be similar to stick font above? */
        return pointsize ;
    }
    else
      return pointsize ;    /** \todo Confirm this is correct */
  }
}

static Bool calc_width_and_height(PCL5Context *pcl5_ctxt,
                                  HPGL2CharacterInfo *char_info,
                                  Glyph *glyphs, uint32 num_glyphs,
                                  HPGL2Real *width, HPGL2Real *height)
{
  sbbox_t bbox ;

  switch ( char_info->text_direction ) {
  default :
  case RIGHT :
  case LEFT :
    /* Find the width of the glyphs (i.e. the sum of the advances). */
    if ( !glyphs_stringwidth(pcl5_ctxt, glyphs, num_glyphs, width, height) )
      return FALSE ;

    /* Use cap height rather than the height of chars actually drawn,
       otherwise consecutive labels of differing chars may misalign. */
    if ( !characteristic_bbox(pcl5_ctxt, &bbox) )
      return FALSE ;
    *height = bbox.y2 - bbox.y1 ;
    break ;
  case DOWN :
  case UP : {
    HPGL2Real vertical_advance = get_vertical_char_advance(char_info) ;
    Bool take_widest = TRUE ;

    /* Take width from the first glyph if it supposed to abut the pen position,
       otherwise just take width from widest character.  It seems complicated,
       but this what the reference printers appear to do. */
    switch ( char_info->label_origin ) {
    default :
    case 1 : case 4 : case 7 : case 11 : case 14 : case 17 :
      if ( char_info->text_direction == UP ) {
        if ( !glyph_bbox(pcl5_ctxt, glyphs[0], &bbox) )
          return FALSE ;
        *width = bbox.x2 - bbox.x1 ;
        take_widest = FALSE ;
      }
      break ;
    case 3 : case 6 : case 9 : case 13 : case 16 : case 19 :
      if ( char_info->text_direction == DOWN ) {
        if ( !glyph_bbox(pcl5_ctxt, glyphs[0], &bbox) )
          return FALSE ;
        *width = bbox.x2 - bbox.x1 ;
        take_widest = FALSE ;
      }
      break ;
    }

    if ( take_widest ) {
      /* Find the widest glyph. */
      uint32 i ;
      *width = 0 ;
      for ( i = 0 ; i < num_glyphs ; ++i ) {
        HPGL2Real new_width ;

        if ( !glyph_bbox(pcl5_ctxt, glyphs[i], &bbox) )
          return FALSE ;

#if defined(DEBUG_BUILD) && defined(DRAW_BOUNDING_BOX)
        (void)draw_bounding_box(bbox) ;
#endif

        new_width = bbox.x2 - bbox.x1 ;
        if ( new_width > *width )
          *width = new_width ;
      }
    }

    /* Use cap height rather than the height of chars actually drawn,
       otherwise consecutive labels of differing chars may misalign. */
    if ( !characteristic_bbox(pcl5_ctxt, &bbox) )
      return FALSE ;
    *height = bbox.y2 - bbox.y1 ;

    *height += (num_glyphs - 1) * vertical_advance * (1 + char_info->extra_space_width) ;
    break ;
   }
  }

  return TRUE ;
}

/* To make the label abut the pen position we need to know the left and right
   bearings. */
static Bool calc_bearings(PCL5Context *pcl5_ctxt, HPGL2Real *origin, Glyph glyph,
                          HPGL2Real *left_bearing, HPGL2Real *right_bearing)
{
  sbbox_t bbox ;

  if ( left_bearing ) *left_bearing = 0 ;
  if ( right_bearing ) *right_bearing = 0 ;

  /* Left and right bearings are always zero for SPACE. */
  if ( glyph == SPACE )
    return TRUE ;

  if ( !glyph_bbox(pcl5_ctxt, glyph, &bbox) )
    return FALSE ;

  if ( left_bearing )
    *left_bearing = bbox.x1 - origin[0] ;

  if ( right_bearing ) {
    HPGL2Real advance ;

    if ( !glyphs_stringwidth(pcl5_ctxt, &glyph, 1, &advance, NULL) )
      return FALSE ;

    *right_bearing = origin[0] + advance - bbox.x2 ;
  }

  return TRUE ;
}

static Bool start_position(PCL5Context *pcl5_ctxt,
                           Glyph *glyphs, uint32 num_glyphs,
                           HPGL2Real *reset)
{
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  Font *font = get_font(&char_info->font_info, char_info->font_info.active_font) ;
  HPGL2Real vertical_advance = get_vertical_char_advance(char_info) ;
  HPGL2Real offset, width, height, start[2], dx, dy ;
  HPGL2Real left_bearing = 0, right_bearing = 0 ;

  HQASSERT(font->valid, "Invalid font");

  if ( font->is_stick_font ) {
    if ( is_fixed_spacing( font ))
      /* For stick fonts, X and Y offsets appear to be based on the horizontal
       * advance, for both horizontal and vertical text paths.  It is supposed
       * to be 16 grid units compared with the total width of 48 units.
       */
      offset = STICK_CHARACTER_ADVANCE(font->size * HPGL2_POINT_TO_PLOTTER_UNITS) * (1.0 / 3.0) ;
    else
      /** \todo Find out whether this should be more similar to the above */
      offset = font->size * HPGL2_POINT_TO_PLOTTER_UNITS * 0.25 ;   /* (1.0 / 3.0) */
  }
  else
    offset = font->size * HPGL2_POINT_TO_PLOTTER_UNITS * 0.25 ;

  if ( !calc_width_and_height(pcl5_ctxt, char_info, glyphs, num_glyphs, &width, &height) )
    return FALSE ;

  if ( !gs_currentpoint(&gstateptr->thepath, &reset[0], &reset[1]) )
    return FALSE ;

  /* Calculate bearings to make sure the label abuts the current pen position. */
  switch ( char_info->text_direction ) {
  case RIGHT :
    if ( !calc_bearings(pcl5_ctxt, reset, glyphs[0], &left_bearing, NULL) ||
         !calc_bearings(pcl5_ctxt, reset, glyphs[num_glyphs-1], NULL, &right_bearing) )
      return FALSE ;
    break ;
  case LEFT :
    if ( !calc_bearings(pcl5_ctxt, reset, glyphs[num_glyphs-1], &left_bearing, NULL) ||
         !calc_bearings(pcl5_ctxt, reset, glyphs[0], NULL, &right_bearing) )
      return FALSE ;
    break ;
  }

  /* Determine pen position after label based on continuation conditions. */
  dx = dy = 0 ;
  switch ( char_info->text_direction ) {
  case RIGHT :
    switch ( char_info->label_origin ) {
    case 1 : case 2 : case 3 : case 11 : case 12 : case 13 : case 21 :
      dx += width ;
      break ;
    }
    break ;
  case DOWN :
    switch ( char_info->label_origin ) {
    case 3 : case 6 : case 9 : case 13 : case 16 : case 19 :
      /** \todo Should this be extra_space_height? */
      dy -= num_glyphs * vertical_advance * (1 + char_info->extra_space_width) ;
      break ;
    }
    break ;
  case LEFT :
    switch ( char_info->label_origin ) {
    case 7 : case 8 : case 9 : case 17 : case 18 : case 19 :
      dx -= width ;
      break ;
    }
    break ;
  case UP :
    switch ( char_info->label_origin ) {
    case 1 : case 4 : case 7 : case 11 : case 14 : case 17 : case 21 :
      /** \todo Should this be extra_space_height? */
      dy += num_glyphs * vertical_advance * (1 + char_info->extra_space_width) ;
      break ;
    }
    break ;
  }

  /* Transform the reset deltas by the lineend_matrix to include the effects of
     char size scaling, run/rise rotation, but to exclude the effects of
     slanting (which otherwise would screw-up the reset). */
  MATRIX_TRANSFORM_DXY(dx, dy, dx, dy, &char_info->lineend_matrix) ;
  reset[0] += dx ;
  reset[1] += dy ;

  start[0] = start[1] = 0 ;

  /* Start position adjustments specific to the direction. */
  switch ( char_info->text_direction ) {
  case RIGHT :
    break ;
  case LEFT :
    /* Since we're going leftwards we need to start further to the right. */
    start[0] += width ;
    break ;
  case DOWN :
    /* Since we're going downwards we need to start higher up. */
    /** \todo Should this be extra_space_height? */
    start[1] += (num_glyphs - 1) * vertical_advance * (1 + char_info->extra_space_width) ;
    /* fall through */
  case UP :
    /* Centre the glyphs on the widest glyph. */
    start[0] += width * 0.5 ;
    break ;
  }

  /* Start position adjustments specific to the label origin. */
  switch ( char_info->label_origin ) {
  case 11 :
    start[0] += offset ;
    start[1] += offset ;
    /* fall through */
  case 1 :
    start[0] -= left_bearing ;
    break ;

  case 12 :
    start[0] += offset ;
    /* fall through */
  case 2 :
    start[0] -= left_bearing ;
    start[1] -= height * 0.5 ;
    break ;

  case 13 :
    start[0] += offset ;
    start[1] -= offset ;
    /* fall through */
  case 3 :
    start[0] -= left_bearing ;
    start[1] -= height ;
    break ;

  case 14 :
    start[1] += offset ;
    /* fall through */
  case 4 :
    start[0] -= (width - right_bearing) * 0.5 ;
    break ;

  case 15 :
    /* fall through */
  case 5 :
    start[0] -= (width - right_bearing) * 0.5 ;
    start[1] -= height * 0.5 ;
    break ;

  case 16 :
    start[1] -= offset ;
    /* fall through */
  case 6 :
    start[0] -= (width - right_bearing) * 0.5 ;
    start[1] -= height ;
    break ;

  case 17 :
    start[0] -= offset ;
    start[1] += offset ;
    /* fall through */
  case 7 :
    start[0] -= width - right_bearing ;
    break ;

  case 18 :
    start[0] -= offset ;
    /* fall through */
  case 8 :
    start[0] -= width - right_bearing ;
    start[1] -= height * 0.5 ;
    break ;

  case 19 :
    start[0] -= offset ;
    start[1] -= offset ;
    /* fall through */
  case 9 :
    start[0] -= width - right_bearing ;
    start[1] -= height ;
    break ;

  case 21 : /* PCL-compatible label origin */

    /** \todo Should take account of PCL left and top offsets.  PCL Tech Ref,
       "Character Descriptor Formats", 11-51.  "The left and top offsets locate
       the character reference point about the cursor position (see Figure 11-6
       and Figure 11-7)." */
    break ;

  default :
    HQFAIL("Unexpected label origin") ;
  }

  /* Transform the start deltas by the lineend_matrix to include the effects of
     char size scaling, run/rise rotation, but to exclude the effects of
     slanting (which otherwise would screw-up the start). */
  MATRIX_TRANSFORM_DXY(start[0], start[1], start[0], start[1], &char_info->lineend_matrix) ;

  if ( !gs_moveto(FALSE /* absolute */, start, &gstateptr->thepath) )
    return FALSE ;

  /* Track the start position for the label, it is the 'left margin' used for
     backspace and horizontal tab handling. */
  if ( !gs_currentpoint(&gstateptr->thepath, &char_info->left_margin[0],
                        &char_info->left_margin[1]) )
    return FALSE ;

  return TRUE ;
}

/* pre_position is called before each glyph */
static Bool pre_position(PCL5Context *pcl5_ctxt, HPGL2CharacterInfo *char_info,
                         Glyph glyph, HPGL2Real *pre_advance)
{
  HPGL2Real advance[2] = { 0, 0 } ;
  sbbox_t bbox ;

  pre_advance[0] = pre_advance[1] = 0 ;

  switch ( char_info->text_direction ) {
  case RIGHT :
    break ;
  case LEFT :
    /* The normal plotchar advance has to happen in pre_position when going leftwards.
       We need to step backwards BEFORE plotting the character. */
    if ( !glyphs_stringwidth(pcl5_ctxt, &glyph, 1, &advance[0], NULL) )
      return FALSE ;

    /* Negate advance because we're going leftwards. */
    advance[0] = -advance[0] ;
    break ;
  case DOWN :
  case UP :
    /* Centre-justify the character. */
    if ( !glyph_bbox(pcl5_ctxt, glyph, &bbox) )
      return FALSE ;
    pre_advance[0] = -(bbox.x2 - bbox.x1) * 0.5 ;
    break ;
  }

  /* The pre_advance is to centre-justify chars and must be removed after the
     character is plotted.  The advance for leftwards is left in. */
  advance[0] += pre_advance[0] ;
  advance[1] += pre_advance[1] ;

  MATRIX_TRANSFORM_DXY(advance[0], advance[1], advance[0], advance[1], &char_info->advance_matrix) ;

  if ( !gs_moveto(FALSE /* absolute */, advance, &gstateptr->thepath) )
    return FALSE ;

  return TRUE ;
}

/* post_position is called after each glyph */
static Bool post_position(PCL5Context *pcl5_ctxt, HPGL2CharacterInfo *char_info,
                          HPGL2Real *pre_advance, Glyph glyph, FVECTOR *post_advance)
{
  HPGL2Real vertical_advance = get_vertical_char_advance(char_info) ;
  HPGL2Real advance[2] = { 0, 0 } ;
  Font *font ;
  HPGL2Real plotter_units ;

  UNUSED_PARAM(Glyph, glyph) ;

  switch ( char_info->text_direction ) {
  case RIGHT :
    font = get_font(&(char_info->font_info), char_info->font_info.active_font) ;
    HQASSERT(font != NULL, "font is NULL") ;
    HQASSERT(font->valid, "font is invalid") ;
    HQASSERT(font->ps_font_validity == PS_FONT_VALID, "PS font was not set") ;

    /* N.B. Stick font advance is not rounded to PCL5 Units */
    /** \todo By analogy with stick fonts we assume that the advance
     *        for arc fonts is not rounded, but we should check this.
     */
    if (! font->is_stick_font ) {
      if ( !font->spacing && font->hmi) {
        /** \todo Should this just do the same as below? */
        plotter_units = (HPGL2Real) (font->hmi * HPGL2_POINT_TO_PLOTTER_UNITS) ;
        advance[0] = round_hpgl2_plotter_units_to_pcl5_unit(pcl5_ctxt, plotter_units) ;
      }
      else {
        advance[0] = post_advance->x ;

        /* Round to nearest PCL5 Unit */
        advance[0] = round_hpgl2_plotter_units_to_pcl5_unit(pcl5_ctxt, advance[0]) ;
      }

      advance[1] = post_advance->y ;
      advance[1] = round_hpgl2_plotter_units_to_pcl5_unit(pcl5_ctxt, advance[1]) ;
    }
    else {
      advance[0] = post_advance->x ;
      advance[1] = post_advance->y ;
    }

    /** \todo Should rounding be postponed until after extra space and/or repeated ? */
    advance[0] += char_info->extra_space_width * advance[0] ;
    break ;
  case LEFT :
    /* Advance happened in pre_position when going leftwards. */
    break ;
  case DOWN :
    vertical_advance = -vertical_advance ;
    /* fall through */
  case UP :
    advance[0] = 0 ;
    /** \todo Should this be extra_space_height ? */
    advance[1] = vertical_advance * (1 + char_info->extra_space_width) ;
    break ;
  }

  /* Undo the pre_advance from pre_position, ready for the next char. */
  advance[0] -= pre_advance[0] ;
  advance[1] -= pre_advance[1] ;

  MATRIX_TRANSFORM_DXY(advance[0], advance[1], advance[0], advance[1], &char_info->advance_matrix) ;

  /** \todo Should the above rounding actually take place here instead? */

  if ( !gs_moveto(FALSE /* absolute */, advance, &gstateptr->thepath) )
    return FALSE ;

  return TRUE ;
}

static Bool calc_char_size_matrix(PCL5Context *pcl5_ctxt, HPGL2CharacterInfo *char_info,
                                  Bool bitmap_font_restrictions, OMATRIX *matrix)
{
  Font *font = get_font(&char_info->font_info, char_info->font_info.active_font) ;
  HPGL2Real width = char_info->char_size_width, height = char_info->char_size_height ;

  HQASSERT(font->valid, "Font is invalid") ;

  *matrix = identity_matrix ;

  switch ( char_info->char_size_mode ) {
  case RELATIVE : {
    HPGL2ConfigInfo *config_info = get_hpgl2_config_info(pcl5_ctxt) ;
    /* P2-P1, converted from percentage plotter units to centimetres */
    width *= 0.000025 * (config_info->scale_points.p2.x - config_info->scale_points.p1.x) ;
    height *= 0.000025 * (config_info->scale_points.p2.y - config_info->scale_points.p1.y) ;
   }
   /* fall through into absolute */
  case ABSOLUTE : {
    HPGL2Real orig_width, orig_height, sx, sy ;
    sbbox_t bbox ;

    if ( !characteristic_bbox(pcl5_ctxt, &bbox) )
      return FALSE ;

    /* We're scaling up the "nominal character". */
    orig_height = bbox.y2 - bbox.y1 ;
    if ( font->is_stick_font ) {
      if ( is_fixed_spacing(font) )
        orig_width = bbox.x2 - bbox.x1 ;
      else
        orig_width = HPGL2_ARC_NOMINAL_CHAR_PROPORTION * orig_height ;
    } else {
      orig_width = HPGL2_NOMINAL_CHAR_PROPORTION * orig_height ;
    }

    /* Centimetres to plotter units */
    width *= 400 ;
    height *= 400 ;

    if ( orig_width < EPSILON || orig_height < EPSILON )
      break ; /* div by zero, give up */

    sx = width / orig_width ;
    sy = height / orig_height ;

    if ( bitmap_font_restrictions ) {
      /* The spec states char size is "approximate" for bitmap fonts, but
         looking at the results from the HP printers this appears more like
         mirroring is ignored.  For bitmap fonts the spec states "character size
         is determined by height for proportional fonts and by width for
         fixed-spaced fonts". */

      /* PCL COMPATIBILITY For the HP4250 reference printer choosing width
         scaling only appears to happen for Stick fonts. */
      if ( font->spacing == 0 && font->is_stick_font ) {
        /* Choose width scaling for fixed-space fonts. */
        if ( sx < 0 ) sx = -sx ;
        sy = sx ; /* preserve bitmap char aspect ratio */
      }
      else {
        /* Choose height scaling for proportional fonts. */
        if ( sy < 0 ) sy = -sy ;
        sx = sy ; /* preserve bitmap char aspect ratio */
      }
    }

    matrix->matrix[0][0] = sx ;
    matrix->matrix[0][1] = 0 ;
    matrix->matrix[1][0] = 0 ;
    matrix->matrix[1][1] = sy ;
    matrix->matrix[2][0] = 0 ;
    matrix->matrix[2][1] = 0 ;
    MATRIX_SET_OPT_BOTH(matrix) ;
   }
  }

  return TRUE ;
}
/* Calculate matrices to handle char size, slant angle and direction.  Two
   matrices are calculated, one for glyphs and one for line ends.  The
   lineend_matrix excludes the transformation for slanting otherwise LF would
   be offset according to the slant. */
static Bool calc_font_matrices(PCL5Context *pcl5_ctxt, HPGL2CharacterInfo *char_info)
{
  OMATRIX translate ;
  HPGL2Real tx, ty, stickarc_italic_angle = 0 ;
  Bool bitmap_font_restrictions ;

  /* Ensure the HPGL and PS fonts are valid */
  if (! ps_font_matches_valid_pcl(&char_info->font_info)) {
    /* One or other of these is invalid */
    if (!hpgl2_set_font(pcl5_ctxt, &char_info->font_info))
      return FALSE ;
  }

  /* All the transformations are applied through the ctm because the alternative
     of using the font matrix would mean a new font every time a font is set.
     There is no way of passing the font matrix through to UFTS as a criteria,
     and so there would be potentially many duplicates in the font cache. */
  gs_setfontctm(&identity_matrix) ;

  /* Changing character size also changes the width of line used to draw Stick
     font characters. stick_pen_width_factor is derived from the scale
     matrix. */
  char_info->stick_pen_width_factor = 1 ;

  /* font matrix needs to be adjusted to reflect plot size scaling of HPGL, if
   * the HPGL does not use user units. */
  if ( char_info->char_size_mode == DEFAULT &&
       char_info->slant_angle == 0 &&
       char_info->direction_mode == DEFAULT &&
       stickarc_italic_angle == 0 ) {

    if ( hpgl2_is_scaled(pcl5_ctxt) )
    {
      char_info->glyphs_matrix = identity_matrix ;
      char_info->advance_matrix = identity_matrix ;
      char_info->lineend_matrix = identity_matrix ;
      char_info->glyphs_inverse = identity_matrix ;
      char_info->lineend_inverse = identity_matrix ;

      return TRUE;
    }
    else
    {
      OMATRIX plot_scale;

      plot_scale = identity_matrix;
      plot_scale.matrix[0][0] = horizontal_scale_factor(pcl5_ctxt);
      plot_scale.matrix[1][1] = vertical_scale_factor(pcl5_ctxt);

      char_info->glyphs_matrix = plot_scale ;
      char_info->advance_matrix = plot_scale ;
      char_info->lineend_matrix = plot_scale ;

      /* If the glyphs matrix cannot be inverted we need to ignore labels.
         Note we cannot use lost mode for this as that only deals with current
         pen position, not for things like bad char size values. */
      if ( matrix_inverse(&char_info->glyphs_matrix, &char_info->glyphs_inverse) &&
           matrix_inverse(&char_info->lineend_matrix, &char_info->lineend_inverse) )
        char_info->degenerate_matrices = FALSE ;
      else
        char_info->degenerate_matrices = TRUE ;
    }

    return TRUE ;
  }


  /* Translate to the origin. */

  if ( !gs_currentpoint(&gstateptr->thepath, &tx, &ty) )
    return FALSE ;

  /* Check we're at a remotely valid pen position. */
  if ( fabs(tx) > HPGL2_PEN_POSITION_LIMIT || fabs(ty) > HPGL2_PEN_POSITION_LIMIT ) {
    hpgl2_set_lost_mode(pcl5_ctxt, TRUE) ;
    return TRUE ;
  }

  translate.matrix[0][0] = 1.0 ;
  translate.matrix[0][1] = 0 ;
  translate.matrix[1][0] = 0 ;
  translate.matrix[1][1] = 1.0 ;
  translate.matrix[2][0] = -tx ;
  translate.matrix[2][1] = -ty ;
  MATRIX_SET_OPT_BOTH(&translate) ;

  char_info->glyphs_matrix = translate ;
  char_info->advance_matrix = translate ;
  char_info->lineend_matrix = translate ;

  /* Bitmap fonts, or when SB1 is active, restrict direction to orthogonal
     angles, char size is approximate and slant is ignored. */
  bitmap_font_restrictions = char_info->scalable_bitmap == 1 ||
    is_bitmap_font(&char_info->font_info, char_info->font_info.active_font) ;

  /* Add a scale matrix for the absolute/relative size operators. */
  if ( char_info->char_size_mode != DEFAULT ) {
    OMATRIX matrix ;

    if ( !calc_char_size_matrix(pcl5_ctxt, char_info,
                                bitmap_font_restrictions, &matrix) )
      return FALSE ;

    /* Changing character size also changes the width of line used to draw
       Stick font characters.  HP4700 seems to choose the smallest factor. */
    char_info->stick_pen_width_factor = fabs(matrix.matrix[0][0]) ;
    if ( char_info->stick_pen_width_factor > fabs(matrix.matrix[1][1]) )
      char_info->stick_pen_width_factor = fabs(matrix.matrix[1][1]) ;

    matrix_mult(&char_info->glyphs_matrix, &matrix, &char_info->glyphs_matrix) ;
    matrix_mult(&char_info->advance_matrix, &matrix, &char_info->advance_matrix) ;
    matrix_mult(&char_info->lineend_matrix, &matrix, &char_info->lineend_matrix) ;
  }

  /* Add a matrix to handle text slanting (the lineend_matrix excludes slanting
     otherwise LF would be offset according to the slant). */

  if ( char_info->slant_angle != 0 && !bitmap_font_restrictions ) {
    OMATRIX matrix ;

    /* Make a matrix to shear the glyphs. */
    matrix.matrix[0][0] = 1 ;
    matrix.matrix[0][1] = 0 ;
    matrix.matrix[1][0] = char_info->slant_angle ;
    matrix.matrix[1][1] = 1 ;
    matrix.matrix[2][0] = 0 ;
    matrix.matrix[2][1] = 0 ;
    MATRIX_SET_OPT_BOTH(&matrix) ;

    /* Do not update lineend_matrix - it must exclude the slant. */
    matrix_mult(&char_info->glyphs_matrix, &matrix, &char_info->glyphs_matrix) ;
    matrix_mult(&char_info->advance_matrix, &matrix, &char_info->advance_matrix) ;
  }

  /* Add a rotation according to the direction run and rise. */
  if ( char_info->direction_mode != DEFAULT ) {
    HPGL2Real rise = char_info->direction_rise, run = char_info->direction_run ;

    switch ( char_info->direction_mode ) {
    case RELATIVE : {
      HPGL2ConfigInfo *config_info = get_hpgl2_config_info(pcl5_ctxt) ;
      run *= 0.01 * (config_info->scale_points.p2.x - config_info->scale_points.p1.x) ;
      rise *= 0.01 * (config_info->scale_points.p2.y - config_info->scale_points.p1.y) ;
     }
     /* fall through into absolute */
    case ABSOLUTE : {
      HPGL2Real angle, sin_angle, cos_angle ;
      OMATRIX matrix ;

      angle = atan2(rise, run) ;
      angle *= RAD_TO_DEG ;
      NORMALISE_ANGLE(angle) ;
      SINCOS_ANGLE(angle, sin_angle, cos_angle) ;

      matrix.matrix[0][0] = cos_angle ;
      matrix.matrix[0][1] = sin_angle ;
      matrix.matrix[1][0] = -sin_angle ;
      matrix.matrix[1][1] = cos_angle ;
      matrix.matrix[2][0] = 0 ;
      matrix.matrix[2][1] = 0 ;
      MATRIX_SET_OPT_BOTH(&matrix) ;

      matrix_mult(&char_info->glyphs_matrix, &matrix, &char_info->glyphs_matrix) ;
      matrix_mult(&char_info->advance_matrix, &matrix, &char_info->advance_matrix) ;
      matrix_mult(&char_info->lineend_matrix, &matrix, &char_info->lineend_matrix) ;

      /* Bitmapped characters can be printed only with orthogonal directions
         (0, 90, 180, or 270 degrees).  For any other direction angles we need
         to rotate each char to snap it to the nearest orthogonal angle. */
      if ( bitmap_font_restrictions &&
           fabs(angle) > EPSILON && fabs(angle - 90) > EPSILON &&
           fabs(angle - 180) > EPSILON && fabs(angle - 270) > EPSILON ) {
        if ( angle <= 45 || angle >= 315 ) {
          angle = -angle ;
        } else if ( angle <= 135 ) {
          angle = 90 - angle ;
        } else if ( angle <= 225 ) {
          angle = 180 - angle ;
        } else {
          angle = 270 - angle ;
        }
        NORMALISE_ANGLE(angle) ;
        SINCOS_ANGLE(angle, sin_angle, cos_angle) ;

        /* Rotate bitmap font to an orthogonal angle. */
        matrix.matrix[0][0] = cos_angle ;
        matrix.matrix[0][1] = sin_angle ;
        matrix.matrix[1][0] = -sin_angle ;
        matrix.matrix[1][1] = cos_angle ;
        matrix.matrix[2][0] = 0 ;
        matrix.matrix[2][1] = 0 ;
        MATRIX_SET_OPT_BOTH(&matrix) ;

        /* Exclude this transformation from the advance_matrix and lineend_matrix. */
        matrix_mult(&char_info->glyphs_matrix, &matrix, &char_info->glyphs_matrix) ;
      }
      break ;
     }
    }
  }


  /* Finally, translate back to the original position. */

  translate.matrix[0][0] = 1.0 ;
  translate.matrix[0][1] = 0 ;
  translate.matrix[1][0] = 0 ;
  translate.matrix[1][1] = 1.0 ;
  translate.matrix[2][0] = tx ;
  translate.matrix[2][1] = ty ;
  MATRIX_SET_OPT_BOTH(&translate) ;

  matrix_mult(&char_info->glyphs_matrix, &translate, &char_info->glyphs_matrix) ;
  matrix_mult(&char_info->advance_matrix, &translate, &char_info->advance_matrix) ;
  matrix_mult(&char_info->lineend_matrix, &translate, &char_info->lineend_matrix) ;


  /* apply any plot size scaling for the font matrices explicitly. The device
   * ctm does not deal with plot size scaling. Translation component of the
   * matrix is in real plotter units, and does not need scaling. */
  if ( !hpgl2_is_scaled(pcl5_ctxt) )
  {
    OMATRIX matrix;

    matrix = identity_matrix;
    matrix.matrix[0][0] = horizontal_scale_factor(pcl5_ctxt);
    matrix.matrix[0][1] = 0;
    matrix.matrix[1][0] = 0;
    matrix.matrix[1][1] = vertical_scale_factor(pcl5_ctxt);
    matrix_mult(&char_info->glyphs_matrix,
                &matrix,
                &char_info->glyphs_matrix) ;
    matrix_mult(&char_info->advance_matrix,
                &matrix,
                &char_info->advance_matrix) ;
    matrix_mult(&char_info->lineend_matrix,
                &matrix,
                &char_info->lineend_matrix) ;
  }

  /* If the glyphs matrix cannot be inverted we need to ignore labels.
     Note we cannot use lost mode for this as that only deals with current
     pen position, not for things like bad char size values. */

  if ( matrix_inverse(&char_info->glyphs_matrix, &char_info->glyphs_inverse) &&
       matrix_inverse(&char_info->lineend_matrix, &char_info->lineend_inverse) )
    char_info->degenerate_matrices = FALSE ;
  else
    char_info->degenerate_matrices = TRUE ;

  return TRUE ;
}

static Bool handle_BS_HT(PCL5Context *pcl5_ctxt, HPGL2CharacterInfo *char_info, Glyph glyph)
{
  HPGL2Real linefeed = get_linefeed(char_info) ;
  Glyph spaceglyph = SPACE ;
  HPGL2Real spacewidth, plot[2] = { 0, 0 } ;
  HPGL2Point current_point ;

  if ( !glyphs_stringwidth(pcl5_ctxt, &spaceglyph, 1, &spacewidth, NULL) )
    return FALSE ;

  if ( !gs_currentpoint(&gstateptr->thepath, &current_point.x, &current_point.y) )
    return FALSE ;

  /* Find current position without all the effects from the glyphs matrix.
     This simplifies the BS/HT code below because we don't need to worry
     about rotation and scaling until the end. */

  plot[0] = current_point.x - char_info->left_margin[0] ;
  plot[1] = current_point.y - char_info->left_margin[1] ;

  MATRIX_TRANSFORM_DXY(plot[0], plot[1], plot[0], plot[1], &char_info->lineend_inverse) ;

  current_point.x = char_info->left_margin[0] + plot[0] ;
  current_point.y = char_info->left_margin[1] + plot[1] ;

  switch ( char_info->text_direction ) {
  case RIGHT :
    if ( glyph == BS ) {
      /* Move one column left unless at left margin, in which case no action is taken. */
      plot[0] = current_point.x ;
      if ( (current_point.x - spacewidth) > char_info->left_margin[0] )
        plot[0] -= spacewidth ;
    } else {
      /* Move to the next horizontal tab stop.  The tab stops are at the left margin,
         and every eight columns to the right of the left margin. */
      plot[0] = char_info->left_margin[0] ;
      if ( char_info->left_margin[0] <= current_point.x ) {
        HPGL2Real tabwidth = 8 * spacewidth ;
        HPGL2Real num_tabs = ((current_point.x - char_info->left_margin[0]) / tabwidth) + EPSILON ;
        plot[0] += ((int32)num_tabs + 1) * tabwidth ;
      }
    }
    plot[1] = current_point.y ;
    break ;
  case DOWN :
    if ( glyph == BS ) {
      /* Move one column left unless at left margin, in which case no action is taken. */
      plot[1] = current_point.y ;
      if ( (current_point.y + linefeed * (1 + char_info->extra_space_width)) < char_info->left_margin[1] )
        plot[1] += linefeed * (1 + char_info->extra_space_width) ;
    } else {
      /* Move to the next horizontal tab stop.  The tab stops are at the left margin,
         and every eight columns to the right of the left margin. */
      plot[1] = char_info->left_margin[1] ;
      if ( char_info->left_margin[1] >= current_point.y ) {
        HPGL2Real tabwidth = 8 * linefeed * (1 + char_info->extra_space_width) ;
        HPGL2Real num_tabs = ((char_info->left_margin[1] - current_point.y) / tabwidth) + EPSILON ;
        plot[1] -= ((int32)num_tabs + 1) * tabwidth ;
      }
    }
    plot[0] = current_point.x ;
    break ;
  case LEFT :
    if ( glyph == BS ) {
      /* Move one column left unless at left margin, in which case no action is taken. */
      plot[0] = current_point.x ;
      if ( (current_point.x + spacewidth) < char_info->left_margin[0] )
        plot[0] += spacewidth ;
    } else {
      /* Move to the next horizontal tab stop.  The tab stops are at the left margin,
         and every eight columns to the right of the left margin. */
      plot[0] = char_info->left_margin[0] ;
      if ( char_info->left_margin[0] >= current_point.x ) {
        HPGL2Real tabwidth = 8 * spacewidth ;
        HPGL2Real num_tabs = ((char_info->left_margin[0] - current_point.x) / tabwidth) + EPSILON ;
        plot[0] -= ((int32)num_tabs + 1) * tabwidth ;
      }
    }
    plot[1] = current_point.y ;
    break ;
  case UP :
    if ( glyph == BS ) {
      /* Move one column left unless at left margin, in which case no action is taken. */
      plot[1] = current_point.y ;
      if ( (current_point.y - linefeed * (1 + char_info->extra_space_width)) > char_info->left_margin[1] )
        plot[1] -= linefeed * (1 + char_info->extra_space_width) ;
    } else {
      /* Move to the next horizontal tab stop.  The tab stops are at the left margin,
         and every eight columns to the right of the left margin. */
      plot[1] = char_info->left_margin[1] ;
      if ( char_info->left_margin[1] <= current_point.y ) {
        HPGL2Real tabwidth = 8 * linefeed * (1 + char_info->extra_space_width) ;
        HPGL2Real num_tabs = ((char_info->left_margin[1] - current_point.y) / tabwidth) + EPSILON ;
        plot[1] += ((int32)num_tabs + 1) * tabwidth ;
      }
    }
    plot[0] = current_point.x ;
    break ;
  }

  plot[0] -= current_point.x ;
  plot[1] -= current_point.y ;

  MATRIX_TRANSFORM_DXY(plot[0], plot[1], plot[0], plot[1],
                       &char_info->lineend_matrix) ;

  if ( !gs_moveto(FALSE /* absolute */, plot, &gstateptr->thepath) )
    return FALSE ;

  return TRUE ;
}

static Bool handle_SI_SR(PCL5Context *pcl5_ctxt, HPGL2CharacterInfo *char_info,
                         Glyph glyph)
{
  if (! switch_to_font(pcl5_ctxt, &char_info->font_info,
                       glyph == SI ? PRIMARY : SECONDARY, FALSE))
    return FALSE ;

  /* Need to refresh the font matrices, because char_size scaling is affected by
     font height changes. */
  if ( !calc_font_matrices(pcl5_ctxt, char_info) )
    return FALSE ;

  return TRUE ;
}

static Bool handle_CR(HPGL2PrintState *print_state)
{
  HPGL2Real plot[2] = { 0, 0 } ;

  plot[0] = print_state->Carriage_Return_point.x ;
  plot[1] = print_state->Carriage_Return_point.y ;

  if ( !gs_moveto(TRUE /* absolute */, plot, &gstateptr->thepath) )
    return FALSE ;

  return TRUE ;
}

static Bool handle_LF(HPGL2PrintState *print_state, HPGL2CharacterInfo *char_info,
                      HPGL2Real lines)
{
  HPGL2Real linefeed = get_linefeed(char_info) ;
  HPGL2Real plot[2] = { 0, 0 } ;

  switch ( char_info->text_direction ) {
  case LEFT :
    lines = -lines ;
    /* fall through */
  case RIGHT :
    linefeed += (char_info->extra_space_height * linefeed) ;
    if ( char_info->linefeed_direction == 0 )
      plot[1] = -linefeed * lines ;
    else
      plot[1] = linefeed * lines ;
    break ;
  case UP :
    lines = -lines ;
    /* fall through */
  case DOWN :
    linefeed += (char_info->extra_space_height * linefeed) ;
    if ( char_info->linefeed_direction == 0 )
      plot[0] = -linefeed * lines ;
    else
      plot[0] = linefeed * lines ;
    break ;
  }

  MATRIX_TRANSFORM_DXY(plot[0], plot[1], plot[0], plot[1],
                       &char_info->lineend_matrix) ;

  /* Update the carriage return point to include the linefeed. */
  print_state->Carriage_Return_point.x += plot[0] ;
  print_state->Carriage_Return_point.y += plot[1] ;

  if ( !gs_moveto(FALSE /* absolute */, plot, &gstateptr->thepath) )
    return FALSE ;

  return TRUE ;
}

static Bool handle_SPACE(PCL5Context *pcl5_ctxt, HPGL2CharacterInfo *char_info,
                         HPGL2Real spaces)
{
  HPGL2Real plot[2] = { 0, 0 } ;

  switch ( char_info->text_direction ) {
  case LEFT :
    spaces = -spaces ;
    /* fall through */
  case RIGHT : {
    Glyph spaceglyph = SPACE ;
    HPGL2Real spacewidth ;

    /* Includes extra spacing */
    if ( !glyphs_stringwidth(pcl5_ctxt, &spaceglyph, 1, &spacewidth, NULL) )
      return FALSE ;

    plot[0] = spacewidth * spaces ;
    break ;
   }
  case UP :
    spaces = -spaces ;
    /* fall through */
  case DOWN : {
    HPGL2Real vertical_advance = get_vertical_char_advance(char_info) ;

    /** \todo Should this be extra_space_height? */
    vertical_advance += (char_info->extra_space_width * vertical_advance) ;

    /* HP printers ignore linefeed direction */
    plot[1] = -vertical_advance * spaces ;
    break ;
   }
  }

  MATRIX_TRANSFORM_DXY(plot[0], plot[1], plot[0], plot[1],
                       &char_info->lineend_matrix) ;

  if ( !gs_moveto(FALSE /* absolute */, plot, &gstateptr->thepath) )
    return FALSE ;

  return TRUE ;
}

static void set_stickarc_pen_width(PCL5Context *pcl5_ctxt, HPGL2CharacterInfo *char_info)
{
  FontSelInfo *font_sel_info = get_font_sel_info(&char_info->font_info, char_info->font_info.active_font) ;

  hpgl2_set_pen_width_mode(pcl5_ctxt, HPGL2_PEN_WIDTH_METRIC) ;

  if ( font_sel_info->stroke_weight == 9999 ) {
    hpgl2_set_pen_width(pcl5_ctxt, hpgl2_get_current_pen(pcl5_ctxt), 0) ;
  } else {
    Font *font = get_font(&char_info->font_info, char_info->font_info.active_font) ;
    HPGL2Real pen_width ;

    HQASSERT(font->valid, "Invalid font");
    HQASSERT(font->is_stick_font, "Expected stick or arc font");

    /* The formula used for stick fonts comes from the DesignJet language
     * guide, where it is written as line width =
     * 0.1 * min(height, 1.5 * character width) * pow(1.13, stroke weight)
     *
     * The stroke width for stick fonts does not appear to vary with the
     * individual glyph chosen, and the height in question appears to be
     * the STICK_CHARACTER_BODY_HEIGHT i.e. the height of the 32*32 grid.
     * Assuming 1.5 * the character width comes to 48 units, it will
     * always be the height that is relevant for stick fonts.
     */
    if ( is_fixed_spacing(font) ) {
      pen_width = 0.1 * STICK_CHARACTER_BODY_HEIGHT(font->size) ;
      pen_width *= pow(1.13, font_sel_info->stroke_weight) ;
      pen_width *= HPGL2_POINT_TO_MMS ;      /* we require a metric pen width */
    } else {
      /** \todo The DesignJet guide formula is supposed to apply to arc fonts
       *        too, so see if they should adopt something more like the above.
       */
      pen_width = font->size * HPGL2_ARC_PEN_WIDTH ;
      pen_width *= pow(1.13, font_sel_info->stroke_weight) ;
    }

    /* Changing character size also changes the width of line used to draw
       Stick font characters. */
    pen_width *= char_info->stick_pen_width_factor ;

    hpgl2_set_pen_width(pcl5_ctxt, hpgl2_get_current_pen(pcl5_ctxt), pen_width) ;
  }
}

static Bool set_fill_pen(PCL5Context *pcl5_ctxt, HPGL2CharacterInfo *char_info,
                         HPGL2Integer current_pen, HPGL2Real current_pen_width,
                         HPGL2Integer pen_width_mode)
{
  Font* font = get_font(&char_info->font_info, char_info->font_info.active_font) ;
  Bool bitmap_font_restrictions ;

  HQASSERT(font->valid, "Font is invalid") ;

  /* bitmap and stick fonts cannot be edged */
  bitmap_font_restrictions =
    font->is_stick_font ||
    char_info->scalable_bitmap == 1 ||
    font->bitmapped ;

  switch ( char_info->fill_mode ) {
  case 0 :
    /* "Specifies solid fill using the current pen and edging with the specified
       pen (or current pen if the edge pen parameter is not specified)." */
    hpgl2_set_current_pen(pcl5_ctxt, current_pen, TRUE /* force solid */) ;

    if ( font->is_stick_font )
      set_stickarc_pen_width(pcl5_ctxt, char_info) ;
    return TRUE ;
  case 1 : {
    /* "Specifies edging with the specified pen (or current pen if the edge pen
       parameter is not specified). Characters are filled only if they cannot be
       edged (bitmap or stick characters), using the edge pen." */
    HPGL2Integer pen = char_info->edge_pen_specified ? char_info->edge_pen : current_pen ;

    if ( bitmap_font_restrictions && pen != 0 ) {
      hpgl2_set_current_pen(pcl5_ctxt, pen, TRUE /* force solid */) ;

      if ( font->is_stick_font )
        set_stickarc_pen_width(pcl5_ctxt, char_info) ;
      return TRUE ;
    }
    return FALSE ;
   }
  case 2 :
    /* "Specifies filled characters using the current fill type (refer to the FT
       command in Chapter 20, The Line and Fill Characteristics Group). The
       currently selected pen is used. Characters are not edged. If the edge pen
       parameter is specified, it is ignored." */
  case 3 :
    /* "Specifies filled characters using the current fill type (refer to the FT
       command in Chapter 20, The Line and Fill Characteristics Group). The
       currently selected pen is used. Characters are edged with the specified
       pen (or current pen if the edge pen parameter is not specified)." */
    hpgl2_set_current_pen(pcl5_ctxt, current_pen, TRUE /* force solid */) ;
    hpgl2_sync_fill_mode(pcl5_ctxt, TRUE) ;

    if ( font->is_stick_font ) {
      set_stickarc_pen_width(pcl5_ctxt, char_info) ;
    } else {
      hpgl2_set_pen_width(pcl5_ctxt, current_pen, current_pen_width) ;
      hpgl2_set_pen_width_mode(pcl5_ctxt, pen_width_mode) ;
    }
    return TRUE ;
  }

  return FALSE ;
}

static Bool set_edge_pen(PCL5Context *pcl5_ctxt, HPGL2CharacterInfo *char_info,
                         HPGL2Integer current_pen)
{
  Font *font = get_font(&char_info->font_info, char_info->font_info.active_font) ;
  Bool bitmap_font_restrictions ;

  HQASSERT(font->valid, "Font is invalid") ;

  /* bitmap and stick fonts cannot be edged */
  bitmap_font_restrictions =
    font->is_stick_font ||
    char_info->scalable_bitmap == 1 ||
    font->bitmapped ;

  switch ( char_info->fill_mode ) {
  case 0 :
    /* "Specifies solid fill using the current pen and edging with the specified
       pen (or current pen if the edge pen parameter is not specified)." */
    if ( !char_info->edge_pen_specified )
      return FALSE ;
    /* fall through */
  case 1 :
    /* "Specifies edging with the specified pen (or current pen if the edge pen
       parameter is not specified). Characters are filled only if they cannot be
       edged (bitmap or stick characters), using the edge pen." */
  case 3 : {
    /* "Specifies filled characters using the current fill type (refer to the FT
       command in Chapter 20, The Line and Fill Characteristics Group). The
       currently selected pen is used. Characters are edged with the specified
       pen (or current pen if the edge pen parameter is not specified)." */
    HPGL2Integer pen = char_info->edge_pen_specified ? char_info->edge_pen : current_pen ;

    if ( !bitmap_font_restrictions && pen != 0 ) {
      Font *font = get_font(&char_info->font_info, char_info->font_info.active_font) ;

      HQASSERT(font->valid, "Invalid font");

      hpgl2_set_current_pen(pcl5_ctxt, pen, TRUE /* force solid */) ;

      /* Edge thickness varies in proportion to point size. */
      hpgl2_set_pen_width(pcl5_ctxt, pen, HPGL2_MAGIC_EDGE_PROPORTION * font->size) ;
      hpgl2_set_pen_width_mode(pcl5_ctxt, HPGL2_PEN_WIDTH_METRIC) ;

      return TRUE ;
    }
    return FALSE ;
   }
  case 2 :
    /* "Specifies filled characters using the current fill type (refer to the FT
       command in Chapter 20, The Line and Fill Characteristics Group). The
       currently selected pen is used. Characters are not edged. If the edge pen
       parameter is specified, it is ignored." */
    return FALSE ;
  }

  return FALSE ;
}

static Bool stroke_glyph(PCL5Context *pcl5_ctxt, HPGL2CharacterInfo *char_info,
                         Glyph glyph, FVECTOR *post_advance)
{
  Bool result = FALSE ;
  STROKE_PARAMS params ;
  HPGL2Real currentpos[2] ;
  Font *font = get_font(&char_info->font_info, char_info->font_info.active_font) ;
  HQASSERT(font->valid, "Font is invalid") ;

  if ( !gs_currentpoint(&gstateptr->thepath, &currentpos[0], &currentpos[1]) )
    return FALSE ;

#define return DO_NOT_RETURN_goto_cleanup_INSTEAD!

  if ( !hpgl2_plotchar(pcl5_ctxt, char_info, glyph, TRUE /* stroke */,
                       DOCHARPATH, FALSE /* hatchfill */,
                       font->is_stick_font /* apply_font_matrix */,
                       post_advance) )
    goto cleanup ;

  set_gstate_stroke(&params, &gstateptr->thepath, NULL, FALSE) ;

  if ( !hpgl2_stroke_internal(&params, STROKE_NORMAL) )
    goto cleanup ;

  if ( !gs_newpath() )
    goto cleanup ;

  result = TRUE ;
 cleanup :
  /* Reinstate current point. */
  if ( !gs_moveto(TRUE /* absolute */, currentpos, &gstateptr->thepath) )
    result = FALSE ;

#undef return
  return result ;
}

static Bool draw_glyphs(PCL5Context *pcl5_ctxt, Glyph *glyphs, uint32 num_glyphs,
                        Bool symbol)
{
  Bool result = FALSE ;
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt) ;
  HPGL2Integer current_pen = hpgl2_get_current_pen(pcl5_ctxt) ;
  HPGL2Real current_pen_width = hpgl2_get_pen_width(pcl5_ctxt, current_pen) ;
  HPGL2Real edge_pen_width = hpgl2_get_pen_width(pcl5_ctxt, char_info->edge_pen) ;
  HPGL2Integer pen_width_mode = hpgl2_get_pen_width_mode(pcl5_ctxt) ;
  Font* font = get_font(&char_info->font_info, char_info->font_info.active_font) ;
  Bool bitmap_font, hatchfill ;
  OMATRIX ctm = thegsPageCTM(*gstateptr) ;
  HDLTinfo savedHDLT ;
  Bool do_reset = FALSE ;
  HPGL2Real reset[2] ;
  uint32 i ;
  Bool restore_LT = FALSE;
  int32 current_wmode, current_vmode, wmode, vmode;

  if ( !flush_vignette(VD_Default) )
    return FALSE ;

  /* Ensure the HPGL and PS fonts are valid */
  if (! ps_font_matches_valid_pcl(&char_info->font_info)) {
    /* One or other of these is invalid */
    if (!hpgl2_set_font(pcl5_ctxt, &char_info->font_info))
      return FALSE ;
  }

  if (!get_ps_text_mode(pcl5_ctxt, &current_wmode, &current_vmode))
    return FALSE;

  wmode = vmode = (char_info->label_mode == 2 || char_info->label_mode == 3) ? 1 : 0;

  if ((wmode != current_wmode || vmode != current_vmode)
       && !set_ps_text_mode(pcl5_ctxt, wmode, vmode))
      return FALSE;

  bitmap_font =
    font->is_stick_font ||
    char_info->scalable_bitmap == 1 ||
    font->bitmapped ;

  hatchfill = ( !bitmap_font &&
                char_info->fill_mode != 0 &&
                (linefill_info->fill_params.fill_type == HPGL2_FILL_CROSSHATCH ||
                 linefill_info->fill_params.fill_type == HPGL2_FILL_HATCH) ) ;

  if ( !finishaddchardisplay(pcl5_ctxt->corecontext->page, num_glyphs) ) {
    /* w/v mode might have changed above, reset on error exit. */
    if (current_wmode != wmode || current_vmode != vmode)
      (void)set_ps_text_mode(pcl5_ctxt, current_wmode, current_vmode);
    return FALSE ;
  }

  savedHDLT = gstateptr->theHDLTinfo ;
  gstateptr->theHDLTinfo.next = &savedHDLT ;
  if ( !IDLOM_BEGINTEXT(NAME_LB) ) {
    gstateptr->theHDLTinfo = savedHDLT ;
    /* w/v mode might have changed above, reset on error exit. */
    if (current_wmode != wmode || current_vmode != vmode)
      (void)set_ps_text_mode(pcl5_ctxt, current_wmode, current_vmode);
    return FALSE ;
  }

  textContextEnter() ;

#define return DO_NOT_RETURN_goto_cleanup_INSTEAD!

  if ( !symbol && !start_position(pcl5_ctxt, glyphs, num_glyphs, reset) )
    goto cleanup ;
  do_reset = TRUE ;

  for ( i = 0 ; i < num_glyphs ; ++i ) {
    HPGL2Real pre_advance[2] = { 0, 0 } ;
    FVECTOR post_advance = { 0 } ;
    Glyph glyph = glyphs[i] ;

    if ( char_info->transparent_data_mode == NORMAL ) {
      switch ( glyph ) {
      case BS : /* BackSpace */
      case HT : /* HorizontalTab */
        if ( !handle_BS_HT(pcl5_ctxt, char_info, glyph) )
          goto cleanup ;
        continue ;
      case SO : /* ShiftOut */
      case SI : /* ShiftIn */
        if ( !handle_SI_SR(pcl5_ctxt, char_info, glyph) )
          goto cleanup ;
        continue ;
      }
    }

    /* N.B. The active font may have changed above */
    font = get_font(&char_info->font_info, char_info->font_info.active_font) ;
    HQASSERT(ps_font_matches_valid_pcl(&char_info->font_info),
             "Expected valid HPGL and PS fonts") ;

    if ( !symbol && !pre_position(pcl5_ctxt, char_info, glyph, pre_advance) )
      goto cleanup ;

    /* N.B. Since gs_modifyctm premultiplies the current CTM, there is no need
     *      for a special PCL version, because the half pixel offset to mimic
     *      the reference printer snap to grid will already be included in the
     *      CTM, and this will be unaffected by the premultiply.
     */
    if ( !font->is_stick_font )
      gs_modifyctm(&char_info->glyphs_matrix) ;

    /* PCL COMPATIBILITY
     * For the HP4250 reference printer, edging is always done with solid
     * lines. If the line type is 0 when edging is done, the change to
     * a solid line persists after the edging is complete. Otherwise, the
     * previous line type is restored.
     * If edging is required, then the mapping of LT 0 to solid line is
     * done before any filling.
     * If edging is requested, but cannot be applied to the font (e.g. bitmap
     * or stick font) then the mapping of LT 0 to solid occurs even though
     * the font is actually filled.
     */

    if ( set_fill_pen(pcl5_ctxt, char_info, current_pen,
                      current_pen_width, pen_width_mode) ) {
      /* PCL COMPATIBILITY See comment above.
       * If "edging" is requested, force a solid line type for fill also if
       * current line type is 0.  */

      if ( char_info->fill_mode != 2 && bitmap_font ) {
        /* filling in place of edging for a font.
         * Font must be drawn solid, but ensure that we restore the line
         * type unless current LT is 0. If this branch is executed,
         * set_edge_pen will return false. */

        if ( linefill_info->line_type.type != HPGL2_LINETYPE_SOLID ) {
          restore_LT = ( linefill_info->line_type.type != 0 );
          if ( ! LT_default(pcl5_ctxt) ) {
            restore_LT = FALSE;
            goto cleanup;
          }
        }
      }
      else  if ( char_info->fill_mode == 3 || char_info->fill_mode == 0 ) {
        /* edging required */
        if ( linefill_info->line_type.type == 0 ) {
          /* force LT0 to solid here. */
          if ( ! LT_default(pcl5_ctxt) )
            goto cleanup;
        }
      }

      if ( !hpgl2_plotchar(pcl5_ctxt, char_info, glyph, FALSE /* stroke */,
                           DOSHOW, hatchfill,
                           font->is_stick_font /* apply_font_matrix */,
                           &post_advance) )
        goto cleanup ;
    }

    if ( set_edge_pen(pcl5_ctxt, char_info, current_pen) ) {
      /* PCL COMPATIBILITY See above.
       * HP4250 Reference printers always force LT to solid when edging.
       * This change is restored, unless the original LT is 0.
       * If fill forced solid line type, no further action needed here.
       */
      if ( linefill_info->line_type.type != HPGL2_LINETYPE_SOLID ) {
          restore_LT = ( linefill_info->line_type.type != 0 );
          if ( ! LT_default(pcl5_ctxt) ) {
            restore_LT = FALSE;
            goto cleanup;
          }
      }

      if ( !stroke_glyph(pcl5_ctxt, char_info, glyph, &post_advance) )
        goto cleanup ;
    }

    /* PCL COMPATIBILITY See above comments. */
    if ( restore_LT ) {
      restore_previous_linetype_internal(pcl5_ctxt, FALSE);
      restore_LT = FALSE;
    }

    /* N.B. Here ctm obtained from the gstate should already include the half
     *      pixel offset to mimic reference printer snap to grid, so set the PS
     *      CTM directly.
     */
    if ( !gs_setctm(&ctm, FALSE) )
      goto cleanup ;

    if ( !symbol ) {
      if ( !post_position(pcl5_ctxt, char_info, pre_advance, glyph, &post_advance) )
        goto cleanup ;
    }
  }

  result = TRUE ;
 cleanup :

  /* N.B. Here ctm obtained from the gstate should already include the half
   *      pixel offset to mimic reference printer snap to grid, so set the PS
   *      CTM directly.
   */
  if ( !gs_setctm(&ctm, FALSE) )
    result = FALSE ;

  /* Set the pen position after all the glyphs. */
  if ( do_reset && !symbol && !gs_moveto(TRUE /* absolute */, reset, &gstateptr->thepath) )
    result = FALSE ;

  /* Reset pens, pen width and pen width mode to their original values. */
  hpgl2_set_pen_width(pcl5_ctxt, current_pen, current_pen_width) ;
  if ( char_info->edge_pen != current_pen )
    hpgl2_set_pen_width(pcl5_ctxt, char_info->edge_pen, edge_pen_width) ;
  hpgl2_set_pen_width_mode(pcl5_ctxt, pen_width_mode) ;
  hpgl2_set_current_pen(pcl5_ctxt, current_pen, FALSE /* force solid */) ;

  /* Reset pen color, as the fill might have forced pcl pattern to change. */
  hpgl2_sync_pen_color(pcl5_ctxt, FALSE /* force solid */) ;

  textContextExit() ;
  if ( !IDLOM_ENDTEXT(NAME_LB, result) )
    result = FALSE ;
  gstateptr->theHDLTinfo = savedHDLT ;

  if ( result && !finishaddchardisplay(pcl5_ctxt->corecontext->page, 1) )
    result = FALSE;

  if ((current_wmode != wmode || current_vmode != vmode)
      && !set_ps_text_mode(pcl5_ctxt, current_wmode, current_vmode))
    result = FALSE;

#undef return
  return result;
}

Bool hpgl2_glyph_bbox(PCL5Context *pcl5_ctxt, Glyph glyph, sbbox_t *bbox)
{
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;

  if ( !calc_font_matrices(pcl5_ctxt, char_info) )
    return FALSE ;

  if ( char_info->degenerate_matrices || hpgl2_in_lost_mode(pcl5_ctxt) )
    return TRUE ;

  if ( !glyph_bbox(pcl5_ctxt, glyph, bbox) )
    return FALSE ;

  MATRIX_TRANSFORM_XY(bbox->x1, bbox->y1,
                      bbox->x1, bbox->y1,
                      &char_info->glyphs_matrix) ;
  MATRIX_TRANSFORM_XY(bbox->x2, bbox->y2,
                      bbox->x2, bbox->y2,
                      &char_info->glyphs_matrix) ;

  return TRUE ;
}

Bool hpgl2_draw_symbol(PCL5Context *pcl5_ctxt, Glyph glyph)
{
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  HPGL2Point curr_pt, offset ;
  HPGL2Real coords[2] ;
  Bool bitmap_font_restrictions ;
  sbbox_t bbox ;

  /* Ensure the HPGL and PS fonts are valid */
  if (! ps_font_matches_valid_pcl(&char_info->font_info)) {
    /* One or other of these is invalid */
    if (!hpgl2_set_font(pcl5_ctxt, &char_info->font_info))
      return FALSE ;
  }

  if ( !calc_font_matrices(pcl5_ctxt, char_info) )
    return FALSE ;

  if ( char_info->degenerate_matrices || hpgl2_in_lost_mode(pcl5_ctxt) )
    return TRUE ;

  if ( !glyph_bbox(pcl5_ctxt, glyph, &bbox) )
    return FALSE ;

  if ( !gs_currentpoint(&gstateptr->thepath, &curr_pt.x, &curr_pt.y) )
    return FALSE ;

  bitmap_font_restrictions = char_info->scalable_bitmap == 1 ||
    is_bitmap_font(&char_info->font_info, char_info->font_info.active_font) ;

  /* PCL COMPATIBILITY The HP4700 appears to draw bitmap symbols at a fixed
     position regardless of which character is being printed.  The actual
     character height used to 'centre' the glyph doesn't appear to be pointsize.
     After careful experimentation I determined that the number is 1.2 times the
     height of 'M' :-) This affects test 5c FTS 1862 and the 'a' symbols. */
  if ( bitmap_font_restrictions ) {
    sbbox_t bbox_M ;

    if ( !characteristic_bbox(pcl5_ctxt, &bbox_M) )
      return FALSE ;

    bbox.y1 = curr_pt.y ;
    bbox.y2 = curr_pt.y + (bbox_M.y2 - bbox_M.y1) * 1.2 ;
  }

  /* Centre the glyph's bbox on the current point.  x1,y1 differs from curr_pt
     for those chars drawn above the baseline or with a non-zero left bearing
     (eg the asterisk char in a proportional font). */
  coords[0] = curr_pt.x - bbox.x1 + (bbox.x2 - bbox.x1) * -0.5 ;
  coords[1] = curr_pt.y - bbox.y1 + (bbox.y2 - bbox.y1) * -0.5 ;

  /* Now hack coords to match 4700 bad output (skip this if you want asterisk
     chars centred properly). */
  offset.x = 0 ;
  offset.y = ((bbox.y2 - curr_pt.y) * -0.5) - coords[1] ;
  coords[0] += offset.x ;
  coords[1] += offset.y ;

  /* The glyphs_matrix is not set in the CTM yet; so
     multiply the translation by the glyphs_matrix now. */
  MATRIX_TRANSFORM_DXY(coords[0], coords[1], coords[0], coords[1],
                       &char_info->glyphs_matrix) ;

  if ( !gs_moveto(FALSE, coords, &gstateptr->thepath) )
    return FALSE ;

  return draw_glyphs(pcl5_ctxt, &glyph, 1, TRUE /* symbol */) ;
}

#define NO_GLYPH (-1)

/* next_label parses the stream into substrings of glyphs.  The substrings are
   delimited by CR/LF, the label terminator, or when the buffer is full (in
   which case we may get the wrong result, glyphs positioned incorrectly).
   glyphs are packed into unsigned 16-bit integers to eventually handle two-byte
   fonts.  CR and LF codes are returned in their own one glyph substring.  The
   return value indicates whether there are more glyphs to follow. */
static Bool next_label(PCL5Context *pcl5_ctxt, uint32 buf_size,
                       Glyph *glyphs, uint32 *num_glyphs, Bool *more,
                       int32 *continuation)
{
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  FontSelInfo* font_sel_info = get_font_sel_info(&char_info->font_info, char_info->font_info.active_font) ;
  Font *font = get_font(&char_info->font_info, char_info->font_info.active_font) ;
  Glyph label_terminator ;

  HQASSERT(font->valid, "Font is invalid") ;

  *num_glyphs = 0 ;

  *more = TRUE ;

  /* LB terminator will always be char code in range 0 to 255, even in
   * label modes that read 2 byte character codes. */
  label_terminator = char_info->label_terminator;

  while ( *num_glyphs + 1 <= buf_size ) {
    int32 ch ;
    Glyph glyph ;

    /* if there was a character left over from the last invocation, process
     * that first. */
    if ( *continuation != NO_GLYPH ) {
      ch = CAST_SIGNED_TO_GLYPH(*continuation);
      *continuation = NO_GLYPH;
      HQASSERT(ch == CR || ch == LF,
              "Unexpected continuation character in next_label");
    }
    else if ( !hpgl2_read_label_character(pcl5_ctxt, &ch) ) {
      *more = FALSE ;
      return ch == EOF ;
    }
    else if ( ch == EOF )
      return TRUE;      /* catch lack of terminator, or correct 2 byte data */

    glyph = CAST_SIGNED_TO_GLYPH(ch) ;

    HQASSERT(pcl5_ctxt->config_params.two_byte_char_support_enabled || ( glyph < 255 ),
            "chracter code out of range" );

    if ( glyph == label_terminator ) {
      if ( char_info->label_terminator_mode == 0 ) {
        /* print the terminating char */
        glyphs[*num_glyphs] = glyph ;
        ++(*num_glyphs) ;
      }
      *more = FALSE ;
      return TRUE ;
    }

    /* Stick and arc fonts are limited to a particular set of symbol sets. */

    /* Convert Stick and Arc fonts to the Roman8 symbol set. */
    if ( font->is_stick_font ) {
      /* Don't mess with control codes. */
      if ( glyph >= SPACE ) {
        switch ( font_sel_info->symbol_set ) {
        case ASCII :
          if ( glyph > 159 )
            glyph = SPACE ;
          else if ( glyph >= 128 )
            continue ; /* Glyphs in the range 128 to 159 are simply ignored. */
          break ;
        default :
        case ROMAN_8 :
          /* Glyphs in the range 128 to 159 are simply ignored. */
          if ( glyph >= 128 && glyph <= 159 )
            continue ;
          break ;
        case ROMAN_EXT :
          if ( glyph < 128 )
            glyph += 128 ;
          else if ( glyph <= 159 )
            continue ; /* Glyphs in the range 128 to 159 are simply ignored. */
          else
            glyph = SPACE ;
          break ;
        }
      }
    }

    /* For non-transparent mode, catch control codes and non-printable
     * characters before adding to label for printing. */
    if ( char_info->transparent_data_mode == NORMAL ) {
      /* Check for a control code with associated functionality. */
      switch ( glyph ) {
      case BS : /* BackSpace */
      case HT : /* HorizontalTab */
      case SO : /* ShiftOut */
      case SI : /* ShiftIn */
        /* These control codes are handled in draw_glyphs and can be
           just added to the label string. */
        break ;

      case LF : /* LineFeed */
      case CR : /* CarriageReturn */
        /* CR and LF control codes split a label into two separate parts,
           equivalent to having two separate LB commands. */
        if ( *num_glyphs == 0 ) {
          glyphs[*num_glyphs] = glyph ;
          ++(*num_glyphs) ;
        } else {
          /* Already read some glyphs so put the control code back
             and finish the current label token. */
          *continuation = ch ;
        }
        return TRUE ;

      default :
        /* Determine whether the non-control char is printable for the symbol set. */
        if ( !character_is_printable(font, glyph) ) {
          continue;
        }
      }
    }

    /* Add the character to the label for printing. */
    glyphs[*num_glyphs] = glyph ;
    ++(*num_glyphs) ;
  }

  return TRUE ;
}

/* ============================================================================
 * Operators
 * ============================================================================
 */
Bool hpgl2op_AD(PCL5Context *pcl5_ctxt)
{
  uint32 i  = 0;
  uint8  terminator;

  /* If there are no font parameters, then set default selection */
  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) ) {
    HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt);

    hpgl2_default_font_sel_info(&char_info->font_info.secondary);
    handle_sel_criteria_change(pcl5_ctxt, &char_info->font_info, SECONDARY) ;
    return TRUE;
  }

  for ( i = 0 ; i < MAX_KINDS ; ++i ) { /* NB already done one param */
    if ( !parse_font_definition_pair(pcl5_ctxt, SECONDARY) )
      return TRUE ;

    if ( hpgl2_scan_separator(pcl5_ctxt) <= 0 )
      return TRUE ;
  }

  return TRUE ;
}

Bool hpgl2op_CF(PCL5Context *pcl5_ctxt)
{
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  int32 fill_mode, edge_pen ;
  uint8 terminator ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {
    /* CF without parameters is a solid fill with no edging. */
    char_info->fill_mode = 0 ;
    char_info->edge_pen = 0 ;
    char_info->edge_pen_specified = TRUE ;
    return TRUE ;
  }

  char_info->edge_pen_specified = FALSE ;

  if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &fill_mode) <= 0 )
    return TRUE ;

  if ( fill_mode < 0 || fill_mode > 3 )
    return TRUE ;

  char_info->fill_mode = CAST_SIGNED_TO_UINT8(fill_mode) ;

  if ( hpgl2_scan_separator(pcl5_ctxt) <= 0 )
    return TRUE ;

  if ( hpgl2_scan_integer(pcl5_ctxt, &edge_pen) <= 0 )
    return TRUE ;

#define EDGE_PEN_RANGE (0x3fffffff)

  if ( edge_pen < -EDGE_PEN_RANGE || edge_pen >= EDGE_PEN_RANGE )
    return TRUE ;

  char_info->edge_pen = edge_pen ;
  char_info->edge_pen_specified = TRUE ;

  return TRUE ;
}

Bool hpgl2op_CP(PCL5Context *pcl5_ctxt)
{
  HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt) ;
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  uint8 terminator ;

  if ( !calc_font_matrices(pcl5_ctxt, char_info) )
    return FALSE ;

  if ( char_info->degenerate_matrices || hpgl2_in_lost_mode(pcl5_ctxt) )
    return TRUE ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {
    /* CP without parameters does a CR-LF. */

    if ( !handle_CR(print_state) ||
         !handle_LF(print_state, char_info, 1) )
      return FALSE ;

  } else {
    /* CP with parameters */
    HPGL2Real spaces, lines ;

    if ( hpgl2_scan_clamped_real(pcl5_ctxt, &spaces) <= 0 ||
         hpgl2_scan_separator(pcl5_ctxt) <= 0 ||
         hpgl2_scan_clamped_real(pcl5_ctxt, &lines) <= 0 )
      return TRUE ;

    if ( lines != 0 ) {
      /* CP lines go the opposite way to a normal linefeed. */
      if ( !handle_LF(print_state, char_info, -lines) )
        return FALSE ;
    }

    if ( spaces != 0 ) {
      if ( !handle_SPACE(pcl5_ctxt, char_info, spaces) )
        return FALSE ;
    }
  }

  return TRUE ;
}

Bool hpgl2op_DI(PCL5Context *pcl5_ctxt)
{
  HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt) ;
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  HPGL2Real run, rise ;
  uint8 terminator ;

  /* Update the carriage return point to the current pen location. */
  if ( !gs_currentpoint(&gstateptr->thepath,
                        &print_state->Carriage_Return_point.x,
                        &print_state->Carriage_Return_point.y) )
    return FALSE ;

  char_info->direction_mode = ABSOLUTE ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {
    /* Reset to default. */
    char_info->direction_run = 1 ;
    char_info->direction_rise = 0 ;
    return TRUE ;
  }

  if ( hpgl2_scan_clamped_real(pcl5_ctxt, &run) <= 0 )
    return TRUE ;

  if ( hpgl2_scan_separator(pcl5_ctxt) < 1 )
    return TRUE ;

  if ( hpgl2_scan_clamped_real(pcl5_ctxt, &rise) <= 0 )
    return TRUE ;

  char_info->direction_run = run ;
  char_info->direction_rise = rise ;

  return TRUE ;
}

Bool hpgl2op_DR(PCL5Context *pcl5_ctxt)
{
  HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt) ;
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  HPGL2Real run, rise ;
  uint8 terminator ;

  /* Update the carriage return point to the current pen location. */
  if ( !gs_currentpoint(&gstateptr->thepath,
                        &print_state->Carriage_Return_point.x,
                        &print_state->Carriage_Return_point.y) )
    return FALSE ;

  char_info->direction_mode = RELATIVE ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {
    /* Reset to default. */
    char_info->direction_run = 1 ;
    char_info->direction_rise = 0 ;
    return TRUE ;
  }

  if ( hpgl2_scan_clamped_real(pcl5_ctxt, &run) <= 0 )
    return TRUE ;

  if ( hpgl2_scan_separator(pcl5_ctxt) < 1 )
    return TRUE ;

  if ( hpgl2_scan_clamped_real(pcl5_ctxt, &rise) <= 0 )
    return TRUE ;

  char_info->direction_run = run ;
  char_info->direction_rise = rise ;

  return TRUE ;
}

Bool hpgl2op_DT(PCL5Context *pcl5_ctxt)
{
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  int32 ch ;

  /* NOTE: Cannot use hpgl2_scan_terminator here, because the only terminator
     suitable for defaulting the label terminator is ';'. */

  ch = pcl5_ctxt->last_char;
  if (ch == EOF)
    return TRUE ;

  if ( ch == ';' ) {
    /* Reset to default. */
    char_info->label_terminator = ETX ;
    char_info->label_terminator_mode = 1 ;
    pcl5_ctxt->last_char = Getc(pcl5_ctxt->flptr);
    return TRUE ;
  }

  switch ( ch ) {
  case NUL : case LF : case 27 : case ';' :
    break ; /* ignore out-of-range value */
  default :
    char_info->label_terminator = (uint8)ch ;
  }

  pcl5_ctxt->last_char = Getc(pcl5_ctxt->flptr);

  if ( hpgl2_scan_separator(pcl5_ctxt) < 1 )
    return TRUE ;

  ch = pcl5_ctxt->last_char;
  if (ch == EOF)
    return TRUE ;

  switch ( ch ) {
  case '0' :
    char_info->label_terminator_mode = 0 ;
    break ;
  case '1' :
    char_info->label_terminator_mode = 1 ;
    break ;
  default :
    break ; /* ignore out-of-range value */
  }

  pcl5_ctxt->last_char = Getc(pcl5_ctxt->flptr);
  return TRUE ;
}

Bool hpgl2op_DV(PCL5Context *pcl5_ctxt)
{
  HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt) ;
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  uint8 terminator ;
  int32 direction ;

  /* Update the carriage return point to the current pen location. */
  if ( !gs_currentpoint(&gstateptr->thepath,
                        &print_state->Carriage_Return_point.x,
                        &print_state->Carriage_Return_point.y) )
    return FALSE ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {
    char_info->text_direction = RIGHT ;
    char_info->linefeed_direction = 0 ;
    return TRUE ;
  }

  if ( !hpgl2_scan_clamped_integer(pcl5_ctxt, &direction) )
    return TRUE ;

  /* If it's within range update, otherwise ignore */
  switch ( direction ) {
  case RIGHT :
  case DOWN :
  case LEFT :
  case UP :
    char_info->text_direction = CAST_SIGNED_TO_UINT8(direction) ;
  }

  if ( hpgl2_scan_separator(pcl5_ctxt) <= 0 )
    return TRUE ;

  if ( !hpgl2_scan_clamped_integer(pcl5_ctxt, &direction) )
    return TRUE ;

  /* If it's within range update, otherwise ignore */
  if ( 0 <= direction && direction <= 1 ) {
    char_info->linefeed_direction = CAST_SIGNED_TO_UINT8(direction) ;
  }

  return TRUE ;
}

Bool hpgl2op_ES(PCL5Context *pcl5_ctxt)
{
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  uint8 terminator ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {
    /* Reset to default. */
    char_info->extra_space_width = 0 ;
    char_info->extra_space_height = 0 ;
    return TRUE ;
  }

  if ( hpgl2_scan_clamped_real(pcl5_ctxt, &char_info->extra_space_width) <= 0 )
    return TRUE ;

  if ( hpgl2_scan_separator(pcl5_ctxt) < 1 )
    return TRUE ;

  if ( hpgl2_scan_clamped_real(pcl5_ctxt, &char_info->extra_space_height) <= 0 )
    return TRUE ;

  return TRUE ;
}

Bool hpgl2op_FI(PCL5Context *pcl5_ctxt)
{
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  int32 id ;

  if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &id) <= 0 )
    return TRUE ;

  /* Ignore out of range values */
  if ( id < 0 || id > 32767 )
    return TRUE ;

  if ( !do_fontselection_by_id(pcl5_ctxt, &char_info->font_info, id,
                               NULL /* string */, 0 /* length */, PRIMARY) )
    return FALSE ;

  /* Implicit SB1 ; but don't do the font selection, just record
   * that bitmaps may now be selected.
   */
  if (is_bitmap_font(&char_info->font_info, PRIMARY)) {
    FontSelInfo *font_sel_info = get_font_sel_info(&char_info->font_info,
                                                   PRIMARY) ;
    font_sel_info->exclude_bitmap = FALSE;

    font_sel_info = get_font_sel_info(&char_info->font_info,
                                                   SECONDARY) ;
    font_sel_info->exclude_bitmap = FALSE;

    char_info->scalable_bitmap = 1 ;
  }

  return TRUE ;
}

Bool hpgl2op_FN(PCL5Context *pcl5_ctxt)
{
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  int32 id ;

  if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &id) <= 0 )
    return TRUE ;

  /* Ignore out of range values */
  if ( id < 0 || id > 32767 )
    return TRUE ;

  if ( !do_fontselection_by_id(pcl5_ctxt, &char_info->font_info, id,
                               NULL /* string */, 0 /* length */,  SECONDARY) )
    return FALSE ;

  /* Implicit SB1 ; but don't do the font selection, just record
   * that bitmaps may now be selected.
   */
  if (is_bitmap_font(&char_info->font_info, SECONDARY)) {
    FontSelInfo *font_sel_info = get_font_sel_info(&char_info->font_info,
                                                   SECONDARY) ;
    font_sel_info->exclude_bitmap = FALSE;
    font_sel_info = get_font_sel_info(&char_info->font_info,
                                                   PRIMARY) ;
    font_sel_info->exclude_bitmap = FALSE;
    char_info->scalable_bitmap = 1 ;
  }

  return TRUE ;
}

/** \todo two byte fonts etc */
Bool hpgl2op_LB(PCL5Context *pcl5_ctxt)
{
  HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt) ;
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  Bool more = TRUE ;
#define GLYPHS_BUFSIZE (1024)
  static Glyph glyphs[GLYPHS_BUFSIZE] ;
  uint32 num_glyphs ;
  int32 continuation = NO_GLYPH;

  /* Ensure the HPGL and PS fonts are valid */
  if (! ps_font_matches_valid_pcl(&char_info->font_info)) {
    /* One or other of these is invalid */
    if (!hpgl2_set_font(pcl5_ctxt, &char_info->font_info))
      return FALSE ;
  }

  if ( !calc_font_matrices(pcl5_ctxt, char_info) )
    return FALSE ;

  if ( char_info->degenerate_matrices || hpgl2_in_lost_mode(pcl5_ctxt) )
    return TRUE ;

  do {
    if ( !next_label(pcl5_ctxt,
                     GLYPHS_BUFSIZE,
                     glyphs,
                     &num_glyphs,
                     &more,
                     &continuation) )
      return FALSE ;

    if ( num_glyphs == 0 )
      break ;

    /* Handle CR/LF different in non-transparent mode */
    if ( char_info->transparent_data_mode == NORMAL ) {
      if ( glyphs[0] == CR ) {
        if ( !handle_CR(print_state) )
          return FALSE ;
        continue;

      } else if ( glyphs[0] == LF ) {
        if ( !handle_LF(print_state, char_info, 1) )
          return FALSE ;
        continue;
      }
    }
    if ( !draw_glyphs(pcl5_ctxt, glyphs, num_glyphs, FALSE /* symbol */) )
      return FALSE ;

  } while ( more ) ;

  return TRUE ;
}

Bool hpgl2op_LO(PCL5Context *pcl5_ctxt)
{
  HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt) ;
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  uint8 terminator ;
  int32 position ;

  /* Update the carriage return point to the current pen location. */
  if ( !gs_currentpoint(&gstateptr->thepath,
                        &print_state->Carriage_Return_point.x,
                        &print_state->Carriage_Return_point.y) )
    return FALSE ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {
    char_info->label_origin = 1 ;
    return TRUE ;
  }

  if ( !hpgl2_scan_clamped_integer(pcl5_ctxt, &position) )
    return TRUE ;

  /* If it's within range update label_origin, otherwise ignore */
  if ( (1 <= position && position <= 9) ||
       (11 <= position && position <= 19) ||
       position == 21 ) {
    char_info->label_origin = CAST_SIGNED_TO_UINT8(position) ;
  }

  return TRUE ;
}

Bool hpgl2op_SA(PCL5Context *pcl5_ctxt)
{
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;

  if (! switch_to_font(pcl5_ctxt, &char_info->font_info, SECONDARY, FALSE))
    return FALSE ;

  return TRUE ;
}

Bool hpgl2op_SB(PCL5Context *pcl5_ctxt)
{
  int32 n ;
  uint8 terminator ;
  Bool result = TRUE ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {
    /* Reset to default, scalable fonts only. */
    do_SB0(pcl5_ctxt) ;
    return TRUE ;
  }

  if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &n) <= 0 )
    return TRUE ;

  switch ( n ) {
  case 0 : /* Scalable fonts only */
    do_SB0(pcl5_ctxt) ;
    break ;
  case 1 : /* Bitmap fonts allowed */
    do_SB1(pcl5_ctxt) ;
    break ;
  }

  return result ;
}

Bool hpgl2op_SD(PCL5Context *pcl5_ctxt)
{
  uint32 i  = 0;
  uint8  terminator;

  /* If there are no font parameters, then set default selection */
  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) ) {
    HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt);

    hpgl2_default_font_sel_info(&char_info->font_info.primary);
    handle_sel_criteria_change(pcl5_ctxt, &char_info->font_info, PRIMARY) ;
    return TRUE;
  }

  for (i = 0 ; i < MAX_KINDS ; ++i ) { /* nb we have already done one param */
    if ( !parse_font_definition_pair(pcl5_ctxt, PRIMARY) )
      return TRUE ;

    if ( hpgl2_scan_separator(pcl5_ctxt) <= 0 )
      return TRUE ;
  }

  return TRUE ;
}

Bool hpgl2op_SI(PCL5Context *pcl5_ctxt)
{
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  HPGL2Real width, height ;
  uint8 terminator ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {
    /* Reset to default. */
    char_info->char_size_mode = DEFAULT ;
    return TRUE ;
  }

  char_info->char_size_mode = ABSOLUTE ;

  if ( hpgl2_scan_clamped_real(pcl5_ctxt, &width) <= 0 )
    return TRUE ;

  if ( hpgl2_scan_separator(pcl5_ctxt) < 1 )
    return TRUE ;

  if ( hpgl2_scan_clamped_real(pcl5_ctxt, &height) <= 0 )
    return TRUE ;

  char_info->char_size_width = width ;
  char_info->char_size_height = height ;

  return TRUE ;
}

Bool hpgl2op_SL(PCL5Context *pcl5_ctxt)
{
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  uint8 terminator ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {
    /* Reset to default. */
    char_info->slant_angle = 0 ;
    return TRUE ;
  }

  if ( hpgl2_scan_clamped_real(pcl5_ctxt, &char_info->slant_angle) <= 0 )
    return TRUE ;

  return TRUE ;
}

Bool hpgl2op_SR(PCL5Context *pcl5_ctxt)
{
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  HPGL2Real width, height ;
  uint8 terminator ;

  char_info->char_size_mode = RELATIVE ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {
    /* Reset to default. */
    char_info->char_size_width = 0.75 ;
    char_info->char_size_height = 1.5 ;
    return TRUE ;
  }

  if ( hpgl2_scan_clamped_real(pcl5_ctxt, &width) <= 0 )
    return TRUE ;

  if ( hpgl2_scan_separator(pcl5_ctxt) < 1 )
    return TRUE ;

  if ( hpgl2_scan_clamped_real(pcl5_ctxt, &height) <= 0 )
    return TRUE ;

  char_info->char_size_width = width ;
  char_info->char_size_height = height ;

  return TRUE ;
}

Bool hpgl2op_SS(PCL5Context *pcl5_ctxt)
{
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;

  if (! switch_to_font(pcl5_ctxt, &char_info->font_info, PRIMARY, FALSE))
    return FALSE ;

  return TRUE ;
}

Bool hpgl2op_TD(PCL5Context *pcl5_ctxt)
{
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt) ;
  uint8 terminator ;
  int32 ch ;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {
    /* Reset to default. */
    char_info->transparent_data_mode = NORMAL ;
    return TRUE ;
  }

  ch = pcl5_ctxt->last_char;
  if (ch == EOF)
    return TRUE ;

  switch ( ch ) {
  case '0' :
    char_info->transparent_data_mode = NORMAL ;
    break ;
  case '1' :
    char_info->transparent_data_mode = TRANSPARENT ;
    break ;
  default :
    break ; /* ignore out-of-range value */
  }
  pcl5_ctxt->last_char = Getc(pcl5_ctxt->flptr);

  return TRUE ;
}

Bool hpgl2op_LM(PCL5Context *pcl5_ctxt)
{
  HPGL2Integer mode = 0;
  HPGL2Integer row = 0;
  HPGL2CharacterInfo *char_info = get_hpgl2_character_info(pcl5_ctxt);
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  uint8 terminator;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) )
  {
    char_info->label_mode = 0;
    char_info->label_row = 0;
    return TRUE;
  }

  if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &mode) <= 0)
    return TRUE; /* syntax error */

  if ( hpgl2_scan_separator(pcl5_ctxt) )
  {
    if (hpgl2_scan_clamped_integer(pcl5_ctxt, &row) <= 0)
      return TRUE; /* syntax error */
  }

  (void)hpgl2_scan_terminator(pcl5_ctxt, &terminator);

  if ( !pcl5_ctxt->config_params.two_byte_char_support_enabled )
    return TRUE;

  /* Only side effect the state if the parameters are all within range.
   * Note that we have assumed that if the row_num is not supplied, then the
   * default value is to be applied. Thus we always change the row_number.
   */

  if ( mode < 0 || mode > 3 )
    return TRUE; /* mode out of bounds. */

  if ( row < 0 || row > 255 )
    row = 0;

  /* LM disables SM */
  linefill_info->symbol_mode_char = NUL ;
  char_info->label_row = CAST_UNSIGNED_TO_UINT16(row << 8) ;
  char_info->label_mode = CAST_SIGNED_TO_UINT8(mode);

  return TRUE;
}

/* Symbol mode characters are converted to 16 bits, if required, at the
 * point where they are to be printed. The row applied to symbol mode
 * character is that in force when points are plotted.
 *
 * SM is turned off by LM operator so SM char, if valid, always matches
 * the current LM mode.
 */
Glyph hpgl2_get_SM_char(PCL5Context *pcl5_ctxt)
{
  HPGL2LineFillInfo *linefill_info = NULL;
  HPGL2CharacterInfo *char_info = NULL;

  linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  char_info = get_hpgl2_character_info(pcl5_ctxt);

  if ( linefill_info->symbol_mode_char == NUL )
    return NUL;

  if ( pcl5_ctxt->config_params.two_byte_char_support_enabled )
  {
    if ( hpgl2_multi_byte_font(char_info) )
    {
      /* need 2 byte character code */

      if ( char_info->label_mode == 0 || char_info->label_mode == 2 )
      {
        /* but SM char is 1 byte, apply the row from LM state. */
        HQASSERT(char_info->label_row <= 255,
                 "Illegal char code for 1 byte label mode");

        return char_info->label_row + linefill_info->symbol_mode_char;
      }
    }

    /* either not multibyte font, or else we have 2 byte SM */
    return linefill_info->symbol_mode_char;
  }
  else
    return linefill_info->symbol_mode_char;

}

/* Current symbol set determines what size of characters we must produce for
 * labels and symbol mode.
 *
 * Label mode tells us how many characters to read. If 2 byte symbol set
 * and 1 byte label mode, the character codes read are augmented with
 * "row" data from label mode state.
 *
 * In HPGL, symbol sets are modelled either 1 or 2 bytes. There is no
 * multibyte encoding. In the 2 byte symbol set, the symbol set is modelled as
 * a 256 by 256 matrix. 2 byte characters codes have first byte for row,
 * second byte for column. The character code value is 256*row + column.
 * In label mode is 1 byte mode, then each byte read determines the
 * column of the character. The row is provided from the LM state.
 */

/*
 * Read the next character from the input stream according to
 * a) the label mode
 * b) the current symbol set (whether it is 16 bits or not).
 * This function will not check for command terminators etc.
 * The result can indicate EOF, hence it is signed quantity.
 */
Bool hpgl2_read_label_character(PCL5Context *pcl5_ctxt, int32 *res)
{
  int32 glyph_code;
  HPGL2CharacterInfo * char_info = NULL;

  char_info = get_hpgl2_character_info(pcl5_ctxt);

  *res = 0;

  /* read 1 byte */
  glyph_code = pcl5_ctxt->last_char;
  pcl5_ctxt->last_char = Getc(pcl5_ctxt->flptr);
  if (glyph_code == EOF) {
    *res = glyph_code;
    return TRUE;
  }

  if ( pcl5_ctxt->config_params.two_byte_char_support_enabled ) {
    if ( hpgl2_multi_byte_font(char_info) ) {
      /* build 2 byte char code by either parsing more data,
       * or applying the row offset from LM.
       */
      if ( char_info->label_mode == 1 || char_info->label_mode == 3 ) {
        /* 2 byte encoding. */
        uint8 row = CAST_SIGNED_TO_UINT8(glyph_code);
        int32 column;

        column = pcl5_ctxt->last_char;
        if (column == EOF) {
          *res = column;
          return TRUE;
        }
        pcl5_ctxt->last_char = Getc(pcl5_ctxt->flptr);
        glyph_code = CAST_SIGNED_TO_GLYPH(column + ( row << 8 ));
        HQASSERT( glyph_code < 65536, "Glyph code out of range");

      } else {
        /* If LM is 1 byte, and the symbol set is 2 bytes, provide row byte
         * for char code from label mode state. */

        HQASSERT( char_info->label_mode == 0 || char_info->label_mode == 2,
                  "Illegal HPGL LM mode.");

        glyph_code =  CAST_SIGNED_TO_GLYPH(glyph_code + char_info->label_row);
      }
    }
  }

  *res = glyph_code;
  return TRUE;

}

Bool hpgl2_multi_byte_font(HPGL2CharacterInfo *char_info)
{
  /* The current font is considered multibyte if the symbol set is
   * one of a known set of multibyte symbolsets.
   */
  /** \todo
   * Do we need to force the current font to be valid,
   * before making this choice? Or is it safe just to assert the font is
   * valid.
   */
  /** \todo
   * For the time being we have to use the symbol set from the relevant
   * font selection structure. The Font structure itself does not contain
   * the appropriate symbol set information ( it has only the symbolset
   * type ).
   */


#if 0
  /** \todo
   * Emergency fix; we don't seem able to retrieve symbol set date from the
   * font.
   * Allow wide characters only in 16 bit label mode.
   */
  FontSelInfo *current_font = NULL;
  current_font = char_info->font_info.active_font == PRIMARY ?
    &char_info->font_info.primary :
    &char_info->font_info.secondary ;

  return ( current_font->symbol_set ==  0   /* UNICODE ? */
           || current_font->symbol_set == 610  /* WIN31J-DBCS/ShiftJIS */
           || current_font->symbol_set == 555  /* JIS */
         );
#endif
  return char_info->label_mode == 1 || char_info->label_mode == 3;
}

/* ============================================================================
* Log stripped */
 
