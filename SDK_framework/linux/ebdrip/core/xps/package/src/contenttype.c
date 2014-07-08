/** \file
 * \ingroup xpscontent
 *
 * $HopeName: COREedoc!package:src:contenttype.c(EBDSDK_P.1) $
 * $Id: package:src:contenttype.c,v 1.37.10.1.1.1 2013/12/19 11:24:46 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of XPS content type stream callbacks.
 *
 * See localFunctions declaration at the end of this file for the element
 * callbacks this file implements.
 */

/** \defgroup xpscontent XPS content type stream callbacks.
    \ingroup xps */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "swctype.h"
#include "swerrors.h"
#include "xml.h"

#include "xps.h"
#include "xpspriv.h"
#include "xpsscan.h"
#include "xpspriv.h"

#include "namedef_.h"

/*=============================================================================
 * XML start/end element callbacks
 *=============================================================================
 */

static int32 xps_Types_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(Default), XML_INTERN(ns_package_2006_content_types), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP},
    { XML_INTERN(Override), XML_INTERN(ns_package_2006_content_types), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP },
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;
  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED) ;

  return TRUE ;
}

static int32 xps_Default_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt ;
  static xps_extension_t *extension ;
  static xmlGIStr *mimetype ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Extension), NULL, NULL, xps_convert_extension, &extension },
    { XML_INTERN(ContentType), NULL, NULL, xps_convert_mimetype, &mimetype },
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "NULL xps_ctxt") ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) ) {
    if (extension != NULL)
      xps_extension_free(&extension) ;
    return error_handler(UNDEFINED) ;
  }
  if (! xmlg_valid_children(filter, localname, uri, valid_children)) {
    xps_extension_free(&extension) ;
    return error_handler(UNDEFINED) ;
  }

  if (! xps_types_add_default(filter, extension, mimetype)) {
    xps_extension_free(&extension) ;
    return FALSE ;
  }

  xps_extension_free(&extension) ;
  return TRUE ;
}

static int32 xps_Override_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt ;
  static xps_partname_t *partname ;
  static xmlGIStr *mimetype ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    XMLG_VALID_CHILDREN_END
  } ;

  /* Note that the content type stream is the only XPS XML stream
     which uses part names. */
  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(PartName), NULL, NULL, xps_convert_partname, &partname },
    { XML_INTERN(ContentType), NULL, NULL, xps_convert_mimetype, &mimetype },
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "NULL xps_ctxt") ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE)) {
    if (partname != NULL)
      xps_partname_free(&partname) ;
    return error_handler(UNDEFINED) ;
  }
  if (! xmlg_valid_children(filter, localname, uri, valid_children)) {
    xps_partname_free(&partname) ;
    return error_handler(UNDEFINED) ;
  }

  if (! xps_types_add_override(filter, partname, mimetype)) {
    xps_partname_free(&partname) ;
    return FALSE ;
  }

  xps_partname_free(&partname) ;
  return TRUE ;
}

/*=============================================================================
 * Register functions
 *=============================================================================
 */

static xpsElementFuncts local_functions[] =
{
  { XML_INTERN(Types),
    xps_Types_Start,
    NULL, /* No end element callback. */
    NULL /* No characters callback. */
  },
  { XML_INTERN(Default),
    xps_Default_Start,
    NULL, /* No end element callback. */
    NULL /* No characters callback. */
  },
  { XML_INTERN(Override),
    xps_Override_Start,
    NULL, /* No end element callback. */
    NULL /* No characters callback. */
  },
  XPS_ELEMENTFUNCTS_END
} ;

Bool xmlcb_register_funcs_xps_contenttype(
      xmlDocumentContext *xps_ctxt,
      xmlGFilter *filter)
{
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;
  HQASSERT(filter != NULL, "filter is NULL") ;

  return xps_register_cb_array(xps_ctxt,
                               filter,
                               XML_INTERN(ns_package_2006_content_types),
                               local_functions) ;
} 

/* ============================================================================
* Log stripped */
