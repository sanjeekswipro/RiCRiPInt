/* impl.h.poolepvm: ELECTRONIC PUBLISHING "VIRTUAL MEMORY" CLASS INTERFACE
 *
 * $Id: poolepvm.h,v 1.3.11.1.1.1 2013/12/19 11:27:09 anon Exp $
 * $HopeName: MMsrc!poolepvm.h(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2002-2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * .purpose: Internal interface to EPVM functionality.  */

#ifndef poolepvm_h
#define poolepvm_h


#include "poolams.h"
#include "mpmtypes.h"


/* forward declaration of structures */

typedef struct EPVMStruct EPVMStruct;
typedef EPVMStruct *EPVM;
typedef struct EPVMSaveStruct EPVMSaveStruct;
typedef EPVMSaveStruct *EPVMSave;
typedef struct EPVMSegStruct EPVMSegStruct;
typedef EPVMSegStruct *EPVMSeg;


/* EPVMSegStruct -- EPVM segment instances */

#define EPVMSegSig      ((Sig)0x519EBF39) /* SIGnature EPVM seG */

struct EPVMSegStruct {
  AMSSegStruct amsSegStruct; /* superclass fields must come first */
  EPVMSave save;         /* owning save structure */
  Sig sig;               /* design.mps.pool.outer-structure.sig */
};


/* EPVMStruct -- EPVM pool instance structure */

#define EPVMSig         ((Sig)0x519EBF33) /* SIGnature EPVM */

struct EPVMStruct {
  AMSStruct amsStruct;      /* generic AMS structure */
  Index saveLevel;          /* current save level */
  Index maxSaveLevel;       /* maximum save level */
  Size subsequentSegRound;  /* modulus of subsequent segs */
  EPVMSaveStruct *saves;    /* pointer to array of save structs */
  Sig sig;
};

#define Pool2EPVM(pool) PARENT(EPVMStruct, amsStruct.poolStruct, pool)
#define EPVM2Pool(epvm) (&(epvm)->amsStruct.poolStruct)


extern Bool EPVMCheck(EPVM epvm);

extern Bool EPVMSegCheck(EPVMSeg epvmSeg);

#define EPVMLevelSave(epvm, level) (&(epvm)->saves[level])


typedef AMSPoolClass EPVMPoolClass;
extern EPVMPoolClass EPVMPoolClassGet(void);

typedef SegClass EPVMSegClass;
extern EPVMSegClass EPVMSegClassGet(void);


#endif /* poolepvm_h */
