/** \file
 * \ingroup fixedpage
 *
 * $HopeName: COREedoc!fixedpage:src:canvas.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of XPS fixed payload canvas callbacks.
 *
 * See canvas_functions declaration at the end of this file for the element
 * callbacks this file implements.
 */

#include "core.h"

#include "swerrors.h"
#include "objects.h"
#include "namedef_.h"
#include "xml.h"
#include "xmltypeconv.h"
#include "gstack.h"
#include "gs_color.h"       /* GSC_FILL */
#include "gstate.h"
#include "tranState.h"
#include "render.h"
#include "swmemory.h"
#include "display.h"
#include "pathcons.h"
#include "clipops.h"
#include "gu_ctm.h"
#include "gu_path.h"
#include "system.h"
#include "objnamer.h"
#include "rectops.h"
#include "pathops.h"

#include "xpspriv.h"

#include "fixedpagepriv.h"

#define CANVAS_STATE_NAME "XPS Canvas"

/** \brief Structure to contain Canvas state. */
typedef struct xpsCanvasState_s {
  xpsCallbackStateBase base;

  /** For the gsave/grestore around the Canvas. */
  int32 gstate_id;
  Group *group;

  OMATRIX transform ; /**< RenderTransform captured by MatrixTransform. */
  OMATRIX savedctm ;  /**< Copy of the CTM before modification. */

  /** Abbreviated paths are scanned in the commit after transform matrix is known. */
  utf8_buffer abbreviated_clip ;
  PATHINFO clip ; /**< Clip path property. */
  int32 clip_rule; /**< FillRule associated with clipping path. */

  int32 gstate_mask_id; /**< gsave id for the opacity mask if saved. */
  Group* mask_group; /**< Group object for the opacity mask. */

  Bool pattern_ok ;  /**< Are we in a state where it's OK to accept a pattern? */

  OBJECT_NAME_MEMBER
} xpsCanvasState ;

/* Extract the xps context and/or XPS state, asserting that the right
   objects have been found. */
static inline Bool xps_canvas_state(
  /*@notnull@*/ /*@in@*/           xmlGFilter *filter,
  /*@null@*/ /*@out@*/             xmlDocumentContext **xps_ptr,
  /*@notnull@*/ /*@out@*/          xpsCanvasState **state_ptr)
{
  xmlDocumentContext *xps_ctxt;
  xpsCanvasState *state ;

  HQASSERT(filter != NULL, "filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");

  if ( xps_ptr != NULL )
    *xps_ptr = xps_ctxt ;

  state = (xpsCanvasState*)xps_ctxt->callback_state;
  if ( state == NULL || state->base.type != XPS_STATE_CANVAS )
    return FALSE ;

  VERIFY_OBJECT(state, CANVAS_STATE_NAME) ;

  *state_ptr = state ;

  return TRUE ;
}

/** Are we in a canvas, and if so is it OK to expand a pattern? */
Bool canvas_pattern_valid(xpsCallbackState *state)
{
  xpsCanvasState *canvas_state = (xpsCanvasState*)state ;
  HQASSERT(canvas_state != NULL, "No state") ;

  return (canvas_state->base.type == XPS_STATE_CANVAS &&
          canvas_state->pattern_ok) ;
}

static Bool apply_render_transform(xpsCanvasState *state)
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

  /* We need to generate a transform that undoes the CTM, adds the
     RenderTransform to the original CTM, and then re-does the CTM.
     Invertible matrices should not cause error, but whether a degenerate
     path or no path is displayed is implementation-dependent. */
  if ( matrix_inverse(&thegsPageCTM(*gstateptr), &transform) ) {
    matrix_mult(&transform, &newctm, &transform) ;

    /* Should always delay clip path creation until after transform matrix has been
       seen; Path code needs to convert to device space to handle cases like
       omitting a segment when the two points coincide. Calling path_transform
       after seeing a transform won't give the same results as delaying
       creating the path. */
    HQASSERT(!state->clip.firstpath, "Can't transform clip path after changing transform matrix") ;
  }

  return gs_setctm(&newctm, FALSE) ;
}

/*=============================================================================
 * XML start/end element callbacks
 *=============================================================================
 */

static Bool xps_Canvas_Commit(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  DL_STATE *page = get_core_context_interp()->page ;
  xpsCanvasState *state = NULL ;
  COLORSPACE_ID dummyspace_id ;
  OBJECT colorSpace = OBJECT_NOTVM_NOTHING ;
  int32 name_id ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;
  UNUSED_PARAM( xmlGAttributes *, attrs ) ;

  if ( !xps_canvas_state(filter, NULL, &state) )
    return error_handler(UNREGISTERED) ;

  /* The canvas (device) colorspace is the same as the VirtualDeviceSpace.
   * We have already checked that there is a suitable blend space for it.
   */
  dlVirtualDeviceSpace(page, &name_id, &dummyspace_id);

  object_store_name(&colorSpace, name_id, LITERAL) ;

  /* Should always delay path creation until after transform matrix has been
     seen; Path code needs to convert to device space to handle cases like
     omitting a segment when the two points coincide. Calling path_transform
     after seeing a transform won't give the same results as delaying
     creating the path.  The attribute string is guaranteed to be around
     until after the commit. */
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

  /* RenderTransform and Clip can appear in either order, so defer
     processing to here. The clip path can be stolen from the canvas state
     after transformation by gs_addclip. */
  if ( thePath(state->clip) != NULL ) {
    if ( !gs_addclip(state->clip_rule, &state->clip, FALSE) )
      return FALSE ;
  }

  /* Set the OpacityMask before opening the group as the mask needs to be
     applied on the canvas's group, not the objects inside the group. */
  if ( state->mask_group ) {
    if ( !tsSetSoftMask(gsTranState(gstateptr),
                        AlphaSoftMask,
                        groupId(state->mask_group),
                        gstateptr->colorInfo) )
      return FALSE ;
  }

  if ( !groupOpen(page, colorSpace, TRUE /* isolated */, FALSE /* knockout */,
                  TRUE /* banded */, NULL /* bgcolor */, NULL /* xferfn */,
                  NULL /* patternTA */, GroupSubGroup, &state->group) )
      return FALSE ;

  if (! gs_gpush(GST_GSAVE))
    return FALSE ;

  return TRUE;
}


static Bool xps_Canvas_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  Bool success = FALSE;
  xmlDocumentContext *xps_ctxt = xmlg_get_user_data(filter) ;

  xpsCanvasState *state;

  static xmlGIStr *lang, *edgemode ;
  static utf8_buffer name, automation_name, automation_helptext ;
  static USERVALUE opacity ;
  static Bool opacity_set, dummy, name_set, lang_set ;
  static xps_abbrevgeom_designator abbreviated_clip = { XML_INTERN(Canvas_Clip) } ;
  static OMATRIX matrix;
  static xps_matrix_designator matrix_designator = {
    XML_INTERN(Canvas_RenderTransform), &matrix
  };

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(Canvas_Resources), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(Canvas_RenderTransform), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(Canvas_Clip), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(Canvas_OpacityMask), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(Canvas), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP},
    { XML_INTERN(Glyphs), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP},
    { XML_INTERN(Path), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Opacity), NULL, &opacity_set, xps_convert_fl_ST_ZeroOne, &opacity },
    /* References; ordered for least work */
    { XML_INTERN(Clip), NULL, &dummy, xps_convert_ST_RscRefAbbrGeom, &abbreviated_clip },
    { XML_INTERN(RenderOptions_EdgeMode), NULL, &dummy, xps_convert_ST_EdgeMode, &edgemode },
    { XML_INTERN(OpacityMask), NULL, &dummy, xps_convert_ST_RscRef, XML_INTERN(Canvas_OpacityMask) },
    { XML_INTERN(RenderTransform), NULL, &dummy, xps_convert_ST_RscRefMatrix, &matrix_designator },
    { XML_INTERN(Name), NULL, &name_set, xps_convert_ST_Name, &name },
    { XML_INTERN(FixedPage_NavigateUri), NULL, &dummy, xps_convert_navigate_uri, NULL },
    { XML_INTERN(AutomationProperties_Name), NULL, &dummy, xps_convert_ST_UnicodeString, &automation_name },
    { XML_INTERN(AutomationProperties_HelpText), NULL, &dummy, xps_convert_ST_UnicodeString, &automation_helptext },
    { XML_INTERN(lang), XML_INTERN(ns_w3_xml_namespace), &lang_set, xml_convert_lang, &lang },
    XML_ATTRIBUTE_MATCH_END
  } ;

  static XPS_COMPLEXPROPERTYMATCH complex_properties[] = {
    { XML_INTERN(Canvas_Resources), XML_INTERN(ns_xps_2005_06), NULL, NULL, TRUE },
    { XML_INTERN(Canvas_RenderTransform), XML_INTERN(ns_xps_2005_06), XML_INTERN(RenderTransform), NULL, TRUE },
    { XML_INTERN(Canvas_Clip), XML_INTERN(ns_xps_2005_06), XML_INTERN(Clip), NULL, TRUE },
    { XML_INTERN(Canvas_OpacityMask), XML_INTERN(ns_xps_2005_06), XML_INTERN(OpacityMask), NULL, TRUE },
    { XML_INTERN(Visual), XML_INTERN(ns_xps_2005_06), NULL, NULL, TRUE },
    XPS_COMPLEXPROPERTYMATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  abbreviated_clip.attributebuffer.codeunits = NULL ;
  abbreviated_clip.attributebuffer.unitlength = 0 ;
  MATRIX_COPY(matrix_designator.matrix, &identity_matrix);

  if (! xps_commit_register(filter, localname, uri, attrs, complex_properties,
                            xps_Canvas_Commit))
    return FALSE ;

  /* Must clean up minipath properly if used. */
  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED) ;

  /* Make a new canvas. */
  state = mm_alloc(mm_xml_pool, sizeof(xpsCanvasState),
                   MM_ALLOC_CLASS_XPS_CALLBACK_STATE);
  if (state == NULL)
    return error_handler(VMERROR) ;

#define return DO_NOT_RETURN_GO_TO_state_cleanup_INSTEAD!

  state->base.type = XPS_STATE_CANVAS;
  state->base.next = xps_ctxt->callback_state;
  state->gstate_id = GS_INVALID_GID;
  state->group = NULL;
  MATRIX_COPY(&state->transform, matrix_designator.matrix) ;
  MATRIX_COPY(&state->savedctm, &thegsPageCTM(*gstateptr)) ;
  state->abbreviated_clip = abbreviated_clip.attributebuffer ;
  path_init(&state->clip);
  state->clip_rule = EOFILL_TYPE ;
  state->gstate_mask_id = GS_INVALID_GID;
  state->mask_group = NULL;
  state->pattern_ok = FALSE;
  NAME_OBJECT(state, CANVAS_STATE_NAME) ;

  if (! gs_gpush(GST_GROUP))
    goto state_cleanup;

  state->gstate_id = gstackptr->gId;

  /* Set opacity before groupOpen to apply opacity to the group rather than to
     its contents directly.  Opacity must be applied multiplicatively, in case
     the canvas is within a visual brush which has specified an opacity value
     already. */
  if ( opacity_set ) {
    USERVALUE opacity_f = opacity * tsConstantAlpha(gsTranState(gstateptr), FALSE);
    USERVALUE opacity_s = opacity * tsConstantAlpha(gsTranState(gstateptr), TRUE);
    tsSetConstantAlpha(gsTranState(gstateptr), FALSE, opacity_f, gstateptr->colorInfo);
    tsSetConstantAlpha(gsTranState(gstateptr), TRUE, opacity_s, gstateptr->colorInfo);
  }

  if ( !apply_render_transform(state) )
    goto state_cleanup;

  /* Push the new canvas on the state stack. */
  xps_ctxt->callback_state = (xpsCallbackState*)state;

  success = TRUE;

 state_cleanup:
  if (! success) {
    VERIFY_OBJECT(state, CANVAS_STATE_NAME) ;
    UNNAME_OBJECT(state) ;
    mm_free(mm_xml_pool, state, sizeof(xpsCanvasState));
  }

#undef return
  return success;
}

static Bool xps_Canvas_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt ;
  xmlGFilterChain *filter_chain ;
  xpsXmlPartContext *xmlpart_ctxt ;
  xpsCanvasState *state = NULL ;
  xpsResourceBlock *resblock ;
  uint32 depth ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;
  depth = xmlg_get_element_depth(filter) ;
  filter_chain = xmlg_get_fc(filter) ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;

  /* The rest of the cleanups rely on the state being present. */
  if (! xps_canvas_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  /* destroy all resource blocks at this level */
  while ((resblock = SLL_GET_HEAD(&(xmlpart_ctxt->resourceblock_stack), xpsResourceBlock, sll)) != NULL) {
#if 0
    /* resources are defined two levels deeper */
    HQASSERT(xps_resblock_depth(resblock) <= depth + 2,
             "resource depth deeper than expected") ;
#endif
    if (xps_resblock_depth(resblock) == depth + 2) {
      SLL_REMOVE_HEAD(&(xmlpart_ctxt->resourceblock_stack)) ;
      xps_resblock_destroy(&resblock) ;
    } else {
      break ;
    }
  }

  /* A group will not exist if it has been entirely clipped out,
     or it wasn't created because there's no transparency in the job. */
  if (state->group) {
    /* Always close a group once it has been opened. */
    if ( !groupClose(&state->group, success) )
      success = FALSE ;
  }

  HQASSERT(state->gstate_id != GS_INVALID_GID,
           "Not a valid canvas gsave gstate id");
  if ( !gs_cleargstates(state->gstate_id, GST_GROUP, NULL) )
    success = FALSE ;

  path_free_list(thePath(state->clip), mm_pool_temp) ;

  /* Pop the canvas stack. */
  xps_ctxt->callback_state = state->base.next ;
  UNNAME_OBJECT(state) ;
  mm_free(mm_xml_pool, state, sizeof(xpsCanvasState)) ;

  return success ;
}

static Bool xps_Canvas_Resources_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xpsCanvasState* state;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  if (! xps_canvas_state(filter, NULL, &state) )
    return success && error_handler(UNREGISTERED) ;

  return success;
}

static Bool xps_Canvas_Resources_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps;
  xpsCanvasState *state;

  /* No attributes allowed. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(ResourceDictionary), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(localname != NULL, "NULL localname");

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  /* Extract xps types */
  if (! xps_canvas_state(filter, &xps, &state) )
    return error_handler(UNREGISTERED) ;

  return TRUE;
}

static Bool xps_Canvas_Clip_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt ;
  xpsCanvasState *state ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(PathGeometry), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    XMLG_VALID_CHILDREN_END
  } ;

  /* No attributes allowed. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;
  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  /* No attributes */

  if ( !xps_canvas_state(filter, &xps_ctxt, &state) )
    return error_handler(UNREGISTERED) ;

  HQASSERT(xps_ctxt->ignore_isstroked, "ignore_isstroked should be true");
  HQASSERT(!xps_ctxt->use_pathfill, "use_pathfill should be false");
  HQASSERT(thePath(xps_ctxt->path) == NULL, "path should be empty");
  HQASSERT(thePath(xps_ctxt->pathfill) == NULL, "pathfill should be empty");

  return TRUE;
}

static Bool xps_Canvas_Clip_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt;
  xpsCanvasState *state = NULL ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  /* The rest of the cleanups rely on the state being present. */
  if ( !xps_canvas_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  /* Steal constructed path from xps context and put into path state. This will
     later be transformed by the RenderTransform and applied as a clip. */
  path_free_list(thePath(state->clip), mm_pool_temp) ;
  if ( xps_ctxt->use_pathfill ) {
    state->clip = xps_ctxt->pathfill ;
    path_init(&xps_ctxt->pathfill) ;
    xps_ctxt->use_pathfill = FALSE ;
    path_free_list(thePath(xps_ctxt->path), mm_pool_temp) ;
    path_init(&xps_ctxt->path) ;
  } else {
    state->clip = xps_ctxt->path ;
    path_init(&xps_ctxt->path) ;
  }

  HQASSERT(!xps_ctxt->use_pathfill, "use_pathfill should still be false");
  HQASSERT(thePath(xps_ctxt->path) == NULL, "path should be empty");
  HQASSERT(thePath(xps_ctxt->pathfill) == NULL, "pathfill should be empty");

  state->clip_rule = xps_ctxt->fill_rule ;

  return success;
}

static Bool xps_Canvas_RenderTransform_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsCanvasState *state = NULL ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(MatrixTransform), XML_INTERN(ns_xps_2005_06), XMLG_ONE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  /* No attributes allowed. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;
  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  if ( !xps_canvas_state(filter, &xps_ctxt, &state) )
    return error_handler(UNREGISTERED) ;

  /* No attributes */

  xps_ctxt->transform = &state->transform ;

  return TRUE; /* keep on parsing */
}

static Bool xps_Canvas_RenderTransform_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt;
  xpsCanvasState *state = NULL ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  /* MatrixTransform should already have captured the matrix */
  if ( !xps_canvas_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  if ( xps_ctxt->transform != NULL )
    success = (success && detail_error_handler(SYNTAXERROR,
                                               "Required MatrixTransform element is missing.")) ;

  xps_ctxt->transform = NULL ;

  if ( success )
    success = apply_render_transform(state) ;

  return success;
}

static Bool xps_Canvas_OpacityMask_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  DL_STATE *page = get_core_context_interp()->page ;
  xmlDocumentContext *xps_ctxt ;
  xpsCanvasState *state = NULL ;

  COLORSPACE_ID dummyspace_id ;
  OBJECT colorSpace = OBJECT_NOTVM_NOTHING ;
  int32 name_id ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(SolidColorBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(ImageBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(VisualBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(LinearGradientBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(RadialGradientBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(Visual), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  /* No attributes allowed. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;
  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  if ( !xps_canvas_state(filter, &xps_ctxt, &state) )
    return error_handler(UNREGISTERED) ;

  /* The opacity mask colorspace is the same as the VirtualDeviceSpace */
  dlVirtualDeviceSpace(page, &name_id, &dummyspace_id);

  object_store_name(&colorSpace, name_id, LITERAL) ;

  /* An additional gsave is required around opacity mask creation in case
     it disturbs the results of prior Canvas callbacks. */
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

static Bool xps_Canvas_OpacityMask_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt;
  xpsCanvasState *state = NULL ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  if ( !xps_canvas_state(filter, &xps_ctxt, &state) )
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

  return success;
}

/*=============================================================================
 * Register functions
 *=============================================================================
 */

xpsElementFuncts canvas_functions[] =
{
  { XML_INTERN(Canvas),
    xps_Canvas_Start,
    xps_Canvas_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(Canvas_Resources),
    xps_Canvas_Resources_Start,
    xps_Canvas_Resources_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(Canvas_Clip),
    xps_Canvas_Clip_Start,
    xps_Canvas_Clip_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(Canvas_RenderTransform),
    xps_Canvas_RenderTransform_Start,
    xps_Canvas_RenderTransform_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(Canvas_OpacityMask),
    xps_Canvas_OpacityMask_Start,
    xps_Canvas_OpacityMask_End,
    NULL /* No characters callback. */
  },
  XPS_ELEMENTFUNCTS_END
};

/* ============================================================================
* Log stripped */
