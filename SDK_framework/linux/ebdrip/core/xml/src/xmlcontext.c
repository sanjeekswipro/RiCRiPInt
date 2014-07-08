/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!src:xmlcontext.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of xmlexec context.
 */

#include "core.h"
#include "coreinit.h"
#include "swstart.h"
#include "metrics.h"
#include "mps.h"
#include "mpscmvff.h"
#include "mmcompat.h"           /* mm_alloc_with_header etc.. */
#include "objects.h"            /* OINTEGER */
#include "swerrors.h"           /* error_handler */
#include "ripdebug.h"           /* register_ripvar */
#include "namedef_.h"           /* NAME_* */
#include "objnamer.h"
#include "monitor.h"            /* monitorf */
#include "hqmemcpy.h"
#include "hqmemset.h"

#include "xml.h"                /* xmlg* interface */

#include "xmlrecognitionpriv.h"
#include "xmldebug.h"
#include "xmlcontext.h"
#include "xmlops.h"
#include "xmlparse.h"

#ifdef PROFILE_XML
#include <stdio.h>
#define XML_TRACE(_out_) printf(_out_)
#else
#define XML_TRACE(_out_)
#endif

/* ============================================================================
 * Global contexts
 * ============================================================================
 */
XMLContext_Context xml_context = {
  (xml_contextid_t) 1,
} ;

/**
 * The core XML sub-system context.
 */
xmlGContext *core_xml_subsystem = NULL ;

/**
 * The core URI sub-system context.
 */
hqn_uri_context_t *core_uri_context = NULL ;

#ifdef METRICS_BUILD
static struct xml_metrics {
  int32 mm_xml_pool_max_size ;
  int32 mm_xml_pool_max_objects ;
  int32 mm_xml_pool_max_frag;
} xml_metrics ;

static Bool xml_metrics_update(sw_metrics_group *metrics)
{
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("MM")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("XML")) )
    return FALSE ;

  SW_METRIC_INTEGER("PeakPoolSize",
                    xml_metrics.mm_xml_pool_max_size) ;
  SW_METRIC_INTEGER("PeakPoolObjects",
                    xml_metrics.mm_xml_pool_max_objects) ;
  SW_METRIC_INTEGER("PeakPoolFragmentation",
                    xml_metrics.mm_xml_pool_max_frag);

  sw_metrics_close_group(&metrics) ;
  sw_metrics_close_group(&metrics) ;

  return TRUE ;
}

static void xml_metrics_reset(int reason)
{
  struct xml_metrics init = { 0 } ;

  UNUSED_PARAM(int, reason) ;

  xml_metrics = init ;
}

static sw_metrics_callbacks xml_metrics_hook = {
  xml_metrics_update,
  xml_metrics_reset,
  NULL
} ;
#endif

/* ============================================================================
 * Memory allocation routines used for plugging the XML sub-system,
 * the XML parser and the XML filter chains. The memory allocation
 * functions should NOT be used.
 *
 * The XML sub-system memory pool survives the life time of the RIP.
 *
 * The XML memory pool survives the life time of the first
 * xmlexec. Subsequent xmlexec invocations will use the same memory
 * pool. Typically this means that the XML memory pool survives the
 * life time of an XML job.
 * ============================================================================
 */
mm_pool_t mm_xml_pool = NULL;

static Bool xml_mem_pool_swinit(struct SWSTART *params)
{
  Bool res;

  UNUSED_PARAM(struct SWSTART *, params) ;
  HQASSERT(mm_xml_pool == NULL, "xml pool is not NULL");

  res = mm_pool_create(&mm_xml_pool, XML_PARSE_POOL_TYPE, XML_POOL_PARAMS);
  if ( res != MM_SUCCESS ) {
    mm_xml_pool = NULL;
    return error_handler(VMERROR);
  }

  return TRUE;
}

static void xml_mem_pool_finish(void)
{
  HQASSERT(mm_xml_pool != NULL, "xml pool is NULL");

#if defined(DEBUG_BUILD)
  /* Check for any mem leaks in xml pool */
  {
    size_t cb =  mm_pool_size(mm_xml_pool);
    if ( cb > 0 ) {
      monitorf((uint8*)"xml pool mem leak of %u bytes", cb);
    }
  }
#endif

#if defined(METRICS_BUILD)
  { /* Track peak memory allocated in pool. */
    size_t max_size = 0, max_frag = 0;
    int32 max_objects ;
    mm_debug_total_highest(mm_xml_pool, &max_size, &max_objects, &max_frag);
    xml_metrics.mm_xml_pool_max_size = CAST_SIZET_TO_INT32(max_size) ;
    xml_metrics.mm_xml_pool_max_objects = max_objects ;
    xml_metrics.mm_xml_pool_max_frag = CAST_SIZET_TO_INT32(max_frag);
  }
#endif

  mm_pool_destroy(mm_xml_pool);
}

static void * xmlexec_malloc(size_t size)
{
  void *alloc;
  HQASSERT(mm_xml_pool != NULL, "xml pool is NULL") ;

  XML_TRACE("alloc: xmlexec_malloc\n") ;

  alloc = mm_alloc_with_header(mm_xml_pool, (mm_size_t)size,
                               MM_ALLOC_CLASS_XML_PARSER) ;
  if (alloc == NULL)
    (void) error_handler(VMERROR);

  return alloc;
}

static void * xmlexec_realloc(void *memPtr, size_t size)
{
  void *alloc;
  HQASSERT(mm_xml_pool != NULL, "xml pool is NULL") ;

  alloc = mm_realloc_with_header(mm_xml_pool, memPtr, (mm_size_t)size,
                                 MM_ALLOC_CLASS_XML_PARSER) ;
  if (alloc == NULL)
    (void) error_handler(VMERROR);

  return alloc;
}

static void xmlexec_free(void *memPtr)
{
  HQASSERT(mm_xml_pool != NULL, "xml pool is NULL") ;
  mm_free_with_header(mm_xml_pool, memPtr) ;
}

/* Used to plug XML parser and filter chain memory allocation. */
xmlGMemoryHandler xmlexec_memory_handlers =
{
  xmlexec_malloc,
  xmlexec_realloc,
  xmlexec_free
} ;

static mps_pool_t mps_xml_subsystem_pool = NULL ;

/**
 * \brief Each block allocated needs to remember its size, so we add
 * a header to keep it in.  HEADER_BLOCK and BLOCK_HEADER translate
 * between a pointer to the header and a pointer to the block (what
 * the caller of malloc gets).
 */
typedef struct xml_header_s {
  size_t size;
} xml_header_s;

#define MPS_PF_ALIGN    8 /* .hack.align */

/* To emulate EPDR-like behaviour using mvff */
#define EPDR_LIKE ( mps_bool_t )1, ( mps_bool_t )1, ( mps_bool_t )1

#define HEADER_SIZE \
  ((sizeof(xml_header_s) + MPS_PF_ALIGN - 1) & ~(MPS_PF_ALIGN - 1))
#define HEADER_BLOCK(header) \
  ((void *)((char *)(header) + HEADER_SIZE))
#define BLOCK_HEADER(block) \
  ((xml_header_s *)((char *)(block) - HEADER_SIZE))

static Bool xml_subsystem_mem_pool_swinit(struct SWSTART *params)
{
  mps_res_t res ;

  UNUSED_PARAM(struct SWSTART *, params) ;

  HQASSERT(mps_xml_subsystem_pool == NULL, "xml subsystem pool is not NULL") ;

  /* We need to create an MPS pool directly because the URI and XML
     layers are not within the core. */
  res = mps_pool_create(&mps_xml_subsystem_pool, mm_arena, mps_class_mvff(),
                        (size_t)65536, (size_t)32, (size_t)8, EPDR_LIKE) ;
  if ( res == MPS_RES_OK ) {
    mps_word_t label = mps_telemetry_intern("XML library pool");
    mps_telemetry_label((mps_addr_t)mps_xml_subsystem_pool, label);
  } else {
    return FAILURE(FALSE) ;
  }

  return TRUE;
}

static void xml_subsystem_mem_pool_finish(void)
{
  HQASSERT(mps_xml_subsystem_pool != NULL, "xml subsystem pool is NULL");

#if 0
  /* No equivalent in MPS layer that I can find yet. */
#if defined(DEBUG_BUILD)
  /* Check for any mem leaks in xml pool */
  {
    size_t cb =  mm_pool_size(mm_xml_temp_pool);
    if ( cb > 0 ) {
      monitorf((uint8*)"xml temp pool mem leak of %u bytes", cb);
    }
  }
#endif
#endif

  mps_pool_destroy(mps_xml_subsystem_pool);
}

void * xml_subsystem_malloc(size_t size)
{
  HQASSERT(mps_xml_subsystem_pool != NULL, "xml temp pool is NULL");

  XML_TRACE("alloc: xml_subsystem_malloc\n") ;

  if (size != 0 && (size + HEADER_SIZE > size)) {
    void * p ;
    mps_res_t res ;
    res = mps_alloc(&p, mps_xml_subsystem_pool, size + HEADER_SIZE) ;
    if (! res) {
      xml_header_s *header ;
      header = p ;
      header->size = size + HEADER_SIZE ;
      return HEADER_BLOCK(header) ;
    }
  }

  (void)error_handler(VMERROR) ;
  return NULL ;
}

void xml_subsystem_free(void *memPtr)
{
  size_t size;
  xml_header_s *header;

  HQASSERT(mps_xml_subsystem_pool != NULL, "xml subsystem pool is NULL") ;

  if (memPtr == NULL)
    return ;

  header = BLOCK_HEADER(memPtr) ;
  size = header->size;

  /* detect double frees */
  HQASSERT(size != 0, "size is zero") ;
  header->size = 0 ;

  mps_free(mps_xml_subsystem_pool, header, size);
}

void* xml_subsystem_realloc(void *memPtr, size_t size)
{
  size_t oldsize ;
  HQASSERT(mps_xml_subsystem_pool != NULL, "xml temp pool is NULL");

  XML_TRACE("alloc: xml_subsystem_malloc\n") ;

  oldsize = BLOCK_HEADER(memPtr)->size ;

  if (size != 0 && (size + HEADER_SIZE > size)) {
    void *p, *newptr ;
    mps_res_t res ;
    res = mps_alloc(&p, mps_xml_subsystem_pool, size + HEADER_SIZE) ;
    if (! res) {
      xml_header_s *header ;
      header = p ;
      header->size = size + HEADER_SIZE ;
      newptr = HEADER_BLOCK(p) ;
      HqMemCpy( newptr, memPtr, oldsize ) ;
      xml_subsystem_free(memPtr) ;
      return newptr ;
    }
  }

  (void)error_handler(VMERROR) ;
  return NULL ;
}

static xmlGMemoryHandler xml_subsystem_memory_functs =
{
  xml_subsystem_malloc,
  xml_subsystem_realloc,
  xml_subsystem_free
} ;

/* ============================================================================
 * URI sub-system memory allocation routines. These functions should
 * NOT be used. We overload into the XML subsystem pool for the core.
 * ============================================================================
 */
#define uri_malloc  xml_subsystem_malloc
#define uri_realloc xml_subsystem_realloc
#define uri_free    xml_subsystem_free

/**
 * Structure required to re-map URI memory management through initialisation
 * function.
 */
static hqn_uri_memhandler_t uri_memory_functs = {
  uri_malloc,
  uri_realloc,
  uri_free
} ;

/* ============================================================================
 * String interning
 * ============================================================================
 */
static Bool intern_create_fn(
      xmlGContext *xml_subsystem_context,
      const xmlGIStr **istr,
      const uint8 *strbuf,
      uint32 bytelen)
{
  UNUSED_PARAM(xmlGContext *, xml_subsystem_context) ;
  return intern_create(istr, strbuf, bytelen) ;
}

static uintptr_t intern_hash_fn(
      xmlGContext *xml_subsystem_context,
      const xmlGIStr *istr)
{
  UNUSED_PARAM(xmlGContext *, xml_subsystem_context) ;
  return intern_hash(istr) ;
}

static uint32 intern_length_fn(
      xmlGContext *xml_subsystem_context,
      const xmlGIStr *istr)
{
  UNUSED_PARAM(xmlGContext *, xml_subsystem_context) ;
  return intern_length(istr) ;
}

static const uint8 *intern_value_fn(
      xmlGContext *xml_subsystem_context,
      const xmlGIStr *istr)
{
  UNUSED_PARAM(xmlGContext *, xml_subsystem_context) ;
  return intern_value(istr) ;
}

static Bool intern_equal_fn(
      xmlGContext *xml_subsystem_context,
      const xmlGIStr *istr1,
      const xmlGIStr *istr2)
{
  UNUSED_PARAM(xmlGContext *, xml_subsystem_context) ;
  return istr1 == istr2 ;
}

/**
 * Function pointers for interning strings.
 */
static xmlGIStrHandler xml_intern_functs =
{
  intern_create_fn,
  NULL, /* reserve */
  NULL, /* destroy */
  intern_hash_fn,
  intern_length_fn,
  intern_value_fn,
  intern_equal_fn,
  NULL /* terminate */
};

/* ============================================================================
 * XML processing init/terminate
 * ============================================================================
 */

static Bool core_uri_context_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  if (! hqn_uri_init(&core_uri_context, &uri_memory_functs)) {
    return FAILURE(FALSE) ;
  }
  HQASSERT(core_uri_context != NULL, "core_uri_context is NULL") ;

  return TRUE ;
}

static void core_uri_context_finish(void)
{
  hqn_uri_finish(&core_uri_context) ;
}

/** Initialise the core XML system, which consists of libgenxml, the URI system,
 * and associated memory pools.
 */
static Bool xml_g_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  /* initialize the core XML sub-system. */
  if (! xmlg_initialize(&core_xml_subsystem,
                        mps_xml_subsystem_pool,
                        &xml_subsystem_memory_functs,
                        &xml_intern_functs,
                        core_uri_context)) {
    return FAILURE(FALSE) ;
  }

  return TRUE;
}

/** Finish the core XML system.
 */
static void xml_g_finish(void)
{
  xmlg_terminate(&core_xml_subsystem) ;
}

static void init_C_globals_xmlcontext(void)
{
  XMLContext_Context init = {
    (xml_contextid_t) 1,
  } ;

  xml_context = init ;
  /* initialise xml context list to be empty. */
  SLL_RESET_LIST(&xml_context.sls_contexts) ;
  core_xml_subsystem = NULL ;
  core_uri_context = NULL ;
  mm_xml_pool = NULL ;
  mps_xml_subsystem_pool = NULL ;

#ifdef METRICS_BUILD
  xml_metrics_reset(SW_METRICS_RESET_BOOT) ;
  sw_metrics_register(&xml_metrics_hook) ;
#endif
}

/** Initialisation sub-table for CORExml. */
static core_init_t xml_init[] = {
  /* XML streams init/finish does nothing at the moment, so don't bother
     calling it. */
  CORE_INIT_LOCAL("psdevuri", NULL, psdevuri_swstart, NULL, psdevuri_finish),
  CORE_INIT_LOCAL("namespace", NULL, namespace_recognition_swstart, NULL,
                  namespace_recognition_finish),
  CORE_INIT_LOCAL("XML subsystem memory",
                  xml_subsystem_mem_pool_swinit, NULL, NULL,
                  xml_subsystem_mem_pool_finish),
  CORE_INIT_LOCAL("XML memory", xml_mem_pool_swinit, NULL, NULL,
                  xml_mem_pool_finish),
  CORE_INIT_LOCAL("core URI", NULL, core_uri_context_swstart, NULL,
                  core_uri_context_finish),
  CORE_INIT_LOCAL("XMLg", NULL, xml_g_swstart, NULL, xml_g_finish),
} ;

static Bool xml_swinit(SWSTART *params)
{
  return core_swinit_run(xml_init, NUM_ARRAY_ITEMS(xml_init), params) ;
}

/**
 * \brief Initialise xml handling module.
 */
static Bool xml_swstart(SWSTART *params)
{
  XMLPARAMS *xmlparams ;

  CoreContext.xmlparams = xmlparams = mm_alloc_static(sizeof(XMLPARAMS)) ;
  if ( xmlparams == NULL )
    return FALSE ;

#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
  register_ripvar(NAME_debug_xml, OINTEGER, &debug_xml) ;
#endif

  return core_swstart_run(xml_init, NUM_ARRAY_ITEMS(xml_init), params) ;
}

static Bool xml_postboot(void)
{
  return core_postboot_run(xml_init, NUM_ARRAY_ITEMS(xml_init)) ;
}

/**
 * \brief Tidy up xml handling module on application exit.
 */
static void xml_finish(void)
{
  corecontext_t* corecontext;

  core_finish_run(xml_init, NUM_ARRAY_ITEMS(xml_init)) ;

  if ((corecontext = get_core_context()) != NULL) {
    corecontext->xmlparams = NULL ;
  }
}

IMPORT_INIT_C_GLOBALS(psdevuri)
IMPORT_INIT_C_GLOBALS(recognition)
IMPORT_INIT_C_GLOBALS(xmldebug)

void xml_C_globals(core_init_fns *fns)
{
  init_C_globals_psdevuri() ;
  init_C_globals_recognition() ;
  init_C_globals_xmlcontext() ;
  init_C_globals_xmldebug() ;

  fns->swinit = xml_swinit ;
  fns->swstart = xml_swstart ;
  fns->postboot = xml_postboot ;
  fns->finish = xml_finish ;

  core_C_globals_run(xml_init, NUM_ARRAY_ITEMS(xml_init)) ;
}

/* ============================================================================
 * XML execution context.
 * ============================================================================
 */

Bool xmlexec_context_create(
  XMLExecContext**  pp_xmlexec_context)
{
  XMLExecContext* p_xmlexec_context ;
  HQASSERT((pp_xmlexec_context != NULL), "pp_xmlexec_context is NULL") ;

  *pp_xmlexec_context = NULL ;

  /* Allocate context signalling VMERROR as required */
  p_xmlexec_context = mm_alloc(mm_xml_pool, sizeof(XMLExecContext),
                               MM_ALLOC_CLASS_XML_CONTEXT) ;

  if ( p_xmlexec_context == NULL )
    return error_handler(VMERROR) ;

  /* Quick initialisation of context */
  HqMemZero(p_xmlexec_context, sizeof(XMLExecContext)) ;

  /* Add new context to MRU list */
  SLL_RESET_LINK(p_xmlexec_context, sll) ;
  SLL_ADD_HEAD(&xml_context.sls_contexts, p_xmlexec_context, sll) ;

  /* Assign new context id */
  p_xmlexec_context->id = xml_context.next_id++ ;
  if ( xml_context.next_id == 0 ) {
    HQFAIL("wrapped a 32 bit counter!") ;
    xml_context.next_id = 1 ;
  }

  NAME_OBJECT(p_xmlexec_context, XMLEXEC_CONTEXT_NAME) ;

  *pp_xmlexec_context = p_xmlexec_context ;

  return TRUE ;
}

void xmlexec_context_destroy(
  XMLExecContext**  pp_xmlexec_context)
{
  XMLExecContext*   p_xmlexec_context ;

  HQASSERT((pp_xmlexec_context != NULL), "pp_xmlexec_context is NULL") ;
  HQASSERT((*pp_xmlexec_context != NULL), "pp_xmlexec_context pointer is NULL") ;

  p_xmlexec_context = *pp_xmlexec_context ;

  /* Remove context from head of MRU list */
  HQASSERT((p_xmlexec_context == SLL_GET_HEAD(&xml_context.sls_contexts, XMLExecContext, sll)),
           "context not MRU") ;

  /* Call doc type context destroy before doing anything else. */
  if (p_xmlexec_context->doc_context != NULL &&
      p_xmlexec_context->f_destroy != NULL) {
      p_xmlexec_context->f_destroy(&(p_xmlexec_context->doc_context)) ;
  }

  SLL_REMOVE_HEAD(&xml_context.sls_contexts) ;
  UNNAME_OBJECT(p_xmlexec_context) ;

  /* Free the context */
  mm_free(mm_xml_pool, p_xmlexec_context, sizeof(XMLExecContext)) ;
  *pp_xmlexec_context = NULL ;
}

xml_contextid_t xml_current_context_id(void)
{
  XMLExecContext *p_xmlexec_context ;

  p_xmlexec_context = SLL_GET_HEAD(&xml_context.sls_contexts, XMLExecContext, sll) ;
  if ( p_xmlexec_context != NULL )
    return p_xmlexec_context->id ;

  return 0 ;
}

xmlDocumentContext* xml_get_current_doc_context(void)
{
  XMLExecContext *p_xmlexec_context ;

  p_xmlexec_context = SLL_GET_HEAD(&xml_context.sls_contexts, XMLExecContext, sll) ;
  if ( p_xmlexec_context == NULL )
    return NULL ;

  return p_xmlexec_context->doc_context ;
}


/* ============================================================================
* Log stripped */
