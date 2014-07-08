/** \file
 * \ingroup fixedpage
 *
 * $HopeName: COREedoc!fixedpage:src:geometry.c(EBDSDK_P.1) $
 * $Id: fixedpage:src:geometry.c,v 1.76.10.1.1.1 2013/12/19 11:24:47 anon Exp $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of XPS fixed payload geometry callbacks.
 *
 * See geometry_functions declaration at the end of this file for the
 * element callbacks this file implements.
 *
 * The major geometry operation (PathGeometry) appends the result of
 * its markup to the gstate path.  At the end of this operation, the
 * gstate path will be valid, and xps_ctxt->fill_rule will be set to
 * the appropriate fill rule (if included) for the geometry. The
 * caller should clear the gstate path with gs_newpath() before the
 * geometry operation callback.
 */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "objects.h"
#include "namedef_.h"
#include "swerrors.h"
#include "gu_cons.h"
#include "graphics.h"
#include "gstack.h"
#include "gu_fills.h"
#include "pathcons.h"
#include "gu_path.h"
#include "xml.h"
#include "xmltypeconv.h"
#include "gu_ctm.h"
#include "system.h"
#include "clippath.h"
#include "params.h"
#include "constant.h"

#include "xpspriv.h"
#include "xpsscan.h"
#include "fixedpagepriv.h"

/*=============================================================================
 * Utility functions
 *=============================================================================
 */

#define GEOMETRY_STATE_NAME "XPS Geometry"

/** \brief Parse states for subpath elements */
enum {
  GEOMETRY_STATE_INVALID, /**< PathFigure not allowed. */
  GEOMETRY_STATE_INIT,    /**< Initial state; not in subpath. */
  GEOMETRY_STATE_FIGURE   /**< In PathFigure. */
} ;

/** \brief Structure to contain Geometry state. */
typedef struct xpsGeometryState_s {
  xpsCallbackStateBase base; /**< Superclass MUST be first element. */

  int32 seen ;        /**< Parse state for subpath, see enum above. */
  int32 rule1, rule2 ; /**< Fill rule for this PathGeometry/GeometryGroup. */

  Bool smooth_valid; /**< Last operator was a cubic bezier. */

  Bool figures_attribute_used; /**< Figures attribute was set on PathFigure. */

  SYSTEMVALUE smooth_ptx, smooth_pty; /**< First control point if doing a smooth bezier. */

  Bool isclosed ;       /**< TRUE if the path should be closed. */
  Bool isfilled ;       /**< TRUE if the current figure is to be filled. */

  /** Abbreviated paths are scanned in the commit after transform matrix is known. */
  utf8_buffer abbreviated_path ;

  OMATRIX transform ;   /**< Local Transform matrix; also used to save CTM. */
  OMATRIX savedctm ;    /**< Saved copy of original CTM. */

  OBJECT_NAME_MEMBER
} xpsGeometryState ;

/* Extract the xps context and/or XPS state, asserting that the right
   objects have been found. */
static inline Bool xps_geometry_state(
  /*@notnull@*/ /*@in@*/           xmlGFilter *filter,
  /*@null@*/ /*@out@*/             xmlDocumentContext **xps_ptr,
  /*@notnull@*/ /*@out@*/          xpsGeometryState **state_ptr)
{
  xmlDocumentContext *xps_ctxt;
  xpsGeometryState *state ;

  HQASSERT(filter != NULL, "filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");

  if ( xps_ptr != NULL )
    *xps_ptr = xps_ctxt ;

  state = (xpsGeometryState*)xps_ctxt->callback_state;
  if ( state == NULL || state->base.type != XPS_STATE_GEOMETRY )
    return FALSE ;

  VERIFY_OBJECT(state, GEOMETRY_STATE_NAME) ;

  *state_ptr = state ;

  return TRUE ;
}

/** Apply transform matrix to path. This is used for when the mini-path
    syntax is used in CombinedGeometry, to apply the transforms for the
    paths.
    Previous is bunkum but left until old CombinedGeometry code is removed.
    Now used to set up CTM for any PathFigure or to update path from
    PathGeometry Figures attribute.*/
static Bool apply_geometry_transform(
  xpsGeometryState *state)
{
  OMATRIX transform, newctm ;

  /* If there has been no modification of the CTM prior to this, and the
     new matrix won't do anything, we can quit early. */
  if ( MATRIX_EQ(&state->transform, &identity_matrix) &&
       MATRIX_EQ(&state->savedctm, &thegsPageCTM(*gstateptr)) )
    return TRUE ;

  /* We need to modify the CTM, even though we will transform the path
     explicitly, because child elements may use it. */
  matrix_mult(&state->transform, &state->savedctm, &newctm) ;

  /* The new local transformation matrix is the same as the old local
     transformation matrix, so there is nothing to do here. Move along. */
  if ( MATRIX_EQ(&newctm, &thegsPageCTM(*gstateptr)) )
    return TRUE ;

  /* We need to generate a transform that undoes the CTM, adds the
     RenderTransform, and then re-does the CTM. Invertible matrices
     should not cause error, but whether a degenerate path or no path is
     displayed is implementation-dependent. */
  if ( matrix_inverse(&thegsPageCTM(*gstateptr), &transform) ) {
    matrix_mult(&transform, &newctm, &transform) ;
  }

  /* Set the new CTM */
  return gs_setctm(&newctm, FALSE) ;
}

/*=============================================================================
 * XML start/end element callbacks
 *=============================================================================
 */

/** Commit function for PathGeometry is used to enforce ordering of child
    properties vs. child elements. DO NOT REMOVE. */
static Bool xps_PathGeometry_Commit(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsGeometryState *state = NULL;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;
  UNUSED_PARAM(xmlGAttributes *, attrs) ;

  if ( !SystemParams.XPS )
    return error_handler(INVALIDACCESS);

  if ( !xps_geometry_state(filter, &xps_ctxt, &state) )
    return error_handler(UNREGISTERED) ;

  /* Should always delay path creation until after transform matrix has been
     seen; Path code needs to convert to device space to handle cases like
     omitting a segment when the two points coincide. Calling path_transform
     after seeing a transform won't give the same results as delaying
     creating the path.  The attribute string is guaranteed to be around
     until after the commit. */
  if ( state->abbreviated_path.unitlength > 0 ) {
    if ( !xps_convert_ST_AbbrGeom(filter, NULL, &state->abbreviated_path, &xps_ctxt->path) )
      return xmlg_attributes_invoke_match_error(filter, localname, /* We do not track prefix */ NULL,
                                                NULL, XML_INTERN(Figures)) ;
  }

  return TRUE ;
}

/** PathGeometry adds to the existing gstate path. The contexts in which it is
    used will clear the gstate path if appropriate (this is done in all cases
    except GeometryGroup, where the children accumulate). */
static Bool xps_PathGeometry_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  Bool success;
  xmlDocumentContext *xps_ctxt;
  xpsGeometryState *state ;
  int32 fill_rule ;

  static xmlGIStr *fillrule_name ;
  static Bool fillrule_set, figures_set, dummy ;
  static OMATRIX matrix;
  static xps_matrix_designator matrix_designator = {
    XML_INTERN(PathGeometry_Transform), &matrix
  } ;
  static xps_abbrevgeom_designator abbreviated_path = { XML_INTERN(PathFigure) } ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(PathGeometry_Transform), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(PathFigure), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Figures), NULL, &figures_set, xps_convert_ST_RscRefAbbrGeom, &abbreviated_path },
    { XML_INTERN(FillRule), NULL, &fillrule_set, xps_convert_ST_FillRule, &fillrule_name },
    { XML_INTERN(Transform), NULL, &dummy, xps_convert_ST_RscRefMatrix, &matrix_designator },
    XML_ATTRIBUTE_MATCH_END
  } ;

  static XPS_COMPLEXPROPERTYMATCH complex_properties[] = {
    { XML_INTERN(PathGeometry_Transform), XML_INTERN(ns_xps_2005_06), XML_INTERN(Transform), NULL, TRUE },
    XPS_COMPLEXPROPERTYMATCH_END
  };

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(filter != NULL, "filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");

  abbreviated_path.attributebuffer.codeunits = NULL ;
  abbreviated_path.attributebuffer.unitlength = 0 ;
  MATRIX_COPY(matrix_designator.matrix, &identity_matrix);

  if (! xps_commit_register(filter, localname, uri, attrs, complex_properties,
                            xps_PathGeometry_Commit))
    return FALSE ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  fill_rule = EOFILL_TYPE ;
  if ( fillrule_set ) {
    switch ( XML_INTERN_SWITCH(fillrule_name) ) {
    case XML_INTERN_CASE(NonZero):
      fill_rule = NZFILL_TYPE ;
      break ;
    case XML_INTERN_CASE(EvenOdd):
      fill_rule = EOFILL_TYPE ;
      break ;
    default:
      HQFAIL("FillRule is invalid.") ; /* Scanner ought to catch invalid values. */
    }
  }

  /* Make a new geometry state. */
  state = mm_alloc(mm_xml_pool, sizeof(xpsGeometryState),
                   MM_ALLOC_CLASS_XPS_CALLBACK_STATE) ;
  if (state == NULL) {
    return error_handler(VMERROR);
  }

  state->base.type = XPS_STATE_GEOMETRY;
  state->base.next = xps_ctxt->callback_state;
  state->seen = GEOMETRY_STATE_INIT ;
  state->rule1 = state->rule2 = fill_rule ;
  state->smooth_valid = FALSE;
  state->figures_attribute_used = figures_set ;
  state->abbreviated_path = abbreviated_path.attributebuffer ;
  MATRIX_COPY(&state->savedctm, &thegsPageCTM(*gstateptr)) ;
  MATRIX_COPY(&state->transform, matrix_designator.matrix) ;
  NAME_OBJECT(state, GEOMETRY_STATE_NAME) ;

  /* Transform for PathGeometry may be attribute, and path may have been defined
   * with Figures attribute. */
  success = apply_geometry_transform(state) ;

  if ( !success ) {
    UNNAME_OBJECT(state);
    (void)gs_setctm(&state->savedctm, FALSE) ;
    mm_free(mm_xml_pool, state, sizeof(xpsGeometryState));
    return(FALSE);
  }

  xps_ctxt->callback_state = (xpsCallbackState*)state ;

  return TRUE; /* keep on parsing */
}

static Bool xps_PathGeometry_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt;
  xpsGeometryState *state = NULL;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  if ( !xps_geometry_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  if ( !gs_setctm(&state->savedctm, FALSE) )
    success = FALSE ;

  xps_ctxt->callback_state = state->base.next ;
  xps_ctxt->fill_rule = state->rule1 ;
  UNNAME_OBJECT(state) ;
  mm_free(mm_xml_pool, state, sizeof(xpsGeometryState)) ;

  return success;
}

static Bool xps_PathGeometry_Transform_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsGeometryState *state = NULL;

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

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;
  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  if ( !xps_geometry_state(filter, &xps_ctxt, &state) )
    return error_handler(UNREGISTERED) ;

  xps_ctxt->transform = &state->transform ;

  return TRUE; /* keep on parsing */
}

static Bool xps_PathGeometry_Transform_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt;
  xpsGeometryState *state = NULL;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  /* MatrixTransform should have captured the matrix */
  if ( !xps_geometry_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  if ( xps_ctxt->transform != NULL )
    success = (success && detail_error_handler(SYNTAXERROR,
                                               "Required MatrixTransform element is missing.")) ;

  xps_ctxt->transform = NULL ;

  if ( success )
    success = apply_geometry_transform(state) ;

  return success;
}

static Bool xps_PathFigure_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsGeometryState *state = NULL;

  static SYSTEMVALUE startpoint[2] ;
  static Bool isclosed_set, isclosed, isfilled_set, isfilled ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(ArcSegment), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OR_MORE_OF, 1},
    { XML_INTERN(PolyLineSegment), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OR_MORE_OF, 1},
    { XML_INTERN(PolyBezierSegment), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OR_MORE_OF, 1},
    { XML_INTERN(PolyQuadraticBezierSegment), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OR_MORE_OF, 1},
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(StartPoint), NULL, NULL, xps_convert_ST_Point, &startpoint },
    { XML_INTERN(IsClosed), NULL, &isclosed_set, xps_convert_ST_Boolean, &isclosed },
    { XML_INTERN(IsFilled), NULL, &isfilled_set, xps_convert_ST_Boolean, &isfilled },
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  isclosed = FALSE;
  isfilled = TRUE;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;
  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  if ( !xps_geometry_state(filter, &xps_ctxt, &state) )
    return error_handler(UNREGISTERED) ;

  if ( state->figures_attribute_used )
    return detail_error_handler(SYNTAXERROR,
                                "<PathGeometry> has Figures property and <PathFigure> child element(s).") ;

  if ( state->seen != GEOMETRY_STATE_INIT )
    return error_handler(SYNTAXERROR) ;

  if ( !isfilled && !xps_ctxt->use_pathfill ) {
    /* Fill and stroke paths are no longer the same. */
    if ( !path_copy(&xps_ctxt->pathfill, &xps_ctxt->path, mm_pool_temp) )
      return FALSE;
    xps_ctxt->use_pathfill = TRUE;
  }

  if ( !gs_moveto(TRUE, startpoint, &xps_ctxt->path) )
    return FALSE;

  if ( isfilled && xps_ctxt->use_pathfill ) {
    if ( !gs_moveto(TRUE, startpoint, &xps_ctxt->pathfill) )
      return FALSE;
  }

  state->seen = GEOMETRY_STATE_FIGURE;
  state->smooth_valid = FALSE;
  state->isclosed = isclosed;
  state->isfilled = isfilled;

  return TRUE; /* keep on parsing */
}

static Bool xps_PathFigure_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt;
  xpsGeometryState *state = NULL;
  Bool isclosed;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  if ( !xps_geometry_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  state->seen = GEOMETRY_STATE_INIT ;
  state->smooth_valid = FALSE;

  /* All segments are stroked in a CombinedGeometry. */
  isclosed = ( xps_ctxt->ignore_isstroked ? TRUE : state->isclosed );

  if (! path_close(isclosed ? CLOSEPATH : MYCLOSE,
                   &xps_ctxt->path))
    return FALSE;

  if ( state->isfilled && xps_ctxt->use_pathfill ) {
    if (! path_close(isclosed ? CLOSEPATH : MYCLOSE,
                     &xps_ctxt->pathfill))
      return FALSE;
  }

  return success;
}

static Bool xps_ArcSegment_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsGeometryState *state = NULL;
  Bool sweepflag = TRUE ; /* Clockwise */

  static SYSTEMVALUE point[2], size[2] ;
  static double xrotation ;
  static Bool largearc, stroked_set, stroked ;
  static xmlGIStr *sweepdirection ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Point), NULL, NULL, xps_convert_ST_Point, &point },
    { XML_INTERN(Size), NULL, NULL, xps_convert_ST_PointGE0, &size },
    { XML_INTERN(RotationAngle), NULL, NULL, xps_convert_dbl_ST_Double, &xrotation },
    { XML_INTERN(IsLargeArc), NULL, NULL, xps_convert_ST_Boolean, &largearc },
    { XML_INTERN(SweepDirection), NULL, NULL, xps_convert_ST_SweepDirection, &sweepdirection },
    { XML_INTERN(IsStroked), NULL, &stroked_set, xps_convert_ST_Boolean, &stroked },
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  if ( !xps_geometry_state(filter, &xps_ctxt, &state) )
    return error_handler(UNREGISTERED) ;

  if ( state->seen != GEOMETRY_STATE_FIGURE )
    return error_handler(SYNTAXERROR) ;

  stroked = TRUE ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    return error_handler(UNDEFINED) ;
  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  switch ( XML_INTERN_SWITCH(sweepdirection) ) {
  case XML_INTERN_CASE(Clockwise):
    /* sweepflag default is Clockwise, i.e. a positive angle. */
    break ;
  case XML_INTERN_CASE(Counterclockwise):
    sweepflag = FALSE ;
    break ;
  default:
    HQFAIL("SweepDirection is invalid.") ;
  }

  if ( xps_ctxt->ignore_isstroked )
    stroked = TRUE ;

  if (! gs_ellipticalarcto(TRUE, stroked, largearc, sweepflag,
                           size[0], size[1], xrotation * DEG_TO_RAD,
                           point[0], point[1],
                           &xps_ctxt->path))
    return FALSE;

  if ( state->isfilled && xps_ctxt->use_pathfill ) {
    if (! gs_ellipticalarcto(TRUE, stroked, largearc, sweepflag,
                             size[0], size[1], xrotation * DEG_TO_RAD,
                             point[0], point[1],
                             &xps_ctxt->pathfill))
      return FALSE;
  }

  state->smooth_valid = FALSE;

  return TRUE;
}

static Bool xps_ArcSegment_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xpsGeometryState *state = NULL;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  if ( !xps_geometry_state(filter, NULL, &state) )
    return success && error_handler(UNREGISTERED) ;

  HQASSERT(state->seen == GEOMETRY_STATE_FIGURE, "Path state unexpected") ;

  return success;
}

static Bool xps_PolyLineSegment_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsGeometryState *state = NULL;

  static SYSTEMVALUE point[2] ;
  static Bool stroked_set, stroked ;
  static utf8_buffer points_buf ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Points), NULL, NULL, xps_convert_utf8, &points_buf },
    { XML_INTERN(IsStroked), NULL, &stroked_set, xps_convert_ST_Boolean, &stroked },
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  if ( !xps_geometry_state(filter, &xps_ctxt, &state) )
    return error_handler(UNREGISTERED) ;

  if ( state->seen != GEOMETRY_STATE_FIGURE )
    return error_handler(SYNTAXERROR) ;

  stroked = TRUE ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    return error_handler(UNDEFINED) ;
  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  if ( xps_ctxt->ignore_isstroked )
    stroked = TRUE ;

  /* From s0schema.xsd 0.90

    <!-- Points: List of ST_Point, separated by arbitrary whitespace -->
    <xs:simpleType name="ST_Points">
        <xs:restriction base="xs:string">
            <xs:whiteSpace value="collapse" />
<!--
            <xs:pattern value="[rn][scs][rn]( [rn][scs][rn])*"/>
-->
            <xs:pattern value="((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?))*" />
        </xs:restriction>
    </xs:simpleType>
  */
  do {
    if ( !xps_convert_ST_Point(filter, XML_INTERN(Points), &points_buf, point) ) {
      return xmlg_attributes_invoke_match_error(filter, localname, prefix, uri, XML_INTERN(Points)) ;
    }

    if (! gs_lineto(TRUE, stroked, point, &xps_ctxt->path))
      return FALSE;

    if ( state->isfilled && xps_ctxt->use_pathfill ) {
      if (! gs_lineto(TRUE, stroked, point, &xps_ctxt->pathfill))
        return FALSE;
    }

  } while ( utf8_iterator_more(&points_buf) ) ;

  state->smooth_valid = FALSE;

  return TRUE;
}

static Bool xps_PolyLineSegment_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xpsGeometryState *state = NULL;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  if ( !xps_geometry_state(filter, NULL, &state) )
    return success && error_handler(UNREGISTERED) ;

  HQASSERT(state->seen == GEOMETRY_STATE_FIGURE, "Path state unexpected") ;

  return success;
}

static Bool xps_PolyBezierSegment_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsGeometryState *state = NULL;

  static SYSTEMVALUE points[6] ;
  static Bool stroked_set, stroked ;
  static utf8_buffer points_buf ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Points), NULL, NULL, xps_convert_utf8, &points_buf },
    { XML_INTERN(IsStroked), NULL, &stroked_set, xps_convert_ST_Boolean, &stroked },
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  if ( !xps_geometry_state(filter, &xps_ctxt, &state) )
    return error_handler(UNREGISTERED) ;

  if ( state->seen != GEOMETRY_STATE_FIGURE )
    return error_handler(SYNTAXERROR) ;

  stroked = TRUE ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    return error_handler(UNDEFINED) ;
  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  if ( xps_ctxt->ignore_isstroked )
    stroked = TRUE ;

  /* From s0schema.xsd 0.90

    <!-- Points: List of ST_Point, separated by arbitrary whitespace -->
    <xs:simpleType name="ST_Points">
        <xs:restriction base="xs:string">
            <xs:whiteSpace value="collapse" />
<!--
            <xs:pattern value="[rn][scs][rn]( [rn][scs][rn])*"/>
-->
            <xs:pattern value="((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?))*" />
        </xs:restriction>
    </xs:simpleType>
  */
  do {
    if ( !xps_convert_ST_Point(filter, XML_INTERN(Points), &points_buf, &points[0]) ||
         !xps_convert_ST_Point(filter, XML_INTERN(Points), &points_buf, &points[2]) ||
         !xps_convert_ST_Point(filter, XML_INTERN(Points), &points_buf, &points[4]) ) {
      return xmlg_attributes_invoke_match_error(filter, localname, prefix, uri, XML_INTERN(Points)) ;
    }

    if (! gs_curveto(TRUE, stroked, points, &xps_ctxt->path))
      return FALSE;

    if ( state->isfilled && xps_ctxt->use_pathfill ) {
      if (! gs_curveto(TRUE, stroked, points, &xps_ctxt->pathfill))
        return FALSE;
    }

  } while ( utf8_iterator_more(&points_buf) ) ;

  /* Store the second control point in case the subsequent operator is a smooth bezier. */
  state->smooth_valid = TRUE;
  state->smooth_ptx = points[2];
  state->smooth_pty = points[3];

  return TRUE;
}

static Bool xps_PolyBezierSegment_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xpsGeometryState *state = NULL;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  if ( !xps_geometry_state(filter, NULL, &state) )
    return success && error_handler(UNREGISTERED) ;

  HQASSERT(state->seen == GEOMETRY_STATE_FIGURE, "Path state unexpected") ;

  if (!success)
    state->smooth_valid = FALSE;

  return success;
}

static Bool xps_PolyQuadraticBezierSegment_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsGeometryState *state = NULL;

  static SYSTEMVALUE points[4] ;
  static Bool stroked_set, stroked ;
  static utf8_buffer points_buf ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Points), NULL, NULL, xps_convert_utf8, &points_buf },
    { XML_INTERN(IsStroked), NULL, &stroked_set, xps_convert_ST_Boolean, &stroked },
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  if ( !xps_geometry_state(filter, &xps_ctxt, &state) )
    return error_handler(UNREGISTERED) ;

  if ( state->seen != GEOMETRY_STATE_FIGURE )
    return error_handler(SYNTAXERROR) ;

  stroked = TRUE ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    return error_handler(UNDEFINED) ;
  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  if ( xps_ctxt->ignore_isstroked )
    stroked = TRUE ;

  /* From s0schema.xsd 0.90

    <!-- Points: List of ST_Point, separated by arbitrary whitespace -->
    <xs:simpleType name="ST_Points">
        <xs:restriction base="xs:string">
            <xs:whiteSpace value="collapse" />
<!--
            <xs:pattern value="[rn][scs][rn]( [rn][scs][rn])*"/>
-->
            <xs:pattern value="((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?))*" />
        </xs:restriction>
    </xs:simpleType>
  */
  do {
    if ( !xps_convert_ST_Point(filter, XML_INTERN(Points), &points_buf, &points[0]) ||
         !xps_convert_ST_Point(filter, XML_INTERN(Points), &points_buf, &points[2]) ) {
      return xmlg_attributes_invoke_match_error(filter, localname, prefix, uri, XML_INTERN(Points)) ;
    }

    if (! gs_quadraticcurveto(TRUE, stroked, points, &xps_ctxt->path))
      return FALSE;

    if ( state->isfilled && xps_ctxt->use_pathfill ) {
      if (! gs_quadraticcurveto(TRUE, stroked, points, &xps_ctxt->pathfill))
        return FALSE;
    }

  } while ( utf8_iterator_more(&points_buf) ) ;

  state->smooth_valid = FALSE;

  return TRUE;
}

static Bool xps_PolyQuadraticBezierSegment_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xpsGeometryState *state = NULL;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  if ( !xps_geometry_state(filter, NULL, &state) )
    return success && error_handler(UNREGISTERED) ;

  HQASSERT(state->seen == GEOMETRY_STATE_FIGURE, "Path state unexpected") ;

  return success;
}

/*=============================================================================
 * Register functions
 *=============================================================================
 */

xpsElementFuncts geometry_functions[] =
{
  { XML_INTERN(PathGeometry),
    xps_PathGeometry_Start,
    xps_PathGeometry_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(PathGeometry_Transform),
    xps_PathGeometry_Transform_Start,
    xps_PathGeometry_Transform_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(PathFigure),
    xps_PathFigure_Start,
    xps_PathFigure_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(ArcSegment),
    xps_ArcSegment_Start,
    xps_ArcSegment_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(PolyLineSegment),
    xps_PolyLineSegment_Start,
    xps_PolyLineSegment_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(PolyBezierSegment),
    xps_PolyBezierSegment_Start,
    xps_PolyBezierSegment_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(PolyQuadraticBezierSegment),
    xps_PolyQuadraticBezierSegment_Start,
    xps_PolyQuadraticBezierSegment_End,
    NULL /* No characters callback. */
  },
  XPS_ELEMENTFUNCTS_END
};

/* ============================================================================
* Log stripped */
