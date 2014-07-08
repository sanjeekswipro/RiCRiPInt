/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxltext.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Text operator handling functions
 */

#include "core.h"
#include "routedev.h"
#include "plotops.h"
#include "display.h"

#include "pclxltypes.h"
#include "pclxldebug.h"
#include "pclxlcontext.h"
#include "pclxlerrors.h"
#include "pclxloperators.h"
#include "pclxlattributes.h"
#include "pclxlgraphicsstate.h"
#include "pclxlpsinterface.h"
#include "pclxlfont.h"

 /* Max size of DL objects for XL text runs. 0 = old value (1024)
    There's a peak in performance around 384 before dropping again at 512, but
    if memory is not an issue, 1024 seems to be the best bet on the FitPC. */
#define MAX_XL_CHRS 1024
#define MIN_XL_CHRS 48
#define MAX_PCLXL_CHARS_PER_DL_ENTRY 1024
#define MIN_PCLXL_CHARS_PER_DL_ENTRY 1

/**
 * \brief pclxl_text() provides the bulk of the implementation
 * of PCLXL "Text" and "TextPath" operators whose operation
 * is essentially identical down to the point of actually
 * performing the Postscript "plot char" operation
 * which differs in implementation between the two PCLXL operators
 *
 * In order to implement the relatively minor divergent implementation
 * pclxl_text() takes a pointer to a function to actually perform the
 * "plot char/char path" which it calls at the correct point
 */

/* Changes have been made here so that for text commands with very few glyphs
 * we still create a form with min_XL_chrs slots.  By using the setg_required
 * flag we can then see if we can continue using the same form as for the last
 * text command or whether we need to start a new DL entry.  The setg_required
 * flag is also used to ensure that plotchar does not do DEVICE_SETG more
 * than necessary.
 * The setg_required flag has also been somewhat overloaded to avoid doing
 * parts of this function which cannot change anything if the flag is not
 * already set.
 */

Bool
pclxl_text(PCLXL_PARSER_CONTEXT parser_context,
           Bool                 outline_char_path)
{
  static PCLXL_ATTR_MATCH match[4] = {
#define TEXT_TEXT_DATA        (0)
    {PCLXL_AT_TextData | PCLXL_ATTR_REQUIRED},
#define TEXT_X_SPACING_DATA   (1)
    {PCLXL_AT_XSpacingData},
#define TEXT_Y_SPACING_DATA   (2)
    {PCLXL_AT_YSpacingData},
    PCLXL_MATCH_END
  };

  static Bool last_drew_normal_text = FALSE ;

  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_CHAR_DETAILS char_details = &graphics_state->char_details;
  PCLXL_FONT_DETAILS current_font = &char_details->current_font;
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;
  PCLXL_ATTRIBUTE text_data_attr;
  PCLXL_ATTRIBUTE x_spacing_attr;
  PCLXL_ATTRIBUTE y_spacing_attr;
  size_t i;
  Bool result = TRUE;

  uint32 slots_required, slots_remaining = 0; /* compiler */
  uint8 vmode = 0; /* compiler */
  uint8 wmode = 0; /* compiler */

  uint32 max_chars = MAX_XL_CHRS;
  uint32 min_chars = MIN_XL_CHRS;
  if (max_chars == 0)
    max_chars = MAX_PCLXL_CHARS_PER_DL_ENTRY;
  if (min_chars == 0)
    min_chars = MIN_PCLXL_CHARS_PER_DL_ENTRY;
  if (max_chars < min_chars)
    max_chars = min_chars;

  /* We are expecting a mandatory TextData attribute and optional
   * XSpacingData and YSpacingData.
   *
   * The former is treated as an array of character codes within the
   * current font (we must check that there *is* a current font)
   *
   * The latter, if present, are the X (and Y) escapements to be
   * applied (within the scope of the current character CTM) *after*
   * each character has been drawn
   *
   * The default for the escapements is 0.0 which means that one of
   * the two spacing data are essentially mandatory as otherwise the
   * characters are plotted on top of one another.
   */

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* TextData */
  text_data_attr = match[TEXT_TEXT_DATA].result;
  /* XSpacingData */
  x_spacing_attr = match[TEXT_X_SPACING_DATA].result;
  if ( x_spacing_attr &&
      (text_data_attr->array_length != x_spacing_attr->array_length) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_TEXT, PCLXL_ILLEGAL_ARRAY_SIZE,
                        ("XSpacingData array length does not match text"));
    return(FALSE);
  }
  /* YSpacingData */
  y_spacing_attr = match[TEXT_Y_SPACING_DATA].result;
  if ( y_spacing_attr &&
       (text_data_attr->array_length != y_spacing_attr->array_length) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_TEXT, PCLXL_ILLEGAL_ARRAY_SIZE,
                        ("YSpacingData array length does not match text"));
    return(FALSE);
  }

  if ( !pclxl_set_font(parser_context, current_font, outline_char_path) ) {
    return FALSE;
  }

  if ( !graphics_state->ctm_is_invertible )
  {
    /*
     * The Current Transformation Matrix (CTM)
     * is "singular" (a.k.a. "degenerate")
     * and so does not have an inverse
     *
     * This is probably because we have a page scale (factor)
     * of zero in either or both of the X and Y directions
     * and so any and all text output is zero-sized
     *
     * So we quietly return TRUE here
     */

    return TRUE;
  }

  if ( !graphics_state->current_point ) {
    PCLXL_ERROR_HANDLER(pclxl_context, (outline_char_path ? PCLXL_SS_VECTOR : PCLXL_SS_IMAGE),
                        PCLXL_CURRENT_CURSOR_UNDEFINED,
                        ("There is no current position"));
    return FALSE;
  }

  /* Note that at present char_details->char_color is just a synonym
     (i.e. reference to) the fill_details->brush_source */
  /*
   * N.B. These can result in the setg_required flag being set so this
   * must be done before we check the flag.  However there is no point
   * setting the color if no color related commands have been received
   * since we were last here.
   * todo may be better to avoid setting the setg_required flag on
   * receipt of those commands and do it where necessary inside
   * pclxl_ps_set_color instead.
   */
  if ( (char_details->char_color->color_array_len > 0) &&
       ((non_gs_state->setg_required &&
         !pclxl_ps_set_color(pclxl_context, char_details->char_color, FALSE /* For an image? */)) ||
         (!pclxl_ps_set_rop3(pclxl_context, graphics_state->ROP3, FALSE))) ) {
    /* We have failed to set the char color (or the ROP code). So there
     * is little point in trying to plot the characters
     */
    return FALSE;
  }

#ifdef DEBUG_BUILD
  if ( text_data_attr->value.v_ubytes[0] != 7 )
  {
    PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
                ("%s(TextData = \"%s\" (%d characters), XSpacing = [%d values], YSpacing = [%d values])",
                 (outline_char_path ? "TextPath" : "Text"),
                 (text_data_attr->data_type == PCLXL_DT_UByte_Array ? text_data_attr->value.v_ubytes : (uint8*) text_data_attr->value.v_uint16s),
                 text_data_attr->array_length,
                 (x_spacing_attr != NULL ? x_spacing_attr->array_length : 0),
                 (y_spacing_attr != NULL ? y_spacing_attr->array_length : 0)));
  }
#endif

  if (non_gs_state->text_mode_changed && non_gs_state->setg_required) {
    /* todo Review this for trunk */
    /* Set the /V and /W modes if necessary */
    vmode = (char_details->char_sub_modes_len
             ? char_details->char_sub_modes[0]
             : PCLXL_eNoSubstitution);
    wmode = (char_details->writing_mode ? 1 : 0);
  
    if (current_font->original_wmode != wmode || current_font->original_vmode != vmode)
      pclxl_ps_set_char_mode(graphics_state, wmode, vmode);
  }

  /* If we are swapping over to normal text from outline text assume a setg
   * is required.  The flag should normally already be set either from start
   * up or from drawing something else possibly including an outline char, but
   * there is a a possibility that we set up for an outline char and then
   * never drew it, so never set the flag.
   * todo Review for trunk if this can really occur.
   * todo Check this is correctly placed below other stuff that checks the
   * setg_required flag above.
   */
  if (!last_drew_normal_text && !outline_char_path)
    non_gs_state->setg_required = TRUE;

  /* Only update this if we will actually be calling plotchar */
  if (outline_char_path || char_details->char_color->color_array_len != 0)
    last_drew_normal_text = !outline_char_path;

  /* Set up the DL entry */
  /* \todo can we bail out much earlier in this case? */
  if(text_data_attr->array_length <= 0)
    goto tidyup;

  if (non_gs_state->setg_required) {
    slots_required = min(max_chars, text_data_attr->array_length);
    slots_remaining = max(min_chars, slots_required) ;
    result = finishaddchardisplay(pclxl_context->corecontext->page, slots_remaining) ;
  }
 
  textContextEnter();

  for ( i = 0; result && (i < text_data_attr->array_length); i++ ) {
    uint16 character = (text_data_attr->data_type == PCLXL_DT_UByte_Array
                        ? text_data_attr->value.v_ubytes[i]
                        : text_data_attr->value.v_uint16s[i]);
    int32  x_escapement = (x_spacing_attr
                           ? (x_spacing_attr->data_type == PCLXL_DT_UByte_Array
                              ? x_spacing_attr->value.v_ubytes[i]
                              : (x_spacing_attr->data_type == PCLXL_DT_UInt16_Array
                                 ? x_spacing_attr->value.v_uint16s[i]
                                 : x_spacing_attr->value.v_int16s[i]))
                           : 0);
    int32  y_escapement = (y_spacing_attr
                           ? (y_spacing_attr->data_type == PCLXL_DT_UByte_Array
                              ? y_spacing_attr->value.v_ubytes[i]
                              : (y_spacing_attr->data_type == PCLXL_DT_UInt16_Array
                                 ? y_spacing_attr->value.v_uint16s[i]
                                 : y_spacing_attr->value.v_int16s[i]))
                           : 0);


    if (displaylistfreeslots() == 0) {
      slots_required = min(max_chars, text_data_attr->array_length - i);
      slots_remaining = max(min_chars, slots_required) ;
      result = result && finishaddchardisplay(pclxl_context->corecontext->page, slots_remaining) ;
    }

    /* We have a good one here: IFF we are EITHER drawing a character outline
     * path OR we are drawing in a non-NullBrush the we must plot the
     * character.
     *
     * BUT if we are apparently drawing a solid character but using a NULL
     * brush, then we must still go ahead and advance the cursor position by
     * the sum of the character offsets.
     */
    /* Only bother doing this if the result is still ok.
     * Deal with the /V and /W mode at the start and end of the loop.
     */
    result = result &&
      ((!outline_char_path && char_details->char_color->color_array_len == 0) ||
       pclxl_ps_plot_char(pclxl_context, character, outline_char_path)) ;

    if (result) {
      /* \todo if we often end up not drawing the character
       * it would probably be better to do all the escapement additions first
       * before doing a single pclxl_ps_moveto.
       */
      graphics_state->current_point_xy.x += (PCLXL_SysVal) x_escapement;
      graphics_state->current_point_xy.y += (PCLXL_SysVal) y_escapement;

      result = pclxl_ps_moveto(pclxl_context, graphics_state->current_point_xy.x,
                               graphics_state->current_point_xy.y) ;
    }

  } /* end of for loop */

  textContextExit();
  
tidyup:
  /* Restore the /V and /W modes if necessary */
  if (current_font->original_wmode != wmode || current_font->original_vmode != vmode) {
    /* \todo Resetting the mode is very unfortunate as it means we may get
     * in a cycle of setting the setg_required flag and the font.
     * todo review why we reset this - is it only for the benefit of
     * PCL5 in a passthrough situation?
     * N.B. There should be a save/restore round every page anyway.
     */
    HQFAIL("Was not expecting this to be used in the Rift suite") ;
    non_gs_state->setg_required = TRUE ;

    pclxl_ps_set_char_mode(graphics_state, current_font->original_wmode,
                           current_font->original_vmode) ;
  }
  
  /* todo Did this ever give an error message if finishaddchardisplay went wrong? */
  return result;
}

/*
 * Tag 0xa8 Text
 */

Bool
pclxl_op_text(PCLXL_PARSER_CONTEXT parser_context)
{
  return pclxl_text(parser_context, FALSE);
}

/*
 * Tag 0xa9 Text Path
 */

Bool
pclxl_op_text_path(PCLXL_PARSER_CONTEXT parser_context)
{
  return pclxl_text(parser_context, TRUE);
}

/******************************************************************************
* Log stripped */
