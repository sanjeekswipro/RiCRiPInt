/* ============================================================================
 * $HopeName: HQNgenericxml!src:xmlgfunctable.c(EBDSDK_P.1) $
 * $Id: src:xmlgfunctable.c,v 1.18.10.1.1.1 2013/12/19 11:24:21 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/* \file
 * \brief Implementation of XML function tables.
 */

#include "xmlgpriv.h"
#include "xmlgassert.h"
#include "xmlgfunctablepriv.h"
#include "xmlginternpriv.h"

/**
 * Medium prime.
 * I would expect this to be enough URI/element name pairs.
 * \todo Hash size and function will need profiling.
 *
 * Note that slot zero is reserved for entries which have no
 * localname, hence the + 1 on the hash value. This is needed in the
 * event that a localname hashes to zero and has a NULL callback which
 * would cause another lookup with the same value URI.
 */
#define FUNCTTABLE_HASH_SIZE 1009

struct FunctEntry {
  const xmlGIStr *localname ;              /* Key1                */
  const xmlGIStr *uri ;                    /* Key2                */
  xmlGStartElementCallback f_start ;       /* Start callback      */
  xmlGCharactersCallback   f_characters ;  /* Characters callback */
  xmlGEndElementCallback   f_end ;         /* End callback        */
  struct FunctEntry *next ;                /* Singly-linked list  */
} ;

struct xmlGFunctTable {
  xmlGContext *xml_ctxt ;
  xmlGFilterChain *filter_chain ;
  /* Number of entries in hash table */
  unsigned int num_entries ;
  /* The hash table */
  /*@partial@*/
  struct FunctEntry **table ;
} ;

#define HASH_LOCALNAME(_localname) (CAST_UINTPTRT_TO_UINT32( ((uintptr_t)_localname >> 2) % (FUNCTTABLE_HASH_SIZE + 1) ))

static
/*@null@*/
struct FunctEntry *xmlg_find_funct_entry(
      xmlGFunctTable *table,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      unsigned int *hval)
{
  struct FunctEntry *curr ;
  XMLGASSERT(table != NULL, "table is NULL") ;
  XMLGASSERT(hval != NULL, "hval is NULL") ;

  if (localname == NULL) {
    *hval = 0 ;
    if (uri == NULL) {
      for (curr=table->table[*hval]; curr!=NULL; curr=curr->next) {
        if (curr->uri == NULL) {
          return curr ;
        }
      }
    } else {
      for (curr=table->table[*hval]; curr!=NULL; curr=curr->next) {
        if (xmlg_istring_equal(table->xml_ctxt, curr->uri, uri)) {
          return curr ;
        }
      }
    }
  } else {
    /* Lets just use the pointer of the interned localname. */
    *hval = HASH_LOCALNAME(localname) ;

    for (curr=table->table[*hval]; curr!=NULL; curr=curr->next) {
      if (xmlg_istring_equal(table->xml_ctxt, curr->localname, localname) &&
          xmlg_istring_equal(table->xml_ctxt, curr->uri, uri)) {
        return curr ;
      }
    }
  }

  return NULL;
}

HqBool xmlg_funct_table_create(
      xmlGFilterChain *filter_chain,
      xmlGFunctTable **table)
{
  unsigned int i;
  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  XMLGASSERT(table != NULL, "table pointer is NULL");
  *table = xmlg_fc_malloc(filter_chain, sizeof(xmlGFunctTable));

  if (*table == NULL) {
    return FALSE;
  }

  (*table)->table = xmlg_fc_malloc(filter_chain, sizeof(struct FunctEntry*) * (FUNCTTABLE_HASH_SIZE + 1));
  if ((*table)->table == NULL) {
    xmlg_fc_free(filter_chain, *table);
    *table = NULL;
    return FALSE;
  }

  /* Initialize the table structure. */
  (*table)->filter_chain = filter_chain;
  (*table)->xml_ctxt = filter_chain->xml_ctxt;
  (*table)->num_entries = 0;
  for (i=0; i<FUNCTTABLE_HASH_SIZE + 1; i++) {
    (*table)->table[i] = NULL;
  }
  return TRUE;
}

void xmlg_funct_table_destroy(
      xmlGFunctTable **table)
{
  xmlGContext *xml_ctxt ;
  xmlGFilterChain *filter_chain ;
  unsigned int i ;
  struct FunctEntry *curr, *next ;

  XMLGASSERT(table != NULL, "table is NULL") ;
  XMLGASSERT(*table != NULL, "table pointer is NULL") ;

  xml_ctxt = (*table)->xml_ctxt ;
  filter_chain = (*table)->filter_chain ;

  for (i=0; i<FUNCTTABLE_HASH_SIZE + 1; i++) {
    for (curr = (*table)->table[i]; curr != NULL; curr = next) {
      next = curr->next ;

      if (curr->localname != NULL)
        xmlg_istring_destroy((*table)->xml_ctxt, &curr->localname);
      if (curr->uri != NULL)
        xmlg_istring_destroy((*table)->xml_ctxt, &curr->uri) ;

      xmlg_fc_free(filter_chain, curr) ;
      (*table)->num_entries-- ;
    }
  }
  XMLGASSERT((*table)->num_entries == 0, "num_entries is not zero.") ;

  xmlg_fc_free(filter_chain, (*table)->table) ;
  xmlg_fc_free(filter_chain, *table) ;
  (*table) = NULL ;
  return ;
}

static
HqBool xmlg_funct_table_register(
      xmlGFunctTable *table,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      struct FunctEntry **entry)
{
  xmlGContext *xml_ctxt;
  xmlGFilterChain *filter_chain;
  struct FunctEntry *curr;
  unsigned int hval;

  XMLGASSERT(table != NULL, "table is NULL") ;

  xml_ctxt = table->xml_ctxt ;
  filter_chain = table->filter_chain ;

  *entry = NULL ;

  curr = xmlg_find_funct_entry(table, localname, uri, &hval) ;

  if (curr == NULL) {
    curr = xmlg_fc_malloc(filter_chain, sizeof(struct FunctEntry)) ;
    if (curr == NULL) {
      return FALSE ;
    }

    curr->localname = localname;
    if ( localname != NULL && ! xmlg_istring_reserve(table->xml_ctxt, localname) ) {
      xmlg_fc_free(filter_chain, curr) ;
      return FALSE ;
    }

    curr->uri = uri;
    if ( uri != NULL && ! xmlg_istring_reserve(table->xml_ctxt, uri) ) {
      xmlg_istring_destroy(table->xml_ctxt, &curr->localname) ;
      xmlg_fc_free(filter_chain, curr) ;
      return FALSE ;
    }

    curr->f_start = NULL ;
    curr->f_characters = NULL ;
    curr->f_end = NULL ;

    curr->next = table->table[hval] ;
    table->table[hval] = curr ;
    table->num_entries++ ;
  }
  *entry = curr ;
  return TRUE ;
}

HqBool xmlg_funct_table_register_start_element_cb(
      xmlGFunctTable *table,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      xmlGStartElementCallback f)
{
  struct FunctEntry *curr;

  XMLGASSERT(table != NULL, "table is NULL");
  XMLGASSERT(f != NULL, "f is NULL");

  if (! xmlg_funct_table_register(table, localname, uri, &curr) )
    return FALSE;

  curr->f_start = f;
  return TRUE;
}

HqBool xmlg_funct_table_register_end_element_cb(
      xmlGFunctTable *table,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      xmlGEndElementCallback f)
{
  struct FunctEntry *curr;

  XMLGASSERT(table != NULL, "table is NULL");
  XMLGASSERT(f != NULL, "f is NULL");

  if (! xmlg_funct_table_register(table, localname, uri, &curr) )
    return FALSE;

  curr->f_end = f;
  return TRUE;
}

HqBool xmlg_funct_table_register_characters_cb(
      xmlGFunctTable *table,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      xmlGCharactersCallback f)
{
  struct FunctEntry *curr;

  XMLGASSERT(table != NULL, "table is NULL");
  XMLGASSERT(f != NULL, "f is NULL");

  if (! xmlg_funct_table_register(table, localname, uri, &curr) )
    return FALSE;

  curr->f_characters = f;
  return TRUE;
}

/*@null@*/
xmlGStartElementCallback xmlg_funct_table_remove_start_element_cb(
      xmlGFunctTable *table,
      const xmlGIStr *localname,
      const xmlGIStr *uri)
{
  struct FunctEntry *curr;
  unsigned int hval;
  xmlGStartElementCallback f;

  if ((curr = xmlg_find_funct_entry(table, localname, uri, &hval)) == NULL)
    return NULL;

  f = curr->f_start;
  curr->f_start = NULL;
  return f;
}

/*@null@*/
xmlGEndElementCallback xmlg_funct_table_remove_end_element_cb(
      xmlGFunctTable *table,
      const xmlGIStr *localname,
      const xmlGIStr *uri)
{
  struct FunctEntry *curr;
  unsigned int hval;
  xmlGEndElementCallback f;


  if ((curr = xmlg_find_funct_entry(table, localname, uri, &hval)) == NULL)
    return NULL;

  f = curr->f_end;
  curr->f_end = NULL;
  return f;
}

/*@null@*/
xmlGCharactersCallback xmlg_funct_table_remove_characters_cb(
      xmlGFunctTable *table,
      const xmlGIStr *localname,
      const xmlGIStr *uri)
{
  struct FunctEntry *curr;
  unsigned int hval;
  xmlGCharactersCallback f;

  if ((curr = xmlg_find_funct_entry(table, localname, uri, &hval)) == NULL)
    return NULL;

  f = curr->f_characters;
  curr->f_characters = NULL;
  return f;
}

/*@null@*/
xmlGStartElementCallback xmlg_start_funct_table_lookup(
      xmlGFunctTable *table,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      HqBool *found)
{
  struct FunctEntry *curr;
  unsigned int hval;

  if (table == NULL)
    return NULL ;

  if ((curr = xmlg_find_funct_entry(table, localname, uri, &hval)) == NULL) {
    *found = FALSE ;
    return NULL;
  }
  *found = TRUE ;

  return curr->f_start;
}

/*@null@*/
xmlGEndElementCallback xmlg_end_funct_table_lookup(
      xmlGFunctTable *table,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      HqBool *found)
{
  struct FunctEntry *curr;
  unsigned int hval;

  if (table == NULL)
    return NULL ;

  if ((curr = xmlg_find_funct_entry(table, localname, uri, &hval)) == NULL) {
    *found = FALSE ;
    return NULL ;
  }
  *found = TRUE ;

  return curr->f_end;
}

/*@null@*/
xmlGCharactersCallback xmlg_characters_funct_table_lookup(
      xmlGFunctTable *table,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      HqBool *found)
{
  struct FunctEntry *curr;
  unsigned int hval;

  if (table == NULL)
    return NULL ;

  if ((curr = xmlg_find_funct_entry(table, localname, uri, &hval)) == NULL) {
    *found = FALSE ;
    return NULL;
  }
  *found = TRUE ;

  return curr->f_characters;
}

/* Lookup function entry for the localname, uri pair. If not found
   then degrade to see if there is a callback for this namespace, if
   not found, then degrade to look for a global function (no localname
   and no uri). This function uses as few tests as possible. */
HqBool xmlg_funct_table_degrade_lookup(
      xmlGFunctTable *table,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      xmlGStartElementCallback *fs,
      xmlGCharactersCallback *fc,
      xmlGEndElementCallback *fe)
{
  struct FunctEntry *curr ;
  unsigned int hval ;

  HQASSERT(table != NULL, "table is NULL") ;
  HQASSERT(fs != NULL, "fs is NULL") ;
  HQASSERT(fc != NULL, "fc is NULL") ;
  HQASSERT(fe != NULL, "fe is NULL") ;

  *fs = NULL;
  *fc = NULL;
  *fe = NULL;

  /* We assert NULL as zero so the hash function works in this
     case. */
  HQASSERT((intptr_t)NULL == 0, "NULL is not zero") ;

  hval = HASH_LOCALNAME(localname) ;

  /* Look for localname and uri. */
  for (curr=table->table[hval]; curr!=NULL; curr=curr->next) {
    if (xmlg_istring_equal(table->xml_ctxt, curr->localname, localname) &&
        xmlg_istring_equal(table->xml_ctxt, curr->uri, uri))
      goto found ;
  }

  /* Look for localname with NULL uri. */
  for (curr=table->table[hval]; curr!=NULL; curr=curr->next) {
    if (xmlg_istring_equal(table->xml_ctxt, curr->localname, localname) &&
        curr->uri == NULL)
      goto found ;
  }

  /* Look for NULL localname and NULL uri. */
  hval = 0 ;
  for (curr=table->table[hval]; curr!=NULL; curr=curr->next) {
    if (curr->localname == NULL &&
        curr->uri == NULL)
      goto found ;
  }

  return FALSE ;

 found:

  *fs = curr->f_start ;
  *fc = curr->f_characters ;
  *fe = curr->f_end ;

  return TRUE ;
}

/* ============================================================================
* Log stripped */
