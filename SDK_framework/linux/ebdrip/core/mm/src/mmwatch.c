/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:mmwatch.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Basic MM watch implementation
 */

#include "core.h"

#include "mm.h"
#include "mmpool.h"
#include "mmtag.h"
#include "hqmemset.h"
#include "vm.h"

#include "rbtree.h"

#include "swcopyf.h"

#if defined( MM_USE_MONITOR_WINDOW )
#include "monitor.h"
#else
#include <stdio.h> /* For FILE, fopen and fprintf */
#endif

/*
 * If watching isn't compiled in, this isn't required either
 */
#ifdef MM_DEBUG_WATCH


#if defined( USE_MM_DEBUGGING ) || defined( DEBUG_BUILD )
#define MM_DEBUG_WATCH_TOTALS
#else
#undef MM_DEBUG_WATCH_TOTALS
#endif


/* The current watcher function */
mm_debug_watcher_t mm_debug_watcher = NULL ;


/* --- Exported routines --- */

/* This routine replaces the current watcher function */
mm_debug_watcher_t mm_debug_watch( mm_debug_watcher_t watcher )
{
  mm_debug_watcher_t result = mm_debug_watcher ;
  mm_debug_watcher = watcher ;
  return result ;
}

/* This routine calls the provided watcher function for every
 * "current" allocation
 */
void mm_debug_watch_live( mm_debug_watcher_t watcher )
{
  mm_debug_tag_apply( watcher ) ;
}


/* String lookup table for each of the known allocation classes
 */
#define MM_ALLOC_CLASS( name ) #name,
static char *class_str[] = {
#ifndef DOXYGEN_SKIP
#include "mm_class.h"
#endif /* !DOXYGEN_SKIP */
  "unknown class"
} ;
#undef MM_ALLOC_CLASS


/*
 * Single char code for watch type (Alloc/Free/Truncate/Live)
 */
static char watch_str[] = "AFTL" ;


/*
 * debug_mm_watchlevel can be set to filter out all mm traces except
 * those that we want. Typical values:
 *   DEBUG_MM_WATCH_ALLOCFREE (7)  - mm_alloc, mm_free, mm_truncate calls only
 *   DEBUG_MM_WATCH_LIVE      (8)  - explicit mm_debug_watch_live calls only
 *   DEBUG_MM_WATCH_ALL       (15) - both of the above
 * Default is 0 (no tracing)
 */
#define DEBUG_MM_WATCH_ALLOCFREE \
  ((1 << MM_WATCH_ALLOC) | (1 << MM_WATCH_FREE) | (1 << MM_WATCH_TRUNCATE))
#define DEBUG_MM_WATCH_LIVE       (1 << MM_WATCH_LIVE)
#define DEBUG_MM_WATCH_ALL \
  (DEBUG_MM_WATCH_ALLOCFREE | DEBUG_MM_WATCH_LIVE)

int debug_mm_watchlevel = 0;

/* Where we write the watch information to */
static const char *watch_filename = "mmlog" ; /* In same folder as RIP binary */
static FILE *watch_fileptr  = NULL ;


/*
 * Handy lookup routines
 */
static char *class_lookup( mm_alloc_class_t class )
{
  mm_alloc_class_t idx ;

  /* Quick integrity check on class string table */
  HQASSERT(( sizeof(class_str) / sizeof(char *)) - 1
           == MM_ALLOC_CLASS_UNDECIDED + 1,
           "class_str table definition out of sync with "
           "mm_alloc_class_e - regenerate table" ) ;

  if ( class > MM_ALLOC_CLASS_UNDECIDED )
    idx = MM_ALLOC_CLASS_UNDECIDED + 1 ;
  else
    idx = class ;

  return class_str[ idx ];
}


static char *pooltype_lookup( mm_pooltype_t type )
{
  char *str ;

  /* If the pool type isn't valid (for some reason) then display this */
  static char *unknown_pool_str = "unknown pool" ;

  str = get_pooltype_name( type ) ;
  if ( str == NULL )
    str = unknown_pool_str ;
  return str ;
}

/** Allocation callback for the mm totals tree. */

/*@null@*/ /*@out@*/ /*@only@*/ mm_addr_t mmtt_alloc(
                                          size_t size ,
  /*@null@*/                              void *data )
  /*@ensures MaxSet(result) == (size - 1); @*/
{
  UNUSED_PARAM( void * , data ) ;
  return ( mm_addr_t )malloc( size ) ;
}

/** Free callback for the mm totals tree. */

void mmtt_free(
  /*@notnull@*/ /*@out@*/ /*@only@*/      mm_addr_t what ,
                                          size_t size ,
  /*@null@*/                              void *data )
{
  UNUSED_PARAM( size_t , size ) ;
  UNUSED_PARAM( void * , data ) ;
  free( what ) ;
}

/*
 * The simplest watcher: just dump the details to the log.  I didn't
 * add "oldsize" to this output, which I added to the interface to
 * track truncates properly, so that I didn't break scripts which rely
 * on the format of this log staying constant.
 */
void mm_trace( mm_addr_t ptr, size_t oldsize, size_t size,
               mm_pool_t pool, mm_alloc_class_t class,  size_t seq,
               char *file, int line, mm_debug_watch_t watch )
{
  UNUSED_PARAM( size_t , oldsize ) ;

  /* Ignore anything below our current debug trace level */
  if ( ! (( 1 << watch ) & debug_mm_watchlevel ))
    return ;

#if ! defined( MM_USE_MONITOR_WINDOW )
  /* If we don't have a file already opened, then open it.
   * If the open fails then we disable the watcher.
   * Note that we never actually close this file...
   */
  if ( ! watch_fileptr ) {
    watch_fileptr = fopen( watch_filename, "w" ) ;
    if ( ! watch_fileptr )
      debug_mm_watchlevel = 0 ;
  }

  fprintf( watch_fileptr,
#else
  /* Outputting to the monitor window is useful 'cos it gets interleaved
   * with other debugging output. However the downside is that it slows
   * things down and it means this output can't be enabled immediately
   * (output stream used by monitorf isn't created until part-way through
   * dostart).
   */
  monitorf(
#endif /* !MM_USE_MONITOR_WINDOW */
           "%p: %7lu bytes %c %-33s %-16s at %p seq=%06lu, %s(%d)\n",
           ptr,
           (unsigned long)size, /* size_t is long on some platforms */
           watch_str[ watch ],
           class_lookup( class ),
           pooltype_lookup( pool->type ),
           pool,
           (unsigned long)seq,
           file, line ) ;
}

/** A simple routine to allow the log file to be manually flushed. */

int mm_trace_flush( void )
{
  /* Returns the 0 if successfully flushed (or no fileptr), EOF otherwise */
  return ( watch_fileptr ? fflush( watch_fileptr ) : 0 ) ;
}

/** A simple routine to allow the log file to be manually closed. */

int mm_trace_close( void )
{
  int status = 0;
  /* Returns the 0 if successfully closed (or no fileptr), EOF otherwise */
  if (watch_fileptr != NULL)
    status = fclose( watch_fileptr );
  watch_fileptr = NULL;
  return status;
}


/** The root of the mm_totals_tree binary tree. */

static RBT_ROOT *mm_totals_tree_root = NULL ;

/** Flags controlling which qualities of allocation are to be tracked
    in the hierarchy of mm_totals_tree trees. Yes, this is a
    bitfield. It doesn't appear in an API, and so I beg to argue that
    its existence is justified :->

    Note that if you want to have only by_pool set, you might as well
    just use MM_DEBUG_TOTAL instead.

    \c free_stragglers is there so that you can turn off the freeing
    of totals nodes as their totals go to zero, so that you can see in
    the dump output that such allocations did exist at some point, but
    now they've been freed. */

static struct mm_totals_tree_flags {
  unsigned int by_pool:1 ;
  unsigned int by_class:1 ;
  unsigned int by_file:1 ;
  unsigned int by_line:1 ;
  unsigned int by_size:1 ;
  unsigned int free_stragglers:1 ;
}
mm_totals_tree_flags = { 1 , 1 , 1 , 1 , 1 , 1 } ;

/** Set the flags controlling which distinguishing factors are used in
    building the mm_totals_tree trees. */

void mmtt_setflags( Bool by_pool,
                    Bool by_class,
                    Bool by_file,
                    Bool by_line,
                    Bool by_size,
                    Bool free_stragglers )
{
  HQASSERT( mm_totals_tree_root == NULL ,
            "Can't sensibly change mmtt flags when a tree's already built." ) ;

  mm_totals_tree_flags.by_pool = by_pool ? 1 : 0 ;
  mm_totals_tree_flags.by_class = by_class ? 1 : 0 ;
  mm_totals_tree_flags.by_file = by_file ? 1 : 0 ;
  mm_totals_tree_flags.by_line = by_line ? 1 : 0 ;
  mm_totals_tree_flags.by_size = by_size ? 1 : 0 ;
  mm_totals_tree_flags.free_stragglers = free_stragglers ? 1 : 0 ;
}

/** An mm_totals_tree node data structure which is identified either
    by a string or a number. */

typedef struct {
  Bool named ;

  union {
    const char *name ;
    size_t number ;
  } u ;

  size_t count ;
  size_t size_total ;

  /** Callback function which (if non-null) will be called when the
      values from this node are changed. */

  void ( *callback )( mm_pool_t pool , mm_alloc_class_t class ,
                      char *file , int line , size_t oldsize ,
                      size_t size , size_t total_count ,
                      size_t total_size ) ;

  RBT_ROOT *subtree ;
}
MMTT_NODE_DATA ;

/** Look up the count and total size of allocations with the given
    distinguishing factors. Of course, only those factors which have
    been used to construct the trees will be significant. In other
    words, if you didn't set \b by_line in \c mmtt_setflags, then the
    \c line argument below will be ignored. */

void mmtt_get_totals( mm_pool_t pool , mm_alloc_class_t class ,
                      char *file , int line , size_t size ,
                      size_t *total_count , size_t *total_size )
{
  RBT_ROOT *current_root = mm_totals_tree_root ;
  RBT_NODE *node = NULL ;
  MMTT_NODE_DATA *data ;

  *total_count = 0 ;
  *total_size = 0 ;

  if ( current_root != NULL && mm_totals_tree_flags.by_pool ) {
    node = rbt_search( current_root , ( uintptr_t )pool ) ;

    if ( node != NULL ) {
      data = rbt_get_node_data( node ) ;
      HQASSERT_LPTR( data ) ;
      current_root = data->subtree ;
    }
    else {
      return ;
    }
  }

  if ( current_root != NULL && mm_totals_tree_flags.by_class ) {
    node = rbt_search( current_root , ( uintptr_t )class ) ;

    if ( node != NULL ) {
      data = rbt_get_node_data( node ) ;
      HQASSERT_LPTR( data ) ;
      current_root = data->subtree ;
    }
    else {
      return ;
    }
  }

  if ( current_root != NULL && mm_totals_tree_flags.by_file ) {
    node = rbt_search( current_root , ( uintptr_t )file ) ;

    if ( node != NULL ) {
      data = rbt_get_node_data( node ) ;
      HQASSERT_LPTR( data ) ;
      current_root = data->subtree ;
    }
    else {
      return ;
    }
  }

  if ( current_root != NULL && mm_totals_tree_flags.by_line ) {
    node = rbt_search( current_root , ( uintptr_t )line ) ;

    if ( node != NULL ) {
      data = rbt_get_node_data( node ) ;
      HQASSERT_LPTR( data ) ;
      current_root = data->subtree ;
    }
    else {
      return ;
    }
  }

  if ( current_root != NULL && mm_totals_tree_flags.by_size ) {
    node = rbt_search( current_root , ( uintptr_t )size ) ;

    if ( node != NULL ) {
      data = rbt_get_node_data( node ) ;
      HQASSERT_LPTR( data ) ;
      current_root = data->subtree ;
    }
    else {
      return ;
    }
  }

  data = rbt_get_node_data( node ) ;
  *total_count = data->count ;
  *total_size = data->size_total ;
}

/** Free a mm_totals_tree tree node plus any subtrees. Note that the
    node should already have been removed from its parent tree. */

static void mmtt_freenode( RBT_ROOT *root , RBT_NODE *node )
{
  if ( node != NULL ) {
    MMTT_NODE_DATA *data = rbt_get_node_data( node ) ;

    if ( data != NULL ) {
      if ( data->subtree != NULL ) {
        RBT_NODE *del_node = NULL;

        HQASSERT( rbt_node_count( data->subtree ) == 1 ,
                  "Any subtree should have exactly one node at this point." ) ;
        /** \todo RGG
         * There seems to be a crash in this code when the totals
         * tree is enabled for memory debugging.
         * Does the comment above, concerning the removal of the node before
         * calling mmtt_freenode apply here as well?
         * Need JonW to answer this one. Removing the node does seem to work
         * though.
         */
        del_node = rbt_remove(data->subtree, rbt_root_node( data->subtree)) ;
        mmtt_freenode( data->subtree , del_node) ;
        rbt_dispose( data->subtree ) ;
      }
    }

    rbt_free_node( root , node ) ;
  }
}

/** Allocate and insert a new node into the given mm_totals_tree
    tree. Returns a pointer to the new node. */

static RBT_NODE *mmtt_createnode( RBT_ROOT *root , uintptr_t key ,
                                  Bool named , char *name ,
                                  size_t number , size_t size )
{
  RBT_NODE *node ;
  MMTT_NODE_DATA node_data ;

  node_data.named = named ;
  if ( named ) {
    node_data.u.name = name ;
  }
  else {
    node_data.u.number = number ;
  }
  node_data.count = 1 ;
  node_data.size_total = size ;
  node_data.callback = NULL ;

  node_data.subtree = NULL ;

  node = rbt_allocate_node( root , key , & node_data ) ;

  if ( node == NULL ) {
    HQFAIL( "mmtt_createnode allocation failed." ) ;
    return NULL ;
  }

  rbt_insert( root , node ) ;

  return node ;
}

#if defined( ASSERT_BUILD )
static Bool mmtt_assert_callback( RBT_ROOT *root , RBT_NODE *node ,
                                  void *priv_data )
{
  MMTT_NODE_DATA *data = rbt_get_node_data( node ) ;

  UNUSED_PARAM( RBT_ROOT * , root ) ;
  UNUSED_PARAM( void * , priv_data ) ;

  HQASSERT( data->named == FALSE || data->named == TRUE ,
            "Sanity checks failed on node data" ) ;

  return TRUE ;
}

void mm_totals_tree_asserts( RBT_ROOT *root )
{
  if ( root != NULL ) {
    ( void )rbt_walk( root , mmtt_assert_callback , NULL ) ;
  }
}
#else
#define mm_totals_tree_asserts( _root ) EMPTY_STATEMENT()
#endif

/** Update a single subtree arising from a call to
    mm_totals_tree(). Find the correct node in the tree from \c *root
    to correspond with the given \c key. If no such node exists,
    create it. Once the node is in hand, update its count and totals
    fields according to the mm_watch params (\c pool to \c watch,
    inclusive). Returns a pointer to the updated node, since
    subsequent calls will be concerned with its subtree. */

static RBT_NODE *mm_totals_tree_single( mm_pool_t pool ,
                                        mm_alloc_class_t class ,
                                        char *file ,
                                        int line ,
                                        size_t oldsize ,
                                        size_t size ,
                                        mm_debug_watch_t watch ,
                                        RBT_ROOT **root ,
                                        uintptr_t key ,
                                        Bool named ,
                                        char *name ,
                                        size_t number )
{
  RBT_NODE *node = NULL ;
  MMTT_NODE_DATA *data ;

  HQASSERT( root != NULL , "Null root ptr" ) ;

  if ( *root != NULL ) {
    node = rbt_search( *root , key ) ;
  }

  if ( node != NULL ) {
    data = rbt_get_node_data( node ) ;

    HQASSERT( data->named == FALSE || data->named == TRUE ,
              "Sanity checks failed on node data" ) ;

    switch ( watch ) {
      case MM_WATCH_ALLOC:
      case MM_WATCH_LIVE:
        /* We assume if we're being called with MM_WATCH_LIVE that we
           started with a blank slate. Otherwise, things will be
           counted twice, but there's nothing we can do about that. If
           whoever's driving the debugger is getting fancy, maybe it's
           even intended that we count things twice. */
        data->count++ ;
        data->size_total += size ;
        break ;

      case MM_WATCH_FREE:
        HQASSERT( data->size_total >= size , "About to underflow." ) ;

        data->count-- ;
        data->size_total -= size ;

        if ( mm_totals_tree_flags.free_stragglers && data->count == 0 ) {
          HQASSERT( data->size_total == 0 , "Lost track of size somehow" ) ;
          mmtt_freenode( *root , rbt_remove( *root , ( RBT_NODE * )node )) ;
          return NULL ;
        }
        break ;

      case MM_WATCH_TRUNCATE:
        if ( key == ( uintptr_t )oldsize ) {
          /* Keying by size won't work unless we pretend this is a
             free of the old size block followed by an alloc of the
             new size (otherwise later when the truncated block got
             freed, we wouldn't find it in the tree). */
          ( void )mm_totals_tree_single( pool ,class , file , line , 0 ,
                                         oldsize , MM_WATCH_FREE , root ,
                                         ( uintptr_t )oldsize , named , name ,
                                         number ) ;
          return mm_totals_tree_single( pool ,class , file , line , 0 ,
                                        size , MM_WATCH_ALLOC , root ,
                                        ( uintptr_t )size , named , name ,
                                        number ) ;
        }
        else {
          HQASSERT( data->size_total >= ( oldsize - size ) ,
                    "About to underflow." ) ;

          data->size_total -= ( oldsize - size ) ;
        }
        break ;

      default:
        HQFAIL( "Unrecognised MM_WATCH type." ) ;
    }

    if ( data->callback != NULL ) {
      ( *( data->callback ))( pool , class , file , line , oldsize , size ,
                              data->count , data->size_total ) ;
    }
  }
  else {
    HQASSERT( watch == MM_WATCH_ALLOC || watch == MM_WATCH_LIVE ,
              "A free or truncate on a block we dont know about: "
              "did tree building start too late?" ) ;

    /* First, create the subtree root if necessary. */

    if ( *root == NULL ) {
      *root = rbt_init( NULL , mmtt_alloc , mmtt_free ,
                        ( named ? rbt_compare_string_keys :
                          rbt_compare_integer_keys ) ,
                        0 , sizeof( MMTT_NODE_DATA )) ;

      if ( *root == NULL ) {
        return NULL ;
      }
    }

    node = mmtt_createnode( *root , key , named , name , number , size ) ;
  }

  data = rbt_get_node_data( node ) ;
  HQASSERT_LPTR( data ) ;
  mm_totals_tree_asserts( data->subtree ) ;

  return node ;
}

/** A watcher which builds red-black binary trees to keep track of the
    totals allocated by class, and optionally file, line and
    size. Note that this watcher just builds the data structures:
    you'll need to call mm_totals_tree_dump to send them to the
    monitor. */

void mm_totals_tree( mm_addr_t ptr, size_t oldsize, size_t size,
                     mm_pool_t pool, mm_alloc_class_t class,  size_t seq,
                     char *file, int line, mm_debug_watch_t watch )
{
  RBT_ROOT **current_root = & mm_totals_tree_root ;
  RBT_NODE *node ;
  MMTT_NODE_DATA *data ;

  UNUSED_PARAM( mm_addr_t , ptr ) ;
  UNUSED_PARAM( size_t , seq ) ;

  /* Ignore anything below our current debug trace level. */
  if ( ! (( 1 << watch ) & debug_mm_watchlevel ))
    return ;

  if ( mm_totals_tree_flags.by_pool ) {
    char *pool_type = pooltype_lookup( pool->type ) ;

    node = mm_totals_tree_single( pool , class , file , line , oldsize ,
                                  size , watch , current_root ,
                                  ( uintptr_t )pool_type , TRUE ,
                                  pool_type , 0 ) ;

    if ( node != NULL ) {
      data = rbt_get_node_data( node ) ;
      HQASSERT_LPTR( data ) ;
      current_root = & data->subtree ;
    }
    else {
      return ;
    }
  }

  if ( mm_totals_tree_flags.by_class ) {
    char *class_name = class_lookup( class ) ;

    node = mm_totals_tree_single( pool , class , file , line , oldsize ,
                                  size , watch , current_root ,
                                  ( uintptr_t )class_name , TRUE ,
                                  class_name , 0 ) ;

    if ( node != NULL ) {
      data = rbt_get_node_data( node ) ;
      HQASSERT_LPTR( data ) ;
      current_root = & data->subtree ;
    }
    else {
      return ;
    }
  }

  if ( mm_totals_tree_flags.by_file ) {
    node = mm_totals_tree_single( pool , class , file , line , oldsize ,
                                  size , watch , current_root ,
                                  ( uintptr_t )file , TRUE ,
                                  file , 0 ) ;

    if ( node != NULL ) {
      data = rbt_get_node_data( node ) ;
      HQASSERT_LPTR( data ) ;
      current_root = & data->subtree ;
    }
    else {
      return ;
    }
  }

  if ( mm_totals_tree_flags.by_line ) {
    node = mm_totals_tree_single( pool , class , file , line , oldsize ,
                                  size , watch , current_root ,
                                  ( uintptr_t )line , FALSE ,
                                  NULL , line ) ;

    if ( node != NULL ) {
      data = rbt_get_node_data( node ) ;
      HQASSERT_LPTR( data ) ;
      current_root = & data->subtree ;
    }
    else {
      return ;
    }
  }

  if ( mm_totals_tree_flags.by_size ) {
    node = mm_totals_tree_single( pool , class , file , line , oldsize ,
                                  size , watch , current_root ,
                                  ( watch == MM_WATCH_TRUNCATE ?
                                    ( uintptr_t )oldsize : ( uintptr_t )size ) ,
                                  FALSE , NULL , size ) ;
  }
}

/** Pointers to all the nodes whose subtrees are currently being
    dumped. */

static MMTT_NODE_DATA *mmtt_dump_stack[ 5 ] ;

/** Calculated column widths so the output is nice and neat. */

static int mmtt_dump_column_width[ 7 ] ;

/** How many subtrees to expect during the dump. */

static uint32 mmtt_dump_depth ;

/** The current stack depth during a dump. */

static uint32 mmtt_stack_size ;

/** The pre-pass callback for \c mm_totals_tree_dump to count levels
    and calculate maximum column widths. */

static Bool mmtt_dump_counts_callback( RBT_ROOT *root , RBT_NODE *node ,
                                       void *priv_data )
{
  int len ;
  MMTT_NODE_DATA *node_data = rbt_get_node_data( node ) ;
  uint8 buf[ 256 ] ;

  UNUSED_PARAM( RBT_ROOT * , root ) ;
  UNUSED_PARAM( void * , priv_data ) ;

  if ( node_data->named ) {
    len = ( int32 )strlen( node_data->u.name ) ;
  }
  else {
    swcopyf( buf , ( uint8* )"%u" , node_data->u.number ) ;
    len = ( int32 )strlen(( const char * )buf ) ;
  }

  if ( len > mmtt_dump_column_width[ mmtt_stack_size ]) {
    mmtt_dump_column_width[ mmtt_stack_size ] = len ;
  }

  swcopyf( buf , ( uint8* )"%u" , node_data->count ) ;
  len = ( int32 )strlen(( const char * )buf ) ;

  if ( len > mmtt_dump_column_width[ 5 ]) {
    mmtt_dump_column_width[ 5 ] = len ;
  }

  swcopyf( buf , ( uint8* )"%u" , node_data->size_total ) ;
  len = ( int32 )strlen(( const char * )buf ) ;

  if ( len > mmtt_dump_column_width[ 6 ]) {
    mmtt_dump_column_width[ 6 ] = len ;
  }

  if ( node_data->subtree != NULL ) {
    mmtt_stack_size++ ;
    ( void )rbt_walk( node_data->subtree , mmtt_dump_counts_callback , NULL ) ;
    mmtt_stack_size-- ;
  }

  return TRUE ;
}

/** The output callback for \c mm_totals_tree_dump. */

static Bool mmtt_dump_output_callback( RBT_ROOT *root , RBT_NODE *node ,
                                       void *priv_data )
{
  uint32 i ;
  MMTT_NODE_DATA *node_data = rbt_get_node_data( node ) ;

  UNUSED_PARAM( RBT_ROOT * , root ) ;
  UNUSED_PARAM( void * , priv_data ) ;

  mmtt_dump_stack[ mmtt_stack_size ] = node_data ;

  for ( i = 0 ; i < mmtt_dump_depth ; i++ ) {
    MMTT_NODE_DATA *stack_data = mmtt_dump_stack[ i ] ;

    if ( i > mmtt_stack_size ) {
#if defined( MM_USE_MONITOR_WINDOW )
      monitorf(( uint8 * )
#else
      fprintf( watch_fileptr,
#endif
               "%.*s," , mmtt_dump_column_width[ i ] + 1 ,
               "                                        "
               "                                        " ) ;
    }
    else if ( stack_data->named ) {
#if defined( MM_USE_MONITOR_WINDOW )
      monitorf(( uint8 * )
#else
      fprintf( watch_fileptr,
#endif
               "%*s," , mmtt_dump_column_width[ i ] + 1 ,
               stack_data->u.name ) ;
    }
    else {
#if defined( MM_USE_MONITOR_WINDOW )
      monitorf(( uint8 * )
#else
      fprintf( watch_fileptr,
#endif
               "%*u," , mmtt_dump_column_width[ i ] + 1 ,
               CAST_SIZET_TO_UINT32( stack_data->u.number )) ;
    }
  }

#if defined( MM_USE_MONITOR_WINDOW )
  monitorf(( uint8 * )
#else
  fprintf( watch_fileptr,
#endif
           "%*u,%*u\n" , mmtt_dump_column_width[ 5 ] + 1 ,
           CAST_SIZET_TO_UINT32( node_data->count ) ,
           mmtt_dump_column_width[ 6 ] + 1 ,
           CAST_SIZET_TO_UINT32( node_data->size_total )) ;

  if ( node_data->subtree != NULL ) {
    mmtt_stack_size++ ;
    ( void )rbt_walk( node_data->subtree , mmtt_dump_output_callback , NULL ) ;
    mmtt_stack_size-- ;
  }

  return TRUE ;
}

/** Dump the current totals tree to the log (or monitor window, if
    \c MM_USE_MONITOR_WINDOW is defined). */

void mm_totals_tree_dump( void )
{
  if ( mm_totals_tree_root != NULL && ! rbt_is_empty( mm_totals_tree_root )) {
#if ! defined( MM_USE_MONITOR_WINDOW )
    /* If we don't have a file already opened, then open it.
     * Note that we never actually close this file...
     */
    if ( ! watch_fileptr ) {
      watch_fileptr = fopen( watch_filename, "w" ) ;
      if ( ! watch_fileptr ) {
        HQFAIL( "Couldn't open mm watch file" ) ;
        return ;
      }
    }
#endif

    /* Initialise column widths - must take account of the fact that
       the heading might be wider than the data. */

    mmtt_dump_column_width[ 0 ] = 4 ;
    mmtt_dump_column_width[ 1 ] = 5 ;
    mmtt_dump_column_width[ 2 ] = 4 ;
    mmtt_dump_column_width[ 3 ] = 4 ;
    mmtt_dump_column_width[ 4 ] = 4 ;
    mmtt_dump_column_width[ 5 ] = 5 ;
    mmtt_dump_column_width[ 6 ] = 5 ;

    /* Now figure out how wide each column needs to be. */

    mmtt_stack_size = 0 ;
    ( void )rbt_walk( mm_totals_tree_root , mmtt_dump_counts_callback , NULL ) ;

    mmtt_dump_depth = 0 ;

    if ( mm_totals_tree_flags.by_pool ) {
#if defined( MM_USE_MONITOR_WINDOW )
      monitorf(( uint8 * )
#else
      fprintf( watch_fileptr,
#endif
               "%*s," , mmtt_dump_column_width[ mmtt_dump_depth++ ] + 1 ,
               "Pool" ) ;
    }

    if ( mm_totals_tree_flags.by_class ) {
#if defined( MM_USE_MONITOR_WINDOW )
      monitorf(( uint8 * )
#else
      fprintf( watch_fileptr,
#endif
               "%*s," , mmtt_dump_column_width[ mmtt_dump_depth++ ] + 1 ,
               "Class" ) ;
    }

    if ( mm_totals_tree_flags.by_file ) {
#if defined( MM_USE_MONITOR_WINDOW )
      monitorf(( uint8 * )
#else
      fprintf( watch_fileptr,
#endif
               "%*s," , mmtt_dump_column_width[ mmtt_dump_depth++ ] + 1 ,
               "File" ) ;
    }

    if ( mm_totals_tree_flags.by_line ) {
#if defined( MM_USE_MONITOR_WINDOW )
      monitorf(( uint8 * )
#else
      fprintf( watch_fileptr,
#endif
               "%*s," , mmtt_dump_column_width[ mmtt_dump_depth++ ] + 1 ,
               "Line" ) ;
    }

    if ( mm_totals_tree_flags.by_size ) {
#if defined( MM_USE_MONITOR_WINDOW )
      monitorf(( uint8 * )
#else
      fprintf( watch_fileptr,
#endif
               "%*s," , mmtt_dump_column_width[ mmtt_dump_depth++ ] + 1 ,
               "Size" ) ;
    }

#if defined( MM_USE_MONITOR_WINDOW )
      monitorf(( uint8 * )
#else
      fprintf( watch_fileptr,
#endif
               "%*s,%*s\n" , mmtt_dump_column_width[ 5 ] + 1 , "Count" ,
               mmtt_dump_column_width[ 6 ] + 1 , "Total" ) ;

    mmtt_stack_size = 0 ;
    ( void )rbt_walk( mm_totals_tree_root , mmtt_dump_output_callback , NULL ) ;
  }

  /* I know that's all for now: might as well do a flush. */

  ( void )mm_trace_flush() ;
}

/** Initialise the watcher code. */

void mm_watch_init( void )
{
  /* Uncomment this line to turn on watching of all MM operations. */
  /* debug_mm_watchlevel = DEBUG_MM_WATCH_ALL ; */

#if defined ( MM_DEBUG_WATCH_TOTALS )
  ( void )mm_debug_watch( mm_totals_tree ) ;
#else
  /* If mm watching is enabled (compiled in - see mm/export/mm.h) then
   * install a stock mm watch function for the mm_alloc, mm_free and,
   * internally, mm_truncate events (this also includes the promise code
   * in this glue layer which may call these routines).
   */
  ( void )mm_debug_watch( mm_trace ) ;
#endif
}

/** Finish the watcher code. Currently does nothing. */

void mm_watch_finish( void )
{
}


#endif /* MM_DEBUG_WATCH */


void init_C_globals_mmwatch(void)
{
#ifdef MM_DEBUG_WATCH
  struct mm_totals_tree_flags flagsinit = { 1, 1, 1, 1, 1, 1 } ;

  mm_debug_watcher = NULL ;
  debug_mm_watchlevel = 0 ;
  watch_fileptr = NULL ;
  mm_totals_tree_root = NULL ;
  mm_totals_tree_flags = flagsinit ;
  HqMemSetPtr(mmtt_dump_stack, NULL, NUM_ARRAY_ITEMS(mmtt_dump_stack)) ;
  mmtt_dump_column_width[ 0 ] = 0 ;
  mmtt_dump_column_width[ 0 ] = 1 ;
  mmtt_dump_column_width[ 0 ] = 2 ;
  mmtt_dump_column_width[ 0 ] = 3 ;
  mmtt_dump_column_width[ 0 ] = 4 ;
  mmtt_dump_column_width[ 0 ] = 5 ;
  mmtt_dump_column_width[ 0 ] = 6 ;
  mmtt_dump_depth = 0 ;
  mmtt_stack_size = 0 ;
#endif /* MM_DEBUG_WATCH */
}

/*
* Log stripped */
