/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:arithops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS arithmetic operators
 */

#include "core.h"
#include "swerrors.h"
#include "swoften.h"
#include "often.h"

#include "constant.h"
#include "objects.h"
#include "params.h"
#include "psvm.h"
#include "stacks.h"

#include "namedef_.h"

/* ----------------------------------------------------------------------------
   function:            add_()             author:              Andrew Cave
   creation date:       08-Oct-1987        last modification:   06-Feb-1987
   arguments:           none .
   description:

   See PostScript reference manual page 115.

---------------------------------------------------------------------------- */
Bool add_(ps_context_t *pscontext)
{
  int32 ssize ;
  int32 type ;
  OBJECT *o1 , *o2 ;
  SYSTEMVALUE aa1 = 0 ; /* shut compiler up */

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( ssize < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = TopStack( operandstack , ssize ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  type = oType(*o1) ;
  if ( type == OINTEGER ) {
    int32 num1 = oInteger(*o1) ;

    type = oType(*o2) ;
    if ( type == OINTEGER ) {
      int32 num2 = oInteger(*o2) ;
      int32 res = num1 + num2 ;
      if ((( num1 ^ num2 ) < 0 ) || (( res ^ num1 ) >= 0 )) {
        oInteger(*o1) = res ;
        pop( & operandstack ) ;
        return TRUE ;
      }
      aa1 = ( SYSTEMVALUE )num1 + ( SYSTEMVALUE )num2 ;
      if ( intrange( aa1 ) ) {
        oInteger(*o1) = ( int32 )aa1 ;
        pop( & operandstack ) ;
        return TRUE ;
      }
    }
    else if ( type == OREAL ) {
      SYSTEMVALUE aa2 ;
      if (object_get_XPF(o2, &aa2) == XPF_INVALID)
        return error_handler( TYPECHECK ) ;  /* malformed XPF */
      aa1 = ( SYSTEMVALUE )num1 + aa2 ;
    }
    else if ( type == OINFINITY )
      aa1 = OINFINITY_VALUE ;
    else
      return error_handler( TYPECHECK ) ;
  }
  else {
    if ( type == OREAL ) {
      if (object_get_XPF(o1, &aa1) == XPF_INVALID)
        return error_handler( TYPECHECK ) ;
    } else if ( type == OINFINITY )
      aa1 = OINFINITY_VALUE ;
    else
      return error_handler( TYPECHECK ) ;

    type = oType(*o2) ;
    if ( type == OREAL ) {
      SYSTEMVALUE aa2 ;
      if (object_get_XPF(o2, &aa2) == XPF_INVALID)
        return error_handler( TYPECHECK ) ;
      aa1 += aa2 ;
    } else if ( type == OINTEGER )
      aa1 += ( SYSTEMVALUE )oInteger(*o2) ;
    else if ( type == OINFINITY )
      aa1 = OINFINITY_VALUE ;
    else
      return error_handler( TYPECHECK ) ;
  }

  pop( & operandstack ) ;

  object_store_XPF(o1, aa1) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            div_()             author:              Andrew Cave
   creation date:       08-Oct-1987        last modification:   06-Feb-1987
   arguments:           none .
   description:

   See PostScript reference manaul page 148.

---------------------------------------------------------------------------- */
Bool div_(ps_context_t *pscontext)
{
  int32 ssize ;
  OBJECT *o1 , *o2 ;
  SYSTEMVALUE aa1 , aa2 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( ssize < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = TopStack( operandstack , ssize ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  if ( !object_get_numeric(o1, &aa1) ||
       !object_get_numeric(o2, &aa2) )
    return FALSE ;

  if ( aa2 == 0.0 )
    return error_handler( UNDEFINEDRESULT ) ;

  aa1 = aa1 / aa2 ;

  pop( & operandstack ) ;

  theLen(*o1) = 0 ;
  if ( realrange( aa1 )) {
    if ( ! realprecision( aa1 ))
      aa1 = 0.0 ;
    theTags(*o1) = OREAL | LITERAL ;
    oReal(*o1) = ( USERVALUE )aa1 ;
  }
  else
    theTags(*o1) = OINFINITY | LITERAL ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            idiv_()            author:              Andrew Cave
   creation date:       08-Oct-1987        last modification:   06-Feb-1987
   arguments:           none .
   description:

   See PostScript reference manual page 168.

---------------------------------------------------------------------------- */
Bool idiv_(ps_context_t *pscontext)
{
  int32 ssize ;
  int32 type ;
  OBJECT *o1 , *o2 ;
  SYSTEMVALUE aa1 , aa2 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( ssize < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = TopStack( operandstack , ssize ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  type = oType(*o1) ;
  if ( type == OINTEGER ) {
    int32 num1 = oInteger(*o1) ;

    type = oType(*o2) ;
    if ( type == OINTEGER ) {
      int32 num2 = oInteger(*o2) ;
      if ( num2 == 0 || (num1 == MININT32 && num2 == -1))
        return error_handler( UNDEFINEDRESULT ) ;

      oInteger(*o1) = num1 / num2 ;
      pop( & operandstack ) ;
      return TRUE ;
    }
    else {
      if ( theISaveLangLevel( workingsave ) >= 2 )
        return error_handler( TYPECHECK ) ;

      aa1 = ( SYSTEMVALUE )num1 ;
      if ( type == OREAL )
        aa2 = ( SYSTEMVALUE )oReal(*o2) ;
      else if ( type == OINFINITY )
        aa2 = OINFINITY_VALUE ;
      else
        return error_handler( TYPECHECK ) ;
    }
  }
  else {
    if ( theISaveLangLevel( workingsave ) >= 2 )
      return error_handler( TYPECHECK ) ;

    if ( type == OREAL )
      aa1 = ( SYSTEMVALUE )oReal(*o1) ;
    else if ( type == OINFINITY )
      aa1 = OINFINITY_VALUE ;
    else
      return error_handler( TYPECHECK ) ;

    if ( !object_get_numeric(o2, &aa2) )
      return FALSE ;
  }

  if ( aa2 == 0.0 )
    return error_handler( UNDEFINEDRESULT ) ;

  aa1 = aa1 / aa2 ;
  if ( aa1 >= BIGGEST_INTEGER )
    aa1 = BIGGEST_INTEGER - 1.0 ;
  else if ( aa1 < ( -BIGGEST_INTEGER ))
    aa1 = -BIGGEST_INTEGER ;

  pop( & operandstack ) ;

  theLen(*o1) = 0 ;
  theTags(*o1) = OINTEGER | LITERAL ;
  oInteger(*o1) = ( int32 )aa1 ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            mod_()             author:             Andrew Cave
   creation date:       08-Oct-1987        last modification:   06-Feb-1987
   arguments:           none .
   description:

   See PostScript reference manual page 185.

---------------------------------------------------------------------------- */
Bool mod_(ps_context_t *pscontext)
{
  int32 ssize ;
  OBJECT *o1 , *o2 ;
  int32 t0 , t1 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( ssize < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = TopStack( operandstack , ssize ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  if ( oType(*o1) != OINTEGER )
    return error_handler( TYPECHECK ) ;
  t0 = oInteger(*o1) ;

  if ( oType(*o2) != OINTEGER )
    return error_handler( TYPECHECK ) ;
  t1 = oInteger(*o2) ;

  if ( ! t1 )
    return error_handler( UNDEFINEDRESULT ) ;

  t0 = t0 % t1 ;

  oInteger(*o1) = t0 ;
  pop( & operandstack ) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            mul_()             author:              Andrew Cave
   creation date:       08-Oct-1987        last modification:   06-Feb-1987
   arguments:           none .
   description:

   See PostScript reference manual page 186.

---------------------------------------------------------------------------- */
Bool mul_(ps_context_t *pscontext)
{
  int32 ssize ;
  int32 type ;
  OBJECT *o1 , *o2 ;
  SYSTEMVALUE aa1 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( ssize < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = TopStack( operandstack , ssize ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  type = oType(*o1) ;
  if ( type == OINTEGER ) {
    type = oType(*o2) ;
    if ( type == OINTEGER ) {
      aa1 = ( SYSTEMVALUE )oInteger(*o1) *
            ( SYSTEMVALUE )oInteger(*o2) ;
      if ( intrange( aa1 ) ) {
        oInteger(*o1) = ( int32 )aa1 ;
        pop( & operandstack ) ;
        return TRUE ;
      }
    }
    else if ( type == OREAL ) {
      aa1 = ( SYSTEMVALUE )oInteger(*o1) *
            ( SYSTEMVALUE )oReal(*o2) ;
    }
    else if ( type == OINFINITY )
      aa1 = ( SYSTEMVALUE )oInteger(*o1) *
            OINFINITY_VALUE ;
    else
      return error_handler( TYPECHECK ) ;
  }
  else {
    if ( type == OREAL )
      aa1 = ( SYSTEMVALUE )oReal(*o1) ;
    else if ( type == OINFINITY )
      aa1 = OINFINITY_VALUE ;
    else
      return error_handler( TYPECHECK ) ;

    type = oType(*o2) ;
    if ( type == OREAL )
      aa1 *= ( SYSTEMVALUE )oReal(*o2) ;
    else if ( type == OINTEGER )
      aa1 *= ( SYSTEMVALUE )oInteger(*o2) ;
    else if ( type == OINFINITY )
      aa1 *= OINFINITY_VALUE ;
    else
      return error_handler( TYPECHECK ) ;
  }

  pop( & operandstack ) ;

  theLen(*o1) = 0 ;
  if ( realrange( aa1 )) {
    if ( ! realprecision( aa1 ))
      aa1 = 0.0 ;
    theTags(*o1) = OREAL | LITERAL ;
    oReal(*o1) = ( USERVALUE )aa1 ;
  }
  else
    theTags(*o1) = OINFINITY | LITERAL ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            sub_()             author:              Andrew Cave
   creation date:       08-Oct-1987        last modification:   06-Feb-1987
   arguments:           none .
   description:

   See PostScript reference manual page 230.

---------------------------------------------------------------------------- */
Bool sub_(ps_context_t *pscontext)
{
  int32 ssize ;
  int32 type ;
  OBJECT *o1 , *o2 ;
  SYSTEMVALUE aa1 = 0 ; /* shut compiler up */

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( ssize < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = TopStack( operandstack , ssize ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  type = oType(*o1) ;
  if ( type == OINTEGER ) {
    int32 num1 = oInteger(*o1) ;

    type = oType(*o2) ;
    if ( type == OINTEGER ) {
      int32 num2 = oInteger(*o2) ;
      int32 res = num1 - num2 ;
      if ((( num1 ^ (-num2) ) < 0 ) || (( res ^ num1 ) >= 0 )) {
        oInteger(*o1) = res ;
        pop( & operandstack ) ;
        return TRUE ;
      }
      aa1 = ( SYSTEMVALUE )num1 - ( SYSTEMVALUE )num2 ;
      if ( intrange( aa1 ) ) {
        oInteger(*o1) = ( int32 )aa1 ;
        pop( & operandstack ) ;
        return TRUE ;
      }
    }
    else if ( type == OREAL ) {
      SYSTEMVALUE aa2 ;
      if (object_get_XPF(o2, &aa2) == XPF_INVALID)
        return error_handler( TYPECHECK ) ;  /* error if malformed XPF */
      aa1 = ( SYSTEMVALUE )num1 - aa2 ;
    }
    else if ( type == OINFINITY )
      aa1 = OINFINITY_VALUE ;
    else
      return error_handler( TYPECHECK ) ;
  }
  else {
    if ( type == OREAL ) {
      if (object_get_XPF(o1, &aa1) == XPF_INVALID)
        return error_handler( TYPECHECK ) ;  /* error if malformed XPF */
    } else if ( type == OINFINITY )
      aa1 = OINFINITY_VALUE ;
    else
      return error_handler( TYPECHECK ) ;

    type = oType(*o2) ;
    if ( type == OREAL ) {
      SYSTEMVALUE aa2 ;
      if (object_get_XPF(o2, &aa2) == XPF_INVALID)
        return error_handler( TYPECHECK ) ;
      aa1 -= aa2 ;
    } else if ( type == OINTEGER )
      aa1 -= ( SYSTEMVALUE )oInteger(*o2) ;
    else if ( type == OINFINITY )
      aa1 = OINFINITY_VALUE ;
    else
      return error_handler( TYPECHECK ) ;
  }

  pop( & operandstack ) ;

  object_store_XPF(o1, aa1) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            abs_()             author:              Andrew Cave
   creation date:       08-Oct-1987        last modification:   06-Feb-1987
   arguments:           none .
   description:

   See PostScript reference manual page 115.

---------------------------------------------------------------------------- */
Bool abs_(ps_context_t *pscontext)
{
  int32 ssize ;
  int32 type ;
  OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;
  type = oType(*theo) ;

  if ( type == OREAL ) {
    USERVALUE tmp = oReal(*theo) ;
    if ( tmp < 0.0f )
      oReal(*theo) = -tmp ;
    return TRUE ;
  }
  else if ( type == OINTEGER ) {
    int32 tmp = oInteger(*theo) ;
    if ( tmp < 0 ) {
      if ( tmp == -tmp ) {
        theTags(*theo) = OREAL | LITERAL ;
        oReal(*theo) = ( USERVALUE )BIGGEST_INTEGER ;
      }
      else {
        oInteger(*theo) = -tmp ;
      }
    }
    return TRUE ;
  }
  else if ( type == OINFINITY )
    return TRUE ;
  else
    return error_handler( TYPECHECK ) ;
}

/* ----------------------------------------------------------------------------
   function:            neg_()             author:              Andrew Cave
   creation date:       08-Oct-1987        last modification:   06-Feb-1987
   arguments:           none .
   description:

   See PostScript reference manual page 187.

---------------------------------------------------------------------------- */
Bool neg_(ps_context_t *pscontext)
{
  int32 ssize ;
  int32 type ;
  OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;
  type = oType(*theo) ;

  if ( type == OREAL ) {
    USERVALUE tmp = oReal(*theo) ;
    theLen(*theo) = 0 ;
    oReal(*theo) = -tmp ;
    return TRUE ;
  }
  else if ( type == OINTEGER ) {
    int32 tmp = oInteger(*theo) ;
    if ( tmp == -tmp && tmp != 0 ) {
      theTags(*theo) = OREAL | LITERAL ;
      oReal(*theo) = ( USERVALUE )BIGGEST_INTEGER ;
      theLen(*theo) = 0 ;
    }
    else {
      oInteger(*theo) = -tmp ;
    }
    return TRUE ;
  }
  else if ( type == OINFINITY )
    return TRUE ;
  else
    return error_handler( TYPECHECK ) ;
}

/* For the next set of operators see file 'mathops.c' */













/* Log stripped */
