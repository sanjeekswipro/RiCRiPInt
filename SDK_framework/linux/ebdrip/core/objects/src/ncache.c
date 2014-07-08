/** \file
 * \ingroup objects
 *
 * $HopeName: COREobjects!src:ncache.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Name cache management.
 */

#include "core.h"
#include "hqmemcpy.h"
#include "hqmemcmp.h"
#include "mm.h"
#include "mmcompat.h"
#include "mps.h" /* mps_res_t */
#include "objects.h"
#include "swerrors.h"
#include "namedef_.h"
#include "objimpl.h"
#include "gcscan.h" /* ncache_finalize */


/* Storage for the name cache */
#define NAMECACHETABLESIZE 4096

NAMECACHE *namepurges = NULL ;

static NAMECACHE **thenamecache = NULL ;
static mps_root_t ncache_root;
static mps_root_t ncache_weak_root;

static uint32 nc_hash(const uint8 *nm, uint32 ln) ;
static void nc_insertname( NAMECACHE *nptr ) ;

static mps_res_t MPS_CALL ncache_top_scan(mps_ss_t ss, void* dummy, size_t level);
static mps_res_t MPS_CALL ncache_clear_root_scan(mps_ss_t ss, void* p, size_t s);


void init_C_globals_ncache(void)
{
  int32 i ;

  namepurges = NULL ;
  thenamecache = NULL ;
  ncache_root = NULL ;

  for ( i = 0 ; i < NAMES_COUNTED; ++i ) {
    system_names[ i ].next = NULL ;
    system_names[ i ].sid = 0 ;
    system_names[ i ].dictobj = NULL ;
    system_names[ i ].dictval = NULL ;
    system_names[ i ].dictsid = NULL ;
    system_names[ i ].dictcpy = NULL ;
  }
}

#if defined( ASSERT_BUILD )
int32 ncache_asserts(void)
{
  return (thenamecache != NULL) ;
}
#endif


Bool ncache_init(void)
{
  int32 i ;

  HQASSERT(thenamecache == NULL, "Name cache already allocated") ;

  thenamecache = mm_alloc_static( NAMECACHETABLESIZE * sizeof( NAMECACHE * )) ;
  if ( thenamecache == NULL )
    return FALSE ;

  for ( i = 0 ; i < NAMECACHETABLESIZE ; ++i )
    thenamecache[ i ] = NULL ;

  HQASSERT(ncache_asserts(), "Name cache not initialised") ;

/* Insert all of system names into the namecache, dummy names (unassigned
 * indexes) don't have name parts, i.e., the CList is NULL for these.
 */
  for ( i = 0 ; i < NAMES_COUNTED; ++i ) {
    NAMECACHE *nptr = & system_names[ i ] ;
    if ( theICList( nptr ))
      nc_insertname( nptr ) ;
  }

  if ( mps_root_create( &ncache_root, mm_arena, mps_rank_exact(), 0,
                        /* Scan it all, from level 0 upwards */
                        ncache_top_scan, NULL, 0 ) != MPS_RES_OK )
    return FAILURE(FALSE) ;
  if ( mps_root_create( &ncache_weak_root, mm_arena, mps_rank_weak(), 0,
                        ncache_clear_root_scan, NULL, 0 ) != MPS_RES_OK ) {
    mps_root_destroy( ncache_root );
    return FAILURE(FALSE);
  }
  return TRUE ;
}


void ncache_finish( void )
{
  mps_root_destroy( ncache_weak_root );
  mps_root_destroy( ncache_root );
}


/* Debug-only routine (i.e. made to be called from the debugger or local
 * debug code) which dumps the extra stats gathered in the name cache
 * about what's been causing lots and lots of calls to fast_extract_hash.
 */
#if defined( NAMECACHE_STATS )
void debugDumpNameCacheHits( void )
{
  NAMECACHE *nc ;
  int32 i ;

  monitorf( "Name                             :   Hit    :   Hit+   :   Miss   :   Miss+\n" , nc->len , nc->clist ,
            nc->hit_shallow , nc->hit_deep ,
            nc->miss_shallow , nc->miss_deep ) ;

  for ( i = 0 ; i < NAMECACHETABLESIZE ; i++ ) {
    nc = thenamecache[ i ] ;

    while ( nc ) {
      monitorf( "%-32.*s : %8.d : %8.d : %8.d : %8.d\n" , nc->len , nc->clist ,
                nc->hit_shallow , nc->hit_deep ,
                nc->miss_shallow , nc->miss_deep ) ;

      nc = nc->next ;
    }
  }
}
#endif

/** Lookup a name in a particular hash chain of the name cache. */
static inline NAMECACHE *nc_lookupname(const uint8 *nm, uint32 ln, uint32 hashkey)
{
  NAMECACHE *ptr = thenamecache[ hashkey ] ;

  while (ptr != NULL &&
         (ln != theINLen(ptr) ||        /* for optimisation only */
          HqMemCmp(nm, (int32)ln, theICList(ptr), theINLen(ptr)) != 0) ) {
    ptr = ptr->next ;
  }

  return ptr ;
}

/** Insert a namecache object into the cache. This is only used for pre-defined
    names on initialising the name cache. */
static void nc_insertname( NAMECACHE *nptr )
{
  uint32 hashkey ;

  HQASSERT( nptr , "nptr NULL in nc_insertname" ) ;
  HQASSERT( theICList( nptr ) , "string NULL in nc_insertname" ) ;
  HQASSERT( theINLen( nptr ) == strlen_int32( (char *) theICList( nptr ) ) ,
            "length given is incorrect" ) ;

  hashkey = nc_hash( theICList( nptr ) , theINLen( nptr )) ;
  /* Duplicates can happen due to realtype being duplicated for infinitytype.
   * Once we get rid of infinity types it can be removed and the following can
   * become an assert.
   */
  if ( nc_lookupname( theICList( nptr ) , theINLen( nptr ) , hashkey ) != NULL )
    return ;

  nptr->next = thenamecache[ hashkey ] ;

#if defined( NAMECACHE_STATS )
  /* These aren't initialized in nametab_.c, so better late than never.
   * DON'T touch the other fields!  They might be in use.  Yes, really. */
  nptr->hit_deep = 0 ;
  nptr->hit_shallow = 0 ;
  nptr->miss_deep = 0 ;
  nptr->miss_shallow = 0 ;
#endif

  thenamecache[ hashkey ] = nptr ;
  /* These names need not be finalized, because they are stored statically. */
}


/** Find or create a name cache for a name. This function is used by
    cachename and intern_string, each of which puts its own length check on
    top. */
static inline NAMECACHE *nc_cachename(const uint8 *nm, uint32 ln)
{
  uint32 hashkey ;
  NAMECACHE *ptr ;

  HQASSERT(ncache_asserts(), "Name cache not initialised") ;

  HQASSERT(( nm == NULL && ln == 0 ) ||
           ( nm != NULL && ln != 0 ) , "nm/ln inconsistent in cachename" ) ;

  hashkey = nc_hash( nm , ln ) ;
  ptr = nc_lookupname( nm , ln , hashkey ) ;

  if ( ! ptr ) {
    ptr = (NAMECACHE *)mm_ps_alloc_weak( mm_pool_ps_typed_global,
                                         ln + sizeof( NAMECACHE ));
    if ( ptr == NULL ) {
      (void)error_handler(VMERROR);
      return NULL;
    }
    ptr->typetag = tag_NCACHE ;
    theICList( ptr ) = ( uint8 * )( ptr + 1 ) ;
    HqMemCpy( theICList( ptr ) , nm , ( int32 )ln ) ;
    theINLen( ptr )      = CAST_TO_UINT16(ln) ;
    theISaveLevel( ptr ) = CAST_TO_UINT8(get_core_context_interp()->savelevel) ;
    ptr->next   = thenamecache[ hashkey ] ;
    theIOpClass( ptr ) = 0 ;
    theINameNumber( ptr ) = -1 ;

    ptr->dictobj = NULL ;
    ptr->dictval = NULL ;
    ptr->dictcpy = NULL ;

#if defined( NAMECACHE_STATS )
    ptr->hit_deep = 0 ;
    ptr->hit_shallow = 0 ;
    ptr->miss_deep = 0 ;
    ptr->miss_shallow = 0 ;
#endif

#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
    ptr->flags = 0 ;    /* [51291] */
#endif

    thenamecache[ hashkey ] = ptr ;
  }
  return ( ptr ) ;
}

/* ----------------------------------------------------------------------------
   function:            cachename(..)      author:              Andrew Cave
   creation date:       02-Oct-1987        last modification:   ##-###-####
   arguments:           nm , ln .
   description:

   This function looks up the name in the name cache, inserts it if it isn't
   there and then returns its cache value .

---------------------------------------------------------------------------- */
NAMECACHE *cachename(const uint8 *nm, uint32 ln)
{
  if ( ln > MAXPSNAME ) {
    ( void )error_handler( LIMITCHECK ) ;
    return NULL ;
  }

  return nc_cachename(nm, ln) ;
}

NAMECACHE *cachelongname(const uint8 *nm, uint32 ln)
{
  if ( ln > MAXINTERN ) {
    ( void )error_handler( LIMITCHECK ) ;
    return NULL ;
  }

  return nc_cachename(nm, ln) ;
}

/* ----------------------------------------------------------------------------
   function:            lookupname(..)     author:              Andrew Cave
   creation date:       02-Oct-1987        last modification:   ##-###-####
   arguments:           nm , ln .
   description:

   This function looks up the name in the name cache, returning the ptr to it's
   location, returning NULL if it doesn't exist.

---------------------------------------------------------------------------- */
NAMECACHE *lookupname(const uint8 *nm, uint32 ln)
{
  uint32 hashkey ;

  HQASSERT(ncache_asserts(), "Name cache not initialised") ;
  HQASSERT(( nm == NULL && ln == 0 ) ||
           ( nm != NULL && ln != 0 ) , "nm/ln inconsistent in lookupname" ) ;

  if ( ln > MAXPSNAME ) {
    ( void )error_handler( LIMITCHECK ) ;
    return NULL ;
  }
  hashkey = nc_hash( nm , ln ) ;
  return nc_lookupname( nm , ln , hashkey ) ;
}

NAMECACHE *lookuplongname(const uint8 *nm, uint32 ln)
{
  uint32 hashkey ;

  HQASSERT(ncache_asserts(), "Name cache not initialised") ;
  HQASSERT(( nm == NULL && ln == 0 ) ||
           ( nm != NULL && ln != 0 ) , "nm/ln inconsistent in lookupname" ) ;

  if ( ln > MAXINTERN ) {
    ( void )error_handler( LIMITCHECK ) ;
    return NULL ;
  }

  hashkey = nc_hash( nm , ln ) ;
  return nc_lookupname( nm , ln , hashkey ) ;
}

/** This function calculates the name cache hash bucket for the given string. */
static uint32 nc_hash(const uint8 *nm , uint32 ln )
{
  uint32 lni ;

  HQASSERT(( nm == NULL && ln == 0 ) ||
           ( nm != NULL && ln != 0 ) , "nm/ln inconsistent in nc_hash" ) ;
  HQASSERT( ln <= MAXINTERN , "name too long" ) ;

  lni = ln ;

  while ( lni > 3 ) {
    uint32 t1, t2 ;

    t1 = nm[ 0 ] ;
    t2 = ( ln & 3 ) ;
    ln += t1 ;
    ln += ( t1 << t2 ) ;
    t1 = nm[ 1 ] ;
    t2 = ( ln & 3 ) ;
    ln += t1 ;
    ln += ( t1 << t2 ) ;
    t1 = nm[ 2 ] ;
    t2 = ( ln & 3 ) ;
    ln += t1 ;
    ln += ( t1 << t2 ) ;
    t1 = nm[ 3 ] ;
    t2 = ( ln & 3 ) ;
    ln += t1 ;
    ln += ( t1 << t2 ) ;

    nm  += 4 ;
    lni -= 4 ;
  }

  while ( lni > 0 ) {
    uint32 t1, t2 ;

    t1 = nm[ 0 ] ;
    t2 = ( ln & 3 ) ;
    ln += t1 ;
    ln += ( t1 << t2 ) ;

    nm  += 1 ;
    lni -= 1 ;
  }

  return ( ln & ( NAMECACHETABLESIZE - 1 )) ;
}


/* If name->dictval points to the extension array on the current level,
   repoint it to restored dict slot. */
void ncache_restore_prepare(NAMECACHE *name, OBJECT* slot,
                            OBJECT *base, OBJECT *currext, int32 slevel)
{
  UNUSED_PARAM(OBJECT *, base); UNUSED_PARAM(int32, slevel);
  HQASSERT(currext != NULL, "No extension array");
  HQASSERT(!(name->dictobj != NULL && !DICT_IN_ARRAY(name->dictval, currext)),
           "Name is unique, yet dictval doesn't point in the given dict");

  if ( name->dictval != slot
       && (name->dictobj != NULL /* unique, so must be in array */
           || DICT_IN_ARRAY(name->dictval, currext)) ) {
    HQASSERT(name->dictobj == base /* unique now in base */
             /* or will revert to it or undefined */
             || name->dictcpy == base || name->dictcpy == NC_DICTCACHE_RESET,
             "Namecache doesn't point to the expected dict");
    name->dictval = slot;
  }
}


/* ----------------------------------------------------------------------------
   function:            purge_ncache(..)   author:              Andrew Cave
   creation date:       02-Oct-1987        last modification:   ##-###-####
   arguments:           value .
   description:

   This function removes any cached names from the cache which were added after
   the corresponding save was performed.

---------------------------------------------------------------------------- */
void purge_ncache(int32 slevel)
{
  int32 i ;
  NAMECACHE *curr ;
  NAMECACHE **base ;

  HQASSERT(ncache_asserts(), "Name cache not initialised") ;

  base = thenamecache ;

  for ( i = 0 ; i < NAMECACHETABLESIZE ; ++i ) {
    if (( curr = (*base++)) != NULL ) {
      if ( theISaveLevel( curr ) > slevel ) {
        do {
          if ( NULL == ( curr = curr->next ))
            break ;
        } while ( theISaveLevel( curr ) > slevel );
        base[ -1 ] = curr ;
      }
      while ( curr ) {
#ifdef debugac
        if ( curr->dictcpy && !curr->dictobj )
          printf("rs: %.*s\n",theINLen(curr),theICList(curr));
#endif
        if ( curr->dictcpy == NC_DICTCACHE_RESET ) {
          curr->dictobj = NULL ;
          curr->dictval = NULL ;
        }
        else {
          curr->dictobj = curr->dictcpy ;
        }
        curr = curr->next ;
      }
    }
  }
}


/* ncache_finalize - finalize a NAMECACHE: unlink it from the lists
 *
 * Compare this to purge_ncache.
 */
void ncache_finalize(NAMECACHE *obj)
{
  int found = FALSE;
  int32 i;
  register NAMECACHE *curr;
  register NAMECACHE **prev;
  NAMECACHE **base;

  HQASSERT(ncache_asserts(), "Name cache not initialised");

  for ( i = 0, base = thenamecache ; i < NAMECACHETABLESIZE ; ++i, ++base ) {
    if ( *base != NULL ) {
      /* Run down the cache list and unlink the name if it's on it */
      prev = base;
      while (( curr = *prev ) != NULL ) {
        if ( curr == obj ) {
          *prev = curr->next; found = TRUE;
          break;
        } else {
          prev = &curr->next;
        }
      }
    }
    if ( found ) break;
  }
  HQASSERT( found, "Couldn't unlink finalized name from cache" );

  ncache_purge_finalize(obj);
}


/* Scan a single NAMECACHE */

mps_res_t MPS_CALL ncache_scan(size_t *len_out,
                               mps_ss_t ss, NAMECACHE *nc)
{
#if defined(ASSERT_BUILD)
  if ( nc->dictobj != NULL ) {
    OBJECT *thed = nc->dictobj;
    OBJECT *arr = oType(thed[-1]) != ONOTHING ? oDict(thed[-1]) : thed;

    HQASSERT(arr != NULL && DICT_IN_ARRAY(nc->dictval, arr),
             "dictval out of range");
  }
#endif
  /* The dictval field can be skipped, since it's only live when
   * it's uniquely defined in some dictionary and that dictionary is
   * live.  In that situation, the dictionary entry is points to is
   * scanned anyway.  It is weak, but it doesn't have to be cleared
   * when the referent dies, as it will never be used in that case,
   * because the only way for the referent to die is for the entry
   * to get redefined or the dictionary to die.
   *
   * The next and dictsid fields are not fixed, because they are
   * weak references used by restore.  Finalization will update them
   * if the referent dies.
   *
   * The length calculation is relying on the name being allocated
   * as a part of the NAMECACHE object. */
  MPS_SCAN_BEGIN(ss)
    if (!MPS_IS_RETAINED(&nc->dictobj, TRUE))
      nc->dictobj = NULL;
    if ( nc->dictcpy != NC_DICTCACHE_RESET ) {
      if (!MPS_IS_RETAINED(&nc->dictcpy, TRUE))
        nc->dictcpy = NULL;
    }
  MPS_SCAN_END(ss);
  *len_out = sizeof(NAMECACHE) + nc->len;
  return MPS_RES_OK;
}


/* ncache_top_scan - scans the namecache on the given savelevel and above */

mps_res_t MPS_CALL ncache_top_scan(mps_ss_t ss, void* dummy, size_t level)
{
  size_t i;
  NAMECACHE *curr ;
  NAMECACHE **base ;

  HQASSERT( ncache_asserts(), "Name cache not initialised" );
  UNUSED_PARAM( void*, dummy );

  MPS_SCAN_BEGIN( ss )
    for ( i = 0, base = thenamecache; i < NAMECACHETABLESIZE; ++i, ++base ) {
      curr = *base;
      while ( curr != NULL && theISaveLevel( curr ) >= (int32)level ) {
        MPS_RETAIN( &curr, TRUE );
        /* This simply ignores the system names as they are not in VM (quickly,
           as the address is usually far from the arena). */
        curr = curr->next;
      }
    }
  MPS_SCAN_END( ss );
  return MPS_RES_OK;
}


mps_res_t MPS_CALL ncache_clear_root_scan(mps_ss_t ss, void* p, size_t s)
{
  size_t i;
  mps_res_t res = MPS_RES_OK;

  UNUSED_PARAM(void*, p); UNUSED_PARAM(size_t, s);
  for ( i = 0 ; i < NAMES_COUNTED ; ++i ) {
    NAMECACHE *curr = &system_names[i];
    if ( curr->clist != NULL ) {
      size_t dummy;

      res = ncache_scan(&dummy, ss, curr);
      if ( res != MPS_RES_OK )
        break;
    }
  }
  return res;
}


/*
Log stripped */
