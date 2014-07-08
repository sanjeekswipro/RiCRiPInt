/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWptdev!src:xmlcbcks.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "ptincs.h"
#include "expat.h"
#include "printticket.h"

#include "ggxml.h"
#include "msxml.h"

#include "xmlcbcks.h"

/**
 * @file
 * @brief XML callbacks
 */


/** @brief The XML parser handle */
static XML_Parser xml_handle;

/** @brief The character which separates fields within Expat. */
static const XML_Char uri_sep = '|';

void pt_xml_set_error(
  int32   errcode,
  uint8*  errmsg)
{
  PTUserData* pUserData = (PTUserData*) XML_GetUserData (xml_handle);
  HQASSERT(pUserData != NULL, "No user data");

  /* Flag that an XML error has been encountered */
  pUserData->xml_error = TRUE;

  pUserData->error_details.code = errcode;
  /* Record where the parser is when the error was flagged */
  pUserData->error_details.lineno = XML_GetCurrentLineNumber(xml_handle);
  pUserData->error_details.columnno = XML_GetCurrentColumnNumber(xml_handle);
  if ( errmsg != NULL ) {
    pUserData->error_details.errmsg = errmsg;
    pUserData->error_details.errmsglen = strlen_int32((char*)errmsg);
  } else {
    pUserData->error_details.errmsg = NULL;
    pUserData->error_details.errmsglen = 0;
  }
}

int32 pt_xml_error_details(
  XML_ERR_DETAILS*  p_details)
{
  int32 error = FALSE;

  if (xml_handle)
  {
    PTUserData* pUserData = (PTUserData*) XML_GetUserData (xml_handle);
    HQASSERT(pUserData != NULL, "No user data");

    error = pUserData->xml_error;

    if ( p_details != NULL ) {
      *p_details = pUserData->error_details;
      /* Only clear the error when the details have been retrieved */
      pUserData->xml_error = FALSE;
    }
  }

  return(error);
}

int32 parse_chunk(
  XML_Parser  handler,
  uint8*      buf,
  uint32      buflen,
  int32      terminate)
{
  int parse_result;
  PTUserData* pUserData = (PTUserData*) XML_GetUserData (handler);
  HQASSERT(pUserData != NULL, "No user data");

  parse_result = XML_Parse(handler, (const char *)buf, (int)buflen, (int)terminate);

  /* Pick up XML parse errors from Expat or callbacks */
  if ( (parse_result != XML_STATUS_OK) || pUserData->xml_error ) {
    if ( !pUserData->xml_error ) {
      /* Expat hit an XML parsing error - set up error details */
      pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)XML_ErrorString(XML_GetErrorCode(handler)));
    }

    if (pt_new)
      pt_release (&pt_new);

    return(FALSE);
  }

  return(TRUE);
}

static const XML_NAMESPACE_ELEMENTS* ns_elements[] = {
  &ms_elements,
  &gg_elements
};

#define NUM_NAMESPACES  (sizeof(ns_elements)/sizeof(ns_elements[0]))


/** @brief Function to find callbacks for namespace/element
 * combination - unrecognised namespace or element name is treated as
 * an error */
static
const XML_ELEMENT_CALLBACKS* p_find_element(
  const char* uri)
{
  int32 ns;
  char* p_element;

  const XML_ELEMENT_CALLBACKS*  p_callbacks;

  /* Find start callback for namespace and element name */
  for ( ns = 0; ns < NUM_NAMESPACES; ns++ ) {
    if ( strncmp(uri, (char*)ns_elements[ns]->ns.uri, CAST_SIGNED_TO_SIZET(ns_elements[ns]->ns.uri_len)) == 0 ) {
      break;
    }
  }
  if ( ns == NUM_NAMESPACES ) {
    pt_xml_set_error(XPSPT_ERROR_RANGECHECK, (uint8*)"Unrecognised namespace.");
    return(NULL);
  }

  p_element = strchr(uri, uri_sep) + 1;
  for ( p_callbacks = ns_elements[ns]->callbacks; p_callbacks; p_callbacks++ ) {
    if ( strncmp(p_element, (char*)p_callbacks->name, CAST_SIGNED_TO_SIZET(p_callbacks->len)) == 0 ) {
      break;
    }
  }
  if ( !p_callbacks ) {
    pt_xml_set_error(XPSPT_ERROR_RANGECHECK, (uint8*)"Unrecognised element.");
    return(NULL);
  }

  return(p_callbacks);
}

/**
 * @brief Call any registered finalizer callback functions.
 */
static
void call_finalizer_callbacks (void* data)
{
  int32 ns;

  for ( ns = 0; ns < NUM_NAMESPACES; ns++ ) {
    if ( ns_elements[ns]->finalizer_callback )
      ns_elements[ns]->finalizer_callback (data);
  }
}

static
int32 push_elem(
  XML_ELEMENT_STACK* pElemStack,
  const XML_ELEMENT_CALLBACKS* p_element)
{
  if ( pElemStack->top == ELEMENT_STACK_LIMIT ) {
    pt_xml_set_error(XPSPT_ERROR_LIMITCHECK, (uint8*)"Element stack overflow.");
    return(FALSE);
  }
  pElemStack->p_elements[++pElemStack->top] = p_element;
  return(TRUE);
}

static
const XML_ELEMENT_CALLBACKS* top_elem(
  XML_ELEMENT_STACK* pElemStack)
{
  if ( pElemStack->top < 0 ) {
    pt_xml_set_error(XPSPT_ERROR_LIMITCHECK, (uint8*)"Element stack empty.");
    return(NULL);
  }
  return(pElemStack->p_elements[pElemStack->top]);
}

static
int32 pop_elem(
  XML_ELEMENT_STACK* pElemStack)
{
  if ( top_elem(pElemStack) == NULL ) {
    pt_xml_set_error(XPSPT_ERROR_LIMITCHECK, (uint8*)"Element stack underflow.");
    return(FALSE);
  }
  pElemStack->p_elements[pElemStack->top] = NULL;
  pElemStack->top--;
  return(TRUE);
}

static
void clear_elems(
  XML_ELEMENT_STACK* pElemStack)
{
  while (pElemStack->top >= 0)
    pop_elem (pElemStack);
}

/** @brief Element start tag callback */
static
void XMLCALL start_element_ns(
  void*         data,
  const char*   uri,
  const char**  attributes)
{
  const XML_ELEMENT_CALLBACKS* p_callbacks;
  PTUserData* pUserData = (PTUserData*) data;
  HQASSERT(pUserData != NULL, "No user data");

  if ( pUserData->xml_error ) {
    return;
  }

  /* Find namespace element callbacks (there must be one).  Add it to the
   * element stack and call the start tag callback if there is one */
  p_callbacks = p_find_element(uri);
  if ( p_callbacks ) {
    PTUserData* pUserData = (PTUserData*) data;
    if ( !push_elem(&pUserData->elem_stack, p_callbacks) ) {
      return;
    }
    if ( p_callbacks->start ) {
      p_callbacks->start(data, attributes);
    }
  }
}

/** @brief Element end tag callback */
static
void XMLCALL end_element_ns(
  void*       data,
  const char* uri)
{
  const XML_ELEMENT_CALLBACKS* p_callbacks;
  PTUserData* pUserData = (PTUserData*) data;
  HQASSERT(pUserData != NULL, "No user data");

  if ( pUserData->xml_error ) {
    return;
  }

  /* Find namespace element callbacks (there must be one) and check it matches
   * the corresponding start tag.  Pop it from the element stack and call the
   * end tag callback if there is one. */
  p_callbacks = p_find_element(uri);
  if ( p_callbacks ) {
    if ( p_callbacks != top_elem(&pUserData->elem_stack) ) {
      pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"End tag does not match start tag.");
      return;
    }
    (void)pop_elem(&pUserData->elem_stack);
    if ( p_callbacks->end ) {
      p_callbacks->end(data);
    }
  }
}

/** @brief Character data callback */
static
void XMLCALL character_data(
  void*       data,
  const char* s,
  int         len)
{
  const XML_ELEMENT_CALLBACKS* p_callbacks;
  PTUserData* pUserData = (PTUserData*) data;
  HQASSERT(pUserData != NULL, "No user data");

  if ( pUserData->xml_error ) {
    return;
  }

  /* Get the current element callbacks from the stack and call the cdata
   * callback if there is one. */
  p_callbacks = top_elem(&pUserData->elem_stack);
  if ( p_callbacks && p_callbacks->cdata ) {
    p_callbacks->cdata(data, s, len);
  }
}

/** @brief Free off an XML namespace */
static
void xml_ns_free(
  XML_NAMESPACE*  ns)
{
  if ( ns->ns.prefix != NULL ) {
    MemFree(ns->ns.prefix);
  }
  if ( ns->ns.uri != NULL ) {
    MemFree(ns->ns.uri);
  }
  MemFree(ns);
}

/** @brief Namespace attribute callback */
static
void XMLCALL start_namespace(
  void*       data,
  const char* prefix,
  const char* uri)
{
  XML_NAMESPACE*  ns_new;
  PTUserData* pUserData = (PTUserData*) data;

  /* Allocate namespace record */
  ns_new = (XML_NAMESPACE *) MemAlloc(sizeof(XML_NAMESPACE), FALSE, FALSE);
  if ( ns_new == NULL ) {
    pt_xml_set_error(XPSPT_ERROR_OUTOFMEM, (uint8*)"Out of memory.");
    return;
  }
  ns_new->ns.uri = ns_new->ns.prefix = NULL;
  ns_new->next = NULL;

  /* Copy prefix if present */
  if ( prefix != NULL ) {
    ns_new->ns.prefix = utl_strdup((uint8*)prefix);
    if ( ns_new->ns.prefix == NULL ) {
      xml_ns_free(ns_new);
      pt_xml_set_error(XPSPT_ERROR_OUTOFMEM, (uint8*)"Out of memory.");
      return;
    }
  }
  /* Copy namespace uri */
  ns_new->ns.uri = utl_strdup((uint8*)uri);
  if ( ns_new->ns.uri == NULL ) {
    xml_ns_free(ns_new);
    pt_xml_set_error(XPSPT_ERROR_OUTOFMEM, (uint8*)"Out of memory.");
    return;
  }
  ns_new->ns.uri_len = strlen_int32(uri);

  /* Add namespace to head of list */
  ns_new->next = pUserData->ns_list;
  pUserData->ns_list = ns_new;
}

/** @brief Remove a namespace prefix from the list */
static
void XMLCALL end_namespace(
  void*       data,
  const char* prefix)
{
  XML_NAMESPACE*  ns;
  XML_NAMESPACE** p_ns;
  PTUserData* pUserData = (PTUserData*) data;
  HQASSERT(pUserData != NULL, "No user data");

  p_ns = &pUserData->ns_list;
  while ( *p_ns != NULL ) {
    if ( ((prefix == NULL) && ((*p_ns)->ns.prefix == NULL)) ||
         ((prefix != NULL) && strcmp(prefix, (char*)(*p_ns)->ns.prefix) == 0) ) {
      ns = *p_ns;
      *p_ns = ns->next;
      xml_ns_free(ns);
      break;
    }
    p_ns = &(*p_ns)->next;
  }
}

void xml_ns_purge(PTUserData* pUserData)
{
  XML_NAMESPACE*  ns;
  XML_NAMESPACE*  ns_next;

  /* Release each namespace in list */
  ns = pUserData->ns_list;
  while ( ns != NULL ) {
    ns_next = ns->next;
    xml_ns_free(ns);
    ns = ns_next;
  }
  pUserData->ns_list = NULL;
}


uint8* xml_ns_find_uri(
  PTUserData* pUserData,
  uint8*  prefix)
{
  XML_NAMESPACE*  ns;

  /* Return URI of first namespace matching given prefix */
  for ( ns = pUserData->ns_list; ns != NULL; ns = ns->next ) {
    if ( ((prefix == NULL) && (ns->ns.prefix == NULL)) ||
         ((prefix != NULL) && strcmp((char*)prefix, (char*)ns->ns.prefix) == 0) ) {
      return(ns->ns.uri);
    }
  }
  return(NULL);
}

uint8* xml_ns_find_prefix(
  PTUserData* pUserData,
  uint8*  uri)
{
  int32           len;
  XML_NAMESPACE*  ns;

  /* Return prefix of first namespace matching given uri */
  len = strlen_int32((char*)uri);
  for ( ns = pUserData->ns_list; ns != NULL; ns = ns->next ) {
    if ( (len == ns->ns.uri_len) &&
         (strcmp((char*)uri, (char*)ns->ns.uri) == 0) ) {
      return(ns->ns.prefix);
    }
  }
  return(NULL);
}

static void freeParser (XML_Parser* pParser)
{
  if (*pParser)
  {
    void* pUserData = XML_GetUserData (*pParser);
    if (pUserData)
    {
      /* Ensure namespace list is empty */
      xml_ns_purge (pUserData);

      MemFree (pUserData);
    }

    XML_ParserFree (*pParser);
    *pParser = NULL;
  }
}

static PTUserData* createUserData ()
{
  const XML_ERR_DETAILS init_error_info = { -1, -1, -1, NULL, 0 };

  /* Allocate memory */
  PTUserData* pUserData = (PTUserData*) MemAlloc (sizeof (PTUserData), TRUE, FALSE);
  if (! pUserData)
    return NULL;

  /* Initialise */
  pUserData->elem_stack.top = -1;
  pUserData->error_details = init_error_info;

  return pUserData;
}

#ifdef INRIP_PTDEV

static void * pt_xml_malloc( size_t cbSize )
{
  return MemAlloc( cbSize, FALSE, FALSE );
}

static void * pt_xml_realloc( void * pMem, size_t cbSize )
{
  return MemRealloc( pMem, cbSize );
}

static void pt_xml_free( void * pMem )
{
  MemFree( pMem );
}

static XML_Memory_Handling_Suite pt_xml_memSuite = { pt_xml_malloc, pt_xml_realloc, pt_xml_free };

#endif

int32 pt_xml_start(void)
{
  PTUserData* pUserData;

  /* NYI allocation of PT handler */
  freeParser (&xml_handle);
#ifdef INRIP_PTDEV
  xml_handle = XML_ParserCreate_MM(NULL, &pt_xml_memSuite, &uri_sep);
#else
  xml_handle = XML_ParserCreate_MM(NULL, NULL, &uri_sep);
#endif
  if ( xml_handle == NULL ) {
    return(FALSE);
  }

  /* Turn prefix mapping on. */
  XML_SetReturnNSTriplet(xml_handle, 1);

  XML_SetElementHandler(xml_handle, start_element_ns, end_element_ns);
  XML_SetNamespaceDeclHandler(xml_handle, start_namespace, end_namespace);
  XML_SetCharacterDataHandler(xml_handle, character_data);

  /* Set user data */
  pUserData = createUserData ();
  if (! pUserData)
  {
    freeParser (&xml_handle);
    return(FALSE);
  }
  XML_SetUserData(xml_handle, pUserData);

  return(TRUE);
}

void pt_xml_end(void)
{
  if (xml_handle)
  {
    PTUserData* pUserData = (PTUserData*) XML_GetUserData (xml_handle);

    /* Remove any entries left on the element stack */
    clear_elems (&pUserData->elem_stack);

    /* Find and call any registered finalizer callbacks */
    call_finalizer_callbacks (pUserData);

    /* Release the Expat parser */
    freeParser (&xml_handle);
  }
}


int32 xml_parse(
  uint8*  buffer,
  int32*  len)
{
  /* ParseChunk always consumes the entire buffer. */
  return(parse_chunk(xml_handle, buffer, *len, FALSE));
}


int32 xml_find_attr(
  char**  attributes,
  uint8*  name)
{
  char* sep;
  int32 i;
  int32 name_len;

  name_len = strlen_int32((char*)name);
  for ( i = 0; attributes[i]; i += 2 ) {
    sep = strchr(attributes[i], uri_sep);
    if ( sep != NULL ) {
      sep++;
    } else {
      sep = attributes[i];
    }
    if ( strncmp(sep, (char*)name, CAST_SIGNED_TO_SIZET(name_len)) == 0 ) {
      return(i);
    }
  }
  return(-1);
}

double xml_parse_double(
  char*   str,
  char**  endstr)
{
  return(strToDouble(str, endstr));
}

int32 xml_get_double(
  char*   str,
  double* val)
{
  char* end;

  *val = xml_parse_double(str, &end);
  return(end != str);
}

int32 xml_parse_integer(
  char*   str,
  char**  endstr)
{
  return(strtol(str, endstr, 10));
}

int32 xml_get_integer(
  char*   str,
  int32*  val)
{
  char* end;

  *val = xml_parse_integer(str, &end);
  return(end != str);
}


XML_QNAME* xml_parse_qname(
  PTUserData* pUserData,
  char*   str,
  char**  endstr)
{
  char*       sep;
  char*       localpart;
  char*       end;
  uint8*      uri;
  XML_QNAME*  qname;

  /* Remove any leading whitespace from input */
  while ( *str && isspace(*str) ) {
    str++;
  }

  *endstr = str;

  /* Find end of QName prefix and localparts */
  localpart = str;
  sep = strchr(str, ':');
  if ( sep != NULL ) {
    *sep = '\0'; /* Quick hack to make prefix NUL terminated */
    localpart = sep + 1;
  }
  end = localpart;
  while ( *end && !isspace(*end) ) {
    end++;
  }
  *end = '\0';

  /* Check localpart is not empty */
  if ( (sep == NULL) ? (end == str) : (end == localpart) ) {
    return(FALSE);
  }

  /* Allocate new qname and copy uri and localparts */
  qname = (XML_QNAME *) MemAlloc(sizeof(XML_QNAME), FALSE, FALSE);
  if ( qname == NULL ) {
    return(FALSE);
  }
  qname->uri = qname->localpart = NULL;

  uri = xml_ns_find_uri(pUserData, (uint8*)str);
  if ( uri != NULL ) {
    qname->uri = utl_strdup(uri);
    if ( qname->uri == NULL ) {
      xml_qname_free(&qname);
      return(FALSE);
    }
  }
  qname->localpart = utl_strdup((uint8*)localpart);
  if ( qname->localpart == NULL ) {
    xml_qname_free(&qname);
    return(FALSE);
  }

  /* Return pointer to character after QName */
  *endstr = end;

  return(qname);
}

extern
int32 xml_get_qname(
  PTUserData* pUserData,
  char*   str,
  XML_QNAME** p_qname)
{
  char* end;

  *p_qname = xml_parse_qname(pUserData, str, &end);
  return(*p_qname != NULL);
}

int32 xml_qname_cmp(
  XML_QNAME*  qn1,
  XML_QNAME*  qn2)
{
  int32   rc;

  /* If prefix uris match then compare localparts */
  if ( qn1->uri == NULL ) {
    if ( qn2->uri != NULL ) {
      /* qn1 does not have a URI, qn2 does, so qn1 is less than */
      return(-1);
    }
    /* Both URIs are not present - compare localparts */

  } else if ( qn2->uri == NULL ) {
    /* qn1 has a URI, qn2 does not, so qn1 is greater */
    return(1);

  } else { /* First compare URIs */
    rc = strcmp((char*)qn1->uri, (char*)qn2->uri);
    if ( rc != 0 ) {
      return(rc);
    }
  }
  /* URIs not present or equal - compare localparts */
  return(strcmp((char*)qn1->localpart, (char*)qn2->localpart));
}

XML_QNAME* xml_qname_copy(
  XML_QNAME*  qname)
{
  XML_QNAME*  qname_copy;

  qname_copy = (XML_QNAME *) MemAlloc(sizeof(XML_QNAME), FALSE, FALSE);
  if ( qname_copy == NULL ) {
    return(NULL);
  }
  qname_copy->uri = qname_copy->localpart = NULL;

  if (qname->uri)
  {
    qname_copy->uri = utl_strdup(qname->uri);
    if ( qname_copy->uri == NULL ) {
      xml_qname_free(&qname_copy);
      return(NULL);
    }
  }

  qname_copy->localpart = utl_strdup(qname->localpart);
  if ( qname_copy->localpart == NULL ) {
    xml_qname_free(&qname_copy);
    return(NULL);
  }

  return(qname_copy);
}

void xml_qname_free(
  XML_QNAME** p_qname)
{
  XML_QNAME*  qname;

  qname = *p_qname;
  if ( qname->uri != NULL ) {
    MemFree(qname->uri);
  }
  if ( qname->localpart != NULL ) {
    MemFree(qname->localpart);
  }
  MemFree(qname);
  *p_qname = NULL;
}

#ifndef INRIP_PTDEV

/* For plugin builds.
 */
double strToDouble (char* pStr, char** ppEndStr)
{
  PlgFwTextString ptbz = (PlgFwTextString) pStr;
  double          d;

  if( PlgFwStrToD( &ptbz, &d ) )
  {
    if( ppEndStr )
      *ppEndStr = (char *) ptbz;
  }
  else
  {
    d = 0.0;

    if( ppEndStr )
      *ppEndStr = pStr;
  }

  return d;
}

#endif



/* EOF pt_xml.c */
