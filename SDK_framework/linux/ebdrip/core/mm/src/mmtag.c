/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:mmtag.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Basic MM tagging code
 */

#include "core.h"
#include "hqmemcpy.h"
#include "hqmemset.h"

#include "mm.h"
#include "mmtag.h"
#include "mmwatch.h"

/*
 * If tagging isn't compiled in then this isn't required either
 */
#ifdef MM_DEBUG_TAG

/* "Tags" are out-of-line data structures (allocated using malloc),
 * which keep track of every allocated object. They are used for
 * fenceposting and leak identification, and may be used for
 * memory-mapping debug tools in future.
 *
 * Note that the previous tag storage mechanism was a simple linked list
 * which gave chronicly bad performance once DL-based pools were hooked up
 * to the tagging code (after fixing tags+promises issue). One benefit of
 * that approach was that it did give us an ordering of the allocated
 * addresses (allowing simple overlap detection, and the ablility to walk
 * the memory map in order). This is lost with a very simple hash
 * technique.
 *
 * An alternative scheme which did retain the ability was instead used -
 * although it is slightly more complex and appeared to have equally good
 * performance vs overhead criteria: Same basic idea of a hash table but
 * with two levels of hashing, and a hashing function which works on the
 * top N bits of the address so that adjacent (address-wise, not time-wise)
 * allocations are either hashed together or will be in the 'next' hash value.
 */

/* --- Macros --- */

/* On 32 bit platforms we use the upper 24 bits of the pointer
 * address.
 *
 * On 64 bit platforms we use the mid 26 bits starting from bit 34,
 * ignoring the last 8 bits just like 32 bit platforms. This should
 * work well for now as most allocations will occur within the initial
 * 16 GiB range. If this proves to be a problem in the future it has
 * been suggested that we introduce a 3rd hash table to deal with more
 * bits from the pointer address. NOTE: it is unlikely we will ever
 * need to use all 64 bits as most 64bit OS's currently limit a
 * processes virtual address space to around 8 TiB (as of 2011 but may
 * change in another 20 years, jwk&).
 */

/* Hash masks need to be carefully constructed taking TAG_START_BIT,
   TABLE_BITS1 and TABLE_BITS2 into consideration. Use leading zeros
   for clarity only. */
#if defined(PLATFORM_IS_64BIT)

#define TABLE_BITS1   14
#define TAG_START_BIT 34
#define TAG_HASHMASK1 0x00000003FFF00000
#define TAG_HASHMASK2 0x00000000000FFF00

#else /* Assume 32 bit platform. */

#define TABLE_BITS1   12
#define TAG_START_BIT 32
#define TAG_HASHMASK1 0xFFF00000
#define TAG_HASHMASK2 0x000FFF00

#endif /* PLATFORM_IS_64BIT */

#define TABLE_BITS2   12

/* First level hash:
 * 32 bit & 64 bit pointers:
 *    based on next "TABLE_BITS2" bits of ptr address starting at
 *    "TAG_START_BIT"
 */
#define TAG_LEVEL1HASH_ENTRIES      ( 1 << TABLE_BITS1 )
#define TAG_LEVEL1HASH_FN( _addr )  ((( uintptr_t )( _addr ) & TAG_HASHMASK1 ) >> (TAG_START_BIT - TABLE_BITS1))

/* Second level hash:
 * 32 bit & 64 bit pointers:
 *    based on next "TABLE_BITS2" bits of ptr address (immediately
 *    after the "First level hash" bits).
 */
#define TAG_LEVEL2HASH_ENTRIES      ( 1 << TABLE_BITS2 )
#define TAG_LEVEL2HASH_FN( _addr )  ((( uintptr_t )( _addr ) & TAG_HASHMASK2 ) >> (TAG_START_BIT - (TABLE_BITS1 + TABLE_BITS2)) )

/* --- Types --- */

typedef struct mm_debug_tag_t {
  struct mm_debug_tag_t *next ; /* next in the list */
  mm_addr_t ptr ;               /* the allocated object */
  mm_size_t size ;              /* its size */
  mm_pool_t pool ;              /* its pool */
  mm_alloc_class_t class ;      /* its class */
  mm_size_t seq ;               /* the sequence number of this allocation */
  char *file ;                  /* the location of the allocation */
  int line ;
} mm_debug_tag_t ;

typedef struct mm_debug_tagtable_t {
  mm_debug_tag_t *hashtable[ TAG_LEVEL2HASH_ENTRIES ] ;
  int32 tagcount ;
} mm_debug_tagtable_t ;


/* --- Variables --- */

/* This toggles wether or not mm debugging is enabled - do we create
 * the allocation-tracking tags or not.
 */
#ifdef USE_MM_DEBUGGING
int32 debug_mm_usetags = TRUE ;
#else
int32 debug_mm_usetags = FALSE ;
#endif /* USE_MM_DEBUGGING */

/* The tags hash table which is basically an array of ptrs to
 * second level hashtables of linked lists of tags.
 */
static mm_debug_tagtable_t **mm_tag_hashtable = NULL ;

/* The sequence number of the next allocation */
static mm_size_t mm_tag_seq_number = 0 ;

/* The number of tags currently allocated. This isn't used within the
 * tagging code except to provde debug info on (a) the amount of tag
 * memory currently allocated and (b) the number of objects currently
 * allocated.
 */
static int32 mm_tag_tagcount = 0 ;

/* The number of second level hash tables currently allocated. This
 * isn't used within the tagging code except to provde debug info on
 * the amount of memory allocated by the tagging code.
 */
static int32 mm_tag_tablecount = 0 ;


/* --- Exported routines --- */


/* Allocates and initialises the hash table for our tags.
 * If the allocation fails, we warn the user but allow the mm system to
 * proceed by effectively disabling tagging.
 */
void mm_tag_init( void )
{
  int32 tablesize ;

  HQASSERT( mm_tag_hashtable == NULL,
            "mm_tag_init: tag hash table already allocated!" ) ;

  /* If debugging is disabled, then don't create the table - this is enough
   * to prevent tags being created later on
   */
  if ( !debug_mm_usetags )
    return ;

  tablesize = sizeof( mm_debug_tagtable_t * ) * TAG_LEVEL1HASH_ENTRIES ;
  mm_tag_hashtable = ( mm_debug_tagtable_t ** )malloc( tablesize ) ;

  HQASSERT( mm_tag_hashtable,
            "mm_tag_init: failed to create tag hash table.\n"
            "Continuing disables tagging" ) ;
  if ( mm_tag_hashtable == NULL )
    return ;

  /* Now initialise it */
  HqMemZero(mm_tag_hashtable, tablesize ) ;
  mm_tag_tagcount   = 0 ;
  mm_tag_tablecount = 0 ;
}


/* Clears out the hash table if we managed to allocate it */
void mm_tag_finish( void )
{
  int32 idx1 ;

  /* If we didn't get a tag hashtable then we aren't tagging! */
  if ( mm_tag_hashtable == NULL )
    return ;

  for ( idx1 = TAG_LEVEL1HASH_ENTRIES - 1 ; idx1 >= 0 ; idx1-- ) {
    mm_debug_tagtable_t *table = mm_tag_hashtable[ idx1 ] ;
    if ( table != NULL ) {
      int32 idx2 ;
      for ( idx2 = TAG_LEVEL2HASH_ENTRIES - 1 ; idx2 >= 0 ; idx2-- ) {
        mm_debug_tag_t *tag = table->hashtable[ idx2 ] ;
        while ( tag != NULL ) {
          mm_debug_tag_t *nexttag = tag->next ;
          free( tag ) ;
          tag = nexttag ;
        }
      }
      free( table ) ;
    }
  }
  free( mm_tag_hashtable ) ;
  mm_tag_hashtable = NULL ;
  mm_tag_tagcount = 0 ;
  mm_tag_tablecount = 0 ;
}


/* This function makes a tag and adds it to the list */
void mm_debug_tag_add( char *file,
                       int line,
                       mm_addr_t ptr,
                       mm_size_t size,
                       mm_pool_t pool,
                       mm_alloc_class_t class )
{
  mm_debug_tagtable_t **tagtable ;
  mm_debug_tag_t      **taglist ;
  mm_debug_tag_t       *newtag ;

  /* If we didn't get a tag hashtable then we aren't tagging! */
  if ( mm_tag_hashtable == NULL )
    return ;

  /* Make the tag */
  newtag = ( mm_debug_tag_t * )malloc( sizeof( mm_debug_tag_t )) ;
  if ( newtag == NULL ) {
    HQFAIL( "mm_debug_tag_add: failed to allocate tagging block" ) ;
    mm_tag_finish() ;
    return ;
  }

  newtag->ptr   = ptr ;
  newtag->size  = size ;
  newtag->pool  = pool ;
  newtag->class = class ;
  newtag->file  = file ;
  newtag->line  = line ;
  newtag->seq   = ++mm_tag_seq_number ;

  MM_DEBUG_WATCH_ALLOC( ptr, size, pool, class, newtag->seq, file, line ) ;

  /* Find appropriate entry in first level hash table. If this is NULL then
   * need to allocate second level hash table (if this fails then we clean up
   * and disable tagging).
   */
  tagtable = & mm_tag_hashtable[ TAG_LEVEL1HASH_FN( ptr ) ] ;
  if ( *tagtable == NULL ) {
    *tagtable = ( mm_debug_tagtable_t * )malloc( sizeof( mm_debug_tagtable_t )) ;
    if ( *tagtable == NULL ) {
      HQASSERT( *tagtable,
                "mm_debug_tag_add: Failed to create second level hash table; disabling tagging" ) ;
      free( newtag ) ;
      mm_tag_finish() ;
      return ;
    }
    HqMemZero(*tagtable, sizeof( mm_debug_tagtable_t )) ;
    mm_tag_tablecount++ ;
  }

  /* We find the appropriate entry in the second level hash table, and then
   * where to insert the tag in the linked list (which we maintain ordered
   * by decreasing ptr address).
   * It is obviously illegal to already have a tag for the ptr!
   */
  taglist = & ( *tagtable )->hashtable[ TAG_LEVEL2HASH_FN( ptr ) ];
  while ( *taglist != NULL && ( *taglist )->ptr > ptr )
    taglist = & ( *taglist )->next ;

  HQASSERT( *taglist == NULL || ( *taglist )->ptr != ptr,
            "mm_debug_tag_add: Allocated address matches a previous allocation" ) ;

  newtag->next = *taglist ;
  *taglist = newtag ;
  ( *tagtable )->tagcount++ ;
  mm_tag_tagcount++ ;
}


/* this function, called by mm_free(), removes a tag from the list,
 * checking the size and pool of the allocation */
void mm_debug_tag_free( mm_addr_t ptr,
                        mm_size_t size,
                        mm_pool_t pool )
{
  mm_debug_tagtable_t **tagtable ;
  mm_debug_tag_t      **taglist ;
  mm_debug_tag_t       *tag ;

#if ! defined( ASSERT_BUILD )
  UNUSED_PARAM( mm_size_t, size ) ;
  UNUSED_PARAM( mm_pool_t, pool ) ;
#endif

  /* If we didn't get a tag hashtable then we aren't tagging! */
  if ( mm_tag_hashtable == NULL )
    return ;

  /* Find appropriate line in first level hash table.
   * If there is no second level hash table then probable double-free,
   * or bad ptr etc.
   */
  tagtable = & mm_tag_hashtable[ TAG_LEVEL1HASH_FN( ptr ) ] ;
  HQASSERT( *tagtable,
            "mm_debug_tag_free: Trying to free un-allocated object" ) ;
  if ( *tagtable == NULL )
    return ;

  /* Find appropriate line in second level hash table.
   * Then walk through the linked-list of tags to find the one we want.
   * If its not there then the caller is trying to free something that
   * has never been allocated; already freed; or the ptr is just wrong.
   */
  taglist = & ( *tagtable )->hashtable[ TAG_LEVEL2HASH_FN( ptr ) ] ;
  while ( *taglist != NULL && ( *taglist )->ptr > ptr )
    taglist = & ( *taglist )->next ;

  tag = *taglist ;
  HQASSERT( tag && tag->ptr == ptr,
            "mm_debug_tag_free: Trying to free un-allocated object" ) ;
  if ( tag == NULL ||
       tag->ptr != ptr )
    return ; /* Give up now if we didn't find a tag */

  HQASSERT( tag->size == size,
            "mm_debug_tag_free: Wrong size of object freed" ) ;
  HQASSERT( tag->pool == pool,
            "mm_debug_tag_free: Wrong pool of object freed" ) ;

  MM_DEBUG_WATCH_FREE( tag->ptr, tag->size, tag->pool, tag->class, tag->seq,
                       tag->file, tag->line ) ;
  *taglist = tag->next ;
  free( tag ) ;

  /* If the second level hash table is now empty then remove it */
  if ( --(( *tagtable )->tagcount ) == 0 ) {
    free( *tagtable ) ;
    *tagtable = NULL ;
    mm_tag_tablecount-- ;
  }
  mm_tag_tagcount-- ;
}


/* this function is called when an object is truncated, and modifies
   the corresponding tag. Truncation to zero is not permitted */
void mm_debug_tag_truncate( mm_addr_t base,
                            mm_size_t oldsize,
                            mm_size_t newsize,
                            mm_pool_t pool )
{
  mm_debug_tagtable_t *table ;
  mm_debug_tag_t      *tag ;

#if ! defined( ASSERT_BUILD )
  UNUSED_PARAM( mm_size_t, oldsize ) ;
  UNUSED_PARAM( mm_pool_t, pool ) ;
#endif

  /* If we didn't get a tag hashtable then we aren't tagging! */
  if ( mm_tag_hashtable == NULL )
    return ;

  /* Find appropriate line in first level hash table.
   * If there is no second level hash table then the caller is trying to
   * resize something that has already been freed, or just bad ptr etc.
   */
  table = mm_tag_hashtable[ TAG_LEVEL1HASH_FN( base ) ] ;
  HQASSERT( table,
            "mm_debug_tag_truncate: Trying to truncate un-allocated object" ) ;
  if ( table == NULL )
    return ;

  /* Find appropriate line in second level hash table.
   * Then walk through the linked-list of tags to find the one we want.
   * If its not there then the caller is trying to resize something that
   * has already been freed; never been allocated; or the ptr is just wrong.
   */
  tag = table->hashtable[ TAG_LEVEL2HASH_FN( base ) ] ;
  while ( tag != NULL && tag->ptr > base )
    tag = tag->next ;

  HQASSERT( tag && tag->ptr == base,
            "mm_debug_tag_truncate: Trying to truncate un-allocated object" ) ;

  if ( tag == NULL ||
       tag->ptr != base )
    return ; /* Give up now if we didn't find a tag */

  HQASSERT( tag->size == oldsize,
            "mm_debug_tag_truncate: Wrong size of object truncated" ) ;
  HQASSERT( tag->pool == pool,
            "mm_debug_tag_truncate: Wrong pool of object truncated" ) ;
  HQASSERT( tag->size > newsize || newsize == 0,
            "mm_debug_tag_truncate: Illegal size for object truncation" ) ;

  MM_DEBUG_WATCH_TRUNCATE( tag->ptr, oldsize, newsize, tag->pool, tag->class,
                           tag->seq, tag->file, tag->line ) ;

  /* resize the tag */
  tag->size = newsize ;
}


/* this function frees all the tags from a given pool */
void mm_debug_tag_free_pool( mm_pool_t pool )
{
  int32 idx1 ;

  /* If we didn't get a tag hashtable then we aren't tagging! */
  if ( mm_tag_hashtable == NULL )
    return ;

  /* For every hash line in every second level hash table, walk through
   * linked list of tags and remove any that belong to the specified pool
   */
  for ( idx1 = TAG_LEVEL1HASH_ENTRIES - 1 ; idx1 >= 0 ; idx1-- ) {
    mm_debug_tagtable_t **tagtable = & mm_tag_hashtable[ idx1 ] ;
    if ( *tagtable != NULL ) {
      int32 idx2 ;
      for ( idx2 = TAG_LEVEL2HASH_ENTRIES - 1 ; idx2 >= 0 ; idx2-- ) {
        mm_debug_tag_t **taglist = & ( *tagtable )->hashtable[ idx2 ] ;
        while ( *taglist != NULL ) {
          if (( *taglist )->pool == pool ) {
            mm_debug_tag_t *tag = *taglist ;
            *taglist = tag->next ;
            MM_DEBUG_WATCH_FREE( tag->ptr, tag->size, tag->pool, tag->class,
                                 tag->seq, tag->file, tag->line ) ;
            free( tag ) ;
            ( *tagtable )->tagcount-- ;
            mm_tag_tagcount-- ;
          }
          else {
            taglist = & ( *taglist )->next ;
          }
        }
      }

      /* If the second level hash table is now empty then destroy it */
      if ( ( *tagtable )->tagcount == 0 ) {
        free( *tagtable ) ;
        *tagtable = NULL ;
        mm_tag_tablecount-- ;
      }
    }
  }
}


/* this function applies its argument to all the allocated objects,
 * in decreasing allocation address order
 */
void mm_debug_tag_apply( mm_debug_watcher_t fn )
{
  int32 idx1, idx2 ;
  mm_debug_tagtable_t *table ;
  mm_debug_tag_t      *tag ;

  /* If we didn't get a tag hashtable then we aren't tagging! */
  if ( mm_tag_hashtable == NULL )
    return ;

  for ( idx1 = TAG_LEVEL1HASH_ENTRIES - 1 ; idx1 >= 0 ; idx1-- ) {
    table = mm_tag_hashtable[ idx1 ] ;
    if ( table != NULL ) {
      for ( idx2 = TAG_LEVEL2HASH_ENTRIES - 1 ; idx2 >= 0 ; idx2 -- ) {
        for ( tag = table->hashtable[ idx2 ] ; tag != NULL ; tag = tag->next ) {
          fn( tag->ptr, 0, tag->size, tag->pool, tag->class,
              tag->seq, tag->file, tag->line, MM_WATCH_LIVE ) ;
        }
      }
    }
  }
}


/* This function, which is only ever intended to be called from the
 * debugger, returns the tag for a valid ptr (ptr must point to
 * beginning of allocation, not within allocated address range).
 * If ptr isn't valid, NULL is returned.
 */
mm_debug_tag_t *mm_debug_tag_lookup_addr( mm_addr_t ptr )
{
  mm_debug_tagtable_t *table ;
  mm_debug_tag_t      *tag ;

  /* If we didn't get a tag hashtable then we aren't tagging! */
  if ( mm_tag_hashtable == NULL )
    return NULL ;

  /* Find appropriate line in first level hash table.
   * If there is no second level hash table then probable double-free,
   * or bad ptr etc.
   */
  table = mm_tag_hashtable[ TAG_LEVEL1HASH_FN( ptr ) ] ;
  if ( table == NULL )
    return NULL ;

  /* Find appropriate line in second level hash table.
   * Then walk through the linked-list of tags to find the one we want.
   * If its not there then the caller is trying to free something that
   * has never been allocated; already freed; or the ptr is just wrong.
   */
  tag = table->hashtable[ TAG_LEVEL2HASH_FN( ptr ) ] ;
  while ( tag != NULL && tag->ptr > ptr )
    tag = tag->next ;

  if ( tag == NULL ||
       tag->ptr != ptr )
    return NULL ;

  return tag ; /* Return tag if we found one, or NULL */
}

#endif /* MM_DEBUG_TAG */

void init_C_globals_mmtag(void)
{
#ifdef MM_DEBUG_TAG
#ifdef USE_MM_DEBUGGING
  debug_mm_usetags = TRUE ;
#else
  debug_mm_usetags = FALSE ;
#endif /* USE_MM_DEBUGGING */
  mm_tag_hashtable = NULL ;
  mm_tag_seq_number = 0 ;
  mm_tag_tagcount = 0 ;
#endif /* MM_DEBUG_TAG */
}

/* Log stripped */
