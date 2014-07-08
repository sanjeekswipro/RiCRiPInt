/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:resourcecache.c(EBDSDK_P.1) $
 * $Id: src:resourcecache.c,v 1.16.4.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief Implementation of our in RIP PCL5 resource cache. Holds
 * internal/downloaded macros and patterns.
 */

#include "core.h"
#include "resourcecache.h"
#include "fileio.h"
#include "hqmemcpy.h"
#include "hqmemcmp.h"

#include "pcl5context.h"
#include "pcl5context_private.h"
#include "factorypatterns.h"
#include "fontselection.h"
#include "macros.h"
#include "pagecontrol.h"

/* ============================================================================
 * String ID cache for macros
 * ============================================================================
 */

#define STRING_ID_CACHE_TABLE_SIZE 37

/* Because pcl5_resource is ALWAYS the first field of specfic resource
   type, we can simply use resource to check its type, id etc.. */
typedef struct PCL5StringIdCacheEntry {
  union {
    pcl5_resource detail ;
    pcl5_macro macro ;
    pcl5_font aliased_font ;
  } resource ;

  struct PCL5StringIdCacheEntry *next ;
} PCL5StringIdCacheEntry ;

struct PCL5StringIdCache {
  PCL5StringIdCacheEntry* table[STRING_ID_CACHE_TABLE_SIZE] ;
  uint32 table_size ;
} ;

void pcl5_cleanup_ID_string(pcl5_resource_string_id *string_id)
{
  HQASSERT(string_id != NULL, "string_id is NULL") ;

  if (string_id->buf != NULL) {
    mm_free(mm_pcl_pool, string_id->buf, string_id->length) ;
    string_id->buf = NULL ;
    string_id->length = 0 ;
  }
}

/* N.B. This expects an empty to_string buffer, (or the
 *      same one as the from_string buffer).
 */
Bool pcl5_copy_ID_string(pcl5_resource_string_id *to_string,
                         pcl5_resource_string_id *from_string)
{
  HQASSERT(to_string != NULL, "to_string is NULL") ;
  HQASSERT(from_string != NULL, "from_string is NULL") ;
  HQASSERT(to_string->buf == NULL || to_string->buf == from_string->buf,
           "to_string already exists") ;
  HQASSERT(to_string->length == 0 || to_string->length == from_string->length,
           "to_string already exists") ;

  if (from_string->buf != NULL) {
    if ((to_string->buf = mm_alloc(mm_pcl_pool, from_string->length,
                                    MM_ALLOC_CLASS_PCL_CONTEXT)) == NULL) {
      return FALSE ;
    }

    HqMemCpy(to_string->buf, from_string->buf, from_string->length) ;
    to_string->length = from_string->length ;
  }

  return TRUE ;
}


/* Gets called when one deletes the real macro. */
static void delete_macro_alias(PCL5StringIdCache *string_id_cache,
                               PCL5IdCache *id_cache, PCL5StringIdCacheEntry *entry,
                               PCL5StringIdCacheEntry **next_entry)
{
  HQASSERT(entry->resource.detail.resource_type == SW_PCL5_MACRO,
           "Incorrect resource type.") ;

  /* This is a macro, check to see if we have an alias which
     ought to be removed as well. */

  if (entry->resource.macro.alias != NULL) {
    HQASSERT(entry->resource.macro.alias->alias ==
             &entry->resource.macro, "Alias pointers appear to be corrupt") ;
    entry->resource.macro.alias->alias = NULL ;

    HQASSERT(entry->resource.macro.alias->detail.PCL5FreePrivateData == NULL,
             "Umm, free private data function pointer ought to be NULL.") ;

    /* If the alias is the next entry (when iterating over a hash
       chain), we need to update next_entry to the alias's next
       entry. */
    if (next_entry != NULL && &((*next_entry)->resource.macro) == entry->resource.macro.alias)
      *next_entry = entry->next ;

    if (entry->resource.macro.alias->detail.string_id.buf != NULL) {
      pcl5_string_id_cache_remove(string_id_cache,
                                  id_cache,
                                  entry->resource.macro.alias->detail.string_id.buf,
                                  entry->resource.macro.alias->detail.string_id.length,
                                  TRUE) ;
    } else {
      pcl5_id_cache_remove(id_cache,
                           entry->resource.macro.alias->detail.numeric_id,
                           TRUE) ;
    }
  }
}

static void string_free_entry(PCL5StringIdCache *string_id_cache,
                              PCL5IdCache *id_cache, PCL5StringIdCacheEntry *entry,
                              PCL5StringIdCacheEntry **next_entry)
{
  size_t size = sizeof(PCL5StringIdCacheEntry) + entry->resource.detail.string_id.length ;

  if (entry->resource.detail.private_data != NULL) {
    if (entry->resource.detail.PCL5FreePrivateData != NULL) {
      entry->resource.detail.PCL5FreePrivateData(entry->resource.detail.private_data) ;
      delete_macro_alias(string_id_cache, id_cache, entry, next_entry) ;
    } else {
      /* Its an alias macro. */
      HQASSERT(entry->resource.detail.resource_type == SW_PCL5_MACRO,
               "Incorrect resource type.") ;

      if (entry->resource.macro.alias != NULL)
        entry->resource.macro.alias->alias = NULL ;
    }
  }

  mm_free(mm_pcl_pool, entry, size) ;
}

static Bool pcl5_string_id_cache_find(PCL5StringIdCache *string_id_cache, uint8 *string, int32 length,
                                      PCL5StringIdCacheEntry **entry, PCL5StringIdCacheEntry **prev, uint32 *hash)
{
  PCL5StringIdCacheEntry *curr, *temp_prev = NULL ;

  HQASSERT(string_id_cache != NULL, "string_id_cache is NULL") ;
  HQASSERT(entry != NULL, "entry is NULL") ;
  HQASSERT(hash != NULL, "hash is NULL") ;
  HQASSERT(string != NULL, "string is NULL") ;
  HQASSERT(length > 0, "length is not greater than zero") ;

  /* Compute a hash on a string. This is an implementation of hashpjw
     without any branches in the loop. */
  {
/* Constants for PJW hash function. */
#define PJW_SHIFT        (4)            /* Per hashed char hash shift */
#define PJW_MASK         (0xf0000000u)  /* Mask for hash top bits */
#define PJW_RIGHT_SHIFT  (24)           /* Right shift distance for hash top bits */

    uint32 bits = 0 ;
    int32 i = length ;
    uint8 *p = string ;

    *hash = 0;
    while ( i-- > 0 ) {
      *hash = (*hash << PJW_SHIFT) + *p++ ;
      bits = *hash & PJW_MASK ;
      *hash ^= bits | (bits >> PJW_RIGHT_SHIFT) ;
    }

    *hash = *hash % STRING_ID_CACHE_TABLE_SIZE ;
  }

  *prev = NULL ;
  *entry = NULL ;

  for (curr = string_id_cache->table[*hash]; curr != NULL; curr = curr->next) {
    if (curr->resource.detail.string_id.length == length &&
        HqMemCmp(curr->resource.detail.string_id.buf, curr->resource.detail.string_id.length,
                 string, length) == 0) {
      *entry = curr ;
      *prev = temp_prev ;
      return TRUE ;
    }
    temp_prev = curr ;
  }

  return FALSE ;
}

void pcl5_string_id_cache_remove(PCL5StringIdCache *string_id_cache,
                                 PCL5IdCache *id_cache,  uint8 *string, int32 length, Bool associated_only)
{
  PCL5StringIdCacheEntry *entry, *prev ;
  uint32 hash ;

  HQASSERT(string_id_cache != NULL, "string_id_cache is NULL") ;
  HQASSERT(string != NULL, "string is NULL") ;
  HQASSERT(length > 0, "length is not greater than zero") ;

  if (pcl5_string_id_cache_find(string_id_cache, string, length, &entry, &prev, &hash)) {
    /* This is a bit of a hack. We just happen to know that if there
       is a deallocation function its not an associated entry in the
       hash. */
    if (associated_only && entry->resource.detail.PCL5FreePrivateData != NULL)
      return ;

    /* Unlink. */
    if (prev == NULL) {
      string_id_cache->table[hash] = entry->next ;
    } else {
      prev->next = entry->next ;
    }

    string_free_entry(string_id_cache, id_cache, entry, NULL) ;
  }
}

/* Remove all temporary entries from the passed cache. If
 * 'include_permanent' is TRUE, permanent entries will be removed too.
 */
void pcl5_string_id_cache_remove_all(PCL5StringIdCache *string_id_cache,
                                     PCL5IdCache *id_cache, Bool include_permanent)
{
  int32  i ;
  PCL5StringIdCacheEntry *curr, *next_entry, *prev ;

  HQASSERT(string_id_cache != NULL, "string_id_cache is NULL") ;

  /* If we really need a faster way to iterate over entires we should
     maintain a stack of them. */
  for (i=0; i<STRING_ID_CACHE_TABLE_SIZE; i++) {
    curr = string_id_cache->table[i] ;
    prev = NULL ;
    while (curr != NULL) {
      next_entry = curr->next ;

      if (include_permanent || ! curr->resource.detail.permanent) {
        if (prev == NULL) {
          string_id_cache->table[i] = curr->next ;
        } else {
          prev->next = curr->next ;
        }
        string_free_entry(string_id_cache, id_cache, curr, &next_entry) ;
      } else {
        prev = curr ;
      }

      curr = next_entry ;
    }
  }
}

pcl5_macro* pcl5_string_id_cache_get_macro(PCL5StringIdCache *string_id_cache, uint8 *string, int32 length)
{
  PCL5StringIdCacheEntry *entry, *prev ;
  uint32 hash ;

  HQASSERT(string_id_cache != NULL, "string_id_cache is NULL") ;
  HQASSERT(string != NULL, "string is NULL") ;
  HQASSERT(length > 0, "length is not greater than zero") ;

  if (pcl5_string_id_cache_find(string_id_cache, string, length, &entry, &prev, &hash))
    return &(entry->resource.macro) ;

  return NULL ;
}

pcl5_font* pcl5_string_id_cache_get_font(PCL5StringIdCache *string_id_cache, uint8 *string, int32 length)
{
  PCL5StringIdCacheEntry *entry, *prev ;
  uint32 hash ;

  HQASSERT(string_id_cache != NULL, "string_id_cache is NULL") ;
  HQASSERT(string != NULL, "string is NULL") ;
  HQASSERT(length > 0, "length is not greater than zero") ;

  if (pcl5_string_id_cache_find(string_id_cache, string, length, &entry, &prev, &hash))
    return &(entry->resource.aliased_font) ;

  return NULL ;
}

Bool pcl5_string_id_cache_insert_macro(PCL5StringIdCache *string_id_cache,
                                       PCL5IdCache *id_cache,
                                       uint8 *string, int32 length,
                                       pcl5_macro *macro, pcl5_macro **new_macro)
{
  PCL5StringIdCacheEntry *entry, *prev ;
  uint32 hash ;

  HQASSERT(string_id_cache != NULL, "string_id_cache is NULL") ;
  HQASSERT(string != NULL, "string is NULL") ;
  HQASSERT(length > 0, "length is not greater than zero") ;

  *new_macro = NULL ;

  if (pcl5_string_id_cache_find(string_id_cache, string, length, &entry, &prev, &hash)) {
    if (prev == NULL) {
      string_id_cache->table[hash] = entry->next ;
    } else {
      prev->next = entry->next ;
    }
    string_free_entry(string_id_cache, id_cache, entry, NULL) ;
  }

  if ((entry = mm_alloc(mm_pcl_pool, sizeof(PCL5StringIdCacheEntry) + length, MM_ALLOC_CLASS_PCL_CONTEXT)) == NULL) {
    return FALSE ;
  } else {
    entry->next = string_id_cache->table[hash] ;
    string_id_cache->table[hash] = entry ;
  }

  entry->resource.macro = *macro ;
  entry->resource.detail.resource_type = SW_PCL5_MACRO ;
  entry->resource.detail.string_id.buf = (uint8*)entry + sizeof(PCL5StringIdCacheEntry) ;
  entry->resource.detail.string_id.length = length ;
  HqMemCpy(entry->resource.detail.string_id.buf, string, length) ;
  entry->resource.macro.alias = NULL ;

  *new_macro = &(entry->resource.macro) ;
  return TRUE ;
}

Bool pcl5_string_id_cache_insert_font(PCL5StringIdCache *string_id_cache,
                                      PCL5IdCache *id_cache,
                                      uint8 *string, int32 length,
                                      pcl5_font *font, pcl5_font **new_font)
{
  PCL5StringIdCacheEntry *entry, *prev ;
  uint32 hash ;

  HQASSERT(string_id_cache != NULL, "string_id_cache is NULL") ;
  HQASSERT(string != NULL, "string is NULL") ;
  HQASSERT(length > 0, "length is not greater than zero") ;

  *new_font = NULL ;

  if (pcl5_string_id_cache_find(string_id_cache, string, length, &entry, &prev, &hash)) {
    if (prev == NULL) {
      string_id_cache->table[hash] = entry->next ;
    } else {
      prev->next = entry->next ;
    }
    string_free_entry(string_id_cache, id_cache, entry, NULL) ;
  }

  if ((entry = mm_alloc(mm_pcl_pool, sizeof(PCL5StringIdCacheEntry) + length, MM_ALLOC_CLASS_PCL_CONTEXT)) == NULL) {
    return FALSE ;
  } else {
    entry->next = string_id_cache->table[hash] ;
    string_id_cache->table[hash] = entry ;
  }

  entry->resource.aliased_font = *font ;
  entry->resource.detail.resource_type = SW_PCL5_FONT ;
  entry->resource.detail.string_id.buf = (uint8*)entry + sizeof(PCL5StringIdCacheEntry) ;
  entry->resource.detail.string_id.length = length ;
  HqMemCpy(entry->resource.detail.string_id.buf, string, length) ;
  entry->resource.aliased_font.alias = NULL ;

  *new_font = &(entry->resource.aliased_font) ;
  return TRUE ;
}

Bool pcl5_string_id_cache_insert_aliased_macro(PCL5StringIdCache *string_id_cache,
                                               PCL5IdCache *id_cache,
                                               pcl5_macro *orig_macro,
                                               uint8 *string, int32 length)
{
  pcl5_macro macro, *new_macro ;
  Bool success ;

  macro.detail.resource_type = SW_PCL5_MACRO ;
  macro.detail.numeric_id = 0 ;
  macro.detail.string_id.buf = NULL ;
  macro.detail.string_id.length = 0 ;
  macro.detail.permanent = orig_macro->detail.permanent ;
  macro.detail.device = NULL ;
  /* Point to the original macro data. */
  macro.detail.private_data = orig_macro->detail.private_data ;
  /* This is an alias name so no deallocation required. */
  macro.detail.PCL5FreePrivateData = NULL ;

  success = pcl5_string_id_cache_insert_macro(string_id_cache, id_cache, string, length,
                                              &macro, &new_macro) ;

  orig_macro->alias = new_macro ;
  new_macro->alias = orig_macro ;

  return success ;
}

Bool pcl5_string_id_cache_insert_aliased_font(PCL5StringIdCache *string_id_cache,
                                              PCL5IdCache *id_cache,
                                              pcl5_font *orig_font,
                                              uint8 *string, int32 length)
{
  pcl5_font font, *new_font ;
  Bool success ;

  font.detail.resource_type = SW_PCL5_FONT ;
  font.detail.numeric_id = 0 ;
  font.detail.string_id.buf = NULL ;
  font.detail.string_id.length = 0 ;
  font.detail.permanent = orig_font->detail.permanent ;
  font.detail.device = NULL ;
  /* Point to the original macro data. */
  font.detail.private_data = orig_font->detail.private_data ;
  /* This is an alias name so no deallocation required. */
  font.detail.PCL5FreePrivateData = NULL ;

  success = pcl5_string_id_cache_insert_font(string_id_cache, id_cache, string, length,
                                             &font, &new_font) ;
  /*
  orig_font->alias = new_font ;
  */
  new_font->alias = orig_font ;

  return success ;
}

void pcl5_string_id_cache_set_permanent(PCL5StringIdCache *string_id_cache, uint8 *string, int32 length, Bool permanent)
{
  PCL5StringIdCacheEntry *entry, *prev ;
  uint32 hash ;

  if (pcl5_string_id_cache_find(string_id_cache, string, length, &entry, &prev, &hash)) {
    entry->resource.detail.permanent = permanent ;

    /* If the macro has an alias, make sure the permanent state is
       reflected in alias as well. */
    if (entry->resource.detail.resource_type == SW_PCL5_MACRO) {
      if (entry->resource.macro.alias != NULL) {
        entry->resource.macro.alias->detail.permanent = permanent ;
      }
    }

  }
}

static
Bool pcl5_string_id_cache_create(PCL5StringIdCache **string_id_cache, uint32 table_size)
{
  PCL5StringIdCache *new_string_id_cache ;
  int32 i ;

  UNUSED_PARAM(uint32, table_size) ;
  HQASSERT(string_id_cache != NULL, "string_id_cache is NULL") ;

  if ((new_string_id_cache = mm_alloc(mm_pcl_pool, sizeof(PCL5StringIdCache), MM_ALLOC_CLASS_PCL_CONTEXT)) == NULL) {
    *string_id_cache = NULL ;
    return FALSE ;
  }
  for (i=0; i<STRING_ID_CACHE_TABLE_SIZE; i++) {
    new_string_id_cache->table[i] = NULL ;
  }

#if 0
  TBD
  new_string_id_cache->table_size = table_size ;
#else
  new_string_id_cache->table_size = STRING_ID_CACHE_TABLE_SIZE ;
#endif

  *string_id_cache = new_string_id_cache ;

  return TRUE ;
}

void pcl5_string_id_cache_destroy(PCL5StringIdCache **string_id_cache,
                                  PCL5IdCache *id_cache)
{
  int32  i ;
  PCL5StringIdCacheEntry *curr, *next_entry ;
  HQASSERT(string_id_cache != NULL, "string_id_cache is NULL") ;
  HQASSERT(*string_id_cache != NULL, "*string_id_cache is NULL") ;

  /* If we really need a faster way to iterate over entires we should
     maintain a stack of them. */
  for (i=0; i<STRING_ID_CACHE_TABLE_SIZE; i++) {
    curr = (*string_id_cache)->table[i] ;
    while (curr != NULL) {
      next_entry = curr->next ;
      string_free_entry(*string_id_cache, id_cache, curr, NULL) ;
      curr = next_entry ;
    }
  }

  mm_free(mm_pcl_pool, *string_id_cache, sizeof(PCL5StringIdCache)) ;
  *string_id_cache = NULL ;
}

/* ============================================================================
 * Numeric ID cache for patterns and macros
 * ============================================================================
 */

#define ID_CACHE_TABLE_SIZE 37

/* Because pcl5_resource is ALWAYS the first field of specfic resource
   type, we can simply use resource to check its type, id etc.. */
struct PCL5IdCacheEntry {
  union {
    pcl5_resource detail ;
    pcl5_pattern pattern ;
    pcl5_macro macro ;
    pcl5_font aliased_font ;
  } resource ;

  struct PCL5IdCacheEntry *next ;
} ;

struct PCL5IdCache {
  PCL5IdCacheEntry* table[ID_CACHE_TABLE_SIZE] ;
  PCL5IdCacheEntry* zombies ;
  uint32 table_size ;
} ;

/**
 * Free the raster data in the passed pattern.
 */
static void free_pattern_data(pcl5_pattern *pattern)
{
  if (pattern->data != NULL) {
    uint32 size = pcl5_id_cache_pattern_data_size(pattern->width,
                                                  pattern->height,
                                                  pattern->bits_per_pixel) ;
    mm_free(mm_pcl_pool, pattern->data, size) ;
    pattern->data = NULL;
  }
}

static void free_entry(PCL5IdCacheEntry *entry)
{
  if (entry->resource.detail.resource_type == SW_PCL5_PATTERN) {
    free_pattern_data(&entry->resource.pattern);
  }

  if (entry->resource.detail.private_data != NULL) {
    if (entry->resource.detail.PCL5FreePrivateData != NULL)
      entry->resource.detail.PCL5FreePrivateData(entry->resource.detail.private_data) ;
  }

  mm_free(mm_pcl_pool, entry, sizeof(PCL5IdCacheEntry)) ;
}

static Bool pcl5_id_cache_find(PCL5IdCache *id_cache, int16 id, PCL5IdCacheEntry **entry,
                               PCL5IdCacheEntry **prev, uint32 *hash)
{
  PCL5IdCacheEntry *curr, *temp_prev = NULL ;

  HQASSERT(id_cache != NULL, "id_cache is NULL") ;
  HQASSERT(entry != NULL, "entry is NULL") ;
  HQASSERT(hash != NULL, "hash is NULL") ;

  *hash = id % ID_CACHE_TABLE_SIZE ;
  *prev = NULL ;
  *entry = NULL ;

  for (curr = id_cache->table[*hash]; curr != NULL; curr = curr->next) {
    if (curr->resource.detail.numeric_id == id) {
      *entry = curr ;
      *prev = temp_prev ;
      return TRUE ;
    }
    temp_prev = curr ;
  }

  return FALSE ;
}

void make_zombie(PCL5IdCache *id_cache, PCL5IdCacheEntry *entry)
{
  HQASSERT(id_cache != NULL, "id_cache is NULL") ;
  HQASSERT(entry != NULL, "entry is NULL") ;

  entry->next = id_cache->zombies ;
  id_cache->zombies = entry ;
  if (entry->resource.detail.resource_type == SW_PCL5_MACRO) {
    if (entry->resource.macro.alias != NULL) {
      entry->resource.macro.alias->alias = NULL ;
      entry->resource.macro.alias = NULL ;
    }
  }
}

void pcl5_id_cache_remove(PCL5IdCache *id_cache, int16 id, Bool associated_only)
{
  PCL5IdCacheEntry *entry, *prev ;
  uint32 hash ;

  if (pcl5_id_cache_find(id_cache, id, &entry, &prev, &hash)) {
    /* This is a bit of a hack. We just happen to know that if there
       is a deallocation function its not an associated entry in the
       hash. */
    if (associated_only && entry->resource.detail.PCL5FreePrivateData != NULL)
      return ;

    /* Unlink. */
    if (prev == NULL) {
      id_cache->table[hash] = entry->next ;
    } else {
      prev->next = entry->next ;
    }
    make_zombie(id_cache, entry) ;
  }
}

void pcl5_id_cache_kill_zombies(PCL5IdCache *id_cache)
{
  PCL5IdCacheEntry *curr ;

  curr = id_cache->zombies ;
  while (curr != NULL) {
    id_cache->zombies = curr->next ;
    free_entry(curr) ;
    curr = id_cache->zombies ;
  }
}

/* Remove all temporary entries from the passed cache. If
 * 'include_permanent' is TRUE, permanent entries will be removed too.
 */
void pcl5_id_cache_remove_all(PCL5IdCache *id_cache, Bool include_permanent)
{
  int32  i ;
  PCL5IdCacheEntry *curr, *next_entry, *prev ;
  HQASSERT(id_cache != NULL, "id_cache is NULL") ;

  /* If we really need a faster way to iterate over entires we should
     maintain a stack of them. */
  for (i=0; i<ID_CACHE_TABLE_SIZE; i++) {
    curr = id_cache->table[i] ;
    prev = NULL ;
    while (curr != NULL) {
      next_entry = curr->next ;

      if (include_permanent || ! curr->resource.detail.permanent) {
        if (prev == NULL) {
          id_cache->table[i] = curr->next ;
        } else {
          prev->next = curr->next ;
        }
        make_zombie(id_cache, curr) ;
      } else {
        prev = curr ;
      }

      curr = next_entry ;
    }
  }
}

Bool pcl5_id_cache_insert_pattern(PCL5IdCache *id_cache, int16 id,
                                  pcl5_pattern *pattern,
                                  pcl5_pattern **new_pattern)
{
  PCL5IdCacheEntry *entry, *prev ;
  uint32 hash ;
  int32 data_size ;
  uint8* pattern_data ;

  *new_pattern = NULL ;

  if (pcl5_id_cache_find(id_cache, id, &entry, &prev, &hash)) {
    if (prev == NULL) {
      id_cache->table[hash] = entry->next ;
    } else {
      prev->next = entry->next ;
    }
    make_zombie(id_cache, entry) ;
  }

  entry = mm_alloc(mm_pcl_pool, sizeof(PCL5IdCacheEntry),
                   MM_ALLOC_CLASS_PCL_CONTEXT);
  if (entry == NULL)
    return FALSE;

  /* Allocate memory for the pattern data; this will be populated by the client. */
  data_size = pcl5_id_cache_pattern_data_size(pattern->width, pattern->height,
                                              pattern->bits_per_pixel) ;
  pattern_data = mm_alloc(mm_pcl_pool, data_size, MM_ALLOC_CLASS_PCL_CONTEXT) ;
  if (pattern_data == NULL) {
    mm_free(mm_pcl_pool, entry, sizeof(PCL5IdCacheEntry)) ;
    return FALSE ;
  }

  entry->next = id_cache->table[hash] ;
  id_cache->table[hash] = entry ;

  entry->resource.pattern = *pattern ;

  HQASSERT(entry->resource.detail.resource_type == SW_PCL5_PATTERN &&
           entry->resource.detail.private_data == NULL &&
           entry->resource.detail.PCL5FreePrivateData == NULL,
           "Cache entry incorrectly configured.");

  entry->resource.pattern.data = pattern_data;
  /* Copy the original pattern data in if present. */
  if (pattern->data != NULL)
    HqMemCpy(pattern_data, pattern->data, data_size);

  *new_pattern = &(entry->resource.pattern) ;
  return TRUE ;
}

/* See header for doc. */
void pcl5_id_cache_release_pattern_data(PCL5IdCache *id_cache, int16 id)
{
  pcl5_pattern* pattern = pcl5_id_cache_get_pattern(id_cache, id);
  if (pattern != NULL) {
    free_pattern_data(pattern);
  }
}

pcl5_pattern* pcl5_id_cache_get_pattern(PCL5IdCache *id_cache, int16 id)
{
  PCL5IdCacheEntry *entry, *prev ;
  uint32 hash ;

  if (pcl5_id_cache_find(id_cache, id, &entry, &prev, &hash))
    return &(entry->resource.pattern) ;

  return NULL ;
}

pcl5_macro* pcl5_id_cache_get_macro(PCL5IdCache *id_cache, int16 id)
{
  PCL5IdCacheEntry *entry, *prev ;
  uint32 hash ;

  if (pcl5_id_cache_find(id_cache, id, &entry, &prev, &hash))
    return &(entry->resource.macro) ;

  return NULL ;
}

pcl5_font* pcl5_id_cache_get_font(PCL5IdCache *id_cache, int16 id)
{
  PCL5IdCacheEntry *entry, *prev ;
  uint32 hash ;

  if (pcl5_id_cache_find(id_cache, id, &entry, &prev, &hash))
    return &(entry->resource.aliased_font) ;

  return NULL ;
}

Bool pcl5_id_cache_insert_macro(PCL5IdCache *id_cache, int16 id, pcl5_macro *macro, pcl5_macro **new_macro)
{
  PCL5IdCacheEntry *entry, *prev ;
  uint32 hash ;

  *new_macro = NULL ;

  if (pcl5_id_cache_find(id_cache, id, &entry, &prev, &hash)) {
    if (prev == NULL) {
      id_cache->table[hash] = entry->next ;
    } else {
      prev->next = entry->next ;
    }
    make_zombie(id_cache, entry) ;
  }

  if ((entry = mm_alloc(mm_pcl_pool, sizeof(PCL5IdCacheEntry), MM_ALLOC_CLASS_PCL_CONTEXT)) == NULL) {
    return FALSE ;
  } else {
    entry->next = id_cache->table[hash] ;
    id_cache->table[hash] = entry ;
  }

  entry->resource.macro = *macro ;
  entry->resource.detail.resource_type = SW_PCL5_MACRO ;
  entry->resource.macro.alias = NULL ;

  *new_macro = &(entry->resource.macro) ;
  return TRUE ;
}

Bool pcl5_id_cache_insert_aliased_macro(PCL5IdCache *id_cache, pcl5_macro *orig_macro, int16 id)
{
  pcl5_macro macro, *new_macro ;
  Bool success ;

  macro.detail.resource_type = SW_PCL5_MACRO ;
  macro.detail.numeric_id = 0 ;
  macro.detail.string_id.buf = NULL ;
  macro.detail.string_id.length = 0 ;
  macro.detail.permanent = orig_macro->detail.permanent ;
  macro.detail.device = NULL ;
  /* Point to the original macro data. */
  macro.detail.private_data = orig_macro->detail.private_data ;
  /* This is an alias name so no deallocation required. */
  macro.detail.PCL5FreePrivateData = NULL ;

  success = pcl5_id_cache_insert_macro(id_cache, id, &macro, &new_macro) ;

  orig_macro->alias = new_macro ;
  new_macro->alias = orig_macro ;

  return success ;
}

Bool pcl5_id_cache_insert_font(PCL5IdCache *id_cache, int16 id, pcl5_font *font, pcl5_font **new_font)
{
  PCL5IdCacheEntry *entry, *prev ;
  uint32 hash ;

  *new_font = NULL ;

  if (pcl5_id_cache_find(id_cache, id, &entry, &prev, &hash)) {
    if (prev == NULL) {
      id_cache->table[hash] = entry->next ;
    } else {
      prev->next = entry->next ;
    }
    make_zombie(id_cache, entry) ;
  }

  if ((entry = mm_alloc(mm_pcl_pool, sizeof(PCL5IdCacheEntry), MM_ALLOC_CLASS_PCL_CONTEXT)) == NULL) {
    return FALSE ;
  } else {
    entry->next = id_cache->table[hash] ;
    id_cache->table[hash] = entry ;
  }

  entry->resource.aliased_font = *font ;
  entry->resource.detail.resource_type = SW_PCL5_FONT ;
  entry->resource.aliased_font.alias = NULL ;

  *new_font = &(entry->resource.aliased_font) ;
  return TRUE ;
}

Bool pcl5_id_cache_insert_aliased_font(PCL5IdCache *id_cache, pcl5_font *orig_font, int16 id)
{
  pcl5_font font, *new_font ;
  Bool success ;

  font.detail.resource_type = SW_PCL5_FONT ;
  font.detail.numeric_id = id ;
  font.detail.string_id.buf = NULL ;
  font.detail.string_id.length = 0 ;
  font.detail.permanent = orig_font->detail.permanent ;
  font.detail.device = NULL ;
  /* Point to the original macro data. */
  font.detail.private_data = orig_font->detail.private_data ;
  /* This is an alias name so no deallocation required. */
  font.detail.PCL5FreePrivateData = NULL ;

  success = pcl5_id_cache_insert_font(id_cache, id, &font, &new_font) ;

  /*
  orig_font->alias = new_font ;
  */
  new_font->alias = orig_font ;

  return success ;
}

uint32 pcl5_id_cache_pattern_data_size(int32 width, int32 height, int32 bitsPerPixel)
{
  HQASSERT(bitsPerPixel == 1 || bitsPerPixel == 8, "Invalid bitsPerPixel.");
  if (bitsPerPixel == 1)
    return ((width + 7) / 8) * height;
  else
    return width * height;
}

void pcl5_id_cache_set_permanent(PCL5IdCache *id_cache, int16 id, Bool permanent)
{
  PCL5IdCacheEntry *entry, *prev ;
  uint32 hash ;

  if (pcl5_id_cache_find(id_cache, id, &entry, &prev, &hash)) {
    entry->resource.detail.permanent = permanent ;

    /* If the macro has an alias, make sure the permanent state is
       reflected in alias as well. */
    if (entry->resource.detail.resource_type == SW_PCL5_MACRO) {
      if (entry->resource.macro.alias != NULL) {
        entry->resource.macro.alias->detail.permanent = permanent ;
      }
    }

  }
}

static
Bool pcl5_id_cache_create(PCL5IdCache **id_cache, uint32 table_size)
{
  PCL5IdCache *new_id_cache ;
  int32 i ;

  UNUSED_PARAM(uint32, table_size) ;
  HQASSERT(id_cache != NULL, "id_cache is NULL") ;

  if ((new_id_cache = mm_alloc(mm_pcl_pool, sizeof(PCL5IdCache), MM_ALLOC_CLASS_PCL_CONTEXT)) == NULL) {
    *id_cache = NULL ;
    return FALSE ;
  }
  for (i=0; i<ID_CACHE_TABLE_SIZE; i++) {
    new_id_cache->table[i] = NULL ;
  }

  new_id_cache->zombies = NULL ;
#if 0
  TBD
  new_id_cache->table_size = table_size ;
#else
  new_id_cache->table_size = ID_CACHE_TABLE_SIZE ;
#endif

  *id_cache = new_id_cache ;

  return TRUE ;
}

void pcl5_id_cache_destroy(PCL5IdCache **id_cache)
{
  int32  i ;
  PCL5IdCacheEntry *curr, *next_entry ;
  HQASSERT(id_cache != NULL, "id_cache is NULL") ;
  HQASSERT(*id_cache != NULL, "*id_cache is NULL") ;

  pcl5_id_cache_kill_zombies(*id_cache) ;

  /* If we really need a faster way to iterate over entires we should
     maintain a stack of them. */
  for (i=0; i<ID_CACHE_TABLE_SIZE; i++) {
    curr = (*id_cache)->table[i] ;
    while (curr != NULL) {
      next_entry = curr->next ;
      free_entry(curr) ;
      curr = next_entry ;
    }
  }

  mm_free(mm_pcl_pool, *id_cache, sizeof(PCL5IdCache)) ;
  *id_cache = NULL ;
}

/* See header for doc. */
void pcl5_id_cache_start_interation(PCL5IdCache* id_cache,
                                    PCL5IdCacheIterator* iterator)
{
  iterator->cache = id_cache;
  iterator->index = -1;
  iterator->entry = NULL;
}

/* See header for doc. */
pcl5_resource* pcl5_id_cache_iterate(PCL5IdCacheIterator* iterator)
{
  PCL5IdCache* cache = iterator->cache;

  /* Move on to the next entry in the linked list. */
  if (iterator->entry != NULL)
    iterator->entry = iterator->entry->next;

  /* If the entry is null, find the next occupied table entry. */
  if (iterator->entry == NULL) {
    do {
      iterator->index ++;
    } while (CAST_SIGNED_TO_UINT32(iterator->index) < cache->table_size &&
             cache->table[iterator->index] == NULL);

    if (CAST_SIGNED_TO_UINT32(iterator->index) < cache->table_size)
      iterator->entry = cache->table[iterator->index];
  }

  return &iterator->entry->resource.detail;
}

/* ============================================================================
 * Init/Finish PCL5 resource caches.
 * ============================================================================
 */
void pcl5_resource_caches_finish(PCL5_RIP_LifeTime_Context *pcl5_rip_context)
{
  HQASSERT(pcl5_rip_context != NULL, "pcl5_rip_context is NULL") ;

  destroy_pattern_caches(pcl5_rip_context) ;

  if (pcl5_rip_context->resource_caches.shading != NULL)
    pcl5_id_cache_destroy(&(pcl5_rip_context->resource_caches.shading)) ;

  if (pcl5_rip_context->resource_caches.cross_hatch != NULL)
    pcl5_id_cache_destroy(&(pcl5_rip_context->resource_caches.cross_hatch)) ;

  if (pcl5_rip_context->resource_caches.user != NULL)
    pcl5_id_cache_destroy(&(pcl5_rip_context->resource_caches.user)) ;

  if (pcl5_rip_context->resource_caches.hpgl2_user != NULL)
    pcl5_id_cache_destroy(&(pcl5_rip_context->resource_caches.hpgl2_user)) ;

  if (pcl5_rip_context->resource_caches.aliased_string_font != NULL)
    pcl5_string_id_cache_destroy(&(pcl5_rip_context->resource_caches.aliased_string_font),
                                 pcl5_rip_context->resource_caches.aliased_font) ;

  if (pcl5_rip_context->resource_caches.aliased_font != NULL)
    pcl5_id_cache_destroy(&(pcl5_rip_context->resource_caches.aliased_font)) ;

  if (pcl5_rip_context->resource_caches.string_macro != NULL)
    pcl5_string_id_cache_destroy(&(pcl5_rip_context->resource_caches.string_macro),
                                 pcl5_rip_context->resource_caches.macro) ;

  if (pcl5_rip_context->resource_caches.macro != NULL)
    pcl5_id_cache_destroy(&(pcl5_rip_context->resource_caches.macro)) ;
}

Bool pcl5_resource_caches_init(PCL5_RIP_LifeTime_Context *pcl5_rip_context)
{

  HQASSERT(pcl5_rip_context != NULL, "pcl5_rip_context is NULL") ;

  pcl5_rip_context->resource_caches.shading = NULL ;
  pcl5_rip_context->resource_caches.cross_hatch = NULL ;
  pcl5_rip_context->resource_caches.user = NULL ;
  pcl5_rip_context->resource_caches.hpgl2_user = NULL ;
  pcl5_rip_context->resource_caches.macro = NULL ;
  pcl5_rip_context->resource_caches.string_macro = NULL ;
  pcl5_rip_context->resource_caches.aliased_font = NULL ;
  pcl5_rip_context->resource_caches.aliased_string_font = NULL ;

  if (! pcl5_id_cache_create(&(pcl5_rip_context->resource_caches.shading), 10))
    goto cleanup ;
  if (! pcl5_id_cache_create(&(pcl5_rip_context->resource_caches.cross_hatch), 6))
    goto cleanup ;
  if (! pcl5_id_cache_create(&(pcl5_rip_context->resource_caches.user), 41))
    goto cleanup ;
  if (! pcl5_id_cache_create(&(pcl5_rip_context->resource_caches.hpgl2_user), 8))
    goto cleanup ;
  if (! pcl5_id_cache_create(&(pcl5_rip_context->resource_caches.macro), 37))
    goto cleanup ;
  if (! pcl5_string_id_cache_create(&(pcl5_rip_context->resource_caches.string_macro), 37))
    goto cleanup ;
  if (! pcl5_id_cache_create(&(pcl5_rip_context->resource_caches.aliased_font), 37))
    goto cleanup ;
  if (! pcl5_string_id_cache_create(&(pcl5_rip_context->resource_caches.aliased_string_font), 37))
    goto cleanup ;

  if (! init_pattern_caches(pcl5_rip_context))
    goto cleanup ;

  return TRUE ;

 cleanup:
  pcl5_resource_caches_finish(pcl5_rip_context) ;

  return FALSE ;
}

/* ============================================================================
 * Operator callbacks are below here.
 * ============================================================================
 */

#define MAX_STRING_ID_LENGTH 511

/* Alphanumeric ID command. */
Bool pcl5op_ampersand_n_W(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  uint8 string_id[MAX_STRING_ID_LENGTH] ;
  int32 string_length ;
  int32 num_bytes ;
  int32 operation ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* Max length is 512 bytes, not 32K as normal! */
  num_bytes = min(value.integer, MAX_STRING_ID_LENGTH + 1);

  /* Data comprises operator byte and possible string */
  if ( num_bytes == 0 ) {
    return(TRUE);
  }
  if ((operation = Getc(pcl5_ctxt->flptr)) == EOF)
    return FALSE ;
  num_bytes-- ;
  string_length = num_bytes;
  if ( num_bytes > 0 ) {
    if ( file_read(pcl5_ctxt->flptr, string_id, string_length, NULL) <= 0 ) {
      return(FALSE);
    }
  }

  /* See test QL 5e CET 20.04. Seems string lengths are limited to 253
     characters, not 511. Not only that, but it appears that the
     command is ignored if the string length is greater than 253
     characters. */
  if (string_length > 253)
    return TRUE ;

  /* The only time a string length of zero is allowed is for operators 100, 21 or 20. */
  if (string_length == 0 && operation != 100 && operation != 21 && operation != 20)
    return TRUE ;

  /* We have the string length and the string itself. */
  switch (operation) {
  case 0: /* Sets the current Font ID to the given String ID. */
    if (! set_font_string_id(pcl5_ctxt, string_id, string_length))
      return FALSE ;
    break ;
  case 1: /* Associates the current Font ID to the font with the String ID
             supplied. */
    if (! associate_font_string_id(pcl5_ctxt, string_id, string_length))
      return FALSE ;
    break ;
  case 2: /* Selects the font referred to by the String ID as primary. */
    if (! select_font_string_id_as_primary(pcl5_ctxt, string_id, string_length))
      return FALSE ;
    break ;
  case 3: /* Selects the font referred to by the String ID as secondary. */
    if (! select_font_string_id_as_secondary(pcl5_ctxt, string_id, string_length))
      return FALSE ;
    break ;
  case 4: /* Sets the current Macro ID to the String ID. */
    if (! set_macro_string_id(pcl5_ctxt, string_id, string_length))
      return FALSE ;
    break ;
  case 5: /* Associates the current Macro ID to the supplied String ID. */
    if (! associate_macro_string_id(pcl5_ctxt, string_id, string_length))
      return FALSE ;
    break ;
  case 20: /* Deletes the font association named by the current Font ID. */
    if (! delete_font_associated_string_id(pcl5_ctxt))
      return FALSE ;
    break ;
  case 21: /* Deletes the macro association named by the current Macro ID. */
    if (! delete_macro_associated_string_id(pcl5_ctxt))
      return FALSE ;
    break ;
  case 100: /* Media select (see media selection table). */
    if (pcl5_ctxt->pass_through_mode != PCLXL_SNIPPET_JOB_PASS_THROUGH) {
      /* N.B. If no data is supplied in the alphanumeric command this seems
       * equivalent to asking for media type 0 or Plain.
       */
      if (string_length > 0) {
        if (! set_media_type_from_alpha(pcl5_ctxt, string_id, string_length))
          return FALSE ;
      } else {
        if (! set_media_type(pcl5_ctxt, 0))
          return FALSE ;
      }
    }
    break ;
  }

  return TRUE ;
}

void reset_aliased_fonts(PCL5_RIP_LifeTime_Context *pcl5_rip_context)
{
  HQASSERT(pcl5_rip_context != NULL, "pcl5_rip_context is NULL") ;

  pcl5_string_id_cache_remove_all(pcl5_rip_context->resource_caches.aliased_string_font,
                                  pcl5_rip_context->resource_caches.aliased_font,
                                  TRUE /* Include permanent? */) ;

  pcl5_id_cache_remove_all(pcl5_rip_context->resource_caches.aliased_font,
                           TRUE /* Include permanent? */) ;
  pcl5_id_cache_kill_zombies(pcl5_rip_context->resource_caches.aliased_font) ;
}


/* ============================================================================
* Log stripped */
