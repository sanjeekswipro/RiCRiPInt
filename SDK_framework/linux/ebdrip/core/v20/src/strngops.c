/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:strngops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS string operators
 */

#include "core.h"
#include "objects.h"

#include "psvm.h"
#include "stacks.h"
#include "swerrors.h"
#include "hqmemcmp.h"

/* ----------------------------------------------------------------------------
   function:            string_()          author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none.
   description:

   See PostScript reference manual page 228.

---------------------------------------------------------------------------- */
Bool longstring_(ps_context_t *pscontext)
{
  int32 ssize ;
  OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;
  if ( oType(*theo) != OINTEGER )
    return error_handler( TYPECHECK ) ;
  ssize = oInteger(*theo) ;

  /* Setup string and initialise strings elements to zero. */
  return ps_longstring(theo, NULL, ssize) ;
}

Bool string_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;
  if ( oType(*theo) != OINTEGER )
    return error_handler( TYPECHECK ) ;
  ssize = oInteger(*theo) ;

/* Setup string and initialise strings elements to zero. */
  return ps_string(theo, NULL, ssize) ;
}

/* For the length_() operator see file 'tokenops.c' */
/* For the get_() & put_() operators see file 'copyops.c */
/* For the getinterval_() & putinterval_() operators see file 'tokenops.c' */
/* For the copy_() operators see file 'copyops.c */
/* For the forall_() operators see file 'execops.c */

/* ----------------------------------------------------------------------------
   function:            anchorsearch_()    author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none.
   description:

   See PostScript reference manual page 116.

---------------------------------------------------------------------------- */
Bool anchorsearch_(ps_context_t *pscontext)
{
  int32 stringsize , postsize , matchsize ;
  OBJECT *o1 , *o2 ;

  uint8 tags1 , tags2 ;
  uint8 *str , *seek ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

/*  Check types. */
  o1 = stackindex( 1 , & operandstack ) ;
  o2 = theTop( operandstack ) ;
  tags1 = theTags(*o1) ;
  tags2 = theTags(*o2) ;
  if ( oType(*o1) != OSTRING || oType(*o2) != OSTRING )
    return error_handler( TYPECHECK ) ;

/*  Check access requirements. */
  if ( (!oCanRead(*o1) && !object_access_override(o1)) ||
       (!oCanRead(*o2) && !object_access_override(o2)) )
    return error_handler( INVALIDACCESS ) ;

/* Extract the locations of seek & string, and their lengths.*/
  seek = oString(*o2) ;
  str = oString(*o1) ;
  matchsize = theLen(*o2) ;
  stringsize = theLen(*o1) ;

/*  Remove the trivial cases. */
  postsize = stringsize - matchsize ;
  if ( postsize < 0 ) {
    Copy(o2, &fnewobj) ;
    return TRUE ;
  }
  if ( matchsize == 0 ) {
    if ( ! push( & tnewobj , & operandstack ))
      return FALSE ;
    theTags(*o2) = tags1 ;
    SETGLOBJECTTO(*o2, oGlobalValue(*o1));
    return TRUE ;
  }
/*
  Compare initial sub-string of string (of length match_size) with seek.
  If initial sub-string matches, then setup post & match, and boolean true.
*/
  if ( HqMemCmp( str , matchsize , seek , matchsize) == 0 ) {
    if ( ! push( & tnewobj , & operandstack ))
      return FALSE ;
    theTags(*o2) = tags1 ;
    theLen(*o2) = ( uint16 )matchsize ;
    oString(*o2) = str ;
    SETGLOBJECTTO(*o2, oGlobalValue(*o1));

    theLen(*o1) = ( uint16 )postsize ;
    oString(*o1) = ( postsize != 0 ? str + matchsize : NULL ) ;
    return TRUE ;
  }
/*  Else no match occurred, so push boolean false. */
  Copy(o2, &fnewobj) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            search_()          author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none.
   description:

   See PostScript reference manual page 211.

---------------------------------------------------------------------------- */
Bool search_(ps_context_t *pscontext)
{
  int32  postsize , matchsize , presize ;
  OBJECT *o1 , *o2 ;
  uint8  *str ;

  uint8  tags1 , tags2 ;
  uint8  *seek ;
  int32  stringsize ;
  OBJECT newobject = OBJECT_NOTVM_NOTHING ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

/*  Check types. */
  o1 = stackindex( 1 , & operandstack ) ;
  o2 = theTop( operandstack ) ;
  tags1 = theTags(*o1) ;
  tags2 = theTags(*o2) ;
  if ( oType(*o1) != OSTRING || oType(*o2) != OSTRING )
    return error_handler( TYPECHECK ) ;

/*  Check access requirements. */
  if ( (!oCanRead(*o1) && !object_access_override(o1)) ||
       (!oCanRead(*o2) && !object_access_override(o2)) )
    return error_handler( INVALIDACCESS ) ;

/*  Extract the location of seek & string, and their lengths. */
  seek = oString(*o2) ;
  str = oString(*o1) ;
  matchsize = theLen(*o2) ;
  stringsize = theLen(*o1) ;

/*  Remove the trivial cases. */
  if ( stringsize == 0 ) {
    Copy(o2, &fnewobj) ;
    return TRUE ;
  }
  if ( matchsize == 0 ) {
    theTags( newobject ) = tags1 ;
    theLen( newobject )  = 0 ;
    oString( newobject ) = NULL ;
    SETGLOBJECTTO(newobject, oGlobalValue(*o1));

    if ( ! push2(&newobject, &tnewobj, &operandstack) )
      return FALSE ;

    theTags(*o2) = tags1 ;
    SETGLOBJECTTO(*o2, oGlobalValue(*o1));
    return TRUE ;
  }
  postsize = stringsize - matchsize ;
  presize = 0 ;
/*
  Compare sub-strings of string (of length match_size) with seek,
  until no sub-string left that is that long, or have found match.
*/
  while ( postsize >= 0 ) {

/*  If matched up sub-string, then setup post, match & pre. */
    if ( ! HqMemCmp( & str[ presize ] , matchsize , seek , matchsize )) {
      theTags( newobject ) = tags1 ;
      theLen( newobject )  = ( uint16 )presize ;
      oString( newobject ) = ( presize != 0 ? str : NULL ) ;
      SETGLOBJECTTO(newobject, oGlobalValue(*o1));

      if ( ! push2(&newobject, &tnewobj, &operandstack) )
        return FALSE ;

      theTags(*o2) = tags1 ;
      theLen(*o2) = ( uint16 )matchsize ;
      oString(*o2) = str + presize ;
      SETGLOBJECTTO(*o2, oGlobalValue(*o1));

      theLen(*o1) = ( uint16 )postsize ;
      oString(*o1) = ( postsize != 0 ? str + presize + matchsize : NULL ) ;
      return TRUE ;
    }
/* Otherwise look at next sub-string. */
    ++presize ;
    --postsize ;
  }
/*  No match occurred, so push the boolean false. */
  Copy(o2, &fnewobj) ;
  return TRUE ;
}

/* For the token_() operator see file 'shared2ops.c */



/* Log stripped */
