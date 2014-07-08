/** \file
 * \ingroup types
 *
 * $HopeName: COREtypes!export:idhashtable.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * The IdHashTable is a hash table where entries are accessed by unique numeric
 * identifier. Duplicate entries are not allowed; inserting a new entry with the
 * same ID as an existing entry will override the existing entry.
 *
 * When explicitly removing entries, or overwriting them with a new entry with
 * the same ID, the dead entry will be either immediately destroyed or added to
 * a list of 'zombie' entries (depending on the 'destroy_on_remove' parameter
 * passed to the contructor); in the latter case the 'zombie' entries will not
 * be deallocated until an explicit call to kill the zombie entries is made, or
 * the hash table is destroyed.
 */
#ifndef _idhashtable_h_
#define _idhashtable_h_

#include "mm.h"

typedef struct IdHashTableEntry IdHashTableEntry;

/**
 * Internal entry management structure; this should not be considered part of
 * the public API.
 */
typedef struct {
  uint32 id;
  Bool permanent;
  IdHashTableEntry *next;
} IdHashTableEntryPrivate;

/**
 * Table entry structure.
 */
struct IdHashTableEntry {
  /* Internal management structure. */
  IdHashTableEntryPrivate private;

  /* User data field. */
  void* data;
};

/**
 * Destructor method; required to allow the table to delete it's entries.
 */
typedef void (IdHashTableEntryDestructor)(IdHashTableEntry* entry);

/**
 * Id Hash Table structure.
 */
typedef struct IdHashTable {
  IdHashTableEntry** table;
  IdHashTableEntry* zombies;

  IdHashTableEntryDestructor* entry_destructor;

  mm_pool_t pool;
  uint32 table_size;
  Bool destroy_on_remove;
} IdHashTable;


/**
 * Constructor.
 * \param destroy_on_remove When TRUE, entries are destroyed (using the passed
 *        destructor) when they are removed. When FALSE, removed entries are
 *        added to a list of 'zombies', where they persist until the zombie list
 *        is explicitly purged (see id_hashtable_kill_zombies()) or the table
 *        itself is destroyed.
 * \param entry_destructor This function will be called when an entry should be
 *        destroyed.
 */
IdHashTable* id_hashtable_create(mm_pool_t pool,
                                 uint32 table_size, Bool destroy_on_remove,
                                 IdHashTableEntryDestructor* entry_destructor);

/**
 * Destructor. All table contents (including any zombies) will be destroyed.
 */
void id_hashtable_destroy(IdHashTable **self_pointer);

/**
 * Insert the passed entry using the specified Id. The 'data' field of the
 * passed entry should be initialised by the client. Once inserted the entry
 * is owned by the table; it will manage it's lifecycle, calling the table's
 * entry destructor as required.
 *
 * The entry will be initially set to non-permanent.
 */
void id_hashtable_insert(IdHashTable* self,
                         IdHashTableEntry* new_entry, uint32 id);

/**
 * Set the permanence of an entry; this affects how entries are treated during
 * a call to id_hashtable_remove_all().
 */
void id_hashtable_set_permanent(IdHashTable *self, uint32 id, Bool permanent);

/**
 * Find an entry.  Returns NULL if no entry exists with the given id.
 */
IdHashTableEntry* id_hashtable_find(IdHashTable *self, uint32 id);

/**
 * Removed an entry. Depending on the table policy (specified to the
 * constructor), the removed entry will be deleted or added to the zombie list.
 */
void id_hashtable_remove(IdHashTable *self, uint32 id);

/**
 * Destroy any zombies entries.
 */
void id_hashtable_kill_zombies(IdHashTable *self);

/**
 * Remove all temporary entries from the passed cache. If
 * 'include_permanent' is TRUE, permanent entries will be removed too.
 */
void id_hashtable_remove_all(IdHashTable *self, Bool include_permanent);

/**
 * Returns true if the table is empty of live and zombie entries.
 */
Bool id_hashtable_empty(IdHashTable* self);

#endif

/* Log stripped */

