/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!src:recognition.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Filter to recognise XML namespaces and register appropriate
 * PDL handlers.
 *
 * Note that the recognition filter is re-entrant. Population of the
 * namespace recognition table is NOT re-entrant. Although the global
 * recognition_table is available to all the filter callbacks, care
 * has been taken to obtain the table via the filter itself. Should we
 * decide to make the recognition table non-global, this simply means
 * changing the way the recognition table gets populated.
 */

#include "core.h"
#include "swerrors.h"
#include "mm.h"

#include "xml.h"
#include "xmlcontext.h"
#include "xmlrecognitionpriv.h"

/* ============================================================================
 * Namespace recognition table functions.
 * ============================================================================
 */

/** Smallish prime.
 * Do not expect that many namespaces will be registered.
 * \todo Hash size and function will need profiling.
 */
#define NAMESPACE_RECOGNITION_HASHTABLESIZE 37

struct NSRecognitionEntry {
  const xmlGIStr *uri ;                     /* Key                */
  XMLDocContextCreate f_create_context ;    /* Payload            */
  XMLDocContextDestroy f_destroy_context ;
  struct NSRecognitionEntry *next ;         /* Singly-linked list */
} ;

typedef struct Recognition_fltr_ctxt {
  /* Number of entries in hash table */
  unsigned int num_entries ;
  /* The hash table */
  /*@partial@*/
  struct NSRecognitionEntry **table ;
} Recognition_fltr_ctxt ;

/**
 * \brief Maps namespaces to XML PDL's.
 *
 * \note The recognition table can be populated with an empty
 * namespace for schema's which do not make use of namespaces. Should
 * we requires this, the XML start element callback
 * "first_element_callback()" would need to be improved to recognise
 * the XML PDL from different information (Example: by recognising a
 * well known element name).
 */
static Recognition_fltr_ctxt *recognition_table = NULL;

static
/*@null@*/
struct NSRecognitionEntry* find_recognition_entry(
      Recognition_fltr_ctxt *filter_ctxt,
      const xmlGIStr *uri,
      uint32 *hval)
{
  uintptr_t hash ;
  struct NSRecognitionEntry *curr;
  HQASSERT(filter_ctxt != NULL, "filter_ctxt is NULL") ;
  HQASSERT(uri != NULL, "uri is NULL") ;
  HQASSERT(hval != NULL, "hval is NULL") ;

  UNUSED_PARAM(Recognition_fltr_ctxt*, filter_ctxt);

  hash = intern_hash(uri) % NAMESPACE_RECOGNITION_HASHTABLESIZE;
  *hval = CAST_UINTPTRT_TO_UINT32(hash) ;

  for (curr=recognition_table->table[*hval]; curr!=NULL; curr=curr->next) {
    if ( uri == curr->uri )
      return curr;
  }
  return NULL;
}

Bool namespace_recognition_swstart(struct SWSTART *params)
{
  uint32 i ;

  UNUSED_PARAM(struct SWSTART *, params) ;
  HQASSERT(recognition_table == NULL, "recognition_table is not NULL") ;

  if ((recognition_table = mm_alloc(mm_pool_fixed,
                                    sizeof(struct Recognition_fltr_ctxt),
                                    MM_ALLOC_CLASS_XML_NAMESPACE)) == NULL)
    return error_handler(VMERROR) ;

  recognition_table->table = mm_alloc(mm_pool_fixed,
                                      sizeof(struct NSRecognitionEntry*) *
                                      NAMESPACE_RECOGNITION_HASHTABLESIZE,
                                      MM_ALLOC_CLASS_XML_NAMESPACE) ;

  if (recognition_table->table == NULL) {
    namespace_recognition_finish();
    return error_handler(VMERROR) ;
  }

  /* Initialize the table structure. */
  recognition_table->num_entries = 0 ;
  for (i=0; i < NAMESPACE_RECOGNITION_HASHTABLESIZE; i++) {
    recognition_table->table[i] = NULL ;
  }
  return TRUE ;
}

void namespace_recognition_finish(void)
{
  uint32 i ;
  struct NSRecognitionEntry *curr, *next ;

  if ( recognition_table != NULL ) {
    if ( recognition_table->table != NULL ) {

      for (i=0; i<NAMESPACE_RECOGNITION_HASHTABLESIZE; i++) {
        for (curr=recognition_table->table[i]; curr!=NULL; curr=next) {
          next = curr->next ;
          curr->uri = NULL ;
          mm_free(mm_pool_temp, curr, sizeof(struct NSRecognitionEntry)) ;
          recognition_table->num_entries-- ;
        }
      }
      HQASSERT(recognition_table->num_entries == 0, "num_entries is not zero.");

      mm_free(mm_pool_fixed,
              recognition_table->table,
              sizeof(struct NSRecognitionEntry*) *
              NAMESPACE_RECOGNITION_HASHTABLESIZE) ;
    }

    mm_free(mm_pool_fixed,
            recognition_table, sizeof(struct Recognition_fltr_ctxt)) ;
    recognition_table = NULL ;
  }
}

Bool namespace_recognition_add(
      const xmlGIStr *uri,
      XMLDocContextCreate f_create_context,
      XMLDocContextDestroy f_destroy_context)
{
  struct NSRecognitionEntry *curr ;
  uint32 hval ;

  HQASSERT(recognition_table != NULL, "recognition_table is NULL") ;

  curr = find_recognition_entry(recognition_table, uri, &hval) ;

  if (curr == NULL) {
    curr = mm_alloc(mm_pool_temp, sizeof(struct NSRecognitionEntry),
                    MM_ALLOC_CLASS_XML_NAMESPACE) ;

    if (curr == NULL)
      return error_handler(VMERROR) ;

    curr->uri = uri ;
    curr->next = recognition_table->table[hval] ;
    recognition_table->table[hval] = curr ;
    recognition_table->num_entries++ ;
  } else {
    /* Clobber any entry which may already be in this slot. */
  }

  curr->f_create_context =  f_create_context ;
  curr->f_destroy_context = f_destroy_context ;

  return TRUE ;
}

static struct NSRecognitionEntry *namespace_recognition_get(
      Recognition_fltr_ctxt *filter_ctxt,
      const xmlGIStr *uri)
{
  uint32 hval ;
  return find_recognition_entry(filter_ctxt, uri, &hval) ;
}

/* ============================================================================
 * Recognition filter.
 * ============================================================================
 */
static int32 namespace_cb(
      xmlGFilter *filter,
      const xmlGIStr *prefix,
      const xmlGIStr *uri)
{
  xmlGFilterChain *filter_chain ;
  XMLExecContext* xmlexec_ctxt ;
  Recognition_fltr_ctxt *filter_ctxt ;
  struct NSRecognitionEntry *entry ;

  UNUSED_PARAM( const xmlGIStr *, prefix ) ;
  HQASSERT(filter != NULL, "filter is NULL") ;

  filter_ctxt = xmlg_get_user_data(filter) ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;

  xmlexec_ctxt = SLL_GET_HEAD(&xml_context.sls_contexts, XMLExecContext, sll) ;
  HQASSERT((xmlexec_ctxt != NULL), "xmlexec_ctxt is NULL") ;

  /* Do we know about this XML namespace? */
  entry = namespace_recognition_get(filter_ctxt, uri) ;

  if (entry != NULL) {
    if (xmlexec_ctxt->doc_context == NULL) {

      /* Create the appropriate context for this doc type. */
      if (entry->f_create_context != NULL &&
          ! entry->f_create_context(xmlexec_ctxt->xmlexec_params,
                                    xmlexec_ctxt->xml_flptr,
                                    &(xmlexec_ctxt->doc_context),
                                    filter_chain)) {
        return FALSE ;
      }

      xmlexec_ctxt->f_destroy = entry->f_destroy_context ;

      /* Since we have recognised a namespace, remove the namespace
         callback from the recognition filter as we are not interested
         in any further namespace declarations. Also remove the
         temporary start element callback. Validation will now be done
         via the PDL processor from now on. */

      (void)xmlg_deregister_start_element_cb(filter, NULL, NULL) ;
      (void)xmlg_remove_namespace_cb(filter) ;

      /* TBD: look into removing the filter from this chain. Its not
         too bad for XPS because only the .rels part has this
         recognition filter in place. */

    } else {
      HQFAIL("namespace recognition has occured more than once on an xmlexec context") ;
    }
  }
  /* If we do not recognize the namespace, we keep on processing. */

  return TRUE ;
}

/* This start element callback is registered as the very first element
   callback. If this callback is reached, it means that no namespaces
   were recognised. If it turns out that we want to understand XML
   markup with no namespaces, this function will need extending to
   handle that case. */
static int32 first_element_cb (
       xmlGFilter *filter,
       const xmlGIStr *localname,
       const xmlGIStr *prefix,
       const xmlGIStr *uri,
       xmlGAttributes *attrs)
{
  Recognition_fltr_ctxt *filter_ctxt ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;
  UNUSED_PARAM( const xmlGAttributes* , attrs ) ;

  HQASSERT(filter != NULL, "filter is NULL") ;

  filter_ctxt = xmlg_get_user_data(filter) ;

  return detail_error_handler(SYNTAXERROR,
               "Unable to determine XML page description language. No recognised namespaces.") ;
}

Bool recognition_xml_filter_init(
      xmlGFilterChain *filter_chain,
      uint32 position,
      xmlGFilter **filter)
{
  xmlGFilter *new_filter ;

  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  HQASSERT(filter != NULL, "filter is NULL") ;

  *filter = NULL ;

  if (! xmlg_fc_new_filter(filter_chain, &new_filter, position, recognition_table, NULL))
    return error_handler(UNDEFINED) ;

  /* watch all elements */
  if (! xmlg_register_start_element_cb(new_filter,
                                       NULL, NULL, /* all elements */
                                       first_element_cb)) {
    xmlg_f_destroy(&new_filter) ;
    return error_handler(UNDEFINED) ;
  }

  xmlg_set_namespace_cb(new_filter, namespace_cb) ;

  *filter = new_filter ;

  return TRUE ;
}

void init_C_globals_recognition(void)
{
  recognition_table = NULL ;
}

/* ============================================================================
* Log stripped */
