/** \file
 * \ingroup pdf
 *
 * $HopeName: COREpdf_base!src:pdfcntxt.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF context operations implementation
 */

#include "core.h"
#include "pdfcntxt.h"

#include "mm.h" /* mm_arena */
#include "lowmem.h"
#include "coreinit.h"
#include "metrics.h"
#include "objecth.h"
#include "graphics.h"

#include "gstack.h"             /* gs_gpush */
#include "monitor.h"            /* monitorf */
#include "swerrors.h"           /* VMERROR */
#include "swmemory.h"           /* gs_cleargstates */
#include "system.h"             /* initSystemMemoryCaches */
#include "hqmemset.h"

#include "swpdf.h"              /* PDFXCONTEXT */
#include "pdfres.h"             /* pdf_add_resource */
#include "pdfstrm.h"            /* pdf_purgestreams */
#include "pdfxref.h"            /* pdf_sweepxref */


#ifdef DEBUG_BUILD
static Bool debug_xrefcachetotals ;
#endif

static mps_res_t pdf_scan_marking_context(mps_ss_t ss, PDFCONTEXT *pdfc);
static low_mem_offer_t *pdf_lowmem_solicit(low_mem_handler_t *handler,
                                           corecontext_t *context,
                                           size_t count,
                                           memory_requirement_t* requests);
static Bool pdf_lowmem_release(low_mem_handler_t *handler,
                               corecontext_t *context,
                               low_mem_offer_t *offer);

#ifdef METRICS_BUILD
struct pdf_context_metrics {
  int32 pdfxc_mm_structure_pool_max_size ;
  int32 pdfxc_mm_structure_pool_max_objects ;
  int32 pdfxc_mm_structure_pool_max_frag;
  int32 pdfxc_mm_object_pool_max_size ;
  int32 pdfxc_mm_object_pool_max_objects ;
  int32 pdfxc_mm_object_pool_max_frag;
} pdf_context_metrics ;

static Bool pdf_context_metrics_update(sw_metrics_group *metrics)
{
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("MM")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("PDFStructure")) )
    return FALSE ;

  SW_METRIC_INTEGER("PeakPoolSize",
                    pdf_context_metrics.pdfxc_mm_structure_pool_max_size) ;
  SW_METRIC_INTEGER("PeakPoolObjects",
                    pdf_context_metrics.pdfxc_mm_structure_pool_max_objects) ;
  SW_METRIC_INTEGER("PeakPoolFragmentation",
                    pdf_context_metrics.pdfxc_mm_structure_pool_max_frag);
  sw_metrics_close_group(&metrics) ;
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("PDFObject")) )
    return FALSE ;
  SW_METRIC_INTEGER("PeakPoolSize",
                    pdf_context_metrics.pdfxc_mm_object_pool_max_size) ;
  SW_METRIC_INTEGER("PeakPoolObjects",
                    pdf_context_metrics.pdfxc_mm_object_pool_max_objects) ;
  SW_METRIC_INTEGER("PeakPoolFragmentation",
                    pdf_context_metrics.pdfxc_mm_object_pool_max_frag);

  sw_metrics_close_group(&metrics) ;
  sw_metrics_close_group(&metrics) ;

  return TRUE ;
}

static void pdf_context_metrics_reset(int reason)
{
  struct pdf_context_metrics init = { 0 } ;

  UNUSED_PARAM(int, reason) ;

  pdf_context_metrics = init ;
}

static sw_metrics_callbacks pdf_context_metrics_hook = {
  pdf_context_metrics_update,
  pdf_context_metrics_reset,
  NULL
} ;
#endif


/** \brief xcontext_scan - scan an execution context
 *
 * Only scan the fields that come from the PS parameters, and the
 * marking contexts (the PDF stack needs scanning). */
static mps_res_t xcontext_scan(mps_ss_t ss, PDFXCONTEXT *pdfxc)
{
  mps_res_t res = MPS_RES_OK;
  PDFCONTEXT *pdfc;

  /* The list of execution contexts is traversed in the caller. */
  pdfc = pdfxc->pdfc;
  while ( pdfc && res == MPS_RES_OK ) {
    res = pdf_scan_marking_context( ss, pdfc );
    pdfc = pdfc->next ;
  }
  if ( res != MPS_RES_OK ) return res;
  MPS_SCAN_BEGIN( ss )
    MPS_RETAIN( &pdfxc->flptr, TRUE );
  MPS_SCAN_END( ss );
  res = ( *pdfxc->methods.scan_context )( ss, pdfxc );
  return res;
}


/** xcontext_base_scan - scan all the contexts from this base */
static mps_res_t xcontext_base_scan(mps_ss_t ss, void *p, size_t s)
{
  mps_res_t res = MPS_RES_OK;
  PDFXCONTEXT *pdfxc;

  UNUSED_PARAM( size_t, s );
  HQASSERT(p != NULL, "Invalid execution context base");
  pdfxc = *(PDFXCONTEXT **)p;
  while ( pdfxc && res == MPS_RES_OK ) {
    res = xcontext_scan( ss, pdfxc );
    pdfxc = pdfxc->next ;
  }
  return res;
}


/* For the global linked list of contexts */
#define MAX_XCONTEXT_BASE 2 /* PDF in, PDF out */

static int32 pdfxcontext_nextid ;
static PDFXCONTEXT *pdf_xcontext_purge ;
static int32 pdf_nxcontext_bases ;
static PDFXCONTEXT **pdf_xcontext_bases[MAX_XCONTEXT_BASE] = { NULL, NULL } ;
static mps_root_t pdf_xcontext_base_roots[MAX_XCONTEXT_BASE];

static void init_C_globals_pdfcntxt(void)
{
#ifdef DEBUG_BUILD
  debug_xrefcachetotals = FALSE ;
#endif

  pdfxcontext_nextid = 1 ;
  pdf_xcontext_purge = NULL ;
  pdf_nxcontext_bases = 0;
  HqMemSetPtr(pdf_xcontext_bases, NULL, MAX_XCONTEXT_BASE) ;
  HqMemSetPtr(pdf_xcontext_base_roots, NULL, MAX_XCONTEXT_BASE) ;

#ifdef METRICS_BUILD
  pdf_context_metrics_reset(SW_METRICS_RESET_BOOT) ;
  sw_metrics_register(&pdf_context_metrics_hook) ;
#endif
}


/** The PDF low-memory handler. */
static low_mem_handler_t pdf_lowmem_handler = {
  "PDF xref cache and streams",
  memory_tier_disk, pdf_lowmem_solicit, pdf_lowmem_release, TRUE,
  0, FALSE };


static Bool pdf_swinit(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  return low_mem_handler_register(&pdf_lowmem_handler);
}


static void pdf_finish(void)
{
  low_mem_handler_deregister(&pdf_lowmem_handler);
  while ( pdf_nxcontext_bases > 0 ) {
    mps_root_destroy(pdf_xcontext_base_roots[--pdf_nxcontext_bases]);
  }
}


IMPORT_INIT_C_GLOBALS(pdfxref)

void pdf_C_globals(core_init_fns *fns)
{
  init_C_globals_pdfcntxt() ;
  init_C_globals_pdfxref() ;

  fns->swstart = pdf_swinit ;
  fns->finish = pdf_finish ;
}

/* Register a new execution context base. Each module using the PDF subsystem
   should register an execution context base with it. */
Bool pdf_register_execution_context_base(PDFXCONTEXT **base)
{
  HQASSERT(base != NULL, "Invalid execution context base") ;
  HQASSERT(pdf_nxcontext_bases < MAX_XCONTEXT_BASE,
           "Too many execution context bases; recompile with higher limit") ;

#ifdef ASSERT_BUILD
  {
    int32 i ;
    for ( i = 0 ; i < pdf_nxcontext_bases ; ++i ) {
      HQASSERT(pdf_xcontext_bases[i] != base,
               "Execution context base already registered") ;
    }
  }
#endif
  pdf_xcontext_bases[pdf_nxcontext_bases] = base ;

  /* Create root last so we force cleanup on success. */
  if ( mps_root_create( &pdf_xcontext_base_roots[pdf_nxcontext_bases],
                        mm_arena, mps_rank_exact(),
                        0, xcontext_base_scan, (void *)base, 0 ) != MPS_RES_OK )
    return FAILURE(FALSE) ;

  ++pdf_nxcontext_bases ;

  return TRUE ;
}

/**
 * Cleanup a partially initialsed execution context.
 */
static void beginExeContextFailureCleanup(PDFXCONTEXT *pdfxc,
                                          Bool clearSMCaches)
{
  if (pdfxc->mm_object_pool != NULL) {
    mm_pool_destroy(pdfxc->mm_object_pool);
    pdfxc->mm_object_pool = NULL;
  }

  if (clearSMCaches)
    clearSystemMemoryCaches(pdfxc->mm_structure_pool);

  if (pdfxc->mm_structure_pool != NULL) {
    mm_pool_destroy(pdfxc->mm_structure_pool);
    pdfxc->mm_structure_pool = NULL;
  }

  mm_free(mm_pool_temp, (mm_addr_t)pdfxc, sizeof(PDFXCONTEXT));
}

/** Create an execution context, insert it into the global linked list
 * and create its memory pools. Doesn't need the pdfc param any more.
 * Beginning an execution context implicitly begins a 'special'
 * marking context, which isn't really a marking context at all but
 * just a placeholder for the infrastructure xref operations.
 */

Bool pdf_begin_execution_context(PDFXCONTEXT **new_pdfxc,
                                 PDFXCONTEXT **base ,
                                 PDF_METHODS *methods,
                                 corecontext_t *corecontext)
{
  int32 i ;
  PDFXCONTEXT *pdfxc ;

  HQASSERT( new_pdfxc , "new_pdfxc NULL in pdf_begin_execution_context." ) ;
  HQASSERT( base , "base NULL in pdf_begin_execution_context." ) ;
  HQASSERT( methods , "methods NULL in pdf_begin_execution_context." ) ;

#ifdef ASSERT_BUILD
  for ( i = 0 ; i < pdf_nxcontext_bases ; ++i ) {
    if ( base == pdf_xcontext_bases[i] )
      break ;
  }
  HQASSERT(i < pdf_nxcontext_bases,
           "bad base in pdf_begin_execution_context." ) ;
#endif

  /* Don't allow further processing at save level 0 or 2. Someone has most
   * likely been messing with startjob/exitserver to achieve this and we don't
   * support running jobs at that level. We will get a crash later otherwise.
   */
  if ( corecontext->savelevel <= SAVELEVELINC ) {
    HQFAIL("Expected a save level > 2") ;
    return error_handler(UNDEFINED);
  }

  /* Allocate the new context from the temp pool. */
  pdfxc = ( PDFXCONTEXT * )mm_alloc( mm_pool_temp ,
                                     sizeof( PDFXCONTEXT ) ,
                                     MM_ALLOC_CLASS_PDF_CONTEXT ) ;

  if ( ! pdfxc )
    return error_handler( VMERROR ) ;

  *new_pdfxc = pdfxc ;

  /* Initialise the fields to sensible defaults */

  /* Clear the structure (NULL any pointers, zero integer values, etc). */
  HqMemZero(pdfxc, sizeof(PDFXCONTEXT));

  pdfxc->id = 0 ;
  pdfxc->next = NULL ;

  pdfxc->corecontext = corecontext ;

  pdfxc->pdfc = NULL ;

  pdfxc->metadata.id = onull;
  pdfxc->metadata.version = onull;
  pdfxc->metadata.renditionClass = onull;

  /* This rogue value for pageId is important: anything loaded into
     the xref cache while it is set will live for the whole lifetime
     of the execution context. Its power should be used wisely! Most
     objects should be tagged with a value >= 0 so that the
     XRefCacheLifetime param value means they're freed when they've
     not been used for a number of pages. */
  pdfxc->pageId = -1;

  pdfxc->error = FALSE ;

  pdfxc->mm_object_pool = NULL ;
  pdfxc->mm_structure_pool = NULL ;
  pdfxc->savelevel = corecontext->savelevel ;

  pdfxc->flptr = NULL ;
  pdfxc->filepos.low = pdfxc->filepos.high = 0 ;
  pdfxc->fileend.low = pdfxc->fileend.high = 0 ;

  pdfxc->crypt_info = NULL ;

  pdfxc->xrefsec = NULL ;

  pdfxc->streams = NULL ;
  pdfxc->ErrorOnFlateChecksumFailure = TRUE ;

  for ( i = 0 ; i < XREF_CACHE_SIZE ; i++ )
    pdfxc->xrefcache[ i ] = NULL ;

  pdfxc->pdfwalk_depth = 1;

  pdfxc->pdd_orientation = 0 ;

#ifdef DEBUG_BUILD
  pdfxc->debugtotal_cacheloads = 0 ;
  pdfxc->debugtotal_cachehits = 0 ;
  pdfxc->debugtotal_cachereleases = 0 ;
  pdfxc->debugtotal_cachereclaims = 0 ;
#endif

  pdfxc->recursion_depth  = 0 ;
  pdfxc->lookup_depth = 0 ;

  pdfxc->u.v = NULL ;

  /* We don't need to initialise pdfxc->FlushBuff */

  /* Allocate the MM pools for the context */
  if ( mm_pool_create( & pdfxc->mm_object_pool,
                       PDF_POOL_TYPE, PDF_POOL_PARAMS ) != MM_SUCCESS )
    return error_handler( VMERROR ) ;

  if ( mm_pool_create( & pdfxc->mm_structure_pool,
                       PDF_POOL_TYPE, PDF_POOL_PARAMS ) != MM_SUCCESS ) {
    beginExeContextFailureCleanup(pdfxc, FALSE);
    return error_handler( VMERROR ) ;
  }

  if ( !initSystemMemoryCaches(pdfxc->mm_structure_pool) ) {
    beginExeContextFailureCleanup(pdfxc, FALSE);
    return error_handler(VMERROR);
  }

  pdfxc->methods = *methods ;
  PDF_CHECK_METHOD( begin_execution_context ) ;

  pdfxc->lowmemRedoXrefs = FALSE;
  pdfxc->lowmemXrefPageId = -1;
  pdfxc->lowmemXrefCount = 0;
  pdfxc->lowmemRedoStreams = FALSE;
  pdfxc->lowmemStreamCount = 0;
  pdfxc->in_deferred_xrefcache_flush = FALSE;

  if ( ! ( * pdfxc->methods.begin_execution_context )( pdfxc ) ||
       ! pdf_begin_marking_context( pdfxc , NULL , NULL ,
                                    PDF_STREAMTYPE_PAGE )) {
    beginExeContextFailureCleanup(pdfxc, TRUE);
    return FALSE ;
  }

  /* Set up the new context in the global list. If we
   * want to constrain the number of concurrent contexts
   * and/or the range of ids, this is where to do it.
   */

  pdfxc->id = pdfxcontext_nextid++ ;
  pdfxc->next = *base ;
  *base = pdfxc ;

  return TRUE ;
}

/** Unlink the given context and destroy the structure memory pool - it
 * should no longer be possible for the context to be partially
 * constructed but the code might as well handle that case anyway.
 */

Bool pdf_end_execution_context( PDFXCONTEXT *pdfxc , PDFXCONTEXT **base )
{
  Bool result = TRUE ;

  HQASSERT( pdfxc , "pdfxc NULL in pdf_end_execution_context." ) ;
  HQASSERT( base , "base NULL in pdf_end_execution_context." ) ;
  HQASSERT( pdfxc->pdfc , "pdfxc->pdfxc NULL in pdf_end_execution_context." ) ;
  HQASSERT( pdfxc->pdfc->mc == 0 ,
            "stale marking context(s) in pdf_end_execution_context." ) ;

#ifdef ASSERT_BUILD
  {
    int32 i ;
    for ( i = 0 ; i < pdf_nxcontext_bases ; ++i ) {
      if ( base == pdf_xcontext_bases[i] )
        break ;
    }
    HQASSERT(i < pdf_nxcontext_bases,
             "bad base in pdf_end_execution_context." ) ;
  }
#endif

  /* Ensure that no reference to any filters in this execution context persist
  beyond its lifetime. */
  fileio_close_pdf_filters(pdfxc->id, NULL);

  if ( ! pdf_end_marking_context( pdfxc->pdfc , NULL )) {
    result = FALSE ;
  }

  PDF_CHECK_METHOD( end_execution_context ) ;

  if ( ! ( *pdfxc->methods.end_execution_context )( pdfxc )) {
    result = FALSE ;
  }

  /* Remove the context from the linked list */
  if ( *base == pdfxc ) {
    *base = pdfxc->next ;
  }
  else {
    PDFXCONTEXT *pdfxc_prev ;
    for ( pdfxc_prev = *base ;
          pdfxc_prev->next != pdfxc ;
          pdfxc_prev = pdfxc_prev->next )
      ;
    HQASSERT( pdfxc_prev , "Should have found pdfxc" ) ;
    pdfxc_prev->next = pdfxc->next ;
  }

  /* Destroy the structure memory pool if exists */

  if ( pdfxc->mm_structure_pool ) {
#if defined(METRICS_BUILD)
    { /* Track peak memory allocated in pool. This is only going
         to record the maximum amoungest all created. */
      size_t max_size = 0, max_frag = 0;
      int32 max_objects ;
      mm_debug_total_highest(pdfxc->mm_structure_pool,
                             &max_size, &max_objects, &max_frag);
      if (pdf_context_metrics.pdfxc_mm_structure_pool_max_size < CAST_SIZET_TO_INT32(max_size))
        pdf_context_metrics.pdfxc_mm_structure_pool_max_size = CAST_SIZET_TO_INT32(max_size) ;
      if (pdf_context_metrics.pdfxc_mm_structure_pool_max_objects < max_objects)
        pdf_context_metrics.pdfxc_mm_structure_pool_max_objects = max_objects ;
      if (pdf_context_metrics.pdfxc_mm_structure_pool_max_frag < CAST_SIZET_TO_INT32(max_frag))
        pdf_context_metrics.pdfxc_mm_structure_pool_max_frag = CAST_SIZET_TO_INT32(max_frag);

      mm_debug_total_highest(pdfxc->mm_object_pool,
                             &max_size, &max_objects, &max_frag);
      if (pdf_context_metrics.pdfxc_mm_object_pool_max_size < CAST_SIZET_TO_INT32(max_size))
        pdf_context_metrics.pdfxc_mm_object_pool_max_size = CAST_SIZET_TO_INT32(max_size) ;
      if (pdf_context_metrics.pdfxc_mm_object_pool_max_objects < max_objects)
        pdf_context_metrics.pdfxc_mm_object_pool_max_objects = max_objects ;
      if (pdf_context_metrics.pdfxc_mm_object_pool_max_frag < CAST_SIZET_TO_INT32(max_frag))
        pdf_context_metrics.pdfxc_mm_object_pool_max_frag = CAST_SIZET_TO_INT32(max_frag);
    }
#endif
    clearSystemMemoryCaches(pdfxc->mm_structure_pool);
    mm_pool_destroy( pdfxc->mm_structure_pool ) ;
    pdfxc->mm_structure_pool = NULL ;
  }

  /* Now add the pdfxcontext to the waste list so the object pool (and
     pdfxcontext) can be save/restore'd away. */
  pdfxc->next = pdf_xcontext_purge ;

  pdf_xcontext_purge = pdfxc ;

  return result ;
}

/* ---------------------------------------------------------------------- */
Bool pdf_find_execution_context(int32 id, PDFXCONTEXT *base,
                                PDFXCONTEXT **pdfxc )
{
  PDFXCONTEXT *current_pdfxc = base ;

  HQASSERT( pdfxc , "pdfxc NULL in pdf_find_execution_context." ) ;

  if ( id == 0 )
    return FALSE ;

  while ( current_pdfxc ) {
    if ( current_pdfxc->id == id ) {
      *pdfxc = current_pdfxc ;
      return TRUE ;
    }

    current_pdfxc = current_pdfxc->next ;
  }

  return FALSE ;
}

Bool pdf_purge_execution_contexts( int32 savelevel )
{
  PDFXCONTEXT *pdfxc , *ppdfxc ;
  int32 i ;

  for ( i = 0 ; i < pdf_nxcontext_bases ; ++i ) {
    pdfxc = *pdf_xcontext_bases[i] ;

    /* This loop is written so that PDF contexts can be removed during the
       loop; the next pointer is found before any action is taken. Ending of
       PDF in execution contexts is now done by the PDF in purge context
       routine. */
    while ( pdfxc ) {
      PDFXCONTEXT *next = pdfxc->next ;

      PDF_CHECK_METHOD(purge_execution_context) ;

      if (! (*pdfxc->methods.purge_execution_context)(pdfxc, savelevel))
        return FALSE ;

      pdfxc = next ;
    }
  }

  /* Free all the pdf execution contexts and their object memory pools
     in the purge queue that are being restored away. */
  ppdfxc = NULL ;
  pdfxc = pdf_xcontext_purge ;

  while ( pdfxc ) {

    PDFXCONTEXT *tpdfxc = pdfxc ;
    pdfxc = pdfxc->next ;

    if ( tpdfxc->savelevel >= savelevel ) {

      /* Remove the context from the linked list. */
      if ( ppdfxc )
        ppdfxc->next = tpdfxc->next ;
      else
        pdf_xcontext_purge = tpdfxc->next ;

      /* Destroy object memory pool if exists and the pdfxcontext itself.*/
      HQASSERT( tpdfxc->mm_structure_pool == NULL ,
                "mm_structure_pool should already have been freed" ) ;
      if ( tpdfxc->mm_object_pool ) {
        mm_pool_destroy( tpdfxc->mm_object_pool ) ;
        tpdfxc->mm_object_pool = NULL ;
      }
      mm_free( mm_pool_temp , ( mm_addr_t )tpdfxc , sizeof( PDFXCONTEXT )) ;
    }
    else
      ppdfxc = ( ppdfxc ? ppdfxc->next : pdf_xcontext_purge ) ;
  }

  return TRUE;
}

/** Begin a new marking context given the enclosing execution context. */

Bool pdf_begin_marking_context(PDFXCONTEXT *pdfxc, PDFCONTEXT **new_pdfc,
                               OBJECT *resource , int streamtype )
{
  PDFCONTEXT *pdfc ;

  HQASSERT( pdfxc , "pdfxc NULL in pdf_begin_marking_context." ) ;

  /* This is no longer required for array access safety, but it remains
   * in the code as a useful safeguard against infinite loops.
   */

  if ( pdfxc->pdfc && ( pdfxc->pdfc->mc >= PDF_MAX_MC_NESTCOUNT )) {
    HQFAIL( "PDF marking context nesting limit exceeded." ) ;
    return error_handler( LIMITCHECK ) ;
  }

  /* Allocate the new context from the structure pool. */

  pdfc = ( PDFCONTEXT * )mm_alloc( pdfxc->mm_structure_pool ,
                                   sizeof( PDFCONTEXT ) ,
                                   MM_ALLOC_CLASS_PDF_CONTEXT ) ;

  if ( ! pdfc )
    return error_handler( VMERROR ) ;

  if ( new_pdfc )
    *new_pdfc = pdfc ;

  /* Initialise the fields to sensible defaults. */

  /* Clear the structure (NULL any pointers, zero integer values, etc). */
  HqMemZero(pdfc, sizeof(PDFCONTEXT));

  pdfc->next = NULL ;

  pdfc->pdfxc = pdfxc ;

  pdfc->corecontext = pdfxc->corecontext;

  pdfc->mc = 0 ;
  HQASSERT( streamtype >= PDF_STREAMTYPE_PAGE &&
            streamtype <= PDF_STREAMTYPE_PATTERN ,
            "Unexpected streamtype value" ) ;
  pdfc->streamtype = streamtype ;

  pdfc->contents = NULL ;
  pdfc->contentsindex = 0 ;
  pdfc->contentsStream = NULL ;

  pdfc->pdfenv = NULL ;

  {
    int32 i ;
    int32 n = sizeof( pdfc->resource_cache ) / sizeof( PDF_RESOURCE_CACHE ) ;

    for ( i = 0 ; i < n ; ++ i ) {
      pdfc->resource_cache[ i ].resource = NULL ;
      pdfc->resource_cache[ i ].valid = FALSE ;
    }
  }

  pdfc->u.v = NULL ;

  /* Invoke the begin marking context method unless this is the special
   * placeholder outermost context.
   */

  if ( pdfxc->pdfc ) {
    PDF_CHECK_METHOD( begin_marking_context ) ;

    if ( !( * pdfxc->methods.begin_marking_context )( pdfxc , pdfc )) {
      mm_free( pdfxc->mm_structure_pool ,
               ( mm_addr_t )pdfc ,
               sizeof( PDFCONTEXT )) ;
      return FALSE ;
    }

    /* Invoke the context-specific call-back for initialising special
     * marking context parameters.
     */
    if (pdfxc->methods.init_marking_context != NULL  &&  pdfc->u.i != NULL) {
      if (! (*pdfxc->methods.init_marking_context)
          ( pdfc->u.i, pdfxc->methods.init_mc_arg )) {
        mm_free( pdfxc->mm_structure_pool, (mm_addr_t) pdfc, sizeof(*pdfc) );
        pdfxc->methods.init_marking_context = NULL;
        pdfxc->methods.init_mc_arg = NULL;
        return FALSE;
      }
      /* Reset the function pointer as this method is a "one-shot" only.
       * Client code has to call pdf_set_mc_callback() to set it up again.
       */
      pdfxc->methods.init_marking_context = NULL;
      pdfxc->methods.init_mc_arg = NULL;
    }
  }

  /* Inherit certain values from the current marking context if there is one. */

  pdfc->next = pdfxc->pdfc ;

  if ( pdfc->next ) {
    int32 i ;
    int32 n = sizeof( pdfc->resource_cache ) / sizeof( PDF_RESOURCE_CACHE ) ;

    pdfc->mc = pdfc->next->mc + 1 ;

    pdfc->pdfenv = pdfc->next->pdfenv ;

    for ( i = 0 ; i < n ; ++ i ) {
      pdfc->resource_cache[ i ].resource =
        pdfc->next->resource_cache[ i ].resource ;
      pdfc->resource_cache[ i ].valid =
        pdfc->next->resource_cache[ i ].valid ;
    }
  }

  if ( resource )
    if ( ! pdf_add_resource( pdfc , resource ))
      return FALSE ;

  /* Hook up the pointers between the execution and marking contexts. */

  pdfxc->pdfc = pdfc ;
  pdfc->pdfxc = pdfxc ;

  return TRUE ;
}

Bool pdf_end_marking_context(PDFCONTEXT *pdfc, OBJECT *resource)
{
  PDFXCONTEXT *pdfxc ;
  Bool result = TRUE ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  HQASSERT( pdfc->mc >= 0 , "Badly nested begin/end marking contexts." ) ;

  PDF_CHECK_METHOD( end_marking_context ) ;

  if ( ! ( *pdfxc->methods.end_marking_context )( pdfc )) {
    result = FALSE ;
  }

#ifdef DEBUG_BUILD
  if ( debug_xrefcachetotals ) {
    monitorf( ( uint8* )"%% xref cache debug totals:\n" ) ;
    monitorf( ( uint8* )"%% loads: %d\n" , pdfxc->debugtotal_cacheloads ) ;
    monitorf( ( uint8* )"%% hits: %d\n" , pdfxc->debugtotal_cachehits ) ;
    monitorf( ( uint8* )"%% releases: %d\n" , pdfxc->debugtotal_cachereleases ) ;
    monitorf( ( uint8* )"%% reclaims: %d\n" , pdfxc->debugtotal_cachereclaims ) ;
  }
#endif

  /* Restore file positions and free PDF_FILERESTORE_LIST entries */
  result = pdf_restorestreams( pdfc, result ) ;

  /* Link chain back to the 'previous' one. */

  pdfxc->pdfc = pdfc->next ;

  if ( resource )
    pdf_remove_resource( pdfc ) ;

  mm_free( pdfxc->mm_structure_pool ,
           ( mm_addr_t )pdfc ,
           sizeof( PDFCONTEXT )) ;

  return result ;
}


/* pdf_set_mc_callback()
 * Sets the address of a call-back function in PDF_METHODS so that it will be
 * called when a new marking context is being created and initialised (see
 * pdf_begin_marking_context() above).  Once called, the function pointer is
 * set to NULL to stop it being called again (e.g. for inappropriate marking
 * contexts).  Client code has to re-invoke pdf_set_mc_callback() each time.
 */
void pdf_set_mc_callback( PDFXCONTEXT *pdfxc,
                          PDF_INIT_MC_CALLBACK init_mc_func,
                          void *pContextArg )   /* argument for the call-back */
{
  HQASSERT( pdfxc != NULL, "pdfxc null in pdf_set_mc_callback" );
  pdfxc->methods.init_marking_context = init_mc_func;
  pdfxc->methods.init_mc_arg = pContextArg;
}


static mps_res_t pdf_scan_marking_context(mps_ss_t ss, PDFCONTEXT *pdfc)
{
  mps_res_t res;

  /* The list of contexts is traversed in the caller. */
  /* This is called from xcontext_scan, so skip the execution context. */
  res = pdfc->pdfxc->methods.scan_marking_context( ss, pdfc );
  return res;
}


/** Solicit method of the PDF low-memory handler. */
static low_mem_offer_t *pdf_lowmem_solicit(low_mem_handler_t *handler,
                                           corecontext_t *context,
                                           size_t count,
                                           memory_requirement_t* requests)
{
  static low_mem_offer_t offer;
  size_t prediction = 0, i;
  mm_pool_t pool = NULL;
  Bool pool_seen = FALSE;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  /* nothing to assert about count */
  HQASSERT(requests != NULL, "No requests");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(size_t, count); UNUSED_PARAM(memory_requirement_t*, requests);

  if ( !context->between_operators )
    return NULL;
  for ( i = 0 ; i < (size_t)pdf_nxcontext_bases ; ++i ) {
    PDFXCONTEXT *pdfxc = *pdf_xcontext_bases[i];

    while ( pdfxc != NULL ) {
      prediction += pdf_measure_sweepable_xrefs(pdfxc->pdfc)
        + pdf_measure_purgeable_streams(pdfxc->pdfc);
      if ( !pool_seen ) {
        pool = pdfxc->mm_object_pool; pool_seen = TRUE;
      } else {
        if ( pool != pdfxc->mm_object_pool )
          pool = NULL;
      }
      pdfxc = pdfxc->next;
    }
  }
  if ( prediction == 0 )
    return NULL;
  offer.pool = pool;
  /* Ignore streams in pdfxc->mm_structure_pool, probably insignificant. */
  offer.offer_size = prediction;
  offer.offer_cost = 2.0; /* has to reread the object from PDF */
  offer.next = NULL;
  return &offer;
}


/** Release method of the PDF low-memory handler. */
static Bool pdf_lowmem_release(low_mem_handler_t *handler,
                               corecontext_t *context,
                               low_mem_offer_t *offer)
{
  size_t freed = 0, i;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  HQASSERT(offer != NULL, "No offer");
  HQASSERT(offer->next == NULL, "Multiple offers");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(corecontext_t*, context);

  for ( i = 0 ; i < (size_t)pdf_nxcontext_bases ; ++i ) {
    PDFXCONTEXT *pdfxc = *pdf_xcontext_bases[i];

    while ( pdfxc != NULL && freed < offer->taken_size ) {
      size_t orig_size = mm_pool_alloced_size(pdfxc->mm_object_pool);

      if (pdfxc->lowmemXrefCount > 0) {
        (void) pdf_sweepxref(pdfxc->pdfc, FALSE /* closing */, -1);
      }
      if (pdfxc->lowmemStreamCount > 0)
        (void)pdf_purgestreams(pdfxc->pdfc);
      freed += orig_size - mm_pool_alloced_size(pdfxc->mm_object_pool);
      pdfxc = pdfxc->next;
    }
  }
  return TRUE;
}


/* ============================================================================
* Log stripped */
