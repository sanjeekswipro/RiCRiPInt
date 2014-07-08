/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlparsercontext.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief Implements the PCLXL_PARSER_CONTEXT handling functions that
 * maintain a simple parser context.
 */

#include "core.h"

#include "pclxlcontext.h"
#include "pclxlattributes.h"
#include "pclxlparsercontext.h"
#include "pclxlerrors.h"
#include "pclxlpattern.h"
#include "pclxlsmtable.h" /* Note that this file is automatically
                             generated from states\operators.sm */

Bool pclxl_parser_push_stream(
  PCLXL_PARSER_CONTEXT  parser_context,
  FILELIST*             flptr)
{
  PCLXLSTREAM*  p_stream;

  /* Pick up an unused stream structure and start it on the filestream */
  p_stream = SLL_GET_HEAD(&parser_context->free_streams, PCLXLSTREAM, next);
  if ( p_stream == NULL ) {
    return(FALSE);
  }
  SLL_REMOVE_HEAD(&parser_context->free_streams);
  SLL_ADD_HEAD(&parser_context->stream_stack, p_stream, next);
  if ( !pclxl_stream_start(parser_context->pclxl_context, p_stream, flptr) ) {
    return(FALSE);
  }

  return(TRUE);

} /* pclxl_parser_push_stream */

void pclxl_parser_pop_stream(
  PCLXL_PARSER_CONTEXT  parser_context)
{
  PCLXLSTREAM*  p_stream;

  HQASSERT((!SLL_LIST_IS_EMPTY(&parser_context->stream_stack)),
           "pclxl_parser_context: stream stack is empty");

  /* Remove current stream and move to free stream list */
  p_stream = SLL_GET_HEAD(&parser_context->stream_stack, PCLXLSTREAM, next);
  SLL_REMOVE_HEAD(&parser_context->stream_stack);
  pclxl_stream_close(parser_context->pclxl_context, p_stream);
  SLL_ADD_HEAD(&parser_context->free_streams, p_stream, next);

} /* pclxl_parser_pop_stream */

/**
 * \brief Creates a parser context from scratch by accepting an
 * initial parser state and a PCLXL stream
 */

PCLXL_PARSER_CONTEXT pclxl_create_parser_context(
  PCLXL_CONTEXT      pclxl_context,
  PCLXL_PARSER_STATE parser_state)
{
  int32 i;
  PCLXL_PARSER_CONTEXT new_parser_context;
  PCLXL_PARSER_CONTEXT_STRUCT init = {0};

  HQASSERT((pclxl_context != NULL),
           "NULL PCLXL context supplied when attempting to allocate a parser state") ;

  if ( (new_parser_context = mm_alloc(pclxl_context->memory_pool,
                                      sizeof(PCLXL_PARSER_CONTEXT_STRUCT),
                                      MM_ALLOC_CLASS_PCLXL_PARSER_CONTEXT)) == NULL ) {
    /* We failed to allocate a PCLXL parser context structure so we
     * cannot populate it and the caller must not attempt to add it
     * into the parser context stack
     */
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_INSUFFICIENT_MEMORY,
                        ("Failed to allocate new Parser Context structure"));
    return NULL;

  }
  *new_parser_context = init;

  if ( (new_parser_context->attr_set = pclxl_attr_set_create(pclxl_context->memory_pool)) == NULL ) {
    mm_free(pclxl_context->memory_pool, new_parser_context, sizeof(PCLXL_PARSER_CONTEXT_STRUCT));
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_INSUFFICIENT_MEMORY,
                        ("Failed to allocate new Parser Context structure"));
    return(NULL);
  }

  new_parser_context->pclxl_context = pclxl_context;
  new_parser_context->parser_states[new_parser_context->parser_states_index] = parser_state;

  /* Reset stream lists, initialise the fixed number of streams and add them to
   * the free list.
   */
  SLL_RESET_LIST(&new_parser_context->stream_stack);
  SLL_RESET_LIST(&new_parser_context->free_streams);
  for ( i = 0; i < 33; i++ ) {
    pclxl_stream_init(&new_parser_context->streams[i]);
    SLL_ADD_HEAD(&new_parser_context->free_streams, &new_parser_context->streams[i], next);
  }

  new_parser_context->data_source_open = FALSE;

  return new_parser_context;
}

void
pclxl_delete_parser_context(PCLXL_PARSER_CONTEXT parser_context)
{
  HQASSERT((parser_context != NULL), "Cannot delete/free a NULL parser context");

  /* Ensure the attribute set is clear */
  if ( parser_context->attr_set ) {
    pclxl_attr_set_destroy(parser_context->attr_set);
  }

  /* Pop off any remaining active streams */
  while ( !SLL_LIST_IS_EMPTY(&parser_context->stream_stack) ) {
    pclxl_parser_pop_stream(parser_context);
  }

  if (parser_context->pattern_construction.cache_entry != NULL) {
    /* A pattern is in construction; clean up construction objects. */
    pclxl_pattern_free_construction_state(parser_context, TRUE);
  }

  HQASSERT(parser_context->image_reader == NULL, "Raster read context is not NULL") ;

  mm_free(parser_context->pclxl_context->memory_pool,
          parser_context,
          sizeof(PCLXL_PARSER_CONTEXT_STRUCT));
}

PCLXL_PARSER_CONTEXT
pclxl_get_parser_context()
{
  PCLXL_CONTEXT pclxl_context;

  if ( (pclxl_context = pclxl_get_context()) != NULL )
    return pclxl_context->parser_context;

  return NULL;
}

void set_exit_parser(PCLXL_PARSER_CONTEXT parser_context, Bool do_exit)
{
  parser_context->exit_parser = do_exit;
}

/******************************************************************************
* Log stripped */
