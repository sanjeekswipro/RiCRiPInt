/** \file
 * \ingroup matrix
 *
 * $HopeName: SWv20!src:matrxops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Matrix operations.
 */

#include "core.h"
#include "pscontext.h"
#include "mm.h"
#include "mmcompat.h"
#include "swerrors.h"
#include "swoften.h"
#include "often.h"
#include "objects.h"
#include "fonts.h" /* charcontext_t */

#include "matrix.h"
#include "constant.h"
#include "miscops.h"
#include "params.h"
#include "psvm.h"
#include "stacks.h"
#include "graphics.h"
#include "gstack.h"
#include "routedev.h"
#include "devops.h"
#include "swmemory.h"
#include "gu_ctm.h"
#include "pathops.h"
#include "utils.h"

#include "mathfunc.h"

static Bool trans(corecontext_t *context, Bool inverse, Bool delta) ;

/* ----------------------------------------------------------------------------
   function:            matrix_()          author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 184.

---------------------------------------------------------------------------- */
Bool matrix_(ps_context_t *pscontext)
{
  register OBJECT *olist ;
  OBJECT oarray = OBJECT_NOTVM_NOTHING ;
  corecontext_t *corecontext = pscontext->corecontext ;

  if ( NULL == (olist = get_omemory(6)) )
    return error_handler(VMERROR) ;

  theTags(oarray) = OARRAY | LITERAL | UNLIMITED ;
  SETGLOBJECT(oarray, corecontext) ;
  theLen(oarray) = 6 ;
  oArray(oarray) = olist ;

  object_store_real(&olist[0], 1.0f) ;
  object_store_real(&olist[1], 0.0f) ;
  object_store_real(&olist[2], 0.0f) ;
  object_store_real(&olist[3], 1.0f) ;
  object_store_real(&olist[4], 0.0f) ;
  object_store_real(&olist[5], 0.0f) ;

  return push(&oarray, &operandstack) ;
}

/* ----------------------------------------------------------------------------
   function:            initmatrix_()      author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 174.

---------------------------------------------------------------------------- */
Bool initmatrix_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( CURRENT_DEVICE() == DEVICE_NULL )
    return gs_setctm( & identity_matrix ,
                      doing_imposition &&
                      !char_doing_buildchar()) ;
  else
    return gs_setctm( & thegsDeviceCTM(*gstateptr) , INCLUDE_IMPOSITION ) ;
}

/* ----------------------------------------------------------------------------
   function:            identmatrix_()     author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 167.

---------------------------------------------------------------------------- */
Bool identmatrix_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register OBJECT *theo ;
  register OBJECT *olist ;

/*  Check that a matrix object exists on the top of the stack. */
  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;
  if ( oType(*theo) != OARRAY )
    return error_handler( TYPECHECK ) ;
  if ( ! oCanWrite(*theo) )
    if ( ! object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;
  if ( theLen(*theo) != 6 )
    return error_handler( RANGECHECK ) ;

/*  If it does, then replace it's value by them. */
  olist = oArray(*theo) ;
/* Check if saved. */
  if ( ! check_asave(olist, 6, oGlobalValue(*theo), pscontext->corecontext) )
    return FALSE ;

  object_store_real(&olist[0], 1.0f) ;
  object_store_real(&olist[1], 0.0f) ;
  object_store_real(&olist[2], 0.0f) ;
  object_store_real(&olist[3], 1.0f) ;
  object_store_real(&olist[4], 0.0f) ;
  object_store_real(&olist[5], 0.0f) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            defaultmatrix_()   author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 145.

---------------------------------------------------------------------------- */
Bool defaultmatrix_(ps_context_t *pscontext)
{
  int32 ssize ;
  OMATRIX matrix ;
  OMATRIX *mptr ;
  OBJECT *theo ;

/*  Check that a matrix object exists on the top of the stack. */
  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;
  if ( oType(*theo) != OARRAY )
    return error_handler( TYPECHECK ) ;
  if ( ! oCanWrite(*theo) )
    if ( ! object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;
  if ( theLen(*theo) != 6 )
    return error_handler( RANGECHECK ) ;

  if ( CURRENT_DEVICE() == DEVICE_NULL )
    return from_matrix(oArray(*theo), &identity_matrix, oGlobalValue(*theo)) ;
  else {
    SYSTEMVALUE resfactor;
    mptr = & thegsDeviceCTM(*gstateptr) ;

    resfactor = ps_core_context(pscontext)->userparams->ResolutionFactor;
    if ( resfactor > 0.0 )
      mptr = gs_scalebyresfactor( mptr , & matrix , resfactor ) ;

    return from_matrix(oArray(*theo), mptr, oGlobalValue(*theo)) ;
  }
}

/* ----------------------------------------------------------------------------
   function:            currmatrix_()      author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 137.

---------------------------------------------------------------------------- */
Bool currmatrix_(ps_context_t *pscontext)
{
  int32 ssize ;
  OMATRIX tempMatrix1, tempMatrix2 ;
  OMATRIX *mptr ;
  OBJECT *theo ;
  SYSTEMVALUE resfactor;

/*  Check that a matrix object exists on the top of the stack. */
  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;
  if ( oType(*theo) != OARRAY )
    return error_handler( TYPECHECK ) ;
  if ( ! oCanWrite(*theo) )
    if ( ! object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;
  if ( theLen(*theo) != 6 )
    return error_handler( RANGECHECK ) ;

  if ( ! gs_getctm(&tempMatrix1, doing_imposition))
    return FALSE ;

  resfactor = ps_core_context(pscontext)->userparams->ResolutionFactor;
  if ( resfactor > 0.0 )
    mptr = gs_scalebyresfactor( &tempMatrix1 , & tempMatrix2 , resfactor ) ;
  else
    mptr = &tempMatrix1 ;

  return from_matrix(oArray(*theo), mptr, oGlobalValue(*theo)) ;
}

/* ----------------------------------------------------------------------------
   function:            setmatrix_()       author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 219.

---------------------------------------------------------------------------- */
Bool setmatrix_(ps_context_t *pscontext)
{
  int32 ssize ;
  OMATRIX matrix ;
  OMATRIX *mptr ;
  OBJECT *theo ;
  SYSTEMVALUE resfactor;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  mptr = & matrix ;
  theo = TopStack( operandstack , ssize ) ;
  if ( ! is_matrix( theo , mptr ))
    return FALSE ;
  if ( ! oCanRead(*theo) )
    if ( ! object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;

/*  Extract the elements of the matrix, and replace the current CTM with them. */
  pop( & operandstack ) ;

  resfactor = ps_core_context(pscontext)->userparams->ResolutionFactor;
  if ( resfactor > 0.0 )
    mptr = gs_scalebyresfactor( mptr , mptr , 1/resfactor ) ;

  return gs_setctm( mptr , INCLUDE_IMPOSITION ) ;
}

/* ----------------------------------------------------------------------------
   function:            translate_()       author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 233.

---------------------------------------------------------------------------- */
Bool translate_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register int32 glmode ;
  register OBJECT *o1 , *o2 , *o3 ;
  register OBJECT *olist ;

  SYSTEMVALUE args[ 2 ] ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;
/*
  If the top argument is a matrix, which is followed by two numeric
  arguments, then fill it with the transform matrix T produced from
  these.
*/
  o3 = TopStack( operandstack , ssize ) ;
  if ( oType(*o3) == OARRAY ) {
    if ( theLen(*o3) != 6 )
      return error_handler( RANGECHECK ) ;
    if ( theStackSize( operandstack ) < 2 )
      return error_handler( STACKUNDERFLOW ) ;

    o2 = stackindex( 1 , & operandstack ) ;
    o1 = stackindex( 2 , & operandstack ) ;
    switch ( oType(*o1) ) {
    case OREAL :
    case OINTEGER:
    case OINFINITY :
      break ;
    default:
      return error_handler( TYPECHECK ) ;
    }
    switch ( oType(*o2) ) {
    case OREAL :
    case OINTEGER:
    case OINFINITY :
      break ;
    default:
      return error_handler( TYPECHECK ) ;
    }
    if ( ! oCanWrite(*o3) )
      if ( ! object_access_override(o3) )
        return error_handler( INVALIDACCESS ) ;

    olist = oArray(*o3) ;
    /* Check if saved. */
    glmode = oGlobalValue(*o3) ;
    if ( ! check_asave(olist, 6, glmode, pscontext->corecontext))
      return FALSE ;

    object_store_real(&olist[0], 1.0f) ;
    object_store_real(&olist[1], 0.0f) ;
    object_store_real(&olist[2], 0.0f) ;
    object_store_real(&olist[3], 1.0f) ;
    OCopy(olist[4], *o1) ;
    OCopy(olist[5], *o2) ;

    /* Save matrix back to operandstack */
    Copy(o1, o3) ;

    npop( 2 , & operandstack ) ;
    return TRUE ;
  } else {
/*
  Otherwise if there are just two numbers, form the transform
  matrix T, and replace the CTM by itself premultiplied by T.
*/
    if ( ! stack_get_numeric(&operandstack, args, 2) )
      return FALSE ;

    gs_translatectm(args[0], args[1]) ;

    npop( 2 , & operandstack ) ;
    return TRUE ;
  }
}

/* ----------------------------------------------------------------------------
   function:            scale_()           author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 209.

---------------------------------------------------------------------------- */
Bool scale_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register int32 glmode ;
  register OBJECT *o1 , *o2 , *o3 ;
  register OBJECT *olist ;

  SYSTEMVALUE args[ 2 ] ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;
/*
  If the top argument is a matrix, which is followed by two numeric
  arguments, then fill it with the transform matrix T produced from
  these.
*/
  o3 = TopStack( operandstack , ssize ) ;
  if ( oType(*o3) == OARRAY ) {
    if ( theLen(*o3) != 6 )
      return error_handler( RANGECHECK ) ;
    if ( theStackSize( operandstack ) < 2 )
      return error_handler( STACKUNDERFLOW ) ;
    o2 = stackindex( 1 , & operandstack ) ;
    o1 = stackindex( 2 , & operandstack ) ;
    switch ( oType(*o1) ) {
    case OREAL :
    case OINTEGER:
    case OINFINITY :
      break ;
    default:
      return error_handler( TYPECHECK ) ;
    }
    switch ( oType(*o2) ) {
    case OREAL :
    case OINTEGER:
    case OINFINITY :
      break ;
    default:
      return error_handler( TYPECHECK ) ;
    }
    if ( ! oCanWrite(*o3) )
      if ( ! object_access_override(o3) )
        return error_handler( INVALIDACCESS ) ;

    olist = oArray(*o3) ;
    /* Check if saved. */
    glmode = oGlobalValue(*o3) ;
    if ( ! check_asave(olist, 6, glmode, pscontext->corecontext))
      return FALSE ;

    OCopy(olist[0], *o1) ;
    object_store_real(&olist[1], 0.0f) ;
    object_store_real(&olist[2], 0.0f) ;
    OCopy(olist[3], *o2) ;
    object_store_real(&olist[4], 0.0f) ;
    object_store_real(&olist[5], 0.0f) ;

    Copy( o1 , o3 ) ;

    npop( 2 , & operandstack ) ;
    return TRUE ;
  }
/*
  Otherwise if there are just two numbers, form the scale
  matrix S, and replace the CTM by itself premultiplied by S.
*/
  else {
    if ( ! stack_get_numeric(&operandstack, args, 2) )
      return FALSE ;

    matrix_scale(&thegsPageCTM(*gstateptr), args[0], args[1], &thegsPageCTM(*gstateptr)) ;

    newctmin |= NEWCTM_RCOMPONENTS ;
    gotFontMatrix( theFontInfo(*gstateptr)) = FALSE ;
    theLookupMatrix( theFontInfo(*gstateptr)) = NULL ;

    npop( 2 , & operandstack ) ;
    return TRUE ;
  }
}

/* ----------------------------------------------------------------------------
   function:            rotate_()          author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 206.

---------------------------------------------------------------------------- */
Bool rotate_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register int32 glmode ;
  register OBJECT *o1 , *o2 ;
  register OBJECT *olist ;
  register SYSTEMVALUE  cs , sn ;
  SYSTEMVALUE angle ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;
/*
  If the top argument is a matrix, which is followed by one numeric
  argument, then fill it with the transform matrix T produced from
  these.
*/
  o2 = TopStack( operandstack , ssize ) ;
  if ( oType(*o2) == OARRAY ) {
    if ( theLen(*o2) != 6 )
      return error_handler( RANGECHECK ) ;

    if ( ssize < 1 )
      return error_handler(STACKUNDERFLOW) ;

    o1 = stackindex( 1 , & operandstack ) ;

    if ( !object_get_numeric(o1, &angle) )
      return FALSE ;

    if ( ! oCanWrite(*o2) )
      if ( ! object_access_override(o2) )
        return error_handler( INVALIDACCESS ) ;

    NORMALISE_ANGLE( angle ) ;
    SINCOS_ANGLE( angle , sn , cs ) ;

    Copy( o1 , o2 ) ;
    pop( & operandstack ) ;

    olist = oArray(*o1) ;
/* Check if saved. */
    glmode = oGlobalValue(*o1) ;
    if ( ! check_asave(olist, 6, glmode, pscontext->corecontext))
      return FALSE ;

    object_store_real(&olist[0], (USERVALUE)cs) ;
    object_store_real(&olist[1], (USERVALUE)sn) ;
    object_store_real(&olist[2], (USERVALUE)-sn) ;
    object_store_real(&olist[3], (USERVALUE)cs) ;
    object_store_real(&olist[4], 0.0f) ;
    object_store_real(&olist[5], 0.0f) ;

    return TRUE ;
  }
/*
  Otherwise if there is just one number, form the rotation
  matrix R, and replace the CTM by itself premultiplied by R.
*/
  else {
    if ( ! stack_get_numeric(&operandstack, &angle, 1) )
      return FALSE ;
    if ( angle != 0.0 ) {
      OMATRIX matrix ;

      matrix_set_rotation(&matrix, angle) ;
      gs_modifyctm(&matrix) ;
    }
  }
  pop( & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            concat_()          author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 130.

---------------------------------------------------------------------------- */
Bool concat_(ps_context_t *pscontext)
{
  int32 ssize ;
  OMATRIX matrix ;
  OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

/*  Check that a matrix exists on the top of the operand stack. */
  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;
  if ( ! is_matrix( theo , & matrix ))
    return FALSE ;
  if ( ! oCanRead(*theo) )
    if ( ! object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;

/*  Extract its elements, and premultiply the CTM by it. */
  gs_modifyctm( & matrix ) ;
  pop( & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            concatmatrix_()    author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 130.

---------------------------------------------------------------------------- */
Bool concatmatrix_(ps_context_t *pscontext)
{
  OMATRIX m1 , m2 , m3 ;
  register int32 ssize ;
  register OBJECT *o1, *o2, *optr ;

/*  Check that three matrices exist on the top of the operand stack */

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( ssize < 2 )
    return error_handler( STACKUNDERFLOW ) ;

  optr = TopStack( operandstack , ssize ) ;
  if ( oType(*optr) != OARRAY )
    return error_handler( TYPECHECK ) ;
  if ( theLen(*optr) != 6 )
    return error_handler( RANGECHECK ) ;

  o2 = stackindex(1, &operandstack) ;
  if ( ! is_matrix(o2, &m2) )
    return FALSE ;

  o1 = stackindex(2, &operandstack) ;
  if ( ! is_matrix(o1, &m1) )
    return FALSE ;

  if ( (!oCanRead(*o1) && !object_access_override(o1)) ||
       (!oCanRead(*o2) && !object_access_override(o2)) )
    return error_handler( INVALIDACCESS ) ;

  if ( ! oCanWrite(*optr) )
    if ( ! object_access_override(optr) )
      return error_handler( INVALIDACCESS ) ;

  matrix_mult( & m1 , & m2 , & m3 ) ;
  if ( ! from_matrix(oArray(*optr), &m3, oGlobalValue(*optr)) )
    return FALSE ;

  Copy( o1 , optr ) ;
  npop( 2 , & operandstack ) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            transform_()       author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 233.

---------------------------------------------------------------------------- */
Bool transform_(ps_context_t *pscontext)
{
  return trans(ps_core_context(pscontext), FALSE , FALSE ) ;
}

/* ----------------------------------------------------------------------------
   function:            dtransform_()      author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 148.

---------------------------------------------------------------------------- */
Bool dtransform_(ps_context_t *pscontext)
{
  return trans(ps_core_context(pscontext), FALSE, TRUE ) ;
}

/* ----------------------------------------------------------------------------
   function:            itransform_()      author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 176.

---------------------------------------------------------------------------- */
Bool itransform_(ps_context_t *pscontext)
{
  return trans(ps_core_context(pscontext), TRUE , FALSE ) ;
}

/* ----------------------------------------------------------------------------
   function:            idtransform_()     author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 168.

---------------------------------------------------------------------------- */
Bool idtransform_(ps_context_t *pscontext)
{
  return trans(ps_core_context(pscontext), TRUE , TRUE ) ;
}

/* Utility function for above functions - acts according to inverse & delta */
static Bool trans(corecontext_t *context, Bool inverse, Bool delta)
{
  int32 ssize ;

  int32 n ;
  OMATRIX matrix ;
  OMATRIX *mptr ;
  OBJECT *o1, *o2 ;
  SYSTEMVALUE rx , ry ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;
/*
  If the top argument is a matrix, which is followed by two numeric arguments,
  then transform  the two  numeric arguments by it, or it's inverse, according
  to the flag inverse.
*/
  n = 0 ;
  o2 = TopStack( operandstack , ssize ) ;
  if ( oType(*o2) == OARRAY || oType(*o2) == OPACKEDARRAY ) {
    mptr = & matrix ;
    if ( ! is_matrix( o2 , mptr ))
      return FALSE ;
    if ( ! oCanRead(*o2) )
      if ( ! object_access_override(o2) )
        return error_handler( INVALIDACCESS ) ;
    n = 1 ;
    if ( ssize < 2 )
      return error_handler( STACKUNDERFLOW ) ;
    o2 = ( & o2[ -1 ] ) ;
    o1 = ( & o2[ -1 ] ) ;
    if ( ! fastStackAccess( operandstack )) {
      o2 = stackindex( 1 , & operandstack ) ;
      o1 = stackindex( 2 , & operandstack ) ;
    }
  }
  else {
    SYSTEMVALUE resfactor;

    mptr = & thegsPageCTM(*gstateptr) ;

    if ( doing_imposition ) {
      mptr = gs_scalebypagebasematrix( mptr , & matrix ) ;
      if ( ! mptr )
        return FALSE ;
    }

    resfactor = context->userparams->ResolutionFactor;
    if ( resfactor > 0.0 )
      mptr = gs_scalebyresfactor( mptr , & matrix , resfactor ) ;

    if ( ssize < 1 )
      return error_handler( STACKUNDERFLOW ) ;
    o1 = ( & o2[ -1 ] ) ;
    if ( ! fastStackAccess( operandstack ))
      o1 = stackindex( 1 , & operandstack ) ;
  }

  if ( !object_get_numeric(o1, &rx) ||
       !object_get_numeric(o2, &ry) )
    return FALSE ;

/*
  Otherwise if there are just two numbers, then transform the two numeric
  arguments by the CTM or it's inverse, according  to  the  flag inverse.
*/
  if ( inverse ) {
    if ( mptr == & matrix ) {
      if ( ! matrix_inverse( mptr , mptr ))
        return error_handler( UNDEFINEDRESULT ) ;
    }
    else {
      /* mptr == & thegsPageCTM(*gstateptr) */
      SET_SINV_SMATRIX( mptr , NEWCTM_ALLCOMPONENTS ) ;
      if ( SINV_NOTSET( NEWCTM_ALLCOMPONENTS ) )
        return error_handler( UNDEFINEDRESULT ) ;
      mptr = & sinv ;
    }
  }

/* If the delta flag is set, then use a distance tranformation. */
  if ( n )
   pop( & operandstack ) ;

  if ( delta ) {
    MATRIX_TRANSFORM_DXY( rx, ry, rx, ry, mptr ) ;
  }
  else {
    MATRIX_TRANSFORM_XY( rx, ry, rx, ry, mptr ) ;
  }

  object_store_numeric(o1, rx) ;
  object_store_numeric(o2, ry) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            invertmatrix_()    author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 175.

---------------------------------------------------------------------------- */
Bool invertmatrix_(ps_context_t *pscontext)
{
  register OBJECT *o1 , *o2 ;

  OMATRIX matrix ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

/*  Check that two matrices exist on the top of the operand stack. */
  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = theTop( operandstack ) ;
  if ( oType(*o2) != OARRAY )
    return error_handler( TYPECHECK ) ;
  if ( ! oCanWrite(*o2) )
    if ( ! object_access_override(o2) )
      return error_handler( INVALIDACCESS ) ;
  if ( theLen(*o2) != 6 )
    return error_handler( RANGECHECK ) ;

  o1 = stackindex( 1 , & operandstack ) ;
  if ( ! is_matrix( o1 , & matrix ))
    return FALSE ;
  if ( ! oCanRead(*o1) )
    if ( ! object_access_override(o1) )
      return error_handler( INVALIDACCESS ) ;

  if ( ! matrix_inverse( & matrix , & matrix ))
    return error_handler( UNDEFINEDRESULT ) ;

  if ( ! from_matrix(oArray(*o2), &matrix, oGlobalValue(*o2)) )
    return FALSE ;

  pop( & operandstack  ) ;
  Copy( o1 , o2 ) ;

  return TRUE ;
}

/*
Log stripped */
