/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __REALTIME_H__
#define __REALTIME_H__

/*
 * $HopeName: SWsecurity!src:realtime.h(EBDSDK_P.1) $
 */

/*
* Log stripped */


/* ----------------------- Includes --------------------------------------- */


/* This is probably all you need to use this header file */
typedef signed int int32;
typedef unsigned short uint16;


/* ----------------------- Macros --------------------------------------- */

/* Some deliberate obfuscation */
#define CheckRealTimeImageSecurity  countValues
#define TestRealTimeImageSecurity   enumerateValues

/* ----------------------- Functions ------------------------------------ */

/* Full check of the security
 * Returns 0 for failure, non-zero for success
 * If successful sets *serialNumber to the 16-bit serial number
 */
extern int32 CheckRealTimeImageSecurity( uint16 * serialNumber );

/* A heartbeat test of the security
 * Returns 0 for failure, non-zero for success
 */
extern int32 TestRealTimeImageSecurity( void );


#endif /* !__REALTIME_H__ */
