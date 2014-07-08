/*
 * $HopeName: SWle-security!export:lesecgen.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

/**
 * @file
 *
 * @brief APIs to generate and interpret LE security data
 */

#ifndef __LESECGEN_H__
#define __LESECGEN_H__

/* ----------------------------- Includes ---------------------------------- */

#include "std.h"


/* ----------------------------- Types ------------------------------------- */

typedef struct LeSecPassword
{
  uint32 id;
  uint32 value;

} LeSecPassword;


/* ----------------------------- Defines ----------------------------------- */

#define MAX_HANDSHAKE_LEN 64


/* ----------------------------- Functions --------------------------------- */

/**
 * @brief Encode provided data buffer
 */
extern void lesecEncode( uint32 len, uint8 * pIn, uint8 * pOut );

/**
 * @brief Decode provided data buffer
 */
extern void lesecDecode( uint32 len, uint8 * pIn, uint8 * pOut );

/**
 * @brief Validate provided data buffer
 */
extern int32 lesecValidateData( uint32 oemID, uint32 len, uint8 * pData );

/**
 * @brief Extract OEM ID from decoded data buffer
 */
extern uint32 getOemID( uint8 * pData );

/**
 * @brief Extract security number from decoded data buffer
 */
extern uint32 getSecurityNo( uint8 * pData );

/**
 * @brief Extract resolution limit from decoded data buffer
 */
extern uint32 getResLimit( uint8 * pData );

/**
 * @brief Extract product version from decoded data buffer
 */
extern uint32 getProductVersion( uint8 * pData );

/**
 * @brief Extract platform code from decoded data buffer
 */
extern uint32 getPlatform( uint8 * pData );

/**
 * @brief Extract handshake data from decoded data buffer
 */
extern uint8 * copyHandshake( uint8 * pData, uint32 * pcbLen );

/**
 * @brief Free handshake data obtained via copyHandshake()
 */
extern void freeHandshake( uint8 * pHandshake );

/**
 * @brief Extract passwords from decoded data buffer
 */
extern LeSecPassword * copyPasswords( uint8 * pData, uint32 * pnPasswords );

/**
 * @brief Free passwords obtained via copyPasswords()
 */
extern void freePasswords( LeSecPassword * pPasswords );

/**
 * @brief Create unencoded data buffer
 */
extern uint8 * createData( uint32 oemID, uint32 secNo,
  uint32 resLimit, uint32 productVersion, uint32 platform,
  uint32 cbHandshakeLen, uint8 * pHandshake,
  uint32 nPasswords, LeSecPassword * pPasswords,
  uint32 * pcbLen );

/**
 * @brief Free data buffer obtained via createData()
 */
extern void freeData( uint8 * pData );

/**
 * @brief Fill buffer with random values
 */
extern void fillBuffer( uint8 * pBuffer, uint32 len );


/*
* Log stripped */

#endif /* __LESECGEN_H__ */

/* end of lesecgen.h */
