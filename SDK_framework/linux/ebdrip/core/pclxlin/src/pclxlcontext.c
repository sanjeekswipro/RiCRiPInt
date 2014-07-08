/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlcontext.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief Implementation of PCLXL "context" structure(s) which are
 * used to record the current parsing context.  The PCLXL maintains a
 * stack of contexts that represents the nested PCLXL language
 * constructs.
 */

#include "core.h"
#include "swstart.h"
#include "monitor.h"
#include "gu_path.h"
#include "graphics.h"

#ifdef DEBUG_BUILD
#include "mm.h"
#endif

#include "pclxluserstream.h"
#include "pclxlcontext.h"
#include "pclxldebug.h"
#include "pclxlscan.h"
#include "pclxlerrors.h"
#include "pclxlgraphicsstate.h"
#include "pclxlparsercontext.h"
#include "pclxlsmtable.h"
#include "pclxlpsinterface.h"
#include "pclxlfont.h"
#include "pclxlpattern.h"
#include "pclxlpassthrough.h"

/* I declare this directly here because I don't want any other file
   using this global directly. */
extern PCLXL_CONTEXT pclxl_job_lifetime_context ;

PCLXL_CONTEXT pclxl_get_context(void)
{
  return pclxl_job_lifetime_context ;
}

void pclxl_destroy_context(PCLXL_CONTEXT *old_pclxl_context)
{
  PCLXL_CONTEXT pclxl_context ;
  mm_pool_t memory_pool ;

  HQASSERT(old_pclxl_context != NULL, "old_pclxl_context is NULL") ;
  pclxl_context = *old_pclxl_context ;
  HQASSERT((pclxl_context != NULL), "Null PCLXL context cannot be deleted");
  HQASSERT((pclxl_context->memory_pool != NULL), "PCLXL context refers to a NULL memory pool");
  memory_pool = pclxl_context->memory_pool;

  PCLXL_DEBUG(PCLXL_DEBUG_INITIALIZATION, ("Deleting PCLXL_CONTEXT"));

  pclxl_ps_finish_core(pclxl_context);

  while ( pclxl_context->graphics_state != NULL ) {
    pclxl_delete_graphics_state(pclxl_context,
                                pclxl_pop_graphics_state(&pclxl_context->graphics_state));
  }

  if ( pclxl_context->parser_context != NULL ) {
    pclxl_delete_parser_context(pclxl_context->parser_context);
    pclxl_context->parser_context = NULL;
  }

  if ( pclxl_context->error_info_list != NULL ) {
    pclxl_delete_error_info_list(&pclxl_context->error_info_list);
  }

  if ( pclxl_context->font_header != NULL ) {
    pclxl_delete_font_header(pclxl_context->font_header);
    pclxl_context->font_header = NULL;
  }

  if ( pclxl_context->char_data != NULL ) {
    pclxl_delete_char_data(pclxl_context->char_data);
    pclxl_context->char_data = NULL;
  }

  pclxl_patterns_finish(pclxl_context);

  if (pclxl_context->stream_cache != NULL) {
    pclxl_stream_cache_destroy(&pclxl_context->stream_cache) ;
  }

  pclxl_unmount_user_defined_stream_device() ;

  pclxl_unmount_pass_through_device() ;

  mm_free(memory_pool, pclxl_context, sizeof(PCLXL_CONTEXT_STRUCT));

#if defined(DEBUG_BUILD)
  /* Check for any mem leaks in xml pool */
  {
    size_t cb =  mm_pool_size(memory_pool) ;
    if ( cb > 0 ) {
      monitorf((uint8*)"PCLXL pool mem leak of %u bytes\n", cb) ;
    }
/* #define PCLXL_DEBUG_MEMORY_ALLOCATION 1 */
#ifdef PCLXL_DEBUG_MEMORY_ALLOCATION

    /*
     * Note that although this may create the "mmlog" file
     * We must still also enable the ongoing logging of memory allocations
     * by setting debug_mm_watchlevel to DEBUG_MM_WATCH_LIVE (== 8)
     * in core\mm\src\mmwatch.c
     */

    mm_debug_watch_live( mm_trace ) ;
    mm_trace_close() ;

#endif
  }
#endif

#if defined(METRICS_BUILD)
  { /* Track peak memory allocated in pool.*/
    size_t max_size = 0 ;
    int32 max_objects ;
    mm_debug_total_highest(memory_pool, &max_size, &max_objects ) ;
  }
#endif

  if ( mm_sac_present(memory_pool) == MM_SUCCESS )
  {
    mm_sac_destroy(memory_pool);
  }

  mm_pool_destroy(memory_pool);

  *old_pclxl_context = NULL ;
}

Bool pclxl_create_context(
  corecontext_t*  corecontext,
  OBJECT*         config_params_dict,
  PCLXL_CONTEXT*  pclxl_context)
{
  mm_pool_t memory_pool;
  /*
   * In order to be able to use the PCLXL memory pool
   * as the storage for Postscript PATHLISTs and CLIPRECORDs
   * which are created/destroyed via "core" memory allocation function
   * mm_sac_alloc() we need the PCLXL memory pool to support
   * "Segregated Allocation Classes for these classes of object
   *
   * So, in addition to creating the raw memory pool we must
   * assitionally create/attach a "sac"
   */
#define SAC_ALLOC_PATHSIZE (DWORD_ALIGN_UP(size_t, sizeof(PATHLIST)))   /* 16 bytes - 03-Nov-2008 */
#define SAC_ALLOC_LINESIZE (DWORD_ALIGN_UP(size_t, sizeof(LINELIST)))   /* 24 bytes - 03-Nov-2008 */
#define SAC_ALLOC_CLIPSIZE (DWORD_ALIGN_UP(size_t, sizeof(CLIPRECORD))) /* 96 bytes - 03-Nov-2008 */
  struct mm_sac_classes_t sac_classes[] =
  {
    { SAC_ALLOC_PATHSIZE, 512, 20 },
    { SAC_ALLOC_LINESIZE, 1024, 30, },
    { SAC_ALLOC_CLIPSIZE, 128, 10 },
  };
  PCLXL_CONTEXT new_pclxl_context;

  pclxl_initialize_streams();

  *pclxl_context = NULL;

  if ( mm_pool_create(&memory_pool, PCLXL_POOL_TYPE, PCLXL_POOL_PARAMS) != MM_SUCCESS ) {
    (void) PCLXL_ERROR_HANDLER(NULL,
                               PCLXL_SS_KERNEL,
                               PCLXL_INSUFFICIENT_MEMORY,
                               ("Failed to allocate PCLXL memory pool"));
    return FALSE;
  }

  if ( mm_sac_create(memory_pool, sac_classes, NUM_ARRAY_ITEMS(sac_classes)) != MM_SUCCESS )
  {
    mm_pool_destroy(memory_pool);

    (void) PCLXL_ERROR_HANDLER(NULL,
                               PCLXL_SS_KERNEL,
                               PCLXL_INSUFFICIENT_MEMORY,
                               ("Failed to create Segregrated Access Control"));

    return FALSE;
  }

  if ( (new_pclxl_context = mm_alloc(memory_pool, sizeof(PCLXL_CONTEXT_STRUCT),
                                     MM_ALLOC_CLASS_PCLXL_CONTEXT)) == NULL) {
    mm_pool_destroy(memory_pool);
    (void) PCLXL_ERROR_HANDLER(NULL,
                               PCLXL_SS_KERNEL,
                               PCLXL_INSUFFICIENT_MEMORY,
                               ("Failed to allocate PCLXL context structure"));
    return FALSE;
  }

  HqMemZero(new_pclxl_context, sizeof(PCLXL_CONTEXT_STRUCT));

  new_pclxl_context->corecontext = corecontext;
  new_pclxl_context->memory_pool = memory_pool;
  new_pclxl_context->parser_context = NULL;
  new_pclxl_context->graphics_state = NULL;
  new_pclxl_context->error_reporting = PCLXL_eErrorPage;
  new_pclxl_context->error_info_list = NULL;
  new_pclxl_context->font_header = NULL;
  new_pclxl_context->config_params = pclxl_config_params;

  pclxl_patterns_init(new_pclxl_context);

#define USER_DEFINED_STREAM_HASH_SIZE 37

  if (! pclxl_stream_cache_create(&new_pclxl_context->stream_cache,
                                  USER_DEFINED_STREAM_HASH_SIZE, memory_pool)) {
    pclxl_destroy_context(&new_pclxl_context);
  }

  /* We now want: 1 x PCLXL_PARSER_CONTEXT, 1 x PCLXL_NON_GS_STATE and
   * initially just 1 x PCLXL_GRAPHICS_STATE
   */

  if ( (config_params_dict != NULL) &&
       (!pclxl_set_config_params(config_params_dict,
                                 &new_pclxl_context->config_params,
                                 (uint8*) "pclxlexec")) ) {
    pclxl_destroy_context(&new_pclxl_context);
    return FALSE;
  }

  if ( (new_pclxl_context->parser_context =
        pclxl_create_parser_context(new_pclxl_context, STATE_JOB)) == NULL ) {
    (void) PCLXL_ERROR_HANDLER(new_pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INSUFFICIENT_MEMORY,
                               ("Failed to create initial PCLXL parser context"));

    pclxl_destroy_context(&new_pclxl_context);
    return FALSE;
  }

  if ( (new_pclxl_context->graphics_state =
        pclxl_create_graphics_state(NULL, new_pclxl_context)) == NULL ) {
    (void) PCLXL_ERROR_HANDLER(new_pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INSUFFICIENT_MEMORY,
                               ("Failed to create initial PCLXL graphics state"));

    pclxl_destroy_context(&new_pclxl_context);
    return FALSE;
  }

  if ( !pclxl_init_non_gs_state(new_pclxl_context,
                                &new_pclxl_context->non_gs_state) ) {
    pclxl_destroy_context(&new_pclxl_context);
    return FALSE;
  }

  if ( !pclxl_set_courier_weight(new_pclxl_context,
                                 new_pclxl_context->config_params.courier_weight) )
  {
    pclxl_destroy_context(&new_pclxl_context);
    return FALSE;
  }

  if ( !pclxl_ps_init_core(new_pclxl_context) ) {
    pclxl_destroy_context(&new_pclxl_context);
    return FALSE;
  }

  if (! pclxl_mount_user_defined_stream_device()) {
    pclxl_destroy_context(&new_pclxl_context);
    return FALSE;
  }

  if (! pclxl_mount_pass_through_device()) {
    pclxl_destroy_context(&new_pclxl_context);
    return FALSE;
  }

  PCLXL_DEBUG(PCLXL_DEBUG_INITIALIZATION, ("Created new PCLXL_CONTEXT"));

  *pclxl_context = new_pclxl_context;
  return TRUE ;
}

/******************************************************************************
* Log stripped */
