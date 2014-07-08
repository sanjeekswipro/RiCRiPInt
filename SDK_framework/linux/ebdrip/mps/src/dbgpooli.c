/* impl.c.dbgpooli: POOL DEBUG MIXIN C INTERFACE
 *
 * $Id: dbgpooli.c,v 1.6.10.1.1.1 2013/12/19 11:27:10 anon Exp $
 * $HopeName: MMsrc!dbgpooli.c(EBDSDK_P.1) $
 * Copyright (C) 2002-2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * .source: design.mps.object-debug
 */

#include "dbgpool.h"
#include "mps.h"
#include "mpm.h"

SRCID(dbgpooli, "$Id: dbgpooli.c,v 1.6.10.1.1.1 2013/12/19 11:27:10 anon Exp $");


/* mps_pool_check_fenceposts -- check all the fenceposts in the pool */

void MPS_CALL mps_pool_check_fenceposts(mps_pool_t mps_pool)
{
  Pool pool = (Pool)mps_pool;
  Arena arena;
  
  /* CHECKT not AVERT, see design.mps.interface.c.check.space */
  AVER(CHECKT(Pool, pool));
  arena = PoolArena(pool);

  ArenaEnter(arena);

  AVERT(Pool, pool);
  DebugPoolCheckFences(pool);

  ArenaLeave(arena);
}


/* mps_pool_check_free_space -- check free space in the pool for overwrites */

void MPS_CALL mps_pool_check_free_space(mps_pool_t mps_pool)
{
  Pool pool = (Pool)mps_pool;
  Arena arena;
  
  /* CHECKT not AVERT, see design.mps.interface.c.check.space */
  AVER(CHECKT(Pool, pool));
  arena = PoolArena(pool);

  ArenaEnter(arena);

  AVERT(Pool, pool);
  DebugPoolCheckFreeSpace(pool);

  ArenaLeave(arena);
}
