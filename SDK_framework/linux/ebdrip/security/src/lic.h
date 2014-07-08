/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __LIC_H__
#define __LIC_H__

/*
 * $HopeName: SWsecurity!src:lic.h(EBDSDK_P.1) $
 * Export of the Harlequin License Server RIP security.
 *
* Log stripped */

/* ----------------------- Includes ---------------------------------------- */

#include "product.h"

/* ----------------------- Types ------------------------------------------- */


/* ----------------------- Data -------------------------------------------- */


/* ----------------------- Functions --------------------------------------- */

extern int32 fullTestLIC(int32 * resultArray);
extern int32 testLIC(void);
extern void endLIC(void);

extern void reportLICError(void);

extern uint32 getLicenseUpPeriod(void);

#ifdef CORESKIN
extern void doForceCheckHLSExpiringLicenses( void * pUnused );
extern void installHLStasks( void );

extern void lockHLSMutex( void );
extern void unlockHLSMutex( void );
#endif

#endif /* !__LIC_H__ */
