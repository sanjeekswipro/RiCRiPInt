/** \file
 * \ingroup types
 *
 * $HopeName: COREtypes!src:rbthuff.c(EBDSDK_P.1) $
 * $Id: src:rbthuff.c,v 1.10.1.1.1.1 2013/12/19 11:25:08 anon Exp $
 *
 * Copyright (C) 1992-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * For generating a very compact representation of a data set using a
 * red-black tree as the intermediate representation of its
 * alphabet. Since the exact frequency of each character in the
 * alphabet is known at the time of the creation of the code, the
 * Huffman code ought to be exactly optimal - just don't ask me for a
 * rigorous proof.
 */

#include "std.h"
#include "rbthuff.h"

#include "hqmemcpy.h"

/** Huffman code structure - all that's needed to decode a stream. */

struct huffcode {
  /** Opaque private data pointer for the implementation. */
  /*@null@*/ /*@dependent@*/
  void *privdata ;

  /** Left arms of the decoding tree. */
  /*@null@*/ /*@only@*/
  uint32 *left ;

  /** Right arms of the decoding tree. */
  /*@null@*/ /*@only@*/
  uint32 *right ;

  /** Number of characters represented. */
  uint32 nch ;
} ;

/** Tree node data structure for building Huffman codes. */

typedef struct {
  /** Sequence number in the alphabet. Incremented each time a new
      unique character is added to the alphabet. */
  uint32 seqnum ;

  /** Number of times this character is used in the data stream to be
      encoded. */
  uint32 count ;

  /** The Huffman code assigned to this character. */
  uint32 code ;

  /** The length of this character's Huffman code. */
  uint32 codelen ;
}
RBT_HUFF_DATA ;

/** Simple lookup table for masking an indexed bit in a 32 bit word. I
    had a nagging feeling this existed already somewhere else in the
    RIP, but I couldn't find it. */

static uint32 setbit[ 32 ] =
  { 0x00000001L , 0x00000002L , 0x00000004L , 0x00000008L ,
    0x00000010L , 0x00000020L , 0x00000040L , 0x00000080L ,
    0x00000100L , 0x00000200L , 0x00000400L , 0x00000800L ,
    0x00001000L , 0x00002000L , 0x00004000L , 0x00008000L ,
    0x00010000L , 0x00020000L , 0x00040000L , 0x00080000L ,
    0x00100000L , 0x00200000L , 0x00400000L , 0x00800000L ,
    0x01000000L , 0x02000000L , 0x04000000L , 0x08000000L ,
    0x10000000L , 0x20000000L , 0x40000000L , 0x80000000L } ;

/*! \brief Allocation callback for Huffman tree nodes. */

/*@null@*/ /*@out@*/ /*@only@*/
static mm_addr_t rbthuff_rbt_alloc( mm_size_t size , /*@null@*/ void *data )
{
  mm_addr_t result = mm_alloc( mm_pool_temp , size ,
                               MM_ALLOC_CLASS_RBT_HUFF_TREE ) ;

  UNUSED_PARAM( void * , data ) ;

  return result ;
}

/*! \brief Free callback for Huffman tree nodes. */

static void rbthuff_rbt_free( /*@out@*/ /*@only@*/ mm_addr_t what ,
                              mm_size_t size , /*@null@*/ void *data )
{
  UNUSED_PARAM( void * , data ) ;
  mm_free( mm_pool_temp , what , size ) ;
}

/** Make a new tree as a precursor to building a code. */

RBT_ROOT *rbthuff_new_tree( rbt_compare_keys_fn compare_keys ,
                            uint32 key_length , /*@null@*/ void *data )
{
  return rbt_init( data , rbthuff_rbt_alloc , rbthuff_rbt_free ,
                   compare_keys , key_length , sizeof( RBT_HUFF_DATA )) ;
}

/** Increment the frequency count for the character identified by the
    given key. If this is the first reference to this character, use
    the supplied callbacks to try to allocate a new node for it in the
    tree. */

Bool rbthuff_increment_char( /*@in@*/ RBT_ROOT *root , uintptr_t key )
{
  RBT_NODE *node = rbt_search( root , key ) ;

  if ( node == NULL ) {
    RBT_HUFF_DATA new_data ;

    new_data.seqnum = rbt_node_count( root ) ;
    new_data.count = 0 ;
    new_data.code = 0 ;
    new_data.codelen = 0 ;

    node = rbt_allocate_node( root , key , & new_data ) ;

    if ( node == NULL ) {
      return FALSE ;
    }

    rbt_insert( root , node ) ;
  }

  {
    RBT_HUFF_DATA *data = rbt_get_node_data( node ) ;

    data->count++ ;
  }

  return TRUE ;
}

/** Get the current count for a given node. */

uint32 rbthuff_get_char_count( /*@in@*/ RBT_NODE *node )
{
  RBT_HUFF_DATA *data = rbt_get_node_data( node ) ;

  return data->count ;
}

/** Set the current count in the given node. */

void rbthuff_set_char_count( /*@in@*/ RBT_NODE *node , uint32 count )
{
  RBT_HUFF_DATA *data = rbt_get_node_data( node ) ;

  data->count = count ;
}

/** Tree walking callback which counts up the bits which will be used
    in the compressed output. */

Bool rbthuff_count_bits( /*@in@*/ RBT_ROOT *root , /*@in@*/ RBT_NODE *node ,
                         void *walk_data )
{
  uint32 *total_bits = ( uint32 * )walk_data ;
  RBT_HUFF_DATA *data = rbt_get_node_data( node ) ;

  UNUSED_PARAM( RBT_ROOT * , root ) ;

  *total_bits += data->count * data->codelen ;

  return TRUE ;
}

/** Each element of \c index refers to an element of the \c nprob
    array, and this routine maintains a heap structure of ascending
    values of \c nprob. (So that at any time, \c rbthuff_make_code can
    take \c index[ 0 ] and know that it represents the current lowest
    probability character in the alphabet). See Section 8.3 of
    "Numerical Recipes in C" by Press et al., 2nd ed. */

static void rbthuff_maint_heap( /*@in@*/ uint32 *index ,
                                /*@in@*/ uint32 *nprob ,
                                uint32 n , uint32 i )
{
  uint32 j ;
  uint32 k ;

  k = index[ i ] ;

  while ( i < ( n >> 1 )) {
    if (( j = i << 1 ) < n &&
        nprob[ index[ j ]] > nprob[ index[ j + 1 ]]) {
      j++;
    }

    if ( nprob[ k ] <= nprob[ index[ j ]]) {
      break;
    }

    index[ i ] = index[ j ] ;
    i = j ;
  }

  index[ i ] = k ;
}

/** Accessor for the node sequence number. */

uint32 rbthuff_get_seqnum( /*@in@*/ RBT_NODE *node )
{
  RBT_HUFF_DATA *data = rbt_get_node_data( node ) ;

  return data->seqnum ;
}

/** A red black tree walk callback: populate the probability index
    ready to generate a Huffman code. */

static Bool rbthuff_populate_probs( RBT_ROOT *root , RBT_NODE *node ,
                                    void *walk_data )
{
  uint32 *nprob = ( uint32 * )walk_data ;
  RBT_HUFF_DATA *data = rbt_get_node_data( node ) ;

  UNUSED_PARAM( RBT_ROOT * , root ) ;

  nprob[ data->seqnum ] = data->count ;

  return TRUE ;
}

/** A red-black tree walk callback which uses the data in the \c up
    vector to generate the Huffman code for the character represented
    by this node. */

static Bool rbthuff_make_node_code( /*@in@*/ RBT_ROOT *root ,
                                    /*@in@*/ RBT_NODE *node ,
                                    void *walk_data )
{
  RBT_HUFF_DATA *data = rbt_get_node_data( node ) ;
  int32 *up = ( int32 * )walk_data ;
  int32 ibit = 0 ;
  uint32 n = 0 ;
  int32 h ;

  UNUSED_PARAM( RBT_ROOT * , root ) ;

  for ( h = up[ data->seqnum ] ; h ; h = up[ h ] , ibit++ ) {
    if ( h < 0 ) {
      n |= setbit[ ibit ] ;
      h = -h ;
    }
  }

  HQASSERT( ibit <= 32 , "Max code length overflowing 32 bits" ) ;

  data->code = n ;
  data->codelen = ibit ;

  return TRUE ;
}

/** Given the red-black tree \c root, in which each node represents a
    character in the alphabet to be encoded and contains its frequency
    (exact probability, if you like), construct a Huffman code in the
    structure hcode to represent the alphabet. The \c alloc_fn
    callback is used to create the tree tables which represent the
    code, and it get the opaque alloc_data pointer passed to it so the
    caller can control lifetime etc. */

Bool rbthuff_make_code( /*@in@*/ RBT_ROOT *root , /*@out@*/ HUFFCODE **hcode ,
                        rbthuff_alloc_fn alloc_fn , rbthuff_free_fn free_fn ,
                        /*@dependent@*/ void *alloc_data )
{
  Bool result = FALSE ;
  HUFFCODE *newcode ;
  mm_size_t heap_size = sizeof( uint32 ) * 2 * rbt_node_count( root ) - 1 ;
  int32 *up = NULL ;
  uint32 *index = NULL ;
  uint32 *nprob = NULL ;
  int32 node ;
  uint32 j ;
  uint32 k ;

  HQASSERT( rbt_node_count( root ) > 1 ,
            "rbthuff_make_code shouldn't be called with a degenerate tree." ) ;

  newcode = alloc_fn( sizeof( HUFFCODE ) , alloc_data ) ;

  if ( newcode == NULL ) {
    return FALSE ;
  }

  newcode->nch = rbt_node_count( root ) ;
  newcode->left = NULL ;
  newcode->right = NULL ;

  /* These arrays need no initialisation: each element is written
     exactly once in the main loop below. */

  newcode->left = alloc_fn( sizeof( uint32 ) * newcode->nch , alloc_data ) ;

  if ( newcode->left == NULL ) {
    goto CLEANUP ;
  }

  newcode->right = alloc_fn( sizeof( uint32 ) * newcode->nch , alloc_data ) ;

  if ( newcode->right == NULL ) {
    goto CLEANUP ;
  }

  /* Arrays representing the character frequencies and the heap that
     will keep track of them. */

  index = mm_alloc( mm_pool_temp , heap_size , MM_ALLOC_CLASS_RBT_WORKSPACE ) ;

  if ( index == NULL ) {
    goto CLEANUP ;
  }

  up = mm_alloc( mm_pool_temp , heap_size , MM_ALLOC_CLASS_RBT_WORKSPACE ) ;

  if ( up == NULL ) {
    goto CLEANUP ;
  }

  nprob = mm_alloc( mm_pool_temp , heap_size , MM_ALLOC_CLASS_RBT_WORKSPACE ) ;

  if ( nprob == NULL ) {
    goto CLEANUP ;
  }

#if defined( S_SPLINT_S )
  index[ 0 ] = 0 ;
  up[ 0 ] = 0 ;
  nprob[ 0 ] = 0 ;
#endif

  /* OK, now populate the probabilities array from the character
     frequencies stored in the tree. */

  ( void )rbt_walk( root , rbthuff_populate_probs , nprob ) ;

  for ( j = 0 ; j < newcode->nch ; j++ ) {
    index[ j ] = j ;
  }

  j = newcode->nch ;

  /* Modify the index array so that it's a valid heap representing the
     starting probabilities. */

  do {
    rbthuff_maint_heap( index , nprob , newcode->nch - 1 , --j ) ;
  }
  while ( j != 0 ) ;

  j = newcode->nch - 1 ;
  k = j ;

  /* Combine heap nodes, remaking the heap at each stage. */

  do {
    node = index[ 0 ] ;
    index[ 0 ] = index[ j-- ] ;
    rbthuff_maint_heap( index , nprob , j , 0 ) ;
    nprob[ ++k ] = nprob[ index[ 0 ]] + nprob[ node ] ;

    /* Store left and right children of a node. */

    newcode->left[ k - newcode->nch ] = node ;
    newcode->right[ k - newcode->nch ] = index[ 0 ] ;

    /* Indicate whether a node is a left or right child of its parent. */

    up[ index[ 0 ]] = -( long )k ;
    up[ node ] = index[ 0 ] = k ;
    rbthuff_maint_heap( index , nprob , j , 0 ) ;
  }
  while ( j != 0 ) ;

  up[ k ] = 0 ;

  /* Make the Huffman code from the tree. */

  ( void )rbt_walk( root , rbthuff_make_node_code , up ) ;

  *hcode = newcode ;
  result = TRUE ;

 CLEANUP:

  if ( index != NULL ) {
    mm_free( mm_pool_temp , index , heap_size ) ;
  }

  if ( up != NULL ) {
    mm_free( mm_pool_temp , up , heap_size ) ;
  }

  if ( nprob != NULL ) {
    mm_free( mm_pool_temp , nprob , heap_size ) ;
  }

  if ( ! result ) {
    rbthuff_free_code( newcode , free_fn , alloc_data ) ;
  }

  return result ;
}

/** Returns to overall memory size of a HUFFCODE structure. */

size_t rbthuff_code_size( /*@in@*/ HUFFCODE *hcode )
{
  size_t size = sizeof( *hcode ) ;

  if ( hcode->left != NULL ) {
    size += sizeof( hcode->left[ 0 ]) * hcode->nch ;
  }

  if ( hcode->right != NULL ) {
    size += sizeof( hcode->right[ 0 ]) * hcode->nch ;
  }

  return size ;
}

/** Frees the given HUFFCODE structure. */

void rbthuff_free_code( /*@in@*/ /*@only@*/ HUFFCODE *hcode ,
                        rbthuff_free_fn free_fn ,
                        /*@dependent@*/ void *alloc_data )
{
  if ( hcode->left != NULL ) {
    free_fn( hcode->left , sizeof( uint32 ) * hcode->nch , alloc_data ) ;
    hcode->left = NULL ;
  }

  if ( hcode->right != NULL ) {
    free_fn( hcode->right , sizeof( uint32 ) * hcode->nch , alloc_data ) ;
    hcode->right = NULL ;
  }

  free_fn( hcode , sizeof( HUFFCODE ) , alloc_data ) ;
}

/** Huffman encode the single character represented by the given \c
    node in the tree using the code in the structure hcode. Write the
    result to the output buffer \c buf starting at bit \c *bitcount
    (whose smallest valid value is zero), and increment \c *bitcount
    appropriately. This routine is called repeatedly to encode
    consecutive characters in a message, but must be preceded by a
    single initializing call to \c rbthuff_make_code, which constructs
    hcode. */

void rbthuff_encode( /*@in@*/ RBT_NODE *node , uint8 *buf ,
                     /*@in@*/ uint32 *bitcount )
{
  RBT_HUFF_DATA *data = rbt_get_node_data( node ) ;
  int32 n ;

  /* Loop over the bits in the stored Huffman code. */

  for ( n = data->codelen - 1 ; n >= 0 ; n-- , ++( *bitcount )) {
    int32 nc = *bitcount / 8 ;
    int32 l = *bitcount % 8 ;

    /* Set appropriate bits in output buffer. */

    if ( l == 0 ) {
      buf[ nc ] = 0 ;
    }

    if ( data->code & setbit[ n ]) {
      buf[ nc ] |= setbit[ l ] ;
    }
  }
}

/** Encode a single bit representing a Boolean into the stream. Not
    strictly part of a Huffman code, but it's a neat way of
    interleaving encoded characters with flags in one neat package. */

void rbthuff_encodebit( Bool flag , uint8 *buf , uint32 *bitcount )
{
  int32 nc ;
  int32 l ;

  nc = *bitcount / 8 ;
  l = *bitcount % 8 ;

  if ( l == 0 ) {
    buf[ nc ] = 0 ;
  }

  if ( flag ) {
    buf[ nc ] |= setbit[ l ] ;
  }

  ++( *bitcount ) ;
}

/** Decode a single Huffman-encoded character from the given bit
    stream \c buf, starting at position \c *bitcount. Returns the
    index of the character into the lookup table associated with \c
    hcode, and updates \c *bitcount appropriately. Note that it is the
    caller's responsibility to ensure that the stream is long
    enough. */

uint32 rbthuff_decode( /*@in@*/ uint8 *buf ,
                       /*@in@*/ uint32 *bitcount ,
                       /*@in@*/ HUFFCODE *hcode )
{
  uint32 node ;

  /* Set node to the top of the decoding tree, and loop until a valid
     character is obtained. */

  HQASSERT_LPTR( hcode->left ) ;
  HQASSERT_LPTR( hcode->right ) ;

  for ( node = 2 * ( hcode->nch - 1 ) ; node >= hcode->nch ; ) {
    uint32 byte = *bitcount / 8 ;

    /* Branch left or right in tree, depending on node's value. */

    node = (( buf[ byte ] & setbit[ 7 & ( *bitcount )++ ]) != 0 ?
            hcode->right[ node - hcode->nch ] :
            hcode->left[ node - hcode->nch ]) ;
  }

  /* We have reached a terminal node, so we have a complete character
     and can return. */

  return node ;
}

/** Give me exactly one bit from the encoded stream. Not strictly part
    of any Huffman code, but designed as a little shortcut for
    interleaving Booleans with encoded characters. */

Bool rbthuff_decodebit( /*@in@*/ uint8 *buf , /*@in@*/ uint32 *bitcount )
{
  uint32 byte = *bitcount / 8 ;

  return (( buf[ byte ] & setbit[ 7 & ( *bitcount )++ ]) != 0 ) ;
}

/* Log stripped */
