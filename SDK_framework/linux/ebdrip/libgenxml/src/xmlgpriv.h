#ifndef __XMLGPRIV_H__
#define __XMLGPRIV_H__
/* ============================================================================
 * $HopeName: HQNgenericxml!src:xmlgpriv.h(EBDSDK_P.1) $
 * $Id: src:xmlgpriv.h,v 1.29.10.1.1.1 2013/12/19 11:24:21 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \brief Private routines between XML backend and public interface.
 */

#include "hqnuri.h"
#include "xmlgtype.h"
#include "xmlgassert.h"
#include "xmlgattrspriv.h"
#include "xmlgnsblockpriv.h"
#include "xmlgfunctablepriv.h"
#include "xmlginternpriv.h"
#include "xmlgfilterpriv.h"
#include "xmlgfilterchainpriv.h"
#include "xmlg.h"

/* ============================================================================
 */

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(DWORD_ALIGN_UP)
#define DWORD_ALIGN_UP(x) (((int32)(x)+7)&~7)
#endif

/* ============================================================================
 * Internal types
 * ============================================================================
 */

#define NAME_AND_LENGTH(_str) (uint8 *)("" _str ""), sizeof("" _str "") - 1

struct xmlGContext {
  mps_pool_t pool ;
  mps_sac_t sac ;
  struct xmlGMemoryHandler memory_handler ;
  struct xmlGIStrHandler intern_handler ;
  xmlGInternPool *intern_pool ;
  hqn_uri_context_t *uri_context ;
} ;

typedef struct xmlGParserCommon {
  struct xmlGContext *xml_ctxt ;

  struct xmlGMemoryHandler memory_handler ;

  xmlGFilterChain *filter_chain ;

  /* Gets called for *every* URI seen in the XML and allows one to
     implement a URI alias mechanism. */
  xmlGMapUriCallback mapuri_cb ;

  xmlGParseErrorCallback parse_error_cb ;

  /* Under error conditions, we stop asking the real parser to give us
     line and column information. */
  uint32 prev_line, prev_column ;

  /* When set, this stops parsing and FALSE will be returned from the
     parse command. */
  HqBool error_abort ;

  /* When set, this stops parsing and TRUE will be returned from the
     parse command. */
  HqBool success_abort ;
} xmlGParserCommon ;

/* ============================================================================
 * Memory allocation/deallocation functions.
 * ============================================================================
 */

extern
/*@only@*/ /*@null@*/
void * xmlg_subsystem_malloc(
      /*@in@*/ /*@notnull@*/
      xmlGContext *xml_ctxt,
      size_t size) ;

extern
/*@only@*/ /*@null@*/
void * xmlg_subsystem_realloc(
      /*@in@*/ /*@notnull@*/
      xmlGContext *xml_ctxt,
      /*@out@*/ /*@null@*/ /*@only@*/
      void *memPtr,
      size_t size) ;

extern void xmlg_subsystem_free(
      /*@in@*/ /*@notnull@*/
      xmlGContext *xml_ctxt,
      /*@out@*/ /*@null@*/ /*@only@*/
      void *memPtr) ;

extern
/*@only@*/ /*@null@*/
void * xmlg_parser_malloc(
      /*@in@*/ /*@notnull@*/
      xmlGParser *xml_parser,
      size_t size) ;

extern
/*@only@*/ /*@null@*/
void * xmlg_parser_realloc(
      /*@in@*/ /*@notnull@*/
      xmlGParser *xml_parser,
      /*@out@*/ /*@null@*/ /*@only@*/
      void *memPtr,
      size_t size) ;

extern void xmlg_parser_free(
      /*@in@*/ /*@notnull@*/
      xmlGParser *xml_parser,
      /*@out@*/ /*@null@*/ /*@only@*/
      void *memPtr) ;

extern
/*@only@*/ /*@null@*/
void * xmlg_fc_malloc(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain,
      size_t size) ;

extern
/*@only@*/ /*@null@*/
void * xmlg_fc_realloc(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain,
      /*@out@*/ /*@null@*/ /*@only@*/
      void *memPtr,
      size_t size) ;

extern void xmlg_fc_free(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain,
      /*@out@*/ /*@null@*/ /*@only@*/
      void *memPtr) ;

/* ============================================================================
 * Basic utility functions.
 * ============================================================================
 */

extern
HqBool xmlg_subsystem_common_init(
      xmlGContext **xml_ctxt,
      mps_pool_t pool,
      xmlGMemoryHandler *memHandler,
      xmlGIStrHandler *internHandler,
      hqn_uri_context_t *uri_context) ;

extern
void xmlg_subsystem_common_terminate(
      xmlGContext **xml_ctxt) ;

/**
 * \brief This gets called from xmlGHandlerNew and initializes common parts of
 * the XML parse structure.
 */
extern
HqBool xmlg_parser_common_init(
      xmlGParser *xml_parser,
      xmlGContext *xml_ctxt,
      xmlGMemoryHandler *memory_handler,
      xmlGFilterChain *filter_chain) ;

extern
void xmlg_parser_common_terminate(
      /*@in@*/ /*@notnull@*/
      xmlGParser *xml_parser) ;

extern
HqBool xmlg_parser_line_and_column(
      xmlGParser *xml_parser,
      uint32 *line,
      uint32 *column) ;

/**
 * This is implemented by each backend parser so I can get to the common
 * data structure in parser handler (which is different per parser).
 */
extern
/* Defined as shared as this returns a pointer to an internal structure which
 * belongs to the containing structure which is released.
 */
/*@notnull@*/ /*@shared@*/
xmlGParserCommon* xmlg_get_parser_common(
      /*@in@*/ /*@notnull@*/
      xmlGParser *xml_parser) ;

#ifdef __cplusplus
}
#endif

/* ============================================================================
* Log stripped */
#endif /*! __XMLGPRIV_H__*/
