/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pagecontrol.c(EBDSDK_P.1) $
 * $Id: src:pagecontrol.c,v 1.135.1.1.1.1 2013/12/19 11:25:02 anon Exp $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the PCL5 "Page Control" category.
 *
 * Page Size                         ESC & l # A
 * Paper (Media) Source              ESC & l # H
 * Media Type (Obsolete?)            ESC & l # M
 * Page Length (Obsolete)            ESC & l # P
 * Orientation                       ESC & l # O
 * Print Direction                   ESC & a # P
 * Left Margin                       ESC & a # L
 * Right Margin                      ESC & a # M
 * Clear Horizontal Margins          ESC 9
 * Top Margin                        ESC & l # E
 * Text Length                       ESC & l # F
 * Perforation Skip                  ESC & l # L
 * Horizontal Motion Index           ESC & k # H
 * Vertical Motion Index             ESC & l # C
 * Line Spacing                      ESC & l # D
 * Custom Paper Width                ESC & f # I
 * Custom Paper Length/Height        ESC & f # J
 */

#include "core.h"
#include "pagecontrol.h"

#include "pcl5context_private.h"
#include "pcl5state.h"
#include "printenvironment_private.h"
#include "cursorpos.h"
#include "pcl5scan.h"
#include "pcl5fonts.h"
#include "jobcontrol.h"
#include "pcl5raster.h"
#include "hpgl2config.h"
#include "pcl5ctm.h"
#include "macros.h"
#include "pcl.h"

#include "fileio.h"
#include "monitor.h"
#include "display.h"
#include "namedef_.h"
#include "miscops.h"
#include "swcopyf.h"
#include "params.h"
#include "stacks.h"
#include "gstate.h"

Bool set_printdirection(PCL5Context *pcl5_ctxt, uint32 printdirection);
void set_vmi_from_formlines(PCL5Context *pcl5_cxt, uint32 value);

/**
 * Force the current page to be rendered.
 */
static Bool renderPage(PCL5Context *pcl5_ctxt)
{
  ps_context_t *pscontext ;
  Bool success ;

  HQASSERT(pcl5_ctxt->corecontext != NULL, "No core context") ;
  pscontext = pcl5_ctxt->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;

  success = showpage_(pscontext);

  /* Now that rendering is complete, the user pattern cache can be
     purged of zombies. */
  pcl5_id_cache_kill_zombies(pcl5_ctxt->resource_caches.user);
  pcl5_id_cache_kill_zombies(pcl5_ctxt->resource_caches.hpgl2_user);

  return success;
}

PageCtrlInfo* get_page_ctrl_info(PCL5Context *pcl5_ctxt)
{
  PCL5PrintEnvironment *mpe ;
  PageCtrlInfo *page_ctrl_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  mpe = get_current_mpe(pcl5_ctxt) ;
  HQASSERT(mpe != NULL, "mpe is NULL") ;

  page_ctrl_info = &(mpe->page_ctrl) ;

  return page_ctrl_info ;
}

const PageCtrlInfo* get_default_page_ctrl_info(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *print_state ;
  const PCL5PrintEnvironment *mpe ;
  const PageCtrlInfo *page_ctrl_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  mpe = get_default_mpe(print_state) ;
  HQASSERT(mpe != NULL, "mpe is NULL") ;

  page_ctrl_info = &(mpe->page_ctrl) ;

  return page_ctrl_info ;
}

/* See header for doc. */
void default_page_control(PageCtrlInfo* self, PCL5ConfigParams* config_params)
{
  /* Note that we can't use an InitChecker here, as the structure contains
  doubles which can introduce alignment gaps (doubles like to be 64-bit
  aligned). */

#ifdef A4_PAGE_SIZE
  self->page_size = A4;
#else
  self->page_size = LETTER;
#endif

  self->print_direction = 0;
  self->orientation = 0;
  self->paper_source = MAIN_SOURCE;
  self->media_type = 0;
  self->vmi = 1200;
  self->hmi = 720;
  self->top_margin = DEFAULT_TOP_MARGIN;
  self->text_length = DEFAULT_TEXT_LENGTH;
  self->left_margin = 0;
  self->right_margin = DEFAULT_PAGE_WIDTH;
  self->perforation_skip = TRUE;
  self->line_termination = config_params->default_line_termination;

  self->physical_page_width = DEFAULT_PHYSICAL_PAGE_WIDTH;
  self->physical_page_length = DEFAULT_PHYSICAL_PAGE_LENGTH;
  self->clip[0] = self->clip[1] = self->clip[2] = self->clip[3] = DEFAULT_CLIP_WIDTH;
  self->portrait_offset = DEFAULT_PORTRAIT_OFFSET;
  self->landscape_offset = DEFAULT_LANDSCAPE_OFFSET;
  self->page_width = DEFAULT_PAGE_WIDTH;
  self->page_length = DEFAULT_PAGE_LENGTH;
  self->max_text_length = DEFAULT_MAX_TEXT_LENGTH;

  /* The default unit of measure for PCL units is 300 dpi. */
  self->pcl_unit_size = 7200 / 300;
}


/* Unpack the optional parameters dict, overwriting relevant pagecontrol
 * settings to complete the PJL current environment (as understood by
 * core).
 */
Bool pagecontrol_apply_pjl_changes(PCL5Context *pcl5_ctxt, PCL5ConfigParams* config_params)
{
  PageCtrlInfo *page_info ;

  /** \todo PCL5VMI */

  HQASSERT( pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT( config_params != NULL, "config_params is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT( page_info != NULL, "page_info is NULL") ;

  if ( config_params->default_line_termination >= 0 &&
       config_params->default_line_termination < 4 )
    page_info->line_termination = config_params->default_line_termination ;

  return TRUE ;
}


void save_page_control(PCL5Context *pcl5_ctxt,
                       PageCtrlInfo *to,
                       PageCtrlInfo *from,
                       Bool overlay)
{
  int32 i ;

  HQASSERT(to != NULL && from != NULL, "PageCtrlInfo is NULL") ;

  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;

  if (overlay) {
    /* N.B. The only items here explicitly mentioned in the Tech Ref
     * are page size, orientation and paper source.  To keep in the spirit of
     * this we should probably also change anything which is associated with a
     * command causing a setpagedevice call, (and it seems likely that these
     * commands should be ignored if doing an overlay macro).
     *
     * It is also not clear whether margins should be defaulted to PJL
     * values, and if so whether the HPGL2 picture frame should be
     * defaulted to its original size and position.  (This is
     * currently handled in pcl5state.c, but should probably be done
     * here.)
     */

    to->page_size = from->page_size ;
    to->print_direction = from->print_direction ;
    to->orientation = from->orientation ;
    to->paper_source = from->paper_source ;
    to->media_type = from->media_type ;

    to->physical_page_width = from->physical_page_width ;
    to->physical_page_length = from->physical_page_length ;

    for (i = 0; i < 4; i++)
      to->clip[i] = from->clip[i] ;

    to->portrait_offset = from->portrait_offset ;
    to->landscape_offset = from->landscape_offset ;
    to->page_width = from->page_width ;
    to->page_length = from->page_length ;
  }
}

void restore_page_control(PCL5Context *pcl5_ctxt,
                          PageCtrlInfo *to,
                          PageCtrlInfo *from)
{
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;

  /* N.B. Media type is being copied on save and restore for all macros.
   *      This is another way of saying it is not really part of the MPE,
   *      which is good because otherwise there is an akward issue of
   *      what to do if the type is found to be different on macro exit.
   */
  /** \todo Consider moving this to PCL5PrintState, and test whether same
   *  applies to e.g. paper source and other pagedevice items.
   */
  to->media_type = from->media_type ;
}

/* ========================================================================= */
void invalidate_hmi(PCL5Context *pcl5_ctxt)
{
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  page_info->hmi = INVALID_HMI ;
}

/* Convert HMI from font (in points) to HMI for page (in PCL internal units),
 * rounding to the nearest PCL Unit, (whose size depends on the UOM).
 */
void scale_hmi_for_page(PCL5Context *pcl5_ctxt, PCL5Real font_hmi)
{
  PageCtrlInfo *page_info ;
  PCL5Real internal_units ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;
  HQASSERT(font_hmi >= 0, "font_hmi is negative") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  /* Calculate the number of PCL Internal Units per character */
  internal_units = font_hmi * 100 ;

  /* Round the number of PCL Internal Units to the nearest PCL unit */
  page_info->hmi = round_pcl_internal_to_pcl_unit(pcl5_ctxt, internal_units) ;
}

/* Set the HMI value which is provided in 1/120 inch increments */
Bool set_hmi_directly(PCL5Context *pcl5_ctxt, PCL5Real value)
{
  PCL5PrintState *print_state ;
  PageCtrlInfo *page_info ;
  FontInfo *font_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;
  HQASSERT(value >= 0 && value <= 32767, "Invalid HMI value") ;

  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  font_info = pcl5_get_font_info(pcl5_ctxt) ;
  HQASSERT(font_info != NULL, "font_info is NULL") ;

  /* This will do any pending fontselection, to ensure the HMI about to be
   * set up here is not overwritten when the fontselection takes place.
   */
  if (page_info->hmi == INVALID_HMI) {
    if (! set_hmi_from_font(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), -1))
      return FALSE ;
  }

  /* Scale to PCL internal units, i.e * 7200/120 and round to nearest
   * whole PCL internal unit.
   */
  page_info->hmi = (int32) ((value * 60) + 0.5f) ;

  return TRUE ;
}


/* Set the VMI value which is provided in 1/48 inch increments */
void set_vmi_directly(PCL5Context *pcl5_ctxt, PCL5Real value)
{
  PageCtrlInfo *page_info ;
  uint32 vmi ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;
  HQASSERT(value >= 0 && value <= 32767, "Invalid VMI value") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  /* Scale to PCL internal units, i.e * 7200/48 and round to nearest
   * whole PCL internal unit.
   */
  vmi = (int32) ((value * 150) + 0.5f) ;

  /* Ignore the command if VMI greater than logical page length */
  if (vmi <= page_info->page_length) {
    page_info->vmi = vmi ;

  if (! cursor_explicitly_set(pcl5_ctxt))
    set_default_cursor_y_position(pcl5_ctxt) ;
  }
}


/* Set the VMI from lines-per-inch */
void set_vmi_from_lpi(PCL5Context *pcl5_ctxt, uint32 value)
{
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  /* N.B. The value 0 appears to be being treated as 12 */
  if (value == 0)
    value = 12 ;

  /* Scale to PCL internal units */
  /* N.B. Valid values are such that no precision is lost here */
  value = 7200 / value ;

  /* Ignore the command if VMI greater than logical page length
   *
   * N.B. It doesn't explicitly say to do this in the Tech Ref,
   * and this has not been tested, so is by analogy with the
   * standard VMI command.
   */
  if (value <= page_info->page_length)
    page_info->vmi = value ;
}


/* Set the left margin, which is provided as a column number */
Bool set_left_margin(PCL5Context *pcl5_ctxt, uint32 value)
{
  PageCtrlInfo *page_info ;
  PCL5Real x, y ;
  uint32 margin_pos ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  /* Ensure HMI is valid */
  if (page_info->hmi == INVALID_HMI) {
    if (! set_hmi_from_font(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), -1))
      return FALSE ;
  }

  /* Scale to PCL internal units */
  margin_pos = value * page_info->hmi ;

  /* The same column as right margin gives a 1-column wide printable area.
   * But expressed in PCL internal units, (x-coord on logical page), need
   * left margin less than right.  Otherwise ignore the request.
   */
  if (margin_pos < page_info->right_margin) {
    PCL5Ctms *ctms = get_pcl5_ctms(pcl5_ctxt);
    OMATRIX orig_orientation_ctm = *ctm_orientation(ctms);

    /* Set the margin */
    page_info->left_margin = margin_pos ;

    /* If the cursor has been explicitly set, and is to the left of the new
     * margin move it to the margin.  If it hasn't yet been explicitly set
     * move it to the default horizontal (i.e. left margin) position regardless
     * of its current position.
     */
    if (! cursor_explicitly_set(pcl5_ctxt)) {
      set_default_cursor_x_position(pcl5_ctxt) ;
    }
    else {
      get_cursor_position(pcl5_ctxt, &x, &y) ;

      if (x < page_info->left_margin)
        move_cursor_to_left_margin(pcl5_ctxt) ;
    }

    ctm_recalculate(pcl5_ctxt);
    /* setting the text length will alter the PCL ctm, so the pciture
     * frame anchor needs updating.  */
    handle_ctm_change_picture_frame(pcl5_ctxt,
                                    &orig_orientation_ctm,
                                    ctm_orientation(ctms));
  }

  return TRUE ;
}


/* Set the right margin, which is provided as a column number */
Bool set_right_margin(PCL5Context *pcl5_ctxt, uint32 value)
{
  PageCtrlInfo *page_info ;
  PCL5Real x, y ;
  uint32 margin_pos ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  /* Ensure HMI is valid */
  if (page_info->hmi == INVALID_HMI) {
    if (! set_hmi_from_font(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), -1))
      return FALSE ;
  }

  /* Add 1 to set right margin to the right of the specified column,
   * and scale to PCL internal units */
  margin_pos = (value + 1) * page_info->hmi ;

  /* If greater than right edge of logical page, set to right edge */
  if (margin_pos > page_info->page_width)
    margin_pos = page_info->page_width ;

  /* The same column as right margin gives a 1-column wide printable area.
   * But expressed in PCL internal units, (x-coord on logical page), need
   * left margin less than right.  Otherwise ignore the request.
   *
   * N.B. If the defaults were validated correctly this can't happen
   * if we also needed to set to right edge above, but don't rely on
   * this for now.
   */

  if (margin_pos > page_info->left_margin) {
    PCL5Ctms *ctms = get_pcl5_ctms(pcl5_ctxt);
    OMATRIX orig_orientation_ctm = *ctm_orientation(ctms);

    /* Set the margin */
    page_info->right_margin = margin_pos ;

    /* If the cursor is to the right of the new margin move it to the margin */
    get_cursor_position(pcl5_ctxt, &x, &y) ;

    if (x > page_info->right_margin) {
      /* N.B. Probably don't want to move the cursor here if it has not yet
       * been explicitly set, but then again it's hard to envisage how this
       * case could arise, so just assert here.
       */
      HQASSERT(cursor_explicitly_set(pcl5_ctxt), "cursor not explicitly set") ;
      move_cursor_to_right_margin(pcl5_ctxt) ;
    }

    ctm_recalculate(pcl5_ctxt);
    /* setting the text length will alter the PCL ctm, so the pciture
     * frame anchor needs updating.  */
    handle_ctm_change_picture_frame(pcl5_ctxt,
                                    &orig_orientation_ctm,
                                    ctm_orientation(ctms));
  }

  return TRUE ;
}

void set_max_text_length(PCL5Context *pcl5_ctxt)
{
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  HQASSERT(page_info->top_margin >= 0 &&
           page_info->top_margin <= page_info->page_length,
           "Top margin is in unexpected position" ) ;

  page_info->max_text_length = page_info->page_length
                                - page_info->top_margin ;
}

/* Set the top margin, which is provided as number of lines from the
 * top of the logical page.  Also set the maximum text length which
 * we keep for convenience.
 */
void set_top_margin(PCL5Context *pcl5_ctxt, uint32 value)
{
  PageCtrlInfo *page_info ;
  uint32 margin_pos ;
  PCL5Ctms *ctms = get_pcl5_ctms(pcl5_ctxt);
  PrintModelInfo *print_model ;
  OMATRIX orig_ctm, orig_orientation_ctm ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  print_model = get_print_model(pcl5_ctxt) ;
  HQASSERT(print_model != NULL, "print_model is NULL") ;

  /* Ignore the command if the current VMI is zero */
  if (page_info->vmi == 0)
    return ;

  /* Scale to PCL internal units */
  margin_pos = value * page_info->vmi ;

  /* Ignore the command if it would be below the page */
  if (margin_pos <= page_info->page_length ) {

    /* Set the margin and maximum text length */
    page_info->top_margin = margin_pos ;
    set_max_text_length(pcl5_ctxt) ;

    /* Copy the original matrix since it's about to be changed. */
    orig_ctm = *ctm_current(ctms) ;
    orig_orientation_ctm = *ctm_orientation(ctms);
    ctm_recalculate(pcl5_ctxt) ;

    /* Update cursor coordinates to keep in same physical position
     * N.B. Since transform_cursors ensures the PCL5FontState underline cursors
     *      are transformed, this should be called before setting the default
     *      cursor Y position, so that the underline callback is not called
     *      with the relevant state only partly set up.
     *
     * N.B. HPGL defines a coordinate system wrt to the picture frame anchor
     *      point, whose physical location should not change when PCL matrix
     *      changes.
     */
    transform_cursors(pcl5_ctxt, &orig_ctm, ctm_current(ctms)) ;

    /** \todo It has not been confirmed whether this is correct in the
     *  case of an explicitly set reference point.
     */
    transform_cursor(&print_model->pattern_origin,
                     &orig_ctm,
                     ctm_current(ctms)) ;

    handle_ctm_change_picture_frame(pcl5_ctxt,
                                    &orig_orientation_ctm,
                                    ctm_orientation(ctms));

    /** \todo See if easier to just not transform current cursor */
    if (! cursor_explicitly_set(pcl5_ctxt))
      set_default_cursor_y_position(pcl5_ctxt) ;
  }
}

/* Calculate the maximum text length that applies if the current page
 * were in print direction 0.
 * This code is derived directly from setprintdirection. Cannot use
 * setprintdirection function directly, as it may have am effect on
 * the pcl state - e.g. moving cursor to page. */
/** \todo @@@ TODO this function calculates all the effects of setting
 * print direction back to 0 degress. Some of this is redundant in the
 * present state.
 */
uint32 get_default_text_length(PCL5Context *pcl5_ctxt)
{
  PageCtrlInfo temp_page_info, *page_info = NULL;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  temp_page_info = *page_info;
  rotate_margins_for_printdirection(&temp_page_info, 0);

  return temp_page_info.text_length;
}

/* Reset the text length, to effectively place the bottom margin half
 * an inch from the bottom of the logical page.
 */
/** \todo Or should this be set up by considering defaults from the PJL current
 * environment together with current top margin?  (C.f. text length command and
 * top margin command as well as what we do when perforation skip mode is
 * changed).
 */
void reset_text_length(PCL5Context *pcl5_ctxt)
{
  PageCtrlInfo *page_info ;
  OMATRIX orig_orientation_ctm;
  PCL5Ctms *ctms = NULL;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  HQASSERT( page_info->max_text_length >= 0,
             "Top margin is below logical page") ;

  ctms = get_pcl5_ctms(pcl5_ctxt);

  if (page_info->max_text_length < HALF_INCH)
    page_info->text_length = page_info->max_text_length ;
  else
    page_info->text_length = page_info->max_text_length - HALF_INCH ;

  /* setting the text length will alter the PCL ctm, so the pciture
   * frame anchor needs updating.  */
  orig_orientation_ctm = *ctm_orientation(ctms);
  ctm_recalculate(pcl5_ctxt);
  handle_ctm_change_picture_frame(pcl5_ctxt,
                                  &orig_orientation_ctm,
                                  ctm_orientation(ctms));
}


/* Set the text length, which is provided as number of lines from the top
 * margin.
 */
Bool set_text_length(PCL5Context *pcl5_ctxt, uint32 value)
{
  PageCtrlInfo *page_info ;
  uint32 text_length ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  /* Ignore the command if the current VMI is zero */
  if (page_info->vmi == 0)
    return TRUE ;

  /* Scale to PCL internal units */
  text_length = value * page_info->vmi ;

  /* Ignore the command if position greater than the maximum text
   * length, (bottom margin would be off page).
   */
  HQASSERT( page_info->max_text_length >= 0,
             "Top margin is below logical page") ;

  if (text_length <= page_info->max_text_length ) {
    PCL5Ctms *ctms = get_pcl5_ctms(pcl5_ctxt);
    OMATRIX orig_orientation_ctm = *ctm_orientation(ctms);

    /* N.B. Text length zero appears to be a special case.
     * The text length appears to be reset.
     */
    if (text_length == 0)
      reset_text_length(pcl5_ctxt) ;
    else
      page_info->text_length = text_length ;

    ctm_recalculate(pcl5_ctxt);
    /* setting the text length will alter the PCL ctm, so the pciture
     * frame anchor needs updating.  */
    handle_ctm_change_picture_frame(pcl5_ctxt,
                                    &orig_orientation_ctm,
                                    ctm_orientation(ctms));
  }

  return TRUE ;
}


/* Set up the PCL5 margins afresh or adjust them for new page size */
void set_pcl5_margins(PCL5Context *pcl5_ctxt, Bool job_start)
{
  PageCtrlInfo *page_info;
  const PageCtrlInfo *default_page_info;
  uint32 default_right_margin_width ;
  int32 left_margin_position, right_margin_position ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  default_page_info = get_default_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(default_page_info != NULL, "default_page_info is NULL") ;

  /* Default the margin positions */
  if (job_start) {
    /** \todo Assert at pjl_current_env level */
    if (page_info->page_length >= HALF_INCH)
      page_info->top_margin = HALF_INCH ;
    else
      page_info->top_margin = 0 ;

    set_max_text_length(pcl5_ctxt) ;
    reset_text_length(pcl5_ctxt);

    page_info->left_margin = 0 ;
    page_info->right_margin = page_info->page_width ;
  }
  else {
    /* N.B. This is slightly from how these are set above, since this allows
     * existing margin distances from logical page edges to be maintained,
     * but expressed in terms of new logical page dimensions.  It is safe
     * to use this either with a current MPE one or more levels above the
     * base MPE, or indeed on the base (default) MPE itself.
     * This also allows for PJL (or user) default margin values to be set up
     * in future.
     */
    /** \todo What if the VMI is zero - normally we wouldn't change e.g. the top
     *  margin.  N.B. Also assuming that some kind of validation of the defaults
     *  will have taken place before now.  Need to be careful about relationships
     *  between these.
     */
    if (page_info->page_length >= HALF_INCH) {
      page_info->top_margin = default_page_info->top_margin ;

      if (page_info->top_margin > page_info->page_length)
        page_info->top_margin = page_info->page_length ;
    }
    else
      page_info->top_margin = 0 ;

    set_max_text_length(pcl5_ctxt) ;
    reset_text_length(pcl5_ctxt);

    left_margin_position = (int32) default_page_info->left_margin ;

    if (left_margin_position >= (int32) page_info->page_width)
      left_margin_position = (int32) (page_info->page_width - 1) ;

    page_info->left_margin = (left_margin_position > 0) ? (uint32) left_margin_position : 0 ;

    default_right_margin_width = default_page_info->page_width
                                  - default_page_info->right_margin ;

    right_margin_position = (int32) (page_info->page_width - default_right_margin_width) ;

    if (right_margin_position < (int32) (page_info->left_margin + 1))
      right_margin_position = (int32) (page_info->left_margin + 1) ;

    page_info->right_margin = (uint32) right_margin_position ;
  }
}


/** \todo This is the replacement for set_pcl_page_dimensions using OEM settable values.
 *  It needs more work.
 */
void set_pcl_page_dimensions(PCL5Context *pcl5_ctxt,
                             uint32 left_offset,
                             uint32 right_offset,
                             uint32 top_offset,
                             uint32 bottom_offset)
{
  PageCtrlInfo *page_info ;
  RasterGraphicsInfo *rast_info ;

  UNUSED_PARAM(uint32, top_offset) ;
  UNUSED_PARAM(uint32, bottom_offset) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;
  rast_info = get_rast_info(pcl5_ctxt) ;
  HQASSERT(rast_info != NULL, "rast_info is NULL") ;

  if ( page_info->orientation == 0 || page_info->orientation == 2) {
    /** \todo Handle different left and right offsets - for now take average */
    page_info->portrait_offset = (left_offset + right_offset) / 2 ;

    if (2 * page_info->portrait_offset > page_info->physical_page_width)
      page_info->portrait_offset = page_info->physical_page_width / 2 ;

    page_info->page_width = page_info->physical_page_width -
                             (2 * page_info->portrait_offset) ;

    /** \todo Handle top and bottom offsets */
    page_info->page_length = page_info->physical_page_length ;
  }
  else {
    /* N.B. Left and right mean as seen in the current orientation */
    /** \todo Handle different left and right offsets - for now take average */
    page_info->landscape_offset = (left_offset + right_offset) /2 ;

    if (2 * page_info->landscape_offset > page_info->physical_page_length)
      page_info->landscape_offset = page_info->physical_page_length / 2 ;

    page_info->page_width = page_info->physical_page_length -
                             (2 * page_info->landscape_offset) ;

    /** \todo Handle top and bottom offsets */
    page_info->page_length = page_info->physical_page_width ;
  }
}

#if defined(DEBUG_BUILD)
/* Draw lines just inside the margins, i.e just inside the text area.
 * As this is called just before the showpage, the margins will be shown as
 * they have been most recently positioned.
 */
Bool draw_border(PCL5Context *pcl5_ctxt)
{
#define HALF_STROKE_WIDTH 12    /* PCL internal units */

  uint8 buffer[544] ;
  PageCtrlInfo *page_info ;
  PCL5Real x0, x1, x2, x3, x4, y1, y2, y3, y4 ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  x0 = (PCL5Real) HALF_STROKE_WIDTH ;

  x1 = 0 ;
  x2 = (PCL5Real) page_info->page_width ;
  x3 = (PCL5Real) page_info->left_margin + HALF_STROKE_WIDTH ;
  x4 = (PCL5Real) page_info->right_margin - HALF_STROKE_WIDTH ;

  y1 = (PCL5Real) HALF_STROKE_WIDTH ;
  y2 = (PCL5Real) (page_info->text_length - HALF_STROKE_WIDTH) ;
  y3 =  - (PCL5Real) page_info->top_margin ;
  y4 = (PCL5Real) page_info->max_text_length ;

  swncopyf(buffer, sizeof(buffer), (uint8*)
           "0.5 setcolor "
           "%d setlinewidth "
           "newpath "
           "%lf %lf moveto "
           "%lf %lf lineto "
           "%lf %lf lineto "
           "%lf %lf lineto "
           "closepath "
           "stroke "
           "newpath "
           "%lf %lf moveto "
           "%lf %lf lineto "
           "closepath "
           "stroke "
           "newpath "
           "%lf %lf moveto "
           "%lf %lf lineto "
           "closepath "
           "stroke "
           "newpath "
           "%lf %lf moveto "
           "%lf %lf lineto "
           "closepath "
           "stroke "
           "newpath "
           "%lf %lf moveto "
           "%lf %lf lineto "
           "closepath "
           "stroke "
           "0 setcolor ",

           /* Stroke Width */
           (2 * HALF_STROKE_WIDTH),

           /* Top Corner */
           x0, (y3 + y1),

           /* Top Edge */
           (x2 - x0), (y3 + y1),

           /* Right Edge */
           (x2 - x0), (y4 - y1),

           /* Bottom Edge (followed by Left Edge) */
           x0, (y4 - y1),

           /* Top Margin */
           x1, y1, x2, y1,

           /* Bottom Margin */
           x1, y2, x2, y2,

           /* Left Margin */
           x3, y3, x3, y4,

           /* Right Margin */
           x4, y3, x4, y4) ;

  return run_ps_string(buffer) ;

#undef HALF_STROKE_WIDTH
}
#endif /* DEBUG_BUILD */


/* Handle a change to PCL5 page size, orientation etc, calling PS setpagedevice
 * with the buffer provided, using the results to modify the PCL5 state, and
 * doing any other necessary PCL5 state changes.  If no buffer is provided,
 * the setpagedevice call is made filling in all relevant items from the MPE.
 * N.B. It is assumed that the previous page has already been thrown.
 */
/** \todo See if it is possible to get rid of the printdirection element of
 *  this, which does not really fit very well.
 */
Bool handle_pcl5_page_change(PCL5Context *pcl5_ctxt,
                             uint8 *buffer,
                             int32 reason,
                             Bool reset_printdirection)
{
  struct PCL5PrintState *print_state;
  FontInfo *font_info ;
  PCL5Numeric zero = {0} ;
  ps_context_t *pscontext ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;
  HQASSERT(displaylistisempty(pcl5_ctxt->corecontext->page), "Expected empty display list") ;


  HQASSERT(pcl5_ctxt->corecontext != NULL, "No core context") ;
  pscontext = pcl5_ctxt->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;

  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  font_info = pcl5_get_font_info(pcl5_ctxt) ;
  HQASSERT(font_info != NULL, "font_info is NULL") ;

  /* Every page has a PS save/restore to recover PSVM.
   * So restore here to keep all setpagedevice calls at the same level.
   */

  (void) gs_gexch(GST_HPGL, GST_GSAVE) ;  /* get HPGL one back if there */
  if (! push(&(print_state->save), &operandstack) || ! restore_(pscontext))
    return FALSE ;

  if (buffer != NULL) {
    if (! run_ps_string(buffer))
      return FALSE ;
  }
  else {
    /* This calls setpagedevice with all relevant MPE items */
    if (! set_device_from_MPE(pcl5_ctxt))
      return FALSE ;
  }

  /* Default the print direction if requested */
  /** \todo Or can we just set page_info->print_direction = 0?
   *  The only difference this would make would be to the cursor position
   *  stack.  But then does the comment re moving positions to the logical
   *  page on popping them make sense?  (Or only due to portrait and
   *  landscape offsets?)
   *  Since we're only printing what's in the buffer this could even be
   *  done immediately from the command handler.
   */
  if (reset_printdirection) {
    if (!pcl5op_ampersand_a_P(pcl5_ctxt, FALSE, zero))
      return FALSE ;
  }

  /* Set up the new MPE state, e.g. new page dimensions from the pagedevice. */
  if (! set_MPE_from_device(pcl5_ctxt, reason))
    return FALSE ;

  /* The restore will have invalidated various PS settings, so note the
   * fact here.
   */
  handle_ps_font_change(pcl5_ctxt) ;
  font_sel_caches_free() ;
  print_state->setg_required += 1 ;
  reset_last_pcl5_and_hpgl_colors();

  /* Now must save again before doing rest of the page */
  if (! save_(pscontext))
    return FALSE ;

  Copy(&(print_state->save), theTop(operandstack)) ;

  if (pcl5_ctxt->pass_through_mode == PCLXL_WHOLE_JOB_PASS_THROUGH) {
    HQASSERT(pcl5_ctxt->state_info != NULL, "state_info is NULL") ;
    Copy(pcl5_ctxt->state_info->pclxl_save_object, theTop(operandstack)) ;
    pcl5_ctxt->state_info->use_pcl5_page_clip =
      pcl5_ctxt->state_info->use_pcl5_page_setup =
        TRUE;
  }

  pop(&operandstack) ;

  return TRUE ;
}


/* Page Setup */
Bool reset_page(PCL5Context *pcl5_ctxt)
{
  PrintModelInfo *print_info ;
  FontInfo *font_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  print_info = get_print_model(pcl5_ctxt) ;
  HQASSERT(print_info != NULL, "print_info is NULL") ;
  font_info = pcl5_get_font_info(pcl5_ctxt) ;
  HQASSERT(font_info != NULL, "font_info is NULL") ;

  /* Sanitise CTM after setpagedevice to ensure sufficient accuracy in CTMs. */
  if ( !ctm_sanitise_default_matrix(pcl5_ctxt) )
    return FALSE ;

  /* Set up the orientation and printstate CTMs from base CTM.
   * N.B. The base CTM must have been correctly setup before
   *      this point.
   */
  ctm_recalculate(pcl5_ctxt) ;
  set_default_cursor_position(pcl5_ctxt) ;

  /* Re-sync pixel placement which appears to be preserved across pages. */
  set_pixel_placement(print_info, print_info->pixel_placement);

  /** \todo This should probably be more closely associated with the restore
   * that necessitates it, but will have the same effect for now.
   */
  handle_ps_font_change(pcl5_ctxt) ;
  font_sel_caches_free() ;
  reset_last_pcl5_and_hpgl_colors();
  return TRUE ;
}


/* See header for doc. */
Bool throw_page(PCL5Context *pcl5_ctxt, Bool reset, Bool unconditional)
{
  PCL5PrintState *print_state ;
  PCL5PrintEnvironment *mpe ;
  JobCtrlInfo *job_info ;
  PageCtrlInfo *page_info ;
  FontInfo *font_info ;
  Bool success = TRUE ;
  Bool throw_page ;
  int32 old_macro_mode ;
  Bool within_text_run ;
  ps_context_t *pscontext ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  HQASSERT(pcl5_ctxt->corecontext != NULL, "No core context") ;
  pscontext = pcl5_ctxt->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;

  mpe = get_current_mpe(pcl5_ctxt) ;
  HQASSERT(mpe != NULL, "mpe is NULL") ;

  job_info = get_job_ctrl_info(pcl5_ctxt) ;
  HQASSERT(job_info != NULL, "job_info is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  font_info = pcl5_get_font_info(pcl5_ctxt) ;
  HQASSERT(font_info != NULL, "font_info is NULL") ;

  within_text_run = print_state->font_state.within_text_run ;

  /* As well as making the text run nesting simpler, this will ensure
   * that any pending text characters are flushed to the DL before
   * the displaylistisempty test below.
   */
  if (within_text_run)
    success = pcl5_end_text_run(pcl5_ctxt, TRUE) ;
  else
    success = pcl5_flush_text_run(pcl5_ctxt, 1) ;

  throw_page = ( unconditional || ! displaylistisempty(pcl5_ctxt->corecontext->page) ) &&
               ( pcl5_ctxt->pass_through_mode != PCLXL_SNIPPET_JOB_PASS_THROUGH ) ;

#if defined(DEBUG_BUILD)
  if ( debug_pcl5 & PCL5_MARGINS ) {
    if (throw_page)
      success = success && draw_border(pcl5_ctxt) ;
  }
#endif /* DEBUG_BUILD */

  if (throw_page) {
    /* Handle any underlining this throw page might invoke. */
    if (! underline_callback(pcl5_ctxt, TRUE))
      return FALSE ;

    if (print_state->allow_macro_overlay) {
      int32 underline_mode ;
      CursorPosition cursor_pos ;
      MacroInfo *macro_info = get_macro_info(pcl5_ctxt) ;
      HQASSERT(macro_info != NULL, "macro_info is NULL") ;

      underline_mode = print_state->font_state.underline_mode ;
      cursor_pos = *get_cursor(pcl5_ctxt) ;

      /* Disable during overlay macro to avoid infinite loop if macro
         throws page */
      print_state->allow_macro_overlay = FALSE ;
      success = success && pcl5_mpe_save(pcl5_ctxt, CREATE_OVERLAY_ENV) ;

      if (success) {
        old_macro_mode = pcl5_current_macro_mode ;
        pcl5_current_macro_mode = PCL5_EXECUTING_OVERLAY_MACRO ;

        success = execute_macro_definition(pcl5_ctxt, macro_info, TRUE) ;

        pcl5_current_macro_mode = old_macro_mode ;
        print_state->font_state.underline_mode = underline_mode ;
        success = pcl5_mpe_restore(pcl5_ctxt) && success ;
        set_cursor_position(pcl5_ctxt, cursor_pos.x, cursor_pos.y) ;
      }
      print_state->allow_macro_overlay = TRUE ;
    }
  }

  if (pcl5_ctxt->pass_through_mode != PCLXL_SNIPPET_JOB_PASS_THROUGH ) {

    /* Every page has a PS save/restore to recover PS memory */
    (void) gs_gexch(GST_HPGL, GST_GSAVE) ;  /* get HPGL one back if there */
    if (! push(&(print_state->save), &operandstack) || ! restore_(pscontext))
      return FALSE ;

    if (throw_page && success) {
      success = set_ps_num_copies(pcl5_ctxt) && renderPage(pcl5_ctxt) ;

#if defined(DEBUG_BUILD)
      if ( debug_pcl5 & PCL5_CONTROL ) {
        monitorf((uint8*)"---- throw page (markings exist on the page)\n") ;
      }
#endif /* DEBUG_BUILD */

      if (success) {
        /* More suitable pagedevice settings may now be available, e.g. more
         * suitable paper, so repeat the setpagedevice request.
         */
        /** \todo A possibly optimisation may be to query the pagedevice
         * first, and only do the setpagedevice call if the required values
         * differ from those already in the pagedevice.
         */
        if (! set_device_from_MPE(pcl5_ctxt))
          return FALSE ;

        /* Find out what values were actually set up. */
        if (! set_MPE_from_device(pcl5_ctxt, PCL5_STATE_MAINTENANCE))
          return FALSE ;
      }
    }

    /* The restore will have invalidated various PS settings.
     * Although some of these may be set up again by reset_page
     * it is probably safest to note the fact here.
     */
    /** \todo Any other PS settings invalidated by restore in need of reflection
        to PCL? */
    handle_ps_font_change(pcl5_ctxt) ;
    font_sel_caches_free() ;
    print_state->setg_required += 1 ;
    reset_last_pcl5_and_hpgl_colors();

    if (! save_(pscontext))
      return FALSE ;

    Copy(&(print_state->save), theTop(operandstack)) ;

    if (pcl5_ctxt->pass_through_mode == PCLXL_WHOLE_JOB_PASS_THROUGH) {
      HQASSERT(pcl5_ctxt->state_info != NULL, "state_info is NULL") ;
      Copy(pcl5_ctxt->state_info->pclxl_save_object, theTop(operandstack)) ;
    }

    pop(&operandstack) ;
  }

  if (success) {
    if (throw_page) {
      print_state->page_number++ ;

      if (! job_info->duplex)
        print_state->duplex_page_number += 2 ;
      else
        print_state->duplex_page_number++ ;
    }
    if (reset) {
      PCL5Real x, y ;
      get_cursor_position(pcl5_ctxt, &x, &y) ;

      success = reset_page(pcl5_ctxt) ;

      /* Unconditional page throws preserve the cursor x position. */
      /** \todo Is this also the case for an XL snippet passthrough? */
      if (unconditional)
        set_cursor_x_absolute(pcl5_ctxt, x) ;
    }
  }

#ifndef RQ64403
  if (throw_page && success)
    success = success && reset_underline_details(pcl5_ctxt) ;

  if (success && within_text_run) {
      FontSelInfo *dummy_font_sel_info ;
      FontInfo *dummy_font_info ;
      Font *dummy_font ;

      if (success && ! pcl5_start_text_run(pcl5_ctxt, &dummy_font_sel_info,
                                               &dummy_font_info, &dummy_font))
        success = FALSE ;
  }
#else
  if (success && throw_page)
    success = reset_underline_details(pcl5_ctxt) ;

  if (success) {
    success = pcl5_set_text_run_restart_required(pcl5_ctxt, FALSE) ;
    if (success && within_text_run) {
      FontSelInfo *dummy_font_sel_info ;
      FontInfo *dummy_font_info ;
      Font *dummy_font ;

      success = pcl5_start_text_run(pcl5_ctxt, &dummy_font_sel_info,
                                    &dummy_font_info, &dummy_font) ;
    }
  }
#endif
  return success ;
}


Bool set_page_size(PCL5Context *pcl5_ctxt, int32 page_size)
{
  uint8 buffer[96];
  uint8 *spd = buffer;
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;

  /* Print anything in printbuffer */
  if (! throw_page(pcl5_ctxt, FALSE, FALSE))
    return FALSE ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  /* This is for the setpagedevice call to request the new page size */
  /* N.B. For now we are assuming PCL5 and XL share the same orientation.
   * If this turns out not to be the case e.g. if XL needs to retain
   * its original orientation after a passthrough to PCL5, then this
   * should be the PCL5Orientation.
   */
  if (page_size != page_info->page_size) {
    swncopyf(buffer, sizeof(buffer),
             (uint8*)"<</PCL5PageSize %d/PCLOrientation %d>>setpagedevice",
             page_size, page_info->orientation) ;
  } else {
    spd = NULL;
  }

  /* Make the PS setpagedevice call and fill in the resulting MPE state
   * including the new page dimensions.
   */
  if (! handle_pcl5_page_change(pcl5_ctxt, spd, PCL5_PAGE_SIZE_CHANGE, TRUE))
    return FALSE ;

  /* The last requested PCL5 page size in the page device should now match
   * the value we just requested.
   * Note that this may well differ from the actual page size set by
   * the pagedevice call above.
   */
  HQASSERT(page_info->page_size == page_size,
           "Unexpected value for last requested page size") ;

  /* Default the pattern reference point */
  set_default_pattern_reference_point(pcl5_ctxt) ;

  /* Reset certain print state items */
  /** \todo Are HMI and VMI unaffected, or are they defaulted as for
   *  orientation command?  If they are defaulted, should this also
   *  happen if the media size has changed following a currentpagedevice
   *  call?
   */
  HQASSERT(pcl5_ctxt->print_state != NULL, "print_state is NULL") ;
  pcl5_ctxt->print_state->allow_macro_overlay = FALSE ;

  return (reset_page(pcl5_ctxt));
}


Bool set_paper_source(PCL5Context *pcl5_ctxt, uint32 paper_source)
{
  uint8 buffer[64];
  uint8 *spd = buffer;
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;

  /* Print anything in printbuffer */
  if (! throw_page(pcl5_ctxt, FALSE, FALSE))
    return FALSE ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  /* This is for the setpagedevice call to request the new paper source */
  if (paper_source != page_info->paper_source) {
    swncopyf(buffer, sizeof(buffer),
             (uint8*)"<</PCL5PaperSource %d>>setpagedevice",
             paper_source ) ;
  } else {
    spd = NULL;
  }

  /* Make the PS setpagedevice call and fill in the resulting MPE state
   * including any new page dimensions.
   */
  if (! handle_pcl5_page_change(pcl5_ctxt, spd, PCL5_STATE_MAINTENANCE, FALSE))
    return FALSE ;

  /* Remember this as the last requested paper source.
   * Note that this may well differ from the actual source set by
   * the pagedevice call above.
   */
  page_info->paper_source = paper_source ;

  return (reset_page(pcl5_ctxt));
}


Bool set_media_type(PCL5Context *pcl5_ctxt, uint32 media_type)
{
  uint8 buffer[64];
  uint8 *spd = buffer;
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;

  /* Print anything in printbuffer */
  if (! throw_page(pcl5_ctxt, FALSE, FALSE))
    return FALSE ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  /* This is for the setpagedevice call to request the new media type */
  if (media_type != page_info->media_type) {
    /* N.B. This could be due to an alphanumeric command with no data */
    swncopyf(buffer, sizeof(buffer),
             (uint8*)"<</PCL5MediaType %d>>setpagedevice",
             media_type) ;
  } else {
    spd = NULL;
  }

  /* Make the PS setpagedevice call and set up any new PCL page dimensions and
   * other MPE state from the pagedevice.
   *
   * N.B. This also sets the integer media type value corresponding to the
   * integer value into the PageCtrlInfo.  (This should strictly be the value we
   * requested, rather than the value we got from the setpagedevice call above,
   * though these are currently always the same).
   */
  return (handle_pcl5_page_change(pcl5_ctxt, spd, PCL5_STATE_MAINTENANCE,
                                  FALSE) &&
          reset_page(pcl5_ctxt));
}


Bool set_media_type_from_alpha(PCL5Context *pcl5_ctxt, uint8 *media_type_buf, int32 length)
{
  /* Alpha media type can be up to 253 bytes in length - see
   * pcl5op_ampersand_n_W() for more details on length restrictions. */
  uint8 buffer[64 + 256];

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;
  HQASSERT(media_type_buf != NULL, "Null media type buffer") ;
  HQASSERT(length >= 0 && length <= 253, "media type length invalid") ;

  /* Print anything in printbuffer */
  if (! throw_page(pcl5_ctxt, FALSE, FALSE))
    return FALSE ;

  /* This is for the setpagedevice call to request the new media type */
  swncopyf(buffer, sizeof(buffer),
           (uint8*)"<</PCL5MediaType(%.*s)>>setpagedevice",
           length, media_type_buf) ;

  /* Make the PS setpagedevice call and set up any new PCL page dimensions and
   * other MPE state from the pagedevice.
   *
   * N.B. This also sets the integer media type value corresponding to the
   * string value into the PageCtrlInfo.  (This should strictly be the value we
   * requested, rather than the value we got from the setpagedevice call above,
   * though these are currently always the same).
   */
  return (handle_pcl5_page_change(pcl5_ctxt, buffer, PCL5_STATE_MAINTENANCE,
                                  FALSE) &&
          reset_page(pcl5_ctxt));
}


/** \todo This command needs more investigation.
 *  Modelled on set_page_size for now.
 */
Bool set_page_length(PCL5Context *pcl5_ctxt, uint32 length)
{
  uint8 buffer[96];
  uint8 *spd = buffer;
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;

  /* Print anything in printbuffer */
  if (! throw_page(pcl5_ctxt, FALSE, FALSE))
    return FALSE ;

  /* This is for the setpagedevice call to request the new page size */
  /* N.B. For now we are assuming PCL5 and XL share the same orientation.
   * If this turns out not to be the case e.g. if XL needs to retain
   * its original orientation after a passthrough to PCL5, then this
   * should be the PCL5Orientation.
   */
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  swncopyf(buffer, sizeof(buffer),
           (uint8*)"<</PCL5PageLength %d/PCLOrientation %d"
                   "/PCL5PageSize -1>>setpagedevice",
           length, page_info->orientation) ;

  /* Make the PS setpagedevice call and fill in the resulting MPE state
   * including the new page dimensions.
   */
  if (! handle_pcl5_page_change(pcl5_ctxt, spd, PCL5_PAGE_SIZE_CHANGE, TRUE))
    return FALSE ;

  /* Default the pattern reference point */
  set_default_pattern_reference_point(pcl5_ctxt) ;

  /* Reset certain print state items */
  /** \todo Are HMI and VMI unaffected, or are they defaulted as for
   *  orientation command?  If they are defaulted, should this also
   *  happen if the media size has changed following a currentpagedevice
   *  call?
   */
  HQASSERT(pcl5_ctxt->print_state != NULL, "print_state is NULL") ;
  pcl5_ctxt->print_state->allow_macro_overlay = FALSE ;

  return (reset_page(pcl5_ctxt));
}


/* factor out the margin manipulation for printdirection, as it is useful
 * when calculating characteristics of page wrt default orientation as well
 * as when actually changing the printdirection. For example, HPGL only
 * operates wrt default printdirection.
 */
void rotate_margins_for_printdirection(PageCtrlInfo *page_info,
                                       uint32 printdirection)
{
  int32 angle ;
  uint32 right_margin_width, bottom_margin_width, text_width ;
  uint32 temp ;

  right_margin_width = page_info->page_width - page_info->right_margin ;
  bottom_margin_width = page_info->max_text_length - page_info->text_length ;
  text_width = page_info->right_margin - page_info->left_margin ;

  if ( page_info->print_direction == printdirection )
    return;

  angle = printdirection - page_info->print_direction;

  if (angle < 0)
    angle += 360 ;

  right_margin_width = page_info->page_width - page_info->right_margin ;
  bottom_margin_width = page_info->max_text_length - page_info->text_length ;
  text_width = page_info->right_margin - page_info->left_margin ;

  switch (angle) {
  case 90:
    temp = page_info->page_length ;
    page_info->page_length = page_info->page_width ;
    page_info->page_width = temp ;

    page_info->top_margin = page_info->left_margin ;
    page_info->text_length = text_width ;
    page_info->left_margin = bottom_margin_width ;
    page_info->right_margin = page_info->max_text_length ;
    break ;

  case 180:
    /* N.B. page_length, page_width and text_length unchanged */
    temp = page_info->left_margin ;
    page_info->top_margin = bottom_margin_width ;
    page_info->left_margin = right_margin_width ;
    page_info->right_margin = page_info->page_width - temp ;
    break ;

  case 270:
   temp = page_info->page_length ;
   page_info->page_length = page_info->page_width ;
   page_info->page_width = temp;

   page_info->left_margin = page_info->top_margin ;
   page_info->right_margin = temp - bottom_margin_width ;
   page_info->top_margin = right_margin_width ;
   page_info->text_length = text_width ;
   break ;

  default:
    HQFAIL("Unexpected print direction") ;
  }

  page_info->max_text_length = page_info->page_length - page_info->top_margin ;

  return ;

}

/* N.B. The Tech Ref says HMI should not be defaulted here, whereas the
 * Developers Guide says it should be.  Tests agree with the Tech Ref.
 */
/** \todo The Tech Ref doesn't mention transforming stack coordinates
 *  needs testing.
 */
Bool set_printdirection(PCL5Context *pcl5_ctxt, uint32 printdirection)
{
  PCL5PrintEnvironment *mpe ;
  PCL5Ctms *ctms = get_pcl5_ctms(pcl5_ctxt);
  PageCtrlInfo *page_info ;
  RasterGraphicsInfo *rast_info ;
  PrintModelInfo *print_model ;
  int32 angle ;
  uint32 right_margin_width, bottom_margin_width, text_width ;
  uint32 temp ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;
  mpe = get_current_mpe(pcl5_ctxt) ;
  HQASSERT(mpe != NULL, "mpe is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  /* How does the printdirection compare to current? */
  angle = printdirection - page_info->print_direction;
  HQASSERT(angle != 0, "Zero change in print direction") ;

  rast_info = get_rast_info(pcl5_ctxt) ;
  HQASSERT(rast_info != NULL, "rast_info is NULL") ;

  print_model = get_print_model(pcl5_ctxt) ;
  HQASSERT(print_model != NULL, "print_model is NULL") ;

  if (angle < 0)
    angle += 360 ;

  /* Set the new print direction and page dimensions */
  page_info->print_direction = printdirection ;

  /** \todo Possibly define these as part of print state */
  right_margin_width = page_info->page_width - page_info->right_margin ;
  bottom_margin_width = page_info->max_text_length - page_info->text_length ;
  text_width = page_info->right_margin - page_info->left_margin ;

  if (! underline_callback(pcl5_ctxt, TRUE))
    return TRUE ;

  switch (angle) {
  case 90:
    temp = page_info->page_length ;
    page_info->page_length = page_info->page_width ;
    page_info->page_width = temp ;

    page_info->top_margin = page_info->left_margin ;
    page_info->text_length = text_width ;
    page_info->left_margin = bottom_margin_width ;
    page_info->right_margin = page_info->max_text_length ;
    break ;

  case 180:
    /* N.B. page_length, page_width and text_length unchanged */
    temp = page_info->left_margin ;
    page_info->top_margin = bottom_margin_width ;
    page_info->left_margin = right_margin_width ;
    page_info->right_margin = page_info->page_width - temp ;
    break ;

  case 270:
   temp = page_info->page_length ;
   page_info->page_length = page_info->page_width ;
   page_info->page_width = temp;

   page_info->left_margin = page_info->top_margin ;
   page_info->right_margin = temp - bottom_margin_width ;
   page_info->top_margin = right_margin_width ;
   page_info->text_length = text_width ;
   break ;

  default:
    HQFAIL("Unexpected print direction") ;
  }

  /* One more thing to update */
  page_info->max_text_length = page_info->page_length - page_info->top_margin ;

  /* Recalculate the CTM, and update cursor positions to maintain same physical
   * location in the new coordinate space. */
  {
    OMATRIX orig_ctm, orig_orientation_ctm ;
    /* Copy the original matrix since it's about to be changed. */
    orig_ctm = *ctm_current(ctms) ;
    orig_orientation_ctm = *ctm_orientation(ctms);
    ctm_recalculate(pcl5_ctxt);

    /* Update cursor coordinates to keep in same physical position */
    /* N.B. This may not be required for PCL5FontState cursors, though
     * probably does no harm either.
     */
    transform_cursors(pcl5_ctxt, &orig_ctm, ctm_current(ctms)) ;

    transform_cursor(&print_model->pattern_origin,
                     &orig_ctm,
                     ctm_current(ctms)) ;

     /* HPGL picture frame is relative to the orientation ctm, and the changes
     * to the margins give above will side effect the orientation ctm, so need
     * to adjust the HPGL picture frame anchor point. */
    handle_ctm_change_picture_frame(pcl5_ctxt,
                                    &orig_orientation_ctm,
                                    ctm_orientation(ctms));

  }

  return TRUE ;
}


Bool set_orientation(PCL5Context *pcl5_ctxt, uint32 orientation)
{
  uint8 buffer[96];
  uint8 *spd = buffer;
  PageCtrlInfo *page_info ;
  const PageCtrlInfo *default_page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;

  /* Print anything in printbuffer */
  if (! throw_page(pcl5_ctxt, FALSE, FALSE))
    return FALSE ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  /* This is for the setpagedevice call to request the new orientation */
  /* N.B. For now we are assuming PCL5 and XL share the same orientation.
   * If this turns out not to be the case e.g. if XL needs to retain
   * its original orientation after a passthrough to PCL5, then this
   * should be the PCL5Orientation.
   */
  if (orientation != page_info->orientation) {
    swncopyf(buffer, sizeof(buffer),
             (uint8*)"<</PCL5PageSize %d/PCLOrientation %d>>setpagedevice",
             page_info->page_size, orientation) ;
  } else {
    spd = NULL;
  }

  /* Make the PS setpagedevice call and fill in the resulting MPE state
   * including any new page dimensions.
   *
   * N.B. If necessary, this will also default the margin positions and set up a
   * new clip.
   */
  if (! handle_pcl5_page_change(pcl5_ctxt, spd, PCL5_STATE_MAINTENANCE, TRUE))
    return FALSE ;

  /* Set this last requested orientation */
  /* N.B. There is no reason why this should differ (at the moment)
   * from the orientation returned from the currentpagedevice call,
   * but this is just to be strictly accurate.
   */
  page_info->orientation = orientation ;

  /* Default the HMI and VMI
   * N.B. In the case of the HMI, this means setting it from the active font.
   * Note that we cannot simply set INVALID_HMI as it could then be set up
   * again from font select by criteria, whereas it may have been set by Id.
   */
  if (! set_hmi_from_font(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), -1))
    return FALSE ;

  default_page_info = get_default_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(default_page_info != NULL, "default_page_info is NULL") ;
  page_info->vmi = default_page_info->vmi ;

  /* Default the pattern reference point */
  set_default_pattern_reference_point(pcl5_ctxt) ;

  /* Reset certain print state items */
  HQASSERT(pcl5_ctxt->print_state != NULL, "print_state is NULL") ;
  pcl5_ctxt->print_state->allow_macro_overlay = FALSE ;

  return (reset_page(pcl5_ctxt));
}

Bool set_custom_paper_width(PCL5Context *pcl5_ctxt, uint32 paper_width)
{
  uint8 buffer[64];
  uint8 *spd = buffer;
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;

  /* Print anything in printbuffer */
  if (! throw_page(pcl5_ctxt, FALSE, FALSE))
    return FALSE ;

  /* This is for the setpagedevice call to request the new paper source */
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;
  if (paper_width != page_info->custom_page_width) {
    swncopyf(buffer, sizeof(buffer),
             (uint8*)"<</PCL5CustomPageWidth %d>>setpagedevice",
             paper_width ) ;
  } else {
    spd = NULL;
  }

  /* Make the PS setpagedevice call and fill in the resulting MPE state
   * including any new page dimensions.
   */
  if (! handle_pcl5_page_change(pcl5_ctxt, spd, PCL5_STATE_MAINTENANCE, FALSE))
    return FALSE ;

  /* Remember this as the last requested paper source.
   * Note that this may well differ from the actual source set by
   * the pagedevice call above.
   */
  page_info->custom_page_width = paper_width ;

  return (reset_page(pcl5_ctxt));
}

Bool set_custom_paper_length(PCL5Context *pcl5_ctxt, uint32 paper_length)
{
  uint8 buffer[64];
  uint8 *spd = buffer;
  PageCtrlInfo *page_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;

  /* Print anything in printbuffer */
  if (! throw_page(pcl5_ctxt, FALSE, FALSE))
    return FALSE ;

  /* This is for the setpagedevice call to request the new paper source */
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;
  if (paper_length != page_info->custom_page_length) {
    swncopyf(buffer, sizeof(buffer),
             (uint8*)"<</PCL5CustomPageHeight %d>>setpagedevice",
             paper_length ) ;
  } else {
    spd = NULL;
  }

  /* Make the PS setpagedevice call and fill in the resulting MPE state
   * including any new page dimensions.
   */
  if (! handle_pcl5_page_change(pcl5_ctxt, spd, PCL5_STATE_MAINTENANCE, FALSE))
    return FALSE ;

  /* Remember this as the last requested custom paper length.
   */
  page_info->custom_page_length = paper_length ;

  return (reset_page(pcl5_ctxt));
}

/* Set up the default page dimensions and defaults the margin positions,
 * so should be called initially, before anything setting default margin
 * values.  Once not at job_start, margin values assumed to be set.
 */
void set_default_pcl_page_dimensions(PCL5Context *pcl5_ctxt, Bool job_start, uint32 left, uint32 right, uint32 top, uint32 bottom)
{
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;

  /* Set up the new PCL page dimensions */
  set_pcl_page_dimensions(pcl5_ctxt, left, right, top, bottom) ;

  /* Set up the margin values for the new page dimensions */
  set_pcl5_margins(pcl5_ctxt, job_start) ;
}

/* See header for doc. */
void set_pcl_unit_size(PageCtrlInfo* self, uint32 number_of_internal_units)
{
  self->pcl_unit_size = number_of_internal_units;
}

/* See header for doc. */
PCL5Real pcl_unit_size(PageCtrlInfo* self)
{
  return self->pcl_unit_size;
}

/* See header for doc. */
PCL5Real pcl_unit_to_internal(PageCtrlInfo* self, PCL5Real pcl_units)
{
  return pcl_units * self->pcl_unit_size;
}


/* Round number of PCL Internal Units to the nearest PCL Unit.
 * N.B. This function rounds a negative number of PCL Internal Units to
 * the negative of the number that the same positive number of units would
 * be rounded to.
 */
int32 round_pcl_internal_to_pcl_unit(PCL5Context *pcl5_ctxt, PCL5Real internal_units)
{
  PageCtrlInfo* page ;
  PCL5Real unit_size ;
  uint32 pcl_units, pcl_internal_units ;
  Bool neg ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  page = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page != NULL, "page is NULL") ;

  neg = (internal_units < 0) ;

  /* Get the PCL Unit size */
  unit_size = pcl_unit_size(page) ;
  HQASSERT(unit_size != 0, "zero pcl unit size") ;

  /* Round to nearest PCL Unit */
  pcl_units = (uint32) ((fabs(internal_units) / unit_size) + 0.5f) ;

  /* Convert to PCL internal units */
  if (!neg)
    pcl_internal_units = ((int32) pcl_unit_to_internal(page, pcl_units)) ;
  else
    pcl_internal_units = - ((int32) pcl_unit_to_internal(page, pcl_units)) ;

  return pcl_internal_units ;
}


/* ============================================================================
 * Operator callbacks are below here.
 * ============================================================================
 */

/* Page Size */
Bool pcl5op_ampersand_l_A(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  /** \todo Are negative values treated as positive? */
  uint32 size = (uint32) value.integer ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  if (pcl5_ctxt->pass_through_mode == PCLXL_SNIPPET_JOB_PASS_THROUGH)
    return TRUE ;

  if (! set_page_size(pcl5_ctxt, size))
    return FALSE ;

  return TRUE ;
}


/* Paper Source */
Bool pcl5op_ampersand_l_H(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  /** \todo Are negative values treated as positive? */
  uint32 paper_source = (uint32) value.integer ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  if (pcl5_ctxt->pass_through_mode == PCLXL_SNIPPET_JOB_PASS_THROUGH)
    return TRUE ;

  if (! set_paper_source(pcl5_ctxt, paper_source))
    return FALSE ;

  return TRUE ;
}


/* Media type (appears obsolete) */
/* N.B. From the description in the PCL5 Comparison guide this seems similar to the
 *      alphanumeric media select command.
 *      However, the reference printer appears to completely ignore this command.
 */
Bool pcl5op_ampersand_l_M(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  UNUSED_PARAM(PCL5Numeric, value) ;
  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  if (pcl5_ctxt->pass_through_mode == PCLXL_SNIPPET_JOB_PASS_THROUGH)
    return TRUE ;

#if 0
  /* N.B. The reference printer appears to completely ignore this command */
  /** \todo Are negative values of media type treated as positive? */
  if (! set_media_type(pcl5_ctxt, (uint32) value.integer))
    return FALSE ;
#endif

  return TRUE ;
}


/** \todo Is this the obsolete page length command?  Find more info. */
/* Page length.
 * There is little documentation on this command but it is part of the QL CET,
 * and is mentioned in passing for other commnds.  A google for it doesn't turn
 * up much useful information other than a possible valid range of 5-128.
 * Some testing will be required.
 */
/** \todo This command needs more investigation */
Bool pcl5op_ampersand_l_P(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  uint32 length = (uint32) value.integer ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* Range check */
  if ((length < 5) || (length > 128))
    return TRUE ;

  /** \todo Is this correct? Added since this shouldn't do setpagedevice */
  if (pcl5_ctxt->pass_through_mode == PCLXL_SNIPPET_JOB_PASS_THROUGH)
    return TRUE ;

  if (! set_page_length(pcl5_ctxt, length))
    return FALSE ;

  return TRUE ;
}

/* Orientation */
Bool pcl5op_ampersand_l_O(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
#define NUM_VALID_VALUES 4
  PageCtrlInfo *page_info ;
  uint32 i ;
  uint32 valid_values[NUM_VALID_VALUES] = {0,1,2,3} ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  for (i=0; i<NUM_VALID_VALUES; i++) {
    if ((uint32)value.integer == valid_values[i])
      break ;
  }

  /* Ignore the command if not one of these */
  if (i >= NUM_VALID_VALUES)
    return TRUE ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  /* The printer ignores this command if the orientation is unchanged */
  if ((uint32)value.integer == page_info->orientation)
    return TRUE ;

  if (pcl5_ctxt->pass_through_mode == PCLXL_SNIPPET_JOB_PASS_THROUGH)
    return TRUE ;

  if (! set_orientation(pcl5_ctxt, (uint32) value.integer))
    return FALSE ;

  return TRUE ;

#undef NUM_VALID_VALUES
}

/* Print Direction */
/* N.B. This appears to implicitly end raster graphics regardless of whether
 *      the printdirection is changed, so this is handled in the scanner.
 */
Bool pcl5op_ampersand_a_P(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
#define NUM_VALID_VALUES 4
  PageCtrlInfo *page_info ;
  uint32 i ;
  uint32 valid_values[NUM_VALID_VALUES] = {0,90,180,270} ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  for (i=0; i<NUM_VALID_VALUES; i++) {
    if ((uint32)value.integer == valid_values[i])
      break ;
  }

  /* Ignore the command if not one of these */
  if (i >= NUM_VALID_VALUES)
    return TRUE ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  /* There is nothing to do if the print direction is unchanged */
  if ((uint32)value.integer == page_info->print_direction)
    return TRUE ;

  if (! set_printdirection(pcl5_ctxt, (uint32)value.integer))
    return FALSE ;

  return TRUE ;

#undef NUM_VALID_VALUES
}

/* Left Margin */
Bool pcl5op_ampersand_a_L(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* N.B. Values appear to be limited to something like the range
     (-32767 to 32767) and negative values treated as positive. */
  value.integer = (int32) pcl5_limit_to_range(value.integer, -32767, 32767) ;

  if (! set_left_margin(pcl5_ctxt, (uint32)abs(value.integer)))
    return FALSE ;

  return TRUE ;
}

/* Right Margin */
Bool pcl5op_ampersand_a_M(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* N.B. Values appear to be limited to something like the range
     (-32767 to 32767) and negative values treated as positive. */
  value.integer = (int32) pcl5_limit_to_range(value.integer, -32767, 32767) ;

  if (! set_right_margin(pcl5_ctxt, (uint32) abs(value.integer)))
    return FALSE ;

  return TRUE ;
}


/* ESC-9 Clear Horizontal Margins */
/* Ignore explicit_sign and value as they will NOT be set for two
   character escape sequences. */
Bool pcl5op_9(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PageCtrlInfo *page_info ;
  PCL5Ctms *ctms = NULL;
  OMATRIX orig_orientation_ctm;

  UNUSED_PARAM(int32, explicit_sign) ;
  UNUSED_PARAM(PCL5Numeric, value) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  ctms = get_pcl5_ctms(pcl5_ctxt);

  /* Reset the margins */
  page_info->left_margin = 0 ;
  page_info->right_margin = page_info->page_width ;

  /* If the cursor has not yet been explicitly set, move it to the default
   * horizontal (i.e. left margin) position.
   * N.B. It is assumed here that the cursor value cannot be negative,
   *      so cannot be left of the margin, (c.f. set_left_margin).
   *      This assumption may not be strictly correct, (see cursorpos.c
   *      comments on moving cursor to te logical page).
   */

  if (! cursor_explicitly_set(pcl5_ctxt))
    set_default_cursor_x_position(pcl5_ctxt) ;

  orig_orientation_ctm = *ctm_orientation(ctms);
  ctm_recalculate(pcl5_ctxt);
  /* change to margins affects the PCL orientation ctm, and so the current
   * picture frame anchor point needs adjusting to ensure that picture
   * frame remains constant.
   */
  handle_ctm_change_picture_frame(pcl5_ctxt,
                                    &orig_orientation_ctm,
                                    ctm_orientation(ctms));
  return TRUE ;
}


/* Top Margin */
Bool pcl5op_ampersand_l_E(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PCL5PrintState *p_state ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  p_state = pcl5_ctxt->print_state ;
  HQASSERT(p_state != NULL, "p_state is NULL") ;

  /* N.B. Values appear to be limited to something like the range
   * (-32767 to 32767) and negative values treated as positive.
   */
  value.integer = (int32) pcl5_limit_to_range(value.integer, -32767, 32767) ;

  set_top_margin(pcl5_ctxt, (uint32) abs(value.integer)) ;
  reset_text_length(pcl5_ctxt) ;

  return TRUE ;
}


/* Text Length */
Bool pcl5op_ampersand_l_F(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* N.B. Negative values appear to be treated as positive.  Assume by analogy
   * with top margin command that values are limited to something like the
   * range (-32767 to 32767).
   */
  value.integer = (int32) pcl5_limit_to_range(value.integer, -32767, 32767) ;

  /* This sets the text length, i.e. in effect the bottom margin */
  if (! set_text_length(pcl5_ctxt, (uint32) abs(value.integer)))
    return FALSE ;

  return TRUE ;
}


/* Perforation Skip */
Bool pcl5op_ampersand_l_L(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PageCtrlInfo *page_info ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  /* Ignore out of range value */
  if (value.integer != 0 && value.integer != 1)
    return TRUE ;

  /* N.B. It does not appear to be the case that the top margin is defaulted.
   * It is possible that the page length may be, (the text length isn't).
   */
  /** \todo Check whether page length is defaulted */
  page_info->perforation_skip = value.integer ;

  return TRUE ;
}


/* Horizontal Motion Index (HMI) */
Bool pcl5op_ampersand_k_H(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* N.B. The command is not ignored for values outside the range so assume
   *      the values are limited to the range.
   *
   *      They also appear to be truncated to 4 decimal places, and negative
   *      values treated as positive.
   */
  value.real = pcl5_limit_to_range(value.real, -32767, 32767) ;
  value.real = pcl5_truncate_to_4d(value.real) ;

  if (! set_hmi_directly(pcl5_ctxt, fabs(value.real)))
    return FALSE ;

  return TRUE ;
}

/* Vertical Motion Index (VMI) */
/** \todo Somewhere we will need to be able to deal with out of range user defaults */
Bool pcl5op_ampersand_l_C(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* N.B. It has not been possible to test whether values greater than 32767
   *      are limited to 32767, however assume they are by analogy with HMI.
   *
   *      They do appear to be truncated to 4 decimal places, and negative
   *      values treated as positive.
   */
  value.real = pcl5_limit_to_range(value.real, -32767, 32767) ;
  value.real = pcl5_truncate_to_4d(value.real) ;

  set_vmi_directly(pcl5_ctxt, fabs(value.real)) ;

  return TRUE ;
}

/* Line Spacing */
Bool pcl5op_ampersand_l_D(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
#define NUM_VALID_VALUES 11
  uint32 i ;

  /* N.B. Negative values appear to be treated as positive */
  uint32 spacing = (uint32)abs(value.integer) ;

  /* N.B. The Tech Ref is unclear on whether 0 is a valid value.  However, it
   * appears to be being treated as the value 12, so allow it here.
   */
  uint32 valid_values[NUM_VALID_VALUES] = {0,1,2,3,4,6,8,12,16,24,48} ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  for (i=0; i<NUM_VALID_VALUES; i++) {
    if (spacing == valid_values[i])
      break ;
  }

  /* Ignore the command if not one of these */
  if (i<NUM_VALID_VALUES) {
    set_vmi_from_lpi(pcl5_ctxt, spacing) ;
  }

  return TRUE ;
}
    /* Custom Paper Width */
Bool pcl5op_ampersand_f_I(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  /** \todo Are negative values treated as positive? */
  uint32 paper_width = (uint32) value.integer ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  if (pcl5_ctxt->pass_through_mode == PCLXL_SNIPPET_JOB_PASS_THROUGH)
    return TRUE ;

  if (! set_custom_paper_width(pcl5_ctxt, paper_width))
    return FALSE ;

  return TRUE ;
}

/* Paper Paper Length/Height */
Bool pcl5op_ampersand_f_J(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  /** \todo Are negative values treated as positive? */
  uint32 paper_length = (uint32) value.integer ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  if (pcl5_ctxt->pass_through_mode == PCLXL_SNIPPET_JOB_PASS_THROUGH)
    return TRUE ;

  if (! set_custom_paper_length(pcl5_ctxt, paper_length))
    return FALSE ;

  return TRUE ;
}

#undef NUM_VALID_VALUES

/* ============================================================================
* Log stripped */
