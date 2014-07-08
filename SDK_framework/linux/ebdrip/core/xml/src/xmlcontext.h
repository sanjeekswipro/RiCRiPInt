/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!src:xmlcontext.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface for xmlexec context.
 *
 * Although the context is called "XMLExecContext", it does not
 * necessarily mean that the PS operator "xmlexec" is running. It may
 * be an "xmlopen" or friend. None the less, an XML execution context
 * will still exist, hence the name.
 */

#ifndef __XML_CONTEXT_H__
#define __XML_CONTEXT_H__ (1)

#include "lists.h"    /* dll_link_t */
#include "mm.h"       /* mm_pool_t */
#include "objnamer.h"
#include "xml.h"

struct FILELIST ; /* from COREfileio */
struct OBJECT ;   /* from COREobjects */
struct SWSTART ; /* from COREinterface */

/**
 * xml execution context meta-data.
 */
typedef struct XMLContext_Context {
  xml_contextid_t   next_id;      /* Unsigned non-zero integer */
  sll_list_t        sls_contexts; /* List of active contexts */
} XMLContext_Context ;

extern XMLContext_Context xml_context ;

#define XMLEXEC_CONTEXT_NAME "XML Exec Context" /* for VERIFY_OBJECT() */

/**
 * xml execution context.
 */
struct XMLExecContext {
  /** List links - must be first. */
  sll_link_t sll;

  /** Unique context id  - for if we have xmlopen/xmlexecid/etc. */
  xml_contextid_t id;

  /** Filelist of xml source. */
  struct FILELIST* xml_flptr;

  Bool error_occured ;

  /** Function to destroy the doc type context. */
  XMLDocContextDestroy f_destroy;

  /** This is where we hang doc specific context. */
  xmlDocumentContext *doc_context ;

  /** A function to alias URIs for namespace lookups. */
  xmlGMapUriCallback map_uri ;

  /** Private data for the URI mapping function. */
  void *map_uri_data ;

  /** xmlexec_ params dictionary. */
  struct OBJECT *xmlexec_params ;

  OBJECT_NAME_MEMBER
} ;

/* Life time of the XML subsystem allocation pool is per RIP
   instance. */

void* xml_subsystem_malloc(size_t size) ;

void* xml_subsystem_realloc(void *memPtr, size_t size) ;

void xml_subsystem_free(void *memPtr) ;

Bool psdevuri_swstart(struct SWSTART *params) ;

void psdevuri_finish(void) ;

/* ============================================================================
* Log stripped */
#endif /* !__XML_CONTEXT_H__ */
