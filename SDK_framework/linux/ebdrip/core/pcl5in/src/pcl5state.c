/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pcl5state.c(EBDSDK_P.1) $
 * $Id: src:pcl5state.c,v 1.157.1.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of PCL5 Print Environment, Cursor Posn Stack, etc.
 */

#include "core.h"
#include "pcl5state.h"

#include "pcl5context_private.h"
#include "printenvironment_private.h"
#include "cursorpos.h"
#include "pagecontrol.h"
#include "pcl5color.h"
#include "pcl5raster.h"
#include "hpgl2config.h"
#include "pcl5fonts.h"
#include "pcl5ctm.h"
#include "hpgl2state.h"
#include "resourcecache.h"
#include "macros.h"
#include "hqmemset.h"
#include "hqmemcpy.h"

#include "objects.h"
#include "namedef_.h"
#include "devops.h"
#include "dicthash.h"
#include "dictscan.h"
#include "display.h"
#include "graphics.h"
#include "gstack.h"
#include "gu_ctm.h"
#include "miscops.h"
#include "monitor.h"
#include "params.h"
#include "stacks.h"
#include "swcopyf.h"
#include "swerrors.h"
#include "ascii.h"

/* ============================================================================
 * Utility functions.
 * ============================================================================
 */

/* Get the default MPE (i.e. the MPE representing the PJL current
   environment) from the print environments list. External callers
   should not need to change values in the default MPE. */
const PCL5PrintEnvironment* get_default_mpe(PCL5PrintState *p_state)
{
  PCL5PrintEnvironmentLink *p_env_tail;
  const PCL5PrintEnvironment *mpe;

  HQASSERT( p_state != NULL, "PCL5PrintState is NULL");

  /* Get the tail link from the print environments list */
  p_env_tail = SLL_GET_TAIL(&(p_state->mpe_list), PCL5PrintEnvironmentLink, sll);
  HQASSERT( p_env_tail->mpe_type == PJL_CURRENT_ENV,
             "Unexpected print environment link type " );

  HQASSERT( p_env_tail->slevel == 0, "Unexpected slevel in default mpe");
  HQASSERT( p_env_tail->macro_nest_level == -1,
             "Unexpected macro nest level in default mpe");

  mpe = &(p_env_tail->mpe);
  return mpe;
}

PCL5PrintEnvironment* get_current_mpe(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *p_state;
  PCL5PrintEnvironment *mpe;

  HQASSERT( pcl5_ctxt != NULL, "pcl5_ctxt is NULL");

  p_state = pcl5_ctxt->print_state;
  HQASSERT( p_state != NULL, "PCL5PrintState is NULL");

  mpe = p_state->mpe;
  return mpe;
}

/* ============================================================================
 * Default, save and restore print environment for module specific
 * code. Please keep functions prefixed default_, save_ and
 * restore_. Also please try to keep the same pattern for function
 * signatures.
 * ============================================================================
 */

/* Set the passed print environment to default values. This is like
   setting static initializers. */
static
void default_pcl5_print_environment(PCL5Context *pcl5_ctxt,
                                    PCL5PrintEnvironment* env)
{
  PCL5ConfigParams* config_params = &pcl5_ctxt->config_params;

  default_job_control(&env->job_ctrl, config_params);
  default_page_control(&env->page_ctrl, config_params);
  default_text_info(&env->text_info);
  default_font_info(&env->font_info);
  default_font_management(&env->font_management);
  default_macro_info(&env->macro_info);
  default_print_model(&env->print_model);
  default_raster_graphics_info(&env->raster_graphics);
  default_trouble_shooting_info(&env->trouble_shooting);
  default_status_readback_info(&env->status_readback);
  default_picture_frame_info(&env->picture_frame);
  default_area_fill(&env->area_fill);
  default_ctm(&env->pcl5_ctms);
  default_color_info(&env->color_info);

  /* Do all the HPGL2 default values. */
  default_hpgl2printenv(env);
}

static
Bool save_pcl5_print_environment(PCL5Context *pcl5_ctxt,
                                 PCL5PrintEnvironment *to_env,
                                 PCL5PrintEnvironment *from_env,
                                 Bool overlay)
{
  /** \todo The Tech Ref mentions preserving the cursor position stack
   *  if this is an overlay macro.  However elsewhere it is described
   *  as not part of the MPE, which seems more believable.  Should
   *  check.
   */
  save_job_control(pcl5_ctxt, &to_env->job_ctrl, &from_env->job_ctrl, overlay);
  save_page_control(pcl5_ctxt, &to_env->page_ctrl, &from_env->page_ctrl, overlay) ;

  if (! save_font_management_info(pcl5_ctxt, &to_env->font_management, &from_env->font_management))
    return FALSE ;

  if (! save_macro_info(pcl5_ctxt, &to_env->macro_info, &from_env->macro_info))
    return FALSE ;

  if (! save_font_info(pcl5_ctxt, &to_env->font_info, &from_env->font_info, overlay))
    return FALSE ;

  save_pcl5_print_model(pcl5_ctxt, &to_env->print_model, &from_env->print_model, overlay);
  save_raster_graphics_info(pcl5_ctxt, &to_env->raster_graphics, &from_env->raster_graphics, overlay);

  /* Add more if needed. */
#if 0
  save_trouble_shooting_info(pcl5_ctxt, &to_env->trouble_shooting, &from_env->trouble_shooting);
  save_status_readback_info(pcl5_ctxt, &to_env->status_readback, &from_env->status_readback);
  save_picture_frame_info(pcl5_ctxt, &to_env->picture_frame, &from_env->picture_frame);
  save_area_fill(pcl5_ctxt, &to_env->area_fill, &from_env->area_fill);
  save_ctm(pcl5_ctxt, &to_env->pcl5_ctms, &from_env->pcl5_ctms);
#endif

  save_color_info(pcl5_ctxt, &to_env->color_info, &from_env->color_info);

  /* Save all the HPGL2 values. */
  save_hpgl2printenv(pcl5_ctxt, to_env, from_env);

  return TRUE ;
}

/* Allow modules to restore any internal state that they might need, and to
 * free any allocated data, such as ID strings, in the 'from' environment,
 * this is about to be freed.
 */
static
void restore_pcl5_print_environment(PCL5Context *pcl5_ctxt,
                                    PCL5PrintEnvironment *to_env,
                                    PCL5PrintEnvironment *from_env)
{
  restore_job_control(pcl5_ctxt, &to_env->job_ctrl, &from_env->job_ctrl);
  restore_page_control(pcl5_ctxt, &to_env->page_ctrl, &from_env->page_ctrl);
  restore_font_info(pcl5_ctxt, &to_env->font_info, &from_env->font_info);
  restore_font_management_info(pcl5_ctxt, &to_env->font_management, &from_env->font_management);
  restore_macro_info(pcl5_ctxt, &to_env->macro_info, &from_env->macro_info);
  restore_pcl5_print_model(pcl5_ctxt, &to_env->print_model, &from_env->print_model);
  restore_raster_graphics_info(pcl5_ctxt, &to_env->raster_graphics, &from_env->raster_graphics);

  /* Add more if needed. */
#if 0
  restore_trouble_shooting_info(pcl5_ctxt, &to_env->trouble_shooting, &from_env->trouble_shooting);
  restore_status_readback_info(pcl5_ctxt, &to_env->status_readback, &from_env->status_readback);
  restore_picture_frame_info(pcl5_ctxt, &to_env->picture_frame, &from_env->picture_frame);
  restore_area_fill(pcl5_ctxt, &to_env->area_fill, &from_env->area_fill);
  restore_ctm(pcl5_ctxt, &to_env->pcl5_ctms, &from_env->pcl5_ctms);
#endif

  restore_color_info(pcl5_ctxt, &to_env->color_info, &from_env->color_info);

  /* Restore all the HPGL2 values. */
  restore_hpgl2printenv(pcl5_ctxt, to_env, from_env);
}

/* ============================================================================
 * (Re)set device related, i.e. pagedevice and userdict values from MPE,
 * or conversely set MPE values from the pagedevice.
 * ============================================================================
 */
/* N.B. In fact this also sets one value from the PCL5PrintState */
Bool set_device_from_MPE(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *print_state ;
  JobCtrlInfo *job_info ;
  PageCtrlInfo *page_info ;
  uint8 buffer[512] ;
  OBJECT *cpd ;
  Bool needs_spd ;
  ps_context_t *pscontext ;

  enum {
    duplex,
    tumble,
    pcl_page_size,
    pcl_orientation,
    pcl5_media_type,
    pcl5_paper_source,
    NUM_ENTRIES
  } ;

  static NAMETYPEMATCH match[NUM_ENTRIES + 1] = {
    {NAME_Duplex | OOPTIONAL,           1, {OBOOLEAN}},
    {NAME_Tumble | OOPTIONAL,           1, {OBOOLEAN}},
    {NAME_PCL5PageSize | OOPTIONAL,     1, {OINTEGER}},
    {NAME_PCLOrientation | OOPTIONAL,   1, {OINTEGER}},
    {NAME_PCL5MediaType | OOPTIONAL,    1, {OINTEGER}},
    {NAME_PCL5PaperSource | OOPTIONAL,  1, {OINTEGER}},
    DUMMY_END_MATCH
  } ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  HQASSERT(pcl5_ctxt->pass_through_mode != PCLXL_SNIPPET_JOB_PASS_THROUGH,
           "Should not be here in XL pass through mode") ;

  HQASSERT(displaylistisempty(pcl5_ctxt->corecontext->page), "Expected empty display list") ;

  HQASSERT(pcl5_ctxt->corecontext != NULL, "No core context") ;
  pscontext = pcl5_ctxt->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;

  print_state = pcl5_ctxt->print_state ;
  HQASSERT( print_state != NULL, "state pointer is NULL");

  job_info = get_job_ctrl_info(pcl5_ctxt) ;
  HQASSERT(job_info != NULL, "job_info is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  /** \todo The other values - output bin etc */
  /* N.B. For now we are assuming PCL5 and XL share the same orientation.
   * If this turns out not to be the case e.g. if XL needs to retain
   * its original orientation after a passthrough to PCL5, then this
   * should be the PCL5Orientation.
   */

  /* Build up a string for passing to the interpreter.
   * It will contain a call to run the procset procedure and a call to setpagedevice.
   * The procset will only set the color mode if different.
   */
  swncopyf(buffer, sizeof(buffer), (uint8*)
           "mark{%d /HqnEmbedded/ProcSet findresource/HqnEbd_SetColorMode get exec}stopped cleartomark",
           print_state->current_color_mode);

  if (!currentpagedevice_(pscontext))
    return FALSE;
  cpd = theTop(operandstack);
  pop(&operandstack);

  if (!dictmatch(cpd, match))
    return FALSE ;

  /* Optimise this call to setpagedevice by only calling it when something has
   * changed.
   * Use the while loop as a single shot exception handler.
   */
  needs_spd = TRUE;
  do {
    if (match[duplex].result != NULL) {
      if (oBool(*match[duplex].result) != job_info->requested_duplex)
        break;
    }
    if (match[tumble].result != NULL) {
      if (oBool(*match[tumble].result) != (job_info->requested_binding == SHORT_EDGE))
        break;
    }
    if (match[pcl_page_size].result != NULL) {
      if (oInteger(*match[pcl_page_size].result) != (int32) page_info->page_size)
        break;
    }
    if (match[pcl_orientation].result != NULL) {
      if (oInteger(*match[pcl_orientation].result) != (int32) page_info->orientation)
        break;
    }
    if (match[pcl5_media_type].result != NULL) {
      if (oInteger(*match[pcl5_media_type].result) != (int32) page_info->media_type)
        break;
    }
    if (match[pcl5_paper_source].result != NULL) {
      if (oInteger(*match[pcl5_paper_source].result) != (int32) page_info->paper_source)
        break;
    }

    needs_spd = FALSE;
  } while (FALSE);

  if (needs_spd) {
    int bufpos = (int) strlen((char *) buffer);
    swncopyf(buffer + bufpos, sizeof(buffer) - bufpos, (uint8*)
              " << /PCL5PageSize %d "
              "    /PCLOrientation %d "
              "    /PCL5PaperSource %d "
              "    /PCL5MediaType %d "
              "    /Duplex %s "
              "    /Tumble %s >> setpagedevice ",
              page_info->page_size,
              page_info->orientation,
              page_info->paper_source,
              page_info->media_type,
              (job_info->requested_duplex ? "true" : "false"),
              ((job_info->requested_binding == SHORT_EDGE) ? "true" : "false")) ;
  }

  if (! run_ps_string(buffer))
    return FALSE ;

  return TRUE ;
}

/* Convert real valued points value to integer valued centipoints */
#define PTS_TO_CENTIPTS(v)  ((int32)((v)*100 + 0.5))

/* Query the page device and set up MPE values accordingly.
 * N.B. The PCL5_PAGE_SIZE_CHANGE reason is really a type of
 *      PCL5_STATE_MAINTENANCE reason, which needs some special handling.
 *      handling.  At the moment we only test for the latter so this doesn't
 *      cause any problems, but one to bear in mind if this changes.
 */
/** \todo Review state for whole job passthrough from XL.  Currently this
 *  queries the device and fills in the base level MPE in the case of
 *  whole job passthrough.  This MPE is subsequently copied.  However,
 *  this means that apart from quantities for which we maintain a separate
 *  parameter for the requested and actual setting, (where behaviour will
 *  depend on whether the last requested setting from XL is put into both
 *  base and working MPEs), a subsequent PCL5 reset will have no effect.
 */
Bool set_MPE_from_device(PCL5Context *pcl5_ctxt, uint32 reason)
{
  PCL5PrintState *p_state ;
  JobCtrlInfo *job_info ;
  PageCtrlInfo *page_info ;
  OBJECT *dict ;
  SYSTEMVALUE values[4] ;
  int32 temp_clip[4] ;
  int32 int_val = 0 ;
  int32 i ;
  Bool page_size_provided = FALSE ;
  Bool orientation_provided = FALSE ;
  Bool page_size_change = FALSE ;
  Bool orientation_change = FALSE ;
  Bool logical_page_provided = FALSE ;
  Bool printable_area_provided = FALSE ;
  ps_context_t *pscontext ;
  Bool resolution_provided = FALSE ;
#if 0
  Bool leading_edge_provided = FALSE ;
#endif

  /* Top level pagedevice entries relevant to PCL5 MPE */
  enum {
    HW_RESOLUTION = 0,
    DUPLEX,
    TUMBLE,
    JOG,
    LEADING_EDGE,
    PAGE_SIZE,
    PCL_ORIENTATION,
    PCL_DEFAULT_DUPLEX,
    PCL_DEFAULT_ORIENTATION,
    PCL_DEFAULT_PAGE_SIZE,    /* /PageSize values corresponding to the /PCL5DefaultPageSize */
    PCL_DEFAULT_TUMBLE,
    PCL5_DEFAULT_PAGE_SIZE,
    PCL5_DEFAULT_LOGICAL_PAGE,
    PCL5_DEFAULT_MEDIA_TYPE,
    PCL5_DEFAULT_PAPER_SOURCE,
    PCL5_DEFAULT_PRINTABLE_AREA,
    PCL5_LOGICAL_PAGE,
    PCL5_MEDIA_TYPE,
    PCL5_OUTPUT_BIN,
    PCL5_PAGE_LENGTH,
    PCL5_PAPER_SOURCE,
    PCL5_PRINTABLE_AREA,
    PCL5_REQUESTED_PAGE_SIZE,
    PCL5_VMI,
    NUM_DEFAULT_ENTRIES
  } ;

  static NAMETYPEMATCH match[NUM_DEFAULT_ENTRIES + 1] = {
    {NAME_HWResolution | OOPTIONAL, 1, {OARRAY}},
    {NAME_Duplex | OOPTIONAL, 1, {OBOOLEAN}},
    {NAME_Tumble | OOPTIONAL, 1, {OBOOLEAN}},
    {NAME_Jog | OOPTIONAL, 1, {OINTEGER}},
    {NAME_LeadingEdge | OOPTIONAL, 1, {OINTEGER}},
    {NAME_PageSize, 1, {OARRAY}},
    {NAME_PCLOrientation | OOPTIONAL, 1, {OINTEGER}},
    {NAME_PCLDefaultDuplex | OOPTIONAL, 1, {OBOOLEAN}},
    {NAME_PCLDefaultOrientation | OOPTIONAL, 1, {OINTEGER}},
    {NAME_PCLDefaultPageSize, 1, {OARRAY}},
    {NAME_PCLDefaultTumble | OOPTIONAL, 1, {OBOOLEAN}},
    {NAME_PCL5DefaultPageSize | OOPTIONAL, 1, {OINTEGER}},
    {NAME_PCL5DefaultLogicalPage | OOPTIONAL, 1, {OARRAY}},
    {NAME_PCL5DefaultMediaType | OOPTIONAL, 1, {OINTEGER}},
    {NAME_PCL5DefaultPaperSource | OOPTIONAL, 1, {OINTEGER}},
    {NAME_PCL5DefaultPrintableArea | OOPTIONAL, 1, {OARRAY}},
    {NAME_PCL5LogicalPage | OOPTIONAL, 1, {OARRAY}},
    {NAME_PCL5MediaType | OOPTIONAL, 1, {OINTEGER}},
    {NAME_PCL5OutputBin | OOPTIONAL, 1, {OINTEGER}},
    {NAME_PCL5PageLength | OOPTIONAL, 1, {OINTEGER}},
    {NAME_PCL5PaperSource | OOPTIONAL, 1, {OINTEGER}},
    {NAME_PCL5PrintableArea | OOPTIONAL, 1, {OARRAY}},
    {NAME_PCL5RequestedPageSize | OOPTIONAL, 1, {OINTEGER}},
    {NAME_PCL5VMI | OOPTIONAL, 1, {OREAL}},
    DUMMY_END_MATCH
  } ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(reason == PCL5_STATE_CREATION ||
           reason == PCL5_STATE_CREATION_FOR_XL_PASSTHROUGH ||
           reason == PCL5_STATE_CHANGE_FOR_XL_PASSTHROUGH ||
           reason == PCL5_PAGE_SIZE_CHANGE ||
           reason == PCL5_STATE_MAINTENANCE,
           "Unknown reason for set_MPE_from_device") ;

  HQASSERT(pcl5_ctxt->pass_through_mode != PCLXL_NO_PASS_THROUGH ||
           (reason != PCL5_STATE_CREATION_FOR_XL_PASSTHROUGH &&
            reason != PCL5_STATE_CHANGE_FOR_XL_PASSTHROUGH),
           "Contradictory passthrough modes") ;

  HQASSERT(pcl5_ctxt->corecontext != NULL, "No core context") ;
  pscontext = pcl5_ctxt->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;

  if (! currentpagedevice_(pscontext))
    return FALSE ;

  dict = theTop(operandstack) ;
  HQASSERT( dict != NULL, "null page device dictionary") ;
  HQASSERT( oType(*dict) == ODICTIONARY && oCanRead(*oDict(*dict)),
            "Expected readable page device dictionary") ;

  p_state = pcl5_ctxt->print_state ;
  HQASSERT( p_state != NULL, "state pointer is NULL");

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT( page_info != NULL, "page_info is NULL") ;

  job_info = get_job_ctrl_info(pcl5_ctxt) ;
  HQASSERT( job_info != NULL, "job_info is NULL") ;

  /* Check dictionary entries */
  if (!dictmatch(dict, match))
    return FALSE ;

  /* Do the deed(s) ... */
  /** \todo HWResolution, Duplex, Tumble, Jog, etc, and refactor this */
  switch (reason) {
    case PCL5_STATE_CREATION:
    case PCL5_STATE_CREATION_FOR_XL_PASSTHROUGH:
      if (match[PCL5_DEFAULT_PAGE_SIZE].result)
        page_info->page_size = oInteger(*match[PCL5_DEFAULT_PAGE_SIZE].result) ;
      break ;

    case PCL5_STATE_CHANGE_FOR_XL_PASSTHROUGH:
    case PCL5_PAGE_SIZE_CHANGE:
      if (match[PCL5_REQUESTED_PAGE_SIZE].result)
        page_info->page_size = oInteger(*match[PCL5_REQUESTED_PAGE_SIZE].result) ;
      break ;

    default:
      break ;
  }

  switch (reason) {
    case PCL5_STATE_CREATION:
    case PCL5_STATE_CHANGE_FOR_XL_PASSTHROUGH:
    case PCL5_PAGE_SIZE_CHANGE:
    case PCL5_STATE_MAINTENANCE:
      if (match[PAGE_SIZE].result) {
        if (! object_get_numeric_array(match[PAGE_SIZE].result, values, 2))
          return FALSE ;
        page_size_provided = TRUE ;
      }
      break ;

    case PCL5_STATE_CREATION_FOR_XL_PASSTHROUGH:
      if (match[PCL_DEFAULT_PAGE_SIZE].result) {
        if (! object_get_numeric_array(match[PCL_DEFAULT_PAGE_SIZE].result, values, 2))
          return FALSE ;
        page_size_provided = TRUE ;
      }
      break ;
  }

  /** \todo Handle PageSize with width greater than length */
  if (page_size_provided) {
    if (values[0] > 0 && values[1] > 0) {
      uint32 width = PTS_TO_CENTIPTS(values[0]);
      uint32 length = PTS_TO_CENTIPTS(values[1]);
      if (length >= width) {
        if (width != page_info->physical_page_width ||
            length != page_info->physical_page_length) {
          page_info->physical_page_width = width;
          page_info->physical_page_length = length;
          page_size_change = TRUE ;
        }
      }
      else
        monitorf(( uint8 * )UVS("%%%%[ Warning: PageSize length smaller than width ]%%%%\n")) ;
    }
  }

  /* PCLOrientation
   * N.B. This must be set up before set_page_dimensions is called.
   */
  /** \todo It is possible that in order to make setpagedevice behave
   * as in red book and throw unused blank reverse side of duplex
   * sheets that it will no longer be suitable for this type of
   * orientation change, and so all 4 printable area and logical
   * page offsets would need to be provided at outset and core pcl
   * to sort out which one to use.
   */
  switch (reason) {
    case PCL5_STATE_CREATION:
    case PCL5_STATE_CHANGE_FOR_XL_PASSTHROUGH:
    case PCL5_PAGE_SIZE_CHANGE:
    case PCL5_STATE_MAINTENANCE:
      if ( match[PCL_ORIENTATION].result) {
        int_val = oInteger(*match[PCL_ORIENTATION].result) ;
        orientation_provided = TRUE ;
      }
      break ;

    case PCL5_STATE_CREATION_FOR_XL_PASSTHROUGH:
      if ( match[PCL_DEFAULT_ORIENTATION].result) {
        int_val = oInteger(*match[PCL_DEFAULT_ORIENTATION].result) ;
        orientation_provided = TRUE ;
      }
      break ;
  }

  if (orientation_provided) {
    if (int_val >= 0 && int_val < 4) {
      if ( page_info->orientation != (uint32) int_val) {
        page_info->orientation = (uint32) int_val ;
        orientation_change = TRUE ;
      }
    }
  }

  /* PCL5LogicalPage */
  /* N.B. We do not allow for the same page size to have different logical page
   *      offsets or a different printable area.  In the case of wide A4, we
   *      don't expect to swap between A4 and wide A4 during the job.
   */
  if ((reason == PCL5_STATE_CREATION) ||
      (reason == PCL5_STATE_CREATION_FOR_XL_PASSTHROUGH) ||
      page_size_change ||
      orientation_change) {

    switch (reason) {
      case PCL5_STATE_CREATION:
      case PCL5_STATE_CHANGE_FOR_XL_PASSTHROUGH:
      case PCL5_PAGE_SIZE_CHANGE:
      case PCL5_STATE_MAINTENANCE:
        if ( match[PCL5_LOGICAL_PAGE].result) {
          if (! object_get_numeric_array(match[PCL5_LOGICAL_PAGE].result, values, 4))
            return FALSE ;
          logical_page_provided = TRUE ;
        }
        break ;

      case PCL5_STATE_CREATION_FOR_XL_PASSTHROUGH:
        if ( match[PCL5_DEFAULT_LOGICAL_PAGE].result) {
          if (! object_get_numeric_array(match[PCL5_DEFAULT_LOGICAL_PAGE].result, values, 4))
            return FALSE ;
          logical_page_provided = TRUE ;
        }
        break ;
    }

    /** \todo Should the defaults be the current A4/Letter defaults or zeros? */
    if (logical_page_provided) {
      if (values[0] >= 0 && values[1] >= 0 && values[2] >= 0 && values[3] >= 0) {
        /** \todo If pcl5exec dict param is allowed to set margin values, they will
         * need to be applied after this point, or this will need to be rethought.
         */
        Bool state_creation = (reason == PCL5_STATE_CREATION ||
                               reason == PCL5_STATE_CREATION_FOR_XL_PASSTHROUGH) ;

        set_default_pcl_page_dimensions(pcl5_ctxt,
                                        state_creation,
                                        PTS_TO_CENTIPTS(values[0]),
                                        PTS_TO_CENTIPTS(values[1]),
                                        PTS_TO_CENTIPTS(values[2]),
                                        PTS_TO_CENTIPTS(values[3]));
      }
    }

    /* PCL5PrintableArea */
    /* N.B. The default pcl page dimensions must be set up before now.
     *      These clips are currently always based on the physical media
     *      in the portrait orientation.  Clips must always be applied
     *      with the base CTM in place.
     */

    switch (reason) {
      case PCL5_STATE_CREATION:
      case PCL5_STATE_CHANGE_FOR_XL_PASSTHROUGH:
      case PCL5_PAGE_SIZE_CHANGE:
      case PCL5_STATE_MAINTENANCE:
        if ( match[PCL5_PRINTABLE_AREA].result) {
          if (! object_get_numeric_array(match[PCL5_PRINTABLE_AREA].result, values,4))
            return FALSE ;
          printable_area_provided = TRUE ;
        }
        break ;

      case PCL5_STATE_CREATION_FOR_XL_PASSTHROUGH:
        if ( match[PCL5_DEFAULT_PRINTABLE_AREA].result) {
          if (! object_get_numeric_array(match[PCL5_DEFAULT_PRINTABLE_AREA].result, values,4))
            return FALSE ;
          printable_area_provided = TRUE ;
        }
        break ;
    }

    /** \todo Should the defaults be the current A4/Letter defaults or zeros? */
    if (printable_area_provided) {
      if (values[0] >= 0 && values[1] >= 0 && values[2] >= 0 && values[3] >= 0) {
        /* Convert from values left, right, top, bottom offsets from edge
         * of physical page, to top left (x0,y0), right bottom (x1,y1) of
         * printable area when expressed in the base coordinate system.
         */
        temp_clip[0] = PTS_TO_CENTIPTS(values[0]);
        temp_clip[1] = PTS_TO_CENTIPTS(values[2]);
        temp_clip[2] = page_info->physical_page_width - PTS_TO_CENTIPTS(values[1]);
        temp_clip[3] = page_info->physical_page_length - PTS_TO_CENTIPTS(values[3]);

        if ( pcl5_ctxt->state_info ) {
          pcl5_ctxt->state_info->pcl5_page_clip_offsets[0] = PTS_TO_CENTIPTS(values[0]);
          pcl5_ctxt->state_info->pcl5_page_clip_offsets[1] = PTS_TO_CENTIPTS(values[1]);
          pcl5_ctxt->state_info->pcl5_page_clip_offsets[2] = PTS_TO_CENTIPTS(values[2]);
          pcl5_ctxt->state_info->pcl5_page_clip_offsets[3] = PTS_TO_CENTIPTS(values[3]);
        }

        if (temp_clip[0] < 0)
          temp_clip[0] = 0 ;

        if (temp_clip[1] < 0)
          temp_clip[1] = 0 ;

        if (temp_clip[2] < temp_clip[0])
          temp_clip[2] = temp_clip[0] ;

        if (temp_clip[3] < temp_clip[1])
          temp_clip[3] = temp_clip[1] ;

        for (i = 0 ; i < 4; i++)
          page_info->clip[i] = (uint32) temp_clip[i] ;
      }
    }
    else if ( pcl5_ctxt->state_info ) {
      pcl5_ctxt->state_info->pcl5_page_clip_offsets[0] =
        pcl5_ctxt->state_info->pcl5_page_clip_offsets[1] =
          pcl5_ctxt->state_info->pcl5_page_clip_offsets[2] =
            pcl5_ctxt->state_info->pcl5_page_clip_offsets[3] = DEFAULT_CLIP_WIDTH;
    }
  }

  /* N.B. The default pcl page dimensions must be set up before making
   * HPGL2 changes.
   */
  if (reason == PCL5_STATE_CREATION || reason == PCL5_STATE_CREATION_FOR_XL_PASSTHROUGH) {
    /* Set up default HPGL picture frame. */
    default_picture_frame_dimensions(pcl5_ctxt) ;
    default_picture_frame_position(pcl5_ctxt) ;
  }
  else if ((reason == PCL5_PAGE_SIZE_CHANGE) || page_size_change || orientation_change) {

    /* In the event of a PCL5_PAGE_SIZE_CHANGE the margins must be defaulted
     * even if there was no actual page_size_change.
     */
    if (reason == PCL5_PAGE_SIZE_CHANGE)
      set_pcl5_margins(pcl5_ctxt, FALSE) ;

    /* N.B. In fact it may be alright to call hpgl2_handle_PCL_page_change
     * even at start of job, though probably unneccessary.
     * Strictly speaking this should also be called in the event of an
     * orientation command being received for a new orientation.  Currently,
     * this will always result in the orientation_change flag being set,
     * so no special handling is necessary for orientation.
     */
    hpgl2_handle_PCL_page_change(pcl5_ctxt) ;
  }

  /** \todo Handling for if duplex mode has changed, e.g. possible additional
   *  additional page throw or whatever is required.
   */
  if (reason != PCL5_STATE_CREATION_FOR_XL_PASSTHROUGH) {

    if (match[DUPLEX].result) {
      job_info->duplex = oBool(*match[DUPLEX].result) ;

      if (! job_info->duplex)
        job_info->binding = SIMPLEX ;
    }

    if (match[TUMBLE].result) {
      if (job_info->duplex) {
        Bool tumble = oBool(*match[TUMBLE].result) ;

        if (tumble)
          job_info->binding = SHORT_EDGE ;
        else
          job_info->binding = LONG_EDGE ;
      }
    }
  }
  else {
    if (match[PCL_DEFAULT_DUPLEX].result) {
      job_info->duplex = oBool(*match[PCL_DEFAULT_DUPLEX].result) ;

      if (! job_info->duplex)
        job_info->binding = SIMPLEX ;
    }

    if (match[PCL_DEFAULT_TUMBLE].result) {
      if (job_info->duplex) {
        Bool tumble = oBool(*match[PCL_DEFAULT_TUMBLE].result) ;

        if (tumble)
          job_info->binding = SHORT_EDGE ;
        else
          job_info->binding = LONG_EDGE ;
      }
    }
  }

  /* We need to know the last Media Type requested, which we store as an
   * integer.  Since the command may be given in the form of a string,
   * we need to pick up the equivalent integer value here.
   */
  switch (reason) {
    case PCL5_STATE_CREATION:
    case PCL5_STATE_CHANGE_FOR_XL_PASSTHROUGH:
    case PCL5_PAGE_SIZE_CHANGE:
    case PCL5_STATE_MAINTENANCE:
      /** \todo Strictly all but PCL5_STATE_CREATION here should be getting
       *  the last requested media size, (which also elimates the need to
       *  do anything here in the case of PCL5_PAGE_SIZE_CHANGE).  However,
       *  this is currently always the same as the last requested value,
       *  so leave PCL5_PAGE_SIZE_CHANGE in here for consistency).
       */
      if ( match[PCL5_MEDIA_TYPE].result)
        page_info->media_type = oInteger(*match[PCL5_MEDIA_TYPE].result) ;
      break ;

    case PCL5_STATE_CREATION_FOR_XL_PASSTHROUGH:
      if ( match[PCL5_DEFAULT_MEDIA_TYPE].result)
        page_info->media_type = oInteger(*match[PCL5_DEFAULT_MEDIA_TYPE].result) ;
      break ;
  }

  /* We need to know the last Paper Source requested, which we store as an
   * integer.  Since the command may be given in the form of a string,
   * we need to pick up the equivalent integer value here.
   */
  switch (reason) {
    case PCL5_STATE_CREATION:
    case PCL5_STATE_CHANGE_FOR_XL_PASSTHROUGH:
    case PCL5_PAGE_SIZE_CHANGE:
    case PCL5_STATE_MAINTENANCE:
      /** \todo The selection of Paper source
       * would seem to be loosely interconnected/related to
       * the selection of either/both of page size and media type.
       * So we may need to re-evaluate/re-set the paper source
       * to any one of the PCL5 default or the last requested value
       * from either the PCL5 job or the PCLXL passthrough.
       * For the moment I have simply mirrored the
       * handling of MEDIA_TYPE/DEFAULT_MEDIA_TYPE above
       */
      if ( match[PCL5_PAPER_SOURCE].result)
        page_info->paper_source = oInteger(*match[PCL5_PAPER_SOURCE].result) ;
      break ;

    case PCL5_STATE_CREATION_FOR_XL_PASSTHROUGH:
      if ( match[PCL5_DEFAULT_PAPER_SOURCE].result)
        page_info->paper_source = oInteger(*match[PCL5_DEFAULT_PAPER_SOURCE].result) ;
      break ;
  }

  /* Do a set of things that only happen for the two state creation cases */
  if (reason == PCL5_STATE_CREATION || reason == PCL5_STATE_CREATION_FOR_XL_PASSTHROUGH) {

    /* Take the values set up by the skin to be the 'desired' values.
     * N.B. In the case of XL passthrough, the last requested values for
     *      duplex and binding form part of the XL passthrough.
     */
    /** \todo May just want to make these part of the page device dict too */
    job_info->requested_duplex = job_info->duplex ;
    job_info->requested_binding = job_info->binding ;

    /* The VMI should only ever be defaulted from PJL, i.e. this is effectively
     * already a default value.
     */
    /** \todo Should this be preserved round a reset in the case of snippet
     *  passthrough - probably not?
     */
    if ( match[PCL5_VMI].result) {
      PCL5Numeric vmi_val ;

      if (! object_get_numeric(match[PCL5_VMI].result, (SYSTEMVALUE*) &(vmi_val.real)) ||
          ! pcl5op_ampersand_l_C(pcl5_ctxt, FALSE, vmi_val))
        return FALSE ;
    }

    /** \todo Do something with this value, (or don't bother getting it).
     *  We currently work this out from the CTM, which is more dynamic
     *  but should be unneccessary.
     *  N.B. We currently don't handle different x and y resolutions.
     */
    /** \todo This isn't actually part of the MPE right now, see if
     *  want to move it in, or move this code.
     */
    if ( match[HW_RESOLUTION].result) {
      if (! object_get_numeric_array(match[HW_RESOLUTION].result, values, 2))
        return FALSE ;

      if (values[0] > 0 && values[1] > 0) {
        p_state->hw_resolution[0] = (uint32) values[0] ;
        p_state->hw_resolution[1] = (uint32) values[1] ;
        resolution_provided = TRUE ;
      }
    }
  }

  /* N.B. The base coordinate system has (0,0) at the top left of the physical
   * page, with x-axis increasing right, y-axis increasing down.  This CTM is
   * never directly used for printing but gives a convenient starting point for
   * setting up the orientation CTM.
   *
   * In fact this strictly only needs to grab the gstate CTM for the base CTM,
   * and doesn't really need to do ctm_recalculate, as long as reset_page is
   * always done following state creation, printer reset and throw_page, (but
   * may be needed for setting default pattern reference point unless we
   * do a special reset first page command).
   *
   * In the case of PCL5_STATE_CREATION_FOR_XL_PASSTHROUGH the current page
   * size may not be the default page size, so it doesn't make sense to set
   * this value, leave it as the identity matrix instead.  In this case this
   * CTM should never be used.  Instead we are about to (on following call
   * to set_MPE_from_device) set the value corresponding to the current page
   * size into the working MPE.  We will then stick with the working MPE value
   * for XL snippet passthrough, whereas for XL whole job passthrough where we
   * may reset to using a copy of the base MPE, we will set_device_from_MPE as
   * part of the reset, which will result in the correct gstate value, (which
   * we will then pick up and place into the working MPE).
   */

  /** \todo Will the CTMs be OK if an XL whole job passthrough does an overlay
   *  macro?
   */

  if (reason != PCL5_STATE_CREATION_FOR_XL_PASSTHROUGH) {
    ctm_set_base(pcl5_ctxt, &thegsPageCTM(*gstateptr)) ;

    /* Sanitise CTMs to ensure sufficient accuracy */
    if ( !ctm_sanitise_default_matrix(pcl5_ctxt) )
      return FALSE;

#if 0
    /* Find out which is the leading edge or work it out from the base CTM */
    if ( match[LEADING_EDGE].result) {
      int_val = oInteger(*match[LEADING_EDGE].result) ;

      if (int_val >= 0 && int_val < 4) {
        p_state->media_orientation = (uint32) int_val ;
        leading_edge_provided = TRUE ;
      }
    }

    if (!leading_edge_provided)
#endif
      set_media_orientation(pcl5_ctxt) ;

    if (!resolution_provided &&
        (reason == PCL5_STATE_CREATION || reason == PCL5_STATE_CHANGE_FOR_XL_PASSTHROUGH)) {
      /* N.B. The base CTM must be setup before this point */
      set_device_resolution(pcl5_ctxt) ;
    }
  }

  pop(&operandstack) ;

  /* Any setpagedevice before this point will have cleared the clip,
   * so set it up again here.
   */
  /** \todo Some refactoring to make this more closely connected to
   * the setpagedevice call */
  /** \todo Should we be clipping at all if this is the first page of a passthrough? */

  /* N.B. Don't clip if we are creating a PCL5 state for XL as we will shortly
   *      set up a clip with the right values, and don't want to get the
   *      intersection of right and wrong values.
   */
  if (reason != PCL5_STATE_CREATION_FOR_XL_PASSTHROUGH) {

    OBJECT clip_obj = OBJECT_NOTVM_NOTHING ;
    USERVALUE temp_clip[4] ;
    uint32 x_resolution, y_resolution ;

    /* Convert from top left and bottom right corners of printable area to
     * x, y, width, height for rectclip operator.
     */
    temp_clip[0] = (USERVALUE) page_info->clip[0] ;
    temp_clip[1] = (USERVALUE) page_info->clip[1] ;
    temp_clip[2] = (USERVALUE) page_info->clip[2] - page_info->clip[0] ;
    temp_clip[3] = (USERVALUE) page_info->clip[3] - page_info->clip[1] ;

    /* This gives a printable area which is one pixel too wide and too long,
     * so subtract a single pixel from the clip dimensions.
     */
    /** \todo If rectclip is changed to take account of the pixel touching
     *  rule, this should be done by temporarily setting the rule to
     *  tesselating.  (Meanwhile, this is done based on the rule at render
     *  time, so it is possible this will subtract a pixel unneccessarily
     *  if we have changed to tesselating by then).
     */
    if (p_state->media_orientation == 0 || p_state->media_orientation == 2) {
      x_resolution = p_state->hw_resolution[0] ;
      y_resolution = p_state->hw_resolution[1] ;
    }
    else {
      x_resolution = p_state->hw_resolution[1] ;
      y_resolution = p_state->hw_resolution[0] ;
    }

    temp_clip[2] -= (USERVALUE) (7200.0 / x_resolution) ;
    temp_clip[3] -= (USERVALUE) (7200.0 / y_resolution) ;

    if (! ps_array(&clip_obj, 4))
      return FALSE ;

    for (i = 0 ; i < 4; i++) {
      object_store_real(&oArray(clip_obj)[i], temp_clip[i]) ;
    }

   if (! push(&clip_obj, &operandstack) || ! rectclip_(pscontext))
     return FALSE ;
  }

  return TRUE ;
}

/* ============================================================================
 * MPE management. Please prefix function pcl5_mpe_. Please also
 * always pass through PCL5Context* to keep the function sigs looking
 * the same not to mention its highly likely that these functions will
 * need the PCL5 context anyway.
 * ============================================================================
 */

/** Unpack the optional parameters dict, overwriting relevant settings
    to complete the PJL current environment (as understood by
    core). \todo The other parameters. Maybe some of this code can also
    be shared with setpcl5defaults. */
static
Bool pcl5_mpe_apply_pjl_changes(PCL5Context *pcl5_ctxt,
                                PCL5ConfigParams* config_params,
                                Bool *font_info_provided,
                                int32 *font_number,
                                int32 *font_source)
{
  HQASSERT( pcl5_ctxt != NULL, "pcl5_ctxt is NULL");
  HQASSERT( config_params != NULL, "config_params is NULL");

  if ( !jobcontrol_apply_pjl_changes(pcl5_ctxt, config_params))
    return FALSE ;

  if ( !pagecontrol_apply_pjl_changes(pcl5_ctxt, config_params))
    return FALSE ;

  if ( !pcl5fonts_apply_pjl_changes(pcl5_ctxt,
                                    config_params,
                                    font_info_provided,
                                    font_number,
                                    font_source))
    return FALSE ;

  return TRUE ;
}


/* This creates a PJL default environment which is a combination of C
   static defaults built into the RIP C code and set via the
   default_pcl5_print_environment() function, then by applying any PJL
   modifications to that. */
static
Bool pcl5_mpe_create_defaults(PCL5Context *pcl5_ctxt, PCL5ConfigParams* config_params)
{
  PCL5PrintState *p_state;
  PCL5PrintEnvironmentLink *p_env;
  int32 font_number = 0, font_source = 0;
  Bool font_info_provided = FALSE;
  int32 creation_reason = PCL5_STATE_CREATION;

  HQASSERT( pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  p_state = pcl5_ctxt->print_state ;
  HQASSERT( p_state != NULL, "state pointer is NULL");

  /* Initialise the environments list */
  SLL_RESET_LIST(&p_state->mpe_list);

  p_env = mm_alloc(mm_pcl_pool, sizeof(PCL5PrintEnvironmentLink),
                   MM_ALLOC_CLASS_PCL_CONTEXT);

  if (p_env == NULL)
    return error_handler(VMERROR);

  /* Quick initialisation of environment link */
  HqMemZero(p_env, sizeof(PCL5PrintEnvironmentLink));

  /* Default the PCL5PrintEnvironment */
  default_pcl5_print_environment(pcl5_ctxt, &(p_env->mpe));

  p_env->slevel = 0;
  p_env->macro_nest_level = -1;
  p_env->mpe_type = PJL_CURRENT_ENV;

  /* Add new environment link to head of environments list, and add
     the shortcut to the current print environment which is needed for
     modifying PJL current environment and setting up the default
     fonts.*/
  SLL_RESET_LINK(p_env, sll);
  SLL_ADD_HEAD(&(p_state->mpe_list), p_env, sll);
  p_state->mpe = &(p_env->mpe);

  /* Use the contents of any parameters dict to modify the PJL current
     environment (as understood by core). */
  if ( config_params != NULL ) {
    if (! pcl5_mpe_apply_pjl_changes(pcl5_ctxt,
                                     config_params,
                                     &font_info_provided,
                                     &font_number,
                                     &font_source))
      goto tidyup;
  }

  /* Set the default symbol set.
   * N.B. This should be done before selecting the default font.
   *      As with setting the default font it will also get set up
   *      in an XL passthrough situation.
   */
  /** \todo Review for XL passthrough */
  if (! set_default_symbolset())
    return FALSE ;

  /* Select the default font.
   * N.B. Do this here rather than in pcl5_mpe_apply_pjl_changes so that
   * it also gets set up in an XL passthrough situation.
   */
  /** \todo If in XL snippet mode, should the default font be the XL
   *  default font, rather than the PCL5 one?
   */
  /** \todo We may want to do something different with font_info_provided flag
   *  (and font_number, font_source), possibly combine into some kind of
   *  font info structure, if it turns out to be needed for the long term.
   */
  if (! default_pcl5fonts(pcl5_ctxt, font_number, font_source, font_info_provided))
    return FALSE;

  /* We're not expecting a PS font to have been set up yet */
  HQASSERT(p_state->mpe->font_info.primary_font.ps_font_validity == PS_FONT_INVALID,
           "Unexpected PS font" ) ;

  /* Query the pagedevice for any default parameters */
  if (pcl5_ctxt->pass_through_mode != PCLXL_NO_PASS_THROUGH)
    creation_reason = PCL5_STATE_CREATION_FOR_XL_PASSTHROUGH ;

  if (! set_MPE_from_device(pcl5_ctxt, creation_reason))
    goto tidyup;

  /* N.B. This currently relies on valid CTMs at this point */
  /** \todo Review this */
  set_default_pattern_reference_point(pcl5_ctxt) ;

  /* Add a working copy to the head of the list. This also fills in
     the shortcut from the PCL5PrintState to the current print
     environment, (MPE). */
  if (! pcl5_mpe_save(pcl5_ctxt, CREATE_MPE))
    goto tidyup;

  return TRUE;

tidyup:
  mm_free(mm_pcl_pool, p_env, sizeof(PCL5PrintEnvironmentLink));
  return FALSE;
}


/* The purpose of this function is to free up any data pointed to by the
 * MPE element, e.g. prior to freeing the MPE, or overwriting the pointers.
 */
static
void pcl5_mpe_cleanup(PCL5PrintEnvironment *mpe)
{
  cleanup_macroinfo_strings(&(mpe->macro_info));
  cleanup_font_management_ID_string(&(mpe->font_management));
}


static
void pcl5_mpe_destroy_all(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *p_state ;
  PCL5PrintEnvironmentLink *p_env ;

  p_state = pcl5_ctxt->print_state;
  HQASSERT( p_state != NULL, "state pointer is NULL");

  while (! SLL_LIST_IS_EMPTY(&(p_state->mpe_list)))
  {
    p_env = SLL_GET_HEAD(&(p_state->mpe_list), PCL5PrintEnvironmentLink, sll);
    SLL_REMOVE_HEAD(&(p_state->mpe_list));

    pcl5_mpe_cleanup(&(p_env->mpe)) ;
    mm_free(mm_pcl_pool, p_env, sizeof(PCL5PrintEnvironmentLink));
  }

  /* Reset the shortcut to the first PCL5PrintEnvironment */
  p_state->mpe = NULL;
}


/* Restore the MPE on exiting a called or overlay macro */
Bool pcl5_mpe_restore(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *p_state;
  PCL5PrintEnvironmentLink *p_env_head, *p_env_head_new = NULL;
  PageCtrlInfo *page_info_new, *page_info;
  Bool page_settings_changed = FALSE;
  Bool success = TRUE;

  HQASSERT( pcl5_ctxt != NULL, "context pointer is NULL");

  p_state = pcl5_ctxt->print_state;
  HQASSERT( p_state != NULL, "state pointer is NULL");

  HQASSERT( ! SLL_LIST_IS_EMPTY(&(p_state->mpe_list)),
            "Print environments list is empty ");

  /* Get the head link */
  p_env_head = SLL_GET_HEAD(&(p_state->mpe_list), PCL5PrintEnvironmentLink, sll);

  /* Check we have a macro level MPE */
  HQASSERT( p_env_head->mpe_type > MODIFIED_PRINT_ENV &&
            p_env_head->macro_nest_level > 0,
            "Unexpected MPE type ");

  p_env_head_new = SLL_GET_NEXT(p_env_head, PCL5PrintEnvironmentLink, sll);

  /* Allow modules to restore any internal state that they might need.
   * N.B. They should also free any allocated data, such as ID strings
   *      in the 'from' environment since this itself is about to be
   *      freed.
   */
  restore_pcl5_print_environment(pcl5_ctxt, &(p_env_head_new->mpe), &(p_env_head->mpe));

  page_info = &(p_env_head->mpe.page_ctrl);
  page_info_new = &(p_env_head_new->mpe.page_ctrl);

  /** \todo PCL5 Tech Ref page 24-8 also mentions page_length here.  At the moment
   *  this depends on page size and orientation, but what about obsolete page length
   *  command?  Also what about a paper source change?
   *
   *  Orientation is not explicitly mentioned here, (PCL5 Tech Ref page 5-9).
   *  Perhaps this means the combination of orientation and print direction?
   *  Should find out whether ending a macro means ending raster graphics anyway,
   *  and what should happen to the left graphics margin.
   */
  if ((page_info_new->orientation != page_info->orientation ||
       page_info_new->print_direction != page_info->print_direction) &&
      pcl5_ctxt->interpreter_mode == RASTER_GRAPHICS_MODE) {

    /* End raster graphics */
    if (!end_raster_graphics_callback(pcl5_ctxt, TRUE))
      success = FALSE;
  }

  /* N.B. Currently physical page length is always greater than physical page
   *      width, so no need to check for these values being swapped over.
   *
   *      This does not appear to be done if we are about to drop below the
   *      level of an overlay macro, presumably because we are about to do the
   *      page throw that caused the overlay macro to run.
   *
   *      Orientation goes here, as expected, and printdirection, though that
   *      is less expected.  They both go here separately, i.e. it is not just
   *      the combination that needs to have changed.
   *
   *      Interestingly the reference printer also throws a page here if the
   *      printdirection changes, even though a printdirection change would
   *      not normally cause a page throw.
   */
  /** \todo Check for swapped width/length if above changes. */

  if ((p_env_head->mpe_type != OVERLAY_ENV_1) &&
      (page_info_new->physical_page_width != page_info->physical_page_width ||
       page_info_new->physical_page_length != page_info->physical_page_length ||
       page_info_new->orientation != page_info->orientation ||
       page_info_new->print_direction != page_info->print_direction ||
       page_info_new->page_length != page_info->page_length ||
       page_info_new->page_width != page_info->page_width))
  {
    page_settings_changed = TRUE;

    /* Print anything in printbuffer */
    success = success && throw_page(pcl5_ctxt, FALSE, FALSE) ;
  }

  /* N.B. It should not be necessary to cleanup the MPE here, since this
   *      should have been done as part of restore_pcl5_print_environment
   *      above.
   */
  SLL_REMOVE_HEAD(&(p_state->mpe_list));
  mm_free(mm_pcl_pool, p_env_head, sizeof(PCL5PrintEnvironmentLink));

  /* Point the PCL5PrintState shortcut back to the active MPE */
  p_state->mpe = &(p_env_head_new->mpe);

  if (!success)
    return FALSE;

  /* N.B. If the primary or secondary font used at the new (restored) macro
   *      level has been deleted, the font will already have been marked
   *      as invalid, (see PCL5 Tech Ref page 24-8).
   *
   *      All that remains is to invalidate the ps_font_set flags.  (This is
   *      done here since it needs the PCL5Context to contain the new MPE.
   */
  invalidate_ps_font_match(pcl5_ctxt) ;

  /* Ensure that we do any DEVICE_SETG that may be needed after this point */
  p_state->setg_required += 1 ;

  reset_last_pcl5_and_hpgl_colors();
  set_hpgl2_sync_required();

#ifdef RQ64403
  if (!pcl5_set_text_run_restart_required(pcl5_ctxt, FALSE))
    return FALSE ;
#endif
  /* Keep device settings in step with new MPE */
  if (page_settings_changed) {

    /* Make the PS setpagedevice call and fill in the resulting MPE state
     * including e.g. any new page dimensions, then set up the new page
     * and CTM settings.
     */
    if (! handle_pcl5_page_change(pcl5_ctxt, NULL, PCL5_STATE_MAINTENANCE, FALSE) ||
        ! reset_page(pcl5_ctxt))
      return FALSE ;
  }

  return TRUE ;
}

Bool pcl5_mpe_save(PCL5Context *pcl5_ctxt, int32 reason)
{
  PCL5PrintState *p_state;
  PCL5PrintEnvironmentLink *p_env_new, *p_env_old, *p_env_tail ;

  HQASSERT( pcl5_ctxt != NULL, "context pointer is NULL");
  p_state = pcl5_ctxt->print_state;
  HQASSERT( p_state != NULL, "state pointer is NULL");
  HQASSERT( reason == CREATE_MPE ||
            reason == CREATE_MACRO_ENV ||
            reason == CREATE_OVERLAY_ENV,
            "Unexpected MPE save reason" );

  /* Get hold of the existing head of the list, i.e. highest save level */
  p_env_old = SLL_GET_HEAD(&(p_state->mpe_list), PCL5PrintEnvironmentLink, sll);
  p_env_tail = SLL_GET_TAIL(&(p_state->mpe_list), PCL5PrintEnvironmentLink, sll);

  HQASSERT( p_env_old->mpe_type < MAX_PRINT_ENVIRONMENT &&
            p_env_old->macro_nest_level <= MAX_MACRO_NEST_LEVEL,
            "Unexpected PCL5PrintEnvironmentLink values ");

  HQASSERT( ( p_env_old->mpe_type == PJL_CURRENT_ENV && p_env_old->macro_nest_level == -1 &&
              p_env_old->slevel == 0 && reason == CREATE_MPE ) ||
            ( p_env_old->mpe_type == MODIFIED_PRINT_ENV && p_env_old->macro_nest_level == 0 &&
              p_env_old->slevel == 1 && ( reason == CREATE_MACRO_ENV || reason == CREATE_OVERLAY_ENV)) ||
            ( p_env_old->mpe_type > MODIFIED_PRINT_ENV && p_env_old->macro_nest_level > 0 &&
              p_env_old->slevel > 1 && ( reason == CREATE_MACRO_ENV || reason == CREATE_OVERLAY_ENV)),
            "Unexpected PCL5PrintEnvironmentLink values ");

  if ( reason == CREATE_MACRO_ENV && p_env_old->macro_nest_level > MAX_MACRO_NEST_LEVEL )
    return TRUE;      /* ignore the command */

  /* Create the new link */
  if ((p_env_new = mm_alloc(mm_pcl_pool, sizeof(PCL5PrintEnvironmentLink),
                            MM_ALLOC_CLASS_PCL_CONTEXT)) == NULL)
    return error_handler(VMERROR);

  if (reason == CREATE_OVERLAY_ENV) {
    *p_env_new = *p_env_tail ;
  } else {
    *p_env_new = *p_env_old ;
  }

  /* Allow modules to save any internal state that they might need. */
  if (reason != CREATE_MPE) {
    if (! save_pcl5_print_environment(pcl5_ctxt, &(p_env_new->mpe), &(p_env_old->mpe),
                                      (reason == CREATE_OVERLAY_ENV))) {
      pcl5_mpe_cleanup(&(p_env_new->mpe)) ;
      mm_free(mm_pcl_pool, p_env_new, sizeof(PCL5PrintEnvironmentLink));
      return FALSE ;
    }
  }

  /* Update the link information */
  p_env_new->slevel++;

  switch (reason) {
  case CREATE_MPE:
    p_env_new->mpe_type = MODIFIED_PRINT_ENV;
    p_env_new->macro_nest_level = 0;
    break;

  case CREATE_MACRO_ENV:
    HQASSERT( p_env_new->mpe_type <= MACRO_ENV_3 &&
              p_env_new->mpe_type <= OVERLAY_ENV_3,
              "Unexpected PCL5PrintEnvironmentLink values ");
    p_env_new->mpe_type++;
    p_env_new->macro_nest_level++;
    break;

  case CREATE_OVERLAY_ENV:
    HQASSERT( p_env_new->mpe_type < OVERLAY_ENV_1,
              "Unexpected PCL5PrintEnvironmentLink values ");
    p_env_new->mpe_type = OVERLAY_ENV_1;
    p_env_new->macro_nest_level = 1;

#ifdef RQ64403
    if (! pcl5_set_text_run_restart_required(pcl5_ctxt, FALSE))
      return FALSE ;
#endif
    break;
  }

  /* Add new environment link to head of environments list */
  SLL_RESET_LINK(p_env_new, sll);
  SLL_ADD_HEAD(&(p_state->mpe_list), p_env_new, sll);

  /* Add the shortcut to the current print environment */
  p_state->mpe = &(p_env_new->mpe);

    /** \todo This really belongs with the code for dealing with the
     *  pagecontrol or HPGL2 environment.  Put here for now, since
     *  it uses the PCL5Context.  The CTM recalculate and HPGL2 part,
     *  as well as (presumbaly) setting the left graphics margin, must
     *  be done after PCL5 margins have been set up.
     *
     *  If it turns out that PCL5 margins should not be defaulted, then
     *  simply copy the 4 margins settings plus max_text_length to the
     *  new environment along with all the other page settings in
     *  save_page_control.
     *
     *  It is not actually clear whether the HPLG2 part of this is
     *  required at all, and if it is whether it needs to be calling
     *  hpgl2_handle_PCL_page_change or just the picture frame defaults.
     *  It will probably not be required if the PCL5 margins don't
     *  change.
     */
    if (reason == CREATE_OVERLAY_ENV) {
      OMATRIX orig_ctm ;
      PCL5Ctms *orig_ctms ;
      PCL5Numeric Id, val ;
      ColorPalette *new_palette ;

      /* The PS font may not match the new PCL environment */
      invalidate_ps_font_match(pcl5_ctxt) ;

      /* Ensure that we do any DEVICE_SETG that may be needed after this point */
      p_state->setg_required += 1 ;

      reset_last_pcl5_and_hpgl_colors();
      set_hpgl2_sync_required();

      orig_ctms = get_pcl5_ctms(pcl5_ctxt) ;
      orig_ctm = *ctm_current(orig_ctms) ;

      set_pcl5_margins(pcl5_ctxt, FALSE) ;
      ctm_recalculate(pcl5_ctxt) ;
      transform_cursors(pcl5_ctxt, &orig_ctm, ctm_current(orig_ctms)) ;
      set_default_cursor_position(pcl5_ctxt) ;

      /* Set the default HPGL2 picture frame for the new margin
         settings. */
      hpgl2_handle_PCL_page_change(pcl5_ctxt) ;

      /* Although it is a rather odd thing to do, (and is not mentioned in the
       * PCL5 Tech Ref or Color Tech Ref), the reference printer, (unlike some
       * other printers), appears to calculate and set a default left graphics
       * margin for images at this point.
       */
      set_left_graphics_margin(pcl5_ctxt, TRUE);

      /** \todo This really ought to be done in pcl5color.c.
       * Again a problem is that the PCL5Context is needed, so can't
       * be done as part of saving the print environment above.
       */
      Id.integer = 0 ;
      val.integer = 1 ; /* B & W */

      /* Ensure palette zero is selected if available.  If it doesn't exist
       * add a default black and white palette to the head of the store.
       */
      /** \todo It is not clear whether palette zero should be replaced with
       *  the black and white palette as that seems to fit the rest
       *  of the color model less well.  (Revisit if overlay macro is
       *  showing B&W where it should show color).
       */
      if (! pcl5op_ampersand_p_S(pcl5_ctxt, FALSE, Id))
        goto tidyup;

      if (get_active_palette(pcl5_ctxt)->ID != 0) {
        new_palette = copy_active_palette(pcl5_ctxt, 0);

        if (new_palette != NULL)
          SLL_ADD_HEAD(&(pcl5_ctxt->print_state->color_state.palette_store), new_palette, sll);
      }

      /* Actually could check first whether the active palette is
         already B&W */
      if (! create_simple_palette(pcl5_ctxt, PCL5_CS_GRAY))
        goto tidyup;
    }

  return TRUE;

tidyup:
  HQASSERT(reason != CREATE_MPE, "Unexpected reason");
  HQASSERT(p_env_new == SLL_GET_HEAD(&(p_state->mpe_list), PCL5PrintEnvironmentLink, sll),
           "Expected new MPE to be attached by now");

  SLL_REMOVE_HEAD(&(p_state->mpe_list));
  pcl5_mpe_cleanup(&(p_env_new->mpe));
  mm_free(mm_pcl_pool, p_env_new, sizeof(PCL5PrintEnvironmentLink));
  return FALSE ;
}


/* Reinstate values into the provided (current MPE) that were saved before
 * the MPE was reset.  This is necessary as pagedevice related settings
 * must not be changed on a printer reset.
 */
/** \todo May want to make this function more generic, or if we ended up
 *  doing a full mpe_restore on printer reset could be integrated with
 *  e.g. restore_page_control, restore_job_control.
 */
void preserve_mpe_values_on_reset(PCL5Context* pcl5_ctxt,
                                  JobCtrlInfo *saved_job_info,
                                  PageCtrlInfo *saved_page_info,
                                  PCL5Ctms *saved_ctms)
{
  JobCtrlInfo *job_info ;
  PageCtrlInfo *page_info ;
  int32 i ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  job_info = get_job_ctrl_info(pcl5_ctxt) ;
  HQASSERT(job_info != NULL, "job_info is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  job_info->duplex = saved_job_info->duplex ;
  job_info->binding = saved_job_info->binding ;
  job_info->job_separation = saved_job_info->job_separation ;
  job_info->output_bin = saved_job_info->output_bin ;
  job_info->requested_duplex = saved_job_info->requested_duplex ;
  job_info->requested_binding = saved_job_info->requested_binding ;

  page_info->page_size = saved_page_info->page_size ;
  page_info->orientation = saved_page_info->orientation ;
  page_info->paper_source = saved_page_info->paper_source ;
  page_info->media_type = saved_page_info->media_type ;
  page_info->physical_page_width = saved_page_info->physical_page_width ;
  page_info->physical_page_length = saved_page_info->physical_page_length ;

  for (i= 0; i < 4; i++)
    page_info->clip[i] = saved_page_info->clip[i] ;

  page_info->portrait_offset = saved_page_info->portrait_offset ;
  page_info->landscape_offset = saved_page_info->landscape_offset ;
  page_info->page_width = saved_page_info->page_width ;
  page_info->page_length = saved_page_info->page_length ;

  /* Adjust the reset margin values to match the saved page dimensions */
  set_pcl5_margins(pcl5_ctxt, FALSE) ;

  /* Set up the correct CTMs, by starting from the base CTM saved from
   * before the reset, and by recalculating the other CTMs.
   * N.B. This must be done after the margins have been set up above,
   *      in order to get the correct calculation for CTMs above base.
   */
  ctm_set_base(pcl5_ctxt, ctm_base(saved_ctms)) ;
}

/* ============================================================================
 * Print state management. Please prefix all functions
 * pcl5_printstate_ and always pass through PCL5Context*.
 * ============================================================================
 */

/* Unpack any values from the optional parameters dict that are used
 * directly by the PCL5PrintState.
 */
static
Bool get_pcl5_print_state_dict_params(PCL5Context *pcl5_ctxt, PCL5ConfigParams* config_params)
{
  PCL5PrintState *print_state ;

  HQASSERT( pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT( config_params != NULL, "config_params is NULL") ;
  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  UNUSED_PARAM(PCL5Context*, pcl5_ctxt);
  UNUSED_PARAM(PCL5ConfigParams*, config_params);

  return TRUE ;
}


Bool pcl5_printstate_create(PCL5Context *pcl5_ctxt, PCL5ConfigParams* config_params)
{
  PCL5PrintState *p_state;
  OBJECT ufstname = OBJECT_NOTVM_STRING("UFST");
  OBJECT ffname = OBJECT_NOTVM_STRING("FF");
  OBJECT back_channel = OBJECT_NOTVM_STRING("%embedded%/BackChannel");
  ps_context_t *pscontext ;

  HQASSERT( pcl5_ctxt != NULL, "pcl5_ctxt is NULL");

  HQASSERT(pcl5_ctxt->corecontext != NULL, "No core context") ;
  pscontext = pcl5_ctxt->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;

  p_state = mm_alloc(mm_pcl_pool, sizeof(PCL5PrintState),
                     MM_ALLOC_CLASS_PCL_CONTEXT);

  if (p_state == NULL)
    return error_handler(VMERROR);

  pcl5_ctxt->print_state = p_state;

  /* Quick initialisation of state */
  HqMemZero(p_state, sizeof(PCL5PrintState));

  p_state->save = onothing ;          /* Struct copy to set slot properties */
  p_state->back_channel = back_channel ;

  p_state->page_number = 1;
  p_state->duplex_page_number = 1;

  set_default_font_state(&(p_state->font_state)) ;

  p_state->possible_raster_data_trim = FALSE ;

  /* Assume SETG is required. This is a count, not a boolean. */
  p_state->setg_required = 1 ;

  reset_last_pcl5_and_hpgl_colors();
  set_hpgl2_sync_required();

  /* Get the handle for the PFIN module.
   * N.B. There is no need to release this when done.
   */

  /* \todo : For the moment, PCL favours the ufst module, but looks for
   * a font fusion module if no ufst module is found. This will have to
   * be made properly configurable at some stage.
   */
  p_state->ufst = pfin_findpfin(&ufstname);
  if (p_state->ufst == NULL)
    p_state->ufst = pfin_findpfin(&ffname);

  if (p_state->ufst == NULL) {
    HQTRACE(TRUE,("Unable to find UFST module."));
    (void) error_handler(CONFIGURATIONERROR);
    goto tidyup;
  }

  /* Get the default color mode i.e. monochrome, RGB, etc */
  /** \todo Should this be part of color_state_init, or
   *  possible set_MPE_from_device, (though not part of MPE).
   */
  get_default_color_mode(pcl5_ctxt);

  /* N.B. This includes setting up the default B&W palette */
  if (! color_state_init(pcl5_ctxt))
    goto tidyup ;

  if (! hpgl2_print_state_init(&p_state->hpgl2_print_state) )
    goto tidyup;

  /* Set up growable array for soft font character data */
  garr_init(&p_state->char_data, mm_pcl_pool);

  /** \todo Fill in other PCL5PrintState items */

  /* Grab any dict params used directly in the PCL5PrintState */
  if (config_params != NULL) {
    if (! get_pcl5_print_state_dict_params(pcl5_ctxt, config_params))
      goto tidyup;
  }

  /* Start the print environments list and add the shortcut to the
   * current print environment (MPE) from the PCL5PrintState.
   */
  if (! pcl5_mpe_create_defaults(pcl5_ctxt, config_params))
    goto tidyup;

  if (pcl5_ctxt->pass_through_mode == PCLXL_NO_PASS_THROUGH) {
    /* There is a PS restore then a save following every page to recover
     * PS memory, so need to get the first save done here.  (In the case
     * of an XL passthrough this will already have taken place).
     */
    if (! save_(pscontext))
      goto tidyup;

    Copy(&(p_state->save), theTop(operandstack)) ;
    pop(&operandstack) ;
  }

  return TRUE;

tidyup:
  color_state_finish(pcl5_ctxt) ;
  mm_free(mm_pcl_pool, p_state, sizeof(PCL5PrintState));
  pcl5_ctxt->print_state = NULL;
  return FALSE;
}

Bool pcl5_printstate_destroy(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *p_state;
  Bool success = TRUE ;
  ps_context_t *pscontext ;

  HQASSERT( pcl5_ctxt != NULL, "pcl5_ctxt is NULL");

  HQASSERT(pcl5_ctxt->corecontext != NULL, "No core context") ;
  pscontext = pcl5_ctxt->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;

  p_state = pcl5_ctxt->print_state;
  HQASSERT( p_state != NULL, "state pointer is NULL");

  font_sel_caches_free() ;
#ifdef FONT_CACHES_REPORT
  font_sel_caches_report() ;
#endif

  /** \todo Possibly make these flags part of the color state? */
  reset_last_pcl5_and_hpgl_colors();
  pcl5_mpe_destroy_all(pcl5_ctxt);
  color_state_finish(pcl5_ctxt) ;
  hpgl2_print_state_destroy(&p_state->hpgl2_print_state);

  /* Release any partial soft font char data not passed to PFIN */
  garr_empty(&p_state->char_data);

  if (pcl5_ctxt->pass_through_mode == PCLXL_NO_PASS_THROUGH) {
    /* There is a PS restore then a save following every page to
     * recover PS memory, so need to do the final restore here.
     * (In the case of an XL passthrough this will be handled
     * later by the XL code).
     */
    (void) gs_gexch(GST_HPGL, GST_GSAVE) ;  /* get HPGL one back if there */
    if (! push(&(p_state->save), &operandstack) || ! restore_(pscontext))
      success = FALSE ;
  }

  mm_free(mm_pcl_pool, p_state, sizeof(PCL5PrintState));
  pcl5_ctxt->print_state = NULL;

  return success ;
}

/* A collection of things that need to happen on a 'printer reset',
   i.e. this will probably be used on UEL or ESC-E (at least from
   PCL5). */
/** \todo Perhaps this should just deal with PCL5PrintState items with
 *  other PCL5Context and device items being dealt with from the calling
 *  functions.
 */
Bool pcl5_printstate_reset(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *p_state;
  PCL5PrintEnvironmentLink *p_env_head, *p_env_tail;
  JobCtrlInfo saved_job_control;
  PageCtrlInfo saved_page_control;
  PCL5Ctms saved_ctms;

  HQASSERT( pcl5_ctxt != NULL, "pcl5_ctxt is NULL");

  p_state = pcl5_ctxt->print_state;
  HQASSERT( p_state != NULL, "PCL5PrintState is NULL");
  HQASSERT( ! SLL_LIST_IS_EMPTY(&(p_state->mpe_list)),
            "Print environments list is empty ");

  /* If all macros were tidily finished there should only be one MPE above the
   * PJL Current Environment (as understood by core).  Re-initialise it from
   * the PJL Current Environment.
   */
  p_env_head = SLL_GET_HEAD(&(p_state->mpe_list), PCL5PrintEnvironmentLink, sll);
  p_env_tail = SLL_GET_TAIL(&(p_state->mpe_list), PCL5PrintEnvironmentLink, sll);

  HQASSERT( p_env_head->mpe_type == MODIFIED_PRINT_ENV, "Unexpected MPE type");
  HQASSERT( p_env_head->macro_nest_level == 0, "Unexpected macro nest level");
  HQASSERT( p_env_head->slevel == 1, "Unexpected print environment link slevel");

  HQASSERT( p_env_tail->mpe_type == PJL_CURRENT_ENV, "Unexpected MPE type");
  HQASSERT( p_env_tail->macro_nest_level == -1, "Unexpected macro nest level");
  HQASSERT( p_env_tail->slevel == 0, "Unexpected print environment link slevel");

  /* Copy the PJL Current Environment to the working MPE.
   * N.B. If we have an XL snippet passthrough, pagedevice related settings
   *      must not be reset, so need to save them here and restore after
   *      resetting the MPE.
   */
  if (pcl5_ctxt->pass_through_mode == PCLXL_SNIPPET_JOB_PASS_THROUGH) {
    saved_job_control = p_env_head->mpe.job_ctrl;
    saved_page_control = p_env_head->mpe.page_ctrl;
    saved_ctms = p_env_head->mpe.pcl5_ctms;
  }

  /* Any MPE elements containing pointers to allocated data need to free the
   * data here since we are about to overwrite the pointers.
   * N.B. Currently none of the items being restored on XL snippet passthrough
   *      contain pointers to allocated data.  If this changes, will need to
   *      review this to avoid freeing the data, then restoring the pointers.
   */
  pcl5_mpe_cleanup(&(p_env_head->mpe));

  p_env_head->mpe = p_env_tail->mpe;

  /* Copy back any saved pagecontrol and jobcontrol elements we need */
  if (pcl5_ctxt->pass_through_mode == PCLXL_SNIPPET_JOB_PASS_THROUGH) {
    preserve_mpe_values_on_reset(pcl5_ctxt,
                                 &saved_job_control,
                                 &saved_page_control,
                                 &saved_ctms);
  }

  /** \todo Easier just to default the PCL5PrintEnvironment here? */
  /* Initialise any other (non device-related) settings which are e.g. held
   * in C statics, i.e. will not be initialised as part of the MPE copy
   * above, but would be as part of default_pcl5_print_environment.
   */
  reinstate_pcl5_print_model(pcl5_ctxt) ;

  /* Initialise some context settings */
  if ( pcl5_ctxt->pcl5c_enabled) {
    pcl5_ctxt->interpreter_mode = PCL5C_MODE;
  } else {
    pcl5_ctxt->interpreter_mode = PCL5E_MODE;
  }

  /* Initialise some print state settings */
  p_state->allow_macro_overlay = FALSE;

  p_state->cursor_explicitly_set = FALSE ;
  set_default_font_state(&(p_state->font_state)) ;
  clear_cursor_position_stack(pcl5_ctxt);

  p_state->possible_raster_data_trim = FALSE ;

  color_state_reset(pcl5_ctxt) ;
  hpgl2_handle_PCL_printer_reset(pcl5_ctxt);

  /* Remove all temporary entries from the user pattern cache. */
  pcl5_id_cache_remove_all(pcl5_ctxt->resource_caches.user, FALSE) ;

  /* Delete all temporary macros */
  destroy_macros(pcl5_ctxt, 7) ;

  /* Delete all temporary fonts and symbol sets */
  do_font_control(pcl5_ctxt, FONTCTRL_DELETE_ALLTEMP);
  do_symset_control(pcl5_ctxt, SYMSETCTRL_DELETE_ALLTEMP);

  reset_aliased_fonts(&pcl5_rip_context) ;

  /* Release any partial soft font char data not passed to PFIN */
  garr_empty(&p_state->char_data);

  /* Invalidate the PS font and free the fontselection caches */
  handle_ps_font_change(pcl5_ctxt) ;
  font_sel_caches_free() ;

  reset_last_pcl5_and_hpgl_colors();
  /** \todo Possibly make the flag part of the HPGL2PrintState? */
  set_hpgl2_sync_required();

  /** \todo Find out what should happen to the other PCL5PrintState items */

  if (pcl5_ctxt->pass_through_mode != PCLXL_SNIPPET_JOB_PASS_THROUGH ) {

    p_state->current_color_mode = p_state->default_color_mode ;

    /* Make the PS calls to set the colormode, and do setpagedevice, and to
     * fill in the resulting MPE state including the new page dimensions.
     */
    if (! handle_pcl5_page_change(pcl5_ctxt, NULL, PCL5_STATE_MAINTENANCE, FALSE))
      return FALSE ;
  }

  /* Assume that SETG is required */
  p_state->setg_required = 1 ;

  return TRUE;
}

/*
 * pcl5_set_config_params() does most of the "donkey-work" behind setpcl5params_()
 * and behind the handling of the optional per-job configuration params dictionary
 * supplied to pcl5exec_()
 * and thus through to pcl5_execute_stream() and pcl5_create_context()
 *
 * pcl5_set_config_params() takes a dictionary and a pointer to
 * a PCL5ConfigParams struture which will either be a free-standing global
 * or may be a sub-structure element within a PCL5Context
 *
 * The configuration parameters, which are all optional, consist of:
 *
 *  /Copies           INTEGER             0 to 32767
 *  /BackChannel      STRING              upto ? characters
 *  /Courier          INTEGER             0 or 1 (i.e. essentially a boolean, but historically an integer)
 *  /FontNumber       INTEGER             range unknown, probably non-negative
 *  /FontSource       INTEGER or STRING   ignored as we currently only support a value of 0
 *  /Pitch            REAL,               range probably non-negative
 *  /PointSize        REAL,               0.00 to 999.75
 *  /SymbolSet        INTEGER             need to look this up
 *  /LineTermination  INTEGER
 *  /PCL5Mode         NAME
 *  /TwoByteCharacterSupport  BOOLEAN
 *  /VirtualDeviceSpace <<
 *    /DeviceGray     ONAME
 *    /DeviceRGB      ONAME
 *    /DeviceCMYK     ONAME
 *    /Other          ONAME
 *  >>
 */
/** \todo Fix /SymbolSet configuration parameters */

Bool pcl5_set_config_params(OBJECT*           odict,
                            PCL5ConfigParams* config_params)
{
  enum {
    DEFAULT_PAGE_COPIES = 0,
    BACKCHANNEL,
    COURIER_WEIGHT,
    DEFAULT_FONT_NUMBER,
    DEFAULT_FONT_SOURCE,
    DEFAULT_PITCH,
    DEFAULT_POINT_SIZE,
    DEFAULT_SYMBOL_SET,
    DEFAULT_LINE_TERMINATION,
    PCL5_MODE,
    TWO_BYTE_CHAR_SUPPORT,
    VIRTUAL_DEVICE_SPACE,
    NUM_DEFAULT_ENTRIES
  };

  static NAMETYPEMATCH match[NUM_DEFAULT_ENTRIES + 1] = {
    {NAME_Copies | OOPTIONAL, 1, {OINTEGER}},
    {NAME_BackChannel | OOPTIONAL, 1, {OSTRING}},
    {NAME_Courier | OOPTIONAL, 1, {OINTEGER}},
    {NAME_FontNumber | OOPTIONAL, 1, {OINTEGER}},
    {NAME_FontSource | OOPTIONAL, 2, {OINTEGER, OSTRING}},  /** \todo What type should this be? */
    {NAME_Pitch | OOPTIONAL, 2, {OREAL, OINTEGER}},
    {NAME_PointSize | OOPTIONAL, 2, {OREAL, OINTEGER}},
    {NAME_SymbolSet | OOPTIONAL, 1, {OINTEGER}},
    {NAME_LineTermination | OOPTIONAL, 1, {OINTEGER}},
    {NAME_PCL5Mode | OOPTIONAL, 1, {ONAME}},
    {NAME_TwoByteCharacterSupport | OOPTIONAL, 1, {OBOOLEAN}},
    {NAME_VirtualDeviceSpace | OOPTIONAL, 1, {ODICTIONARY}},
    DUMMY_END_MATCH
  };

  OBJECT* o_default_page_copies = NULL;
  OBJECT* o_backchannel = NULL;
  OBJECT* o_courier = NULL;
  OBJECT* o_default_font_number = NULL;
  OBJECT* o_default_font_source = NULL;
  OBJECT* o_default_pitch = NULL;
  OBJECT* o_default_point_size = NULL;
  OBJECT* o_default_symbol_set = NULL;
  OBJECT* o_default_line_termination = NULL;
  OBJECT* o_pcl5_mode = NULL;
  OBJECT* o_two_byte_char_support = NULL;
  OBJECT* o_vds = NULL;

  HQASSERT(odict, "Cannot extract pcl5 config params from a NULL dictionary");
  HQASSERT(config_params, "Cannot extract pcl5 config params into a NULL config params struct");

  /*
   * The caller will possibly already have checked that odict
   * is indeed a dictionary, but as we aren't called very often
   * we'll repeat the check here just to be sure
   */

  if ( oType(*odict) != ODICTIONARY ) {
    return error_handler(TYPECHECK);
  }

  /*
   * Check that the dictionary is readable
   */

  if ( !oCanRead(*oDict(*odict)) &&
       !object_access_override(oDict(*odict)) ) {
    return error_handler(INVALIDACCESS);
  }

  /*
   * Match the dictionary against our "match template"
   * Note that I don't think we care about any "excess"/unknown entries?
   */

  if ( !dictmatch(odict, match) ) {
    return FALSE; /* dictmatch() will already have raised any TYPECHECK errors */
  }

  /*
   * Ok, we have a dictionary
   * and we have "matched" the dictionary entries
   * against our match table.
   *
   * So we can process each entry if present
   */

  if ( (o_default_page_copies = match[DEFAULT_PAGE_COPIES].result) != NULL ) {
    /*
     * Yes, we have a /Copies dictionary key
     * But is it an integer?
     * And if so, is it within the allowed range?
     */

    int32 default_page_copies = oInteger(*o_default_page_copies);

    if ( (default_page_copies >= 0) &&
         (default_page_copies <= 32767) ) {
      config_params->default_page_copies = default_page_copies;
    }
    else {
      return error_handler(RANGECHECK);
    }
  }

  if ( ((o_backchannel = match[BACKCHANNEL].result) != NULL) ) {
    /*
     * Yes, we have found the /BackChannel dictionary key
     * But is it a string?
     * And if so, is it less than or equal to the length we have allocated for it?
     */

    uint32 backchannel_len = theLen(*o_backchannel);

    if ( backchannel_len > sizeof(config_params->backchannel) ) {
      return error_handler(RANGECHECK);
    }
    else {
      /*
       * We must copy the backchannel name
       * allowing for 1 or 2-byte character representations
       * and significant embedded Nul bytes
       */

      HqMemCpy(config_params->backchannel,
               oString(*o_backchannel),
               backchannel_len);

      config_params->backchannel_len = backchannel_len;

      /*
       * IFF there is still room in our array beyond
       * the backchannel string, then we will choose
       * to zero-fill the additional bytes
       * because we could then attempt to use the backchannel name
       * as a C-string (and thus view it in a debugger as such
       */

      if ( backchannel_len < (sizeof(config_params->backchannel)) )
        HqMemZero(&config_params->backchannel[backchannel_len],
                  (sizeof(config_params->backchannel) - backchannel_len));
    }
  }

  if ( (o_courier = match[COURIER_WEIGHT].result) != NULL ) {
    /*
     * Yes, we have a /Courier dictionary key
     * But is it an integer?
     * And if so is it either 0 or 1 (the only allowed values)?
     */

    int32 dark_courier = oInteger(*o_courier);

    if ( (dark_courier == 0) ||
         (dark_courier == 1) ) {
      config_params->dark_courier = dark_courier;
    }
    else {
      return error_handler(RANGECHECK);
    }
  }

  if ( (o_default_font_number = match[DEFAULT_FONT_NUMBER].result) != NULL ) {
    /*
     * We have a /FontNumber dictionary key
     * But is it an integer?
     * And if so is it in range?
     */

    int32 default_font_number = oInteger(*o_default_font_number);

    if ( (default_font_number >= 0) &&
         (default_font_number <= 65535) ) {
      config_params->default_font_number = default_font_number;
    }
    else {
      return error_handler(RANGECHECK);
    }
  }

  if ( (o_default_font_source = match[DEFAULT_FONT_SOURCE].result) != NULL ) {
    /*
     * We have a /FontSource dictionary key
     * But is it an integer?
     * And if so is it in range?
     */

    int32 default_font_source = 0;

    if ( (oType(*o_default_font_source) == OINTEGER) &&
         (((default_font_source = oInteger(*o_default_font_source)) < 0) ||
          (default_font_source > 65535)) ) {
      return error_handler(RANGECHECK);
    }
    else {
      /**
       * \todo Okay, despite having found and validated the Font "Source"
       * we are going to completely ignore it
       * because we currently have no idea about how to implement
       * a font "source" other than 0
       */

      config_params->default_font_source = 0;
    }
  }

  if ( (o_default_pitch = match[DEFAULT_PITCH].result) != NULL ) {
    /*
     * We have found the /Pitch dictionary key
     * But it is a real (or integer?) value
     */

    PCL5Real default_pitch = 0.0;

    if ( oType(*o_default_pitch) == OREAL )
    {
      default_pitch = oReal(*o_default_pitch);
    }
    else
    {
      default_pitch = oInteger(*o_default_pitch);
    }

    if ( (default_pitch >= 0.0) &&
         (default_pitch <= 65535.0) ) {
      config_params->default_pitch = default_pitch;
    }
    else {
      return error_handler(RANGECHECK);
    }
  }

  if ( (o_default_point_size = match[DEFAULT_POINT_SIZE].result) != NULL ) {
    /*
     * We have found the /PointSize dictionary key
     * But it is a real (or integer?) value
     */

    PCL5Real default_point_size = 0.0;

    if ( oType(*o_default_point_size) == OREAL ) {
      default_point_size = oReal(*o_default_point_size);
    }
    else {
      default_point_size = oInteger(*o_default_point_size);
    }

    if ( (default_point_size >= 0.0) &&
         (default_point_size <= 65535.0) ) {
      config_params->default_point_size = default_point_size;
    }
    else {
      return error_handler(RANGECHECK);
    }
  }

  if ( (o_default_symbol_set = match[DEFAULT_SYMBOL_SET].result) != NULL ) {
    /*
     * We have found the /SymbolSet dictionary key
     * But is it a non-negative integer?
     * (and is it less than or equal to 32767?)
     */

    int32 default_symbol_set = oInteger(*o_default_symbol_set);

    if ( (default_symbol_set >= 0) &&
         (default_symbol_set <= 65535) ) {
      config_params->default_symbol_set = default_symbol_set;
    }
    else {
      return error_handler(RANGECHECK);
    }
  }

  if ( (o_default_line_termination = match[DEFAULT_LINE_TERMINATION].result) != NULL ) {
    /*
     * Yes we have found the /LineTermination dictionary key
     * But is it an integer?
     * And if so is it within range?
     */

    int32 default_line_termination = oInteger(*o_default_line_termination);

    if ( (default_line_termination >= 0) &&
         (default_line_termination <= 65535) ) {
      config_params->default_line_termination = default_line_termination;
    }
    else {
      return error_handler(RANGECHECK);
    }
  }

  if ( (o_pcl5_mode = match[PCL5_MODE].result) != NULL ) {
    /*
     * We have found the /PCL5Mode dictionary key
     * Its value should be one of PCL5c or PCL5e
     */

    int32 pcl5_mode = 0;

    if ( ((pcl5_mode = oNameNumber(*o_pcl5_mode)) != NAME_PCL5e) &&
         (pcl5_mode != NAME_PCL5c) ) {
      return error_handler(RANGECHECK);
    }
    else if ( config_params == &pcl5_config_params ) {
      /*
       * If we are called from setpcl5params_()
       * i.e. our local "config_params" pointer points at the global "pcl5_config_params")
       * then we presume that this is because this is part
       * of Rip configuration/set-up and is therefore
       * "allowed" to set the global pcl5_config_params PCL5 mode
       */

      config_params->pcl5c_enabled = (pcl5_mode == NAME_PCL5c);
    }
    else {
      /*
       * If we have been called from/via pcl5exec_()
       * then we are only allowed to force PCL5e (mode)
       * from a PCL5c-enabled Rip (i.e. to demote the Rip's capabilities
       *
       * We are *not* allowed to promote a PCL5e-only Rip
       * into being a PCL5c-capable Rip.
       */

      config_params->pcl5c_enabled = config_params->pcl5c_enabled &&
                                     (pcl5_mode == NAME_PCL5c);
    }
  }

  if ( (o_two_byte_char_support = match[TWO_BYTE_CHAR_SUPPORT].result) != NULL) {
    /*
     * We have found the /TwoByteCharacterSupport dictionary key
     * which is supposed to be a Boolean value
     */

    config_params->two_byte_char_support_enabled = oBool(*o_two_byte_char_support);
  }

  if ( (o_vds = match[VIRTUAL_DEVICE_SPACE].result) != NULL) {
    if ( !pcl_param_vds_select(o_vds, &config_params->vds_select) )
      return FALSE ;
  }

  return TRUE;
}

Bool pcl_param_vds_select(OBJECT *odict, int32 *value)
{
  int32 newvalue ;

  enum {
    match_Gray,
    match_CMYK,
    match_RGB,
    match_Other,
    NUM_DEFAULT_ENTRIES
  };

  static NAMETYPEMATCH match[NUM_DEFAULT_ENTRIES + 1] = {
    {NAME_DeviceGray | OOPTIONAL, 1, {ONAME}},
    {NAME_DeviceCMYK | OOPTIONAL, 1, {ONAME}},
    {NAME_DeviceRGB | OOPTIONAL, 1, {ONAME}},
    {NAME_Other | OOPTIONAL, 1, {ONAME}},
    DUMMY_END_MATCH
  };

  HQASSERT(odict && oType(*odict) == ODICTIONARY,
           "No dictionary for PCL virtual device space selection");

  if ( !oCanRead(*oDict(*odict)) &&
       !object_access_override(oDict(*odict)) )
    return error_handler(INVALIDACCESS);

  if ( !dictmatch(odict, match) )
    return FALSE;

#define UPDATE_VDS(value_, index_, shift_) MACRO_START                  \
  OBJECT *_obj_ ;                                                       \
  if ( (_obj_ = match[index_].result) != NULL ) {                       \
    /* Get rid of previous value, then add new value. */                \
    (value_) &= ~(PCL_VDS_MASK << (shift_)) ;                           \
    switch ( oNameNumber(*_obj_) ) {                                    \
    case NAME_DeviceGray:                                               \
      (value_) |= PCL_VDS_GRAY_F_T << (shift_) ;                        \
      break ;                                                           \
    case NAME_DeviceRGB:                                                \
      (value_) |= PCL_VDS_RGB_T_T << (shift_) ;                         \
      break ;                                                           \
    case NAME_OverrideRGB:                                              \
      (value_) |= PCL_VDS_RGB_T_F << (shift_) ;                         \
      break ;                                                           \
    case NAME_DeviceCMYK:                                               \
      (value_) |= PCL_VDS_CMYK_F_T << (shift_) ;                        \
      break ;                                                           \
    case NAME_Default:                                                  \
      (value_) |= PCL_VDS_INIT & (PCL_VDS_MASK << (shift_)) ;           \
      break ;                                                           \
    default:                                                            \
      return error_handler(RANGECHECK) ;                                \
    }                                                                   \
  }                                                                     \
MACRO_END

  newvalue = *value ;

  UPDATE_VDS(newvalue, match_Gray, PCL_VDS_GRAY_SHIFT) ;
  UPDATE_VDS(newvalue, match_RGB, PCL_VDS_RGB_SHIFT) ;
  UPDATE_VDS(newvalue, match_CMYK, PCL_VDS_CMYK_SHIFT) ;
  UPDATE_VDS(newvalue, match_Other, PCL_VDS_OTHER_SHIFT) ;

  *value = newvalue ;

  return TRUE ;
}

PCL5ConfigParams pcl5_config_params = { 0 };

/*
 * pcl5_setpcl5params_call_count counts the number of times
 * that setpcl5params_() has been called since last Rip (re-)boot
 *
 * Currently this count is used to prevent setpcl5params_()
 * actually performing any actions on any call except the first call
 */

uint32 pcl5_setpcl5params_call_count = 0;

void init_C_globals_pcl5state(void)
{
  PCL5ConfigParams config_param_defaults = {
    1,            /* /Copies          a.k.a. default_page_copies */
    "", 0,        /* /BackChannel     a.k.a. backchannel */
    0,            /* /Courier         a.k.a. dark_courier */
    0,            /* /FontNumber      a.k.a. default_font_number */
    0,            /* /FontSource      a.k.a. default_font_source */
    10,           /* /Pitch           a.k.a. default_pitch */
    12,           /* /PointSize       a.k.a. default_point_size */
    PC_8,         /* /SymbolSet       a.k.a. default_symbol_set */
    1,            /* /LineTermination a.k.a. default_line_termination */
    TRUE,         /* /PCL5Mode        gets converted into pcl5c_enabled Bool */
    TRUE,         /* /TwoByteCharacterSupport a.k.a. two_byte_char_support_enabled */
    PCL_VDS_INIT, /* /VirtualDeviceSpace selection */
  };

  pcl5_config_params = config_param_defaults;

  pcl5_setpcl5params_call_count = 0;
}

/* ============================================================================
 * PS operators.
 * ============================================================================
 */

Bool setpcl5params_(ps_context_t* pscontext)
{
  OBJECT* odict;

  UNUSED_PARAM(ps_context_t*, pscontext);

  if ( isEmpty(operandstack) ) {
    return error_handler(STACKUNDERFLOW) ;
  }

  if ( oType(*(odict = theTop(operandstack))) != ODICTIONARY ) {
    return error_handler(TYPECHECK) ;
  }

  if ( pcl5_setpcl5params_call_count++ ||
       pcl5_set_config_params(odict, &pcl5_config_params) )
  {
    pop(&operandstack);

    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

/* ============================================================================
* Log stripped */
