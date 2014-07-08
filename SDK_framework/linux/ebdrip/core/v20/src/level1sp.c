/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:level1sp.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Conversion of crucial routines in Level1Separator procset to C.
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "stacks.h"
#include "control.h"
#include "miscops.h"
#include "dicthash.h"
#include "fileio.h"
#include "routedev.h"           /* DEVICE_INVALID_CONTEXT */
#include "swmemory.h"

#include "graphics.h"
#include "gstate.h"
#include "gs_color.h"
#include "gschead.h"
#include "params.h"

#include "namedef_.h"

#include "stackops.h"
#include "gscdevci.h"

/* ---------------------------------------------------------------------- */
/* The following routine implements:
 * /setcmykoverprint 1 dict def
 * /setcmykoverprint {   % [ c m y k -> -- ]
 *   //setcmykoverprint begin
 *     save
 *       currentuserparams /OverprintProcess 2 copy known
 *       { get } { pop pop true } ifelse exch
 *     restore
 *     {
 *       /OverprintProcess false def currentdict setuserparams
 *       setcmykcolor
 *       /OverprintProcess true def currentdict setuserparams
 *    }{
 *       setcmykcolor
 *     } ifelse
 *  end
 * } bind def
 *
 * See See SW:procsets:Level1Separator for details.
 */
Bool l1setcmykoverprint_(ps_context_t *pscontext)
{
  Bool result ;
  Bool overprintprocess ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( DEVICE_INVALID_CONTEXT())
    return error_handler( UNDEFINED ) ;

  overprintprocess = gsc_getoverprintmode(gstateptr->colorInfo) ;
  result = gsc_setoverprintmode(gstateptr->colorInfo, FALSE) &&
           gsc_setcmykcolor( gstateptr->colorInfo , & operandstack , GSC_FILL ) &&
           gsc_setoverprintmode(gstateptr->colorInfo, overprintprocess);

  return result ;
}

/* ---------------------------------------------------------------------- */
static OBJECT *fhgetobject( NAMECACHE *nptr )
{
  OBJECT *dict ;

  dict = nptr->dictobj ;
  if ( dict != NULL ) {
    if ( oInteger( dict[ -2 ] ))        /* On dictstack*/
      return nptr->dictval ;
    else
      return NULL ;
  }
  else if ( nptr->dictval == NULL )
    return NULL ;
  else {
    int32 dloop ;
    int32 dlmax ;
    OBJECT name = OBJECT_NOTVM_NOTHING ;
    theTags( name ) = ONAME | LITERAL ;
    oName( name ) = nptr ;
    dlmax = theStackSize( dictstack ) ;
    for ( dloop = 0 ; dloop <= dlmax ; ++dloop ) {
      OBJECT *theo ;
      dict = stackindex( dloop , & dictstack ) ;
      theo = fast_extract_hash( dict , & name ) ;
      if ( theo )
        return theo ;
    }
    return NULL ;
  }
  /* NOT REACHED */
}

/* ---------------------------------------------------------------------- */
static OBJECT *fhgetdict( NAMECACHE *nptr )
{
  OBJECT *dict ;

  dict = nptr->dictobj ;
  if ( dict != NULL ) {
    if ( oInteger( dict[ -2 ] )) {      /* On dictstack*/
      if ( dict == oDict( userdict )) {
        return & userdict ;
      }
      else {
        int32 dloop ;
        int32 dlmax ;
        dlmax = theStackSize( dictstack ) ;
        for ( dloop = 0 ; dloop <= dlmax ; ++dloop ) {
          OBJECT *tdict ;
          tdict = stackindex( dloop , & dictstack ) ;
          if ( dict == oDict( *tdict ))
            return tdict ;
        }
        HQFAIL( "Should have found dict" ) ;
        return NULL ;
      }
      /* NOT REACHED */
    }
    else
      return NULL ;
  }
  else if ( nptr->dictval == NULL )
    return NULL ;
  else {
    int32 dloop ;
    int32 dlmax ;
    OBJECT name = OBJECT_NOTVM_NOTHING ;
    theTags( name ) = ONAME | LITERAL ;
    oName( name ) = nptr ;
    dlmax = theStackSize( dictstack ) ;
    for ( dloop = 0 ; dloop <= dlmax ; ++dloop ) {
      OBJECT *theo ;
      OBJECT *tdict ;
      tdict = stackindex( dloop , & dictstack ) ;
      theo = fast_extract_hash( tdict , & name ) ;
      if ( theo )
        return tdict ;
    }
    return NULL ;
  }
  /* NOT REACHED */
}

/* ---------------------------------------------------------------------- */
static Bool fhupdateinkoverprint( void )
{
  NAMECACHE *inkopn ;
  OBJECT *inkopo ;
  int32 overprint = gsc_getoverprint(gstateptr->colorInfo, GSC_FILL) ;

  inkopn = system_names + NAME_inkoverprint ;
  inkopo = fhgetobject( inkopn ) ;

  if ( inkopo ) {
    if ( oType( *inkopo ) == OBOOLEAN &&
         oBool( *inkopo ) == overprint ) {
      return TRUE ;
    }
    else {
      OBJECT *inkopd ;
      OBJECT bool ;

      object_store_bool(object_slot_notvm(&bool), overprint) ;

      inkopd = fhgetdict( inkopn ) ;
      HQASSERT( inkopd , "Got OBJECT so must get dict" ) ;
      return fast_insert_hash_name(inkopd, NAME_inkoverprint, &bool) ;
    }
  }
  else {
    OBJECT bool ;

    object_store_bool(object_slot_notvm(&bool), overprint) ;

    return fast_insert_hash_name(topDictStackObj, NAME_inkoverprint, &bool) ;
  }
}

/* ---------------------------------------------------------------------- */
/* The following routine implements:
 * /setoverprint {   % [ bool -> -- ]
 *   /FreeHandDict where dup { pop pop inkoverprint } if
 *   {
 *     pop % ignore the setting - inkoverprint says so
 *   }{
 *     setoverprint % the real one
 *   } ifelse
 * } bind def
 *
 * See See SW:procsets:Level1Separator for details.
 */
Bool l1setoverprint_(ps_context_t *pscontext)
{
  NAMECACHE *fhdn ;
  OBJECT *fhdo ;

  fhdn = system_names + NAME_FreeHandDict ;
  fhdo = fhgetobject( fhdn ) ;

  if ( fhdo ) {
    NAMECACHE *inkopn ;
    OBJECT *inkopo ;

    inkopn = system_names + NAME_inkoverprint ;
    inkopo = fhgetobject( inkopn ) ;

    if ( inkopo &&
         oType( *inkopo ) == OBOOLEAN &&
         oBool( *inkopo ) )
      return pop_(pscontext) ;
    else
      return setoverprint_(pscontext) ;
  }
  else
    return setoverprint_(pscontext) ;
  /* NOT REACHED */
}

/* ---------------------------------------------------------------------- */
/* The following routine implements:
 * /setinkoverprint { % [ bool -> -- ]
 *   /FreeHandDict where {
 *     pop                     % the dictionary containing FreeHandDict
 *     /inkoverprint false def % so that setoverprint does something
 *     dup setoverprint        % make the input value the current setting
 *     /inkoverprint exch def  % ... and that of inkoverprint
 *   } {
 *     pop                     % do nothing if not FreeHand
 *   } ifelse
 * } bind def
 *
 * See SW:procsets:Level1Separator for details.
 */
Bool l1setinkoverprint_(ps_context_t *pscontext)
{
  NAMECACHE *fhdn ;
  OBJECT *fhdo ;

  fhdn = system_names + NAME_FreeHandDict ;
  fhdo = fhgetobject( fhdn ) ;

  if ( fhdo ) {
    return setoverprint_(pscontext) &&
           fhupdateinkoverprint() ;
  }
  else
    return pop_(pscontext) ;
  /* NOT REACHED */
}

/* ---------------------------------------------------------------------- */
/* The following routine implements:
 * /overprintprocess { % [ [c m y k] -> -- ]
 *   /FreeHandDict where {
 *     pop                   % the dictionary containing FreeHandDict
 *     aload pop             % the c,m,y,k components of the input color
 *     3 -1 0 {              % for each component starting at black ...
 *       exch 0 eq {
 *         pop false         % doesnt contribute to overprinting if 0 (no ink)
 *       }{
 *         spots exch get 5 get % otherwise as given in the FreeHand spots array
 *       }ifelse
 *       4 1 roll
 *     } for
 *     or or or setinkoverprint % setinkoverprint if any is true
 *   }{
 *     pop
 *   } ifelse
 * } bind def
 *
 * See See SW:procsets:Level1Separator for details.
 */
Bool l1overprintprocess_(ps_context_t *pscontext)
{
  NAMECACHE *fhdn ;
  OBJECT *fhdo ;

  fhdn = system_names + NAME_FreeHandDict ;
  fhdo = fhgetobject( fhdn ) ;

  if ( fhdo ) {
    int32 type ;
    int32 len ;
    int32 opval ;
    OBJECT *theo ;
    OBJECT *olist ;
    OBJECT *spots ;
    if ( isEmpty( operandstack ))
      return error_handler( STACKUNDERFLOW ) ;

    theo = theTop( operandstack ) ;
    type = oType( *theo ) ;
    if ( type != OARRAY && type != OPACKEDARRAY )
      return error_handler( TYPECHECK ) ;

    if ( ! oCanRead( *theo ))
      if ( ! object_access_override(theo) )
        return error_handler( INVALIDACCESS ) ;

    len = theLen(*theo) ;
    if ( len < 4 )
      return error_handler( RANGECHECK ) ;

    opval = FALSE ;
    spots = NULL ;
    while ((--len) >= 0 ) {
      int32 optest = FALSE ;

      olist = oArray( *theo ) + len ;
      type = oType( *olist ) ;
      if ( type == OINTEGER )
        optest = ( oInteger( *olist ) != 0 ) ;
      else if ( type == OREAL )
        optest = ( oReal( *olist ) != 0.0f ) ;
      else
        return error_handler( TYPECHECK ) ;

      if ( optest ) {
        OBJECT *tmpo ;
        if ( spots == NULL ) {
          spots = fhgetobject( system_names + NAME_spots ) ;
          if ( spots == NULL )
            return error_handler( UNDEFINED ) ;
          type = oType( *spots ) ;
          if ( type != OARRAY && type != OPACKEDARRAY )
            return error_handler( TYPECHECK ) ;
          if ( theLen(*spots) < 4 )
            return error_handler( RANGECHECK ) ;
        }
        tmpo = oArray( *spots ) + len ;
        type = oType( *tmpo ) ;
        if ( type != OARRAY && type != OPACKEDARRAY )
          return error_handler( TYPECHECK ) ;
        if ( theLen(*tmpo) < 6 )
          return error_handler( RANGECHECK ) ;
        tmpo = oArray( *tmpo ) + 5 ;
        if ( oType( *tmpo ) != OBOOLEAN )
          return error_handler( TYPECHECK ) ;
        opval |= oBool( *tmpo ) ;
        if ( opval != FALSE )
          break;
      }
    }

    if ( ! gsc_setoverprint( gstateptr->colorInfo , GSC_FILL , opval ))
      return FALSE;
    if ( ! fhupdateinkoverprint())
      return FALSE ;

    pop( & operandstack ) ;
    return TRUE ;
  }
  else
    return pop_(pscontext) ;
  /* NOT REACHED */
}


/* Log stripped */
