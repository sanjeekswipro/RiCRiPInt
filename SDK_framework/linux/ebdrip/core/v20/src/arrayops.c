/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:arrayops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS array operators
 */

#include "core.h"
#include "swerrors.h"
#include "mm.h"
#include "mmcompat.h"
#include "objects.h"
#include "fileio.h"

#include "psvm.h"
#include "pscontext.h"
#include "stacks.h"
#include "graphics.h"
#include "stackops.h"   /* extern for num_to_mark() */
#include "utils.h"      /* extern for get1B()       */
#include "swmemory.h"   /* extern for get_omemory() + */

/* ----------------------------------------------------------------------------
   function:            array_()           author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   23-Jan-1995
   arguments:           none .
   description:

   Modifications:
   23-Jan-1995   D. Strauss  If arraysize is 0, set olist to NULL instead
                             of calling get_omemory().

   See PostScript reference manual page 120.

---------------------------------------------------------------------------- */
Bool array_(ps_context_t *pscontext)
{
  register int32 arraysize ;
  register OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  arraysize = theStackSize( operandstack ) ;
  if ( EmptyStack( arraysize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , arraysize ) ;
  if ( oType(*theo) != OINTEGER )
    return error_handler( TYPECHECK ) ;
  arraysize = oInteger(*theo) ;

  /* Setup the array. */
  if (arraysize <= MAXPSARRAY)
    return ps_array(theo, arraysize) ;

#if defined( DEBUG_BUILD )
  /* Large arrays can only be created in DEBUG builds for now */
  {
    OBJECT * obj = get_omemory(arraysize) ;
    if (obj == NULL)
      return error_handler(VMERROR) ;

    obj = extended_array(obj, arraysize) ;
    if (obj == NULL)
      return error_handler(VMERROR) ;

    theTags(*theo) = OEXTENDED | LITERAL | UNLIMITED ;
    SETGLOBJECT(*theo, pscontext->corecontext) ;
    theLen(*theo) = OLONGARRAY ;
    oArray(*theo) = obj ;
  }
  return TRUE ;
#else
  return error_handler(RANGECHECK) ;
#endif
}


/* For the [ operator see mark_() operator in file 'setup.c' */

/* ----------------------------------------------------------------------------
   function:            endmark_()         author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   23-Jan-1995
   arguments:           none .
   description:

   Modifications:
   23-Jan-1995   D. Strauss  If arraysize is 0, set olist to NULL instead
                             of calling get_omemory().

   See PostScript reference manual page 113.

---------------------------------------------------------------------------- */
Bool endmark_(ps_context_t *pscontext)
{
  register int32 i ;
  register int32 arraysize ;
  register OBJECT *olist , *omin , *tempo ;
  corecontext_t *corecontext = pscontext->corecontext ;

/* Check for mark on stack, and count the number of objects to the mark. */
  arraysize = num_to_mark() ;
  if ( arraysize < 0 )
    return error_handler( UNMATCHEDMARK ) ;
  if ( arraysize > MAXPSARRAY )
    return error_handler( LIMITCHECK ) ;

/* Check OBJECTS for illegal LOCAL --> GLOBAL */
  if ( corecontext->glallocmode )
    for ( i = 0 ; i < arraysize ; ++i ) {
      tempo = stackindex( i , & operandstack ) ;
      if ( illegalLocalIntoGlobal(tempo, corecontext) )
        return error_handler( INVALIDACCESS ) ;
    }

  if (arraysize == 0) {
    olist = NULL;
  } else if ( NULL == (olist = get_omemory(arraysize)) ) {
    return error_handler(VMERROR) ;
  }

/*  Replace the mark on the stack by the array. */
  omin = stackindex( arraysize , & operandstack ) ;
  theTags(*omin)= OARRAY | LITERAL | UNLIMITED ;
  SETGLOBJECT(*omin, corecontext) ;
  theLen(*omin)= (uint16) arraysize ;
  oArray(*omin) = olist ;

/* Pop the elements off the top of the stack into the array's list. */
  if (olist) {
    omin = olist ;
    for ( olist += ( arraysize - 1 ) ; olist >= omin ; --olist ) {
      tempo = theTop( operandstack ) ;
      Copy( olist , tempo ) ;
      pop( & operandstack ) ;
    }
  }
  return TRUE ;
}

/* For the length_() operator see file 'shared2ops.c' */
/* For the get_() & put_() operators see file 'shared1ops.c' */
/* For the getinterval_() & putinterval_() operators see file 'shared2ops.c' */

/* ----------------------------------------------------------------------------
   function:            aload_()           author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none.
   description:

   See PostScript reference manual page 115.

---------------------------------------------------------------------------- */
Bool aload_(ps_context_t *pscontext)
{
  OBJECT otemp = OBJECT_NOTVM_NOTHING ;

  register OBJECT *olist = ( & otemp ) , *omax ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  omax = theTop( operandstack ) ;
  Copy( olist , omax ) ;
  switch ( oXType(*olist) ) {
  case OARRAY :
  case OPACKEDARRAY :
    break ;
  case OLONGARRAY :
  case OLONGPACKEDARRAY :
    HQFAIL("Cannot aload a long array") ;
    /* drop through */
  default:
    return error_handler( TYPECHECK ) ;
  }

  if ( ! oCanRead(*olist) && !object_access_override(olist) )
    return error_handler( INVALIDACCESS ) ;

  pop( & operandstack ) ;

/* Extract list of arrays elements and push them onto the operand stack. */
  olist = oArray(*olist) ;
  for ( omax = olist + (int32)theLen( otemp ) ; olist < omax ; ++olist )
    if ( ! push( olist , & operandstack ))
      return FALSE ;

/*  Replace the array on the stack. */
  return push( & otemp , & operandstack ) ;
}

/* ----------------------------------------------------------------------------
   function:            astore_()          author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 121.

---------------------------------------------------------------------------- */
Bool astore_(ps_context_t *pscontext)
{
  OBJECT otemp = OBJECT_NOTVM_NOTHING ;

  register int32 i ;
  register int32 arraysize , stacksize ;
  register OBJECT *theo = ( & otemp ) ;
  register OBJECT *olist , *omin , *tempo ;
  corecontext_t *corecontext = pscontext->corecontext ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  tempo = theTop( operandstack ) ;
  Copy( theo , tempo ) ;
  switch ( oType(*theo) ) {
  case OARRAY :
  case OPACKEDARRAY :
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }

  if ( !oCanWrite(*theo) && !object_access_override(theo) )
    return error_handler( INVALIDACCESS ) ;

  arraysize = (int32) theLen(*theo);
  stacksize = theStackSize( operandstack ) ;
  if ( stacksize < arraysize )
    return error_handler( STACKUNDERFLOW ) ;

  if ( ! arraysize )
    return TRUE ;

/* Check OBJECTS for illegal LOCAL --> GLOBAL */
  if ( oGlobalValue(*theo))
    for ( i = 1 ; i <= arraysize ; ++i ) {
      tempo = stackindex( i , & operandstack ) ;
      if ( illegalLocalIntoGlobal(tempo, corecontext) )
        return error_handler( INVALIDACCESS ) ;
    }

  pop( & operandstack ) ;

/*  Obtain location of arrays list of objects. */
  omin = oArray(*theo) ;

/* Check if array has been saved. */
  if ( ! check_asave(omin, arraysize, oGlobalValue(*theo), corecontext) )
    return FALSE ;

/* Pop the elements off the top of the stack into the array's list. */
  for ( olist = omin + ( arraysize - 1 ) ; olist >= omin ; --olist ) {
    tempo = theTop( operandstack ) ;
    Copy(olist, tempo) ;
    pop( & operandstack ) ;
  }
/*  Replace the array on the stack. */
  return push( & otemp , & operandstack ) ;
}

/* For the copy_() operator see file 'shared1ops.c' */
/* For the forall_() operator see file 'execops.c'  */

/* ----------------------------------------------------------------------------
   function:            setpacking_()      author:              Andrew Cave
   creation date:       03-Jan-1989        last modification:   ##-###-####
   arguments:           none .
   description:

   See LaserWriter Reference page 110.

---------------------------------------------------------------------------- */
Bool setpacking_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return get1B( & theIPacking( workingsave )) ;
}

/* ----------------------------------------------------------------------------
   function:            currpacking_()     author:              Andrew Cave
   creation date:       03-Jan-1989        last modification:   ##-###-####
   arguments:           none .
   description:

   See LaserWriter Reference page 111.

---------------------------------------------------------------------------- */
Bool currpacking_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push(
              theIPacking( workingsave ) ? & tnewobj : & fnewobj ,
              & operandstack ) ;
}

/* ----------------------------------------------------------------------------
   function:            pacjedarray_()     author:              Andrew Cave
   creation date:       04-Jan-1989        last modification:   ##-###-####
   arguments:           none .
   description:

   See LaserWriter Reference manual page 111.

---------------------------------------------------------------------------- */
Bool packedarray_(ps_context_t *pscontext)
{
  register int32 i ;
  register int32 arraysize , stacksize ;
  register OBJECT *olist , *omin , *tempo ;
  OBJECT newobject = OBJECT_NOTVM_NOTHING ;
  corecontext_t *corecontext = pscontext->corecontext ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  tempo = theTop( operandstack ) ;
  if (oType(*tempo) != OINTEGER )
    return error_handler( TYPECHECK ) ;

  arraysize = oInteger(*tempo) ;
  if ( arraysize < 0 )
    return error_handler( UNMATCHEDMARK ) ;
  if ( arraysize > MAXPSARRAY )
    return error_handler( LIMITCHECK ) ;

  stacksize = theStackSize( operandstack ) ;
  if ( stacksize < arraysize )
    return error_handler( STACKUNDERFLOW ) ;

/* Check OBJECTS for illegal LOCAL --> GLOBAL */
  if ( corecontext->glallocmode )
    for ( i = 0 ; i < arraysize ; ++i ) {
      tempo = stackindex( i , & operandstack ) ;
      if ( illegalLocalIntoGlobal(tempo, corecontext) )
        return error_handler( INVALIDACCESS ) ;
    }

  if (arraysize == 0)
    olist = NULL;
  else if ( NULL == (olist = get_omemory(arraysize)) )
    return error_handler(VMERROR) ;

/*  Replace the mark on the stack by the array. */
  theTags( newobject ) = OPACKEDARRAY | LITERAL | READ_ONLY ;
  SETGLOBJECT(newobject, corecontext) ;
  theLen( newobject ) = (uint16) arraysize ;
  oArray( newobject ) = olist ;

/* Pop the elements off the top of the stack into the array's list. */
  pop( & operandstack ) ;
  if (olist != NULL) {
    omin = olist ;
    for ( olist += ( arraysize - 1 ) ; olist >= omin ; --olist ) {
      tempo = theTop( operandstack ) ;
      Copy( olist , tempo ) ;
      pop( & operandstack ) ;
    }
  }
  return push( & newobject , & operandstack ) ;
}



/* Log stripped */
