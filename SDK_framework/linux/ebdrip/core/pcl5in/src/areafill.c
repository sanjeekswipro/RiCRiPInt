/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:areafill.c(EBDSDK_P.1) $
 * $Id: src:areafill.c,v 1.24.1.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the PCL5 "Rectangular Area Fill Graphics" category.
 */

#include "core.h"
#include "areafill.h"
#include "pcl5context_private.h"
#include "printenvironment_private.h"
#include "pcl5scan.h"

#include "fileio.h"
#include "monitor.h"
#include "rectops.h"
#include "gs_color.h"
#include "pclGstate.h"
#include "display.h"

static AreaFillInfo* get_area_fill(PCL5Context* context)
{
  HQASSERT(context != NULL && context->print_state != NULL &&
           context->print_state->mpe != NULL, "Bad context.");

  return &context->print_state->mpe->area_fill;
}

/* See header for doc. */
void default_area_fill(AreaFillInfo* self)
{
  SETXY(self->size, 0, 0);
}


/* Round the RECTANGLE size up to the nearest whole number of dots,
 * as suggested by the PCL5 Tech Ref and confirmed from the reference
 * printer, (for the default PCL5 Grid Intersection.  For Grid Centred
 * it will be 1 pixel smaller).  To get the equivalent effect we need
 * to round up then subtract 1.  We also need to adjust the position
 * of the top left corner, (in user space).
 */
static void adjust_rect_vals(PCL5Context *pcl5_ctxt, RECTANGLE *rect)
{
  PCL5PrintState *print_state;
  PageCtrlInfo *page_info;
  SYSTEMVALUE x_dots, y_dots;
  uint32 x_resolution, y_resolution;
  uint32 rotation;

#if defined(DEBUG_BUILD)
  IPOINT top_left, top_right, bottom_right;
  int32 width, height;
#endif /* DEBUG_BUILD */

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL");
  HQASSERT(rect != NULL, "rect is NULL");

  print_state = pcl5_ctxt->print_state;
  page_info = get_page_ctrl_info(pcl5_ctxt);

  HQASSERT(print_state != NULL, "print_state is NULL");
  HQASSERT(page_info != NULL, "page_info is NULL");

  HQASSERT(print_state->hw_resolution[0] != 0, "resolution is zero");
  HQASSERT(print_state->hw_resolution[1] != 0, "resolution is zero");

  rotation = (((print_state->media_orientation + page_info->orientation) * 90)
              + page_info->print_direction) % 360;

  /* Work out the resolution in the x and y directions of user space. */
  if (rotation == 0 || rotation == 180) {
    x_resolution = print_state->hw_resolution[0];
    y_resolution = print_state->hw_resolution[1];
  }
  else {
    x_resolution = print_state->hw_resolution[1];
    y_resolution = print_state->hw_resolution[0];
  }

  /* Adjust the start position for the area fill.
   * N.B. This is necessary because when pixel touching we will add a column or
   *      row to the right of and below the area fill, in device space.
   *      However, the reference printer in effect expects this column or row
   *      to be added to the right of or below the rectfill in user space.
   *      The following is therefore quite dependent on our exact implementation
   *      of pixel touching.
   *
   *      Nothing special needs doing here for the tesselating pixel touching
   *      rule, (and we do not need to know whether this is in effect), as we
   *      already agree with the reference printer in omitting to paint a
   *      column and row to the right of and below the rectfill, in device
   *      space.
   */

  switch(rotation) {
    case 0:
      break;

    case 90:
      /* Start the rectangle 1 device pixel further right */
      rect->x += 7200 / (SYSTEMVALUE) x_resolution;
      break;

    case 180:
      /* Start the rectangle 1 device pixel further right and down */
      rect->x += 7200 / (SYSTEMVALUE) x_resolution;
      rect->y += 7200 / (SYSTEMVALUE) y_resolution;
      break;

    case 270:
      /* Start the rectangle 1 device pixel lower down */
      rect->y += 7200 / (SYSTEMVALUE) y_resolution;
      break;

    default:
      HQFAIL("Unexpected rotation for rectfill");
      break;
  }

  /* Adjust the size */
  x_dots = (rect->w * x_resolution / (SYSTEMVALUE) 7200);
  y_dots = (rect->h * y_resolution / (SYSTEMVALUE) 7200);

  rect->w = (ceil(x_dots) - 1) * 7200 / (SYSTEMVALUE) x_resolution;
  rect->h = (ceil(y_dots) - 1) * 7200 / (SYSTEMVALUE) y_resolution;

#if defined(DEBUG_BUILD)
  top_left = ctm_transform(pcl5_ctxt, rect->x, rect->y);
  top_right = ctm_transform(pcl5_ctxt, (rect->x + rect->w), rect->y);
  bottom_right = ctm_transform(pcl5_ctxt, (rect->x + rect->w), (rect->y + rect->h));

  if (rotation == 0 || rotation == 180) {
    width = abs(top_right.x - top_left.x);
    height = abs(top_right.y - bottom_right.y);
  }
  else {
    width = abs(top_right.y - top_left.y);
    height = abs(top_right.x - bottom_right.x);
  }

  HQASSERT((width + 1) == ceil(x_dots), "Unexpected width for area fill");
  HQASSERT((height + 1) == ceil(y_dots), "Unexpected height for area fill");
#endif /* DEBUG_BUILD */
}


/**
 * The Horizontal Rectangle Size command specifies the rectangle width in
 * decipoints.
 */
Bool pcl5op_star_c_H(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  AreaFillInfo* self = get_area_fill(pcl5_ctxt);

  UNUSED_PARAM(int32, explicit_sign);

  /* N.B. Printer treats negative values as positive.
   * Limit to range by analogy with width in PCL Units.
   * Truncate to 4 decimal places by analogy with height.
   */
  value.real = pcl5_limit_to_range(fabs(value.real), 0, 32767);
  value.real = pcl5_truncate_to_4d(value.real);

  self->size.x = DECIPOINTS_TO_INTERNAL(value.real);

  return TRUE;
}

/**
 * The Vertical Rectangle Size command specifies the rectangle height in
 * decipoints.
 */
Bool pcl5op_star_c_V(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  AreaFillInfo* self = get_area_fill(pcl5_ctxt);

  UNUSED_PARAM(int32, explicit_sign);

  /* N.B. Printer treats negative values as positive.
   * Limit to range by analogy with height in PCL Units.
   * The value appears to be truncated to 4 decimal places.
   */
  value.real = pcl5_limit_to_range(fabs(value.real), 0, 32767);
  value.real = pcl5_truncate_to_4d(value.real);

  self->size.y = DECIPOINTS_TO_INTERNAL(value.real);

  return TRUE;
}


/**
 * The Horizontal Rectangle Size command specifies the rectangle width in PCL
 * Units.
 */
Bool pcl5op_star_c_A(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  AreaFillInfo* self = get_area_fill(pcl5_ctxt);
  PageCtrlInfo* page = get_page_ctrl_info(pcl5_ctxt);

  UNUSED_PARAM(int32, explicit_sign);

  /* N.B. Printer treats negative values as positive and limits range */
  value.integer = (int32) pcl5_limit_to_range(abs(value.integer), 0, 32767);

  self->size.x = pcl_unit_to_internal(page, value.integer);

  return TRUE;
}

/**
 * This Vertical Rectangle Size command specifies the rectangle height in PCL
 * Units.
 */
Bool pcl5op_star_c_B(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  AreaFillInfo* self = get_area_fill(pcl5_ctxt);
  PageCtrlInfo* page = get_page_ctrl_info(pcl5_ctxt);

  UNUSED_PARAM(int32, explicit_sign);

  /* N.B. Printer treats negative values as positive and limits range */
  value.integer = (int32) pcl5_limit_to_range(abs(value.integer), 0, 32767);

  self->size.y = pcl_unit_to_internal(page, value.integer);

  return TRUE;
}

/**
 * This command fills a rectangular area with the specified area fill.
 * The top-left corner of the rectangle is defined by the current cursor
 * position, and the width and height from the current rectangle width/height,
 * as set be the appropriate commands.
 */
Bool pcl5op_star_c_P(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  AreaFillInfo* self = get_area_fill(pcl5_ctxt);
  PrintModelInfo *pm = get_print_model(pcl5_ctxt);
  PCL5Ctms* ctms = get_pcl5_ctms(pcl5_ctxt);
  PageCtrlInfo *page_info = get_page_ctrl_info(pcl5_ctxt);
  PCL5Real x, y;
  RECTANGLE rectangle;
  uint32 pattern_type = 0;
  uint32 temp_pattern_type = 0;
  Bool success = TRUE;

  UNUSED_PARAM(int32, explicit_sign);
  UNUSED_PARAM(PCL5Numeric, value);

   /* We do this lazily now */
  if (! pcl5_flush_text_run(pcl5_ctxt, 1))
    return FALSE ;
  
  /* Rectfills force any partial underline to be drawn. */
  /** \todo Is this true even if the rectfill is omitted below? */
  underline_callback(pcl5_ctxt, TRUE);

  ctm_install(ctms);

  get_cursor_position(pcl5_ctxt, &x, &y);

  rectangle.x = x;
  rectangle.y = y;
  rectangle.w = self->size.x;
  rectangle.h = self->size.y;

  /* Limit the rectangle size to the logical page */
  if (rectangle.x + rectangle.w >= page_info->page_width)
    rectangle.w = (SYSTEMVALUE) max(0, page_info->page_width - rectangle.x);

  if (rectangle.y + rectangle.h >= page_info->max_text_length)
    rectangle.h = (SYSTEMVALUE) max(0, page_info->max_text_length - rectangle.y);

  if (rectangle.w == 0 || rectangle.h == 0)
    return TRUE;

  /* Allow for differences in how pixel touching rules are applied to rectfills
   * in rip and reference printer by adjusting position of top left corner, (in
   * user space), and by adjusting the rectangle size.
   */
  adjust_rect_vals(pcl5_ctxt, &rectangle);

  if (IN_RANGE(value.integer, 0, 5))
    pattern_type = value.integer;

  if ( !set_current_color(pcl5_ctxt) )
    return FALSE;

  /* Sections 5-8 and 5-50 of the color techical reference mention white 'rules'
   * and white fills always being treated as opaque, i.e. always erase
   * regardless of the pattern transparency mode.  Testing confirms this for
   * white (erase) fills, for black fills with foreground color white, and for
   * fills using the current pattern, where the current pattern is a white
   * (erase) fill, or a black fill and the foreground color is white.
   *
   * No special handling is needed here in the event that the temp_pattern_type
   * is the PCL5_ERASE_PATTERN, since that will be dealt with correctly by one
   * of the two cases below, (the pattern ID being irrelevant in this case).
   *
   * Note fills using the current pattern use the current pattern ID, whilst
   * other fills use the pending pattern ID.
   */

  temp_pattern_type = pattern_type;

  if (pattern_type == PCL5_CURRENT_PATTERN)
    temp_pattern_type = pm->current_pattern_type;

  if (temp_pattern_type == PCL5_SOLID_FOREGROUND && foreground_is_white(pcl5_ctxt)) {
    set_current_pattern_with_id(pcl5_ctxt, 0, PCL5_ERASE_PATTERN);
  }
  else if (pattern_type == PCL5_CURRENT_PATTERN)
    set_current_pattern_with_id(pcl5_ctxt, pm->current_pattern_id, PCL5_CURRENT_PATTERN);
  else
    set_current_pattern_with_id(pcl5_ctxt, pm->pending_pattern_id, pattern_type);

  success = success && dorectfill(1, &rectangle, GSC_FILL, RECT_NORMAL);

  return success;
}

/* ============================================================================
* Log stripped */
