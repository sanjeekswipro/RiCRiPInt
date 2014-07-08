/** \file
 * \ingroup pdf
 *
 * $HopeName: COREpdf_base!src:pdfmatch.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF dictionary matching implementation
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "dictscan.h"
#include "namedef_.h"

#include "swpdf.h"
#include "pdfmatch.h"
#include "pdfxref.h"

static Bool pdf_validate_object_type( NAMETYPEMATCH *onematch ,
                                      Bool stop_at_indirect ) ;

/** pdf_dictmatch is an optimised version of dictmatch with extra
 * behaviour. In addition to the functionality of dictmatch,
 * pdf_dictmatch resolves xref objects (objects with the type
 * oindirect) and checks the type of the resolved object in the list
 * of types in the nametypematch.
 *
 * The position of the oindirect type in the nametypematch is
 * important (in contrast to dictmatch where its position is
 * irrelevant). The types ocurring before an oindirect type are
 * permitted as direct (including the oindirect) and indirect objects,
 * but the types following oindirect are only allowed after the
 * indirection has been resolved (excluding the oindirect).
 *
 * For example, the type list:
 *
 * 'OSTRING, OARRAY, OINDIRECT, ODICTIONARY' allows a direct object of
 * the type string, array and indirect. If the object is an xref
 * object, then it is resolved with a call to pdf_lookupxref, and its
 * type must be string, array or dictionary. Also, since specifying an
 * entry with the type null is equivalent to omitting the entry, if
 * the entry is optional and the type is null, at either stage, no
 * error is given.
 */
Bool pdf_dictmatch( PDFCONTEXT *pdfc , OBJECT *dict , NAMETYPEMATCH *match_objects )
{
  NAMETYPEMATCH *onematch ;

  HQASSERT( pdfc , "pdfc NULL in pdf_dictmatch" ) ;
  HQASSERT( dict , "dict NULL in pdf_dictmatch" ) ;
  HQASSERT( oType(*dict) == ODICTIONARY , "non dict use of pdf_dictmatch" ) ;
  HQASSERT( match_objects , "match_objects NULL in pdf_dictmatch" ) ;

  for ( onematch = match_objects ;
        theISomeLeft( onematch ) ;
        ++onematch ) {
    oName( nnewobj ) = theIMName( onematch ) ;
    theIDictResult( onematch ) = fast_extract_hash( dict , & nnewobj ) ;

    if ( theIMCount( onematch ) != 0 ) {
      if ( ! pdf_validate_object_type( onematch , TRUE ))
        return FALSE ;

      if ( theIDictResult( onematch ) &&
           oType(*theIDictResult(onematch)) == OINDIRECT ) {
        if ( ! pdf_lookupxref( pdfc , & theIDictResult( onematch ) ,
                               oXRefID(*theIDictResult(onematch)) ,
                               theGen(*theIDictResult(onematch)) ,
                               FALSE ))
          return FALSE ;

        if ( theIMCount( onematch ) > 1 ) {
          if ( ! pdf_validate_object_type( onematch , FALSE ))
            return FALSE ;
        }
      }
    }
  }

  return TRUE ;
}

/* Do the type checking on the object. If stop_at_indirect is true,
 * types allowed are those before, and including, oindirect; if
 * stop_at_indirect is false types allowed are those before and after,
 * but excluding oindirect.
 *
 * Additionally, ONOTHING is allowed as the first element in the list.
 * If present then an optional entry of an incorrect type is ignored,
 * as though it wasn't present.
 */

static Bool pdf_validate_object_type( NAMETYPEMATCH *onematch ,
                                      Bool stop_at_indirect )
{
  OBJECT *theo ;
  uint8 *match_ptr ;
  uint8 count ;
  OBJECT keyobject = OBJECT_NOTVM_NOTHING ;
  Bool ignore_type = FALSE ;

  HQASSERT( onematch , "onematch is null in pdf_validate_object_type" ) ;
  theo = theIDictResult( onematch ) ;

  theTags( keyobject ) = ONAME | LITERAL ;
  oName( keyobject ) = theIMName( onematch ) ;

  /* The PDF spec defines that omitting a key is the same as defining it
   * as null.
   */

  if ( ! theo )
    theo = & onull ;

  if ( ! stop_at_indirect && oType(*theo) == OINDIRECT )
    return errorinfo_error_handler( TYPECHECK , & onull , & keyobject ) ;

  /* if the first entry is ONOTHING, set the ignore flag and skip to the first
   * real entry.
   */
  match_ptr = theIMatch( onematch ) ;
  count = theIMCount( onematch ) ;
  if ( match_ptr[0] == ONOTHING ) {
    ignore_type = TRUE ;
    ++match_ptr ;
    --count ;
  }
  for ( ; count ; --count , ++match_ptr ) {
    if ( match_ptr[ 0 ] == oXType(*theo) ||
         ( stop_at_indirect && match_ptr[ 0 ] == OINDIRECT )) {
      break ;
    }
  }

  if ( count == 0 || match_ptr[ 0 ] != oXType(*theo) ) {
    /* type not in the list. */
    if ( theIOptional( onematch ) &&
         (oType(*theo) == ONULL || ignore_type) )
      /* Specifying an entry with null is equivalent to omitting the
       * entry, therefore do not give an error if the entry is
       * optional. Since onull was not in the list of types the result
       * slot must be overridden with a null ptr in case theo was the
       * onull object. */
      theIDictResult( onematch ) = NULL ;
    else
      return errorinfo_error_handler( TYPECHECK , & onull , & keyobject ) ;
  }

  return TRUE ;
}

/* end of file pdfmatch.c */

/* Log stripped */
