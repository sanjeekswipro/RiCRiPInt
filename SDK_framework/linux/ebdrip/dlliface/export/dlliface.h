/* Copyright (C) 1999-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 *
 * $HopeName: SWdlliface!export:dlliface.h(EBDSDK_P.1) $
 *
 * Wrapper layer to turn core RIP into a DLL.
 */


#ifndef __DLLSKIN_H__
#define __DLLSKIN_H__

#include "swstart.h"
#include "hqcstass.h"

/**
 * \file
 * \ingroup dlliface
 * \brief Functions for booting the RIP when it is encapsulated in its own
 * dynamic library.
 */

/**
 * \defgroup entrypoint_ifaces RIP Entry Point Interfaces
 */

/**
 * \defgroup dlliface RIP Dynamic Link Library Interface
 * \ingroup entrypoint_ifaces
 * \{
 */

typedef void (RIPCALL SwExitFn) ( int32 n, uint8 *text );
typedef void (RIPCALL SwRebootFn) ( void );
typedef void (RIPCALL SwWarnFn) ( uint8 * buffer );
typedef void (RIPCALL SwStartTickleTimerFn)(void);
typedef void (RIPCALL SwStopTickleTimerFn)(void);

typedef struct DllFuncs
{
  SwExitFn             * pfnSwExit;
  SwRebootFn           * pfnSwReboot;
  HqCustomAssert_fn    * pfnHqCustomAssert;
  HqCustomTrace_fn     * pfnHqCustomTrace;
  SwWarnFn             * pfnSwWarn;
  SwStartTickleTimerFn * pfnSwStartTickleTimer;
  SwStopTickleTimerFn  * pfnSwStopTickleTimer;
} DllFuncs;


extern HqBool RIPCALL SwDllInit ( SWSTART * start, DllFuncs * pDllFuncs );
#ifdef DYLIB
extern void RIPCALL SwDllStart ( SWSTART * start, DllFuncs * pDllFuncs );
#else
extern void RIPCALL SwLibStart ( SWSTART * start, DllFuncs * pDllFuncs );
#endif
extern void RIPCALL SwDllShutdown( void ) ;
extern void RIPCALL SwDllWarn( uint8 * buffer ) ;

/**
 * \}
 */

#endif
