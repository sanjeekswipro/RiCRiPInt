/** \file
 * \ingroup fixedpage
 *
 * $HopeName: COREedoc!fixedpage:src:visualbrush.c(EBDSDK_P.1) $
 * $Id: fixedpage:src:visualbrush.c,v 1.100.10.1.1.1 2013/12/19 11:24:47 anon Exp $
 *
 * Copyright (C) 2004-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of XPS fixed payload drawing brush callbacks.
 *
 * See visualbrush_functions declaration at the end of this file for the element
 * callbacks this file implements.
 */

#include "core.h"
#include "mmcompat.h"     /* mm_alloc_with_header etc.. */
#include "swerrors.h"
#include "objects.h"
#include "hqmemcpy.h"
#include "namedef_.h"
#include "graphics.h"
#include "gstate.h"
#include "gschead.h"      /* gsc_setcolordirect */
#include "imagecontext.h"
#include "hqunicode.h"
#include "xml.h"
#include "xmltypeconv.h"
#include "objnamer.h"
#include "gu_ctm.h"       /* gs_modifyctm */
#include "swcopyf.h"
#include "constant.h"     /* EPSILON */
#include "miscops.h"      /* run_ps_string */
#include "pattern.h" /* gs_makepattern */
#include "params.h"
#include "plotops.h"
#include "clipops.h"
#include "xpspriv.h"
#include "fixedpagepriv.h"

/*=============================================================================
 * Utility functions
 *=============================================================================
 */
#define BRUSH_STATE_NAME "XPS Drawing Brush"

/** \brief Structure to contain Brush state.

    This structure will be split into multiple copies when brushes are
    separated into their own files. */
typedef struct xpsVisualBrushState_s {
  xpsCallbackStateBase base; /**< Superclass MUST be first element. */

  OMATRIX transform ; /**< Local Transform matrix. */

  int32 colortype ;   /**< Color chain used by this brush. */

  utf8_buffer name ;  /**< Resource named under which drawing is stored. */

  USERVALUE opacity ; /**< VisualBrush opacity attribute. */

  OBJECT_NAME_MEMBER
} xpsVisualBrushState ;

/* Extract the xps context and/or XPS state, asserting that the right
   objects have been found. */
static inline Bool xps_brush_state(
  /*@notnull@*/ /*@in@*/           xmlGFilter *filter,
  /*@null@*/ /*@out@*/             xmlDocumentContext **xps_ptr,
  /*@notnull@*/ /*@out@*/          xpsVisualBrushState **state_ptr)
{
  xmlDocumentContext *xps_ctxt;
  xpsVisualBrushState *state ;

  HQASSERT(filter != NULL, "filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");

  if ( xps_ptr != NULL )
    *xps_ptr = xps_ctxt ;

  state = (xpsVisualBrushState*)xps_ctxt->callback_state;
  if ( state == NULL || state->base.type != XPS_STATE_VISUALBRUSH )
    return FALSE ;

  VERIFY_OBJECT(state, BRUSH_STATE_NAME) ;

  *state_ptr = state ;

  return TRUE ;
}

static Bool drawing_name(utf8_buffer *dest, const uint8 *src, uint32 length)
{
  const utf8_buffer match = {UTF8_AND_LENGTH("{StaticResource")};
  utf8_buffer scan ;

  HQASSERT(src, "No name to copy") ;
  HQASSERT(dest, "Nowhere to copy name") ;

  if ( length > 0 ) {
    UTF8 *namedata ;

    HQASSERT(src != NULL, "No code units to copy") ;

    /* This is a hack to extract the name from a resource reference to
       a VisualBrush.Visual. */
    scan.codeunits = (uint8 *)src ;
    scan.unitlength = length ;

    /* Must be a {StaticResource */
    if ( xml_match_string(&scan, &match) ) {
      if (xml_match_space(&scan) == 0)
        return error_handler(SYNTAXERROR) ;
      src = scan.codeunits ;

      /* Look for trailing }, from the end of the attribute value. In most
         cases this will be quicker. */
      while ( --scan.unitlength > 0 ) {
        if ( scan.codeunits[scan.unitlength] == '}' )
          break ;
      }
      length = scan.unitlength ;
    }

    if ( (namedata = mm_alloc_with_header(mm_xml_pool, length + 1,
                           MM_ALLOC_CLASS_XPS_DRAWNAME)) == NULL )
      return error_handler(VMERROR) ;

    HqMemCpy(namedata, src, length) ;
    namedata[length] = '\0' ;
    dest->codeunits = namedata ;
  } else {
    dest->codeunits = NULL ;
  }

  dest->unitlength = length ;

  return TRUE ;
}

/*=============================================================================
 * XML start/end element callbacks
 *=============================================================================
 */

/** Bitflags for tile flip directions. */
enum {
  FLIP_X = 1,
  FLIP_Y = 2
} ;

static Bool xps_VisualBrush_Commit(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt ;
  xpsVisualBrushState *state = NULL;
#define PSBUFSIZE (1200)
  uint8 psbuf[PSBUFSIZE], *psend ;
  int32 flip, tilingtype ;
  OBJECT pattern_dict = OBJECT_NOTVM_NOTHING,
    pattern_name = OBJECT_NOTVM_NOTHING;
  OMATRIX transform ;
  RECTANGLE brushbox;

  static RECTANGLE viewbox, viewport ;
  static xmlGIStr *viewboxunits, *viewportunits, *tilemode ;
  static Bool tilemode_set, dummy ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Viewbox), NULL, NULL, xps_convert_ST_ViewBox, &viewbox},
    { XML_INTERN(Viewport), NULL, NULL, xps_convert_ST_ViewBox, &viewport},
    { XML_INTERN(ViewboxUnits), NULL, &dummy, xps_convert_ST_ViewUnits, &viewboxunits},
    { XML_INTERN(ViewportUnits), NULL, &dummy, xps_convert_ST_ViewUnits, &viewportunits},
    { XML_INTERN(TileMode), NULL, &tilemode_set, xps_convert_ST_TileMode, &tilemode},
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;

  if ( !SystemParams.XPS )
    return error_handler(INVALIDACCESS);

  if ( !xps_brush_state(filter, &xps_ctxt, &state) )
    return error_handler(UNREGISTERED) ;

  /* Do not consume all attributes. */
  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, FALSE) )
    return error_handler(UNDEFINED) ;

  /* Non-invertible matrices are not an error. Metro 0.6d defines both the
     case of mapping a degenerate viewport and a degenerate viewbox as if a
     transparent brush were used.  Degenerate clipping implies an empty
     pattern bbox, so make the brush transparent in this case as well. */
  if ( clippingisdegenerate(gstateptr) ||
       viewport.w < EPSILON || viewport.h < EPSILON ||
       viewbox.w < EPSILON || viewbox.h < EPSILON ) {
    state->colortype = GSC_UNDEFINED ;
    return TRUE ;
  }

  if ( state->name.unitlength == 0 ) {
    state->colortype = GSC_UNDEFINED ;
    return TRUE ;
  }

  /* Modify the transform to map ViewBox onto ViewPort.  This maps the
     visualbrush source space onto the pattern space - which is further
     modified by the pattern matrix (state->transform). */
  MATRIX_00(&transform) = viewport.w / viewbox.w ;
  MATRIX_01(&transform) = 0.0 ;
  MATRIX_10(&transform) = 0.0 ;
  MATRIX_11(&transform) = viewport.h / viewbox.h ;
  MATRIX_20(&transform) = viewport.x - viewbox.x * MATRIX_00(&transform) ;
  MATRIX_21(&transform) = viewport.y - viewbox.y * MATRIX_11(&transform) ;
  MATRIX_SET_OPT_BOTH(&transform) ;
  matrix_mult(&transform, &state->transform, &transform) ;

  /* Small tile handling on the cheap!
     If the width or height of the brush tile is below two device pixels then the
     viewport is scaled to avoid pathological rendering problems and to give a
     vague averaging impression (2x2 pixels allows some mid-tone colours).  For these
     cases the viewer produces an average colour to paint across the brush area,
     but since this method is only optional, and awkward for us to implement, we
     don't do it.  The result is likely to vary considerably from the viewer. */
  if ( tilemode_set && XML_INTERN_SWITCH(tilemode) != XML_INTERN_CASE(None) ) {
    OMATRIX test ;
    double dx1, dy1, dx2, dy2, fac1, fac2 ;
#define MIN_TILE_SIZE 2 /* device pixels */

    matrix_mult(&transform, &thegsPageCTM(*gstateptr), &test) ;
    MATRIX_TRANSFORM_DXY(viewbox.w, 0, dx1, dy1, &test) ;
    MATRIX_TRANSFORM_DXY(0, viewbox.h, dx2, dy2, &test) ;

    if ( (dx1 * dx1 + dy1 * dy1) < (EPSILON * EPSILON) ||
         (dx2 * dx2 + dy2 * dy2) < (EPSILON * EPSILON) ) {
      /* Give up and make it completely transparent. */
      state->colortype = GSC_UNDEFINED ;
      return TRUE ;
    }

    fac1 = MIN_TILE_SIZE / sqrt(dx1 * dx1 + dy1 * dy1) ;
    fac2 = MIN_TILE_SIZE / sqrt(dx2 * dx2 + dy2 * dy2) ;
    if ( fac1 > 1 || fac2 > 1 ) {
      HQTRACE(TRUE,("WARNING! VisualBrush tile size is smaller than a few device pixels "
              "- result may be unexpected.\n")) ;
      /* Re-scale the viewport and re-do the transform so we end up with a
         brush tile that is at least two device pixels by two device pixels. */
      if ( fac1 > 1 )
        viewport.w *= fac1 ;
      if ( fac2 > 1 )
        viewport.h *= fac2 ;
      MATRIX_00(&transform) = viewport.w / viewbox.w ;
      MATRIX_01(&transform) = 0.0 ;
      MATRIX_10(&transform) = 0.0 ;
      MATRIX_11(&transform) = viewport.h / viewbox.h ;
      MATRIX_20(&transform) = viewport.x - viewbox.x * MATRIX_00(&transform) ;
      MATRIX_21(&transform) = viewport.y - viewbox.y * MATRIX_11(&transform) ;
      MATRIX_SET_OPT_BOTH(&transform) ;
      matrix_mult(&transform, &state->transform, &transform) ;
    }
  }

  {
    /* Non-invertible matrices are treated as if a transparent brush were
       used. */
    OMATRIX test ;

    matrix_mult(&transform, &thegsPageCTM(*gstateptr), &test) ;
    if ( !matrix_inverse(&test, &test) ) {
      state->colortype = GSC_UNDEFINED ;
      return TRUE ;
    }
  }

  flip = 0 ;
  tilingtype = 0 ;
  brushbox = viewbox;
  if ( tilemode_set )
    switch ( XML_INTERN_SWITCH(tilemode) ) {
    case XML_INTERN_CASE(Flipxy):
    case XML_INTERN_CASE(FlipXY):
      flip = FLIP_X|FLIP_Y ;
      brushbox.x -= brushbox.w ;
      brushbox.y -= brushbox.h ;
      brushbox.w += brushbox.w ;
      brushbox.h += brushbox.h ;
      tilingtype = 2 ;
      break ;
    case XML_INTERN_CASE(Flipy):
    case XML_INTERN_CASE(FlipY):
      flip = FLIP_Y ;
      brushbox.y -= brushbox.h ;
      brushbox.h += brushbox.h ;
      tilingtype = 2 ;
      break ;
    case XML_INTERN_CASE(Flipx):
    case XML_INTERN_CASE(FlipX):
      flip = FLIP_X ;
      brushbox.x -= brushbox.w ;
      brushbox.w += brushbox.w ;
      tilingtype = 2 ;
      break ;
    case XML_INTERN_CASE(Tile):
      tilingtype = 2 ;
      break ;
    case XML_INTERN_CASE(None):
      break ;
    default:
      HQFAIL("TileMode value should have been checked in start callback") ;
    }

  /* VisualBrush.Visual uses xmlcache to store its contents. The contents
     are executed here inside a pattern creation context. */

  /** \todo @@@ TODO FIXME ajcd 2004-09-14: major hack to create a pattern.
     Doesn't work for all types or flips. */
  swcopyf(psbuf, (uint8*)
          "<< /PatternType 101\n"
          "   /PaintType 1\n"
          "   /TilingType %d\n"
          "   /BBox [%f %f %f %f]\n"
          "   /XStep %f\n"
          "   /YStep %f\n"
          "   /xpsbrush 1183615869 internaldict /xpsbrush get\n"
          "   /PaintProc {\n"
          "     begin\n"
          "         [ /CA %f /ca %f /SetTransparency pdfmark\n"
          "         clipsave\n"
          "         %f %f %f %f rectclip\n"
          "         %d (%.*s) xpsbrush\n"
          "         cliprestore\n",
          tilingtype,
          brushbox.x, brushbox.y,
          brushbox.x + brushbox.w, brushbox.y + brushbox.h,
          brushbox.w, brushbox.h,
          state->opacity, state->opacity,
          viewbox.x, viewbox.y, viewbox.w, viewbox.h,
          xmlg_get_fc_id(filter),
          state->name.unitlength,
          state->name.codeunits) ;
  psend = psbuf + strlen((const char *)psbuf) ;
  if ( (flip & FLIP_X) != 0 ) {
    swcopyf(psend, (uint8*)
            "         [-1 0 0 1 %f 0] concat\n"
            "         clipsave\n"
            "         %f %f %f %f rectclip\n"
            "         %d (%.*s) xpsbrush\n"
            "         cliprestore\n",
            viewbox.x + viewbox.x,
            viewbox.x, viewbox.y, viewbox.w, viewbox.h,
            xmlg_get_fc_id(filter),
            state->name.unitlength,
            state->name.codeunits) ;
    psend += strlen((const char *)psend) ;
  }
  if ( (flip & FLIP_Y) != 0 ) {
    swcopyf(psend, (uint8*)
            "         [1 0 0 -1 0 %f] concat\n"
            "         clipsave\n"
            "         %f %f %f %f rectclip\n"
            "         %d (%.*s) xpsbrush\n"
            "         cliprestore\n",
            viewbox.y + viewbox.y,
            viewbox.x, viewbox.y, viewbox.w, viewbox.h,
            xmlg_get_fc_id(filter),
            state->name.unitlength,
            state->name.codeunits) ;
    psend += strlen((const char *)psend) ;
  }
  if ( (flip & (FLIP_X|FLIP_Y)) == (FLIP_X|FLIP_Y) ) {
    swcopyf(psend, (uint8*)
            "         [-1 0 0 1 %f 0] concat\n"
            "         clipsave\n"
            "         %f %f %f %f rectclip\n"
            "         %d (%.*s) xpsbrush\n"
            "         cliprestore\n",
            viewbox.x + viewbox.x,
            viewbox.x, viewbox.y, viewbox.w, viewbox.h,
            xmlg_get_fc_id(filter),
            state->name.unitlength,
            state->name.codeunits) ;
    psend += strlen((const char *)psend) ;
  }
  swcopyf(psend, (uint8*)
          "       end\n"
          "   }\n"
          ">> [ %f %f %f %f %f %f ]",
          transform.matrix[0][0],
          transform.matrix[0][1],
          transform.matrix[1][0],
          transform.matrix[1][1],
          transform.matrix[2][0],
          transform.matrix[2][1]);
  psend += strlen((const char *)psend) ;

  HQASSERT(psend - psbuf < PSBUFSIZE, "Uh-oh just run off the end of psbuf");

  object_store_name(&pattern_name, NAME_Pattern, LITERAL);

  if ( !run_ps_string(psbuf) ||
       !gs_makepattern(&operandstack, &pattern_dict) ||
       !object_access_reduce(READ_ONLY, &pattern_dict) ||
       !push2(&pattern_dict, &pattern_name, &operandstack) ||
       !gsc_setcolorspace(gstateptr->colorInfo, &operandstack, state->colortype) ||
       !gsc_setcolor(gstateptr->colorInfo, &operandstack, state->colortype) )
    return FALSE;

  return TRUE ;
}

int32 xps_VisualBrush_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsVisualBrushState *state ;
  Bool result = FALSE ;

  static USERVALUE opacity ;
  static Bool opacity_set, drawing_set, viewportunits_set, viewboxunits_set,
              tilemode_set, dummy ;
  static OMATRIX matrix;
  static RECTANGLE viewbox, viewport ;
  static xmlGIStr *viewboxunits, *viewportunits, *tilemode ;
  static xps_matrix_designator matrix_designator = {
    XML_INTERN(VisualBrush_Transform), &matrix
  };

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(VisualBrush_Transform), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(VisualBrush_Visual), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    /* Viewbox, Viewport, TileMode, ViewboxUnits, ViewportUnits are handled by
       the commit callback. */
    { XML_INTERN(Opacity), NULL, &opacity_set, xps_convert_fl_ST_ZeroOne, &opacity},
    { XML_INTERN(Transform), NULL, &dummy, xps_convert_ST_RscRefMatrix, &matrix_designator},
    { XML_INTERN(Visual), NULL, &drawing_set, xps_convert_ST_RscRef, XML_INTERN(VisualBrush_Visual)},
    /* We need these as we are checking all attributes in this callback. */
    { XML_INTERN(Viewbox), NULL, NULL, xps_convert_ST_ViewBox, &viewbox},
    { XML_INTERN(Viewport), NULL, NULL, xps_convert_ST_ViewBox, &viewport},
    { XML_INTERN(ViewboxUnits), NULL, &viewboxunits_set, xps_convert_ST_ViewUnits, &viewboxunits},
    { XML_INTERN(ViewportUnits), NULL, &viewportunits_set, xps_convert_ST_ViewUnits, &viewportunits},
    { XML_INTERN(TileMode), NULL, &tilemode_set, xps_convert_ST_TileMode, &tilemode},
    XML_ATTRIBUTE_MATCH_END
  } ;

  static XPS_COMPLEXPROPERTYMATCH complexproperties[] = {
    { XML_INTERN(VisualBrush_Transform), XML_INTERN(ns_xps_2005_06), XML_INTERN(Transform), NULL, TRUE },
    { XML_INTERN(VisualBrush_Visual), XML_INTERN(ns_xps_2005_06), XML_INTERN(Visual), NULL, TRUE },
    XPS_COMPLEXPROPERTYMATCH_END
  };

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(filter != NULL, "filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");
  HQASSERT(xps_ctxt->colortype == GSC_FILL ||
           xps_ctxt->colortype == GSC_STROKE,
           "Expect colortype to be defined at this point");

  MATRIX_COPY(matrix_designator.matrix, &identity_matrix);

  if (! xps_commit_register(filter, localname, uri, attrs, complexproperties,
                            xps_VisualBrush_Commit))
    return FALSE;
  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;

  /* If we have already seen a Visual resource, then we do not want it as a
     child element, and vice-versa. We skip the first property if we saw it
     as a resource reference. */
  if (drawing_set)
    xps_commit_unregister(filter, localname, uri,
                          XML_INTERN(VisualBrush_Visual),
                          XML_INTERN(ns_xps_2005_06)) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  /* Make a new brush state. */
  state = mm_alloc(mm_xml_pool, sizeof(xpsVisualBrushState),
                   MM_ALLOC_CLASS_XPS_CALLBACK_STATE );
  if (state == NULL) {
    return error_handler(VMERROR);
  }

#define return DO_NOT_RETURN_GO_TO_brush_start_cleanup_INSTEAD!

  state->base.type = XPS_STATE_VISUALBRUSH;
  state->base.next = xps_ctxt->callback_state;
  state->colortype = xps_ctxt->colortype;
  state->name.codeunits = NULL ;
  state->name.unitlength = 0 ;
  state->transform = *matrix_designator.matrix ;
  state->opacity = opacity_set ? opacity : 1.0f ;
  NAME_OBJECT(state, BRUSH_STATE_NAME) ;

  if ( drawing_set ) {
    const xmlGIStr *prefix ;
    const uint8 *v ;
    uint32 l ;

    result = xmlg_attributes_lookup(attrs, XML_INTERN(Visual), NULL, &prefix, &v, &l) ;
    if (! result) {
      HQFAIL("We matched a Visual, so a second lookup should always work.") ;
      goto brush_start_cleanup ;
    }
    result = drawing_name(&state->name, v, l) ;
    if (! result)
      goto brush_start_cleanup ;
  }

  /* Good completion; link the new brush into the context. */
  xps_ctxt->callback_state = (xpsCallbackState*)state ;

  result = TRUE ;

 brush_start_cleanup:
  if (! result ) {
    VERIFY_OBJECT(state, BRUSH_STATE_NAME) ;
    if ( state->name.codeunits )
      mm_free_with_header(mm_xml_pool, state->name.codeunits) ;
    UNNAME_OBJECT(state) ;
    mm_free(mm_xml_pool, state, sizeof(xpsVisualBrushState)) ;
  }

#undef return
  return result;
}

static Bool xps_VisualBrush_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt;
  xpsVisualBrushState *state = NULL;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  /* The rest of the cleanups rely on the state being present. */
  if ( !xps_brush_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  if ( state->name.codeunits )
    mm_free_with_header(mm_xml_pool, state->name.codeunits) ;

  /* XML cache is freed as part of the resource block it was attached to. */

  xps_ctxt->colortype = state->colortype;
  xps_ctxt->callback_state = state->base.next ;
  UNNAME_OBJECT(state) ;
  mm_free(mm_xml_pool, state, sizeof(xpsVisualBrushState)) ;

  return success;
}

static Bool xps_VisualBrush_Transform_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsVisualBrushState *state = NULL;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(MatrixTransform), XML_INTERN(ns_xps_2005_06), XMLG_ONE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  /* No attributes allowed. Also, no compatibility attributes are allowed on
     properties. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix );

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;
  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  if ( !xps_brush_state(filter, &xps_ctxt, &state) )
    return error_handler(SYNTAXERROR) ;

  xps_ctxt->transform = &state->transform ;

  return TRUE; /* keep on parsing */
}

static Bool xps_VisualBrush_Transform_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt;
  xpsVisualBrushState *state ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  /* MatrixTransform should have captured the matrix */
  if ( !xps_brush_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  if ( xps_ctxt->transform != NULL )
    success = (success && detail_error_handler(SYNTAXERROR,
      "Required MatrixTransform element is missing.")) ;

  xps_ctxt->transform = NULL ;

  return success;
}

static Bool xps_VisualBrush_Visual_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt ;
  xpsXmlPartContext *xmlpart_ctxt ;
  xmlGFilterChain *filter_chain ;
  xpsVisualBrushState *state = NULL;
  xpsResourceBlock *curr_resblock ;
  uint8 resname[15] ;
  Bool is_remote = FALSE ;
  uint32 resource_uid ;

  static uint32 uniqueid = 0 ;

  /* No attributes allowed. Also, no compatibility attributes are allowed on
     properties. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM(xmlGAttributes *, attrs) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;

  HQASSERT(filter != NULL, "filter is NULL") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt != NULL, "xmlpart_ctxt is NULL") ;
  HQASSERT(localname != NULL, "localname is NULL");

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;

  if ( !xps_brush_state(filter, &xps_ctxt, &state) )
    return error_handler(UNREGISTERED) ;

  /* Capture the drawing as an XMLCache into the resource cache. We will add
     the drawing to the root resource block, creating a new root block if
     there is not already one. */
  curr_resblock = SLL_GET_TAIL(&xmlpart_ctxt->resourceblock_stack, xpsResourceBlock, sll);

  if ( curr_resblock == NULL ) {
    /* No existing resources; create one, it will be the root resources and
       will be freed by FixedPage. */
    if ( !xps_resources_start(filter, localname, uri, RES_SCOPE_FIXEDPAGE) )
      return FALSE ;

    curr_resblock = SLL_GET_TAIL(&xmlpart_ctxt->resourceblock_stack, xpsResourceBlock, sll);
    HQASSERT(curr_resblock, "No resource block after resources start") ;
  } else {
    /* We have an existing resource block; add drawings to the root block so
       that lifetime is at least as long as the path in which we were called
       (this could be a recursive drawing brush, with the closest resources
       within the path we are drawing). */
    xps_resources_append(filter, localname, uri);
  }

  if (xmlpart_ctxt->executing_stack != NULL) {
    is_remote = xps_resource_is_remote(xmlpart_ctxt->executing_stack) ;
    resource_uid = xps_resource_uid(xmlpart_ctxt->executing_stack) ;
  } else {
    is_remote = FALSE ;
    resource_uid = xps_resblock_uid(curr_resblock) ;
  }

  xmlpart_ctxt->defining_brush_resource = TRUE ;

  /* Create a name that XPS cannot produce for the XML cache entry. */
  if ( ++uniqueid == 0 ) {
    HQFAIL("Wrapped uniqueid around 32 bit unsigned int.") ;
    uniqueid = 1 ;
  }

  swcopyf(resname, (uint8 *)"{%08x}", uniqueid) ;
  if ( !drawing_name(&state->name, resname, strlen_int32((char *)resname)) ||
       !xps_resblock_add_resource(curr_resblock,
                                  state->name.codeunits,
                                  state->name.unitlength,
                                  &xmlpart_ctxt->active_resource,
                                  is_remote, resource_uid ) )
  {
    /* Turn off defining resources. */
    (void)xps_resources_end(filter, TRUE) ;
    xmlpart_ctxt->defining_brush_resource = FALSE ;
    return FALSE ;
  }

  return TRUE;
}

static Bool xps_VisualBrush_Visual_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt ;
  xpsXmlPartContext *xmlpart_ctxt ;
  xmlGFilterChain *filter_chain ;
  xpsVisualBrushState *state ;

  UNUSED_PARAM(const xmlGIStr *, localname) ;
  UNUSED_PARAM(const xmlGIStr *, prefix) ;
  UNUSED_PARAM(const xmlGIStr *, uri) ;

  HQASSERT(filter != NULL, "filter is NULL") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt != NULL, "xmlpart_ctxt is NULL") ;

  if ( !xps_brush_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  /* Turn off defining brush resource. */
  xmlpart_ctxt->defining_brush_resource = FALSE ;

  return success;
}

/** This is an internal implementation element; it is an error for it to
    appear in markup. It is explicitly started and ended by xpsbrush_(),
    which is called from the PostScript pattern routines. Its purpose is to
    validate that the xpsbrush_() call only happened at an allowed point
    during Path or Glyphs. */
static Bool xps_Visual_Visual_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsCallbackState *state ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(Path), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(Canvas), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(Glyphs), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    XMLG_VALID_CHILDREN_END
  } ;

  /* No attributes allowed. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix );

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  HQASSERT(filter != NULL, "filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");
  state = xps_ctxt->callback_state;

  if ( state == NULL )
    return error_handler(SYNTAXERROR) ;

  /* Is it OK to expand brush contents here? */
  if ( !path_pattern_valid(state) &&
       !glyphs_pattern_valid(state) &&
       !canvas_pattern_valid(state) )
    return error_handler(SYNTAXERROR) ;

  return TRUE ;
}

/** This is an internal implementation element; it is an error for it to
    appear in markup. It is explicitly started and ended by xpsbrush_(),
    which is called from the PostScript pattern routines. Its purpose is to
    validate that the xpsbrush_() call only happened at an allowed point
    during Path or Glyphs. */
static Bool xps_Visual_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(Visual_Visual), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  /* Our internal Visual element MUST have a Visual reference. The
     Visual reference gets wrapped in Visual.Visual */
  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Visual), NULL, NULL, xps_convert_ST_RscRef, XML_INTERN(Visual_Visual)},
    XML_ATTRIBUTE_MATCH_END
  } ;

  HQASSERT(filter != NULL, "filter is NULL");

  UNUSED_PARAM( const xmlGIStr* , prefix );

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  return TRUE ;
}

Bool xps_brush_execute(xmlGFilterChain *filter_chain,
                       utf8_buffer *name)
{
  xmlGAttributes *attrs ;
  Bool status ;
  uint8 *resource_reference ;

  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  HQASSERT(name != NULL, "name is NULL") ;
  HQASSERT(name->unitlength != 0, "name length is zero.") ;

  /* {StaticResource } */
#define EXTRA_LEN 17

  /* name + static + NUL */
  if ((resource_reference = mm_alloc_with_header(mm_xml_pool, name->unitlength + EXTRA_LEN + 1,
                                                 MM_ALLOC_CLASS_XPS_DRAWNAME)) == NULL )
    return error_handler(VMERROR) ;

  swcopyf(resource_reference, (uint8 *)"{StaticResource %.*s}", name->unitlength, name->codeunits) ;

  if (! xmlg_attributes_create(filter_chain, &attrs)) {
    mm_free_with_header(mm_xml_pool, resource_reference) ;
    return error_handler(UNDEFINED) ;
  }

  if (! xmlg_attributes_insert(attrs, XML_INTERN(Visual), NULL, NULL,
                               resource_reference, name->unitlength + EXTRA_LEN)) {
    mm_free_with_header(mm_xml_pool, resource_reference) ;
    xmlg_attributes_destroy(&attrs) ;
    return error_handler(UNDEFINED) ;
  }

  /* Execute our internal element <Visual Visual="<reference>"/> */
  status = xmlg_fc_execute_start_element(filter_chain, XML_INTERN(Visual),
                                         /* prefix */ NULL,
                                         XML_INTERN(ns_xps_2005_06), attrs) &&
           xmlg_fc_execute_end_element(filter_chain,
                                       XML_INTERN(Visual),
                                       /* prefix */ NULL,
                                       XML_INTERN(ns_xps_2005_06), TRUE) ;
  if (!status)
    (void) error_handler(UNDEFINED);

  mm_free_with_header(mm_xml_pool, resource_reference) ;
  xmlg_attributes_destroy(&attrs) ;

  return status ;
}

/*=============================================================================
 * Register functions
 *=============================================================================
 */

xpsElementFuncts visualbrush_functions[] =
{
  { XML_INTERN(VisualBrush),
    xps_VisualBrush_Start,
    xps_VisualBrush_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(VisualBrush_Transform),
    xps_VisualBrush_Transform_Start,
    xps_VisualBrush_Transform_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(VisualBrush_Visual),
    xps_VisualBrush_Visual_Start,
    xps_VisualBrush_Visual_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(Visual),
    xps_Visual_Start,
    NULL, /* No end callback */
    NULL /* No characters callback. */
  },
  { XML_INTERN(Visual_Visual),
    xps_Visual_Visual_Start,
    NULL, /* No end callback */
    NULL /* No characters callback. */
  },
  XPS_ELEMENTFUNCTS_END
};

/* ============================================================================
* Log stripped */
