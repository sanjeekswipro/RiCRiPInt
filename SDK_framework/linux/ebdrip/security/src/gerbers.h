/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __GERBERS_H__
#define __GERBERS_H__

/*
 * $HopeName: SWsecurity!src:gerbers.h(EBDSDK_P.1) $
 */

/*
* Log stripped */


/* ----------------------- Includes --------------------------------------- */


/* This is probably all you need to use this header file */
typedef signed int int32;
typedef unsigned short uint16;


/* ----------------------- Macros --------------------------------------- */

/* Some deliberate obfuscation */
#define CheckGergerSDongle  countDict
#define TestGerberSDongle   enumerateDict

/* ----------------------- Functions ------------------------------------ */

/* Check the dongle is there and that the PostScript option is enabled */
extern int32 CheckGergerSDongle(uint16 * serialNumber);

/* A heartbeat test to see if the dongle is still there */
extern int32 TestGerberSDongle(void);


#endif /* !__GERBERS_H__ */
