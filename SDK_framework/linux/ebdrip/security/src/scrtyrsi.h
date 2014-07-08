/* $HopeName: SWsecurity!src:scrtyrsi.h(EBDSDK_P.1) $ */
/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __SCRTYRSI_H__
#define __SCRTYRSI_H__

/*
* Log stripped */


/* ----------------------- Includes --------------------------------------- */

#ifdef __cplusplus
extern "C"
{
#endif


/* This should be the minimal subset of std.h above that 
   you need to use this header file */

/* Must be in sync w/ Password array */
enum	ORDERCODES { ocIAAGK, ocGICXO };

typedef signed int int32;
typedef unsigned short uint16;


/* ----------------------- Macros --------------------------------------- */

/* Some deliberate obfuscation */
#define CheckRSIDongle  countArray
#define TestRSIDongle   enumerateArray

/* ----------------------- Functions ------------------------------------ */

/* Check the dongle is present and determine its unique serial number. Retrun TRUE on
   success and FALSE on absence of dongle or inability to read it or otherwise
   suspect dongle */
extern int32 CheckRSIDongle(uint16 * serialNumber);

/* A heartbeat test to see if the dongle is still there: return TRUE if so, FALSE if
   not. Must physically check the dongle is present, but need not do a full check on
   the contents. */
extern int32 TestRSIDongle(void);

#ifdef __cplusplus
}
#endif

#endif /* !__SCRTYRSI_H__ */
