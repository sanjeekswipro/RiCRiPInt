/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __DLLSEC_H__
#define __DLLSEC_H__

/*
 * $HopeName: SWsecurity!src:dllsec.h(EBDSDK_P.1) $
 * Interface to a DLL (shared library or code fragment) to provide
 * security for ScriptWorks.
 *
* Log stripped */

/* ----------------------- Includes ---------------------------------------- */

#include "std.h"
#include "obfusc.h"

/* ----------------------- Types ------------------------------------------- */

typedef struct Licence
{
  uint16 usageDay;    /* The latest day of use */
  uint16 expiryDay;   /* The expiry day, after which licence cannot be used */
  uint16 data[8];     /* Security data */
  uint8  version;     /* Version no of this struct */
  uint8  spare1;
  uint8  spare2;
  uint8  spare3;
} Licence;
                                                 
#define LICENCE_LENGTH   (sizeof( Licence ))
#define ASCII85_LENGTH   (((LICENCE_LENGTH) * 5) / 4 )
#define ENCODED_LENGTH   ((ASCII85_LENGTH) + 2)

/* ----------------------- Functions --------------------------------------- */

extern int32 fullTestDLL(int32 * resultArray);
extern int32 testDLL(void);
#define renewDLL getDirDLL /* obfuscation */
extern int32 renewDLL(void);
extern void reportWatermarkError(void) ;

#endif /* !__DLLSEC_H__ */
