/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfMetadata.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2011 Global Graphics Software Ltd. All Rights Reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 */

#include "core.h"
#include "objects.h"
#include "pdfMetadata.h"

#include "swpdf.h"
#include "xml.h"
#include "pdfx.h"
#include "pdfmem.h"
#include "hqmemcpy.h"
#include "dicthash.h"
#include "namedef_.h"
#include "uvms.h"
#include "monitor.h"

/** The maximum length of an element name, including its prefix. */
#define MAX_ELEMENT_NAME_LENGTH 2048

/**
 * This structure holds data used by the callbacks within the Metadata filter.
 */
typedef struct MetadataPrivate {
  PDFCONTEXT* pdfc;
  OBJECT* dictionary;
} MetadataPrivate;

/**
 * Insert the string defined by 'buf' and 'buflen' into the metadata
 * dictionary, using the passed key.
 */
static Bool dictInsert(xmlGFilter *filter,
                       NAMECACHE* namecache,
                       const uint8 *buf,
                       uint32 buflen)
{
  MetadataPrivate* data = xmlg_get_user_data(filter);
  OBJECT string = OBJECT_NOTVM_NOTHING;
  OBJECT name = OBJECT_NOTVM_NOTHING;
  int32 dictSize;

  object_store_namecache(&name, namecache, LITERAL);

  if (! pdf_create_string(data->pdfc, buflen, &string))
    return FALSE;

  HqMemCpy(oString(string), buf, buflen);

  /* Non-VM dictionaries cannot be enlarged; assert it's not too small. */
  getDictLength(dictSize, data->dictionary);
  HQASSERT(dictSize < getDictMaxLength(oDict(*data->dictionary)),
           "Metadata dictionary too small.");

  return pdf_fast_insert_hash(data->pdfc, data->dictionary, &name, &string);
}

/**
 * Character data callback for pdf elements.
 */
static Bool pdfCDataCallback(xmlGFilter *filter, const uint8 *buf, uint32 buflen)
{
  const xmlGIStr* name;
  const xmlGIStr* prefix;
  NAMECACHE* elementName;
  uint32 fullLength;
  uint8 temp[MAX_ELEMENT_NAME_LENGTH];

  xmlg_get_current_element(filter, &name, &prefix);

  /* Note we add one for the ':' between prefix and name. */
  fullLength = prefix->name.len + name->name.len + 1;
  if (fullLength > MAX_ELEMENT_NAME_LENGTH) {
    HQFAIL("Element name to long.");
    return FALSE;
  }

  /* Combine prefix and name and insert into name cache. */
  HqMemCpy(temp, prefix->name.clist, prefix->name.len);
  temp[prefix->name.len] = ':';
  HqMemCpy(temp + prefix->name.len + 1, name->name.clist, name->name.len);
  elementName = cachename(temp, fullLength);

  if (elementName == NULL) {
    return FALSE;
  }

  return dictInsert(filter, elementName, buf, buflen);
}

/**
 * Register a the PDF/X detection filter with the passed filter chain at the
 * specified position.
 */
static Bool createMetadataFilter(xmlGFilterChain* filterChain,
                                 uint32 position, MetadataPrivate* data)
{
  xmlGFilter* filter = NULL;
  int32 i;
  struct {
    xmlGIStr* namespace;
    xmlGIStr* name;
  } elements[] = {
    { XML_INTERN(ns_pdfx_id), XML_INTERN(GTS_PDFXVersion) },
    { XML_INTERN(ns_pdf), XML_INTERN(Trapped) },
    { XML_INTERN(ns_xmp_mm), XML_INTERN(DocumentID) },
    { XML_INTERN(ns_xmp_mm), XML_INTERN(VersionID) },
    { XML_INTERN(ns_xmp_mm), XML_INTERN(RenditionClass) },
    { XML_INTERN(ns_st_ref), XML_INTERN(documentID) },
    { XML_INTERN(ns_st_ref), XML_INTERN(versionID) },
    { XML_INTERN(ns_st_ref), XML_INTERN(renditionClass) },
  };

  if (! xmlg_fc_new_filter(filterChain, &filter, position, data, NULL))
    return FALSE;

  /* Note that we don't try to destroy the filter if we error now; we assume
  the containing chain will destroy it in the error case. */

  for (i = 0; i < NUM_ARRAY_ITEMS(elements); i ++) {
    if (! xmlg_register_characters_cb(filter, elements[i].name,
                                      elements[i].namespace,
                                      pdfCDataCallback)) {
      return FALSE;
    }
  }

  return TRUE;
}

/** Error callback.
 */
static void metadataParseError(xmlGParser *parser, hqn_uri_t *uri,
                               uint32 line, uint32 column,
                               uint8 *detail, int32 detailLength)
{
  UNUSED_PARAM(xmlGParser*, parser);
  UNUSED_PARAM(hqn_uri_t*, uri);
  UNUSED_PARAM(uint32, line);
  UNUSED_PARAM(uint32, column);

  if (detail == NULL || detailLength == 0)
    monitorf(UVS("Error parsing document metadata.\n"));
  else
    monitorf(UVM("Error parsing document metadata: %.*s\n"), detailLength, detail);
}

/**
 * Parse the metadata in the passed stream.
 * Metadata keys are returned in the passed dictionary (which will be allocated
 * by this method, but must be deallocated by the client).
 * Keys in the dictionary will take the form of the element name in the
 * metadata, e.g. NAME_pdfxid_GTS_PDFVersion - note that the
 * namespace prefix is included to avoid clashes between elements with the same
 * name in different namespaces.
 * Only specific elements will be added to the result dictionary - see
 * createMetadataFilter() for the list of element callbacks registered.
 *
 * \return FALSE on error.
 */
Bool pdfMetadataParse(PDFCONTEXT* pdfc, FILELIST* metadataStream,
                      OBJECT* dictionary)
{
  Bool result = FALSE;
  XMLExecContext* context = NULL;
  hqn_uri_t* uri = NULL;
  uint8* uriString = (uint8*)"http://www.globalgraphics.com/pdfxml";
  xmlGFilterChain* filterChain = NULL;
  MetadataPrivate data;

  data.pdfc = pdfc;
  data.dictionary = dictionary;

  /* Initialise XML context and create XML filter chain. */
  if (! xmlexec_context_create(&context))
    return FALSE;

  if (! hqn_uri_parse(core_uri_context, &uri, uriString,
                      strlen_uint32((char*)uriString), TRUE))
    goto cleanup;

  if (! xmlg_fc_new(core_xml_subsystem, &filterChain, &xmlexec_memory_handlers,
                    uri, uri, NULL))
    goto cleanup;

  xmlg_fc_set_parse_error_cb(filterChain, metadataParseError);

  /* Create the dictionary used to pass metadata values to the client. */
  if (! pdf_create_dictionary(pdfc, 10, dictionary))
    goto cleanup;

  /* Add any filters to the chain. */

  if (! createMetadataFilter(filterChain, 0, &data))
    goto cleanup;

  /* Parse the metadata XML. Note that we don't treat any errors within the
  metadata parse as fatal. */
  if (! xml_parse_stream(metadataStream, filterChain))
    monitorf(UVS("Document metadata could not be read and will be ignored.\n"));

  result = TRUE;

cleanup:
  hqn_uri_free(&uri);

  if (filterChain != NULL)
    xmlg_fc_destroy(&filterChain);

  if (context != NULL)
    xmlexec_context_destroy(&context);

  if (!result && oType(*dictionary) != ONULL) {
    pdf_freeobject(pdfc, dictionary);
    object_store_null(dictionary);
  }

  return result;
}

/* Log stripped */

