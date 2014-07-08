/** \file
 * \ingroup xpsprops
 *
 * $HopeName: COREedoc!package:src:coreproperties.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of XPS core properties callbacks.
 *
 * See localFunctions declaration at the end of this file for the element
 * callbacks this file implements.
 */

/** \defgroup xpsprops XPS Core Properties
    \ingroup xps */

#include "core.h"
#include "hqmemcmp.h"
#include "hqmemcpy.h"
#include "swdevice.h"
#include "fileio.h"
#include "swerrors.h"
#include "monitor.h"
#include "xml.h"
#include "xmltypeconv.h"

#include "xpspriv.h"
#include "package.h"

#include "namedef_.h"

static Bool ignore_characters = FALSE ;

/*=============================================================================
 * XML start/end element callbacks
 *=============================================================================
 */
static int32 xps_coreProperties_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(creator), XML_INTERN(ns_purl_org_dc_elements_1_1), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP },
    { XML_INTERN(created), XML_INTERN(ns_purl_org_dc_terms), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP },
    { XML_INTERN(identifier), XML_INTERN(ns_purl_org_dc_elements_1_1), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP },
    { XML_INTERN(contentType), XML_INTERN(ns_package_2006_metadata_core_properties), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP },
    { XML_INTERN(title), XML_INTERN(ns_purl_org_dc_elements_1_1), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP },
    { XML_INTERN(subject), XML_INTERN(ns_purl_org_dc_elements_1_1), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP },
    { XML_INTERN(description), XML_INTERN(ns_purl_org_dc_elements_1_1), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP },
    { XML_INTERN(keywords), XML_INTERN(ns_package_2006_metadata_core_properties), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP },
    { XML_INTERN(language), XML_INTERN(ns_purl_org_dc_elements_1_1), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP },
    { XML_INTERN(category), XML_INTERN(ns_package_2006_metadata_core_properties), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP },
    { XML_INTERN(version), XML_INTERN(ns_package_2006_metadata_core_properties), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP },
    { XML_INTERN(revision), XML_INTERN(ns_package_2006_metadata_core_properties), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP },
    { XML_INTERN(lastModifiedBy), XML_INTERN(ns_package_2006_metadata_core_properties), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP },
    { XML_INTERN(modified), XML_INTERN(ns_purl_org_dc_terms), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP },
    { XML_INTERN(lastPrinted), XML_INTERN(ns_package_2006_metadata_core_properties), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP },
    { XML_INTERN(contentStatus), XML_INTERN(ns_package_2006_metadata_core_properties), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP },
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(filter != NULL, "NULL filter") ;

  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "NULL xps_ctxt") ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    return error_handler(UNDEFINED) ;
  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED) ;

  return TRUE ;
}

static int32 xps_InternalProperty_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlGFilterChain *filter_chain ;
  xpsXmlPartContext *xmlpart_ctxt ;

  static utf8_buffer dublin_type, dublin_value ;
  static Bool dublin_type_set, dublin_value_set ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    XMLG_VALID_CHILDREN_END
  } ;

  /* The following restrictions MUST be applied to every XML document
     instance that contains Open Packaging Conventions core properties:

     1. Document elements MUST NOT contain refinements to the Dublin
        Core elements, except for the two specified in the schema
        (<dcterms:created> and <dcterms:modified>) [MG.1].

     2. Document elements MUST NOT contain the xml:lang attribute
        [MG.2]. For Dublin Core elements, this restriction is enforced
        by applications.

     3. Document elements MUST NOT contain the xsi:type attribute,
        except for <dcterms:created> and <dcters:modified> elements
        where it MUST be present and MUST hold the value
        "dcterms:W3CDTF" [MG.3].0. */

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(value), NULL, &dublin_value_set, xps_convert_dublin_value, &dublin_value },
    XML_ATTRIBUTE_MATCH_END
  } ;

  static XML_ATTRIBUTE_MATCH match_with_type[] = {
    { XML_INTERN(type), XML_INTERN(ns_w3_xml_xsi), &dublin_type_set, xps_convert_dublin_type, &dublin_type },
    { XML_INTERN(value), NULL, &dublin_value_set, xps_convert_dublin_value, &dublin_value },
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  /* Extract and check arguments */
  HQASSERT(filter != NULL, "NULL filter") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;

  ignore_characters = FALSE ;

  /* Note that this callback does not get called unless the namespace
     is correct, so we don't need to check it. */
  if (localname == XML_INTERN(created) || localname == XML_INTERN(modified)) {
    if (! xmlg_attributes_match(filter, localname, uri, attrs, match_with_type, TRUE) )
      return error_handler(UNDEFINED) ;

    { /* Check that the value is equal to dcterms:W3CDTF */
      uint8 ch, *in, *limit, *prefix ;
      uint32 prefix_len = 0 ;
      in = dublin_type.codeunits ;
      limit = in + dublin_type.unitlength ;
      prefix = in ;

      while (in != limit) {
        ch = *(in++) ;
        if (ch == ':') {
          prefix_len = CAST_PTRDIFFT_TO_UINT32(in - dublin_type.codeunits) - 1 ;
          break ;
        }
      }

      if (in == limit)
        return error_handler(UNDEFINED) ;

      { /* Resolve prefix and check prefix. */
        const xmlGIStr *istr_prefix ;
        const xmlGIStr *istr_type_uri ;

        if (! intern_create(&istr_prefix, prefix, prefix_len))
          return error_handler(VMERROR) ;

        if (! xmlg_get_namespace_uri(filter, istr_prefix, &istr_type_uri))
          return error_handler(UNDEFINED) ;

        if (istr_type_uri != XML_INTERN(ns_purl_org_dc_terms))
          return error_handler(UNDEFINED) ;
      }

      if (HqMemCmp(in, CAST_PTRDIFFT_TO_INT32(limit - in), NAME_AND_LENGTH("W3CDTF")) != 0)
        return error_handler(UNDEFINED) ;
    }

  } else {
    if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
      return error_handler(UNDEFINED) ;
  }

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED) ;

  HQASSERT(! xmlpart_ctxt->within_element,
           "within element has been left on") ;

  xmlpart_ctxt->within_element = TRUE ;

  monitorf((uint8*)"%s%.*s: ", UTF8_BOM, intern_length(localname),
           intern_value(localname)) ;

  if (dublin_value_set) {
    monitorf((uint8*)"%s%.*s", UTF8_BOM, dublin_value.unitlength, dublin_value.codeunits) ;
    ignore_characters = TRUE ;
  }

  return TRUE ;
}

static int32 xps_InternalProperty_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlGFilterChain *filter_chain ;
  xpsXmlPartContext *xmlpart_ctxt ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  /* Extract and check arguments */
  HQASSERT(filter != NULL, "NULL filter") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;

  xmlpart_ctxt->within_element = FALSE ;

  monitorf((uint8*)"\n") ;

  ignore_characters = FALSE ; /* Just to be tidy. */

  return success ;
}

static int32 xps_InternalProperty_character_cb(
      xmlGFilter *filter,
      const uint8 *buf,
      uint32 buflen)
{
  xmlGFilterChain *filter_chain ;
  xpsXmlPartContext *xmlpart_ctxt ;

  HQASSERT(filter != NULL, "NULL filter") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt != NULL, "NULL xmlpart_ctxt") ;

  HQASSERT(xmlpart_ctxt->within_element,
           "within element is not set") ;

  if (! ignore_characters)
    monitorf((uint8*)"%s%.*s", UTF8_BOM, buflen, buf) ;

  return XMLG_RESULT_FORWARD ;
}


/*=============================================================================
 * Register functions
 *=============================================================================
 */

/* Element names in the OPC core properties namespace. */
static xpsElementFuncts local_functions_one[] =
{
  { XML_INTERN(coreProperties),
    xps_coreProperties_Start,
    NULL, /* No end element callback. */
    NULL /* No characters callback. */
  },
  { XML_INTERN(contentType),
    xps_InternalProperty_Start,
    xps_InternalProperty_End,
    xps_InternalProperty_character_cb
  },
  { XML_INTERN(keywords),
    xps_InternalProperty_Start,
    xps_InternalProperty_End,
    xps_InternalProperty_character_cb
  },
  { XML_INTERN(category),
    xps_InternalProperty_Start,
    xps_InternalProperty_End,
    xps_InternalProperty_character_cb
  },
  { XML_INTERN(version),
    xps_InternalProperty_Start,
    xps_InternalProperty_End,
    xps_InternalProperty_character_cb
  },
  { XML_INTERN(revision),
    xps_InternalProperty_Start,
    xps_InternalProperty_End,
    xps_InternalProperty_character_cb
  },
  { XML_INTERN(lastModifiedBy),
    xps_InternalProperty_Start,
    xps_InternalProperty_End,
    xps_InternalProperty_character_cb
  },
  { XML_INTERN(contentStatus),
    xps_InternalProperty_Start,
    xps_InternalProperty_End,
    xps_InternalProperty_character_cb
  },
  { XML_INTERN(lastPrinted),
    xps_InternalProperty_Start,
    xps_InternalProperty_End,
    xps_InternalProperty_character_cb
  },
  XPS_ELEMENTFUNCTS_END
} ;

/* Element names in the dc: namespace. */
static xpsElementFuncts local_functions_two[] =
{
  { XML_INTERN(creator),
    xps_InternalProperty_Start,
    xps_InternalProperty_End,
    xps_InternalProperty_character_cb
  },
  { XML_INTERN(identifier),
    xps_InternalProperty_Start,
    xps_InternalProperty_End,
    xps_InternalProperty_character_cb
  },
  { XML_INTERN(title),
    xps_InternalProperty_Start,
    xps_InternalProperty_End,
    xps_InternalProperty_character_cb
  },
  { XML_INTERN(subject),
    xps_InternalProperty_Start,
    xps_InternalProperty_End,
    xps_InternalProperty_character_cb
  },
  { XML_INTERN(description),
    xps_InternalProperty_Start,
    xps_InternalProperty_End,
    xps_InternalProperty_character_cb
  },
  { XML_INTERN(language),
    xps_InternalProperty_Start,
    xps_InternalProperty_End,
    xps_InternalProperty_character_cb
  },
  XPS_ELEMENTFUNCTS_END
} ;

/* Element names in the dcterms: namespace. */
static xpsElementFuncts local_functions_three[] =
{
  { XML_INTERN(created),
    xps_InternalProperty_Start,
    xps_InternalProperty_End,
    xps_InternalProperty_character_cb
  },
  { XML_INTERN(modified),
    xps_InternalProperty_Start,
    xps_InternalProperty_End,
    xps_InternalProperty_character_cb
  },
  XPS_ELEMENTFUNCTS_END
} ;

Bool xmlcb_register_funcs_xps_coreproperties(
      xmlDocumentContext *xps_ctxt,
      xmlGFilter *filter)
{
  Bool status ;

  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;
  HQASSERT(filter != NULL, "filter is NULL") ;

  status = xps_register_cb_array(xps_ctxt,
                                 filter,
                                 XML_INTERN(ns_package_2006_metadata_core_properties),
                                 local_functions_one) ;
  if (status)
    status = xps_register_cb_array(xps_ctxt,
                                   filter,
                                   XML_INTERN(ns_purl_org_dc_elements_1_1),
                                   local_functions_two) ;

  if (status)
    status = xps_register_cb_array(xps_ctxt,
                                   filter,
                                   XML_INTERN(ns_purl_org_dc_terms),
                                   local_functions_three) ;
  return status ;
}

void init_C_globals_coreproperties(void)
{
  ignore_characters = FALSE ;
}

/* ============================================================================
* Log stripped */
