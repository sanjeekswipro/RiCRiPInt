/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:userobjs.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS user objects
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "namedef_.h"
#include "mm.h"
#include "mmcompat.h"

#include "bitblts.h"
#include "matrix.h"
#include "miscops.h"
#include "stacks.h"
#include "dicthash.h"
#include "display.h"
#include "graphics.h"
#include "swmemory.h"
#include "execops.h"
#include "pscontext.h"

#define USEROBJECTS_INITIAL_SIZE           50
#define USEROBJECTS_PERCENTAGE_OVERGROWTH 150

/* ----------------------------------------------------------------------------
   function:            get_userobjects   author:              John Sturdy
   creation date:       02-Jul-1991       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
static OBJECT *get_userobjects_array( int32 indexrequired )
{
  int32 asize ;
  int32 copylen ;
  OBJECT *userobj ;
  OBJECT *newuserobj ;
  OBJECT *userobjs ;
  OBJECT *newuserobjs ;

  static OBJECT userobject ;

  oName( nnewobj ) = system_names + NAME_UserObjects ;

  if (( userobjs = fast_user_extract_hash( & nnewobj )) != NULL ) {

    if ( oType( *userobjs ) != OARRAY ) {
      ( void )error_handler( TYPECHECK ) ;
      return NULL ;
    }
    if ( oGlobalValue(*userobjs) ) {
      ( void )error_handler( INVALIDACCESS ) ;
      return NULL ;
    }

    if ( indexrequired < 0 ||
         indexrequired < theLen(*userobjs) )
      return ( userobjs ) ;
  }
  else if ( indexrequired < 0 )
    return NULL ;

  asize = indexrequired ;
  if ( asize < USEROBJECTS_INITIAL_SIZE )
    asize = USEROBJECTS_INITIAL_SIZE ;
  asize = ( asize * USEROBJECTS_PERCENTAGE_OVERGROWTH ) / 100 ;
  if ( asize > MAXPSARRAY )
    asize = MAXPSARRAY ;

  newuserobjs = ( & userobject ) ;

  if (( newuserobj = ( OBJECT * )get_lomemory( asize )) == NULL ) {
    ( void )error_handler( VMERROR ) ;
    return NULL ;
  }

  theTags(*newuserobjs) = OARRAY | LITERAL | UNLIMITED ;
  SETGLOBJECTTO(*newuserobjs, FALSE) ;
  theLen(*newuserobjs) = (uint16)asize ;
  oArray( *newuserobjs ) = newuserobj ;

  if ( userobjs ) {
    copylen = (int32)theLen(*userobjs) ;
    asize -= copylen ;
    userobj = oArray( *userobjs ) ;
    while ((--copylen) >= 0 ) {
      Copy( newuserobj , userobj ) ;
      ++userobj ;
      ++newuserobj ;
    }
  }
  while ((--asize) >= 0 ) {
    theTags(*newuserobj) = ONULL | LITERAL ;
    ++newuserobj ;
  }

  if ( ! insert_hash( & userdict , & nnewobj , newuserobjs ))
    return NULL ;

  return ( newuserobjs ) ;
}

/* ----------------------------------------------------------------------------
   function:            defineuserobject_ author:              John Sturdy
   creation date:       02-Jul-1991       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool defineuserobject_(ps_context_t *pscontext)
{
  int32  indexno ;
  OBJECT *value ;
  OBJECT *index ;
  OBJECT *userobj ;
  OBJECT *userobjs ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  value = theTop( operandstack ) ;
  index = ( & value[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    index = stackindex( 1 , & operandstack ) ;

  if ( oType( *index ) != OINTEGER )
    return error_handler( TYPECHECK ) ;

  indexno = oInteger( *index ) ;
  if ( indexno < 0 )
    return error_handler( RANGECHECK ) ;
  if ( indexno >= MAXPSARRAY )
    return error_handler( LIMITCHECK ) ;

  if (( userobjs = get_userobjects_array( indexno )) == NULL )
    return FALSE ;         /* Should have already called error_handler */

  if ( ! oCanWrite(*userobjs) && !object_access_override(userobjs) )
    return error_handler( INVALIDACCESS ) ;

  userobj = oArray( *userobjs ) ;
  if (!check_asave(userobj, theLen(*userobjs), FALSE, pscontext->corecontext))
    return FALSE ;

  userobj += indexno ;
  Copy( userobj , value ) ;

  npop( 2 , & operandstack ) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            undefineuserobject_ author:              John Sturdy
   creation date:       02-Jul-1991       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool undefineuserobject_(ps_context_t *pscontext)
{
  int32  len ;
  int32  indexno ;
  OBJECT *index ;
  OBJECT *userobj ;
  OBJECT *userobjs ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  index = theTop( operandstack ) ;
  if ( oType( *index ) != OINTEGER )
    return error_handler( TYPECHECK ) ;

  indexno = oInteger( *index ) ;

  if (( userobjs = get_userobjects_array( -1 )) != NULL ) {
    if ( ! oCanWrite(*userobjs) && !object_access_override(userobjs) )
      return error_handler( INVALIDACCESS ) ;

    len = (int32)theLen(*userobjs) ;
    if ( indexno < 0 || indexno >= len )
      return error_handler( RANGECHECK ) ;

    userobj = oArray( *userobjs ) ;
    if ( ! check_asave(userobj, len, FALSE, pscontext->corecontext))
      return FALSE ;

    userobj += indexno ;
    theTags(*userobj) = ONULL | LITERAL ;
  }

  pop( & operandstack ) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            execuserobject_   author:              John Sturdy
   creation date:       02-Jul-1991       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool execuserobject_(ps_context_t *pscontext)
{
  int32  indexno ;
  OBJECT *index ;
  OBJECT *userobj ;
  OBJECT *userobjs ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  index = theTop( operandstack ) ;
  if ( oType( *index ) != OINTEGER )
    return error_handler( TYPECHECK ) ;

  indexno = oInteger( *index ) ;

  if ( NULL == (userobjs = get_userobjects_array( -1 )) )
    return error_handler( UNDEFINED ) ;

  if ( ! oCanRead(*userobjs) && !object_access_override(userobjs) )
    return error_handler( INVALIDACCESS ) ;

  if ( indexno < 0 || indexno >= theLen(*userobjs) )
    return error_handler( RANGECHECK ) ;

  userobj = oArray( *userobjs ) + indexno ;

  pop( & operandstack ) ;

  return setup_pending_exec( userobj , FALSE ) ;
}


/* Log stripped */
