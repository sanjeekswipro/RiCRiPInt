/* ============================================================================
 * $HopeName: HQNgenericxml!src:xmlg.c(EBDSDK_P.1) $
 * $Id: src:xmlg.c,v 1.46.10.1.1.1 2013/12/19 11:24:21 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \ingroup libgenxml
 * \brief Implements many of the generic XML parse routines.
 *
 * This files does not have any XML parser backend specific code within it.
 */

#include <string.h>
#include <stdlib.h>

#ifdef PROFILE_XML_MEMORY_ALLOCATION
#include <stdio.h>
#define XML_TRACE(_out_) printf(_out_)
#else
#define XML_TRACE(_out_)
#endif

#include "mps.h"
#include "xmlgpriv.h"


/* ============================================================================
 * Wrappers to malloc, etc.
 * ============================================================================
 * Because malloc_fcn et al don't declare a calling convention but malloc et al do.
 */
static void * xml_malloc( size_t size )
{
  return malloc( size );
}

static void * xml_realloc( void *ptr, size_t size )
{
  return realloc( ptr, size );
}

static void xml_free( void *ptr )
{
  free( ptr );
}

/* ============================================================================
 * Subsystem allocation/deallocation functions.
 * ============================================================================
 */
void * xmlg_subsystem_malloc(
      xmlGContext *xml_ctxt,
      size_t size)
{
  XMLGASSERT(xml_ctxt != NULL,
             "xml_ctxt is NULL") ;

  XML_TRACE("alloc: subsystem_malloc\n") ;

  return xml_ctxt->memory_handler.f_malloc(size) ;
}

void * xmlg_subsystem_realloc(
      xmlGContext *xml_ctxt,
      void *memPtr,
      size_t size)
{
  XMLGASSERT(xml_ctxt != NULL,
             "xml_ctxt is NULL") ;

  XML_TRACE("alloc: subsystem_realloc\n") ;

  return xml_ctxt->memory_handler.f_realloc(memPtr, size) ;
}

void xmlg_subsystem_free(
      xmlGContext *xml_ctxt,
      void *memPtr)
{
  XMLGASSERT(xml_ctxt != NULL,
             "xml_ctxt is NULL") ;

  XML_TRACE("alloc: subsystem_free\n") ;

  xml_ctxt->memory_handler.f_free(memPtr) ;
  return ;
}

/* ============================================================================
 * Parser allocation/deallocation functions.
 *
 * These get plugged into the 3rd party XML parser libraries. It means
 * we can, if so desired, track memory allocation in these parsers.
 * ============================================================================
 */
void * xmlg_parser_malloc(
      xmlGParser *xml_parser,
      size_t size)
{
  xmlGParserCommon *c;
  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL");
  c = xmlg_get_parser_common(xml_parser);

  XML_TRACE("alloc: parser_malloc\n") ;

  return c->memory_handler.f_malloc(size);
}

void * xmlg_parser_realloc(
      xmlGParser *xml_parser,
      void *memPtr,
      size_t size)
{
  xmlGParserCommon *c ;
  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL") ;
  c = xmlg_get_parser_common(xml_parser) ;

  XML_TRACE("alloc: parser_realloc\n") ;

  return c->memory_handler.f_realloc(memPtr, size);
}

void xmlg_parser_free(
      xmlGParser *xml_parser,
      void *memPtr)
{
  xmlGParserCommon *c;
  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL");
  c = xmlg_get_parser_common(xml_parser);

  XML_TRACE("alloc: parser_free\n") ;

  c->memory_handler.f_free(memPtr);
  return;
}

/* ============================================================================
 * Filter allocation/deallocation functions.
 *
 * Because the filter chains are not tied to a parser, they need their
 * own memory allocation/deallocation routines. Its worth doing this
 * rather than just using the XML sub-system memory allocation
 * routines as typically the sub-system survives the lifetime of a
 * process where as filter chains will be short lived and any pool
 * can be thrown away and tested for leakage.
 * ============================================================================
 */
void * xmlg_fc_malloc(
      xmlGFilterChain *filter_chain,
      size_t size)
{
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL");

  XML_TRACE("alloc: fc_malloc\n") ;

  return filter_chain->memory_handler.f_malloc(size);
}

void * xmlg_fc_realloc(
      xmlGFilterChain *filter_chain,
      void *memPtr,
      size_t size)
{
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL");

  XML_TRACE("alloc: fc_realloc\n") ;

  return filter_chain->memory_handler.f_realloc(memPtr, size);
}

void xmlg_fc_free(
      xmlGFilterChain *filter_chain,
      void *memPtr)
{
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL");

  XML_TRACE("alloc: fc_free\n") ;

  filter_chain->memory_handler.f_free(memPtr);
}

/* ============================================================================
 * Generic parser functions.
 * ============================================================================
 */
void xmlg_p_set_map_uri_cb(
      xmlGParser *xml_parser,
      xmlGMapUriCallback f)
{
  xmlGParserCommon *c;
  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL");
  c = xmlg_get_parser_common(xml_parser);

  c->mapuri_cb = f;
}

void xmlg_p_set_parse_error_cb(
      /*@in@*/ /*@notnull@*/
      xmlGParser *xml_parser,
      /*@null@*/
      xmlGParseErrorCallback f)
{
  xmlGParserCommon *c;
  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL");
  c = xmlg_get_parser_common(xml_parser);

  c->parse_error_cb = f;
}

/* ============================================================================
 * Initialize/terminate common part functions.
 * ============================================================================
 */
void xmlg_subsystem_common_terminate(
      xmlGContext **xml_ctxt)
{
  XMLGASSERT(xml_ctxt != NULL,
             "xml subsystem is NULL");
  XMLGASSERT(*xml_ctxt != NULL,
             "xml subsystem pointer is NULL");

  mps_sac_destroy((*xml_ctxt)->sac) ;

  xmlg_subsystem_free(*xml_ctxt, *xml_ctxt);
  *xml_ctxt = NULL;
}

/**
 * \brief Initialize common parts of the XML subsystem.
 *
 * This is called from the backend specific xmlg_initialize and intializes
 * the common parts of the XML sub system.
 */
HqBool xmlg_subsystem_common_init(
      xmlGContext **xml_ctxt,
      mps_pool_t pool,
      xmlGMemoryHandler *memory_handler,
      xmlGIStrHandler *intern_handler,
      hqn_uri_context_t *uri_context)
{
  xmlGContext *new_subsystem ;

  XMLGASSERT(xml_ctxt != NULL, "xml_ctxt is NULL") ;
  XMLGASSERT(pool != NULL, "pool is NULL") ;

  *xml_ctxt = NULL ;

  if (memory_handler != NULL) {
    if (memory_handler->f_malloc == NULL ||
        memory_handler->f_realloc == NULL ||
        memory_handler->f_free == NULL) {

      XMLGASSERT(FALSE, "incomplete memory handler") ;
      return FALSE ;
    }

    if ((new_subsystem = memory_handler->f_malloc(sizeof(struct xmlGContext))) == NULL)
      return FALSE ;

    new_subsystem->memory_handler.f_malloc = memory_handler->f_malloc ;
    new_subsystem->memory_handler.f_realloc = memory_handler->f_realloc ;
    new_subsystem->memory_handler.f_free = memory_handler->f_free ;

  } else {
    if ((new_subsystem = xml_malloc(sizeof(struct xmlGContext))) == NULL)
      return FALSE ;

    new_subsystem->memory_handler.f_malloc = xml_malloc ;
    new_subsystem->memory_handler.f_realloc = xml_realloc ;
    new_subsystem->memory_handler.f_free = xml_free ;
  }

  new_subsystem->pool = pool ;
  new_subsystem->intern_pool = NULL ;
  new_subsystem->uri_context = uri_context ;

  /* The new XML sub system's memory handler has been assigned from
     this point onwards for all XML calls. */

  if (intern_handler != NULL) {
    XMLGASSERT(intern_handler->f_intern_create != NULL, "f_intern_create is NULL");
    XMLGASSERT(intern_handler->f_intern_hash != NULL, "f_intern_hash is NULL");
    XMLGASSERT(intern_handler->f_intern_length != NULL, "f_intern_length is NULL");
    XMLGASSERT(intern_handler->f_intern_value != NULL, "f_intern_value is NULL");
    XMLGASSERT(intern_handler->f_intern_equal != NULL, "f_intern_equal is NULL");
    /* f_intern_reserve, f_intern_destroy, f_intern_terminate are optional */

    new_subsystem->intern_handler.f_intern_create = intern_handler->f_intern_create ;
    new_subsystem->intern_handler.f_intern_reserve = intern_handler->f_intern_reserve ;
    new_subsystem->intern_handler.f_intern_destroy = intern_handler->f_intern_destroy ;
    new_subsystem->intern_handler.f_intern_hash = intern_handler->f_intern_hash ;
    new_subsystem->intern_handler.f_intern_length = intern_handler->f_intern_length ;
    new_subsystem->intern_handler.f_intern_value = intern_handler->f_intern_value ;
    new_subsystem->intern_handler.f_intern_equal = intern_handler->f_intern_equal ;
    new_subsystem->intern_handler.f_intern_terminate = intern_handler->f_intern_terminate ;

  } else if (! xmlg_intern_default(new_subsystem)) {
    xmlg_subsystem_free(new_subsystem, new_subsystem) ;
    return FALSE;
  }

  {
  /* What do we think is a typical element depth for XPS jobs. */
#define EXPECTED_MAX_ELEMENT_DEPTH 20

    /* Be careful to keep all SAC allocations 8 byte aligned. */
    struct mps_sac_classes_s sac_classes[] = { /* size, num, freq */
      { SAC_ALLOC_ELEMENT_SIZE          , EXPECTED_MAX_ELEMENT_DEPTH,  1 },       /* Element stack */
      /* { SAC_ALLOC_VALID_CHILD_LINK_SIZE , EXPECTED_MAX_ELEMENT_DEPTH,  1 },*/  /* Valid link children */
      { SAC_ALLOC_VALID_CHILD_SIZE      , EXPECTED_MAX_ELEMENT_DEPTH,  1 },       /* Valid children */
      { SAC_ALLOC_ATTRIBUTES_SIZE       , EXPECTED_MAX_ELEMENT_DEPTH,  1 } } ;    /* Attributes */

    if ( mps_sac_create( &(new_subsystem->sac), new_subsystem->pool,
                         sizeof( sac_classes ) / sizeof( sac_classes[ 0 ] ),
                         sac_classes ) != MPS_RES_OK ) {
      xmlg_subsystem_free(new_subsystem, new_subsystem) ;
      return FALSE ;
    }
  }

  *xml_ctxt = new_subsystem ;

  return TRUE ;
}

HqBool xmlg_parser_common_init(
      xmlGParser *xml_parser,
      xmlGContext *xml_ctxt,
      xmlGMemoryHandler *memory_handler,
      xmlGFilterChain *filter_chain)
{
  xmlGParserCommon *c;

  XMLGASSERT(xml_ctxt != NULL, "xml_ctxt is NULL");
  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL");

  c = xmlg_get_parser_common(xml_parser) ;

  /* Plug memory xml_parser first. */
  if (memory_handler != NULL) {
    if (memory_handler->f_malloc == NULL ||
        memory_handler->f_realloc == NULL ||
        memory_handler->f_free == NULL) {

      XMLGASSERT(FALSE, "incomplete memory handler") ;
      return FALSE ;
    }
    c->memory_handler.f_malloc = memory_handler->f_malloc;
    c->memory_handler.f_realloc = memory_handler->f_realloc;
    c->memory_handler.f_free = memory_handler->f_free;
  } else {

    /* Use XML sub system memory handler. */
    c->memory_handler.f_malloc = xml_ctxt->memory_handler.f_malloc;
    c->memory_handler.f_realloc = xml_ctxt->memory_handler.f_realloc;
    c->memory_handler.f_free = xml_ctxt->memory_handler.f_free;
  }

  c->xml_ctxt = xml_ctxt;
  c->prev_line = 0;
  c->prev_column = 0;
  c->mapuri_cb = NULL ;
  c->filter_chain = filter_chain ;
  c->error_abort = FALSE ;
  c->success_abort = FALSE ;

  if (filter_chain != NULL) {
    /* Hack - was hoping not to do this. */
    filter_chain->xml_parser = xml_parser ;
  }

  return TRUE;
}

void xmlg_parser_common_terminate(
      xmlGParser *xml_parser)
{
  xmlGParserCommon *c;

  XMLGASSERT(xml_parser != NULL, "xml_parser is NULL");
  c = xmlg_get_parser_common(xml_parser);

  /* nothing to do yet */
}

/* ============================================================================
* Log stripped */
