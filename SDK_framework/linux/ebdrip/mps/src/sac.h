/* impl.h.sac: SEGREGATED ALLOCATION CACHES INTERFACE
 *
 * $Id: sac.h,v 1.6.11.1.1.1 2013/12/19 11:27:07 anon Exp $
 * $HopeName: MMsrc!sac.h(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2002-2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 */

#ifndef sac_h
#define sac_h

#include "mpmtypes.h"
#include "mpm.h" /* for PoolArena */


#define sacClassLIMIT ((Count)8)


/* ExternalSAC -- the external face of segregated allocation caches */
/* .sac: This structure must match impl.h.mps.sac. */

typedef struct ExternalSACStruct *ExternalSAC;

typedef struct SACFreeListBlockStruct {
  Size size;
  Count count;
  Count countMax;
  Addr blocks;
} SACFreeListBlockStruct;

typedef SACFreeListBlockStruct *SACFreeListBlock;

typedef struct ExternalSACStruct {
  size_t middle; /* block size for starting searches */
  Bool trapped; /* trap status */
  /* freelist, variable length */
  SACFreeListBlockStruct freelists[2 * sacClassLIMIT];
} ExternalSACStruct;


/* SAC -- the real segregated allocation caches */

#define SACSig ((Sig)0x5195AC99) /* SIGnature SAC */

typedef struct SACStruct *SAC;

typedef struct SACStruct {
  Sig sig;
  Pool pool;
  Count classesCount;  /* number of classes */
  Index middleIndex;   /* index of the middle */
  ExternalSACStruct esacStruct; /* variable length, must be last */
} SACStruct;

#define SACOfExternalSAC(esac) PARENT(SACStruct, esacStruct, esac)

#define ExternalSACOfSAC(sac) (&((sac)->esacStruct))

#define SACArena(sac) PoolArena((sac)->pool)


/* SACClasses -- structure for specifying classes in the cache */
/* .sacc: This structure must match impl.h.mps.sacc. */

typedef struct SACClassesStruct *SACClasses;

typedef struct SACClassesStruct {
  Size blockSize;
  Count cachedCount;
  unsigned frequency;
} SACClassesStruct;


extern Res SACCreate(SAC *sac_o, Pool pool, Count classesCount,
                     SACClasses classes);
extern void SACDestroy(SAC sac);
extern Res SACFill(Addr *p_o, SAC sac, Size size, Bool hasReservoirPermit);
extern void SACEmpty(SAC sac, Addr p, Size size);
extern void SACFlush(SAC sac);
extern Size SACFreeSize(SAC sac);


#endif /* sac_h */
