#ifndef __XMLGFILTERCHAINPRIV_H__
#define __XMLGFILTERCHAINPRIV_H__
/* ============================================================================
 * $HopeName: HQNgenericxml!src:xmlgfilterchainpriv.h(EBDSDK_P.1) $
 * $Id: src:xmlgfilterchainpriv.h,v 1.7.11.1.1.1 2013/12/19 11:24:21 anon Exp $
 *
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \brief Private interfaces for XML filter chains.
 */

#define MAX_NUM_FILTERS 30

typedef struct xmlGBase {
  struct xmlGBase *next ;
  uint32 depth ;
  hqn_uri_t *uri ;
} xmlGBase ;

struct xmlGFilterChain {
  /* I wanted to avoid having to add input adapters to the filter
     chain. */
  xmlGParser *xml_parser ;

  xmlGParseErrorCallback parse_error_cb ;

  int32 id ;

  /* element depth of the filter chain - used for namespace
     tracking */
  uint32 depth ;

  struct xmlGMemoryHandler memory_handler ;

  void *user_data ;

  /* Are we under error condition? */
  HqBool error_abort ;

  /* The XML sub-system context. */
  xmlGContext *xml_ctxt ;

  /* The URI of the XML stream we are parsing. */
  hqn_uri_t *uri ;

  /**
   * Base URI of this XML stream.
   *
   * \note The XML base may be different from the XML parse URI. A
   * good example of this is XPS relationship parts where the base URI
   * is the associated part and not the relationship part itself.
   */
  struct xmlGBase *base ;

  /* We track namespaces in the filter chain because filters may not
     see all prefix declarations yet may need to look them up. */
  HqBool new_prefix_block ;
  NamespaceBlockStack *namespace_stack ;

  /* In reality, there are never going to be that many filters, so why
     not keep them in an array rather than managing a list. */
  xmlGFilter* filters[MAX_NUM_FILTERS] ;

  /* When we start to undo the filters because of an abort, we keep
     calling the filters undo function until there the filter reports
     that it has exhausted its undo stack. When all filters are
     undone, we exit from the filter chain execute command with
     FALSE. */
  HqBool all_filters_undone ;
  HqBool filters_undone[MAX_NUM_FILTERS] ;

  /* When one of the filter user callbacks fails, we drop into an undo
     loop. This array ensures that the filters user error handler is
     only called once. */
  HqBool error_cb_called[MAX_NUM_FILTERS] ;

  /* Despite keeping the filters in an array to track position, we do
     keep doubly linked lists for fast interation. */
  xmlGFilter *first ;
  xmlGFilter *last ;

  struct xmlGFilterChain *next ;
} ;

/* This is here so the parser can call it when a parse error occurs */
extern
void xmlg_fc_execute_undo(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain) ;

/* ============================================================================
* Log stripped */
#endif
