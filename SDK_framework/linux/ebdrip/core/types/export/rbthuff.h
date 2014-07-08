/** \file
 * \ingroup types
 *
 * $HopeName: COREtypes!export:rbthuff.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Huffman code generated from a red-black tree.
 * Assumes std.h has been included.
 */

#ifndef __RBTHUFF_H__
#define __RBTHUFF_H__

#include "rbtree.h"

typedef /*@null@*/ /*@out@*/ /*@only@*/ mm_addr_t
( *rbthuff_alloc_fn )( mm_size_t size  , /*@dependent@*/ void *alloc_data )
  /*@ensures MaxSet(result) == (size - 1); @*/ ;

typedef void ( *rbthuff_free_fn )( /*@reldef@*/ /*@only@*/ mm_addr_t what ,
                                   mm_size_t size ,
                                   /*@dependent@*/ void *alloc_data ) ;

typedef struct huffcode HUFFCODE ;

RBT_ROOT *rbthuff_new_tree( rbt_compare_keys_fn compare_keys ,
                            uint32 key_length , /*@null@*/ void *data ) ;
uint32 rbthuff_get_seqnum( /*@in@*/ RBT_NODE *node ) ;
Bool rbthuff_make_code( /*@in@*/ RBT_ROOT *root , /*@out@*/ HUFFCODE **hcode ,
                        rbthuff_alloc_fn alloc_fn , rbthuff_free_fn free_fn ,
                        /*@dependent@*/ void *alloc_data ) ;
size_t rbthuff_code_size( /*@in@*/ HUFFCODE *hcode ) ;
void rbthuff_free_code( /*@in@*/ HUFFCODE *hcode , rbthuff_free_fn free_fn ,
                        /*@dependent@*/ void *alloc_data ) ;
void rbthuff_encode( /*@in@*/ RBT_NODE *node , uint8 *buf ,
                     /*@in@*/ uint32 *bitcount ) ;
void rbthuff_encodebit( Bool flag , uint8 *buf , /*@in@*/ uint32 *bitcount ) ;
uint32 rbthuff_decode( /*@in@*/ uint8 *buf ,
                       /*@in@*/ uint32 *bitcount ,
                       /*@in@*/ HUFFCODE *hcode ) ;
Bool rbthuff_decodebit( /*@in@*/ uint8 *buf , /*@in@*/ uint32 *bitcount ) ;

Bool rbthuff_increment_char( /*@in@*/ RBT_ROOT *root , uintptr_t key ) ;
uint32 rbthuff_get_char_count( /*@in@*/ RBT_NODE *node ) ;
void rbthuff_set_char_count( /*@in@*/ RBT_NODE *node , uint32 count ) ;

Bool rbthuff_count_bits( /*@in@*/ RBT_ROOT *root , RBT_NODE *node ,
                         void *walk_data ) ;

#endif /* __RBTHUFF_H__ */

/* Log stripped */
