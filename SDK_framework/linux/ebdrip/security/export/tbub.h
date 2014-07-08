/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __TBUB_H__
#define __TBUB_H__

/*
 * $HopeName: SWsecurity!export:tbub.h(EBDSDK_P.1) $
 * Try-before-you-buy checking functionality
 *
 */               

/* ----------------------- Includes ---------------------------------------- */

#include "product.h"

#ifdef CORESKIN

#include "fwstring.h"

/* ----------------------- Macros ------------------------------------------ */

/* Obfuscation */
#define startTBUB                 s222
#define endTBUB                   s223
#define IsTryBeforeYouBuyEnabled  s224
#define IsTryBeforeYouBuyPossible s225
#define localLicenceServerRunning s226
#define resetLocalLicenceServer   s227
#define getTbubPermitName         s228
#define getTbubUniqueID           s229
#define getTbubDaysToRun          s230

/* ----------------------- Functions --------------------------------------- */

extern void startTBUB( void );
extern void endTBUB( void );

extern int32 IsTryBeforeYouBuyEnabled( void );
extern int32 IsTryBeforeYouBuyPossible( void );

extern int32 localLicenceServerRunning( void );
extern int32 resetLocalLicenceServer( void );

extern int32 installTBYB( FwTextString ptbzPermitFile );

extern int32 getTbubUniqueID( uint32 * pid );

/* Returns -1 if licence no licence or expired */
extern int32 getTbubDaysToRun( void );

/* Returns TRUE if license expiry can be determined, in which case
 * *pfExpiring is TRUE is TBYB license is expiring and *pDaysLeft
 * is number of days remaining (1 = license expires today, 0 = expired)
 */
extern int32 tbyb_license_expiring( int32 * pfExpiring, uint32 * pDaysLeft );

#endif /* CORESKIN */

#endif /* !__TBUB_H__ */
