/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:diff.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Differencing predictors for PNG, Flate, etc.
 */


#define OBJECT_MACROS_ONLY

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "dictscan.h"
#include "mm.h"
#include "hqmemset.h"
#include "namedef_.h"

#include "fileio.h"  /* filters require FILELIST */
#include "diff.h"


/* Predictor numbers */

#define DIFF_PREDICTOR_NONE         1
#define DIFF_PREDICTOR_TIFF2        2
#define DIFF_PREDICTOR_PNG_NONE     10
#define DIFF_PREDICTOR_PNG_SUB      11
#define DIFF_PREDICTOR_PNG_UP       12
#define DIFF_PREDICTOR_PNG_AVERAGE  13
#define DIFF_PREDICTOR_PNG_PAETH    14
#define DIFF_PREDICTOR_PNG_OPTIMUM  15




static void  diffDecodePredictorTIFF2( uint8 *current , uint8 *left ,
                                       uint8 *top , uint8 *topleft ) ;
static void  diffDecodePredictorTIFF2_16( uint8 *current , uint8 *left ,
                                       uint8 *top , uint8 *topleft ) ;

static void  diffDecodePredictorPNGNone( uint8 *current , uint8 *left ,
                                         uint8 *top , uint8 *topleft ) ;

static void  diffDecodePredictorPNGSub( uint8 *current , uint8 *left ,
                                        uint8 *top , uint8 *topleft ) ;

static void  diffDecodePredictorPNGUp( uint8 *current , uint8 *left ,
                                       uint8 *top , uint8 *topleft ) ;

static void  diffDecodePredictorPNGAverage( uint8 *current , uint8 *left ,
                                            uint8 *top , uint8 *topleft ) ;

static void  diffDecodePredictorPNGPaeth( uint8 *current , uint8 *left ,
                                          uint8 *top , uint8 *topleft ) ;

static void  diffEncodePredictorTIFF2( uint8 *dest,
                                       uint8 *current , uint8 *left ,
                                       uint8 *top , uint8 *topleft ) ;

static void  diffEncodePredictorPNGNone( uint8 *dest,
                                         uint8 *current , uint8 *left ,
                                         uint8 *top , uint8 *topleft ) ;

static void  diffEncodePredictorPNGSub( uint8 *dest,
                                        uint8 *current , uint8 *left ,
                                        uint8 *top , uint8 *topleft ) ;

static void  diffEncodePredictorPNGUp( uint8 *dest,
                                       uint8 *current , uint8 *left ,
                                       uint8 *top , uint8 *topleft ) ;

static void  diffEncodePredictorPNGAverage( uint8 *dest,
                                            uint8 *current , uint8 *left ,
                                            uint8 *top , uint8 *topleft ) ;

static void  diffEncodePredictorPNGPaeth( uint8 *dest,
                                          uint8 *current , uint8 *left ,
                                          uint8 *top , uint8 *topleft ) ;

/* Initialise the diff state from the given object - can be NULL */

/* diffInit should be called on a DIFF_STATE, if that object is to
 * swap from encoding to decoding (or vice versa).
 */

Bool diffInit( DIFF_STATE *state , OBJECT *theo )
{
  enum {
    match_Predictor, match_Columns, match_Colors, match_BitsPerComponent,
    match_n_entries
  } ;
  static NAMETYPEMATCH thematch[match_n_entries + 1] = {
    { NAME_Predictor | OOPTIONAL ,                1 , { OINTEGER }} ,
    { NAME_Columns | OOPTIONAL ,                  1 , { OINTEGER }} ,
    { NAME_Colors | OOPTIONAL ,                   1 , { OINTEGER }} ,
    { NAME_BitsPerComponent | OOPTIONAL ,         1 , { OINTEGER }} ,
    DUMMY_END_MATCH
  };

  HQASSERT( state , "state NULL in diffInit." ) ;

  /* Initialise the state. */

  state->predictor = 1 ;
  state->columns = 1 ;
  state->colors = 1 ;
  state->bpc = 8 ;
  state->dfn = NULL ;
  state->efn = NULL ;
  state->count = 0 ;
  state->base = NULL ;
  state->current = NULL ;
  state->previous = NULL ;
  state->limit = NULL ;
  state->size = 0 ;

  if ( ! theo )
    return TRUE ;

  if ( ! dictmatch( theo , thematch ))
    return FALSE ;

  /* Unpack and validate any differencing parameters */

  if ( thematch[match_Predictor].result )
    state->predictor = oInteger(*thematch[match_Predictor].result) ;
  if ( thematch[match_Columns].result )
    state->columns = oInteger(*thematch[match_Columns].result) ;
  if ( thematch[match_Colors].result )
    state->colors = oInteger(*thematch[match_Colors].result) ;
  if ( thematch[match_BitsPerComponent].result )
    state->bpc = oInteger(*thematch[match_BitsPerComponent].result) ;

  if (( state->predictor < DIFF_PREDICTOR_NONE ) ||
      (( state->predictor > DIFF_PREDICTOR_TIFF2 ) &&
       ( state->predictor < DIFF_PREDICTOR_PNG_NONE )) ||
      ( state->predictor > DIFF_PREDICTOR_PNG_OPTIMUM ) ||
      ( state->columns <= 0 ) ||
      ( state->colors < 1 ) ||
      ((state->predictor != DIFF_PREDICTOR_NONE) &&
       ( state->bpc != 1 ) &&
       ( state->bpc != 2 ) &&
       ( state->bpc != 4 ) &&
       ( state->bpc != 8 ) &&
       ( state->bpc != 16 )))
    return error_handler( RANGECHECK ) ;

  /* Are we differencing? If so, allocate the differencing buffer. */

  if ( state->predictor != DIFF_PREDICTOR_NONE ) {
    if ( state->predictor >= DIFF_PREDICTOR_PNG_NONE )
      state->bpp = ( state->colors * state->bpc + 7 ) >> 3 ;
    else
      state->bpp = state->colors ;

    if (state->bpc == 16)
      state->bpp *= 2;

    switch ( state->predictor ) {
      case DIFF_PREDICTOR_TIFF2:
        state->dfn = diffDecodePredictorTIFF2 ;
        if (! state->efn) /* If not already set */
          state->efn = diffEncodePredictorTIFF2 ;

        if (state->bpc == 16) {
          state->dfn = diffDecodePredictorTIFF2_16 ;
          state->size = ( state->columns * state->colors * 2) + state->bpp ;
        } else {
          state->size = ((( state->columns * state->colors * state->bpc +
                         7 )) >> 3 ) * ( 8 / state->bpc ) + state->bpp ;
        }
        break ;
      case DIFF_PREDICTOR_PNG_NONE:
        if (! state->efn) /* If not already set */
          state->efn = diffEncodePredictorPNGNone ;
      case DIFF_PREDICTOR_PNG_SUB:
        if (! state->efn) /* If not already set */
          state->efn = diffEncodePredictorPNGSub ;
      case DIFF_PREDICTOR_PNG_UP:
        if (! state->efn) /* If not already set */
          state->efn = diffEncodePredictorPNGUp ;
      case DIFF_PREDICTOR_PNG_AVERAGE:
        if (! state->efn) /* If not already set */
          state->efn = diffEncodePredictorPNGAverage ;
      case DIFF_PREDICTOR_PNG_PAETH:
        if (! state->efn) /* If not already set */
          state->efn = diffEncodePredictorPNGPaeth ;
      case DIFF_PREDICTOR_PNG_OPTIMUM:
        if (! state->efn) { /* If not already set */
          state->efn = diffEncodePredictorPNGNone ;
          state->predictor = DIFF_PREDICTOR_PNG_NONE ;
        }
        /* Predictor will be set from the tags in the data later on. */
        state->size = (( state->columns * state->colors *
                         state->bpc + 7 ) >> 3 ) + state->bpp ;
        break ;
      default:
        HQFAIL( "Bad predictor value: should't get here." ) ;
        return error_handler( RANGECHECK ) ;
    }

    state->base = ( uint8 * )mm_alloc( mm_pool_temp ,
                                       ( state->size ) * 2 ,
                                       MM_ALLOC_CLASS_DIFF_STATE ) ;

    if ( state->base == NULL )
      return error_handler( VMERROR ) ;

    HqMemZero((uint8 *)state->base , state->size * 2);

    state->current = state->base + state->bpp ;
    state->previous = state->base + state->size + state->bpp ;
    state->limit = state->base + state->size ;
  }

  return TRUE ;
}

void diffClose( DIFF_STATE *state )
{
  HQASSERT( state , "state NULL in diffClose." ) ;

  if ( state->base ) {
    mm_free( mm_pool_temp , ( mm_addr_t ) state->base ,
             ( state->size ) * 2 ) ;
    state->base = NULL ;
  }
}

/* Apply differencing predictors to the source data and store it in dest -
 * src and dest may be the same in the decode case. The dest_len parameter
 * contains the maximum allowable number of bytes in the output buffer on
 * entry, on exit contains the count of bytes actually written.
 */

Bool diffDecode( DIFF_STATE *state , uint8 *src , int32 src_len ,
                 uint8 *dest , int32 *dest_len )
{
  int32 dest_count ;

  HQASSERT( state , "state NULL in diffDecode." ) ;
  HQASSERT( dest_len , "dest_len NULL in diffDecode." ) ;

  dest_count = *dest_len ;

  if ( state->predictor == DIFF_PREDICTOR_NONE )
    return TRUE ;

  /* Three stages: copy/unpack, apply differencing and copy/repack */

  while ( src_len > 0 ) {
    int32 icount = src_len ;

    HQASSERT( dest_count > 0 , "No more room in diffDecode." ) ;

    /* Stage 1: copy/unpack */

    {
      uint8 *current = state->current ;
      uint8 *limit = state->limit ;

      /* Get the PNG algorithm tag if necessary */

      if ( state->predictor >= DIFF_PREDICTOR_PNG_NONE ) {
        HQASSERT( state->count >= 0 ,
                  "count out of range in diffDecode." ) ;

        if ( state->count == 0 ) {
          switch ( *src++ + DIFF_PREDICTOR_PNG_NONE ) {
            case DIFF_PREDICTOR_PNG_NONE:
              state->dfn = diffDecodePredictorPNGNone ;
              break ;
            case DIFF_PREDICTOR_PNG_SUB:
              state->dfn = diffDecodePredictorPNGSub ;
              break ;
            case DIFF_PREDICTOR_PNG_UP:
              state->dfn = diffDecodePredictorPNGUp ;
              break ;
            case DIFF_PREDICTOR_PNG_AVERAGE:
              state->dfn = diffDecodePredictorPNGAverage ;
              break ;
            case DIFF_PREDICTOR_PNG_PAETH:
              state->dfn = diffDecodePredictorPNGPaeth ;
              break ;
            case DIFF_PREDICTOR_TIFF2:
            case DIFF_PREDICTOR_PNG_OPTIMUM:
            default:
              HQFAIL( "Bad predictor value: should't get here." ) ;
              return error_handler( RANGECHECK ) ;
          }

          src_len-- ;
          icount-- ;
          state->count = state->size - state->bpp ;

        }
      }

      if ( state->predictor == DIFF_PREDICTOR_TIFF2 ) {
        switch ( state->bpc ) {
          case 1:
            while ((( --icount ) >= 0 ) && ( current < limit )) {
              uint8 byte = *src++ ;

              current[ 7 ] = ( uint8 )( byte & 0x1 ) ;
              byte >>= 1 ;
              current[ 6 ] = ( uint8 )( byte & 0x1 ) ;
              byte >>= 1 ;
              current[ 5 ] = ( uint8 )( byte & 0x1 ) ;
              byte >>= 1 ;
              current[ 4 ] = ( uint8 )( byte & 0x1 ) ;
              byte >>= 1 ;
              current[ 3 ] = ( uint8 )( byte & 0x1 ) ;
              byte >>= 1 ;
              current[ 2 ] = ( uint8 )( byte & 0x1 ) ;
              byte >>= 1 ;
              current[ 1 ] = ( uint8 )( byte & 0x1 ) ;
              byte >>= 1 ;
              current[ 0 ] = ( uint8 )( byte & 0x1 ) ;

              current += 8 ;
            }
            break ;
          case 2:
            while ((( --icount ) >= 0 ) && ( current < limit )) {
              uint8 byte = *src++ ;

              current[ 3 ] = ( uint8 )( byte & 0x3 ) ;
              byte >>= 2 ;
              current[ 2 ] = ( uint8 )( byte & 0x3 ) ;
              byte >>= 2 ;
              current[ 1 ] = ( uint8 )( byte & 0x3 ) ;
              byte >>= 2 ;
              current[ 0 ] = ( uint8 )( byte & 0x3 ) ;

              current += 4 ;
            }
            break ;
          case 4:
            while ((( --icount ) >= 0 ) && ( current < limit )) {
              uint8 byte = *src++ ;

              current[ 1 ] = ( uint8 )( byte & 0xf ) ;
              byte >>= 4 ;
              current[ 0 ] = ( uint8 )( byte & 0xf ) ;

              current += 2 ;
            }
            break ;
          case 16:
          case 8:
            while ((( --icount ) >= 0 ) && ( current < limit )) {
              *current++ = *src++ ;
            }
            break ;
        }
      }
      else {
        while ((( --icount ) >= 0 ) && ( current < limit )) {
          *current++ = *src++ ;
        }
      }

      icount++ ;
      /* This value is only needed for the PNG case, but it's quicker to

       * just calculate it every time.

       */
      state->count -= ( src_len - icount ) ;
    }

    /* Stage 2: apply differencing */

    {
      uint8 *current = state->current ;
      uint8 *top = state->previous ;
      uint8 *left = state->current - state->bpp ;
      uint8 *topleft = state->previous - state->bpp ;
      uint8 *limit = state->limit ;

      HQASSERT( state->dfn ,
                "About to jump to NULL in diffDecode!" ) ;

      while ( current < limit ) {
        ( * state->dfn )( current , left , top , topleft ) ;
        current++ ;
        top++ ;
        left++ ;
        topleft++ ;
        if (state->bpc == 16) {
          current++ ;
          top++ ;
          left++ ;
          topleft++ ;
        }
      }
    }

    /* Stage 3: copy/repack */

    {
      uint8 *current = state->current ;
      int32 i = src_len - icount ;

      dest_count -= i ;

      if ( state->predictor == DIFF_PREDICTOR_TIFF2 ) {
        switch ( state->bpc ) {
          case 1:
            while ( i-- ) {
              uint8 byte ;

              byte = ( uint8 )( current[ 0 ] & 0x1 ) ;
              byte <<= 1 ;
              byte |= ( current[ 1 ] & 0x1 ) ;
              byte <<= 1 ;
              byte |= ( current[ 2 ] & 0x1 ) ;
              byte <<= 1 ;
              byte |= ( current[ 3 ] & 0x1 ) ;
              byte <<= 1 ;
              byte |= ( current[ 4 ] & 0x1 ) ;
              byte <<= 1 ;
              byte |= ( current[ 5 ] & 0x1 ) ;
              byte <<= 1 ;
              byte |= ( current[ 6 ] & 0x1 ) ;
              byte <<= 1 ;
              byte |= ( current[ 7 ] & 0x1 ) ;

              *dest++ = byte ;
              current += 8 ;
            }
            break ;
          case 2:
            while ( i-- ) {
              uint8 byte ;

              byte = ( uint8 )( current[ 0 ] & 0x3 ) ;
              byte <<= 2 ;
              byte |= ( current[ 1 ] & 0x3 ) ;
              byte <<= 2 ;
              byte |= ( current[ 2 ] & 0x3 ) ;
              byte <<= 2 ;
              byte |= ( current[ 3 ] & 0x3 ) ;

              *dest++ = byte ;
              current += 4 ;
            }
            break ;
          case 4:
            while ( i-- ) {
              uint8 byte ;

              byte = ( uint8 )( current[ 0 ] & 0xf ) ;
              byte <<= 4 ;
              byte |= ( current[ 1 ] & 0xf ) ;

              *dest++ = byte ;
              current += 2 ;
            }
            break ;
          case 16:
          case 8:
            {
              while ( i-- ) {
                *dest++ = *current++ ;
              }
            }
            break ;
        }
      }
      else {
        while ( i-- ) {
          *dest++ = *current++ ;
        }
      }

      HQASSERT( current <= state->limit ,
                "Shouldn't exceed limit here in diffDecode." ) ;

      if ( current == state->limit ) {
        /* Switch the current and previous pointers */

        if ( current == state->base + state->size ) {
          state->previous = state->base + state->bpp ;
          state->current = state->base + state->size + state->bpp ;
          state->limit = state->base + 2 * state->size ;
        }
        else {
          state->previous = state->base + state->size + state->bpp ;
          state->current = state->base + state->bpp ;
          state->limit = state->base + state->size ;
        }
      }
      else {
        state->previous += ( current - state->current ) ;
        state->current = current ;
      }
    }

    src_len = icount ;
  }

  /* Output length takes account of any PNG tag bytes present */

  *dest_len -= dest_count ;

  return TRUE ;
}


/* All the predictor functions deliberately limit precision so that
 * an overflow is entirely possible: this is OK, believe me.
 */

/* DECODING */

/* TIFF2 predictor */

static void diffDecodePredictorTIFF2( uint8 *current , uint8 *left ,
                                      uint8 *top , uint8 *topleft )
{
  HQASSERT( current , "current NULL in diffDecodePredictorTIFF2." ) ;
  HQASSERT( left , "left NULL in diffDecodePredictorTIFF2." ) ;

  UNUSED_PARAM( uint8 * , top ) ;
  UNUSED_PARAM( uint8 * , topleft ) ;

  *current = ( uint8 )( *current + *left ) ;
}

static void diffDecodePredictorTIFF2_16( uint8 *current , uint8 *left ,
                                      uint8 *top , uint8 *topleft )
{
  uint16 val1,val2;

  HQASSERT( current , "current NULL in diffDecodePredictorTIFF2_16." ) ;
  HQASSERT( left , "left NULL in diffDecodePredictorTIFF2_16." ) ;

  UNUSED_PARAM( uint8 * , top ) ;
  UNUSED_PARAM( uint8 * , topleft ) ;

  /* Bigendian 16 bit add */
  val1 = (uint16)current[1] ;
  val1 |= ((uint16)current[0])<<8 ;

  val2 = (uint16)left[1] ;
  val2 |= ((uint16)left[0])<<8 ;

  val1 = (uint16)(val1 + val2) ;

  *current++ = (uint8)(val1 >> 8) ;
  *current = (uint8)(val1 & 0xff) ;
}

/* PNG predictors */

static void diffDecodePredictorPNGNone( uint8 *current , uint8 *left ,
                                        uint8 *top , uint8 *topleft )
{
  UNUSED_PARAM( uint8 * , current ) ;
  UNUSED_PARAM( uint8 * , left ) ;
  UNUSED_PARAM( uint8 * , top ) ;
  UNUSED_PARAM( uint8 * , topleft ) ;
}

static void diffDecodePredictorPNGSub( uint8 *current , uint8 *left ,
                                       uint8 *top , uint8 *topleft )
{
  HQASSERT( current , "current NULL in diffDecodePredictorPNGSub." ) ;
  HQASSERT( left , "left NULL in diffDecodePredictorPNGSub." ) ;

  UNUSED_PARAM( uint8 * , top ) ;
  UNUSED_PARAM( uint8 * , topleft ) ;

  *current = ( uint8 )( *current + *left ) ;
}

static void diffDecodePredictorPNGUp( uint8 *current , uint8 *left ,
                                      uint8 *top , uint8 *topleft )
{
  HQASSERT( current , "current NULL in diffDecodePredictorPNGUp." ) ;
  HQASSERT( top , "top NULL in diffDecodePredictorPNGUp." ) ;

  UNUSED_PARAM( uint8 * , left ) ;
  UNUSED_PARAM( uint8 * , topleft ) ;

  *current = ( uint8 )( *current + *top ) ;
}

static void diffDecodePredictorPNGAverage( uint8 *current , uint8 *left ,
                                           uint8 *top , uint8 *topleft )
{
  HQASSERT( current , "current NULL in diffDecodePredictorPNGAverage." ) ;
  HQASSERT( left , "left NULL in diffDecodePredictorPNGAverage." ) ;
  HQASSERT( top , "top NULL in diffDecodePredictorPNGAverage." ) ;

  UNUSED_PARAM( uint8 * , topleft ) ;

  *current = ( uint8 ) ( *current + (( ( uint32 ) *top + ( uint32 ) *left ) / 2 )) ;
}

static void diffDecodePredictorPNGPaeth( uint8 *current , uint8 *left ,
                                         uint8 *top , uint8 *topleft )
{
  int32 p ;
  int32 pa ;
  int32 pb ;
  int32 pc ;

  HQASSERT( current , "current NULL in diffDecodePredictorPNGPaeth." ) ;
  HQASSERT( left , "left NULL in diffDecodePredictorPNGPaeth." ) ;
  HQASSERT( top , "top NULL in diffDecodePredictorPNGPaeth." ) ;
  HQASSERT( topleft , "topleft NULL in diffDecodePredictorPNGPaeth." ) ;

  p = *left + *top - *topleft ;
  pa = abs( p - *left ) ;
  pb = abs( p - *top ) ;
  pc = abs( p - *topleft ) ;

  if (( pa <= pb ) && ( pa <= pc )) {
    *current = ( uint8 )( *current + *left ) ;
  }
  else {
    if ( pb <= pc )
      *current = ( uint8 )( *current + *top ) ;
    else
      *current = ( uint8 )( *current + *topleft ) ;
  }
}

/* ENCODING */

/* TIFF2 predictor */

static void diffEncodePredictorTIFF2( uint8 *dest,
                                      uint8 *current , uint8 *left ,
                                      uint8 *top , uint8 *topleft )
{
  HQASSERT( current , "current NULL in diffEncodePredictorTIFF2." ) ;
  HQASSERT( left , "left NULL in diffEncodePredictorTIFF2." ) ;
  HQASSERT( dest , "dest NULL in diffEncodePredictorTIFF2." ) ;

  UNUSED_PARAM( uint8 * , top ) ;
  UNUSED_PARAM( uint8 * , topleft ) ;

  *dest = ( uint8 )( *current - *left ) ;
}

/* PNG predictors */

static void diffEncodePredictorPNGNone( uint8 *dest,
                                        uint8 *current , uint8 *left ,
                                        uint8 *top , uint8 *topleft )
{
  UNUSED_PARAM( uint8 * , left ) ;
  UNUSED_PARAM( uint8 * , top ) ;
  UNUSED_PARAM( uint8 * , topleft ) ;

  *dest = *current;
}

static void diffEncodePredictorPNGSub( uint8 *dest,
                                       uint8 *current , uint8 *left ,
                                       uint8 *top , uint8 *topleft )
{
  HQASSERT( current , "current NULL in diffEncodePredictorPNGSub." ) ;
  HQASSERT( left , "left NULL in diffEncodePredictorPNGSub." ) ;
  HQASSERT( dest , "dest NULL in diffEncodePredictorPNGSub." ) ;

  UNUSED_PARAM( uint8 * , top ) ;
  UNUSED_PARAM( uint8 * , topleft ) ;

  *dest = ( uint8 )( *current - *left ) ;
}

static void diffEncodePredictorPNGUp( uint8 *dest,
                                      uint8 *current , uint8 *left ,
                                      uint8 *top , uint8 *topleft )
{
  HQASSERT( current , "current NULL in diffEncodePredictorPNGUp." ) ;
  HQASSERT( top , "top NULL in diffEncodePredictorPNGUp." ) ;
  HQASSERT( dest , "dest NULL in diffEncodePredictorPNGUp." ) ;

  UNUSED_PARAM( uint8 * , left ) ;
  UNUSED_PARAM( uint8 * , topleft ) ;

  *dest = ( uint8 )( *current - *top ) ;
}

static void diffEncodePredictorPNGAverage( uint8 *dest,
                                           uint8 *current , uint8 *left ,
                                           uint8 *top , uint8 *topleft )
{
  HQASSERT( current , "current NULL in diffEncodePredictorPNGAverage." ) ;
  HQASSERT( left , "left NULL in diffEncodePredictorPNGAverage." ) ;
  HQASSERT( top , "top NULL in diffEncodePredictorPNGAverage." ) ;
  HQASSERT( dest , "dest NULL in diffEncodePredictorPNGAverage." ) ;

  UNUSED_PARAM( uint8 * , topleft ) ;

  *dest = ( uint8 ) ( *current - (( ( uint32 ) *top + ( uint32 ) *left ) / 2 )) ;
}

static void diffEncodePredictorPNGPaeth( uint8 *dest,
                                         uint8 *current , uint8 *left ,
                                         uint8 *top , uint8 *topleft )
{
  int32 p ;
  int32 pa ;
  int32 pb ;
  int32 pc ;

  HQASSERT( current , "current NULL in diffEncodePredictorPNGPaeth." ) ;
  HQASSERT( left , "left NULL in diffEncodePredictorPNGPaeth." ) ;
  HQASSERT( top , "top NULL in diffEncodePredictorPNGPaeth." ) ;
  HQASSERT( topleft , "topleft NULL in diffEncodePredictorPNGPaeth." ) ;
  HQASSERT( dest , "dest NULL in diffEncodePredictorPNGPaeth." ) ;

  p = *left + *top - *topleft ;
  pa = abs( p - *left ) ;
  pb = abs( p - *top ) ;
  pc = abs( p - *topleft ) ;

  if (( pa <= pb ) && ( pa <= pc )) {
    *dest = ( uint8 )( *current - *left ) ;
  }
  else {
    if ( pb <= pc )
      *dest = ( uint8 )( *current - *top ) ;
    else
      *dest = ( uint8 )( *current - *topleft ) ;
  }
}


/* Apply differencing predictors to the source data and store it in dest,
 * unless there is no predictor - src and dest must not be the same for
 * encode as the encoded buffer will be longer than the source for PNG
 * predictors. The dest_len parameter contains the maximum allowable number
 * of bytes in the output buffer on entry, on exit contains the count of bytes
 * actually written. src_len parameter contains the maximmum number of input
 * data bytes, and at exit contains the number consumed. For PNG predictor
 * this is not the same as the number of destination bytes written.
 * A byte is only consumed if its difference can be output. At exit,
 * *src_len <= *dest_len.
 * diff_state maintains enough state so that diffEncode can be used
 * iteratively. If there is not enough destination space to output all
 * of the differenced version of the source, the remainder may be
 * retrieved by calling diffEncode again, using the previously returned
 * src_len value to indicate where in the source buffer the difference
 * process should restart.
 * If no predictor is specified -OR- the src_len is 0,
 * dest_len returns as -1 and the source is NOT copied into dest. This is an
 * optimisation.
 *
 */
Bool diffEncode( DIFF_STATE *state, uint8* src, int32* src_len,
                 uint8* dest, int32* dest_len )
{
/*
 * This version processes input one sample at a time, reading
 * and differencing each sample as it is read. This is a simple
 * means of ensuring synchronization between the writing and
 * reading. If a byte is read, then its differenced version will
 * be written out in the same call to diffEncode.
 */

  int32 local_count ; /* tracks amount of space in the differencing buffer. */
  int32 dest_count ; /* tracks available space for output. */
  int32 src_count ; /* tracks amount of data for input. */
  uint8* current_ptr ; /* tracks current datum position, for writing to
                          scan line buffer. */
  uint8* previous_ptr ; /* tracks the corresponding position of the current
                           dataum in the previous line.  */

  HQASSERT( state , "state NULL in diffEncode." ) ;
  HQASSERT( src != dest, "dest and src are the same.");
  HQASSERT( *dest_len >= 0, "dest_len negative in diffEncode");
  HQASSERT( *src_len >= 0, "src_len is negatvie in diffEncode") ;

  dest_count = *dest_len ;
  src_count = *src_len ;
  local_count = state->count ;
  current_ptr = state->current ;
  previous_ptr = state->previous ;

  /* dest_count has to reflect the number predictor flags
   * written into the differenced output, for PNG predicotr useage. */

  /* Optimisation. The caller MUST test dest_len for -1 as the src
   * ought to be used for the encoding.
   */
  if ( state->predictor == DIFF_PREDICTOR_NONE || *src_len == 0) {
    *dest_len = -1;
    return TRUE ;
  }

  while ( src_count && dest_count )
  {

    HQASSERT (local_count >= 0, "Illegal count value in diffEncode") ;

    /* local_count serves dual purpoase, as a flag to indicate new output
     * line (and so need for a PNG predictor flag, if appropriate), and
     * also as an indicator that the whole scan line of input information
     * has been read. Hence, must be in sync for both input and
     * output.
     * if count == 0 at this point, we are actually at the start of
     * processing a scan line of data.
     * Reset count to read in a full scan line.
     * processing of data depends on the predictor type and for TIFF,
     * the bits per components.
     */

    if ( local_count == 0 ) {
      if ( state->predictor >= DIFF_PREDICTOR_PNG_NONE)
      {
        *dest++ = (uint8)(state->predictor - DIFF_PREDICTOR_PNG_NONE) ;
        dest_count-- ;
      }
      local_count = state->size - state->bpp ;
    }

    /* if a char is read, then it is differenced and output.
     * Let us difference...
     */

    if ( state->predictor == DIFF_PREDICTOR_TIFF2 && state->bpc != 8 )
    {
      uint8 mask = 0 ;
      int32 ppb; /* components per byte */
      uint8 * left = current_ptr - state->bpp ;

      /* Setup a low bit mask.
       * mask could go into the initialization of the filter, as could
       * calculation of the bytes per pixel (bpp)
       */
      switch ( state->bpc )
      {
        case 1:
          mask = 0x01 ;
          break ;
        case 2:
          mask = 0x03 ;
          break ;
        case 4:
          mask = 0x0F ;
          break ;
        default:
          HQFAIL ( "Unsupported bits per component in diffEncode" ) ;
          break ;
      }

      HQASSERT ( state->bpc == 1 || state->bpc == 2 || state->bpc == 4 ,
                  "Illegal BitsPerComponent value in diffEncode" ) ;

      ppb = 8 / state->bpc ;

      previous_ptr = left ;


      while ( local_count && src_count && dest_count )
      {
        uint8 cbyte, dbyte;
        int32 c, bpc = state->bpc;

        /* current and previous buffers will hold the
         * unpacked color component values.
         * Expanding each component into its own byte
         * means it is safe to split the processing
         * of a colour value into chunks. E.g a 3 color, 4 bit
         * sample is differenced using 2 bytes of working space.
         * The colour value is read as 2 bytes, expanded into the
         * 3 components of one sample and the first component of
         * second value. These can be differenced independently,
         * and repacked into bytes.
         */

        cbyte = *src++ ;
        --src_count ;
        local_count -= ppb ; /* each byte input give ppb components */

        HQASSERT ( local_count >= 0,
                   "Illegal state in diffEncode (local_count) " ) ;

        /* Perform pixel diffs directly. */
        dbyte = 0x00;

        /* Unpack pixels. */
        for (c = ppb - 1; c >= 0; c--) {
          current_ptr[c] = (uint8)(cbyte & mask); cbyte >>= bpc;
        }

        /* Calculate diff and re-pack into a byte. */
        for (c=0; c < ppb; c++) {
          /* Truncate subtraction. */
          dbyte |= ((uint8)(current_ptr[c] - previous_ptr[c]) & mask);
          /* Don't shift on last pixel. */
          if (c < ppb - 1)
            dbyte <<= bpc;
        }

        current_ptr += ppb ;
        previous_ptr += ppb ;

        /* Place pixel diffs into destination. */
        *dest++ = dbyte ;
        --dest_count ;
      }

    }
    else
    {
      /* top is the previous ptr ,left is bpp bytes to left of current_ptr,
       * topleft is bpp bytes to the left of previous.
       */

      uint8 *left = current_ptr - state->bpp ;
      uint8 *topleft = previous_ptr - state->bpp ;

      /* iterations can be set to the minimum of local_count,
       * src_count and dest_count, if you'd prefer.
       */

      while ( local_count && src_count && dest_count )
      {
        /* buf_ptr is where we store the input data,
         * state->previous is the corresponding point of previous line of data.
         */
        *current_ptr = *src++;
        src_count-- ;
        local_count-- ;

        /* do the differencing. */
        ( * state->efn )( dest, current_ptr , left , previous_ptr , topleft ) ;

        /* previous and current ptrs move in step. */
        ++dest ;
        --dest_count ;
        ++previous_ptr ;
        ++left ;
        ++topleft ;
        ++current_ptr ;

      }

    }

    if ( local_count == 0 )
    {
      /* input buffer is full - we have processed a full scan line.
       * swap over the buffers.
       * don't output a PNG predicitor indicator here - leave that to
       * the top of the loop, to cope with the case where we have
       * also run out of destination space.
       */

      HQASSERT(  (current_ptr == (state->base + state->size) )
       || ( current_ptr == (state->base + 2 * state->size) ) ,
         "Invalid current_ptr at end of scan line in diffEncode" ) ;

      if ( current_ptr == state->base + state->size )
      {
        previous_ptr = state->previous = state->base + state->bpp ;
        current_ptr = state->current = state->base + state->size + state->bpp ;
        state->limit = state->base + 2 * state->size ;
      }
      else
      {
        previous_ptr = state->previous =
          state->base + state->size + state->bpp ;
        current_ptr = state->current = state->base + state->bpp ;
        state->limit = state->base + state->size ;
      }

    }

    /* otherwise leave count as it is, we will pick up the differencing
     * at this point on the next call to diffEncode.
     */

  }

  /* Update the number of characters consummed and written.
   * Update DIFF_STATE so we can carry on differencing from the
   * next call to diffEncode.
   * The caller is responsible for adjusting the data source and
   * sink pointers.
   */
  *dest_len = (*dest_len) - dest_count ;
  *src_len = (*src_len) - src_count ;
  state->count = local_count ;
  state->current = current_ptr ;
  state->previous = previous_ptr ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
* Log stripped */
