/** \file
 * \ingroup tiff
 *
 * $HopeName: SWv20tiff!src:tifcntxt.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Creation and usage of Tiff execution contexts
 */

#include "core.h"
#include "swerrors.h"   /* VMERROR */
#include "mm.h"         /* mm_alloc() */
#include "gcscan.h"
#include "mps.h"
#include "objects.h"
#include "monitor.h"
#include "metrics.h"

#include "lists.h"      /* DLL_ */

#include "t6params.h"   /* TIFF6Params */
#include "ifdreadr.h"   /* ifd reader */
#include "tifcntxt.h"   /* tiff_context_t */

typedef struct {
  mm_pool_t         mm_pool;
  tiff_contextid_t  next_id;
  dll_list_t        dls_contexts;
} tiff_context_context_t;

static tiff_context_context_t tiff_context = {
  NULL,
  (tiff_contextid_t)1,
};

#ifdef METRICS_BUILD
static struct tiff_metrics {
  int32 tiff_context_pool_max_size ;
  int32 tiff_context_pool_max_objects ;
  int32 tiff_context_pool_max_frag;
} tiff_metrics ;

static Bool tiff_metrics_update(sw_metrics_group *metrics)
{
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("MM")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("TIFF")) )
    return FALSE ;

  SW_METRIC_INTEGER("PeakPoolSize",
                    tiff_metrics.tiff_context_pool_max_size) ;
  SW_METRIC_INTEGER("PeakPoolObjects",
                    tiff_metrics.tiff_context_pool_max_objects) ;
  SW_METRIC_INTEGER("PeakPoolFragmentation",
                    tiff_metrics.tiff_context_pool_max_frag);

  sw_metrics_close_group(&metrics) ;
  sw_metrics_close_group(&metrics) ;

  return TRUE ;
}

static void tiff_metrics_reset(int reason)
{
  struct tiff_metrics init = { 0 } ;

  UNUSED_PARAM(int, reason) ;

  tiff_metrics = init ;
}

static sw_metrics_callbacks tiff_metrics_hook = {
  tiff_metrics_update,
  tiff_metrics_reset,
  NULL
} ;
#endif


/** tiff_context_root -- root for the new object temporaries */
static mps_root_t tiff_context_root;

/** scanning function for the TIFF contexts */
static mps_res_t MPS_CALL tiff_scan_context_root(mps_ss_t ss, void *p, size_t s)
{
  mps_res_t res = MPS_RES_OK;
  tiff_context_t *p_context;

  UNUSED_PARAM( void *, p ); UNUSED_PARAM( size_t, s );
  p_context = DLL_GET_HEAD( &tiff_context.dls_contexts, tiff_context_t, dll );
  while ( p_context && res == MPS_RES_OK ) {
    res = ps_scan_field( ss, &p_context->ofile_tiff );
    if ( res != MPS_RES_OK ) return res;

    p_context = DLL_GET_NEXT(p_context, tiff_context_t, dll);
  }
  return res;
}

void init_C_globals_tifcntxt(void)
{
  /* Init context list */
  DLL_RESET_LIST(&tiff_context.dls_contexts);
#ifdef METRICS_BUILD
  tiff_metrics_reset(SW_METRICS_RESET_BOOT) ;
  sw_metrics_register(&tiff_metrics_hook) ;
#endif
}

/** tiff_init_contexts() initialises any tiff global structures.
 */
Bool tiff_init_contexts(void)
{
  /* Create root last so we force cleanup on success. */
  if ( mps_root_create( &tiff_context_root, mm_arena, mps_rank_exact(),
                        0, tiff_scan_context_root, NULL, 0 ) != MPS_RES_OK )
    return FAILURE(FALSE) ;

  return TRUE ;
}


/** tiff_finish_contexts - deinitialization for tiff global structures. */
void tiff_finish_contexts(void)
{
  mps_root_destroy( tiff_context_root );
}


/** tiff_new_context() creates a new tiff exec context and adds it
 * to the global list of contexts created. Each context created gets
 * a new unique id (well, it does cycle after ~4x10^9 contexts)
 * It returns either a valid pointer to a context or NULL and VMERROR
 * is signalled.
 */
Bool tiff_new_context(
  corecontext_t       *corecontext,
  tiff_context_t**    pp_new_context)   /* O */
{
  TIFF6PARAMS *tiff6params = corecontext->tiff6params;
  tiff_context_t* p_context;
  mm_result_t     res;

  HQASSERT((pp_new_context != NULL),
           "tiff_new_context: NULL pointer to returned context pointer");

  *pp_new_context = NULL;

  if ( DLL_LIST_IS_EMPTY(&tiff_context.dls_contexts) ) {
    res = mm_pool_create(&tiff_context.mm_pool, TIFF_POOL_TYPE, TIFF_POOL_PARAMS);
    if ( res != MM_SUCCESS ) {
      return detail_error_handler(VMERROR, "Failed to prepare memory for TIFF data.");
    }
  }

  /* Allocate context signalling VMERROR as required */
  p_context = mm_alloc(tiff_context.mm_pool, sizeof(tiff_context_t),
                       MM_ALLOC_CLASS_TIFF_CONTEXT);
  if ( p_context != NULL ) {
    /* Got context structure - init list links and add to list */
    DLL_RESET_LINK(p_context, dll);
    DLL_ADD_HEAD(&tiff_context.dls_contexts, p_context, dll);

    /* Pick up new context id and bump the count */
    p_context->id = tiff_context.next_id++;

    /* Handle wraparound of id just in case */
    if ( tiff_context.next_id == (tiff_contextid_t)0 ) {
      HQFAIL("tiff_new_context: You wrapped a 32 bit counter!");
      tiff_context.next_id = (tiff_contextid_t)1;
    }

    /* Default controls from tiff params */
    p_context->f_ignore_orientation = tiff6params->f_ignore_orientation;
    p_context->f_adjust_ctm         = tiff6params->f_adjust_ctm;
    p_context->f_do_setcolorspace   = tiff6params->f_do_setcolorspace;
    p_context->f_install_iccprofile = tiff6params->f_install_iccprofile;
    p_context->f_invert_image       = tiff6params->f_invert_image;
    p_context->f_do_imagemask       = tiff6params->f_do_imagemask;
    p_context->f_do_pagesize        = tiff6params->f_do_pagesize;
    p_context->f_no_units_same_as_inch = tiff6params->f_no_units_same_as_inch;
    p_context->f_ignore_ES0         = tiff6params->f_ignore_ES0;
    p_context->f_ES0_as_ES2         = tiff6params->f_ES0_as_ES2;

    p_context->ofile_tiff = onothing ;  /* Struct copy to set slot properties */
    p_context->number_images = 0;

    p_context->mm_pool = tiff_context.mm_pool;
    p_context->p_reader = NULL;

    p_context->defaultresolution[0] = tiff6params->defaultresolution[0];
    p_context->defaultresolution[1] = tiff6params->defaultresolution[1];

    *pp_new_context = p_context;

  } else {
    if ( DLL_LIST_IS_EMPTY(&tiff_context.dls_contexts) ) {
#if defined(METRICS_BUILD)
      { /* Track peak memory allocated in pool. */
        size_t max_size = 0, max_frag = 0;
        int32 max_objects ;
        mm_debug_total_highest(tiff_context.mm_pool, &max_size, &max_objects, &max_frag);
        if (tiff_metrics.tiff_context_pool_max_size < CAST_SIZET_TO_INT32(max_size))
          tiff_metrics.tiff_context_pool_max_size = CAST_SIZET_TO_INT32(max_size) ;
        if (tiff_metrics.tiff_context_pool_max_objects < max_objects)
          tiff_metrics.tiff_context_pool_max_objects = max_objects ;
        if (tiff_metrics.tiff_context_pool_max_frag < CAST_SIZET_TO_INT32(max_frag))
          tiff_metrics.tiff_context_pool_max_frag = CAST_SIZET_TO_INT32(max_frag);
      }
#endif
      /* Still no contexts so destroy the pool we created */
      mm_pool_destroy(tiff_context.mm_pool);
    }
    return error_handler(VMERROR);
  }

  return TRUE;
} /* Function tiff_new_context */


/** tiff_free_context() removes a context from the global list and
 * frees it off.
 */
void tiff_free_context(
  tiff_context_t**  pp_context)       /* I */
{
  tiff_context_t*   p_context;

  HQASSERT((pp_context != NULL),
           "tiff_free_context: NULL pointer to context pointer");
  HQASSERT((*pp_context != NULL),
           "tiff_free_context: NULL context pointer");
  HQASSERT((!DLL_LIST_IS_EMPTY(&tiff_context.dls_contexts)),
           "tiff_free_context: list of contexts is empty");

  p_context = *pp_context;

#if defined( ASSERT_BUILD )
  {
    tiff_context_t* p_list_context;
    /* Find*/
    p_list_context = DLL_GET_HEAD(&tiff_context.dls_contexts, tiff_context_t, dll);
    while ( p_list_context != p_context ) {
      p_list_context = DLL_GET_NEXT(p_list_context, tiff_context_t, dll);
    }
    HQASSERT((p_list_context != NULL),
             "tiff_free_context: context not found in list");
  }
#endif

  /* Remove context from list */
  DLL_REMOVE(p_context, dll);

  /* Free off the reader and its contexts */
  tiff_free_reader(&(p_context->p_reader));

  /* Free of the main context */
  mm_free(p_context->mm_pool, p_context, sizeof(tiff_context_t));
  *pp_context = NULL;

  if ( DLL_LIST_IS_EMPTY(&tiff_context.dls_contexts) ) {
    /* Was last context in list - destory pool */

#if defined( DEBUG_BUILD )
    /* Check for any mem leaks in tiff pool */
    {
      size_t c;

      c =  mm_pool_size(tiff_context.mm_pool);
      if ( c > 0 ) {
        monitorf(UVM("tiffexec mem leak of %u bytes"), c);
      }
    }
#endif

#if defined(METRICS_BUILD)
    { /* Track peak memory allocated in pool. */
      size_t max_size = 0, max_frag = 0;
      int32 max_objects ;
      mm_debug_total_highest(tiff_context.mm_pool, &max_size, &max_objects, &max_frag);
      if (tiff_metrics.tiff_context_pool_max_size < CAST_SIZET_TO_INT32(max_size))
        tiff_metrics.tiff_context_pool_max_size = CAST_SIZET_TO_INT32(max_size) ;
      if (tiff_metrics.tiff_context_pool_max_objects < max_objects)
        tiff_metrics.tiff_context_pool_max_objects = max_objects ;
      if (tiff_metrics.tiff_context_pool_max_frag < CAST_SIZET_TO_INT32(max_frag))
        tiff_metrics.tiff_context_pool_max_frag = CAST_SIZET_TO_INT32(max_frag);
    }
#endif
    mm_pool_destroy(tiff_context.mm_pool);
  }

} /* Function tiff_free_context */


/*
 * tiff_first_context()
 */
tiff_context_t* tiff_first_context(void)
{
  /* Return first context in list */
  return(DLL_GET_HEAD(&tiff_context.dls_contexts, tiff_context_t, dll));

} /* Function tiff_first_context */


/*
 * tiff_next_context()
 */
tiff_context_t* tiff_next_context(
  tiff_context_t* p_context)        /* I */
{
  HQASSERT((p_context != NULL),
           "tiff_context_t: NULL context pointer");

  /* Return next context in list */
  return(DLL_GET_NEXT(p_context, tiff_context_t, dll));

} /* Function tiff_next_context */



/* Log stripped */
