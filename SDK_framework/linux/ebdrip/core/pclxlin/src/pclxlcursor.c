/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlcursor.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of basic PCLXL cursor positioning functions
 * including the PCLXL "SetCursor" and "SetCursorRel" operators
 */

#include "core.h"

#include "pclxltypes.h"
#include "pclxldebug.h"
#include "pclxlcontext.h"
#include "pclxlerrors.h"
#include "pclxloperators.h"
#include "pclxlattributes.h"
#include "pclxlgraphicsstate.h"
#include "pclxlpsinterface.h"

#ifdef DEBUG_BUILD
#include "pclxltest.h"
#endif

int32
pclxl_moveto(PCLXL_CONTEXT pclxl_context,
             PCLXL_SysVal x,
             PCLXL_SysVal y)
{
  if ( pclxl_context->graphics_state->ctm_is_invertible &&
       !pclxl_ps_moveto(pclxl_context, x, y) )
  {
    return FALSE;
  }
  else
  {
    /*
     * Re-synchronize our notion of current position
     * with Postscript's current position
     * and return.
     */

    pclxl_get_current_path_and_point(pclxl_context->graphics_state);
    return TRUE;
  }
}

int32
pclxl_moveif(PCLXL_CONTEXT pclxl_context,
             PCLXL_SysVal x,
             PCLXL_SysVal y)
{
  if ( pclxl_context->graphics_state->ctm_is_invertible &&
       !pclxl_ps_moveif(pclxl_context, x, y) )
  {
    return FALSE;
  }
  else
  {
    /*
     * Re-synchronize our notion of current position
     * with Postscript's current position
     * and return.
     */

    pclxl_get_current_path_and_point(pclxl_context->graphics_state);
    return TRUE;
  }
}

static
PCLXL_ATTR_MATCH match_point[2] = {
#define POINT_POINT   (0)
  {PCLXL_AT_Point | PCLXL_ATTR_REQUIRED},
  PCLXL_MATCH_END
};

/*
 * Tag 0x6b SetCursor
 *
 * This PCLXL operator accepts an (x,y) coordinate pair
 * as a single "Point" attribute
 *
 * The "x" and "y" values may be unsigned single byte integers,
 * unsigned 16-bit integers or signed 16-bit integers
 *
 * The numeric values are always in "user" units
 * but we convert them and store them as PCLXL "internal" units
 * which are 1/7200th of an inch
 */

Bool
pclxl_op_set_cursor(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_SysVal_XY user_coord;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match_point,
                             pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* Point */
  pclxl_attr_get_real_xy(match_point[POINT_POINT].result, &user_coord);

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
              ("SetCursor(%f,%f)", user_coord.x, user_coord.y));

#ifdef DEBUG_BUILD

  if ( pclxl_context->config_params.debug_pclxl & PCLXL_DEBUG_CURSOR_POSITION )
    (void) pclxl_dot(pclxl_context, user_coord.x, user_coord.y, 3, NULL);

#endif

  /* Move to this new position and record this as the new "current" position */
  return(pclxl_moveto(pclxl_context, user_coord.x, user_coord.y));
}

/*
 * Tag 0x6c
 */

Bool
pclxl_op_set_cursor_rel(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_SysVal_XY user_coord;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match_point,
                             pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* Point */
  pclxl_attr_get_real_xy(match_point[POINT_POINT].result, &user_coord);

  if ( !graphics_state->current_point ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_VECTOR, PCLXL_CURRENT_CURSOR_UNDEFINED,
                        ("There is no current position to move (%c%f,%c%f) user units relative to",
                         ((user_coord.x >= 0.0) ? '+' : '-'), user_coord.x,
                         ((user_coord.y >= 0.0) ? '+' : '-'), user_coord.y));
    return(FALSE);
  }

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
              ("SetCursorRel(%f,%f) => (%f,%f)",
               user_coord.x,
               user_coord.y,
               (graphics_state->current_point_xy.x + user_coord.x),
               (graphics_state->current_point_xy.y + user_coord.y)));

  /* "moveto" this relative position and  record this as the new current position
   */

#ifdef DEBUG_BUILD

  if ( pclxl_context->config_params.debug_pclxl & PCLXL_DEBUG_CURSOR_POSITION )
    (void) pclxl_dot(pclxl_context,
                     (graphics_state->current_point_xy.x + user_coord.x),
                     (graphics_state->current_point_xy.y + user_coord.y),
                     3, NULL);

#endif

  return(pclxl_moveto(pclxl_context,
                      (graphics_state->current_point_xy.x + user_coord.x),
                      (graphics_state->current_point_xy.y + user_coord.y)));
}

/******************************************************************************
* Log stripped */
