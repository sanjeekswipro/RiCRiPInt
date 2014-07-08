/** \file
 * \ingroup fixedpage
 *
 * $HopeName: COREedoc!fixedpage:src:path.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of XPS fixed payload path callbacks.
 *
 * See path_functions declaration at the end of this file for the element
 * callbacks this file implements.
 */

#include "core.h"

#include "objects.h"
#include "namedef_.h"
#include "swerrors.h"
#include "graphics.h"
#include "gstate.h"
#include "tranState.h"
#include "pathcons.h"
#include "clipops.h"
#include "gu_fills.h"
#include "gu_ctm.h"
#include "xml.h"
#include "xmltypeconv.h"
#include "pathops.h"
#include "gu_path.h"
#include "system.h"
#include "swmemory.h"
#include "objnamer.h"
#include "render.h"
#include "gschead.h"
#include "display.h"
#include "rectops.h"
#include "pattern.h"
#include "vndetect.h"
#include "params.h"

#include "xpspriv.h"
#include "xpsscan.h"
#include "fixedpagepriv.h"

#define PATH_STATE_NAME "XPS Path"

#define NO_FILL_TYPE -1 /* Not a valid fill type; used to override FillRule */

/** \brief Structure to contain Path state. */
typedef struct xpsPathState_s {
  xpsCallbackStateBase base;

  int32 gstate_id; /**< gsave id if saved. */
  int32 fill_rule; /**< fill rule for paths. Defaults to EOFILL. */
  int32 clip_rule; /**< clip rule for paths. Defaults to EOFILL. */
  int32 paint_flags; /**< Current paint style (one of XPS_PAINT_*). */

  /** Abbreviated paths are scanned in the commit after transform matrix is known. */
  utf8_buffer abbreviated_clip ;
  utf8_buffer abbreviated_path ;

  Bool use_datafill; /**< TRUE indicates datafill should be used for filling,
                          otherwise the fill and stroke are the same and data
                          can be used for stroking. */
  PATHINFO data ;    /**< For fill and stroke if they are the same, or just stroke. */
  PATHINFO datafill ; /**< For fill only, if fill and stroke are different. */
  PATHINFO clip ; /**< Path for Clip. */

  Bool transform_invertible ; /**< RenderTransform is invertible; otherwise Path will be transparent. */
  OMATRIX transform ; /**< Transform matrix for Clip, Data, Fill, Stroke. */
  OMATRIX savedctm ;  /**< Copy of CTM before modification. */

  int32 gstate_mask_id; /**< gsave id for the opacity mask if saved. */
  Group* mask_group; /**< Group object for the opacity mask. */

  USERVALUE saved_opacity ; /** Manually save/restore opacity for efficiency. */

  /**< Stroke and fill opacity from a path brush.  They need to be kept
     separate from the gstate alphas to be able to handle the implicit group
     in dostrokefill according to XPS rules. */
  USERVALUE stroke_brush_opacity;
  USERVALUE fill_brush_opacity;

  Bool pattern_ok ;  /**< Are we in a state where it's OK to accept a pattern? */

  OBJECT_NAME_MEMBER
} xpsPathState ;

/* Extract the xps context and/or XPS state, asserting that the right
   objects have been found. */
static inline Bool xps_path_state(
  /*@notnull@*/ /*@in@*/           xmlGFilter *filter,
  /*@null@*/ /*@out@*/             xmlDocumentContext **xps_ptr,
  /*@notnull@*/ /*@out@*/          xpsPathState **state_ptr)
{
  xmlDocumentContext *xps_ctxt;
  xpsPathState *state ;

  HQASSERT(filter != NULL, "filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");

  if ( xps_ptr != NULL )
    *xps_ptr = xps_ctxt ;

  state = (xpsPathState*)xps_ctxt->callback_state;
  if ( state == NULL || state->base.type != XPS_STATE_PATH )
    return FALSE ;

  VERIFY_OBJECT(state, PATH_STATE_NAME) ;

  *state_ptr = state ;

  return TRUE ;
}

/** Are we in a path, and if so is it OK to expand a pattern? */
Bool path_pattern_valid(xpsCallbackState *state)
{
  xpsPathState *path_state = (xpsPathState*)state ;
  HQASSERT(state != NULL, "No state") ;

  return (path_state->base.type == XPS_STATE_PATH &&
          path_state->pattern_ok) ;
}

/* Add a gsave only if necessary. */
static Bool bracket_path(xpsPathState *state)
{
  VERIFY_OBJECT(state, PATH_STATE_NAME) ;

  if ( state->gstate_id == GS_INVALID_GID ) {
    if (! gs_gpush(GST_GSAVE))
      return FALSE;

    state->gstate_id = gstackptr->gId ;
  }

  return TRUE ;
}

static Bool apply_render_transform(xpsPathState *state)
{
  OMATRIX transform, newctm ;

  /* If there has been no modification of the CTM prior to this, and the
     new matrix won't do anything, we can quit early. */
  if ( MATRIX_EQ(&state->transform, &identity_matrix) &&
       MATRIX_EQ(&state->savedctm, &thegsPageCTM(*gstateptr)) )
    return TRUE ;

  /* We need to modify the CTM, even though we will transform the path
     explicitly, because we may be stroking and the linewidth is affected
     by the transform. Also, the context for Fill and Stroke brushes
     is affected by the RenderTransform. */
  matrix_mult(&state->transform, &state->savedctm, &newctm) ;

  /* The new local transformation matrix is the same as the old local
     transformation matrix, so there is nothing to do here. Move along. */
  if ( MATRIX_EQ(&newctm, &thegsPageCTM(*gstateptr)) )
    return TRUE ;

  /* Are the RenderTransform and CTM invertible?  If not, the Path must be
     treated as completely transparent. */
  if ( !matrix_inverse(&state->transform, &transform) ||
       !matrix_inverse(&thegsPageCTM(*gstateptr), &transform) ) {
    state->transform_invertible = FALSE ;
    return TRUE ;
  }

  /* We need to generate a transform that undoes the CTM, adds the
     RenderTransform to the original CTM, and then re-does the CTM.
     Invertible matrices should not cause error, but whether a degenerate
     path or no path is displayed is implementation-dependent. */
  matrix_mult(&transform, &newctm, &transform) ;

  /* Should always delay path creation until after transform matrix has been
     seen; Path code needs to convert to device space to handle cases like
     omitting a segment when the two points coincide. Calling path_transform
     after seeing a transform won't give the same results as delaying
     creating the path. */
  HQASSERT(!state->clip.firstpath, "Can't transform clip path after changing transform matrix") ;
  HQASSERT(!state->data.firstpath, "Can't transform path after changing transform matrix") ;
  HQASSERT(!state->datafill.firstpath, "Can't transform fill path after changing transform matrix") ;

  if ((state->paint_flags & XPS_PAINT_FILL) != 0) {
    /* Modify fill pattern space matrix by transform. */
    if ( !pattern_matrix_remake(GSC_FILL, &state->transform, FALSE) )
      return FALSE ;
  }

  if ((state->paint_flags & XPS_PAINT_STROKE) != 0) {
    /* Modify stroke pattern space matrix by transform. */
    if ( !pattern_matrix_remake(GSC_STROKE, &state->transform, FALSE) )
      return FALSE ;
  }

  return gs_setctm(&newctm, FALSE) ;
}

/*=============================================================================
 * XML start/end element callbacks
 *=============================================================================
 */

static
Bool xps_Path_Commit(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsPathState *state = NULL;
  Bool result = TRUE ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;
  UNUSED_PARAM( xmlGAttributes *, attrs ) ;

  if ( !SystemParams.XPS )
    return error_handler(INVALIDACCESS);

  if ( !xps_path_state(filter, &xps_ctxt, &state) )
    return error_handler(UNREGISTERED) ;

  /* Should always delay path creation until after transform matrix has been
     seen; Path code needs to convert to device space to handle cases like
     omitting a segment when the two points coincide. Calling path_transform
     after seeing a transform won't give the same results as delaying
     creating the path.  The attribute string is guaranteed to be around
     until after the commit. */
  if ( state->abbreviated_path.unitlength > 0 ) {
    static PATHINFO path = PATHINFO_STATIC(NULL,NULL,NULL) ;
    static xps_path_designator path_designator = {
      XML_INTERN(Path_Data), &path, NO_FILL_TYPE
    } ;
    path_init(path_designator.path);
    path_designator.fillrule = NO_FILL_TYPE ; /* Not a valid fill rule */
    if ( !xps_convert_ST_AbbrGeomF(filter, NULL, &state->abbreviated_path, &path_designator) )
      return xmlg_attributes_invoke_match_error(filter, localname, /* We do not track prefix */ NULL,
                                                NULL, XML_INTERN(Data)) ;
    state->data = *path_designator.path;
    state->fill_rule = path_designator.fillrule;
  }

  if ( state->abbreviated_clip.unitlength > 0 ) {
    static PATHINFO clip = PATHINFO_STATIC(NULL,NULL,NULL) ;
    static xps_path_designator clip_designator = {
      XML_INTERN(Path_Clip), &clip, EOFILL_TYPE
    } ;
    path_init(clip_designator.path);
    clip_designator.fillrule = EOFILL_TYPE ;
    if ( !xps_convert_ST_AbbrGeomF(filter, NULL, &state->abbreviated_clip, &clip_designator) )
      return xmlg_attributes_invoke_match_error(filter, localname, /* We do not track prefix */ NULL,
                                                NULL, XML_INTERN(Clip)) ;
    state->clip = *clip_designator.path;
    state->clip_rule = clip_designator.fillrule;
  }

  /* Set default fill rule if not already set */
  if ( state->fill_rule == NO_FILL_TYPE )
    state->fill_rule = EOFILL_TYPE ;

  /* Metro does not stroke lines with zero width. */
  if ( gstateptr->thestyle.linewidth == 0.0f )
    state->paint_flags &= ~XPS_PAINT_STROKE ;

  if ( state->clip.firstpath != NULL ) {
    if ( !bracket_path(state) ||
         !gs_addclip(state->clip_rule, &state->clip, FALSE) )
      return FALSE ;
  }

  state->pattern_ok = TRUE ; /* Can now accept child pattern callbacks */

  /* Set flag to indicate path comes from XPS and therefore slightly
     different stroking rules apply.  May as well set it on the fill path as
     well, although currently not used for anything. */
  state->data.flags |= PATHINFO_XPS ;
  if ( state->use_datafill )
    state->datafill.flags |= PATHINFO_XPS ;

  /* Avoid creating an implicit group for fill and stroke if either turned out to be empty. */
  if ( (state->paint_flags & (XPS_PAINT_FILL|XPS_PAINT_STROKE)) == (XPS_PAINT_FILL|XPS_PAINT_STROKE) ) {
    if ( !(state->data).lastline ) {
      state->paint_flags &= ~XPS_PAINT_STROKE;
      if ( !state->use_datafill )
        /* fill and stroke are the same, and both use state->data */
        state->paint_flags &= ~XPS_PAINT_FILL;
    }
    if ( state->use_datafill && !(state->datafill).lastline ) {
      state->paint_flags &= ~XPS_PAINT_FILL;
    }
    /* Combined stroke and fill always causes a flush vignette.  Need to
       explicit call it here in case we end up doing the stroke and fill
       separately (becuase one of the paths happens to be empty). */
    if ( !flush_vignette(VD_Default) )
      return FALSE;
  }

  if ( (state->paint_flags & (XPS_PAINT_FILL|XPS_PAINT_STROKE)) == (XPS_PAINT_FILL|XPS_PAINT_STROKE) ) {
    /* Requires both stroke and fill.  May need to create an implicit
       group if transparency is involved. */

    /* It's too complicated to handle the fill and image brush as a clipped
       image, in addition to a stroke.  So give up and set up the pattern
       colorspace for the imagebrush. */
    if ( drawing_direct_image(xps_ctxt) )
      result = xps_draw_direct_image(xps_ctxt, FALSE);

    result = result &&
             dostrokefill(&state->data, &state->datafill, state->use_datafill,
                          state->stroke_brush_opacity, state->fill_brush_opacity,
                          IMPLICIT_GROUP_XPS,
                          FALSE /* closepath */, state->fill_rule) ;
  }
  else if ((state->paint_flags & XPS_PAINT_FILL) != 0) {
    /* Fill the path. */
    PATHINFO *fpath;
    USERVALUE alpha;

    alpha = state->fill_brush_opacity * tsConstantAlpha(gsTranState(gstateptr), GSC_FILL);
    tsSetConstantAlpha(gsTranState(gstateptr), GSC_FILL,
                       alpha, gstateptr->colorInfo);

    /* Use the datafill fill path if present, otherwise the fill and
       stroke paths are the same. */
    fpath = ( state->use_datafill ? &state->datafill : &state->data );

    if ( drawing_direct_image(xps_ctxt) ) {
      /* Replace the fill and image brush with a clipped image. */
      result = gs_cpush(); /* clipsave */
      if ( result ) {
        result = ( gs_addclip(state->fill_rule, fpath, TRUE) &&
                   xps_draw_direct_image(xps_ctxt, TRUE) );

        result = gs_ctop() && result; /* cliprestore */
      }
    } else {
      result = dofill(fpath, state->fill_rule, GSC_FILL, FILL_NORMAL);
    }
  }
  else if ((state->paint_flags & XPS_PAINT_STROKE) != 0) {
    /* Stroke the path. */
    STROKE_PARAMS sparams;
    USERVALUE alpha;

    HQASSERT(!drawing_direct_image(xps_ctxt),
             "Only expect a candidate direct image for a fill");

    alpha = state->stroke_brush_opacity * tsConstantAlpha(gsTranState(gstateptr), GSC_STROKE);
    tsSetConstantAlpha(gsTranState(gstateptr), GSC_STROKE,
                       alpha, gstateptr->colorInfo);

    set_gstate_stroke(&sparams, &state->data, NULL, FALSE);

    result = dostroke(&sparams, GSC_STROKE, STROKE_NORMAL) ;
  }

  state->pattern_ok = FALSE ;

  return result ;
}

static Bool xps_Path_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsPathState *state ;
  Bool result = FALSE ;
  uint8 dashcap_value ;

  static Bool opacity_set, dasharray_set, startcap_set, dashcap_set, endcap_set, linejoin_set,
    dashoffset_set, miterlimit_set, thickness_set, dummy, lang_set, snap_set, name_set ;
  static LINESTYLE linestyle;
  static xmlGIStr *startcap, *dashcap, *endcap, *linejoin, *lang ;
  static Bool snap ;
  static utf8_buffer name, automation_name, automation_helptext ;
  static USERVALUE opacity, dashoffset, miterlimit, thickness ;
  static xps_abbrevgeom_designator abbreviated_clip = { XML_INTERN(Path_Clip) } ;
  static xps_abbrevgeom_designator abbreviated_path = { XML_INTERN(Path_Data) } ;
  static OMATRIX matrix;
  static xps_matrix_designator matrix_designator = { XML_INTERN(Path_RenderTransform), &matrix };
  static xps_color_designator fill_designator  = { XML_INTERN(Path_Fill), FALSE };
  static xps_color_designator stroke_designator =  { XML_INTERN(Path_Stroke), FALSE };

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(Path_RenderTransform), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(Path_Clip), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(Path_OpacityMask), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(Path_Fill), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(Path_Stroke), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(Path_Data), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(Visual), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP},
    { XML_INTERN(ImmediateDiscard), XML_INTERN(ns_ggs_xps_2007_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Opacity), NULL, &opacity_set, xps_convert_fl_ST_ZeroOne, &opacity },
    { XML_INTERN(StrokeDashArray), NULL, &dasharray_set, xps_convert_StrokeDashArray, &linestyle },
    { XML_INTERN(StrokeDashCap), NULL, &dashcap_set, xps_convert_ST_DashCap, &dashcap },
    { XML_INTERN(StrokeDashOffset), NULL, &dashoffset_set, xps_convert_fl_ST_Double, &dashoffset },
    { XML_INTERN(StrokeEndLineCap), NULL, &endcap_set, xps_convert_ST_DashCap, &endcap },
    { XML_INTERN(StrokeLineJoin), NULL, &linejoin_set, xps_convert_ST_LineJoin, &linejoin },
    { XML_INTERN(StrokeMiterLimit), NULL, &miterlimit_set, xps_convert_fl_ST_GEOne, &miterlimit },
    { XML_INTERN(StrokeStartLineCap), NULL, &startcap_set, xps_convert_ST_DashCap, &startcap },
    { XML_INTERN(StrokeThickness), NULL, &thickness_set, xps_convert_fl_ST_GEZero, &thickness },
    { XML_INTERN(Name), NULL, &name_set, xps_convert_ST_Name, &name },
    { XML_INTERN(FixedPage_NavigateUri), NULL, &dummy, xps_convert_navigate_uri, NULL },
    { XML_INTERN(AutomationProperties_Name), NULL, &dummy, xps_convert_ST_UnicodeString, &automation_name },
    { XML_INTERN(AutomationProperties_HelpText), NULL, &dummy, xps_convert_ST_UnicodeString, &automation_helptext },
    { XML_INTERN(lang), XML_INTERN(ns_w3_xml_namespace), &lang_set, xml_convert_lang, &lang },
    { XML_INTERN(SnapsToDevicePixels), NULL, &snap_set, xps_convert_ST_Boolean, &snap },
    { XML_INTERN(Clip), NULL, &dummy, xps_convert_ST_RscRefAbbrGeom, &abbreviated_clip },
    { XML_INTERN(OpacityMask), NULL, &dummy, xps_convert_ST_RscRef, XML_INTERN(Path_OpacityMask) },
    { XML_INTERN(Fill), NULL, &dummy, xps_convert_ST_RscRefColor, &fill_designator },
    { XML_INTERN(Stroke), NULL, &dummy, xps_convert_ST_RscRefColor, &stroke_designator },
    { XML_INTERN(Data), NULL, &dummy, xps_convert_ST_RscRefAbbrGeom, &abbreviated_path },
    { XML_INTERN(RenderTransform), NULL, &dummy, xps_convert_ST_RscRefMatrix, &matrix_designator },
    XML_ATTRIBUTE_MATCH_END
  } ;

  static XPS_COMPLEXPROPERTYMATCH complex_properties[] = {
    { XML_INTERN(Path_RenderTransform), XML_INTERN(ns_xps_2005_06), XML_INTERN(RenderTransform), NULL, TRUE },
    { XML_INTERN(Path_Clip), XML_INTERN(ns_xps_2005_06), XML_INTERN(Clip), NULL, TRUE },
    { XML_INTERN(Path_OpacityMask), XML_INTERN(ns_xps_2005_06), XML_INTERN(OpacityMask), NULL, TRUE },
    /* Visual MUST appear immediately after Path_OpacityMask */
    { XML_INTERN(Visual), XML_INTERN(ns_xps_2005_06), NULL, NULL, TRUE },
    { XML_INTERN(Path_Fill), XML_INTERN(ns_xps_2005_06), XML_INTERN(Fill), NULL, TRUE },
    { XML_INTERN(Path_Stroke), XML_INTERN(ns_xps_2005_06), XML_INTERN(Stroke), NULL, TRUE },
    { XML_INTERN(Path_Data), XML_INTERN(ns_xps_2005_06), XML_INTERN(Data), NULL, TRUE },
    XPS_COMPLEXPROPERTYMATCH_END
  };

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(filter != NULL, "filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");

  dashoffset = 0.0f ;
  theDashListLen(linestyle) = 0 ;
  thickness = 1.0f ;
  abbreviated_path.attributebuffer.codeunits = NULL ;
  abbreviated_path.attributebuffer.unitlength = 0 ;
  abbreviated_clip.attributebuffer.codeunits = NULL ;
  abbreviated_clip.attributebuffer.unitlength = 0 ;
  MATRIX_COPY(matrix_designator.matrix, &identity_matrix);
  fill_designator.color_set = FALSE;
  fill_designator.color_profile_partname = NULL;
  stroke_designator.color_set = FALSE;
  stroke_designator.color_profile_partname = NULL;

  if (! xps_commit_register(filter, localname, uri, attrs, complex_properties,
                            xps_Path_Commit))
    return FALSE ;
  /* Must clean up minipath properly if used. */
#define return DO_NOT_RETURN_GO_TO_early_cleanup_INSTEAD!
/* Any stroke and fill opacity in the path's brush need to be kept
     separate from the gstate alphas to be able to handle the implicit group
     in dostrokefill (called from xps_Path_Commit) according to XPS rules.
     The Opacity attribute (which can be supplied in addition, and which
     applies to both stroke and fill) is included in the gstate alphas as
     normal. */
  HQASSERT(!xps_ctxt->capture_opacity, "capture_opacity should be false at this point");
  HQASSERT(xps_ctxt->stroke_brush_opacity == 1.0 &&
           xps_ctxt->fill_brush_opacity == 1.0, "stroke and fill xps_ctxt opacities not initialised properly");
  xps_ctxt->capture_opacity = TRUE;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE)) {
    (void) error_handler(UNDEFINED);
    goto early_cleanup ;
  }

  if (! xmlg_valid_children(filter, localname, uri, valid_children)) {
    (void) error_handler(UNDEFINED);
    goto early_cleanup ;
  }

  /* Make a new path state. */
  state = mm_alloc(mm_xml_pool, sizeof(xpsPathState),
                   MM_ALLOC_CLASS_XPS_CALLBACK_STATE) ;
  if (state == NULL) {
    result = error_handler(VMERROR);
    goto early_cleanup ;
  }

#undef return
#define return DO_NOT_RETURN_GO_TO_state_cleanup_INSTEAD!

  state->base.type = XPS_STATE_PATH;
  state->base.next = xps_ctxt->callback_state;
  state->gstate_id = GS_INVALID_GID;
  state->paint_flags = XPS_PAINT_NONE;
  state->abbreviated_path = abbreviated_path.attributebuffer ;
  state->use_datafill = FALSE;
  path_init(&state->data);
  path_init(&state->datafill); /* only required when fill and stroke paths differ */
  state->fill_rule = NO_FILL_TYPE ; /* Not a valid fill rule */
  state->abbreviated_clip = abbreviated_clip.attributebuffer ;
  path_init(&state->clip);
  state->clip_rule = EOFILL_TYPE ;
  state->transform_invertible = TRUE ;
  MATRIX_COPY(&state->transform, matrix_designator.matrix) ;
  MATRIX_COPY(&state->savedctm, &thegsPageCTM(*gstateptr)) ;
  state->stroke_brush_opacity = 1.0;
  state->fill_brush_opacity = 1.0;
  state->gstate_mask_id = GS_INVALID_GID;
  state->mask_group = NULL;
  state->saved_opacity = tsConstantAlpha(gsTranState(gstateptr), FALSE) ;
  HQASSERT(state->saved_opacity == tsConstantAlpha(gsTranState(gstateptr), TRUE),
           "Stroking and non-stroking opacity should be the same in XPS") ;
  state->pattern_ok = FALSE;
  NAME_OBJECT(state, PATH_STATE_NAME) ;

  if ( opacity_set ) {
    HQASSERT(xps_ctxt->colortype == GSC_UNDEFINED,
             "Do not expect colortype to be defined at this point");
    /* Set both stroke and fill opacity. */
    tsSetConstantAlpha(gsTranState(gstateptr), TRUE, opacity, gstateptr->colorInfo);
    tsSetConstantAlpha(gsTranState(gstateptr), FALSE, opacity, gstateptr->colorInfo);
  }

  gstateptr->thestyle.linewidth = thickness;

  /* We need to know the dashcap type and dash offset before performing
     minimum dash length adjustment of the dash array, so prepare these now.
     Don't put them in the gstate until we've had a chance to bracket it. */
  dashcap_value = BUTT_CAP; /* default dashlinecap */
  if ( dashcap_set ) {
    /* Square is intentionally omitted from dashlinecap */
    switch ( XML_INTERN_SWITCH(dashcap) ) {
    case XML_INTERN_CASE(Flat):
      dashcap_value = BUTT_CAP;
      break;
    case XML_INTERN_CASE(Round):
      dashcap_value = ROUND_CAP;
      break;
    case XML_INTERN_CASE(Square):
      dashcap_value = SQUARE_CAP;
      break;
    case XML_INTERN_CASE(Triangle):
      dashcap_value = TRIANGLE_CAP;
      break;
    default:
      HQFAIL("DashCap is incorrect.") ;
      goto state_cleanup;
    }
  }

  if ( dasharray_set ) {
    uint32 i;
    uint16 dashlistlen = 0 ;
    USERVALUE gapadjust = 0.0f ;

    if (! bracket_path(state) )
      goto state_cleanup;

    OCopy(gstateptr->thestyle.dashpattern, onull);
    for ( i = 0u; i < theDashListLen(linestyle); i += 2 ) {
      if ( dashcap_value == BUTT_CAP && theDashList(linestyle)[i] <= 0.0f ) {
        /* Degenerate dashes with Flat dashcap are not rendered (Render Rules
           2005-01-26.3). The dash gap needs to be added to the previous dash
           gap. */
        /** \todo @@@ TODO FIXME ajcd 2005-01-28: How close to zero should be
           considered "degenerate"? */
        if ( dashlistlen > 0 ) {
          theDashList(linestyle)[dashlistlen - 1] += theDashList(linestyle)[i+1] * thickness ;
        } else {
          gapadjust = (USERVALUE)theDashList(linestyle)[i+1] ;
          dashoffset += gapadjust ;
        }
        continue ;
      }

      theDashList(linestyle)[dashlistlen] = theDashList(linestyle)[i] * thickness;
      theDashList(linestyle)[dashlistlen + 1] = theDashList(linestyle)[i + 1] * thickness;
      dashlistlen += 2 ;
    }

    /* If we removed any initial degenerate dash segments, add their gaps to
       the final gap so the phase remains the same. */
    if ( dashlistlen > 0 )
      theDashList(linestyle)[dashlistlen - 1] += gapadjust * thickness ;

    if ( !gs_storedashlist(&gstateptr->thestyle, theDashList(linestyle), dashlistlen) )
      goto state_cleanup ;

    /* Flat dash cap and all stroked dashes of length zero means that the
       line is not drawn at all (according to Rendering Rules).  The easiest
       way to do this is to set line width to zero. */
    if ( dashcap_value == BUTT_CAP && dashlistlen == 0 )
      gstateptr->thestyle.linewidth = 0.0f;
  }

  gstateptr->thestyle.startlinecap = BUTT_CAP; /* default startlinecap */
  if ( startcap_set ) {
    switch ( XML_INTERN_SWITCH(startcap) ) {
    case XML_INTERN_CASE(Flat):
      gstateptr->thestyle.startlinecap = BUTT_CAP;
      break;
    case XML_INTERN_CASE(Round):
      gstateptr->thestyle.startlinecap = ROUND_CAP;
      break;
    case XML_INTERN_CASE(Square):
      gstateptr->thestyle.startlinecap = SQUARE_CAP;
      break;
    case XML_INTERN_CASE(Triangle):
      gstateptr->thestyle.startlinecap = TRIANGLE_CAP;
      break;
    default:
      HQFAIL("StartCap is incorrect.") ;
      goto state_cleanup;
    }
  }

  gstateptr->thestyle.dashlinecap = dashcap_value ;

  gstateptr->thestyle.endlinecap = BUTT_CAP; /* default endlinecap */
  if ( endcap_set ) {
    switch ( XML_INTERN_SWITCH(endcap) ) {
    case XML_INTERN_CASE(Flat):
      gstateptr->thestyle.endlinecap = BUTT_CAP;
      break;
    case XML_INTERN_CASE(Round):
      gstateptr->thestyle.endlinecap = ROUND_CAP;
      break;
    case XML_INTERN_CASE(Square):
      gstateptr->thestyle.endlinecap = SQUARE_CAP;
      break;
    case XML_INTERN_CASE(Triangle):
      gstateptr->thestyle.endlinecap = TRIANGLE_CAP;
      break;
    default:
      HQFAIL("EndCap is incorrect.") ;
      goto state_cleanup;
    }
  }

  gstateptr->thestyle.linejoin = MITERCLIP_JOIN ; /* default Miter */
  if ( linejoin_set ) {
    switch ( XML_INTERN_SWITCH(linejoin) ) {
    case XML_INTERN_CASE(Miter):
      gstateptr->thestyle.linejoin = MITERCLIP_JOIN;
      break ;
    case XML_INTERN_CASE(Bevel):
      gstateptr->thestyle.linejoin = BEVEL_JOIN;
      break ;
    case XML_INTERN_CASE(Round):
      gstateptr->thestyle.linejoin = ROUND_JOIN;
      break ;
    default:
      HQFAIL("LineJoin is incorrect.") ;
      goto state_cleanup;
    }
  }

  gstateptr->thestyle.dashoffset = dashoffset * thickness ;

  if ( miterlimit_set ) {
    /* Don't know what default is, so gsave/grestore to preserve it. */
    if (! bracket_path(state) )
      goto state_cleanup;

    gstateptr->thestyle.miterlimit = miterlimit;
  }

  /* Render transform may have been supplied as a matrix(...) attribute. */
  if (! apply_render_transform(state))
    goto state_cleanup;

  /* Fill attribute may be present, specifying color directly. */
  if ( fill_designator.color_set && state->transform_invertible ) {
    state->paint_flags |= XPS_PAINT_FILL;
    if ( !xps_setcolor(xps_ctxt, GSC_FILL, &fill_designator) )
      goto state_cleanup ;
    state->fill_brush_opacity = xps_ctxt->fill_brush_opacity;
  }

  /* Stroke attribute may be present, specifying color directly. */
  if ( stroke_designator.color_set && state->transform_invertible ) {
    state->paint_flags |= XPS_PAINT_STROKE;
    if ( !xps_setcolor(xps_ctxt, GSC_STROKE, &stroke_designator) )
      goto state_cleanup ;
    state->stroke_brush_opacity = xps_ctxt->stroke_brush_opacity;
  }

  /* Good completion; link the new path into the context. */
  xps_ctxt->callback_state = (xpsCallbackState*)state ;

  result = TRUE ;

 state_cleanup:
  if ( !result ) {
    VERIFY_OBJECT(state, PATH_STATE_NAME) ;
    if ( state->gstate_id != GS_INVALID_GID )
      (void)gs_cleargstates(state->gstate_id, GST_GSAVE, NULL);
    (void)gs_setctm(&state->savedctm, FALSE) ;
    UNNAME_OBJECT(state) ;
    mm_free(mm_xml_pool, state, sizeof(xpsPathState)) ;
  }

 early_cleanup:
  xps_ctxt->capture_opacity = FALSE;
  xps_ctxt->stroke_brush_opacity = 1.0;
  xps_ctxt->fill_brush_opacity = 1.0;
  (void)gs_storedashlist(&linestyle, NULL, 0);
  if (fill_designator.color_profile_partname != NULL)
    xps_partname_free(&fill_designator.color_profile_partname);
  if (stroke_designator.color_profile_partname != NULL)
    xps_partname_free(&stroke_designator.color_profile_partname);

#undef return
  return result;
}

static Bool xps_Path_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt ;
  xpsPathState *state = NULL;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  /* The rest of the cleanups rely on the state being present. */
  if ( !xps_path_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  if ( state->gstate_id != GS_INVALID_GID )
    if ( !gs_cleargstates(state->gstate_id, GST_GSAVE, NULL) )
      success = FALSE ;

  /* For efficiency, stroke and fill opacity values are restored manually.
     Opacity may be set before a bracket_path, so this must be done after the
     gs_cleargstates.  (May as well just set the values as it is as cheap as
     getting the values from the gstate and then testing on if they have
     changed.) */
  tsSetConstantAlpha(gsTranState(gstateptr), TRUE, state->saved_opacity, gstateptr->colorInfo);
  tsSetConstantAlpha(gsTranState(gstateptr), FALSE, state->saved_opacity, gstateptr->colorInfo);

  /* Changing just the transformation matrix doesn't require a gsave, restore
     it here in case it was changed. */
  if ( !gs_setctm(&state->savedctm, FALSE) )
    success = FALSE ;

  path_free_list(state->data.firstpath, mm_pool_temp) ;
  path_free_list(state->datafill.firstpath, mm_pool_temp) ;
  path_free_list(state->clip.firstpath, mm_pool_temp) ;
  xps_ctxt->callback_state = state->base.next ;
  UNNAME_OBJECT(state) ;
  mm_free(mm_xml_pool, state, sizeof(xpsPathState)) ;

  reset_direct_image(xps_ctxt) ;

  return success;
}

static Bool xps_Path_Fill_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsPathState *state ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(SolidColorBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(ImageBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(VisualBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(LinearGradientBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(RadialGradientBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    XMLG_VALID_CHILDREN_END
  } ;

  /* No attributes allowed. Also, no compatibility attributes are allowed on
     properties. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix );

  if (! xps_path_state(filter, &xps_ctxt, &state))
    return error_handler(UNREGISTERED) ;

  xps_ctxt->capture_opacity = TRUE ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  /* Set up the current color type, so that opacity et. al. apply to the
     right place. */
  xps_ctxt->colortype = GSC_FILL ;

  /* Allow the option of converting a path with an untiled image brush to be
     converted into a clipped image. */
  allow_direct_image(xps_ctxt) ;

  return TRUE; /* keep on parsing */
}

static Bool xps_Path_Fill_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt;
  xpsPathState *state = NULL;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  if ( !xps_path_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  HQASSERT(xps_ctxt->colortype == GSC_UNDEFINED ||
           xps_ctxt->colortype == GSC_FILL,
           "Color type is not correct at Path.Fill end") ;

  if ( xps_ctxt->colortype == GSC_FILL && state->transform_invertible ) {
    /* We're going to fill the subsequent path. */
    state->paint_flags |= XPS_PAINT_FILL;
  }

  /* Reset the current color type */
  xps_ctxt->colortype = GSC_UNDEFINED ;

  state->fill_brush_opacity = xps_ctxt->fill_brush_opacity ;
  xps_ctxt->capture_opacity = FALSE ;
  xps_ctxt->fill_brush_opacity = 1.0 ;

  disallow_direct_image(xps_ctxt) ;

  return success;
}

static Bool xps_Path_Stroke_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsPathState *state ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(SolidColorBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(ImageBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(VisualBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(LinearGradientBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(RadialGradientBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    XMLG_VALID_CHILDREN_END
  } ;

  /* No attributes allowed. Also, no compatibility attributes are allowed on
     properties. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix );

  if (! xps_path_state(filter, &xps_ctxt, &state))
    return error_handler(UNREGISTERED) ;

  xps_ctxt->capture_opacity = TRUE ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  /* Set up the current color type, so that opacity et. al. apply to the
     right place. */
  xps_ctxt->colortype = GSC_STROKE ;

  return TRUE; /* keep on parsing */
}

static Bool xps_Path_Stroke_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt;
  xpsPathState *state = NULL;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  if ( !xps_path_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  HQASSERT(xps_ctxt->colortype == GSC_UNDEFINED ||
           xps_ctxt->colortype == GSC_STROKE,
           "Color type is not correct at Path.Stroke end") ;

  if ( xps_ctxt->colortype == GSC_STROKE && state->transform_invertible ) {
    /* We're going to stroke the subsequent path. */
    state->paint_flags |= XPS_PAINT_STROKE;
  }

  /* Reset the current color type */
  xps_ctxt->colortype = GSC_UNDEFINED ;

  state->stroke_brush_opacity = xps_ctxt->stroke_brush_opacity ;
  xps_ctxt->capture_opacity = FALSE ;
  xps_ctxt->stroke_brush_opacity = 1.0 ;

  return success;
}

static Bool xps_Path_Clip_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt ;
  xpsPathState *state ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(PathGeometry), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    XMLG_VALID_CHILDREN_END
  } ;

  /* No attributes allowed. Also, no compatibility attributes are allowed on
     properties. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  if (! xps_path_state(filter, &xps_ctxt, &state))
    return error_handler(UNREGISTERED) ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  HQASSERT(xps_ctxt->ignore_isstroked, "ignore_isstroked should be true");
  HQASSERT(!xps_ctxt->use_pathfill, "use_pathfill should be false");
  HQASSERT(xps_ctxt->path.firstpath == NULL, "path should be empty");
  HQASSERT(xps_ctxt->pathfill.firstpath == NULL, "pathfill should be empty");

  return TRUE; /* keep on parsing */
}

static Bool xps_Path_Clip_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt ;
  xpsPathState *state = NULL;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  /* The rest of the cleanups rely on the state being present. */
  if ( !xps_path_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  /* Steal constructed path from xps context and put into path state. This may
     later be transformed by the RenderTransform and applied as a clip. */
  path_free_list(state->clip.firstpath, mm_pool_temp) ;
  if ( xps_ctxt->use_pathfill ) {
    state->clip = xps_ctxt->pathfill ;
    path_init(&xps_ctxt->pathfill) ;
    xps_ctxt->use_pathfill = FALSE ;
    path_free_list(xps_ctxt->path.firstpath, mm_pool_temp) ;
    path_init(&xps_ctxt->path) ;
  } else {
    state->clip = xps_ctxt->path ;
    path_init(&xps_ctxt->path) ;
  }

  HQASSERT(!xps_ctxt->use_pathfill, "use_pathfill should still be false");
  HQASSERT(xps_ctxt->path.firstpath == NULL, "path should be empty");
  HQASSERT(xps_ctxt->pathfill.firstpath == NULL, "pathfill should be empty");

  state->clip_rule = xps_ctxt->fill_rule ;

  return success;
}

static Bool xps_Path_RenderTransform_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsPathState *state = NULL;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(MatrixTransform), XML_INTERN(ns_xps_2005_06), XMLG_ONE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  /* No attributes allowed. Also, no compatibility attributes are allowed on
     properties. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  if (! xps_path_state(filter, &xps_ctxt, &state))
    return error_handler(UNREGISTERED) ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  xps_ctxt->transform = &state->transform ;

  return TRUE; /* keep on parsing */
}

static Bool xps_Path_RenderTransform_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt;
  xpsPathState *state = NULL;

  UNUSED_PARAM( xmlGFilter* , filter ) ;
  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  /* MatrixTransform should have captured the matrix */
  if ( !xps_path_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  if ( xps_ctxt->transform != NULL )
    success = (success && detail_error_handler(SYNTAXERROR,
                                               "Required MatrixTransform element is missing.")) ;

  xps_ctxt->transform = NULL ;

  if ( success )
    success = apply_render_transform(state) ;

  return success;
}

static Bool xps_Path_OpacityMask_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  DL_STATE *page = get_core_context_interp()->page ;
  xmlDocumentContext *xps_ctxt;
  xpsPathState *state = NULL;

  OBJECT colorSpace = OBJECT_NOTVM_NOTHING;
  COLORSPACE_ID dummyspace_id;
  int32 name_id;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(SolidColorBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(ImageBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(VisualBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(LinearGradientBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(RadialGradientBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(Visual), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  /* No attributes allowed. Also, no compatibility attributes are allowed on
     properties. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  if (! xps_path_state(filter, &xps_ctxt, &state))
    return error_handler(UNREGISTERED) ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  /* Will need to undo the setting of the mask at the end of Path. */
  if (! bracket_path(state) )
    return FALSE;

  /* The opacity mask colorspace is the same as the VirtualDeviceSpace */
  dlVirtualDeviceSpace(page, &name_id, &dummyspace_id);

  object_store_name(&colorSpace, name_id, LITERAL) ;

  /* An additional gsave is required around opacity mask creation in case
     Path.Fill and Path.Data have already been done. */
  if (gs_gpush(GST_GROUP)) {
    int32 gid = gstackptr->gId ;

    if ( groupOpen(page, colorSpace, TRUE /* isolated */, FALSE /* knockout */,
                   TRUE /* banded */, NULL /* bgcolor */, NULL /* xferfn */,
                   NULL /* patternTA */, GroupAlphaSoftMask, &state->mask_group) ) {
      if (gs_gpush(GST_GSAVE)) {
        xps_ctxt->colortype = GSC_FILL;
        state->gstate_mask_id = gid ;
        return TRUE ;
      }
      (void)groupClose(&state->mask_group, FALSE) ;
    }
    (void)gs_cleargstates(gid, GST_GROUP, NULL);
  }

  return FALSE ;
}

static Bool xps_Path_OpacityMask_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt;
  xpsPathState *state = NULL;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  if ( !xps_path_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  state->pattern_ok = TRUE ; /* Can now accept child pattern callbacks */

  /* Clear this before making the soft mask in case the opacity mask has
     paths, glyphs or canvases with an opacity attribute. */
  xps_ctxt->colortype = GSC_UNDEFINED;

  if (success) {
    /* The brush has already been setup; now paint the current clipping
       rectangle with the brush to make an object to put into the soft mask
       group.  Note that the whole rectangle does not require compositing,
       only those regions covered by the objects that use the soft mask. */
    sbbox_t brushbox;
    OMATRIX inverse_ctm;

    brushbox = thegsPageClip(*gstateptr).rbounds ;

    /* If can't invert the ctm, treat as transparent by having an empty group. */
    if ( matrix_inverse(&thegsPageCTM(*gstateptr), &inverse_ctm) ) {
      RECTANGLE rect;

      bbox_transform(&brushbox, &brushbox, &inverse_ctm) ;

      bbox_to_rectangle(&brushbox, &rect) ;

      if (! dorectfill(1, &rect, GSC_FILL, RECT_NORMAL))
        success = FALSE;
    }
  }

  if (! groupClose(&state->mask_group, success))
    success = FALSE;

  state->pattern_ok = FALSE ;

  HQASSERT(state->gstate_mask_id != GS_INVALID_GID,
           "Must have a gstate id for the opacity mask");
  if ( !gs_cleargstates(state->gstate_mask_id, GST_GROUP, NULL) )
    success = FALSE ;
  state->gstate_mask_id = GS_INVALID_GID;

  if (success) {
    success = tsSetSoftMask(gsTranState(gstateptr),
                            AlphaSoftMask,
                            groupId(state->mask_group),
                            gstateptr->colorInfo) ;
  }

  return success;
}

static Bool xps_Path_Data_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsPathState *state ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(PathGeometry), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    XMLG_VALID_CHILDREN_END
  } ;

  /* No attributes allowed. Also, no compatibility attributes are allowed on
     properties. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  if (! xps_path_state(filter, &xps_ctxt, &state))
    return error_handler(UNREGISTERED) ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  HQASSERT(!xps_ctxt->use_pathfill, "use_pathfill should be false");
  HQASSERT(xps_ctxt->path.firstpath == NULL, "path should be empty");
  HQASSERT(xps_ctxt->pathfill.firstpath == NULL, "path should be empty");

  /* Act on the attribute IsStroked. */
  xps_ctxt->ignore_isstroked = FALSE;

  return TRUE; /* keep on parsing */
}

static Bool xps_Path_Data_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt ;
  xpsPathState *state = NULL;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  /* The rest of the cleanups rely on the state being present. */
  if ( !xps_path_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  /* Steal constructed path from xps context and put into path state. This will
     later be transformed by the RenderTransform and applied as a fill or
     stroke. */
  path_free_list(state->data.firstpath, mm_pool_temp) ;
  state->data = xps_ctxt->path ;
  path_init(&xps_ctxt->path) ;

  path_free_list(state->datafill.firstpath, mm_pool_temp) ;
  state->datafill = xps_ctxt->pathfill ;
  path_init(&xps_ctxt->pathfill) ;

  state->use_datafill = xps_ctxt->use_pathfill;
  xps_ctxt->use_pathfill = FALSE;

  /* Reset these back to true; IsStroked does not apply on clips. */
  xps_ctxt->ignore_isstroked = TRUE;

  if ( state->fill_rule == NO_FILL_TYPE )
    state->fill_rule = xps_ctxt->fill_rule ;

  return success;
}

/*=============================================================================
 * Register functions
 *=============================================================================
 */

xpsElementFuncts path_functions[] =
{
  { XML_INTERN(Path),
    xps_Path_Start,
    xps_Path_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(Path_Fill),
    xps_Path_Fill_Start,
    xps_Path_Fill_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(Path_Stroke),
    xps_Path_Stroke_Start,
    xps_Path_Stroke_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(Path_Clip),
    xps_Path_Clip_Start,
    xps_Path_Clip_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(Path_RenderTransform),
    xps_Path_RenderTransform_Start,
    xps_Path_RenderTransform_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(Path_OpacityMask),
    xps_Path_OpacityMask_Start,
    xps_Path_OpacityMask_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(Path_Data),
    xps_Path_Data_Start,
    xps_Path_Data_End,
    NULL /* No characters callback. */
  },
  XPS_ELEMENTFUNCTS_END
};

/* ============================================================================
* Log stripped */
