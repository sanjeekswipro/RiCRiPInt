/** \file
 * \ingroup matrix
 *
 * $HopeName: SWv20!export:matrix.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS matrix manipulation API
 */

#ifndef __MATRIX_H__
#define __MATRIX_H__

/** \defgroup matrix Graphic State matrix manipulation
    \ingroup gstate */
/** \{ */

#include "swvalues.h"

#define EPSILON_ACC_R	0.0000001	/* Rotation   accuracy; 1 in 10 million.   */
#define EPSILON_ACC_T	0.00001		/* Transation accuracy; 1 in 100 thousand. */

#define EPSILON_CLEAN_R	0.000001	/* Rotation   cleanliness; 1 in a million.   */
#define EPSILON_CLEAN_T	0.001		/* Transation cleanliness; 1 in a thousand. */

#define MATRIX_CLEAN_EPSILON(_mval, _w1, _w2, _epsilon ) MACRO_START \
  int32 _k_ ; \
  SYSTEMVALUE _mval_ = (_mval) ; \
  for ( _k_ = (_w1) ; _k_ <= (_w2) ; ++_k_ ) { \
    SYSTEMVALUE _mint_ = _mval_ ; \
    if ( _k_ > 1 ) \
      _mint_ *= ( SYSTEMVALUE )(_k_) ; \
    _mint_ = (_mint_ >= 0.0) ? floor(_mint_ + 0.5) : ceil(_mint_ - 0.5); \
    if ( _k_ > 1 ) \
      _mint_ /= ( SYSTEMVALUE )(_k_) ; \
    if ( fabs( _mval_ - _mint_ ) < (_epsilon) ) { \
      (_mval) = _mint_ ; \
      break ; \
    } \
  } \
MACRO_END

#define MATRIX_CLEAN_EPSILON_R(_mval, _w1, _w2 ) \
  MATRIX_CLEAN_EPSILON( _mval, _w1, _w2, EPSILON_CLEAN_R )

#define MATRIX_CLEAN_EPSILON_T(_mval, _w1, _w2 ) \
  MATRIX_CLEAN_EPSILON( _mval, _w1, _w2, EPSILON_CLEAN_T )

#define MATRIX_OPT_0011	0x01 /**< Matrix has a non-zero major component. */
#define MATRIX_OPT_1001	0x02 /**< Matrix has a non-zero minor component. */
/** Matrix has non-zero major and minor components. If the matrix optimisation
    is *not* MATRIX_OPT_BOTH, then it is either an orthogonal transformation,
    or is a degenerate (all zero) matrix. */
#define MATRIX_OPT_BOTH	( MATRIX_OPT_0011 | MATRIX_OPT_1001 )

#define MATRIX_SET_OPT_0011( _matrix ) MACRO_START				\
  (_matrix)->opt = 0 ;								\
  if ( (_matrix)->matrix[ 0 ][ 0 ] != 0.0 ||					\
       (_matrix)->matrix[ 1 ][ 1 ] != 0.0 )					\
    (_matrix)->opt |= MATRIX_OPT_0011 ;						\
MACRO_END

#define MATRIX_SET_OPT_1001( _matrix ) MACRO_START				\
  (_matrix)->opt = 0 ;								\
  if ( (_matrix)->matrix[ 1 ][ 0 ] != 0.0 ||					\
       (_matrix)->matrix[ 0 ][ 1 ] != 0.0 )					\
    (_matrix)->opt |= MATRIX_OPT_1001 ;						\
MACRO_END

#define MATRIX_SET_OPT_BOTH( _matrix ) MACRO_START				\
  (_matrix)->opt = 0 ;								\
  if ( (_matrix)->matrix[ 0 ][ 0 ] != 0.0 ||					\
       (_matrix)->matrix[ 1 ][ 1 ] != 0.0 )					\
    (_matrix)->opt |= MATRIX_OPT_0011 ;						\
  if ( (_matrix)->matrix[ 1 ][ 0 ] != 0.0 ||					\
       (_matrix)->matrix[ 0 ][ 1 ] != 0.0 )					\
    (_matrix)->opt |= MATRIX_OPT_1001 ;						\
MACRO_END

#define MATRIX_REQ( _m1, _m2 )							\
  (((_m1)->opt == (_m2)->opt ) &&						\
										\
   ((((_m1)->opt & MATRIX_OPT_0011 ) == 0 ) ||					\
    (((_m1)->matrix[0][0] == (_m2)->matrix[0][0] ) &&				\
     ((_m1)->matrix[1][1] == (_m2)->matrix[1][1] ))) &&				\
										\
   ((((_m1)->opt & MATRIX_OPT_1001 ) == 0 ) ||					\
    (((_m1)->matrix[1][0] == (_m2)->matrix[1][0] ) &&				\
     ((_m1)->matrix[0][1] == (_m2)->matrix[0][1] ))))

#define MATRIX_REQ_EPSILON( _m1, _m2 )						\
  (((_m1)->opt == (_m2)->opt ) &&						\
										\
   ((((_m1)->opt & MATRIX_OPT_0011 ) == 0 ) ||					\
    ((fabs((_m1)->matrix[0][0] - (_m2)->matrix[0][0]) <=                        \
        max(fabs((_m1)->matrix[0][0]),                                          \
            fabs((_m2)->matrix[0][0]))*EPSILON_ACC_R) &&	                \
     (fabs((_m1)->matrix[1][1] - (_m2)->matrix[1][1]) <=                        \
        max(fabs((_m1)->matrix[1][1]),                                          \
            fabs((_m2)->matrix[1][1]))*EPSILON_ACC_R))) &&                      \
										\
   ((((_m1)->opt & MATRIX_OPT_1001 ) == 0 ) ||					\
    ((fabs((_m1)->matrix[1][0] - (_m2)->matrix[1][0]) <=                        \
        max(fabs((_m1)->matrix[1][0]),                                          \
            fabs((_m2)->matrix[1][0]))*EPSILON_ACC_R) &&	                \
     (fabs((_m1)->matrix[0][1] - (_m2)->matrix[0][1]) <=                        \
        max(fabs((_m1)->matrix[0][1]),                                          \
            fabs((_m2)->matrix[0][1]))*EPSILON_ACC_R))))

#define MATRIX_TEQ( _m1, _m2 )							\
  (((_m1)->matrix[2][0] == (_m2)->matrix[2][0] ) &&				\
   ((_m1)->matrix[2][1] == (_m2)->matrix[2][1] ))

#define MATRIX_TEQ_EPSILON( _m1, _m2 )						\
  ((fabs( (_m1)->matrix[2][0] - (_m2)->matrix[2][0] ) < EPSILON_ACC_T ) &&	\
   (fabs( (_m1)->matrix[2][1] - (_m2)->matrix[2][1] ) < EPSILON_ACC_T ))

#define MATRIX_EQ( _m1, _m2 )							\
  (MATRIX_REQ( _m1, _m2 ) && MATRIX_TEQ( _m1, _m2 ))

#define MATRIX_EQ_EPSILON( _m1, _m2 )						\
  (MATRIX_REQ_EPSILON( _m1, _m2 ) && MATRIX_TEQ_EPSILON( _m1, _m2 ))

#define MATRIX_TRANSFORM_XY(_x, _y, _rx, _ry, _matrix ) MACRO_START		\
  SYSTEMVALUE _px_ = ( SYSTEMVALUE )(_x) ;					\
  SYSTEMVALUE _py_ = ( SYSTEMVALUE )(_y) ;					\
  OMATRIX *_mptr_ = (_matrix) ;							\
  HQASSERT( matrix_assert( _mptr_ ) , "matrix not a proper optimised matrix" ) ;\
  if ( _mptr_->opt == MATRIX_OPT_0011 ) {					\
    (_rx) = _mptr_->matrix[ 2 ][ 0 ] + (_px_) * _mptr_->matrix[ 0 ][ 0 ] ;	\
    (_ry) = _mptr_->matrix[ 2 ][ 1 ] + (_py_) * _mptr_->matrix[ 1 ][ 1 ] ;	\
  }										\
  else {									\
    if ( _mptr_->opt == MATRIX_OPT_1001 ) {					\
      (_rx) = _mptr_->matrix[ 2 ][ 0 ] + (_py_) * _mptr_->matrix[ 1 ][ 0 ] ;	\
      (_ry) = _mptr_->matrix[ 2 ][ 1 ] + (_px_) * _mptr_->matrix[ 0 ][ 1 ] ;	\
    }										\
    else {									\
      (_rx) = _mptr_->matrix[ 2 ][ 0 ] + (_px_) * _mptr_->matrix[ 0 ][ 0 ] +	\
					 (_py_) * _mptr_->matrix[ 1 ][ 0 ] ;	\
      (_ry) = _mptr_->matrix[ 2 ][ 1 ] + (_px_) * _mptr_->matrix[ 0 ][ 1 ] +	\
					 (_py_) * _mptr_->matrix[ 1 ][ 1 ] ;	\
    }										\
  }										\
MACRO_END

#define MATRIX_TRANSFORM_DXY(_x, _y, _rx, _ry, _matrix ) MACRO_START		\
  SYSTEMVALUE _px_ = ( SYSTEMVALUE )(_x) ;					\
  SYSTEMVALUE _py_ = ( SYSTEMVALUE )(_y) ;					\
  OMATRIX *_mptr_ = (_matrix) ;							\
  HQASSERT( matrix_assert( _mptr_ ) , "matrix not a proper optimised matrix" ) ;\
  if ( _mptr_->opt == MATRIX_OPT_0011 ) {					\
    (_rx) = (_px_) * _mptr_->matrix[ 0 ][ 0 ] ;					\
    (_ry) = (_py_) * _mptr_->matrix[ 1 ][ 1 ] ;					\
  }										\
  else {									\
    if ( _mptr_->opt == MATRIX_OPT_1001 ) {					\
      (_rx) = (_py_) * _mptr_->matrix[ 1 ][ 0 ] ;				\
      (_ry) = (_px_) * _mptr_->matrix[ 0 ][ 1 ] ;				\
    }										\
    else {									\
      (_rx) = (_px_) * _mptr_->matrix[ 0 ][ 0 ] +	                        \
	      (_py_) * _mptr_->matrix[ 1 ][ 0 ] ;                               \
      (_ry) = (_px_) * _mptr_->matrix[ 0 ][ 1 ] +				\
	      (_py_) * _mptr_->matrix[ 1 ][ 1 ] ;				\
    }										\
  }										\
MACRO_END

#define MATRIX_COPY( _mdst, _msrc ) MACRO_START					\
 OMATRIX *_mdst_ = (_mdst) ;							\
 OMATRIX *_msrc_ = (_msrc) ;							\
 HQASSERT( matrix_assert( _msrc_ ) , "matrix not a proper optimised matrix" ) ;	\
 _mdst_->matrix[ 0 ][ 0 ] = _msrc_->matrix[ 0 ][ 0 ] ;				\
 _mdst_->matrix[ 1 ][ 0 ] = _msrc_->matrix[ 1 ][ 0 ] ;				\
 _mdst_->matrix[ 2 ][ 0 ] = _msrc_->matrix[ 2 ][ 0 ] ;				\
 _mdst_->matrix[ 0 ][ 1 ] = _msrc_->matrix[ 0 ][ 1 ] ;				\
 _mdst_->matrix[ 1 ][ 1 ] = _msrc_->matrix[ 1 ][ 1 ] ;				\
 _mdst_->matrix[ 2 ][ 1 ] = _msrc_->matrix[ 2 ][ 1 ] ;				\
 _mdst_->opt = _msrc_->opt ;							\
MACRO_END

#define MATRIX_00(_matrix)	(_matrix)->matrix[ 0 ][ 0 ]
#define MATRIX_10(_matrix)	(_matrix)->matrix[ 1 ][ 0 ]
#define MATRIX_20(_matrix)	(_matrix)->matrix[ 2 ][ 0 ]
#define MATRIX_01(_matrix)	(_matrix)->matrix[ 0 ][ 1 ]
#define MATRIX_11(_matrix)	(_matrix)->matrix[ 1 ][ 1 ]
#define MATRIX_21(_matrix)	(_matrix)->matrix[ 2 ][ 1 ]

typedef struct OMATRIX {
  SYSTEMVALUE matrix[ 3 ][ 2 ] ;
  int32 opt ;
} OMATRIX ;

extern OMATRIX identity_matrix ;

#if defined( ASSERT_BUILD )
int32 matrix_assert( OMATRIX *matrix ) ;
#endif

/* Translate 'matrix' by the passed amount, storing into 'result' (which may
point to the same instance as 'matrix'). */
void matrix_translate( OMATRIX *matrix,
                       SYSTEMVALUE tx, SYSTEMVALUE ty,
                       OMATRIX *result );

void matrix_scale( OMATRIX *matrix,
                   SYSTEMVALUE sx, SYSTEMVALUE sy,
                   OMATRIX *result );

/**
 * Create a new scale matrix for the specified values.
 */
void matrix_set_scale(OMATRIX* matrix, SYSTEMVALUE sx, SYSTEMVALUE sy);

/**
 * Create a new rotation matrix for the specified angle.
 */
void matrix_set_rotation(OMATRIX *matrix, SYSTEMVALUE angleInDegrees);

void matrix_mult( OMATRIX *m1, OMATRIX *m2, OMATRIX *result);

void matrix_copy( OMATRIX *result, OMATRIX *matrix );

int32 matrix_inverse( OMATRIX *matrix, OMATRIX *result);

void matrix_clean( OMATRIX *matrix );

void matrix_snap( OMATRIX *matrix , int32 snapvalue );

/* These 9s were 3x3, but in the first phase of the great springclean
   of 93, 9 was the simplest thing to go for. Later, we should make
   all these things be 3x3 again, but throughout the RIP this time.
*/
int32 matrix_inverse_3x3(SYSTEMVALUE m[9], SYSTEMVALUE inverse[9]);

/**
 * Returns true if the passed matrices are equal.
 */
Bool matrix_equal(OMATRIX* a, OMATRIX* b);

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
