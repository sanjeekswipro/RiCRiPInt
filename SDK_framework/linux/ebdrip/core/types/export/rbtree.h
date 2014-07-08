/** \file
 * \ingroup types
 *
 * $HopeName: COREtypes!export:rbtree.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * A set of generally useful binary tree implementations.
 * Assumes std.h has been included.
 */

#ifndef __RBTREE_H__
#define __RBTREE_H__

#include "coretypes.h"
#include "mm.h"

typedef struct rbt_root RBT_ROOT ;
typedef struct rbt_node RBT_NODE ;

/** Function type for comparing keys in a red-black tree. */
typedef int32 ( *rbt_compare_keys_fn )( /*@in@*/ RBT_ROOT *root ,
                                        uintptr_t key1 , uintptr_t key2 ) ;

/** Function type for the tree code to call when it wants to allocate
    memory. The private opaque data pointer from the tree root is
    passed in. */
typedef /*@null@*/ /*@out@*/ /*@only@*/ mm_addr_t
( *rbt_alloc_fn )( mm_size_t size , /*@null@*/ void *data )
  /*@ensures MaxSet(result) == (size - 1); @*/ ;

/** Function type for the tree code to call when it wants to free
    memory. The private opaque data pointer from the tree root is
    passed in. */
typedef void ( *rbt_free_fn )( /*@reldef@*/ /*@only@*/ mm_addr_t what ,
                               mm_size_t size , /*@null@*/ void *data ) ;

/** Callback for walking trees - called once per node. */
typedef Bool ( *rbt_callback_fn )( /*@in@*/ RBT_ROOT *root ,
                                   /*@in@*/ RBT_NODE *node ,
                                   /*@null@*/ void *walk_data ) ;

int32 rbt_compare_integer_keys( /*@in@*/ RBT_ROOT *root , uintptr_t key1 ,
                                uintptr_t key2 ) ;
int32 rbt_compare_string_keys( /*@in@*/ RBT_ROOT *root , uintptr_t key1 ,
                               uintptr_t key2 ) ;
int32 rbt_compare_binary_keys( /*@in@*/ RBT_ROOT *root , uintptr_t key1 ,
                               uintptr_t key2 ) ;

void rbt_free_node( RBT_ROOT *root , /*@null@*/ /*@only@*/ RBT_NODE *node ) ;
/*@null@*/ /*@out@*/ /*@only@*/
RBT_NODE *rbt_allocate_node( /*@in@*/ RBT_ROOT *root , uintptr_t key ,
                             /*@null@*/ void *data ) ;

/*@dependent@*/ /*@out@*/
RBT_NODE *rbt_root_node( RBT_ROOT *root ) ;
uintptr_t rbt_get_node_key( /*@in@*/ RBT_NODE *node ) ;
void rbt_set_node_key( /*@in@*/ RBT_ROOT *root , /*@in@*/ RBT_NODE *node ,
                       uintptr_t key ) ;
/*@dependent@*/ void *rbt_get_node_data( /*@in@*/ RBT_NODE *node ) ;
void rbt_set_node_data( /*@in@*/ RBT_ROOT *root , /*@in@*/ RBT_NODE *node ,
                        /*@null@*/ void *data ) ;

/*@out@*/ RBT_ROOT *rbt_init( /*@null@*/ void *data ,
                              rbt_alloc_fn alloc_fn ,
                              rbt_free_fn free_fn ,
                              rbt_compare_keys_fn compare_keys ,
                              mm_size_t key_length ,
                              mm_size_t node_data_size ) ;
/*@dependent@*/ void *rbt_get_root_data( /*@in@*/ RBT_ROOT *root ) ;
void rbt_dispose( /*@null@*/ /*@only@*/ RBT_ROOT *root ) ;
void rbt_insert( /*@in@*/ RBT_ROOT *root , /*@in@*/ RBT_NODE *x ) ;
/*@only@*/ RBT_NODE *rbt_remove( /*@in@*/ RBT_ROOT *root ,
                                 /*@in@*/ RBT_NODE *z ) ;
/*@dependent@*/ RBT_NODE *rbt_search( /*@in@*/ RBT_ROOT *root ,
                                      uintptr_t key ) ;
/*@dependent@*/ RBT_NODE *rbt_minimum( /*@in@*/ RBT_ROOT *root ,
                                       /*@in@*/ RBT_NODE *x ) ;
/*@dependent@*/ RBT_NODE *rbt_maximum( /*@in@*/ RBT_ROOT *root ,
                                       /*@in@*/ RBT_NODE *x ) ;
/*@dependent@*/ RBT_NODE *rbt_successor( /*@in@*/ RBT_ROOT *root ,
                                         /*@in@*/ RBT_NODE *x ) ;
/*@dependent@*/ RBT_NODE *rbt_predeccessor( /*@in@*/ RBT_ROOT *root ,
                                            /*@in@*/ RBT_NODE *x ) ;
Bool rbt_is_empty( RBT_ROOT *root ) ;
uint32 rbt_node_count( RBT_ROOT *root ) ;

#if defined( DEBUG_BUILD )
void rbt_dump( /*@in@*/ RBT_ROOT *root , /*@in@*/ RBT_NODE *node ,
               uint32 level ) ;
#endif

Bool rbt_walk( /*@in@*/ RBT_ROOT *root , rbt_callback_fn callback ,
               /*@null@*/ /*@reldef@*/ void *walk_data ) ;

RBT_ROOT *rbtsimple_new_tree( rbt_compare_keys_fn compare_keys ,
                              uint32 key_length , /*@null@*/ void *data ) ;

Bool rbtsimple_create_node( /*@in@*/ RBT_ROOT *root , uintptr_t key ) ;

#endif /* __RBTREE_H__ */

/* Log stripped */
