/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxloperators.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions that handle individual PCLXL operators.
 * These functions are referenced by the pclxl_operator_details[] array
 * and are exclusively accessed via this array by pclxl_handle_operator()
 *
 * Note that it is anticipated that this file will be split up
 * into a number of separate files that each implement some of these operators.
 */

#include "core.h"
#include "swcopyf.h"
#include "namedef_.h"   /* NAME_strict_pclxl_protocol_class, NAME_debug_pclxl etc.*/

#include "pclxltypes.h"
#include "pclxldebug.h"
#include "pclxlcontext.h"
#include "pclxlscan.h"
#include "pclxlerrors.h"
#include "pclxloperators.h"
#include "pclxlattributes.h"
#include "pclxlgraphicsstate.h"
#include "pclxlpsinterface.h"
#include "pclxlsmtable.h"
#include "pclxltags.h"


static
uint8* pclxl_operator_string[PCLXL_OP_Passthrough - PCLXL_OP_BeginSession + 1] =
{
  (uint8*)"BeginSession",
  (uint8*)"EndSession",
  (uint8*)"BeginPage",
  (uint8*)"EndPage",
  NULL,
  (uint8*)"VendorUnique",
  (uint8*)"Comment",
  (uint8*)"OpenDataSource",
  (uint8*)"CloseDataSource",
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  (uint8*)"BeginFontHeader",
  (uint8*)"ReadFontHeader",
  (uint8*)"EndFontHeader",
  (uint8*)"BeginChar",
  (uint8*)"ReadChar",
  (uint8*)"EndChar",
  (uint8*)"RemoveFont",
  (uint8*)"SetCharAttributes",
  (uint8*)"SetDefaultGs",
  (uint8*)"SetColorTreatment",
  NULL,
  NULL,
  (uint8*)"BeginStream",
  (uint8*)"ReadStream",
  (uint8*)"EndStream",
  (uint8*)"ExecStream",
  (uint8*)"RemoveStream",
  (uint8*)"PopGS",
  (uint8*)"PushGS",
  (uint8*)"SetClipReplace",
  (uint8*)"SetBrushSource",
  (uint8*)"SetCharAngle",
  (uint8*)"SetCharScale",
  (uint8*)"SetCharShear",
  (uint8*)"SetClipIntersect",
  (uint8*)"SetClipRectangle",
  (uint8*)"SetClipToPage",
  (uint8*)"SetColorSpace",
  (uint8*)"SetCursor",
  (uint8*)"SetCursorRel",
  (uint8*)"SetHalftoneMethod",
  (uint8*)"SetFillMode",
  (uint8*)"SetFont",
  (uint8*)"SetLineDash",
  (uint8*)"SetLineCap",
  (uint8*)"SetLineJoin",
  (uint8*)"SetMiterLimit",
  (uint8*)"SetPageDefaultCTM",
  (uint8*)"SetPageOrigin",
  (uint8*)"SetPageRotation",
  (uint8*)"SetPageScale",
  (uint8*)"SetPaintTxMode",
  (uint8*)"SetPenSource",
  (uint8*)"SetPenWidth",
  (uint8*)"SetROP",
  (uint8*)"SetSourceTxMode",
  (uint8*)"SetCharBoldValue",
  (uint8*)"SetNeutralAxis",
  (uint8*)"SetClipMode",
  (uint8*)"SetPathToClip",
  (uint8*)"SetCharSubMode",
  NULL,
  NULL,
  (uint8*)"CloseSubPath",
  (uint8*)"NewPath",
  (uint8*)"PaintPath",
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  (uint8*)"ArcPath",
  (uint8*)"SetColorTrapping",
  (uint8*)"BezierPath",
  (uint8*)"SetAdaptiveHalftoning",
  (uint8*)"BezierRelPath",
  (uint8*)"Chord",
  (uint8*)"ChordPath",
  (uint8*)"Ellipse",
  (uint8*)"EllipsePath",
  NULL,
  (uint8*)"LinePath",
  NULL,
  (uint8*)"LineRelPath",
  (uint8*)"Pie",
  (uint8*)"PiePath",
  (uint8*)"Rectangle",
  (uint8*)"RectanglePath",
  (uint8*)"RoundRectangle",
  (uint8*)"RoundRectanglePath",
  NULL,
  NULL,
  NULL,
  NULL,
  (uint8*)"Text",
  (uint8*)"TextPath",
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  (uint8*)"BeginImage",
  (uint8*)"ReadImage",
  (uint8*)"EndImage",
  (uint8*)"BeginRastPattern",
  (uint8*)"ReadRastPattern",
  (uint8*)"EndRastPattern",
  (uint8*)"BeginScan",
  NULL,
  (uint8*)"EndScan",
  (uint8*)"ScanLineRel",
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  (uint8*)"Passthrough",
};

uint8* pclxl_error_get_tag_string(
  PCLXL_TAG op_tag,
  int32     error_code)
{
  static uint8 unknown_op_name[64];

  if ( (error_code != PCLXL_ILLEGAL_OPERATOR_TAG) && is_xloperator(op_tag) ) {
    return pclxl_operator_string[op_tag - PCLXL_OP_BeginSession];
  }

  /*
   * Valid/genuine PCLXL *operators* will always be in the range 0x41
   * (BeginSession) and 0xbf (Passthrough)
   *
   * However during error handling it is perfectly possible that we will want
   * to report an error associated with a non-operator tag.
   *
   * Therefore we handle this by constructing the following "dummy" operator
   * string so that pclxl_report_errors() can none-the-less call
   * pclxl_get_tag_string()
   */

  if ( is_xlreserved(op_tag) ) {
    swncopyf(unknown_op_name, sizeof(unknown_op_name),
             (uint8*) "operator 0x%02x reserved for future use", op_tag);

  } else {
    /* All other tag bytes are just reported as a hex value */
    swncopyf(unknown_op_name, sizeof(unknown_op_name), (uint8*) "0x%02x", op_tag);
  }

  return unknown_op_name;

} /* pclxl_error_get_tag_string */

uint8* pclxl_get_tag_string(
  PCLXL_TAG op_tag)
{
  return(pclxl_error_get_tag_string(op_tag, 0));
}


#define XLOP_GC_SAFE    (0x01)
#define XLOP_SETG_REQD  (0x02)

typedef struct pclxl_op_det_struct {
  PCLXL_PROTOCOL_VERSION  protocol_version;
  uint32                  flags;
  PCLXL_OP_FN_PTR         op_function;
} PCLXL_OP_DET_STRUCT;

/* Macro to simplify initialising operator details,
 * c, r - operator minimum protocol class and revision
 * p - protovar related flags
 * f - operator function
 */
#define XL_OP_DETAILS(c, r, p, f) \
  {PCLXL_PACK_PROTOCOL_CLASS_AND_REVISION((c), (r)), (p), (f)}

/* Macro for reserved operators, protocol class and revision should ensure
 * stream protocol version check fails.
 */
#define XL_OP_RESERVED  XL_OP_DETAILS(PCLXL_MAX_PROTOCOL_CLASS, PCLXL_MAX_PROTOCOL_REVISION, 0, NULL)

/**
 * \brief
 * pclxl_operator_details is an array of 97 structures that is intended to be
 * indexed using a PCLXL "operator" byte code tag that is read from a PCLXL
 * stream and then offset by 0x41 which is the lowest ID operator
 * ("BeginSession") byte code tag.
 *
 * Each structure contains the operator name and the earliest PCLXL protocol
 * version in which this operator was supported and a pointer to a function that
 * handles the operator
 */

/* setg_required flag.
 * As a shortcut to finding all the places in the code where PS gstate or
 * colorType changes such as to require a DEVICE_SETG, an extra field has been
 * added to this structure so that if we encounter an operator which may make
 * this happen, we can set the setg_required flag in the PCLXL_NON_GS_STATE.
 * This allows the next text command to call finishaddchardisplay and to
 * ensure that any necessary setg is done.
 *
 * So far this table has been filled in very conservatively, i.e. there are
 * likely to be places here where we could get away without setting the flag,
 * (or setting it could be delayed until inside the operator where we can see
 * whether a real change has taken place), and hence get a performance gain.
 */
static
PCLXL_OP_DET_STRUCT pclxl_operator_details[PCLXL_OP_Passthrough - PCLXL_OP_BeginSession + 1] =
{
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_begin_session),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_end_session),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_begin_page),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_end_page),
  XL_OP_RESERVED,
  XL_OP_DETAILS(2, 0, 0, pclxl_op_vendor_unique),
  XL_OP_DETAILS(2, 0, 0, pclxl_op_comment),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_open_data_source),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_close_data_source),
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_DETAILS(1, 1, 0, pclxl_op_begin_font_header),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_read_font_header),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_end_font_header),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_begin_char),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_read_char),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_end_char),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_remove_font),
  XL_OP_DETAILS(2, 0, XLOP_SETG_REQD, pclxl_op_set_char_attributes),
  XL_OP_DETAILS(2, 1, XLOP_SETG_REQD, pclxl_op_set_default_gs),
  XL_OP_DETAILS(2, 1, 0, pclxl_op_set_color_treatment),
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_DETAILS(1, 1, 0, pclxl_op_begin_stream),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_read_stream),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_end_stream),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_exec_stream),
  XL_OP_DETAILS(2, 0, 0, pclxl_op_remove_stream),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_pop_gs),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_push_gs),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_set_clip_replace),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_set_brush_source),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_set_char_angle),        /* setg_required dealt with in pclxl_op_set_font */
  XL_OP_DETAILS(1, 1, 0, pclxl_op_set_char_scale),        /* setg_required dealt with in pclxl_op_set_font */
  XL_OP_DETAILS(1, 1, 0, pclxl_op_set_char_shear),        /* setg_required dealt with in pclxl_op_set_font */
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_set_clip_intersect),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_set_clip_rectangle),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_set_clip_to_page),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_set_color_space),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_set_cursor),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_set_cursor_rel),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_set_halftone_method),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_set_fill_mode),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_set_font),              /* setg_required dealt with in handler */
  XL_OP_DETAILS(1, 1, 0, pclxl_op_set_line_dash),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_set_line_cap),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_set_line_join),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_set_miter_limit),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_set_page_default_CTM),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_set_page_origin),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_set_page_rotation),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_set_page_scale),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_set_paint_tx_mode),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_set_pen_source),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_set_pen_width),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_set_ROP),               /* setg_required dealt with in handler */
  XL_OP_DETAILS(1, 1, 0, pclxl_op_set_source_tx_mode),    /* setg_required dealt with in handler */
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_set_char_bold_value),
  XL_OP_DETAILS(3, 0, XLOP_SETG_REQD, pclxl_op_set_neutral_axis),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_set_clip_mode),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_set_path_to_clip),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_set_char_sub_mode),
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_DETAILS(1, 1, 0, pclxl_op_close_sub_path),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_new_path),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_paint_path),
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_DETAILS(1, 1, 0, pclxl_op_arc_path),
  XL_OP_DETAILS(3, 0, 0, pclxl_op_set_color_trapping),      /* NYI so no setg_required */
  XL_OP_DETAILS(1, 1, 0, pclxl_op_bezier_path),
  XL_OP_DETAILS(3, 0, 0, pclxl_op_set_adaptive_halftoning), /* NYI so no setg_required */
  XL_OP_DETAILS(1, 1, 0, pclxl_op_bezier_rel_path),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_chord),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_chord_path),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_ellipse),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_ellipse_path),
  XL_OP_RESERVED,
  XL_OP_DETAILS(1, 1, 0, pclxl_op_line_path),
  XL_OP_RESERVED,
  XL_OP_DETAILS(1, 1, 0, pclxl_op_line_rel_path),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_pie),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_pie_path),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_rectangle),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_rectangle_path),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_round_rectangle),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_round_rectangle_path),
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_DETAILS(1, 1, 0, pclxl_op_text),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_text_path),
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_DETAILS(1, 1, XLOP_GC_SAFE|XLOP_SETG_REQD, pclxl_op_begin_image),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_read_image),
  XL_OP_DETAILS(1, 1, 0, pclxl_op_end_image),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_begin_rast_pattern),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_read_rast_pattern),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_end_rast_pattern),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_begin_scan),
  XL_OP_RESERVED,
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_end_scan),
  XL_OP_DETAILS(1, 1, XLOP_SETG_REQD, pclxl_op_scan_line_rel),
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_RESERVED,
  XL_OP_DETAILS(3, 0, XLOP_SETG_REQD, pclxl_op_pass_through)
};

static
PCLXL_OP_DET_STRUCT* pclxl_get_operator_details(
  PCLXL_TAG op_tag)
{
  HQASSERT((is_xloperator(op_tag)),
           "pclxl_get_operator_details: not an operator tag");

  return &pclxl_operator_details[op_tag - PCLXL_OP_BeginSession];
}

/*
 * The valid operator tag sequence is handled using a state transition table in
 * which there are (currently) 17 states which each have an associated array of
 * 97 possible "transitions", one for each possible operator tag
 *
 * We basically obtain the current state's transition array and then look up the
 * transition for this latest operator tag
 *
 * The transition consists of an "action" to perform and a new state to switch
 * to (assuming the action is successful)
 */

static
Bool pclxl_valid_op_sequence(
  PCLXL_TAG            op_tag,
  PCLXL_PARSER_CONTEXT parser_context)
{
  uint8 current_state;
  uint8* current_state_transitions;
  uint8 this_op_transition;
  uint8 new_state;
  uint8 action;

  HQASSERT(((op_tag >= SM_EVENT_OFFSET) && (op_tag < (SM_EVENT_OFFSET + SM_EVENT_COUNT))),
           "Parser State Machine only handles restricted range of operator tags");

  current_state = parser_context->parser_states[parser_context->parser_states_index];
  current_state_transitions = pclxl_state_table[current_state];
  this_op_transition = current_state_transitions[(op_tag - SM_EVENT_OFFSET)];
  new_state = SM_STATE(this_op_transition);
  action = SM_ACTION(this_op_transition);

  PCLXL_DEBUG(PCLXL_DEBUG_STATE_MACHINE,
              ("Existing state = %d, \"%s\" (Tag 0x%02x), position = %d, action = %d, new state = %d%s",
               current_state,
               pclxl_get_tag_string(op_tag),
               op_tag,
               pclxl_stream_op_counter(pclxl_parser_current_stream(parser_context)),
               action,
               new_state,
               (new_state == STATE_ERROR ? " (Error)" : "")));

  if ( new_state == STATE_ERROR ) {
    PCLXL_ERROR_HANDLER(parser_context->pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_OPERATOR_SEQUENCE,
                        ("The \"%s\" (Tag 0x%02x) was not expected at this position in the PCLXL data stream",
                         pclxl_get_tag_string(op_tag), op_tag));
    return(FALSE);
  }

  switch ( action ) {
  case ACTION_NOCHANGE:
    /*
     * For this op_tag in this state, there is no need to do anything because we
     * are already in the correct state
     *
     * But the one thing that we can do here is to check that we really are
     * already in the target state
     */
    HQASSERT((new_state == parser_context->parser_states[parser_context->parser_states_index]),
             "We're supposed to already be in the correct parser state");
    return(TRUE);

  case ACTION_CHANGE:
    /*
     * For this tag in this state we are supposed to simply change the existing
     * parser context state to be the new state (But note that this new state
     * might be the error state)
     */
    parser_context->parser_states[parser_context->parser_states_index] = new_state;
    return(TRUE);

  case ACTION_PUSH:
    /*
     * We have been asked to "push" a new parser context (in the new state
     * specified) onto the stack of parser_contexts
     *
     * Note that in doing so, we move any attribute_list from beneath the
     * existing context to belong to this new context instead
     */
    if ( parser_context->parser_states_index < (sizeof(parser_context->parser_states) - 1) ) {
      parser_context->parser_states_index++;
      parser_context->parser_states[parser_context->parser_states_index] = new_state;
      return(TRUE);
    }
    PCLXL_ERROR_HANDLER(parser_context->pclxl_context, PCLXL_SS_KERNEL, PCLXL_INTERNAL_ERROR,
                        ("Too many parser states. Parser state index already = %d when trying to push new state %d",
                         parser_context->parser_states_index, new_state));
    return(FALSE);

  case ACTION_POP:
    /*
     * We have been asked to "pop" the current parser context (This presumes
     * that we have previously "pushed" the current context on top of a
     * previous/"parent" context)
     */
    HQASSERT((new_state == parser_context->parser_states[parser_context->parser_states_index]),
             "\"new state\" in the case of \"pop\" action actually holds the state that we are supposed to be in *before* the \"pop\"");
    HQASSERT((parser_context->parser_states_index > 0), "Cannot \"pop\" last remaining parser state");
#ifdef DEBUG_BUILD
    parser_context->parser_states[parser_context->parser_states_index] = 0;
#endif /* DEBUG_BUILD */
    parser_context->parser_states_index--;
    return(TRUE);

  default:
    PCLXL_ERROR_HANDLER(parser_context->pclxl_context, PCLXL_SS_KERNEL, PCLXL_INTERNAL_ERROR,
                        ("Unknown operator sequence state change value %d", action));
    return(FALSE);
  }

  /*NOTREACHED*/
}

/*
 * pclxl_handle_operator() is called by the PCLXL scan function(s) whenever they
 * read a PCLXL tag
 *
 * This function then looks up the tag details which include details of the
 * expected/allowed associated attributes (if any).
 *
 * If the attributes are valid and the tag is valid in this version of the
 * protocol stream it then it goes ahead and calls the associated tag handling
 * function which *may* result in the reading of yet more bytes from the PCLXL
 * data stream
 *
 * It returns a negative result code if a failure occurred and a positive number
 * of bytes read if it succeeded
 */

Bool
pclxl_handle_operator(PCLXL_TAG            op_tag,
                      PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_OP_DET_STRUCT* op_details = NULL;
  Bool op_fn_result = TRUE;
  PCLXLSTREAM* p_stream;

  op_details = pclxl_get_operator_details(op_tag);

  p_stream = pclxl_parser_current_stream(parser_context);
  if ( !pclxl_stream_min_protocol(p_stream, op_details->protocol_version) ) {
    /*
     * This operator is only supported in a higher PCLXL protocol class/revision
     * than this PCLXL stream (header) has declared/admitted to
     */

    PCLXL_ERROR_HANDLER(parser_context->pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_OPERATOR_TAG,
                        ("PCLXL tag \"%s\" is only supported in protocol class %d revision %d or higher. This stream declared as protocol class %d revision %d",
                        pclxl_get_tag_string(op_tag),
                        PCLXL_UNPACK_PROTOCOL_CLASS(op_details->protocol_version),
                        PCLXL_UNPACK_PROTOCOL_REVISION(op_details->protocol_version),
                        PCLXL_UNPACK_PROTOCOL_CLASS(pclxl_stream_protocol(pclxl_parser_current_stream(parser_context))),
                        PCLXL_UNPACK_PROTOCOL_REVISION(pclxl_stream_protocol(pclxl_parser_current_stream(parser_context)))));
    return FALSE;
  }

  /*
   * We now need to check that this tag is indeed valid at this point in the
   * protocol stream
   */
  if ( !pclxl_valid_op_sequence(op_tag, parser_context) ) {
    return FALSE;
  }

#ifdef DEBUG_BUILD

  if ( parser_context->pclxl_context->config_params.debug_pclxl )
  {
    PCLXL_DEBUG(PCLXL_DEBUG_TAGS,
                ("Operator \"%s\" (Tag 0x%02x)", pclxl_get_tag_string(op_tag), op_tag));
  }

#endif /* DEBUG_BUILD */

  if ( (op_details->flags & XLOP_SETG_REQD) != 0 )
    parser_context->pclxl_context->non_gs_state.setg_required = TRUE ;

  if ( (op_details->flags & XLOP_GC_SAFE) == 0 )
    gc_unsafe_from_here_on();
  op_fn_result = (*op_details->op_function)(parser_context);

  /* If the last command was a pass through and we get this far, it
     means that we ought to be caching the attributes and command. */
  if (! parser_context->last_command_was_pass_through) {
    /* Clear the attribute set ready for the next operator */
    pclxl_attr_set_empty(parser_context->attr_set);
  }

  return op_fn_result;
}


/*
 * Tag 0x46 "VendorUnique" is not mentioned anywhere in any version of
 * the PCLXL Protocol Class "Specifications"
 * but has been empirically determined to exist.
 *
 * Unfortunately without a specification of what it does
 * and what attributes it accepts
 * we cannot really do *anything* with it except simply "consume"
 * it and any associated attributes (and any subsequent embedded data?)
 */

Bool
pclxl_op_vendor_unique(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[5] = {
#define VENDORUNIQUE_VU_EXTENSION   (0)
    {PCLXL_AT_VUExtension},
#define VENDORUNIQUE_VA_ATTR1       (1)
    {PCLXL_AT_VUAttr1},
#define VENDORUNIQUE_VA_ATTR2       (2)
    {PCLXL_AT_VUAttr2},
#define VENDORUNIQUE_VA_ATTR3       (3)
    {PCLXL_AT_VUAttr3},
    PCLXL_MATCH_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
              ("Operator \"VendorUnique\" (Tag 0x%02x) ignored/skipped as we have no specification for it",
               pclxl_stream_op_tag(pclxl_parser_current_stream(parser_context))));

  /* For now a no-op apart from basic attribute checking based on what we have
   * seen - no validation is done.
   *
   * There is a VUDataLength(146) attribute which has not yet been seen in the
   * wild.  This may indicate embedded data, in which case if we see the
   * attribute we can (expect to) consume that amount of embedded data.
   */

  return(TRUE);
}

/*
 * Tag 0x47 Comment
 *
 * Comments are ignored. I.e. we do absolutely nothing with them
 * (except perhaps trace them?)
 */

Bool
pclxl_op_comment(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define COMMENT_COMMENT_DATA  (0)
    {PCLXL_AT_CommentData},
    PCLXL_MATCH_END
  };

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match,
                             parser_context->pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_COMMENTS),
              ("Comment(...)"));

  return(TRUE);
}

/*
 * Tag 0x57
 */

Bool
pclxl_op_set_default_gs(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;

  if ( !pclxl_attr_set_match_empty(parser_context->attr_set, pclxl_context,
                                   PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS, ("SetDefaultGS"));

  return pclxl_set_default_graphics_state(pclxl_context, graphics_state);
}

/*
 * Tag 0x58
 */
/* The AllObjectTypes and Text/Vector/RasterObject attributes do not appear in
 * any HP documentation, but do appear in the QL FTS Anomaly jobs A009.bin and
 * A012.bin.
 */
Bool
pclxl_op_set_color_treatment(PCLXL_PARSER_CONTEXT parser_context)
{
#ifdef TASK_368031
  static PCLXL_ATTR_MATCH match[6] = {
#define SETCOLORTREATMENT_COLOR_TREATMENT (0)
    {PCLXL_AT_ColorTreatment},
#define SETCOLORTREATMENT_ALL_OBJECT_TYPES (1)
    {PCLXL_AT_AllObjectTypes},
#define SETCOLORTREATMENT_TEXT_OBJECTS (2)
    {PCLXL_AT_TextObjects},
#define SETCOLORTREATMENT_VECTOR_OBJECTS (3)
    {PCLXL_AT_VectorObjects},
#define SETCOLORTREATMENT_RASTER_OBJECTS (4)
    {PCLXL_AT_RasterObjects},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION color_treatment_values[] = {
    PCLXL_eNoTreatment,
    PCLXL_eVivid,
    PCLXL_eScreenMatch,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  Bool stream_30;
  uint8 color_treatment;

  stream_30 = pclxl_stream_min_protocol(pclxl_parser_current_stream(parser_context),
                                        PCLXL_PROTOCOL_VERSION_3_0);

  /* Check for allowed attributes and data types.
   * QL CET E602.bin expects illegal attribute reported before missing
   * attribute, so cannot require at least 1 of the above attributes to be
   * present. */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* ColorTreatment */
  if ( match[SETCOLORTREATMENT_COLOR_TREATMENT].result ) {
    if ( match[SETCOLORTREATMENT_ALL_OBJECT_TYPES].result ||
         match[SETCOLORTREATMENT_TEXT_OBJECTS].result ||
         match[SETCOLORTREATMENT_VECTOR_OBJECTS].result ||
         match[SETCOLORTREATMENT_RASTER_OBJECTS].result ) {
      if ( stream_30 ) {
        PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_TEXT, PCLXL_ILLEGAL_ATTRIBUTE_COMBINATION,
                            ("Got object based as well as generic attributes."));
      } else {
        PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_TEXT, PCLXL_ILLEGAL_ATTRIBUTE,
                            ("Got object based treatment attributes in pre 3.0 stream"));
      }
      return(FALSE);
    }

    return(pclxl_attr_match_enumeration(match[SETCOLORTREATMENT_COLOR_TREATMENT].result,
                                        color_treatment_values, &color_treatment,
                                        pclxl_context, PCLXL_SS_KERNEL));
  }

  if ( !stream_30 ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_TEXT, PCLXL_ILLEGAL_ATTRIBUTE,
                        ("Got object based treatment attributes in pre 3.0 stream"));
    return(FALSE);
  }

  /* AllObjectTypes */
  if ( match[SETCOLORTREATMENT_ALL_OBJECT_TYPES].result ) {
    if ( match[SETCOLORTREATMENT_TEXT_OBJECTS].result ||
         match[SETCOLORTREATMENT_VECTOR_OBJECTS].result ||
         match[SETCOLORTREATMENT_RASTER_OBJECTS].result ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_TEXT, PCLXL_ILLEGAL_ATTRIBUTE_COMBINATION,
                          ("Got all object and object specific attributes"));
      return(FALSE);
    }

    return(pclxl_attr_match_enumeration(match[SETCOLORTREATMENT_ALL_OBJECT_TYPES].result,
                                        color_treatment_values, &color_treatment,
                                        pclxl_context, PCLXL_SS_KERNEL));
  }

  if ( !match[SETCOLORTREATMENT_TEXT_OBJECTS].result &&
       !match[SETCOLORTREATMENT_VECTOR_OBJECTS].result &&
       !match[SETCOLORTREATMENT_RASTER_OBJECTS].result ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_TEXT, PCLXL_MISSING_ATTRIBUTE,
                        ("Absolutely no attributes present"));
    return(FALSE);
  }

  /* TextObjects */
  if ( match[SETCOLORTREATMENT_TEXT_OBJECTS].result ) {
    if ( !pclxl_attr_match_enumeration(match[SETCOLORTREATMENT_TEXT_OBJECTS].result,
                                       color_treatment_values, &color_treatment,
                                       pclxl_context, PCLXL_SS_KERNEL)) {
      return(FALSE);
    }
  }
  /* VectorObjects */
  if ( match[SETCOLORTREATMENT_VECTOR_OBJECTS].result ) {
    if ( !pclxl_attr_match_enumeration(match[SETCOLORTREATMENT_VECTOR_OBJECTS].result,
                                       color_treatment_values, &color_treatment,
                                       pclxl_context, PCLXL_SS_KERNEL)) {
      return(FALSE);
    }
  }
  /* RasterObjects */
  if ( match[SETCOLORTREATMENT_RASTER_OBJECTS].result ) {
    if ( !pclxl_attr_match_enumeration(match[SETCOLORTREATMENT_RASTER_OBJECTS].result,
                                       color_treatment_values, &color_treatment,
                                       pclxl_context, PCLXL_SS_KERNEL)) {
      return(FALSE);
    }
  }

  /* No support for operator yet */

  return(TRUE);
#else
  UNUSED_PARAM(PCLXL_PARSER_CONTEXT, parser_context) ;
    /* 368031 - temp solution to enable some jobs using enum 3 and 4 to process */
  return (TRUE);
#endif
}

#ifdef DEBUG_BUILD

static uint32
pclxl_graphics_state_stack_depth(PCLXL_GRAPHICS_STATE_STACK* gs_stack)
{
  uint32 i;
  PCLXL_GRAPHICS_STATE gs;

  for ( i = 0, gs = *gs_stack ;
        ((gs != NULL) && (gs->postscript_op < PCLXL_PS_SAVE_RESTORE)) ;
        i++, gs = gs->parent_graphics_state ) ;

  return i;
}

#endif

Bool
pclxl_pop_gs(PCLXL_CONTEXT pclxl_context,
             Bool          job_requested_pop_gs)
{
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_GRAPHICS_STATE parent_graphics_state;
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;
  ps_context_t *pscontext ;

  HQASSERT(pclxl_context->corecontext != NULL, "No core context") ;
  pscontext = pclxl_context->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;

  HQASSERT((graphics_state != NULL),
           "There is no current graphics state, not even the root PCLXL graphics state created by pclxlexec_()");

  non_gs_state->setg_required = TRUE;

  /*
   * We must guard against the removal of the graphics state
   * that was created via the creation of the root PCLXL_CONTEXT
   * that was created by, and persistent for the duration of,
   * the call to pclxlexec()
   */
  if ( ((parent_graphics_state = graphics_state->parent_graphics_state) == NULL) )
  {
    /*
     * Why have we tried to "pop" the original/root graphics state?!
     * At this level the graphics state stack should be fully under our control
     * and so we should only be attempting to "pop" graphics states
     * that were pushed there by BeginSession or BeginPage
     */

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Attempt to \"pop\" the \"root\" graphics state. This is an internal error"));

    return FALSE;
  }

  if ( (job_requested_pop_gs) &&
       (graphics_state->postscript_op >= PCLXL_PS_SAVE_RESTORE) )
  {
    /*
     * We are trying to pop a graphics state from the stack
     * as a direct result of a PopGS PCLXL operator.
     *
     * But the graphics state at the top of the stack
     * was *not* pushed there as a result of a corresponding PushGS operator.
     *
     * This situation can only occur if the current PCLXL "job"
     * is trying to do more "PopGS"s than the number of preceding "PushGS"s
     */

    if ( pclxl_context->config_params.strict_pclxl_protocol_class )
    {
      (void) PCLXL_WARNING_HANDLER(pclxl_context,
                                   PCLXL_SS_KERNEL,
                                   PCLXL_GRAPHICS_STATE_STACK_ERROR,
                                   ("Attempt to \"PopGS\" more (additional) graphics states than were created using \"PushGS\""));
    }

    return TRUE;  /* Note that we return TRUE as this is only a *warning* */
  }

  if ( (graphics_state = pclxl_pop_graphics_state(&pclxl_context->graphics_state)) == NULL )
  {
    /*
     * Huh? We appear to have failed to "pop" the graphics state
     * from the top of the stack.
     * This has just got to be an internal error
     */

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to \"pop\" graphics state from the stack?!"));

    return FALSE;
  }

  /*
   * We must now do whatever is necessary to restore/revert to
   * the previous *Postscript* graphics state
   */

  if ( ((graphics_state->postscript_op == PCLXL_PS_SAVE_RESTORE) &&
        (!pclxl_ps_restore(pclxl_context,
                           &graphics_state->ps_save_object))) ||
       ((graphics_state->postscript_op == PCLXL_PS_GSAVE_GRESTORE) &&
        (!grestore_(pscontext))) ||
       ((graphics_state->postscript_op == PCLXL_PS_NO_OP_SET_CTM) &&
        (!pclxl_set_current_ctm(parent_graphics_state,
                                &parent_graphics_state->current_ctm)))
     )
  {
    return FALSE;
  }
  else
  {
    /*
     * Also back to the previous PCL-specific graphics state
     */

    pclxl_pcl_grestore(pclxl_context, parent_graphics_state);

    /*
     * And finally we can delete the popped graphics state
     */

    pclxl_delete_graphics_state(pclxl_context, graphics_state);

    return TRUE;
  }
}

/*
 * Tag 0x60
 */

Bool
pclxl_op_pop_gs(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;

#ifdef DEBUG_BUILD

  uint32 graphics_state_stack_depth = pclxl_graphics_state_stack_depth(&pclxl_context->graphics_state);

#endif

  if ( !pclxl_attr_set_match_empty(parser_context->attr_set, pclxl_context,
                                   PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_GS_STACK),
              ("PopGS(stack_depth %d -> %d)",
                graphics_state_stack_depth, (graphics_state_stack_depth - 1)));

  return pclxl_pop_gs(pclxl_context, TRUE);
}

Bool
pclxl_push_gs(PCLXL_CONTEXT pclxl_context,
              uint8         postscript_op)
{
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_GRAPHICS_STATE graphics_state_copy;
  ps_context_t *pscontext ;

  HQASSERT(pclxl_context->corecontext != NULL, "No core context") ;
  pscontext = pclxl_context->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;

  /**
   * \todo If we wish to limit the number of "pushed" graphics states
   * then this is the place to do the check/implement the limit
   */

  if ( (graphics_state_copy = pclxl_create_graphics_state(graphics_state,
                                                            pclxl_context)) == NULL )
  {
    /*
     * We have failed to create a copy of the current graphics state
     * So we have nothing to "push" onto the graphics state stack
     * A suitable error has already been logged so we simply return False here
     */


    return FALSE;
  }

  if  ( ((postscript_op == PCLXL_PS_SAVE_RESTORE) &&
         (!pclxl_ps_save(pclxl_context,
                         &graphics_state_copy->ps_save_object))) ||
        ((postscript_op ==PCLXL_PS_GSAVE_GRESTORE) &&
         (!gsave_(pscontext))) )
  {
    (void) pclxl_delete_graphics_state(pclxl_context, graphics_state_copy);

    return FALSE;
  }

  graphics_state_copy->postscript_op = postscript_op;

  pclxl_push_graphics_state(graphics_state_copy,
                            &pclxl_context->graphics_state);

  return TRUE;
}

/*
 * Tag 0x61
 */

Bool
pclxl_op_push_gs(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;

#ifdef DEBUG_BUILD

  uint32 graphics_state_stack_depth = pclxl_graphics_state_stack_depth(&pclxl_context->graphics_state);

#endif

  if ( !pclxl_attr_set_match_empty(parser_context->attr_set, pclxl_context,
                                   PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_GS_STACK),
              ("PushGS(stack_depth %d -> %d)",
               graphics_state_stack_depth, (graphics_state_stack_depth + 1)));

  return pclxl_push_gs(pclxl_context, PCLXL_PS_GSAVE_GRESTORE);
}

/*
 * Tag 0x6e
 */

Bool
pclxl_op_set_fill_mode(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define SETFILLMODE_FILL_MODE  (0)
    {PCLXL_AT_FillMode | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION allowed_fill_mode_values[] = {
    PCLXL_eNonZeroWinding,
    PCLXL_eEvenOdd,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_FillMode fill_mode;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* FillMode */
  if ( !pclxl_attr_match_enumeration(match[SETFILLMODE_FILL_MODE].result,
                                     allowed_fill_mode_values, &fill_mode,
                                     pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS, ("SetFillMode %d", fill_mode));

  return(pclxl_set_fill_mode(pclxl_context->graphics_state, fill_mode));
}

/*
 * Tag 0x70
 */

Bool
pclxl_op_set_line_dash(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[4] = {
#define SETLINEDASH_SOLID_LINE      (0)
    {PCLXL_AT_SolidLine},
#define SETLINEDASH_LINE_DASH_STYLE (1)
    {PCLXL_AT_LineDashStyle},
#define SETLINEDASH_DASH_OFFSET     (2)
    {PCLXL_AT_DashOffset},
    PCLXL_MATCH_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_ATTRIBUTE dash_style_attr;
  uint32 solid_line;
  int32 dash_offset;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match_at_least_1(parser_context->attr_set, match,
                                        pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* SolidLine */
  if ( match[SETLINEDASH_SOLID_LINE].result ) {
    if ( match[SETLINEDASH_LINE_DASH_STYLE].result || match[SETLINEDASH_DASH_OFFSET].result ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_COMBINATION,
                          ("Found dashed line attributes with SolidLine"));
      return(FALSE);
    }

    solid_line = pclxl_attr_get_uint(match[SETLINEDASH_SOLID_LINE].result);

    if ( solid_line != 0 ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                          ("SolidLine must be 0"));
      return(FALSE);
    }

    PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS, ("SetLineDash SolidLine"));

    return(pclxl_set_line_dash(pclxl_context, pclxl_context->graphics_state, NULL, 0));
  }

  /* LineDashStyle */
  if ( !match[SETLINEDASH_LINE_DASH_STYLE].result ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_MISSING_ATTRIBUTE,
                        ("Neither SolidLine or LineDashStyle seen"));
    return(FALSE);
  }
  dash_style_attr = match[SETLINEDASH_LINE_DASH_STYLE].result;

  /* DashOffset */
  dash_offset = 0;
  if ( match[SETLINEDASH_DASH_OFFSET].result ) {
    dash_offset = pclxl_attr_get_int(match[SETLINEDASH_DASH_OFFSET].result);
  }

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS, ("SetLineDash LineDashStyle"));

  return(pclxl_set_line_dash(pclxl_context, pclxl_context->graphics_state,
                             dash_style_attr, dash_offset));
}

/*
 * Tag 0x71
 */

Bool
pclxl_op_set_line_cap(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define SETLINECAP_LINE_CAP_STYLE   (0)
    {PCLXL_AT_LineCapStyle | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION allowed_line_cap_values[] = {
    PCLXL_eRoundCap,
    PCLXL_eTriangleCap,
    PCLXL_eSquareCap,
    PCLXL_eButtCap,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_LineCap line_cap;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* LineCapStyle */
  if ( !pclxl_attr_match_enumeration(match[SETLINECAP_LINE_CAP_STYLE].result,
                                     allowed_line_cap_values, &line_cap,
                                     pclxl_context, PCLXL_SS_STATE) ) {
    return(FALSE);
  }

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS, ("SetLineCap %d", line_cap));

  return(pclxl_set_line_cap(pclxl_context->graphics_state, line_cap));
}

/*
 * Tag 0x72
 */

Bool
pclxl_op_set_line_join(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define SETLINEJOIN_LINE_JOIN_STYLE (0)
    {PCLXL_AT_LineJoinStyle | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION allowed_line_join_values[] = {
    PCLXL_eMiterJoin,
    PCLXL_eRoundJoin,
    PCLXL_eBevelJoin,
    PCLXL_eNoJoin,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_LineJoin line_join;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* LineJoinStyle */
  if ( !pclxl_attr_match_enumeration(match[SETLINEJOIN_LINE_JOIN_STYLE].result,
                                     allowed_line_join_values, &line_join,
                                     pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS, ("SetLineJoin %d", line_join));

  return(pclxl_set_line_join(pclxl_context->graphics_state, line_join));
}

/*
 * Tag 0x73
 */

Bool
pclxl_op_set_miter_limit(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define SETMITERLIMIT_MITER_LENGTH  (0)
    {PCLXL_AT_MiterLength | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_SysVal miter_length;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* MiterLength */
  miter_length = pclxl_attr_get_real(match[SETMITERLIMIT_MITER_LENGTH].result);

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
              ("SetMiterLimit(MiterLength = %d)", miter_length));

  return(pclxl_set_miter_limit(pclxl_context, pclxl_context->graphics_state, miter_length));
}

/*
 * Tag 0x78
 */

Bool
pclxl_op_set_paint_tx_mode(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define SETPAINTTXMODE_TX_MODE  (0)
    {PCLXL_AT_TxMode | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION allowed_tx_modes[] = {
    PCLXL_eOpaque,
    PCLXL_eTransparent,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* TxMode */
  if ( !pclxl_attr_match_enumeration(match[SETPAINTTXMODE_TX_MODE].result,
                                     allowed_tx_modes, &graphics_state->paint_tx_mode,
                                     pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
              ("SetPaintTxMode(TxMode = %s)",
               ((graphics_state->paint_tx_mode == PCLXL_eOpaque) ?  "eOpaque" : "eTransparent")));

  pclxl_pcl_set_paint_transparency(pclxl_context, graphics_state->paint_tx_mode == PCLXL_eTransparent);
  return(TRUE);
}

/*
 * Tag 0x7b SetROP
 */

Bool
pclxl_op_set_ROP(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define SETROP_ROP3   (0)
    {PCLXL_AT_ROP3 | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_ROP3 rop3;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* ROP3 */
  rop3 = CAST_UNSIGNED_TO_UINT8(pclxl_attr_get_uint(match[SETROP_ROP3].result));

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_RASTERS),
              ("SetROP3(ROP3 = %d)", rop3));

  graphics_state->ROP3 = rop3;

  pclxl_ps_set_rop3(pclxl_context, rop3, FALSE);

  return TRUE;
}

/*
 * Tag 0x7c
 */

Bool
pclxl_op_set_source_tx_mode(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define SETSOURCETXMODE_TX_MODE   (0)
    {PCLXL_AT_TxMode | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION allowed_tx_modes[] = {
    PCLXL_eOpaque,
    PCLXL_eTransparent,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = parser_context->pclxl_context->graphics_state;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* TxMode */
  if ( !pclxl_attr_match_enumeration(match[SETSOURCETXMODE_TX_MODE].result,
                                     allowed_tx_modes, &graphics_state->source_tx_mode,
                                     pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
              ("SetSourceTxMode(TxMode = %s)",
               ((graphics_state->source_tx_mode == PCLXL_eOpaque) ? "eOpaque" : "eTransparent")));

  pclxl_pcl_set_source_transparency(pclxl_context, graphics_state->source_tx_mode == PCLXL_eTransparent);
  return(TRUE);
}

/*
 * Tag 0x7e
 */

Bool
pclxl_op_set_neutral_axis(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[5] = {
#define SETNEUTRALAXIS_ALL_OBJECT_TYPES   (0)
    {PCLXL_AT_AllObjectTypes},
#define SETNEUTRALAXIS_TEXT_OBJECTS       (1)
    {PCLXL_AT_TextObjects},
#define SETNEUTRALAXIS_VECTOR_OBJECTS     (2)
    {PCLXL_AT_VectorObjects},
#define SETNEUTRALAXIS_RASTER_OBJECTS     (3)
    {PCLXL_AT_RasterObjects},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION allowed_black_types[] = {
    PCLXL_eTonerBlack,
    PCLXL_eProcessBlack,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_COLOR_SPACE_DETAILS color_space_details;
  PCLXL_BlackType all_obj_types_black_type;
  PCLXL_BlackType raster_black_type;
  PCLXL_BlackType text_black_type;
  PCLXL_BlackType vector_black_type;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match_at_least_1(parser_context->attr_set, match,
                                        pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  if ( match[SETNEUTRALAXIS_ALL_OBJECT_TYPES].result ) {
    if ( match[SETNEUTRALAXIS_TEXT_OBJECTS].result ||
         match[SETNEUTRALAXIS_VECTOR_OBJECTS].result ||
         match[SETNEUTRALAXIS_RASTER_OBJECTS].result ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_TEXT, PCLXL_ILLEGAL_ATTRIBUTE_COMBINATION,
                          ("Found specific object type attribute alongside all object type"));
      return(FALSE);
    }

    /* AllObjectTypes */
    if ( !pclxl_attr_match_enumeration(match[SETNEUTRALAXIS_ALL_OBJECT_TYPES].result,
                                       allowed_black_types, &all_obj_types_black_type,
                                       pclxl_context, PCLXL_SS_KERNEL) ) {
      return(FALSE);
    }

    PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
                ("SetNeutralAxis(AllObjectTypes = %s)",
                 (all_obj_types_black_type ? "eProcessBlack" : "eTonerBlack")));

    text_black_type = vector_black_type = raster_black_type = all_obj_types_black_type;

  } else { /* Per object type */

    color_space_details = graphics_state->color_space_details;

    /* TextObjects */
    text_black_type = color_space_details->text_black_type;
    if ( match[SETNEUTRALAXIS_TEXT_OBJECTS].result ) {
      if ( !pclxl_attr_match_enumeration(match[SETNEUTRALAXIS_TEXT_OBJECTS].result,
                                         allowed_black_types, &text_black_type,
                                         pclxl_context, PCLXL_SS_KERNEL) ) {
        return(FALSE);
      }
    }
    /* VectorObjects */
    vector_black_type = color_space_details->vector_black_type;
    if ( match[SETNEUTRALAXIS_VECTOR_OBJECTS].result ) {
      if ( !pclxl_attr_match_enumeration(match[SETNEUTRALAXIS_VECTOR_OBJECTS].result,
                                         allowed_black_types, &vector_black_type,
                                         pclxl_context, PCLXL_SS_KERNEL) ) {
        return(FALSE);
      }
    }
    /* RasterObjects */
    raster_black_type = color_space_details->raster_black_type;
    if ( match[SETNEUTRALAXIS_RASTER_OBJECTS].result ) {
      if ( !pclxl_attr_match_enumeration(match[SETNEUTRALAXIS_RASTER_OBJECTS].result,
                                         allowed_black_types, &raster_black_type,
                                         pclxl_context, PCLXL_SS_KERNEL) ) {
        return(FALSE);
      }
    }

    PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
                ("SetNeutralAxis(RasterObjects = %s, TextObjects = %s, VectorObjects = %s)",
                 (match[SETNEUTRALAXIS_RASTER_OBJECTS].result ? (raster_black_type ? "eProcessBlack" : "eTonerBlack") : "<unchanged>"),
                 (match[SETNEUTRALAXIS_TEXT_OBJECTS].result ? (text_black_type ? "eProcessBlack" : "eTonerBlack") : "<unchanged>"),
                 (match[SETNEUTRALAXIS_VECTOR_OBJECTS].result ? (vector_black_type ? "eProcessBlack" : "eTonerBlack") : "<unchanged>")));
  }

  return(pclxl_set_black_types(pclxl_context, graphics_state,
                               raster_black_type, text_black_type, vector_black_type));
}

/*
 * Tag 0x92
 */

Bool
pclxl_op_set_color_trapping(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define SETCOLORTRAPPING_ALL_OBJECT_TYPES (0)
    {PCLXL_AT_AllObjectTypes | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION color_trapping_values[] = {
    PCLXL_eDisable,
    PCLXL_eMax,
    PCLXL_eNormal,
    PCLXL_eLight,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  uint8 color_trapping;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* AllObjectTypes */
  if ( !pclxl_attr_match_enumeration(match[SETCOLORTRAPPING_ALL_OBJECT_TYPES].result,
                                     color_trapping_values, &color_trapping,
                                     pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* No support for operator yet */

  return(TRUE);
}

/*
 * Tag 0x94
 */

Bool
pclxl_op_set_adaptive_halftoning(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[5] = {
#define SETADAPTIVEHT_ALL_OBJECT_TYPES    (0)
    {PCLXL_AT_AllObjectTypes},
#define SETADAPTIVEHT_TEXT_OBJECTS        (1)
    {PCLXL_AT_TextObjects},
#define SETADAPTIVEHT_VECTOR_OBJECTS      (2)
    {PCLXL_AT_VectorObjects},
#define SETADAPTIVEHT_RASTER_OBJECTS      (3)
    {PCLXL_AT_RasterObjects},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION adaptive_halftoning_values[] = {
    PCLXL_eDisable,
    PCLXL_eEnable,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  uint8 dummy;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match_at_least_1(parser_context->attr_set, match,
                                        pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  if ( match[SETADAPTIVEHT_ALL_OBJECT_TYPES].result ) {
    if ( match[SETADAPTIVEHT_TEXT_OBJECTS].result ||
         match[SETADAPTIVEHT_VECTOR_OBJECTS].result ||
         match[SETADAPTIVEHT_RASTER_OBJECTS].result ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_TEXT, PCLXL_ILLEGAL_ATTRIBUTE_COMBINATION,
                          ("Found specific object type attribute alongside all object type"));
      return(FALSE);
    }

    /* AllObjectTypes */
    if ( !pclxl_attr_match_enumeration(match[SETADAPTIVEHT_ALL_OBJECT_TYPES].result,
                                       adaptive_halftoning_values, &dummy,
                                       pclxl_context, PCLXL_SS_KERNEL) ) {
      return(FALSE);
    }

  } else { /* Per object type */

    /* TextObjects */
    if ( match[SETADAPTIVEHT_TEXT_OBJECTS].result ) {
      if ( !pclxl_attr_match_enumeration(match[SETADAPTIVEHT_TEXT_OBJECTS].result,
                                         adaptive_halftoning_values, &dummy,
                                         pclxl_context, PCLXL_SS_KERNEL) ) {
        return(FALSE);
      }
    }
    /* VectorObjects */
    if ( match[SETADAPTIVEHT_VECTOR_OBJECTS].result ) {
      if ( !pclxl_attr_match_enumeration(match[SETADAPTIVEHT_VECTOR_OBJECTS].result,
                                         adaptive_halftoning_values, &dummy,
                                         pclxl_context, PCLXL_SS_KERNEL) ) {
        return(FALSE);
      }
    }
    /* RasterObjects */
    if ( match[SETADAPTIVEHT_RASTER_OBJECTS].result ) {
      if ( !pclxl_attr_match_enumeration(match[SETADAPTIVEHT_RASTER_OBJECTS].result,
                                         adaptive_halftoning_values, &dummy,
                                         pclxl_context, PCLXL_SS_KERNEL) ) {
        return(FALSE);
      }
    }
  }

  /* Operator not implemented yet */

  return(TRUE);
}

/******************************************************************************
* Log stripped */
