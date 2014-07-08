/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:objmatch.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Object matching callbacks for use with walk_dictionary. This is used for
 * spot function matching and CIE colourspace encode/decode/transform
 * procedure matching.
 */


#include "core.h"
#include "hqmemcmp.h"
#include "objects.h"
#include "fileio.h"

#include "constant.h"
#include "stacks.h"
#include "dictops.h"
#include "dicthash.h"

#include "objmatch.h"

/* objectMatch checks if two object arrays are semantically the same.

   NOTE: this function was originally used for spot function matching only,
   and was extended for matching CIE Encode/Decode/Transfer procedures, but
   it isn't a general object matching function yet.
*/
static Bool objectMatch(OBJECT *srcObj, OBJECT *mchObj, int32 count,
                        int32 recLevel)
{
  int32 loop ;
  SYSTEMVALUE srcSVal , mchSVal ;
  OBJECT *optr, *mptr ;

  HQASSERT(count > 0, "Count is less than one in objectMatch") ;
  HQASSERT(srcObj, "Source object is NULL in objectMatch") ;
  HQASSERT(mchObj, "Match object is NULL in objectMatch") ;

  if ( recLevel < 0 )          /* To stop recursive procs. */
    return FALSE ;

  do {
    switch ( oType(*srcObj) ) {
    case OINTEGER :
    case OREAL :
      if ( oType(*srcObj) == OINTEGER )
        srcSVal = (SYSTEMVALUE)oInteger(*srcObj) ;
      else
        srcSVal = (SYSTEMVALUE)oReal(*srcObj) ;

      if ( oType(*mchObj) == OINTEGER )
        mchSVal = (SYSTEMVALUE)oInteger(*mchObj) ;
      else if ( oType(*mchObj) == OREAL )
        mchSVal = (SYSTEMVALUE)oReal(*mchObj) ;
      else
        return FALSE ;

      if ( fabs(srcSVal - mchSVal) > EPSILON )
        return FALSE ;

      break ;
    case ONAME :
    case OOPERATOR :
      if ( oType(*srcObj) == OOPERATOR ) {
        optr = srcObj ;
      } else {  /* ONAME - Lookup up name - must lookup to an OOPERATOR. */
        if ( (int32)theTags(*srcObj) & EXECUTABLE ) {
          optr = NULL ;
          for ( loop = 0 ; loop <= theStackSize(dictstack) ; ++loop )
            if ( (optr = fast_extract_hash(stackindex(loop, &dictstack), srcObj))
                != NULL )
              break ;
          if ( ! optr || oType(*optr) != OOPERATOR )
            return FALSE ;      /* name lookup failed, or found wrong type */
        } else {        /* Literal name: check if they are the same symbol */
          if ( oType(*mchObj) != ONAME ||
              ((int32)theTags(*mchObj) & EXECUTABLE) != 0 ||
              oName(*srcObj) != oName(*mchObj) )
            return FALSE ;      /* not the same literal, so fail */
          break ;               /* continue with next match */
        }
      }

      if ( oType(*mchObj) == OOPERATOR ) {
        mptr = mchObj ;
      } else if ( oType(*mchObj) == ONAME ) { /* Look up name */
        if ( (int32)theTags(*mchObj) & EXECUTABLE ) {
          mptr = NULL ;
          for ( loop = 0 ; loop <= theStackSize(dictstack) ; ++loop )
            if ( (mptr = fast_extract_hash(stackindex(loop, &dictstack), mchObj))
                != NULL )
              break ;
          if ( ! mptr || oType(*mptr) != OOPERATOR )
            return FALSE ;      /* name lookup failed, or found wrong type */
        } else          /* literal name: source name wasn't literal, so fail */
          return FALSE ;
      } else                    /* Match object isn't a name or operator */
        return FALSE ;

      HQASSERT( oType(*optr) == OOPERATOR &&
               oType(*mptr) == OOPERATOR,
               "Names didn't look up to operators") ;

      if ( (theTags(*optr) & EXECUTABLE) != (theTags(*mptr) & EXECUTABLE) ||
          (oOp(*optr) != oOp(*mptr)) )
        return FALSE ;  /* not the same operator, or not the same effect */
      break ;
    case OARRAY :
    case OPACKEDARRAY :
      switch ( oType(*mchObj) ) {
      case OARRAY:
      case OPACKEDARRAY:
        if ( theLen(*srcObj) != theLen(*mchObj) ||
            (theTags(*srcObj) & EXECUTABLE) != (theTags(*mchObj) & EXECUTABLE) )
          return FALSE ;

        if ( theLen(*srcObj) > 0 &&     /* zero length arrays match */
             ! objectMatch(oArray(*srcObj),
                           oArray(*mchObj),
                           theLen(*srcObj),
                           recLevel - 1) )
          return FALSE ;
        break ;
      default:
        return FALSE ;
      }
      break ;
    case OSTRING:
      if ( (theTags(*srcObj) & EXECUTABLE) != (theTags(*mchObj) & EXECUTABLE) )
        return FALSE ;
      if ( oType(*mchObj) != OSTRING ||
           HqMemCmp (oString(*srcObj), (int32)theLen(*srcObj),
                     oString(*mchObj), (int32)theLen(*mchObj)) != 0 )
        return FALSE;
      break ;
    case ONULL:
      if ( (theTags(*srcObj) & EXECUTABLE) != (theTags(*mchObj) & EXECUTABLE) )
        return FALSE ;
    case OINFINITY:
    case OMARK:
      /* These types need to match in type only */
      if ( oType(*srcObj) != oType(*mchObj) )
        return FALSE ;
      break ;
    case OFILE:
      if ( (theTags(*srcObj) & EXECUTABLE) != (theTags(*mchObj) & EXECUTABLE) )
        return FALSE ;
      if ( theFilterIdPart(theLen(*srcObj)) != theFilterIdPart(theLen(*mchObj)))
        /* the bottom 15 bits of the lengths are used to determine whether or
           not filters are sharing the same underlying filelist structure even
           though they are different filters */
        return FALSE ;
      /* FALLTHRU */
    case OBOOLEAN:
    case OFONTID:
    case OSAVE:
    case OGSTATE:
      /* These types should match in value and type, we theOther to emphasise
         that it's exact value equality */
      if ( oType(*srcObj) != oType(*mchObj) ||
           oOther(*srcObj) != oOther(*mchObj) )
        return FALSE ;
      break ;
    default:
      /* No other object types should be accessible via PostScript */
      HQFAIL("Unimplemented object match type in objectMatch") ;
      return FALSE ;
    }
    srcObj++ ;
    mchObj++ ;
  } while ( (--count) != 0 ) ;
  return TRUE ;
}

Bool wd_match_obj(OBJECT *key, OBJECT *value, void *arg)
{
  OBJECT_MATCH *omptr = (OBJECT_MATCH *)arg ;
  if ( objectMatch(omptr->obj, value, 1 /* object count */,
                   16 /* max recursion */ ) ) {
    omptr->key = key ;
    return FALSE ;
  }
  return TRUE ;
}

/* Log stripped */
/* end of objmatch.c */
