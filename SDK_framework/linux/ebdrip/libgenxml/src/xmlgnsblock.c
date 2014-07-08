/* ============================================================================
 * $HopeName: HQNgenericxml!src:xmlgnsblock.c(EBDSDK_P.1) $
 * $Id: src:xmlgnsblock.c,v 1.14.11.1.1.1 2013/12/19 11:24:21 anon Exp $
 * 
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */

#include "xmlgpriv.h"
#include "xmlgnsblockpriv.h"
#include "xmlginternpriv.h"
#include "xmlgassert.h"

/**
 * Smallish prime.
 * Do not expect there will many namespaces in a single element.
 * \todo Hash size and function will need profiling.
 */
#define NAMESPACEBLOCK_HASH_SIZE 31

struct PrefixEntry {
  const xmlGIStr *prefix;  /* Key */
  const xmlGIStr *uri;     /* Payload */
  struct PrefixEntry *next;     /* Singly-linked list */
};

struct NamespaceBlock {
  xmlGFilterChain *filter_chain;
  /* Number of entries in hash table */
  uint32 num_entries;
  /* The hash table */
  /*@partial@*/
  struct PrefixEntry **table;
  uint32 depth;
  struct NamespaceBlock *next;
};

struct NamespaceBlockStack {
  xmlGFilterChain *filter_chain;
  NamespaceBlock *top;
};

static
/*@null@*/
struct PrefixEntry *xmlg_find_prefix(
      const NamespaceBlock *namespaceblock,
      const xmlGIStr *prefix,
      uint32 *hval)
{
  xmlGContext *xml_ctxt;
  xmlGFilterChain *filter_chain;
  struct PrefixEntry *curr;
  
  XMLGASSERT(namespaceblock != NULL, "namespaceblock is NULL");
  XMLGASSERT(hval != NULL, "hval is NULL");

  filter_chain = namespaceblock->filter_chain;
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL");
  xml_ctxt = filter_chain->xml_ctxt;

  *hval = 0;
  if (prefix != NULL) {
    /* Lets just use the pointer of the interned localname. */
    *hval = CAST_UINTPTRT_TO_UINT32( ((uintptr_t)prefix >> 2) % NAMESPACEBLOCK_HASH_SIZE) ;
  }

  for (curr = namespaceblock->table[*hval]; curr != NULL; curr = curr->next) {
    if ( xmlg_istring_equal(xml_ctxt, curr->prefix, prefix) ) {
      return curr;
    }
  }
  return NULL;
}

/* ============================================================================
 * Namespace block functions.
 */
HqBool namespace_block_create(
      xmlGFilterChain *filter_chain,
      NamespaceBlock **namespaceblock)
{
  uint32 i ;

  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL");
  XMLGASSERT(namespaceblock != NULL, "namespaceblock pointer is NULL");

  if ((*namespaceblock = xmlg_fc_malloc(filter_chain, sizeof(NamespaceBlock))) == NULL)
    return FALSE;

  (*namespaceblock)->table = xmlg_fc_malloc(filter_chain,
        sizeof(struct PrefixEntry*) * NAMESPACEBLOCK_HASH_SIZE) ;
  if ((*namespaceblock)->table == NULL) {
    xmlg_fc_free(filter_chain, *namespaceblock) ;
    *namespaceblock = NULL;
    return FALSE;
  }

  /* Initialize the namespace block structure. */
  (*namespaceblock)->filter_chain = filter_chain;
  (*namespaceblock)->num_entries = 0;
  (*namespaceblock)->depth = 0;
  (*namespaceblock)->next = NULL;

  for (i=0; i<NAMESPACEBLOCK_HASH_SIZE; i++) {
    (*namespaceblock)->table[i] = NULL;
  }
  return TRUE;
}

void namespace_block_destroy(
      NamespaceBlock **namespaceblock)
{
  xmlGContext *xml_ctxt ;
  xmlGFilterChain *filter_chain ;
  uint32 i;
  struct PrefixEntry *curr, *next;

  XMLGASSERT(namespaceblock != NULL, "namespaceblock is NULL");
  XMLGASSERT(*namespaceblock != NULL, "namespaceblock pointer is NULL");

  filter_chain = (*namespaceblock)->filter_chain ;
  xml_ctxt = filter_chain->xml_ctxt ;

  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL");

  for (i=0; i<NAMESPACEBLOCK_HASH_SIZE; i++) {
    for (curr = (*namespaceblock)->table[i]; curr != NULL; curr = next) {
      next = curr->next;

      xmlg_istring_destroy(xml_ctxt, &curr->prefix);
      xmlg_istring_destroy(xml_ctxt, &curr->uri);

      xmlg_fc_free(filter_chain, curr) ;
      (*namespaceblock)->num_entries--;
    }
  }
  XMLGASSERT((*namespaceblock)->num_entries == 0, "num_entries is not zero.");

  xmlg_fc_free(filter_chain, (*namespaceblock)->table);
  xmlg_fc_free(filter_chain, *namespaceblock);
  (*namespaceblock) = NULL;
  return;
}

HqBool namespace_block_add_namespace(
      NamespaceBlock *namespaceblock,
      const xmlGIStr *prefix,
      const xmlGIStr *uri)
{
  xmlGContext *xml_ctxt ;
  xmlGFilterChain *filter_chain ;
  struct PrefixEntry *curr;
  uint32 hval;

  XMLGASSERT(namespaceblock != NULL, "namespaceblock is NULL");

  filter_chain = namespaceblock->filter_chain;
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL");
  xml_ctxt = filter_chain->xml_ctxt;

  curr = xmlg_find_prefix(namespaceblock,
                          prefix,
                          &hval);

  if (curr == NULL) {
    curr = xmlg_fc_malloc(filter_chain, sizeof(struct PrefixEntry));
    if (curr == NULL) {
      return FALSE;
    }

    curr->prefix = prefix;
    if (!xmlg_istring_reserve(xml_ctxt, prefix) ) {
      xmlg_fc_free(filter_chain, curr) ;
      return FALSE ;
    }

    curr->uri = uri;
    if ( !xmlg_istring_reserve(xml_ctxt, uri) ) {
      xmlg_istring_destroy(xml_ctxt, &curr->prefix) ;
      xmlg_fc_free(filter_chain, curr) ;
      return FALSE ;
    }

    curr->next = namespaceblock->table[hval];
    namespaceblock->table[hval] = curr;
    namespaceblock->num_entries++;
  } else {
    /* Keep the existing prefix istr as the key, but we ought to destroy
     * the URI istr payload.
     */
    xmlg_istring_destroy(xml_ctxt, &curr->uri);
    XMLGASSERT(curr->uri == NULL, "uri is not NULL");

    if ( !xmlg_istring_reserve(xml_ctxt, uri) )
      return FALSE ;

    curr->uri = uri;
  }

  return TRUE;
}

HqBool namespace_block_get_namespace(
      const NamespaceBlock *namespaceblock,
      const xmlGIStr* prefix,
      const xmlGIStr **uri)
{
  struct PrefixEntry *curr;
  xmlGFilterChain *filter_chain;
  xmlGContext *xml_ctxt;
  uint32 hval;

  XMLGASSERT(namespaceblock != NULL, "namexpaceblock is NULL");
  XMLGASSERT(uri != NULL, "URI pointer pointer is NULL");

  filter_chain = namespaceblock->filter_chain;
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL");
  xml_ctxt = filter_chain->xml_ctxt;

  *uri = NULL ;

  curr = xmlg_find_prefix(namespaceblock,
                          prefix,
                          &hval);

  if ( curr == NULL ||
       !xmlg_istring_reserve(xml_ctxt, curr->uri) )
    return FALSE ;

  *uri = curr->uri;

  return TRUE;
}

/* ============================================================================
 * Namespace block stack functions.
 */

HqBool namespace_stack_create(
      xmlGFilterChain *filter_chain,
      NamespaceBlockStack **namespace_stack)
{
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL");
  XMLGASSERT(namespace_stack != NULL, "namespace_stack is NULL");

  *namespace_stack = xmlg_fc_malloc(filter_chain, sizeof(NamespaceBlockStack));
  if (*namespace_stack == NULL) {
    return FALSE;
  }
  (*namespace_stack)->filter_chain = filter_chain;
  (*namespace_stack)->top = NULL;
  return TRUE;
}

void namespace_stack_destroy(
      NamespaceBlockStack **namespace_stack)
{
  xmlGFilterChain *filter_chain;

  XMLGASSERT(namespace_stack != NULL, "namespace_stack is NULL");
  XMLGASSERT(*namespace_stack != NULL, "namespace_stack pointer is NULL");
  XMLGASSERT((*namespace_stack)->top == NULL, "namespace stack is not empty");

  filter_chain = (*namespace_stack)->filter_chain;

  xmlg_fc_free(filter_chain, *namespace_stack);
  *namespace_stack = NULL;
}

HqBool namespace_stack_push(
      NamespaceBlockStack *namespace_stack,
      NamespaceBlock *namespace_block,
      uint32 depth)
{
  XMLGASSERT(namespace_stack != NULL, "namespace_stack is NULL");
  XMLGASSERT(namespace_block != NULL, "namespace_block is NULL");

  namespace_block->next = namespace_stack->top;
  namespace_stack->top = namespace_block;
  namespace_block->depth = depth;
  return TRUE;
}

/**
 * Pop the namespace block from the top of the stack and destroy it.
 * The pop only occurs if the depths match.
 */
void namespace_stack_pop(
      NamespaceBlockStack *namespace_stack,
      uint32 depth)
{
  NamespaceBlock *top_namespace_block;
  XMLGASSERT(namespace_stack != NULL, "namespace_stack is NULL");

  top_namespace_block = namespace_stack->top;

  if (top_namespace_block != NULL) {
    if (depth == top_namespace_block->depth) {
      namespace_stack->top = namespace_stack->top->next;
      namespace_block_destroy(&top_namespace_block);
      XMLGASSERT(top_namespace_block == NULL, "top namespace block is not NULL");
    }
  }
}

extern
NamespaceBlock * namespace_stack_top(
      NamespaceBlockStack *namespace_stack)
{
  XMLGASSERT(namespace_stack != NULL, "namespace_stack is NULL");

  return namespace_stack->top;
}

/**
 * Find the URI from the provided prefix.
 */
HqBool namespace_stack_find(
      const NamespaceBlockStack *namespace_stack,
      const xmlGIStr *prefix,
      const xmlGIStr **uri)
{
  struct NamespaceBlock *namespaceblock;
  XMLGASSERT(namespace_stack != NULL, "namespace_stack is NULL");
  XMLGASSERT(prefix != NULL, "prefix is NULL");
  XMLGASSERT(uri != NULL, "uri is NULL");

  namespaceblock = namespace_stack->top;
  while (namespaceblock != NULL) {
    if (namespace_block_get_namespace(namespaceblock, prefix, uri)) {
      return TRUE;
    }
    namespaceblock = namespaceblock->next;
  }
  return FALSE;
}

/* ============================================================================
* Log stripped */
