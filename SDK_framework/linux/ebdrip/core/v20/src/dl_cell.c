/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:dl_cell.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Display list manipulation and rendering functions for compressed cells.
 */

#include "core.h"

#include "display.h" /* make_listobject */
#include "dl_cell.h" /* CELL */
#include "hqmemcpy.h" /* HqMemCpy */
#include "plotops.h" /* degenerateClipping */
#include "routedev.h" /* DEVICE_CELL */
#include "rbthuff.h" /* HUFFCODE */
#include "swerrors.h" /* error_handler */
#include "mm.h"
#include "htrender.h" /* ht_getModularHalftoneRef */
#include "gu_htm.h" /* MODHTONE_REF */
#include "blitcolors.h" /* blit_color_t */
#include "blitcolorh.h" /* blit_quantise_set_screen */


/** \brief Compressed RLE cell display list structure.

Span lengths are represented by the Huffman code \c length_hcode and
its accompanying lookup table length_lut.

These objects can be self-colored (all spans are either transparent or
have their color defined by the DL object's dl_color) or multicolored,
in which case the span colors are represented by the color Huffman
code \c color_hcode and LUT \c color_lut. */

struct CELL {
  /*! The mm pool from which this cell is allocated. */
  mm_pool_t pool ;

  /*! The bounding box in device pixels. */
  dbbox_t bbox ;

  uint32 colorant_count ;

  uint32 num_colors ;
  uint32 num_spots ;
  uint32 num_lengths ;

  COLORANTINDEX *cis ;

  HUFFCODE *color_hcode ;
  HUFFCODE *spot_hcode ;
  HUFFCODE *length_hcode ;

  COLORVALUE *color_lut ;
  SPOTNO *spot_lut ;
  uint8 *length_lut ;

  /*! Rows are initially allocated full size and then truncated to the precise
      size after encoding. */
  uint32 initial_row_bit_count ;

  uint32 *y_stream_bit_counts ;
  uint8 **y_streams ;
} ;


/** \brief Return the cell object's overall bounding box. */

void cellbbox( /*@in@*/ CELL *cell , /*@out@*/ dbbox_t *bbox )
{
  *bbox = cell->bbox ;
}

/*! \brief Callback to allocate memory for compressed cell Huffman
           code tables.

Since these things are added to the display list, the memory comes
from the DL pool. */

/*@null@*/ /*@out@*/ /*@only@*/ static mm_addr_t
cellalloctable( mm_size_t size  , /*@dependent@*/ void *alloc_data )
  /*@ensures MaxSet(result) == (size - 1); @*/
{
  return mm_alloc(( mm_pool_t )alloc_data , size , MM_ALLOC_CLASS_DL_CELL ) ;
}

/*! \brief Callback to free memory from compressed cell Huffman code
           tables. */

static void cellfreetable(
  /*@reldef@*/ /*@only@*/ mm_addr_t what , mm_size_t size  ,
  /*@dependent@*/ void *alloc_data )
{
  mm_free(( mm_pool_t )alloc_data , what , size ) ;
}

/*! \brief Simple: free a compressed cell. */

void cellfree( /*@in@*/ /*@only@*/ CELL *cell )
{
  uint32 nrows = cell->bbox.y2 - cell->bbox.y1 + 1 ;

  if ( cell->color_lut != NULL ) {
    mm_free( cell->pool , cell->color_lut ,
             ( sizeof( COLORVALUE ) * cell->num_colors *
               cell->colorant_count )) ;
  }

  if ( cell->spot_lut != NULL ) {
    mm_free( cell->pool , cell->spot_lut ,
             ( sizeof( SPOTNO ) * cell->num_spots * cell->colorant_count )) ;
  }

  /** \todo The cis array is shared among many cells: don't free it now. */

  if ( cell->length_lut != NULL ) {
    mm_free( cell->pool , cell->length_lut ,
             ( sizeof( int32 ) * cell->num_lengths )) ;
  }

  if ( cell->y_streams != NULL ) {
    uint32 row ;
    for ( row = 0; row < nrows; ++row ) {
      if ( cell->y_streams[ row ] != NULL )
        mm_free( cell->pool , cell->y_streams[ row ] ,
                 cell->y_stream_bit_counts[ row ] / 8 + 1 ) ;
    }
    mm_free( cell->pool , cell->y_streams ,
             sizeof( *cell->y_streams ) * nrows ) ;
  }

  if ( cell->y_stream_bit_counts != NULL ) {
    mm_free( cell->pool , cell->y_stream_bit_counts ,
             sizeof( *cell->y_stream_bit_counts ) * nrows ) ;
  }

  if ( cell->color_hcode != NULL ) {
    rbthuff_free_code( cell->color_hcode , cellfreetable , cell->pool ) ;
  }

  if ( cell->length_hcode != NULL ) {
    rbthuff_free_code( cell->length_hcode , cellfreetable , cell->pool  ) ;
  }

  mm_free( cell->pool , cell , sizeof( CELL )) ;
}

/*! \brief Memory footprint for this cell. */

size_t cellfootprint( /*@in@*/ /*@only@*/ CELL *cell )
{
  /** \todo Take account of \c cis when they're individually allocated. */

  uint32 nrows = cell->bbox.y2 - cell->bbox.y1 + 1 ;
  size_t size = sizeof( CELL ) +
    ( sizeof( COLORVALUE ) * cell->num_colors * cell->colorant_count ) +
    ( sizeof( SPOTNO ) * cell->num_spots * cell->colorant_count ) +
    ( sizeof( int32 ) * cell->num_lengths ) +
    ( sizeof( uint32 ) * nrows ) +
    ( sizeof( uint8* ) * nrows ) ;

  if ( cell->y_stream_bit_counts != NULL ) {
    uint32 row;
    for ( row = 0; row < nrows; ++row ) {
      size += cell->y_stream_bit_counts[ row ] ;
    }
  }

  if ( cell->color_hcode != NULL ) {
    size += rbthuff_code_size( cell->color_hcode ) ;
  }

  if ( cell->length_hcode != NULL ) {
    size += rbthuff_code_size( cell->length_hcode ) ;
  }

  return size ;
}

/*! \brief RBT walk callback for inserting color values into the
           lookup table.
*/

static Bool cellpopulatecolorlut( /*@in@*/ RBT_ROOT *root ,
                                  /*@in@*/ RBT_NODE *node ,
                                  void *walk_data )
{
  CELL *cell = ( CELL * )walk_data ;

  UNUSED_PARAM( RBT_ROOT * , root ) ;

  HqMemCpy( & cell->color_lut[ rbthuff_get_seqnum( node ) *
                               cell->colorant_count ] ,
            rbt_get_node_key( node ) ,
            cell->colorant_count * sizeof( COLORVALUE )) ;

  return TRUE ;
}

/*! \brief RBT walk callback for inserting color values into the
           lookup table.
*/

static Bool cellpopulatespotlut( /*@in@*/ RBT_ROOT *root ,
                                 /*@in@*/ RBT_NODE *node ,
                                 void *walk_data )
{
  CELL *cell = ( CELL * )walk_data ;

  UNUSED_PARAM( RBT_ROOT * , root ) ;

  HqMemCpy( & cell->spot_lut[ rbthuff_get_seqnum( node ) *
                              cell->colorant_count ] ,
            rbt_get_node_key( node ) ,
            cell->colorant_count * sizeof( int32 )) ;

  return TRUE ;
}

/*! \brief RBT walk callback for inserting length values into the
           lookup table.
*/

static Bool cellpopulatelengthlut( /*@in@*/ RBT_ROOT *root ,
                                   /*@in@*/ RBT_NODE *node ,
                                   void *walk_data )
{
  CELL *cell = ( CELL * )walk_data ;

  UNUSED_PARAM( RBT_ROOT * , root ) ;

  cell->length_lut[ rbthuff_get_seqnum( node )] =
    CAST_UNSIGNED_TO_UINT8( rbt_get_node_key( node )) ;

  return TRUE ;
}

/*! \brief Allocate a compressed cell ready for the stream to be
           compressed.

The cell can either be color or monochrome (where all spans have the
same color, which is the color of the overall DL object). If the cell
is monochrome, \c color_tree and \c spot_tree will be null, and we use
a single bit instead of a color character in the compressed stream to
indicate whether or not the span is transparent. If that's the case,
\c span_count tells us how many such bits to make room for.  */

Bool cellallocate( /*@in@*/ mm_pool_t pool ,
                   /*@in@*/ dbbox_t *bbox , uint32 colorant_count ,
                   /*@dependent@*/ /*@null@*/ /*@in@*/ COLORANTINDEX *cis ,
                   /*@null@*/ /*@in@*/ RBT_ROOT *color_tree ,
                   /*@null@*/ /*@in@*/ RBT_ROOT *spot_tree ,
                   /*@in@*/ RBT_ROOT *length_tree ,
                   uint32 span_count ,
                   /*@out@*/ CELL **ccptr )
{
  CELL *cell = NULL ;
  Bool result = FALSE ;
  uint32 nrows = bbox->y2 - bbox->y1 + 1, ncols = bbox->x2 - bbox->x1 + 1 ;
  uint32 stream_bytes ;

  cell = mm_alloc( pool , sizeof( CELL ) ,
                   MM_ALLOC_CLASS_DL_CELL ) ;

  if ( cell == NULL ) {
    result = error_handler( VMERROR ) ;
    goto CLEANUP ;
  }

  cell->pool = pool ;
  cell->bbox = *bbox ;
  cell->colorant_count = colorant_count ;
  cell->num_colors = 0 ;
  cell->num_spots = 0 ;
  cell->num_lengths = 0 ;
  cell->cis = NULL ;
  cell->color_hcode = NULL ;
  cell->spot_hcode = NULL ;
  cell->length_hcode = NULL ;
  cell->color_lut = NULL ;
  cell->spot_lut = NULL ;
  cell->length_lut = NULL ;
  cell->initial_row_bit_count = 0 ;
  cell->y_stream_bit_counts = NULL ;
  cell->y_streams = NULL ;

  cell->y_stream_bit_counts = mm_alloc( pool ,
                                        ( sizeof( *cell->y_stream_bit_counts )
                                          * nrows ) ,
                                        MM_ALLOC_CLASS_DL_CELL ) ;

  if ( cell->y_stream_bit_counts == NULL ) {
    result = error_handler( VMERROR ) ;
    goto CLEANUP ;
  }

  HqMemZero( cell->y_stream_bit_counts,
             sizeof( *cell->y_stream_bit_counts ) * nrows ) ;

  cell->y_streams = mm_alloc( pool ,
                              ( sizeof( *cell->y_streams ) * nrows ) ,
                              MM_ALLOC_CLASS_DL_CELL ) ;

  if ( cell->y_streams == NULL ) {
    result = error_handler( VMERROR ) ;
    goto CLEANUP ;
  }

  HqMemZero( cell->y_streams, sizeof( *cell->y_streams ) * nrows );

  /* OK, turn the trees into Huffman codes and allocate the LUTs */

  if ( color_tree != NULL ) {
    cell->num_colors = rbt_node_count( color_tree ) ;
    if ( ! rbthuff_make_code( color_tree , & cell->color_hcode ,
                              cellalloctable , cellfreetable , pool )) {
      result = error_handler( VMERROR ) ;
      goto CLEANUP ;
    }

    cell->color_lut = mm_alloc( pool ,
                                ( sizeof( COLORVALUE ) * cell->num_colors *
                                  colorant_count ) ,
                                MM_ALLOC_CLASS_DL_CELL_LUT ) ;

    if ( cell->color_lut == NULL ) {
      result = error_handler( VMERROR ) ;
      goto CLEANUP ;
    }

    ( void )rbt_walk( color_tree , cellpopulatecolorlut , cell ) ;
    cell->cis = cis ;

    HQASSERT_LPTR( spot_tree ) ;

    cell->num_spots = rbt_node_count( spot_tree ) ;
    if ( cell->num_spots > 1 ) {
      if ( ! rbthuff_make_code( spot_tree , & cell->spot_hcode ,
                                cellalloctable , cellfreetable , pool )) {
        result = error_handler( VMERROR ) ;
        goto CLEANUP ;
      }
    }

    cell->spot_lut = mm_alloc( pool ,
                                ( sizeof( SPOTNO ) * cell->num_spots *
                                  colorant_count ) ,
                                MM_ALLOC_CLASS_DL_CELL_LUT ) ;

    if ( cell->spot_lut == NULL ) {
      result = error_handler( VMERROR ) ;
      goto CLEANUP ;
    }

    if ( cell->num_spots > 1 ) {
      ( void )rbt_walk( spot_tree , cellpopulatespotlut , cell ) ;
    }
    else {
      HqMemCpy( & cell->spot_lut[ 0 ] ,
                rbt_get_node_key( rbt_root_node( spot_tree )) ,
                cell->colorant_count * sizeof( SPOTNO )) ;
    }
  }

  cell->num_lengths = rbt_node_count( length_tree ) ;
  HQASSERT( cell->num_lengths > 0 , "How can there be no length codes?" ) ;
  if ( cell->num_lengths > 1 ) {
    if ( ! rbthuff_make_code( length_tree , & cell->length_hcode ,
                              cellalloctable , cellfreetable , pool )) {
      result = error_handler( VMERROR ) ;
      goto CLEANUP ;
    }
  }

  cell->length_lut = mm_alloc( pool ,
                               ( sizeof( int32 ) * cell->num_lengths ) ,
                               MM_ALLOC_CLASS_DL_CELL_LUT ) ;

  if ( cell->length_lut == NULL ) {
    result = error_handler( VMERROR ) ;
    goto CLEANUP ;
  }

  if ( cell->num_lengths > 1 ) {
    ( void )rbt_walk( length_tree , cellpopulatelengthlut , cell ) ;
  }
  else {
    cell->length_lut[ 0 ] =
      CAST_UNSIGNED_TO_UINT8( rbt_get_node_key( rbt_root_node( length_tree ))) ;
  }

  /* Calculate the initial maximum size of a row's worth of stream data.
     After encoding, if the final size is less, the memory is truncated. */

  stream_bytes = 0 ;

  if ( color_tree != NULL ) {
    stream_bytes += sizeof( uint32 ) * ncols ;
    if ( cell->num_spots > 1 ) {
      stream_bytes += sizeof( uint32 ) * ncols ;
    }
  }
  else {
    stream_bytes += sizeof( uint8 ) * min( span_count, ncols ) ;
  }

  /* If there's only one span length, we don't bother encoding span length - we
     unconditionally pick the first and only one in the LUT. We rely on the fact
     that the Huffman code entry for the node is initialised to length zero in
     rbthuff_increment_char() and so rbthuff_encode() to just works. Same goes
     for spots above. */

  if ( cell->num_lengths > 1 ) {
    stream_bytes += sizeof( uint8 ) * ncols ;
  }

  cell->initial_row_bit_count = stream_bytes * BITS_BYTE ;

  result = TRUE ;

 CLEANUP:

  if ( result ) {
    *ccptr = cell ;
  }
  else {
    if ( cell != NULL ) {
      cellfree( cell ) ;
    }

    *ccptr = NULL ;
  }

  return result ;
}

/*! \brief Allocate a completely empty CELL object. */

Bool cellallocatedummycell( /*@in@*/ mm_pool_t pool ,
                            /*@in@*/  dbbox_t *bbox ,
                                      uint32 colorant_count ,
                            /*@out@*/ CELL **ccptr )
{
  CELL *cell = NULL ;

  cell = mm_alloc( pool , sizeof( CELL ) ,
                   MM_ALLOC_CLASS_DL_CELL ) ;

  if ( cell == NULL )
    return error_handler( VMERROR ) ;

  cell->bbox = *bbox ;
  cell->colorant_count = colorant_count ;
  cell->num_colors = 0 ;
  cell->num_spots = 0 ;
  cell->num_lengths = 0 ;
  /* num_lengths == 0 indicates an empty cell. */
  cell->cis = NULL ;
  cell->color_hcode = NULL ;
  cell->spot_hcode = NULL ;
  cell->length_hcode = NULL ;
  cell->color_lut = NULL ;
  cell->spot_lut = NULL ;
  cell->length_lut = NULL ;
  cell->initial_row_bit_count = 0 ;
  cell->y_stream_bit_counts = NULL ;
  cell->y_streams = NULL ;

  *ccptr = cell ;

  return TRUE ;
}

/** Return a flag indicating whether this cell is monochrome (no color
    component in stream, just a bit indicating whether a span is
    transparent or not). */

Bool cellismono( /*@in@*/ CELL *cell )
{
  return cell->num_colors == 0 ;
}

/** Return the total number of colorants in the cell. */

uint32 cellcolorantcount( /*@in@*/ CELL *cell )
{
  return cell->colorant_count ;
}

/** Return a pointer to the cell's colorant index array. */

COLORANTINDEX *cellcolorantindices( /*@in@*/ CELL *cell )
{
  return cell->cis ;
}

/** Sets an integer which gives the index into colors in this cell
    corresponding with the given \c COLORANTINDEX. Returns FALSE if no
    such mapping exists. */

Bool cellmapcolorant( /*@in@*/ CELL *cell , COLORANTINDEX ci , uint32 *mapping )
{
  uint32 cn ;

  for ( cn = 0 ; cn < cell->colorant_count ; cn++ ) {
    if ( cell->cis[ cn ] == ci ) {
      *mapping = cn ;
      return TRUE ;
    }
  }

  return FALSE ;
}


static inline void write_byte(uint8 val, uint8 *stream, uint32 *bitcount)
{
  HQASSERT((*bitcount & 7) == 0, "Stream not on byte boundary");
  stream[*bitcount / 8] = val;
  *bitcount += 8;
}

static inline void write_word(uint32 val, uint8 *stream, uint32 *bitcount)
{
  HQASSERT((*bitcount & 7) == 0, "Stream not on byte boundary");
  BYTE_STORE32_PLATFORM(&stream[*bitcount / 8], val);
  *bitcount += 32;
}

static inline uint8 read_byte(uint8 *stream, uint32 *bitcount)
{
  HQASSERT((*bitcount & 7) == 0, "Stream not on byte boundary");
  stream += *bitcount / 8;
  *bitcount += 8;
  return stream[0];
}

static inline uint32 read_word(uint8 *stream, uint32 *bitcount)
{
  HQASSERT((*bitcount & 7) == 0, "Stream not on byte boundary");
  stream += *bitcount / 8;
  *bitcount += 32;
  return BYTE_LOAD32_UNSIGNED_PLATFORM(stream);
}

/** Encode a span into a cell's stream. The position in the stream is given by
    bitcount, which is updated after the encode. If the \c color parameter is
    NULL, insert a single bit (padded to a byte) into the stream to indicate
    whether the span is transparent. */

void cellencodespan( /*@in@*/ CELL *cell ,
                     /*@null@*/ /*@in@*/ RBT_NODE *color ,
                     /*@null@*/ /*@in@*/ RBT_NODE *spot ,
                     Bool transparent ,
                     /*@in@*/ RBT_NODE *length ,
                     /*@in@*/ uint32 row ,
                     /*@in@*/ uint32 *bitcount )
{
  uint8 *stream = cell->y_streams[ row ] ;
  uint32 index;

  if ( color != NULL ) {
    index = rbthuff_get_seqnum( color ) ;
    write_word( index , stream , bitcount ) ;

    if ( cell->num_spots > 1 && ! transparent ) {
      /* We don't bother to encode spots unless the span is not
         transparent. */
      HQASSERT_LPTR( spot ) ;
      index = rbthuff_get_seqnum( spot ) ;
      write_word( index , stream , bitcount ) ;
    }
  }
  else {
    write_byte( (uint8)transparent , stream , bitcount ) ;
  }

  if ( cell->length_hcode != NULL ) {
    index = rbthuff_get_seqnum( length ) ;
    HQASSERT( index < cell->num_lengths , "Length index out of range." ) ;
    write_byte( cell->length_lut[index] , stream , bitcount ) ;
  }

  HQASSERT( *bitcount <= cell->y_stream_bit_counts[ row ] ,
            "Overrun stream bits!" ) ;
}


static inline void cell_blit_color_unpack(/*@in@*/ blit_color_t *blit_color,
                                          /*@in@*/ const MODHTONE_REF *selected_mht,
                                          /*@in@*/ COLORANTINDEX *cis,
                                          /*@in@*/ COLORVALUE *color,
                                          /*@in@*/ SPOTNO *spots,
                                          /*@in@*/ channel_index_t colorant_count)
{
  channel_index_t blit_ci_i = blit_color->map->nchannels;
  SPOTNO spot = SPOT_NO_INVALID;
  HTTYPE httype;
  COLORANTINDEX blit_ci = COLORANTINDEX_UNKNOWN;

  HQASSERT(colorant_count > 0, "No channels in cell");
  HQASSERT(blit_ci_i > 0, "No channels in blit");

  while (blit_ci_i--) {
    COLORVALUE cell_cv = COLORVALUE_TRANSPARENT;
    channel_index_t cell_ci_i = colorant_count;

    blit_ci = blit_color->map->channel[blit_ci_i].ci;
    /* Be sure to preserve the colorant order of the color map. */
    /* \todo Could prebuild this mapping somewhere earlier. */
    while (cell_ci_i--) {
      if (cis[cell_ci_i] == blit_ci) {
        cell_cv = color[cell_ci_i];
        break;
      }
    }

    if ( cell_cv == COLORVALUE_TRANSPARENT /* overprint */ ) {
      /* DL cells have no type info. themselves, other than that established
       * when the rendering of the DL cell is started. Don't mark type as
       * absent. */
      if ( blit_color->map->channel[blit_ci_i].type != channel_is_type )
        blit_channel_mark_absent(blit_color, blit_ci_i);
    }
    else {
      /* Screening is only going have one channel, so just remember the spot. */
      spot = spots[cell_ci_i];
      blit_color->unpacked.channel[blit_ci_i].cv = cell_cv;
      blit_channel_mark_present(blit_color, blit_ci_i);
    }
  }
  /* Cell was created opaque, so no need to set alpha. */
  /** \todo Should set type, but it's not available here. */
#ifdef ASSERT_BUILD
  blit_color->valid = blit_color_unpacked;
#endif

  /** \todo Should decide type for screen, but no type info available here. */
  httype = REPRO_TYPE_OTHER;
  if ( blit_color->map->apply_properties ) {
    Bool selected =
      selected_mht == NULL
      || (spot != SPOT_NO_INVALID &&
          selected_mht == ht_getModularHalftoneRef(spot, httype, blit_ci));
      /* If screening, there was only one channel, and that was the ci. */
    blit_apply_render_properties(blit_color, selected, FALSE);
  }

  if ( blit_color->nchannels > 0 )
    blit_quantise_set_screen(blit_color, spot, httype);
}

/** Decode a span from the cell's stream. The position in the stream is given by
    bitcount, which is updated after the decode. Don't expect a color component
    in the stream if the \c blit_color parameter is NULL - instead a single bit
    (padded to a byte) will indicate whether the span is transparent.

    The color of the span can be used to set up the UNQUANTIZED colorvalues of a
    blit_color_t object passed into this function. The blit_color_t associated
    colormap is used for this.
*/

void celldecodespan( /*@in@*/ CELL *cell ,
                     /*@out@*/ Bool *transparent ,
                     /*@out@*/ uint8 *length ,
                     /*@in@*/ uint32 row ,
                     /*@in@*/ uint32 *bitcount ,
                     /*@in@*/ const MODHTONE_REF *selected_mht,
                     /*@in@*/ blit_color_t *blit_color )
{
  uint8 *stream = cell->y_streams[ row ] ;
  uint32 index ;
  COLORVALUE *colors = NULL;
  SPOTNO *spots = NULL;

  if ( blit_color != NULL ) {
    HQASSERT( *bitcount < cell->y_stream_bit_counts[ row ] ,
              "Ran out of stream bits!" ) ;
    index = read_word( stream , bitcount ) ;
    HQASSERT( index < cell->num_colors , "Color index out of range." ) ;

    /* A color index of zero means the span is transparent. */
    colors = &cell->color_lut[ index * cell->colorant_count ];
    *transparent = ( index == 0 ) ;

    if ( ! *transparent ) {
      /* We don't bother to encode spots unless the span is not
         transparent. */
      if ( cell->num_spots > 1 ) {
        HQASSERT( *bitcount < cell->y_stream_bit_counts[ row ] ,
                  "Ran out of stream bits!" ) ;
        index = read_word( stream , bitcount ) ;
      }
      else {
        index = 0 ;
      }
      HQASSERT( index < cell->num_spots , "Spot index out of range." ) ;

      spots = &cell->spot_lut[ index * cell->colorant_count ];
    }
  }
  else {
    *transparent = read_byte( stream , bitcount ) ;
  }

  if ( cell->length_hcode != NULL ) {
    HQASSERT( *bitcount < cell->y_stream_bit_counts[ row ] ,
              "Ran out of stream bits!" ) ;
    *length = read_byte( stream , bitcount ) ;
  }
  else {
    /* There's only one span length. */
    *length = cell->length_lut[ 0 ] ;
  }

  if ( blit_color != NULL && !*transparent ) {
    cell_blit_color_unpack(blit_color, selected_mht,
                           cell->cis, colors, spots, cell->colorant_count);
    *transparent = blit_color->nchannels == 0;
  }
}


#if defined( DEBUG_CELL_OUTLINES )
void celldecodedebugcolor( /*@in@*/ CELL *cell ,
                           /*@in@*/ blit_color_t *blit_color )
{
  if ( blit_color != NULL && blit_color->map != NULL ) {
    channel_index_t blit_ci_i = blit_color->map->nchannels;

    while (blit_ci_i--) {
      blit_color->unpacked.channel[blit_ci_i].cv = COLORVALUE_HALF;
    }
    /* Cell was created opaque, so no need to set alpha. */
    blit_color->type =
      blit_color->unpacked.channel[blit_color->map->type_index].cv =
        SW_PGB_LW_OBJECT;
#ifdef ASSERT_BUILD
    blit_color->valid = blit_color_unpacked;
#endif
  }
}
#endif


/** Record the y offset into the bit stream for the row given. */

Bool cellstartrow( CELL *cell , uint32 row , uint32 *bitcurrent )
{
  HQASSERT( cell->y_streams[ row ] == NULL , "Stream row already encoded" ) ;

  cell->y_streams[ row ] = mm_alloc( cell->pool ,
                                     cell->initial_row_bit_count / 8 + 1 ,
                                     MM_ALLOC_CLASS_DL_CELL_STREAM ) ;
  if ( cell->y_streams[ row ] == NULL )
    return error_handler( VMERROR ) ;

  cell->y_stream_bit_counts[ row ] = cell->initial_row_bit_count ;
  *bitcurrent = 0 ;
  return TRUE ;
}

/** At the end of encoding a row, truncate the row's stream to the precise size. */

void cellfinishrow( CELL *cell , uint32 row , uint32 bitcount )
{
  HQASSERT( cell->y_streams[ row ] != NULL, "Row missing" ) ;
  HQASSERT( bitcount <= cell->y_stream_bit_counts[ row ] ,
            "Ran out of stream bits!" ) ;

  if ( cell->y_stream_bit_counts[ row ] > bitcount ) {
    mm_truncate( cell->pool, cell->y_streams[ row ],
                 cell->y_stream_bit_counts[ row ] / 8 + 1, bitcount / 8 + 1 ) ;
    cell->y_stream_bit_counts[ row ] = bitcount;
  }
}

/** Return the stored bit stream offset for the row requested. */

void cellgetrowstart( CELL *cell , uint32 row , uint32 *bitcount )
{
  UNUSED_PARAM( CELL* , cell ) ;
  UNUSED_PARAM( uint32 , row) ;
  *bitcount = 0 ;
}

/** \brief Add a cell object to the display list. */

Bool addcelldisplay( DL_STATE *page, CELL *cell )
{
  LISTOBJECT *lobj ;
  dbbox_t bbox ;

  cellbbox( cell , & bbox ) ;
  bbox_intersection( & cclip_bbox , & bbox , & bbox ) ;

  /* General rectangular clipping. */
  if ( degenerateClipping || bbox_is_empty( & bbox )) {
    HQFAIL( "Cell being clipped out: huh?" ) ;
    return TRUE ;
  }

  /* There shouldn't be any need to call finishaddchardisplay - cells
     are added during the render initialise phase, long after any
     possible show. Obviously, should that ever change, this argument
     no longer applies. */

  if ( ! make_listobject( page , RENDER_cell , & bbox, & lobj )) {
    return FALSE ;
  }

  if ( lobj->objectstate != NULL ) {
    CLIPOBJECT *clip = NULL ;

    clip = lobj->objectstate->clipstate ;

    bbox_intersection( & bbox , & clip->bounds , & bbox ) ;
  }

  if ( bbox_is_empty( & bbox )) {
    free_listobject( lobj , page ) ;
    return TRUE ;
  }

  lobj->dldata.cell = cell ;

  /* Cell objects are always in device colors. */

  lobj->marker |= MARKER_DEVICECOLOR ;

  return add_listobject( page, lobj , NULL ) ;
}

/** Add the given cell to the display list. */

Bool docell( /*@in@*/ DL_STATE *page, /*@in@*/ CELL *cell )
{
  HQASSERT_LPTR( cell ) ;

  return DEVICE_CELL(page, cell) ;
}

Bool emptycell( /*@in@*/ const CELL *cell )
{
  return cell->num_lengths == 0 ;
}

/* Log stripped */
