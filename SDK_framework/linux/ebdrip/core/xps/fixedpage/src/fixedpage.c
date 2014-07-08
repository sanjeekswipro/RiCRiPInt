/** \file
 * \ingroup fixedpage
 *
 * $HopeName: COREedoc!fixedpage:src:fixedpage.c(EBDSDK_P.1) $
 * $Id: fixedpage:src:fixedpage.c,v 1.128.1.1.1.1 2013/12/19 11:24:47 anon Exp $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of XPS fixed page callbacks.
 */

#include "core.h"

#include "objects.h"
#include "namedef_.h"
#include "swerrors.h"
#include "constant.h"
#include "swcopyf.h"
#include "graphics.h"
#include "miscops.h"
#include "gu_ctm.h"
#include "devops.h"
#include "gschead.h"
#include "stacks.h"
#include "gstate.h"
#include "params.h"
#include "swmemory.h"
#include "gschcms.h"
#include "monitor.h"
#include "dicthash.h"
#include "dlstate.h"        /* inputpage */
#include "timing.h"         /* SW_TRACE_* */

#include "xml.h"
#include "xmltypeconv.h"

#include "xpspriv.h"
#include "xpspt.h"
#include "xpsscan.h"
#include "xpsfonts.h"
#include "xpsresblock.h"
#include "xpsiccbased.h"
#include "fixedpagepriv.h"

#include "printticket.h"

#define FIXEDPAGE_STATE_NAME "XPS FixedPage"

/** \brief Structure to contain FixedPage state. */
typedef struct xpsFixedPageState_s {
  xpsCallbackStateBase base;

  OBJECT save ; /**< Save used for restore at end of page. */

  OBJECT_NAME_MEMBER
} xpsFixedPageState ;

/* Extract the xps context and/or XPS state, asserting that the right
   objects have been found. */
static inline Bool xps_fixedpage_state(
  /*@notnull@*/ /*@in@*/           xmlGFilter *filter,
  /*@null@*/ /*@out@*/             xmlDocumentContext **xps_ptr,
  /*@notnull@*/ /*@out@*/          xpsFixedPageState **state_ptr)
{
  xmlDocumentContext *xps_ctxt;
  xpsFixedPageState *state ;

  HQASSERT(filter != NULL, "filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");

  if ( xps_ptr != NULL )
    *xps_ptr = xps_ctxt ;

  state = (xpsFixedPageState*)xps_ctxt->callback_state;
  if ( state == NULL || state->base.type != XPS_STATE_FIXEDPAGE )
    return FALSE ;

  VERIFY_OBJECT(state, FIXEDPAGE_STATE_NAME) ;

  *state_ptr = state ;

  return TRUE ;
}

/*=============================================================================
 * XML start/end element callbacks
 *=============================================================================
 */

/** XPS LinkTargets element start callback. */
static Bool xps_LinkTarget_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  static utf8_buffer name ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Name), NULL, NULL, xps_convert_ST_Name, &name },
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(filter != NULL, "filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  return TRUE;
}

/** Commit function for FixedPage is used to enforce ordering of child
    properties vs. child elements. DO NOT REMOVE. */
static Bool xps_FixedPage_Commit(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  UNUSED_PARAM( xmlGFilter* , filter ) ;
  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;
  UNUSED_PARAM( xmlGAttributes *, attrs ) ;
  return TRUE ;
}

/** XPS FixedPage element start callback. */
static Bool xps_FixedPage_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  corecontext_t *context = get_core_context_interp();
  xmlDocumentContext* xps_ctxt;
  xpsFixedPageState *state ;
  Bool result = FALSE ;
  static xmlGIStr *lang ;
  static utf8_buffer name ;
  static Bool contentbox_set, bleedbox_set, name_set ;
  static double height, width ;
  static RECTANGLE contentbox, bleedbox ;
  sbbox_t pagebox;
  sbbox_t contentbox_bbox;
  sbbox_t bleedbox_bbox;
  int32 dummyname_id;
  COLORSPACE_ID space_id;
  OBJECT omiterlimit = OBJECT_NOTVM_INTEGER(10) ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(FixedPage_Resources), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(Canvas), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP},
    { XML_INTERN(Glyphs), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP},
    { XML_INTERN(Path), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP},
    { XML_INTERN(ImmediateDiscard), XML_INTERN(ns_ggs_xps_2007_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Height), NULL, NULL, xps_convert_dbl_ST_GEOne, &height},
    { XML_INTERN(Width), NULL, NULL, xps_convert_dbl_ST_GEOne, &width},
    { XML_INTERN(ContentBox), NULL, &contentbox_set, xps_convert_ST_ContentBox, &contentbox},
    { XML_INTERN(BleedBox), NULL, &bleedbox_set, xps_convert_ST_BleedBox, &bleedbox},
    { XML_INTERN(lang), XML_INTERN(ns_w3_xml_namespace), NULL, xml_convert_lang, &lang },
    { XML_INTERN(Name), NULL, &name_set, xps_convert_ST_Name, &name },
    XML_ATTRIBUTE_MATCH_END
  } ;

  static XPS_COMPLEXPROPERTYMATCH complex_properties[] = {
    { XML_INTERN(FixedPage_Resources), XML_INTERN(ns_xps_2005_06), NULL, NULL, TRUE },
    XPS_COMPLEXPROPERTYMATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix );

  HQASSERT(filter != NULL, "filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");

  if (xps_ctxt->discard_parser != NULL) {
    if (! xps_process_discards(filter, xps_ctxt->discard_parser)) {
      /* Discard control errors are not fatal. */
      error_clear_context(context->error);
      xps_close_discard_parser(&xps_ctxt->discard_parser);
    }
  }

  if (! xps_commit_register(filter, localname, uri, attrs, complex_properties,
                            xps_FixedPage_Commit))
    return FALSE ;

  if ( !xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  /* Set the PageSize bbox */
  bbox_store(&pagebox, 0, 0, width, height);

  /* Default content box to pagebbox if not set or invalid */
  if ( contentbox_set ) {
    rectangle_to_bbox(&contentbox, &contentbox_bbox);
    /* Valid if in normal form, at least min size, and lies within pagebbox */
    contentbox_set = (bbox_is_normalised(&contentbox_bbox) &&
                      bbox_has_min_size(&contentbox_bbox, 1.0, 1.0) &&
                      bbox_contains_epsilon(&pagebox, &contentbox_bbox, EPSILON, EPSILON));
  }
  if ( !contentbox_set ) {
    bbox_to_rectangle(&pagebox, &contentbox);
  }

  /* Default bleed box to pagebbox if not set or invalid */
  if ( bleedbox_set ) {
    rectangle_to_bbox(&bleedbox, &bleedbox_bbox);
    /* pagebbox must lie within the bleed box */
    bleedbox_set = bbox_contains_epsilon(&bleedbox_bbox, &pagebox, EPSILON, EPSILON);
  }
  if ( !bleedbox_set ) {
    bbox_to_rectangle(&pagebox, &bleedbox);
  }

  /* Pass page details to PT device before running start of page config */
  if ( !pt_page_details(&xps_ctxt->printticket,
                        width, height, &bleedbox, &contentbox) ) {
    return(FALSE);
  }
  if ( !pt_config_start(&xps_ctxt->printticket) ) {
    return(FALSE);
  }

  /* The default miterlimit is defined as 10 (Metro 0.8, 4.1). */
  if (!push(&omiterlimit, &operandstack) || !gs_setmiterlimit(&operandstack))
    return FALSE;

  /* See if an effective blend space for RGB has been defined */
  if ( !gsc_getBlendInfoSet(gstateptr->colorInfo, SPACE_DeviceRGB)) {

    /* Set up a default blend space for RGB */
    if (!set_xps_blendRGB())
      return FALSE;
  }

  /* Check there is an appropriate blend space for the VirtualDeviceSpace */
  dlVirtualDeviceSpace(context->page, &dummyname_id, &space_id);

  /* Warn if there is no suitable blend space */
  if ( !gsc_getBlendInfoSet(gstateptr->colorInfo, space_id) ) {
    monitorf(UVS("Warning: No blend space found for virtual device - naive color conversions will occur.\n"));
  }

  /* Squirrel the default rendering intent which is derived from the current
   * rendering intent after the print ticket has had chance to modify it
   */
  if (!gsc_getrenderingintent(gstateptr->colorInfo, &operandstack))
    return FALSE;
  xps_ctxt->defaultRenderingIntentName = oName(*theTop(operandstack));
  pop(&operandstack);  /* The currentrenderingintent */


  /* Make a new fixed page state. */
  state = mm_alloc(mm_xml_pool, sizeof(xpsFixedPageState),
                   MM_ALLOC_CLASS_XPS_CALLBACK_STATE) ;
  if (state == NULL) {
    return error_handler(VMERROR);
  }

#define return DO_NOT_RETURN_GO_TO_fixedpage_start_cleanup_INSTEAD!

  state->base.type = XPS_STATE_FIXEDPAGE;
  state->base.next = xps_ctxt->callback_state;
  state->save = onothing ; /* Struct copy to set slot properties */
  NAME_OBJECT(state, FIXEDPAGE_STATE_NAME) ;

  if (! save_(context->pscontext))
    goto fixedpage_start_cleanup;

  Copy(&state->save, theTop(operandstack));
  pop(&operandstack);

  /* Good completion; link the new fixedpage state into the context. */
  xps_ctxt->callback_state = (xpsCallbackState*)state ;

  probe_begin(SW_TRACE_XPS_PAGE, (intptr_t)context->page->pageno);

  result = TRUE;

 fixedpage_start_cleanup:
  if ( !result ) {
    VERIFY_OBJECT(state, FIXEDPAGE_STATE_NAME) ;
    UNNAME_OBJECT(state) ;
    mm_free(mm_xml_pool, state, sizeof(xpsFixedPageState)) ;
  }

#undef return
  return result;
}

/** XPS FixedPage element end callback. */
static Bool xps_FixedPage_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  corecontext_t *context = get_core_context_interp();
  ps_context_t *pscontext = context->pscontext ;
  xmlDocumentContext *xps_ctxt ;
  xpsFixedPageState *state = NULL ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;

  probe_end(SW_TRACE_XPS_PAGE, (intptr_t)context->page->pageno);

  /* The cleanups rely on the state being present. */
  if ( !xps_fixedpage_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  /* TBD: what ought to happen in an error roll back? For now, do nothing.
   */
  if ( success )
    if (! push(&state->save, &operandstack) || ! restore_(pscontext))
      success = FALSE;

  if ( success )
    success = showpage_(pscontext) ;

  /** \todo TODO - Not clear from any documentation whether this should be
   * before or after page is rasterised. For example what about watermarks on
   * top of marked content? May need to split between pre- and
   * post-rasterisation calls, yeuch.
   */
  /* Must run any end of page config (regardless of success) */
  if ( !pt_config_end(&xps_ctxt->printticket, !success) )
    success = FALSE ;

  xps_ctxt->callback_state = state->base.next ;
  UNNAME_OBJECT(state) ;

  mm_free(mm_xml_pool, state, sizeof(xpsFixedPageState)) ;

  return success;
}

/** XPS FixedPage Resources property element start callback. */
static Bool xps_FixedPage_Resources_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  /* No attributes allowed. Also, no compatibility attributes are allowed on
     properties. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(ResourceDictionary), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(filter != NULL, "NULL filter");
  HQASSERT(localname != NULL, "NULL localname");

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  return TRUE;
}

/** XPS ResourceDictionary element start callback. */
static Bool xps_ResourceDictionary_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  static Bool source_set ;
  static xps_partname_t *sourcecheck ;
  /* We need auto's to handle recursive calls */
  xps_partname_t *source ;
  Bool status = FALSE ;
  xmlDocumentContext *xps_ctxt ;
  xpsXmlPartContext *xmlpart_ctxt ;
  xmlGFilterChain *filter_chain ;
  xmlGIStr *remote_resource_mimetype ;

  static XMLG_VALID_CHILDREN no_children[] = {
    XMLG_VALID_CHILDREN_END
  } ;

  static XMLG_VALID_CHILDREN remote_resource_doc_element[] = {
    { XML_INTERN(ResourceDictionary), XML_INTERN(ns_xps_2005_06), XMLG_ONE, XMLG_NO_GROUP },
    XMLG_VALID_CHILDREN_END
  } ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(Canvas), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP },
    { XML_INTERN(Glyphs), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP },
    { XML_INTERN(Path), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP },
    { XML_INTERN(SolidColorBrush), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP },
    { XML_INTERN(LinearGradientBrush), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP },
    { XML_INTERN(RadialGradientBrush), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP },
    { XML_INTERN(VisualBrush), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP },
    { XML_INTERN(ImageBrush), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP },
    { XML_INTERN(MatrixTransform), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP },
    { XML_INTERN(PathGeometry), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP },
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Source), NULL, &source_set, xps_convert_part_reference, &sourcecheck },
    XML_ATTRIBUTE_MATCH_END
  } ;

  static XPS_CONTENT_TYPES remote_resource_content_types[] = {
    { XML_INTERN(mimetype_xps_resourcedictionary) },
    XPS_CONTENT_TYPES_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "NULL xps_ctxt") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "NULL filter_chain") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt != NULL, "NULL xmlpart_ctxt") ;

  source = NULL ;
  sourcecheck = NULL ;
  source_set = FALSE ;

#define return DO_NOT_return_GO_TO_cleanup_INSTEAD!

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    goto cleanup ;

  /* Build auto's */
  source = sourcecheck ;

  if (source_set) {
    /* A remote resource is not allowed to reference a remote resource
       directly. This will catch the this error case at declaration
       time. */
    if (xps_ctxt->remote_resource) {
      (void)detail_error_handler(UNDEFINED, "Remote resource dictionaries must not reference another remote resource.") ;
      goto cleanup ;
    }

    /* If we are executing a remote resource. */
    if (xmlpart_ctxt->executing_stack != NULL &&
        xps_resource_is_executing(xmlpart_ctxt->executing_stack) &&
        xps_resource_is_remote(xmlpart_ctxt->executing_stack)) {
      (void)detail_error_handler(UNDEFINED, "Remote resource dictionaries must not reference another remote resource.") ;
      goto cleanup ;
    }

    /* If Source has been used, then no child elements are allowed. */
    if (! xmlg_valid_children(filter, localname, uri, no_children)) {
      (void) error_handler(UNDEFINED);
      goto cleanup ;
    }
    xps_ctxt->remote_resource = TRUE ;
    xps_ctxt->remote_resource_depth = xmlg_get_element_depth(filter) ;
    xps_ctxt->remote_resource_source = xmlpart_ctxt ;

    /* We do not look for a relationships part associated with a
       remote resource. */
    status = xps_parse_xml_from_partname(filter, source,
                                         0, /* load nothing */
                                         XPS_PART_VERSIONED,
                                         remote_resource_doc_element,
                                         XML_INTERN(rel_xps_2005_06_required_resource),
                                         remote_resource_content_types,
                                         &remote_resource_mimetype) ;

    xps_ctxt->remote_resource = FALSE ;
    xps_ctxt->remote_resource_depth = 0 ;
    xps_ctxt->remote_resource_source = NULL ;
  } else {
    if (! xmlg_valid_children(filter, localname, uri, valid_children)) {
      (void) error_handler(UNDEFINED) ;
      goto cleanup ;
    }

    status = xps_resources_start(filter, localname, uri, RES_SCOPE_WHERE_DECLARED) ;
  }

cleanup:
  if (source != NULL)
    xps_partname_free(&source) ;

#undef return
  return status ;
}

/** XPS MatrixTransform element start callback. */
static Bool xps_MatrixTransform_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;

  static XMLG_VALID_CHILDREN valid_children[] = {
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Matrix), NULL, NULL, xps_convert_ST_Matrix, NULL},
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(filter != NULL, "filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");

  /* Destination for captured matrix is set by surrounding element. */
  if ( xps_ctxt->transform == NULL )
    return error_handler(UNREGISTERED) ;

  match[0].data = xps_ctxt->transform ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;
  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  /* We now have the transform, prevent others from capturing it. */
  xps_ctxt->transform = NULL ;

  return TRUE;
}

/** XPS MatrixTransform element end callback. */
static Bool xps_MatrixTransform_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt;

  UNUSED_PARAM( xmlGFilter* , filter ) ;
  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  HQASSERT(filter != NULL, "filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");

  /* We should now have captured a transform. */
  if ( xps_ctxt->transform != NULL )
    return success && error_handler(UNREGISTERED) ;

  xps_ctxt->transform = NULL ;

  return success;
}

/*=============================================================================
 * Register functions
 *=============================================================================
 */
static xpsElementFuncts local_functions[] =
{
  { XML_INTERN(LinkTarget),
    xps_LinkTarget_Start,
    NULL,
    NULL /* No characters callback. */
  },
  { XML_INTERN(FixedPage),
    xps_FixedPage_Start,
    xps_FixedPage_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(FixedPage_Resources),
    xps_FixedPage_Resources_Start,
    NULL,
    NULL /* No characters callback. */
  },
  { XML_INTERN(ResourceDictionary),
    xps_ResourceDictionary_Start,
    NULL, /* no end element callback. */
    NULL /* No characters callback. */
  },
  { XML_INTERN(MatrixTransform),
    xps_MatrixTransform_Start,
    xps_MatrixTransform_End,
    NULL /* No characters callback. */
  },
  XPS_ELEMENTFUNCTS_END
};

Bool xmlcb_register_functs_fixedpage(
      xmlDocumentContext *xps_ctxt,
      xmlGFilter *filter)
{
  return (xps_register_cb_array(xps_ctxt, filter,
                                XML_INTERN(ns_xps_2005_06),
                                local_functions) &&
          xps_register_cb_array(xps_ctxt, filter,
                                XML_INTERN(ns_xps_2005_06),
                                documentsequence_functions) &&
          xps_register_cb_array(xps_ctxt, filter,
                                XML_INTERN(ns_xps_2005_06),
                                fixeddocument_functions) &&
          xps_register_cb_array(xps_ctxt, filter,
                                XML_INTERN(ns_xps_2005_06),
                                imagebrush_functions) &&
          xps_register_cb_array(xps_ctxt, filter,
                                XML_INTERN(ns_xps_2005_06),
                                gradientbrush_functions) &&
          xps_register_cb_array(xps_ctxt, filter,
                                XML_INTERN(ns_xps_2005_06),
                                visualbrush_functions) &&
          xps_register_cb_array(xps_ctxt, filter,
                                XML_INTERN(ns_xps_2005_06),
                                solidbrush_functions) &&
          xps_register_cb_array(xps_ctxt, filter,
                                XML_INTERN(ns_xps_2005_06),
                                canvas_functions) &&
          xps_register_cb_array(xps_ctxt, filter,
                                XML_INTERN(ns_xps_2005_06),
                                geometry_functions) &&
          xps_register_cb_array(xps_ctxt, filter,
                                XML_INTERN(ns_xps_2005_06),
                                glyph_functions) &&
          xps_register_cb_array(xps_ctxt, filter,
                                XML_INTERN(ns_xps_2005_06),
                                path_functions) &&
          xps_register_cb_array(xps_ctxt, filter,
                                XML_INTERN(ns_ggs_xps_2007_06),
                                fixed_page_extension_functions)) ;
}

/* ============================================================================
* Log stripped */
