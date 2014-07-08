/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:copyops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Copying of objects.
 */

#include "core.h"
#include "pscontext.h"
#include "swerrors.h"
#include "swoften.h"
#include "hqmemcpy.h"
#include "mm.h" /* SAVELEVELINC */
#include "objects.h"
#include "fileio.h"
#include "namedef_.h"

#include "often.h"

#include "bitblts.h"
#include "matrix.h"
#include "params.h"
#include "psvm.h"
#include "miscops.h"
#include "stacks.h"
#include "dicthash.h"
#include "display.h"
#include "graphics.h"

#include "gstate.h"
#include "gstack.h"
#include "swmemory.h"
#include "plotops.h"

#include "spdetect.h"
#include "gschead.h"
#include "rcbcntrl.h"
#include "gscequiv.h"

static Bool copy_gstate(corecontext_t *corecontext, /*@in@*/ /*@notnull@*/ OBJECT *o2);

static Bool copy_string(/*@in@*/ /*@notnull@*/ OBJECT *o2);

static Bool copy_array(/*@in@*/ /*@notnull@*/ register OBJECT *o2);

static Bool copy_dictionary(/*@in@*/ /*@notnull@*/ OBJECT *o2);

static Bool copy_n(/*@in@*/ /*@notnull@*/ OBJECT *theo);


/* ----------------------------------------------------------------------------
   function:            copy_()            author:              Andrew Cave
   creation date:       16-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 131.

---------------------------------------------------------------------------- */
Bool copy_(ps_context_t *pscontext)
{
  OBJECT *theo ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;
  if ( oType(*theo) == OINTEGER )
    return copy_n( theo ) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  switch ( oType(*theo) ) {

  case ODICTIONARY :
    return copy_dictionary( theo ) ;

  case OARRAY :
    return copy_array(theo) ;

  case OSTRING :
  case OLONGSTRING :
    return copy_string(theo) ;

  case OGSTATE:
    return copy_gstate(ps_core_context(pscontext), theo) ;

  default:
    return error_handler( TYPECHECK ) ;
  }
}

/* ---------------------------------------------------------------------- */
static Bool copy_gstate(corecontext_t *corecontext, OBJECT *o2)
{
  int32 glmode ;
  OBJECT *o1 ;
  GSTATE *gs_dst ;
  GSTATE *gs_src ;

  glmode = oGlobalValue(*o2) ;
  gs_dst = oGState(*o2) ;

  o1 = stackindex( 1 , & operandstack ) ;
  if ( oType(*o1) != OGSTATE )
    return error_handler( TYPECHECK ) ;

  /* if same object, don't do anything, could cause problems if we do.
     n.b. genoa tests (memo25 on interference tests) do this */
  gs_src = oGState(*o1) ;
  if ( gs_dst != gs_src ) {
    if ( glmode && ! check_gstate(corecontext, gs_src ))
      return error_handler( INVALIDACCESS ) ;

    if ( GSTATEISNOTSAVED(gs_dst, corecontext) &&
         !check_gsave(corecontext, gs_dst, glmode) )
      return FALSE ;

    /* Remove anything already in the destination gstate */
    gs_discardgstate( gs_dst ) ;

    /* Copy the content of the gstate, but leave id, type and next intact */
    if ( ! gs_copygstate(gs_dst, gs_src, GST_GSAVE, NULL))
      return FALSE ;

    NEWGSTATESAVED(gs_dst, corecontext) ;
  }

  Copy( o1 , o2 ) ;

  pop( & operandstack ) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            copy_string()      author:              Andrew Cave
   creation date:       16-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See copy_() above for a description of what copy_string() does.

---------------------------------------------------------------------------- */
static Bool copy_string(OBJECT *o2)
{
  OBJECT *o1 ;
  uint8 *cfrom , *cto ;
  int32 lfrom, lto ;

  if ( oType(*o2) == OSTRING ) {
    lto = theLen(*o2) ;
    cto = oString(*o2) ;
  } else {
    LONGSTR *longstr ;
    HQASSERT(oType(*o2) == OLONGSTRING, "Not a string or longstring to copy") ;
    longstr = oLongStr(*o2) ;
    lto = theLSLen(*longstr) ;
    cto = theLSCList(*longstr) ;
  }

  o1 = stackindex( 1 , & operandstack ) ;
  if ( oType(*o1) == OSTRING ) {
    lfrom = theLen(*o1) ;
    cfrom = oString(*o1) ;
  } else if ( oType(*o1) == OLONGSTRING ) {
    LONGSTR *longstr ;
    longstr = oLongStr(*o1) ;
    lfrom = theLSLen(*longstr) ;
    cfrom = theLSCList(*longstr) ;
  } else
    return error_handler( TYPECHECK ) ;

  if ( (!oCanRead(*o1) && !object_access_override(o1)) ||
       (!oCanWrite(*o2) && !object_access_override(o2)) )
    return error_handler( INVALIDACCESS ) ;

  if ( lfrom > lto )
    return error_handler( RANGECHECK ) ;

  /* Set up substring, then copy string values across. Use a substring of o2,
     this is where the string allocation comes from. */
  if ( !ps_string_interval(o1, o2, 0, lfrom) )
    return FALSE ;

  /* N.B. use HqMemMove to cope with overlapping blocks */
  HqMemMove( cto , cfrom , lfrom ) ;

  pop( & operandstack ) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            copy_array()       author:              Andrew Cave
   creation date:       16-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See copy_() above for a description of what copy_array() does.

---------------------------------------------------------------------------- */
static Bool copy_array(OBJECT *o2)
{
  OBJECT *o1 ;

  o1 = stackindex( 1 , & operandstack ) ;
  switch ( oType(*o1) ) {
  case OARRAY :
  case OPACKEDARRAY :
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }

  if ( (!oCanRead(*o1) && !object_access_override(o1)) ||
       (!oCanWrite(*o2) && !object_access_override(o2)) )
    return error_handler( INVALIDACCESS ) ;

  /*  Copy array across, and then set up subarray. */
  if ( !ps_array_copy(o1, o2) )
    return(FALSE);

  theLen(*o2) = theLen(*o1);
  Copy( o1 , o2 ) ;

  pop( & operandstack ) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            copy_dictionary()  author:              Andrew Cave
   creation date:       16-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See copy_() above for a description of what copy_dictionary() does.

---------------------------------------------------------------------------- */
static Bool copy_dictionary(OBJECT *o2)
{
  OBJECT *o1 ;

  o1 = stackindex( 1 , & operandstack ) ;
  if ( oType(*o1) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  if ( ! CopyDictionary( o1 , o2 , NULL , NULL ))       /* src,dst */
    return FALSE ;

  if ( theISaveLangLevel( workingsave ) == 1 ) {
    theTags(*oDict(*o2)) &= ~ACCEMASK ;
    theTags(*oDict(*o2)) |= theTags(*oDict(*o1)) & ACCEMASK ;
  }

  Copy( o1 , o2 ) ;
  pop( & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            copy_n()           author:              Andrew Cave
   creation date:       16-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See copy_() above for a description of what copy_n() does.

---------------------------------------------------------------------------- */
static Bool copy_n(OBJECT *theo)
{
  register int32 num , loop ;

  num = oInteger(*theo) ;
  loop = theStackSize( operandstack ) ;
  if (( num < 0 ) || ( num > loop ))
    return error_handler( RANGECHECK ) ;

  pop( & operandstack ) ;
  if ( ! num )
    return TRUE ;

  --num ;
  for ( loop = 0 ; loop <= num ; ++loop )
    if ( ! push( stackindex( num , & operandstack ) , & operandstack ))
      return FALSE ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            get_()             author:              Andrew Cave
   creation date:       16-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 164.

---------------------------------------------------------------------------- */
Bool get_(ps_context_t *pscontext)
{
#define GET_ACTION( Lthed , Lther ) MACRO_START \
  /* NOTHING */ \
MACRO_END

#define GET_TEST( Lthed )  \
  ( Lthed == thed /* Must be our dictionary... */ )

#define GET_RESULT( Lres ) MACRO_START \
  o2 = Lres ;            \
  Copy( o1 , o2 ) ;      \
  pop( & operandstack ) ;\
MACRO_END /* return TRUE after invocation of this is implicit */

  register OBJECT *o1 , *o2 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = theTop( operandstack ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  if ( oType(*o1) == ODICTIONARY ) {
    OBJECT *thed = oDict(*o1) ;

    NAMECACHE_DICT_EXTRACT_HASH( o2 , GET_TEST , GET_ACTION , GET_RESULT ) ;

    if ( ! oCanRead(*thed) && !object_access_override(thed) )
      return error_handler( INVALIDACCESS ) ;

    error_clear_newerror_context(ps_core_context(pscontext)->error);
    if ( NULL == ( o2 = extract_hash( o1 , o2 ))) {
      if ( ! newerror ) {
        o2 = theTop( operandstack ) ;
        return namedinfo_error_handler( UNDEFINED , NAME_Info , o2 ) ;
      }

      return FALSE ;
    }
  }
  else {
    register int32 anindex ;
    int32 len ;

    if ( oType(*o2) != OINTEGER )
      return error_handler( TYPECHECK ) ;

    if ( !oCanRead(*o1) && !object_access_override(o1) )
      return error_handler( INVALIDACCESS ) ;

    switch ( oXType(*o1) ) {
    case OLONGSTRING:
      len = theLSLen(*oLongStr(*o1)) ;
      break ;
    case OLONGARRAY: case OLONGPACKEDARRAY:
      len = oLongArrayLen(*o1) ;
      break ;
    default:
      len = theLen(* o1 ) ;
    }

    anindex = oInteger(*o2) ;
    if (( anindex < 0 ) || anindex >= len )
      return error_handler( RANGECHECK ) ;

    switch ( oXType(*o1) ) {
    case OSTRING:
      o2 = ( & inewobj ) ;
      oInteger(inewobj) = oString(*o1)[anindex] ;
      break ;
    case OLONGSTRING:
      o2 = ( & inewobj ) ;
      oInteger(inewobj) = theLSCList(*oLongStr(*o1))[anindex] ;
      break ;
    case OARRAY: case OPACKEDARRAY:
      o2 = oArray(*o1) + anindex ;
      break ;
    case OLONGARRAY: case OLONGPACKEDARRAY:
      o2 = oLongArray(*o1) + anindex ;
      break ;
    default:
      return error_handler( TYPECHECK ) ;
    }
  }
  Copy( o1 , o2 ) ;
  pop( & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            put_()             author:              Andrew Cave
   creation date:       16-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 195.

---------------------------------------------------------------------------- */
Bool put_(ps_context_t *pscontext)
{
#define PUT_ACTION( Lthed , Lther ) MACRO_START \
  /* NOTHING */ \
MACRO_END

#define PUT_TEST( Lthed )  \
  ( Lthed == thed /* Must be our dictionary... */ )

#define PUT_RESULT() MACRO_START \
  npop( 3 , & operandstack ) ; \
MACRO_END /* return TRUE after invocation of this is implicit */

  register uint8 *clist ;
  register int32 alen ;
  register int32 theval , anindex ;
  register OBJECT *o1 , *o2 , *o3 ;
  register OBJECT *olist ;
  corecontext_t *corecontext = pscontext->corecontext ;

  if ( theStackSize( operandstack ) < 2 )
    return error_handler( STACKUNDERFLOW ) ;

  o3 = theTop( operandstack ) ;
  o2 = ( & o3[ -1 ] ) ;
  o1 = ( & o3[ -2 ] ) ;
  if ( ! fastStackAccess( operandstack )) {
    o2 = stackindex( 1 , & operandstack ) ;
    o1 = stackindex( 2 , & operandstack ) ;
  }

  if ( oType(*o1) == ODICTIONARY ) {
    OBJECT *thed = oDict(*o1) ;

    if ( oType(*o2) == ONAME ) {
      int32 opclass = theIOpClass(oName(*o2));

      if ( (opclass & RECOMBINEDETECTOP) && rcbn_enabled() )
        gsc_rcbequiv_handle_detectop(o1, o2);

      if ( (opclass & SEPARATIONDETECTOP) && thed != oDict(systemdict) )
        theICMYKDetected(workingsave) = theICMYKDetect(workingsave) = TRUE ;
    }

    NAMECACHE_DICT_INSERT_HASH(corecontext, o2, o3, PUT_TEST, PUT_ACTION, PUT_RESULT) ;

    if ( ! insert_hash( o1 , o2 , o3 ))
      return FALSE ;
  }
  else {
    if ( oType(*o2) != OINTEGER )
      return error_handler( TYPECHECK ) ;

    anindex = oInteger(*o2) ;

    switch ( oXType(*o1) ) {
    case OSTRING:
    case OLONGSTRING:
      if ( oType(*o3) != OINTEGER )
        return error_handler( TYPECHECK ) ;

      if ( !oCanWrite(*o1) && !object_access_override(o1) )
        return error_handler( INVALIDACCESS ) ;

      if ( oType(*o1) == OLONGSTRING ) {
        alen = theLSLen(*oLongStr(*o1)) ;
        clist = theLSCList(*oLongStr(*o1)) ;
      }
      else { /* OSTRING */
        alen = theLen(*o1) ;
        clist = oString(*o1) ;
      }

      if ( anindex < 0 || anindex >= alen )
        return error_handler( RANGECHECK ) ;

      theval = oInteger(*o3) ;
      /* Fast test for (( theval < 0 ) || ( theval > 255 )) */
      if ( (theval & ~255) != 0 )
        return error_handler( RANGECHECK ) ;

      clist[ anindex ] = (uint8)theval ;
      break ;
    case OLONGARRAY:
    case OLONGPACKEDARRAY:
      if ( !oCanWrite(*o1) && !object_access_override(o1) )
        return error_handler( INVALIDACCESS ) ;

      alen = oLongArrayLen(*o1) ;
      if (( anindex < 0 ) || ( anindex >= alen ))
        return error_handler( RANGECHECK ) ;

      olist = oLongArray(*o1) ;

      /* Check OBJECTS for illegal LOCAL --> GLOBAL */
      if ( oGlobalValue(*o1))
        if ( illegalLocalIntoGlobal(o3, corecontext) )
          return error_handler( INVALIDACCESS ) ;

      /* Check if saved. */
      if ( ! check_asave_one(olist, alen, anindex, oGlobalValue(*o1),
                             corecontext) )
        return FALSE ;
      olist += anindex ;
      Copy(olist, o3) ;
      break ;
    case OARRAY:
    case OPACKEDARRAY:
      if ( !oCanWrite(*o1) && !object_access_override(o1) )
        return error_handler( INVALIDACCESS ) ;

      alen = theLen(* o1 ) ;
      if (( anindex < 0 ) || ( anindex >= alen ))
        return error_handler( RANGECHECK ) ;

      olist = oArray(*o1) ;

      /* Check OBJECTS for illegal LOCAL --> GLOBAL */
      if ( oGlobalValue(*o1))
        if ( illegalLocalIntoGlobal(o3, corecontext) )
          return error_handler( INVALIDACCESS ) ;

      /* Check if saved. */
      if ( ! check_asave_one(olist, alen, anindex, oGlobalValue(*o1),
                             corecontext) )
        return FALSE ;
      olist += anindex ;
      Copy(olist, o3) ;
      break ;
    default:
      return error_handler( TYPECHECK ) ;
    }
  }

  npop( 3 , & operandstack ) ;
  return TRUE ;
}

/*
Log stripped */
