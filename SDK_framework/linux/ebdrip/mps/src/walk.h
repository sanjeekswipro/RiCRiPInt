/* impl.h.walk: WALKING INTERFACE
 *
 * $HopeName: MMsrc!walk.h(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2002-2011 Global Graphics Software Ltd. All rights reserved.
 */

#ifndef walk_h
#define walk_h

#include "mpmtypes.h"
#include "mps.h"


extern Res ArenaRootsWalk(Globals arenaGlobals, mps_roots_stepper_t f,
                          void *p, size_t s);


#define FormattedObjectsStepClosureSig ((Sig)0x519F05C1)

typedef struct FormattedObjectsStepClosureStruct *FormattedObjectsStepClosure;

typedef struct FormattedObjectsStepClosureStruct {
  Sig sig;
  mps_formatted_objects_stepper_t f;
  void *p;
  size_t s;
} FormattedObjectsStepClosureStruct;


extern Bool FormattedObjectsStepClosureCheck(FormattedObjectsStepClosure c);


extern void ArenaFormattedObjectsWalk(Arena arena, FormattedObjectsStepMethod f,
                                      void *p);


#endif /* walk_h */
