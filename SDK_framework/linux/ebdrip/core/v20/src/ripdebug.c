/** \file
 * \ingroup debug
 *
 * $HopeName: SWv20!src:ripdebug.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS Rip debugging code
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "monitor.h"
#include "namedef_.h"

#include "stacks.h"

#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)

/* -------------------------------------------------------------------------- */
/* [51291] Set/clear the breakpoint flag on an executable name */

Bool togglebreakpoint(uint32 setbreak)
{
  OBJECT*     obj ;
  NAMECACHE*  name ;

  HQASSERT(setbreak == 0 || setbreak == NCFLAG_BREAK, "Incorrect parameter") ;

  if ( 0 > theStackSize(operandstack) )
    return error_handler(STACKUNDERFLOW) ;

  obj = theTop(operandstack) ;
  if (oType(*obj) != ONAME)
    return error_handler(TYPECHECK) ;

  name = oName(*obj) ;
  name->flags = (name->flags & ~NCFLAG_BREAK) | setbreak ;

  pop( &operandstack ) ;

  return TRUE ;
}

Bool hqnsetbreakpoint_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return togglebreakpoint(NCFLAG_BREAK) ;
}

Bool hqnclearbreakpoint_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return togglebreakpoint(0) ;
}

typedef struct ripvar {
  int32 name ;
  int32 type ;
  void *value ;
} RIPVAR ;

#define MAXRIPVAR 512

static int32 no_ripvars = 0 ;
static RIPVAR ripvars[ MAXRIPVAR ] = { 0 } ;

static RIPVAR *find_ripvar( int32 name )
{
  int32 i ;

  for ( i = 0 ; i < no_ripvars ; ++i )
    if ( ripvars[ i ].name == name )
      return  & ripvars[ i ]  ;
  return NULL ;
}

void register_ripvar( int32 name , int32 type , void *value )
{
  RIPVAR *ripvar ;

  HQASSERT( name > 0 , "name out of range (-ve) in register_ripvar" ) ;
  HQASSERT( name < NAMES_COUNTED , "name out of range (+ve) in register_ripvar" ) ;
  HQASSERT( type == OBOOLEAN ||
            type == OINTEGER ||
            type == OREAL ,
            "illegal registration type in register_ripvar" ) ;
  HQASSERT( value , "value NULL in register_ripvar" ) ;

  ripvar = find_ripvar( name ) ;
  if ( ripvar ) {
    if ( name  == ripvar->name &&
         type  == ripvar->type &&
         value == ripvar->value )
      return ;
    monitorf((uint8*)"multiple registrations:\n" ) ;
    monitorf((uint8*)" %d (%08x,%d)\n" ,
             name , value , type ) ;
    monitorf((uint8*)" %d (%08x,%d)\n" ,
             ripvar->name , ripvar->value , ripvar->type ) ;
    return ;
  }
  else {
    if ( no_ripvars == MAXRIPVAR ) {
      HQFAIL("Cannot register any more ripvars; increase MAXRIPVAR") ;
      return ;
    }
    ripvar = ( & ripvars[ no_ripvars++ ] ) ;
    ripvar->name  = name ;
    ripvar->type  = type ;
    ripvar->value = value ;
  }
}

Bool breakpoint_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return TRUE ;
}

Bool setripvar_(ps_context_t *pscontext)
{
  OBJECT *o1 ;
  OBJECT *o2 ;
  RIPVAR *ripvar ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = theTop( operandstack ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  if ( oType(*o1) != ONAME )
    return error_handler( TYPECHECK) ;

  if ( oType(*o2) != OBOOLEAN &&
       oType(*o2) != OINTEGER &&
       oType(*o2) != OREAL )
    return error_handler( TYPECHECK) ;

  HQASSERT( oName(*o1) , "lost NAMECACHE" ) ;
  ripvar = find_ripvar(oNameNumber(*o1)) ;
  if ( ripvar ) {
    HQASSERT( ripvar->value , "lost value of ripvar" ) ;
    if ( oType(*o2) == OBOOLEAN &&
         ripvar->type == OBOOLEAN )
      (*((Bool*)(ripvar->value))) = oBool(*o2) ;
    else if ( oType(*o2) == OINTEGER &&
              ripvar->type == OINTEGER )
      (*((int32*)(ripvar->value))) = oInteger(*o2) ;
    else if ( oType(*o2) == OREAL &&
              ripvar->type == OREAL )
      (*((USERVALUE*)(ripvar->value))) = oReal(*o2) ;
    else
      return error_handler( TYPECHECK) ;
  }

  npop( 2 , & operandstack ) ;

  return TRUE ;
}


Bool getripvar_(ps_context_t *pscontext)
{
  OBJECT *o1 ;
  RIPVAR *ripvar ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  o1 = theTop( operandstack ) ;
  if ( oType( *o1 ) != ONAME )
    return error_handler( TYPECHECK) ;

  HQASSERT( oName( *o1 ) , "lost NAMECACHE" ) ;
  ripvar = find_ripvar(oNameNumber(*o1)) ;
  if ( ripvar ) {
    HQASSERT( ripvar->value , "lost value of ripvar" ) ;
    switch ( ripvar->type ) {
    case OBOOLEAN:
      object_store_bool(o1, *((Bool*)(ripvar->value))) ;
      break ;
    case OINTEGER:
      object_store_integer(o1, *((int32*)(ripvar->value))) ;
      break ;
    case OREAL:
      object_store_real(o1, *((USERVALUE*)(ripvar->value))) ;
      break ;
    default:
      return error_handler( TYPECHECK ) ;
    }
  } else
    return error_handler( UNDEFINED ) ;

  return TRUE ;
}

void init_C_globals_ripdebug(void)
{
  no_ripvars = 0 ;
}

#else

void init_C_globals_ripdebug(void)
{
  /* Nothing to do */
}

#endif /* DEBUG_BUILD || ASSERT_BUILD */

/* Log stripped */
