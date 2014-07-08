/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __RNBWDONG_H__
#define __RNBWDONG_H__

/*
 * $HopeName: SWsecurity!export:rnbwdong.h(EBDSDK_P.1) $
 * Interface to a C plus and SuperPro dongles
 *
* Log stripped */

/* ----------------------- Includes ---------------------------------------- */

#include "product.h"  /* includes std.h ... */
#include "obfusc.h"

#ifdef SENTINEL_OR_ACTIVATOR_M
#  error SENTINEL_OR_ACTIVATOR_M is obsolete symbol. Activator no longer supported.
#endif

/* ----------------------- Types ------------------------------------------- */


/* ----------------------- Functions --------------------------------------- */

extern int32 fullTestSuperpro (int32 * quantums);
extern void reportSuperproError (void);
extern int32 testSuperpro (void);
extern void endSuperpro( void );
extern int32 getSuperproSerialNo (uint32 * pSerialNo);
extern int32 getSuperproSerialAndSecurityNo (uint32 * pSerialNo, uint32 * pSecurityNo);

extern int32 fullTestUpgradeSuperpro (int32 * quantums);
extern int32 testUpgradeSuperpro (void);

extern int32 fullTestCplus( int32 * resultArray );
extern int32 testCplus(void);
extern void reportCplusError(void);
extern int32 getCplusSerialNo (uint32 * pSerialNo);
extern int32 getCplusSerialAndSecurityNo (uint32 * pSerialNo, uint32 * pSecurityNo);

extern void reportSentinelorActivatorError (void);
extern void reportSentinelError(void);

/* Return TRUE if dongle is SuperPro and has time-expiry feature. */
extern int32 dongleIsTimedSuperPro( void );

/* Decrement the count of time remaining in a time-expiry SuperPro. */
extern void decrementSuperProTimer( void );

/* Some SuperPro dongles can hold date info
 * This is implemented using counter cells, and cannot be set backwards
 * The format for get/setSuperproDongleDate() param is the licence
 * server format, i.e. YYYYMMDD
 */
extern int32 superproDongleHoldsDate( int32 * pfHoldsDate );
extern int32 getSuperproDongleDate( uint32 * pDate );
extern int32 setSuperproDongleDate( uint32 date );

extern int32 getSuperproLLvLocales( uint32 * pLLvLocales );

#endif /* !__RNBWDONG_H__ */
