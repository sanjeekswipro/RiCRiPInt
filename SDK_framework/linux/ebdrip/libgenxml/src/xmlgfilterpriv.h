#ifndef __XMLGFILTERPRIV_H__
#define __XMLGFILTERPRIV_H__
/* ============================================================================
 * $HopeName: HQNgenericxml!src:xmlgfilterpriv.h(EBDSDK_P.1) $
 * $Id: src:xmlgfilterpriv.h,v 1.12.11.1.1.1 2013/12/19 11:24:21 anon Exp $
 *
 * Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \brief Private interfaces for XML filter.
 */

/* ============================================================================
 * These structures are defined here so that we can set up SAC
 * allocations for them. All structures ought to be 8 byte aligned
 * although this is protected by aligning the structure size on
 * allocation and de-allocation.
 */

/* This structure gets allocated per element and is used to track
   anything which requires element based scoping.  Needs to be 8 byte
   aligned. */
typedef struct xmlGElement {
  /*@null@*/ /*@partial@*/ /*@owned@*/
  struct xmlGElement *next ;
  const xmlGIStr *localname ;
  const xmlGIStr *prefix ;
  const xmlGIStr *uri ;

  xmlGStartElementCallback f_start ;
  xmlGCharactersCallback f_characters ;
  xmlGEndElementCallback f_end ;

  /* Used for when an end element callback potentially goes
     recursive. */
  HqBool am_within_callback ;

  /* Depth of the filter chain. */
  uint32 filter_chain_depth ;

  /* Depth of the filter. */
  uint32 filter_depth ;
} xmlGElement ;

#define SAC_ALLOC_ELEMENT_SIZE DWORD_ALIGN_UP(sizeof(struct xmlGElement))

/**
 * Medium prime.
 * Would not expect that many child elements.
 */
#define VALID_CHILDREN_HASH_SIZE 41

struct ValidChildEntry {
  HqBool is_being_used ;             /* Is this entry being used? */
  const xmlGIStr *localname ;        /* Key1 */
  const xmlGIStr *uri ;              /* Key2 */
  uint32 num_occurrences ;           /* Number of times we have seen this child */
  uint32 constraint ;                /* Constraint type */
  int32 constraint_arg ;             /* A numeric argument for the constraint */
  uint32 sequence_num ;              /* Sequence number added */
  struct ValidChildEntry *next ;     /* Singly-linked list */
} ;

struct xmlGValidChildTable {
  xmlGFilter *filter ;
  const xmlGIStr *parent_localname ;
  const xmlGIStr *parent_uri ;
  uint32 depth ;
  uint32 num_entries ; /* Number of entries in hash table. Also used
                          to track the order in which valid children
                          are added for in sequence testing. */
  HqBool is_link ;
  HqBool allow_any_child ;

  /*@partial@*/
  struct ValidChildEntry **table ;   /* The hash table */
  struct xmlGValidChildTable *next ; /* Singly linked list. Stack of validity blocks. */
} ;

#define SAC_ALLOC_VALID_CHILD_SIZE DWORD_ALIGN_UP(sizeof(struct xmlGValidChildTable) + \
                                                  (sizeof(struct ValidChildEntry *) * VALID_CHILDREN_HASH_SIZE) + \
                                                  (sizeof(struct ValidChildEntry) * VALID_CHILDREN_HASH_SIZE))

#define SAC_ALLOC_VALID_CHILD_LINK_SIZE DWORD_ALIGN_UP(sizeof(struct xmlGValidChildTable) + \
                                                       (sizeof(struct ValidChildEntry *) * VALID_CHILDREN_HASH_SIZE))

struct xmlGFilter {
  /*@null@*/ /*@partial@*/
  void *user_data ;

  /* The depth of the element being parsed. Starts at 0. */
  uint32 depth ;

  uint32 position ;

  /* Under error conditions, we stop asking the real parser to give us
     line and column information. */
  uint32 prev_line ;
  uint32 prev_column ;

  /* When set, this stops execution and FALSE will be returned from
     the filter chain execution. */
  HqBool error_abort ;

  /* When set, this stops execution and TRUE will be returned from the
     filter chain execution. */
  HqBool success_abort ;

  /* Tracks end element callbacks for this filter. */
  /*@null@*/ /*@partial@*/ /*@owned@*/
  xmlGElement *element_stack ;

  /* Tracks valid children for the current element. */
  /*@null@*/ /*@partial@*/ /*@owned@*/
  xmlGValidChildTable *valid_child_stack ;

  /* User supplied callback functions. */
  xmlGNamespaceCallback namespace_cb ;

  /* Global character callback function. */
  xmlGCharactersCallback character_cb ;

  xmlGXmlDeclarationCallback xml_decl_cb ;

  xmlGXmlDTDStartCallback xml_dtd_start_cb ;

  xmlGValidityErrorCallback validity_error_cb ;

  xmlGUserErrorCallback user_error_cb ;

  xmlGFunctTable *element_callbacks ;

  xmlGFilterCleanupCallback cleanup_cb ;

  struct xmlGContext *xml_ctxt ;

  struct xmlGFilterChain *filter_chain ;

  /* Used to interate over filters on a filter chain quickly. */
  struct xmlGFilter *next ;
  struct xmlGFilter *prev ;
} ;

/* ============================================================================
 * Filter execute functions
 * ============================================================================
 */
extern
int32 xmlg_f_execute_start_element(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *prefix,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri,
      /*@in@*/ /*@null@*/
      xmlGAttributes *attributes) ;

extern
int32 xmlg_f_execute_end_element(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *prefix,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri,
      HqBool success) ;

extern
int32 xmlg_f_execute_characters(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@null@*/
      const uint8 *buf,
      uint32 buflen) ;

extern
int32 xmlg_f_execute_xml_decl(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      const uint8 *version,
      uint32 version_len,
      /*@in@*/ /*@null@*/
      const uint8 *encoding,
      uint32 encoding_len,
      int32 standalone) ;

int32 xmlg_f_execute_start_dtd(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      const uint8 *doctypeName,
      uint32 doctypeName_len,
      /*@in@*/ /*@null@*/
      const uint8 *sysid,
      uint32 sysid_len,
      /*@in@*/ /*@null@*/
      const uint8 *pubid,
      uint32 pubid_len,
      int32 has_internal_subset) ;

extern
int32 xmlg_f_execute_namespace(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *prefix,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri) ;

/**
 * \returns TRUE if there are more undo functions to be called. FALSE
 * when the last undo function on this filter has been called.
 */
extern
HqBool xmlg_f_execute_undo(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      uint32 undo_depth,
      /*@in@*/ /*@notnull@*/
      HqBool *recur_out) ;

extern
void xmlg_f_error_abort(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      HqBool fire_error_handler) ;

/* ============================================================================
* Log stripped */
#endif
