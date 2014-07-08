/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlpage.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of PCLXL operator handlers for
 * BeginPage and EndPage operators
 */

#include "core.h"
#include "hqmemcpy.h"
/* #include "swcopyf.h" only needed if we assemble any Postscript strings in this file */
#include "display.h"  /* displaylistisempty() */
#include "hqmemset.h"
#include "namedef_.h"

#include "pcl.h"  /* for PASS_THROUGH_MARGIN */

#include "pclxltypes.h"
#include "pclxldebug.h"
#include "pclxlcontext.h"
#include "pclxlerrors.h"
#include "pclxloperators.h"
#include "pclxlattributes.h"
#include "pclxlgraphicsstate.h"
#include "pclxlpsinterface.h"
#include "pclxlfont.h"
#include "pclxlpattern.h"

#ifdef DEBUG_BUILD
#include "pclxltest.h"
#endif


Bool
pclxl_throw_page(PCLXL_CONTEXT        pclxl_context,
                 PCLXL_MEDIA_DETAILS  media_details,
                 uint32               page_copies,
                 Bool                 duplex_alignment_page_throw)
{
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;

  if (!finishaddchardisplay(pclxl_context->corecontext->page, 1)) {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to flush chars to DL"));
    return FALSE;
  }

  /*
   * I know that this seems weird but we do a Postscript "restore" *after* all
   * the drawing operations but *before* actually showing the page.
   *
   * This is because there are a number of "hooks" ("EndPage.ps", "BeginPage.ps"
   * etc.) that are allowed to make changes to the Postscript state including
   * making additional marks on the current page!  and modifying the graphics
   * state for the next page
   */

  if ( !pclxl_pop_gs(pclxl_context, FALSE) )
  {
    return FALSE;
  }

  pclxl_pcl_grestore(pclxl_context, pclxl_context->graphics_state);

  if ( !pclxl_ps_showpage(pclxl_context, page_copies) )
  {
    return FALSE;
  }

  /*
   * Note that although we *always* perform a restore and we must do this
   * *before* the "showpage" we must only perform a save IFF this is a duplex
   * alignment page throw Because if this is a page throw called by
   * pclxl_op_end_page() then the above "restore" is intended to match the save
   * performed by/in pclxl_setup_page_device()
   */

  if ( (duplex_alignment_page_throw) &&
       (!pclxl_push_gs(pclxl_context, PCLXL_PS_SAVE_RESTORE)) )
  {
    return FALSE;
  }

  /*
   * Not sure whether this test needs to be an OR or an AND We certainly want to
   * only increment duplex_page_side by 1 when actually doing duplex
   *
   * And we certainly only want to increment this count by 1 when throwing a
   * page side to correctly switch into or out of duplex page mode So I
   * currently think this needs to be an OR-test
   */

  if ( media_details->duplex || duplex_alignment_page_throw )
  {
      /*
       * When we are doing duplex pages (or when we are doing a page-throw to
       * correctly end a sequence of duplex pages before starting a simplex page
       * sequence) Then we only count this page throw as a single *side* of the
       * media
       */

      non_gs_state->duplex_page_side_count += 1;
      /* fix for blank pages thrown every page pcl duplex page side needs updating for each page*/
      non_gs_state->requested_media_details.duplex_page_side = non_gs_state->current_media_details.duplex_page_side ^ 0x1;  }
  else
  {
    /*
     * This is the end of a real PCLXL page and we're currently doing *simplex*
     * printing to this single-sided page will result in the equivalent of two
     * duplex *sides* being "consumed" So we must increment the duplex page
     * *side* count by 2 so that the duplex_page_side_count is still correctly
     * maintained
     */

    non_gs_state->duplex_page_side_count += 2;
  }

  return TRUE;
}

#ifdef DEBUG_BUILD
static uint8* pclxl_units_of_measure[] = {
  (uint8*) "inch",
  (uint8*) "millimeter",
  (uint8*) "tenth of a millimeter"
};
#endif

Bool
pclxl_handle_duplex(PCLXL_CONTEXT        pclxl_context,
                    PCLXL_MEDIA_DETAILS  previous_media_details,
                    PCLXL_MEDIA_DETAILS  current_media_details)
{
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;

  Bool leaving_duplex_mode;
  Bool media_change;
  Bool current_page_is_marked;

  /*
   * Note that the following CTM is going to be based round
   * the Postscript coordinate system
   * I.e. "points" with a bottom left-hand corner origin
   * with X increasing towards the right
   * and Y increasing up the page
   */

  pclxl_get_current_ctm(pclxl_context,
                        &graphics_state->physical_page_ctm,
                        NULL /* we are not interested in the invertibility of this matrix */);

  /*
   * Ok, we have available to us
   * a) the existing/current media details
   * b) the requested media details
   * c) the resultant media details
   *
   * We must compare the current and resultant media details
   * to see what has changed and what duplex page side phase changes are needed
   * and therefore how many (if any) blank pages are required
   * before we start marking this new page
   */

  leaving_duplex_mode = (previous_media_details->duplex && (! current_media_details->duplex));

  media_change = ((non_gs_state->duplex_page_side_count > 1) &&
                  (((previous_media_details->media_size != current_media_details->media_size) ||
                   ((previous_media_details->media_size_name == NULL) && (current_media_details->media_size_name != NULL)) ||
                   ((previous_media_details->media_size_name != NULL) && (current_media_details->media_size_name == NULL)) ||
                   ((previous_media_details->media_size_name != NULL) &&
                    (current_media_details->media_size_name != NULL) &&
                    (!strcmp((char*) previous_media_details->media_size_name, (char*) current_media_details->media_size_name))) ||
                   (previous_media_details->media_size_xy.x != current_media_details->media_size_xy.x) ||
                   (previous_media_details->media_size_xy.y != current_media_details->media_size_xy.y))));

  current_page_is_marked = (!displaylistisempty(pclxl_context->corecontext->page));

  /*
   * If:
   *
   * 1) We are doing duplex and we're about to do side 1 of a page
   *    but this page is intended to go on side 2
   *
   * Or
   *
   * 2) We are doing duplex and we're about to do side 2 of a page
   *    but this page is intended to go on side 1
   *
   * Or
   *
   * 3) We are not doing duplex (any more) and we're about to do side 2
   *    of a previous duplex sequence
   *    but we are now starting to do simplex printing
   *
   * Or
   *
   * 4) We are doing duplex printing
   *    but we have a media (size) change
   *
   * Then in all these cases we need to do at least one additional
   * page throw(s) to eject the current page
   *
   */
  /**
   * \todo Special case of no media (size) change AND no marks
   * on this first side of the page means that no page throws are required
   * && (media_change || current_page_is_marked)
   */

  if ( (current_media_details->duplex &&
        ((non_gs_state->duplex_page_side_count % 2) == 1) &&
        (current_media_details->duplex_page_side == PCLXL_eBackMediaSide)
       ) ||
       (current_media_details->duplex &&
        ((non_gs_state->duplex_page_side_count % 2) == 0) &&
        (current_media_details->duplex_page_side == PCLXL_eFrontMediaSide)
       ) ||
       (leaving_duplex_mode &&
        ((non_gs_state->duplex_page_side_count % 2) == 1) &&
        (current_media_details->simplex_page_side == PCLXL_eSimplexFrontSide)
       )
     )
  {
    /*
     * If we are doing duplex AND media size change AND this next page
     * is destined for the second side of the new media
     * then we also have to throw (away) the first side of the new media
     */

    Bool two_page_throws_required = (current_media_details->duplex &&
                                     media_change &&
                                     current_media_details->duplex_page_side == PCLXL_eBackMediaSide);

    /*
     * Then, in all these cases we need to do an additional (blank) page throw
     * to get to the correct side of (the next piece of) the media
     */

    if ( !pclxl_throw_page(pclxl_context, previous_media_details, 1, TRUE) )
    {
      return FALSE;
    }
    else if ( two_page_throws_required &&
              !pclxl_throw_page(pclxl_context, previous_media_details, 1, TRUE) )
    {
      return FALSE;
    }
  }

  return TRUE;
}

/*
 * pclxl_get_current_media_details() is passed a PCLXL_MEDIA_DETAILS struct
 * which it attempts to complete with as many details about the current
 * Postscript page device as possible.
 *
 * It is also optionally passed a "requested" media size
 * which is typically derived by taking the previous page' media details
 * overlaying any media attribute values supplied with a BeginPage operator
 *
 * If this "requested" media details is supplied,
 * then it is used as the starting point for the request to get the current
 * media details.
 *
 * Otherwise suitable defaults are used
 */

Bool
pclxl_get_current_media_details(PCLXL_CONTEXT       pclxl_context,
                                PCLXL_MEDIA_DETAILS requested_media_details,
                                PCLXL_MEDIA_DETAILS current_media_details)
{
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;

  if ( requested_media_details )
  {
    *current_media_details = *requested_media_details;
  }
  else
  {
    (void) pclxl_get_default_media_details(pclxl_context,
                                           current_media_details);
  }

  if ( !pclxl_ps_currentpagedevice(pclxl_context, current_media_details) )
  {
    return FALSE;
  }
  else
  {
    pclxl_get_current_ctm(pclxl_context,
                          &graphics_state->physical_page_ctm,
                          NULL /* we are not interested in the invertibility of this matrix */);

#ifdef DEBUG_BUILD

    {
      OMATRIX* p_ctm = &graphics_state->physical_page_ctm;

      PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS,
                  ("Physical Page CTM ((%f,%f)(%f,%f)(%f,%f))",
                   MATRIX_00(p_ctm), MATRIX_01(p_ctm),
                   MATRIX_10(p_ctm), MATRIX_11(p_ctm),
                   MATRIX_20(p_ctm), MATRIX_21(p_ctm)));
    }

#endif

    return TRUE;
  }
}

/*
 * pclxl_setup_page_device() basically takes all the page configuration values,
 * whether defaulted to their existing values
 * or extraced from the various optional BeginPage attribute values
 * and attempts to setup a page (device) that matches these requested values
 *
 * The big complication here is that the target device may not be able to
 * support all these requested values
 * (for instance it would be impossible to support A3 media on an A4 printer,
 *  nor would it be possible to do duplex printing on a non-duplex printer,
 *  and maybe it would be impossible to switch back from A3 to A4
 *  media if A4 has run out)
 *
 * So we must *attempt* to set up the page device
 * and then query the resultant page device
 * to determine what actually got set up
 */

Bool
pclxl_setup_page_device(PCLXL_CONTEXT        pclxl_context,
                        PCLXL_MEDIA_DETAILS  previous_media_details,
                        PCLXL_MEDIA_DETAILS  requested_media_details,
                        PCLXL_MEDIA_DETAILS  current_media_details)
{
  /*
   * The first thing to do is call "setpagedevice"
   * We supply both the "previous_media_details"
   * and the "requested_media_details"
   * so that (in the future) we can minimize the settings that need to be changed
   * possibly even not making the request at all
   */

  if ( !pclxl_ps_setpagedevice(pclxl_context,
                               previous_media_details,
                               requested_media_details) )
  {
    return FALSE;
  }

  /*
   * Then we must  query the resultant settings that may be different
   * from those we actually asked for
   */

  if ( !pclxl_get_current_media_details(pclxl_context,
                                        requested_media_details,
                                        current_media_details) )
  {
    return FALSE;
  }

  /*
   * Having set up the device we perform a save
   * which is matched by a restore either in pclxl_op_begin_page()
   * if it detects a subsequent error, or by pclxl_throw_page()
   * but only when called by pclxl_op_end_page()
   */

  if ( !pclxl_push_gs(pclxl_context, PCLXL_PS_SAVE_RESTORE) )
  {
    return FALSE;
  }

  if ( !pclxl_handle_duplex(pclxl_context,
                            previous_media_details,
                            current_media_details) )
  {
    (void) pclxl_pop_gs(pclxl_context, FALSE);

    return FALSE;
  }
  else
  {
    /*
     * If we get to here, then we must remember that there is now
     * an additional Postscript "save" object on the operand stack
     * that we must remember to remove with a corresponding "restore"
     * called by pclxl_op_end_page()
     *
     * And note that this is currently actually effected by
     * the call to pclxl_throw_page() with FALSE when called from
     * pclxl_op_end_page()
     */

    return TRUE;
  }
}

Bool
pclxl_set_default_ctm(PCLXL_CONTEXT pclxl_context,
                      PCLXL_GRAPHICS_STATE graphics_state,
                      PCLXL_NON_GS_STATE   non_gs_state)
{
  PCLXL_SysVal x_scaling_factor =
    (pclxl_ps_units_per_pclxl_uom(non_gs_state->measurement_unit) * 100.0 /
     ((non_gs_state->current_media_details.orientation % 2) ?
      non_gs_state->units_per_measure.res_y :
      non_gs_state->units_per_measure.res_x));

  PCLXL_SysVal y_scaling_factor =
    (pclxl_ps_units_per_pclxl_uom(non_gs_state->measurement_unit) * 100.0 /
     ((non_gs_state->current_media_details.orientation % 2) ?
      non_gs_state->units_per_measure.res_x :
      non_gs_state->units_per_measure.res_y));

  PCLXL_MEDIA_DETAILS current_media_details = &non_gs_state->current_media_details;

  PCLXL_SysVal page_width = (current_media_details->media_size_xy.x / x_scaling_factor);

  PCLXL_SysVal page_height = (current_media_details->media_size_xy.y / y_scaling_factor);

  /*
   * The first thing we need to do is to change the current CTM
   * So that we can begin using the (current) PCLXL "user" units
   * rather than the native PCLXL/PCL5 units
   */

  if ( !pclxl_ps_scale(pclxl_context, x_scaling_factor, y_scaling_factor) )
  {
    return FALSE;
  }

  /**
   * We now need to handle the page orientation
   * to transform the setpagedefault default top-left-hand-corner (0,0) origin portrait page
   * into any different (i.e. Landscape, Reverse-Landscape or Reverse-Portrait page orientation
   */

/* #define PCLXL_PERFORMS_TUMBLE 1 */
#define PCLXL_PERFORMS_TUMBLE 0

  if ( (((current_media_details->orientation == PCLXL_eDefaultOrientation) ||
         (current_media_details->orientation == PCLXL_ePortraitOrientation)) &&

#if PCLXL_PERFORMS_TUMBLE

        ((!current_media_details->duplex) ||
         (current_media_details->duplex_page_side == PCLXL_eFrontMediaSide) ||
         (current_media_details->duplex_binding == PCLXL_eDuplexVerticalBinding))) ||
       ((current_media_details->orientation == PCLXL_eReversePortrait) &&
        (current_media_details->duplex) &&
        (current_media_details->duplex_page_side == PCLXL_eBackMediaSide) &&
        (current_media_details->duplex_binding == PCLXL_eDuplexHorizontalBinding))

#else

        TRUE)

#endif

     )
  {
    /*
     * For Portrait (or Default) orientation pages
     * on simplex or front side of duplex pages
     * or ReversePortrait back side of duplex horizontal binding pages
     * We don't need to perform any further CTM changes
     * So we simply record the page width and height
     */

    graphics_state->page_width = page_width;

    graphics_state->page_height = page_height;
  }
  else if ( ((current_media_details->orientation == PCLXL_eReversePortrait) &&

#if PCLXL_PERFORMS_TUMBLE

             ((!current_media_details->duplex) ||
              (current_media_details->duplex_page_side == PCLXL_eFrontMediaSide) ||
              (current_media_details->duplex_binding == PCLXL_eDuplexVerticalBinding))) ||
            (((current_media_details->orientation == PCLXL_eDefaultOrientation) ||
              (current_media_details->orientation == PCLXL_ePortraitOrientation)) &&
              (current_media_details->duplex) &&
              (current_media_details->duplex_page_side == PCLXL_eBackMediaSide) &&
              (current_media_details->duplex_binding == PCLXL_eDuplexHorizontalBinding))

#else

             TRUE)

#endif

          )
  {
    /*
     * Reverse Portrait basically means a 180-degree
     * rotation about the page centre.
     * This is implemented as a translation of the origin
     * down to the bottom right-hand corner of the page
     * and then a 180-degree rotation about this new origin
     *
     * Note that we also apply this transform for
     * Portrait/Default orientation pages on
     * horizontally bound back side of duplex pages
     */

    if ( (!pclxl_ps_translate(pclxl_context, page_width, page_height)) ||
         (!pclxl_ps_rotate(pclxl_context, 180.0)) )
    {
      return FALSE;
    }
    else
    {
      graphics_state->page_width = page_width;

      graphics_state->page_height = page_height;
    }
  }
  else if ( ((current_media_details->orientation == PCLXL_eLandscapeOrientation) &&

#if PCLXL_PERFORMS_TUMBLE

             ((!current_media_details->duplex) ||
              (current_media_details->duplex_page_side == PCLXL_eFrontMediaSide) ||
              (current_media_details->duplex_binding == PCLXL_eDuplexVerticalBinding))) ||
            ((current_media_details->orientation == PCLXL_eReverseLandscape) &&
             (current_media_details->duplex) &&
             (current_media_details->duplex_page_side == PCLXL_eBackMediaSide) &&
             (current_media_details->duplex_binding == PCLXL_eDuplexHorizontalBinding))

#else

             TRUE)

#endif

          )
  {
    /*
     * Again we translate+rotate the origin
     * In this case down to the bottom left-hand corner of the page
     * and the clockwise by 90 degrees (i.e. -90 degrees)
     *
     * But we also swap the page width and height
     */

    if ( (!pclxl_ps_translate(pclxl_context, 0, page_height)) ||
         (!pclxl_ps_rotate(pclxl_context, -90.0)) )
    {
      return FALSE;
    }
    else
    {
      graphics_state->page_width = page_height;

      graphics_state->page_height = page_width;
    }
  }
  else if ( ((current_media_details->orientation == PCLXL_eReverseLandscape) &&

#if PCLXL_PERFORMS_TUMBLE

             ((!current_media_details->duplex) ||
              (current_media_details->duplex_page_side == PCLXL_eFrontMediaSide) ||
              (current_media_details->duplex_binding == PCLXL_eDuplexVerticalBinding))) ||
            ((current_media_details->orientation == PCLXL_eLandscapeOrientation) &&
             (current_media_details->duplex) &&
             (current_media_details->duplex_page_side == PCLXL_eBackMediaSide) &&
             (current_media_details->duplex_binding == PCLXL_eDuplexHorizontalBinding))

#else

             TRUE)

#endif

          )
  {

    /*
     * Again we translate+rotate the origin
     * In this case across to the top right-hand corner of the page
     * and the clockwise by 90 degrees (i.e. -90 degrees)
     *
     * But we also swap the page width and height
     */

    if ( (!pclxl_ps_translate(pclxl_context, page_width, 0)) ||
         (!pclxl_ps_rotate(pclxl_context, 90.0)) )
    {
      return FALSE;
    }
    else
    {
      graphics_state->page_width = page_height;

      graphics_state->page_height = page_width;
    }
  }
  else
  {
    /*
     * Oh dear, for this orientation, duplex, page side and binding
     * we weren't able to compute a suitable transform
     */

    PCLXL_ERROR_HANDLER(pclxl_context,
                        PCLXL_SS_KERNEL,
                        PCLXL_INTERNAL_ERROR,
                        ("Unhandled combination of Orientation = %d, %s, PageSide %d, %s binding",
                         current_media_details->orientation,
                         (current_media_details->duplex ? "Duplex" : "Simplex"),
                         (current_media_details->duplex ? current_media_details->duplex_page_side : current_media_details->simplex_page_side),
                         ((current_media_details->duplex && (current_media_details->duplex_binding == PCLXL_eDuplexHorizontalBinding)) ?
                          "Horizontal" : "Vertical")));

    return FALSE;
  }

  /*
   * And now is the obvious point to capture the logical page CTM
   * which we will need to restore behind the SetPageDefaultCTM operator
   */

  pclxl_get_current_ctm(pclxl_context,
                        &graphics_state->logical_page_ctm,
                        NULL /* we are not interested in the invertibility of this matrix */);

#ifdef DEBUG_BUILD

  {
    OMATRIX* p_ctm = &graphics_state->logical_page_ctm;

    PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS,
                ("Logical Page CTM ((%f,%f)(%f,%f)(%f,%f))\n(i.e. translated, rotated and scaled to %.1f dots per %s by %.1f dots per %s)",
                 MATRIX_00(p_ctm), MATRIX_01(p_ctm),
                 MATRIX_10(p_ctm), MATRIX_11(p_ctm),
                 MATRIX_20(p_ctm), MATRIX_21(p_ctm),
                 non_gs_state->units_per_measure.res_x,
                 pclxl_units_of_measure[non_gs_state->measurement_unit],
                 non_gs_state->units_per_measure.res_y,
                 pclxl_units_of_measure[non_gs_state->measurement_unit]
                )
               );
  }

#endif

  graphics_state->page_angle = 0;
  graphics_state->page_scale.x = graphics_state->page_scale.y = 1.0;

  pclxl_get_current_ctm(pclxl_context,
                        &graphics_state->current_ctm,
                        &graphics_state->ctm_is_invertible);

#ifdef DEBUG_BUILD

  {
    OMATRIX* p_ctm = &graphics_state->current_ctm;

    PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS,
                ("Set default CTM ((%f,%f)(%f,%f)(%f,%f))%s",
                 MATRIX_00(p_ctm), MATRIX_01(p_ctm),
                 MATRIX_10(p_ctm), MATRIX_11(p_ctm),
                 MATRIX_20(p_ctm), MATRIX_21(p_ctm),
                 (graphics_state->ctm_is_invertible ? "" : " which is NOT invertible")));

    if ( pclxl_context->config_params.debug_pclxl & PCLXL_DEBUG_PAGE_CTM )
      (void) pclxl_debug_origin(pclxl_context);
  }

#endif

  return TRUE;
}

Bool pclxl_set_page_clip(PCLXL_CONTEXT pclxl_context)
{
  ps_context_t *pscontext ;
  PCLXL_MEDIA_DETAILS current_media_details = &pclxl_context->non_gs_state.current_media_details;

  HQASSERT(pclxl_context->corecontext != NULL, "No core context") ;
  pscontext = pclxl_context->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;

  return (initclip_(pscontext) &&
          pclxl_ps_set_page_clip(pclxl_context,
                                 &pclxl_context->graphics_state->physical_page_ctm,
                                 current_media_details->printable_area_margins) &&
          clipsave_(pscontext));
}

/*
 * Tag 0x43 BeginPage
 *
 * Basically we obtain details of the requested PCLXL media
 * and then set up a Postscript "page device" to match this
 * request as closely as possible.
 *
 * We then reset the graphics state back to the
 * "default" start-of-page graphics state
 *
 * (What "default" means is still subject to exact specification.
 *  And there may be other things besides the default graphics state
 *  that need specification.)
 *
 * The RIP is then readied to begin accepting PCLXL drawing operations
 * for the new page
 */

Bool
pclxl_op_begin_page(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[11] = {
#define BEGINPAGE_ORIENTATION   (0)
    {PCLXL_AT_Orientation},
#define BEGINPAGE_MEDIA_SIZE    (1)
    {PCLXL_AT_MediaSize},
#define BEGINPAGE_CUSTOM_MEDIA_SIZE (2)
    {PCLXL_AT_CustomMediaSize},
#define BEGINPAGE_CUSTOM_MEDIA_SIZE_UNITS (3)
    {PCLXL_AT_CustomMediaSizeUnits},
#define BEGINPAGE_MEDIA_TYPE    (4)
    {PCLXL_AT_MediaType},
#define BEGINPAGE_MEDIA_SOURCE  (5)
    {PCLXL_AT_MediaSource},
#define BEGINPAGE_MEDIA_DESTINATION (6)
    {PCLXL_AT_MediaDestination},
#define BEGINPAGE_SIMPLEX_PAGE_MODE (7)
    {PCLXL_AT_SimplexPageMode},
#define BEGINPAGE_DUPLEX_PAGE_MODE (8)
    {PCLXL_AT_DuplexPageMode},
#define BEGINPAGE_DUPLEX_PAGE_SIDE (9)
    {PCLXL_AT_DuplexPageSide},
    PCLXL_MATCH_END
  };
  PCLXL_ENUMERATION* orientation_values;
  static PCLXL_ENUMERATION orientation_values_all[] = {
    PCLXL_eDefaultOrientation,    /* Not used for 1.1 and 2.0 streams */
    PCLXL_ePortraitOrientation,
    PCLXL_eLandscapeOrientation,
    PCLXL_eReversePortrait,
    PCLXL_eReverseLandscape,
    PCLXL_ENUMERATION_END
  };
  static PCLXL_ENUMERATION cms_units_values[] = {
    PCLXL_eInch,
    PCLXL_eMillimeter,
    PCLXL_eTenthsOfAMillimeter,
    PCLXL_ENUMERATION_END
  };
  static PCLXL_ENUMERATION media_source_values[] = {
    PCLXL_eDefaultSource,
    PCLXL_eAutoSelect,
    PCLXL_eManualFeed,
    PCLXL_eMultiPurposeTray,
    PCLXL_eUpperCassette,
    PCLXL_eLowerCassette,
    PCLXL_eEnvelopeTray,
    PCLXL_eThirdCassette,
    PCLXL_ENUMERATION_END
  };
  static PCLXL_ENUMERATION media_destination_values[] = {
    PCLXL_eDefaultDestination,
    PCLXL_eFaceDownBin,
    PCLXL_eFaceUpBin,
    PCLXL_eJobOffsetBin,
    PCLXL_ENUMERATION_END
  };
  static PCLXL_SimplexPageMode sim_page_side_values[] = {
    PCLXL_eSimplexFrontSide,
    PCLXL_ENUMERATION_END
  };
  static PCLXL_DuplexPageSide dup_page_side_values[] = {
    PCLXL_eFrontMediaSide,
    PCLXL_eBackMediaSide,
    PCLXL_ENUMERATION_END
  };
  static PCLXL_DuplexPageMode dup_binding_values[] = {
    PCLXL_eDuplexHorizontalBinding,
    PCLXL_eDuplexVerticalBinding,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_NON_GS_STATE non_gs_state  = &pclxl_context->non_gs_state;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_SysVal_XY custom_media_size_xy;
  PCLXL_SysVal cms_to_1_7200_multiplier;
  Bool stream_21;
  PCLXL_Measure cms_units;
  size_t max_len;
  size_t copy_len;
  PCLXL_ENUMERATION orientation;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  stream_21 = pclxl_stream_min_protocol(pclxl_parser_current_stream(parser_context),
                                        PCLXL_PROTOCOL_VERSION_2_1);
  /* Orientation */
  if ( match[BEGINPAGE_ORIENTATION].result ) {
    orientation_values = stream_21 ? orientation_values_all : &orientation_values_all[1];
    if ( pclxl_attr_valid_enumeration(match[BEGINPAGE_ORIENTATION].result,
                                      orientation_values, &orientation) ) {
      non_gs_state->requested_media_details.orientation = orientation;

    } else {
      PCLXL_WARNING_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ORIENTATION,
                            ("Invalid orientation enumeration - using existing."));
    }

  } else if ( !stream_21 ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_MISSING_ATTRIBUTE,
                        ("Orientation missing in a 1.1 or 2.0 stream"));
    return(FALSE);
  }

  /* MediaSize */
  if ( match[BEGINPAGE_MEDIA_SIZE].result ) {
    if ( match[BEGINPAGE_CUSTOM_MEDIA_SIZE].result ||
         match[BEGINPAGE_CUSTOM_MEDIA_SIZE_UNITS].result ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_COMBINATION,
                          ("Found Custom media attributes in addition to MediaSize"));
      return(FALSE);
    }

    if ( match[BEGINPAGE_MEDIA_SIZE].result->data_type == PCLXL_DT_UByte ) {
      /* Enumerated media size. */
      non_gs_state->requested_media_details.media_size =
        CAST_SIGNED_TO_UINT8(pclxl_attr_get_int(match[BEGINPAGE_MEDIA_SIZE].result));
      non_gs_state->requested_media_details.media_size_value_type = PCLXL_MEDIA_SIZE_ENUM_VALUE;
      non_gs_state->requested_media_details.media_size_name = NULL;
      non_gs_state->requested_media_details.media_size_xy.x = -1;
      non_gs_state->requested_media_details.media_size_xy.y = -1;

      PCLXL_DEBUG(PCLXL_DEBUG_ATTRIBUTES,
                  ("MediaSize (enum) = 0x%02x", (non_gs_state->requested_media_details.media_size)));

    } else if ( pclxl_stream_min_protocol(pclxl_parser_current_stream(parser_context),
                                          PCLXL_PROTOCOL_VERSION_2_0) ) {
      /* Media size name string allowed from 2.0 */
      non_gs_state->requested_media_details.media_size_name =
        match[BEGINPAGE_MEDIA_SIZE].result->value.v_ubytes;
      non_gs_state->requested_media_details.media_size_value_type = PCLXL_MEDIA_SIZE_NAME_VALUE;
      non_gs_state->requested_media_details.media_size = (PCLXL_MediaSize)-1; /* Awooga - this is 255! */
      non_gs_state->requested_media_details.media_size_xy.x = -1;
      non_gs_state->requested_media_details.media_size_xy.y = -1;

      PCLXL_DEBUG(PCLXL_DEBUG_ATTRIBUTES,
                  ("MediaSize (name) = \"%s\"", (non_gs_state->requested_media_details.media_size_name)));

    } else {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_DATA_TYPE,
                          ("String MediaSize attribute not supported in this protocol class."));
    }

  } else if ( match[BEGINPAGE_CUSTOM_MEDIA_SIZE].result &&
              match[BEGINPAGE_CUSTOM_MEDIA_SIZE_UNITS].result ) {
    /* CustomMediaSize */
    pclxl_attr_get_real_xy(match[BEGINPAGE_CUSTOM_MEDIA_SIZE].result, &custom_media_size_xy);
    /* CustomMediaSizeUnits */
    if ( !pclxl_attr_match_enumeration(match[BEGINPAGE_CUSTOM_MEDIA_SIZE_UNITS].result,
                                       cms_units_values, &cms_units, pclxl_context,
                                       PCLXL_SS_KERNEL) ) {
      return(FALSE);
    }

    cms_to_1_7200_multiplier = (pclxl_ps_units_per_pclxl_uom(cms_units) * 100.0);
    non_gs_state->requested_media_details.media_size_xy.x = custom_media_size_xy.x * cms_to_1_7200_multiplier;
    non_gs_state->requested_media_details.media_size_xy.x = custom_media_size_xy.y * cms_to_1_7200_multiplier;

    PCLXL_DEBUG(PCLXL_DEBUG_ATTRIBUTES,
                ("CustomMediaSize X = %f, Y = %f dots per %s => Page Width = %f, Page Height = %f in PCL5 \"internal\" units (i.e. 1/7200th of an inch)",
                 custom_media_size_xy.x, custom_media_size_xy.y,
                 pclxl_units_of_measure[cms_units],
                 non_gs_state->requested_media_details.media_size_xy.x,
                 non_gs_state->requested_media_details.media_size_xy.y));

    non_gs_state->requested_media_details.media_size_value_type = PCLXL_MEDIA_SIZE_XY_VALUE;
    non_gs_state->requested_media_details.media_size_name = (uint8*) "Custom";
    non_gs_state->requested_media_details.media_size = (PCLXL_MediaSize)-1; /* Awooga - this is 255! */

  } else { /* Default media if didn't get part of custom media attributes if allowed */
    if ( match[BEGINPAGE_CUSTOM_MEDIA_SIZE].result ||
         match[BEGINPAGE_CUSTOM_MEDIA_SIZE_UNITS].result ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_MISSING_ATTRIBUTE,
                          ("Did not get custom size and units attributes"));
      return(FALSE);
    }

    if ( !stream_21 ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_MISSING_ATTRIBUTE,
                          ("Did not get any media size attributes"));
      return(FALSE);
    }

    if ( !pclxl_get_default_media_size(pclxl_context,
                                       &non_gs_state->requested_media_details.media_size_value_type,
                                       &non_gs_state->requested_media_details.media_size,
                                       &non_gs_state->requested_media_details.media_size_name,
                                       &non_gs_state->requested_media_details.media_size_xy) ) {
      return(FALSE);
    }
  }

  /* MediaType */
  max_len = sizeof(non_gs_state->requested_media_details.media_type);
  if ( match[BEGINPAGE_MEDIA_TYPE].result ) {
    copy_len = min(max_len - 1, match[BEGINPAGE_MEDIA_TYPE].result->array_length);
    HqMemCpy(non_gs_state->requested_media_details.media_type,
             match[BEGINPAGE_MEDIA_TYPE].result->value.v_ubytes, copy_len);
    non_gs_state->requested_media_details.media_type[copy_len] = '\0';
  } else {
    HqMemZero(non_gs_state->requested_media_details.media_type, max_len);
  }

  /* MediaSource */
  if ( match[BEGINPAGE_MEDIA_SOURCE].result ) {
    if ( !pclxl_attr_match_enumeration(match[BEGINPAGE_MEDIA_SOURCE].result, media_source_values,
                                       &non_gs_state->requested_media_details.media_source,
                                       pclxl_context, PCLXL_SS_KERNEL) ) {
      return(FALSE);
    }
  }

  /* MediaDestination */
  if ( match[BEGINPAGE_MEDIA_DESTINATION].result ) {
    if ( !pclxl_attr_match_enumeration(match[BEGINPAGE_MEDIA_DESTINATION].result, media_destination_values,
                                       &non_gs_state->requested_media_details.media_destination,
                                       pclxl_context, PCLXL_SS_KERNEL) ) {
      return(FALSE);
    }
  }

  if ( match[BEGINPAGE_SIMPLEX_PAGE_MODE].result ) {
    /* SimplexPageMode */
    if ( match[BEGINPAGE_DUPLEX_PAGE_MODE].result || match[BEGINPAGE_DUPLEX_PAGE_SIDE].result ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_COMBINATION,
                          ("Found duplex attributes in addition to simplex"));
      return(FALSE);
    }

    if ( !pclxl_attr_match_enumeration(match[BEGINPAGE_SIMPLEX_PAGE_MODE].result,
                                       sim_page_side_values,
                                       &non_gs_state->requested_media_details.simplex_page_side,
                                       pclxl_context, PCLXL_SS_KERNEL) ) {
      return(FALSE);
    }

  } else if ( match[BEGINPAGE_DUPLEX_PAGE_MODE].result ) {
    /* DuplexPageMode */
    if ( !pclxl_attr_match_enumeration(match[BEGINPAGE_DUPLEX_PAGE_MODE].result, dup_page_side_values,
                                       &non_gs_state->requested_media_details.duplex_page_side,
                                       pclxl_context, PCLXL_SS_KERNEL) ) {
      return(FALSE);
    }

    if ( match[BEGINPAGE_DUPLEX_PAGE_SIDE].result ) {
      /* DuplexPageSide */
      if ( !pclxl_attr_match_enumeration(match[BEGINPAGE_DUPLEX_PAGE_SIDE].result, dup_binding_values,
                                         &non_gs_state->requested_media_details.duplex_binding,
                                         pclxl_context, PCLXL_SS_KERNEL) ) {
        return(FALSE);
      }
    }

  } else if ( match[BEGINPAGE_DUPLEX_PAGE_SIDE].result ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_MISSING_ATTRIBUTE,
                        ("Got duplex side without duplex page attribute"));
    return(FALSE);
  }

  non_gs_state->previous_media_details = non_gs_state->current_media_details;

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
              ("BeginPage(page_number = %d, orientation = %d, media_size = (%d,\"%s\",X=%f,Y=%f), media_type = \"%s\", media_source = %d, media_destination = %d, duplex = %s, simplex_page_side = %d, duplex_page_side = %d, duplex_binding = %d)",
               non_gs_state->page_number,
               non_gs_state->requested_media_details.orientation,
               non_gs_state->requested_media_details.media_size,
               (non_gs_state->requested_media_details.media_size_name ? non_gs_state->requested_media_details.media_size_name : (uint8*) "<NULL>"),
               non_gs_state->requested_media_details.media_size_xy.x,
               non_gs_state->requested_media_details.media_size_xy.y,
               non_gs_state->requested_media_details.media_type,
               non_gs_state->requested_media_details.media_source,
               non_gs_state->requested_media_details.media_destination,
               (non_gs_state->requested_media_details.duplex ? "True" : "False"),
               non_gs_state->requested_media_details.simplex_page_side,
               non_gs_state->requested_media_details.duplex_page_side,
               non_gs_state->requested_media_details.duplex_binding));

  if ( !pclxl_setup_page_device(pclxl_context,
                                &non_gs_state->previous_media_details,  /* i.e. existing/current media details */
                                &non_gs_state->requested_media_details,
                                &non_gs_state->current_media_details) ) {
    return FALSE;
  }

  /*
   * Note that we re-evaluate the current graphics state
   * just in case pclxl_setup_page_device() has "pushed" another one onto the stack
   * (which it almost certainly will have)
   */

  graphics_state = pclxl_context->graphics_state;

  if ( !pclxl_set_default_ctm(pclxl_context, graphics_state, non_gs_state) ) {
    (void) pclxl_pop_gs(pclxl_context, FALSE);
    return FALSE;
  }

  if ( !pclxl_set_default_graphics_state(pclxl_context, graphics_state) ) {
    (void) pclxl_pop_gs(pclxl_context, FALSE);
    return FALSE;
  }

  if ( !pclxl_set_page_clip(pclxl_context) ) {
    (void) pclxl_pop_gs(pclxl_context, FALSE);
    return FALSE;
  }

#ifdef DEBUG_BUILD

  if ( pclxl_context->config_params.debug_pclxl & PCLXL_DEBUG_MM_GRID_OUTPUT )
    (void) pclxl_mm_graph_paper(pclxl_context);

#endif

  return TRUE;
}


/*
 * Tag 0x44 EndPage
 *
 * The drawing operations for this page are complete
 * and it is ready to be output
 * (with the number of copies specified in an attribute value associated with this PCLXL operator)
 *
 * Once the page has been output
 * we can pop the parser context
 * and the associated graphics state
 */

Bool
pclxl_op_end_page(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define ENDPAGE_PAGE_COPIES   (0)
    {PCLXL_AT_PageCopies},
    PCLXL_MATCH_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* PageCopies */
  non_gs_state->page_copies = pclxl_context->config_params.default_page_copies;
  if ( match[ENDPAGE_PAGE_COPIES].result ) {
    non_gs_state->page_copies = pclxl_attr_get_int(match[ENDPAGE_PAGE_COPIES].result);
  }

  pclxl_pattern_end_page(pclxl_context);

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
              ("EndPage(page_number = %d, page_copies = %d)", non_gs_state->page_number, non_gs_state->page_copies));

  /*
   * We need to clear-down any job-specified additional graphics states
   * at the end of the page.
   *
   * And note that we must do this *before* we call pclxl_throw_page()
   * because this will necessarily do a pclxl_pop_gs() followed later
   * by a pclxl_push_gs() to effect a Postscript "restore" and then "save"
   *
   * So we must already have cleared-down to the correct graphics state level
   * before this.
   */

  while ( ((graphics_state = pclxl_context->graphics_state) != NULL) &&
          (graphics_state->postscript_op < PCLXL_PS_SAVE_RESTORE) )
  {
    if ( !pclxl_pop_gs(pclxl_context, TRUE) )
    {
      return FALSE;
    }
  }

  if ( !pclxl_throw_page(pclxl_context,
                         &non_gs_state->current_media_details,
                         non_gs_state->page_copies,
                         /*
                          * Note that by passing FALSE here
                          * we are telling pclxl_throw_page()
                          * that it's "restore" is the one
                          * that matches the one in BeginPage
                          * and so no balancing "save" is required
                          */
                         FALSE) )
  {
    return FALSE;
  }
  else
  {
    non_gs_state->page_number++;
  }

  /* Rendering is complete; pattern caches can be purged. */

  pclxl_pattern_rendering_complete(pclxl_context);

  return TRUE;
}

/*
 * Tag 0x74 SetPageDefaultCTM
 *
 * This simply replaces the current CTM (in the current graphics state)
 * with the logical page CTM that was cached when the page was set up
 *
 * Note that according to the PCLXL spec. it explicitly does not
 * have any affect on the current *character* CTM
 */

Bool
pclxl_op_set_page_default_CTM(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;

  if ( !pclxl_attr_set_match_empty(parser_context->attr_set, pclxl_context,
                                   PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
              ("SetPageDefaultCTM()"));

  graphics_state->page_angle = 0;
  graphics_state->page_scale.x = graphics_state->page_scale.y = 1.0;

  return pclxl_set_current_ctm(graphics_state, &graphics_state->logical_page_ctm);
}

/*
 * Tag 0x75 SetPageOrigin
 *
 * SetPageOrigin is passed an (x,y) point in the current user coordinates
 * and performs a Postscript "translate" to relocate the current page origin
 *
 * If the "translate" is successful, then the new (Postscript) CTM
 * is retrieved and stored in the current graphics state.
 *
 * Note that we *may* have to switch back to the current CTM
 * before doing the translate if the current *character* CTM is in force
 *
 * However I currently envisage that the *character* CTM will only be in force
 * for the brief duration of actually doing character/text operations
 */

Bool
pclxl_op_set_page_origin(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define SETPAGEORIGIN_PAGE_ORIGIN   (0)
    {PCLXL_AT_PageOrigin | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  OMATRIX existing_ctm;
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_SysVal_XY user_coord;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* PageOrigin */
  pclxl_attr_get_real_xy(match[SETPAGEORIGIN_PAGE_ORIGIN].result, &user_coord);

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
              ("SetPageOrigin(%f,%f)", user_coord.x, user_coord.y));

  /* We must attempt to translate the page origin to this new position  and if
   * successful we must record the new current CTM
   */
  if ( !pclxl_ps_translate(pclxl_context, user_coord.x, user_coord.y) ) {
    return FALSE;
  }

  existing_ctm = graphics_state->current_ctm;

#ifdef DEBUG_BUILD

  if ( pclxl_context->config_params.debug_pclxl & PCLXL_DEBUG_PAGE_CTM )
    (void) pclxl_debug_origin(pclxl_context);

#endif

  /* I am not sure whether we need to recalculate and update the current
   * *character* CTM here also?  If it is to be updated, then we supply its
   * location Otherwise we supply a NULL location
   */
  (void) pclxl_get_current_ctm(pclxl_context,
                               &graphics_state->current_ctm,
                               &graphics_state->ctm_is_invertible);

  /* It appears that we must also re-evaluate the current position (if any) in
   * terms of this new CTM
   *
   * To derive a new user-coordinate under this new CTM that is equivalent to
   * the existing user-coordinate under the previous CTM
   */
  return(!graphics_state->current_point ||
         pclxl_reset_current_point(pclxl_context,
                                   &graphics_state->current_point_xy,
                                   &existing_ctm,
                                   &graphics_state->current_ctm,
                                   &graphics_state->current_point_xy));
}

/*
 * Tag 0x76 SetPageRotation
 */

Bool
pclxl_op_set_page_rotation(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define SETPAGEROTATION_PAGE_ANGLE    (0)
    {PCLXL_AT_PageAngle | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_Int32 rotation_angle;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* PageAngle */
  rotation_angle = pclxl_attr_get_int(match[SETPAGEROTATION_PAGE_ANGLE].result);
  /* Not documented but QL CET E102.bin raises an error */
  if ( (rotation_angle < -360) || (rotation_angle > 360) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_STATE, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                        ("Rotation angle must be in range [-360,360]"));
    return(FALSE);
  }

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
              ("SetPageRotation(%d)", rotation_angle));

  return(pclxl_set_page_rotation(pclxl_context, rotation_angle));
}


/*
 * Tag 0x77 SetPageScale
 */

Bool
pclxl_op_set_page_scale(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[4] = {
#define SETPAGESCALE_PAGE_SCALE   (0)
    {PCLXL_AT_PageScale},
#define SETPAGESCALE_MEASURE      (1)
    {PCLXL_AT_Measure},
#define SETPAGESCALE_UNITS_PER_MEASURE (2)
    {PCLXL_AT_UnitsPerMeasure},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION measure_values[] = {
    PCLXL_eInch,
    PCLXL_eMillimeter,
    PCLXL_eTenthsOfAMillimeter,
    PCLXL_ENUMERATION_END
  };
  PCLXL_SysVal_XY page_scale;
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_NON_GS_STATE non_gs_state = &parser_context->pclxl_context->non_gs_state;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match_at_least_1(parser_context->attr_set, match,
                                        pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* PageScale */
  if ( match[SETPAGESCALE_PAGE_SCALE].result ) {
    if ( match[SETPAGESCALE_MEASURE].result ||
         match[SETPAGESCALE_UNITS_PER_MEASURE].result ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_COMBINATION,
                          ("Must supply either PageScale OR Measure + UnitsPerMeasure but not both"));
      return(FALSE);
    }

    /* PageScale */
    pclxl_attr_get_real_xy(match[SETPAGESCALE_PAGE_SCALE].result, &page_scale);

#define MAX_PAGE_SCALE  (32767.0)
    if ( (page_scale.x < 0) || (page_scale.x > MAX_PAGE_SCALE) ||
         (page_scale.y < 0) || (page_scale.y > MAX_PAGE_SCALE) ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                          ("Illegal PageScale Attribute Value (%f,%f) (both must be in the range 0 to %f)",
                           page_scale.x, page_scale.y, MAX_PAGE_SCALE));
      return FALSE;
    }

    PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
                ("SetPageScale(x = %f, y = %f)", page_scale.x, page_scale.y));

  } else {
    PCLXL_Measure measure;
    PCLXL_UnitsPerMeasure units_per_measure;
    PCLXL_SysVal uom_old;
    PCLXL_SysVal uom_new;

    if ( !match[SETPAGESCALE_MEASURE].result || !match[SETPAGESCALE_UNITS_PER_MEASURE].result ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_MISSING_ATTRIBUTE,
                          ("Found only one of Measure and UnitsPerMeasure"));
      return(FALSE);
    }

    /* Measure */
    if ( !pclxl_attr_match_enumeration(match[SETPAGESCALE_MEASURE].result, measure_values,
                                       &measure, pclxl_context, PCLXL_SS_KERNEL) ) {
      return(FALSE);
    }

    /* UnitsPerMeasure */
    if ( match[SETPAGESCALE_UNITS_PER_MEASURE].result->data_type == PCLXL_DT_UInt16_XY ) {
      units_per_measure.res_x = match[SETPAGESCALE_UNITS_PER_MEASURE].result->value.v_uint16s[0];
      units_per_measure.res_y = match[SETPAGESCALE_UNITS_PER_MEASURE].result->value.v_uint16s[1];
    } else {
      units_per_measure.res_x = match[SETPAGESCALE_UNITS_PER_MEASURE].result->value.v_real32s[0];
      units_per_measure.res_y = match[SETPAGESCALE_UNITS_PER_MEASURE].result->value.v_real32s[1];
    }

    if ( (units_per_measure.res_x <= 0) ||
         (units_per_measure.res_y <= 0) ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                          ("Illegal Attribute Value"));
      return(FALSE);
    }

    uom_old = pclxl_ps_units_per_pclxl_uom(non_gs_state->measurement_unit);
    uom_new = pclxl_ps_units_per_pclxl_uom(measure);
    page_scale.x = (non_gs_state->units_per_measure.res_x*uom_new)/(units_per_measure.res_x*uom_old);
    page_scale.y = (non_gs_state->units_per_measure.res_y*uom_new)/(units_per_measure.res_y*uom_old);

    PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
                ("SetPageScale(Measure = %d, UnitsPerMeasure = (%f,%f)) => (x = %g, y = %g)",
                 measure, units_per_measure.res_x, units_per_measure.res_y,
                 page_scale.x, page_scale.y));
  }

  return pclxl_set_page_scale(pclxl_context, &page_scale);
}

/******************************************************************************
* Log stripped */
