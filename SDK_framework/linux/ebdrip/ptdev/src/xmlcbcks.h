/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWptdev!src:xmlcbcks.h(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */
#ifndef __XMLCBCKS_H__
#define __XMLCBCKS_H__  (1)

/**
 * @file
 * @brief XML callbacks
 */

#include <stddef.h> /* size_t */
#include "std.h"


/** @brief Structure to hold an XML namespace prefix and URI */
typedef struct XML_NS {
  uint8*    uri;      /* NULL for the default XML namespace */
  int32     uri_len;
  uint8*    prefix;
} XML_NS;

/** @brief Structure associating XML element name with callbacks for start tag, end tag,
 * and character data.  Callbacks can be NULL
 */
typedef struct XML_ELEMENT_CALLBACKS {
  uint8*  name;
  int32   len;
  void    (*start)(void* data, const char** attributes);
  void    (*end)(void* data);
  void    (*cdata)(void* data, const char* cdata, int len);
} XML_ELEMENT_CALLBACKS;

/** @brief Structure to maintain a list of namespaces seen */
typedef struct XML_NAMESPACE {
  struct XML_NAMESPACE* next;
  XML_NS                ns;
} XML_NAMESPACE;

#define ELEMENT_STACK_LIMIT (64)

/**
 * @brief Simple stack to track nesting of XML elements.  Allows for
 * checks of matching start and end tags, and handling of cdata for
 * current active element.
 */
typedef struct XML_ELEMENT_STACK {
  const XML_ELEMENT_CALLBACKS* p_elements[ELEMENT_STACK_LIMIT];
  int32 top;
} XML_ELEMENT_STACK;

/** @brief XML_ERR_DETAILS */
typedef struct XML_ERR_DETAILS {
  int32   code;
  int32   lineno;
  int32   columnno;
  uint8*  errmsg;
  int32   errmsglen;
} XML_ERR_DETAILS;

typedef struct PTUserData {
  XML_ERR_DETAILS error_details;
  int32 xml_error; /**< Error encountered while parsing XML */

  XML_ELEMENT_STACK elem_stack;
  XML_NAMESPACE* ns_list;

  int32 xsd_type;

  char* pValueData;
  size_t nValueDataSize;
  size_t nValueDataCapacity;
} PTUserData;

typedef void (*XML_FINALIZER_CALLBACK)(void* data);

/** @brief Structure to hold a set of callbacks for an XML namespace */
typedef struct XML_NAMESPACE_ELEMENTS {
  XML_NS                  ns;
  const XML_ELEMENT_CALLBACKS*  callbacks;

  /**
   * @brief If non-NULL the finalizer callback is guaranteed to be called after
   * parsing a sequence of XML (whether parsing succeeded or not).
   */
  XML_FINALIZER_CALLBACK finalizer_callback;
} XML_NAMESPACE_ELEMENTS;

/** @brief Initialise the XML parser for a new XML stream */
extern
int32 pt_xml_start(void);

/** @brief Close down the initialised XML parser */
extern
void pt_xml_end(void);

/** @brief Push buffered XML stream data to the initialised XML parser */
extern
int32 xml_parse(
  uint8*  buffer,
  int32*  len);


/** @brief Find namespace URI for given prefix */
extern
uint8* xml_ns_find_uri(
  PTUserData* pUserData,
  uint8*  prefix);

/** @brief Find prefix for given namespace URI */
extern
uint8* xml_ns_find_prefix(
  PTUserData* pUserData,
  uint8*  uri);

/** @brief Release all namespaces still remaining.  In normal parsing
 * there should be none left, but if there was an error this function
 * should be called to recover memory used. */
extern
void xml_ns_purge(PTUserData* pUserData);


/** @brief Set up details on parsing errors */
extern
void pt_xml_set_error(
  int32   errcode,
  uint8*  errmsg);


/** @brief Return details of last error set up */
extern
int32 pt_xml_error_details(
  XML_ERR_DETAILS*  p_details);


/**
 * @brief Function to find index of attribute name in list of attributes or -1.
 * Skips over any namespace if present.  It reality namespace should be part of
 * the match. */
extern
int32 xml_find_attr(
  char**  attributes,
  uint8*  name);

/* Functions to parse primitive XML data types. */

/** @brief A function to parse a real value returning a flag if one was not found. */
extern
double xml_parse_double(
  char*   str,
  char**  endstr);

/** @brief A function to parse a real value returning a flag if one was not found. */
extern
int32 xml_get_double(
  char*   str,
  double* val);

/** @brief A function to parse an integer value returning the value and a pointer to the
 * start of characters after the last character of the integer. */
extern
int32 xml_parse_integer(
  char*   str,
  char**  endstr);

/** @brief A function to parse a integer value returning a flag if one was not found. */
extern
int32 xml_get_integer(
  char*   str,
  int32*  val);

/** @brief Structure to hold the parts of an XML QName */
typedef struct XML_QNAME {
  uint8*  uri;      /* namespace uri */
  uint8*  localpart;
} XML_QNAME;

/**
 * @brief Function to parse a qname into prefix and localpart.  Does not handle
 * non-ASCII characters, and does not validate parts are NCNames.
 */
extern
XML_QNAME* xml_parse_qname(
  PTUserData* pUserData,
  char*   str,
  char**  endstr);

/** @brief Function to parse a qname returning a flag if one was found */
extern
int32 xml_get_qname(
  PTUserData* pUserData,
  char*   str,
  XML_QNAME** p_qname);

/**
 * @brief Compare two QNames, returning as per strcmp.  Ordering is
 * first on namespace uri and then on localpart.
 *
 * @return >0, 0, <0 if qn1 is greater, equal, or less than qn2 in
 * ASCII character set */
extern
int32 xml_qname_cmp(
  XML_QNAME*  qn1,
  XML_QNAME*  qn2);

/** @brief Create a copy of a QName */
extern
XML_QNAME* xml_qname_copy(
  XML_QNAME*  qname);

/** @brief Release memory used by a QName */
extern
void xml_qname_free(
  XML_QNAME** p_qname);

#endif /* !__XMLCBCKS_H__ */


/* EOF xmlcbcks.h */
