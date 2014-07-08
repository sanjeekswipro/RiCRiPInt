/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __EVE3DONG_H__

/*
 * Eve 3 dongle interface - header file.
 * $HopeName: SWsecurity!export:eve3dong.h(EBDSDK_P.1) $
 * 
 * Revision History:
 * 
* Log stripped */


/* -------- Includes -------- */

#include "product.h"  /* includes std.h ... */


#ifdef EVE_DONGLE  /* Only compile this file if an eve dongle is attached. */


/* -------- Public Macros -------- */

/* Size of array below */
#define CHALLENGEKEY_ARRAY_SIZE 7

/* Challenge values for Eve 3 */
#define INITIALIZE_CHALLENGE_ARRAY(aChallengeValue) MACRO_START \
  aChallengeValue[0] = 24650 ;	    /* Parsnip's Birthday. */	\
  aChallengeValue[1] = 10866 ;	    /* Ravey's   Birthday. */	\
  aChallengeValue[2] = (uint16) 211061 ;	    /* Nick's    Birthday. */	\
  aChallengeValue[3] = 3761 ;	    /* Huge's    Birthday. */	\
  aChallengeValue[4] = 28152 ;	    /* Torill's  Birthday. */	\
  aChallengeValue[5] = 11358 ;	    /* Derekk's  Birthday. */	\
  aChallengeValue[6] = 20688;       /* Toshi's   Birthday. */   \
MACRO_END

/* Harlequin's developer ID */
#define EVE3_HQN_ID  (62486)


/* -------- Public functions -------- */

extern int32 fEVE3TestDongle( void );

extern int32 fEVE3FullTestDongle( int32 * resultArray );

extern void eve3EndDongle( void );

extern void reportEve3DongleError( void );

/* Return TRUE if dongle is Eve3 with time-expiry feature. */
extern int32 dongleIsTimedEve3( void );

/* Decrement the count of time remaining in a time-expiry Eve3. */
extern void decrementEve3Timer( void );

extern int32 getEve3SerialNo( uint32 * pSerialNo );

extern int32 getEve3SerialAndSecurityNo( uint32 * pSerialNo, uint32 * pSecurityNo );

#endif /* EVE_DONGLE */

#endif /* Multiple inclusion protection. */
