/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!export:xmlstrhash.h(EBDSDK_P.1) $
 * $Id: export:xmlstrhash.h,v 1.11.10.1.1.1 2013/12/19 11:25:08 anon Exp $
 *
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Generic hash function where the key is a NULL terminated string.
 *
 * Key is a string. User payload is (void *). Has plugable memory management
 * and hash function.
 *
 * \todo Possibly promote this data type to core types.
 */

#ifndef __XMLSTRHASH_H__
#define __XMLSTRHASH_H__

typedef struct XmlStrHashTable XmlStrHashTable;

typedef
/*@null@*/
void * (*XmlStrHashMallocFunct)(
      size_t size);

typedef
void (*XmlStrHashFreeFunct)(
      /*@only@*/ /*@out@*/ /*@null@*/
      void *memPtr);

typedef
void (*XmlStrHashPayloadFreeFunct)(
      /*@only@*/ /*@out@*/ /*@null@*/
      void *memPtr);

typedef
uint32 (*XmlStrHash)(
      /*@in@*/ /*@notnull@*/
      const uint8 *str,
      uint32 strlen);

/**
 * \brief Create a hash table.
 * \param size Size of the hash table.
 * \param fPayloadFree Pointer to payload free function. This function gets
 * called for every entry in the hash table when the hash table gets destroyed.
 * \param fMalloc Memory allocation function. An error will occur if this is
 * NULL.
 * \param fFree Memory de-allocation function. An error will occur if this is
 * NULL.
 * \param fHash String hash function. If this parameter is NULL, a PJW hashing
 * function will be used.
 * \returns A pointer to the created hash table or NULL if an error occured.
 */
extern
/*@owned@*/ /*@null@*/
XmlStrHashTable *xmlstr_hash_create(
      uint32 size,
      XmlStrHashPayloadFreeFunct fPayloadFree,
      XmlStrHashMallocFunct fMalloc,
      XmlStrHashFreeFunct fFree,
      XmlStrHash fHash);

/**
 * \brief Destroy a hash table freeing all memory.
 *
 * Will call the provided payload free function on each entry in the hash
 * table.
 *
 * \param table A handle to a valid hash table pointer.
 */
extern
void xmlstr_hash_destroy(
      /*@owned@*/ /*@notnull@*/ /*@in@*/
      XmlStrHashTable **table);

/**
 * \brief Get the hash entry for the provided key.
 * \param table Pointer to a valid hash table.
 * \param key Pointer to NULL terminated string - the key.
 * \param keylen Length of key.
 * \param value Handle to a void pointer. This gets set to the payload for the
 * given key.
 *
 * \note Hash values can be NULL, hence a Bool return.
 * \retval FALSE The key did not exist.
 * \retval TRUE The key did exist.
 */
extern
Bool xmlstr_hash_get(
      /*@notnull@*/ /*@in@*/
      XmlStrHashTable *table,
      /*@null@*/ /*@in@*/
      const uint8 *key,
      uint32 keylen,
      /*@notnull@*/ /*@out@*/
      void **value);

/**
 * \brief Add new entry into hash table.
 * \param table Pointer to a valid hash table.
 * \param key Pointer to NULL terminated string - the key.
 * \param keylen Length of key.
 * \param value Pointer to object to be inserted into the hash table - the
 * payload.
 * \param old_value Will be set to the address of an existing entry if it
 * existed.
 *
 * Adding a new entry Will clobber the old entry if it already exists.
 * old_value is set to either NULL or the memory address of the object
 * which existed in that slot.
 *
 * \retval TRUE on success
 * \retval FALSE on failure, which means that memory could not be allocated
 * for the new entry.
 */
extern
Bool xmlstr_hash_add(
      /*@notnull@*/ /*@in@*/
      XmlStrHashTable *table,
      /*@null@*/ /*@in@*/
      const uint8 *key,
      uint32 keylen,
      /*@null@*/ /*@in@*/
      void *value,
      /*@notnull@*/ /*@out@*/
      void **old_value);

/**
 * \brief Remove an entry from the hash table.
 * \param table Pointer to a valid hash table.
 * \param key Pointer to NULL terminated string.
 * \param keylen Length of key.
 *
 * Remove an entry from the hash table and returns a pointer to the user data
 * in the payload.
 */
extern void *xmlstr_hash_remove(
      /*@notnull@*/ /*@in@*/
      XmlStrHashTable *table,
      /*@null@*/ /*@in@*/
      const uint8 *key,
      uint32 keylen);

/* ============================================================================
* Log stripped */
#endif /*!__XMLSTRHASH_H__*/
