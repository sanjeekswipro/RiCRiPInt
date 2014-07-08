/** \file
 * \ingroup gstate
 *
 * $HopeName: SWv20!export:gu_ctm.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Graphics state matrix manipulation functions.
 */

#ifndef __GU_CTM_H__
#define __GU_CTM_H__

#include "matrix.h"             /* OMATRIX */

#define NEWCTM_RCOMPONENTS	0x0f
#define NEWCTM_TCOMPONENTS	0x30
#define NEWCTM_ALLCOMPONENTS	( NEWCTM_RCOMPONENTS | NEWCTM_TCOMPONENTS )

#define SET_SINV_SMATRIX( _matrix , _usecomp ) MACRO_START		\
  /* Check if the components we're interested in are out of date. */	\
  int32 _usecomp_ = newctmin & (_usecomp) ;				\
  if ( _usecomp_ != 0 ) {						\
    Bool _uptodate_ = TRUE ;						\
    if (( _usecomp_ & NEWCTM_RCOMPONENTS ) != 0 ) {			\
      newctmin &= (~NEWCTM_RCOMPONENTS) ;				\
      if ( ! MATRIX_REQ( & smatrix, (_matrix))) {			\
	_uptodate_ = FALSE ;						\
      }									\
    }									\
    if (( _usecomp_ & NEWCTM_TCOMPONENTS ) != 0 ) {			\
      newctmin &= (~NEWCTM_TCOMPONENTS) ;				\
      if ( ! MATRIX_TEQ( & smatrix, (_matrix))) {			\
	_uptodate_ = FALSE ;						\
      }									\
    }									\
    if ( ! _uptodate_ ) {						\
      if ( matrix_inverse( (_matrix) , & sinv )) {			\
	newctmin = 0 ; /* Everything up to date. */			\
	MATRIX_COPY( & smatrix , (_matrix) ) ;				\
      }									\
      else								\
        newctmin = NEWCTM_ALLCOMPONENTS ;				\
    }									\
  }									\
  else									\
    HQASSERT(((( (_usecomp) & NEWCTM_RCOMPONENTS ) == 0 ) ||		\
              ( MATRIX_REQ( & smatrix, (_matrix))))			\
	     &&								\
	     ((( (_usecomp) & NEWCTM_TCOMPONENTS ) == 0 ) ||		\
	      ( MATRIX_TEQ( & smatrix, (_matrix)))),			\
	     "newctmin not set when matrix out of date" ) ;		\
MACRO_END

#define SINV_NOTSET( _usecomp )	(( newctmin & (_usecomp)) != 0 )

/** The functions gs_getctm() and gs_setctm() change thier action based on the
current imposition state; this macro evaluations to true when imposition should
be removed (for gs_getctm()) or added to (for gs_setctm()) the ctm. */
#define INCLUDE_IMPOSITION (doing_imposition && !char_doing_buildchar() && \
                            CURRENT_DEVICE() != DEVICE_CHAR && \
                            CURRENT_DEVICE() != DEVICE_PATTERN1 && \
                            CURRENT_DEVICE() != DEVICE_PATTERN2)
extern int32 newctmin;
extern OMATRIX sinv;
extern OMATRIX smatrix	;

Bool gs_getctm(OMATRIX* result, Bool remove_imposition) ;
Bool gs_setctm(OMATRIX *mptr, int32 apply_imposition) ;
Bool gs_setdefaultctm(OMATRIX *mptr, Bool modify_defaultpagectm) ;
void gs_modifyctm(OMATRIX *mptr) ;
void gs_translatectm(SYSTEMVALUE x, SYSTEMVALUE y);

OMATRIX *gs_scalebypagebasematrix(OMATRIX *msrc, OMATRIX *mdst) ;
OMATRIX *gs_scalebyresfactor(OMATRIX *msrc, OMATRIX *mdst, SYSTEMVALUE resfactor) ;

#endif /* protection for multiple inclusion */


/*
Log stripped */
