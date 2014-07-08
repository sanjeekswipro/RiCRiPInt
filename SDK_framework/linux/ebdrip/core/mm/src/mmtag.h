/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:mmtag.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Basic MM tagging code interface
 */

#ifndef __MMTAG_H__
#define __MMTAG_H__

#include "mm.h"
#include "mmfence.h" /* MM_DEBUG_FENCEPOST */


/* fenceposting or watching implies tagging */

#if (defined(MM_DEBUG_FENCEPOST) || defined(MM_DEBUG_FENCEPOST_LITE) || defined (MM_DEBUG_WATCH))
#define MM_DEBUG_TAG
#else
#undef MM_DEBUG_TAG
#endif


#ifdef MM_DEBUG_TAG

void mm_tag_init( void ) ;
void mm_tag_finish( void ) ;

void mm_debug_tag_add(char *file,
		      int line,
		      mm_addr_t ptr,
		      mm_size_t size,
		      mm_pool_t pool,
		      mm_alloc_class_t class) ;

void mm_debug_tag_free(mm_addr_t ptr,
		       mm_size_t size,
		       mm_pool_t pool) ;

void mm_debug_tag_truncate(mm_addr_t base,
			   mm_size_t oldsize,
			   mm_size_t newsize,
			   mm_pool_t pool) ;

void mm_debug_tag_free_pool(mm_pool_t pool) ;

void mm_debug_tag_apply( mm_debug_watcher_t fn ) ;


/* Depending on whether MM_DEBUG_LOCN is set, we may or may not have
 * location information available. If it is not available, we have to
 * put a dummy value in the tag.
 */
#ifdef MM_DEBUG_LOCN
# ifdef MM_DEBUG_MPSTAG
#  define MM_DEBUG_TAG_ADD(ptr, size, pool, class) \
          mm_debug_tag_add(location, 0, ptr, size, pool, class)
# else /* !MM_DEBUG_MPSTAG */
#  define MM_DEBUG_TAG_ADD(ptr, size, pool, class) \
          mm_debug_tag_add(file, line, ptr, size, pool, class)
# endif
#else /* !MM_DEBUG_LOCN */
# define MM_DEBUG_TAG_ADD(ptr, size, pool, class) \
         mm_debug_tag_add("not specified", 0, ptr, size, pool, class)
#endif

#define MM_DEBUG_TAG_USES_PARAM(t,p)                    /* nothing */

#else /* MM_DEBUG_TAG */

#define mm_tag_init()                                   EMPTY_STATEMENT()
#define mm_tag_finish()                                 EMPTY_STATEMENT()
#define MM_DEBUG_TAG_ADD(ptr,size,pool,class)           EMPTY_STATEMENT()
#define mm_debug_tag_free(ptr,size,pool)                EMPTY_STATEMENT()
#define mm_debug_tag_truncate(base,ptr,size,pool)       EMPTY_STATEMENT()
#define mm_debug_tag_free_pool(pool)                    EMPTY_STATEMENT()
#define mm_debug_tag_apply(fn)                          EMPTY_STATEMENT()
#define mm_debug_tag_apply_ordered(fn)                  EMPTY_STATEMENT()


#define MM_DEBUG_TAG_USES_PARAM(t, p)                   UNUSED_PARAM(t, p)

#endif /* MM_DEBUG_TAG */

#endif /* __MMTAG_H__ */


/* Log stripped */
