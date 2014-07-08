/* impl.h.shared: SHARED MEMORY SEGMENTS INTERFACE
 *
 * $Id: shared.h,v 1.5.11.1.1.1 2013/12/19 11:27:05 anon Exp $
 * $HopeName: MMsrc!shared.h(EBDSDK_P.1) $
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * .design: See design.mps.arena.shared. */

#ifndef shared_h
#define shared_h

#include "mpmtypes.h"
#include "mpsash.h"


typedef struct SharedSegStruct *SharedSeg;

extern Bool SharedSegCheck(SharedSeg shSeg);

extern Res SharedSegCreate(SharedSeg *shSegReturn, Size size);
extern void SharedSegDestroy(SharedSeg shSeg);

extern Addr SharedSegBase(SharedSeg shSeg);
extern Addr SharedSegLimit(SharedSeg shSeg);

extern Res SharedSegSlaveInit(mps_sh_arena_details_s *ext);
extern void SharedSegSlaveFinish(mps_sh_arena_details_s *ext);

extern Align SharedAlign(void);


#endif /* shared_h */
