/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:mathfunc.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS Mathematic operators
 */

#define NEED_MYATAN2

#include "core.h"
#include "swerrors.h"
#include "swoften.h"
#include "often.h"

#include "constant.h"
#include "objects.h"
#include "stacks.h"

#include "mathfunc.h"

/* ----------------------------------------------------------------------------
   function:            mysqrt_()          author:              Andrew Cave
   creation date:       08-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 224.

---------------------------------------------------------------------------- */
Bool mysqrt_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register OBJECT *theo ;
  register SYSTEMVALUE arg ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;

  /* Do NOT use object_get_numeric, because of different behaviour with
     OINFINITY. */
  switch ( oType(*theo)) {
  case OREAL :
    arg = ( SYSTEMVALUE )oReal(*theo) ;
    break ;
  case OINTEGER :
    arg = ( SYSTEMVALUE )oInteger(*theo) ;
    break ;
  case OINFINITY :
    return TRUE ;
  default:
    return error_handler( TYPECHECK ) ;
  }
/*  Check for validity of argument. */
  if ( arg < 0.0 )
    return error_handler( RANGECHECK ) ;

  theTags(*theo) = OREAL | LITERAL ;
  oReal(*theo) = ( USERVALUE )sqrt(( double )arg ) ;
  theLen(*theo) = 0 ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            myatan_()          author:              Andrew Cave
   creation date:       18-Feb-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 121.

---------------------------------------------------------------------------- */
Bool myatan_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register OBJECT *theo ;
  register SYSTEMVALUE aa0 , aa1 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( ssize < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;

  /* Do NOT use object_get_numeric, because of different behaviour with
     OINFINITY. */
  switch ( oType(*theo)) {
  case OREAL :
    aa1 = ( SYSTEMVALUE )oReal(*theo) ;
    break ;
  case OINTEGER :
    aa1 = ( SYSTEMVALUE )oInteger(*theo) ;
    break ;
  case OINFINITY :
    return TRUE ;
  default:
    return error_handler( TYPECHECK ) ;
  }
  theo = stackindex( 1 , & operandstack ) ;

  /* Do NOT use object_get_numeric, because of different behaviour with
     OINFINITY. */
  switch ( oType(*theo)) {
  case OREAL :
    aa0 = ( SYSTEMVALUE )oReal(*theo) ;
    break ;
  case OINTEGER :
    aa0 = ( SYSTEMVALUE )oInteger(*theo) ;
    break ;
  case OINFINITY :
    return TRUE ;
  default:
    return error_handler( TYPECHECK ) ;
  }
/* Common cases - put in for accuracy. */
  if ( aa0 == 0.0 ) {
    if ( aa1 < 0.0 )
      aa0 = 180.0 ;
    else if ( aa1 == 0.0 )
      return error_handler( UNDEFINEDRESULT ) ;
    else
      aa0 = 0.0 ;
  }
  else if ( aa1 == 0.0 ) {
    if ( aa0 < 0.0 )
      aa0 = 270.0 ;
    else
      aa0 = 90.0 ;
  }
  else {
    aa0 = ( SYSTEMVALUE )( myatan2(( double ) aa0 ,
                                   ( double ) aa1 )
                          * RAD_TO_DEG ) ;
    if ( aa0 < 0 )
      aa0 += 360.0 ;
  }
  pop( & operandstack ) ;
  theTags(*theo) = OREAL | LITERAL ;
  oReal(*theo) = ( USERVALUE )aa0 ;
  theLen(*theo) = 0 ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            mycos_()           author:              Andrew Cave
   creation date:       09-Feb-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 132.

---------------------------------------------------------------------------- */
Bool mycos_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register OBJECT *theo ;
  register SYSTEMVALUE arg ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;

  /* Do NOT use object_get_numeric, because of different behaviour with
     OINFINITY. */
  switch ( oType(*theo)) {
  case OREAL :
    arg = ( SYSTEMVALUE )oReal(*theo) ;
    break ;
  case OINTEGER :
    arg = ( SYSTEMVALUE )oInteger(*theo) ;
    break ;
  case OINFINITY :
    return TRUE ;
  default:
    return error_handler( TYPECHECK ) ;
  }

  NORMALISE_ANGLE( arg ) ;
  COS_ANGLE( arg , arg ) ;

  theTags(*theo) = OREAL | LITERAL ;
  oReal(*theo) = ( USERVALUE )arg ;
  theLen(*theo) = 0 ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            mysin_()           author:              Andrew Cave
   creation date:       08-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 223.

---------------------------------------------------------------------------- */
Bool mysin_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register OBJECT *theo ;
  register SYSTEMVALUE arg ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;

  /* Do NOT use object_get_numeric, because of different behaviour with
     OINFINITY. */
  switch ( oType(*theo)) {
  case OREAL :
    arg = ( SYSTEMVALUE )oReal(*theo) ;
    break ;
  case OINTEGER :
    arg = ( SYSTEMVALUE )oInteger(*theo) ;
    break ;
  case OINFINITY :
    return TRUE ;
  default:
    return error_handler( TYPECHECK ) ;
  }

  NORMALISE_ANGLE( arg ) ;
  SIN_ANGLE( arg , arg ) ;

  theTags(*theo) = OREAL | LITERAL ;
  oReal(*theo) = ( USERVALUE )arg ;
  theLen(*theo) = 0 ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            myexp_()           author:              Andrew Cave
   creation date:       09-Feb-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 223.

---------------------------------------------------------------------------- */
Bool myexp_(ps_context_t *pscontext)
{
  SYSTEMVALUE args[ 2 ] ;
  SYSTEMVALUE result ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ! stack_get_numeric(&operandstack, args, 2) )
    return FALSE ;

  if (( args[ 0 ] == OINFINITY_VALUE ) || ( args[ 1 ] == OINFINITY_VALUE ))
    return TRUE ;

  if (( args[ 0 ] < 0.0 ) && ( args[ 1 ] - (( int32 ) args[ 1 ] ) != 0.0 ))
    return error_handler( UNDEFINEDRESULT ) ;

  result = ( SYSTEMVALUE )pow(( double ) args[ 0 ] , ( double ) args[ 1 ] ) ;

  if ( ! realrange( result ))
    return error_handler( RANGECHECK ) ;
  if ( ! realprecision( result ))
    result = 0.0 ;

  npop( 2 , & operandstack ) ;

  return stack_push_real( result, &operandstack ) ;
}

/* ----------------------------------------------------------------------------
   function:            ln_()              author:              Andrew Cave
   creation date:       09-Feb-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 180.

---------------------------------------------------------------------------- */
Bool ln_(ps_context_t *pscontext)
{
  SYSTEMVALUE arg ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ! stack_get_numeric(&operandstack, &arg, 1) )
    return FALSE ;

/*  Check for validity of argument. */
  if ( arg <= 0.0 )
    return error_handler( UNDEFINEDRESULT ) ;

  if ( arg == OINFINITY_VALUE )
    return TRUE ;

  pop( & operandstack ) ;

  return stack_push_real(( SYSTEMVALUE )log(( double ) arg ), &operandstack) ;
}

/* ----------------------------------------------------------------------------
   function:            mylog_()           author:              Andrew Cave
   creation date:       09-Feb-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 181.

---------------------------------------------------------------------------- */
Bool mylog_(ps_context_t *pscontext)
{
  SYSTEMVALUE arg ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ! stack_get_numeric(&operandstack, &arg, 1) )
    return FALSE ;

/*  Check for validity of argument. */
  if ( arg <= 0.0 )
    return error_handler( UNDEFINEDRESULT ) ;

  if ( arg == OINFINITY_VALUE )
    return TRUE ;

  pop( & operandstack ) ;

  return stack_push_real(( SYSTEMVALUE )log10(( double ) arg ), &operandstack) ;
}

/* For the next set of operators see file 'randomops.c' */



/* Log stripped */
