/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pcl5fonts.c(EBDSDK_P.1) $
 * $Id: src:pcl5fonts.c,v 1.141.1.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the PCL5 "Font Management, User-Defined Symbol
 * Set and Font Creation" categories.
 *
 * Font Management:
 *
 * Font ID # (specify)          ESC * c # D
 * Font Control                 ESC * c # F
 *
 * User-Defined Symbol Set:
 *
 * Symbol Set ID Code           ESC * c # R
 * Define Symbol Set            ESC ( f # W [symbol set definition data]
 * Symbol Set Management        ESC * c # S
 *
 * Font Creation:
 *
 * Font Descriptor /Data        ESC ) s # W [descriptor data ]
 * Character Code               ESC * c # E
 * Character Descriptor/Data    ESC ( s # W[binary data ]
 *
 * Control Codes
 *
 * Shift In  (primary font)     SI
 * Shift Out (secondary font)   SO
 *
 * Escape Sequences
 *
 * Symbol Set[1]                 ESC ( ID
 * Spacing[1]                    ESC ( s # P
 * Pitch[1]                      ESC ( s # H
 * Height[1]                     ESC ( s # V
 * Style[1]                      ESC ( s # S
 * Stroke Weight[1]              ESC ( s # B
 * Typeface[1]                   ESC ( s # T
 * Pitch Mode                    ESC & k # S
 * Font Selection by ID #[1]     ESC ( # X
 * Select Default Font[1]        ESC ( 3 @
 * Transparent Print Data        ESC & p # X [transparent data ]
 * Underline - Enable            ESC & d # D
 *           - Disable           ESC & d @
 *
 * Character Text Path Direction ESC & c # T
 * Text Parsing Method           ESC & t # P
 *
 * Note [1]: Command shown for primary only, reverse parenthesis for
 * secondary command.
 */

#include "core.h"
#include "pcl5fonts.h"

#include "swpfinpcl.h"
#include "pcl5context_private.h"
#include "printenvironment_private.h"
#include "cursorpos.h"
#include "pcl5scan.h"
#include "pcl5state.h"
#include "misc.h"
#include "pagecontrol.h"
#include "fontselection.h"
#include "printmodel.h"

#include "fileio.h"
#include "graphics.h"
#include "gstate.h"
#include "gu_cons.h"
#include "gu_path.h"
#include "gu_fills.h"
#include "monitor.h"
#include "miscops.h"
#include "namedef_.h"
#include "params.h"
#include "pathcons.h"
#include "pathops.h"
#include "swcopyf.h"
#include "routedev.h"
#include "pclGstate.h"
#include "pclutils.h"
#include "swerrors.h"
#include "gu_ctm.h"
#include "mathfunc.h"
#include "timing.h"
#include "plotops.h"
#include "display.h"

/* ========================================================================= */

void set_default_font_state(PCL5FontState *font_state)
{
  /* PCL5 font state. */
  font_state->underline_mode = NO_UNDERLINE ;
  font_state->within_underline_callback = FALSE ;
  font_state->max_underline_dist = 0.0 ;
  font_state->max_underline_thickness = 0.0 ;
  font_state->orig_wmode = 0 ;
  font_state->orig_vmode = 0 ;
  font_state->DL_slots = NUM_CHARS_PER_DL_ENTRY ; /** todo: adaptive */
#ifndef RQ64403
  font_state->within_text_run = FALSE ;/* 64403 removed 65218 restored */
#else
  font_state->text_run_restart_required = TRUE;/* 64403 added */
  font_state->changed_text_mode = FALSE ;/* 64403 added */
#endif
  font_state->ctm_changed = TRUE ; /* Since last text run - assume it's true */
  font_state->text_path_changed = FALSE ; /* This is a hack - needs a better fix */
}

/* See header for doc */
/* N.B. The default last_char_width was found by trial and error.  (There does
 *      appear to be a default, which is independent of the active font size
 *      at backspace time).  This value is easily correct to within 5 PCL
 *      internal units, and HALF_INCH also occurs elsewhere as a default).
 */
void default_text_info(TextInfo* self)
{
  self->text_parsing_method = 0;       /* All 1 byte */
  self->vertical_text_path = FALSE ;
  self->overstrike_pending = FALSE ;
  self->last_char_width = HALF_INCH ;  /* 3600 PCL internal units */
}

TextInfo* get_text_info(PCL5Context *pcl5_ctxt)
{
  PCL5PrintEnvironment *mpe ;
  TextInfo *text_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  mpe = get_current_mpe(pcl5_ctxt) ;
  HQASSERT(mpe != NULL, "mpe is NULL") ;

  text_info = &(mpe->text_info) ;

  return text_info ;
}

/* ========================================================================= */

/* PFIN event handler delete_font
 *
 * PFIN calls PCL5 back immediately after it has deleted a font, in order to
 * allow PCL5 to mark all fonts using the given ID as invalid.  This mechanism
 * relieves PCL5 of the need to remember which fonts are temporary vs
 * permanent.
 *
 * A callback message of -1 is sent by PFIN immediately prior to a font
 * addition, (i.e. indicates that no (further) deletions are to take place),
 * and is the cue for PCL5 to perform any pending font selections at the
 * current MPE level, since these must take place without the possibility of
 * selecting the font which is about to be added.
 *
 * The effect of this is that in the event of a font deletion only the first
 * part of this function is performed, in the event of an addition only the
 * second part.  For a font redefinition this function will be called first
 * to do deletion actions, then again to do the addition actions.
 */
sw_event_result HQNCALL delete_font(void *context, sw_event* ev)
{
  PCL5PrintState *p_state ;
  PCL5Context *pcl5_ctxt = (PCL5Context*) context ;
  int32 id ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(ev != NULL, "event is NULL") ;

  if (ev->message == 0 || ev->length < sizeof(int32))
    return SW_EVENT_CONTINUE ;

  id = * ((int32*) ev->message) ;
  HQASSERT(id >= INTERNAL_ID && id <= 32767, "Unexpected font id") ;

  p_state = pcl5_ctxt->print_state ;
  HQASSERT(p_state != NULL, "p_state is NULL") ;

  font_sel_caches_free();

  /* An ID greater than INTERNAL_ID indicates the ID of the font that has just
   * been deleted.
   */
  if (id > INTERNAL_ID) {
    /* Mark all fonts at all MPE levels above PJL_CURRENT_ENV with that ID
     * as invalid.  (We do not need to invalidate the default font in the
     * PJL_CURRENT_ENV (base)MPE level, since this will be a built-in font).
     * In the case of redefining a font at the current MPE level this will
     * have the effect, (together with the reselection code below), of
     * reselecting it from its criteria in the absence of either version (new
     * or old) of the font being redefined.
     */
    mark_fonts_invalid_for_id(pcl5_ctxt, id) ;
  }
  else {
    /* A font is about to be added, so do any pending fontselections at the
     * current MPE level.  These will always be by criteria, since selections
     * by ID are done eagerly.  Font selections at lower MPE levels, (which
     * can only be necessary due to fonts having been deleted above - since we
     * ensure all pending fontselections are done before moving up a level),
     * take account of the newly added font(s), so are not done until macro
     * exit, (at the earliest).
     */
    if (! do_pending_fontselections(pcl5_ctxt))
      p_state->font_state.delete_font_ev_handler_error = TRUE ;

    /* Any font at any level above PJL_CURRENT_ENV (base) MPE level, marked as
     * STABLE_FONT must now be downgraded to VALID_FONT meaning that it must be
     * reselected, if any criterium is restated, (to allow for the possibility
     * that this newly added font may be selected).
     */
    downgrade_stable_fonts(pcl5_ctxt) ;
  }

  return SW_EVENT_CONTINUE ;
}

/* ========================================================================= */

Bool pcl5_font_notdef(char_selector_t*  selector, int32 type,
                      int32 charCount, FVECTOR *advance,
                      void *data)
{
  char_selector_t selector_copy ;

  UNUSED_PARAM(void *, data) ;

  HQASSERT(selector, "No char selector for PS notdef character") ;
  /* Note: cid > 0 in this assertion, because we shouldn't be notdef mapping
     the notdef cid (value 0) */
  HQASSERT(selector->cid > 0, "PCL5 notdef char selector is not a defined CID") ;

  selector_copy = *selector ;

  /* No CMap lookup for notdef. Use CID 0 (notdef) in current font instead */
  selector_copy.cid = 0 ;

  return plotchar(&selector_copy, type, charCount, NULL, NULL, advance, CHAR_NORMAL) ;
}


/* Set the default symbolset to use for if we ask for a symbolset that is
 * unknown for the font we are using.  This should be set up before the
 * special PFIN/UFST call is made to select the default font.
 *
 * N.B. This symbolset is not the same thing as the symbolset of the
 *      default font.
 */
/** \todo This is hardcoded to Roman 8 for now - review this. */
Bool set_default_symbolset(void)
{
  uint8 buffer[128];

  /* \todo : the calls to setpfinparams will have to be made depending on the
   * font modules available. It is harmless to attempt to config non-existent
   * module, but possibly wasteful.
   */
  swncopyf(buffer, sizeof(buffer), (uint8*)
           " <</UFST <</DefaultSymbolSet %d >> >> setpfinparams ",
           ROMAN_8 ) ;

  if (! run_ps_string(buffer))
    return FALSE ;

  swncopyf(buffer, sizeof(buffer), (uint8*)
           " <</FF <</DefaultSymbolSet %d >> >> setpfinparams ",
           ROMAN_8 ) ;

  if (! run_ps_string(buffer))
    return FALSE ;

  return TRUE ;
}


/** \todo We are not yet set up to deal with font cartridges etc, other
 *  than the ROM.  Also not clear if we need to store this info somewhere.
 *  Currently the info we have is provided via pcl5exec dict unpack and
 *  simply passed in here, but it is a little ungainly - we may want to
 *  find a place to store it.
 *
 *  For now, add a Bool info_provided, which we can set to false to
 *  indicate that this should do a font selection from the default criteria
 *  as we are not yet completely set up to get the other info.  (It also
 *  seems possible this may have to be the fallback in any event, or perhaps
 *  we will fall back to font zero, source zero, (the ROM)).
 */
Bool default_pcl5fonts(PCL5Context *pcl5_ctxt,
                       int32 font_number,
                       int32 font_source,
                       Bool info_provided)
{
  FontInfo *font_info ;
  Font *font ;
  FontSelInfo *font_sel_info ;
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  font_info = pcl5_get_font_info(pcl5_ctxt) ;
  HQASSERT(font_info != NULL, "font_info is NULL") ;

  HQASSERT(font_info->active_font == PRIMARY,
           "Expected PRIMARY font active") ;

  font = get_font(font_info, PRIMARY) ;
  HQASSERT(font != NULL, "font is NULL") ;

  /** \todo Is this assert correct, or can the font already be valid? */
  HQASSERT(!font->valid, "font is already valid") ;
  HQASSERT(font->stability == UNSTABLE, "font is already stable") ;

  font_sel_info = get_font_sel_info(font_info, PRIMARY) ;
  HQASSERT(font_sel_info != NULL, "font_sel_info is NULL") ;

  if (! info_provided) {
    if (! do_fontselection_by_criteria(pcl5_ctxt, font_info))
      return FALSE ;
  }
  else {
    /*  N.B. This is a special PFIN call, similar to font selection by ID. */
    if (! do_pfin_select_default_font(pcl5_ctxt, font_sel_info, font, font_number, font_source)) {
      /** \todo Or just return FALSE anyway? See also the PFIN call above */
      if (! do_fontselection_by_criteria(pcl5_ctxt, font_info))
        return FALSE ;
    }
    set_hmi_from_font(pcl5_ctxt, font_info, font->hmi) ;
  }

  /* Copy it and the final criteria to the secondary font */
  font_info->secondary_font = font_info->primary_font ;
  font_info->secondary = font_info->primary ;

  return TRUE ;
}

void set_sw_datum_with_current_font_id(sw_datum *datum, FontMgtInfo *font_mgt)
{
  static sw_datum temp[] = {
    SW_DATUM_INTEGER(0),
    SW_DATUM_STRING("")
  } ;

  if (font_mgt->string_id.buf == NULL) {
    *datum = temp[0] ;
    datum->value.integer = font_mgt->font_id ;
  } else {
    *datum = temp[1] ;
    datum->value.string = (const char *)font_mgt->string_id.buf ;
    datum->length = font_mgt->string_id.length ;
  }
}

FontInfo* pcl5_get_font_info(PCL5Context *pcl5_ctxt)
{
  PCL5PrintEnvironment *mpe ;
  FontInfo *font_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  mpe = get_current_mpe(pcl5_ctxt) ;
  HQASSERT(mpe != NULL, "mpe is NULL" );

  font_info = &(mpe->font_info) ;

  return font_info ;
}

FontMgtInfo* pcl5_get_font_management(
  PCL5Context*  pcl5_ctxt)
{
  PCL5PrintEnvironment *mpe;

  mpe = get_current_mpe(pcl5_ctxt);
  return(&(mpe->font_management));
}


/* Unpack the optional parameters dict, overwriting relevant pcl5fonts
 * settings to complete the PJL current environment (as understood by
 * core).
 */
/** \todo Probably will want to change how we pass round font_number
 *  and font_source.
 */
Bool pcl5fonts_apply_pjl_changes(PCL5Context *pcl5_ctxt,
                                 PCL5ConfigParams* config_params,
                                 Bool *font_info_provided,
                                 int32 *font_number,
                                 int32 *font_source)
{
  FontInfo *font_info ;
  PCL5Numeric val ;
  uint8 buffer[128] ;

  HQASSERT( pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT( config_params != NULL, "config_params is NULL") ;
  HQASSERT( font_info_provided != NULL, "Null font_info_provided") ;

  font_info = pcl5_get_font_info(pcl5_ctxt) ;
  HQASSERT( font_info != NULL, "font_info is NULL") ;

  *font_info_provided = FALSE ;
  *font_number = 0 ;
  *font_source = 0 ;

  /* N.B. This is done regardless of whether the dict provided a value */
  swncopyf(buffer, sizeof(buffer), (uint8*)
           " <</UFST <</DarkCourier %s >> >> setpfinparams ",
           config_params->dark_courier ? "true" : "false" ) ;

  if (! run_ps_string(buffer))
    return FALSE ;

  /** \todo Don't try to configure non-existent modules.
   * it is harmless to attempt this, but wasteful.
   * Only attempt configuration of the modules that exist in this build.
   * Note not all modules will necessarily support a DarkCourier flag.
   */
  swncopyf(buffer, sizeof(buffer), (uint8*)
           " <</FF <</DarkCourier %s >> >> setpfinparams ",
           config_params->dark_courier ? "true" : "false" ) ;

  if (! run_ps_string(buffer))
    return FALSE ;

  *font_number = config_params->default_font_number;
#if 0
    /** \todo Sort out type, also we can't currently cope with anything
     *  other than a value of zero, meaning the ROM.
     */
  *font_source = config_params->default_font_source;
#endif
  *font_info_provided = TRUE ;

  /* The remaining values are both defaults for select by criteria, and
   * parameters for the special default font selection PFIN/UFST call.
   * It is not strictly necessary to call the command callbacks here,
   * but they are safe to use and they validate the parameters, (or
   * bring them within range).
   */

  val.real = config_params->default_pitch;

  /* Primary pitch */
  if ( !pcl5op_left_paren_s_H(pcl5_ctxt, FALSE, val) )
    return FALSE ;

  val.real = config_params->default_point_size;

    /* Primary height */
  if (! pcl5op_left_paren_s_V(pcl5_ctxt, FALSE, val))
    return FALSE ;

  val.integer = config_params->default_symbol_set;

  if (! pcl5op_left_paren_symbolset(pcl5_ctxt, FALSE, val))
    return FALSE ;

  return TRUE ;
}

/* This function name is
 * misleading.  A simple set_text_mode would be better.
 */
Bool pcl5_set_text_mode(PCL5Context *pcl5_ctxt)
{
  TextInfo *text_info = get_text_info(pcl5_ctxt) ;
  int32 wmode, vmode ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(text_info != NULL, "text_info is NULL") ;

  vmode = wmode = text_info->vertical_text_path ? 1 : 0 ;

  if (! set_ps_text_mode(pcl5_ctxt,wmode, vmode))
    return FALSE ;

  return TRUE ;
}

/**
 * Wrapper for finishaddchardisplay() - will be used to adapt DL_slots
 * depending on usage. slots=0 for 'predicted' text run length, 1 for
 * usual 'non-textual' flush.
 */
Bool pcl5_flush_text_run(PCL5Context *pcl5_ctxt, int32 slots)
{
  PCL5PrintState *print_state ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  if (slots == 0) {
    /** todo Make font_state.DL_slots adaptive */
    slots = print_state->font_state.DL_slots ;
  }

  return finishaddchardisplay(pcl5_ctxt->corecontext->page, slots) ;
}

/**
 * Wrapper for plotchar().
 */
static Bool pcl5_plotchar(PCL5Context *pcl5_ctxt, uint16 ch, Bool inverse)
{
  PCL5PrintState *print_state ;
  char_selector_t char_selector ;
  FVECTOR advance ;
  Bool result = TRUE ;
  int32 slots ;
  CHAR_OPTIONS options = CHAR_NORMAL ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  char_selector.name = NULL ;
  char_selector.cid = ch ;

  slots = displaylistfreeslots() ;

  textContextEnter();
  /* N.B. The existing chars must be flushed to DL before any DEVICE_SETG */
  if ( slots == 0 || print_state->setg_required ) {
    slots = (inverse ? 1 : 0) ;
    result = pcl5_flush_text_run(pcl5_ctxt, slots) ;
  }

  if (inverse)
    options |= CHAR_INVERSE ;

  /* The CHAR_SETG_BLANK signals that we are relying on this
   * DEVICE_SETG so in effect that we are within a text loop.
   * todo There is probably a better way of doing this.
   */
  if (!print_state->setg_required)
    options |= CHAR_NO_SETG ;

  options |= CHAR_SETG_BLANK ;

  print_state->setg_required = 0 ;

  if ( result )
    result = plotchar(&char_selector, DOSHOW, 1, pcl5_font_notdef, NULL,
                      &advance, options);

  textContextExit();
  return result;
}

#ifdef RQ64403
Bool pcl5_set_text_run_restart_required(PCL5Context *pcl5_ctxt, Bool force)
{
  PCL5PrintState *print_state ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  print_state = pcl5_ctxt->print_state ;
  print_state->font_state.text_run_restart_required = TRUE ;
  return pcl5_end_text_run(pcl5_ctxt, force) ;
}
#endif


/* Just some notes.
 * The fact we can be here recursively suggests that it is inadequate to have
 * within_text_run as part of the font_state as there is only 1 copy of that.
 * It isn't really part of the MPE either although we could make special
 * provision.  In any case the "within text run already" assert is clearly not
 * justified in its current form.
 *
 * The print_state->setg_required flag has got heavily overloaded here.  Its
 * primary purpose is so that pcl5_plotchar can figure out whether to pass the
 * CHAR_NO_SETG flag in to plotchar.  But since we have it in our hand at this
 * point we may as well use it to save unnecessary processing.
 * todo This could all be done better with separate flags for color, patterns
 * etc, and we could also compare with doing gstate changes eagerly, (though
 * that is not always completely straightforward and may not be most efficient).
 */
static Bool changed_text_mode = FALSE ; /* \todo Review for trunk */

Bool pcl5_start_text_run(PCL5Context *pcl5_ctxt, FontSelInfo **font_sel_info,
                         FontInfo **font_info, Font **font)
{
  PCL5PrintEnvironment *mpe ;
  PCL5PrintState *print_state ;
  PageCtrlInfo *page_info ;
#ifdef RQ64403
  TextInfo *text_info ;
  int32 wmode, vmode ;
#endif
  uint32 current_pattern_type ;

  /* Note that this might go recursive if a glyph causes a
     throw_page() which in turn then executes an overlay macro which
     has text within it. */

  if (pcl5_recording_a_macro)
    return TRUE ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  /** \todo Is this assert always justified where PCLXL
   *  passthrough is involved?
   */
  HQASSERT(!print_state->font_state.within_text_run,
            "within text run already");

  /* We should probably set force to TRUE here so that we end the DL entry
   * as we may be about to change gstate, (see also notes above function).
   * Don't worry for now as the above assert isn't firing for the rift suite.
   */
  if (print_state->font_state.within_text_run &&
      ! pcl5_end_text_run(pcl5_ctxt, FALSE))
    return FALSE ;
  
  mpe = get_current_mpe(pcl5_ctxt) ;
  HQASSERT(mpe != NULL, "mpe is Null") ;

  *font_info = &(mpe->font_info) ;
  HQASSERT(*font_info != NULL, "*font_info is NULL") ;

  /* N.B. This also ensures the font is valid. */
  if (! get_active_font(pcl5_ctxt, *font_info, font, font_sel_info))
    return FALSE ;

  HQASSERT(*font != NULL, "*font is NULL") ;
  HQASSERT((*font)->valid, "*font is invalid") ;

#ifdef RQ64403
  if (! print_state->font_state.text_run_restart_required)
    return TRUE ;
#endif
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

#ifdef RQ64403
  text_info = get_text_info(pcl5_ctxt) ;
  HQASSERT(text_info != NULL, "text_info is NULL") ;

  vmode = wmode = text_info->vertical_text_path ? 1 : 0 ;
#endif
  if (page_info->hmi == INVALID_HMI) {
    if (! set_hmi_from_font(pcl5_ctxt, *font_info, (*font)->hmi))
      return FALSE ;
  }

  /* Ensure we have the correct PS CTM */
  /* This is strictly more of a text loop optimisation than a setg
   * one.  The setg_required flag should catch cases where the PS
   * state has changed (and some others e.g. color change so we
   * could optimise this some more).  The ctm_changed flag catches
   * where we have recalculated the PCL5 ctm since the last text
   * run.
   */
  if (print_state->setg_required ||
      print_state->font_state.ctm_changed)
    ctm_install(get_pcl5_ctms(pcl5_ctxt)) ;

  /* Ensure the PS font is set. */
  if (! ps_font_matches_valid_pcl(*font_info)) {
    /* We've already done all but this bit of set_fontselection */
    if (! set_ps_font(pcl5_ctxt, *font_info, *font))
      return FALSE ;
  }

  /* Set the required (vertical or horizontal) text mode, and
   * remember current settings so we can restore it afterwards.
   */
  /* We need to call this if the font has
   * changed since last time or if the text mode has changed since last
   * time.  Both of these should be caught by print_state->setg_required
   * flag.
   */
  /* Don't bother with this if the
   * text path command hasn't been used to change the text path
   * in the whole job so far.
   * N.B. If HPGL changes it, it will always clean up so we only
   * need to look at PCL5 commands.  todo this is a HACK.
   */
  /* \todo Review why we restore this */
  if ( print_state->font_state.text_path_changed && print_state->setg_required ) {
    /* No point fetching this if we won't be changing it below */
    if (! get_ps_text_mode(pcl5_ctxt, &print_state->font_state.orig_wmode,
                           &print_state->font_state.orig_vmode))
      return FALSE ;

#ifdef RQ64403
  if (print_state->font_state.orig_wmode != wmode ||
      print_state->font_state.orig_vmode != vmode) {

    if (! set_ps_text_mode(pcl5_ctxt, wmode, vmode))
      return FALSE ;

    print_state->font_state.changed_text_mode = TRUE ;
  }
#endif

#ifndef RQ64403
    if (! pcl5_set_text_mode(pcl5_ctxt))
      return FALSE ;
#endif
    /* We need to be prepared to restore this at end of text run */
    /* todo review this */
    changed_text_mode = TRUE ;

  } /* end of checking and setting text mode */

  /* If we're not going to be doing a setg because nothing has happened
   * which would have changed the gstate there is no point setting the
   * color.
   * N.B. An alternative would be to not set the setg_required flag as
   * part of dispatching the colorops and only set it here if we actually
   * change the PS color.
   */
  if (print_state->setg_required) {
    if ( !set_current_color(pcl5_ctxt) )
      return FALSE ;

    /* Set the current pattern. Note that the erase pattern means a 'white'
     * pattern for glyphs. */
    if (get_current_pattern_type(pcl5_ctxt) == PCL5_ERASE_PATTERN) {
      set_current_pattern(pcl5_ctxt, PCL5_WHITE_PATTERN) ;
    } else {
      set_current_pattern(pcl5_ctxt, PCL5_CURRENT_PATTERN) ;
    }
  } else {
    /* The situation for patterns is a little different in that a change in
     * printdirection and/or ctm does not appear to require a setg but can
     * still mean that things can change for patterns.  (A change in print
     * direction will cause us to set the ctm_changed flag). However, solid
     * black or white patterns are not affected by ctm/printdirection.
     *
     * \todo Actually patterns generally are
     * based on orientation not printdirection but it is still thought
     * that due to how we set them up there could be a problem if we don't
     * take print direction into account.  Is this true?
     */
    current_pattern_type = get_current_pattern_type(pcl5_ctxt) ;

    /* todo also should be ok for shading patterns with ids 0 or >=100 */
    if (print_state->font_state.ctm_changed &&
        current_pattern_type > PCL5_ERASE_PATTERN )
      set_current_pattern(pcl5_ctxt, PCL5_CURRENT_PATTERN) ;
  }

#ifdef RQ64403
  /* Turn restart required off. This will be reset as soon as we
     encounter something which requires the text run to be
     re-initialised . */
  print_state->font_state.text_run_restart_required = FALSE ;
#endif
  print_state->font_state.within_text_run = TRUE ;
  return TRUE ;
}


Bool pcl5_end_text_run(PCL5Context *pcl5_ctxt, Bool force)
{
  PCL5PrintState *print_state ;

  if (pcl5_recording_a_macro)
    return TRUE ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;
/*
  HQASSERT(print_state->font_state.within_text_run,
            "Expected to be in text run") ;
*/
  print_state->font_state.within_text_run = FALSE ;

  /* Call the PCL5 wrapper
   * for finishaddchardisplay for consistency. */
  if (force && ! pcl5_flush_text_run(pcl5_ctxt, 1))
    return FALSE ;

#ifdef RQ64403
  if (print_state->font_state.changed_text_mode) {
    if (! set_ps_text_mode(pcl5_ctxt, print_state->font_state.orig_wmode,
                           print_state->font_state.orig_vmode))
      return FALSE ;
    print_state->font_state.changed_text_mode = FALSE ;

    /* We may go directly into a new text run after a command which
       does not affect the font selection (but the text mode has just
       been restored so we may as well do the full font selection in
       the rare case). */
    print_state->font_state.text_run_restart_required = TRUE ;
  }
#endif

#ifndef RQ64403
  if (changed_text_mode) {
  
    /* \todo Resetting the mode is very unfortunate as it means we may get
     * in a cycle of setting the flag and the font.
     * Assuming we are only setting it for the benefit of HPGL
     * or possibly PCLXL in a passthrough situation it would be better to
     * rely on the fact that those places set this flag anyway and to
     * reset the font on the way in there if necessary, i.e. lazily.
     * In the worst case we do have save/restore round every page.
     */
    HQFAIL("Was not expecting this to be used in the Rift suite") ;
    changed_text_mode = FALSE ;
    print_state->setg_required += 1 ;

    if (! set_ps_text_mode(pcl5_ctxt, print_state->font_state.orig_wmode,
                           print_state->font_state.orig_vmode))
      return FALSE ;
  }
#endif

  print_state->font_state.ctm_changed = FALSE ;

  return TRUE ;
}


/* Text character callback. Must always reside inbetween a call to
 * pcl5_start_text_run() and pcl5_end_text_run(). Note that the text
 * parsing method has already been taken care of.
 *
 * N.B. In the following an overstrike character means the first
 * character following one of more backspaces, which occurred whilst a
 * proportional font was in operation.  In the event that the active
 * font is now a fixed font, nothing special is done for overstrike
 * characters.
 */
Bool pcl5_text_character(PCL5Context *pcl5_ctxt, FontInfo *font_info, Font **font, uint16 ch)
{
  PCL5PrintState *print_state ;
  PageCtrlInfo *page_info ;
  TextInfo *text_info ;
  Bool line_wrap = FALSE ;
  int32 char_width ;
  int32 rel_position = NOT_SET ;
  Bool blank = FALSE ;
  Bool doing_overstrike = FALSE ;
  Bool centre_overstrike = FALSE ;
  PCL5Real start_x_pos = 0, start_y_pos = 0 ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;
  HQASSERT(font != NULL, "font is NULL") ;
  HQASSERT(*font != NULL, "*font is NULL") ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;
  text_info = get_text_info(pcl5_ctxt) ;
  HQASSERT(text_info != NULL, "text_info is NULL") ;
   /**** This code was removed as part of 64403 and has been re-instated as part of 65218
          It may be necessary to remove this code again ????? */
/*
  HQASSERT(print_state->font_state.within_text_run,
           "Expected to be in text run" ) ;
*/
  if (print_state->font_state.within_text_run &&
      !ps_font_matches_valid_pcl(font_info)) {
    FontSelInfo *dummy_font_sel_info ;
    FontInfo *dummy_font_info ;

    if (! pcl5_end_text_run(pcl5_ctxt, FALSE))
      return FALSE ;
    if (! pcl5_start_text_run(pcl5_ctxt, &dummy_font_sel_info,
                              &dummy_font_info, font))
      return FALSE ;

    HQASSERT(*font != NULL, "*font is NULL") ;
  }
  /**** */

  /* Skip the character if not doing transparent character printing
   * and the character is not printable. N.B. In this case it seems
   * that the last_char_width is not updated, and any pending
   * overstrike is left pending.
   */
  if ( (pcl5_ctxt->interpreter_mode != TRANSPARENT_PRINT_DATA_MODE) &&
       ! character_is_printable(*font, ch) )
    return TRUE ;

  /* Find out whether the character exists and if so how wide it is */

  /** \todo Should probably use plotchar with DOSTRINGWIDTH here,
   * since the pfin_get_metrics call always forces a scalable
   * character to be built.  If the final plotchar ends up creating a
   * bitmap character this is inefficient.  Also, the metrics call
   * always assumes horizontal text mode, so for characters which
   * rotate in vertical text mode this will only give the correct
   * width for square characters.  Also, if the final character is
   * drawn in vertical mode, the plotchar will end up creating a new
   * character, which is again inefficient.  The horizontal or
   * vertical mode should be set before the DOSTRINGWIDTH, and this in
   * turn implies that the PS font should be setup before this point.
   * Due to the possible restore below it (and the text mode) will
   * need to be set up again below.
   *
   * Do note though that the pfin_get_metrics call rounds the returned
   * width to the nearest PCL Unit - any replacement here would need
   * to do the same, (and also presumably for any vertical mode).
   */
  if (! pfin_get_metrics(pcl5_ctxt, *font, ch, &char_width))
    return FALSE ;

  /* A character width of -1 means the character does not exist. */
  if (char_width == -1)
    blank = TRUE ;

  if (blank || is_fixed_spacing(*font))
    char_width = page_info->hmi ;

  /* Ensure the PCL cursor is marked as explicitly set */

  /* N.B. Although no character will be drawn if the cursor is at the
   * right margin or page edge, it is hard to envisage a case where
   * this can happen before the cursor is explicitly set, so mark it
   * as such here and assert in the cursor position querying functions
   * below.
   */
  mark_cursor_explicitly_set(pcl5_ctxt) ;

  /* Find out if line wrap is on */
  line_wrap = get_trouble_shooting_info(pcl5_ctxt)->line_wrap ;

  /* Should we treat this as an overstrike character? */

  /* N.B. Our refererence printers do not centre the overstrike
   * character if line wrap is ON, whereas other printers may do so
   * regardless of the line wrap mode.
   */
  if (text_info->overstrike_pending) {
    doing_overstrike = ! is_fixed_spacing(*font) ;
    centre_overstrike = doing_overstrike && !line_wrap ;
  }

  if (! line_wrap) {
    rel_position = cursor_position_relative_to_right_margin(pcl5_ctxt) ;

    /* Do not print the character if it is on the right margin or at
     * the right hand edge of the logical page.
     *
     * N.B. In this case it seems that the last_char_width is not
     * updated, and any pending overstrike is left pending.
     */
    if (rel_position == ON  || cursor_is_at_right_page_edge(pcl5_ctxt))
      return TRUE ;
  } else {
    /* N.B. This can cause a page throw and hence a PS restore, so any
     * PS settings required after this point must be set up again.
     */
    /** \todo Or should reset_page be handling this? */
    /* Just noting it looks like this is
     * handled by throw_page which ends and restarts the text run.
     */

    /* N.B. If treating this as an overstrike character, use the
     * last_char_width as the basis for the calculation.
     */
    if (! doing_overstrike)
      position_cursor_for_next_character(pcl5_ctxt, char_width) ;
    else
      position_cursor_for_next_character(pcl5_ctxt, text_info->last_char_width) ;
  }

  /* Adjust the overstrike position if necessary */
  if (doing_overstrike) {
    /* This is needed to calculate the advance */
    get_cursor_position(pcl5_ctxt, &start_x_pos, &start_y_pos) ;

    if (centre_overstrike) {
      /* It appears that the overstrike character is not centred if a
       * character of the last_char_width printed here would cross the
       * right margin, or the right edge of the logical page.
       *
       * N.B. It is possible that this can leave the cursor slightly
       * left of the left hand edge of the logical page.
       */
      HQASSERT(! line_wrap, "Unexpected line_wrap") ;
      HQASSERT(rel_position == cursor_position_relative_to_right_margin(pcl5_ctxt),
               "Cursor has been repositioned") ;
      HQASSERT(rel_position != ON, "Didn't expect the cursor to be on the right margin") ;

      if ((rel_position == BEFORE && (start_x_pos + text_info->last_char_width) <= print_state->mpe->page_ctrl.right_margin) ||
          (rel_position == BEYOND && (start_x_pos + text_info->last_char_width) <= print_state->mpe->page_ctrl.page_width)) {
        position_cursor_to_centre_overstrike(pcl5_ctxt, char_width) ;
        HQASSERT((rel_position == BEFORE && get_cursor(pcl5_ctxt)->x <= page_info->right_margin) ||
                 (rel_position == BEYOND && get_cursor(pcl5_ctxt)->x <= page_info->page_width),
                 "Cursor is beyond right margin or right edge of logical page") ;
      }
    }
  }

  if (! blank) {
    PrintModelInfo* print_model = get_print_model(pcl5_ctxt);

    /* Set the PS cursor position from the PCL cursor position */
    if (! set_ps_cursor_position(pcl5_ctxt))
      return FALSE ;

    if (! pcl5_plotchar(pcl5_ctxt, ch, FALSE))
      return FALSE ;

    /* Draw the white region around the character if required. */
    if (! print_model->source_transparent) {
      /* We want the source color to be white, so record the foreground color
         now then set the object color to white. This adds a second char
         object to the DL, but while the foreground colour is recorded as the
         character colour in the PCL attrib (for use by the backdrop
         render/ROP test switching code), the actual colour used to render
         the spans will be white (the render loop always unpacks the DL
         colour into the blit colour). */
      if ( !setPclForegroundSource(pcl5_ctxt->corecontext->page,
                                   PCL_FOREGROUND_IN_PCL_ATTRIB) ||
           !set_white() )
        return FALSE ;
      /* Ensure that we do a DEVICE_SETG */
      print_state->setg_required += 1 ;
      if ( !pcl5_plotchar(pcl5_ctxt, ch, TRUE) )
        return FALSE ;
      /* Ensure that we do another DEVICE_SETG after this point */
      print_state->setg_required += 1 ;

#ifndef RQ64403
      /* Make the PS font invalid so we reset everything. */
      /** \todo Is this really necessary ? */
      handle_ps_font_change(pcl5_ctxt) ;
#endif
      /* Restore the color to what it was as we may not reset it to
         what it should be for the next glyph (during a text run). */
      /* Just noting it looks like we
       * should be restoring the color here as we set it to white
       * above.  But inverse chars are not used in the Rift suite.
       */
#ifdef RQ64403
      set_current_color(pcl5_ctxt) ;
#endif
    }
  }

  /* We only increase the max floating underline distance if we
     actually print a glyph using the current font. */
  {
    if (print_state->font_state.underline_mode == FLOATING_UNDERLINE) {
      if (print_state->font_state.max_underline_dist < (*font)->underline_dist)
        print_state->font_state.max_underline_dist = (*font)->underline_dist ;
      if (print_state->font_state.max_underline_thickness < (*font)->underline_thickness)
        print_state->font_state.max_underline_thickness = (*font)->underline_thickness ;
    }
  }

  /* Update the PCL cursor position */
  if (! doing_overstrike)
    move_cursor_x_relative(pcl5_ctxt, char_width) ;
  else
    set_cursor_x_absolute(pcl5_ctxt, (start_x_pos + text_info->last_char_width)) ;

  /* If we are centring an overstrike character, the cursor may have
   * been moved left of the left margin, but it should be back on the
   * logical page by now.
   */
  HQASSERT(get_cursor(pcl5_ctxt)->x >= 0 && get_cursor(pcl5_ctxt)->x <= page_info->page_width,
           "Expected cursor to be on the logical page") ;

  /* Hang on to the char_width for backspace, unless this was an
   * overstrike character, and we are still (or again) in a
   * proportional font, in which case stick with the existing
   * last_char_width.
   */
  if (! doing_overstrike)
    text_info->last_char_width = char_width ;

  /* Having printed a character, there can be no overstrike pending */
  text_info->overstrike_pending = FALSE ;

  /* If clipping, as opposed to wrapping, and we have just crossed the
   * right margin, need to position the cursor back to the right
   * margin.
   *
   * N.B. It does not appear that the last_char_width is adjusted in
   * this case, (nor in the case where the cursor position has just
   * been limited to the right edge of the logical page).
   */
  if (rel_position == BEFORE &&
      cursor_position_relative_to_right_margin(pcl5_ctxt) == BEYOND)
    move_cursor_to_right_margin(pcl5_ctxt) ;

  return TRUE ;
}

/* ========================================================================= */

/* Reset underline distance and thickness. */
Bool reset_underline_details(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *p_state ;
  Font *font ;
  FontSelInfo *font_sel_info ;
  PCL5Real x_pos, y_pos ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  p_state = pcl5_ctxt->print_state ;
  HQASSERT(p_state != NULL, "p_state is NULL") ;

  if (!get_active_font(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), &font, &font_sel_info))
    return FALSE ;

  HQASSERT(font != NULL, "font is NULL") ;

  get_cursor_position(pcl5_ctxt, &x_pos, &y_pos) ;

  p_state->font_state.max_underline_dist = font->underline_dist ;
  p_state->font_state.max_underline_thickness = font->underline_thickness ;

  /* Reset the underline start and end position whenever there is
     a vertical movement. */
  p_state->font_state.start_underline_position.x = x_pos ;
  p_state->font_state.start_underline_position.y = y_pos ;
  p_state->font_state.end_underline_position.x = x_pos ;
  p_state->font_state.end_underline_position.y = y_pos ;

  return TRUE ;
}

/* ========================================================================= */

/* Is wide_ch the first byte of a two byte character?
 * N.B. Assumes 2-byte character support is enabled.
 */
Bool is_first_byte_of_two_byte_char(PCL5Context *pcl5_ctxt, uint16 wide_ch)
{
  TextInfo *text_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(pcl5_ctxt->config_params.two_byte_char_support_enabled,
           "Expected 2-byte support enabled") ;

  text_info = get_text_info(pcl5_ctxt) ;
  HQASSERT(text_info != NULL, "text_info is NULL") ;

  return ( ( (text_info->text_parsing_method == 21) &&
             (wide_ch >= 0x21 && wide_ch <= 0xFF) ) ||
           ( (text_info->text_parsing_method == 31) &&
             ( (wide_ch >= 0x81 && wide_ch <= 0x9F) ||
               (wide_ch >= 0xE0 && wide_ch <= 0xFC) ) ) ||
           ( (text_info->text_parsing_method == 38) &&
             (wide_ch >= 0x80 && wide_ch <= 0xFF) ) ) ;
}

/* Grab a byte from the PCL5Context filestream and add it to wide_ch
 * as the second byte of a 2 byte character.
 * It is assumed that on entry, the value of wide_ch is equal to the
 * value of the first byte.
 * Indicate whether EOF was reached.
 */
void add_second_byte_to_two_byte_char(PCL5Context *pcl5_ctxt,
                                      uint16 *wide_ch,
                                      Bool *eof_reached)
{
  int32 next_byte ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(wide_ch != NULL, "wide_ch is NULL") ;
  HQASSERT(eof_reached != NULL, "eof_reached is NULL") ;

  HQASSERT(pcl5_ctxt->flptr != NULL, "filelist is NULL") ;
  HQASSERT(pcl5_ctxt->config_params.two_byte_char_support_enabled,
           "Expected 2-byte support enabled") ;

  *eof_reached = FALSE ;

  *wide_ch = (*wide_ch) << 8 ;

  /* What do we do if we hit EOF, may as well say we are done.
   * Testing shows that if the last byte is the start of a
   * 2 byte character, that character gets truncated.
   */
  if ((next_byte = Getc(pcl5_ctxt->flptr)) == EOF)
    *eof_reached = TRUE ;
  else
    *wide_ch |= (uint16)next_byte ;
}

/* ============================================================================
 * Operator callbacks are below here.
 * ============================================================================
 */

/* Font id */
Bool pcl5op_star_c_D(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  FontMgtInfo* font_mgt ;

  UNUSED_PARAM(int32, explicit_sign) ;

  if ( (value.integer < 0) || (value.integer > 32767) )
    return TRUE ;

  font_mgt = pcl5_get_font_management(pcl5_ctxt);

  if (font_mgt->string_id.buf != NULL) {
    mm_free(mm_pcl_pool, font_mgt->string_id.buf, font_mgt->string_id.length) ;
    font_mgt->string_id.buf = NULL ;
    font_mgt->string_id.length = 0 ;
  }

  font_mgt->font_id = value.integer ;

  return TRUE ;
}

Bool do_font_control(
  PCL5Context*  pcl5_ctxt,
  int32         command)
{
  static sw_datum pfin_params_array[] = {
    SW_DATUM_ARRAY(pfin_params_array + 1, 4),
    SW_DATUM_INTEGER(PCL_MISCOP_FONT),
    SW_DATUM_INTEGER(0),    /* action */
    SW_DATUM_INTEGER(0),    /* font id */
    SW_DATUM_STRING(""),    /* PFIN font name*/
    SW_DATUM_INTEGER(PCL_ALPHANUMERIC)
  };
  sw_datum * pfin_params = pfin_params_array ;
  FontMgtInfo* font_mgt = 0 ;
  FontInfo* font_info;
  FontSelInfo *dummy;
  Font*     font;
  sw_pfin_result rc;

  /* Parameter indices into the above array */
  enum {
    p_array = 0, p_reason, p_action, p_id, p_code, p_name = p_code, p_nametype
  } ;

  pfin_params[p_action].value.integer = command;

  if (command > FONTCTRL_DELETE_ALLTEMP) {
    font_mgt = pcl5_get_font_management(pcl5_ctxt);
    set_sw_datum_with_current_font_id(&pfin_params[p_id], font_mgt) ;
  }

  switch ( command ) {
  case FONTCTRL_DELETE_ALL:
  case FONTCTRL_DELETE_ALLTEMP:
    pfin_params[p_array].length = 2 ;
    break;

  case FONTCTRL_DELETE_FONTID_CHARCODE:
    pfin_params[p_array].length = 5 ;
    pfin_params[p_code].type = SW_DATUM_TYPE_INTEGER ;
    pfin_params[p_code].value.integer = font_mgt->character_code ;
    break;

  case FONTCTRL_DELETE_FONTID:
  case FONTCTRL_MAKETEMP_FONTID:
  case FONTCTRL_MAKEPERM_FONTID:
    pfin_params[p_array].length = 5 ;
    break;

  case FONTCTRL_CURRENTTEMP_FONTID:
    pcl5_ctxt->print_state->font_state.delete_font_ev_handler_error = FALSE;
    pfin_params[p_array].length = 5 ;
    font_info = pcl5_get_font_info(pcl5_ctxt);
    if (! get_active_font(pcl5_ctxt, font_info, &font, &dummy))
      return(FALSE) ;
    HQASSERT(font != NULL, "font is NULL") ;

    pfin_params[p_name].type = SW_DATUM_TYPE_STRING ;
    pfin_params[p_name].value.string = (const char*)font->name;
    pfin_params[p_name].length = font->name_length;
    break;

  default:
    HQFAIL("Invalid font control command");
    return TRUE ;
  }

  /* N.B. If this ends up deleting a font alias, as opposed to an actual
   *      font, the delete font event handler will not be called, so we
   *      must free the select by ID cache here.
   */
  if (command != FONTCTRL_MAKETEMP_FONTID &&
      command != FONTCTRL_MAKEPERM_FONTID)
    font_id_cache_free() ;

  rc = pfin_miscop(pcl5_ctxt->print_state->ufst, &pfin_params);
  if ( rc != SW_PFIN_SUCCESS )
    return(error_handler(pfin_error(rc)));

  if (command == FONTCTRL_CURRENTTEMP_FONTID &&
      pcl5_ctxt->print_state->font_state.delete_font_ev_handler_error) {
    pcl5_ctxt->print_state->font_state.delete_font_ev_handler_error = FALSE ;
    return(FALSE) ;
  }

  return(TRUE);
}

/* Font Control. */
Bool pcl5op_star_c_F(
  PCL5Context*  pcl5_ctxt,
  int32         explicit_sign,
  PCL5Numeric   value)
{
  UNUSED_PARAM(int32, explicit_sign);

#ifdef RQ64403
  /* Invalid values are simply ignored. */
  if (value.integer < FONTCTRL_DELETE_ALL ||
      value.integer > FONTCTRL_CURRENTTEMP_FONTID)
    return TRUE ;
#endif
  return(do_font_control(pcl5_ctxt, value.integer));
}

/* Symbol Set ID Code. */
Bool pcl5op_star_c_R(
  PCL5Context*  pcl5_ctxt,
  int32         explicit_sign,
  PCL5Numeric   value)
{
  FontMgtInfo* font_mgt;

  UNUSED_PARAM(int32, explicit_sign) ;

  if ( (value.integer < 0) || (value.integer > 32767) ) {
    return(TRUE);
  }

  font_mgt = pcl5_get_font_management(pcl5_ctxt);
  font_mgt->symbol_set_id = value.integer;

  return(TRUE);
}

/* Define Symbol Set */
Bool pcl5op_left_paren_f_W(
  PCL5Context*  pcl5_ctxt,
  int32         explicit_sign,
  PCL5Numeric   value)
{
  static sw_datum define_symbolset[] = {
    SW_DATUM_ARRAY(define_symbolset + 1, 3),
    SW_DATUM_INTEGER(PCL_MISCOP_DEFINE_SYMBOLSET),
    SW_DATUM_INTEGER(0),    /* symbol set id */
    SW_DATUM_STRING("")     /* symbol set data */
  };
  sw_datum* pfin_params;
  FontMgtInfo* font_mgt;
  sw_pfin_result rc;
  uint8*    p_symset;
  int32     symset_size;

  /* Parameter indices into the above array */
  enum {
    p_array = 0, p_reason, p_id, p_data
  } ;

  UNUSED_PARAM(int32, explicit_sign);

  /* Size is limited to 32K-1 bytes */
  symset_size = min(value.integer, 32767);

  /* Flush symbol set data if not enough data or can't get memory for it */
  if ( symset_size == 0 ) {
    return(TRUE);
  }
  if ( symset_size < 18 ) {
    (void)file_skip(pcl5_ctxt->flptr, symset_size, NULL);
    return(TRUE);
  }
  p_symset = mm_alloc(mm_pcl_pool, symset_size, MM_ALLOC_CLASS_PCL5_SYMSET);
  if ( p_symset == NULL ) {
    (void)file_skip(pcl5_ctxt->flptr, symset_size, NULL);
    return(TRUE);
  }

  /* Bail if can't read the symbol set data */
  if ( file_read(pcl5_ctxt->flptr, p_symset, symset_size, NULL) <= 0 ) {
    mm_free(mm_pcl_pool, p_symset, symset_size);
    return(TRUE);
  }

  /* Free the fontselection caches */
  font_sel_caches_free();

  /* Pass symbol set data to PFIN to look after */
  font_mgt = pcl5_get_font_management(pcl5_ctxt);
  define_symbolset[p_id].value.integer = font_mgt->symbol_set_id;

  define_symbolset[p_data].value.string = (const char*)p_symset;
  define_symbolset[p_data].length = symset_size;

  pfin_params = define_symbolset;
  rc = pfin_miscop(pcl5_ctxt->print_state->ufst, &pfin_params);
  /* PFIN takes a copy of the data, so we can free ours now */
  mm_free(mm_pcl_pool, p_symset, symset_size);
  if ( rc != SW_PFIN_SUCCESS ) {
    return(error_handler(pfin_error(rc)));
  }

  /* Since we currently have no way of knowing whether this symbolset
   * is in use, need to invalidate all our fonts.
   */
  /** \todo Only invalidate fonts using this symbolset.  Investigate
   *  whether fonts at lower MPE levels should be immediately
   *  reselected.  Should the default font be invalidated (and
   *  reselected) in this case too?
   */
  mark_fonts_invalid_for_id(pcl5_ctxt, INVALID_ID) ;

  return(TRUE);
}

Bool do_symset_control(
  PCL5Context*  pcl5_ctxt,
  int32         command)
{
  static sw_datum symbolset_cmd[] = {
    SW_DATUM_ARRAY(symbolset_cmd + 1, 3),
    SW_DATUM_INTEGER(PCL_MISCOP_SYMBOLSET),
    SW_DATUM_INTEGER(0),    /* symbol set action */
    SW_DATUM_INTEGER(0)     /* symbol set id */
  };
  sw_datum* pfin_params;
  FontMgtInfo* font_mgt;
  sw_pfin_result rc;

  /* Parameter indices into the above array */
  enum {
    p_array = 0, p_reason, p_action, p_id
  } ;

  /* Always send the last symbol set id set, if it's not needed it wont be read */
  switch ( command ) {
  case SYMSETCTRL_DELETE_ALL:
  case SYMSETCTRL_DELETE_ALLTEMP:
  case SYMSETCTRL_DELETE_SSID:

    /* Since we currently have no way of knowing whether this symbolset
     * is in use, need to invalidate all our fonts.
     */
    /** \todo Only invalidate fonts using this symbolset.  Investigate
     *  whether fonts at lower MPE levels should be immediately
     *  reselected.  Should the default font be invalidated (and
     *  reselected) in this case too?
     */
    font_sel_caches_free();
    mark_fonts_invalid_for_id(pcl5_ctxt, INVALID_ID) ;
    /* dropthrough */
  case SYMSETCTRL_MAKETEMP_SSID:
  case SYMSETCTRL_MAKEPERM_SSID:
    symbolset_cmd[p_action].value.integer = command;
    font_mgt = pcl5_get_font_management(pcl5_ctxt);
    symbolset_cmd[p_id].value.integer = font_mgt->symbol_set_id;

    pfin_params = symbolset_cmd;
    rc = pfin_miscop(pcl5_ctxt->print_state->ufst, &pfin_params);
    if ( rc != SW_PFIN_SUCCESS ) {
      return(error_handler(pfin_error(rc)));
    }
    break;

  default:
    HQFAIL("Invalid user defined symbol set command.");
    break;
  }

  return(TRUE);
}

/* Symbol Set Control. */
Bool pcl5op_star_c_S(
  PCL5Context*  pcl5_ctxt,
  int32         explicit_sign,
  PCL5Numeric   value)
{
  UNUSED_PARAM(int32, explicit_sign);

  /* Invalid values are simply ignored. */
  if (value.integer < SYMSETCTRL_DELETE_ALL ||
      value.integer > SYMSETCTRL_MAKEPERM_SSID ||
      value.integer == 3)
    return TRUE ;

  return(do_symset_control(pcl5_ctxt, value.integer));
}

/* Character code */
Bool pcl5op_star_c_E(
  PCL5Context*  pcl5_ctxt,
  int32         explicit_sign,
  PCL5Numeric   value)
{
  FontMgtInfo* font_mgt;

  UNUSED_PARAM(int32, explicit_sign) ;

  if ( (value.integer < 0) || (value.integer > 65535) ) {
    return(TRUE);
  }

  font_mgt = pcl5_get_font_management(pcl5_ctxt);
  font_mgt->character_code = value.integer;

  return(TRUE);
}

static
Bool send_char_data(
  PCL5Context*  pcl5_ctxt,
  FontMgtInfo*  font_mgt,
  uint8*        data,
  size_t        data_len)
{
  static sw_datum char_data[] = {
    SW_DATUM_ARRAY(char_data + 1, 4),
    SW_DATUM_INTEGER(PCL_MISCOP_DEFINE_GLYPH),
    SW_DATUM_INTEGER(0),    /* font id */
    SW_DATUM_INTEGER(0),    /* char code */
    SW_DATUM_STRING("")     /* character data */
  };
  static sw_datum char_data_alphanumeric[] = {
    SW_DATUM_ARRAY(char_data_alphanumeric + 1, 5),
    SW_DATUM_INTEGER(PCL_MISCOP_DEFINE_GLYPH),
    SW_DATUM_STRING(""),               /* font id */
    SW_DATUM_INTEGER(0),               /* char code */
    SW_DATUM_STRING(""),               /* character data */
    SW_DATUM_INTEGER(PCL_ALPHANUMERIC) /* data type */
  };
  sw_datum* pfin_params;
  sw_pfin_result rc;

  /* Parameter indices into the above arrays */
  enum {
    p_array = 0, p_reason, p_id, p_code, p_data, p_datatype
  } ;

  if (font_mgt->string_id.buf == NULL) {
    pfin_params = char_data;
  } else {
    pfin_params = char_data_alphanumeric;
  }

  /* Pass character data to PFIN to look after */
  set_sw_datum_with_current_font_id(&pfin_params[p_id], font_mgt) ;
  pfin_params[p_code].value.integer = font_mgt->character_code;

  pfin_params[p_data].value.string = (const char*)data;
  pfin_params[p_data].length = (int32)data_len;

  rc = pfin_miscop(pcl5_ctxt->print_state->ufst, &pfin_params);
  if ( rc != SW_PFIN_SUCCESS ) {
    return(error_handler(pfin_error(rc)));
  }

  return(TRUE);

} /* send_char_data */

Bool implicit_end_character_data(PCL5Context *pcl5_ctxt)
{
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;
  pcl5_suspend_execops() ;
  return TRUE ;
}

/* Character data */
Bool pcl5op_left_paren_s_W(
  PCL5Context*  pcl5_ctxt,
  int32         explicit_sign,
  PCL5Numeric   value)
{
  FontMgtInfo *font_mgt;
  uint8 *p_chardata, *new_data;
  int32 chardata_size, ch1, ch2;
  size_t new_len, orig_len;
  Bool success;
  int32 old_mode ;

  UNUSED_PARAM(int32, explicit_sign);

  /* Character data size is limited based on whether 2-byte support is
     enabled. */
  if ( pcl5_ctxt->config_params.two_byte_char_support_enabled ) {
    chardata_size = min(value.integer, 65535);
  } else {
    chardata_size = min(value.integer, 32767);
  }

  if ( chardata_size == 0 ) {
    return(TRUE);
  }

  if ( pcl5_ctxt->interpreter_mode != CHARACTER_DATA_MODE ) {
    /* Start probe on first call and before we start consuming character data */
    probe_begin(SW_TRACE_INTERPRET_PCL5_FONT, pcl5_ctxt);
  }

  /* Read in character data. If not possible (memory, fileio, etc.)
   * then just consume stream bytes and free off any earlier character
   * data (in case we are expecting a continuation block.
   */
  orig_len = garr_length(&pcl5_ctxt->print_state->char_data) ;

  if (pcl5_ctxt->interpreter_mode == CHARACTER_DATA_MODE) {
    if ((ch1 = Getc(pcl5_ctxt->flptr)) == EOF)
      return(TRUE);
    if ( --chardata_size == 0 )
      return(TRUE);
    if ((ch2 = Getc(pcl5_ctxt->flptr)) == EOF)
      return(TRUE);
    if ( --chardata_size == 0 )
      return(TRUE);

    HQASSERT(ch2 == 1, "We were expecting a character continuation block.") ;
  }

  if (! garr_extend(&pcl5_ctxt->print_state->char_data, chardata_size)) {
    (void)file_skip(pcl5_ctxt->flptr, chardata_size, NULL);
    garr_empty(&pcl5_ctxt->print_state->char_data);
    if ( pcl5_ctxt->interpreter_mode != CHARACTER_DATA_MODE ) {
      probe_end(SW_TRACE_INTERPRET_PCL5_FONT, pcl5_ctxt);
    }
    return(TRUE);
  }

  new_data = garr_data(&pcl5_ctxt->print_state->char_data) ;
  new_len = garr_length(&pcl5_ctxt->print_state->char_data) ;

  p_chardata = new_data + orig_len ;

  if ( file_read(pcl5_ctxt->flptr, p_chardata, chardata_size, NULL) <= 0 ) {
    garr_empty(&pcl5_ctxt->print_state->char_data);
    if ( pcl5_ctxt->interpreter_mode != CHARACTER_DATA_MODE ) {
      probe_end(SW_TRACE_INTERPRET_PCL5_FONT, pcl5_ctxt);
    }
    return(TRUE);
  }

  if (pcl5_ctxt->interpreter_mode == CHARACTER_DATA_MODE)
    return(TRUE) ;

  /* We need to look ahead to see if there is a continuation block for
     this character data. The PCL interpreter will return immediately
     when it finds a non character data command. */
  old_mode = pcl5_ctxt->interpreter_mode ;
  pcl5_ctxt->interpreter_mode = CHARACTER_DATA_MODE ;
  success = pcl5_execops(pcl5_ctxt) ;
  pcl5_ctxt->interpreter_mode = old_mode ;

  if (success) {
    new_data = garr_data(&pcl5_ctxt->print_state->char_data) ;
    new_len = garr_length(&pcl5_ctxt->print_state->char_data) ;
    font_mgt = pcl5_get_font_management(pcl5_ctxt);
    success = send_char_data(pcl5_ctxt, font_mgt, new_data, new_len);
  }

  garr_empty(&pcl5_ctxt->print_state->char_data);

  probe_end(SW_TRACE_INTERPRET_PCL5_FONT, pcl5_ctxt);

  return(success);
}

/* The Font Header command is used to download font header data to the printer. */
Bool pcl5op_right_paren_s_W(
  PCL5Context*  pcl5_ctxt,
  int32         explicit_sign,
  PCL5Numeric   value)
{
  static sw_datum define_font[] = {
    SW_DATUM_ARRAY(define_font + 1, 3),
    SW_DATUM_INTEGER(PCL_MISCOP_DEFINE_FONT),
    SW_DATUM_INTEGER(0),    /* font id */
    SW_DATUM_STRING("")     /* font data */
  };
  static sw_datum define_font_alphanumeric[] = {
    SW_DATUM_ARRAY(define_font_alphanumeric + 1, 4),
    SW_DATUM_INTEGER(PCL_MISCOP_DEFINE_FONT),
    SW_DATUM_STRING(""),               /* font alphanumeric id */
    SW_DATUM_STRING(""),               /* font data */
    SW_DATUM_INTEGER(PCL_ALPHANUMERIC) /* data type (namespace) of id string */
  };
  sw_datum* pfin_params;
  FontMgtInfo* font_mgt;
  sw_pfin_result rc;
  uint8*    p_hdr;
  int32     hdr_size = value.integer;

  /* Parameter indices into the above array */
  enum {
    p_array = 0, p_reason, p_id, p_data, p_datatype
  } ;

  UNUSED_PARAM(int32, explicit_sign);

  /* This is to check whether our delete_font PFIN event handler failed */
  pcl5_ctxt->print_state->font_state.delete_font_ev_handler_error = FALSE;

  /* To match the spec we should limit the header size, but many jobs ignore the
   * limit, so we do to. Plus it's more sensible than leaving the remaining
   * binary data to be processed as text. */

  /* Filter out blatantly small font headers */
  if ( hdr_size == 0 ) {
    return(TRUE);
  }
  probe_begin(SW_TRACE_INTERPRET_PCL5_FONT, pcl5_ctxt);
  if ( hdr_size < 64 ) {
    HQFAIL("Small font header seen, please report with job to core rip");
    (void)file_skip(pcl5_ctxt->flptr, hdr_size, NULL);
    probe_end(SW_TRACE_INTERPRET_PCL5_FONT, pcl5_ctxt);
    return(TRUE);
  }

  /* Flush font header data if can't get memory for it */
  p_hdr = mm_alloc(mm_pcl_pool, hdr_size, MM_ALLOC_CLASS_PCL5_FONTHDR);
  if ( p_hdr == NULL ) {
    (void)file_skip(pcl5_ctxt->flptr, hdr_size, NULL);
    probe_end(SW_TRACE_INTERPRET_PCL5_FONT, pcl5_ctxt);
    return(TRUE);
  }

  /* Read font header data */
  if ( file_read(pcl5_ctxt->flptr, p_hdr, hdr_size, NULL) <= 0 ) {
    mm_free(mm_pcl_pool, p_hdr, hdr_size);
    probe_end(SW_TRACE_INTERPRET_PCL5_FONT, pcl5_ctxt);
    return(TRUE);
  }

  /** \todo - decision on whether to process data has to be done after reading the
   * data, else we start to parse the data, which would be oh so wrong */

  /* Pass font header data to PFIN to look after */
  font_mgt = pcl5_get_font_management(pcl5_ctxt);

  if (font_mgt->string_id.buf == NULL) {
    pfin_params = define_font;
  } else {
    pfin_params = define_font_alphanumeric;
  }

  set_sw_datum_with_current_font_id(&pfin_params[p_id], font_mgt) ;

  pfin_params[p_data].value.string = (const char*)p_hdr;
  pfin_params[p_data].length = hdr_size;

  rc = pfin_miscop(pcl5_ctxt->print_state->ufst, &pfin_params);
  /* PFIN takes a copy of the data, so we can free ours now */
  mm_free(mm_pcl_pool, p_hdr, hdr_size);
  if ( rc != SW_PFIN_SUCCESS ) {
    probe_end(SW_TRACE_INTERPRET_PCL5_FONT, pcl5_ctxt);
    return(error_handler(pfin_error(rc)));
  }
  if (pcl5_ctxt->print_state->font_state.delete_font_ev_handler_error) {
    pcl5_ctxt->print_state->font_state.delete_font_ev_handler_error = FALSE;
    probe_end(SW_TRACE_INTERPRET_PCL5_FONT, pcl5_ctxt);
    return(FALSE);
  }

  probe_end(SW_TRACE_INTERPRET_PCL5_FONT, pcl5_ctxt);
  return(TRUE);
}

/* Switch to Primary Font */
Bool pcl5op_control_SI(PCL5Context *pcl5_ctxt)
{
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  if (! switch_to_font(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), PRIMARY, TRUE))
    return FALSE ;

  return TRUE ;
}


/* Switch to Secondary Font */
Bool pcl5op_control_SO(PCL5Context *pcl5_ctxt)
{
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  if (! switch_to_font(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), SECONDARY, TRUE))
    return FALSE ;

  return TRUE ;
}

/* Primary Symbolset */
Bool pcl5op_left_paren_symbolset(PCL5Context *pcl5_ctxt, int32 explicit_sign,
                                 PCL5Numeric value)
{
  UNUSED_PARAM(int32, explicit_sign) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* Value.integer represents the Symbol Set ID code. */
  set_symbolset(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), value.integer, PRIMARY) ;
  return TRUE ;
}


/* Primary Spacing */
Bool pcl5op_left_paren_s_P(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  /* N.B. Printer treats negative values as positive */
  PCL5Integer spacing = abs(value.integer) ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* Ignore out of range values */
  if (spacing != 0 && spacing != 1)
    return TRUE ;

  set_spacing(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), spacing, PRIMARY) ;

  return TRUE ;
}


/* Primary Pitch */
Bool pcl5op_left_paren_s_H(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PCL5Real pitch = value.real ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* N.B. It seems values are limited to the range */
  pitch = pcl5_limit_to_range(pitch, 0.10f, 576.0f) ;

  /** \todo Should this be done before range check ? */
  pitch = pcl5_round_to_2d(pitch) ;

  set_pitch(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), pitch, PRIMARY) ;

  return TRUE ;
}


/* Primary Height */
Bool pcl5op_left_paren_s_V(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PCL5Real height = value.real ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* N.B. It seems values are limited to the range */
  height = pcl5_limit_to_range(height, 0.25f, 999.75f) ;

  /** \todo Should this be done before range check ? */
  height = pcl5_round_to_2d(height) ;

  set_height(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), height, PRIMARY) ;

  return TRUE ;
}


/* Primary Style */
Bool pcl5op_left_paren_s_S(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  /* N.B. Printer treats negative values as positive */
  PCL5Integer style = abs(value.integer) ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* Limit large positive values */
  if (style > 32767)
    style = 32767 ;

  set_style(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), style, PRIMARY) ;

  return TRUE ;
}


/* Primary Stroke Weight */
Bool pcl5op_left_paren_s_B(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PCL5Integer weight = value.integer ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* Limit values to the range */
  if (weight < -7)
    weight = -7 ;
  else if (weight > 7)
    weight = 7 ;

  set_weight(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), weight, PRIMARY) ;

  return TRUE ;
}


/* Primary Typeface */
Bool pcl5op_left_paren_s_T(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  /* N.B. Printer treats negative values as positive */
  PCL5Integer typeface = abs(value.integer) ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* Limit large positive values */
  if (typeface > 65535)
    typeface = 65535 ;

  set_typeface(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), typeface, PRIMARY) ;

  return TRUE ;
}

/* Primary Selection by ID */
Bool pcl5op_left_paren_X(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  uint8 *aliased_string ;
  int32 aliased_length ;
  uint16 aliased_id ;
  /* N.B. Printer treats negative values as positive */
  int32 id = abs(value.integer) ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* Spec says ignore out of range values */
  if (id > 32767)
    return TRUE ;

  find_font_numeric_alias(pcl5_ctxt, (uint16)id, &aliased_string, &aliased_length, &aliased_id) ;

  do_fontselection_by_id(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), aliased_id,
                         aliased_string, aliased_length, PRIMARY) ;

  return TRUE ;
}


/* Default Primary Font */
Bool pcl5op_left_paren_at(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  if (value.integer != 3)
    return TRUE ;

  if (! do_default_font(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), PRIMARY))
    return FALSE ;

  return TRUE ;
}

/* Secondary Symbolset */
Bool pcl5op_right_paren_symbolset(PCL5Context *pcl5_ctxt, int32 explicit_sign,
                                  PCL5Numeric value)
{
  UNUSED_PARAM(int32, explicit_sign) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* Value.integer represents the Symbol Set ID code. */
  set_symbolset(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), value.integer, SECONDARY) ;
  return TRUE ;
}


/* Secondary Spacing */
Bool pcl5op_right_paren_s_P(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  /* N.B. Printer treats negative values as positive */
  PCL5Integer spacing = abs(value.integer) ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* Ignore out of range values */
  if (spacing != 0 && spacing != 1)
    return TRUE ;

  set_spacing(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), spacing, SECONDARY) ;

  return TRUE ;
}


/* Secondary Pitch */
Bool pcl5op_right_paren_s_H(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PCL5Real pitch = value.real ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* N.B. It seems values are limited to the range */
  pitch = pcl5_limit_to_range(pitch, 0.10f, 576.0f) ;

  /** \todo Should this be done before range check ? */
  pitch = pcl5_round_to_2d(pitch) ;

  set_pitch(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), pitch, SECONDARY) ;

  return TRUE ;
}


/* Secondary Height */
Bool pcl5op_right_paren_s_V(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PCL5Real height = value.real ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* N.B. It seems values are limited to the range */
  height = pcl5_limit_to_range(height, 0.25f, 999.75f) ;

  /** \todo Should this be done before range check ? */
  height = pcl5_round_to_2d(height) ;

  set_height(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), height, SECONDARY) ;

  return TRUE ;
}


/* Secondary Style */
Bool pcl5op_right_paren_s_S(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  /* N.B. Printer treats negative values as positive */
  PCL5Integer style = value.integer ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* Limit large positive values */
  if (style > 32767)
    style = 32767 ;

  set_style(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), style, SECONDARY) ;

  return TRUE ;
}


/* Secondary Stroke Weight */
Bool pcl5op_right_paren_s_B(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PCL5Integer weight = value.integer ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* Limit values to the range */
  if (weight < -7)
    weight = -7 ;
  else if (weight > 7)
    weight = 7 ;

  set_weight(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), weight, SECONDARY) ;

  return TRUE ;
}


/* Secondary Typeface */
Bool pcl5op_right_paren_s_T(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  /* N.B. Printer treats negative values as positive */
  PCL5Integer typeface = abs(value.integer) ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* Limit large positive values */
  if (typeface > 65535)
    typeface = 65535 ;

  set_typeface(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), typeface, SECONDARY) ;

  return TRUE ;
}


/* Pitch Mode */
/* N.B. PCL5 Tech Ref doesn't mention this command, but CET18_08 tests for it */
Bool pcl5op_ampersand_k_S(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PCL5Real pitch ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  switch (value.integer) {
    case 0:
      /* Standard pitch */
      pitch = 10 ;
      break ;

    case 2:
      /* Compressed pitch
       *
       * N.B. Or possibly 16.67, but CET 14_06 reports this as 16.66, and PCL5
       * Tech Ref has an example of the (normal) pitch command using the value
       * 16.66, which doesn't prove anything, but may suggest this was the
       * value.  It looks pretty close to 50 characters in 3 inches, (which
       * would be 16.67 to 2 decimal places).
       */
      pitch = 16.66f ;
      break ;

    case 4:
      /* Elite pitch */
      pitch = 12 ;
      break ;

    default:
      return TRUE ;
  }

  set_pitch(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), pitch, PRIMARY) ;
  set_pitch(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), pitch, SECONDARY) ;

  return TRUE ;
}


/* Secondary Selection by ID */
Bool pcl5op_right_paren_X(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  uint8 *aliased_string ;
  int32 aliased_length ;
  uint16 aliased_id ;
  /* N.B. Printer treats negative values as positive */
  int32 id = abs(value.integer) ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* Spec says ignore out of range values */
  if (id > 32767)
    return TRUE ;

  find_font_numeric_alias(pcl5_ctxt, (uint16)id, &aliased_string, &aliased_length, &aliased_id) ;

  do_fontselection_by_id(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), aliased_id,
                         aliased_string, aliased_length, SECONDARY) ;

  return TRUE ;
}


/* Default Secondary Font */
Bool pcl5op_right_paren_at(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  UNUSED_PARAM(int32, explicit_sign) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  if (value.integer != 3)
    return TRUE ;

  if (! do_default_font(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), SECONDARY))
    return FALSE ;

  return TRUE ;
}

/* Transparent print data. */
Bool pcl5op_ampersand_p_X(
  PCL5Context*  pcl5_ctxt,
  int32         explicit_sign,
  PCL5Numeric   value)
{
  TextInfo *text_info ;
  FontSelInfo *font_sel_info ;
  FontInfo *font_info ;
  Font *font ;
  int32 num_bytes;
  int32 ch ;
  int32 old_mode ;
  uint16 wide_ch ;
  Bool eof_reached = FALSE ;

  UNUSED_PARAM(int32, explicit_sign) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  text_info = get_text_info(pcl5_ctxt) ;
  HQASSERT(text_info != NULL, "text_info is NULL") ;

  num_bytes = min(value.integer, 32767) ;

  if (num_bytes == 0)
    return TRUE ;

  if (! pcl5_start_text_run(pcl5_ctxt, &font_sel_info, &font_info, &font))
    return TRUE ;

  old_mode = pcl5_ctxt->interpreter_mode ;
  pcl5_ctxt->interpreter_mode = TRANSPARENT_PRINT_DATA_MODE ;

  do {
    if ((ch = Getc(pcl5_ctxt->flptr)) == EOF)
      break ;

    wide_ch = (uint16)ch ;

    if ( pcl5_ctxt->config_params.two_byte_char_support_enabled ) {
      /* Deal with two byte characters. */
      if ( is_first_byte_of_two_byte_char(pcl5_ctxt, wide_ch) ) {
        add_second_byte_to_two_byte_char(pcl5_ctxt, &wide_ch, &eof_reached) ;
        if (eof_reached)
          break ;
        else
          num_bytes-- ;
      }
    }

    if (! pcl5_text_character(pcl5_ctxt, font_info, &font, wide_ch))
      break ;

  } while ( --num_bytes > 0 );

  (void)pcl5_end_text_run(pcl5_ctxt, FALSE) ;

  pcl5_ctxt->interpreter_mode = old_mode ;
  return TRUE ;
}

/* Underline enable. */
Bool pcl5op_ampersand_d_D(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PCL5PrintState *p_state ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* Default invalid values. */
  if (value.integer != 0 && value.integer != 3)
    value.integer = 0;

  p_state = pcl5_ctxt->print_state ;
  HQASSERT(p_state != NULL, "p_state is NULL") ;

  /* If we get a new underline mode while already in an underline
     mode, draw it. */
  if (p_state->font_state.underline_mode != NO_UNDERLINE)
    underline_callback(pcl5_ctxt, TRUE) ;

  p_state->font_state.underline_mode = value.integer ;

  if (! reset_underline_details(pcl5_ctxt))
    return FALSE ;

  return TRUE ;
}

/* Underline disable. */
Bool pcl5op_ampersand_d_at(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PCL5PrintState *p_state ;

  UNUSED_PARAM(int32, explicit_sign) ;
  UNUSED_PARAM(PCL5Numeric, value) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  p_state = pcl5_ctxt->print_state ;
  HQASSERT(p_state != NULL, "p_state is NULL") ;

  underline_callback(pcl5_ctxt, TRUE) ;

  /* Reset underline information. */
  p_state->font_state.underline_mode = NO_UNDERLINE ;
  p_state->font_state.max_underline_dist = 0.0 ;
  p_state->font_state.max_underline_thickness = 0.0 ;
  p_state->font_state.start_underline_position.x = 0 ;
  p_state->font_state.start_underline_position.y = 0 ;
  p_state->font_state.end_underline_position.x = 0 ;
  p_state->font_state.end_underline_position.y = 0 ;

  return TRUE ;
}

/* Do not call this function directly, but rather
   underline_callback(). */
static Bool draw_underline(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *p_state ;
  PCL5Real x_pos, y_pos, dots_below_base_line ;
  SYSTEMVALUE pos[2] ;
  OMATRIX new_ctm_inverse ;
#if 0
  SYSTEMVALUE dx, dy ;
#endif
  PCL5Ctms *pcl5_ctms ;

  /* We do this lazily now {SAB} */
  /* Call the PCL5 wrapper
   * for finishaddchardisplay for consistency. */
  if (! pcl5_flush_text_run(pcl5_ctxt, 1))
    return FALSE ;

  p_state = pcl5_ctxt->print_state ;

  pcl5_ctms = get_pcl5_ctms(pcl5_ctxt) ;

  if (! matrix_inverse(ctm_current(pcl5_ctms), &new_ctm_inverse))
    return TRUE ;

  /* Ensure we have the correct PS CTM */
  ctm_install(pcl5_ctms);

  get_cursor_position(pcl5_ctxt, &x_pos, &y_pos) ;

  { /* Calculate width and offset from base. */
    /* In internal units, line width is; 3 x (7200 / 300) and
       dots_below_base_line is 5 x (7200 / 300). i.e. They are
       constants. */
    gstateptr->thestyle.linewidth = 72 ;

    if (p_state->font_state.underline_mode == FLOATING_UNDERLINE) {
      /* In internal units, dots_below_base_line is (7200 / 72) *
         dist */
      dots_below_base_line = (p_state->font_state.max_underline_dist * 100) ;
      /* I see no evidence that we should be using the underline
         thickness in the spec. */
      dots_below_base_line += 48 ;
    } else {
      /* Because dostroke() draws the line from its center, we need to
         move the line down 2 pixels (which is 48). */
      dots_below_base_line = 120 + 48 ;
    }
  }

#if 0
  /* Make sure vertical position does not touch 4 pixels by snapping
     the cursor position to the top of a pixel. */
  {
    dx = p_state->font_state.start_underline_position.y ;
    dy = p_state->font_state.start_underline_position.y ;
    MATRIX_TRANSFORM_DXY(dx, dy, dx, dy, ctm_current(pcl5_ctms)) ;
    if (p_state->font_state.start_underline_position.y >= 0) {
      dy = dy < 0 ? -dy : dy ;
      dx = dx < 0 ? -dx : dx ;
    }
    dy = (SYSTEMVALUE)((uint32)dy) ; /* Truncate to whole number. */
    dx = (SYSTEMVALUE)((uint32)dx) ;
    MATRIX_TRANSFORM_DXY(dx, dy, dx, dy, &new_ctm_inverse) ;
    if (p_state->font_state.start_underline_position.y >= 0) {
      dy = dy < 0 ? -dy : dy ;
    }
    p_state->font_state.start_underline_position.y = dy ;
    p_state->font_state.end_underline_position.y = dy ;
  }
#endif

  gs_newpath() ;

  pos[0] = (SYSTEMVALUE)p_state->font_state.start_underline_position.x ;
  pos[1] = (SYSTEMVALUE)p_state->font_state.start_underline_position.y + dots_below_base_line ;

  if (! gs_moveto(TRUE, pos, &gstateptr->thepath))
    return TRUE ;

  pos[0] = (SYSTEMVALUE)p_state->font_state.end_underline_position.x ;
  pos[1] = (SYSTEMVALUE)p_state->font_state.end_underline_position.y + dots_below_base_line ;

  if (! gs_lineto(TRUE, FALSE, pos, &gstateptr->thepath))
    return TRUE ;

  if (! path_close(CLOSEPATH, &gstateptr->thepath))
    return TRUE ;

  {
    STROKE_PARAMS params ;
    Bool result;

    if ( !set_current_color(pcl5_ctxt) )
      return FALSE;

    set_current_pattern(pcl5_ctxt, PCL5_CURRENT_PATTERN);

    set_gstate_stroke(&params, &gstateptr->thepath, NULL, FALSE) ;

    /* Ensure the underline is drawn as text. */
    textContextEnter();
    result = dostroke(&params, GSC_FILL, STROKE_NORMAL);
    textContextExit();

    /* Ensure that we do any DEVICE_SETG that may be needed after this point */
    /** \todo Is this necessary ? */
    p_state->setg_required += 1 ;

    if (! result)
      return TRUE;
  }

  set_cursor_position(pcl5_ctxt, x_pos, y_pos) ;


  return TRUE ;
}

/* This function gets called when-ever there is a cursor position
   change in either the X or Y direction. In it we deal with:

   - Font underlining
*/
Bool underline_callback(PCL5Context *pcl5_ctxt, Bool force_draw)
{
  PCL5PrintState *p_state ;
  p_state = pcl5_ctxt->print_state ;

  /* Safety for recursive calls. */
  if (p_state->font_state.within_underline_callback)
    return TRUE ;

  p_state->font_state.within_underline_callback = TRUE ;

  if (p_state->font_state.underline_mode != NO_UNDERLINE) {
    PCL5Real x_pos, y_pos ;
    get_cursor_position(pcl5_ctxt, &x_pos, &y_pos) ;

    /* We have a vertical cursor position movement. Need to draw the underline. */
    if (p_state->font_state.end_underline_position.y != y_pos || force_draw) {

      /* Avoid drawing zero length underlines. */
      if (p_state->font_state.start_underline_position.x !=
          p_state->font_state.end_underline_position.x) {
        if (! draw_underline(pcl5_ctxt)) {
          p_state->font_state.within_underline_callback = FALSE ;
          return FALSE ;
        }
      }

      /* Reset the underline start and end position whenever there is
         a vertical movement. */
      if (! reset_underline_details(pcl5_ctxt))
        return FALSE ;

    } else if (p_state->font_state.end_underline_position.x < x_pos) {
      p_state->font_state.end_underline_position.x = x_pos ;
      /*
    } else if (p_state->font_state.start_underline_position.x > x_pos) {
      p_state->font_state.start_underline_position.x = x_pos ;
      */
    }
  }

  p_state->font_state.within_underline_callback = FALSE ;
  return TRUE ;
}

/* Character Text Path Direction. */
Bool pcl5op_ampersand_c_T(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PCL5PrintState* print_state ;
  FontInfo *font_info ;
  TextInfo *text_info ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  print_state = pcl5_ctxt->print_state ;
  font_info = pcl5_get_font_info(pcl5_ctxt) ;
  HQASSERT(font_info != NULL, "font_info is NULL") ;
  text_info = get_text_info(pcl5_ctxt) ;
  HQASSERT(text_info != NULL, "text_info is NULL") ;

  switch (value.integer) {
  case -1:
    if (!text_info->vertical_text_path) {
      print_state->setg_required += 1 ;
      print_state->font_state.text_path_changed = TRUE ;
    }
    text_info->vertical_text_path = TRUE ;
    break ;

  case 0:
    if (text_info->vertical_text_path) {
      print_state->setg_required += 1 ;
      print_state->font_state.text_path_changed = TRUE ;
    }
    text_info->vertical_text_path = FALSE ;
    break ;

  default:
    /* N.B. Invalid values are ignored */
    break ;
  }

  return TRUE ;
}

/* Text parsing method. */
Bool pcl5op_ampersand_t_P(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  TextInfo *text_info ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  text_info = get_text_info(pcl5_ctxt) ;
  HQASSERT(text_info != NULL, "text_info is NULL") ;

  switch (value.integer) {
  case 0: case 1: case 21: case 31: case 38:
    text_info->text_parsing_method = value.integer ;
    break ;
  default: /** \todo What do we do with invalid values? */
    break ;
  }

  return TRUE ;
}

/* ============================================================================
* Log stripped */
