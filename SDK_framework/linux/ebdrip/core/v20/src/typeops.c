/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:typeops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS type operators
 */

#include <stdarg.h>

#include "core.h"
#include "constant.h"
#include "objects.h"
#include "stacks.h"
#include "control.h"
#include "swerrors.h"
#include "matrix.h"
#include "fileio.h"
#include "params.h"
#include "psvm.h"
#include "bitblts.h"
#include "display.h"
#include "graphics.h"
#include "swmemory.h"
#include "miscops.h"
#include "scanner.h"
#include "swcopyf.h"
#include "hqmemcpy.h"

#include "namedef_.h"

#include "typeops.h"

#include "lanlevel.h" /* level[12]sysdict, to prevent noaccess on them */

/* static prototypes */
static Bool checktopOaccess(int32 the_check, Bool do_dict);
static int32 do_cvrs( int32 radix ,
               uint32 num , /* important for divisions to work that this is unsigned */
               uint8 str[] , int32 lnth );




/* ----------------------------------------------------------------------------
   function:            type_()            author:              Andrew Cave
   creation date:       12-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 235.

---------------------------------------------------------------------------- */
Bool type_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register OBJECT *theo ;
  int32    type ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;
  type = oXType(*theo) ;
  if (type >= OVIRTUAL) {
    switch (type) {
      case OLONGARRAY:       type = OARRAY ;       break ;
      case OLONGPACKEDARRAY: type = OPACKEDARRAY ; break ;
      default:
        return error_handler(TYPECHECK) ;
    }
  }
  oName( *theo ) =
    &system_names[NAME_integertype + type - 1 ] ;
  theTags(*theo) = ONAME | EXECUTABLE ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            cvlit_()           author:              Andrew Cave
   creation date:       12-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 141.

---------------------------------------------------------------------------- */
Bool cvlit_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;

  theTags(*theo) &= (~EXECUTABLE) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            cvx_()             author:              Andrew Cave
   creation date:       12-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 144.

---------------------------------------------------------------------------- */
Bool cvx_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;

  theTags(*theo) |= EXECUTABLE ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            xcheck_()          author:              Andrew Cave
   creation date:       12-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 240.

---------------------------------------------------------------------------- */
Bool xcheck_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register OBJECT *o1 , *o2 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  o1 = TopStack( operandstack , ssize ) ;
  if ( oExec( *o1 ))
    o2 = ( & tnewobj ) ;
  else
    o2 = ( & fnewobj ) ;

  Copy( o1 , o2 ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            executeonly_()     author:              Andrew Cave
   creation date:       12-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 153.

---------------------------------------------------------------------------- */
Bool executeonly_(ps_context_t *pscontext)
{
  register int32 ssize = theStackSize( operandstack ) ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  return reduceOaccess( EXECUTE_ONLY , FALSE , TopStack( operandstack , ssize )) ;
}

/* ----------------------------------------------------------------------------
   function:            noaccess_()        author:              Andrew Cave
   creation date:       12-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 188.

---------------------------------------------------------------------------- */
Bool noaccess_(ps_context_t *pscontext)
{
  register int32 ssize = theStackSize( operandstack ) ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  return reduceOaccess( NO_ACCESS , TRUE , TopStack( operandstack , ssize )) ;
}

/* ----------------------------------------------------------------------------
   function:            readonly_()        author:              Andrew Cave
   creation date:       12-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 200.

---------------------------------------------------------------------------- */
Bool readonly_(ps_context_t *pscontext)
{
  register int32 ssize = theStackSize( operandstack ) ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  return reduceOaccess( READ_ONLY , TRUE , TopStack( operandstack , ssize )) ;
}

/* Utility function for above three functions  */

int32 reduceOaccess( int32 new_acc , int32 do_dict , register OBJECT *theo )
{
  if ( oType(*theo) == ODICTIONARY ) {
    register OBJECT *thed ;
    uint8 tags ;

    if ( ! do_dict )
      return error_handler( TYPECHECK ) ;

    thed = oDict(*theo) ;

    /* Reject reducing the access of dicts we use internally. Don't need to
       check extension dicts, because extensions are added to the end of the
       dictionary chain. */
    if (( NO_ACCESS == new_acc ) &&
        ( oDict(level1sysdict) == thed ||
          oDict(level2sysdict) == thed ||
          oDict(level3sysdict) == thed ))
      /* systemdict should be one of the above, so no need to check it explicitly. */
      /* oDict( systemdict ) == thed */
      return error_handler( INVALIDACCESS );

    tags = theTags(*thed) ;
    /*
     * Access rites on dictionaries compound in a wierd way
     * dict readonly readonly => OK
     * dict noaccess noaccess => OK
     * dict readonly noaccess => /invalidaccess
     * dict noaccess readonly => OK
     */
    if ( tagsAccess( tags ) < new_acc )
      return TRUE ;
    if ( (new_acc == NO_ACCESS) && (tagsAccess(tags) == READ_ONLY ) )
      return error_handler( INVALIDACCESS );
  }

  return object_access_reduce(new_acc, theo) ;
}

/* ----------------------------------------------------------------------------
   function:            rcheck_()          author:              Andrew Cave
   creation date:       12-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 198.

---------------------------------------------------------------------------- */
Bool rcheck_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return checktopOaccess( CANREAD , TRUE ) ;
}

/* ----------------------------------------------------------------------------
   function:            wcheck_()          author:              Andrew Cave
   creation date:       12-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 238.

---------------------------------------------------------------------------- */
Bool wcheck_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return checktopOaccess( CANWRITE , TRUE ) ;
}

/* Utility function for above two functions  */
static int32 checktopOaccess( int32 the_check , int32  do_dict )
{
  register int32 ssize ;
  register OBJECT *o1 , *o2 ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;
  o1 = o2 = TopStack( operandstack , ssize ) ;

  switch ( oXType( *o2 )) {

  case ODICTIONARY:
    if ( ! do_dict )
      return error_handler( TYPECHECK ) ;
    o2 = oDict( *o2 ) ;
  case OSTRING:
  case OLONGSTRING:
  case OARRAY:
  case OPACKEDARRAY:
  case OLONGARRAY:
  case OLONGPACKEDARRAY:
  case OFILE:
  case OGSTATE:
    break;

  default:
    return error_handler( TYPECHECK ) ;
  }
  if ( oAccess( *o2 ) >= the_check )
    Copy( o1, &tnewobj ) ;
  else
    Copy( o1, &fnewobj ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            cvi_()             author:              Andrew Cave
   creation date:       12-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 141.

---------------------------------------------------------------------------- */
Bool cvi_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register OBJECT *theo ;
  register SYSTEMVALUE arg ;

  int32 len , waste ;
  SYSTEMVALUE sarg ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;
  theo = TopStack( operandstack , ssize ) ;

  switch ( oType( *theo )) {
  case OREAL :
    arg = ( SYSTEMVALUE )oReal( *theo ) ;
    if ( ! intrange( arg ))
      return error_handler( RANGECHECK ) ;
    theTags(*theo) = OINTEGER | LITERAL ;
    oInteger( *theo ) = ( int32 )arg ;
    return TRUE ;

  case OINTEGER :
    return TRUE ;

  case OSTRING :
    if ( ! oCanRead( *theo ))
      if ( ! object_access_override(theo))
        return error_handler( INVALIDACCESS ) ;

    len = theLen(*theo) ;
    if ( ! len )
      return error_handler( TYPECHECK ) ;

    waste = 0 ;
    if ( ! s_scanner( oString( *theo ) , len ,
                     &waste , & waste , FALSE , FALSE ))
      return error_handler( TYPECHECK ) ;
    if ( ssize == theStackSize( operandstack ))
      return error_handler( TYPECHECK ) ;

    if ( ! stack_get_numeric(&operandstack, &sarg, 1) ) {
      pop( & operandstack ) ;
      return error_handler( TYPECHECK ) ;
    }
    pop( & operandstack ) ;
    arg = sarg ;
    if ( ! intrange( arg ))
      return error_handler( RANGECHECK ) ;
    theTags(*theo) = OINTEGER | LITERAL ;
    oInteger( *theo ) = ( int )arg ;
    return TRUE ;

  case OINFINITY :
    return error_handler( RANGECHECK ) ;
  default:
    return error_handler( TYPECHECK ) ;
  }
}

/* ----------------------------------------------------------------------------
   function:            cvn_()             author:              Andrew Cave
   creation date:       12-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 142.

---------------------------------------------------------------------------- */
Bool cvn_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register OBJECT *theo ;

  NAMECACHE *ntemp ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;
  theo = TopStack( operandstack , ssize ) ;

  if ( oType( *theo ) != OSTRING )
    return error_handler( TYPECHECK ) ;

  if ( ! oCanRead( *theo ))
    if ( ! object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;

  if ( NULL == ( ntemp = cachename( oString( *theo ) , (uint32) theLen(*theo))))
    return FALSE ;
  theTags(*theo) = (uint8)(oExec( *theo ) | ONAME) ;
  oName( *theo ) = ntemp ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            cvr_()             author:              Andrew Cave
   creation date:       12-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 142.

---------------------------------------------------------------------------- */
Bool cvr_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register OBJECT *theo ;
  register SYSTEMVALUE arg ;

  int32 len , waste ;
  SYSTEMVALUE sarg ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;
  theo = TopStack( operandstack , ssize ) ;

  switch ( oType( *theo )) {
  case OINTEGER :
    theTags(*theo) = OREAL | LITERAL ;
    oReal( *theo ) = ( USERVALUE )oInteger( *theo ) ;
    theLen( *theo ) = 0 ;
    return TRUE ;

  case OREAL :
    return TRUE ;

  case OSTRING :
    if ( ! oCanRead( *theo ))
      if ( ! object_access_override(theo) )
        return error_handler( INVALIDACCESS ) ;

    len = theLen(*theo) ;
    if ( ! len )
      return error_handler( TYPECHECK ) ;

    waste = 0 ;
    if ( ! s_scanner( oString( *theo ) , len ,
                      & waste , &waste , FALSE , FALSE ))
      return error_handler( TYPECHECK ) ;
    if ( ssize == theStackSize( operandstack ))
      return error_handler( TYPECHECK ) ;

    if ( ! stack_get_numeric(&operandstack, &sarg, 1) ) {
      pop( & operandstack ) ;
      return error_handler( TYPECHECK ) ;
    }
    pop( & operandstack ) ;
    arg = sarg ;
    if ( ! realrange( arg ))
      return error_handler( RANGECHECK ) ;
    theTags(*theo) = OREAL | LITERAL ;
    oReal( *theo ) = ( USERVALUE )arg ;
    theLen( *theo ) = 0 ;
    return TRUE ;

  case OINFINITY :
    theTags(*theo) = OINFINITY | LITERAL ;
    theLen( *theo ) = 0 ;
    return TRUE ;
  default:
    return error_handler( TYPECHECK ) ;
  }
}

/* ----------------------------------------------------------------------------
   function:            cvrs_()            author:              Andrew Cave
   creation date:       12-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 143.

---------------------------------------------------------------------------- */
Bool cvrs_(ps_context_t *pscontext)
{
  uint8 temp[ 33 ] ;
  uint8 *clist , *cfrom;
  int32 length , rlength ;
  int32 thenum , radix ;

  register OBJECT *firsto ;
  register OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 2 )
    return error_handler( STACKUNDERFLOW ) ;

  firsto = theTop( operandstack ) ;
  if ( oType( *firsto ) != OSTRING )
    return error_handler( TYPECHECK ) ;

  theo = stackindex( 1 , & operandstack ) ;
  if ( oType( *theo ) != OINTEGER )
    return error_handler ( TYPECHECK ) ;
  radix = oInteger( *theo ) ;

  if ( ! oCanWrite( *firsto ))
    if ( ! object_access_override(firsto) )
      return error_handler( INVALIDACCESS ) ;
  length = theLen(*firsto) ;
  if ( ! length )
    return error_handler( RANGECHECK ) ;
  clist = oString( *firsto ) ;

  if (( radix < 2 ) || ( radix > 36 ))
    return error_handler( RANGECHECK ) ;

  thenum = rlength = 0;         /* init to keep compiler quiet  */

  theo = stackindex( 2 , & operandstack ) ;
  switch ( oType( *theo )) {

  case OINTEGER :
    if ( radix == 10 ) {
      swcopyf(( uint8 * )temp , (uint8*)"%d" , oInteger( *theo )) ;
      rlength = strlen_int32(( char * )temp ) ;
      radix = 1 ;
    }
    else
      thenum = oInteger( *theo ) ;
    break ;

  case OINFINITY :
    npop (3, & operandstack);
    return error_handler (LIMITCHECK);

  case OREAL :
    if ( radix == 10 ) {
      swcopyf(( uint8 * )temp , (uint8*)"%.6g" ,
              ( SYSTEMVALUE ) oReal( *theo )) ;
      rlength = strlen_int32(( char * )temp ) ;

      /* add a '.0' if necessary */
      cfrom = temp ;
      while (* cfrom) {
        if ((int32)*cfrom == '.' || (int32)*cfrom == 'e')
          break;
        cfrom++;
      }
      if ((int32)*cfrom == '\0') {
        cfrom[0] = '.';
        cfrom[1] = '0';
        cfrom[2] = '\0';
        rlength += 2;
      }

      radix = 1 ;
    }
    else {
      if ( ! intrange( oReal( *theo ))) {
        if ( oReal( *theo ) < 0.0 )
          thenum = 0xffffffff ;
        else
          thenum = 0x7fffffff ;
      }
      else
        thenum = ( int32 )oReal( *theo ) ;
    }
    break ;

  default:
    return error_handler (TYPECHECK);
  }

  if ( radix != 1 ) {
    if ( thenum == 0 ) {
      rlength = 1 ;
      temp[ 0 ] = '0' ;
    }
    else {
      rlength = do_cvrs( radix , ( uint32 )thenum , temp , length ) ;
    }
  }

  if (rlength > length || rlength <= 0)
    return error_handler (RANGECHECK);

  HQASSERT( rlength > 0 , "cvrs should produce non-empty string" ) ;
  HqMemCpy( clist , temp , rlength ) ;
  Copy( theo , theTop( operandstack )) ;

  npop( 2 , & operandstack ) ;

  theLen(*theo)  = ( uint16 )rlength ;
  oString( *theo ) = clist ;

  return TRUE ;
}

/* Utility function for the cvrs_() operator */
static int32 do_cvrs(int32 radix,
                     uint32 num, /* important for divisions to work that this is unsigned */
                     uint8 str[], int32 lnth)
{
  int32 temp ;

/*  Base cases of recursion. */
  if ( ! num )
    return  0  ;
/*
  lnth = 0 implies that no more string to store number in.
  Returning a negative quantity indicates an error.
*/
  if ( ! lnth )
    return  -1  ;
/*
  Recursive definition. One less element in string, and divide num by radix.
*/
  if (( temp = do_cvrs( radix , num / radix , str , lnth - 1 )) < 0 )
    return  -1  ;
/*
  Store remainder in correct position.
*/
  if ( num % radix <= 9 )
    str[ temp ] = (uint8)('0' + ( num % radix )) ;
  else
    str[ temp ] = (uint8)(( num % radix ) + 'A' - 10) ;
/*
  Return  where  to  store  next  character.
  Ultimately is the length of the string used.
*/
  return  temp + 1  ;
}

/* ----------------------------------------------------------------------------
   function:            cvs_()             author:              Andrew Cave
   creation date:       12-Oct-1987        last modification:   ##-###-####
   arguments:           none.
   description:

   See PostScript reference manual page 144.

---------------------------------------------------------------------------- */
Bool cvs_(ps_context_t *pscontext)
{
  int32 lfrom ;
  int32 lto ;
  OBJECT *o1 ;
  OBJECT *o2 ;

  uint8 *cto ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = theTop( operandstack ) ;
  if ( oType( *o2 ) != OSTRING )
    return error_handler( TYPECHECK ) ;

  if ( ! oCanWrite( *o2 ))
    if ( ! object_access_override(o2) )
      return error_handler( INVALIDACCESS ) ;

  lto = theLen(*o2) ;
  cto = oString( *o2 ) ;

  o1 = stackindex( 1 , & operandstack ) ;

  switch ( oType( *o1 )) {

  case OINTEGER :
  case OREAL:
  case OBOOLEAN :
  case OINDIRECT :
  case OFILEOFFSET :
    if ( !do_cvs( o1 , cto , lto , & lfrom ))
      return FALSE ;
    HQASSERT( lfrom >  0 , "cvs on these OBJECTs should produce non-empty string" ) ;
    break;

  case OSTRING :
  case OLONGSTRING :
  case ONAME :
  case OOPERATOR :
    if ( !do_cvs( o1 , cto , lto , & lfrom ))
      return FALSE ;
    HQASSERT( lfrom >= 0 , "cvs on these OBJECTs may produce empty string" ) ;
    if ( lfrom == 0 )
      cto = NULL ;
    break;

  case OINFINITY :
    npop (2, & operandstack);
    return error_handler( LIMITCHECK ) ;

  default:
    if ( lto >= 15 )
      HqMemCpy( cto , "--nostringval--" , lfrom = 15 ) ;
    else
      return error_handler( RANGECHECK ) ;
    break;
  }

  Copy(o1, o2);
  pop( & operandstack ) ;
  theLen(*o1)  = (uint16)lfrom ;
  oString( *o1 ) = cto ;

  return TRUE ;
}


int32 do_cvs( OBJECT *o1 , uint8 *cto , int32 lto , int32 *lfrom )
{
  uint8 temp[ 32 ] ;
  uint8 *cfrom;
  NAMECACHE *then ;

  switch ( oType( *o1 )) {

  case OFILEOFFSET :

    /* handle 48-bit and 32-bit positions nicely */
    if ((uint32)theLen(*o1)) {
      swcopyf(( uint8 * )temp, (uint8 *)"16#%x%08x", ((unsigned)theLen(*o1)),
            (unsigned)o1->_d1.vals.fileoffset_low) ;
    } else {
      swcopyf(( uint8 * )temp, (uint8 *)"%u",
            (unsigned)o1->_d1.vals.fileoffset_low) ;
    }
    *lfrom = strlen_int32(( char * )temp ) ;
    if ( *lfrom > lto )
      return error_handler( RANGECHECK ) ;
    HqMemCpy( cto , temp , *lfrom ) ;
    break;

  case OINTEGER :
    swcopyf(( uint8 * )temp , (uint8*)"%d" , oInteger( *o1 )) ;
    *lfrom = strlen_int32(( char * )temp ) ;
    if ( *lfrom > lto )
      return error_handler( RANGECHECK ) ;
    HqMemCpy( cto , temp , *lfrom ) ;
    break;

  case OREAL:
    swcopyf(( uint8 * )( & temp[ 1 ] ) , (uint8*)"%.6g" ,
            ( SYSTEMVALUE ) oReal( *o1 )) ;
    /* add a '.0' if necessary */
    cfrom = & temp[1] ;
    while (* cfrom) {
      if ((int32)*cfrom == '.' || (int32)*cfrom == 'e')
        break;
      cfrom++;
    }
    if ((int32)*cfrom == '\0') {
      cfrom[0] = '.';
      cfrom[1] = '0';
      cfrom[2] = '\0';
    }

    *lfrom = strlen_int32(( char * )( & temp[ 1 ] )) ;

    if (((int32)temp[ 1 ] == '.' ) ||
        (((int32)temp[ 1 ] == '-' ) && ((int32)temp[ 2 ] == '.' )))
      ++*lfrom ;
    if ( *lfrom > lto )
      return error_handler( RANGECHECK ) ;
    if ((int32)temp[ 1 ] == '.' ) {
      temp[ 0 ] = '0' ;
      HqMemCpy( cto , ( & temp[ 0 ] ) , *lfrom ) ;
    }
    else if (((int32)temp[ 1 ] == '-' ) && ((int32)temp[ 2 ] == '.' )) {
      temp[ 0 ] = '-' ;
      temp[ 1 ] = '0' ;
      HqMemCpy( cto , ( & temp[ 0 ] ) , *lfrom ) ;
    }
    else
      HqMemCpy( cto , ( & temp[ 1 ] ) , *lfrom ) ;
    break;

  case OSTRING :
    if ( ! oCanRead( *o1 ))
      if ( ! object_access_override(o1) )
        return error_handler( INVALIDACCESS ) ;

    *lfrom = theLen(*o1) ;
    if ( *lfrom > lto )
      return error_handler( RANGECHECK ) ;
    if ( *lfrom != 0 ) {
      uint8 *cfrom = oString( *o1 ) ;

      HqMemMove( cto , cfrom , *lfrom ) ;
    }
    break ;

  case OLONGSTRING:
    if ( ! oCanRead( *o1 ))
      if ( ! object_access_override(o1) )
        return error_handler( INVALIDACCESS ) ;

    *lfrom = theLSLen(*oLongStr(*o1)) ;
    if ( *lfrom > lto )
      return error_handler( RANGECHECK ) ;
    if ( *lfrom != 0 ) {
      uint8 *cfrom = theLSCList(*oLongStr(*o1)) ;

      HqMemMove( cto , cfrom , *lfrom ) ;
    }
    break ;

  case ONAME :
    then = oName( *o1 ) ;
    HQASSERT( then , "no name hanging off an ONAME" ) ;
    *lfrom = theINLen( then ) ;
    if ( *lfrom > lto )
      return error_handler( RANGECHECK ) ;
    if ( *lfrom != 0 )
      HqMemCpy( cto , theICList( then ) , *lfrom ) ;
    break;

  case OOPERATOR :
    HQASSERT( oOp( *o1 ) , "no operator hanging off an OOPERATOR" ) ;
    then = theIOpName( oOp( *o1 )) ;
    HQASSERT( then , "no name hanging off an OOPERATOR" ) ;
    *lfrom = theINLen( then ) ;
    if ( *lfrom > lto )
      return error_handler( RANGECHECK ) ;
    if ( *lfrom != 0 )
      HqMemCpy( cto , theICList( then ) , *lfrom ) ;
    break ;

  case OBOOLEAN :
    if ( oBool( *o1 ))
      if ( lto >= 4 )
        HqMemCpy( cto , "true" , *lfrom = 4 ) ;
      else
        return error_handler( RANGECHECK ) ;
    else if ( lto >= 5 )
        HqMemCpy( cto , "false" , *lfrom = 5 ) ;
    else
      return error_handler( RANGECHECK ) ;
    break;

  case OINDIRECT :
    swcopyf(( uint8 * )temp , (uint8*)"%d %d R" ,
              oXRefID( *o1 ) ,
              theGen(*o1)) ;
    *lfrom = strlen_int32(( char * )temp ) ;
    if ( *lfrom > lto )
      return error_handler( RANGECHECK ) ;
    HqMemCpy( cto , temp , *lfrom ) ;
    break;

  default:
    /* All other cases should be handled by the caller. */
    break;
  }

  return TRUE ;
}



/* Log stripped */
