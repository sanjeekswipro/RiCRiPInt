/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:jobcontrol.c(EBDSDK_P.1) $
 * $Id: src:jobcontrol.c,v 1.66.1.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the PCL5 "Job Control" category.
 *
 * Universal Exit Language           ESC % - 1 2 3 4 5 X
 * Configuration (I/O)               ESC & b # W[data]
 * Printer Reset                     ESC E
 * Number of Copies                  ESC & l # X
 * Simplex/Duplex                    ESC & l # S
 * Long-edge Offset Registration     ESC & l # U
 * Short-edge Offset Registration    ESC & l # Z
 * Duplex Page Side Selection        ESC & a # G
 * Job Separation                    ESC & l 1 T
 * Output Bin (Media Bin)            ESC & l # G
 * Unit-of-Measure                   ESC & u # D
 */

#include "core.h"
#include "jobcontrol.h"

#include "macros.h"
#include "pcl5context_private.h"
#include "printenvironment_private.h"
#include "pagecontrol.h"
#include "pcl5scan.h"
#include "cursorpos.h"
#include "pcl5fonts.h"
#include "hpgl2config.h"
#include "pcl5ctm.h"
#include "pclutils.h"

#include "dicthash.h"
#include "fileio.h"
#include "miscops.h"
#include "monitor.h"
#include "namedef_.h"
#include "params.h"
#include "swcopyf.h"

JobCtrlInfo* get_job_ctrl_info(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *print_state ;
  PCL5PrintEnvironment *mpe ;
  JobCtrlInfo *job_ctrl_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  mpe = print_state->mpe ;
  HQASSERT(mpe != NULL, "mpe is NULL") ;

  job_ctrl_info = &(mpe->job_ctrl) ;

  return job_ctrl_info ;
}

/* See header for doc. */
void default_job_control(JobCtrlInfo* self, PCL5ConfigParams* config_params)
{
  HQASSERT(self, "NULL Job Control Info");
  HQASSERT(config_params, "NULL config_params");

  self->num_copies = config_params->default_page_copies;
  self->duplex = FALSE;
  self->binding = LONG_EDGE;
  self->job_separation = FALSE;
  self->left_registration = 0;
  self->top_registration = 0;
  self->output_bin = 1;
  self->requested_duplex = FALSE;
  self->requested_binding = LONG_EDGE;
}


/* Unpack the optional parameters dict, overwriting relevant jobcontrol
 * settings to complete the PJL current environment (as understood by
 * core).
 */
Bool jobcontrol_apply_pjl_changes(PCL5Context *pcl5_ctxt, PCL5ConfigParams* config_params)
{
  PCL5Numeric num_copies ;

  HQASSERT( pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT( config_params != NULL, "config_params is NULL") ;

  /* Do the deed(s) ... */
  num_copies.integer = config_params->default_page_copies;

  return pcl5op_ampersand_l_X(pcl5_ctxt, FALSE, num_copies);
}


void save_job_control(PCL5Context *pcl5_ctxt,
                      JobCtrlInfo *to,
                      JobCtrlInfo *from,
                      Bool overlay)
{
  HQASSERT(to != NULL, "JobCtrlInfo is NULL") ;

  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;
  UNUSED_PARAM(JobCtrlInfo*, from) ;

  if (overlay) {
    to->num_copies = 1 ;

#if 0
    /** \todo Suspect all the jobcontrol quantities may need to be
     *  copied, and also it seems likely (or at least possible) that
     *  most or all of the jobcontrol commands should be ignored if
     *  doing an overlay macro.
     *
     * We know that num_copies is not copied across. See PCL 5e CET
     * 24_07 Page 27. PCL 5c CET 25-06 Page 29.
     */
    *to = *from ;
#endif
  }
}

void restore_job_control(PCL5Context *pcl5_ctxt,
                         JobCtrlInfo *to,
                         JobCtrlInfo *from)
{
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;
  UNUSED_PARAM(JobCtrlInfo*, to) ;
  UNUSED_PARAM(JobCtrlInfo*, from) ;
}

/* ========================================================================= */
/* Set the required number of copies into userdict */
Bool set_ps_num_copies(PCL5Context *pcl5_ctxt)
{
  JobCtrlInfo *job_info ;
  OBJECT value = OBJECT_NOTVM_NOTHING ;
  OBJECT nameobj = OBJECT_NOTVM_NOTHING ;

  HQASSERT( pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  job_info = get_job_ctrl_info(pcl5_ctxt) ;
  HQASSERT( job_info != NULL, "job_info is NULL") ;

  object_store_name(&nameobj, NAME_copies, LITERAL) ;
  object_store_integer(&value, (int32) job_info->num_copies) ;
  if (! insert_hash(&userdict, &nameobj, &value))
    return pcl_error_return(pcl5_ctxt->corecontext->error);

  return TRUE ;
}

/* ============================================================================
 * Operator callbacks are below here.
 * ============================================================================
 */

/* ESC % X (Likely to be a UEL Command) */
Bool pcl5op_percent_X(PCL5Context *pcl5_ctxt,
                      int32 explicit_sign, PCL5Numeric value)
{
  Bool success = TRUE ;

  UNUSED_PARAM(int32, explicit_sign) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* When executing a macro, we ignore any UEL seen. */
  if (pcl5_macro_nest_level > 0)
    return TRUE ;

  if (value.real != -12345.0f)
    return TRUE ;

#if defined(DEBUG_BUILD)
  if ( debug_pcl5 & PCL5_CONTROL ) {
    monitorf((uint8*)"---- reset (via UEL)\n") ;
  }
#endif /* DEBUG_BUILD */
  /** \todo what happens if in HPGL2 ? Can this occur? */
  success = pcl5op_E(pcl5_ctxt, explicit_sign, value) ;

  pcl5_ctxt->UEL_seen = TRUE ;
  return success ;
}

/* AppleTalk configuration */
Bool pcl5op_ampersand_b_W(
  PCL5Context*  pcl5_ctxt,
  int32         explicit_sign,
  PCL5Numeric   value)
{
  uint32 numbytes;

  UNUSED_PARAM(int32, explicit_sign) ;

  numbytes = min(value.integer, 32767);

  if ( numbytes > 0 ) {
    return(file_skip(pcl5_ctxt->flptr, numbytes, NULL) > 0);
  }

  return(TRUE);
}

/* ESC-E */
/* Ignore explicit_sign and value as they will NOT be set for two
   character escape sequences. */
Bool pcl5op_E(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PCL5PrintState *print_state ;
  JobCtrlInfo *job_ctrl ;
  Bool success = TRUE ;

  UNUSED_PARAM(int32, explicit_sign) ;
  UNUSED_PARAM(PCL5Numeric, value) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  job_ctrl = get_job_ctrl_info(pcl5_ctxt) ;
  HQASSERT(job_ctrl != NULL, "job_ctrl is NULL") ;

#if defined(DEBUG_BUILD)
  if ( debug_pcl5 & PCL5_CONTROL ) {
    monitorf((uint8*)"---- reset \n") ;
  }
#endif
#if 0
  /* Throw up to 2 pages depending on simplex/duplex mode */
  /** \todo It seem likely some of this behaviour may map onto PS duplex
   * behaviour, so maybe can or should deal with some of it through that.
   */
  if (!job_ctrl->duplex)
    success = throw_page(pcl5_ctxt, FALSE, FALSE) ;
  else if ((print_state->duplex_page_number % 2) == 1) {
    success = throw_page(pcl5_ctxt, FALSE, FALSE) ;

    if ((print_state->duplex_page_number % 2) == 0) {
      success = success && reset_page(pcl5_ctxt) ;
      success = success && throw_page(pcl5_ctxt, FALSE, TRUE) ;
    }
  }
  else {
    success = throw_page(pcl5_ctxt, FALSE, TRUE) ;
  }
#endif
  /* this code replaces the above as the SDK RasterCallback handles the duplex page throw */
  success = throw_page(pcl5_ctxt, FALSE, FALSE) ;

  success = success && pcl5_printstate_reset(pcl5_ctxt) ;
  success = success && reset_page(pcl5_ctxt) ;

  /** \todo Can this happen when actually in HPGL2 ? */
  /** \todo Should this be done in snippet mode? */
  hpgl2_handle_PCL_printer_reset(pcl5_ctxt);

  return success ;
}


/* Copies */
Bool pcl5op_ampersand_l_X(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  JobCtrlInfo *job_ctrl ;
  uint32 num_copies = (uint32) abs(value.integer) ;  /* N.B. negative values treated as positive */

  UNUSED_PARAM(int32, explicit_sign) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* N.B. It would do no harm to fill this in for XL snippet mode, but
   * the value will ultimately be ignored when XL does the actual page
   * throw.  We do need to fill it in for whole job passthrough as it
   * will be honoured except where XL does the end page.
   */

  if (num_copies == 0 ||
      pcl5_ctxt->pass_through_mode == PCLXL_SNIPPET_JOB_PASS_THROUGH ||
      pcl5_current_macro_mode == PCL5_EXECUTING_OVERLAY_MACRO)
    return TRUE ;

  if (num_copies > 32767)
    num_copies = 32767 ;

  job_ctrl = get_job_ctrl_info(pcl5_ctxt) ;
  HQASSERT(job_ctrl != NULL, "job_ctrl is NULL") ;

  job_ctrl->num_copies = num_copies ;

  return TRUE ;
}


/* Simplex/duplex (and binding) */
/** \todo N.B. The page throw even seems to take place if the
   * parameter value value is invalid, (e.g. value 6 or -6).  Should
   * probably check if this is a command where all values are accepted
   * and mapped to one of those below.
   */
Bool pcl5op_ampersand_l_S(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  uint8 buffer[64];
  uint8 *spd = buffer;
  PCL5PrintState *print_state ;
  JobCtrlInfo *job_ctrl ;

  UNUSED_PARAM(int32, explicit_sign) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  if (pcl5_ctxt->pass_through_mode == PCLXL_SNIPPET_JOB_PASS_THROUGH)
    return TRUE ;

  job_ctrl = get_job_ctrl_info(pcl5_ctxt) ;
  HQASSERT(job_ctrl != NULL, "job_ctrl is NULL") ;

  /* Go to next 'front' side of page (if necessary) N.B. Or stay on
   * the current 'front' side if that's where we are already, and
   * there is nothing in the printbuffer.  (Notice that this all
   * depends on the current duplex mode, not the requested one).
   */
  if (!job_ctrl->duplex) {
    /* Do a conditional page throw, (even for a printer which is only
     * simplex anyway), and even if the request is also for simplex.
     */
    if (! throw_page(pcl5_ctxt, FALSE, FALSE))
      return FALSE ;
  }
  else {
    print_state = pcl5_ctxt->print_state ;
    HQASSERT(print_state != NULL, "print_state is NULL") ;

    /* N.B. This even seems to be true going from long edge to long
     * edge duplex, so assume the same for short edge to short edge.
     */
    if ((print_state->duplex_page_number % 2) == 1) {
      if (! throw_page(pcl5_ctxt, FALSE, FALSE))
        return FALSE ;

      if ((print_state->duplex_page_number % 2) == 0)
        if (! throw_page(pcl5_ctxt, FALSE, TRUE))
          return FALSE ;
    }
    else {
       /* Do an unconditional page throw */
      if (! throw_page(pcl5_ctxt, FALSE, TRUE))
        return FALSE ;
    }
  }

  /* Interpret the requested settings only updating if they have changed */
  switch (value.integer) {
  case 0:
    if (job_ctrl->requested_duplex ||
        job_ctrl->requested_binding != SIMPLEX) {
      job_ctrl->requested_duplex = FALSE ;
      job_ctrl->requested_binding = SIMPLEX ;
    } else {
      spd = NULL;
    }
    break ;

  case 1:
    if (!job_ctrl->requested_duplex ||
        job_ctrl->requested_binding != LONG_EDGE) {
      job_ctrl->requested_duplex = TRUE ;
      job_ctrl->requested_binding = LONG_EDGE ;
    } else {
      spd = NULL;
    }
    break ;

  case 2:
    if (!job_ctrl->requested_duplex ||
        job_ctrl->requested_binding != SHORT_EDGE) {
      job_ctrl->requested_duplex = TRUE ;
      job_ctrl->requested_binding = SHORT_EDGE ;
    } else {
      spd = NULL;
    }
    break ;

  default:
    return TRUE ;
  }

  if (spd) {
    /* This is for the setpagedevice call to request the new duplex settings.
     * N.B. This assumes Tumble is ignored for simplex.
     */
    swncopyf(buffer, sizeof(buffer),
             (uint8*)"<</Duplex %s/Tumble %s>>setpagedevice",
             (job_ctrl->requested_duplex ? "true" : "false"),
             ((job_ctrl->requested_binding == SHORT_EDGE) ? "true" : "false"));
  }

  /* Make the PS setpagedevice call and fill in the resulting actual duplex
   * and binding and other MPE state from the pagedevice.
   */
  return (handle_pcl5_page_change(pcl5_ctxt, spd, PCL5_STATE_MAINTENANCE,
                                  FALSE) &&
          reset_page(pcl5_ctxt));
}


/* Left Offset (Long Edge) Registration */
Bool pcl5op_ampersand_l_U(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  JobCtrlInfo *job_ctrl = get_job_ctrl_info(pcl5_ctxt);

  UNUSED_PARAM(int32, explicit_sign) ;

  HQASSERT(job_ctrl != NULL, "curr_job_ctrl is NULL") ;

  /* Ignore completely out of range value */
  /** \todo Or maybe limit to this */
  if (value.real < -32767 || value.real > 32767)
    return TRUE ;

  value.real = pcl5_round_to_2d(value.real) ;

  /* Convert to PCL Internal Units */
  job_ctrl->left_registration = 10 * value.real ;

  /* Apply the changes to the CTMs
   * N.B. No cursor transformations appear to take place.
   */
  ctm_recalculate(pcl5_ctxt);

  return TRUE ;
}


/* Top Offset (Short Edge) Registration */
Bool pcl5op_ampersand_l_Z(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  JobCtrlInfo *job_ctrl ;

  UNUSED_PARAM(int32, explicit_sign) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  job_ctrl = get_job_ctrl_info(pcl5_ctxt) ;
  HQASSERT(job_ctrl != NULL, "job_ctrl is NULL") ;

  /* Ignore completely out of range value */
  /** \todo Or maybe limit to this */
  if (value.real < -32767 || value.real > 32767)
    return TRUE ;

  value.real = pcl5_round_to_2d(value.real) ;

  /* Convert to PCL Internal Units */
  job_ctrl->top_registration = 10 * value.real ;

  /* Apply the changes to the CTMs
   * N.B. No cursor transformations appear to take place.
   */
  ctm_recalculate(pcl5_ctxt);

  return TRUE ;
}


/* Duplex page side selection */
Bool pcl5op_ampersand_a_G(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PCL5PrintState *print_state ;
  JobCtrlInfo *job_ctrl ;
  Bool currently_on_front ;
  uint32 duplex_page_number ;
  PCL5Integer side = value.integer ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  job_ctrl = get_job_ctrl_info(pcl5_ctxt) ;
  HQASSERT(job_ctrl != NULL, "job_ctrl is NULL") ;

  if ((side < 0) || (side > 2))
    return TRUE ;

  /* The general rule here is that if we are already on the right
   * side, (or we have simply been asked for the next side), and there
   * is nothing to print do not do a page throw, else need to get to
   * the right side.
   */

  currently_on_front = (print_state->duplex_page_number % 2) ;

  /* If not doing (or can't do) duplex, or have been asked for next side,
   * do a conditional page throw.
   */
  if (!job_ctrl->duplex || side == 0)
    return(throw_page(pcl5_ctxt, TRUE, FALSE)) ;

  /* If currently on the right side, do a conditional page throw.  If
   * that increments the page number follow with an unconditional page
   * throw.
   */
  if ((currently_on_front && side == 1) ||
      (!currently_on_front && side == 2)) {
    duplex_page_number = print_state->duplex_page_number ;
    if (! throw_page(pcl5_ctxt, TRUE, FALSE))
      return FALSE ;

    if (print_state->duplex_page_number > duplex_page_number) {
      if (! throw_page(pcl5_ctxt, TRUE, TRUE))
        return FALSE ;
    }
  }
  else {
    /* Do an unconditional page throw, but do set default cursor
       position, etc */
    if (! throw_page(pcl5_ctxt, FALSE, TRUE) ||
        ! reset_page(pcl5_ctxt))
      return FALSE ;
  }

  return TRUE ;
}


Bool pcl5op_ampersand_l_T(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  FILELIST *flptr ;

  UNUSED_PARAM(int32, explicit_sign) ;
  UNUSED_PARAM(PCL5Numeric, value) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  flptr = pcl5_ctxt->flptr ;
  HQASSERT(flptr != NULL, "flptr is NULL") ;

  return TRUE ;
}

Bool pcl5op_ampersand_l_G(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  FILELIST *flptr ;

  UNUSED_PARAM(int32, explicit_sign) ;
  UNUSED_PARAM(PCL5Numeric, value) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  flptr = pcl5_ctxt->flptr ;
  HQASSERT(flptr != NULL, "flptr is NULL") ;

  return TRUE ;
}

/* Unit of Measure */
Bool pcl5op_ampersand_u_D(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
#define NUM_VALID_VALUES 26
  PageCtrlInfo* page = get_page_ctrl_info(pcl5_ctxt) ;
  uint32 i, units_of_measure ;
  uint32 valid_values[NUM_VALID_VALUES] = {96,100,120,144,150,160,180,200,225,
                                           240,288,300,360,400,450,480,600,720,
                                           800,900,1200,1440,1800,2400,3600,
                                           7200} ;

  UNUSED_PARAM(int32, explicit_sign) ;

  for (i=0; i<NUM_VALID_VALUES; i++) {
    if (value.real <= valid_values[i])
      break ;
  }

  if (i==0) {
    units_of_measure = valid_values[0] ;
  }
  else if (i==NUM_VALID_VALUES) {
    units_of_measure = valid_values[NUM_VALID_VALUES - 1] ;
  }
  else {
    if ((value.real - valid_values[i-1]) / (float) valid_values[i-1] <=
        (valid_values[i] - value.real) / (float) valid_values[i])
      units_of_measure = valid_values[i-1] ;
    else
      units_of_measure = valid_values[i] ;
  }

  /* Calculate the PCL Unit Size. This only depends on UOM so should
   * be done as soon as possible and before anything that might use
   * it.  N.B. 7200 is divisible by all the valid values for UOM.
   */
  set_pcl_unit_size(page, 7200 / units_of_measure) ;

  /* Recalculate HMI */
  /* N.B. This overwrites any directly set HMI value, (even if the UOM
   * didn't change).
   */
  set_hmi_from_font(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), -1) ;

  return TRUE ;

#undef NUM_VALID_VALUES
}

/* ============================================================================
* Log stripped */
