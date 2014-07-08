/* ============================================================================
 * $HopeName: HQNgenericxml!src:xmlgfilterchain.c(EBDSDK_P.1) $
 * $Id: src:xmlgfilterchain.c,v 1.19.10.1.1.1 2013/12/19 11:24:21 anon Exp $
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
 * \brief Implements XML filters.
 */

#include "xmlg.h"
#include "xmlgpriv.h"
#include "xmlginternpriv.h"
#include "xmlgfilterpriv.h"
#include "xmlgfilterchainpriv.h"

typedef struct xmlGFilterChainMetaData {
  uint32            next_id;      /* Unsigned non-zero integer */
  xmlGFilterChain   *first ;
} xmlGFilterChainMetaData ;

xmlGFilterChainMetaData filter_chains = {
  (uint32) 1, NULL
} ;

HqBool xmlg_fc_new(
      xmlGContext *xml_ctxt,
      xmlGFilterChain **filter_chain,
      xmlGMemoryHandler *memory_handler,
      hqn_uri_t *uri,
      hqn_uri_t *base_uri,
      void *user_data)
{
  xmlGFilterChain *new_fc ;
  hqn_uri_t *new_base_uri ;

  HQASSERT(xml_ctxt != NULL, "xml_ctxt is NULL") ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  HQASSERT(uri != NULL, "uri is NULL") ;
  HQASSERT(base_uri != NULL, "base_uri is NULL") ;

  *filter_chain = NULL ;

  if (memory_handler != NULL) {
    if (memory_handler->f_malloc == NULL ||
        memory_handler->f_realloc == NULL ||
        memory_handler->f_free == NULL) {

      XMLGASSERT(FALSE, "incomplete memory handler") ;
      return FALSE ;
    }

    if ((new_fc = memory_handler->f_malloc(sizeof(xmlGFilterChain))) == NULL)
      return FALSE ;
    (void)bzero((char *)new_fc, sizeof(xmlGFilterChain)) ;

    new_fc->memory_handler.f_malloc = memory_handler->f_malloc ;
    new_fc->memory_handler.f_realloc = memory_handler->f_realloc ;
    new_fc->memory_handler.f_free = memory_handler->f_free ;
  } else {
    if ((new_fc = xml_ctxt->memory_handler.f_malloc(sizeof(xmlGFilterChain))) == NULL)
      return FALSE ;
    (void)bzero((char *)new_fc, sizeof(xmlGFilterChain)) ;

    new_fc->memory_handler.f_malloc = xml_ctxt->memory_handler.f_malloc ;
    new_fc->memory_handler.f_realloc = xml_ctxt->memory_handler.f_realloc ;
    new_fc->memory_handler.f_free = xml_ctxt->memory_handler.f_free ;
  }

  new_fc->xml_ctxt = xml_ctxt ;
  new_fc->parse_error_cb = NULL ;
  new_fc->uri = uri ;

  /* Set up the base which scopes the whole XML document. */
  if ((new_fc->base = xmlg_fc_malloc(new_fc, sizeof(xmlGBase))) == NULL) {
    xmlg_fc_free(new_fc, new_fc) ;
    return FALSE;
  }

  if (! hqn_uri_copy(xml_ctxt->uri_context, base_uri, &new_base_uri)) {
    xmlg_fc_free(new_fc, new_fc->base) ;
    xmlg_fc_free(new_fc, new_fc) ;
  }
  new_fc->base->uri = new_base_uri ;
  new_fc->base->next = NULL ;
  new_fc->base->depth = 0 ;

  new_fc->id = filter_chains.next_id++ ;
  new_fc->new_prefix_block = TRUE ;
  new_fc->user_data = user_data ;
  new_fc->first = NULL ;
  new_fc->last = NULL ;

  if (! namespace_stack_create(new_fc, &(new_fc->namespace_stack))) {
    hqn_uri_free(&new_base_uri) ;
    xmlg_fc_free(new_fc, new_fc->base) ;
    xmlg_fc_free(new_fc, new_fc) ;
    return FALSE;
  }

  new_fc->next = filter_chains.first ;
  filter_chains.first = new_fc ;

  /* in a release build, its very unlikely that a filter with ID zero
     will last the life time of uint32 for all subsequent filters
     created */

  if ( filter_chains.next_id == 0 ) {
    XMLGASSERT(FALSE, "wrapped a 32 bit counter!") ;
    filter_chains.next_id = 1 ;
  }

  *filter_chain = new_fc ;
  return TRUE ;
}

void xmlg_fc_destroy(
      xmlGFilterChain **filter_chain)
{
  xmlGFilterChain *old_fc, **prev_fc, *fc ;

  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  HQASSERT(*filter_chain != NULL, "filter_chain pointer is NULL") ;

  old_fc = *filter_chain ;

  prev_fc = &filter_chains.first ;
  for (fc = filter_chains.first ; fc != NULL; fc = fc->next) {
    if (fc == old_fc) {
      XMLGASSERT(fc->id == old_fc->id, "filter chain id's do not match") ;
      *prev_fc = fc->next ;
      break ;
    }
    prev_fc = &fc->next ;
  }

  {
    xmlGFilter *old_f = old_fc->first ;
    while (old_f != NULL) {
      xmlGFilter *next_f = old_f->next ;

      HQASSERT(old_fc->filters[old_f->position] == old_f,
               "filter array is not in synch with filter list") ;

      old_fc->filters[old_f->position] = NULL ;
      xmlg_f_destroy(&old_f) ;
      old_f = next_f ;
    }
  }

  namespace_stack_destroy(&(old_fc->namespace_stack)) ;

  HQASSERT(old_fc->base != NULL, "document base is NULL") ;

  while (old_fc->base != NULL) {
    xmlGBase *next = old_fc->base->next ;
    hqn_uri_free(&(old_fc->base->uri)) ;
    xmlg_fc_free(old_fc, old_fc->base) ;
    old_fc->base = next ;
  }

  /* be careful to do this last */
  xmlg_fc_free(old_fc, old_fc) ;
  *filter_chain = NULL ;
}

HqBool xmlg_fc_new_filter(
      xmlGFilterChain *filter_chain,
      xmlGFilter **filter,
      uint32 position,
      void *user_data,
      xmlGFilterCleanupCallback cleanup_cb)
{
  xmlGFilter *new_filter ;
  int32 i ;

  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  XMLGASSERT(filter != NULL, "filter is NULL") ;
  XMLGASSERT(position < MAX_NUM_FILTERS, "position out of bounds") ;

  *filter = NULL ;

  /* we do NOT allow clobbering - there is too much context in the
     filter to allow it */
  if (filter_chain->filters[position] != NULL)
    return FALSE ;

  if ((new_filter = xmlg_fc_malloc(filter_chain, sizeof(xmlGFilter))) == NULL)
    return FALSE ;
  (void)bzero((char *)new_filter, sizeof(xmlGFilter)) ;

  new_filter->position = position ;
  new_filter->user_data = user_data ;
  new_filter->filter_chain = filter_chain ;
  new_filter->xml_ctxt = filter_chain->xml_ctxt ;
  new_filter->cleanup_cb = cleanup_cb ;
  new_filter->next = NULL ;
  new_filter->prev = NULL ;

  if (! xmlg_funct_table_create(filter_chain, &(new_filter->element_callbacks))) {
    xmlg_fc_free(filter_chain, new_filter) ;
    return FALSE;
  }

  /* Insert new filter. */
  for (i=position + 1; i<MAX_NUM_FILTERS; i++) {
    if (filter_chain->filters[i] != NULL) {
      new_filter->next = filter_chain->filters[i] ;
      filter_chain->filters[i]->prev = new_filter ;
      break ;
    }
  }
  for (i=position - 1; i>=0; i--) {
    if (filter_chain->filters[i] != NULL) {
      new_filter->prev = filter_chain->filters[i] ;
      filter_chain->filters[i]->next = new_filter ;
      break ;
    }
  }

  if (filter_chain->first == NULL ||
      filter_chain->first == new_filter->next)
    filter_chain->first = new_filter ;

  if (filter_chain->last == NULL ||
      filter_chain->last == new_filter->prev)
    filter_chain->last = new_filter ;

  filter_chain->filters[position] = new_filter ;

  *filter = new_filter ;

  return TRUE ;
}

HqBool xmlg_fc_delete_filter_position(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain,
      uint32 removal_position)
{
  xmlGFilter *old_filter ;

  if (removal_position > MAX_NUM_FILTERS) {
    XMLGASSERT(FALSE, "removal_position is out of range") ;
    return FALSE ;
  }

  old_filter = filter_chain->filters[removal_position] ;

  if (old_filter->next != NULL)
    old_filter->next->prev = old_filter->prev ;

  if (old_filter->prev != NULL)
    old_filter->prev->next = old_filter->next ;

  if (old_filter == filter_chain->first)
    filter_chain->first = old_filter->next ;

  if (old_filter == filter_chain->last)
    filter_chain->last = old_filter->prev ;

  xmlg_f_destroy(&old_filter) ;
  filter_chain->filters[removal_position] = NULL ;

  return TRUE ;
}

void xmlg_fc_execute_undo(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain)
{
  HqBool recur_out = FALSE ;

  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL");

  if (filter_chain->all_filters_undone)
    return ;

  /* This should only happen when the XML declaration callback returns
     an error. */
  if (filter_chain->depth == 0) {
    xmlGFilter *filter = filter_chain->last ;

    while (filter != NULL) {
      xmlGFilter *prev_f = filter->prev ;
      if (! filter_chain->error_cb_called[filter->position]) {
        xmlg_f_error_abort(filter, TRUE) ;
        filter_chain->error_cb_called[filter->position] = TRUE ;
      }
      filter = prev_f ;
    }
  }

 while (! recur_out) {
    xmlGFilter *filter = filter_chain->last ;

    namespace_stack_pop(filter_chain->namespace_stack, filter_chain->depth) ;

    if (filter_chain->depth == 0)
      break ;

    /* assume we have done them all */
    filter_chain->all_filters_undone = TRUE ;

    /* We execute all undo's but track the fact that we need to recur
       out. */
    while (filter != NULL && ! recur_out) {
      xmlGFilter *prev_f = filter->prev ;

      if (! filter_chain->error_cb_called[filter->position]) {
        xmlg_f_error_abort(filter, TRUE) ;
        filter_chain->error_cb_called[filter->position] = TRUE ;
      }

      if (! filter_chain->filters_undone[filter->position]) {
        HqBool is_recursive ;

        HQASSERT(filter_chain->depth >= filter->depth,
                 "filter chain depth is less than filter depth") ;

        if (! xmlg_f_execute_undo(filter, filter_chain->depth, &is_recursive)) { /* no more on this filter */
          filter_chain->filters_undone[filter->position] = TRUE ;
        } else {
          filter_chain->all_filters_undone = FALSE ;
        }

        if (! recur_out && is_recursive)
          recur_out = TRUE ;
      }

      filter = prev_f ;
    }

    if (! recur_out)
    {
      HQASSERT( filter_chain->depth > 0, "Filter chain depth about to go negative" );
      filter_chain->depth-- ;
    }
  }
}

HqBool xmlg_fc_execute_start_element(
      xmlGFilterChain *filter_chain,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attributes)
{
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL");
  XMLGASSERT(localname != NULL, "localname is NULL");

  /* The next namespace event we get ought to cause a new namespace
     block to be created. */
  filter_chain->new_prefix_block = TRUE ;
  filter_chain->depth++ ;

  {
    int32 f_result ;
    xmlGFilter *filter = filter_chain->first ;
    while (filter != NULL) {
      xmlGFilter *next_f = filter->next ;

      if (filter_chain->error_abort)
        break ;

      f_result = xmlg_f_execute_start_element(filter, localname, prefix, uri,
                                              attributes) ;
      filter = next_f ;

      switch (f_result) {
        case XMLG_RESULT_ERROR :
          filter_chain->error_abort = TRUE ;
          filter = NULL ;
          break ;
        case XMLG_RESULT_FORWARD :
          break ;
        case XMLG_RESULT_HANDLED :
          filter = NULL ;
          break ;
        default:
          HQFAIL("invalid return status from filter execute") ;
      }
    }
  }

  if (filter_chain->error_abort) {
    xmlg_fc_execute_undo(filter_chain) ;
    namespace_stack_pop(filter_chain->namespace_stack, filter_chain->depth) ;
    return FALSE ;
  }

  return TRUE ;
}

HqBool xmlg_fc_execute_end_element(
      xmlGFilterChain *filter_chain,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      HqBool success)
{
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL");
  XMLGASSERT(localname != NULL, "localname is NULL");

  {
    int32 f_result ;
    xmlGFilter *filter = filter_chain->first ;
    while (filter != NULL) {
      xmlGFilter *next_f = filter->next ;

      if (filter_chain->error_abort)
        break ;

      f_result = xmlg_f_execute_end_element(filter, localname, prefix, uri, success) ;

      filter = next_f ;

      switch (f_result) {
        case XMLG_RESULT_ERROR :
          filter_chain->error_abort = TRUE ;
          filter = NULL ;
          break ;
        case XMLG_RESULT_FORWARD :
          break ;
        case XMLG_RESULT_HANDLED :
          filter = NULL ;
          break ;
        default:
          HQFAIL("invalid return status from filter execute") ;
      }
    }
  }

  if (filter_chain->error_abort)
    xmlg_fc_execute_undo(filter_chain) ;

  if (filter_chain->depth > 0) {
    /* Pop the base if need be. Note that the base at zero depth is
       only destroyed when parsing is torn down. */
    HQASSERT(filter_chain->base != NULL, "filter chain base is NULL") ;
    if (filter_chain->base->depth == filter_chain->depth) {
      xmlGBase *old_base = filter_chain->base ;
      filter_chain->base = old_base->next ;
      HQASSERT(filter_chain->base != NULL, "filter chain base is NULL") ;
      hqn_uri_free(&(old_base->uri)) ;
      xmlg_fc_free(filter_chain, old_base) ;
    }

    filter_chain->depth-- ;
    namespace_stack_pop(filter_chain->namespace_stack, filter_chain->depth) ;


  }

  return (! filter_chain->error_abort) ;
}

HqBool xmlg_fc_execute_namespace(
      xmlGFilterChain *filter_chain,
      const xmlGIStr *prefix,
      const xmlGIStr *uri)
{
  NamespaceBlock *namespaceblock ;

  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL");

  /* Seems we have a new prefix block to allocate. */
  if (filter_chain->new_prefix_block) {
    filter_chain->new_prefix_block = FALSE;
    if (! namespace_block_create(filter_chain, &namespaceblock)) {
      filter_chain->error_abort = TRUE ;
    }

    if (! filter_chain->error_abort &&
        ! namespace_stack_push(filter_chain->namespace_stack,
                               namespaceblock,
                               filter_chain->depth)) {
      /* Memory allocation error. */
      namespace_block_destroy(&namespaceblock);
      filter_chain->error_abort = TRUE ;
    }
  } else {
    namespaceblock = namespace_stack_top(filter_chain->namespace_stack);
    XMLGASSERT(namespaceblock != NULL, "namespace block is NULL");
  }

  if (! filter_chain->error_abort &&
      ! namespace_block_add_namespace(namespaceblock, prefix, uri)) {
    filter_chain->error_abort = TRUE ;
  }

  if (! filter_chain->error_abort) {
    int32 f_result ;
    xmlGFilter *filter = filter_chain->first ;
    while (filter != NULL) {
      xmlGFilter *next_f = filter->next ;

      f_result = xmlg_f_execute_namespace(filter, prefix, uri) ;

      filter = next_f ;

      switch (f_result) {
        case XMLG_RESULT_ERROR :
          filter_chain->error_abort = TRUE ;
          filter = NULL ;
          break ;
        case XMLG_RESULT_FORWARD :
          break ;
        case XMLG_RESULT_HANDLED :
          filter = NULL ;
          break ;
        default:
          HQFAIL("invalid return status from filter execute") ;
      }
    }
  }

  if (filter_chain->error_abort) {
    namespace_stack_pop(filter_chain->namespace_stack, filter_chain->depth) ;
    xmlg_fc_execute_undo(filter_chain) ;
    return FALSE ;
  }

  return TRUE ;
}

HqBool xmlg_fc_execute_characters(
      xmlGFilterChain *filter_chain,
      const uint8 *buf,
      uint32 buflen)
{
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL");

  {
    int32 f_result ;
    xmlGFilter *filter = filter_chain->first ;
    while (filter != NULL) {
      xmlGFilter *next_f = filter->next ;

      f_result = xmlg_f_execute_characters(filter, buf, buflen) ;

      filter = next_f ;

      switch (f_result) {
        case XMLG_RESULT_ERROR :
          filter_chain->error_abort = TRUE ;
          filter = NULL ;
          break ;
        case XMLG_RESULT_FORWARD :
          break ;
        case XMLG_RESULT_HANDLED :
          filter = NULL ;
          break ;
        default:
          HQFAIL("invalid return status from filter execute") ;
      }
    }
  }

  if (filter_chain->error_abort) {
    xmlg_fc_execute_undo(filter_chain) ;
    return FALSE ;
  }

  return TRUE ;
}

HqBool xmlg_fc_execute_xml_decl(
      xmlGFilterChain *filter_chain,
      const uint8 *version,
      uint32 version_len,
      const uint8 *encoding,
      uint32 encoding_len,
      int32 standalone)
{
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL");

  {
    int32 f_result ;
    xmlGFilter *filter = filter_chain->first ;
    while (filter != NULL) {
      xmlGFilter *next_f = filter->next ;

      f_result = xmlg_f_execute_xml_decl(filter, version, version_len, encoding, encoding_len, standalone) ;

      filter = next_f ;

      switch (f_result) {
        case XMLG_RESULT_ERROR :
          filter_chain->error_abort = TRUE ;
          filter = NULL ;
          break ;
        case XMLG_RESULT_FORWARD :
          break ;
        case XMLG_RESULT_HANDLED :
          filter = NULL ;
          break ;
        default:
          HQFAIL("invalid return status from filter execute") ;
      }
    }
  }

  if (filter_chain->error_abort) {
    xmlg_fc_execute_undo(filter_chain) ;
    return FALSE ;
  }

  return TRUE ;
}

HqBool xmlg_fc_execute_start_dtd(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain,
      /*@in@*/ /*@notnull@*/
      const uint8 *doctypeName,
      uint32 doctypeName_len,
      /*@in@*/ /*@null@*/
      const uint8 *sysid,
      uint32 sysid_len,
      /*@in@*/ /*@null@*/
      const uint8 *pubid,
      uint32 pubid_len,
      int32 has_internal_subset)
{
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL");

  {
    int32 f_result ;
    xmlGFilter *filter = filter_chain->first ;
    while (filter != NULL) {
      xmlGFilter *next_f = filter->next ;

      f_result = xmlg_f_execute_start_dtd(filter, doctypeName, doctypeName_len,
                                          sysid, sysid_len, pubid, pubid_len,
                                          has_internal_subset) ;

      filter = next_f ;

      switch (f_result) {
        case XMLG_RESULT_ERROR :
          filter_chain->error_abort = TRUE ;
          filter = NULL ;
          break ;
        case XMLG_RESULT_FORWARD :
          break ;
        case XMLG_RESULT_HANDLED :
          filter = NULL ;
          break ;
        default:
          HQFAIL("invalid return status from filter execute") ;
      }
    }
  }

  if (filter_chain->error_abort) {
    xmlg_fc_execute_undo(filter_chain) ;
    return FALSE ;
  }

  return TRUE ;
}

HqBool xmlg_fc_set_base_uri(
      xmlGFilterChain *filter_chain,
      hqn_uri_t *base)
{
  xmlGBase *new_base ;
  hqn_uri_t *new_base_uri ;

  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  XMLGASSERT(base != NULL, "base is NULL") ;

  if ((new_base = xmlg_fc_malloc(filter_chain, sizeof(xmlGBase))) == NULL)
    return FALSE ;

  HQASSERT(filter_chain->base != NULL, "filter chain base is NULL") ;
  HQASSERT(filter_chain->xml_ctxt != NULL, "filter chain XML context is NULL") ;
  HQASSERT(filter_chain->xml_ctxt->uri_context != NULL, "filter chain XML context uri context is NULL") ;

  if (! hqn_uri_copy(filter_chain->xml_ctxt->uri_context, base, &new_base_uri)) {
    xmlg_fc_free(filter_chain, new_base) ;
    return FALSE ;
  }

  new_base->depth = filter_chain->depth ;
  new_base->next = filter_chain->base ;
  new_base->uri = new_base_uri ;
  filter_chain->base = new_base ;
  return TRUE ;
}

hqn_uri_t *xmlg_fc_get_base_uri(
      xmlGFilterChain *filter_chain)
{
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  return filter_chain->base->uri ;
}

hqn_uri_t* xmlg_fc_get_uri(
      xmlGFilterChain *filter_chain)
{
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  return filter_chain->uri ;
}

void* xmlg_fc_get_user_data(
      xmlGFilterChain *filter_chain)
{
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  return filter_chain->user_data ;
}

void xmlg_fc_set_user_data(
      xmlGFilterChain *filter_chain,
      void *user_data)
{
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  XMLGASSERT(filter_chain->user_data == NULL,
             "filter_chain user_data is not NULL") ;
  filter_chain->user_data = user_data ;
}

void xmlg_fc_set_parse_error_cb(
      xmlGFilterChain *filter_chain,
      xmlGParseErrorCallback f)
{
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL") ;

  filter_chain->parse_error_cb = f ;

  if (filter_chain->xml_parser != NULL)
    xmlg_p_set_parse_error_cb(filter_chain->xml_parser,
                              filter_chain->parse_error_cb) ;
}

xmlGFilterChain* xmlg_find_filter_chain(
      int32 filter_chain_id)
{
  xmlGFilterChain *fc ;
  XMLGASSERT(filter_chain_id > 0, "filter_chain_id less than zero") ;

  for (fc = filter_chains.first ; fc != NULL; fc = fc->next) {
    if (fc->id == filter_chain_id)
      return fc ;
  }

  return NULL ;
}

/* ============================================================================
* Log stripped */
