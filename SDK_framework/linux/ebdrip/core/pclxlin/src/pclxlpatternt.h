/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlpatternt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Types for the PCLXL pattern system.
 */
#ifndef _pclxlpatternt_h_
#define _pclxlpatternt_h_

#include "idhashtable.h"
#include "pclAttribTypes.h"
#include "pixlpckr.h"
#include "pclxlimaget.h"

/**
 * PCLXL Pattern cache entry.
 */
typedef struct {
  IdHashTableEntry hash_entry;
  PclXLPattern pattern;
  mm_pool_t pool;
} PCLXL_CACHED_PATTERN;

/**
 * A link in a linked list of pattern caches.
 */
typedef struct PCLXL_PATTERN_CACHE_LINK {
  struct PCLXL_PATTERN_CACHE_LINK* next;
  IdHashTable* cache;
} PCLXL_PATTERN_CACHE_LINK;

/**
 * State held during pattern construction.
 */
typedef struct {
  uint32 id;
  PCLXL_ENUMERATION persist;
  PCLXL_CACHED_PATTERN* cache_entry;
  uint32 expansion_line_size;
  uint8* expansion_line;
  PixelPacker* packer;
  PCLXL_IMAGE_READ_CONTEXT* image_reader;
} PCLXL_PATTERN_CONTRUCTION_STATE;

/**
 * Pattern caches. There are three levels of caching; session, page and gstate.
 */
typedef struct {
  IdHashTable* session;
  IdHashTable* page;

  /* The entries within caches may be used long after the cache would naturally
   * be destroyed (i.e. until rendering is finished); this happens with
   * gstate-level caches for example. Thus we hold a list of all the gstate
   * caches created (which are not empty) for the current page, allowing them to
   * be purged once rendering is complete. */
  PCLXL_PATTERN_CACHE_LINK* gstate_caches;
} PCLXL_PATTERN_CACHES;


#endif

/* Log stripped */

