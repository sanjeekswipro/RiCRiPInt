/** \file
 * \ingroup matrix
 *
 * $HopeName: SWv20!src:matrix.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS matrix manipulation functions
 */

#include "core.h"
#include "swoften.h"
#include "often.h"

#include "bitblts.h"
#include "matrix.h"
#include "constant.h"
#include "objects.h"
#include "display.h"
#include "graphics.h"
#include "mathfunc.h"

OMATRIX identity_matrix ;

#if defined( ASSERT_BUILD )

#include <stdarg.h>
#include <float.h>
#include "swcopyf.h"

Bool matrix_assert( OMATRIX *matrix )
{
  HQASSERT( ( matrix->opt & (~MATRIX_OPT_BOTH)) == 0 , "unknown matrix optimisation" ) ;

  if (( matrix->opt & MATRIX_OPT_0011 ) != 0 )
    HQASSERT( matrix->matrix[ 0 ][ 0 ] != 0.0 ||
              matrix->matrix[ 1 ][ 1 ] != 0.0 ,
              "matrix isn't really MATRIX_OPT_0011" ) ;

  if ( matrix->matrix[ 0 ][ 0 ] != 0.0 ||
       matrix->matrix[ 1 ][ 1 ] != 0.0 )
    HQASSERT( ( matrix->opt & MATRIX_OPT_0011 ) != 0 ,
              "matrix should be MATRIX_OPT_0011" ) ;

  if (( matrix->opt & MATRIX_OPT_1001 ) != 0 )
    HQASSERT( matrix->matrix[ 1 ][ 0 ] != 0.0 ||
              matrix->matrix[ 0 ][ 1 ] != 0.0 ,
              "matrix isn't really MATRIX_OPT_1001" ) ;

  if ( matrix->matrix[ 1 ][ 0 ] != 0.0 ||
       matrix->matrix[ 0 ][ 1 ] != 0.0 )
    HQASSERT( ( matrix->opt & MATRIX_OPT_1001 ) != 0 ,
              "matrix should be MATRIX_OPT_1001" ) ;

  return TRUE ;
}

static OMATRIX check_matrix = { 0 } ;

/* define macro FREXP that work round the Mac 68K library bug which
 * only does a ADDQ.W #$1 ,D0. This means that 0xffffffff "wraps" to
 * 0xffff0000 instead of 0x00000000. Note that 0xffff0000 is an invalid
 * return from frexp, since the smallest exponent that can be returned
 * (for a double) is DBL_MIN_EXP (-1021).
 *
 * While we're here, treat the magnitude of zero as smaller than the smallest
 * representable number, since otherwise we might treat zero anomalously.
 */
#define FREXP( _n, _s, _e ) MACRO_START \
  (_n) = frexp( (_s) , (_e) ) ;         \
  if ( *(_e) == 0xffff0000 )            \
    *(_e) = 0 ;                         \
  if ( (_n) == 0.0 )                    \
    *(_e) = DBL_MIN_EXP -1 ;            \
MACRO_END

/* #define MEASURE_MATRIX_ACCURACY_BOUNDS */
#ifdef MEASURE_MATRIX_ACCURACY_BOUNDS
static int32 matrix_accuracy_bounds[3][2] = {{100, 100}, {100,100}, {100,100}};
#endif

#define ABSOLUTE_PRECISION_BOUND (-20)

 /* This routine compares the "error quotient" |a - a'|/max(|a|, |a'|) of
  * corresponding positions in the matrix argument and the check array against
  * a bound defined by "min_accuracy".
  *
  * Where the error quotient  is between 2^-(min_accuracy -1) and
  * 2^-(min_accuracy), this may or may not report failure depending on the
  * mantissas of the FP representations.
  *
  * Note that if a or a' are zero, the "error quotient" is bound to be one
  * and so that check is bound to fail no matter how small the other is.
  * As insurance, we also perform a check on |a - a'| so that very small
  * differences in very small numbers are ignored whatever the relative
  * magnitudes.
  */
static void matrix_check_one( OMATRIX *matrix, int32 i, int32 j , int32 min_accuracy )
{
  int32 maxexp ;
  int32 exp1, exp2, exp3 ;
  SYSTEMVALUE s1, s2, s3 ;
  SYSTEMVALUE n1, n2, n3 ;

  char assert_message[ 256 ] ;

  s1 = matrix->matrix[ i ][ j ] ;
  s2 = check_matrix.matrix[ i ][ j ] ;

  s3 = s2 - s1 ;

  if ( s3 == 0.0 ) /* Numbers eq; everything ok. */
    return ;

  FREXP( n1, s1, &exp1 ) ;
  FREXP( n2, s2, &exp2 ) ;

  HQASSERT( n1 != n2 || exp1 != exp2,
    "Numbers are the same yet difference is non-zero" );

  FREXP( n3, s3, &exp3 ) ;

  if ( exp3 <= ABSOLUTE_PRECISION_BOUND - min_accuracy )
      return;

  maxexp = ( exp1 > exp2 ? exp1 : exp2 ) ;

#ifdef MEASURE_MATRIX_ACCURACY_BOUNDS
  if ( maxexp - exp3 < matrix_accuracy_bounds[i][j] ) {
    matrix_accuracy_bounds[i][j] = maxexp - exp3;
  }
#endif
  if ( maxexp - exp3 < min_accuracy ) {
    /* The magnitude of the error is large compared to that of the numbers */
    swcopyf(( uint8 * )assert_message,
      ( uint8 * )"matrix check fails [%d][%d]: %d - %d < %d",
      i, j, maxexp, exp3, min_accuracy ) ;
    HQFAIL( assert_message ) ;
    return ;
  }
}
 /* We tolerate greater inaccuracy in the translation column than the other
  * two columns. This is both because the matrix inversion routine is more
  * likely to introduce errors to these rows and because errors there are
  * less serious, since they mount additively rather than multiplicatively.
  */
#define MIN_MATRIX_ACCURACY_BITS_R (45)
#define MIN_MATRIX_ACCURACY_BITS_T (20)

static Bool matrix_check(OMATRIX *matrix)
{
  int32 i , j ;

  for ( i = 0 ; i < 2 ; ++i )
    for ( j = 0 ; j < 2 ; ++j )
      matrix_check_one( matrix, i, j , MIN_MATRIX_ACCURACY_BITS_R) ;
  matrix_check_one( matrix, 2, 0 , MIN_MATRIX_ACCURACY_BITS_T) ;
  matrix_check_one( matrix, 2, 1 , MIN_MATRIX_ACCURACY_BITS_T) ;

  return TRUE ;
}

static Bool matrix_check_mult(OMATRIX *m1, OMATRIX *m2)
{
  int32 i ;

  for ( i = 0 ; i < 3 ; ++i ) {
    check_matrix.matrix[ i ][ 0 ] = m1->matrix[ i ][ 0 ] * m2->matrix[ 0 ][ 0 ] +
                             m1->matrix[ i ][ 1 ] * m2->matrix[ 1 ][ 0 ] ;
    check_matrix.matrix[ i ][ 1 ] = m1->matrix[ i ][ 0 ] * m2->matrix[ 0 ][ 1 ] +
                             m1->matrix[ i ][ 1 ] * m2->matrix[ 1 ][ 1 ] ;
  }
  check_matrix.matrix[ 2 ][ 0 ] += m2->matrix[ 2 ][ 0 ] ;
  check_matrix.matrix[ 2 ][ 1 ] += m2->matrix[ 2 ][ 1 ] ;
  return TRUE ;
}

static Bool matrix_check_inverse(OMATRIX *matrixptr)
{
  SYSTEMVALUE determinant;

  determinant = matrixptr->matrix[ 0 ][ 0 ] * matrixptr->matrix[ 1 ][ 1 ] -
                matrixptr->matrix[ 0 ][ 1 ] * matrixptr->matrix[ 1 ][ 0 ] ;
  if ( determinant == 0.0 )
     return FALSE ;

  check_matrix.matrix[ 0 ][ 0 ] = matrixptr->matrix[ 1 ][ 1 ] / determinant ;
  check_matrix.matrix[ 1 ][ 0 ] = -matrixptr->matrix[ 1 ][ 0 ] / determinant ;
  check_matrix.matrix[ 2 ][ 0 ] =
    (( matrixptr->matrix[ 1 ][ 0 ] * matrixptr->matrix[ 2 ][ 1 ] ) -
     ( matrixptr->matrix[ 1 ][ 1 ] * matrixptr->matrix[ 2 ][ 0 ] )) /
    determinant ;

  check_matrix.matrix[ 0 ][ 1 ] = -matrixptr->matrix[ 0 ][ 1 ] / determinant ;
  check_matrix.matrix[ 1 ][ 1 ] = matrixptr->matrix[ 0 ][ 0 ] / determinant ;
  check_matrix.matrix[ 2 ][ 1 ] =
    (( matrixptr->matrix[ 0 ][ 1 ] * matrixptr->matrix[ 2 ][ 0 ] ) -
     ( matrixptr->matrix[ 0 ][ 0 ] * matrixptr->matrix[ 2 ][ 1 ] )) /
    determinant ;

  return TRUE;
}
#endif /* defined( ASSERT_BUILD ) */

/* ----------------------------------------------------------------------------
Translate 'matrix' by the passed amount, storing into 'result' (which may
point to the same instance as 'matrix').
*/
void matrix_translate(OMATRIX *matrix,
                      SYSTEMVALUE x, SYSTEMVALUE y,
                      OMATRIX *result)
{
  SYSTEMVALUE tx, ty;

  HQASSERT(matrix != NULL && result != NULL,
           "matrix_translate - parameters cannot be NULL");

  /* If necessary, copy the original into result, which we'll then apply the
  translation to. */
  if (matrix != result)
    matrix_copy(result, matrix);

  MATRIX_TRANSFORM_DXY(x, y, tx, ty, result);
  result->matrix[ 2 ][ 0 ] += tx;
  result->matrix[ 2 ][ 1 ] += ty;
}

void matrix_scale(OMATRIX *matrix,
                  SYSTEMVALUE sx, SYSTEMVALUE sy,
                  OMATRIX *result)
{
  if ( matrix != result )
    matrix_copy(result, matrix) ;

  if (( result->opt & MATRIX_OPT_0011 ) != 0 ) {
    SYSTEMVALUE m_00 = result->matrix[ 0 ][ 0 ] * sx ;
    SYSTEMVALUE m_11 = result->matrix[ 1 ][ 1 ] * sy ;
    result->matrix[ 0 ][ 0 ] = m_00 ;
    result->matrix[ 1 ][ 1 ] = m_11 ;
    if ( m_00 == 0.0 && m_11 == 0.0 )
      result->opt &= (~MATRIX_OPT_0011) ;
  }
  if (( result->opt & MATRIX_OPT_1001 ) != 0 ) {
    SYSTEMVALUE m_10 = result->matrix[ 1 ][ 0 ] * sy ;
    SYSTEMVALUE m_01 = result->matrix[ 0 ][ 1 ] * sx ;
    result->matrix[ 1 ][ 0 ] = m_10 ;
    result->matrix[ 0 ][ 1 ] = m_01 ;
    if ( m_10 == 0.0 && m_01 == 0.0 )
      result->opt &= (~MATRIX_OPT_1001) ;
  }

  HQASSERT(matrix_assert(result), "matrix not a proper optimised matrix") ;
}

/* See header for doc. */
void matrix_set_scale(OMATRIX* matrix, SYSTEMVALUE sx, SYSTEMVALUE sy)
{
  matrix->matrix[0][0] = sx;
  matrix->matrix[1][0] = 0;
  matrix->matrix[0][1] = 0;
  matrix->matrix[1][1] = sy;
  matrix->matrix[2][0] = 0;
  matrix->matrix[2][1] = 0;
  MATRIX_SET_OPT_0011(matrix);
  HQASSERT(matrix_assert(matrix), "matrix not a proper optimised matrix") ;
}

/* See header for doc. */
void matrix_set_rotation(OMATRIX* matrix, SYSTEMVALUE angleInDegrees)
{
  SYSTEMVALUE cs, sn;

  NORMALISE_ANGLE(angleInDegrees);
  SINCOS_ANGLE(angleInDegrees, sn, cs);

  matrix->matrix[0][0] = cs;
  matrix->matrix[1][0] = -sn;
  matrix->matrix[0][1] = sn;
  matrix->matrix[1][1] = cs;
  matrix->matrix[2][0] = 0;
  matrix->matrix[2][1] = 0;
  MATRIX_SET_OPT_BOTH(matrix) ;
  HQASSERT(matrix_assert(matrix), "matrix not a proper optimised matrix") ;
}

/* ----------------------------------------------------------------------------
   function:            matrix_mult(..)    author:              Andrew Cave
   creation date:       05-Oct-1987        last modification:   ##-###-####
   arguments:           m1 , m2 , result .
   description:

      This is a general 3x3 matrix multiplication routine. result=m1*m2.

---------------------------------------------------------------------------- */
void matrix_mult( OMATRIX *m1,
                  OMATRIX *m2,
                  OMATRIX *result )
{
#ifdef TEST_ME
  /* Below code is pretty hairy, so we assert it's ok on bootup. */
  static Bool checkme = TRUE ;
  if ( checkme ) {
    int32 srcdst ;
    int32 m1_00, m1_10, m1_20, m1_01, m1_11, m1_21 ;
    int32 m2_00, m2_10, m2_20, m2_01, m2_11, m2_21 ;
    OMATRIX testm1, testm2, testm3 ;
    checkme = FALSE ;
#define INCCHECK  128
#define MINCHECK -256
#define MAXCHECK  256
    for ( m1_00 = MINCHECK ; m1_00 <= MAXCHECK ; m1_00 += INCCHECK )
    for ( m1_10 = MINCHECK ; m1_10 <= MAXCHECK ; m1_10 += INCCHECK )
    for ( m1_20 = MINCHECK ; m1_20 <= MAXCHECK ; m1_20 += INCCHECK )
    for ( m1_01 = MINCHECK ; m1_01 <= MAXCHECK ; m1_01 += INCCHECK )
    for ( m1_11 = MINCHECK ; m1_11 <= MAXCHECK ; m1_11 += INCCHECK )
    for ( m1_21 = MINCHECK ; m1_21 <= MAXCHECK ; m1_21 += INCCHECK )
    for ( m2_00 = MINCHECK ; m2_00 <= MAXCHECK ; m2_00 += INCCHECK )
    for ( m2_10 = MINCHECK ; m2_10 <= MAXCHECK ; m2_10 += INCCHECK )
    for ( m2_20 = MINCHECK ; m2_20 <= MAXCHECK ; m2_20 += INCCHECK )
    for ( m2_01 = MINCHECK ; m2_01 <= MAXCHECK ; m2_01 += INCCHECK )
    for ( m2_11 = MINCHECK ; m2_11 <= MAXCHECK ; m2_11 += INCCHECK )
    for ( m2_21 = MINCHECK ; m2_21 <= MAXCHECK ; m2_21 += INCCHECK )
    for ( srcdst = 1 ; srcdst <= 3 ; ++srcdst ) {
      testm1.matrix[0][0] = ( SYSTEMVALUE )m1_00 ;
      testm1.matrix[1][0] = ( SYSTEMVALUE )m1_10 ;
      testm1.matrix[2][0] = ( SYSTEMVALUE )m1_20 ;
      testm1.matrix[0][1] = ( SYSTEMVALUE )m1_01 ;
      testm1.matrix[1][1] = ( SYSTEMVALUE )m1_11 ;
      testm1.matrix[2][1] = ( SYSTEMVALUE )m1_21 ;
      MATRIX_SET_OPT_BOTH( & testm1 ) ;
      testm2.matrix[0][0] = ( SYSTEMVALUE )m2_00 ;
      testm2.matrix[1][0] = ( SYSTEMVALUE )m2_10 ;
      testm2.matrix[2][0] = ( SYSTEMVALUE )m2_20 ;
      testm2.matrix[0][1] = ( SYSTEMVALUE )m2_01 ;
      testm2.matrix[1][1] = ( SYSTEMVALUE )m2_11 ;
      testm2.matrix[2][1] = ( SYSTEMVALUE )m2_21 ;
      MATRIX_SET_OPT_BOTH( & testm2 ) ;
      if ( srcdst == 1 )
        matrix_mult( & testm1, & testm2, & testm1 ) ;
      if ( srcdst == 2 )
        matrix_mult( & testm1, & testm2, & testm2 ) ;
      if ( srcdst == 3 )
        matrix_mult( & testm1, & testm2, & testm3 ) ;
    }
  }
#endif

  HQASSERT( matrix_assert( m1 ) , "m1 not a proper optimised matrix" ) ;
  HQASSERT( matrix_assert( m2 ) , "m2 not a proper optimised matrix" ) ;
  HQASSERT( matrix_check_mult( m1 , m2 ) , "setup for mult check" ) ;

  if ( m1->opt == 0 ) {
    SYSTEMVALUE m2_20 = m2->matrix[ 2 ][ 0 ] ,
                m2_21 = m2->matrix[ 2 ][ 1 ] ;
    if ( m2->opt == 0 ) {
      result->matrix[ 0 ][ 0 ] = 0.0 ;
      result->matrix[ 1 ][ 0 ] = 0.0 ;
      result->matrix[ 2 ][ 0 ] = m2_20 ;
      result->matrix[ 0 ][ 1 ] = 0.0 ;
      result->matrix[ 1 ][ 1 ] = 0.0 ;
      result->matrix[ 2 ][ 1 ] = m2_21 ;
      result->opt = 0 ;
    }
    else {
      SYSTEMVALUE m1_20 = m1->matrix[ 2 ][ 0 ] ,
                  m1_21 = m1->matrix[ 2 ][ 1 ] ;
      if ( m2->opt == MATRIX_OPT_0011 ) {
        SYSTEMVALUE m2_00 = m2->matrix[ 0 ][ 0 ] ,
                    m2_11 = m2->matrix[ 1 ][ 1 ] ;
        result->matrix[ 0 ][ 0 ] = 0.0 ;
        result->matrix[ 1 ][ 0 ] = 0.0 ;
        result->matrix[ 2 ][ 0 ] = m1_20 * m2_00 + m2_20 ;
        result->matrix[ 0 ][ 1 ] = 0.0 ;
        result->matrix[ 1 ][ 1 ] = 0.0 ;
        result->matrix[ 2 ][ 1 ] = m1_21 * m2_11 + m2_21 ;
        result->opt = 0 ;
      }
      else if ( m2->opt == MATRIX_OPT_1001 ) {
        SYSTEMVALUE m2_10 = m2->matrix[ 1 ][ 0 ] ,
                    m2_01 = m2->matrix[ 0 ][ 1 ] ;
        result->matrix[ 0 ][ 0 ] = 0.0 ;
        result->matrix[ 1 ][ 0 ] = 0.0 ;
        result->matrix[ 2 ][ 0 ] = m1_21 * m2_10 + m2_20 ;
        result->matrix[ 0 ][ 1 ] = 0.0 ;
        result->matrix[ 1 ][ 1 ] = 0.0 ;
        result->matrix[ 2 ][ 1 ] = m1_20 * m2_01 + m2_21 ;
        result->opt = 0 ;
      }
      else {
        SYSTEMVALUE m2_00 = m2->matrix[ 0 ][ 0 ] ,
                    m2_11 = m2->matrix[ 1 ][ 1 ] ,
                    m2_10 = m2->matrix[ 1 ][ 0 ] ,
                    m2_01 = m2->matrix[ 0 ][ 1 ] ;
        HQASSERT( m2->opt == MATRIX_OPT_BOTH, "unknown m2 opt flags" ) ;
        result->matrix[ 0 ][ 0 ] = 0.0 ;
        result->matrix[ 1 ][ 0 ] = 0.0 ;
        result->matrix[ 2 ][ 0 ] = m1_20 * m2_00 + m1_21 * m2_10 + m2_20 ;
        result->matrix[ 0 ][ 1 ] = 0.0 ;
        result->matrix[ 1 ][ 1 ] = 0.0 ;
        result->matrix[ 2 ][ 1 ] = m1_20 * m2_01 + m1_21 * m2_11 + m2_21 ;
        result->opt = 0 ;
      }
    }
  }
  else if ( m1->opt == MATRIX_OPT_0011 ) {
    SYSTEMVALUE m2_20 = m2->matrix[ 2 ][ 0 ] ,
                m2_21 = m2->matrix[ 2 ][ 1 ] ;
    if ( m2->opt == 0 ) {
      result->matrix[ 0 ][ 0 ] = 0.0 ;
      result->matrix[ 1 ][ 0 ] = 0.0 ;
      result->matrix[ 2 ][ 0 ] = m2_20 ;
      result->matrix[ 0 ][ 1 ] = 0.0 ;
      result->matrix[ 1 ][ 1 ] = 0.0 ;
      result->matrix[ 2 ][ 1 ] = m2_21 ;
      result->opt = 0 ;
    }
    else {
      SYSTEMVALUE m1_00 = m1->matrix[ 0 ][ 0 ] ,
                  m1_11 = m1->matrix[ 1 ][ 1 ] ,
                  m1_20 = m1->matrix[ 2 ][ 0 ] ,
                  m1_21 = m1->matrix[ 2 ][ 1 ] ;
      if ( m2->opt == MATRIX_OPT_0011 ) {
        SYSTEMVALUE m2_00 = m2->matrix[ 0 ][ 0 ] ,
                    m2_11 = m2->matrix[ 1 ][ 1 ] ;
        result->matrix[ 0 ][ 0 ] = m1_00 * m2_00 ;
        result->matrix[ 1 ][ 0 ] = 0.0 ;
        result->matrix[ 2 ][ 0 ] = m1_20 * m2_00 + m2_20 ;
        result->matrix[ 0 ][ 1 ] = 0.0 ;
        result->matrix[ 1 ][ 1 ] = m1_11 * m2_11 ;
        result->matrix[ 2 ][ 1 ] = m1_21 * m2_11 + m2_21 ;
        MATRIX_SET_OPT_0011( result ) ;
      }
      else if ( m2->opt == MATRIX_OPT_1001 ) {
        SYSTEMVALUE m2_10 = m2->matrix[ 1 ][ 0 ] ,
                    m2_01 = m2->matrix[ 0 ][ 1 ] ;
        result->matrix[ 0 ][ 0 ] = 0.0 ;
        result->matrix[ 1 ][ 0 ] = m1_11 * m2_10 ;
        result->matrix[ 2 ][ 0 ] = m1_21 * m2_10 + m2_20 ;
        result->matrix[ 0 ][ 1 ] = m1_00 * m2_01 ;
        result->matrix[ 1 ][ 1 ] = 0.0 ;
        result->matrix[ 2 ][ 1 ] = m1_20 * m2_01 + m2_21 ;
        MATRIX_SET_OPT_1001( result ) ;
      }
      else {
        SYSTEMVALUE m2_00 = m2->matrix[ 0 ][ 0 ] ,
                    m2_11 = m2->matrix[ 1 ][ 1 ] ,
                    m2_10 = m2->matrix[ 1 ][ 0 ] ,
                    m2_01 = m2->matrix[ 0 ][ 1 ] ;
        HQASSERT( m2->opt == MATRIX_OPT_BOTH, "unknown m2 opt flags" ) ;
        result->matrix[ 0 ][ 0 ] = m1_00 * m2_00 ;
        result->matrix[ 1 ][ 0 ] = m1_11 * m2_10 ;
        result->matrix[ 2 ][ 0 ] = m1_20 * m2_00 + m1_21 * m2_10 + m2_20 ;
        result->matrix[ 0 ][ 1 ] = m1_00 * m2_01 ;
        result->matrix[ 1 ][ 1 ] = m1_11 * m2_11 ;
        result->matrix[ 2 ][ 1 ] = m1_20 * m2_01 + m1_21 * m2_11 + m2_21 ;
        MATRIX_SET_OPT_BOTH( result ) ;
      }
    }
  }
  else if ( m1->opt == MATRIX_OPT_1001 ) {
    SYSTEMVALUE m2_20 = m2->matrix[ 2 ][ 0 ] ,
                m2_21 = m2->matrix[ 2 ][ 1 ] ;
    if ( m2->opt == 0 ) {
      result->matrix[ 0 ][ 0 ] = 0.0 ;
      result->matrix[ 1 ][ 0 ] = 0.0 ;
      result->matrix[ 2 ][ 0 ] = m2_20 ;
      result->matrix[ 0 ][ 1 ] = 0.0 ;
      result->matrix[ 1 ][ 1 ] = 0.0 ;
      result->matrix[ 2 ][ 1 ] = m2_21 ;
      result->opt = 0 ;
    }
    else {
      SYSTEMVALUE m1_10 = m1->matrix[ 1 ][ 0 ] ,
                  m1_01 = m1->matrix[ 0 ][ 1 ] ,
                  m1_20 = m1->matrix[ 2 ][ 0 ] ,
                  m1_21 = m1->matrix[ 2 ][ 1 ] ;
      if ( m2->opt == MATRIX_OPT_0011 ) {
        SYSTEMVALUE m2_00 = m2->matrix[ 0 ][ 0 ] ,
                    m2_11 = m2->matrix[ 1 ][ 1 ] ;
        result->matrix[ 0 ][ 0 ] = 0.0 ;
        result->matrix[ 1 ][ 0 ] = m1_10 * m2_00 ;
        result->matrix[ 2 ][ 0 ] = m1_20 * m2_00 + m2_20 ;
        result->matrix[ 0 ][ 1 ] = m1_01 * m2_11 ;
        result->matrix[ 1 ][ 1 ] = 0.0 ;
        result->matrix[ 2 ][ 1 ] = m1_21 * m2_11 + m2_21 ;
        MATRIX_SET_OPT_1001( result ) ;
      }
      else if ( m2->opt == MATRIX_OPT_1001 ) {
        SYSTEMVALUE m2_10 = m2->matrix[ 1 ][ 0 ] ,
                    m2_01 = m2->matrix[ 0 ][ 1 ] ;
        result->matrix[ 0 ][ 0 ] = m1_01 * m2_10 ;
        result->matrix[ 1 ][ 0 ] = 0.0 ;
        result->matrix[ 2 ][ 0 ] = m1_21 * m2_10 + m2_20 ;
        result->matrix[ 0 ][ 1 ] = 0.0 ;
        result->matrix[ 1 ][ 1 ] = m1_10 * m2_01 ;
        result->matrix[ 2 ][ 1 ] = m1_20 * m2_01 + m2_21 ;
        MATRIX_SET_OPT_0011( result ) ;
      }
      else {
        SYSTEMVALUE m2_00 = m2->matrix[ 0 ][ 0 ] ,
                    m2_11 = m2->matrix[ 1 ][ 1 ] ,
                    m2_10 = m2->matrix[ 1 ][ 0 ] ,
                    m2_01 = m2->matrix[ 0 ][ 1 ] ;
        HQASSERT( m2->opt == MATRIX_OPT_BOTH, "unknown m2 opt flags" ) ;
        result->matrix[ 0 ][ 0 ] = m1_01 * m2_10 ;
        result->matrix[ 1 ][ 0 ] = m1_10 * m2_00 ;
        result->matrix[ 2 ][ 0 ] = m1_20 * m2_00 + m1_21 * m2_10 + m2_20 ;
        result->matrix[ 0 ][ 1 ] = m1_01 * m2_11 ;
        result->matrix[ 1 ][ 1 ] = m1_10 * m2_01 ;
        result->matrix[ 2 ][ 1 ] = m1_20 * m2_01 + m1_21 * m2_11 + m2_21 ;
        MATRIX_SET_OPT_BOTH( result ) ;
      }
    }
  }
  else {
    SYSTEMVALUE m2_20 = m2->matrix[ 2 ][ 0 ] ,
                m2_21 = m2->matrix[ 2 ][ 1 ] ;
    HQASSERT( m1->opt == MATRIX_OPT_BOTH, "unknown m1 opt flags" ) ;
    if ( m2->opt == 0 ) {
      result->matrix[ 0 ][ 0 ] = 0.0 ;
      result->matrix[ 1 ][ 0 ] = 0.0 ;
      result->matrix[ 2 ][ 0 ] = m2_20 ;
      result->matrix[ 0 ][ 1 ] = 0.0 ;
      result->matrix[ 1 ][ 1 ] = 0.0 ;
      result->matrix[ 2 ][ 1 ] = m2_21 ;
      result->opt = 0 ;
    }
    else {
      SYSTEMVALUE m1_00 = m1->matrix[ 0 ][ 0 ] ,
                  m1_11 = m1->matrix[ 1 ][ 1 ] ,
                  m1_10 = m1->matrix[ 1 ][ 0 ] ,
                  m1_01 = m1->matrix[ 0 ][ 1 ] ,
                  m1_20 = m1->matrix[ 2 ][ 0 ] ,
                  m1_21 = m1->matrix[ 2 ][ 1 ] ;
      if ( m2->opt == MATRIX_OPT_0011 ) {
        SYSTEMVALUE m2_00 = m2->matrix[ 0 ][ 0 ] ,
                    m2_11 = m2->matrix[ 1 ][ 1 ] ;
        result->matrix[ 0 ][ 0 ] = m1_00 * m2_00 ;
        result->matrix[ 1 ][ 0 ] = m1_10 * m2_00 ;
        result->matrix[ 2 ][ 0 ] = m1_20 * m2_00 + m2_20 ;
        result->matrix[ 0 ][ 1 ] = m1_01 * m2_11 ;
        result->matrix[ 1 ][ 1 ] = m1_11 * m2_11 ;
        result->matrix[ 2 ][ 1 ] = m1_21 * m2_11 + m2_21 ;
        MATRIX_SET_OPT_BOTH( result ) ;
      }
      else if ( m2->opt == MATRIX_OPT_1001 ) {
        SYSTEMVALUE m2_10 = m2->matrix[ 1 ][ 0 ] ,
                    m2_01 = m2->matrix[ 0 ][ 1 ] ;
        result->matrix[ 0 ][ 0 ] = m1_01 * m2_10 ;
        result->matrix[ 1 ][ 0 ] = m1_11 * m2_10 ;
        result->matrix[ 2 ][ 0 ] = m1_21 * m2_10 + m2_20 ;
        result->matrix[ 0 ][ 1 ] = m1_00 * m2_01 ;
        result->matrix[ 1 ][ 1 ] = m1_10 * m2_01 ;
        result->matrix[ 2 ][ 1 ] = m1_20 * m2_01 + m2_21 ;
        MATRIX_SET_OPT_BOTH( result ) ;
      }
      else {
        SYSTEMVALUE m2_00 = m2->matrix[ 0 ][ 0 ] ,
                    m2_11 = m2->matrix[ 1 ][ 1 ] ,
                    m2_10 = m2->matrix[ 1 ][ 0 ] ,
                    m2_01 = m2->matrix[ 0 ][ 1 ] ;
        HQASSERT( m2->opt == MATRIX_OPT_BOTH, "unknown m2 opt flags" ) ;
        result->matrix[ 0 ][ 0 ] = m1_00 * m2_00 + m1_01 * m2_10 ;
        result->matrix[ 1 ][ 0 ] = m1_10 * m2_00 + m1_11 * m2_10 ;
        result->matrix[ 2 ][ 0 ] = m1_20 * m2_00 + m1_21 * m2_10 + m2_20 ;
        result->matrix[ 0 ][ 1 ] = m1_00 * m2_01 + m1_01 * m2_11 ;
        result->matrix[ 1 ][ 1 ] = m1_10 * m2_01 + m1_11 * m2_11 ;
        result->matrix[ 2 ][ 1 ] = m1_20 * m2_01 + m1_21 * m2_11 + m2_21 ;
        MATRIX_SET_OPT_BOTH( result ) ;
      }
    }
  }
  HQASSERT( matrix_assert( result ) , "result not a proper optimised matrix" ) ;
  HQASSERT( matrix_check( result ) , "result of mult fails" ) ;
}

/* Utility function */
void matrix_copy( OMATRIX *result,
                  OMATRIX *matrix )
{
  MATRIX_COPY( result, matrix ) ;
}

/* ----------------------------------------------------------------------------
   function:            matrix_inverse(..) author:              Andrew Cave
   creation date:       05-Oct-1987        last modification:   ##-###-####
   arguments:           m.
   description:

   This procedure inverts  the  matrix m, leaving  the  result in m.  If the
   matrix is singular, then the procedure returns FALSE.

---------------------------------------------------------------------------- */
Bool matrix_inverse(OMATRIX *matrix, OMATRIX *result)
{
  SYSTEMVALUE m_20 = matrix->matrix[ 2 ][ 0 ] ,
              m_21 = matrix->matrix[ 2 ][ 1 ] ;
  HQASSERT( matrix_assert( matrix ) , "matrix not a proper optimised matrix" ) ;
  if ( matrix->opt == 0 ) {
    /* All components zero, so invalid inverse. */
    return FALSE ;
  }
  else if ( matrix->opt == MATRIX_OPT_0011 ) {
    SYSTEMVALUE m_00 = matrix->matrix[ 0 ][ 0 ] ,
                m_11 = matrix->matrix[ 1 ][ 1 ] ;
    if ( m_00 == 0.0 || m_11 == 0.0 )
       return FALSE ;
    HQASSERT( matrix_check_inverse( matrix ) , "setup for inverse check" ) ;
    m_00 = 1.0 / m_00 ;
    result->matrix[ 0 ][ 0 ] = m_00 ;
    result->matrix[ 1 ][ 0 ] = 0.0 ;
    result->matrix[ 2 ][ 0 ] = -m_00 * m_20 ;
    m_11 = 1.0 / m_11 ;
    result->matrix[ 0 ][ 1 ] = 0.0 ;
    result->matrix[ 1 ][ 1 ] = m_11 ;
    result->matrix[ 2 ][ 1 ] = -m_11 * m_21 ;
    result->opt = MATRIX_OPT_0011 ;
  }
  else if ( matrix->opt == MATRIX_OPT_1001 ) {
    SYSTEMVALUE m_10 = matrix->matrix[ 1 ][ 0 ] ,
                m_01 = matrix->matrix[ 0 ][ 1 ] ;
    if ( m_10 == 0.0 || m_01 == 0.0 )
       return FALSE ;
    HQASSERT( matrix_check_inverse( matrix ) , "setup for inverse check" ) ;
    m_01 = 1.0 / m_01 ;
    result->matrix[ 0 ][ 0 ] = 0.0 ;
    result->matrix[ 1 ][ 0 ] = m_01 ;
    result->matrix[ 2 ][ 0 ] = -m_01 * m_21 ;
    m_10 = 1.0 / m_10 ;
    result->matrix[ 0 ][ 1 ] = m_10 ;
    result->matrix[ 1 ][ 1 ] = 0.0 ;
    result->matrix[ 2 ][ 1 ] = -m_10 * m_20 ;
    result->opt = MATRIX_OPT_1001 ;
  }
  else {
    SYSTEMVALUE det ;
    SYSTEMVALUE m_00 = matrix->matrix[ 0 ][ 0 ] ,
                m_11 = matrix->matrix[ 1 ][ 1 ] ,
                m_10 = matrix->matrix[ 1 ][ 0 ] ,
                m_01 = matrix->matrix[ 0 ][ 1 ] ;
    HQASSERT( matrix->opt == MATRIX_OPT_BOTH, "unknown matrix opt" ) ;
    det = ( m_00 * m_11 - m_01 * m_10 ) ;
    if ( det == 0.0 )
       return FALSE ;
    HQASSERT( matrix_check_inverse( matrix ) , "setup for inverse check" ) ;
    det = 1.0 / det ;
    m_00 *= det ;
    m_11 *= det ;
    m_10 *= det ;
    m_01 *= det ;
    result->matrix[ 0 ][ 0 ] =  m_11 ;
    result->matrix[ 1 ][ 0 ] = -m_10 ;
    result->matrix[ 2 ][ 0 ] =  m_10 * m_21 - m_11 * m_20 ;
    result->matrix[ 0 ][ 1 ] = -m_01 ;
    result->matrix[ 1 ][ 1 ] =  m_00 ;
    result->matrix[ 2 ][ 1 ] =  m_01 * m_20 - m_00 * m_21 ;
    result->opt = MATRIX_OPT_BOTH ;
  }
  HQASSERT( matrix_assert( result ) , "result not a proper optimised matrix" ) ;
  HQASSERT( matrix_check( result ) , "result of inverse fails" ) ;
  return TRUE;
}

void matrix_clean( OMATRIX *matrix )
{
  int32 i , j ;

  HQASSERT( matrix_assert( matrix ) , "matrix not a proper optimised matrix" ) ;

  /* Check if the offsets are close to n.0, n.5,... If they are, force to be exact. */
  for ( i = 0 ; i < 2 ; ++i )
    for ( j = 0 ; j < 2 ; ++j )
      if (((( matrix->opt & MATRIX_OPT_0011 ) != 0 ) && i == j ) ||
          ((( matrix->opt & MATRIX_OPT_1001 ) != 0 ) && i != j ))
        MATRIX_CLEAN_EPSILON_R( matrix->matrix[ i ][ j ], 1, 2 ) ;

  /* for ( i = 2 ; i < 3 ; ++i ) */
    for ( j = 0 ; j < 2 ; ++j )
      MATRIX_CLEAN_EPSILON_T( matrix->matrix[ 2 ][ j ], 1, 2 ) ;

  MATRIX_SET_OPT_BOTH( matrix ) ;

  HQASSERT( matrix_assert( matrix ) , "result not a proper optimised matrix" ) ;
}

void matrix_snap( OMATRIX *matrix , int32 snapvalue )
{
  int32 i , j ;
  SYSTEMVALUE dsnapvalue = 1.0 / ( SYSTEMVALUE )snapvalue ;

  HQASSERT( matrix_assert( matrix ) , "matrix not a proper optimised matrix" ) ;

  /* Check if the offsets are close to n.0, n.5,... If they are, force to be exact. */
  for ( i = 0 ; i < 2 ; ++i )
    for ( j = 0 ; j < 2 ; ++j )
      if (((( matrix->opt & MATRIX_OPT_0011 ) != 0 ) && i == j ) ||
          ((( matrix->opt & MATRIX_OPT_1001 ) != 0 ) && i != j ))
        MATRIX_CLEAN_EPSILON( matrix->matrix[ i ][ j ], snapvalue, snapvalue, dsnapvalue ) ;

  /* for ( i = 2 ; i < 3 ; ++i ) */
    for ( j = 0 ; j < 2 ; ++j )
      MATRIX_CLEAN_EPSILON( matrix->matrix[ 2 ][ j ], snapvalue, snapvalue, dsnapvalue ) ;

  MATRIX_SET_OPT_BOTH( matrix ) ;

  HQASSERT( matrix_assert( matrix ) , "result not a proper optimised matrix" ) ;
}

/* Not really a standard SW matrix... */

Bool matrix_inverse_3x3( register SYSTEMVALUE m [9],
                         register SYSTEMVALUE inverse [9] )
{
  SYSTEMVALUE determinant;

#define AA(m, x, y)  (m[x * 3 + y])
/*
  Calculate the 'determinant' of the matrix. If this is zero, then
  the matrix is singular  and  has no  inverse, so return FALSE.
*/
  {
    SYSTEMVALUE d1 = AA(m, 0, 0) * (AA(m, 1, 1) * AA(m, 2, 2) - AA(m, 1, 2) * AA(m, 2, 1));
    SYSTEMVALUE d2 = AA(m, 1, 0) * (AA(m, 0, 1) * AA(m, 2, 2) - AA(m, 0, 2) * AA(m, 2, 1));
    SYSTEMVALUE d3 = AA(m, 2, 0) * (AA(m, 0, 1) * AA(m, 1, 2) - AA(m, 0, 2) * AA(m, 1, 1));
    determinant = d1 - d2 + d3;
  }

  if ( fabs (determinant) < EPSILON )
    return FALSE ;

  AA(inverse, 0, 0) =   (AA(m, 1, 1) * AA(m, 2, 2) - AA(m, 1, 2) * AA(m, 2, 1)) / determinant;
  AA(inverse, 1, 0) = - (AA(m, 1, 0) * AA(m, 2, 2) - AA(m, 1, 2) * AA(m, 2, 0)) / determinant;
  AA(inverse, 2, 0) =   (AA(m, 1, 0) * AA(m, 2, 1) - AA(m, 1, 1) * AA(m, 2, 0)) / determinant;
  AA(inverse, 0, 1) = - (AA(m, 0, 1) * AA(m, 2, 2) - AA(m, 0, 2) * AA(m, 2, 1)) / determinant;
  AA(inverse, 1, 1) =   (AA(m, 0, 0) * AA(m, 2, 2) - AA(m, 0, 2) * AA(m, 2, 0)) / determinant;
  AA(inverse, 2, 1) = - (AA(m, 0, 0) * AA(m, 2, 1) - AA(m, 0, 1) * AA(m, 2, 0)) / determinant;
  AA(inverse, 0, 2) =   (AA(m, 0, 1) * AA(m, 1, 2) - AA(m, 0, 2) * AA(m, 1, 1)) / determinant;
  AA(inverse, 1, 2) = - (AA(m, 0, 0) * AA(m, 1, 2) - AA(m, 0, 2) * AA(m, 1, 0)) / determinant;
  AA(inverse, 2, 2) =   (AA(m, 0, 0) * AA(m, 1, 1) - AA(m, 0, 1) * AA(m, 1, 0)) / determinant;

  return TRUE;
}

/* See header for doc. */
Bool matrix_equal(OMATRIX* a, OMATRIX* b)
{
  uint32 i;

  for (i = 0; i < 3; i ++) {
    if (a->matrix[i][0] != b->matrix[i][0] || a->matrix[i][1] != b->matrix[i][1])
      return FALSE;
  }
  return TRUE;
}

void init_C_globals_matrix(void)
{
  OMATRIX idinit = { 1.0 , 0.0 , 0.0 , 1.0 , 0.0 , 0.0 , MATRIX_OPT_0011 } ;

  identity_matrix = idinit ;
#if defined( ASSERT_BUILD )
  {
    OMATRIX chkinit = { 0 } ;
    check_matrix = chkinit ;
  }
#endif
}

/* Log stripped */
