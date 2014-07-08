#ifndef __XMLGATTRSPRIV_H__
#define __XMLGATTRSPRIV_H__
/* ============================================================================
 * $HopeName: HQNgenericxml!src:xmlgattrspriv.h(EBDSDK_P.1) $
 * $Id: src:xmlgattrspriv.h,v 1.14.4.1.1.1 2013/12/19 11:24:21 anon Exp $
 * 
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \brief Private attributes interface for XMLG.
 */

#include "xmlgattrs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * These structures are defined here so that we can set up SAC
 * allocations for them. All structures ought to be 8 byte aligned
 * although this is protected by aligning the structure size on
 * allocation and deallocation.
 */

/**
 * Smallish prime. Do not expect there will many attributes in a
 * single element. I tried various XPS jobs when testing the hash
 * size. 17 gave a number of collisions wher as 23 did not.
 */
#define ATTRIBUTE_HASH_SIZE 37

/* Attribute values are generally short lived and typically
   short. Pre-allocate the attribute values so we do not need to make
   a call to a memory allocator for most attribute values. */
#define PRE_ALLOCATED_ATTR_SIZE 128

/**
 * The internal representation of a single attribute.
 */
typedef struct Attribute {
   /* NOTE: When changing, update SWcore!testsrc:swaddin:swaddin.cpp. */
  HqBool is_being_used ;            /* Is this entry being used? */
  const xmlGIStr *attrlocalname ;   /* Key1               */
  const xmlGIStr *attruri ;         /* Key2               */
  const xmlGIStr *attrprefix ;      /* The prefix         */
  uint8 static_attrvalue[PRE_ALLOCATED_ATTR_SIZE] ;
  const uint8 *attrvalue ;          /* Payload data       */
  uint32 attrvaluelen ;
  uint32 hash ;
  HqBool match ;                    /* It's been matched  */
  struct Attribute *next ;          /* Singly-linked list for hash chain  */
  struct Attribute *stack_next ;    /* Linked lists for attribute stack */
  struct Attribute *stack_prev ;
} Attribute ;

/* Structure is hidden from internal code also.
 */
struct xmlGAttributes {
  /* NOTE: When changing, update SWcore!testsrc:swaddin:swaddin.cpp. */
  /* We use the XML context to intern strings. The XML subsystem MUST
     last the lifetime of the attributes. */
  xmlGContext *xml_ctxt ;

  /* We copy the memory handler from the filter chain. If the memory
     handler lives longer than the filter chain, then the attributes
     can remain longer than the filter chain does. */
  xmlGMemoryHandler memory_handler ;

  /* We do not deallocate the attributes unless this is zero. */
  uint32 ref_count ;
  /* Number of entries in hash table */
  uint32 num_entries ;
  /* The hash table */
  /*@partial@*/
  Attribute* table[ATTRIBUTE_HASH_SIZE] ;

  /* We keep a stack of attributes for fast attribute interaction. */
  Attribute *stack ;

  struct Attribute *next_scan ; /* Next attribute being scanned. */
} ;


#define SAC_ALLOC_ATTRIBUTES_SIZE \
  DWORD_ALIGN_UP(sizeof(xmlGAttributes) + \
                 (sizeof(Attribute*) * ATTRIBUTE_HASH_SIZE) + \
                 (sizeof(Attribute) * ATTRIBUTE_HASH_SIZE))

#ifdef __cplusplus
}
#endif

/* ============================================================================
* Log stripped */
#endif /*!__XMLGATTRSPRIV_H__*/
