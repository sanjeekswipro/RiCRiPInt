/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pcl5context.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of pcl5exec context.
 */

#include "core.h"
#include "pcl5context_private.h"

#include "coreinit.h"
#include "pcl5.h"
#include "pcl5color.h"
#include "pcl5scan.h"
#include "hpgl2dispatch.h"
#include "macros.h"
#include "pagecontrol.h"
#include "printmodel.h"
#include "macrodev.h"
#include "pcl5devs.h"
#include "pcl.h"
#include "pcl5ctm.h"
#include "pcl5fonts.h"
#include "hqmemset.h"
#include "metrics.h"
#include "pcl5metrics.h"

#include "ascii.h"
#include "gstack.h"
#include "gu_chan.h"
#include "objects.h"
#include "dictscan.h"
#include "swerrors.h"
#include "monitor.h"
#include "namedef_.h"
#include "ripdebug.h"
#include "jobcontrol.h"
#include "miscops.h"

#include "swpfinpcl.h"

#if defined(DEBUG_BUILD)
int32 debug_pcl5;
#endif /* DEBUG_BUILD */

/* Global for fast testing to see if we are defining a macro or
   not. */
Bool pcl5_recording_a_macro ;

/* Global for fast testing to see if we are executing a macro or
   not. Gets incremented per recursive interpreter call. */
uint32 pcl5_macro_nest_level ;

/* What type of macro are we executing? */
int32 pcl5_current_macro_mode ;

/* The intention is that this pool is used for all PCL processing
   PDL's. i.e. PCL5, PCL-XL and HPGL2. */
mm_pool_t mm_pcl_pool = NULL;

/* List of PCL5 execution contexts. */
PCL5_RIP_LifeTime_Context pcl5_rip_context = {
  (pcl5_contextid_t) 1,
} ;

/* ============================================================================
 * PCL metrics hooks
 * ============================================================================
 */

#ifdef METRICS_BUILD
PCL5_Metrics pcl5_metrics ;

static Bool pcl5_metrics_update(sw_metrics_group *metrics)
{
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("MM")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("PCL")) )
    return FALSE ;

  SW_METRIC_INTEGER("PeakPoolSize",
                    pcl5_metrics.pcl_pool_max_size) ;
  SW_METRIC_INTEGER("PeakPoolObjects",
                    pcl5_metrics.pcl_pool_max_objects) ;
  SW_METRIC_INTEGER("PeakPoolFragmentation",
                    pcl5_metrics.pcl_pool_max_frag);

  sw_metrics_close_group(&metrics) ;
  sw_metrics_close_group(&metrics) ;

  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("PCL")) )
    return FALSE ;

  SW_METRIC_INTEGER("user_patterns", pcl5_metrics.userPatterns) ;

  sw_metrics_close_group(&metrics) ;

  return TRUE ;
}

static void pcl5_metrics_reset(int reason)
{
  PCL5_Metrics init = { 0 } ;

  UNUSED_PARAM(int, reason) ;

  pcl5_metrics = init ;
}

static sw_metrics_callbacks pcl5_metrics_hook = {
  pcl5_metrics_update,
  pcl5_metrics_reset,
  NULL
} ;
#endif

/* ============================================================================
 * PCL memory pool
 * ============================================================================
 */

static Bool pcl_mem_pool_swinit(struct SWSTART *params)
{
  Bool res ;

  UNUSED_PARAM(struct SWSTART *, params) ;
  HQASSERT(mm_pcl_pool == NULL, "pcl pool is not NULL") ;

  res = mm_pool_create(&mm_pcl_pool, PCL_POOL_TYPE, PCL_POOL_PARAMS) ;
  if ( res != MM_SUCCESS ) {
    return FAILURE(FALSE) ;
  }

  return TRUE ;
}

static void pcl_mem_pool_finish(void)
{
  HQASSERT(mm_pcl_pool != NULL, "pcl pool is NULL") ;

#if defined(DEBUG_BUILD)
  /* Check for any mem leaks in xml pool */
  {
    size_t cb =  mm_pool_size(mm_pcl_pool) ;
    if ( cb > 0 ) {
      monitorf((uint8*)"PCL pool mem leak of %u bytes\n", cb) ;
    }
#if 0
    mm_debug_watch_live( mm_trace ) ;
    mm_trace_close() ;
#endif
  }
#endif

#if defined(METRICS_BUILD)
  { /* Track peak memory allocated in pool. */
    size_t max_size = 0, max_frag = 0;
    int32 max_objects ;
    mm_debug_total_highest(mm_pcl_pool, &max_size, &max_objects, &max_frag);
    if (pcl5_metrics.pcl_pool_max_size < CAST_SIZET_TO_INT32(max_size))
      pcl5_metrics.pcl_pool_max_size = CAST_SIZET_TO_INT32(max_size) ;
    if (pcl5_metrics.pcl_pool_max_objects < max_objects)
      pcl5_metrics.pcl_pool_max_objects = max_objects ;
    if (pcl5_metrics.pcl_pool_max_frag < CAST_SIZET_TO_INT32(max_frag))
      pcl5_metrics.pcl_pool_max_frag = CAST_SIZET_TO_INT32(max_frag) ;
  }
#endif

  mm_pool_destroy(mm_pcl_pool) ;
}

/* ============================================================================
 * Module init/finish
 * ============================================================================
 */

static void init_C_globals_pcl5context(void)
{
  PCL5_RIP_LifeTime_Context init = {
    (pcl5_contextid_t) 1,
  } ;

#if defined(DEBUG_BUILD)
  debug_pcl5 = 0 ;
#endif /* DEBUG_BUILD */

  pcl5_recording_a_macro = FALSE ;
  pcl5_macro_nest_level = 0 ;
  pcl5_current_macro_mode = PCL5_NOT_EXECUTING_MACRO ;

  mm_pcl_pool = NULL ;

  pcl5_rip_context = init ;
  /* Initialise PCL5 execution context list to be empty. */
  SLL_RESET_LIST(&pcl5_rip_context.sls_contexts) ;

#ifdef METRICS_BUILD
  pcl5_metrics_reset(SW_METRICS_RESET_BOOT) ;
  sw_metrics_register(&pcl5_metrics_hook) ;
#endif
}

/* Trampolines to be able to use an initialisation table to unwind on error. */
static Bool pclmacrodev_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;
  return pcl5_macrodev_init(&pcl5_rip_context) ;
}

static void pclmacrodev_finish(void)
{
  pcl5_macrodev_finish(&pcl5_rip_context) ;
}

static Bool pclmacrorec_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;
  return pcl5_macro_record_filter_init() ;
}

static Bool pcldevs_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;
  return pcl5_devices_init(&pcl5_rip_context) ;
}

static void pcldevs_finish(void)
{
  pcl5_devices_finish(&pcl5_rip_context) ;
}

static Bool pclrescache_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;
  return pcl5_resource_caches_init(&pcl5_rip_context) ;
}

static void pclrescache_finish(void)
{
  pcl5_resource_caches_finish(&pcl5_rip_context) ;
}

static Bool pclcolor_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;
  return pcl5_color_init(&pcl5_rip_context) ;
}

static void pclcolor_finish(void)
{
  pcl5_color_finish(&pcl5_rip_context) ;
}

/** Initialisation sub-table for COREpcl_pcl5. */
static core_init_t pcl5_init[] = {
  CORE_INIT_LOCAL("PCL memory", pcl_mem_pool_swinit, NULL, NULL,
                  pcl_mem_pool_finish),
  CORE_INIT_LOCAL("PCL5 macrodev", NULL, pclmacrodev_swstart, NULL,
                  pclmacrodev_finish),
  CORE_INIT_LOCAL("PCL5 macro record", NULL, pclmacrorec_swstart, NULL, NULL),
  CORE_INIT_LOCAL("PCL5 devices", NULL, pcldevs_swstart, NULL, pcldevs_finish),
  CORE_INIT_LOCAL("PCL5 resource cache", NULL, pclrescache_swstart, NULL,
                  pclrescache_finish),
  CORE_INIT_LOCAL("PCL5 color", NULL, pclcolor_swstart, NULL, pclcolor_finish),
  CORE_INIT("PCL5 font selection", pcl5_font_sel_C_globals),
} ;

static Bool pcl5_swinit(struct SWSTART *params)
{
  return core_swinit_run(pcl5_init, NUM_ARRAY_ITEMS(pcl5_init), params) ;
}

/**
 * \brief Initialise PCL5 handling module. This gets executed at RIP
 * boot time.
 */
static Bool pcl5_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART*, params) ;

#if defined(DEBUG_BUILD)
  register_ripvar(NAME_debug_pcl5, OINTEGER, &debug_pcl5) ;
#endif /* DEBUG_BUILD */

  return core_swstart_run(pcl5_init, NUM_ARRAY_ITEMS(pcl5_init), params) ;
}

static Bool pcl5_postboot(void)
{
  return core_postboot_run(pcl5_init, NUM_ARRAY_ITEMS(pcl5_init)) ;
}

/**
 * \brief Tidy up PCL5 handling module on RIP shutdown.
 */
static void pcl5_finish(void)
{
  font_sel_caches_free() ;
  core_finish_run(pcl5_init, NUM_ARRAY_ITEMS(pcl5_init)) ;
}

IMPORT_INIT_C_GLOBALS(hpgl2scan)
IMPORT_INIT_C_GLOBALS(macrodev)
IMPORT_INIT_C_GLOBALS(macros)
IMPORT_INIT_C_GLOBALS(pcl5devs)
IMPORT_INIT_C_GLOBALS(pcl5ops)
IMPORT_INIT_C_GLOBALS(pcl5state)
IMPORT_INIT_C_GLOBALS(printmodel)

void pcl5_C_globals(core_init_fns *fns)
{
  init_C_globals_hpgl2scan() ;
  init_C_globals_macrodev() ;
  init_C_globals_macros() ;
  init_C_globals_pcl5context() ;
  init_C_globals_pcl5devs() ;
  init_C_globals_pcl5ops() ;
  init_C_globals_pcl5state() ;
  init_C_globals_printmodel() ;

  fns->swinit = pcl5_swinit ;
  fns->swstart = pcl5_swstart ;
  fns->postboot = pcl5_postboot ;
  fns->finish = pcl5_finish ;

  core_C_globals_run(pcl5_init, NUM_ARRAY_ITEMS(pcl5_init)) ;
}

/* ============================================================================
 * Global contexts
 * ============================================================================
 */

/**
 * Configure the renderer for PCL5 rendering.
 * Backdrop rendering is used in two situations:
 * 1. Whenever the job may be color (because PCL Rop application takes place in
 *    RGB).
 * 2. Whenever the output is not monochrome (a special monochrome blitter exists
 *    to apply the PCL print model in monochrome only).
 */
static
Bool configureRenderer(PCL5Context* p_pcl5_ctxt)
{
  GUCR_RASTERSTYLE *rs = gsc_getRS(gstateptr->colorInfo);
  int valuesPerComponent = gucr_valuesPerComponent(rs);
  Bool use5eMode = (! p_pcl5_ctxt->pcl5c_enabled) && (valuesPerComponent == 2);

  /* Enable PCL5 gstate application in the core. */
  pclGstateEnable(p_pcl5_ctxt->corecontext, TRUE, use5eMode);

  if (! use5eMode) {
    DEVICESPACEID dspace ;
    int32 bits = p_pcl5_ctxt->config_params.vds_select ;

    guc_deviceColorSpace(rs, &dspace, NULL) ;
    switch ( dspace ) {
    case DEVICESPACE_Gray:
      bits >>= PCL_VDS_GRAY_SHIFT ;
      break ;
    case DEVICESPACE_CMYK:
      bits >>= PCL_VDS_CMYK_SHIFT ;
      break ;
    case DEVICESPACE_RGB:
      bits >>= PCL_VDS_RGB_SHIFT ;
      break ;
    default:
      bits >>= PCL_VDS_OTHER_SHIFT ;
      break ;
    }
    bits &= PCL_VDS_MASK ;

    /* Enable OverprintPreview to match behaviour when BackdropRender was required. */
#define SET_VIRTUAL_SPACE(b_, r_, x_)                                  \
    (uint8 *)("<</OverprintPreview " #b_ " >> setinterceptcolorspace " \
              "<<"                                                     \
              "  /VirtualDeviceSpace /" #x_                            \
              "  /LateColorManagement true "                           \
              "  /DeviceROP " #r_                                      \
              ">> setpagedevice")
    return run_ps_string(bits == PCL_VDS_CMYK_F_T
                         ? SET_VIRTUAL_SPACE(false, true, DeviceCMYK)
                         : bits == PCL_VDS_GRAY_F_T
                         ? SET_VIRTUAL_SPACE(false, true, DeviceGray)
                         : bits == PCL_VDS_RGB_T_T
                         ? SET_VIRTUAL_SPACE(true, true, DeviceRGB)
                         : SET_VIRTUAL_SPACE(true, false, DeviceRGB)) ;
  }
  return TRUE;
}

static sw_event_handler evhandler = {delete_font, NULL, 0} ;  /* Event Handler structure */

Bool pcl5_context_create(PCL5Context** pp_pcl5_ctxt,
                         corecontext_t* corecontext,
                         OBJECT *odict,
                         PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO* state_info,
                         int32 pass_through_type)
{
  PCL5Context* p_pcl5_ctxt ;

  HQASSERT((pp_pcl5_ctxt != NULL), "pp_pcl5_ctxt is NULL") ;

  *pp_pcl5_ctxt = NULL ;

  /* Allocate context signalling VMERROR as required */
  p_pcl5_ctxt = mm_alloc(mm_pcl_pool, sizeof(PCL5Context),
                         MM_ALLOC_CLASS_PCL_CONTEXT) ;

  if ( p_pcl5_ctxt == NULL )
    return error_handler(VMERROR) ;

  /* Quick initialisation of context */
  HqMemZero(p_pcl5_ctxt, sizeof(PCL5Context));
  /*
   * Initialize this PCL5 context's config_params
   * from the global pcl5_config_params
   * which *may* have been modified/configured by a prior call to setpcl5params
   *
   * This will include the boolean switch
   * to say whether we are running in 5c or 5e mode
   */

  p_pcl5_ctxt->config_params = pcl5_config_params;
  p_pcl5_ctxt->corecontext = corecontext;

  if ( odict != NULL &&
       !pcl5_set_config_params(odict, &p_pcl5_ctxt->config_params) ) {
    /*
     * Oops, we seem to have failed to process a non-NULL
     * dictionary of config params supplied for this particular job
     *
     * We must return False to say that we have failed to inititialize
     * this newly allocated PCL5 context.
     *
     * The question is: Should we be freeing the allocated
     * and incorrectly initialized space
     */

    return FALSE;
  }

  /* Set the pass through mode very early on as called functions from
     this function test the pass through mode. */
  p_pcl5_ctxt->pass_through_mode = pass_through_type ;

  p_pcl5_ctxt->pcl5c_enabled = p_pcl5_ctxt->config_params.pcl5c_enabled;

  if ( p_pcl5_ctxt->pass_through_mode == PCLXL_NO_PASS_THROUGH ) {
    /* Need to configure the renderer now as following initialisation does a
     * save so can't be done after that. */
    if ( !configureRenderer(p_pcl5_ctxt) ) {
      return(FALSE);
    }
  }

  p_pcl5_ctxt->end_of_hpgl2_op = NULL ;
  p_pcl5_ctxt->end_of_hpgl2_arg.integer = 0 ; /* arbitrary initialisation value */
  p_pcl5_ctxt->end_of_hpgl2_arg.real = 0 ; /* arbitrary initialisation value */

  /* Add new context to MRU list */
  SLL_RESET_LINK(p_pcl5_ctxt, sll) ;
  SLL_ADD_HEAD(&pcl5_rip_context.sls_contexts, p_pcl5_ctxt, sll) ;

  /* Assign new context id */
  p_pcl5_ctxt->id = pcl5_rip_context.next_id++ ;
  if ( pcl5_rip_context.next_id == 0 ) {
    HQFAIL("wrapped a 32 bit counter!") ;
    pcl5_rip_context.next_id = 1 ;
  }

  NAME_OBJECT(p_pcl5_ctxt, PCL5_CONTEXT_NAME) ;

  if (! pcl5_printstate_create(p_pcl5_ctxt, &p_pcl5_ctxt->config_params))
    goto tidyup ;

  p_pcl5_ctxt->is_combined_command = FALSE ;
  p_pcl5_ctxt->is_cached_command = FALSE ;
  p_pcl5_ctxt->cached_operation = 0 ;
  p_pcl5_ctxt->cached_group_char = 0 ;
  p_pcl5_ctxt->cached_value = pcl_zero_value;
  p_pcl5_ctxt->cached_termination_char = 0 ;
  p_pcl5_ctxt->UEL_seen = FALSE ;
  p_pcl5_ctxt->interpreter_mode_end_graphics_pending = FALSE;
  p_pcl5_ctxt->interpreter_mode_on_start_graphics = MODE_NOT_SET;
  if ( p_pcl5_ctxt->pcl5c_enabled ) {
    p_pcl5_ctxt->interpreter_mode = PCL5C_MODE;
  } else {
    p_pcl5_ctxt->interpreter_mode = PCL5E_MODE;
  }

  p_pcl5_ctxt->ops_table = NULL ;
  p_pcl5_ctxt->pcl5_ops_table = NULL ;
  p_pcl5_ctxt->state_info = state_info ;

  /* Add access to global pattern cache. */
  p_pcl5_ctxt->resource_caches = pcl5_rip_context.resource_caches;

  if (! pcl5_funct_table_create(&(p_pcl5_ctxt->pcl5_ops_table)))
    goto tidyup ;

  if (! register_pcl5_ops(p_pcl5_ctxt->pcl5_ops_table))
    goto tidyup ;

  if (! hpgl2_funct_table_create(&(p_pcl5_ctxt->ops_table)))
    goto tidyup ;

  if (! register_hpgl2_ops(p_pcl5_ctxt->ops_table))
    goto tidyup ;

  if (! pcl5_macros_init(p_pcl5_ctxt))
    goto tidyup ;

  pcl5_print_model_init(p_pcl5_ctxt);

  if (! pcl5_mount_macrodev(p_pcl5_ctxt))
    goto tidyup ;

  evhandler.context = p_pcl5_ctxt ;
  if (SwRegisterHandler(EVENT_PCL_FONT_DELETED, &evhandler, 0)
      != SW_RDR_SUCCESS)
    goto tidyup ;

  pcl5_recording_a_macro = FALSE ;
  pcl5_macro_nest_level = 0 ;
  pcl5_current_macro_mode = PCL5_NOT_EXECUTING_MACRO ;

  *pp_pcl5_ctxt = p_pcl5_ctxt ;

  return TRUE ;

tidyup:
  if ( p_pcl5_ctxt->print_state )
    mm_free(mm_pcl_pool, &(p_pcl5_ctxt->print_state), sizeof(struct PCL5PrintState)) ;
  if ( p_pcl5_ctxt->ops_table )
    hpgl2_funct_table_destroy(&(p_pcl5_ctxt->ops_table)) ;
  if ( p_pcl5_ctxt->pcl5_ops_table )
    pcl5_funct_table_destroy(&(p_pcl5_ctxt->pcl5_ops_table)) ;

  mm_free(mm_pcl_pool, p_pcl5_ctxt, sizeof(PCL5Context)) ;
  return FALSE ;
}

Bool pcl5_context_destroy(
  PCL5Context**  pp_pcl5_ctxt)
{
  PCL5Context*   p_pcl5_ctxt ;
  Bool success = TRUE ;

  HQASSERT((pp_pcl5_ctxt != NULL), "pp_pcl5_ctxt is NULL") ;
  HQASSERT((*pp_pcl5_ctxt != NULL), "pp_pcl5_ctxt pointer is NULL") ;

  p_pcl5_ctxt = *pp_pcl5_ctxt ;

  SwDeregisterHandler(EVENT_PCL_FONT_DELETED, &evhandler) ;

  /* Remove context from head of MRU list */
  HQASSERT((p_pcl5_ctxt == SLL_GET_HEAD(&pcl5_rip_context.sls_contexts, PCL5Context, sll)),
           "context not MRU") ;

  /* Document context init. */
  SLL_REMOVE_HEAD(&pcl5_rip_context.sls_contexts) ;

  pcl5_unmount_macrodev(p_pcl5_ctxt) ;
  success = pcl5_printstate_destroy(p_pcl5_ctxt) ;
  pcl5_funct_table_destroy(&(p_pcl5_ctxt->pcl5_ops_table)) ;
  hpgl2_funct_table_destroy(&(p_pcl5_ctxt->ops_table)) ;

  pcl5_print_model_finish(p_pcl5_ctxt);

  pcl5_macros_finish(p_pcl5_ctxt) ;

  /* Free the context */
  mm_free(mm_pcl_pool, p_pcl5_ctxt, sizeof(PCL5Context)) ;
  *pp_pcl5_ctxt = NULL ;

  return success ;
}

pcl5_contextid_t pcl5_current_context_id(void)
{
  PCL5Context *p_pcl5_ctxt ;

  p_pcl5_ctxt = SLL_GET_HEAD(&pcl5_rip_context.sls_contexts, PCL5Context, sll) ;
  if ( p_pcl5_ctxt != NULL )
    return p_pcl5_ctxt->id ;

  return 0 ;
}

PCL5Context* pcl5_current_context(void)
{
  return SLL_GET_HEAD(&pcl5_rip_context.sls_contexts, PCL5Context, sll) ;
}

/* ============================================================================
* Log stripped */
