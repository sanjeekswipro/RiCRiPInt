/** \file
 * \ingroup pdf
 *
 * $HopeName: COREpdf_base!src:pdfxref.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Xref table access functions
 */

#include "core.h"
#include "pdfxref.h"

#include "debugging.h"
#include "swdevice.h"
#include "swerrors.h"
#include "objects.h"
#include "chartype.h"
#include "mm.h"
#include "monitor.h"
#include "psvm.h"

#include "dictops.h"
#include "fileio.h"
#include "gsc_icc.h"

#include "swpdf.h"
#include "pdfmem.h"
#include "pdfcntxt.h"
#include "pdfstrm.h"
#include "stream.h"
#include "namedef_.h"


/* Private Types */

/** Parameter structure, used when calling walk_dictionary(). */
typedef struct {
  PDFCONTEXT* pdfc;
  Bool isStreamDictionary;
} PdfResolveParams;


/* Static Prototypes */

static void pdf_freexrefcache( PDFCONTEXT *pdfc , XREFCACHE *xrefcache ) ;
static Bool pdf_resxref_dictwalkfn( OBJECT *thek , OBJECT *theo ,
                                    void *params ) ;
static Bool pdf_lookupxref_with_id( PDFCONTEXT *pdfc , OBJECT **rpdfobj ,
                                    int32 objnum , uint16 objgen ,
                                    Bool streamDictOnly ,
                                    Bool* idMatch ) ;
static Bool pdf_resolvexrefs_internal( PDFCONTEXT *pdfc , OBJECT *theo ) ;
static Bool pdf_set_xref_last_access_recurse( PDFXCONTEXT *pdfxc ,
                                              OBJECT *theo , int32 pageId ) ;
static void icc_callback(void *data, int objnum) ;

/** Walk the entire cache, freeing all cache objects which are probably
 * no longer needed.
 *
 * There are two separate ways of ensuring a cache object is no longer needed
 *   a) Objects relating to a content stream are marked with the page-number
 *      of the stream they were created from. Then once we move on to a new
 *      page, previous page objects become candidates for freeing.
 *   b) Objects created outside of a content stream (e.g. resources, page
 *      trees). These are marked with the call-stack level they were created
 *      at, and can be freed once we return to a higher level.
 *
 * If depth > 0, free all objects created at a higher depth. Otherwise just
 * free all content-stream objects not associated with the current page.
 * However, if closing is true, we are shutting-down, so just free everything.
 *
 * Returns TRUE if anything was deallocated.
 */
Bool pdf_sweepxref( PDFCONTEXT *pdfc, Bool closing, int32 depth)
{
  int32 i ;
  Bool result = FALSE;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  /* Each entry in the cache array is a linked list. */
  for ( i = 0 ; i < XREF_CACHE_SIZE ; i++ ) {
    XREFCACHE **cacheLink = & pdfxc->xrefcache[ i ] ;

    while ( *cacheLink != NULL ) {
      XREFCACHE *cache = *cacheLink;
      Bool kill = FALSE;

      if ( closing ) {
        kill = TRUE;
      } else if ( depth < 0 ) {
        if ( cache->lastAccessId >= 0 &&
             cache->lastAccessId != pdfxc->pageId ) {
          kill = TRUE;
        }
      } else {
        if ( cache->lastAccessId < 0 && -cache->lastAccessId > depth ) {
          kill = TRUE;
        }
      }

      if ( kill ) {
        cache->flushable = TRUE ;
        result = TRUE ;
      }

      cacheLink = &cache->xrefnxt ;
    }
  }
  pdfxc->lowmemXrefCount = 0 ;
  pdf_deferred_xrefcache_flush( pdfxc ) ;
  return result ;
}

#if defined( DEBUG_BUILD )
/** Debug-only routine to dump information to the monitor about how
    many xref cache entries we have for each page. Useful if we ever
    need to review how the cache is working. */

void pdf_xrefpagetotals( PDFCONTEXT *pdfc )
{
  int32 i ;
  PDFXCONTEXT *pdfxc ;
  uint32 *count = NULL ;
  int32 min_page = MAXINT32 ;
  int32 max_page = MININT32 ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  for ( i = 0 ; i < XREF_CACHE_SIZE ; i++ ) {
    XREFCACHE **cacheLink = & pdfxc->xrefcache[ i ] ;

    while ( *cacheLink != NULL ) {
      XREFCACHE *cache = *cacheLink ;

      if ( cache->lastAccessId > max_page ) {
        max_page = cache->lastAccessId ;
      }

      if ( cache->lastAccessId < min_page ) {
        min_page = cache->lastAccessId ;
      }

      cacheLink = & cache->xrefnxt ;
    }
  }

  count = mm_alloc( pdfxc->mm_structure_pool ,
                    sizeof( *count ) * ( 1 + max_page - min_page ) ,
                    MM_ALLOC_CLASS_UNDECIDED ) ;

  if ( ! count ) {
    HQFAIL( "Out of memory: no error handling since this "
            "is debug-only code." ) ;
    ( void )error_handler( VMERROR );
    return ;
  }

  HqMemZero((uint8 *)count , sizeof( *count ) * ( 1 + max_page - min_page ));

  for ( i = 0 ; i < XREF_CACHE_SIZE ; i++ ) {
    XREFCACHE **cacheLink = & pdfxc->xrefcache[ i ] ;

    while ( *cacheLink != NULL ) {
      XREFCACHE *cache = *cacheLink ;

      count[ cache->lastAccessId - min_page ]++ ;
      cacheLink = & cache->xrefnxt ;
    }
  }

  for ( i = 0 ; i <= ( max_page - min_page ) ; i++ ) {
    if ( count[ i ] > 0 ) {
      monitorf((uint8*)"Page %d: %u\n" , i + min_page , count[ i ]) ;
    }
  }

  mm_free( pdfxc->mm_structure_pool , count ,
           sizeof( *count ) * ( 1 + max_page - min_page )) ;
}

/** Debug-only routine to detail to the monitor what if any xref cache
    objects we are holding for the given page. */

void pdf_xrefcache_dumppage( PDFCONTEXT *pdfc, int32 page )
{
  int32 i ;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  /* Each entry in the cache array is a linked list. */
  for ( i = 0 ; i < XREF_CACHE_SIZE ; i++ ) {
    XREFCACHE **cacheLink = & pdfxc->xrefcache[ i ] ;

    while ( *cacheLink != NULL ) {
      XREFCACHE *cache = *cacheLink;

      if ( cache->lastAccessId == page ) {
        FILELIST *flptr = theIStderr( workingsave ) ;
        monitorf(( uint8 * ) "%d %d obj\n" , cache->objnum , cache->objgen ) ;
        debug_print_object( &cache->pdfobj ) ;
        if (( *theIMyFlushFile( flptr ))( flptr ) == EOF ) {
          HQFAIL( "Flush failed" ) ;
          return ;
        }
        monitorf(( uint8 * ) "\n" ) ;
      }

      cacheLink = &cache->xrefnxt;
    }
  }
}
#endif

/** Sweep the xref cache entries which were last accessed during
    execution of the given page. */

void pdf_sweepxrefpage( PDFCONTEXT *pdfc, int32 page )
{
  PDFXCONTEXT *pdfxc ;
  int32 i ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  gsc_protectICCCache( pdfxc->id , icc_callback , pdfxc ) ;

  /* Each entry in the cache array is a linked list. */
  for ( i = 0 ; i < XREF_CACHE_SIZE ; i++ ) {
    XREFCACHE **cacheLink = & pdfxc->xrefcache[ i ] ;

    while ( *cacheLink != NULL ) {
      XREFCACHE *cache = *cacheLink;

      if ( cache->lastAccessId == page ) {
        /* Mark the stream as waiting to be freed. */
        cache->flushable = TRUE ;
      }

      cacheLink = &cache->xrefnxt ;
    }
  }
}

/** Set the lastAccessId to zero for all objects from the cache which
    have a non-negative lastAccessId. Used for resetting the cache at
    the end of a pass over a page range, to ensure that we're ready
    for any possible next pass. Any extant entries when this is called
    will be aged out of the cache as normal during the next pass if
    they're not used in the next \c XRefCacheLifetime pages. */

void pdf_xrefreset( PDFCONTEXT *pdfc )
{
  PDFXCONTEXT *pdfxc ;
  int32 i ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  for ( i = 0 ; i < XREF_CACHE_SIZE ; i++ ) {
    XREFCACHE *cache ;

    for ( cache = pdfxc->xrefcache[ i ] ; cache != NULL ;
          cache = cache->xrefnxt ) {
      if ( cache->lastAccessId > 0 ) {
        cache->lastAccessId = 0 ;
      }
    }
  }
}

/** Free the xref cache entry just for the identified object
    number. */

void pdf_xrefexplicitpurge( PDFCONTEXT *pdfc, int32 objnum )
{
  PDFXCONTEXT *pdfxc ;
  XREFCACHE *cache ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  gsc_protectICCCache( pdfxc->id , icc_callback , pdfxc ) ;

  for ( cache = pdfxc->xrefcache[ objnum & (XREF_CACHE_SIZE-1)] ;
        cache != NULL ; cache = cache->xrefnxt ) {
    if ( cache->objnum == objnum ) {
      /* Mark the stream as waiting to be freed. */
      cache->flushable = TRUE ;
      return ;
    }
  }
}

/** We store the xref cache slot number inside a stream - this routine
    retrieves it for internal use inside this module. Note that it
    might be tempting to think that the cache slot object should be
    OINDIRECT rather than OINTEGER, but by the time a cache slot is in
    use the generation number is irrelevant - and more importantly
    some code such as the ICC cache code will do more than one
    pdf_resolvexrefs call and that would lead to confusion. It's
    possible for stream object here not to be ultimately derived from
    a StreamDecode filter - annotations have streams which are string
    data sources for instance. */
static Bool pdf_stream_slot( OBJECT *stream , int32 *objnum )
{
  OBJECT *dict ;
  OBJECT *ind ;

  dict = streamLookupDict( stream ) ;
  if ( dict == NULL || oType( *dict ) != ODICTIONARY ) {
    return FALSE ;
  }

  ind = fast_extract_hash_name( dict , NAME_HqnCacheSlot ) ;
  if ( ind == NULL ) {
    return FALSE ;
  }

  HQASSERT( oType( *ind ) == OINTEGER ,
            "Invalid cache slot telltale for stream" ) ;

  HQASSERT( objnum != NULL , "Null objnum pointer" ) ;
  *objnum = oInteger( *ind ) ;

  return TRUE ;
}

/** Params struct for recursively setting cache lastAccessId
    values. */
typedef struct {
  PDFXCONTEXT *pdfxc ;
  int32 pageId ;
  Bool isStreamDictionary ;
}
PDF_XREF_LAST_ACCESS_PARAMS ;

/** Routine for recursively setting cache lastAccessId values in
    dictionaries. */
static Bool pdf_set_xref_last_access_dictwalkfn( OBJECT *thek , OBJECT *theo ,
                                                 void *params )
{
  PDF_XREF_LAST_ACCESS_PARAMS *lap = ( PDF_XREF_LAST_ACCESS_PARAMS * )params ;

  UNUSED_PARAM( OBJECT * , thek ) ;

  HQASSERT( theo , "theo NULL in pdf_set_xref_last_access_dictwalkfn.\n" ) ;
  HQASSERT( lap->pdfxc != NULL ,
            "pdfxc NULL in pdf_set_xref_last_access_dictwalkfn.\n" ) ;

  if ( lap->isStreamDictionary ) {
    /* We need to avoid infinite recursion here just like
       pdf_resxref_dictwalkfn, but we don't need to worry about
       Resources. */
    if ( oNameNumber(*thek) == NAME_DataSource ||
         oNameNumber(*thek) == NAME_HqnCacheSlot ||
         oNameNumber(*thek) == NAME_Thresholds )
      return TRUE;
  }

  return pdf_set_xref_last_access_recurse( lap->pdfxc , theo , lap->pageId ) ;
}

/** Internal routine for recursively setting cache lastAccessId
    values. */
static Bool pdf_set_xref_last_access_recurse( PDFXCONTEXT *pdfxc ,
                                              OBJECT *theo , int32 pageId )
{
  HQASSERT( pdfxc , "pdfxc NULL in pdf_set_xref_last_access_recurse.\n" ) ;
  HQASSERT( theo , "theo NULL in pdf_set_xref_last_access_recurse.\n" ) ;

  if ( ++( pdfxc->recursion_depth ) > PDF_MAX_RECURSION_DEPTH )
    return error_handler( LIMITCHECK ) ;

  switch ( oType( *theo )) {
    case OARRAY:
    case OPACKEDARRAY:
      {
        int32 len = theLen(*theo) ;
        OBJECT *olist = oArray( *theo ) ;
        int32 i ;

        for ( i = 0 ; i < len ; i ++ ) {
          if ( ! pdf_set_xref_last_access_recurse( pdfxc , olist++ , pageId )) {
            return FALSE ;
          }
        }

        break ;
      }

    case ODICTIONARY:
      {
        PDF_XREF_LAST_ACCESS_PARAMS params ;

        params.pdfxc = pdfxc ;
        params.pageId = pageId ;
        params.isStreamDictionary = FALSE ;

        if ( ! walk_dictionary( theo , pdf_set_xref_last_access_dictwalkfn ,
                                & params )) {
          return FALSE ;
        }

        break ;
      }

    case OFILE:
      {
        OBJECT *dict = streamLookupDict( theo ) ;
        OBJECT *ind ;
        PDF_XREF_LAST_ACCESS_PARAMS params ;

        params.pdfxc = pdfxc ;
        params.pageId = pageId ;
        params.isStreamDictionary = TRUE ;

        if ( ! walk_dictionary( dict , pdf_set_xref_last_access_dictwalkfn ,
                                & params )) {
          return FALSE ;
        }

        ind = fast_extract_hash_name( dict , NAME_HqnCacheSlot ) ;
        if ( ind != NULL ) {
          XREFCACHE *cache ;
          int32 objnum ;

          HQASSERT( oType( *ind ) == OINTEGER ,
                    "Invalid cache slot telltale for stream" ) ;
          objnum = oInteger( *ind ) ;

          for ( cache = pdfxc->xrefcache[ objnum & (XREF_CACHE_SIZE-1)] ;
                cache != NULL ; cache = cache->xrefnxt ) {
            if ( cache->objnum == oInteger( *ind ) &&
                 cache->lastAccessId != pageId &&
                 ( cache->lastAccessId >= 0 || pageId < cache->lastAccessId )) {
              cache->lastAccessId = pageId ;
              break ;
            }
          }
        }
        break;
      }

    default:
      /* Nothing to do */
      break ;
  }

  pdfxc->recursion_depth -= 1 ;

  HQASSERT( pdfxc->recursion_depth >= 0 ,
            "Recursion depth went below zero!" ) ;

  return TRUE ;
}

/** In all cases where previously we would have assigned to a cache's
    lastAccessId directly, now we should use this routine. It's
    crucial because it sets the appropriate value in any recursively
    referenced xref objects too. We might have believed that such a
    thing wasn't possible because of the flattening of structure
    involved in pdf_resolvexrefs, but the key thing to remember is
    that we treat streams as a special case. */
static Bool pdf_set_xref_last_access( PDFXCONTEXT *pdfxc , XREFCACHE *cache ,
                                      int32 pageId )
{
  /* We allow the change only if the existing access ID is not
     negative or if the change would make it more negative. */
  if ( cache->lastAccessId >= 0 || pageId < cache->lastAccessId ) {
    cache->lastAccessId = pageId ;
  }

  return pdf_set_xref_last_access_recurse( pdfxc , & cache->pdfobj , pageId ) ;
}

/** Set the last reference id to the current context page for the
    cache slot in question. Controls cache lifetimes for various
    reasons like the CFF downloader needing its objects to have the
    same lifetime as the job, or to update subordinate streams so they
    aren't freed too early. */

Bool pdf_xrefexplicitaccess( PDFXCONTEXT *pdfxc , int32 objnum ,
                             Bool permanent )
{
  XREFCACHE *cache ;

  HQASSERT( objnum > 0 , "Invalid xref cache slot" ) ;

  for ( cache = pdfxc->xrefcache[ objnum & (XREF_CACHE_SIZE-1)] ;
        cache != NULL ; cache = cache->xrefnxt ) {
    if ( cache->objnum == objnum ) {
      return pdf_set_xref_last_access( pdfxc , cache ,
                                       permanent ? MININT32 : pdfxc->pageId ) ;
    }
  }

  return TRUE ;
}


/** Make an RSD on top of the given stream in the xref cache, if it
    doesn't have one already, retrieving its xref cache slot
    identifier as necessary. */

Bool pdf_xrefmakeseekable_stream( PDFXCONTEXT *pdfxc , OBJECT *stream )
{
  XREFCACHE *cache ;
  int32 objnum = -1 ;

  if ( ! pdf_stream_slot( stream , & objnum )) {
    return FALSE ;
  }

  HQASSERT( objnum != -1 , "Invalid xref cache slot" ) ;

  for ( cache = pdfxc->xrefcache[ objnum & (XREF_CACHE_SIZE-1)] ;
        cache != NULL ; cache = cache->xrefnxt ) {
    if ( cache->objnum == objnum ) {
      if ( ! isIRSDFilter( oFile( cache->pdfobj ))) {
        OBJECT onameFilterType = OBJECT_NOTVM_NAME( NAME_ReusableStreamDecode ,
                                                    LITERAL ) ;
        OBJECT odictFilterArgs = OBJECT_NOTVM_NOTHING ;

        if ( ! pdf_create_dictionary( pdfxc->pdfc , 0 , & odictFilterArgs ) ||
             ! pdf_createfilter( pdfxc->pdfc , & cache->pdfobj ,
                                 & onameFilterType , & odictFilterArgs ,
                                 FALSE )) {
          return FALSE ;
        }
        Copy( stream , & cache->pdfobj ) ;
      }
      return TRUE ;
    }
  }

  return FALSE ;
}

/** Set the last reference id for the given stream, retrieving its
    xref cache slot identifier as necessary. */

Bool pdf_xrefexplicitaccess_stream( PDFXCONTEXT *pdfxc , OBJECT *stream ,
                                    Bool permanent )
{
  int32 objnum = -1 ;

  if ( ! pdf_stream_slot( stream , & objnum )) {
    return FALSE ;
  }

  return pdf_xrefexplicitaccess( pdfxc , objnum , permanent ) ;
}

/** Set the last reference id for all indirect objects in the given
    dict which have corresponding entries in the \c
    NAMETYPEMATCH. Useful for quickly but selectively tagging child
    objects with the same last reference id as the parent. */

Bool pdf_xrefexplicitaccess_dictmatch( PDFXCONTEXT *pdfxc ,
                                       NAMETYPEMATCH *match ,
                                       OBJECT *dict , Bool permanent )
{
  int i ;

  for ( i = 0 ; ( int32 )( match[ i ].name ) != END_MATCH_MARKER ; i++ ) {
    OBJECT key = OBJECT_NOTVM_NAME( NAME_PDF , LITERAL ) ;
    OBJECT *theo ;

    oName( key ) = theIMName( & match[ i ]) ;
    theo = fast_extract_hash( dict , & key ) ;
    if ( theo != NULL && oType( *theo ) == OINDIRECT ) {
      if ( ! pdf_xrefexplicitaccess( pdfxc , oXRefID( *theo ) , permanent )) {
        return FALSE ;
      }
    }
  }

  return TRUE ;
}

/** Forces the last reference id of the given stream to be \c
    pdfxc->pageId. Used in specific situations where the caller knows
    definitevely that this will not cause problems with the xref cache
    lifetime of the object concerned and will allow us to claw the
    memory back as soon as possible. */

void pdf_xrefthispageonly( PDFXCONTEXT *pdfxc , OBJECT *stream )
{
  XREFCACHE *cache ;
  int32 objnum = -1 ;

  if ( ! pdf_stream_slot( stream , & objnum )) {
    return ;
  }

  HQASSERT( objnum != -1 , "Invalid xref cache slot" ) ;

  for ( cache = pdfxc->xrefcache[ objnum & (XREF_CACHE_SIZE-1)] ;
        cache != NULL ; cache = cache->xrefnxt ) {
    if ( cache->objnum == objnum ) {
      /* No need to call pdf_set_xref_last_access here: deliberately
         this overrides whatever value is already there and is not
         applied recursively. */
      cache->lastAccessId = pdfxc->pageId ;
      return ;
    }
  }
}

/** Designed for protecting ICC file streams which are cached independent of the
 * PDF xref cache. We want to protect them to avoid making the ICC cache
 * inconsistent.
 */
static void icc_callback(void *data, int objnum)
{
  XREFCACHE *cache;
  PDFXCONTEXT *pdfxc = data;

  cache = pdfxc->xrefcache[ objnum & (XREF_CACHE_SIZE-1) ];

  while ( cache != NULL ) {
    if ( cache->objnum == objnum ) {
      ( void )pdf_set_xref_last_access( pdfxc , cache , MININT32 ) ;
      break;
    }
    cache = cache->xrefnxt;
  }
}

size_t pdf_measure_sweepable_xrefs(PDFCONTEXT *pdfc)
{
  size_t i, count = 0;
  PDFXCONTEXT *pdfxc;

  PDF_CHECK_MC( pdfc );
  PDF_GET_XC( pdfxc );

  /* We only purge objects from previous pages, and the handler purges
   * everything it can in one call. So there's no point measuring more
   * than once per page.
   */
  if (!pdfxc->lowmemRedoXrefs ||
      pdfxc->lowmemXrefPageId == pdfxc->pageId)
    return pdfxc->lowmemXrefCount;

  gsc_protectICCCache(pdfxc->id, icc_callback, pdfxc);

  /* Each entry in the cache array is a linked list. */
  for ( i = 0 ; i < XREF_CACHE_SIZE ; i++ ) {
    XREFCACHE *cache = pdfxc->xrefcache[i];

    while ( cache != NULL ) {
      if ( cache->lastAccessId != pdfxc->pageId
           && cache->lastAccessId >= 0 )
        /* @@@@ guess 3 slots as average object size */
        count += sizeof(XREFCACHE) + 3 * sizeof(OBJECT);
      cache = cache->xrefnxt;
    }
  }

  pdfxc->lowmemRedoXrefs = FALSE;
  pdfxc->lowmemXrefPageId = pdfxc->pageId;
  pdfxc->lowmemXrefCount = count;

  return count;
}

/* ---------------------------------------------------------------------- */

#if defined( ASSERT_BUILD )
static Bool pdftrace_xreflookups = FALSE ;
#endif

/** pdf_lookupxref
 * --------------
 * Call this to lookup an indirect object, given its number and generation,
 * and get an in-memory pointer to it.  The object will be read into cache
 * from disk if necessary. The object will remain in the cache until the current
 * page has been rendered, allowing it to be accessed during rendering if
 * needed.
 *
 * Pass the address of your OBJECT-pointer in "rpdfobj".
 *
 * Pass "objnum" and "objgen" to identify the object.
 *
 * Set "streamDictOnly" if stream objects needn't be constructed along
 * with their filter chains because the caller only needs the
 * dictionary.
 *
 * Returns:
 * Your OBJECT-pointer "*rpdfobj" is set to point to the cached OBJECT.
 */
Bool pdf_lookupxref( PDFCONTEXT *pdfc , OBJECT **rpdfobj ,
                     int32 objnum , uint16 objgen , Bool streamDictOnly )
{
  Bool match;
  Bool result ;

  GET_PDFXC_AND_IXC;

  pdfxc->nestedObjnum[ pdfxc->lookup_depth ] = objnum ;
  pdfxc->lookup_depth++ ;

  result = pdf_lookupxref_with_id(pdfc, rpdfobj, objnum, objgen, streamDictOnly,
                                  &match);

  pdfxc->lookup_depth-- ;

  return result ;
}

/** Implementation for pdf_lookupxref(), with additional support for a stack of
nested objnum's. If the objnum matches an objnum in the stack, it means that
the original object is cyclic and there's no need to continue, and if we did
it'll go infinite. 'idMatch' will be set to TRUE if the objnum matches one in
the stack, otherwise FALSE.

This allows repeated lookups of the same object within a particular context
(such as pdf_resolvexrefs()) to be detected.
*/
static Bool pdf_lookupxref_with_id( PDFCONTEXT *pdfc , OBJECT **rpdfobj ,
                                    int32 objnum , uint16 objgen ,
                                    Bool streamDictOnly ,
                                    Bool* idMatch )
{
  XREFCACHE *xrefcache ;
  XREFCACHE **p_xrefcache ;
  OBJECT *pdfobj ;
  int i;

  GET_PDFXC_AND_IXC;

  HQASSERT( rpdfobj != NULL && idMatch != NULL,
            "pdf_lookupxref_with_id - parameters cannot be null.");
  HQASSERT( objnum >= 0 ,
            "pdf_lookupxref_with_id - object number must not be -ve" ) ;
  HQTRACE( pdftrace_xreflookups , ( "%d %d obj" , objnum , objgen )) ;

  pdfobj = NULL ;
  *rpdfobj = NULL ;
  *idMatch = FALSE ;

  /* Look up pdfobj in cache. */
  for ( p_xrefcache = &pdfxc->xrefcache[ objnum & (XREF_CACHE_SIZE-1) ] ;
        (xrefcache = *p_xrefcache) != NULL ;
        p_xrefcache = &xrefcache->xrefnxt ) {
    if ( xrefcache->objnum == objnum ) {
      if ( xrefcache->streamDict != XREF_StreamDict || streamDictOnly ) {
        /* Move entry to start of list */
        *p_xrefcache = xrefcache->xrefnxt;
        xrefcache->xrefnxt = pdfxc->xrefcache[objnum & (XREF_CACHE_SIZE - 1)];
        pdfxc->xrefcache[objnum & (XREF_CACHE_SIZE - 1)] = xrefcache;

#if defined( DEBUG_BUILD )
        pdfxc->debugtotal_cachehits++ ;
#endif
        HQASSERT( xrefcache->objgen == objgen ,
                  "objgen should be correct in cache" ) ;
        /* Update the last accessing page member to ensure that objects used on
        this page don't get deallocated during a sweep. */
        if ( xrefcache->lastAccessId != pdfxc->pageId &&
             xrefcache->lastAccessId >= 0 ) {
          if ( ! pdf_set_xref_last_access( pdfxc , xrefcache ,
                                           pdfxc->pageId )) {
            return FALSE ;
          }
#if defined( DEBUG_BUILD )
          pdfxc->debugtotal_cachereclaims++ ;
#endif
        }
        else if ( xrefcache->lastAccessId < 0 ) {
          /* Make sure all child objects have the same negative
             lastAccessId as the parent, otherwise we'll end up with
             dangling pointers. */
          if ( ! pdf_set_xref_last_access( pdfxc , xrefcache ,
                                           xrefcache->lastAccessId )) {
            return FALSE ;
          }
        }

        /* Set 'idMatch' if objnum matches one in the stack, i.e. the object
           references are cyclic. */
        HQASSERT( pdfxc->lookup_depth > 0 &&
                  pdfxc->lookup_depth <= PDF_MAX_RECURSION_DEPTH &&
                  pdfxc->nestedObjnum[pdfxc->lookup_depth - 1] == objnum,
                  "Inconsistent nestedObjnum stack") ;
        for ( i = 0; i < pdfxc->lookup_depth - 1; i++ ) {
          if ( pdfxc->nestedObjnum[ i ] == objnum ) {
            HQFAIL("Self-referential PDF object - please tell us");
            *idMatch = TRUE ;
          }
        }

        pdfobj = & xrefcache->pdfobj ;
        /* If object is a stream, then try to rewind it. */
        if ( oType(*pdfobj) == OFILE ) {
          FILELIST *flptr ;

          /* If pdfxc is an input context, flptr will be rewindable; PDF output
             filters are not rewindable. Output and Rewindability are mutually
             exclusive, but we cannot guarantee that either are set; the
             rewindable flag is cleared when a PDF stream is terminated. */
          flptr = oFile( *pdfobj ) ;
          HQASSERT(!isIOutputFile(flptr) || !isIRewindable(flptr),
                   "Output streams should not be rewindable") ;

          if ( !isIOutputFile(flptr) ) {
            Bool rewound = FALSE ;
            if ( !pdf_rewindstream(pdfc, pdfobj, &rewound))
              return FALSE ;
          }
        }

        *rpdfobj = pdfobj ;
        return TRUE ;
      }
      else {
        /* We have the stream dict cached, but now the caller wants
           the whole stream complete with filter chain (or vice versa,
           I suppose, but this was written to speed up HqnPDFChecker
           so the normal pattern is a flurry of calls with
           streamDictOnly set to TRUE during the checking phase
           followed by a bunch with streamDictOnly FALSE during the
           running of the job). Free this cache entry and drop through
           to build a new one. */
        XREFCACHE **ref ;

        for ( ref = & pdfxc->xrefcache[ objnum & (XREF_CACHE_SIZE-1) ] ;
              *ref != xrefcache ;
              ref = & ( *ref )->xrefnxt )
          EMPTY_STATEMENT() ;

        *ref = xrefcache->xrefnxt ;
        pdf_freexrefcache( pdfc , xrefcache ) ;

        /* Since we just freed the cache we found, drop out of the
           loop in order go and re-cache the object. */
        break ;
      }
    }
  }

  /* pdfobj not found in cache; so need to read from disk. */
  {
    FILELIST *flptr, * objfile ;
    OBJECT object = OBJECT_NOTVM_NOTHING ;
    int32 objuse ;
    int8 streamDict = XREF_NotStream ;

    flptr = pdfxc->flptr ;
    HQASSERT( flptr , "flptr is null in pdf_xreflookup" ) ;

    if ( ! pdf_seek_to_xrefobj( pdfc , flptr , objnum , objgen ,
                                & objuse, &objfile ))
      return FALSE ;

    if ( objuse == XREF_Free )
      return TRUE ;

    theGen( object ) = objgen ;
    oXRefID( object ) = objnum ;

    if (objuse == XREF_Uninitialised) {
      OBJECT nullobj = OBJECT_NOTVM_NULL;
      Copy(&object, &nullobj);

    } else {
      PDF_CHECK_METHOD(get_xref_object) ;

      if (objfile == flptr) {
        if ( !(*pdfxc->methods.get_xref_object)(pdfc, flptr, &object,
                                                NULL /* stream info */,
                                                streamDictOnly, &streamDict) )
          return FALSE ;
      } else {
        if ( !(*pdfxc->methods.get_xref_streamobj)(pdfc, objfile, &object) )
          return FALSE ;

        if (( *theIMyCloseFile( objfile ))( objfile, CLOSE_EXPLICIT ) == EOF )
          return FALSE ;
      }

      if ( oType( object ) == ONOTHING )
        return error_handler( UNDEFINEDRESULT ) ;
    }

    xrefcache = pdf_allocxrefcache( pdfc , objnum , objgen ,
                                    & object , streamDict ) ;
    if ( ! xrefcache ) {
      pdf_freeobject( pdfc , & object ) ;
      return error_handler( VMERROR ) ;
    }

    *rpdfobj = & xrefcache->pdfobj ;
  }

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/** Seek to the xref obj. flptr is returned in pstream (if pstream is not NULL)
   unless the object is in a compressed stream. In that case
   the stream objects filter is returned in pstream.
*/
Bool pdf_seek_to_xrefobj( PDFCONTEXT *pdfc , FILELIST *flptr ,
                          int32 objnum , int32 objgen , int32 *objuse,
                          FILELIST ** pstream )
{
  XREFSEC *xrefsec ;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  HQASSERT( flptr , "flptr field NULL in pdf_seek_to_xrefobj" ) ;
  HQASSERT( objuse , "objuse is null in pdf_seek_to_xrefobj" ) ;

  if (pstream)
    *pstream = flptr;

  /* If it is not in any of the xref tables, it is missing. */
  *objuse = XREF_Uninitialised ;

  for ( xrefsec = pdfxc->xrefsec ;
        xrefsec ;
        xrefsec = xrefsec->xrefnxt ) {
    XREFTAB *xreftab ;
    for ( xreftab = xrefsec->xreftab ;
          xreftab ;
          xreftab = xreftab->xrefnxt ) {
      if ( objnum >= xreftab->objnum &&
           objnum < xreftab->objnum + xreftab->number ) {
        XREFOBJ *xrefobj ;
        xrefobj = xreftab->xrefobj ;
        xrefobj += ( objnum - xreftab->objnum ) ;
        if (( xrefobj->objuse == XREF_Used ) &&
            (xrefobj->d.n.objgen == objgen) ) {
          DEVICELIST *dev ;
          Hq32x2 filepos ;
          dev = theIDeviceList( flptr ) ;
          HQASSERT( dev , "dev field NULL in pdf_seek_to_xrefobj" ) ;
          filepos = xrefobj->d.n.offset ;
          if ( isIOutputFile( flptr )) {
            if (( *theIMyFlushFile( flptr ))( flptr ) == EOF )
              return ( *theIFileLastError( flptr ))( flptr ) ;
          } else {
            if (( *theIMyResetFile( flptr ))( flptr ) == EOF )
              return ( *theIFileLastError( flptr ))( flptr ) ;
          }
          if ( ! (*theISeekFile( dev ))( dev , theIDescriptor( flptr ) ,
                                         & filepos , SW_SET ))
            return ( *theIFileLastError( flptr ))( flptr ) ;
          *objuse = xrefobj->objuse ;
          return TRUE ;
        } else {
          /*for compressed obj xrefobj->objgen is objnumber
            and xrefobj->objnum is the parent stream */
          *objuse = xrefobj->objuse ;
          if (xrefobj->objuse == XREF_Compressed ) {
              /* find the object in the compressed stream and go there */
            HQASSERT(objgen == 0,
                "generation number must be zero for object "
                     "in compressed object stream");
            HQASSERT(pstream != NULL,"stream filter pointer required");
            return (*pdfxc->methods.seek_to_compressedxrefobj)(pdfc,pstream,
                                                               xrefobj,objnum);
          }
        }

      }
    }
  }
  return TRUE ;
}

/* ---------------------------------------------------------------------- */

#define XREFOBJ_BLOCK_SIZE 10

Bool pdf_getxrefobj( PDFCONTEXT *pdfc , int32 objnum , uint16 objgen ,
                     XREFOBJ **rxrefobj )
{
  PDFXCONTEXT *pdfxc ;
  XREFSEC *xrefsec ;
  XREFTAB *xreftab ;
  XREFOBJ *xrefobj ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  xrefsec = pdfxc->xrefsec ;
  xrefobj = NULL ;
  *rxrefobj = NULL ;

  HQASSERT( xrefsec ,
            "Must have just one xrefsec in pdf_getxrefobj" ) ;
  HQASSERT( xrefsec->xrefnxt == NULL ,
            "Must have just one xrefsec in pdf_getxrefobj" ) ;

  for ( xreftab = xrefsec->xreftab ;
        xreftab ;
        xreftab = xreftab->xrefnxt )
    if ( objnum >= xreftab->objnum &&
         objnum < xreftab->objnum + xreftab->number ) {
      xrefobj = xreftab->xrefobj ;
      xrefobj += ( objnum - xreftab->objnum ) ;
      if ( xrefobj->objuse != XREF_Uninitialised &&
           xrefobj->d.n.objgen > objgen ) {
        HQFAIL ( "Strange job, uses later object defs with earlier generation"
                 " numbers, in pdf_getxrefobj" ) ;
        return error_handler( UNDEFINED ) ;
      }
      *rxrefobj = xrefobj ;
      return TRUE ;
    }

  HQASSERT( xrefobj == NULL ,
            "Somehow xrefobj is not null in pdf_getxrefobj" ) ;

  /* Not found in the xref tables, so allocate a new xref table for
   * it and a block of xref objects. pdf_allocxreftable adds the
   * table to the end of the list of tables.
   */
  xreftab = pdf_allocxreftab( pdfc , xrefsec ,
                              /* blocks go from 0..BLOCK_SIZE, etc. */
                              (( objnum / XREFOBJ_BLOCK_SIZE ) *
                               XREFOBJ_BLOCK_SIZE ),
                              XREFOBJ_BLOCK_SIZE ) ;
  if ( ! xreftab )
    return error_handler( VMERROR ) ;
  xrefobj = pdf_allocxrefobj( pdfc , xreftab , XREFOBJ_BLOCK_SIZE ) ;
  if ( ! xrefobj )
    return error_handler( VMERROR ) ;
  xrefobj += ( objnum - xreftab->objnum ) ;
  *rxrefobj = xrefobj ;
  return TRUE ;
}

/* ---------------------------------------------------------------------- */

/** Resolve any indirect references within the given object,
 * recursing as necessary.
 */

Bool pdf_resolvexrefs( PDFCONTEXT *pdfc , OBJECT *theo )
{
  return pdf_resolvexrefs_internal( pdfc , theo ) ;
}

/** Recursive implementation for pdf_resolvexrefs. 'resolveId' should be a
unique number, and will be used to mark each object that this function looks-up
via pdf_lookupxref_with_id(); if the same object is looked-up more than once,
it will only be recursively resolved the first time.
*/
static Bool pdf_resolvexrefs_internal( PDFCONTEXT *pdfc , OBJECT *theo )
{

  PDFXCONTEXT *pdfxc ;
  Bool alreadyResolved = FALSE ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  HQASSERT( pdfc , "pdfc NULL in pdf_resolvexrefs.\n" ) ;
  HQASSERT( theo , "theo NULL in pdf_resolvexrefs.\n" ) ;

  if ( ++( pdfxc->recursion_depth ) > PDF_MAX_RECURSION_DEPTH ) {
    return FAILURE( error_handler( LIMITCHECK )) ;
  }

  /* First do a lookup if one is needed */

  if ( oType( *theo ) == OINDIRECT ) {
    OBJECT *thex ;

    pdfxc->nestedObjnum[ pdfxc->lookup_depth ] = oXRefID( *theo ) ;
    pdfxc->lookup_depth++ ;

    if ( ! pdf_lookupxref_with_id( pdfc , & thex , oXRefID( *theo ) ,
                                   theGen(*theo) , FALSE ,
                                   &alreadyResolved )) {
      pdfxc->lookup_depth-- ;
      return FAILURE( FALSE ) ;
    }

    pdfxc->lookup_depth-- ;

    if ( thex == NULL ) {
      return FAILURE( error_handler( UNDEFINEDRESOURCE )) ;
    }

    if ( ! pdf_copyobject( pdfc , thex , theo ) ) {
      return FAILURE( FALSE ) ;
    }
  }

  if ( ! alreadyResolved ) {
    PdfResolveParams params ;

    params.pdfc = pdfc ;
    params.isStreamDictionary = FALSE ;

    /* Now see if the object we have is composite and needs walking */
    switch ( oType( *theo )) {
      case OARRAY:
      case OPACKEDARRAY:
      {
        int32 len = theLen(*theo) ;
        OBJECT *olist = oArray( *theo ) ;
        int32 i ;

        for ( i = 0 ; i < len ; i ++ ) {
          if ( ! pdf_resolvexrefs_internal( pdfc , olist++ )) {
            return FAILURE( FALSE ) ;
          }
        }

        break ;
      }

      case ODICTIONARY:
      {
        if ( ! walk_dictionary( theo , pdf_resxref_dictwalkfn,
                                ( void * )&params )) {
          return FAILURE( FALSE ) ;
        }

        break ;
      }

      case OFILE:
      {
        OBJECT* streamDictionary = streamLookupDict( theo ) ;
        params.isStreamDictionary = TRUE ;
        if ( oType( *streamDictionary ) == ODICTIONARY &&
             ! walk_dictionary( streamDictionary , pdf_resxref_dictwalkfn,
                                ( void * )&params )) {
          return FAILURE( FALSE ) ;
        }

        break;
      }

      default:
        /* Nothing to do */
        break ;
    }
  }

  pdfxc->recursion_depth -= 1 ;

  HQASSERT( pdfxc->recursion_depth >= 0 ,
            "Recursion depth went below zero!" ) ;

  return TRUE ;
}

/* The walk_dictionary function for the above */

static Bool pdf_resxref_dictwalkfn( OBJECT *thek , OBJECT *theo , void *params )
{
  PdfResolveParams* resolveParams = ( PdfResolveParams * )params;

  UNUSED_PARAM( OBJECT * , thek ) ;

  HQASSERT( theo , "theo NULL in pdf_resxref_dictwalkfn.\n" ) ;
  HQASSERT( resolveParams->pdfc , "pdfc NULL in pdf_resxref_dictwalkfn.\n" ) ;

  if ( resolveParams->isStreamDictionary ) {
    /* Don't try to resolve the Resources dictionary in a stream
    dictionary; such resources are generally accessed via
    pdf_get_resource(), and so do not need to be resolved.  Also,
    avoid recursion by not resolving the 'DataSource' if present; this
    is added by the rip, and will be a reference to the very stream
    whose dictionary we are now resolving. A similar argument applies
    to HqnCacheSlot. */
    if ( oNameNumber(*thek) == NAME_Resources ||
         oNameNumber(*thek) == NAME_DataSource ||
         oNameNumber(*thek) == NAME_HqnCacheSlot ||
         oNameNumber(*thek) == NAME_Thresholds )
      return TRUE;
  }

  return pdf_resolvexrefs_internal( resolveParams->pdfc , theo ) ;
}

/* Allocation and initialise a new XREFCACHE object for the passed object
details.
*/
XREFCACHE *pdf_allocxrefcache( PDFCONTEXT *pdfc , int32 objnum ,
                               uint16 objgen , OBJECT *pdfobj ,
                               Bool streamDict )
{
  XREFCACHE **root ;
  XREFCACHE *xrefcache ;
  GET_PDFXC_AND_IXC ;

#if ! defined( ASSERT_BUILD )
  UNUSED_PARAM( uint16 , objgen ) ;
#endif

  HQASSERT( objnum >= 0 , "object number must not be -ve" ) ;
  HQASSERT( pdfobj , "pdfobj NULL in pdf_allocxrefcache" ) ;

  pdfxc->lowmemRedoXrefs = TRUE;

  xrefcache = mm_alloc( pdfxc->mm_structure_pool ,
                        sizeof( XREFCACHE ) ,
                        MM_ALLOC_CLASS_PDF_XREF ) ;

  if ( ! xrefcache ) {
    (void)error_handler( VMERROR );
    return NULL ;
  }

  xrefcache->objnum = objnum ;
#if defined( ASSERT_BUILD )
  xrefcache->objgen = objgen ;
#endif
  xrefcache->streamDict = ( int8 )streamDict ;
  xrefcache->flushable = FALSE ;
  xrefcache->pdfobj = *pdfobj ;
  xrefcache->lastAccessId = 0 ;
  xrefcache->xrefnxt = NULL ;

  if ( ! pdf_set_xref_last_access( pdfxc , xrefcache , pdfxc->pageId )) {
    pdf_freexrefcache( pdfc , xrefcache ) ;
    return NULL ;
  }

#if defined( DEBUG_BUILD )
  pdfxc->debugtotal_cacheloads++ ;
#endif

  root = & pdfxc->xrefcache[ objnum & (XREF_CACHE_SIZE-1) ] ;
  xrefcache->xrefnxt = (*root) ;
  (*root) = xrefcache ;

  return xrefcache ;
}

/* ---------------------------------------------------------------------- */
static void pdf_freexrefcache( PDFCONTEXT *pdfc , XREFCACHE *xrefcache )
{
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  HQASSERT( xrefcache , "xrefcache NULL in pdf_freexrefcache" ) ;

  pdf_freeobject( pdfc , & xrefcache->pdfobj ) ;
  if ( theTags( xrefcache->pdfobj ) == ONOTHING ) {
    mm_free( pdfxc->mm_structure_pool , ( mm_addr_t )xrefcache ,
             sizeof( XREFCACHE )) ;
  }
  else {
    HQASSERT( oType( xrefcache->pdfobj ) == OFILE ||
              pdfxc->recursion_depth > PDF_MAX_RECURSION_DEPTH ,
              "The only reason not to free an xref cache immediately "
              "should be when it's a stream which has been added to the "
              "deferred free list" ) ;
  }
}

/** Free the memory allocated for the parameter dictionary of a filter */
static void pdf_freeparamdict( PDFXCONTEXT *pdfxc , FILELIST *flptr )
{
  OBJECT *theDict ;

  HQASSERT( flptr, "flptr NULL in pdf_freeparamdict" ) ;

  theDict = & theIParamDict( flptr ) ;

  switch ( oType(*theDict) ) {

  case ODICTIONARY: {
    OBJECT theCopy = OBJECT_NOTVM_NOTHING ;

    /* Tag the parameter dictionary as NULL so that it
     * is NOT recusively freed by the calling routine.
     */
    Copy( & theCopy, theDict ) ;
    theTags(*theDict) = ONULL | LITERAL ;

    pdf_freeobject_from_xc( pdfxc , & theCopy ) ;
    break ;
  }

  case ONULL :
    /* The dictionary is being recursively freed so do nothing */
    break ;

  default:
    HQFAIL("theDict is not a dictionary or NULL in pdf_freeparamdict") ;
    break ;
  }
}

/** Free a stream object. It's here rather than pdfmem.c because
    stream frees are generally deferred according to their cache
    lifetime. */
static void pdf_purge_stream( PDFXCONTEXT *pdfxc , FILELIST *flptr )
{
  /* Close all the filters in the chain */
  while ( flptr && flptr != pdfxc->flptr ) {
    if ( (flptr->flags & PURGE_NOTIFY_FLAG) != 0 )
      fileio_close_pdf_filters(flptr->pdf_context_id, flptr);
    /* If necessary, free the memory allocated for the parameter
     * dictionary. At present, this only applies to PDF in.
     */
    if ( isIInputFile( flptr )) {
      pdf_freeparamdict( pdfxc, flptr ) ;
    }

    /* Free up the underlying stuctures. The FILELIST slot
     * is marked as reuseable, and any state is freed.
     */
    ClearIRewindableFlag( flptr ) ;
    if ( isIOpenFile( flptr )) {

      /* If necessary, flush the output */
      if ( ! isIInputFile( flptr )) {
        (void)(*theIMyFlushFile( flptr ))( flptr ) ;
      }

      (void)(*theIMyCloseFile( flptr ))( flptr, CLOSE_EXPLICIT ) ;
    }

    flptr = theIUnderFile( flptr ) ;
  }
}

/** Release a reference to a PDF stream. If there is no reference to
    the stream on the current page for the context then we can free
    it: but even then we must defer that free so that multiple
    references don't confuse the situation. To that end, we have the
    flushable flag. */
void pdf_xref_release_stream( PDFXCONTEXT *pdfxc , OBJECT *stream )
{
  XREFCACHE **cacheLink ;
  int32 objnum = -1 ;

  HQASSERT( stream != NULL && oType( *stream ) == OFILE ,
            "Only streams should be passed to pdf_xref_release_stream" ) ;

  if ( pdfxc->in_deferred_xrefcache_flush ) {
    return ;
  }

  if ( ! pdf_stream_slot( stream , & objnum )) {
    /* It's not in the xref cache so we can free it right now. */
    pdf_purge_stream( pdfxc , oFile( *stream )) ;
    return ;
  }

  HQASSERT( objnum != -1 , "Invalid xref cache slot" ) ;

  cacheLink = & pdfxc->xrefcache[ objnum & (XREF_CACHE_SIZE-1)] ;

  while ( *cacheLink != NULL ) {
    XREFCACHE *cache = *cacheLink ;

    if ( cache->objnum == objnum ) {
      if ( cache->lastAccessId >= 0 && cache->lastAccessId < pdfxc->pageId ) {
        /* Mark the stream as waiting to be freed. */
        cache->flushable = TRUE ;
      }

      return ;
    }

    cacheLink = &cache->xrefnxt ;
  }
}

/** Free all the objects which have been marked as flushable. */
void pdf_deferred_xrefcache_flush( PDFXCONTEXT *pdfxc )
{
  int32 i ;

  pdfxc->in_deferred_xrefcache_flush = TRUE ;

  for ( i = 0 ; i < XREF_CACHE_SIZE ; i++ ) {
    XREFCACHE **cacheLink = & pdfxc->xrefcache[ i ] ;

    while ( *cacheLink != NULL ) {
      XREFCACHE *cache = *cacheLink ;

      if ( cache->flushable ) {
        XREFCACHE *next = cache->xrefnxt ;

        if ( oType( cache->pdfobj ) == OFILE ) {
          pdf_purge_stream( pdfxc , oFile( cache->pdfobj )) ;
          mm_free( pdfxc->mm_structure_pool , ( mm_addr_t )cache ,
                   sizeof( *cache )) ;
        }
        else {
          pdf_freexrefcache( pdfxc->pdfc , cache ) ;
        }
        *cacheLink = next ;
      }
      else {
        cacheLink = &cache->xrefnxt;
      }
    }
  }

  /* Call to purge any streams that are newly flushed; ignore the
     return value because we don't actually care if any were freed. */
  ( void )pdf_purgestreams( pdfxc->pdfc ) ;

  pdfxc->in_deferred_xrefcache_flush = FALSE ;
}

/* ---------------------------------------------------------------------- */
void pdf_storexrefobj( XREFOBJ *xrefobj , Hq32x2 objoff , uint16 objgen )
{
  HQASSERT( xrefobj , "xrefobj NULL in pdf_storexrefobj" ) ;
  HQASSERT( Hq32x2CompareInt32( &objoff, 0) >= 0 ,
            "objoff is -ve in pdf_storexrefobj" ) ;

  xrefobj->d.n.offset = objoff ;
  xrefobj->d.n.objgen = objgen ;
  xrefobj->objuse = XREF_Used ;
}

/* ---------------------------------------------------------------------- */
void pdf_storefreexrefobj( XREFOBJ *xrefobj , int32 objnum , uint16 objgen )
{
  HQASSERT( xrefobj , "xrefobj NULL in pdf_storexrefobj" ) ;

  xrefobj->d.f.objnum = objnum ;
  xrefobj->d.f.objgen = objgen ;
  xrefobj->objuse = XREF_Free ;
}

void pdf_storecompressedxrefobj( XREFOBJ *xrefobj , int32 objnum ,
                                 uint16 streamindex )
{
  HQASSERT( xrefobj , "xrefobj NULL in pdf_storexrefobj" ) ;

  xrefobj->d.c.objnum = objnum ;
  xrefobj->d.c.sindex = streamindex ;
  xrefobj->objuse = XREF_Compressed ;
}

Bool pdf_setxrefobjoffset(XREFOBJ *xrefobj, Hq32x2 objoff, uint16 objgen)
{
  HQASSERT( xrefobj , "xrefobj NULL in pdf_setxrefobj" ) ;
  HQASSERT( Hq32x2CompareInt32( &objoff, 0) >= 0 ,
            "objoff is -ve in pdf_setxrefobjoffset" ) ;

  /* Experience with real world corrupted pdf jobs may prove that the
   * sanity check on the generation number is too strict, in which
   * case it can be removed. */
  if ( xrefobj->objuse != XREF_Uninitialised &&
       objgen < xrefobj->d.n.objgen )
    /* Object has already been deleted. */
    return error_handler( UNDEFINED ) ;
  pdf_storexrefobj( xrefobj , objoff , objgen ) ;
  return TRUE ;
}

Bool pdf_setxrefobjuse(XREFOBJ *xrefobj, uint16 objgen, uint8 objuse)
{
  HQASSERT( xrefobj , "xrefobj NULL in pdf_setxrefobjuse" ) ;
  HQASSERT( objuse == XREF_Free || objuse == XREF_Used ||
            objuse == XREF_Compressed  ,
            "objuse must be 'f', 'c' or 'n'" ) ;

  /* Experience with real world corrupted pdf jobs may prove that the
   * sanity check on the generation number is too strict, in which
   * case it can be removed. */
  if ( xrefobj->objuse != XREF_Uninitialised &&
       objuse == XREF_Used && objgen != xrefobj->d.n.objgen )
    return error_handler( UNDEFINED ) ;
  if ( xrefobj->objuse != XREF_Uninitialised ) {
    xrefobj->d.n.objgen = objgen ;
    xrefobj->objuse = objuse ;
  }
  return TRUE ;
}

/* ---------------------------------------------------------------------- */
XREFOBJ *pdf_allocxrefobj( PDFCONTEXT *pdfc , XREFTAB *xreftab , int32 number )
{
  XREFOBJ *xrefobj ;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  HQASSERT( xreftab , "xreftab NULL in pdf_allocxrefobj.\n" ) ;
  HQASSERT( number > 0 , "number of objects must be +ve.\n" ) ;

  xrefobj = mm_alloc( pdfxc->mm_structure_pool ,
                      number * sizeof( XREFOBJ ) ,
                      MM_ALLOC_CLASS_PDF_XREF ) ;
  if ( ! xrefobj ) {
    (void)error_handler( VMERROR );
    return NULL ;
  }

  xreftab->xrefobj = xrefobj ;

  while ((--number) >= 0 )
    (xrefobj++)->objuse = XREF_Uninitialised ;

  return xreftab->xrefobj ;
}

/* ---------------------------------------------------------------------- */
static void pdf_flushxrefobj( PDFCONTEXT *pdfc , XREFTAB *xreftab )
{
  XREFOBJ *xrefobj ;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  HQASSERT( xreftab , "xreftab NULL in pdf_flushxrefobj.\n" ) ;

  xrefobj = xreftab->xrefobj ;
  xreftab->xrefobj = NULL ;

  mm_free( pdfxc->mm_structure_pool ,
           ( mm_addr_t )xrefobj ,
           xreftab->number * sizeof( XREFOBJ )) ;
}

/* ---------------------------------------------------------------------- */
XREFTAB *pdf_allocxreftab( PDFCONTEXT *pdfc , XREFSEC *xrefsec ,
    int32 objnum , int32 number )
{
  XREFTAB *root ;
  XREFTAB *xreftab ;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  HQASSERT( xrefsec , "xrefsec NULL in pdf_allocxreftab.\n" ) ;
  HQASSERT( objnum >= 0 , "object number must be +ve.\n" ) ;
  HQASSERT( number > 0 , "number of objects must be +ve.\n" ) ;

  xreftab = mm_alloc( pdfxc->mm_structure_pool ,
                      sizeof( XREFTAB ) ,
                      MM_ALLOC_CLASS_PDF_XREF ) ;
  if ( ! xreftab ) {
    (void)error_handler( VMERROR );
    return NULL ;
  }

  xreftab->objnum = objnum ;
  xreftab->number = number ;
  xreftab->xrefnxt = NULL ;
  xreftab->xrefobj = NULL ;

  /* Chain onto the end of all the xref tables. */
  if (( root = xrefsec->xreftab ) != NULL ) {
    while ( root->xrefnxt != NULL )
      root = root->xrefnxt ;

    root->xrefnxt = xreftab ;
  } else {
    xrefsec->xreftab = xreftab ;
  }

  return xreftab ;
}

/* ---------------------------------------------------------------------- */
static void pdf_flushxreftab( PDFCONTEXT *pdfc , XREFSEC *xrefsec )
{
  XREFTAB *xreftab ;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  HQASSERT( xrefsec , "xrefsec NULL in pdf_flushxreftab.\n" ) ;

  xreftab = xrefsec->xreftab ;
  xrefsec->xreftab = NULL ;

  while ( xreftab ) {
    XREFTAB *tmp = xreftab ;
    xreftab = xreftab->xrefnxt ;

    pdf_flushxrefobj( pdfc , tmp ) ;
    mm_free( pdfxc->mm_structure_pool ,
             ( mm_addr_t )tmp ,
             sizeof( XREFTAB )) ;
  }
}

/* ---------------------------------------------------------------------- */
XREFSEC *pdf_allocxrefsec( PDFCONTEXT *pdfc , Hq32x2 byteoffset )
{
  XREFSEC *root ;
  XREFSEC *xrefsec ;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  xrefsec = mm_alloc( pdfxc->mm_structure_pool ,
                      sizeof( XREFSEC ) ,
                      MM_ALLOC_CLASS_PDF_XREF ) ;
  if ( ! xrefsec ) {
    (void)error_handler( VMERROR );
    return NULL ;
  }

  xrefsec->byteoffset = byteoffset ;
  xrefsec->xrefnxt = NULL ;
  xrefsec->xreftab = NULL ;

  /* Chain onto the end of all the xref sections. */
  if (( root = pdfxc->xrefsec ) != NULL ) {
    while ( root->xrefnxt != NULL )
      root = root->xrefnxt ;
   root->xrefnxt = xrefsec ;
  }
  else
    pdfxc->xrefsec = xrefsec ;

  return xrefsec ;
}

/* ---------------------------------------------------------------------- */
void pdf_flushxrefsec( PDFCONTEXT *pdfc )
{
  XREFSEC *xrefsec ;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  xrefsec = pdfxc->xrefsec ;
  pdfxc->xrefsec = NULL ;

  while ( xrefsec ) {
    XREFSEC *tmp = xrefsec ;
    xrefsec = xrefsec->xrefnxt ;

    pdf_flushxreftab( pdfc , tmp ) ;
    mm_free( pdfxc->mm_structure_pool ,
             ( mm_addr_t )tmp ,
             sizeof( XREFSEC )) ;
  }
}

void init_C_globals_pdfxref(void)
{
#if defined( ASSERT_BUILD )
  pdftrace_xreflookups = FALSE ;
#endif
}

/* Log stripped */
