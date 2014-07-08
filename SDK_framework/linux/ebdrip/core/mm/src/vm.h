/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:vm.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Miscellaneous MM glue layer internals
 */

#ifndef __VM_H__
#define __VM_H__

#include "mm.h" /* mm_pooltype_t */
#include "mps.h"
#include "lowmem.h" /* memory_requirement_t */


#if defined( USE_MM_DEBUGGING ) || defined( DEBUG_BUILD )
#define MM_DEBUG_SCRIBBLE
#else
#undef MM_DEBUG_SCRIBBLE
#endif


/** Two gigabytes

  Most of the core can't handle objects larger than 2 GB, so warn if one
  is encountered. */
#define TWO_GB ((size_t)2u*1024u*1024u*1024u)


/* This compilation flag controls the feature to force all allocs to fail. */
#undef FAIL_AFTER_N_ALLOCS

#ifdef FAIL_AFTER_N_ALLOCS
extern unsigned long n_alloc_calls;
extern unsigned long fail_after_n;
#endif


extern mps_word_t alloc_class_label[MM_ALLOC_CLASS_LIMIT];


#ifdef MM_DEBUG_MPSTAG

mps_word_t mm_location_label(char *location);

#define MPSTAG_ARG , &dinfo
#define MPSTAG_FN(name) name##_debug
#define MPSTAG_SET_DINFO(class) MACRO_START \
    dinfo.location = mm_location_label(location); \
    dinfo.mps_class = alloc_class_label[class]; \
  MACRO_END

#else

#define MPSTAG_ARG
#define MPSTAG_FN(name) name
#define MPSTAG_SET_DINFO(class)

#endif


/** \brief Does the state of the reserves allow an allocation at default cost?

 \param[out] context  The core context, possibly; if return value is
                      \c TRUE, can be unchanged.

  If reserves are below what this allocation may use, it would
  potentially use costlier reserve memory, so the reserves have to be
  regained first. If not, the allocation can be tried.

  If there's no context (on a skin thread or during init), the
  allocation is allowed.

  This function passes back the core context, if it needed to fetch it
  in order to decide. See callers for usage. */
inline Bool reserves_allow_alloc(corecontext_t **context);


/** Type of allocation function passed to \c mm_low_mem_alloc. */
typedef mps_res_t (MPS_CALL alloc_fn)( mps_addr_t *p, void *args, size_t size
#ifdef MM_DEBUG_MPSTAG
                                     , mps_debug_info_s *dinfo
#endif
                                       );


/** Does low-memory handling for an allocation, retrying it repeatedly.

  \param[out] p  Pointer to the memory allocated.
  \param[in] context  The core context.
  \param[in] request  The allocation request.
  \param[in] fn  The function to call to do the allocation.
  \param[in] args  The first argument to \a fn.
 */
mps_res_t mm_low_mem_alloc(mps_addr_t *p, corecontext_t *context,
                           memory_requirement_t *request,
                           alloc_fn *fn, void *args
#ifdef MM_DEBUG_MPSTAG
                         , mps_debug_info_s *dinfo
#endif
                           );


#endif /* __VM_H__ */

/* Log stripped */
