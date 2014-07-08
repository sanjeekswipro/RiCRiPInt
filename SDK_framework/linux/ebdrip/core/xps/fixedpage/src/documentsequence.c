/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!fixedpage:src:documentsequence.c(EBDSDK_P.1) $
 * $Id: fixedpage:src:documentsequence.c,v 1.64.2.1.1.1 2013/12/19 11:24:47 anon Exp $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of XPS document sequence callbacks.
 */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "swerrors.h"
#include "xml.h"
#include "xmltypeconv.h"
#include "fixedpagepriv.h"

#include "xpstypestream.h"
#include "xpspriv.h"
#include "xpsscan.h"
#include "xpsiccbased.h"
#include "gsc_icc.h"  /* gsc_purgeInactiveICCProfileInfo() */

#include "namedef_.h"

/*=============================================================================
 * XML start/end element callbacks
 *=============================================================================
 */
static int32 xps_DocumentReference_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt ;
  Bool status = FALSE ;
  xmlGIStr *fixeddocument_mimetype ;
  static xps_partname_t *sourcecheck ;
  /* We need auto's to handle recursive calls */
  xps_partname_t *source ;

  static XMLG_VALID_CHILDREN fixeddocument_doc_element[] = {
    { XML_INTERN(FixedDocument), XML_INTERN(ns_xps_2005_06), XMLG_ONE, XMLG_NO_GROUP },
    XMLG_VALID_CHILDREN_END
  } ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Source), NULL, NULL, xps_convert_part_reference, &sourcecheck },
    XML_ATTRIBUTE_MATCH_END
  } ;

  static XPS_CONTENT_TYPES fixeddocument_content_types[] = {
    { XML_INTERN(mimetype_xps_fixeddocument) },
    XPS_CONTENT_TYPES_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "NULL xps_ctxt") ;

#define return DO_NOT_return_GO_TO_cleanup_INSTEAD!

  source = NULL ;
  sourcecheck = NULL ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE)) {
    (void) error_handler(UNDEFINED);
    goto cleanup ;
  }
  if (! xmlg_valid_children(filter, localname, uri, valid_children)) {
    (void) error_handler(UNDEFINED);
    goto cleanup ;
  }

  /* Build auto's */
  source = sourcecheck ;

  if (xps_have_processed_part(filter, source)) {
    (void)detailf_error_handler(UNDEFINED, "FixedDocument %.*s is referenced more than once.",
                                intern_length(source->norm_name),
                                intern_value(source->norm_name)) ;
    goto cleanup ;
  }

  if (! xps_mark_part_as_processed(filter, source))
    goto cleanup ;

  /* There is no relationship type specified from a document sequence
     part to a fixed document part. */
  status = xps_parse_xml_from_partname(filter, source,
                                       XPS_PROCESS_PRINTTICKET_REL,
                                       XPS_PART_VERSIONED | XPS_PART_SIGNED,
                                       fixeddocument_doc_element,
                                       NULL, /* relationship */
                                       fixeddocument_content_types,
                                       &fixeddocument_mimetype) ;

cleanup:
  if (source != NULL)
    xps_partname_free(&source) ;

#undef return
  return status ;
}

static int32 xps_FixedDocumentSequence_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext* xps_ctxt;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(DocumentReference), XML_INTERN(ns_xps_2005_06), XMLG_ONE_OR_MORE, XMLG_NO_GROUP },
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(filter != NULL, "NULL filter") ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    return error_handler(UNDEFINED) ;
  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED) ;

  xps_ctxt = xmlg_get_user_data(filter) ;

  /* (Re)-initialise the default sRGB and scRGB colorspaces */
  xps_sRGB_reset();
  xps_scRGB_reset();

  /* Force the scRGBspace into the icc profile cache */
  if ( !set_xps_scRGB( GSC_FILL ))
    goto tidyup;

  /* Set up the default sRGB colorSpace */
  if ( !set_xps_sRGB( GSC_FILL ) ||
       !set_xps_sRGB( GSC_STROKE ))
    goto tidyup;

  /* Run any start of job config */
  if ( !pt_config_start(&xps_ctxt->printticket) ) {
    goto tidyup;
  }

  return TRUE ;

tidyup:
  xps_sRGB_reset();
  xps_scRGB_reset();
  return FALSE;
}

static int32 xps_FixedDocumentSequence_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "NULL xps_ctxt") ;

  /* Must run any end of job config */
  success = pt_config_end(&xps_ctxt->printticket, !success) && success;

  /* Purge the whole xps iccbased cache */
  xps_icc_cache_purge(-1);
  gsc_purgeInactiveICCProfileInfo(frontEndColorState);

  /* Lose the reference to the default sRGB and scRGB spaces */
  xps_sRGB_reset();
  xps_scRGB_reset();

  if (!success)
    return FALSE ;

  return(success);
}

/*=============================================================================
 * Register functions
 *=============================================================================
 */

xpsElementFuncts documentsequence_functions[] =
{
  { XML_INTERN(DocumentReference),
    xps_DocumentReference_Start,
    NULL, /* No end element callback. */
    NULL /* No characters callback. */
  },
  { XML_INTERN(FixedDocumentSequence),
    xps_FixedDocumentSequence_Start,
    xps_FixedDocumentSequence_End,
    NULL /* No characters callback. */
  },
  XPS_ELEMENTFUNCTS_END
} ;

/* ============================================================================
* Log stripped */
