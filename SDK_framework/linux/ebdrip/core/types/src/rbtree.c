/** \file
 * \ingroup types
 *
 * $HopeName: COREtypes!src:rbtree.c(EBDSDK_P.1) $
 * $Id: src:rbtree.c,v 1.11.1.1.1.1 2013/12/19 11:25:08 anon Exp $
 *
 * Copyright (C) 1992-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * General red-black binary tree routines. A red-black binary tree is
 * the same as an ordinary binary tree, with the extra rules:
 *
 * 1. Every node is either red or black.
 * 2. Every leaf (nil) is black.
 * 3. If a node is red, then both of its children are black.
 * 4. Every simple path from a node to a descendant leaf contains the
 *    same number of black nodes (the "black height" is uniform).
 *
 * Based on the Tome "Introduction to Algorithms" by Cormen, Leiserson
 * and Rivest. A cracking bedtime read.
 */

#include "std.h"

#include "hqmemcmp.h"
#include "hqmemcpy.h"
#include "rbtree.h"
#include "monitor.h"

/** Red black binary tree node. */

struct rbt_node {
  /** Key: must be unique for this tree. */
  uintptr_t key ;

  /** Left descendent. */
  /*@owned@*/
  struct rbt_node *left ;

  /** Right descendent. */
  /*@owned@*/
  struct rbt_node *right ;

  /** Parent. NULL here means that this node is the root of the
      tree. */
  /*@dependent@*/
  struct rbt_node *p ;

  /** Private data - opaque pointer to what this node represents. */
  /*@only@*/
  void *data ;

  /** Color: for ease, this is a boolean - TRUE for red, FALSE for
      black. */
  Bool red ;
} ;

/** Red black binary tree root. */

struct rbt_root {
  /** The root node. */
  /*@dependent@*/
  RBT_NODE *node ;

  /** The nil sentinel for this tree. Stands in for nil[T] in
      rbt_remove etc. Nodes point to this instead of having NULL
      pointers. The only field that matters is its color: it's always
      black. */
  RBT_NODE nil ;

  /** Current node count. Easy to calculate with a walk, of course,
      but it seems worth 4 bytes to just store the live value here. */
  uint32 count ;

  /** Key comparison function. Returns < 0 if key1 is less than key2,
      0 if key1 is equivalent to key2 and > 0 if key1 is greater than
      key2. */
  rbt_compare_keys_fn compare_keys ;

  /** Hook for the tree code to call when it wants to allocate
      memory. */
  rbt_alloc_fn alloc_fn ;

  /** Hook for the tree code to call when it wants to free memory. */
  rbt_free_fn free_fn ;

  /** The length of each key in the tree. Used in the case of raw
      binary keys. */
  mm_size_t key_length ;

  /** The size of any implementation-specific data to be associated
      with each node, with the memory allocated alongside the node
      itself. Note that it's also possible for the node data pointer
      to reference memory which is allocated elsewhere. In that case,
      this value should be zero so that we won't try to free it when
      the tree is disposed of. */
  mm_size_t node_data_size ;

  /** Private data for the tree. */
  /*@only@*/
  void *data ;
} ;

/** Red black tree key comparison callback for numbers. */

int32 rbt_compare_integer_keys( RBT_ROOT *root , uintptr_t key1 ,
                                uintptr_t key2 )
{
  UNUSED_PARAM( RBT_ROOT * , root ) ;

  if ( key1 < key2 ) {
    return -1 ;
  }
  else if ( key1 > key2 ) {
    return 1 ;
  }
  else {
    return 0 ;
  }
}

/** Red black tree key comparison callback for strings. */

int32 rbt_compare_string_keys( RBT_ROOT *root , uintptr_t key1 ,
                               uintptr_t key2 )
{
  UNUSED_PARAM( RBT_ROOT * , root ) ;

  return strcmp(( const char * )key1 , ( const char * )key2 ) ;
}

/** Red black tree comparison callback for raw binary keys. */

int32 rbt_compare_binary_keys( RBT_ROOT *root , uintptr_t key1 ,
                               uintptr_t key2 )
{
  return HqMemCmp(( const uint8 * )key1 ,
                  CAST_SIZET_TO_INT32( root->key_length ) ,
                  ( const uint8 * )key2 ,
                  CAST_SIZET_TO_INT32( root->key_length )) ;
}

/** Free the given node. It must already have been removed from the
    tree. In case you're wondering, that's deliberate: it's so that a
    caller can remove a node from the tree (\c rbt_remove returns a
    pointer to the newly-removed node) and stuff new values in before
    calling rbt_insert once more - all without the memory churn that
    would happen if allocations were tied to inserts and removals led
    always to frees. */

void rbt_free_node( RBT_ROOT *root , /*@null@*/ /*@only@*/ RBT_NODE *node )
{
  if ( node != NULL ) {
    if ( root->key_length > 0 && node->key != 0 ) {
      root->free_fn(( mm_addr_t )node->key , root->key_length ,
                    root->data ) ;
    }

    if ( root->node_data_size > 0 && node->data != NULL ) {
      root->free_fn( node->data , root->node_data_size , root->data ) ;
    }

    root->free_fn( node , sizeof( RBT_NODE ) , root->data ) ;
  }
}

/** Allocate a node, with the given space for private data. */

RBT_NODE *rbt_allocate_node( /*@in@*/ RBT_ROOT *root , uintptr_t key ,
                             void *data )
{
  RBT_NODE *node = root->alloc_fn( sizeof( RBT_NODE ) , root->data ) ;

  if ( node == NULL ) {
    return NULL ;
  }

  /* Allocate the node data if necessary. I know these allocations
     could be rolled into a single one, but the jiggery pokery to get
     proper alignment would make it too crufty. */

  if ( root->node_data_size > 0 ) {
    node->data = root->alloc_fn( root->node_data_size , root->data ) ;

    if ( node->data == NULL ) {
      rbt_free_node( root , node ) ;
      return NULL ;
    }
  }

  rbt_set_node_data( root , node , data ) ;

  /* Non-zero key length implies a binary key that needs space to be
     stored. Otherwise the key can be copied by reference or is an
     integer. */

  if ( root->key_length > 0 ) {
    node->key = ( uintptr_t )root->alloc_fn( root->key_length , root->data ) ;

    if ( node->key == ( uintptr_t )NULL ) {
      rbt_free_node( root , node ) ;
      return NULL ;
    }
  }

  rbt_set_node_key( root , node , key ) ;

  return node ;
}

/** Accessor function for the root node of a tree. */

RBT_NODE *rbt_root_node( RBT_ROOT *root )
{
  return root->node ;
}

/** Returns a pointer to the key for a node. */

uintptr_t rbt_get_node_key( /*@in@*/ RBT_NODE *node )
{
  return node->key ;
}

/** Setter function for the key of a node. Copies by reference or by
    value depending on the value of key_length in the root. Naturally,
    this will corrupt the tree if the node is already inserted, but it
    could be used to change the key of a node which has been removed
    and is about to be reinserted. */

void rbt_set_node_key( /*@in@*/ RBT_ROOT *root , /*@in@*/ RBT_NODE *node ,
                       uintptr_t key )
{
  if ( root->key_length > 0 ) {
    HqMemCpy( node->key , key , CAST_SIZET_TO_INT32( root->key_length )) ;
  }
  else {
    node->key = key ;
  }
}

/** Returns a pointer to the private data hanging off a node. */

void *rbt_get_node_data( /*@in@*/ RBT_NODE *node )
{
  return node->data ;
}

/** Setter function for the private data hanging off a node. Copies by
    reference or by value depending on the value of node_data_size in
    the root. */

void rbt_set_node_data( /*@in@*/ RBT_ROOT *root , /*@in@*/ RBT_NODE *node ,
                        /*@null@*/ void *data )
{
  if ( root->node_data_size > 0 ) {
    /* Seems bizarre to allow data to be null, but doing it this way
       allows for allocating a data slot in each node which is
       populated lazily, and that's handy sometimes. */

    if ( data != NULL ) {
      HqMemCpy( node->data , data ,
                CAST_SIZET_TO_INT32( root->node_data_size )) ;
    }
  }
  else {
    node->data = data ;
  }
}

/** Ordinary binary tree insert - also used as the first phase of the
    red-black binary tree insert.

    Only \c z->key needs be initialised on entry. */

static void bt_insert( /*@in@*/ RBT_ROOT *root , /*@kept@*/ RBT_NODE *z )
{
  RBT_NODE *x = root->node ;
  RBT_NODE *y = &( root->nil ) ;

  z->left = &( root->nil ) ;
  z->right = &( root->nil ) ;
  z->p = &( root->nil ) ;

  while ( x != &( root->nil ) ) {
    y = x ;

    if ( root->compare_keys( root , z->key , x->key ) < 0 ) {
      x = x->left ;
    }
    else {
      HQASSERT( root->compare_keys( root , z->key , x->key ) != 0 ,
                "Key clash!" ) ;
      x = x->right ;
    }
  }

  z->p = y ;
  if ( y == &( root->nil ) ) {
    root->node = z ;
  }
  else if ( root->compare_keys( root , z->key , y->key ) < 0 ) {
    y->left = z ;
  }
  else {
    HQASSERT( root->compare_keys( root , z->key , y->key ) != 0 ,
              "Key clash!" ) ;
    y->right = z ;
  }
}

/** Search for the given key in a binary tree. */

RBT_NODE *rbt_search( RBT_ROOT *root , uintptr_t key )
{
  RBT_NODE *node = root->node ;

  while ( node != &( root->nil ) ) {
    if ( root->compare_keys( root , node->key , key ) == 0 ) {
      return node ;
    }
    else {
      if ( root->compare_keys( root , key , node->key ) < 0 ) {
        node = node->left ;
      }
      else {
        node = node->right ;
      }
    }
  }

  return NULL ;
}

/** Find the node with the minimum key value at or below the given
    node (i.e. this can be called with the root node of a tree, or a
    subtree). */

RBT_NODE *rbt_minimum( RBT_ROOT *root , RBT_NODE *x )
{
  RBT_NODE *node = x ;

  while ( node->left != &( root->nil ) ) {
    node = node->left ;
  }

  return node ;
}

/** Find the node with the maximum key value at or below the given
    node (i.e. this can be called with the root node of a tree, or a
    subtree). */

RBT_NODE *rbt_maximum( RBT_ROOT *root , RBT_NODE *x )
{
  RBT_NODE *node = x ;

  while ( node->right != &( root->nil ) ) {
    node = node->right ;
  }

  return node ;
}

/** Find the next node in sequence after the given one (if any). */

RBT_NODE *rbt_successor( RBT_ROOT *root , RBT_NODE *x )
{
  if ( x != &( root->nil ) ) {
    if ( x->right != &( root->nil ) ) {
      return rbt_minimum( root , x->right ) ;
    }
    else {
      RBT_NODE *y = x->p ;

      while ( y != &( root->nil ) && x == y->right ) {
        x = y ;
        y = y->p ;
      }

      return y ;
    }
  }

  return NULL ;
}

/** Find the node previous to the given one (if any). */

RBT_NODE *rbt_predeccessor( RBT_ROOT *root , RBT_NODE *x )
{
  if ( x != &( root->nil ) ) {
    if ( x->left != &( root->nil ) ) {
      return rbt_maximum( root , x->left ) ;
    }
    else {
      RBT_NODE *y = x->p ;

      while ( y != &( root->nil ) && x == y->left ) {
        x = y ;
        y = y->p ;
      }

      return y ;
    }
  }

  return NULL ;
}

/** Simple: returns a flag indicating whether the tree is empty. */

Bool rbt_is_empty( RBT_ROOT *root )
{
  return ( root->node == & root->nil ) ;
}

/** Returns the number of nodes currently in the tree. */

uint32 rbt_node_count( RBT_ROOT *root )
{
  return root->count ;
}

#if defined( DEBUG_BUILD )
/** Dump the keys of the tree, in ascending order. */

void rbt_dump( RBT_ROOT *root , RBT_NODE *node , uint32 level )
{
  if ( node != &( root->nil ) ) {
    uint32 i ;
    HQASSERT( node != node->right && node != node->left ,
              "bt_dump is going to go infinite!" ) ;
    rbt_dump( root , node->right , level + 1 ) ;
    for ( i = 0 ; i < level ; i++ ) {
      monitorf(( uint8* )" " ) ;
    }
    monitorf(( uint8* )"%s%08x\n" , node->red ? "R" : "B" , node->key ) ;
    rbt_dump( root , node->left , level + 1 ) ;
  }
}
#endif

/** Assert the validity of the tree, recursively, under the given
    node. Only really needed if you're tinkering with the rbt code
    itself. */

#define rbt_validate( _root , _node , _count ) EMPTY_STATEMENT()

#if defined( ASSERT_BUILD )
STATIC Bool rbt_debug = FALSE ;

#if defined( SLOW_BUT_SURE )
#undef rbt_validate
static uint32 rbt_validate( /*@in@*/ RBT_ROOT *root ,
                            /*@in@*/ RBT_NODE *node ,
                            /*@out@*/ uint32 *count )
{
  uint32 black_height_left ;
  uint32 black_height_right ;
  uint32 lcount = 0 ;

  HQASSERT( node != NULL , "Null node: shouldn't happen" ) ;

  if ( node == &( root->nil ) ) {
    /* Note: the "p" member of the sentinel could be anything, since
       it is set during deletion operations. */
    HQASSERT( node->key == ( uintptr_t )0 &&
              node->data == NULL &&
              node->left == NULL &&
              node->right == NULL &&
              node->red == FALSE ,
              "Something's stomped on the sentinel! "
              "(Not a phrase you hear every day)" ) ;

    return 0 ;
  }

  if ( count == NULL ) {
    count = & lcount ;
  }

  count++ ;

  HQASSERT( node->p != NULL && node->left != NULL && node->right != NULL ,
            "There should be no NULL pointers: point to sentinel instead." ) ;

  if ( node->red ) {
    HQASSERT( ! node->left->red && ! node->right->red ,
              "Red nodes must have 2 black descendents." ) ;
  }

  if ( node->left != &( root->nil ) ) {
    HQASSERT( root->compare_keys( root , node->left->key , node->key ) < 0 ,
              "Key order is wrong!" ) ;
  }

  if ( node->right != &( root->nil ) ) {
    HQASSERT( root->compare_keys( root , node->right->key , node->key ) > 0 ,
              "Key order is wrong!" ) ;
  }

  black_height_left = rbt_validate( root , node->left , count ) ;
  black_height_right = rbt_validate( root , node->right , count ) ;

  HQASSERT( black_height_right == black_height_left ,
            "Black heights must match." ) ;

  HQASSERT( lcount == 0 || lcount == root->count ,
            "Node count does not validate" ) ;

  return black_height_left + ( node->red ? 0 : 1 ) ;
}
#endif /* SLOW_BUT_SURE */
#endif /* ASSERT_BUILD */

/** Static recursion function for rbt_walk. If the callback returns
    FALSE then the walk stops and we return FALSE also. */

static Bool rbt_walk_internal( RBT_ROOT *root , RBT_NODE *node ,
                               Bool ( *callback )( RBT_ROOT *root ,
                                                   RBT_NODE *node ,
                                                   void *walk_data ) ,
                               void *walk_data )
{
  if ( node != &( root->nil ) ) {
    RBT_NODE *right = node->right ;
    HQASSERT( node != node->right && node != node->left ,
              "bt_walk is going to go infinite!" ) ;
    rbt_walk_internal( root , node->left , callback , walk_data ) ;
    if ( ! ( *callback )( root , node , walk_data )) {
      return FALSE ;
    }
    rbt_walk_internal( root , right , callback , walk_data ) ;
  }

  return TRUE ;
}

/** Walk each element of the tree in turn. Not safe for the callback
    to alter the structure of the tree, but it's OK if we're freeing
    the whole tree at once. This is the fastest way to enumerate every
    node, but nodes will be walked in an undefined order. */

Bool rbt_walk( RBT_ROOT *root ,
               Bool ( *callback )( RBT_ROOT *root , RBT_NODE *node ,
                                   void *walk_data ) ,
               void *walk_data )
{
  rbt_validate( root , root->node , NULL ) ;
  if ( ! rbt_walk_internal( root , root->node , callback , walk_data )) {
    return FALSE ;
  }
  rbt_validate( root , root->node , NULL ) ;

  return TRUE ;
}

/* ************************************************************************ */

/* Now for the red-black binary tree - most operations complete in O(lg n). */

/** Initialise the root of the tree. */

/*@out@*/ RBT_ROOT *rbt_init( void *data ,
                              rbt_alloc_fn alloc_fn ,
                              rbt_free_fn free_fn ,
                              rbt_compare_keys_fn compare_keys ,
                              mm_size_t key_length ,
                              mm_size_t node_data_size )
{
  RBT_ROOT *root = alloc_fn( sizeof( RBT_ROOT ) , data ) ;

  if ( root == NULL ) {
    return NULL ;
  }

  root->node = &( root->nil ) ;
  root->nil.key = ( uintptr_t )0 ;
  root->nil.left = NULL ;
  root->nil.right = NULL ;
  root->nil.p = NULL ;
  root->nil.data = NULL ;
  root->nil.red = FALSE ;
  root->count = 0 ;
  root->compare_keys = compare_keys ;
  root->alloc_fn = alloc_fn ;
  root->free_fn = free_fn ;
  root->key_length = key_length ;
  root->node_data_size = node_data_size ;
  root->data = data ;

  return root ;
}

/** Getter for the private data from the root. */

void *rbt_get_root_data( /*@in@*/ RBT_ROOT *root )
{
  return root->data ;
}

/** Callback function for rbt_walk used in disposing of a tree. */

static Bool rbt_dispose_node( RBT_ROOT *root , RBT_NODE *node , void *data )
{
  UNUSED_PARAM( void * , data ) ;
  rbt_free_node( root , node ) ;
  return TRUE ;
}

/** Dispose of an entire tree. Safe if it's null. */

void rbt_dispose( /*@null@*/ /*@only@*/ RBT_ROOT *root )
{
  if ( root != NULL ) {
    ( void )rbt_walk( root , rbt_dispose_node , root->data ) ;

    root->free_fn( root , sizeof( RBT_ROOT ) , root->data ) ;
  }
}

/** Red black binary tree Left Rotate method. Used after insertion as
    part of the rebalancing process. */

static void rbt_leftrotate( /*@in@*/ RBT_ROOT *root ,
                            /*@in@*/ RBT_NODE *x )
{
  RBT_NODE *y = x->right ;

  HQASSERT( y != &( root->nil ) , "Right descendent shouldn't be a leaf." ) ;

  HQTRACE( rbt_debug ,
           ( "rbt_leftrotate: [%x] %x , %x" , root , x->key , y->key )) ;

  /* Turn y's left subtree into x's right subtree. */

  x->right = y->left ;
  if ( y->left != &( root->nil ) ) {
    y->left->p = x ;
  }

  /* Link x's parent to y. */

  y->p = x->p ;
  if ( x->p == &( root->nil ) ) {
    root->node = y ;
  }
  else {
    if ( x == x->p->left ) {
      x->p->left = y ;
    }
    else {
      x->p->right = y ;
    }
  }

  /* Put x on y's left. */

  y->left = x ;
  x->p = y ;
}

/** Red black binary tree Right Rotate method. Used after insertion as
    part of the rebalancing process. */

static void rbt_rightrotate( /*@in@*/ RBT_ROOT *root ,
                             /*@in@*/ RBT_NODE *x )
{
  RBT_NODE *y = x->left ;

  HQASSERT( y != &( root->nil ) , "Left descendent shouldn't be a leaf." ) ;

  HQTRACE( rbt_debug ,
           ( "rbt_rightrotate: [%x] %x , %x" , root , x->key , y->key )) ;

  /* Turn y's right subtree into x's left subtree. */

  x->left = y->right ;
  if ( y->right != &( root->nil ) ) {
    y->right->p = x ;
  }

  /* Link x's parent to y. */

  y->p = x->p ;
  if ( x->p == &( root->nil ) ) {
    root->node = y ;
  }
  else {
    if ( x == x->p->right ) {
      x->p->right = y ;
    }
    else {
      x->p->left = y ;
    }
  }

  /* Put x on y's right. */

  y->right = x ;
  x->p = y ;
}

/** Red black binary tree Insert method. Basically this is an ordinary
    insert followed by a process which re-balances the tree, making
    sure the rules for red-black binary trees hold once the balancing
    is done.

    Only \c x->key needs to be initialised on entry. */

void rbt_insert( /*@in@*/ RBT_ROOT *root , RBT_NODE *x )
{
  RBT_NODE *y ;

  rbt_validate( root , root->node , NULL ) ;

  HQTRACE( rbt_debug ,
           ( "rbt_insert: [%x] %x" , root , x->key )) ;

  bt_insert( root , x ) ;

  x->red = TRUE ;

  while ( x != root->node && x->p->red ) {
    if ( x->p == x->p->p->left ) {
      y = x->p->p->right ;

      if ( y->red ) {
        x->p->red = FALSE ;
        y->red = FALSE ;
        x->p->p->red = TRUE ;
        x = x->p->p ;
      }
      else {
        if ( x == x->p->right ) {
          x = x->p ;
          rbt_leftrotate( root , x ) ;
        }

        x->p->red = FALSE ;
        x->p->p->red = TRUE ;
        rbt_rightrotate( root , x->p->p ) ;
      }
    }
    else {
      y = x->p->p->left ;

      if ( y->red ) {
        x->p->red = FALSE ;
        y->red = FALSE ;
        x->p->p->red = TRUE ;
        x = x->p->p ;
      }
      else {
        if ( x == x->p->left ) {
          x = x->p ;
          rbt_rightrotate( root , x ) ;
        }

        x->p->red = FALSE ;
        x->p->p->red = TRUE ;
        rbt_leftrotate( root , x->p->p ) ;
      }
    }
  }

  root->node->red = FALSE ;
  root->count++ ;

  rbt_validate( root , root->node , NULL ) ;
}

/** Rebalance the tree after a deletion */

static void rbt_remove_fixup( RBT_ROOT *root , RBT_NODE *x )
{
  RBT_NODE *w ;

  while ( x != root->node && ! x->red ) {
    HQTRACE( rbt_debug ,
             ( "rbt_remove_fixup: [%x] %x" , root , x->key )) ;

    if ( x == x->p->left ) {
      w = x->p->right ;

      if ( w->red ) {
        w->red = FALSE ;
        x->p->red = TRUE ;
        rbt_leftrotate( root , x->p ) ;
        w = x->p->right ;
      }

      if ( ! w->left->red && ! w->right->red ) {
        w->red = TRUE ;
        x = x->p ;
      }
      else {
        if ( ! w->right->red ) {
          w->left->red = FALSE ;
          w->red = TRUE ;
          rbt_rightrotate( root , w ) ;
          w = x->p->right ;
        }

        w->red = x->p->red ;
        x->p->red = FALSE ;
        w->right->red = FALSE ;
        rbt_leftrotate( root , x->p ) ;
        x = root->node ;
      }
    }
    else {
      w = x->p->left ;

      if ( w->red ) {
        w->red = FALSE ;
        x->p->red = TRUE ;
        rbt_rightrotate( root , x->p ) ;
        w = x->p->left ;
      }

      if ( ! w->right->red && ! w->left->red ) {
        w->red = TRUE ;
        x = x->p ;
      }
      else {
        if ( ! w->left->red ) {
          w->right->red = FALSE ;
          w->red = TRUE ;
          rbt_leftrotate( root , w ) ;
          w = x->p->left ;
        }

        w->red = x->p->red ;
        x->p->red = FALSE ;
        w->left->red = FALSE ;
        rbt_rightrotate( root , x->p ) ;
        x = root->node ;
      }
    }
  }

  x->red = FALSE ;

  HQTRACE( rbt_debug ,
           ( "rbt_remove_fixup complete: [%x] %x" , root , x->key )) ;

  rbt_validate( root , root->node , NULL ) ;
}

/** Red black binary tree remove method (Called "delete" in the
    reference algorithms, but I wanted to make clear that this
    operation does not free the node structure). Does an ordinary
    binary tree delete, then balances the tree back up if necessary so
    it complies with The Rules. Returns the node ready for its memory
    to be freed if appropriate: it's important to realise that this
    might not be the location of the node which was passed in. Note
    that \c "x->p = y->p" can, by design, end up modifying the
    sentinel node.

    Only \c z->key needs to be initialised on entry. */

RBT_NODE *rbt_remove( RBT_ROOT *root , RBT_NODE *z )
{
  RBT_NODE *x ;
  RBT_NODE *y ;

  if ( z == &( root->nil ) ) {
    return NULL ;
  }

  HQTRACE( rbt_debug ,
           ( "rbt_remove: [%x] %x" , root , z->key )) ;

  if ( z->left == &( root->nil ) || z->right == &( root->nil ) ) {
    y = z ;
  }
  else {
    y = rbt_successor( root , z ) ;
  }

  HQASSERT( y != NULL , "Should rbt_successor return NULL?" ) ;

  if ( y->left != &( root->nil ) ) {
    x = y->left ;
  }
  else {
    x = y->right ;
  }

  x->p = y->p ;

  if ( y->p == &( root->nil ) ) {
    root->node = x ;
  }
  else {
    if ( y == y->p->left ) {
      y->p->left = x ;
    }
    else {
      y->p->right = x ;
    }
  }

  if ( y != z ) {
    uintptr_t tk ;
    void *td ;

    /* The example pseudo-code just copies data and "other fields"
       from y to z. That's not good enough when any one of them is a
       pointer: we end up with one data struct being doubly referenced
       and the other one orphaned. Solution: swap the pointers
       instead. */

    tk = z->key ;
    z->key = y->key ;
    y->key = tk ;

    td = z->data ;
    z->data = y->data ;
    y->data = td ;
  }

  if ( y->red == FALSE ) {
    rbt_remove_fixup( root , x ) ;
  }

  root->count-- ;
  rbt_validate( root , root->node , NULL ) ;

  return y ;
}

/***********************************************************************
  Simple RB trees come in handy sometimes: they are defined as trees
  whose only data are the keys themselves.
*/

/*! \brief Allocation callback for Simple tree nodes. */

/*@null@*/ /*@out@*/ /*@only@*/
static mm_addr_t rbtsimple_alloc( mm_size_t size , /*@null@*/ void *data )
{
  mm_addr_t result = mm_alloc( mm_pool_temp , size ,
                               MM_ALLOC_CLASS_RBT_SIMPLE_TREE ) ;

  UNUSED_PARAM( void * , data ) ;

  return result ;
}

/*! \brief Free callback for Simple tree nodes. */

static void rbtsimple_free( /*@out@*/ /*@only@*/ mm_addr_t what ,
                            mm_size_t size , /*@null@*/ void *data )
{
  UNUSED_PARAM( void * , data ) ;
  mm_free( mm_pool_temp , what , size ) ;
}

/** Make a new Simple RB Tree. */

RBT_ROOT *rbtsimple_new_tree( rbt_compare_keys_fn compare_keys ,
                              uint32 key_length , /*@null@*/ void *data )
{
  return rbt_init( data , rbtsimple_alloc , rbtsimple_free ,
                   compare_keys , key_length , 0 ) ;
}

/** Make sure there's a node in the tree with the given key. If it's
    already there, there's nothing to do. */

Bool rbtsimple_create_node( /*@in@*/ RBT_ROOT *root , uintptr_t key )
{
  RBT_NODE *node = rbt_search( root , key ) ;

  if ( node == NULL ) {
    node = rbt_allocate_node( root , key , NULL ) ;

    if ( node == NULL ) {
      return FALSE ;
    }

    rbt_insert( root , node ) ;
  }

  return TRUE ;
}

/* Log stripped */
