/* ============================================================================
 * $HopeName: HQNgenericxml!src:xmlgintern.c(EBDSDK_P.1) $
 * $Id: src:xmlgintern.c,v 1.13.10.1.1.1 2013/12/19 11:24:21 anon Exp $
 *
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/* \file
 * \brief Implements default interned string pool or calls the override
 * intern string functions.
 *
 * \note Internal interning is not yet implemented and unlikely to be
 * implemented unless there is a need.
 */

#include "xmlgwarnings.h"
#include "xmlgpriv.h"
#include "xmlginternpriv.h"
#include "xmlgassert.h"

/* ============================================================================
 * XMLG built in intern interface.
 */

/**
 * The lifetime of the interned strings is between xmlGIntialize and
 * xmlGTerminate.
 */
static HqBool intern_reserve(
      /*@notnull@*/ /*@in@*/
      xmlGContext *xml_ctxt,
      /*@notnull@*/ /*@in@*/
      const xmlGIStr *istr);

static void intern_destroy(
      /*@notnull@*/ /*@in@*/
      xmlGContext *xml_ctxt,
      /*@notnull@*/ /*@out@*/
      const xmlGIStr **istr);

static HqBool intern_create(
      /*@notnull@*/ /*@in@*/
      xmlGContext *xml_ctxt,
      /*@notnull@*/ /*@out@*/
      const xmlGIStr **istr,
      /*@notnull@*/ /*@in@*/ /*@observer@*/
      const uint8 *strbuf,
      uint32 buflen);

static uintptr_t intern_hash(
      /*@notnull@*/ /*@in@*/
      xmlGContext *xml_ctxt,
      /*@notnull@*/ /*@in@*/ /*@observer@*/
      const xmlGIStr *istr);

static uint32 intern_length(
      /*@notnull@*/ /*@in@*/
      xmlGContext *xml_ctxt,
      /*@notnull@*/ /*@in@*/ /*@observer@*/
      const xmlGIStr *istr);

/*@dependent@*/
static const uint8* intern_value(
      /*@notnull@*/ /*@in@*/
      xmlGContext *xml_ctxt,
      /*@notnull@*/ /*@in@*/ /*@observer@*/
      const xmlGIStr *istr);

static HqBool intern_equal(
      /*@notnull@*/ /*@in@*/
      xmlGContext *xml_ctxt,
      /*@notnull@*/ /*@in@*/ /*@observer@*/
      const xmlGIStr *istr1,
      /*@notnull@*/ /*@in@*/ /*@observer@*/
      const xmlGIStr *istr2);

static void intern_terminate(
      /*@notnull@*/ /*@in@*/
      xmlGContext *xml_ctxt,
      /*@notnull@*/ /*@in@*/
      xmlGIStrHandler *handler) ;

/* Constants for PJW hash function */
#define PJW_SHIFT        (4)            /* Per hashed char hash shift */
#define PJW_MASK         (0xf0000000u)  /* Mask for hash top bits */
#define PJW_RIGHT_SHIFT  (24)           /* Right shift distance for hash top bits */

/* Internal implementation of opaque types.

   Other implementations of the xmlGIStr opaque type may exist in other
   files. */
struct xmlGIStr {
  uintptr_t hashval;
  uint32 length;
  uint32 ref_count;
  const uint8 *str;
  xmlGIStr *next, *prev;
};

struct xmlGInternPool {
  uint32 num_entries; /* Number of entries in hash table */
  /*@partial@*/
  xmlGIStr **table;   /* The hash table */
};

/**
 * Medium prime.
 * Likely to be quite a few strings to intern.
 * \todo Hash size and function will need profiling.
 */
#define DEFAULTINTERN_HASH_SIZE 1151

static inline uintptr_t pjw_hash(/*@in@*/ /*@notnull@*/ /*@observer@*/
                                 const uint8 *str,
                                 uint32 strlen)
{
  uintptr_t hash = 0;
  uintptr_t bits = 0;

  XMLGASSERT(str != NULL, "str is NULL");
  XMLGASSERT(strlen > 0, "invalid string length");

  while ( strlen > 0 ) {
    hash = (hash << PJW_SHIFT) + *str++;
    bits = hash & PJW_MASK;
    hash ^= bits|(bits >> PJW_RIGHT_SHIFT);
    --strlen ;
  }
  return hash;
}

/* Initialise the default intern string handler.
 */
HqBool xmlg_intern_default(
      xmlGContext *xml_ctxt)
{
  xmlGInternPool *pool;
  uint32 i;

  pool = xmlg_subsystem_malloc(xml_ctxt, sizeof(xmlGInternPool));
  if (pool == NULL) {
    return FALSE;
  }
  pool->num_entries = 0;
  pool->table = xmlg_subsystem_malloc(xml_ctxt, sizeof(xmlGIStr*) * DEFAULTINTERN_HASH_SIZE);
  if (pool->table == NULL) {
    xmlg_subsystem_free(xml_ctxt, pool);
    return FALSE;
  }
  for (i=0; i<DEFAULTINTERN_HASH_SIZE; i++) {
    pool->table[i] = NULL;
  }

  xml_ctxt->intern_handler.f_intern_create = intern_create ;
  xml_ctxt->intern_handler.f_intern_reserve = intern_reserve ;
  xml_ctxt->intern_handler.f_intern_destroy = intern_destroy ;
  xml_ctxt->intern_handler.f_intern_hash = intern_hash ;
  xml_ctxt->intern_handler.f_intern_length = intern_length ;
  xml_ctxt->intern_handler.f_intern_value = intern_value ;
  xml_ctxt->intern_handler.f_intern_equal = intern_equal ;
  xml_ctxt->intern_handler.f_intern_terminate = intern_terminate ;

  xml_ctxt->intern_pool = pool ;

  return TRUE;
}

static void intern_terminate(
      xmlGContext *xml_ctxt,
      xmlGIStrHandler *handler)
{
  uint32 i;
  xmlGInternPool *pool;
  xmlGIStr *curr, *next;

  UNUSED_PARAM(xmlGIStrHandler *, handler) ;
  pool = xml_ctxt->intern_pool;
  XMLGASSERT(pool != NULL, "intern pool is NULL");

  /* XMLGASSERT(pool->num_entries == 0, "intern pool not empty");
   */
  for (i=0; i < DEFAULTINTERN_HASH_SIZE; i++) {
    for (curr = pool->table[i]; curr != NULL; curr = next) {
      next = curr->next;
      XMLGASSERT(curr->length != 0, "intern string length is zero");
      pool->num_entries--;
      xmlg_subsystem_free(xml_ctxt, curr);
    }
  }
  XMLGASSERT(pool->num_entries == 0,
             "corrupt intern pool, num_entries not zero");

  xmlg_subsystem_free(xml_ctxt, pool->table);
  xmlg_subsystem_free(xml_ctxt, pool);
  xml_ctxt->intern_pool = NULL;

  return;
}

static HqBool intern_reserve(
      xmlGContext *xml_ctxt,
      const xmlGIStr *istr)
{
  /* Declaring istr const was a little white lie. The existing string will not
     be modified in any meaningful way. */
  xmlGIStr *str ;

  XMLG_UNUSED_PARAM(xmlGContext *, xml_ctxt) ;

  str = (xmlGIStr *)istr ;

  XMLGASSERT(istr != NULL, "istr is NULL");

  str->ref_count++;

  return TRUE;
}


static uintptr_t intern_hash(
      xmlGContext *xml_ctxt,
      const xmlGIStr *istr)
{
  XMLG_UNUSED_PARAM(xmlGContext *, xml_ctxt) ;

  XMLGASSERT(istr != NULL, "istr is NULL");

  return istr->hashval;
}

static const uint8* intern_value(
      xmlGContext *xml_ctxt,
      const xmlGIStr *istr)
{
  XMLG_UNUSED_PARAM(xmlGContext *, xml_ctxt) ;

  XMLGASSERT(istr != NULL, "istr is NULL");

  return istr->str;
}

static uint32 intern_length(
      xmlGContext *xml_ctxt,
      const xmlGIStr *istr)
{
  XMLG_UNUSED_PARAM(xmlGContext *, xml_ctxt) ;

  XMLGASSERT(istr != NULL, "istr is NULL");

  return istr->length;
}

static HqBool intern_equal(
      xmlGContext *xml_ctxt,
      const xmlGIStr *istr1,
      const xmlGIStr *istr2)
{
  XMLG_UNUSED_PARAM(xmlGContext *, xml_ctxt) ;

  XMLGASSERT(istr1 != NULL, "istr1 is NULL");
  XMLGASSERT(istr2 != NULL, "istr2 is NULL");

  return (istr1 == istr2) ;
}

static HqBool intern_create(
      xmlGContext *xml_ctxt,
      const xmlGIStr **istr,
      const uint8 *strbuf,
      uint32 buflen)
{
  xmlGInternPool *pool ;
  xmlGIStr *curr;
  uintptr_t hashval, lookup;

  XMLG_UNUSED_PARAM(xmlGContext *, xml_ctxt) ;

  XMLGASSERT(istr != NULL, "istr is NULL");
  XMLGASSERT(strbuf != NULL, "strbuf is NULL");
  XMLGASSERT(buflen > 0, "buflen is not greater than zero");

  *istr = NULL;

  pool = xml_ctxt->intern_pool;
  XMLGASSERT(pool != NULL, "intern pool is NULL");

  hashval = pjw_hash(strbuf, buflen);
  lookup = hashval % DEFAULTINTERN_HASH_SIZE;

  for ( curr = pool->table[lookup]; curr != NULL; curr = curr->next ) {
    if (curr->length == buflen) {
      if (memcmp(curr->str, strbuf, buflen) == 0) {
        break;
      }
    }
  }

  if (curr == NULL) {
    uint8 *str ;

    /* No entry found, so create a new one */
    curr = xmlg_subsystem_malloc(xml_ctxt, sizeof(xmlGIStr) + buflen);
    if (curr == NULL)
      return FALSE;

    pool->num_entries++;
    curr->ref_count = 0;
    curr->length = buflen;
    curr->hashval = hashval;
    curr->next = pool->table[lookup];
    curr->prev = NULL;
    curr->str = str = (uint8 *)(curr + 1);
    (void)memcpy(str, strbuf, buflen);
    if (curr->next != NULL) {
      (curr->next)->prev = curr;
    }

    pool->table[lookup] = curr;
  }

  curr->ref_count++;
  *istr = curr;

  return TRUE;
}

static HqBool deallocate = FALSE ;

static void intern_destroy(
      xmlGContext *xml_ctxt,
      const xmlGIStr **istr)
{
  xmlGInternPool *pool;
  xmlGIStr *str ;

  XMLG_UNUSED_PARAM(xmlGContext *, xml_ctxt) ;

  XMLGASSERT(istr != NULL, "istr is NULL");
  str = (xmlGIStr *)*istr;

  XMLGASSERT(str != NULL, "istr pointer is NULL");

  pool = xml_ctxt->intern_pool;
  XMLGASSERT(pool != NULL, "intern pool is NULL");

  if (deallocate) {
    /* When de-allocating, we MUST have a positive reference count. */
    XMLGASSERT(str->ref_count > 0,
               "istr reference count is not greater than zero");
    str->ref_count--;

    if (str->ref_count == 0) {
      if ( str->next != NULL )
        str->next->prev = str->prev ;

      if ( str->prev != NULL) {
        /* Remove from within list. */
        str->prev->next = str->next;
      } else {
        /* We are at the beginning of the list. */
        pool->table[str->hashval % DEFAULTINTERN_HASH_SIZE] = str->next;
      }
      xmlg_subsystem_free(xml_ctxt, str);
      pool->num_entries--;
    }
  } else {
    /* When not de-allocating, we allow a zero reference count. */
    if (str->ref_count > 0) {
      str->ref_count--;
    } else {
      XMLGASSERT(FALSE,
                 "interned string de-allocated one too many times.") ;
    }
  }

  /* always kill the reference */
  *istr = NULL;

  return;
}

HqBool xmlg_istring_create(
      xmlGContext *xml_ctxt,
      const xmlGIStr **istr,
      const uint8 *strbuf,
      uint32 buflen)
{
  XMLGASSERT(xml_ctxt != NULL, "xml subsystem context is NULL") ;

  return xml_ctxt->intern_handler.f_intern_create(
                       xml_ctxt,
                       istr,
                       strbuf,
                       buflen) ;
}

/* ============================================================================
* Log stripped */
