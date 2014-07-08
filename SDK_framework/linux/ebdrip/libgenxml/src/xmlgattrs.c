/* ============================================================================
 * $HopeName: HQNgenericxml!src:xmlgattrs.c(EBDSDK_P.1) $
 * $Id: src:xmlgattrs.c,v 1.39.11.1.1.1 2013/12/19 11:24:21 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/* \file
 * \brief Implements the XML attribute type.
 */

#define XML_SAC_ALLOCATION

#ifdef PROFILE_XML_ATTRIBUTES
#include <stdio.h>
#define XML_TRACE(_out_) printf(_out_)
#else
#define XML_TRACE(_out_)
#endif

#include "hqunicode.h"
#include "xmlgassert.h"
#include "xmlgattrspriv.h"
#include "xmlgpriv.h"

static void * xmlg_attr_malloc(
      xmlGAttributes *attributes,
      size_t size)
{
  XMLGASSERT(attributes != NULL, "attributes is NULL");

  XML_TRACE("alloc: attr_malloc\n") ;

  return attributes->memory_handler.f_malloc(size);
}

static void xmlg_attr_free(
      xmlGAttributes *attributes,
      void *memPtr)
{
  XMLGASSERT(attributes != NULL, "attributes is NULL");

  XML_TRACE("alloc: attr_free\n") ;

  attributes->memory_handler.f_free(memPtr);
}

#define FAST_XMLG_FIND_ATTRIBUTE(_res, _attributes, _attrlocalname, _attruri, _hval) \
  { \
    Attribute *_curr ; \
    _res = NULL ; \
    _hval = CAST_UINTPTRT_TO_UINT32( ((uintptr_t)_attrlocalname >> 2) % ATTRIBUTE_HASH_SIZE) ; \
    for (_curr = _attributes->table[_hval]; _curr != NULL; _curr = _curr->next) { \
      if ( _curr->is_being_used && \
           xmlg_istring_equal(_attributes->xml_ctxt, _curr->attrlocalname, _attrlocalname) && \
           xmlg_istring_equal(_attributes->xml_ctxt, _curr->attruri, _attruri) ) { \
        _res = _curr ; \
      } \
    } \
  }

HqBool xmlg_attributes_create(
      xmlGFilterChain *filter_chain,
      xmlGAttributes **attributes)
{
  xmlGContext *xml_ctxt ;
  uint32 i ;
  uint8 *mem_block ;

  XMLGASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  XMLGASSERT(attributes != NULL, "attributes pointer pointer is NULL") ;
  xml_ctxt = filter_chain->xml_ctxt ;
  XMLGASSERT(xml_ctxt != NULL, "xml_ctxt is NULL") ;

#ifdef XML_SAC_ALLOCATION
  {
    mps_res_t res ;
    mps_addr_t m ;

    XMLGASSERT(xml_ctxt->sac != NULL, "xml_ctxt sac is NULL") ;
    MPS_SAC_ALLOC_FAST(res, m, xml_ctxt->sac, SAC_ALLOC_ATTRIBUTES_SIZE, TRUE) ;
    mem_block = (uint8*)m ;

    if (res != MPS_RES_OK)
      return FALSE ;
  }
#else
  mem_block = xmlg_fc_malloc(filter_chain, SAC_ALLOC_ATTRIBUTES_SIZE) ;
#endif

  if (mem_block == NULL)
    return FALSE ;

  *attributes = (xmlGAttributes *)mem_block ;

  /* Initialize the attribute structure. Take a copy of the filter
     chain memory handler in the event that the API programmer allows
     the memory handler to out live the filter chain. Useful for when
     caching XML from an XML file where the filter chain which is used
     to parse that file is subsequently destroyed, but the plugged
     memory handler lasts for a longer period, at which point the XML
     attributes can continue to survive without problems. */
  (*attributes)->memory_handler = filter_chain->memory_handler ;
  (*attributes)->ref_count = 1 ;
  (*attributes)->num_entries = 0 ;
  (*attributes)->xml_ctxt = filter_chain->xml_ctxt ;
  (*attributes)->next_scan = NULL ;
  (*attributes)->stack = NULL ;

  mem_block += sizeof(xmlGAttributes) ;

  for (i=0; i<ATTRIBUTE_HASH_SIZE; i++) {
    Attribute *entry = (Attribute *)mem_block ;
    mem_block += sizeof(Attribute) ;

    entry->is_being_used = FALSE ;
    entry->attrlocalname = NULL ;
    entry->attruri = NULL ;
    entry->attrprefix = NULL ;
    entry->attrvalue = NULL ;
    entry->attrvaluelen = 0 ;
    entry->hash = 0 ;
    entry->match = FALSE ;
    entry->next = NULL ;
    entry->stack_next = NULL ;
    entry->stack_prev = NULL ;

    (*attributes)->table[i] = entry ;
  }
  return TRUE ;
}

void xmlg_attributes_destroy(
      xmlGAttributes **attributes)
{
  Attribute *curr, *next ;
  xmlGContext *xml_ctxt ;

  XMLGASSERT(attributes != NULL, "attributes is NULL") ;
  XMLGASSERT(*attributes != NULL, "attributes pointer is NULL") ;
  XMLGASSERT((*attributes)->ref_count != 0, "attributes ref count equal to 0") ;

  xml_ctxt = (*attributes)->xml_ctxt ;
  XMLGASSERT(xml_ctxt != NULL, "xml_ctxt is NULL") ;

  (*attributes)->ref_count-- ;
  if ((*attributes)->ref_count > 0) {
     /* Destroy this reference. */
    (*attributes) = NULL ;
    return ;
  }

  for (curr=(*attributes)->stack; curr!=NULL; curr=next) {
    next = curr->stack_next ;
    HQASSERT(curr->is_being_used == TRUE, "is_being_used incorrectly set") ;

    xmlg_istring_destroy(xml_ctxt, &curr->attrlocalname) ;
    xmlg_istring_destroy(xml_ctxt, &curr->attrprefix) ;
    xmlg_istring_destroy(xml_ctxt, &curr->attruri) ;

    if (curr->attrvaluelen + 1 > PRE_ALLOCATED_ATTR_SIZE)
      xmlg_attr_free((*attributes), (void *)curr->attrvalue) ;
    curr->attrvaluelen = 0 ;

    if (curr != (*attributes)->table[curr->hash]) { /* Only deallocate non-first entries. */
      xmlg_attr_free((*attributes), curr) ;
    } else {
      /* This gets done at creation time so no need to keep potential SAC blocks tidy. */
#if 0
      curr->is_being_used = FALSE ;
      curr->attrlocalname = NULL ;
      curr->attruri = NULL ;
      curr->attrprefix = NULL ;
      curr->match = FALSE ;
      curr->next = NULL ;
#endif
    }
#ifdef DEBUG_BUILD
    /* Why do this, we are about to de-allocate, but I want the assert to remain. */
    (*attributes)->num_entries-- ;
#endif
  }

#ifdef DEBUG_BUILD
  XMLGASSERT((*attributes)->num_entries == 0, "num_entries is not zero.") ;
#endif

#ifdef XML_SAC_ALLOCATION
  XMLGASSERT(xml_ctxt->sac != NULL, "xml_ctxt sac is NULL") ;
  MPS_SAC_FREE_FAST(xml_ctxt->sac, *attributes, SAC_ALLOC_ATTRIBUTES_SIZE) ;
#else
  xmlg_attr_free((*attributes), *attributes) ;
#endif

  (*attributes) = NULL ;

  return ;
}

void xmlg_attributes_reserve(
      xmlGAttributes *attributes)
{
  XMLGASSERT(attributes != NULL, "attributes is NULL") ;

  attributes->ref_count++ ;
}

HqBool xmlg_attributes_lookup(
      xmlGAttributes *attributes,
      const xmlGIStr *attrlocalname,
      const xmlGIStr *attruri,
      const xmlGIStr **attrprefix,
      const uint8 **attrvalue,
      uint32 *attrvaluelen)
{
  Attribute *curr;
  uint32 hval;

  XMLGASSERT(attributes != NULL, "attributes is NULL") ;
  XMLGASSERT(attrlocalname != NULL, "attrlocalname is NULL") ;
  XMLGASSERT(attrprefix != NULL, "attrprefix is NULL") ;
  XMLGASSERT(attrvalue != NULL, "value is NULL") ;
  XMLGASSERT(attrvaluelen != NULL, "value_len is NULL") ;

  /* We do not handle prefix's yet. */
  *attrprefix = NULL ;

  *attrvalue = NULL ;
  *attrvaluelen = 0 ;

  FAST_XMLG_FIND_ATTRIBUTE(curr, attributes, attrlocalname, attruri, hval) ;

  if (curr == NULL)
    return FALSE ;

  HQASSERT(curr->hash == hval, "something wrong with hash value") ;

  *attrvalue = curr->attrvalue ;
  *attrvaluelen = curr->attrvaluelen ;

  return TRUE ;
}

HqBool xmlg_attributes_scan_full(
      xmlGFilter *filter,
      xmlGAttributes *attributes,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      xmlGAttributesScanCallback f)
{
  Attribute *curr ;

  XMLGASSERT(filter != NULL, "filter is NULL") ;
  XMLGASSERT(localname != NULL, "localname is NULL") ;
  XMLGASSERT(attributes != NULL, "attributes is NULL") ;
  XMLGASSERT(f != NULL, "f is NULL") ;

  curr=attributes->stack ;

  HQASSERT(attributes->next_scan == NULL, "next_scan is not NULL") ;

  while (curr != NULL) {
    HQASSERT(curr->is_being_used == TRUE, "is_being_used incorrectly set") ;

    attributes->next_scan = curr->stack_next ;

    if ( !(*f)(filter, attributes, localname, uri,
               curr->attrlocalname, /* prefix */ NULL, curr->attruri,
               curr->attrvalue, curr->attrvaluelen) ) {
      attributes->next_scan = NULL ;
      return FALSE ;
    }

    curr = attributes->next_scan ;
  }

  attributes->next_scan = NULL ;
  return TRUE ;
}

HqBool xmlg_attributes_insert(
      xmlGAttributes *attributes,
      const xmlGIStr *attrlocalname,
      const xmlGIStr *attrprefix,
      const xmlGIStr *attruri,
      const uint8 *attrvalue,
      uint32 attrvaluelen)
{
  Attribute *curr ;
  uint32 hval ;
  uint8 *new_attrvalue ;
  xmlGContext *xml_ctxt ;

  XMLGASSERT(attributes != NULL, "attributes is NULL") ;
  XMLGASSERT(attributes->ref_count > 0,
             "attributes reference count is not greater than 0") ;
  XMLGASSERT(attrlocalname != NULL,
             "attribute localname is NULL") ;

  xml_ctxt = attributes->xml_ctxt ;
  XMLGASSERT(xml_ctxt != NULL, "xml_ctxt is NULL") ;

  new_attrvalue = NULL ;

  FAST_XMLG_FIND_ATTRIBUTE(curr, attributes, attrlocalname, attruri, hval) ;

  if (curr == NULL) {
    curr = attributes->table[hval] ;

    /* Do we need to allocate a new entry in the chain? */
    if (curr->is_being_used) {
      /* We do need to allocate. */
      curr = xmlg_attr_malloc(attributes, sizeof(Attribute));
      if (curr == NULL)
        return FALSE;

      curr->attrlocalname = attrlocalname;
      if (!xmlg_istring_reserve(xml_ctxt, attrlocalname)) {
        xmlg_attr_free(attributes, curr);
        return FALSE ;
      }

      curr->attrprefix = attrprefix;
      if (!xmlg_istring_reserve(xml_ctxt, attrprefix)) {
        xmlg_istring_destroy(xml_ctxt, &curr->attrlocalname) ;
        xmlg_attr_free(attributes, curr);
        return FALSE ;
      }

      curr->attruri = attruri;
      if (!xmlg_istring_reserve(xml_ctxt, attruri)) {
        xmlg_istring_destroy(xml_ctxt, &curr->attrlocalname) ;
        xmlg_istring_destroy(xml_ctxt, &curr->attrprefix) ;
        xmlg_attr_free(attributes, curr);
        return FALSE ;
      }

      /* Insert one after the first pre-allocated entry. */
      curr->next = attributes->table[hval]->next;
      attributes->table[hval]->next = curr;

    } else { /* The first entry is not being used, so we don't need to
                allocate memory. */
      curr->attrlocalname = attrlocalname;
      if (!xmlg_istring_reserve(xml_ctxt, attrlocalname)) {
        return FALSE ;
      }

      curr->attrprefix = attrprefix;
      if (!xmlg_istring_reserve(xml_ctxt, attrprefix)) {
        xmlg_istring_destroy(xml_ctxt, &curr->attrlocalname) ;
        return FALSE ;
      }

      curr->attruri = attruri;
      if (!xmlg_istring_reserve(xml_ctxt, attruri)) {
        xmlg_istring_destroy(xml_ctxt, &curr->attrlocalname) ;
        xmlg_istring_destroy(xml_ctxt, &curr->attrprefix) ;
        return FALSE ;
      }
    }

    if (attrvalue != NULL) {
      XMLGASSERT(attrvaluelen > 0, "attrvaluelen not greater than zero");
      if (PRE_ALLOCATED_ATTR_SIZE < attrvaluelen + 1) {
        new_attrvalue = xmlg_attr_malloc(attributes, attrvaluelen + 1);
        if (new_attrvalue == NULL) {
          if (curr != attributes->table[hval]) {
            xmlg_attr_free(attributes, curr) ;
          }
          return FALSE;
        }
      } else {
        /* Cast away const so we can fill the value. */
        new_attrvalue = (uint8*)&curr->static_attrvalue[0] ;
      }

      (void)memcpy(new_attrvalue, attrvalue, attrvaluelen);
      new_attrvalue[attrvaluelen] = '\0';

    } else {
      /* In the event that someone passes > 0 length with NULL value.
       */
      attrvaluelen = 0;
    }

    curr->is_being_used = TRUE ;
    curr->hash = hval ;
    curr->match = FALSE ;
    attributes->num_entries++;

    /* Add to top of stack. */
    curr->stack_next = attributes->stack ;
    if (curr->stack_next != NULL)
      curr->stack_next->stack_prev = curr ;
    attributes->stack = curr ;
    curr->stack_prev = NULL ;
  } else {
    /* Clobber attribute value which may already be in this slot.
     */
    if (curr->attrvaluelen + 1 > PRE_ALLOCATED_ATTR_SIZE)
      xmlg_attr_free(attributes, (void *)curr->attrvalue) ;
  }

  curr->attrvalue = new_attrvalue ;
  curr->attrvaluelen = attrvaluelen ;

  return TRUE ;
}

/* Not sure if this ought to be in this file any longer. */
HqBool xmlg_attributes_invoke_match_error(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      const xmlGIStr *attr_localname)
{
  xmlGContext *xml_ctxt ;

  XMLGASSERT(filter != NULL, "filter is NULL") ;
  XMLGASSERT(localname != NULL, "localname is NULL") ;
  XMLGASSERT(attr_localname != NULL, "attr_localname is NULL") ;
  xml_ctxt = filter->xml_ctxt;
  XMLGASSERT(xml_ctxt != NULL, "xml_ctxt is NULL") ;

  if (filter->validity_error_cb != NULL) {
    uint32 localname_len, len ;
    const uint8 *localname_value, *name ;

    localname_value = xmlg_istring_value(xml_ctxt, localname) ;
    localname_len = xmlg_istring_length(xml_ctxt, localname) ;
    len = xmlg_istring_length(xml_ctxt, attr_localname) ;
    name = xmlg_istring_value(xml_ctxt, attr_localname) ;

    return filter->validity_error_cb(filter,
                     localname, prefix, uri,
                     NULL, XMLG_ERR_ATTRIBUTE_SCANERROR,
                     "<%.*s> elements \"%.*s\" attribute is invalid.",
                     (int32)localname_len, localname_value, (int32)len, name) ;
  }

  return FALSE ;
}

HqBool xmlg_attributes_match(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      xmlGAttributes *attributes,
      XML_ATTRIBUTE_MATCH match[],
      HqBool check_unmatched)
{
  xmlGContext *xml_ctxt ;
  uint32 hval, localname_len;
  const uint8 *localname_value ;
  Attribute *curr ;

  XMLGASSERT(filter != NULL, "filter is NULL") ;
  xml_ctxt = filter->xml_ctxt;
  XMLGASSERT(xml_ctxt != NULL, "xml_ctxt is NULL") ;
  XMLGASSERT(localname != NULL, "localname is NULL") ;
  XMLGASSERT(match != NULL, "nothing to match attributes");

  /* Stop unused compiler warnings. */
  if (uri == NULL) { }
  if (check_unmatched && attributes != NULL) {
    /* Mark that none of the attributes have been matched. */
    for (curr=attributes->stack; curr!=NULL; curr=curr->stack_next) {
      HQASSERT(curr->is_being_used == TRUE, "is_being_used incorrectly set") ;
      curr->match = FALSE ;
    }
  }

  while ( match->name != NULL ) {
    XMLGASSERT(match->convert != NULL, "No type converter in attribute match") ;

    if ( attributes != NULL) {
      FAST_XMLG_FIND_ATTRIBUTE(curr, attributes, match->name, match->uri, hval) ;
    } else {
      curr = NULL ;
    }

    if ( curr != NULL ) {
      utf8_buffer value;

      if (check_unmatched)
        curr->match = TRUE ;
      value.codeunits = (uint8 *)curr->attrvalue;
      value.unitlength = curr->attrvaluelen;

      /** \todo the cast in the convert callback should not be required */
      if ( !(*match->convert)(filter, (xmlGIStr *)match->name, &value, match->data) ) {

        if (xmlg_attributes_invoke_match_error(filter, localname, /* We do not track prefix */ NULL,
                                               match->uri, match->name))
          continue ;

        xmlg_f_error_abort(filter, TRUE /* fire error cb */) ;
        return FALSE ;
      }

      /* It is an error if the entire attribute was not consumed by the type
         converter. */
      if ( utf8_iterator_more(&value) ) {
        if (filter->validity_error_cb != NULL) {
          uint32 len = xmlg_istring_length(xml_ctxt, match->name) ;
          const uint8* name = xmlg_istring_value(xml_ctxt, match->name) ;
          localname_value = xmlg_istring_value(xml_ctxt, localname) ;
          localname_len = xmlg_istring_length(xml_ctxt, localname) ;
          if (filter->validity_error_cb(filter,
                    match->name, /* We do not track prefix */ NULL, match->uri,
                    NULL, XMLG_ERR_UNKNOWN,
                    "<%.*s> element \"%.*s\" attribute not fully consumed.",
                    (int32)localname_len, localname_value, (int32)len, name)) {
            continue ;
          }
        }

        /* only fire error callback if validity callback fails. */
        xmlg_f_error_abort(filter, TRUE /*fire error cb */) ;
        return FALSE ;
      }

      /* Note that optional argument was present */
      if ( match->optional != NULL )
        *(match->optional) = TRUE ;

    } else if ( match->optional != NULL ) {
      /* Note that optional argument was not present */
      *(match->optional) = FALSE ;

    } else {
      if (filter->validity_error_cb != NULL) {
        uint32 len = xmlg_istring_length(xml_ctxt, match->name) ;
        const uint8* name = xmlg_istring_value(xml_ctxt, match->name) ;
        localname_value = xmlg_istring_value(xml_ctxt, localname) ;
        localname_len = xmlg_istring_length(xml_ctxt, localname) ;
        if (filter->validity_error_cb(filter,
                  match->name, /* We do not track prefix */ NULL,
                  match->uri, NULL, XMLG_ERR_UNKNOWN,
                  "<%.*s> element requires \"%.*s\" attribute to be set.",
                  (int32)localname_len, localname_value, (int32)len, name)) {
          continue ;
        }
      }

      /* only fire error callback if validity callback fails. */
      xmlg_f_error_abort(filter, TRUE /*fire error cb */) ;
      return FALSE ;
    }
    ++match ;
  }

  /* If all of the attributes have not been matched */
  if (check_unmatched  && attributes != NULL) {
    for (curr=attributes->stack; curr!=NULL; curr=curr->stack_next) {
      HQASSERT(curr->is_being_used == TRUE, "is_being_used incorrectly set") ;

      if (! curr->match) {
        if (filter->validity_error_cb != NULL) {
          uint32 len = xmlg_istring_length(xml_ctxt, curr->attrlocalname) ;
          const uint8 *name = xmlg_istring_value(xml_ctxt, curr->attrlocalname) ;
          localname_value = xmlg_istring_value(xml_ctxt, localname) ;
          localname_len = xmlg_istring_length(xml_ctxt, localname) ;
          if (filter->validity_error_cb(filter,
                    curr->attrlocalname, curr->attrprefix, curr->attruri,
                    attributes, XMLG_ERR_UNMATCHED_ATTRIBUTE,
                    "<%.*s> element contains unrecognised \"%.*s\" attribute.",
                    (int32)localname_len, localname_value, (int32)len, name)) {
            continue ;
          }
        }

        /* only fire error callback if validity callback fails. */
        xmlg_f_error_abort(filter, TRUE /*fire error cb */) ;
        return FALSE ;
      } /* end if ! match */

    } /* end for each attribute */
  }

  return TRUE ;
}

void  xmlg_attributes_remove(
      xmlGAttributes *attributes,
      const xmlGIStr *attrlocalname,
      const xmlGIStr *attruri)
{
  Attribute *curr, *prev ;
  uint32 hval ;
  xmlGContext *xml_ctxt ;

  if (attributes == NULL)
    return ;

  XMLGASSERT(attributes->ref_count > 0,
             "attributes reference count is not greater than 0") ;
  XMLGASSERT(attrlocalname != NULL,
             "attribute localname is NULL") ;

  xml_ctxt = attributes->xml_ctxt ;
  XMLGASSERT(xml_ctxt != NULL, "xml_ctxt is NULL") ;

  FAST_XMLG_FIND_ATTRIBUTE(curr, attributes, attrlocalname, attruri, hval) ;

  if (curr != NULL) {
    HQASSERT(curr->hash == hval, "something wrong with hash value") ;

    /* deallocate memory */
    xmlg_istring_destroy(xml_ctxt, &curr->attrlocalname) ;
    xmlg_istring_destroy(xml_ctxt, &curr->attrprefix) ;
    xmlg_istring_destroy(xml_ctxt, &curr->attruri) ;
    if (curr->attrvaluelen + 1 > PRE_ALLOCATED_ATTR_SIZE)
      xmlg_attr_free(attributes, (void *)curr->attrvalue) ;
    curr->attrvaluelen = 0 ;

    /* delink from attribute stack */
    if (attributes->stack == curr)
      attributes->stack = curr->stack_next ;
    if (curr->stack_prev != NULL)
      curr->stack_prev->stack_next = curr->stack_next ;
    if (curr->stack_next != NULL)
      curr->stack_next->stack_prev = curr->stack_prev ;

    /* remove from list */
    if (curr == attributes->table[hval]) {
      /* We need to copy the next one into the first if a next
         exists. */
      if (curr->next == NULL) { /* Simply mark as not used, no
                                   de-allocation on first node required. */
        curr->is_being_used = FALSE ;
      } else {
        Attribute *copy_attr = curr->next ;
        uint32 len = copy_attr->attrvaluelen ;

        attributes->table[hval]->is_being_used = TRUE ;
        attributes->table[hval]->attrlocalname = copy_attr->attrlocalname ;
        attributes->table[hval]->attruri = copy_attr->attruri ;
        attributes->table[hval]->attrprefix = copy_attr->attrprefix ;
        attributes->table[hval]->stack_next = copy_attr->stack_next ;
        attributes->table[hval]->stack_prev = copy_attr->stack_prev ;

        /* If a scan is in progress, patch the next pointer if need
           be. */
        if (attributes->next_scan != NULL &&
            attributes->next_scan == copy_attr) {
          attributes->next_scan = attributes->table[hval] ;
        }

        /* Patch stack pointers again since the address of the next
           object has changed */
        if (attributes->stack == copy_attr) {
          attributes->stack = attributes->table[hval] ;
        }
        if (attributes->table[hval]->stack_next != NULL) {
          attributes->table[hval]->stack_next->stack_prev = attributes->table[hval] ;
        }
        if (attributes->table[hval]->stack_prev != NULL) {
          attributes->table[hval]->stack_prev->stack_next = attributes->table[hval] ;
        }

        if (len + 1 > PRE_ALLOCATED_ATTR_SIZE) {
          /* Copy pointer to allocated attribute value. */
          attributes->table[hval]->attrvalue = copy_attr->attrvalue ;
        } else {
          /* We merely need to copy the attribute value content. */
          (void)memcpy((uint8*)(attributes->table[hval])->static_attrvalue, copy_attr->static_attrvalue, len);
          (attributes->table[hval])->static_attrvalue[len] = '\0' ;
          (attributes->table[hval])->attrvalue = attributes->table[hval]->static_attrvalue ;
        }
        attributes->table[hval]->attrvaluelen = len ;

        attributes->table[hval]->match = copy_attr->match ;
        attributes->table[hval]->next = copy_attr->next ;
        xmlg_attr_free(attributes, copy_attr) ;
      }
    } else {
      /* scan list from the beginning and de-link curr */
      for (prev = attributes->table[hval]; prev != NULL; prev = prev->next) {
        if (prev->next == curr) {
          prev->next = curr->next ;
          break ;
        }
      }
      /* we know its not the first, so de-allocate it. */
      xmlg_attr_free(attributes, curr) ;
    }
    attributes->num_entries-- ;
  }
}

/* ============================================================================
* Log stripped */
