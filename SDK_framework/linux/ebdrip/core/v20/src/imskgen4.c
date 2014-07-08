/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:imskgen4.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * MaskedImage Type 4 code
 */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "objects.h"
#include "caching.h"
#include "swerrors.h"
#include "mm.h"
#include "mmcompat.h"

#include "imskgen4.h"

/* -------------------------------------------------------------------------- */

#define GENMASKTEST1( n ) \
 ( _val_ == maskcolor##n )
#define GENMASKTEST2( n ) \
 (( (_val_ - lmaskcolor##n) | (rmaskcolor##n - _val_) ) >= 0 )

#define GENMASK1( t ) MACRO_START               \
  int32 _val_ ;                                 \
  _val_ = iubuf[ 0 ] ;                          \
  if ( GENMASKTEST##t( 0 ))                     \
    _mask_ |= _bit_ ;                           \
  _bit_ >>= 1 ;                                 \
  iubuf += 1 ;                                  \
MACRO_END

#define GENMASK3( t ) MACRO_START               \
  int32 _val_ ;                                 \
  _val_ = iubuf[ 0 ] ;                          \
  if ( GENMASKTEST##t( 0 )) {                   \
    _val_ = iubuf[ 1 ] ;                        \
    if ( GENMASKTEST##t( 1 )) {                 \
      _val_ = iubuf[ 2 ] ;                      \
      if ( GENMASKTEST##t( 2 ))                 \
        _mask_ |= _bit_ ;                       \
    }                                           \
  }                                             \
  _bit_ >>= 1 ;                                 \
  iubuf += 3 ;                                  \
MACRO_END

#define GENMASK4( t ) MACRO_START               \
  int32 _val_ ;                                 \
  _val_ = iubuf[ 0 ] ;                          \
  if ( GENMASKTEST##t( 0 )) {                   \
    _val_ = iubuf[ 1 ] ;                        \
    if ( GENMASKTEST##t( 1 )) {                 \
      _val_ = iubuf[ 2 ] ;                      \
      if ( GENMASKTEST##t( 2 )) {               \
        _val_ = iubuf[ 3 ] ;                    \
        if ( GENMASKTEST##t( 3 ))               \
          _mask_ |= _bit_ ;                     \
      }                                         \
    }                                           \
  }                                             \
  _bit_ >>= 1 ;                                 \
  iubuf += 4 ;                                  \
MACRO_END

#define GENMASKN( t ) MACRO_START               \
  int32 _n_ = 0 ;                               \
  int32 *_mcol_ = maskcolor ;                   \
  while ( _n_ + 4 <= ncomps ) {                 \
    int32 *_unpack_ = iubuf + _n_ ;             \
    int32 _val_ = _unpack_[ 0 ] ;               \
    if ( ! GENMASKTEST##t( 0 ))                 \
      break ;                                   \
    _val_ = _unpack_[ 1 ] ;                     \
    if ( ! GENMASKTEST##t( 1 ))                 \
      break ;                                   \
    _val_ = _unpack_[ 2 ] ;                     \
    if ( ! GENMASKTEST##t( 2 ))                 \
      break ;                                   \
    _val_ = _unpack_[ 3 ] ;                     \
    if ( ! GENMASKTEST##t( 3 ))                 \
      break ;                                   \
    _mcol_ += 4 * t ;                           \
    _n_ += 4 ;                                  \
  }                                             \
  if ( _n_ + 4 > ncomps ) {                     \
    while ( _n_ < ncomps ) {                    \
      int32 _val_ = iubuf[ _n_ ] ;              \
      if ( ! GENMASKTEST##t( 0 ))               \
        break ;                                 \
      _mcol_ += t ;                             \
      _n_ += 1 ;                                \
    }                                           \
    if ( _n_ == ncomps )                        \
      _mask_ |= _bit_ ;                         \
  }                                             \
  _bit_ >>= 1 ;                                 \
  iubuf += ncomps ;                             \
MACRO_END

#define GENMASK( comp , t ) MACRO_START                 \
  while ( nconv >= 8 ) {                                \
    uint8 _mask_ = 0 ;                                  \
    uint8 _bit_ = 1 << 7 ;                              \
    nconv -= 8 ;                                        \
    GENMASK##comp( t ) ;                                \
    GENMASK##comp( t ) ;                                \
    GENMASK##comp( t ) ;                                \
    GENMASK##comp( t ) ;                                \
    GENMASK##comp( t ) ;                                \
    GENMASK##comp( t ) ;                                \
    GENMASK##comp( t ) ;                                \
    GENMASK##comp( t ) ;                                \
    mbuf[ 0 ] = _mask_ ;                                \
    mbuf += 1 ;                                         \
  }                                                     \
  if ( nconv > 0 ) {                                    \
    uint8 _mask_ = 0 ;                                  \
    uint8 _bit_ = 1 << 7 ;                              \
    do {                                                \
      GENMASK##comp( t ) ;                              \
      nconv -= 1 ;                                      \
    } while ( nconv > 0 ) ;                             \
    mbuf[ 0 ] = _mask_ ;                                \
  }                                                     \
MACRO_END

static void im_genmask1_1( int32 *iubuf ,
                           uint8 *mbuf ,
                           int32 nconv ,
                           int32 *maskcolor )
{
  int32 maskcolor0 = maskcolor[ 0 ] ;
  GENMASK( 1 , 1 ) ;
}

static void im_genmask1_2( int32 *iubuf ,
                           uint8 *mbuf ,
                           int32 nconv ,
                           int32 *maskcolor )
{
  int32 lmaskcolor0 = maskcolor[ 0 ] ;
  int32 rmaskcolor0 = maskcolor[ 1 ] ;
  GENMASK( 1 , 2 ) ;
}

static void im_genmask3_1( int32 *iubuf ,
                           uint8 *mbuf ,
                           int32 nconv ,
                           int32 *maskcolor )
{
  int32 maskcolor0 = maskcolor[ 0 ] ;
  int32 maskcolor1 = maskcolor[ 1 ] ;
  int32 maskcolor2 = maskcolor[ 2 ] ;
  GENMASK( 3 , 1 ) ;
}

static void im_genmask3_2( int32 *iubuf ,
                           uint8 *mbuf ,
                           int32 nconv ,
                           int32 *maskcolor )
{
  int32 lmaskcolor0 = maskcolor[ 0 ] ;
  int32 rmaskcolor0 = maskcolor[ 1 ] ;
  int32 lmaskcolor1 = maskcolor[ 2 ] ;
  int32 rmaskcolor1 = maskcolor[ 3 ] ;
  int32 lmaskcolor2 = maskcolor[ 4 ] ;
  int32 rmaskcolor2 = maskcolor[ 5 ] ;
  GENMASK( 3 , 2 ) ;
}

static void im_genmask4_1( int32 *iubuf ,
                           uint8 *mbuf ,
                           int32 nconv ,
                           int32 *maskcolor )
{
  int32 maskcolor0 = maskcolor[ 0 ] ;
  int32 maskcolor1 = maskcolor[ 1 ] ;
  int32 maskcolor2 = maskcolor[ 2 ] ;
  int32 maskcolor3 = maskcolor[ 3 ] ;
  GENMASK( 4 , 1 ) ;
}

static void im_genmask4_2( int32 *iubuf ,
                           uint8 *mbuf ,
                           int32 nconv ,
                           int32 *maskcolor )
{
  int32 lmaskcolor0 = maskcolor[ 0 ] ;
  int32 rmaskcolor0 = maskcolor[ 1 ] ;
  int32 lmaskcolor1 = maskcolor[ 2 ] ;
  int32 rmaskcolor1 = maskcolor[ 3 ] ;
  int32 lmaskcolor2 = maskcolor[ 4 ] ;
  int32 rmaskcolor2 = maskcolor[ 5 ] ;
  int32 lmaskcolor3 = maskcolor[ 6 ] ;
  int32 rmaskcolor3 = maskcolor[ 7 ] ;
  GENMASK( 4 , 2 ) ;
}

static void im_genmaskN_1( int32 *iubuf ,
                           uint8 *mbuf ,
                           int32 nconv ,
                           int32 ncomps ,
                           int32 *maskcolor )
{
#define maskcolor0 _mcol_[ 0 ]
#define maskcolor1 _mcol_[ 1 ]
#define maskcolor2 _mcol_[ 2 ]
#define maskcolor3 _mcol_[ 3 ]
  GENMASK( N , 1 ) ;
#undef maskcolor0
#undef maskcolor1
#undef maskcolor2
#undef maskcolor3
}

static void im_genmaskN_2( int32 *iubuf ,
                           uint8 *mbuf ,
                           int32 nconv ,
                           int32 ncomps ,
                           int32 *maskcolor )
{
#define lmaskcolor0 _mcol_[ 0 ]
#define rmaskcolor0 _mcol_[ 1 ]
#define lmaskcolor1 _mcol_[ 2 ]
#define rmaskcolor1 _mcol_[ 3 ]
#define lmaskcolor2 _mcol_[ 4 ]
#define rmaskcolor2 _mcol_[ 5 ]
#define lmaskcolor3 _mcol_[ 6 ]
#define rmaskcolor3 _mcol_[ 7 ]
  GENMASK( N , 2 ) ;
#undef lmaskcolor0
#undef rmaskcolor0
#undef lmaskcolor1
#undef rmaskcolor1
#undef lmaskcolor2
#undef rmaskcolor2
#undef lmaskcolor3
#undef rmaskcolor3
}

/* for the generation of the mask from a chromo-key image. */
struct maskgendata {
  int32 ncomps ;
  int32 nconv ;
  int32 *maskcolor ;
  int32 range ;
} ;

MASKGENDATA* im_genmaskopen( int32 ncomps , int32 nconv , OBJECT *maskcolor )
{
  MASKGENDATA *mg ;
  int32 *colorptr , len , range , i ;

  HQASSERT( ncomps > 0 , "ncomps should be +ve" ) ;
  HQASSERT( maskcolor != NULL ,
            "im_genmaskopen: maskcolor NULL" ) ;
  HQASSERT(oType(*maskcolor) == OINTEGER ||
           oType(*maskcolor) == OARRAY ||
           oType(*maskcolor) == OPACKEDARRAY,
           "im_genmaskopen: maskcolor is not an integer/array" ) ;

  if ( ! oCanRead(*maskcolor) )  /* must be readable */
    if ( ! object_access_override(maskcolor) ) {
      ( void ) error_handler( INVALIDACCESS ) ;
      return NULL ;
    }

  /* MaskColor must be one set of color components or two sets
     specifying a range of colors for the mask. */
  len = (oType(*maskcolor) == OINTEGER) ? 1 : theILen( maskcolor ) ;
  if ( len != ncomps && len != ncomps + ncomps ) {
    ( void ) error_handler( RANGECHECK ) ;
    return NULL ;
  }

  HQASSERT( len >= 1 , "/MaskColor len must be at least one" ) ;
  range = len != ncomps ;

  mg = ( MASKGENDATA * )mm_alloc( mm_pool_temp ,
                                  sizeof( MASKGENDATA ) ,
                                  MM_ALLOC_CLASS_IMAGE ) ;
  if ( mg == NULL ) {
    ( void ) error_handler( VMERROR ) ;
    return NULL ;
  }

  mg->maskcolor =
    ( int32 * )mm_alloc_with_header( mm_pool_temp ,
                                     len * sizeof( int32 ) ,
                                     MM_ALLOC_CLASS_IMAGE ) ;
  if ( mg->maskcolor == NULL ) {
    im_genmaskfree( mg ) ;
    ( void ) error_handler( VMERROR ) ;
    return NULL ;
  }

  colorptr = mg->maskcolor ;

  if ( oType(*maskcolor) == OINTEGER ) {
    colorptr[ 0 ] = oInteger(*maskcolor) ;
  }
  else {
    int32 collapse = range ;
    maskcolor = oArray(*maskcolor) ;
    HQASSERT( maskcolor != NULL , "object list in maskcolor is null" ) ;
    for ( i = 0 ; i < len ; ++i ) {
      /* Reals are allowed here because of request 12705, i.e. a work around for
       * a real job. */
      if ( oType(*maskcolor) == OINTEGER ) {
        colorptr[ 0 ] = oInteger(*maskcolor) ;
      }
      else if (oType(*maskcolor) == OREAL) {
        colorptr[ 0 ] = (int32) oReal(*maskcolor) ;
      }
      else {
        im_genmaskfree( mg ) ;
        ( void ) error_handler( TYPECHECK ) ;
        return NULL ;
      }
      if ( collapse && ( i & 1 ) != 0 )
        collapse = colorptr[ 0 ] == colorptr[ -1 ] ;
      ++colorptr ;
      ++maskcolor ;
    }

    if ( collapse ) {
      /* Specified a range with identical left/right bounds so collapse
         so mask generation will go quicker. */
      int32 *colorptr2 ;
      colorptr = colorptr2 = mg->maskcolor ;
      /* i starts at 1 - no need to do 0. */
      len >>= 1 ;
      for ( i = 1 ; i < len ; ++i ) {
        colorptr += 1 ;
        colorptr2 += 2 ;
        *colorptr = *colorptr2 ;
      }
      range = FALSE ;
    }
  }

  mg->ncomps = ncomps ;
  mg->nconv = nconv ;
  mg->range = range ;

  return mg ;
}

/* This routine is needed for PDF out. Rather than expose the detail of
 * internal storage, this simply provides a simple interface to get to
 * the color mask array.
 */
void im_genmask_data( MASKGENDATA *mg , int32 *nvalues , int32 **colorinfo )
{
  HQASSERT(mg != NULL, "maskgen data is NULL");
  HQASSERT(nvalues != NULL, "navlues is NULL");
  HQASSERT(colorinfo != NULL, "colorinfo is NULL");

  *nvalues = mg->range ? mg->ncomps * 2 : mg->ncomps;
  *colorinfo = mg->maskcolor;
}

void im_genmaskfree( MASKGENDATA *mg )
{
  HQASSERT( mg , "im_genmask: mg NULL" ) ;
  if ( mg->maskcolor != NULL )
    mm_free_with_header( mm_pool_temp , ( mm_addr_t )mg->maskcolor ) ;
  mm_free( mm_pool_temp , ( mm_addr_t )mg , sizeof( MASKGENDATA ) ) ;
}

#if defined( ASSERT_BUILD )
static void im_genmask_verify( MASKGENDATA *mg , int32 *iubuf , uint8 *ombuf ) ;
#endif

void im_genmask( MASKGENDATA *mg , int32 *iubuf , uint8 *mbuf )
{
  HQASSERT( mg , "mg NULL in im_genmask" ) ;
  HQASSERT( mg->maskcolor , "maskcolor NULL in im_genmask" ) ;
  HQASSERT( mbuf , "mbuf NULL in im_genmask" ) ;

  if ( mg->range )
    switch ( mg->ncomps ) {
    case 1:
      im_genmask1_2( iubuf , mbuf , mg->nconv , mg->maskcolor ) ;
      break ;
    case 3:
      im_genmask3_2( iubuf , mbuf , mg->nconv , mg->maskcolor ) ;
      break ;
    case 4:
      im_genmask4_2( iubuf , mbuf , mg->nconv , mg->maskcolor ) ;
      break ;
    default:
      im_genmaskN_2( iubuf , mbuf , mg->nconv , mg->ncomps , mg->maskcolor ) ;
      break ;
    }
  else
    switch ( mg->ncomps ) {
    case 1:
      im_genmask1_1( iubuf , mbuf , mg->nconv , mg->maskcolor ) ;
      break ;
    case 3:
      im_genmask3_1( iubuf , mbuf , mg->nconv , mg->maskcolor ) ;
      break ;
    case 4:
      im_genmask4_1( iubuf , mbuf , mg->nconv , mg->maskcolor ) ;
      break ;
    default:
      im_genmaskN_1( iubuf , mbuf , mg->nconv , mg->ncomps , mg->maskcolor ) ;
      break ;
    }

#if defined( ASSERT_BUILD )
  im_genmask_verify( mg , iubuf , mbuf ) ;
#endif
}

#if defined( ASSERT_BUILD )
static void im_genmaskalt_1( int32 *iubuf ,
                             uint8 *mbuf ,
                             int32 nconv ,
                             int32 ncomps ,
                             int32 *maskcolor )
{
  int32 *lmaskcolor , i ;
  uint8 bit, mask ;

  bit = ( 1 << 7 ) ;
  mask = 0 ;
  do {
    lmaskcolor = maskcolor ;
    for ( i = 0 ; i < ncomps ; ++i ) {
      int32 tmp = iubuf[ i ] ;
      if ( tmp != lmaskcolor[ 0 ] )
        break ;
      lmaskcolor += 1 ;
    }
    if ( i == ncomps )
      mask |= bit ;
    if (( bit >>= 1 ) == 0 ) {
      *mbuf++ = mask ;
      bit = ( 1 << 7 ) ;
      mask = 0 ;
    }
    iubuf += ncomps ;
    nconv -= 1 ;
  } while ( nconv > 0 ) ;
  if ( bit != ( 1 << 7 ))
    *mbuf = mask ;
}

static void im_genmaskalt_2( int32 *iubuf ,
                             uint8 *mbuf ,
                             int32 nconv ,
                             int32 ncomps ,
                             int32 *maskcolor )
{
  int32 *lmaskcolor , i ;
  uint8 bit, mask ;

  bit = ( 1 << 7 ) ;
  mask = 0 ;
  do {
    lmaskcolor = maskcolor ;
    for ( i = 0 ; i < ncomps ; ++i ) {
      int32 tmp = iubuf[ i ] ;
      if (((tmp - lmaskcolor[ 0 ])|(lmaskcolor[ 1 ] - tmp)) < 0 )
        break ;
      lmaskcolor += 2 ;
    }
    if ( i == ncomps )
      mask |= bit ;
    if (( bit >>= 1 ) == 0 ) {
      *mbuf++ = mask ;
      bit = ( 1 << 7 ) ;
      mask = 0 ;
    }
    iubuf += ncomps ;
    nconv -= 1 ;
  } while ( nconv > 0 ) ;
  if ( bit != ( 1 << 7 ))
    *mbuf = mask;
}

static void im_genmask_verify( MASKGENDATA *mg , int32 *iubuf , uint8 *mbuf )
{
  uint8 *mbuf1 , *mbuf2 ;
  int32 n , i ;

  n = ( mg->nconv + 7 ) >> 3 ; /* round up to nearest byte and div 8 */
  mbuf1 = mbuf ;

  mbuf2 = ( uint8 * ) mm_alloc( mm_pool_temp , n , MM_ALLOC_CLASS_GENERAL ) ;
  if ( mbuf2 == NULL ) {
    HQFAIL( "im_genmask_verify: out of memory" ) ;
    return ;
  }

  if ( mg->range )
    im_genmaskalt_2( iubuf , mbuf2 , mg->nconv , mg->ncomps , mg->maskcolor ) ;
  else
    im_genmaskalt_1( iubuf , mbuf2 , mg->nconv , mg->ncomps , mg->maskcolor ) ;

  for ( i = 0 ; i < n ; ++i )
    HQASSERT( mbuf1[i] == mbuf2[i] ,
              "im_genmask_verify: verification failed" ) ;

  mm_free( mm_pool_temp , ( mm_addr_t ) mbuf2 , n ) ;
}
#endif

/* -------------------------------------------------------------------------- */

/* Log stripped */
