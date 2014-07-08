/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!src:xmldebug.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * XML debug routines.
 */

#include "core.h"
#include "swerrors.h"
#include "mm.h"
#include "mmcompat.h"          /* mm_alloc_with_header etc.. */
#include "monitor.h"           /* monitorf */

#include "xml.h"
#include "xmlcontext.h"
#include "xmldebug.h"

/* debug_xml defined for assert build too, so we can HQTRACE on its value */
#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
int32 debug_xml = 0 ;
#endif

#if defined(DEBUG_BUILD)

#define XMLTAB_SIZE 4

static uint8* indent;

static Bool debug_xml_characters(
      xmlGFilter *filter,
      const uint8 *buf,
      uint32 buflen)
{
  unsigned int pos, escaped_len, depth, numspaces, i;
  uint8 *escaped_buf;

  HQASSERT((filter != NULL), "filter is NULL");
  HQASSERT((buf != NULL), "buf is NULL");

  if ( (debug_xml & DEBUG_XML_CHARACTERS) == 0 )
    return TRUE ;

  if (buf != NULL && buflen > 0) {
    depth = xmlg_get_element_depth(filter);
    numspaces = (depth * XMLTAB_SIZE);

    if ((indent = mm_alloc_with_header(mm_xml_pool, numspaces + 1,
                                       MM_ALLOC_CLASS_XML_DEBUG)) == NULL)
      return error_handler(VMERROR);
    for (i=0; i<numspaces; i++) {
      indent[i] = ' ';
    }
    indent[numspaces] = '\0';
    escaped_len = buflen;

    for (i=0; i<buflen; i++)
      if (buf[i] == '\n' ||
          buf[i] == '\t' ||
          buf[i] == '\r')
        escaped_len++;

    if ((escaped_buf = mm_alloc_with_header(mm_xml_pool, escaped_len + 1,
                                            MM_ALLOC_CLASS_XML_DEBUG)) == NULL) {
      mm_free_with_header(mm_xml_pool, indent);
      indent = NULL;
      return error_handler(VMERROR);
    }

    pos = 0;
    for (i=0; i<buflen; i++) {
      switch (buf[i]) {
        case '\n':
          escaped_buf[pos++] = '\\';
          escaped_buf[pos++] = 'n';
          break;
        case '\t':
          escaped_buf[pos++] = '\\';
          escaped_buf[pos++] = 't';
          break;
        case '\r':
          escaped_buf[pos++] = '\\';
          escaped_buf[pos++] = 'r';
          break;
        default:
          escaped_buf[pos++] = buf[i];
          break;
      }
    }
    escaped_buf[pos] = '\0';
    HQASSERT(pos == escaped_len,
             "pos is not equal to the escaped length");

    monitorf((uint8*)"%s    CHARS(len=%d)[%.*s]\n",
             indent,
             buflen,
             escaped_len,
             escaped_buf);

    mm_free_with_header(mm_xml_pool, escaped_buf);
    mm_free_with_header(mm_xml_pool, indent);
    indent = NULL;
  }
  return TRUE;
}

static Bool debug_xml_attributes(
      xmlGFilter *filter,
      xmlGAttributes *attrs,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      const xmlGIStr *attrlocalname,
      const xmlGIStr *attrprefix,
      const xmlGIStr *attruri,
      const uint8 *attrvalue,
      uint32 attrvaluelen)
{
  static char *more_data = " ....truncated";
  static char *no_more_data = "";
  char *end_attr = no_more_data;
  uint32 display_len = attrvaluelen;

  UNUSED_PARAM(xmlGFilter *, filter);

  HQASSERT((filter != NULL), "filter is NULL");
  HQASSERT((attrlocalname != NULL), "attrlocalname is NULL");
  UNUSED_PARAM( xmlGAttributes* , attrs ) ;
  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;
  UNUSED_PARAM( const xmlGIStr* , attrprefix ) ;
  HQASSERT(indent != NULL, "indent is NULL");

  if ( (debug_xml & DEBUG_XML_ATTRIBUTES) == 0 )
    return TRUE ;

  if (attrvaluelen > 80) {
    display_len = 80;
    end_attr = more_data;
  }

  if (attrvalue != NULL) {
    if (attruri != NULL) {
      monitorf((uint8*)"%s              %.*s=\"%.*s%s\" URI(%.*s)\n",
               indent,
               intern_length(attrlocalname),
               intern_value(attrlocalname),
               display_len,
               attrvalue,
               end_attr,
               intern_length(attruri),
               intern_value(attruri));
    } else {
      monitorf((uint8*)"%s              %.*s=\"%.*s%s\" URI(NULL)\n",
               indent,
               intern_length(attrlocalname),
               intern_value(attrlocalname),
               display_len,
               attrvalue,
               end_attr);
    }
  } else {
    if (attruri != NULL) {
      monitorf((uint8*)"%s              %.*s=NULL URI(%.*s)\n",
               indent,
               intern_length(attrlocalname),
               intern_value(attrlocalname),
               intern_length(attruri),
               intern_value(attruri));
    } else {
      monitorf((uint8*)"%s              %.*s=NULL URI(NULL)\n",
               indent,
               intern_length(attrlocalname),
               intern_value(attrlocalname));
    }
  }
  return TRUE;
}

static Bool debug_xml_start_element(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  unsigned int line, column, depth, numspaces, i;

  HQASSERT((filter != NULL), "filter is NULL");
  HQASSERT((localname != NULL), "localname is NULL");
  UNUSED_PARAM( const xmlGIStr * , prefix ) ;
  HQASSERT(indent == NULL, "indent is not NULL");

  if ( (debug_xml & DEBUG_XML_STARTEND) == 0 )
    return TRUE ;

  depth = xmlg_get_element_depth(filter);
  numspaces = (depth * XMLTAB_SIZE);

  if ((indent = mm_alloc_with_header(mm_xml_pool, numspaces + 1,
                                     MM_ALLOC_CLASS_XML_DEBUG)) == NULL)
    return error_handler(VMERROR);
  for (i=0; i<numspaces; i++) {
    indent[i] = ' ';
  }
  indent[numspaces] = '\0';

  if (!xmlg_line_and_column(filter, &line, &column))
    return error_handler(UNDEFINED);

  if (uri != NULL) {
    monitorf((uint8*)"%s  <%.*s> URI(%.*s) line=%04d col=%04d\n",
             indent,
             intern_length(localname),
             intern_value(localname),
             intern_length(uri),
             intern_value(uri),
             line,
             column);
  } else {
    monitorf((uint8*)"%s  <%.*s> URI(NULL)\n",
             indent,
             intern_length(localname),
             intern_value(localname));
  }

  if (attrs != NULL) {
    if (! xmlg_attributes_scan_full(filter, attrs, localname, uri, debug_xml_attributes)) {
      mm_free_with_header(mm_xml_pool, indent);
      indent = NULL;
      return error_handler(UNDEFINED);
    }
  }

  mm_free_with_header(mm_xml_pool, indent);
  indent = NULL;
  return TRUE;
}

static Bool debug_xml_end_element(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  unsigned int line, column, depth, numspaces, i;
  char *failstr = "";

  HQASSERT((filter != NULL), "filter is NULL");
  HQASSERT((localname != NULL), "localname is NULL");
  UNUSED_PARAM( const xmlGIStr * , uri ) ;
  UNUSED_PARAM( const xmlGIStr * , prefix ) ;

  HQASSERT(indent == NULL, "indent is not NULL");

  if ( (debug_xml & DEBUG_XML_STARTEND) == 0 )
    return success ;

  if (! success)
    failstr = " - FAILURE";

  depth = xmlg_get_element_depth(filter);
  numspaces = (depth * XMLTAB_SIZE);

  if ((indent = mm_alloc_with_header(mm_xml_pool, numspaces + 1,
                                     MM_ALLOC_CLASS_XML_DEBUG)) == NULL)
    return error_handler(VMERROR) ;

  for (i=0; i<numspaces; i++) {
    indent[i] = ' ';
  }
  indent[numspaces] = '\0';

  if (!xmlg_line_and_column(filter, &line, &column))
    return error_handler(UNDEFINED);

  monitorf((uint8*)"%s  </%.*s> line=%04d col=%04d%s\n",
           indent,
           intern_length(localname),
           intern_value(localname),
           line,
           column,
           failstr);

  mm_free_with_header(mm_xml_pool, indent);
  indent = NULL;
  return success;
}

Bool debug_xml_filter_init(
      xmlGFilterChain *filter_chain,
      uint32 position,
      xmlGFilter **filter)
{
  xmlGFilter *new_filter ;

  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  HQASSERT(filter != NULL, "filter is NULL") ;

  *filter = NULL ;

  if (! xmlg_fc_new_filter(filter_chain, &new_filter, position, NULL, NULL))
    return error_handler(UNDEFINED) ;

  /* watch all elements */
  if (! xmlg_register_start_element_cb(new_filter, NULL, NULL, /* all elements */
                                        debug_xml_start_element) ||
      ! xmlg_register_end_element_cb(new_filter, NULL, NULL, /* all elements */
                                     debug_xml_end_element) ||
      ! xmlg_register_characters_cb(new_filter, NULL, NULL, /* all elements */
                                    debug_xml_characters)) {

    xmlg_f_destroy(&new_filter) ;
    return error_handler(UNDEFINED) ;
  }

  *filter = new_filter ;

  return TRUE ;
}

Bool debug_xml_filter_dispose(
    xmlGFilter **filter)
{
  HQASSERT(filter != NULL, "filter is NULL") ;

  /* In this filter, the filter data is a static so we don't need to
     do any deallocation. */

  xmlg_f_destroy(filter) ;

  return TRUE ;
}

#endif

void init_C_globals_xmldebug(void)
{
#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
  debug_xml = 0 ;
#endif
#if defined(DEBUG_BUILD)
  indent = NULL ;
#endif
}


/* ============================================================================
* Log stripped */
