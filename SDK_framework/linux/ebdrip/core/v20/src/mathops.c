/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:mathops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS mathematical operators
 */

#include "core.h"
#include "swerrors.h"
#include "swoften.h"
#include "often.h"

#include "objects.h"
#include "stacks.h"

/* ----------------------------------------------------------------------------
   function:            ceiling_()         author:              Andrew Cave
   creation date:       08-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 126.

---------------------------------------------------------------------------- */
Bool ceiling_(ps_context_t *pscontext)
{
  int32 ssize ;
  OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;

  if ( oType(*theo) == OREAL ) {
    int32 iarg ;
    SYSTEMVALUE arg ;

    arg = ( SYSTEMVALUE )oReal(*theo) ;
    if ( !intrange(arg) )
      return TRUE ;

    iarg = ( int32 )arg ;
    if ( iarg >= 0 && arg > 0.0 && ( SYSTEMVALUE )iarg - arg != 0.0 )
      ++iarg ;
    oReal(*theo) = ( USERVALUE )iarg ;
    theLen(*theo) = 0 ;
    return TRUE ;
  }
  else if ( oType(*theo) == OINTEGER ||
            oType(*theo) == OINFINITY )
    return TRUE ;
  else
    return error_handler( TYPECHECK ) ;
}

/* ----------------------------------------------------------------------------
   function:            myfloor_()         author:              Andrew Cave
   creation date:       08-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 157.

---------------------------------------------------------------------------- */
Bool myfloor_(ps_context_t *pscontext)
{
  int32 ssize ;
  OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;

  if ( oType(*theo) == OREAL ) {
    int32 iarg ;
    SYSTEMVALUE arg ;

    arg = ( SYSTEMVALUE )oReal(*theo) ;
    if ( !intrange(arg) )
      return TRUE ;

    iarg = ( int32 )arg ;
    if ( iarg <= 0 && arg < 0.0 && ( SYSTEMVALUE )iarg - arg != 0.0 )
      --iarg ;
    oReal(*theo) = ( USERVALUE )iarg ;
    theLen(*theo) = 0 ;
    return TRUE ;
  }
  else if ( oType(*theo) == OINTEGER ||
            oType(*theo) == OINFINITY )
    return TRUE ;
  else
    return error_handler( TYPECHECK ) ;
}

/* ----------------------------------------------------------------------------
   function:            round_()           author:              Andrew Cave
   creation date:       08-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 206.

---------------------------------------------------------------------------- */
Bool round_(ps_context_t *pscontext)
{
  int32 ssize ;
  OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;

  if ( oType(*theo) == OREAL ) {
    int32 iarg ;
    SYSTEMVALUE arg ;

    arg = ( SYSTEMVALUE )oReal(*theo) ;
    if ( !intrange(arg) )
      return TRUE ;

    iarg = ( int32 )( arg + 0.5 ) ;
    if ( iarg <= 0 && arg < -0.5 && ( SYSTEMVALUE )iarg - arg != 0.5 )
      --iarg ;
    oReal(*theo) = ( USERVALUE )iarg ;
    theLen(*theo) = 0 ;
    return TRUE ;
  }
  else if ( oType(*theo) == OINTEGER ||
            oType(*theo) == OINFINITY )
    return TRUE ;
  else
    return error_handler( TYPECHECK ) ;
}

/* ----------------------------------------------------------------------------
   function:            truncate_()        author:              Andrew Cave
   creation date:       08-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 234.

---------------------------------------------------------------------------- */
Bool truncate_(ps_context_t *pscontext)
{
  int32 ssize ;
  OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;

  if ( oType(*theo) == OREAL ) {
    int32 iarg ;
    SYSTEMVALUE arg ;

    arg = ( SYSTEMVALUE )oReal(*theo) ;
    if ( !intrange(arg) )
      return TRUE ;

    iarg = ( int32 )arg ;
    oReal(*theo) = ( USERVALUE )iarg ;
    theLen(*theo) = 0 ;
    return TRUE ;
  }
  else if ( oType(*theo) == OINTEGER ||
            oType(*theo) == OINFINITY )
    return TRUE ;
  else
    return error_handler( TYPECHECK ) ;
}

/* For the next set of operators see file 'mathfuncs.c' */



/* Log stripped */
