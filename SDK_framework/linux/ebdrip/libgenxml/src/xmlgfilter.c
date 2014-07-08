/* ============================================================================
 * $HopeName: HQNgenericxml!src:xmlgfilter.c(EBDSDK_P.1) $
 * $Id: src:xmlgfilter.c,v 1.32.4.1.1.1 2013/12/19 11:24:21 anon Exp $
 *
 * Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \ingroup libgenxml
 * \brief Implements XML filters.
 *
 * Functions which are NOT prefixed "xmlg_f" are public
 * functions. Functions which are prefixed "xmlg_f" are private to
 * xmlg. Functions which are private to the filter code (i.e. this
 * file) are declared static.
 */

#define XML_SAC_ALLOCATION
#undef PROFILE_XML_FILTER

#ifdef PROFILE_XML_FILTER
#include <stdio.h>
#define XML_TRACE(_out_) printf(_out_)
#else
#define XML_TRACE(_out_)
#endif

#include "xmlg.h"
#include "xmlgpriv.h"
#include "xmlginternpriv.h"
#include "xmlgfilterpriv.h"
#include "xmlgfilterchainpriv.h"

void xmlg_f_error_abort(
      xmlGFilter *filter,
      HqBool fire_error_handler)
{
  uint32 line = 0;
  uint32 column = 0;

  XMLGASSERT(filter != NULL, "filter is NULL") ;

  if (fire_error_handler) {
    /* do this before setting error state. */
    xmlg_line_and_column(filter, &line, &column) ;

    if (filter->user_error_cb != NULL && fire_error_handler && ! filter->success_abort)
      filter->user_error_cb(filter, line, column) ;
  }

  if (! filter->success_abort)
    filter->error_abort = TRUE ;
}

/* Caller responsible for calling the appropriate error handler. */
static HqBool xmlg_element_create(xmlGFilter *filter,
                                  xmlGElement **p_el)
{
  xmlGElement default_el = {0} ;
  xmlGElement *el = NULL ;

  *p_el = NULL ;

#ifdef XML_SAC_ALLOCATION
  {
    mps_res_t res ;
    mps_addr_t m ;
    XMLGASSERT(filter->xml_ctxt->sac != NULL, "xml_ctxt sac is NULL") ;

    MPS_SAC_ALLOC_FAST(res, m, filter->xml_ctxt->sac, SAC_ALLOC_ELEMENT_SIZE, TRUE) ;
    if (res == MPS_RES_OK)
      el = (xmlGElement *)m ;
  }
#else
  el = xmlg_fc_malloc(filter->filter_chain, SAC_ALLOC_ELEMENT_SIZE) ;
#endif

  if ( !el )
    return FALSE ;

  *el = default_el ;
  *p_el = el ;
  return TRUE ;
}

static void xmlg_element_destroy(xmlGFilter *filter,
                                 xmlGElement **p_el)
{
  xmlGElement *el = *p_el ;
  *p_el = NULL ;

  if ( el->localname )
    xmlg_istring_destroy(filter->xml_ctxt, &el->localname) ;
  if ( el->prefix )
    xmlg_istring_destroy(filter->xml_ctxt, &el->prefix) ;
  if ( el->uri )
    xmlg_istring_destroy(filter->xml_ctxt, &el->uri) ;

#ifdef XML_SAC_ALLOCATION
  XMLGASSERT(filter->xml_ctxt->sac != NULL, "xml_ctxt sac is NULL") ;
  MPS_SAC_FREE_FAST(filter->xml_ctxt->sac, el, SAC_ALLOC_ELEMENT_SIZE) ;
#else
  xmlg_fc_free(filter->filter_chain, el) ;
#endif
}

/* ============================================================================
 * Child validity checking functions.
 * ============================================================================
 */

static
/*@null@*/
struct ValidChildEntry *xmlg_find_valid_child_entry(
      struct xmlGValidChildTable *table,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      unsigned int *hval)
{
  struct ValidChildEntry *curr;
  xmlGContext *xml_ctxt;
  xmlGFilter *filter;
#ifdef PROFILE_XML_FILTER
  HqBool collision = FALSE ;
#endif

  XMLGASSERT(table != NULL, "table is NULL");
  XMLGASSERT(localname != NULL, "localname is NULL");
  XMLGASSERT(hval != NULL, "hval is NULL");

  filter = table->filter;
  XMLGASSERT(filter != NULL, "filter is NULL");
  xml_ctxt = filter->xml_ctxt;

  /* Lets just use the hash value of the interned localname. */
  *hval = CAST_UINTPTRT_TO_UINT32( ((uintptr_t)localname >> 2) % VALID_CHILDREN_HASH_SIZE) ;

  for (curr=table->table[*hval]; curr!=NULL; curr=curr->next) {
#ifdef PROFILE_XML_FILTER
    if (collision) {
      XML_TRACE("  valid_child_entry collision\n") ;
    }
    collision = TRUE ;
#endif
    if (curr->is_being_used &&
        xmlg_istring_equal(xml_ctxt, curr->localname, localname) &&
        xmlg_istring_equal(xml_ctxt, curr->uri, uri)) {
      return curr;
    }
  }

  return NULL;
}

static
HqBool xmlg_valid_children_table_create(
      xmlGFilter *filter,
      struct xmlGValidChildTable **table,
      const xmlGIStr *parent_localname,
      const xmlGIStr *parent_uri,
      uint32 parse_depth,
      HqBool is_link)
{
  xmlGContext *xml_ctxt;
  xmlGFilterChain *filter_chain ;
  unsigned int i ;
  uint8 *mem_block ;

  XMLGASSERT(table != NULL, "table pointer is NULL");
  XMLGASSERT(filter != NULL, "filter is NULL") ;
  filter_chain = filter->filter_chain ;
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xml_ctxt = filter_chain->xml_ctxt ;
  XMLGASSERT(xml_ctxt != NULL, "xml_ctxt is NULL") ;

#ifdef XML_SAC_ALLOCATION
  {
    mps_res_t res ;
    mps_addr_t m ;
    XMLGASSERT(xml_ctxt->sac != NULL, "xml_ctxt sac is NULL") ;
    if (is_link) {
      MPS_SAC_ALLOC_FAST(res, m, xml_ctxt->sac, SAC_ALLOC_VALID_CHILD_LINK_SIZE, TRUE) ;
      mem_block = (uint8*)m ;
    } else {
      MPS_SAC_ALLOC_FAST(res, m, xml_ctxt->sac, SAC_ALLOC_VALID_CHILD_SIZE, TRUE) ;
      mem_block = (uint8*)m ;
    }
    if (res != MPS_RES_OK)
      return FALSE ;
  }
#else
  if (is_link) {
    mem_block = xmlg_fc_malloc(filter_chain, SAC_ALLOC_VALID_CHILD_LINK_SIZE) ;
  } else {
    mem_block = xmlg_fc_malloc(filter_chain, SAC_ALLOC_VALID_CHILD_SIZE) ;
  }
#endif

  if (mem_block == NULL)
    return FALSE ;

  *table = (struct xmlGValidChildTable *)mem_block ;
  mem_block += sizeof(struct xmlGValidChildTable) ;
  (*table)->table = (struct ValidChildEntry**)mem_block ;

  /* Initialize the table structure. */
  (*table)->filter = filter ;
  (*table)->parent_localname = parent_localname ;
  (*table)->parent_uri = parent_uri ;
  (*table)->num_entries = 0 ;
  (*table)->depth = parse_depth ;
  (*table)->next = NULL ;
  (*table)->is_link = is_link ;
  (*table)->allow_any_child = FALSE ;

  mem_block += (sizeof(struct ValidChildEntry *) * VALID_CHILDREN_HASH_SIZE) ;

  if (is_link) {
    for (i=0; i<VALID_CHILDREN_HASH_SIZE; i++) {
      (*table)->table[i] = NULL ;
    }
  } else {
    for (i=0; i<VALID_CHILDREN_HASH_SIZE; i++) {
      struct ValidChildEntry *entry = (struct ValidChildEntry *)mem_block ;
      mem_block += sizeof(struct ValidChildEntry) ;
      entry->is_being_used = FALSE ; /* Only need to initialize these values. */
      entry->next = NULL ;
      (*table)->table[i] = entry ;
    }
  }

  return TRUE;
}

static
void xmlg_valid_children_table_destroy(
      struct xmlGValidChildTable **table)
{
  xmlGContext *xml_ctxt;
  xmlGFilterChain *filter_chain;
  xmlGFilter *filter;
  unsigned int i;
  struct ValidChildEntry *curr, *next;

  XMLGASSERT(table != NULL, "table is NULL");
  XMLGASSERT(*table != NULL, "table pointer is NULL");
  filter = (*table)->filter;
  XMLGASSERT(filter != NULL, "filter is NULL") ;
  filter_chain = filter->filter_chain ;
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xml_ctxt = filter_chain->xml_ctxt ;
  XMLGASSERT(xml_ctxt != NULL, "xml_ctxt is NULL") ;

  if ((*table)->is_link) {
#ifdef XML_SAC_ALLOCATION
    XMLGASSERT(xml_ctxt->sac != NULL, "xml_ctxt sac is NULL") ;
    MPS_SAC_FREE_FAST(xml_ctxt->sac, *table, SAC_ALLOC_VALID_CHILD_LINK_SIZE) ;
#else
    xmlg_fc_free(filter_chain, *table);
#endif
  } else {
    for (i=0; i<VALID_CHILDREN_HASH_SIZE; i++) {
      for (curr = (*table)->table[i]; curr != NULL; curr = next) {
        next = curr->next;

        if (curr->is_being_used) {
          XMLGASSERT(curr->localname != NULL, "localname is NULL");
          xmlg_istring_destroy(xml_ctxt, &curr->localname);
          xmlg_istring_destroy(xml_ctxt, &curr->uri);

          if (curr != (*table)->table[i]) { /* Only deallocate non-first entries. */
            xmlg_fc_free(filter_chain, curr);
          }

          (*table)->num_entries--;
        }
      }
    }
    XMLGASSERT((*table)->num_entries == 0, "num_entries is not zero.");

#ifdef XML_SAC_ALLOCATION
    XMLGASSERT(xml_ctxt->sac != NULL, "xml_ctxt sac is NULL") ;
    MPS_SAC_FREE_FAST(xml_ctxt->sac, *table, SAC_ALLOC_VALID_CHILD_LINK_SIZE) ;
#else
    xmlg_fc_free(filter_chain, *table);
#endif
  }

  (*table) = NULL;
  return;
}

static
HqBool xmlg_valid_children_table_register(
      struct xmlGValidChildTable *table,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      int32 constraint_arg,
      uint32 constraint)
{
  xmlGFilterChain *filter_chain;
  xmlGFilter *filter;
  xmlGContext *xml_ctxt;
  struct ValidChildEntry *curr;
  unsigned int hval;

  XMLGASSERT(table != NULL, "table is NULL");
  XMLGASSERT(localname != NULL, "localname is NULL");

  /* Check constraints and the constraint argument. */
  XMLGASSERT(((constraint == XMLG_ZERO_OR_ONE || constraint == XMLG_ZERO_OR_MORE ||
               constraint == XMLG_ZERO || constraint == XMLG_ANY ||
               constraint == XMLG_ONE || constraint == XMLG_ONE_OR_MORE) &&
              constraint_arg == XMLG_NO_GROUP) ||

             (constraint == XMLG_MIN_OCCURS && constraint_arg < 0) ||

             ((constraint == XMLG_GROUP_ONE_OF || constraint == XMLG_GROUP_ZERO_OR_ONE_OF ||
               constraint == XMLG_G_SEQUENCED || constraint == XMLG_G_SEQUENCED_ONE ||
               constraint == XMLG_GROUP_ONE_OR_MORE_OF) &&
              constraint_arg > 0),
             "invalid constraint");

  XMLGASSERT(! table->is_link, "table is a linked table") ;
  filter = table->filter;
  XMLGASSERT(filter != NULL, "filter is NULL");
  xml_ctxt = filter->xml_ctxt;
  filter_chain = filter->filter_chain ;

  if (constraint == XMLG_ANY) {
    HQASSERT(! table->allow_any_child, "allow any child has already been set once") ;
    table->allow_any_child = TRUE;
    return TRUE;
  }

  curr = xmlg_find_valid_child_entry(table, localname, uri, &hval);

  if (curr == NULL) {

    /* Do we need to allocate a new entry in the chain? */
    if (table->table[hval]->is_being_used) {
      if ((curr = xmlg_fc_malloc(filter_chain, sizeof(struct ValidChildEntry))) == NULL)
        return FALSE;

      if (! xmlg_istring_reserve(xml_ctxt, localname) ) {
        xmlg_fc_free(filter_chain, curr) ;
        return FALSE ;
      }

      if (! xmlg_istring_reserve(xml_ctxt, uri) ) {
        xmlg_istring_destroy(xml_ctxt, &localname) ;
        xmlg_fc_free(filter_chain, curr) ;
        return FALSE ;
      }

      /* Copy the values from the table. */
      *curr = *(table->table[hval]) ;

      /* Link back to newly allocated structure. */
      table->table[hval]->next = curr ;
      table->table[hval]->localname = localname ;
      table->table[hval]->uri = uri ;

    } else { /* The first entry is not being used, so we don't need to
                allocate memory. */
      if (! xmlg_istring_reserve(xml_ctxt, localname) ) {
        return FALSE ;
      }
      table->table[hval]->localname = localname;

      if (! xmlg_istring_reserve(xml_ctxt, uri) ) {
        xmlg_istring_destroy(xml_ctxt, &localname) ;
        return FALSE ;
      }
      table->table[hval]->uri = uri;
    }

    XMLGASSERT(constraint_arg < 8 * (int32)sizeof(curr->constraint_arg), "Constraint_Arg too large") ;

    table->table[hval]->is_being_used = TRUE ;
    table->table[hval]->num_occurrences = 0;
    table->table[hval]->constraint_arg = constraint_arg;
    table->table[hval]->constraint = constraint;

    table->num_entries++;
    /* track in sequence information */
    table->table[hval]->sequence_num = table->num_entries ;
  }
  return TRUE;
}

HqBool xmlg_valid_children(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      const XMLG_VALID_CHILDREN valid_child[])
{
  struct xmlGValidChildTable *table;
  uint32 parse_depth;

  XMLGASSERT(filter != NULL, "filter is NULL");

  parse_depth = xmlg_get_element_depth(filter);

  if (filter->valid_child_stack != NULL &&
      filter->valid_child_stack->depth == parse_depth) {
    HQASSERT(FALSE, "valid child check already exists for this parse depth");
    return FALSE;
  }

  if (! xmlg_valid_children_table_create(filter, &table, localname, uri, parse_depth, FALSE))
    return FALSE;

  while (valid_child->localname != NULL) {
    /* Only destroy this table. The stack of tables will be destroyed in
     * latter error recovery.
     */
    if (! xmlg_valid_children_table_register(table, valid_child->localname,
                                             valid_child->uri,
                                             valid_child->constraint_arg,
                                             valid_child->constraint ) ) {
      xmlg_valid_children_table_destroy(&table);
      XMLGASSERT(table == NULL, "table is not NULL");
      return FALSE;
    }

    valid_child++;
  }

  /* Insert at beginning of the stack. */
  table->next = filter->valid_child_stack;
  filter->valid_child_stack = table;

  return TRUE;
}

/* The only way to continue parsing when an error is discovered is to
   have a valid_child_handler which returns TRUE */

/* NOTE: Calling xmlg_istring_value and xmlg_istring_len has at least
   a function call overhead so only call it if need be. This function
   gets called many times during the XML parse process so these
   functions only get called under error conditions. */
static
HqBool valid_child(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs,
      struct xmlGValidChildTable *table)
{
  xmlGContext *xml_ctxt ;
  struct ValidChildEntry *curr;
  unsigned int hval;
  unsigned int i;
  struct ValidChildEntry *find;
  const uint8 *localname_value, *parent_value = (const uint8*)"" ;
  uint32 localname_len, parent_len = 0 ;

  XMLGASSERT(filter != NULL, "filter is NULL");
  XMLGASSERT(localname != NULL, "localname is NULL");
  XMLGASSERT(table != NULL, "table is NULL");

  xml_ctxt = filter->xml_ctxt ;

  /* No children are allowed. This test is done so we can have better
     error reporting */
  if (table->num_entries == 0) {
    localname_value = xmlg_istring_value(xml_ctxt, localname) ;
    localname_len = xmlg_istring_length(xml_ctxt, localname) ;
    /* The table parent_name and parent_uri is NULL when the document
       element is being validated. See special case below which does not
       make use of parent_localname in error string. */
    if (table->parent_localname != NULL) {
      parent_value = xmlg_istring_value(xml_ctxt, table->parent_localname) ;
      parent_len = xmlg_istring_length(xml_ctxt, table->parent_localname) ;
    }

    if (filter->validity_error_cb != NULL && filter->validity_error_cb(filter,
        localname, prefix, uri, attrs, XMLG_ERR_UNKNOWN,
        "<%.*s> element contains subelement <%.*s> where none are allowed.",
        (int32)parent_len, parent_value, (int32)localname_len, localname_value))
      return TRUE;

    xmlg_f_error_abort(filter, TRUE /* fire error cb */) ;
    return FALSE;
  }

  /* If curr is NULL, we are unable to find this element as a valid child. */
  if ((curr = xmlg_find_valid_child_entry(table, localname, uri, &hval)) == NULL) {

    /* If they have specified any child, we are done. */
    if (table->allow_any_child)
      return TRUE;

    /* Special case which does not make use of parent_localname. */
    if (xmlg_get_element_depth(filter) == 0) { /* Its the document element which is incorrect. */
      localname_value = xmlg_istring_value(xml_ctxt, localname) ;
      localname_len = xmlg_istring_length(xml_ctxt, localname) ;

      if (filter->validity_error_cb != NULL && filter->validity_error_cb(filter,
          localname, prefix, uri, attrs, XMLG_ERR_INVALID_DOC_ELEMENT,
          "<%.*s> is an invalid document element.",
          (int32)localname_len, localname_value))
        return TRUE;
    } else {
      localname_value = xmlg_istring_value(xml_ctxt, localname) ;
      localname_len = xmlg_istring_length(xml_ctxt, localname) ;
      /* The table parent_name and parent_uri is NULL when the document
         element is being validated. See special case below which does not
         make use of parent_localname in error string. */
      if (table->parent_localname != NULL) {
        parent_value = xmlg_istring_value(xml_ctxt, table->parent_localname) ;
        parent_len = xmlg_istring_length(xml_ctxt, table->parent_localname) ;
      }

      if (filter->validity_error_cb != NULL && filter->validity_error_cb(filter,
          localname, prefix, uri, attrs, XMLG_ERR_INVALID_CHILD,
          "<%.*s> contains invalid subelement <%.*s>.",
          (int32)parent_len, parent_value, (int32)localname_len, localname_value))
        return TRUE;
    }

    xmlg_f_error_abort(filter, TRUE /* fire error cb */) ;
    return FALSE;
  }

  curr->num_occurrences++;

  /* If we are seeing the element - the count must be at least one */
  switch (curr->constraint) {
  case XMLG_ZERO:
    localname_value = xmlg_istring_value(xml_ctxt, localname) ;
    localname_len = xmlg_istring_length(xml_ctxt, localname) ;
    /* The table parent_name and parent_uri is NULL when the document
       element is being validated. See special case below which does not
       make use of parent_localname in error string. */
    if (table->parent_localname != NULL) {
      parent_value = xmlg_istring_value(xml_ctxt, table->parent_localname) ;
      parent_len = xmlg_istring_length(xml_ctxt, table->parent_localname) ;
    }

    if (filter->validity_error_cb != NULL && filter->validity_error_cb(filter,
        localname, prefix, uri, attrs, XMLG_ERR_UNKNOWN,
        "<%.*s> contains invalid subelement <%.*s>.",
        (int32)parent_len, parent_value, (int32)localname_len, localname_value))
      return TRUE;

    xmlg_f_error_abort(filter, TRUE /* fire error cb */) ;
    return FALSE;
  case XMLG_ONE:
  case XMLG_ZERO_OR_ONE:
    if (curr->num_occurrences == 1) {
      return TRUE;
    } else {
      localname_value = xmlg_istring_value(xml_ctxt, localname) ;
      localname_len = xmlg_istring_length(xml_ctxt, localname) ;
      /* The table parent_name and parent_uri is NULL when the document
         element is being validated. See special case below which does not
         make use of parent_localname in error string. */
      if (table->parent_localname != NULL) {
        parent_value = xmlg_istring_value(xml_ctxt, table->parent_localname) ;
        parent_len = xmlg_istring_length(xml_ctxt, table->parent_localname) ;
      }

      if (filter->validity_error_cb != NULL && filter->validity_error_cb(filter,
          localname, prefix, uri, attrs, XMLG_ERR_UNKNOWN,
          "<%.*s> contains more than one instance of subelement <%.*s>.",
          (int32)parent_len, parent_value, (int32)localname_len, localname_value))
        return TRUE;

      xmlg_f_error_abort(filter, TRUE /* fire error cb */) ;
      return FALSE;
    }
  case XMLG_MIN_OCCURS:
    XMLGASSERT(curr->constraint_arg < 0, "Min occurs not less than zero.") ;
    return TRUE ;
  case XMLG_ONE_OR_MORE:
  case XMLG_ZERO_OR_MORE:
    return TRUE ;
  case XMLG_GROUP_ONE_OR_MORE_OF:
    XMLGASSERT(curr->constraint_arg > 0, "Constraint_Arg not greater than zero.") ;
    /* We must have at least one element (this one). We check all ONE and
       ONE_OR_MORE instances just before we call the end (or commit?) element
       callback. */
    XMLGASSERT(curr->num_occurrences >= 1, "This should never be possible.");
    return TRUE;
  case XMLG_GROUP_ZERO_OR_ONE_OF:
  case XMLG_GROUP_ONE_OF:
    XMLGASSERT(curr->constraint_arg > 0, "Constraint_Arg not greater than zero.") ;
    if (curr->num_occurrences != 1) {
      localname_value = xmlg_istring_value(xml_ctxt, localname) ;
      localname_len = xmlg_istring_length(xml_ctxt, localname) ;
      /* The table parent_name and parent_uri is NULL when the document
         element is being validated. See special case below which does not
         make use of parent_localname in error string. */
      if (table->parent_localname != NULL) {
        parent_value = xmlg_istring_value(xml_ctxt, table->parent_localname) ;
        parent_len = xmlg_istring_length(xml_ctxt, table->parent_localname) ;
      }

      if (filter->validity_error_cb != NULL && filter->validity_error_cb(filter,
          localname, prefix, uri, attrs, XMLG_ERR_UNKNOWN,
          "<%.*s> contains more than one instance of a grouped subelement for which <%.*s> is a member.",
          (int32)parent_len, parent_value, (int32)localname_len, localname_value))
        return TRUE;

      xmlg_f_error_abort(filter, TRUE /* fire error cb */) ;
      return FALSE;
    }

    /* Check that all other elements in the group have not been seen.
       We check that at least one has been seen just before the end
       element callback. */
    /* TBD: Keep a group list so that we do not need to search the
       entire hash table. */
    for (i=0; i<VALID_CHILDREN_HASH_SIZE; i++) {
      for (find = table->table[i]; find != NULL && find->is_being_used; find = find->next) {
        /* If its not me and the constraint is the same and its in my group */
        if (find != curr &&
            find->constraint == curr->constraint &&
            find->constraint_arg == curr->constraint_arg ) {

          /* If one of the other elements has been seen. */
          if (find->num_occurrences != 0) {
            localname_value = xmlg_istring_value(xml_ctxt, localname) ;
            localname_len = xmlg_istring_length(xml_ctxt, localname) ;
            /* The table parent_name and parent_uri is NULL when the document
               element is being validated. See special case below which does not
               make use of parent_localname in error string. */
            if (table->parent_localname != NULL) {
              parent_value = xmlg_istring_value(xml_ctxt, table->parent_localname) ;
              parent_len = xmlg_istring_length(xml_ctxt, table->parent_localname) ;
            }

            if (filter->validity_error_cb != NULL && filter->validity_error_cb(filter,
                localname, prefix, uri, attrs, XMLG_ERR_UNKNOWN,
                "<%.*s> contains more than one instance of a grouped subelement for which <%.*s> is a member.",
                (int32)parent_len, parent_value, (int32)localname_len, localname_value))
              return TRUE;

            xmlg_f_error_abort(filter, TRUE /* fire error cb */) ;
            return FALSE;
          }
        }
      }
    }
    return TRUE;

  case XMLG_G_SEQUENCED:
  case XMLG_G_SEQUENCED_ONE:
    if (curr->num_occurrences != 1) {
      localname_value = xmlg_istring_value(xml_ctxt, localname) ;
      localname_len = xmlg_istring_length(xml_ctxt, localname) ;
      /* The table parent_name and parent_uri is NULL when the document
         element is being validated. See special case below which does not
         make use of parent_localname in error string. */
      if (table->parent_localname != NULL) {
        parent_value = xmlg_istring_value(xml_ctxt, table->parent_localname) ;
        parent_len = xmlg_istring_length(xml_ctxt, table->parent_localname) ;
      }

      if (filter->validity_error_cb != NULL && filter->validity_error_cb(filter,
          localname, prefix, uri, attrs, XMLG_ERR_UNKNOWN,
          "<%.*s> contains more than one instance of a grouped subelement for which <%.*s> is a member.",
          (int32)parent_len, parent_value, (int32)localname_len, localname_value))
        return TRUE;

      xmlg_f_error_abort(filter, TRUE /* fire error cb */) ;
      return FALSE;
    }

    /* Check that all other elements in the group which have been seen
       have a sequence number which is less than the element we have
       just seen. */
    /* TBD: Keep a group list so that we do not need to search the
       entire hash table. */
    for (i=0; i<VALID_CHILDREN_HASH_SIZE; i++) {
      for (find = table->table[i]; find != NULL && find->is_being_used ; find = find->next) {
        /* If its not me, the constraint is the same, its in my group
           and we have seen an instance */
        if (find != curr &&
            find->constraint == curr->constraint &&
            find->constraint_arg == curr->constraint_arg &&
            find->num_occurrences > 0) {

          /* If one of the other elements has been seen, its sequence
             number MUST be less than the one we have just seen. */
          if (find->sequence_num > curr->sequence_num) {
            localname_value = xmlg_istring_value(xml_ctxt, localname) ;
            localname_len = xmlg_istring_length(xml_ctxt, localname) ;
            /* The table parent_name and parent_uri is NULL when the document
               element is being validated. See special case below which does not
               make use of parent_localname in error string. */
            if (table->parent_localname != NULL) {
              parent_value = xmlg_istring_value(xml_ctxt, table->parent_localname) ;
              parent_len = xmlg_istring_length(xml_ctxt, table->parent_localname) ;
            }

            if (filter->validity_error_cb != NULL && filter->validity_error_cb(filter,
                localname, prefix, uri, attrs, XMLG_ERR_UNKNOWN,
                "<%.*s> contains the sequenced subelement <%.*s> appearing before <%.*s>.",
                (int32)parent_len, parent_value,
                (int32)xmlg_istring_length(xml_ctxt, find->localname),
                xmlg_istring_value(xml_ctxt, find->localname),
                (int32)localname_len, localname_value))
              return TRUE;

            xmlg_f_error_abort(filter, TRUE /* fire error cb */) ;
            return FALSE;
          }
        }
      }
    }
    return TRUE ;
  } /* end switch */

  XMLGASSERT(FALSE, "Should never get here.");
  return FALSE;
}

/* NOTE: Calling xmlg_istring_value and xmlg_istring_len has at least
   a function call overhead so only call it if need be. This function
   gets called many times during the XML parse process so these
   functions only get called under error conditions. */
static
HqBool child_constraints_ok(
      xmlGFilter *filter,
      struct xmlGValidChildTable *table)
{
  xmlGContext *xml_ctxt ;
  unsigned int i;
  struct ValidChildEntry *find;
  uint32 instances_found = 0u, groups_needed = 0u ;
  const uint8 *localname_value, *parent_value ;
  uint32 localname_len, parent_len ;

  XMLGASSERT(filter != NULL, "filter is NULL");
  XMLGASSERT(table != NULL, "table is NULL");

  xml_ctxt = filter->xml_ctxt;

  /* Child constraints never happen on the document element. */
  HQASSERT(table->parent_localname != NULL, "parent_localname is NULL") ;

  /* We will have picked up the no child error case in valid_child */
  if (table->num_entries == 0)
    return TRUE;

  /* Not clear if keeping this hash small is any worse performance wise
     than keeping a list along side the hash table. */
  for (i=0; i<VALID_CHILDREN_HASH_SIZE; i++) {
    for (find = table->table[i]; find != NULL && find->is_being_used; find = find->next) {
      switch (find->constraint) {
      case XMLG_ZERO:
        if (find->num_occurrences > 0) {
          localname_value = xmlg_istring_value(xml_ctxt, find->localname) ;
          localname_len = xmlg_istring_length(xml_ctxt, find->localname) ;
          parent_value = xmlg_istring_value(xml_ctxt, table->parent_localname) ;
          parent_len = xmlg_istring_length(xml_ctxt, table->parent_localname) ;

          XMLGASSERT(FALSE, "finding more than one should be caught in valid_child");
          if (filter->validity_error_cb != NULL && filter->validity_error_cb(filter,
              find->localname, NULL /* We do not track this. */, find->uri, NULL, XMLG_ERR_UNKNOWN,
              "<%.*s> contains an instance of subelement <%.*s>.",
              (int32)parent_len, parent_value, (int32)localname_len, localname_value))
            return TRUE;

          /* only fire error callback if validity callback fails. */
          xmlg_f_error_abort(filter, TRUE /* fire error cb */) ;
          return FALSE;
        }
        break;
      case XMLG_ONE:
      case XMLG_G_SEQUENCED_ONE:
        if (find->num_occurrences < 1) {
          localname_value = xmlg_istring_value(xml_ctxt, find->localname) ;
          localname_len = xmlg_istring_length(xml_ctxt, find->localname) ;
          parent_value = xmlg_istring_value(xml_ctxt, table->parent_localname) ;
          parent_len = xmlg_istring_length(xml_ctxt, table->parent_localname) ;

          if (filter->validity_error_cb != NULL && filter->validity_error_cb(filter,
              find->localname, NULL /* We do not track this. */, find->uri, NULL, XMLG_ERR_UNKNOWN,
              "<%.*s> requires a single instance of subelement <%.*s>.",
              (int32)parent_len, parent_value, (int32)localname_len, localname_value))
            return TRUE;

          xmlg_f_error_abort(filter, TRUE /* fire error cb */) ;
          return FALSE;
        }
        if (find->num_occurrences > 1) {
          localname_value = xmlg_istring_value(xml_ctxt, find->localname) ;
          localname_len = xmlg_istring_length(xml_ctxt, find->localname) ;
          parent_value = xmlg_istring_value(xml_ctxt, table->parent_localname) ;
          parent_len = xmlg_istring_length(xml_ctxt, table->parent_localname) ;

          XMLGASSERT(FALSE, "finding more than one should be caught in valid_child");
          if (filter->validity_error_cb != NULL && filter->validity_error_cb(filter,
              find->localname, NULL /* We do not track this. */, find->uri, NULL, XMLG_ERR_UNKNOWN,
              "<%.*s> contains more than one instance of subelement <%.*s>.",
              (int32)parent_len, parent_value, (int32)localname_len, localname_value))
            return TRUE;

          /* only fire error callback if validity callback fails. */
          xmlg_f_error_abort(filter, TRUE /* fire error cb */) ;
          return FALSE;
        }
        break;
      case XMLG_ZERO_OR_ONE:
        if (find->num_occurrences > 1) {
          localname_value = xmlg_istring_value(xml_ctxt, find->localname) ;
          localname_len = xmlg_istring_length(xml_ctxt, find->localname) ;
          parent_value = xmlg_istring_value(xml_ctxt, table->parent_localname) ;
          parent_len = xmlg_istring_length(xml_ctxt, table->parent_localname) ;

          if (filter->validity_error_cb != NULL && filter->validity_error_cb(filter,
              find->localname, NULL /* We do not track this. */, find->uri, NULL, XMLG_ERR_UNKNOWN,
              "<%.*s> contains more than one instance of subelement <%.*s>.",
              (int32)parent_len, parent_value, (int32)localname_len, localname_value))
            return TRUE;

          xmlg_f_error_abort(filter, TRUE /* fire error cb */) ;
          return FALSE;
        }
        break;
      case XMLG_ONE_OR_MORE:
        if (find->num_occurrences < 1) {
          localname_value = xmlg_istring_value(xml_ctxt, find->localname) ;
          localname_len = xmlg_istring_length(xml_ctxt, find->localname) ;
          parent_value = xmlg_istring_value(xml_ctxt, table->parent_localname) ;
          parent_len = xmlg_istring_length(xml_ctxt, table->parent_localname) ;

          if (filter->validity_error_cb != NULL && filter->validity_error_cb(filter,
              find->localname, NULL /* We do not track this. */, find->uri, NULL, XMLG_ERR_UNKNOWN,
              "<%.*s> requires at least one subelement <%.*s>.",
              (int32)parent_len, parent_value, (int32)localname_len, localname_value))
            return TRUE;

          xmlg_f_error_abort(filter, TRUE /* fire error cb */) ;
          return FALSE;
        }
        break;
      case XMLG_ANY:
      case XMLG_ZERO_OR_MORE:
      case XMLG_GROUP_ZERO_OR_ONE_OF:
      case XMLG_G_SEQUENCED:
        break;
      case XMLG_GROUP_ONE_OF:
        XMLGASSERT(find->constraint_arg >= 0 && find->constraint_arg < sizeof(uint32) * 8,
                   "Constraint_Arg out of range") ;
        groups_needed |= (1u << find->constraint_arg) ;
        XMLGASSERT(find->num_occurrences <= 1, "finding more than one should be caught in valid_child");
        if (find->num_occurrences > 0) {
          if ( (instances_found & (1u << find->constraint_arg)) != 0 ) {
            localname_value = xmlg_istring_value(xml_ctxt, find->localname) ;
            localname_len = xmlg_istring_length(xml_ctxt, find->localname) ;
            parent_value = xmlg_istring_value(xml_ctxt, table->parent_localname) ;
            parent_len = xmlg_istring_length(xml_ctxt, table->parent_localname) ;

            if (filter->validity_error_cb != NULL && filter->validity_error_cb(filter,
                find->localname, NULL /* We do not track this. */, find->uri, NULL, XMLG_ERR_UNKNOWN,
                "<%.*s> contains more than one instance of a grouped subelement for which <%.*s> is a member.",
                (int32)parent_len, parent_value, (int32)localname_len, localname_value))
              return TRUE;

            xmlg_f_error_abort(filter, TRUE /* fire error cb */) ;
            return FALSE;
          }
          instances_found |= (1u << find->constraint_arg);
        }
        break;
      case XMLG_GROUP_ONE_OR_MORE_OF:
        XMLGASSERT(find->constraint_arg >= 0 && find->constraint_arg < sizeof(uint32) * 8,
                   "Group out of range") ;
        groups_needed |= (1u << find->constraint_arg) ;
        if (find->num_occurrences > 0) {
          instances_found |= (1u << find->constraint_arg);
        }
        break;
     case XMLG_MIN_OCCURS:
        localname_value = xmlg_istring_value(xml_ctxt, find->localname) ;
        localname_len = xmlg_istring_length(xml_ctxt, find->localname) ;
        parent_value = xmlg_istring_value(xml_ctxt, table->parent_localname) ;
        parent_len = xmlg_istring_length(xml_ctxt, table->parent_localname) ;

        XMLGASSERT(find->constraint_arg < 0, "Min occurs not less than zero.") ;
        if (find->num_occurrences < (uint32)(- find->constraint_arg)) {
          if (filter->validity_error_cb != NULL && filter->validity_error_cb(filter,
              find->localname, NULL /* We do not track this. */, find->uri, NULL, XMLG_ERR_UNKNOWN,
              "<%.*s> requires at least %d <%.*s> subelements.",
              (int32)parent_len, parent_value, -(find->constraint_arg), (int32)localname_len, localname_value))
            return TRUE;

          xmlg_f_error_abort(filter, TRUE /* fire error cb */) ;
          return FALSE;
        }
       break ;
      default:
        XMLGASSERT(FALSE, "Default should not happen");
      }
    }
  }

#if 1
  /** \todo @@@ TODO FIXME ajcd 2005-03-21: comment this out if
      AlternateContent handling interferes with validation. */
  if ( (groups_needed & instances_found) != groups_needed ) {
    parent_value = xmlg_istring_value(xml_ctxt, table->parent_localname) ;
    parent_len = xmlg_istring_length(xml_ctxt, table->parent_localname) ;

    if (filter->validity_error_cb != NULL && filter->validity_error_cb(filter,
        NULL, NULL /* We do not track this. */, NULL, NULL, XMLG_ERR_UNKNOWN,
        "<%.*s> missing subelement from a required subelement group.",
        (int32)parent_len, parent_value))
      return TRUE;

    xmlg_f_error_abort(filter, TRUE /* fire error cb */) ;
    return FALSE;
  }
#endif

  return TRUE;
}

HqBool xmlg_valid_children_link(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      xmlGValidChildTable *from_table,
      uint32 new_depth)
{
  struct xmlGValidChildTable *to_table ;
  uint32 i ;

  if (from_table == NULL)
    return TRUE ;

  if (! xmlg_valid_children_table_create(filter, &to_table, localname, uri, new_depth, TRUE))
    return FALSE ;

  to_table->is_link = TRUE ;

  /* Yes, I DO want to point to the same entries. Its really a link rather
     than a copy. This way, we keep checking the same valid child structure */
  for (i=0; i<VALID_CHILDREN_HASH_SIZE; i++) {
    to_table->table[i] = from_table->table[i] ;
  }

  to_table->num_entries = from_table->num_entries ;
  /* Place on top of stack */
  to_table->next = filter->valid_child_stack ;
  filter->valid_child_stack = to_table ;

  return TRUE ;
}

uint32 xmlg_valid_children_depth(
      xmlGValidChildTable *table)
{
  XMLGASSERT(table != NULL, "table is NULL") ;
  return table->depth ;
}

xmlGValidChildTable* xmlg_valid_children_get_top(
      xmlGFilter *filter)
{
  return filter->valid_child_stack ;
}

/* ============================================================================
 * Filter execute functions
 * ============================================================================
 */

int32 xmlg_f_execute_start_element(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlGContext *xml_ctxt ;
  xmlGFilterChain *filter_chain;
  int32 f_result = TRUE ;
  HqBool success = TRUE ;
  struct xmlGValidChildTable *table ;
  xmlGElement *el ;

  XMLGASSERT(filter != NULL, "filter is NULL") ;
  xml_ctxt = filter->xml_ctxt ;
  filter_chain = filter->filter_chain ;
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL") ;

  /* We have some valid children to check */
  if (filter->valid_child_stack != NULL) {
    /* If we are checking valid children, the child check block will
       always be at the top of the stack. */
    if (filter->depth == filter->valid_child_stack->depth) {
      if (! valid_child(filter, localname, prefix, uri, attrs, filter->valid_child_stack)) {
        /* valid_child is responsible for aborting the parse */

        /* Remove valid child table from stack. */
        table = filter->valid_child_stack ;
        filter->valid_child_stack = table->next ;
        xmlg_valid_children_table_destroy(&table) ;

        return FALSE ;
      }
    } else {
      XMLGASSERT(filter->depth > filter->valid_child_stack->depth,
                 "element depth is not greater than top valid child table") ;
    }
  }

  filter->depth++ ;

  /* Push element block onto stack immediately. We do this first
     because it is possible that one of the callbacks we are about
     to call will execute additional XML start elements - hence we
     need to make sure we are appropriately nested. */
  {
    if ( !xmlg_element_create(filter, &el) ) {
      /* do not fire error cb - filter chain will do it */
      xmlg_f_error_abort(filter, FALSE) ;
      return FALSE ;
    }

    /* If there is no error, lookup and cache all functions associated with this element. */
    if (! filter->success_abort && ! filter->error_abort) {
      (void)xmlg_funct_table_degrade_lookup(filter->element_callbacks,
                                            localname, uri,
                                            &(el->f_start),
                                            &(el->f_characters),
                                            &(el->f_end)) ;
    }

    el->localname = localname ;
    el->uri = uri ;
    el->prefix = prefix ;

    /* Reserve interned strings. */
    if (! xmlg_istring_reserve(xml_ctxt, localname) ||
        ! xmlg_istring_reserve(xml_ctxt, uri) ||
        ! xmlg_istring_reserve(xml_ctxt, prefix)) {
      el->localname = NULL ;
      el->uri = NULL ;
      el->prefix = NULL ;
      success = FALSE ;
    }

    /* The depth of the filter chain is likely to be different from
       the depth of the filter. */
    el->filter_chain_depth = filter_chain->depth ;
    el->filter_depth = filter->depth ;
    el->am_within_callback = FALSE ;

    /* Insert at top. */
    el->next = filter->element_stack ;
    filter->element_stack = el ;
  }

  if (el->f_start != NULL && !filter->success_abort && !filter->error_abort && success) {
    /* if the start goes recursive on a filter chain execute, don't
       execute undo beyond this marker, let the recursive unwind take
       care of it */
    filter->element_stack->am_within_callback = TRUE ;
    f_result = el->f_start(filter, localname, prefix, uri, attrs) ;
    filter->element_stack->am_within_callback = FALSE ;
  } else {
    if (! success)
      f_result = FALSE ;
  }

  /* NOTE: that f_result has multiple states */
  success = f_result > 0 ? TRUE : FALSE ;

  if ( success ) {
    /* At this point, the real callback has succeeded, so we MUST call
       the end callback if any further problems occur. This will be
       done by the element stack undo information. */
  } else {
    /* do NOT fire error cb - filter chain will do it once per
       filter */
    xmlg_f_error_abort(filter, FALSE) ;

    /* If an error occured, then pop the element from the stack as we
       don't want to call its end function. */
    if ( filter->success_abort || filter->error_abort) {
      if (filter->element_stack != NULL) {
        xmlGElement *el = filter->element_stack ;
        XMLGASSERT(el != NULL, "el is NULL") ;

        filter->element_stack = el->next ;

        xmlg_element_destroy(filter, &el) ;
      }
    }

    /* Remove valid child table from stack. */
    if (filter->valid_child_stack != NULL &&
        filter->depth == filter->valid_child_stack->depth) {
      table = filter->valid_child_stack ;
      filter->valid_child_stack = table->next ;
      xmlg_valid_children_table_destroy(&table) ;
    }

    filter->depth--;
  }

  return f_result ;
}

int32 xmlg_f_execute_end_element(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      HqBool success)
{
  xmlGContext *xml_ctxt ;
  xmlGFilterChain *filter_chain ;
  HqBool oldsuccess = success ;
  xmlGElement *el ;
  int32 f_result = TRUE ;
  struct xmlGValidChildTable *table = NULL ;

  XMLGASSERT(filter != NULL, "filter is NULL") ;
  XMLGASSERT(localname != NULL, "localname is NULL") ;

  filter_chain = filter->filter_chain ;
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xml_ctxt = filter_chain->xml_ctxt ;
  XMLGASSERT(xml_ctxt != NULL, "xml_ctxt is NULL") ;

  el = filter->element_stack ;

  XMLGASSERT(el != NULL, "el is NULL") ;
  XMLGASSERT(filter->depth <= el->filter_chain_depth,
             "filter depth is greater than element depth") ;

  /* Remove the valid child structure in case the end element goes
     recursive. */

  /* We have some valid children to check */
  if (filter->valid_child_stack != NULL) {
    /* If we are checking valid children, the child check block will
       always be at the top of the stack. */
    if (filter->depth == filter->valid_child_stack->depth) {

      /* Remove valid child table from stack. */
      table = filter->valid_child_stack ;
      filter->valid_child_stack = table->next;
    } else {
      XMLGASSERT(filter->depth > filter->valid_child_stack->depth,
                 "parse depth is not greater than top valid child table");
    }
  }

  if (el->f_end != NULL) {
    if (success ? (! filter->success_abort && ! filter->error_abort) : TRUE) {
      HQASSERT(filter->element_stack != NULL, "element_stack is NULL") ;
      /* if the end goes recursive on a filter chain execute, don't
         execute undo beyond this marker, let the recursive unwind
         take care of it */
      filter->element_stack->am_within_callback = TRUE ;
      f_result = el->f_end(filter, localname, prefix, uri, success) ;
      filter->element_stack->am_within_callback = FALSE ;
    }

    /* NOTE: that f_result has multiple states */
    success = f_result > 0 ? TRUE : FALSE ;
  }

  if (success != oldsuccess) {
    HQASSERT(oldsuccess,
             "Should not return success from end callbacks when told you've failed") ;
    /* do not fire error cb - filter chain will do it */
    xmlg_f_error_abort(filter, FALSE) ;
  }

  /* Pop this end function from the stack as we don't want to call it
   * twice.  Duplicate code - ought to be merged with code in start
   * callback.
   */
  {
    filter->element_stack = el->next ;

    xmlg_element_destroy(filter, &el) ;
  }

  /* We have some valid children to check */
  if (table != NULL) {
    /* Do not check constraints until we are seeing the real end element
       callback, not some promoted valid child structure. */
    if (! table->is_link) {
      /* Only check children if an error has not already occured */
      if (success) {
        /* Check that ONE, ONE_OR_MORE and group constraints have been met. */
        success = child_constraints_ok(filter, table);

        /* We do NOT invoke the error callback when checking child
           constraints - that is done in that function. */
        if (! success) {
          /* do not fire error cb - filter chain will do it */
          xmlg_f_error_abort(filter, FALSE) ;
          f_result = FALSE ;
        }
      }
    }
    xmlg_valid_children_table_destroy(&table);
  }

  if (filter->depth != 0)
    filter->depth--;

  return f_result ;
}

int32 xmlg_f_execute_characters(
      xmlGFilter *filter,
      const uint8* codeunits,
      uint32 unitlength)
{
  HqBool success = TRUE ;
  int32 f_result = TRUE ;
  xmlGElement *el ;

  XMLGASSERT(filter != NULL, "filter is NULL") ;
  el = filter->element_stack;

  /* We have a character callback before we have seen our first
     element. */
  if (el == NULL)
    return f_result ;

  XMLGASSERT(filter->depth == el->filter_depth,
             "element depth does not match execute depth");

  if (el->f_characters != NULL && ! filter->success_abort && ! filter->error_abort && f_result)
    f_result = el->f_characters(filter, codeunits, unitlength) ;

  /* NOTE: that f_result has multiple states */
  success = f_result > 0 ? TRUE : FALSE ;

  if (! success) {
    /* do not fire error cb - filter chain will do it */
    xmlg_f_error_abort(filter, FALSE) ;

  } else { /* Now execute global characters callback if it exists. */
    if (filter->character_cb != NULL && ! filter->success_abort && ! filter->error_abort && f_result)
      f_result = filter->character_cb(filter, codeunits, unitlength) ;

    /* NOTE: that f_result has multiple states */
    success = f_result > 0 ? TRUE : FALSE ;

    if (! success)
      /* do not fire error cb - filter chain will do it */
      xmlg_f_error_abort(filter, FALSE) ;
  }

  return f_result ;
}

int32 xmlg_f_execute_namespace(
      xmlGFilter *filter,
      const xmlGIStr *prefix,
      const xmlGIStr *uri)
{
  int32 f_result ;
  HqBool success ;
  xmlGNamespaceCallback f ;

  XMLGASSERT(filter != NULL, "filter is NULL") ;

  f = filter->namespace_cb ;

  f_result = TRUE;

  if (! filter->success_abort && !filter->error_abort && f != NULL) {
    f_result = f(filter, prefix, uri) ;

    /* NOTE: that f_result has multiple states */
    success = f_result > 0 ? TRUE : FALSE ;

    if (! success) { /* abort the parse */
      /* do not fire error cb - filter chain will do it */
      xmlg_f_error_abort(filter, FALSE) ;
    }
  }

  return f_result ;
}

int32 xmlg_f_execute_xml_decl(
      xmlGFilter *filter,
      const uint8 *version,
      uint32 version_len,
      const uint8 *encoding,
      uint32 encoding_len,
      int32 standalone)
{
  int32 f_result ;
  HqBool success ;
  xmlGXmlDeclarationCallback f ;

  XMLGASSERT(filter != NULL, "filter is NULL") ;

  f = filter->xml_decl_cb ;

  f_result = TRUE;

  if (! filter->success_abort && !filter->error_abort && f != NULL) {
    f_result = f(filter, version, version_len, encoding, encoding_len, standalone) ;

    /* NOTE: that f_result has multiple states */
    success = f_result > 0 ? TRUE : FALSE ;

    if (! success) { /* abort the parse */
      /* do not fire error cb - filter chain will do it */
      xmlg_f_error_abort(filter, FALSE) ;
    }
  }

  return f_result ;
}

int32 xmlg_f_execute_start_dtd(
      xmlGFilter *filter,
      const uint8 *doctypeName,
      uint32 doctypeName_len,
      const uint8 *sysid,
      uint32 sysid_len,
      const uint8 *pubid,
      uint32 pubid_len,
      int32 has_internal_subset)
{
  int32 f_result ;
  HqBool success ;
  xmlGXmlDTDStartCallback f ;

  XMLGASSERT(filter != NULL, "filter is NULL") ;

  f = filter->xml_dtd_start_cb ;

  f_result = TRUE;

  if (! filter->success_abort && !filter->error_abort && f != NULL) {
    f_result = f(filter, doctypeName, doctypeName_len, sysid, sysid_len, pubid,
                 pubid_len, has_internal_subset) ;

    /* NOTE: that f_result has multiple states */
    success = f_result > 0 ? TRUE : FALSE ;

    if (! success) { /* abort the parse */
      /* do not fire error cb - filter chain will do it */
      xmlg_f_error_abort(filter, FALSE) ;
    }
  }

  return f_result ;
}

HqBool xmlg_f_execute_undo(
      xmlGFilter *filter,
      uint32 undo_depth,
      HqBool *recur_out)
{
  xmlGElement *el ;

  XMLGASSERT(filter != NULL, "filter is NULL") ;
  XMLGASSERT(recur_out != NULL, "recur_out is NULL") ;

  el = filter->element_stack ;
  *recur_out = FALSE ;

  if (el != NULL) {
    XMLGASSERT(el->filter_chain_depth <= undo_depth,
               "undo_depth less than top element") ;

    /* skip this end element, will be picked up later */
    if (el->filter_chain_depth < undo_depth) {
      return TRUE ;
    } else if (el->filter_chain_depth == undo_depth && el->am_within_callback) {
      *recur_out = TRUE ;
      return TRUE ;
    }

    /* NOTE: xmlg_f_execute_end_element pops the undo stack. */
    xmlg_f_execute_end_element(filter, el->localname, el->prefix,
                               el->uri, FALSE) ;

    /* Peek ahead, we may have just executed the last undo callback. */
    if (filter->element_stack == NULL)
      return FALSE ;
  } else {
    return FALSE ;
  }

  return TRUE ;
}

/* ============================================================================
 * Filter functions
 * ============================================================================
 */

hqn_uri_t *xmlg_get_base_uri(
      xmlGFilter *filter)
{
  xmlGFilterChain *filter_chain ;
  XMLGASSERT(filter != NULL, "filter is NULL") ;

  filter_chain = filter->filter_chain ;
  return xmlg_fc_get_base_uri(filter_chain) ;
}

hqn_uri_t* xmlg_get_uri(
      xmlGFilter *filter)
{
  xmlGFilterChain *filter_chain ;
  XMLGASSERT(filter != NULL, "filter is NULL") ;

  filter_chain = filter->filter_chain ;
  return xmlg_fc_get_uri(filter_chain) ;
}

void* xmlg_get_user_data(
      xmlGFilter *filter)
{
  XMLGASSERT(filter != NULL, "filter is NULL") ;
  return filter->user_data ;
}

int32 xmlg_get_fc_id(
      xmlGFilter *filter)
{
  xmlGFilterChain *filter_chain ;
  XMLGASSERT(filter != NULL, "filter is NULL") ;
  filter_chain = filter->filter_chain ;
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL") ;

  return filter_chain->id ;
}

xmlGFilterChain*  xmlg_get_fc(
      xmlGFilter *filter)
{
  XMLGASSERT(filter != NULL, "filter is NULL") ;
  return filter->filter_chain ;
}

HqBool xmlg_line_and_column(
      xmlGFilter *filter,
      uint32 *line,
      uint32 *column)
{
  xmlGFilterChain *filter_chain ;
  xmlGParser *xml_parser ;

  XMLGASSERT(filter != NULL, "filter is NULL") ;
  XMLGASSERT(line != NULL, "line is NULL") ;
  XMLGASSERT(column != NULL, "column is NULL") ;

  /* This is a hack, but I don't see another way for filters to get
     hold of line and column. */
  filter_chain = filter->filter_chain ;

  if (filter_chain->xml_parser != NULL) {
    xml_parser = filter_chain->xml_parser ;
    xmlg_parser_line_and_column(xml_parser, line, column) ;
    return TRUE ;
  }

  return FALSE ;
}

uint32 xmlg_get_element_depth(
      xmlGFilter *filter)
{
  XMLGASSERT(filter != NULL, "filter is NULL") ;
  return filter->depth ;
}

void xmlg_abort(
      xmlGFilter *filter)
{
  XMLGASSERT(filter != NULL, "filter is NULL") ;

  UNUSED_PARAM(xmlGFilter*, filter);
}

HqBool xmlg_get_namespace_uri(
      xmlGFilter *filter,
      const xmlGIStr *prefix,
      const xmlGIStr **uri)
{
  xmlGFilterChain *filter_chain ;

  XMLGASSERT(filter != NULL, "filter is NULL") ;
  XMLGASSERT(prefix != NULL, "prefix is NULL") ;
  XMLGASSERT(uri != NULL, "uri is NULL") ;

  filter_chain = filter->filter_chain ;

  return namespace_stack_find(filter_chain->namespace_stack,
                              prefix, uri) ;
}

void xmlg_f_destroy(
      xmlGFilter **filter)
{
  xmlGFilterChain *filter_chain ;
  xmlGFilter *old_filter ;
  struct xmlGValidChildTable *table ;

  XMLGASSERT(filter != NULL, "filter is NULL") ;
  XMLGASSERT(*filter != NULL, "filter pointer is NULL") ;

  old_filter = *filter ;
  filter_chain = old_filter->filter_chain ;

  if (old_filter->cleanup_cb != NULL)
    old_filter->cleanup_cb(old_filter) ;

  XMLGASSERT(old_filter->element_stack == NULL, "element stack is not NULL");

  /* The only restriction within this function is that you MUST not trash the
     memory function pointers as they will still be used to destroy the
     filter itself. */

  /* This will be the document level validation structure. */
  if (old_filter->valid_child_stack != NULL) {
    XMLGASSERT(old_filter->depth == old_filter->valid_child_stack->depth &&
               old_filter->valid_child_stack->depth == 0,
               "valid child depth is not zero");

    table = old_filter->valid_child_stack;
    old_filter->valid_child_stack = table->next;
    xmlg_valid_children_table_destroy(&table);
  }

  /* Should now be empty. */
  XMLGASSERT(old_filter->valid_child_stack == NULL,
             "valid child stack is not NULL");

  if (filter_chain->first == old_filter)
    filter_chain->first = old_filter->next ;

  if (filter_chain->last == old_filter)
    filter_chain->last = old_filter->prev ;

  if (old_filter->next != NULL)
    old_filter->next->prev = old_filter->prev ;

  if (old_filter->prev != NULL)
    old_filter->prev->next = old_filter->next ;

  filter_chain->filters[old_filter->position] = NULL ;

  xmlg_funct_table_destroy(&(old_filter->element_callbacks)) ;
  xmlg_fc_free(filter_chain, old_filter) ;

  *filter = NULL ;
}

/* ============================================================================
 * Filter set/remove callback functions
 * ============================================================================
 */

void xmlg_set_funct_table(
      xmlGFilter *filter,
      xmlGFunctTable *table)
{
  XMLGASSERT(filter != NULL, "filter is NULL") ;
  filter->element_callbacks = table ;
}

xmlGFunctTable* xmlg_remove_funct_table(
      xmlGFilter *filter)
{
  xmlGFunctTable *t ;
  XMLGASSERT(filter != NULL, "filter is NULL") ;
  t = filter->element_callbacks ;
  filter->element_callbacks = NULL ;

  return t ;
}

void xmlg_set_namespace_cb(
      xmlGFilter *filter,
      xmlGNamespaceCallback f)
{
  XMLGASSERT(filter != NULL, "filter is NULL") ;
  filter->namespace_cb = f ;
}

xmlGNamespaceCallback xmlg_remove_namespace_cb(
     xmlGFilter *filter)
{
  xmlGNamespaceCallback f ;
  XMLGASSERT(filter != NULL, "filter is NULL") ;

  f = filter->namespace_cb ;
  filter->namespace_cb = NULL ;
  return f ;
}

void xmlg_set_character_cb(
      xmlGFilter *filter,
      xmlGCharactersCallback f)
{
  XMLGASSERT(filter != NULL, "filter is NULL") ;
  filter->character_cb = f ;
}

xmlGCharactersCallback xmlg_remove_character_cb(
     xmlGFilter *filter)
{
  xmlGCharactersCallback f ;
  XMLGASSERT(filter != NULL, "filter is NULL") ;

  f = filter->character_cb ;
  filter->character_cb = NULL ;
  return f ;
}

void xmlg_set_xml_decl_cb(
      xmlGFilter *filter,
      xmlGXmlDeclarationCallback f)
{
  XMLGASSERT(filter != NULL, "filter is NULL") ;
  filter->xml_decl_cb = f ;
}

xmlGXmlDeclarationCallback xmlg_remove_xml_decl_cb(
     xmlGFilter *filter)
{
  xmlGXmlDeclarationCallback f ;
  XMLGASSERT(filter != NULL, "filter is NULL") ;

  f = filter->xml_decl_cb ;
  filter->xml_decl_cb = NULL ;
  return f ;
}

void xmlg_set_dtd_start_cb(
      xmlGFilter *filter,
      xmlGXmlDTDStartCallback f)
{
  XMLGASSERT(filter != NULL, "filter is NULL") ;
  filter->xml_dtd_start_cb = f ;
}

xmlGXmlDTDStartCallback xmlg_remove_dtd_start_cb(
     xmlGFilter *filter)
{
  xmlGXmlDTDStartCallback f ;
  XMLGASSERT(filter != NULL, "filter is NULL") ;

  f = filter->xml_dtd_start_cb ;
  filter->xml_dtd_start_cb = NULL ;
  return f ;
}

void xmlg_set_validity_error_cb(
      xmlGFilter *filter,
      xmlGValidityErrorCallback f)
{
  XMLGASSERT(filter != NULL, "filter is NULL") ;
  filter->validity_error_cb = f ;
}

void xmlg_set_user_error_cb(
      xmlGFilter *filter,
      xmlGUserErrorCallback f)
{
  XMLGASSERT(filter != NULL, "filter is NULL") ;
  filter->user_error_cb = f ;
}

HqBool xmlg_register_start_element_cb(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      xmlGStartElementCallback f)
{
  XMLGASSERT(filter != NULL, "filter is NULL") ;

  /* A table must be registered first */
  if (filter->element_callbacks == NULL)
    return FALSE ;

  return xmlg_funct_table_register_start_element_cb(filter->element_callbacks,
                                                    localname,
                                                    uri, f) ;
}

HqBool xmlg_register_end_element_cb(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      xmlGEndElementCallback f)
{
  XMLGASSERT(filter != NULL, "filter is NULL") ;

  /* A table must be registered first */
  if (filter->element_callbacks == NULL)
    return FALSE ;

  return xmlg_funct_table_register_end_element_cb(filter->element_callbacks,
                                                  localname,
                                                  uri, f) ;
}

HqBool xmlg_register_characters_cb(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      xmlGCharactersCallback f)
{
  XMLGASSERT(filter != NULL, "filter is NULL") ;

  /* A table must be registered first */
  if (filter->element_callbacks == NULL)
    return FALSE ;

  return xmlg_funct_table_register_characters_cb(filter->element_callbacks,
                                                 localname, uri, f) ;
}

xmlGStartElementCallback xmlg_deregister_start_element_cb(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *uri)
{
  XMLGASSERT(filter != NULL, "filter is NULL") ;

  if (filter->element_callbacks == NULL)
    return NULL ;

  return xmlg_funct_table_remove_start_element_cb(filter->element_callbacks,
                                                  localname, uri) ;
}

xmlGEndElementCallback xmlg_deregister_end_element_cb(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *uri)
{
  XMLGASSERT(filter != NULL, "filter is NULL") ;

  if (filter->element_callbacks == NULL)
    return NULL ;

  return xmlg_funct_table_remove_end_element_cb(filter->element_callbacks,
                                                localname, uri) ;
}

xmlGCharactersCallback xmlg_deregister_characters_cb(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *uri)
{
  XMLGASSERT(filter != NULL, "filter is NULL") ;

  if (filter->element_callbacks == NULL)
    return NULL ;

  return xmlg_funct_table_remove_characters_cb(filter->element_callbacks,
                                               localname, uri) ;
}

void xmlg_get_current_element(
      xmlGFilter *filter,
      const xmlGIStr **name,
      const xmlGIStr **prefix)
{
  XMLGASSERT(filter != NULL, "filter is NULL") ;
  if (filter->element_stack == NULL) {
    *name = *prefix = NULL;
  }
  else {
    *name = filter->element_stack->localname;
    *prefix = filter->element_stack->prefix;
  }
}

/* ============================================================================
* Log stripped */
