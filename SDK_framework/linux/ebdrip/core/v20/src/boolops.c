/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:boolops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Boolean and bitwise operators
 */

#include "core.h"
#include "hqmemcmp.h"
#include "swerrors.h"
#include "swoften.h"
#include "often.h"
#include "objects.h"

#include "stacks.h"

static Bool gtgeltle(Bool swapargs, Bool negateresult);

/* ----------------------------------------------------------------------------
   function:            eq_()              author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 150.

---------------------------------------------------------------------------- */
Bool eq_(ps_context_t *pscontext)
{
  Bool result ;
  register OBJECT *o1 , *o2 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o1 = theTop( operandstack ) ;
  if ( ! fastStackAccess( operandstack ))
    o2 = stackindex( 1 , & operandstack ) ;
  else
    o2 = ( & o1[ -1 ] ) ;

  if ( ! o1_eq_o2( o1 , o2 , & result )) {
    return FALSE ;
  }

  pop( & operandstack ) ;
  object_store_bool( theTop( operandstack ) , result ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            ne_()              author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 186.

---------------------------------------------------------------------------- */
Bool ne_(ps_context_t *pscontext)
{
  OBJECT *o1 ;

  if ( ! eq_(pscontext))
    return FALSE ;
  o1 = theTop( operandstack ) ;
  oBool(*o1) = !oBool(*o1) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            ge_()              author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 163.

---------------------------------------------------------------------------- */
Bool gt_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gtgeltle( FALSE , FALSE ) ;
}

Bool ge_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gtgeltle( TRUE , TRUE ) ;
}

Bool lt_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gtgeltle( TRUE , FALSE ) ;
}

Bool le_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gtgeltle( FALSE , TRUE ) ;
}

static Bool gtgeltle(Bool swapargs, Bool negateresult)
{
  OBJECT *o1 , *o2 ;
  Bool result ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  if ( swapargs ) {
    o1 = theTop( operandstack ) ;
    if ( ! fastStackAccess( operandstack ))
      o2 = stackindex( 1 , & operandstack ) ;
    else
      o2 = ( & o1[ -1 ] ) ;
  }
  else {
    o2 = theTop( operandstack ) ;
    if ( ! fastStackAccess( operandstack ))
      o1 = stackindex( 1 , & operandstack ) ;
    else
      o1 = ( & o2[ -1 ] ) ;
  }

  switch ( oType( *o1 )) {
    case OINTEGER:
    case OREAL:
    case OINFINITY:
      if ( oType( *o2 ) != OINTEGER &&
           oType( *o2 ) != OREAL &&
           oType( *o2 ) != OINFINITY ) {
        return error_handler( TYPECHECK ) ;
      }
      break ;

    case OSTRING :
    case OLONGSTRING :
      if ( oType( *o2 ) != OSTRING &&
           oType( *o2 ) != OLONGSTRING ) {
        return error_handler( TYPECHECK ) ;
      }
      if ( (!oCanRead(*o1) && !object_access_override(o1)) ||
           (!oCanRead(*o2) && !object_access_override(o2)) ) {
        return error_handler( INVALIDACCESS ) ;
      }
      break ;

    default:
      return error_handler( TYPECHECK ) ;
  }

  result = o1_gt_o2( o1 , o2 ) ;

  pop( & operandstack ) ;
  object_store_bool( theTop( operandstack ) ,
                     negateresult ? !result : result ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            and_()             author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 116.

---------------------------------------------------------------------------- */
Bool and_(ps_context_t *pscontext)
{
  register OBJECT *o1 , *o2 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = theTop( operandstack ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  switch ( oType(*o1)) {

  case OBOOLEAN :
    if ( oType(*o2) != OBOOLEAN )
      return error_handler( TYPECHECK ) ;
    oBool(*o1) &= oBool(*o2) ;
    break ;

  case OINTEGER :
    if ( oType(*o2) != OINTEGER )
      return error_handler( TYPECHECK ) ;
    oInteger(*o1) &= oInteger(*o2) ;
    break ;

    default:
      return error_handler( TYPECHECK ) ;
  }
  pop( & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            not_()             author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 189.

---------------------------------------------------------------------------- */

Bool not_(ps_context_t *pscontext)
{
  register OBJECT *o1 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

/* Check for a boolean on top of operand stack. */
  o1 = theTop( operandstack ) ;
  switch ( oType(*o1)) {
  case OBOOLEAN :
    oBool(*o1) = ( ! oBool(*o1)) ;
    return TRUE ;

  case OINTEGER :
    oInteger(*o1) = ( ~oInteger(*o1)) ;
    return TRUE ;
  default:
    return error_handler( TYPECHECK ) ;
  }
}

/* ----------------------------------------------------------------------------
   function:            or_()              author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 191.

---------------------------------------------------------------------------- */
Bool or_(ps_context_t *pscontext)
{
  register OBJECT *o1 , *o2 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = theTop( operandstack ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  switch ( oType(*o1)) {

  case OBOOLEAN :
    if ( oType(*o2) != OBOOLEAN )
      return error_handler( TYPECHECK ) ;
    oBool(*o1) |= oBool(*o2) ;
    break ;

  case OINTEGER :
    if ( oType(*o2) != OINTEGER )
      return error_handler( TYPECHECK ) ;
    oInteger(*o1) |= oInteger(*o2) ;
    break ;

    default:
      return error_handler( TYPECHECK ) ;
  }
  pop( & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            xor_()             author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 241.

---------------------------------------------------------------------------- */
Bool xor_(ps_context_t *pscontext)
{
  register OBJECT *o1 , *o2 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = theTop( operandstack ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  switch ( oType(*o1)) {

  case OBOOLEAN :
    if ( oType(*o2) != OBOOLEAN )
      return error_handler( TYPECHECK ) ;
    oBool(*o1) ^= oBool(*o2) ;
    break ;

  case OINTEGER :
    if ( oType(*o2) != OINTEGER )
      return error_handler( TYPECHECK ) ;
    oInteger(*o1) ^= oInteger(*o2) ;
    break ;

  default:
    return error_handler( TYPECHECK ) ;
  }
  pop( & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            bitshift_()        author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 125.

---------------------------------------------------------------------------- */
Bool bitshift_(ps_context_t *pscontext)
{
  register int32 shift ;
  register OBJECT *o1 , *o2 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = theTop( operandstack ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  if (( oType(*o1) != OINTEGER ) || ( oType(*o2) != OINTEGER ))
    return error_handler( TYPECHECK ) ;

  shift = oInteger(*o2) ;
  oInteger(*o1) = (int32)( shift < 0 ) ?
    (( shift <= -32 ) ? 0 : ((uint32)oInteger(*o1)) >> (-shift)) :
    (( shift >= 32 ) ? 0 : ((uint32)oInteger(*o1)) << shift ) ;

  pop( & operandstack ) ;
  return TRUE ;
}

/* Log stripped */
