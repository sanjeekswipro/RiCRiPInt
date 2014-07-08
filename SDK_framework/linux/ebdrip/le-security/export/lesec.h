/*
 * $HopeName: SWle-security!export:lesec.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 */

/** @cond LESEC */

/**
 * @file
 *
 * @brief Security API
 */

#ifndef __LESEC_H__
#define __LESEC_H__

#include "std.h"
#include "mps.h"
#include "ripcall.h"


/* Typedefs */
/**
 * @brief Structure passed to handshake function.
 */
typedef struct SwSecHandshakeStruct
{
  uint32  nLength;
  uint8 * pIn;
  uint8 * pOut;

} SwSecHandshakeStruct;

/**
 * @brief Signature of handshake function.
 */
typedef void ( RIPCALL SwSecHandshakeFn ) ( SwSecHandshakeStruct * pParam );

/**
 * @brief Structure to hold initialization parameters.
 */
typedef struct SwSecInitStruct
{
  int32              oemID;
  uint32             dataLen;
  uint8            * pData;
  SwSecHandshakeFn * handshakeFn;

} SwSecInitStruct;


/* ---- Public routines ----- */

/**
 * @brief Pass initialization paramater values to RIP.
 * Must be called before calling SwLeStart().
 *
 * @param[in] pParam
 * Initialization parameter values, as provided by SecInit()
 * @param[in] arena
 * MPS memory arena
 */
extern void RIPCALL SwSecInit( SwSecInitStruct * pParam, mps_arena_t arena );



/**
 * @brief Shutdown LE security.
 * Must be called if SwSecInit() has been called.
 */
extern void RIPCALL SwSecShutdown( void );


/**
 * @brief Provides security-specific configuration for RIP.
 * Call during first read from config device (see config.c).
 *
 * @param[in] buff
 * Buffer to write configuration to
 *
 * @param[in] len
 * Length of buff
 */
extern void RIPCALL SwSecGetConfig( uint8 * buff, int32 len );



#endif /* __LESEC_H__ */

/** @endcond */

/* end of lesec.h */
