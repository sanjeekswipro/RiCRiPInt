/** \file
 * \ingroup types
 *
 * $HopeName: COREtypes!src:idhashtable.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */
#include "core.h"
#include "idhashtable.h"

/**
 * Add the passed entry to the zombie list.
 */
static void add_to_zombies(IdHashTable* self, IdHashTableEntry* entry)
{
  HQASSERT(self != NULL, "self is NULL") ;
  HQASSERT(entry != NULL, "entry is NULL") ;

  entry->private.next = self->zombies ;
  self->zombies = entry ;
}

/**
 * Delete or make a zombie of the passed entry, depending on the table's policy.
 */
static void process_removed_entry(IdHashTable* self, IdHashTableEntry* entry) {
  if (self->destroy_on_remove) {
    self->entry_destructor(entry);
  }
  else {
    add_to_zombies(self, entry);
  }
}

/**
 * Find the entry with the specified id. Returns FALSE if no such entry is
 * present.
 */
static Bool find_entry(IdHashTable* self, uint32 id,
                       IdHashTableEntry** entry,
                       IdHashTableEntry** prev,
                       uint32* hash)
{
  IdHashTableEntry *curr, *temp_prev = NULL ;

  HQASSERT(self != NULL, "self is NULL") ;
  HQASSERT(entry != NULL, "entry is NULL") ;
  HQASSERT(hash != NULL, "hash is NULL") ;

  *hash = id % self->table_size ;
  *prev = NULL ;
  *entry = NULL ;

  for (curr = self->table[*hash]; curr != NULL; curr = curr->private.next) {
    if (curr->private.id == id) {
      *entry = curr ;
      *prev = temp_prev ;
      return TRUE ;
    }
    temp_prev = curr ;
  }

  return FALSE ;
}

/**
 * Unlink the specified entry from the list.
 */
static void unlink_entry(IdHashTable* self,
                         IdHashTableEntry* entry,
                         IdHashTableEntry* prev,
                         uint32 hash) {
  if (prev == NULL) {
    self->table[hash] = entry->private.next ;
  } else {
    prev->private.next = entry->private.next ;
  }
  process_removed_entry(self, entry) ;
}

/* See header for doc. */
IdHashTable* id_hashtable_create(mm_pool_t pool,
                                 uint32 table_size, Bool destroy_on_remove,
                                 IdHashTableEntryDestructor* entry_destructor)
{
  IdHashTable *self ;
  uint32 i ;

  UNUSED_PARAM(uint32, table_size) ;

  self = mm_alloc(pool,
                  sizeof(IdHashTable) + (sizeof(IdHashTableEntry*) * table_size),
                  MM_ALLOC_CLASS_ID_HASH_TABLE);
  if (self == NULL)
    return NULL ;

  self->pool = pool;
  self->table = (IdHashTableEntry**)(self + 1);
  self->zombies = NULL ;
  self->table_size = table_size ;
  self->destroy_on_remove = destroy_on_remove ;
  self->entry_destructor = entry_destructor ;

  for (i = 0; i < table_size; i ++) {
    self->table[i] = NULL ;
  }

  return self ;
}

/* See header for doc. */
void id_hashtable_destroy(IdHashTable **self_pointer)
{
  IdHashTable* self ;

  HQASSERT(self_pointer != NULL, "self_pointer is NULL") ;
  HQASSERT(*self_pointer != NULL, "*self_pointer is NULL") ;

  self = *self_pointer ;
  id_hashtable_remove_all(self, TRUE) ;
  id_hashtable_kill_zombies(self) ;

  mm_free(self->pool, self,
          sizeof(IdHashTable) + (sizeof(IdHashTableEntry*) * self->table_size)) ;
  *self_pointer = NULL ;
}

/* See header for doc. */
void id_hashtable_insert(IdHashTable* self,
                         IdHashTableEntry* new_entry, uint32 id)
{
  IdHashTableEntry *entry, *prev ;
  uint32 hash ;

  /* Remove any existing entry. */
  if (find_entry(self, id, &entry, &prev, &hash)) {
    unlink_entry(self, entry, prev, hash);
  }

  /* Insert. */
  new_entry->private.id = id ;
  new_entry->private.permanent = FALSE ;
  new_entry->private.next = self->table[hash] ;
  self->table[hash] = new_entry ;
}

/* See header for doc. */
void id_hashtable_set_permanent(IdHashTable *self, uint32 id, Bool permanent)
{
  IdHashTableEntry *entry, *prev ;
  uint32 hash ;

  if (find_entry(self, id, &entry, &prev, &hash)) {
    entry->private.permanent = permanent ;
  }
}

/* See header for doc. */
IdHashTableEntry* id_hashtable_find(IdHashTable *self, uint32 id)
{
  IdHashTableEntry *entry, *prev ;
  uint32 hash ;

  if (find_entry(self, id, &entry, &prev, &hash))
    return entry;
  else
    return NULL;
}

/* See header for doc. */
void id_hashtable_remove(IdHashTable *self, uint32 id)
{
  IdHashTableEntry *entry, *prev ;
  uint32 hash ;

  if (find_entry(self, id, &entry, &prev, &hash)) {
    unlink_entry(self, entry, prev, hash);
  }
}

/* See header for doc. */
void id_hashtable_kill_zombies(IdHashTable *self)
{
  IdHashTableEntry *curr ;

  curr = self->zombies ;
  while (curr != NULL) {
    self->zombies = curr->private.next ;
    self->entry_destructor(curr) ;
    curr = self->zombies ;
  }
}

/* See header for doc. */
void id_hashtable_remove_all(IdHashTable *self, Bool include_permanent)
{
  uint32 i ;
  IdHashTableEntry *curr, *next_entry, *prev ;

  HQASSERT(self != NULL, "self is NULL") ;

  for (i = 0; i < self->table_size; i ++) {
    curr = self->table[i] ;
    prev = NULL ;
    while (curr != NULL) {
      next_entry = curr->private.next ;

      if (include_permanent || ! curr->private.permanent) {
        unlink_entry(self, curr, prev, i);
      }
      else {
        prev = curr ;
      }

      curr = next_entry ;
    }
  }
}

/* See header for doc. */
Bool id_hashtable_empty(IdHashTable* self)
{
  uint32 i;

  if (self->zombies != NULL)
    return FALSE;

  for (i = 0; i < self->table_size; i ++) {
    if (self->table[i] != NULL)
      return FALSE;
  }

  return TRUE;
}

/* Log stripped */

