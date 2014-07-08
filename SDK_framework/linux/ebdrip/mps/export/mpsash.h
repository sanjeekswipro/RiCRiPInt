/* $Id: export:mpsash.h,v 1.8.11.1.1.1 2013/12/19 11:27:03 anon Exp $
 * $HopeName: SWmps!export:mpsash.h(EBDSDK_P.1) $
 */

/* impl.h.mpsash: MEMORY POOL SYSTEM ARENA CLASS "SH"
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 */

#ifndef mpsash_h
#define mpsash_h

#include "mps.h"


extern mps_arena_class_t MPS_CALL mps_arena_class_sh(void);

typedef struct mps_sh_arena_details_s mps_sh_arena_details_s;

extern mps_sh_arena_details_s* MPS_CALL mps_sh_arena_details(mps_arena_t arena);
extern mps_res_t MPS_CALL mps_sh_arena_slave_init(mps_sh_arena_details_s *details);
extern void MPS_CALL mps_sh_arena_slave_finish(mps_sh_arena_details_s *details);


#endif /* mpsash_h */
