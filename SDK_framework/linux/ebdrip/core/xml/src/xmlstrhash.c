/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!src:xmlstrhash.c(EBDSDK_P.1) $
 * $Id: src:xmlstrhash.c,v 1.9.10.1.1.1 2013/12/19 11:25:09 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Basic string hash functions
 */

#include "core.h"
#include "hqmemcmp.h"   /* HqMemCmp */
#include "hqmemcpy.h"   /* HqMemCpy */
#include "swerrors.h"
#include "xmlstrhash.h"
#include "objnamer.h"
#include "xmlhash.h"

/* ============================================================================
 * private data
 */
struct Entry {
  uint8 * key;            /* Key                */
  uint32  keylen;         /* Length of key      */
  void * value;           /* Payload data       */
  struct Entry *next;     /* Singly-linked list */
};

#define XMLSTRHASHTABLE_NAME "XmlStrHashTable"

struct XmlStrHashTable {
  uint32 size;                              /* Hash table size */
  uint32 num_entries;                       /* Number of entries in hash table */
  /*@partial@*/
  struct Entry **table;                     /* The hash table */
  XmlStrHashPayloadFreeFunct fPayloadFree;  /* Free the payload data structure */
  XmlStrHashMallocFunct fMalloc;            /* Memory management */
  XmlStrHashFreeFunct fFree;
  XmlStrHash fHash;
  OBJECT_NAME_MEMBER                        /* For validating the object type */
};


/* ============================================================================
 * default hash
 */

static
/*@null@*/
struct Entry *find(
      XmlStrHashTable *table,
      const uint8 *key,
      uint32 keylen,
      uint32 *hval)
{
  struct Entry *curr;

  HQASSERT(table != NULL, "find: hash table null pointer");
  HQASSERT(key != NULL, "find: key null pointer");

  VERIFY_OBJECT(table, XMLSTRHASHTABLE_NAME);

  *hval = table->fHash(key, keylen);
  *hval %= table->size;
  for (curr=table->table[*hval]; curr!=NULL; curr=curr->next) {
    if (keylen == curr->keylen) {
      if (HqMemCmp(key, keylen, curr->key, curr->keylen) == 0) {
        return curr;
      }
    }
  }
  return NULL;
}


/* ============================================================================
 * public interface
 */

XmlStrHashTable*
xmlstr_hash_create(
      uint32 size,
      XmlStrHashPayloadFreeFunct fPayloadFree,
      XmlStrHashMallocFunct fMalloc,
      XmlStrHashFreeFunct fFree,
      XmlStrHash fHash)
{
  XmlStrHashTable *newTable;
  uint32 i;
  HQASSERT(size > 0, "hash_create: size not greater than 0");

  HQASSERT(fMalloc != NULL && fFree != NULL, "hash_create: no memory management supplied");
  if (fMalloc == NULL || fFree == NULL) {
    return NULL;
  }

  newTable = fMalloc(sizeof(XmlStrHashTable));
  if (newTable == NULL) {
    return NULL;
  }

  newTable->fMalloc = fMalloc;
  newTable->fFree = fFree;
  if (fHash == NULL) {
    newTable->fHash = xml_strhash;
  }

  newTable->table = newTable->fMalloc(sizeof(struct Entry **) * size);
  if (newTable->table == NULL) {
    newTable->fFree(newTable);
    return NULL;
  }

  for (i=0; i<size; i++) {
    newTable->table[i] = NULL;
  }
  newTable->size = size;
  newTable->num_entries = 0;
  newTable->fPayloadFree = fPayloadFree;

  NAME_OBJECT(newTable, XMLSTRHASHTABLE_NAME);

  return newTable;
}


void xmlstr_hash_destroy(
      XmlStrHashTable **table)
{
  uint32 i;

  HQASSERT(table != NULL, "table is NULL");
  HQASSERT(*table != NULL, "table pointer is NULL");

  UNNAME_OBJECT(*table);

  for (i=0; i < (*table)->size; i++) {
    struct Entry *curr, *next;
    for (curr=(*table)->table[i]; curr!=NULL; curr=next) {
      next = curr->next;
      if ((*table)->fPayloadFree != NULL) {
        if (curr->value != NULL) {
          (*table)->fPayloadFree(curr->value);
        }
      }
      (*table)->fFree(curr->key);
      (*table)->fFree(curr);
    }
  }
  (*table)->num_entries = 0;
  (*table)->size = 0;
  (*table)->fFree((*table)->table);
  (*table)->fFree((*table));
  *table = NULL;
}


Bool xmlstr_hash_get(
      XmlStrHashTable *table,
      const uint8 *key,
      uint32 keylen,
      void **value)
{
  uint32 hval;
  struct Entry *curr;

  HQASSERT(table != NULL, "table is NULL");
  HQASSERT(key != NULL, "key is NULL");
  HQASSERT(value != NULL, "value is NULL");

  VERIFY_OBJECT(table, XMLSTRHASHTABLE_NAME);

  *value = NULL;
  curr = find(table, key, keylen, &hval);
  if (curr != NULL) {
      *value = curr->value;
      return TRUE;
  }

  return FALSE;
}


Bool xmlstr_hash_add(
      XmlStrHashTable *table,
      const uint8 *key,
      uint32 keylen,
      void *value,
      void **old_value)
{
  struct Entry *curr;
  uint32 hval;

  HQASSERT(table != NULL, "table is NULL");
  HQASSERT(key != NULL, "key is NULL");
  HQASSERT(old_value != NULL, "old_value is NULL");

  VERIFY_OBJECT(table, XMLSTRHASHTABLE_NAME);

  curr = find(table, key, keylen, &hval);
  *old_value = NULL;

  if (curr == NULL) {
    curr = table->fMalloc(sizeof(struct Entry));
    if (curr == NULL) {
      return FALSE;
    }

    /* We know that the key is NUL terminated. Keep the NUL.
     */
    curr->key = table->fMalloc(keylen + 1);
    if (curr->key == NULL) {
      table->fFree(curr);
      return FALSE;
    }
    (void)HqMemCpy((char *)curr->key, (const char *)key, keylen);
    curr->key[keylen] = '\0';

    curr->value = NULL;
    curr->keylen = keylen;
    curr->next = table->table[hval];
    table->table[hval] = curr;
    table->num_entries++;
  }
  *old_value = curr->value;
  curr->value = value;
  return TRUE;
}


void *xmlstr_hash_remove(
      XmlStrHashTable *table,
      const uint8 *key,
      uint32 keylen)
{
  struct Entry *curr, *prev=NULL;
  uint32 hval;

  HQASSERT(table != NULL, "table is NULL");
  HQASSERT(key != NULL, "key is NULL");

  VERIFY_OBJECT(table, XMLSTRHASHTABLE_NAME);

  hval = table->fHash(key, keylen);
  hval %= table->size;

  for (curr=table->table[hval]; curr!=NULL; curr=curr->next) {
    if (keylen == curr->keylen) {
      if (HqMemCmp(key, keylen, curr->key, keylen) == 0) {
        void *old_value = curr->value;
        if (prev != NULL)
          prev->next = curr->next;
        else if (curr == table->table[hval])
          table->table[hval] = curr->next;
        table->fFree(curr->key);
        table->fFree(curr);
        table->num_entries--;
        return old_value;
      }
    }
    prev = curr;
  }
  return NULL;
}

/* ============================================================================
* Log stripped */
