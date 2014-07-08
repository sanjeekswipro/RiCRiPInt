/* Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/* $HopeName: SWdllskin!src:signdev.h(EBDSDK_P.1) $
 *
 * Device to check DLL signature.
 *
* Log stripped */

#ifndef __SIGNDEV_H__
#define __SIGNDEV_H__


/* ----------------------- Includes ---------------------------------------- */

#include "std.h"
#include "swdevice.h"   /* DEVICETYPE */
#include "fwerror.h"    /* FwErrorState */

/* ----------------------- Globals ----------------------------------------- */

extern DEVICETYPE SignCheck_Device_Type;


/* ----------------------- Prototypes -------------------------------------- */

void start_sig_check(void);
void end_sig_check(void);

/* These 3 functions are platform-specific */

/* Prepare to check the DLL's signature */
uint32 StartChecking( FwErrorState * pErrorState, void ** ppState );

/* Do next step of checking the DLL's signature */
uint32 DoNextCheck( FwErrorState * pErrorState, void ** ppState );

/* Tidy up having completed checking the DLL's signature */
void TidyUpAfterChecking( void ** ppState );


#endif
