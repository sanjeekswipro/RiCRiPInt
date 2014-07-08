/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!export:mmcompat.h(EBDSDK_P.1) $
 * $Id: export:mmcompat.h,v 1.16.2.1.1.1 2013/12/19 11:25:11 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Compatibility Layer above the Memory Management Interface
 */

#ifndef __MMCOMPAT_H__
#define __MMCOMPAT_H__

#include "mm.h"


/* Allocation and Freeing with size headers */

extern mm_addr_t mm_alloc_with_header_proc(mm_pool_t pool, mm_size_t size,
                                           mm_alloc_class_t class
                                           MM_DEBUG_LOCN_PARMS );

extern mm_addr_t mm_realloc_with_header_proc(mm_pool_t pool, mm_addr_t what,
                                             mm_size_t size, mm_alloc_class_t class
                                             MM_DEBUG_LOCN_PARMS );

extern mm_result_t mm_alloc_multi_hetero_with_headers_proc(mm_pool_t pool,
                                                           mm_size_t count,
                                                           mm_size_t sizes[],
                                                           mm_alloc_class_t class[],
                                                           mm_addr_t returns[]
                                                           MM_DEBUG_LOCN_PARMS );

extern mm_result_t mm_alloc_multi_homo_with_headers_proc(mm_pool_t pool,
                                                         mm_size_t count,
                                                         mm_size_t size,
                                                         mm_alloc_class_t class,
                                                         mm_addr_t returns[]
                                                         MM_DEBUG_LOCN_PARMS );

extern void mm_free_with_header(mm_pool_t pool, mm_addr_t what );

#define mm_alloc_with_header(     pool, size, class) \
       (mm_alloc_with_header_proc(pool, size, class MM_DEBUG_LOCN_ARGS))

#define mm_realloc_with_header(     pool, what, size, class) \
       (mm_realloc_with_header_proc(pool, what, size, class MM_DEBUG_LOCN_ARGS))

#define mm_alloc_multi_hetero_with_headers(     pool, count, sizes, classes, returns) \
       (mm_alloc_multi_hetero_with_headers_proc(pool, count, sizes, classes, returns \
                                                 MM_DEBUG_LOCN_ARGS))

#define mm_alloc_multi_homo_with_headers(     pool, count, size, class, returns ) \
       (mm_alloc_multi_homo_with_headers_proc(pool, count, size, class, returns \
                                               MM_DEBUG_LOCN_ARGS))


/* "Static" allocation */ 

extern void *mm_alloc_static(size_t size);

/* The out_buf for mm_pretty_memory_string() should have this many
   bytes available for writing. */
#define MAX_MM_PRETTY_MEMORY_STRING_LENGTH 64

/* Pretty print the memory units. We have a function for this so that
   memory units are consistently reported in the RIP. Handles max of 2
   TiB expressed in KiB. Return value is the out_buf passed in. */
extern char *mm_pretty_memory_string(uint32 size_in_kib, char *out_buf);


/* == Revision Log == */
/* Log stripped */
#endif /* __MMCOMPAT_H__ */ 
