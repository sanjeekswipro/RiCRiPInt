/** \file
 * \ingroup pdfobj
 *
 * $HopeName: COREpdf_base!src:pdfmem.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF object and memory allocation/deallocation.
 */

#include "core.h"
#include "hqmemcpy.h"
#include "objects.h"
#include "swerrors.h"
#include "monitor.h"
#include "namedef_.h"

#include "pdfcntxt.h"
#include "pdfmem.h"
#include "pdfstrm.h"
#include "pdfxref.h"

#include "dictops.h"
#include "params.h"
#include "psvm.h"
#include "rsd.h"
#include "stream.h"
#include "swpdfin.h"

static Bool pdf_copyobj_dictwalkfn( OBJECT *thek , OBJECT *theo ,
                                    void *params ) ;
static Bool dictwalk_destroy_dictionary( OBJECT *poKey ,
                                         OBJECT *poValue ,
                                         void *data) ;

struct pdf_copyobj_dwfparams {
  PDFCONTEXT *pdfc ;
  OBJECT *destdict ;
} ;

typedef struct pdf_copyobj_dwfparams PDF_COPYOBJ_DWFPARAMS ;

/* This is the guts of the public function pdf_destroy_dictionary() minus the
 * extraction of the pdf execution context.  This macro is called directly by
 * pdf_copyobject() since there is a compiler optimization bug with VC6sp5 when
 * inlining pdf_destroy_dictionary() into pdf_copyobject().  When we change
 * Windows compiler (to VC7 or some other one) we should put everything back
 * into the original function.  See 50605.
 */
#define PDF_DESTROY_DICTIONARY(pdfxc, len, thedict, context) \
MACRO_START \
  OBJECT* dict; \
  HQASSERT(thedict , "thedict NULL in pdf_destroy_dictionary."); \
  HQASSERT(oType(*thedict) == ODICTIONARY, \
           "Non-dict passed into pdf_destroy_dictionary." ); \
  /* Before we free the dictionary we need to invalidate any fast
     NAMECACHE dict lookups. */ \
  (void)walk_dictionary((thedict), dictwalk_destroy_dictionary, context); \
  dict = oDict(*thedict) - 2; \
  HQASSERT(dict, "dict NULL in pdf_destroy_dictionary."); \
  PDF_FREEOBJECT((pdfxc), dict, NDICTOBJECTS(len)); \
MACRO_END

/* Clear the stack up to the given sequence point:
 * EMPTY_STACK ( == -1 ) to empty it.
 */

void pdf_clearstack( PDFCONTEXT *pdfc , STACK *pdfstack , int32 seqpoint )
{
  int32 ssize ;
  OBJECT *theo ;

  HQASSERT( pdfc , "pdfc NULL in pdf_clearstack." ) ;
  HQASSERT( pdfstack , "pdfstack NULL in pdf_clearstack." ) ;
  HQASSERT( seqpoint >= EMPTY_STACK ,
            "seqpoint out of range in pdf_clearstack." ) ;

  for ( ssize = theIStackSize( pdfstack ) ; ssize > seqpoint ; --ssize ) {
    theo = theITop( pdfstack ) ; ;
    pdf_freeobject( pdfc , theo ) ;
    pop( pdfstack ) ;
  }
}

/* Free the given object. Safe for simple objects, just
 * does nothing. Be careful when building compound objects
 * that will be freed with this routine - don't doubly
 * reference a child compound object!
 */
void pdf_freeobject_from_xc( PDFXCONTEXT *pdfxc , OBJECT *pdfobj )
{
  int32 i ;
  int32 len ;
  uint8  *clist ;
  OBJECT *alist ;
  Bool freed = TRUE ;

  HQASSERT( pdfobj , "pdfobj NULL in pdf_freeobject." ) ;

  if ( ++( pdfxc->recursion_depth ) > PDF_MAX_RECURSION_DEPTH )
    return ;

  switch ( oXType(*pdfobj) ) {
  case OINTEGER:
  case OREAL:
  case OBOOLEAN:
  case ONULL:
  case ONAME:
  case OINDIRECT:
  case OOPERATOR:
  case OFILEOFFSET:
    /* Nothing to free */
    break ;
  case ODICTIONARY: {
    corecontext_t *context = pdfxc->corecontext;
    OBJECT *dict;
    DPAIR *dp;
    int32 alloc_len;

    /* Free each of the values in the dictionary plus the dictionary itself. */
    len = theLen(*pdfobj) ;
    dict = oDict(*pdfobj); alloc_len = DICT_ALLOC_LEN(dict);
    if ( alloc_len == 0 && oType(dict[-1]) == ODICTIONARY ) {
      /* There's an extension, free the initial array, at original length */
      OBJECT *subdict = oDict(dict[-1]);
      uint16 sublen = theLen(dict[-1]);

      HQASSERT(theLen(*dict) == 0, "Initial array not empty");
      PDF_FREEOBJECT( pdfxc, dict-2, NDICTOBJECTS(len) );
      dict = subdict; len = sublen; alloc_len = DICT_ALLOC_LEN(dict);
    }
    for ( i = alloc_len, dp = (DPAIR*)(dict + 1) ; i > 0 ; --i, ++dp )
      if ( theTags( dp->key ) != ONOTHING ) {
        (void)dictwalk_destroy_dictionary( &dp->key, &dp->obj, context );
        pdf_freeobject_from_xc( pdfxc, &dp->obj );
      }
    PDF_FREEOBJECT( pdfxc, dict-2, NDICTOBJECTS(len) );
    break ; }
  case OSTRING:
    /* Free the string data. */
    len = theLen(*pdfobj) ;
    clist = oString(*pdfobj) ;
    if ( len ) {
      PDF_FREESTRING( pdfxc, clist , len ) ;
    }
    break ;
  case OLONGARRAY:
  case OLONGPACKEDARRAY:
    /* Free the array members */
    len = oLongArrayLen(*pdfobj) ;
    alist = oLongArray(*pdfobj) ;
    for ( i = len ; i > 0 ; --i , ++alist )
      pdf_freeobject_from_xc( pdfxc , alist ) ;
    /* Free the array and trampoline (one allocation - see pdf_scanarray) */
    alist = oArray(*pdfobj) ;
    PDF_FREEOBJECT( pdfxc , alist , len + 2 ) ;
    break ;
  case OARRAY:
  case OPACKEDARRAY:
    /* Free each of the objects in the array plus the array itself. */
    len = theLen(*pdfobj) ;
    if ( len ) {
      alist = oArray(*pdfobj) ;
      for ( i = len ; i > 0 ; --i , ++alist )
        pdf_freeobject_from_xc( pdfxc , alist ) ;
      len = theLen(*pdfobj) ;
      alist = oArray(*pdfobj) ;
      PDF_FREEOBJECT( pdfxc, alist , len ) ;
    }
    break ;
  case OFILE:
    /* The stream is to be marked as released. Even if this was the
       last live pointer to the stream, the xref cache entry shouldn't
       be freed straight away but rather stream frees are deferred. */
    pdf_xref_release_stream( pdfxc , pdfobj ) ;
    freed = FALSE ;
    break ;
  default:
    HQFAIL( "Unknown pdf object type." ) ;
  }

  if ( freed ) {
    theTags(*pdfobj) = ONOTHING ;
  }

  pdfxc->recursion_depth -= 1 ;

  HQASSERT( pdfxc->recursion_depth >= 0 ,
            "Recursion depth went below zero!" ) ;
}

/* Convenience wrapper for pdf_freeobject_from_xc().
 */
void pdf_freeobject( PDFCONTEXT *pdfc , OBJECT *pdfobj )
{
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  pdf_freeobject_from_xc(pdfxc, pdfobj);
}

/* Copy the given object - this is a deep copy. We intend eventually to give
 * all execution modes a context as PDF has now - including PostScript. For
 * now, though, the pdfc passed into this routine is defined as the
 * destination context and passing NULL as the pdfc means copy to PSVM.
 */

Bool pdf_copyobject( PDFCONTEXT *pdfc , OBJECT *srcobj , OBJECT *destobj )
{
  int32 i ;
  int32 len ;
  uint8  *clist ;
  OBJECT *alist ;
  PDFXCONTEXT *pdfxc = NULL ;
  corecontext_t *corecontext ;

  if ( pdfc ) {
    PDF_CHECK_MC( pdfc ) ;
    PDF_GET_XC( pdfxc ) ;

    if ( ++( pdfxc->recursion_depth ) > PDF_MAX_RECURSION_DEPTH )
      return error_handler( LIMITCHECK ) ;

    corecontext = pdfxc->corecontext ;
  } else {
    corecontext = get_core_context() ;
  }

  HQASSERT( srcobj , "srcobj NULL in pdf_copyobject." ) ;
  HQASSERT( destobj , "destobj NULL in pdf_copyobject." ) ;
  HQASSERT( srcobj != destobj , "srcobj == destobj in pdf_copyobject." ) ;

  switch ( oXType(*srcobj) ) {
  case OINTEGER:
  case OREAL:
  case OBOOLEAN:
  case ONULL:
  case ONAME:
  case OINDIRECT:
  case OOPERATOR:
  case OFILEOFFSET:
    Copy( destobj , srcobj ) ;
    break ;
  case ODICTIONARY: {
    PDF_COPYOBJ_DWFPARAMS params ;
    len = theLen(*srcobj);

    if ( pdfxc ) {
      if ( ! pdf_create_dictionary( pdfc , len , destobj ))
        return FALSE ;
    }
    else {
      if ( ! ps_dictionary(destobj, len))
        return FALSE ;
    }

    params.pdfc = pdfc ;
    params.destdict = destobj ;

    if ( !walk_dictionary( srcobj ,
                           pdf_copyobj_dictwalkfn ,
                           ( void * )& params )) {
      if ( pdfxc ) {
        /* Use macro to avoid VC6sp5 compiler optimisation bug */
        PDF_DESTROY_DICTIONARY(pdfxc, len, destobj, corecontext);
      }

      /* Rely on restore, I s'pose */
      return FALSE ;
    }
    break ;
  }
  case OSTRING:
    Copy( destobj , srcobj ) ;
    len = theLen(*srcobj) ;
    clist = oString(*srcobj) ;
    if ( len ) {
      if ( pdfxc ) {
        if ( !pdf_create_string(pdfc, len, destobj) )
          return FALSE ;
        HqMemCpy(oString(*destobj) , clist , len ) ;
      } else {
        if ( !ps_string(destobj, clist, len) )
          return error_handler( VMERROR ) ;
      }
    }
    break ;
  case OARRAY:
  case OPACKEDARRAY:
    Copy( destobj , srcobj ) ;
    len = theLen(*srcobj) ;
    if ( len ) {
      OBJECT *destalist ;
      alist = oArray(*srcobj) ;
      if ( pdfxc ) {
        destalist = PDF_ALLOCOBJECT( pdfxc , len ) ;
      } else
        destalist = get_omemory( len ) ;
      if ( !destalist )
        return error_handler( VMERROR ) ;
      oArray(*destobj) = destalist ;
      if ( pdfxc == NULL )
        SETGLOBJECT(*destobj, corecontext) ;
      for ( i = len ; i > 0 ; --i, ++alist, ++destalist ) {
        if ( !pdf_copyobject( pdfc , alist , destalist )) {
          if ( pdfxc ) {
            PDF_FREEOBJECT( pdfxc, oArray(*destobj) , len ) ;
            Copy(destobj, &onull);
          }
          else {
            /* Rely on restore, I s'pose */
          }
          return FALSE ;
        }
      }
    }
    break ;
  case OLONGARRAY:
  case OLONGPACKEDARRAY:
    {
      OBJECT *destalist ;
      Copy( destobj , srcobj ) ;
      len = oLongArrayLen(*srcobj) ; /* always > 65535 */
      if ( pdfxc ) {
        destalist = PDF_ALLOCOBJECT( pdfxc , len + 2 ) ;
      } else
        destalist = get_omemory( len + 2 ) ;
      if ( !destalist )
        return error_handler( VMERROR ) ;

      alist = oArray(*srcobj) ;       /* The old trampoline */
      oArray(*destobj) = destalist ;  /* The new trampoline */
      Copy(destalist++, alist++) ;    /* Length */
      Copy(destalist, alist) ;        /* array ptr */

      oArray(*destalist) = destalist + 1 ;
      destalist += 1 ;
      alist = oArray(*alist) ;

      if ( pdfxc == NULL ) {
        SETGLOBJECT(*destobj, corecontext) ;
        SETGLOBJECT(*(destalist-2), corecontext) ;
        SETGLOBJECT(*(destalist-1), corecontext) ;
      }
      for ( i = len ; i > 0 ; --i, ++alist, ++destalist ) {
        if ( !pdf_copyobject( pdfc , alist , destalist )) {
          if ( pdfxc ) {
            PDF_FREEOBJECT( pdfxc, oArray(*destobj) , len + 2 ) ;
            Copy(destobj, &onull);
          }
          else {
            /* Rely on restore, I s'pose */
          }
          return FALSE ;
        }
      }
    }
    break ;
  case OFILE:
    {
      PDFXCONTEXT *streamxc ;

      /* Streams are different to other objects: we don't deep copy
         them but rather mark them as being used on this page. */

      if ( ! pdf_find_execution_context( theIPDFContextID( oFile( *srcobj )) ,
                                         pdfin_xcontext_base ,
                                         & streamxc )) {
        /* Note that if we ever revive pdfout, we'd have to check
           pdfout_xcontext_base here too. */
        HQFAIL( "Couldn't find stream execution context" ) ;
        return FALSE ;
      }
      HQASSERT( streamxc != NULL , "Null context" ) ;

      if ( ! pdf_xrefexplicitaccess_stream( streamxc , srcobj , FALSE )) {
        return FALSE ;
      }
      Copy( destobj , srcobj ) ;
    }
    break ;
  default:
    HQFAIL( "Trying to copy an unsupported/undefined pdf object type." ) ;
  }

  if ( pdfxc ) {
    pdfxc->recursion_depth -= 1 ;

    HQASSERT( pdfxc->recursion_depth >= 0 ,
              "Recursion depth went below zero!" ) ;
  }

  return TRUE ;
}

OBJECT *pdf_objmemallocfunc(int32 numobjs, void * param)
{
  PDFXCONTEXT *pdfxc = param ;
  OBJECT* obj ;

  /* allocate some local PDF VM for an object of the given size */
  HQASSERT(pdfxc != NULL, "PDF object memory pool NULL") ;
  HQASSERT(numobjs > 0, "Should be at least one object to allocate") ;

  obj = (OBJECT*)mm_alloc(pdfxc->mm_object_pool, sizeof(OBJECT) * numobjs,
                          MM_ALLOC_CLASS_PDF_PSOBJECT);
  if (obj != NULL) {
    OBJECT *slot ;
    OBJECT transfer ;

    theMark(transfer) = (int8)(ISNOTVM | ISLOCAL | pdfxc->savelevel) ;
    theTags(transfer) = ONULL ;
    theLen(transfer) = 0 ;

    for ( slot = obj ; numobjs > 0 ; ++slot, --numobjs ) {
      OBJECT_SET_D0(*slot, OBJECT_GET_D0(transfer)) ; /* Set slot properties */
      OBJECT_SCRIBBLE_D1(*slot) ;
    }
  }

  return obj;
}

static Bool pdf_copyobj_dictwalkfn( OBJECT *thek , OBJECT *theo , void *params )
{
  OBJECT theocopy = OBJECT_NOTVM_NOTHING ;
  PDF_COPYOBJ_DWFPARAMS *dwfparams = ( PDF_COPYOBJ_DWFPARAMS * )params ;
  PDFCONTEXT *pdfc ;
  OBJECT * (*alloc_func)(int32 size, void * params) ;
  void *alloc_data ;

  HQASSERT( theo , "theo NULL in pdf_copyobj_dictwalkfn." ) ;
  HQASSERT( thek , "thek NULL in pdf_copyobj_dictwalkfn." ) ;
  HQASSERT( dwfparams , "dwfparams NULL in pdf_copyobj_dictwalkfn." ) ;

  pdfc = dwfparams->pdfc ;

  /* Insert object using either the PDF object pool or PS VM */
  if ( pdfc ) {
    PDFXCONTEXT *pdfxc ;
    PDF_CHECK_MC( pdfc ) ;
    PDF_GET_XC( pdfxc ) ;

    alloc_func = pdf_objmemallocfunc ;
    alloc_data = pdfxc ;
  }
  else {
    alloc_func = PSmem_alloc_func ;
    alloc_data = NULL ;
  }

  return ( pdf_copyobject( pdfc , theo , & theocopy ) &&
           insert_hash_with_alloc(dwfparams->destdict, thek, & theocopy,
                                  INSERT_HASH_NAMED|INSERT_HASH_DICT_ACCESS,
                                  alloc_func, alloc_data) ) ;
}

/* Create a PDF dictionary with room for len pairs. */

Bool pdf_create_dictionary( PDFCONTEXT *pdfc , int32 len , OBJECT *thedict )
{
  OBJECT *dict ;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  HQASSERT( thedict , "thedict NULL in pdf_create_dictionary." ) ;

  if ( len < 0 )
    return error_handler( RANGECHECK ) ;
  if ( len > MAXPSDICT )
    return error_handler( LIMITCHECK ) ;

  dict = PDF_ALLOCOBJECT( pdfxc, (int32)NDICTOBJECTS(len) );
  if ( !dict )
    return error_handler( VMERROR ) ;

  init_dictionary(thedict, len, UNLIMITED, dict,
                  ISNOTVMDICTMARK(pdfxc->savelevel)) ;

  return TRUE ;
}

Bool pdf_create_array( PDFCONTEXT *pdfc , int32 len , OBJECT *thearray )
{
  OBJECT *array ;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  HQASSERT( thearray , "thearray NULL in pdf_create_array." ) ;

  if ( len < 0 )
    return error_handler( RANGECHECK ) ;
  if ( len > MAXPSARRAY )
    return error_handler( LIMITCHECK ) ;

  if ( len > 0 ) {
    array = PDF_ALLOCOBJECT( pdfxc, len ) ;
    if ( ! array )
      return error_handler( VMERROR ) ;
  }
  else
    array = NULL ;

  theTags(*thearray) = OARRAY | UNLIMITED | LITERAL ;
  SETGLOBJECTTO(*thearray, FALSE) ; /* array itself is local */
  theLen(*thearray) = ( uint16 )len ;
  oArray(*thearray) = array ;

  return TRUE ;
}

Bool pdf_create_string( PDFCONTEXT *pdfc , int32 len , OBJECT *thestring )
{
  uint8 *string ;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  HQASSERT( thestring , "thestring NULL in pdf_create_string." ) ;

  if ( len < 0 )
    return error_handler( RANGECHECK ) ;
  if ( len > MAXPSSTRING )
    return error_handler( LIMITCHECK ) ;

  if ( len > 0 ) {
    string = PDF_ALLOCSTRING( pdfxc, len ) ;
    if ( ! string )
      return error_handler( VMERROR ) ;
  }
  else
    string = NULL ;

  theTags(*thestring) = OSTRING | UNLIMITED | LITERAL ;
  SETGLOBJECTTO(*thestring, FALSE) ; /* string itself is local */
  theLen(*thestring) = ( uint16 )len ;
  oString(*thestring) = string ;

  return TRUE ;
}

/* These are SHALLOW disposes - any compound objects inside the dict/array
 * will not be freed. Call these functions only if that is what you want.
 */

static Bool dictwalk_destroy_dictionary( OBJECT *poKey ,
                                         OBJECT *poValue ,
                                         void *data )
{
  corecontext_t *context = data;

  UNUSED_PARAM( OBJECT * , poValue ) ;

  /* Invalidate shallow cache entry. */
  if ( oType(*poKey) == ONAME ) {
    NAMECACHE *nptr = oName(*poKey) ;
    HQASSERT( nptr->dictval != NULL ,
              "Got this entry from a dict so reset must be non-NULL" ) ;
    if ( context->savelevel <= SAVELEVELINC )
      nptr->dictcpy = NULL ;        /* Invalidate reset */
    nptr->dictobj = NULL ;
  }

  return TRUE ;
}

void pdf_destroy_dictionary( PDFCONTEXT *pdfc , int32 len , OBJECT *thedict )
{
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  /* Expand macro when we next change compiler on Windows to see if optimization
   * bug is no longer present.
   */
  PDF_DESTROY_DICTIONARY(pdfxc, len, thedict, pdfc->corecontext);
}

void pdf_destroy_array( PDFCONTEXT *pdfc , int32 len , OBJECT *thearray )
{
  PDF_CHECK_MC( pdfc ) ;
  HQASSERT( len >=0 , "len must be positive" ) ;
  HQASSERT( thearray != NULL , "thearray NULL in pdf_destroy_array" ) ;
  HQASSERT( oType(*thearray) == OARRAY ||
            oType(*thearray) == OPACKEDARRAY ,
            "Non-array passed into pdf_destroy_array" ) ;

  if ( len != 0 )
  {
    PDFXCONTEXT *pdfxc ;
    OBJECT *array ;

    PDF_GET_XC( pdfxc ) ;

    array = oArray(*thearray) ;
    HQASSERT( array != NULL , "array NULL in pdf_destroy_array" ) ;

    PDF_FREEOBJECT( pdfxc, array , len ) ;
  }
  theTags(*thearray) = ONOTHING ;
}

void pdf_destroy_string( PDFCONTEXT *pdfc , int32 len , OBJECT *thestring )
{
  PDF_CHECK_MC( pdfc ) ;
  HQASSERT( len >=0 , "len must be positive" ) ;
  HQASSERT( thestring != NULL , "thestring NULL in pdf_destroy_string" ) ;
  HQASSERT( oType(*thestring) == OSTRING ,
            "Non-string passed into pdf_destroy_string" ) ;

  if ( len != 0 )
  {
    PDFXCONTEXT *pdfxc ;
    uint8 *string ;

    PDF_GET_XC( pdfxc ) ;

    string = oString(*thestring) ;
    HQASSERT( string != NULL , "string NULL in pdf_destroy_string." ) ;

    PDF_FREESTRING( pdfxc, string , len ) ;
  }
  theTags(*thestring) = ONOTHING ;
}

/* Like the PS matrix_, but uses PDF memory and doesn't push the result. */

Bool pdf_matrix( PDFCONTEXT *pdfc , OBJECT *theo)
{
  register OBJECT *olist ;
  PDFXCONTEXT *pdfxc ;
  int i ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  HQASSERT( theo , "theo NULL in pdf_matrix." ) ;

  if ( NULL == ( olist = ( OBJECT * )PDF_ALLOCOBJECT( pdfxc , 6 )))
    return error_handler( VMERROR ) ;

  theTags(*theo) = OARRAY | LITERAL | UNLIMITED ;
  SETGLOBJECTTO(*theo, FALSE) ;
  theLen(*theo) = 6 ;
  oArray(*theo) = olist ;

  for ( i=0; i<6; i++ ) {
    theTags( olist[i] ) = OREAL | LITERAL ;
    oReal(olist[i]) = (i % 3 == 0) ? 1.0f : 0.0f ;
  }

  return TRUE ;
}

/* Make a new PDF PS string object with the given value. */

Bool pdf_buildstring( PDFCONTEXT *pdfc , uint8 *string , OBJECT *theo )
{
  int32 len ;

  HQASSERT( string , "string NULL in pdf_buildstring." ) ;
  HQASSERT( theo , "theo NULL in pdf_buildstring." ) ;

  len = strlen_int32( ( char * )string ) ;

  if ( ! pdf_create_string( pdfc , len , theo ))
    return FALSE ;

  HqMemCpy(oString(*theo) , string , len ) ;

  return TRUE ;
}

Bool pdf_fast_insert_hash(PDFCONTEXT *pdfc,
                          OBJECT *thed, OBJECT *thekey, OBJECT *theo)
{
  PDFXCONTEXT*    pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  return insert_hash_with_alloc(thed, thekey, theo,
                                INSERT_HASH_NAMED|INSERT_HASH_DICT_ACCESS,
                                pdf_objmemallocfunc, pdfxc);
}

/* Insert the passed name/object pair into the passed dictionary. 'name'
   should just be the sybolic name number as defined in a namedef file, e.g.
*/
Bool pdf_fast_insert_hash_name(PDFCONTEXT *pdfc, OBJECT *dict,
                               int32 nameNumber, OBJECT *value)
{
  OBJECT name = OBJECT_NOTVM_NOTHING ;
  object_store_name(&name, nameNumber, LITERAL);
  return pdf_fast_insert_hash(pdfc, dict, &name, value);
}

/* Log stripped */
