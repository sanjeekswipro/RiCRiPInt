/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!src:commit.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of XPS commit filter.
 */

#include "core.h"
#include "control.h"
#include "display.h"            /* dl_safe_recursion */
#include "swerrors.h"
#include "objnamer.h"
#include "namedef_.h"

#include "xml.h"
#include "xpspriv.h"
#include "xpscommit.h"
#include "xpspartspriv.h"

#if defined(DEBUG_BUILD)
#include "monitor.h"
#include "xpsdebug.h"
#endif

#undef PROFILE_COMMIT_FILTER

#ifdef PROFILE_COMMIT_FILTER
#include <stdio.h>
#define XML_TRACE(_out_) printf(_out_)
#else
#define XML_TRACE(_out_)
#endif

#define XPSCOMMIT_BLOCK_NAME "XPSCOMMIT_BLOCK_NAME"

enum {
  XPS_COMMIT_STARTING,
  XPS_COMMIT_ENDING
} ;

static
/*@null@*/
struct ComplexProperty *find_complex_property(
      struct ComplexProperty **table,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      uint32 *hval)
{
  uintptr_t hash ;
#ifdef PROFILE_COMMIT_FILTER
  HqBool collision = FALSE ;
#endif

  struct ComplexProperty *curr ;
  HQASSERT(table != NULL, "table is NULL") ;
  HQASSERT(localname != NULL, "localname is NULL") ;
  HQASSERT(hval != NULL, "hval is NULL") ;

  /* Lets just use the hash value of the interned localname. */
  hash = intern_hash(localname) >> 2 ;
  hash %= COMPLEXPROPERTY_HASH_SIZE ;

  *hval = CAST_UINTPTRT_TO_UINT32(hash) ;

  for (curr = table[*hval]; curr != NULL && curr->is_being_used; curr = curr->next) {
#ifdef PROFILE_COMMIT_FILTER
    if (collision) {
      XML_TRACE("  complex_property_entry collision\n") ;
    }
    collision = TRUE ;
#endif
    if (curr->localname == localname && curr->uri == uri)
      return curr ;
  }

  return NULL;
}

/* This function MUST always de-allocate the resource reference. */
static Bool xps_execute_resource(
      xmlGFilterChain *filter_chain,
      xpsCommitBlock *commit,
      xpsResourceReference **reference_handle)
{
  Bool status = TRUE ;
  xpsXmlPartContext *xmlpart_ctxt ;
  xpsResourceReference *reference ;

  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt, "no xps xmlpart context") ;
  VERIFY_OBJECT(xmlpart_ctxt, XMLPART_CTXT_NAME) ;
  HQASSERT(reference_handle != NULL, "reference_handle is NULL") ;
  reference = *reference_handle ;
  HQASSERT(reference != NULL, "reference is NULL") ;

  /* In this instance, we do not need to deallocate the reference. */
  if (commit->executing)
    return TRUE ;

  /* Remove references from context while we execute them, or the
     start for the XML cache will go recursive. */
  *reference_handle = NULL ;
  commit->executing = TRUE ;

  /* If we have some resource definitions. */
  if (! SLL_LIST_IS_EMPTY(&xmlpart_ctxt->resourceblock_stack)) {
    xpsResource *resource ;
    xpsResourceBlock *resblock = SLL_GET_HEAD(&xmlpart_ctxt->resourceblock_stack,
                                              xpsResourceBlock, sll) ;
    /* If we are executing a resource. */
    if (xmlpart_ctxt->executing_stack != NULL) {
      const xpsResourceBlock *latest_resblock = xps_resource_get_latest_resblock(xmlpart_ctxt->executing_stack) ;
      const xpsResourceBlock *executing_resblock = xps_resource_get_resblock(xmlpart_ctxt->executing_stack) ;

      if (xps_resource_is_remote(xmlpart_ctxt->executing_stack)) {
        /* Remote resources may only reference resources in the remote
           resource itself. */

        (void)(xps_resblock_get_resource(executing_resblock, reference, &resource)) ;

        if (resource != NULL) {
          if (! xps_resource_is_remote(resource)) {
            status = detailf_error_handler(UNDEFINED,
                                           "Remote resource reference \"%.*s\" is referencing a resource in the source part.",
                                           reference->reflen, reference->refname) ;
          } else { /* OK, its also a remote resource, but is it the
                      same remote resource? */
            if (xps_resource_uid(xmlpart_ctxt->executing_stack) !=
                xps_resource_uid(resource)) {
              status = detailf_error_handler(UNDEFINED,
                                             "Remote resource reference \"%.*s\" is referencing a resource in a different remote resource.",
                                             reference->reflen, reference->refname) ;
            }
          }

          reference->resource = resource ;
        }

      } else { /* We are not executing a remote resource so we have
                  some more complicated scoping to perform. */

        /* Look at all resource dictionaries which were defined
           *after* the resource block in which the executing resource
           resides. These must be nested within the executing
           resource. */
        while (latest_resblock != resblock) {
          if (xps_resblock_get_resource(resblock, reference, &resource)) {
            reference->resource = resource ;
            break ;
          }

          resblock  = SLL_GET_NEXT(resblock, xpsResourceBlock, sll) ;
          HQASSERT(resblock != NULL, "resblock is NULL") ;
        }

        if (reference->resource == NULL) {
          Bool in_scope = FALSE ;
          /* Look at all resourc dictionaries which were defined
             *before* or *within* the resource block in which the
             executing resource resides. */
          do {
            if (resblock == xps_resource_get_resblock(xmlpart_ctxt->executing_stack))
              in_scope = TRUE ;

            if (in_scope && xps_resblock_get_resource(resblock, reference, &resource)) {
              reference->resource = resource ;
              break ;
            }
            resblock  = SLL_GET_NEXT(resblock, xpsResourceBlock, sll) ;
          } while (resblock != NULL) ;
        }
      }
    } else { /* We are not executing a resource, do simple lookup. */
      do {
        if (xps_resblock_get_resource(resblock, reference, &resource)) {
          reference->resource = resource ;
          break ;
        }
        resblock  = SLL_GET_NEXT(resblock, xpsResourceBlock, sll) ;
      } while (resblock != NULL) ;
    }
  }

  if (status && reference->resource == NULL)
    status = detailf_error_handler(UNDEFINED, "Unable to find resource named \"%.*s\".",
                                   reference->reflen, reference->refname) ;

#if defined(DEBUG_BUILD)
  if ((debug_xps & DEBUG_XPS_COMMIT) != 0) {
    monitorf((uint8*)"  START RESOURCE EXECUTE for %.*s\n",
             intern_length(reference->elementname),
             intern_value(reference->elementname)) ;
  }
  if (status)
    HQASSERT(reference->resource != NULL, "reference resource is NULL") ;
#endif

  if (status)
    status = xps_resource_execute(filter_chain, reference->resource,
                                  reference->elementname, commit->uri, NULL) ;

#if defined(DEBUG_BUILD)
  if ( (debug_xps & DEBUG_XPS_COMMIT) != 0)
    monitorf((uint8*)"  END RESOURCE EXECUTE for %.*s\n",
             intern_length(reference->elementname),
             intern_value(reference->elementname)) ;
#endif

  commit->executing = FALSE ;

  mm_free(mm_xml_pool, reference->refname, reference->reflen) ;
  mm_free(mm_xml_pool, reference, sizeof(xpsResourceReference)) ;

  return status ;
}

static Bool xps_commit_required_ok(
      /*@notnull@*/ /*@in@*/
      xmlGFilter *filter,
      /*@notnull@*/ /*@in@*/
      xpsCommitBlock *commit)
{
  struct ComplexProperty *curr, *next ;
  xmlGFilterChain *filter_chain ;

  HQASSERT(filter != NULL, "filter is NULL") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  HQASSERT(commit != NULL, "NULL commit") ;

  /* Look for any required properties which we have not seen. */
  for (curr = commit->stack; curr != NULL && curr->is_being_used; curr = next) {
    next = curr->stack_next ;

    if (curr->reference != NULL && ! curr->haveseen) {
      if (! xps_execute_resource(filter_chain, commit, &curr->reference))
        return FALSE ;
    }

    if ((! curr->optional) && (! curr->haveseen)) {
      return detailf_error_handler(UNDEFINED,
             "Required compound property \"%.*s\" is not defined.",
             intern_length(curr->localname),
             intern_value(curr->localname)) ;

    }
  }

  {
    xpsResourceReference *reference ;

    /* resource references which are not complex properties */
    while ((reference = commit->reference) != NULL) {
      commit->reference = reference->next ;

      if (! xps_execute_resource(filter_chain, commit, &reference))
        return FALSE ;
    }
  }

  return TRUE ;
}

static Bool xps_commit_execute(
    xmlGFilter *filter,
    const xmlGIStr *localname,
    const xmlGIStr *uri,
    int32 startelement)
{
  xmlGFilterChain *filter_chain ;
  xpsXmlPartContext *xmlpart_ctxt ;
  xpsCommitBlock *commit ;
  Bool success = TRUE ;
  uint32 hval ;
  int saved_dl_safe_recursion = dl_safe_recursion ;

  HQASSERT(filter != NULL, "filter is NULL") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt, "xmlpart_ctxt is NULL") ;
  VERIFY_OBJECT(xmlpart_ctxt, XMLPART_CTXT_NAME) ;
  commit = xmlpart_ctxt->commit ;
  HQASSERT(startelement == XPS_COMMIT_STARTING ||
           startelement == XPS_COMMIT_ENDING,
           "startelement is incorrect") ;

  if (startelement == XPS_COMMIT_STARTING) {
    /* Note that the commit block is actually the parent of this start
       element. */

    /* Is this a document start element? Do nothing. */
    if (commit != NULL) {
      HQASSERT(commit->filter_chain == filter_chain,
               "filter chains do not match") ;
      if (! commit->fired) {
        struct ComplexProperty *curr ;
        /* We need to check if its *not* a compound property. */
        curr = find_complex_property(commit->table, localname,
                                     uri, &hval) ;
        /* Its not a complex property, so fire the commit. */
        if (curr == NULL) {
          success = xps_commit_required_ok(filter, commit) ;

          if (success) {
            commit->fired = TRUE ;
            if (commit->f_commit != NULL) {
#if defined(DEBUG_BUILD)
              if ( (debug_xps & DEBUG_XPS_COMMIT) != 0)
                monitorf((uint8*)"FIRE COMMIT for %.*s\n",
                         intern_length(commit->localname),
                         intern_value(commit->localname)) ;
#endif
              ++ps_interpreter_level ;
              success = commit->f_commit(filter,
                                         commit->localname,
                                         commit->prefix,
                                         commit->uri,
                                         commit->attrs) ;
              --ps_interpreter_level ;
#if defined(DEBUG_BUILD)
              /* Useful to see that commit calls occur where expected */
              if ( (debug_xps & DEBUG_XPS_COMMIT) != 0 && ! success)
                monitorf((uint8*)"COMMIT FAILURE from %.*s\n",
                         intern_length(commit->localname),
                         intern_value(commit->localname)) ;
#endif
            }
          }
        } else { /* This is a compound property. */
          struct ComplexProperty *look, *next ;

          /* check to see if there are any resource references which
             we ought to fire before this compound property */
          for (look = commit->stack; look != NULL && look->is_being_used; look = next) {
            next = look->stack_next ;
            if (look->localname == curr->localname)
              break ;

            if (look->reference != NULL) {
              success = xps_execute_resource(filter_chain, commit, &look->reference) ;
            }
          }

          if (curr->haveseen || curr->reference != NULL) {
            if (curr->localname != XML_INTERN(Visual))
            success = detailf_error_handler(UNDEFINED,
                                            "Duplicate compound property \"%.*s\".",
                                            intern_length(curr->localname),
                                            intern_value(curr->localname)) ;
          } else { /* Not seen it before, so mark it as seen */
            curr->haveseen = TRUE;
          }
        }
      }
    }
  } else { /* its an end element tag */
    /* Note that the commit block is for this end element */
    HQASSERT(startelement == XPS_COMMIT_ENDING, "startelement is incorrect") ;
    HQASSERT(commit != NULL, "commit is NULL") ;
    HQASSERT(commit->localname == localname &&
             commit->uri == uri,
             "wrong commit element") ;
    HQASSERT(commit->filter_chain == filter_chain,
             "filter chains do not match") ;

    if (! commit->fired) {
      success = xps_commit_required_ok(filter, commit) ;

      if (success) {
        commit->fired = TRUE ;
        if (commit->f_commit != NULL) {
#if defined(DEBUG_BUILD)
          if ( (debug_xps & DEBUG_XPS_COMMIT) != 0)
            monitorf((uint8*)"FIRE COMMIT for %.*s\n",
                     intern_length(commit->localname),
                     intern_value(commit->localname)) ;
#endif
          ++ps_interpreter_level ;
          success = commit->f_commit(filter,
                                     commit->localname,
                                     commit->prefix,
                                     commit->uri,
                                     commit->attrs) ;
          --ps_interpreter_level ;
#if defined(DEBUG_BUILD)
          /* Useful to see that commit calls occur where expected */
          if ( (debug_xps & DEBUG_XPS_COMMIT) != 0 && ! success)
              monitorf((uint8*)"COMMIT FAILURE from %.*s\n",
                       intern_length(commit->localname),
                       intern_value(commit->localname)) ;
#endif
        }
      }
    }
  }

  dl_safe_recursion = saved_dl_safe_recursion ;

  return success;
}

/** \todo we ought to be testing the uri as well */
Bool xps_commit_resource_ref(
      xmlGFilter *filter,
      const xmlGIStr *elementname,
      xpsResourceReference *reference)
{
  xmlGFilterChain *filter_chain ;
  xpsXmlPartContext *xmlpart_ctxt ;
  xpsCommitBlock *commit ;
  Bool found = FALSE ;

  HQASSERT(filter != NULL, "filter is NULL") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt, "xmlpart_ctxt is NULL") ;
  VERIFY_OBJECT(xmlpart_ctxt, XMLPART_CTXT_NAME) ;

  commit = xmlpart_ctxt->commit ;

  if (commit != NULL) {
    uint32 i ;

    for (i=0; i < COMPLEXPROPERTY_HASH_SIZE; i++) {
      struct ComplexProperty *curr, *next ;
      for (curr = commit->table[i]; curr != NULL && curr->is_being_used; curr = next) {
        next = curr->next ;

        if (curr->localname == elementname) {
          if (curr->reference != NULL) {
            if (curr->localname != XML_INTERN(Visual))
            return detailf_error_handler(UNDEFINED,
                                         "Duplicate compound property \"%.*s\".",
                                         intern_length(curr->localname),
                                         intern_value(curr->localname)) ;
          } else {
            curr->reference = reference ;
            found = TRUE ;
          }
        }
      }
    }

    /* push onto unordered reference list */
    if (! found) {
      reference->next = commit->reference ;
      commit->reference = reference ;
    }
  }

  return TRUE ;
}

Bool xps_commit_register(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      xmlGAttributes *attrs,
      XPS_COMPLEXPROPERTYMATCH complexmatch[],
      xmlGStartElementCallback f_commit)
{
  xmlGFilterChain *filter_chain ;
  xpsXmlPartContext *xmlpart_ctxt ;
  struct ComplexProperty *curr ;
  uint32 hval ;
  xpsCommitBlock *commit ;

  UNUSED_PARAM(const xmlGIStr*, localname);
  UNUSED_PARAM(const xmlGIStr*, uri);

  HQASSERT(filter != NULL, "filter is NULL") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt, "xmlpart_ctxt is NULL") ;
  VERIFY_OBJECT(xmlpart_ctxt, XMLPART_CTXT_NAME) ;
  HQASSERT(complexmatch != NULL, "complex is NULL") ;
  commit = xmlpart_ctxt->commit ;
  HQASSERT(commit != NULL, "commit is NULL") ;
  HQASSERT(commit->localname == localname &&
           commit->uri == uri,
           "wrong commit element") ;
  HQASSERT(commit->filter_chain == filter_chain,
           "filter chains do not match") ;

  /* Empty complex property structures are OK. */
  if (complexmatch->localname == NULL)
    return TRUE ;

  commit->f_commit = f_commit ;

  while (complexmatch->localname != NULL) {
    if ( (curr = find_complex_property(commit->table,
                                       complexmatch->localname,
                                       commit->uri, &hval)) == NULL ) {

      curr = commit->table[hval] ;
      if (curr->is_being_used) {
        if ( (curr = mm_sac_alloc(mm_xml_pool, SAC_ALLOC_COMPLEX_PROPERTY_SIZE,
                                  MM_ALLOC_CLASS_XPS_COMPLEX)) == NULL)
          return error_handler(VMERROR) ;

        /* Copy pre-allocated table entry into new memory. */
        *curr = *(commit->table[hval]) ;

        curr->prev = commit->table[hval] ;
        commit->table[hval]->next = curr ;

        /* Patch other lists. */
        if (curr->prev != NULL)
          curr->prev->next = curr ;

        if (curr->next != NULL)
          curr->next->prev = curr ;

        if (curr->stack_prev != NULL)
          curr->stack_prev->stack_next = curr ;

        if (curr->stack_next != NULL)
          curr->stack_next->stack_prev = curr ;

        if (commit->stack_tail == commit->table[hval])
          commit->stack_tail = curr ;

        if (commit->stack == commit->table[hval])
          commit->stack = curr ;

        curr = commit->table[hval] ;
      } else {
        curr->next = NULL ;
      }

      curr->is_being_used = TRUE ;
      curr->localname = complexmatch->localname ;
#ifdef DEBUG_BUILD
      curr->localname_str = intern_value(complexmatch->localname) ;
#endif
      curr->uri = complexmatch->uri ;
      curr->prev = NULL ;

      /* Thats the hash chain taken care of, now deal with the
         stacks. */

      curr->stack_next = NULL ; /* insert at end of stack */

      if (commit->stack == NULL)
        commit->stack = curr ;

      if (commit->stack_tail != NULL)
        commit->stack_tail->stack_next = curr ;

      curr->stack_prev = commit->stack_tail ;
      commit->stack_tail = curr ;

      commit->num_properties++ ;
    } else {
      HQFAIL("Complex property duplicated in commit table") ;
      return FALSE ;
    }

    curr->haveseen = FALSE ;

    /* Look to see if this complex property has been set via an
       attribute value. If it has been, then set the complex property
       as having been seen. */
    if (attrs != NULL && complexmatch->attr_localname != NULL) {
      const xmlGIStr *attrprefix ;
      const uint8 *attrvalue ;
      uint32 attrvaluelen ;
      if (xmlg_attributes_lookup(attrs, complexmatch->attr_localname, complexmatch->attr_uri,
                                 &attrprefix, &attrvalue, &attrvaluelen)) {
        utf8_buffer scan ;
        scan.codeunits = (UTF8*)attrvalue ;
        scan.unitlength = attrvaluelen ;

        /* We only mark the complex property as having been see if its
           NOT a reference. If the attribute is a reference, we will
           pick up any duplicates when the reference gets executed and
           then the inline complex property gets seen (and hence is
           duplicated). */

        if (! xps_match_static_resource(&scan))
          curr->haveseen = TRUE ;
      }
    }

      /* Clobber any existing entries. */
    curr->optional = complexmatch->optional ;
    curr->reference = NULL ;
    complexmatch++ ;
  }

  return TRUE;
}

void xps_commit_unregister(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      const xmlGIStr *complex_localname,
      const xmlGIStr *complex_uri)
{
  xmlGFilterChain *filter_chain ;
  xpsXmlPartContext *xmlpart_ctxt ;
  struct ComplexProperty *curr, *deallocate = NULL ;
  uint32 hval ;
  xpsCommitBlock *commit ;

  UNUSED_PARAM(const xmlGIStr*, localname);
  UNUSED_PARAM(const xmlGIStr*, uri);

  HQASSERT(filter != NULL, "filter is NULL") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt, "xmlpart_ctxt is NULL") ;
  VERIFY_OBJECT(xmlpart_ctxt, XMLPART_CTXT_NAME) ;
  commit = xmlpart_ctxt->commit ;
  HQASSERT(commit != NULL, "commit is NULL") ;
  HQASSERT(commit->localname == localname &&
           commit->uri == uri,
           "wrong commit element") ;
  HQASSERT(commit->filter_chain == filter_chain,
           "filter chains do not match") ;

  if ( (curr = find_complex_property(commit->table,
                                     complex_localname,
                                     complex_uri, &hval)) != NULL ) {
    struct ComplexProperty *next = curr->next ;

    /* remove from hash */
    if (commit->table[hval] == curr) {
      /* no prev exists for first element */
      if (next == NULL) { /* Simply mark as not used, no
                             de-allocation on first node. */
        curr->is_being_used = FALSE ;
      } else {
        next->prev = NULL ;
        *curr = *next ;
        deallocate = next ;
      }
    } else {
      deallocate = curr ;

      if (curr->prev != NULL)
        curr->prev->next = curr->next ;
      if (next != NULL)
        next->prev = curr->prev ;
    }

    /* remove from stack */
    if (commit->stack == curr) {
      commit->stack = curr->next ;
      HQASSERT(curr->stack_prev == NULL, "previous stack is not NULL") ;
    }
    if (commit->stack_tail == curr) {
      commit->stack_tail = curr->stack_prev ;
      HQASSERT(curr->next == NULL, "next stack is not NULL") ;
    }

    if (curr->stack_prev != NULL)
      curr->stack_prev->stack_next = curr->stack_next ;

    if (curr->stack_next != NULL)
      curr->stack_next->stack_prev = curr->stack_prev ;

    if (curr->reference != NULL) {
      mm_free(mm_xml_pool, curr->reference->refname, curr->reference->reflen) ;
      mm_free(mm_xml_pool, curr->reference, sizeof(xpsResourceReference)) ;
    }

    if (deallocate != NULL) {
      mm_sac_free(mm_xml_pool, deallocate, SAC_ALLOC_COMPLEX_PROPERTY_SIZE) ;
    } else { /* keep it clean */
      curr->localname = NULL ;
#ifdef DEBUG_BUILD
      curr->localname_str = NULL ;
#endif
      curr->uri = NULL ;
      curr->haveseen = FALSE ;
      curr->optional = FALSE ;
      curr->reference = NULL ;
      curr->next = NULL ;
      curr->prev = NULL ;
      curr->stack_next = NULL ;
      curr->stack_prev = NULL ;
    }
  }

  return ;
}

static Bool commit_start_element_cb (
       xmlGFilter *filter,
       const xmlGIStr *localname,
       const xmlGIStr *prefix,
       const xmlGIStr *uri,
       xmlGAttributes *attrs)
{
  xmlGFilterChain *filter_chain ;
  xpsXmlPartContext *xmlpart_ctxt ;
  struct xpsCommitBlock *commit ;
  uint32 i ;
  uint8 *mem_block ;

  HQASSERT(filter != NULL, "filter is NULL") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt, "xmlpart_ctxt is NULL") ;
  VERIFY_OBJECT(xmlpart_ctxt, XMLPART_CTXT_NAME) ;

  if (! xps_commit_execute(filter, localname, uri, XPS_COMMIT_STARTING))
    return FALSE ;

  mem_block = mm_sac_alloc(mm_xml_pool, SAC_ALLOC_COMMIT_BLOCK_SIZE,
                           MM_ALLOC_CLASS_XPS_COMPLEX) ;
  if (mem_block == NULL)
    return error_handler(VMERROR) ;

  commit = (struct xpsCommitBlock*) mem_block ;
  mem_block += sizeof(struct xpsCommitBlock) ;
  commit->table = (struct ComplexProperty**)mem_block ;
  mem_block += (sizeof(struct ComplexProperty*) * COMPLEXPROPERTY_HASH_SIZE) ;

  for (i=0; i < COMPLEXPROPERTY_HASH_SIZE; i++) {
    commit->table[i] = (struct ComplexProperty*)mem_block ;
    commit->table[i]->localname = NULL ;
#ifdef DEBUG_BUILD
    commit->table[i]->localname_str = NULL ;
#endif
    commit->table[i]->uri = NULL ;
    commit->table[i]->haveseen = FALSE ;
    commit->table[i]->optional = FALSE ;
    commit->table[i]->reference = NULL ;
    commit->table[i]->next = NULL ;
    commit->table[i]->prev = NULL ;
    commit->table[i]->stack_next = NULL ;
    commit->table[i]->stack_prev = NULL ;
    commit->table[i]->is_being_used = FALSE ;
    mem_block += sizeof(struct ComplexProperty) ;
  }

  commit->num_properties = 0 ;
  commit->localname = localname ;
  commit->prefix = prefix ;
  commit->uri = uri ;
  commit->attrs = attrs ;
  commit->filter_chain = filter_chain ;
  commit->stack = NULL ;
  commit->stack_tail = NULL ;
  commit->reference = NULL ;
  if (attrs != NULL)
    xmlg_attributes_reserve(attrs) ;

  /* unknown at this time */
  commit->fired = FALSE ;
  commit->executing = FALSE ;
  commit->f_commit = NULL ;
  commit->next = xmlpart_ctxt->commit ;
  xmlpart_ctxt->commit = commit ;

  return TRUE ;
}

static Bool commit_end_element_cb (
       xmlGFilter *filter,
       const xmlGIStr *localname,
       const xmlGIStr *prefix,
       const xmlGIStr *uri,
       Bool success)
{
  xmlGFilterChain *filter_chain ;
  xpsXmlPartContext *xmlpart_ctxt ;
  struct xpsCommitBlock *commit ;
  xpsResourceReference *reference ;
  uint32 i ;

  HQASSERT(filter != NULL, "filter is NULL") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt, "xmlpart_ctxt is NULL") ;
  VERIFY_OBJECT(xmlpart_ctxt, XMLPART_CTXT_NAME) ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;

  /* Only fire commit callbacks if we are not in an error
     condition. Be careful when the commit goes recursive and an error
     happens as the end element callbacks get executed */
  if (success)
    success = xps_commit_execute(filter, localname, uri, XPS_COMMIT_ENDING) ;

  commit = xmlpart_ctxt->commit ;

  if (commit == NULL)
    return success ;

  HQASSERT(commit->filter_chain == filter_chain,
           "filter chains do not match") ;

#if defined(DEBUG_BUILD)
  if (! success)
    commit->fired = TRUE ;

  /* By the time you get to an end element, its commit callback should
     ALWAYS have been fired - unless we are in an error unwind. */
  HQASSERT(commit->fired, "commit has not fired") ;
#endif

  xmlpart_ctxt->commit = commit->next ;

  /* deallocate commit block */
  for (i=0; i < COMPLEXPROPERTY_HASH_SIZE; i++) {
    struct ComplexProperty *curr, *next ;
    for (curr = commit->table[i]; curr != NULL && curr->is_being_used; curr = next) {
      next = curr->next;

      if (curr->reference != NULL) {
        mm_free(mm_xml_pool, curr->reference->refname, curr->reference->reflen) ;
        mm_free(mm_xml_pool, curr->reference, sizeof(xpsResourceReference)) ;
      }

      if (curr != commit->table[i])
        mm_sac_free(mm_xml_pool, curr, SAC_ALLOC_COMPLEX_PROPERTY_SIZE) ;
    }
  }

  while ( (reference = commit->reference) != NULL ) {
    commit->reference = reference->next ;
    mm_free(mm_xml_pool, reference->refname, reference->reflen) ;
    mm_free(mm_xml_pool, reference, sizeof(xpsResourceReference)) ;
  }
  HQASSERT(commit->reference == NULL,
           "references stack is not NULL") ;

  if (commit->attrs != NULL)
    xmlg_attributes_destroy(&commit->attrs) ;

  mm_sac_free(mm_xml_pool, commit, SAC_ALLOC_COMMIT_BLOCK_SIZE) ;

  return success ;
}


/** \todo seems I need to pass xps_ctxt to this function so that the
    xps_ctxt gets put on the filter */
Bool xps_commit_filter_init(
      xmlGFilterChain *filter_chain,
      uint32 position,
      xmlGFilter **filter,
      xmlDocumentContext *xps_ctxt)
{
  xmlGFilter *new_filter ;

  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  HQASSERT(filter != NULL, "filter is NULL") ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;

  *filter = NULL ;

  if (! xmlg_fc_new_filter(filter_chain, &new_filter, position, xps_ctxt,
                           NULL /* no dispose callback */))
    return error_handler(UNDEFINED) ;

  xmlg_set_validity_error_cb(new_filter, xps_validity_error_cb) ;
  xmlg_set_user_error_cb(new_filter, xps_user_error_cb) ;

  /* watch all elements */
  if (! xmlg_register_start_element_cb(new_filter, NULL, NULL, /* all elements */
                                       commit_start_element_cb) ||
      ! xmlg_register_end_element_cb(new_filter, NULL, NULL, /* all elements */
                                     commit_end_element_cb)) {
    xmlg_f_destroy(&new_filter) ;
    return error_handler(UNDEFINED) ;
  }

  *filter = new_filter ;

  return TRUE ;
}

/* ============================================================================
* Log stripped */
