/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlsession.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of PCLXL tag handling functions for
 * BeginSession and EndSession operators
 */

#include "core.h"
#include "pcl.h"              /* pcl5_destroy_passthrough_context */

#include "pclxltypes.h"
#include "pclxldebug.h"
#include "pclxlcontext.h"
#include "pclxlerrors.h"
#include "pclxloperators.h"
#include "pclxlattributes.h"
#include "pclxlgraphicsstate.h"
#include "pclxlpsinterface.h"
#include "pclxlpattern.h"
#include "pclxlpage.h"
#include "pclxlfont.h"
#include "pclxlpassthrough.h"

/*
 * Tag 0x41 BeginSession
 *
 * The BeginSession operator accepts attributes "Measure" and "UnitsPerMeasure"
 * and optionally an "ErrorReport" attribute which is defaulted to eNoReporting
 *
 * These attribute values are stored in the PCLXL context
 */

Bool
pclxl_op_begin_session(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[4] = {
#define SESSION_MEASURE           (0)
    {PCLXL_AT_Measure | PCLXL_ATTR_REQUIRED},
#define SESSION_UNITS_PER_MEASURE (1)
    {PCLXL_AT_UnitsPerMeasure | PCLXL_ATTR_REQUIRED},
#define SESSION_ERROR_REPORT      (2)
    {PCLXL_AT_ErrorReport},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION measure_values[] = {
    PCLXL_eInch,
    PCLXL_eMillimeter,
    PCLXL_eTenthsOfAMillimeter,
    PCLXL_ENUMERATION_END
  };
  static PCLXL_ENUMERATION error_reporting_values[] = {
    PCLXL_eNoReporting,
    PCLXL_eBackChannel,
    PCLXL_eErrorPage,
    PCLXL_eBackChAndErrPage,
    PCLXL_eNWBackChannel,
    PCLXL_eNWErrorPage,
    PCLXL_eNWBackChAndErrPage,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;

#ifdef PCLXL_RESET_BETWEEEN_SESSIONS_IN_SAME_JOB

  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;

  HQASSERT((graphics_state != NULL), "There must be at least 1 graphics state beneath the PCLXL \"context\" when handling \"BeginSession\" operator");
  HQASSERT((graphics_state->parent_graphics_state == NULL), "There must only be 1 graphics state beneath the PCLXL \"context\" when handling a \"BeginSession\" operator");

  /*
   * We will already have initialized the (graphics) state
   * IFF this is the *first* session within the job
   * and our current understanding is that a subsequent session
   * within the same job, should continue with/follow on from the same state
   *
   * So this code is #ifdef'd out
   */

  if ( (!pclxl_init_non_gs_state(pclxl_context, non_gs_state)) ||
       (!pclxl_set_default_graphics_state(pclxl_context, graphics_state)) )
  {
    return FALSE;
  }

#endif

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* Measure */
  if ( !pclxl_attr_match_enumeration(match[SESSION_MEASURE].result, measure_values,
                                     &non_gs_state->measurement_unit,
                                     pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* UnitsPerMeasure */
  if ( match[SESSION_UNITS_PER_MEASURE].result->data_type == PCLXL_DT_UInt16_XY ) {
    non_gs_state->units_per_measure.res_x = match[SESSION_UNITS_PER_MEASURE].result->value.v_uint16s[0];
    non_gs_state->units_per_measure.res_y = match[SESSION_UNITS_PER_MEASURE].result->value.v_uint16s[1];
  } else { /* Real32 */
    non_gs_state->units_per_measure.res_x = match[SESSION_UNITS_PER_MEASURE].result->value.v_real32s[0];
    non_gs_state->units_per_measure.res_y = match[SESSION_UNITS_PER_MEASURE].result->value.v_real32s[1];
  }
#define MAX_UNITS_PER_MEASURE (65535)
  if ( (non_gs_state->units_per_measure.res_x <= 0) || (non_gs_state->units_per_measure.res_x > MAX_UNITS_PER_MEASURE) ||
       (non_gs_state->units_per_measure.res_y <= 0) || (non_gs_state->units_per_measure.res_x > MAX_UNITS_PER_MEASURE) ) {
    PCLXL_ERROR_HANDLER(parser_context->pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                        ("Illegal Attribute Value"));
    return(FALSE);
  }

  /* ErrorReport */
  pclxl_context->error_reporting = PCLXL_eNoReporting;
  if ( match[SESSION_ERROR_REPORT].result ) {
    if ( !pclxl_attr_match_enumeration(match[SESSION_ERROR_REPORT].result, error_reporting_values,
                                       &pclxl_context->error_reporting, pclxl_context, PCLXL_SS_KERNEL) ) {
      return(FALSE);
    }
  }

  /*
   * Yes, we have found (or defaulted) all three parameters
   * associated with a BeginSession operator
   * And that is all we need to do.
   */
  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
              ("BeginSession(Measure = %d, UnitsPerMeasure = [%f,%f], ErrorReport = %d)",
               non_gs_state->measurement_unit,
               non_gs_state->units_per_measure.res_x,
               non_gs_state->units_per_measure.res_y,
               pclxl_context->error_reporting));

  /*
   * Since a single print job (and thus single call to pclxlexec_()
   * Could contain more than one PCLXL "Session"
   * We must be careful to ensure that each session does not
   * impact on the next session.
   */
  return(pclxl_push_gs(pclxl_context, PCLXL_PS_SAVE_RESTORE));
}

/*
 * pclxl_release_resources() performs almost all the work required
 * to implement the EndSession operator.
 *
 * But it is also intended to be called when a PCLXL job is aborted
 * for whatever reason, mid-job. This results in the rest of the PCLXL job
 * being "flushed" upto and including the next UEL.
 *
 * This results in any/all EndPage, RemoveFont, CloseDataSource and EndSession operators
 * are skipped and so *this* function performs all the resource clear-up
 * that would have been performed by the handling of these individual operators
 */

Bool
pclxl_release_resources(PCLXL_CONTEXT pclxl_context,
                        Bool          include_per_page_resources)
{
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_PARSER_CONTEXT parser_context = pclxl_context->parser_context;

  if ( include_per_page_resources )
  {
    pclxl_pattern_end_page(pclxl_context);
  }

#ifdef PCLXL_RESET_BETWEEEN_SESSIONS_IN_SAME_JOB

  {
    PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;

    /*
     * Our current understanding is that the Postscript "page device"
     * should handle any additional page throw needed to eject the last
     * page of a job
     *
     * And we have empirically found that 2 sequential PCLXL "Sessions"
     * *within* the same job are actually run together
     * such that a session starting with duplex page side 2
     * will appear on the back side of an immediately preceding
     * session that ends on duplex side 1
     *
     * So this code is #ifdef'd out
     */

    if ( (non_gs_state->current_media_details.duplex) &&
         (non_gs_state->current_media_details.duplex_page_side == PCLXL_eFrontMediaSide) &&
         ((!pclxl_push_gs(pclxl_context, PCLXL_PS_SAVE_RESTORE)) ||
          (!pclxl_throw_page(pclxl_context, &non_gs_state->current_media_details,
                             1, TRUE)) ||
         (!pclxl_pop_gs(pclxl_context, FALSE))) )
    {
      return FALSE;
    }
  }

#endif

  if ( pclxl_context->passthrough_state_info )
  {
    pcl5_destroy_passthrough_context(pclxl_context->passthrough_state_info);

    pclxl_free_passthrough_state_info(pclxl_context, pclxl_context->passthrough_state_info);

    pclxl_context->passthrough_state_info = NULL;
  }

  pclxl_pattern_end_session(pclxl_context);

  /*
   * We must remove all *temporary* soft fonts created
   * during this session.
   *
   * Note(1) that PCLXL is only able to create *temporary* soft fonts.
   *
   * But a PCL5 passthrough can create/change
   * a temporary soft font into a permanent soft font
   * that then persists beyond the PCLXL session.
   *
   * Note(2) This then means that attempting to submit this same
   * PCL5-passthrough-creates-permanent-soft-font" job twice
   * *may* result in some sort of error being raised
   *
   * Note(3) If PCLXL (named) fonts and PCL5 "alphanumeric" font IDs
   * share the same "namespace" (a.k.a. font header format?)
   * then a subsequent attempt to create an overlapping/duplicate named
   * temporary PCLXL soft font will definitely result in an error
   */
  /**
   * \todo PCLXL temporary soft fonts versus PCL5 (passthrough) permanent
   * alphanumeric soft font overlap needs testing
   */

  (void) pclxl_remove_soft_fonts(pclxl_context, FALSE);

  /*
   * We must clear down any job-added graphics states to leave just the original
   * graphics state created by the pclxlexec_()'s creation of the PCLXL_CONTEXT
   * *AND* any additional graphics state created by pclxl_op_begin_session Note
   * that we can probably simply throw these structures away because we arg
   * going to do a Postscript "restore" anyway
   */

  while ( ((graphics_state = pclxl_context->graphics_state) != NULL) &&
          (graphics_state->parent_graphics_state != NULL) )
  {
    if ( !pclxl_pop_gs(pclxl_context, FALSE) )
    {
      return FALSE;
    }
  }

  /* Ensure the data source is flagged closed */
  parser_context->data_source_open = FALSE;

  return TRUE;
}

/*
 * Tag 0x42 EndSession
 *
 * The EndSession operator does not take any attributes as such
 * (but we will none-the-less validate that this is indeed the case)
 */

Bool
pclxl_op_end_session(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;

  /* Check no attributes were supplied */
  if ( !pclxl_attr_set_match_empty(parser_context->attr_set, pclxl_context,
                                   PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS, ("EndSession"));

  return(pclxl_release_resources(pclxl_context, FALSE));
}

/******************************************************************************
* Log stripped */
