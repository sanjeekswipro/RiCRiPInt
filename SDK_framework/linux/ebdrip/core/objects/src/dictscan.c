/** \file
 * \ingroup objects
 *
 * $HopeName: COREobjects!src:dictscan.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Dictionary lookup and matching.
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "dictscan.h"
#include "namedef_.h"
#include "objimpl.h"

/** Perform lookup, type and access checking for just one object.
   return values:
   0 == worked
   NO_ERROR == end of list
   UNDEFINED == lookup failed
   TYPECHECK == typecheck error
   INVALIDACCESS == access failure
*/
static int32 internal_match(OBJECT *dict, NAMETYPEMATCH *onematch)
{
  register uint8 type , access , *match_ptr , count ;
  register OBJECT *res ;
  OBJECT name = OBJECT_NOTVM_NAME(0, LITERAL);

  if ( theISomeLeft( onematch )) {
    /* do the dict lookup, return UNDEFINED if that fails, otherwise
       stash the object in the result slot (keeping a copy), and carry
       on to the typechecking.  If lookup fails, but the lookup was
       optional, then carry on as if nothing happened. */
    oName(name) = theIMName(onematch);
    if ( (res = theIDictResult( onematch ) = fast_extract_hash(dict, &name))
         == NULL )
      return ( theIOptional( onematch ) ? 0 : UNDEFINED ) ;

    /* extract the type of the object and access permissions */
    type = CAST_UNSIGNED_TO_UINT8(oType(*res)) ;
    access = CAST_UNSIGNED_TO_UINT8(oAccess(*res)) ;

    /* return true if matches any of the types match */
    if ( theIMCount( onematch ) == 0 )
      return 0 ;                  /* Dont bother checking its type. */

    for (match_ptr = theIMatch( onematch ) , count = theIMCount( onematch ) ;
          count ; --count, ++match_ptr ) {
      if ( type == tagsType( *match_ptr )) {
        /* check access based on access provided, and superexec flag. */
        if ( access >= tagsAccess( *match_ptr ) ||
             object_access_override(res) )
          return 0 ;              /* we have sufficient access :-) */
        else
          return INVALIDACCESS ;  /* don't have enough access :-( */
      }
    }
    /* fell through type-checking so return the type-check error number */
    return TYPECHECK ;
  }

  return NOT_AN_ERROR ;
}

/** Perform the match operations, and deal with errors. */
Bool dictmatch(OBJECT *dict, NAMETYPEMATCH match_objects[])
{
  int32 error ;

  HQASSERT(object_asserts(), "Object system not initialised or corrupt") ;

  while ( 0 == ( error = internal_match( dict, match_objects )))
    ++match_objects ;

  /* if there was an error call the error handler */
  if ( error != NOT_AN_ERROR ) {
    OBJECT nullobj, keyobj ;

    theTags( nullobj ) = ONULL | LITERAL ;
    theTags( keyobj ) = ONAME | LITERAL ;
    oName( keyobj ) = theIMName( match_objects ) ;

    /* cause the correct errors to occur */
    return errorinfo_error_handler(error, &nullobj, &keyobj) ;
  }

  return TRUE ;
}

/*
Log stripped */
