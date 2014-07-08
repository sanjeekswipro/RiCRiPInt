 /** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:mmreserve.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Manage the MM reserve pool and arena extension.
 */

#ifndef __MMRESERVE_H__
#define __MMRESERVE_H__

#include "lowmem.h" /* mm_cost_t */


/* This compilation flag decides between refilling reserves after free
   (the old way) or before alloc. */
#undef REGAIN_AT_FREE


/** Initializes the arena size extension controls. */
Bool mm_extension_init(size_t addr_size,
                       size_t working_size,
                       size_t emergency_size,
                       Bool use_all_mem);

/** Finishes the arena size extension controls. */
void mm_extension_finish(void);

/** Creates the reserve pool, and inits the low-memory handler.
 *
 * Doesn't allocate the reserve, mm_reserve_get() is called for that.
 */
Bool mm_reserve_create(void);

/** Destroys the reserve pool, freeing any allocated reserve memory, and
 *  removes the low-memory handler.
 */
void mm_reserve_destroy(void);


/** Refill reserves up to the limit given, when synchronized.

  Refill the reserves from free memory up to the limit given. The caller
  must synchronize, using \c enter_low_mem_handling(). */
Bool mm_refill_reserves_guts(Bool *enough, mm_cost_t limit);


#ifdef REGAIN_AT_FREE

/** Try to refill all reserves (not for public use).

  This is like \c mm_refill_reserves_guts(), except it synchronizes. */
void mm_refill_reserves(void);

/** Check if the reserves (pool and arena) need to be filled. */
#define mm_recheck_reserves() mm_refill_reserves()

#else

#define mm_recheck_reserves()

#endif


/** Regain reserves up to the limit given, when synchronized.

  This is like \c mm_regain_reserves(), except the caller must
  synchronize, using \c enter_low_mem_handling(). */
Bool mm_regain_reserves_guts(Bool *enough, corecontext_t *context,
                             mm_cost_t fill_limit, mm_cost_t cost);

#ifdef REGAIN_AT_FREE
#define mm_regain_reserves_for_alloc(enough, context, limit) (*(enough) = TRUE)
#else
#define mm_regain_reserves_for_alloc(enough, context, limit) \
  mm_regain_reserves_guts(enough, context, limit, limit)
#endif


#endif /* __MMRESERVE_H__ */

/*
* Log stripped */
