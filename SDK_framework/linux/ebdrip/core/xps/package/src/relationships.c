/** \file
 * \ingroup xpsrels
 *
 * $HopeName: COREedoc!package:src:relationships.c(EBDSDK_P.1) $
 * $Id: package:src:relationships.c,v 1.89.9.1.1.1 2013/12/19 11:24:46 anon Exp $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of xps relationship callbacks.
 *
 * See localFunctions declaration at the end of this file for the element
 * callbacks this file implements.
 */

/** \defgroup xpsrels XPS relationship callbacks
    \ingroup xps */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "hqmemcmp.h"
#include "hqmemcpy.h"
#include "swdevice.h"
#include "fileio.h"
#include "swerrors.h"
#include "monitor.h"
#include "xml.h"
#include "xmltypeconv.h"
#include "hqunicode.h"

#include "xpspriv.h"
#include "xpspt.h"
#include "xpsscan.h"
#include "xpsrelsblock.h"
#include "xpstypestream.h"
#include "package.h"
#include "discardStream.h"

#include "namedef_.h"

#define INCORRECT_CONTENT_TYPE_ERROR "Incorrect content type \"%.*s\" found for part %.*s."
#define TARGET_PART_MISSING_ERROR "Target part %.*s does not exist within package."

static Bool target_exists(
      hqn_uri_t *uri)
{
  STAT stat;
  Bool exists ;

  HQASSERT(uri != NULL, "uri is NULL") ;

  if (! stat_from_psdev_uri(uri, &exists, &stat))
    return error_handler(UNDEFINED) ;

  if (exists) {
    return TRUE ;
  } else {
    uint8* part ;
    uint32 part_len ;

    if (! hqn_uri_get_field(uri, &part, &part_len, HQN_URI_PATH)) {
      HQFAIL("Unable to get URI from partname.") ;
      return error_handler(UNDEFINED) ;
    }

    return detailf_error_handler(UNDEFINEDFILENAME, TARGET_PART_MISSING_ERROR,
                                 part_len, part) ;
  }
}

/*=============================================================================
 * XML start/end element callbacks
 *=============================================================================
 */
static int32 xps_Relationships_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(Relationship), XML_INTERN(ns_package_2006_relationships), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  /* No attributes allowed. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  HQASSERT(filter != NULL, "NULL filter") ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;
  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED) ;

  return TRUE ;
}

static int32 xps_Relationships_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt ;
  xmlGFilterChain *filter_chain ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  /* Extract and check arguments */
  HQASSERT(filter != NULL, "NULL filter") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "NULL xps_ctxt") ;

  if (! success)
    return success ;

  /* Must be _rels/.rels because no further XPS processing is done
     from xmlexec_ unless a fixed representation is found. */
  if (! xps_ctxt->startpart_loaded) {
    monitorf(UVS("%%%%[ Warning: Open Package Container does not contain an XPS Document - no output will be produced. ] %%%%\n")) ;
    return TRUE ;
  }

  return success ;
}

static int32 xps_Relationship_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt ;
  xmlGFilterChain *filter_chain ;
  xpsXmlPartContext *xmlpart_ctxt ;
  Bool status = TRUE ;
  Bool relationship_added = FALSE ;
  static Bool targetmode_set ;

  static xmlGIStr *typecheck, *targetmode ;
  static utf8_buffer id ;
  static xps_partname_t *target ;

  xmlGIStr *typecheck_local, *targetmode_local ;
  utf8_buffer id_local ;
  xps_partname_t *target_local ;

  xmlGIStr *type ;
  xmlGIStr *sequence_mimetype, *coreproperties_mimetype ;
  xmlGIStr *mimetype ;
  uint8* part ;
  uint32 part_len ;
  xmlGIStr *intern_id = NULL ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Target), NULL, NULL, xps_convert_part_reference, &target },
    { XML_INTERN(Type), NULL, NULL, xps_convert_Type, &typecheck },
    { XML_INTERN(Id), NULL, NULL, xml_convert_id, &id },
    { XML_INTERN(TargetMode), NULL, &targetmode_set, xps_convert_ST_TargetMode, &targetmode },
    XML_ATTRIBUTE_MATCH_END
  } ;

  static XMLG_VALID_CHILDREN sequence_doc_element[] = {
    { XML_INTERN(FixedDocumentSequence), XML_INTERN(ns_xps_2005_06), XMLG_ONE, XMLG_NO_GROUP },
    XMLG_VALID_CHILDREN_END
  } ;

  static XPS_CONTENT_TYPES sequence_content_types[] = {
    { XML_INTERN(mimetype_xps_fixeddocumentsequence) },
    XPS_CONTENT_TYPES_END
  } ;

  static XMLG_VALID_CHILDREN coreproperties_doc_element[] = {
    { XML_INTERN(coreProperties), XML_INTERN(ns_package_2006_metadata_core_properties), XMLG_ONE, XMLG_NO_GROUP },
    XMLG_VALID_CHILDREN_END
  } ;

  static XPS_CONTENT_TYPES coreproperties_content_types[] = {
    { XML_INTERN(mimetype_package_core_properties) },
    XPS_CONTENT_TYPES_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  /* Extract and check arguments */
  HQASSERT(filter != NULL, "NULL filter") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt != NULL, "NULL xmlpart_ctxt") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "NULL xps_ctxt") ;

  HQASSERT(xps_ctxt->startpart_xmlpart_ctxt != NULL, "startpart_xmlpart_ctxt is NULL") ;

  /* Init match statics. */
  targetmode = typecheck = type = NULL ;
  target = NULL ;

  if (status && ! xmlg_attributes_match(filter, localname, uri, attrs,
                                        match, TRUE))
    status = error_handler(UNDEFINED) ;

  /* Copy statics to locals as the statics may become corrupt when
     this callback goes recursive via another parse instance. */
  target_local = target ;
  typecheck_local = typecheck ;
  id_local = id ;
  targetmode_local = targetmode ;

  if (status && ! xmlg_valid_children(filter, localname, uri, valid_children))
    status = error_handler(UNDEFINED) ;

  if (status && ! xml_map_namespace(typecheck_local, (const xmlGIStr **)&type))
    status = FALSE ;

  /* Intern the Id. */
  if (status)
    status = intern_create(&intern_id, id_local.codeunits, id_local.unitlength) ;

  /* Process the relationships as soon as we see them. In the case of
     the _rels/.rels part, we keep parsing until we find the start
     part. */
  if (status) {

    if (! hqn_uri_get_field(target_local->uri, &part, &part_len,
                            HQN_URI_PATH)) {

      HQFAIL("Unable to get URI from partname.") ;
      status = error_handler(TYPECHECK) ;

    } else {

      switch ( XML_INTERN_SWITCH(type) ) {

      case XML_INTERN_CASE(rel_xps_2005_06_fixedrepresentation):
        /* Check that we have not already seen a fixe document
           sequence. */
        if (xps_ctxt->startpart_loaded)
          status = detail_error_handler(UNDEFINED,
                                        "Package has more than one start part relationship.") ;

        /* Check that the mimetype is correct. */
        if (status) {
          if (! xps_types_get_part_mimetype(filter, target_local, &mimetype)) {
            status = FALSE ;
          } else {
            if (mimetype != XML_INTERN(mimetype_xps_fixeddocumentsequence))
              status = detailf_error_handler(UNDEFINED, INCORRECT_CONTENT_TYPE_ERROR,
                                             intern_length(mimetype), intern_value(mimetype),
                                             part_len, part) ;
          }
        }

        /* Check that the fixed document sequence exists. */
        if (status)
            status = target_exists(target_local->uri) ;

        /* Add the relationship before we start processing the start
           part. */
        if (status)
          relationship_added = xps_add_relationship(xmlpart_ctxt->relationships,
                                                    intern_id, type, target_local, targetmode_local) ;
        status = relationship_added ;

        /* We have managed to get through all the validation so lets
           start processing the XPS package immediately so we do not
           consume any more of the _rels/.rels file. */

        if (status) {
          /* We have found a start part so record this fact. */
          xps_ctxt->startpart_loaded = TRUE ;

          /* And away we go on our merry way.. */
          status = xps_parse_xml_from_partname(filter, target_local,
                                               XPS_PROCESS_PRINTTICKET_REL,
                                               XPS_PART_VERSIONED,
                                               sequence_doc_element,
                                               XML_INTERN(rel_xps_2005_06_fixedrepresentation),
                                               sequence_content_types,
                                               &sequence_mimetype) ;
        }
        break ;

      case XML_INTERN_CASE(rel_xps_2005_06_printticket):
        if (status) {
          if (! xps_types_get_part_mimetype(filter, target_local, &mimetype)) {
            status = FALSE ;
          } else {
            if (mimetype != XML_INTERN(mimetype_printing_printticket))
              status = detailf_error_handler(UNDEFINED, INCORRECT_CONTENT_TYPE_ERROR,
                                             intern_length(mimetype), intern_value(mimetype),
                                             part_len, part) ;
          }

          if (status)
            status = target_exists(target_local->uri) ;

          if (status) {
            if (xmlpart_ctxt->printticket_relationship_seen)
              status = detail_error_handler(UNDEFINED,
                                            "Part has more than one printticket relationship.") ;
          }

          /* Add the relationship before we start processing the print
             ticket. */
          if (status)
            relationship_added = xps_add_relationship(xmlpart_ctxt->relationships,
                                                     intern_id, type, target_local, targetmode_local) ;
          status = relationship_added ;

          /* Process the print ticket. */
          if (status) {
            xmlpart_ctxt->printticket_relationship_seen = TRUE ;
            if (xmlpart_ctxt->base.relationships_to_process & XPS_PROCESS_PRINTTICKET_REL) {
              /* Merge and validate the PT part. */
              status = pt_mandv(filter, &xps_ctxt->printticket, target_local) ;
            }
          }
        }
        break ;

      case XML_INTERN_CASE(rel_package_2006_metadata_core_properties):
        if (status) {
          if (! xps_types_get_part_mimetype(filter, target_local, &mimetype)) {
            status = FALSE ;
          } else {
            if (mimetype != XML_INTERN(mimetype_package_core_properties))
              status = detailf_error_handler(UNDEFINED, INCORRECT_CONTENT_TYPE_ERROR,
                                             intern_length(mimetype), intern_value(mimetype),
                                             part_len, part) ;
          }

          if (status)
            status = target_exists(target_local->uri) ;

          if (xps_ctxt->coreproperties_relationship_seen) {
            status = detail_error_handler(UNDEFINED,
                                          "Package has more than one core properties relationship.") ;
          } else {
            xps_ctxt->coreproperties_relationship_seen = TRUE ;
          }

          if (status)
            relationship_added = xps_add_relationship(xmlpart_ctxt->relationships,
                                                     intern_id, type, target_local, targetmode_local) ;
          status = relationship_added ;

          if (status) {
            status = xps_parse_xml_from_partname(filter, target_local,
                                                 0, /* Core properties does not even have a relationships part. */
                                                 XPS_CORE_PROPERTIES,
                                                 coreproperties_doc_element,
                                                 XML_INTERN(rel_package_2006_metadata_core_properties),
                                                 coreproperties_content_types,
                                                 &coreproperties_mimetype) ;
          }
        }
        break ;

      case XML_INTERN_CASE(ns_xps_2005_06_discard_control):
        /* Discard control part - create a discard control parser. Note that if
        we don't see this relationship before the fixed representation
        relationship it's tough luck; reading to the end of the relationships
        file looking for a discard part would break streaming. */
        if (status) {
          if (! xps_open_discard_parser(target_local,
                                        &xps_ctxt->discard_parser)) {
            /* It's not an error to fail reading the discard control part,
            although failing this early probably means we're about to fail
            somewhere else anyway. */
            error_clear();
          }
          break;
        }

      case XML_INTERN_CASE(rel_package_2006_metadata_thumbnail):
      case XML_INTERN_CASE(rel_package_2006_dsig_origin):
      case XML_INTERN_CASE(rel_package_2006_dsig_signature):
      case XML_INTERN_CASE(rel_package_2006_dsig_certificate):
      case XML_INTERN_CASE(rel_xps_2005_06_required_resource):
      case XML_INTERN_CASE(rel_xps_2005_06_restricted_font):
      case XML_INTERN_CASE(rel_xps_2005_06_annotations):
      default:
        /* We ignore relationships we don't know about. */
        if (status)
          relationship_added = xps_add_relationship(xmlpart_ctxt->relationships,
                                        intern_id, type, target_local, targetmode_local) ;
        status = relationship_added ;
        break ;
      }
    }
  }

  /* Only clean up the target on an error condition as the
     relationships block will take control of de-allocation on
     success. */
  if (! status && target_local != NULL && ! relationship_added)
    xps_partname_free(&target_local) ;

  return status ;
}

/*=============================================================================
 * Register functions
 *=============================================================================
 */

static xpsElementFuncts local_functions[] =
{
  { XML_INTERN(Relationship),
    xps_Relationship_Start,
    NULL, /* No end element callback. */
    NULL /* No characters callback. */
  },
  { XML_INTERN(Relationships),
    xps_Relationships_Start,
    xps_Relationships_End,
    NULL /* No characters callback. */
  },
  XPS_ELEMENTFUNCTS_END
} ;

Bool xmlcb_register_funcs_xps_relationships(
      xmlDocumentContext *xps_ctxt,
      xmlGFilter *filter)
{
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;
  HQASSERT(filter != NULL, "filter is NULL") ;

  return xps_register_cb_array(xps_ctxt,
                               filter,
                               XML_INTERN(ns_package_2006_relationships),
                               local_functions) ;
}

/* ============================================================================
* Log stripped */
