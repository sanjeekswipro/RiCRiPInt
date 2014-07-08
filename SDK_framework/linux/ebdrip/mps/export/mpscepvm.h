/* impl.h.mpscepvm: MEMORY POOL SYSTEM CLASS "EPVM" 
 *
 * $Id: export:mpscepvm.h,v 1.12.1.1.1.1 2013/12/19 11:27:03 anon Exp $
 * $HopeName: SWmps!export:mpscepvm.h(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2002-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

#ifndef mpscepvm_h
#define mpscepvm_h

#include "mps.h"


/* this concrete type is used for save levels in the interface */
typedef size_t mps_epvm_save_level_t;


extern mps_class_t MPS_CALL mps_class_epvm(void);
/* This pool class has three extra parameters to mps_pool_create:
 *  mps_res_t mps_pool_create(mps_pool_t * pool, mps_arena_t arena,
 *                            mps_class_t epvm_class, mps_fmt_t format,
 *                            mps_epvm_save_level_t max_save,
 *                            mps_epvm_save_level_t init_save);
 * being the format used for the objects, the maximum save level and the
 * initial save level.  There's one extra parameter for mps_ap_create:
 *  mps_res_t mps_ap_create(mps_ap_t *ap, mps_pool_t pool,
 *                          mps_bool_t is_obj);
 * Specifying true for is_obj means you want to allocate objects with
 * references of RANKExact in them, false means only objects with no
 * references in them. */

extern mps_class_t MPS_CALL mps_class_epfn(void);
/* This pool class is EPVM with automatic finalization. */

extern mps_class_t MPS_CALL mps_class_epvm_debug(void);
/* This pool class is exactly like EPVM, except it can also do free space
 * checking.  It has an extra parameter between epvm_class and format to
 * specify how:
 *  mps_res_t mps_pool_create(mps_pool_t * pool, mps_arena_t arena,
 *                            mps_class_t epvm_class,
 *                            mps_pool_debug_option_s *opts, mps_fmt_t format,
 *                            mps_epvm_save_level_t max_save,
 *                            mps_epvm_save_level_t init_save);
 * See mps_pool_debug_option_s for details. */

extern mps_class_t MPS_CALL mps_class_epfn_debug(void);
/* This pool class is EPVM_debug with automatic finalization. */


extern mps_bool_t MPS_CALL mps_epvm_check(mps_pool_t* /* pool (output) */,
				          mps_epvm_save_level_t* /* (output) */,
				          mps_arena_t /* arena */,
				          mps_addr_t /* address */);

extern void MPS_CALL mps_epvm_save(mps_pool_t /* pool */);

extern void MPS_CALL mps_epvm_restore(mps_pool_t /* pool */,
			              mps_epvm_save_level_t /* save_level */);

extern mps_res_t MPS_CALL mps_epvm_collect(mps_pool_t /* pool */,
                                           mps_pool_t /* pool */);


#endif /* mpscepvm_h */
