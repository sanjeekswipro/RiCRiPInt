/** \file
 * \ingroup psscan
 *
 * $HopeName: SWv20!src:tokenops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS token operators
 */

#include "core.h"
#include "pscontext.h"
#include "mm.h"
#include "hqmemcpy.h"
#include "swerrors.h"
#include "objects.h"
#include "fileio.h"

#include "params.h"
#include "psvm.h"
#include "scanner.h"
#include "stacks.h"
#include "miscops.h"
#include "swmemory.h"
#include "fileops.h"

/* ----------------------------------------------------------------------------
   function:            getinterval_()     author:              Andrew Cave
   creation date:       13-Oct-1987        last modification:   06-Feb-1995
   arguments:           none .
   description:

   Modifications:
   06-Feb-1995  dstrauss   If doing getinterval on an array and the length
                           is zero, set the olist pointer to NULL.

   See PostScript reference manual page 165.

---------------------------------------------------------------------------- */
Bool getinterval_(ps_context_t *pscontext)
{
  register int32 count , anindex ;
  register OBJECT *theo ;

  if ( theStackSize( operandstack )  < 2 )
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;
  if ( oType(*theo) != OINTEGER )
    return error_handler( TYPECHECK ) ;
  count = oInteger(*theo) ;

  theo = stackindex( 1 , & operandstack ) ;
  if ( oType(*theo) != OINTEGER )
    return error_handler( TYPECHECK ) ;
  anindex = oInteger(*theo) ;

  theo = stackindex( 2 , & operandstack ) ;
  switch ( oXType(*theo) ) {
  case OSTRING :
  case OLONGSTRING :
    if ( !ps_string_interval(theo, theo, anindex, count) )
      return FALSE ;
    break ;
  case OLONGARRAY :
  case OLONGPACKEDARRAY :
    if ( !oCanRead(*theo) && !object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;

    if ( anindex < 0 || count < 0 || anindex + count > oLongArrayLen(*theo) )
      return error_handler( RANGECHECK ) ;

    {
      OBJECT * interval = oLongArray(*theo) + anindex ;
      if (count > MAXPSARRAY) {
        /* result is a new large array, which requires a new trampoline
           allocated in the same allocation mode as the root object - not
           necessarily the same as that of the array. */
        Bool glmode;

        glmode = setglallocmode(pscontext->corecontext, oGlobalValue(*theo));
        interval = extended_array(interval, count) ;
        setglallocmode(pscontext->corecontext, glmode) ;

        if (interval == NULL)
          return error_handler(VMERROR) ;
        oArray(*theo) = interval ;

      } else { /* a small array - convert OEXTENDED into an OARRAY */
        if (oXType(*theo) == OLONGARRAY)
          theTags(*theo) ^= OEXTENDED ^ OARRAY ;
        else
          theTags(*theo) ^= OEXTENDED ^ OPACKEDARRAY ;
        theLen(*theo) = CAST_TO_UINT16(count) ;
        if (count)
          oArray(*theo) = interval ;
        else
          oArray(*theo) = NULL ;
      }
    }
    break ;
  case OARRAY :
  case OPACKEDARRAY :
    if ( !oCanRead(*theo) && !object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;

    if ( anindex < 0 || count < 0 || anindex + count > theLen(*theo) )
      return error_handler( RANGECHECK ) ;

    theLen(*theo) = CAST_TO_UINT16(count) ;
    if ( count != 0 )
      oArray(*theo) += anindex ;
    else
      oArray(*theo) = NULL ;
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }

  npop( 2 , & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            putinterval_()     author:              Andrew Cave
   creation date:       13-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 196.

---------------------------------------------------------------------------- */
Bool putinterval_(ps_context_t *pscontext)
{
  int32 anindex ;
  int32 lo1 , lo2 ;
  uint8 *cl1 = NULL, *cl2 = NULL ; /* silence stupid compiler */
  register OBJECT *o1 , *o2 , *theo ;
  corecontext_t *corecontext = pscontext->corecontext ;

  anindex = theStackSize( operandstack ) ;
  if ( anindex < 2 )
    return error_handler( STACKUNDERFLOW ) ;

  o1 = stackindex( 2 , & operandstack ) ;
  o2 = TopStack( operandstack , anindex ) ;

  if ( oType(*o1) == OARRAY ) {
    /* N.B. o1 cannot be OPACKEDARRAY, you can't modify those */
    if ( oType(*o2) != OARRAY && oType(*o2) != OPACKEDARRAY )
      return error_handler(TYPECHECK) ;
    lo1 = theLen(*o1) ;
    lo2 = theLen(*o2) ;
  } else {
    /* o1 must be a string */
    if ( oType(*o1) == OSTRING ) {
      lo1 = theLen(*o1) ;
      cl1 = oString(*o1) ;
    } else if ( oType(*o1) == OLONGSTRING ) {
      LONGSTR *longstr = oLongStr(*o1) ;
      lo1 = theLSLen(*longstr) ;
      cl1 = theLSCList(*longstr) ;
    } else
      return error_handler(TYPECHECK) ;

    /* o2 must be a string */
    if ( oType(*o2) == OSTRING ) {
      lo2 = theLen(*o2) ;
      cl2 = oString(*o2) ;
    } else if ( oType(*o2) == OLONGSTRING ) {
      LONGSTR *longstr = oLongStr(*o2) ;
      lo2 = theLSLen(*longstr) ;
      cl2 = theLSCList(*longstr) ;
    } else
      return error_handler(TYPECHECK) ;
  }

  theo = stackindex( 1 , & operandstack ) ;
  if ( oType(*theo) != OINTEGER )
    return error_handler( TYPECHECK ) ;
  anindex = oInteger(*theo) ;

  if ( (!oCanWrite(*o1) && !object_access_override(o1)) ||
       (!oCanRead(*o2) && !object_access_override(o2)) )
    return error_handler( INVALIDACCESS ) ;

  HQASSERT(lo1 >= 0 && lo2 >= 0, "Length negative in putinterval") ;

  if ( anindex < 0 || anindex + lo2 > lo1 )
    return error_handler( RANGECHECK ) ;

  if ( oType(*o1) != OARRAY ) {
    HqMemMove(cl1 + anindex, cl2, lo2) ;
  } else {
    register OBJECT *olist1 = oArray(*o1) ;
    register OBJECT *olist2 = oArray(*o2) ;
    register int32 i ;
    int32 glmode = oGlobalValue(*o1) ;

/* Check OBJECTS for illegal LOCAL --> GLOBAL */
    if ( glmode ) {
      for ( i = 0 ; i < lo2 ; ++i ) {
        if ( illegalLocalIntoGlobal(olist2, corecontext) )
          return error_handler( INVALIDACCESS ) ;
        ++olist2 ;
      }
      olist2 = oArray(*o2) ;
    }

    if (lo2 > 0) {
      /* Check if saved. */
      if ( ! check_asave_one(olist1, lo1, anindex, glmode, corecontext))
        return FALSE ;

      olist1 += anindex ;         /* Move to target index */

      if ( olist1 < olist2 ) {
        for ( i = lo2 ; i > 0 ; --i ) {
          Copy(olist1, olist2) ;
          ++olist1 ; ++olist2 ;
        }
      }
      else {                      /* Copy it backwards for overlapping arrays */
        olist1 += ( lo2 - 1 ) ;
        olist2 += ( lo2 - 1 ) ;
        for ( i = lo2 ; i > 0 ; --i ) {
          Copy(olist1, olist2) ;
          --olist1 ; --olist2 ;
        }
      }
    }
  }

  npop( 3 , & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            token_()           author:              Andrew Cave
   creation date:       13-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 232.

---------------------------------------------------------------------------- */
Bool token_(ps_context_t *pscontext)
{
  register OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;

  if ( oType(*theo) == OSTRING ) {
    uint8 *clist = oString(*theo) ;
    int32 length = theLen(*theo) ;
    int32 where = 0 ;
    int32 lineno = 0 ;

    if ( !oCanRead(*theo) && !object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;

    /* Don't call s_scanner() with a zero length string */
    if ( length > 0 &&
         ! s_scanner( clist , length , & where , & lineno , FALSE , FALSE )) {
      pop ( & operandstack ) ; /* must not leave string on the stack */
      return FALSE ;
    }

    if ( ! scannedObject || length == 0 ) {
      Copy(theo, &fnewobj) ;
      return TRUE ;
    }
    if ( ! push( & tnewobj , & operandstack )) {
      pop( & operandstack ) ;
      return FALSE ;
    }
    length -= where ;
    if ( length < 0 )
      length = 0 ;
    theLen(*theo) = CAST_TO_UINT16(length) ;
    if ( length != 0 )
      oString(*theo) = clist + where ;
    else
      oString(*theo) = NULL ;
  } else if ( oType(*theo) == OFILE ) {
    FILELIST *flptr = oFile(*theo) ;
    OBJECT *tempo ;

    if ( !oCanExec(*theo) && !object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;

    /* Direction of dead and corresponding live filter is the same, so this
     * check is valid before checking for dead filter.
     */
    if ( ! isIInputFile( flptr ))
      return error_handler( INVALIDACCESS ) ;

    if ( ! isIOpenFileFilter( theo , flptr )) {
      pop( & operandstack ) ;
      return push( & fnewobj , & operandstack ) ;
    }

    if ( ! f_scanner( flptr , FALSE , FALSE )) {
      pop ( & operandstack ) ; /* must not leave file on the stack */
      return FALSE ;
    }
/*
  If the size of the operand stack  does  not  change, then the end of the
  file was reached before encountering any characters besides white space.
  Otherwise object scanned  is  placed on top of operand stack by scanner.
*/
    if ( ! scannedObject ) {
      /* We have to implicitly close the file since the scanner does not on
       * reaching EOF */
      if ( isIOpenFileFilterById(theLen(*theo), flptr) &&
           ((*theIMyCloseFile(flptr))(flptr, CLOSE_IMPLICIT) == EOF) ) {
        return error_handler(IOERROR) ;
      }
    }
    tempo = theTop( operandstack ) ;
    Copy( theo , tempo ) ;
    theo = scannedObject ? &tnewobj : &fnewobj ;
    Copy( tempo , theo ) ;
  } else /* Not a string or a file */
    return error_handler(TYPECHECK) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            length_()          author:              Andrew Cave
   creation date:       09-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 179.

---------------------------------------------------------------------------- */
Bool length_(ps_context_t *pscontext)
{
  int32 len ;
  OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;
  len = theLen(*theo) ;                       /* For now... */

  switch ( oXType(*theo) ) {
  case ODICTIONARY :
    if ( !oCanRead(*oDict(*theo)) && !object_access_override(oDict(*theo)) )
      return error_handler(INVALIDACCESS) ;

    getDictLength(len, theo) ;
    break ;

  case OLONGSTRING :           /* for long string, len in longstr struct */
    len = theLSLen(*oLongStr(*theo)) ;
    /*@fallthrough@*/
  case OSTRING:
  case OARRAY :
  case OPACKEDARRAY :
    if ( !oCanRead(*theo) && !object_access_override(theo) )
      return error_handler(INVALIDACCESS) ;
    break ;

  case ONAME :                                  /* correct it... */
    len = theINLen(oName(*theo)) ;
    break ;

  case OLONGARRAY :
    if ( !oCanRead(*theo) && !object_access_override(theo) )
      return error_handler(INVALIDACCESS) ;
    len = oInteger(*oArray(*theo)) ;
    break ;

  default :
    return error_handler( TYPECHECK ) ;
  }
  theTags(*theo) = OINTEGER | LITERAL ;
  oInteger(*theo) = len ;

  return TRUE ;
}



/* Log stripped */
