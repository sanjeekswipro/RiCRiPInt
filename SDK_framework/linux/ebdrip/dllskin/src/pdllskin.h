/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/* $HopeName: SWdllskin!src:pdllskin.h(EBDSDK_P.1) $
 *
 * Wrapper layer to turn core RIP into a DLL.
 *
* Log stripped */


#ifndef __PDLLSKIN_H__
#define __PDLLSKIN_H__

#include "std.h"

extern int32 platformGetRealMilliSeconds();
extern int32 platformGetUserMilliSeconds();

extern uint8 * platformGetOperatingSystem( void ) ;

#endif
