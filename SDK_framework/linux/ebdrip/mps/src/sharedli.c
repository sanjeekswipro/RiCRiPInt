/* impl.c.sharedli: SHARED MEMORY SEGMENTS LINUX IMPLEMENTATION
 *
 * $Id: sharedli.c,v 1.5.11.1.1.1 2013/12/19 11:27:04 anon Exp $
 * $HopeName: MMsrc!sharedli.c(EBDSDK_P.1) $
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * .design: See design.mps.arena.shared.
 *
 * .address-type: Here, we know Addr is compatible with LPVOID.  */

#define _XOPEN_SOURCE 500
#include "shared.h"
#include "mpm.h"
#include "mpsash.h"
#include "ring.h"
#include <limits.h> /* INT_MAX */
#include <errno.h>
#include <unistd.h> /* sysconf(2) */
#include <sys/ipc.h> /* IPC_PRIVATE */
#include <sys/shm.h> /* shmget(2) */
#include <sys/types.h> /* shmat(2) */

SRCID(sharedli, "$Id: sharedli.c,v 1.5.11.1.1.1 2013/12/19 11:27:04 anon Exp $");


/* SharedSegStruct -- shared-memory segment descriptor */

#define SharedSegSig ((Sig)0x5195E958) /* SIGnature SEGment SHared */

typedef struct SharedSegStruct {
  Sig sig;
  Addr base, limit;       /* boundaries of the space available to client */
  int shmid;              /* shared segment id */
} SharedSegStruct;


/* SharedSegCheck -- check a SharedSeg */

Bool SharedSegCheck(SharedSeg shSeg)
{
  CHECKS(SharedSeg, shSeg);
  CHECKL(shSeg->base < shSeg->limit);
  CHECKL(shSeg->shmid != -1);
  return TRUE;
}


/* SharedAlign -- return alignment of shared segments */

Align SharedAlign(void)
{
  return (Align)sysconf(_SC_PAGESIZE);
}


/* SharedSegCreate -- allocate a new shared-memory segment of a given size */

Res SharedSegCreate(SharedSeg *shSegReturn, Size size)
{
  Size actualSize;
  Align pagesize;
  SharedSeg shSeg;
  int shmid;
  void *p;

  AVER(shSegReturn != NULL);
  AVER(size > 0);

  pagesize = (Align)sysconf(_SC_PAGESIZE);

  /* Extra page to put the segment descriptor on, and round to page size. */
  actualSize = SizeAlignUp(size + pagesize, pagesize);
  if ((actualSize < size) || (actualSize > (Size)INT_MAX))
    return ResRESOURCE;

  shmid = shmget(IPC_PRIVATE, (int)actualSize, 0600);
  if (shmid == -1) {
    int e = errno; /* copied so that the debugger can see the value */

    AVER(e == ENOSPC || e == ENOMEM || e == EINVAL);
    /* EINVAL occurs when actualSize > SHMMAX. */
    return ResRESOURCE;
  }
  p = shmat(shmid, NULL, 0);
  if (p == (void *)-1) {
    int e = errno;
    int res;

    AVER(e == ENOMEM);
    res = shmctl(shmid, IPC_RMID, NULL);
    AVER(res == 0);
    return (e == ENOMEM) ? ResRESOURCE: ResFAIL;
  }

  shSeg = (SharedSeg)p;
  shSeg->shmid = shmid;
  shSeg->base = AddrAlignUp((Addr)(shSeg+1), pagesize);
  shSeg->limit = AddrAdd((Addr)p, actualSize);
  shSeg->sig = SharedSegSig;
  AVERT(SharedSeg, shSeg);
  *shSegReturn = shSeg;
  return ResOK;
}


/* mps_sh_arena_details -- return enough details for a slave to map it */

mps_sh_arena_details_s *mps_sh_arena_details(mps_arena_t arena)
{
  UNUSED(arena);
  return NULL;
}


/* SharedSegSlaveInit -- map shared segments for a slave process */

Res SharedSegSlaveInit(mps_sh_arena_details_s *ext)
{
  UNUSED(ext);
  return ResOK;
}


/* SharedSegSlaveFinish -- unmap shared segments for a slave process */

void SharedSegSlaveFinish(mps_sh_arena_details_s *ext)
{
  UNUSED(ext);
}


/* SharedSegDestroy -- destroy shared segment */

void SharedSegDestroy(SharedSeg shSeg)
{
  int shmid = shSeg->shmid;
  int res;

  res = shmdt((void *)shSeg);
  AVER(res == 0);
  res = shmctl(shmid, IPC_RMID, NULL);
  AVER(res == 0);
}


/* SharedSegBase -- return the base address of memory available to client */

Addr SharedSegBase(SharedSeg shSeg)
{
  AVERT(SharedSeg, shSeg);
  return shSeg->base;
}


/* SharedSegLimit -- return the limit address of memory available to client */

Addr SharedSegLimit(SharedSeg shSeg)
{
  AVERT(SharedSeg, shSeg);
  return shSeg->limit;
}
