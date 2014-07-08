/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __MPHARDNG_H__
#define __MPHARDNG_H__

/*
 * $HopeName: SWsecurity!export:mphardng.h(EBDSDK_P.1) $
 *
 *
 */               

/* ----------------------- Includes ---------------------------------------- */

#include "product.h"  /* includes std.h ... */
#include "obfusc.h"

/* ----------------------- Types ------------------------------------------- */


/* ----------------------- Functions --------------------------------------- */
extern int32 fullTestMicroPharx3( int32 * resultArray );
extern int32 testMicroPhar(void);
extern int32 getMicroPharSerialNo (uint32 * pSerialNo);
extern int32 getMicroPharSerialAndSecurityNo (uint32 * pSerialNo, uint32 * pSecurityNo);

#endif /* !__MPHARDNG_H__ */
