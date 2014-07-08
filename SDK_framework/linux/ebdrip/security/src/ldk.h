/* Copyright (C) 2012 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __LDK_H__
#define __LDK_H__

/*
 * $HopeName: SWsecurity!src:ldk.h(EBDSDK_P.1) $
 * Export of the SafeNet LDK RIP security.
 *
* Log stripped */

/* ----------------------- Includes ---------------------------------------- */

#include "product.h"

#include "fwstring.h" /* FwStrRecord */


/* ----------------------- Types ------------------------------------------- */


/* ----------------------- Data -------------------------------------------- */


/* ----------------------- Functions --------------------------------------- */

extern int32 fullTestLDK(int32 * resultArray);
extern int32 testLDK(void);
extern void endLDK(void);

extern int32 getLDKSerialNo(uint32 * pSerialNo);

extern void reportLDKError(void);

extern HqBool ldkCanEnableFeature(uint32 nFeature);
extern HqBool ldkEnablesFeature(uint32 nFeature, HqBool * pfEnabled, int32 * pfTrial);
extern HqBool ldkEnablesSubfeature(uint32 nFeature, char * pbzFeatureStr, HqBool * pfEnabled, int32 * pfTrial);

#if defined (HQNLIC)
extern HqBool preferLDK(void);
#endif

#ifdef CORESKIN
extern HqBool ldkGetRIPIdentity(FwStrRecord * prIdentity);
extern HqBool getLDKKeyId(FwStrRecord *prKeyId);

extern void ldkSetFeatureName(uint32 nFeature, FwTextString ptbzName);

extern void installLDKtasks(void);
#endif

#endif /* !__LDK_H__ */
