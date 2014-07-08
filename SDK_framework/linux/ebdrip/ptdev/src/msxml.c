/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWptdev!src:msxml.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */
#include <string.h>

#include "ptincs.h"
#include "printticket.h"

#include "prnttckt.h"
#include "ggxml.h"


/**
 * @file
 * @brief MS XML interfaces.
 */

/**< Parsed PrintTicket XML internal structure */
PRINTTICKET* pt_new;


static
void ms_printticket_start(
  void*         data,
  const char**  attributes)
{
  int32 index;
  int32 val;

  UNUSED_PARAM(void*, data);

  if ( !pt_initialise(&pt_new) ) {
    pt_xml_set_error(XPSPT_ERROR_OUTOFMEM, (uint8*)"Out of memory creating PrintTicket.");
    return;
  }
  index = xml_find_attr((char**)attributes, (uint8*)"version");
  if ( index < 0 ) {
    pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"PrintTicket version attribute missing.");
    return;
  }
  if ( !xml_get_integer((char*)attributes[index + 1], &val) ) {
    pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"PrintTicket version attribute not an integer.");
    return;
  }
  if ( val > 1 ) {
    pt_xml_set_error(XPSPT_ERROR_RANGECHECK, (uint8*)"PrintTicket version attribute value not recognised.");
  }
}

static
void ms_printticket_end(
  void*         data)
{
  int32 pnCanHave[] = {PT_PARAM_FEATURE, PT_PARAM_PROPERTY, PT_PARAM_PARAMETERINIT, PT_PARAM_CHECK_END};

  UNUSED_PARAM(void*, data);

  /* At this stage the print ticket has been parsed and should now be merge and
   * validated according to the scope */
  if ( !pt_complete(pt_new) ) {
    pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"Incomplete print ticket.");
    return;
  }

  if (! pt_param_has_valid_children (pt_new, pnCanHave))
  {
    pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"PrintTicket has invalid children.");
    return;
  }
}

static
void ms_feature_start(
  void*         data,
  const char**  attributes)
{
  int32   index;
  XML_QNAME*  qname;
  PTUserData* pUserData = (PTUserData*) data;

  index = xml_find_attr((char**)attributes, (uint8*)"name");
  if ( index < 0 ) {
    pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"Feature name attribute missing.");
    return;
  }
  if ( !xml_get_qname(pUserData, (char*)attributes[index + 1], &qname) ) {
    pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"Feature name attribute is not a qualified name.");
    return;
  }
  if ( !pt_add_new_param(pt_new, PT_PARAM_FEATURE, qname) ) {
    xml_qname_free(&qname);
    pt_xml_set_error(XPSPT_ERROR_OUTOFMEM, (uint8*)"Out of memory adding Feature.");
  }
}

static
void ms_feature_end(
  void*         data)
{
  int32 pnMustHave[] = {PT_PARAM_FEATURE, PT_PARAM_OPTION, PT_PARAM_CHECK_END};
  int32 pnCanHave[] = {PT_PARAM_FEATURE, PT_PARAM_OPTION, PT_PARAM_PROPERTY, PT_PARAM_CHECK_END};

  UNUSED_PARAM(void*, data);

  if (! pt_param_has_required_children (pt_new, PT_ONE_OR_MORE, pnMustHave) ||
      ! pt_param_has_valid_children (pt_new, pnCanHave))
  {
    pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"Feature element has invalid children.");
    return;
  }

  /* Note that this element has finished */
  pt_end_param(pt_new);
}

static
void ms_option_start(
  void*         data,
  const char**  attributes)
{
  int32   index;
  XML_QNAME*  qname = NULL;
  PTUserData* pUserData = (PTUserData*) data;

  /* Option elements are not required to always have a name attribute */
  index = xml_find_attr((char**)attributes, (uint8*)"name");
  if ( index >= 0 ) {
    if ( !xml_get_qname(pUserData, (char*)attributes[index + 1], &qname) ) {
      pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"Option name attribute is not a qualified name.");
      return;
    }
  }
  if ( !pt_add_new_param(pt_new, PT_PARAM_OPTION, qname) ) {
    xml_qname_free(&qname);
    pt_xml_set_error(XPSPT_ERROR_OUTOFMEM, (uint8*)"Out of memory adding Option.");
  }
}

static
void ms_option_end(
  void*         data)
{
  int32 pnCanHave[] = {PT_PARAM_SCOREDPROPERTY, PT_PARAM_PROPERTY, PT_PARAM_CHECK_END};

  UNUSED_PARAM(void*, data);

  if (! pt_param_has_valid_children (pt_new, pnCanHave))
  {
    pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"Option element has invalid children.");
    return;
  }

  /* Note that this element has finished */
  pt_end_param(pt_new);
}

static
void ms_scoredproperty_start(
  void*         data,
  const char**  attributes)
{
  int32   index;
  XML_QNAME* qname;
  PTUserData* pUserData = (PTUserData*) data;

  index = xml_find_attr((char**)attributes, (uint8*)"name");
  if ( index < 0 ) {
    pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"ScoredProperty name attribute missing.");
    return;
  }
  if ( !xml_get_qname(pUserData, (char*)attributes[index + 1], &qname) ) {
    pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"ScoredProperty name attribute is not a qualified name.");
    return;
  }
  if ( !pt_add_new_param(pt_new, PT_PARAM_SCOREDPROPERTY, qname) ) {
    xml_qname_free(&qname);
    pt_xml_set_error(XPSPT_ERROR_OUTOFMEM, (uint8*)"Out of memory adding ScoredProperty.");
  }
}

static
void ms_scoredproperty_end(
  void*         data)
{
  int32 pnMustHave[] = {PT_PARAM_PARAMETERREF, PT_PARAM_VALUE, PT_PARAM_CHECK_END};
  int32 pnCanHave[] = {PT_PARAM_PARAMETERREF, PT_PARAM_VALUE, PT_PARAM_SCOREDPROPERTY, PT_PARAM_PROPERTY, PT_PARAM_CHECK_END};

  UNUSED_PARAM(void*, data);

  if (! pt_param_has_required_children (pt_new, PT_ONE_OF, pnMustHave) ||
      ! pt_param_has_valid_children (pt_new, pnCanHave))
  {
    pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"ScoredProperty element has invalid children.");
    return;
  }

  /* Note that this element has finished */
  pt_end_param(pt_new);
}

static
void ms_property_start(
  void*         data,
  const char**  attributes)
{
  int32   index;
  XML_QNAME* qname;
  PTUserData* pUserData = (PTUserData*) data;

  index = xml_find_attr((char**)attributes, (uint8*)"name");
  if ( index < 0 ) {
    pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"Property name attribute missing.");
    return;
  }
  if ( !xml_get_qname(pUserData, (char*)attributes[index + 1], &qname) ) {
    pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"Property name attribute is not a qualified name.");
    return;
  }
  if ( !pt_add_new_param(pt_new, PT_PARAM_PROPERTY, qname) ) {
    xml_qname_free(&qname);
    pt_xml_set_error(XPSPT_ERROR_OUTOFMEM, (uint8*)"Out of memory adding Property.");
  }
}

static
void ms_property_end(
  void*         data)
{
  int32 pnMustHave[] = {PT_PARAM_VALUE, PT_PARAM_PROPERTY, PT_PARAM_CHECK_END};

  UNUSED_PARAM(void*, data);

  if (! pt_param_has_required_children (pt_new, PT_ONE_OR_MORE, pnMustHave))
  {
    pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"Property element has invalid children.");
    return;
  }

  /* Note that this element has finished */
  pt_end_param(pt_new);
}

static
void ms_parameterinit_start(
  void*         data,
  const char**  attributes)
{
  int32   index;
  XML_QNAME* qname;
  PTUserData* pUserData = (PTUserData*) data;

  index = xml_find_attr((char**)attributes, (uint8*)"name");
  if ( index < 0 ) {
    pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"ParameterInit name attribute missing.");
    return;
  }
  if ( !xml_get_qname(pUserData, (char*)attributes[index + 1], &qname) ) {
    pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"ParameterInit name attribute is not a qualified name.");
    return;
  }
  if ( !pt_add_new_param(pt_new, PT_PARAM_PARAMETERINIT, qname) ) {
    xml_qname_free(&qname);
    pt_xml_set_error(XPSPT_ERROR_OUTOFMEM, (uint8*)"Out of memory adding ParameterInit.");
  }
}

static
void ms_parameterinit_end(
  void*         data)
{
  int32 pnMustHave[] = {PT_PARAM_VALUE, PT_PARAM_CHECK_END};

  UNUSED_PARAM(void*, data);

  if (! pt_param_has_required_children (pt_new, PT_ONE_OF, pnMustHave))
  {
    pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"ParameterInit does not have a Value element.");
    return;
  }

  /* Note that this element has finished */
  pt_end_param(pt_new);
}

static
void ms_parameterref_start (
  void*         data,
  const char**  attributes)
{
  int32   index;
  XML_QNAME*  qname;
  PTUserData* pUserData = (PTUserData*) data;

  index = xml_find_attr((char**)attributes, (uint8*)"name");
  if ( index < 0 ) {
    pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"ParameterRef name attribute missing.");
    return;
  }
  if ( !xml_get_qname(pUserData, (char*)attributes[index + 1], &qname) ) {
    pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"ParameterRef name attribute is not a qualified name.");
    return;
  }
  if ( !pt_add_new_param(pt_new, PT_PARAM_PARAMETERREF, qname) ) {
    xml_qname_free(&qname);
    pt_xml_set_error(XPSPT_ERROR_OUTOFMEM, (uint8*)"Out of memory adding ParameterRef.");
  }
}

static
void ms_parameterref_end(
  void*         data)
{
  UNUSED_PARAM(void*, data);

  if (pt_param_has_children (pt_new))
  {
    pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"ParameterRef should not have child elements.");
    return;
  }

  /* Note that this element has finished */
  pt_end_param(pt_new);
}


#define XSD_UNDEFINED   (0)
#define XSD_STRING      (1)
#define XSD_INTEGER     (2)
#define XSD_DECIMAL     (3)
#define XSD_QNAME       (4)

/* State for the value cache */
#define MAX_VALUE_DATA_SIZE  (65536)
#define INIT_CACHE_SIZE      (1024)

/**
 * @brief Allocate value cache memory.
 *
 * If memory is already allocated this the content of the cache is reset.
 *
 * @return \c TRUE on success.
 */
static
int32 initValueData (PTUserData* pUserData)
{
  pUserData->nValueDataSize = 0;
  if (pUserData->nValueDataCapacity == 0)
  {
    pUserData->nValueDataCapacity = INIT_CACHE_SIZE;
    pUserData->pValueData = (char*) MemAlloc (pUserData->nValueDataCapacity, FALSE, FALSE);
    if (! pUserData->pValueData)
      pUserData->nValueDataCapacity = 0;
  }

  return (pUserData->pValueData != NULL);
}

/**
 * @brief Append data to the value cache.
 *
 * The cache memory will be reallocated as necessary.  The initial size (and
 * minimum increase) of memory is governed by \c INIT_CACHE_SIZE.
 *
 * @param pData   The data to store.
 * @param nBytes  The number of bytes referenced by pData.
 * @return \c TRUE on success.
 */
static
int32 appendValueData (
  PTUserData* pUserData,
  const char* pData,
  size_t      nBytes)
{
  HQASSERT(pData != NULL, "No data");

  if (nBytes > 0)
  {
    size_t nBytesAvailable = pUserData->nValueDataCapacity - pUserData->nValueDataSize;
    if (nBytes > nBytesAvailable)
    {
      /* Increase buffer size (by minimum of INIT_CACHE_SIZE) */
      size_t nBytesRequired = nBytes - nBytesAvailable;
      size_t nNewSize = pUserData->nValueDataCapacity + (nBytesRequired < INIT_CACHE_SIZE ? INIT_CACHE_SIZE : nBytesRequired + 1);

      char* pNewBuffer = (char*) MemAlloc (nNewSize, FALSE, FALSE);
      if (! pNewBuffer)
        return FALSE;

      /* Copy old data */
      if (pUserData->nValueDataSize > 0)
        memcpy (pNewBuffer, pUserData->pValueData, pUserData->nValueDataSize);
      MemFree (pUserData->pValueData);
      pUserData->pValueData = pNewBuffer;
      pUserData->nValueDataCapacity = nNewSize;
    }

    /* Append new data */
    memcpy (&pUserData->pValueData[pUserData->nValueDataSize], pData, nBytes);
    pUserData->nValueDataSize += nBytes;
  }

  return TRUE;
}

/**
 * @brief Release any memory allocated to the value cache.
 *
 * If no memory is allocated then this is a no-op.
 */
static
void releaseValueData (PTUserData* pUserData)
{
  if (pUserData->pValueData)
  {
    HQASSERT(pUserData->nValueDataCapacity > 0, "Data capacity too small");
    MemFree(pUserData->pValueData);
    pUserData->pValueData = NULL;
    pUserData->nValueDataSize = pUserData->nValueDataCapacity = 0;
  }
}

/**
 * @brief Guaranteed to be called after a sequence of XML has been parsed.
 *
 * This function will tidy up any resources allocated in this file during
 * the parsing process.
 */
static
void ms_finalizer_callback (void* data)
{
  releaseValueData ((PTUserData*) data);
}

static
void ms_value_start(
  void*         data,
  const char**  attributes)
{
  int32   index;
  XML_QNAME*  qname;
  PTUserData* pUserData = (PTUserData*) data;

  HQASSERT(pUserData != NULL, "No user data");

  /* Get type of value element cdata */
  pUserData->xsd_type = XSD_UNDEFINED;
  index = xml_find_attr((char**)attributes, (uint8*)"type");
  if ( index >= 0 ) {
    if ( !xml_get_qname(pUserData, (char*)attributes[index + 1], &qname) ) {
      pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"Value type attribute is not a qualified name.");
      return;
    }
    if ( strcmp((char*)qname->localpart, "decimal") == 0 ) {
      pUserData->xsd_type = XSD_DECIMAL;

    } else if ( strcmp((char*)qname->localpart, "integer") == 0 ) {
      pUserData->xsd_type = XSD_INTEGER;

    } else if ( strcmp((char*)qname->localpart, "QName") == 0) {
      pUserData->xsd_type = XSD_QNAME;

    } else if ( strcmp((char*)qname->localpart, "string") == 0 ) {
      pUserData->xsd_type = XSD_STRING;
    }
    xml_qname_free(&qname);

    if ( pUserData->xsd_type == XSD_UNDEFINED ) {
      pt_xml_set_error(XPSPT_ERROR_RANGECHECK, (uint8*)"Value type attribute not recognised.");
      return;
    }

  } else { /* Default to string if no type specified */
    pUserData->xsd_type = XSD_STRING;
  }

  /* Initialize/reset value cache */
  if ( !initValueData (pUserData) ) {
    pt_xml_set_error(XPSPT_ERROR_OUTOFMEM, (uint8*)"Out of memory allocating Value cache.");
  }

  if ( !pt_add_new_param(pt_new, PT_PARAM_VALUE, NULL) ) {
    pt_xml_set_error(XPSPT_ERROR_OUTOFMEM, (uint8*)"Out of memory adding Value.");
    releaseValueData (pUserData);
  }
}

static
void ms_value_cdata(
  void*         data,
  const char*   cdata,
  int32         len)
{
  PTUserData* pUserData = (PTUserData*) data;

  /* Prevent value cache overflow */
  if ( (pUserData->nValueDataSize + len) > MAX_VALUE_DATA_SIZE ) {
    pt_xml_set_error(XPSPT_ERROR_LIMITCHECK, (uint8*)"Value element data exceeds maximum limit.");
    return;
  }

  /* Add to previous cdata */
  if ( !appendValueData (pUserData, cdata, len) ) {
    pt_xml_set_error(XPSPT_ERROR_OUTOFMEM, (uint8*)"Out of memory appending to Value cache.");
    return;
  }
}

static
void ms_value_end(
  void*         data)
{
  double  decimal;
  int32   integer;
  XML_QNAME*  qname;
  PTUserData* pUserData = (PTUserData*) data;

  HQASSERT(pUserData != NULL, "No user data");

  if (pt_param_has_children (pt_new))
  {
    pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"Value should not have child elements.");
    return;
  }

  /* NUL terminate the cdata string */
  if ( !appendValueData (pUserData, "\0", 1) ) {
    pt_xml_set_error(XPSPT_ERROR_OUTOFMEM, (uint8*)"Out of memory terminating Value cache.");
    return;
  }

  /* Convert cdata according to indicated type and update print ticket value */
  switch ( pUserData->xsd_type ) {
  case XSD_QNAME:
    if ( !xml_get_qname(pUserData, pUserData->pValueData, &qname) ) {
      pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"Value data is not a qualified name.");
      return;
    }
    if ( !pt_set_value(pt_new, PT_VALUE_QNAME, qname) ) {
      xml_qname_free(&qname);
      pt_xml_set_error(XPSPT_ERROR_OUTOFMEM, (uint8*)"Out of memory adding Value qualified name.");
      return;
    }
    break;

  case XSD_STRING:
    if ( !pt_set_value(pt_new, PT_VALUE_STRING, pUserData->pValueData) ) {
      pt_xml_set_error(XPSPT_ERROR_OUTOFMEM, (uint8*)"Out of memory adding Value string.");
      return;
    }
    break;

  case XSD_INTEGER:
    if ( !xml_get_integer(pUserData->pValueData, &integer) ) {
      pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"Value data is not an integer.");
      return;
    }
    if ( !pt_set_value(pt_new, PT_VALUE_INTEGER, &integer) ) {
      pt_xml_set_error(XPSPT_ERROR_OUTOFMEM, (uint8*)"Out of memory adding Value integer.");
      return;
    }
    break;

  case XSD_DECIMAL:
    if ( !xml_get_double(pUserData->pValueData, &decimal) ) {
      pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"Value data is not a decimal.");
      return;
    }
    if ( !pt_set_value(pt_new, PT_VALUE_DECIMAL, &decimal) ) {
      pt_xml_set_error(XPSPT_ERROR_OUTOFMEM, (uint8*)"Out of memory adding Value decimal.");
      return;
    }
    break;
  }

  /* Note that this element has finished */
  pt_end_param(pt_new);
}

static XML_ELEMENT_CALLBACKS ms_callbacks[] = {
  {STRING_AND_LENGTH("PrintCapabilities"),
    NULL,
    NULL,
    NULL},
  {STRING_AND_LENGTH("PrintTicket"),
    ms_printticket_start,
    ms_printticket_end,
    NULL},
  {STRING_AND_LENGTH("Attribute"),
    NULL,
    NULL,
    NULL},
  {STRING_AND_LENGTH("AttributeSet"),
    NULL,
    NULL,
    NULL},
  {STRING_AND_LENGTH("AttributeSetRef"),
    NULL,
    NULL,
    NULL},
  {STRING_AND_LENGTH("Feature"),
    ms_feature_start,
    ms_feature_end,
    NULL},
  {STRING_AND_LENGTH("Option"),
    ms_option_start,
    ms_option_end,
    NULL},
  {STRING_AND_LENGTH("ParameterDef"),
    NULL,
    NULL,
    NULL},
  {STRING_AND_LENGTH("ParameterInit"),
    ms_parameterinit_start,
    ms_parameterinit_end,
    NULL},
  {STRING_AND_LENGTH("ParameterRef"),
    ms_parameterref_start,
    ms_parameterref_end,
    NULL},
  {STRING_AND_LENGTH("Property"),
    ms_property_start,
    ms_property_end,
    NULL},
  {STRING_AND_LENGTH("ScoredProperty"),
    ms_scoredproperty_start,
    ms_scoredproperty_end,
    NULL},
  {STRING_AND_LENGTH("Value"),
    ms_value_start,
    ms_value_end,
    ms_value_cdata},
  {NULL} /* Indicates end of element list */
};

const XML_NAMESPACE_ELEMENTS ms_elements = {
  {STRING_AND_LENGTH("http://schemas.microsoft.com/windows/2003/08/printing/printschemaframework")},
  ms_callbacks,
  ms_finalizer_callback
};


/* EOF msxml.c */
