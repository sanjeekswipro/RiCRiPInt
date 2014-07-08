/** \file
 * \ingroup gstate
 *
 * $HopeName: SWv20!src:gu_ctm.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions to manipulate the gstate's matrices.
 */

#include "core.h"
#include "hqmemcpy.h"

#include "matrix.h"
#include "graphics.h"
#include "routedev.h" /* DEVICE_* */

#include "gstate.h"
#include "params.h"   /* UserParams */
#include "pathops.h"  /* STROKE_PARAMS */
#include "panalyze.h" /* init_path_anaylsis() */
#include "fonts.h" /* charcontext_t */

#include "gu_ctm.h"

#include "namedef_.h" /* operators */

int32 newctmin = NEWCTM_ALLCOMPONENTS ;
OMATRIX smatrix = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0 } ;
OMATRIX sinv    = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0 } ;
OMATRIX smatrix_init = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0 } ;
OMATRIX sinv_init    = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0 } ;

void init_C_globals_gu_ctm(void)
{
  newctmin = NEWCTM_ALLCOMPONENTS ;
  HqMemCpy(&smatrix, &smatrix_init, sizeof(smatrix_init)) ;
  HqMemCpy(&sinv, &sinv_init, sizeof(sinv_init)) ;
}

/** Return the current matrix.
*/
Bool gs_getctm(OMATRIX* result, Bool remove_imposition)
{
  /* Note that we always initialise 'result' because sometimes
  gs_scalebypagebasematrix() doesn't touch it. */
  *result = thegsPageCTM(*gstateptr);
  if (remove_imposition) {
    if (! gs_scalebypagebasematrix(&thegsPageCTM(*gstateptr), result))
      return FALSE;
  }

  return TRUE;
}

/* ----------------------------------------------------------------------------
   function:            setctm(..)         author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           them .
   description:

   Utility function - replaces the current transformation matrix by them.

---------------------------------------------------------------------------- */
Bool gs_setctm( OMATRIX *mptr , int32 apply_imposition )
{
  OMATRIX matrix;

  if ( apply_imposition ) {
    /* see discussion in gstack.c for reasoning behind these equations */
    if ( ! matrix_inverse( & theIgsDevicePageCTM( gstateptr ) , & matrix ))
      return FALSE ;
    matrix_mult( mptr , & matrix , & matrix ) ;
    matrix_mult( & matrix , & pageBaseMatrix , & matrix ) ;
    matrix_mult( & matrix , & theIgsDevicePageCTM( gstateptr ) ,
                 & theIgsPageCTM( gstateptr )) ;
    matrix_clean( & theIgsPageCTM( gstateptr )) ;

    gotFontMatrix( theIFontInfo( gstateptr )) = FALSE ;
    theLookupMatrix( theIFontInfo( gstateptr )) = NULL ;

  } else {
    gotFontMatrix( theIFontInfo( gstateptr )) = FALSE ;
    theLookupMatrix( theIFontInfo( gstateptr )) = NULL ;
    MATRIX_COPY( & theIgsPageCTM( gstateptr ) , mptr ) ;
  }

  newctmin = NEWCTM_ALLCOMPONENTS ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            setdefaultctm(..)  author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           mptr .
   description:

   Utility function - replaces the default current transformation matrix by mptr.

---------------------------------------------------------------------------- */
Bool gs_setdefaultctm( OMATRIX *mptr , int32 modify_defaultpagectm )
{
  MATRIX_COPY( & theIgsDeviceCTM( gstateptr ) , mptr ) ;
  if ( modify_defaultpagectm )
  {
    MATRIX_COPY( & theIgsDevicePageCTM( gstateptr ) ,
                 mptr ) ;

    if ( ! matrix_inverse( & theIgsDevicePageCTM( gstateptr ) ,
                           & theIgsDeviceInversePageCTM( gstateptr )))
      return FALSE ;

    /* initialise path epsilons based on current theIgsDevicePageCTM( gstateptr ) */
    init_path_analysis() ;

  }

  return initmatrix_(get_core_context_interp()->pscontext) ;
}

/* ----------------------------------------------------------------------------
   function:            modify_ctm(..)     author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           m .
   description:

   Utility function - replaces the current transformation matrix by the result of
                      premultiplying it by the given matrix.

---------------------------------------------------------------------------- */
void gs_modifyctm( OMATRIX *mptr )
{
  matrix_mult( mptr , & theIgsPageCTM( gstateptr ) , & theIgsPageCTM( gstateptr )) ;

  newctmin = NEWCTM_ALLCOMPONENTS ;
  gotFontMatrix( theIFontInfo( gstateptr )) = FALSE ;
  theLookupMatrix( theIFontInfo( gstateptr )) = NULL ;
}

/* ----------------------------------------------------------------------------
   Translate the CTM by the passed amount.
*/
void gs_translatectm(SYSTEMVALUE x, SYSTEMVALUE y)
{
  matrix_translate(&thegsPageCTM(*gstateptr), x, y, &thegsPageCTM(*gstateptr));
  newctmin |= NEWCTM_TCOMPONENTS ;
}

/* --------------------------------------------------------------------------
   NOTE: This function does not always store the result in 'mdst'; it may just
         return 'msrc'.
*/
OMATRIX *gs_scalebypagebasematrix(OMATRIX *msrc, OMATRIX *mdst)
{
  if ( !char_doing_buildchar() &&
       CURRENT_DEVICE() != DEVICE_CHAR &&
       CURRENT_DEVICE() != DEVICE_PATTERN1 &&
       CURRENT_DEVICE() != DEVICE_PATTERN2 ) {
    /* Apply the inverse of the pagebase matrix so that
     * transform itransform will work as expected - see gstack.c for
     * discussion of these equations.
     */
    matrix_mult( & pageBaseMatrix ,
                 & thegsDevicePageCTM(*gstateptr) , mdst ) ;
    if ( ! matrix_inverse( mdst , mdst ))
      return NULL ;
    matrix_mult( msrc , mdst , mdst ) ;
    matrix_mult( mdst , & thegsDevicePageCTM(*gstateptr) , mdst ) ;
    matrix_clean( mdst ) ;
    return mdst ;
  }
  else {
    return msrc ;
  }
}

/* --------------------------------------------------------------------------*/
OMATRIX *gs_scalebyresfactor(OMATRIX *msrc, OMATRIX *mdst, SYSTEMVALUE resfactor)
{
  OMATRIX temp ;

  MATRIX_COPY( & temp , & identity_matrix ) ;
  temp.matrix[0][0] = temp.matrix[1][1] = resfactor;
  matrix_mult( msrc , & temp , mdst ) ;
  matrix_clean( mdst ) ;
  return mdst ;
}

/*
Log stripped */
