/* Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_main.h(EBDSDK_P.1) $
 * 
 */
/*! \file
 *  \ingroup OIL
 *  \brief This header file contains the declarations of the high
 * level functions that form the interface to the OIL.
 * 
 * This includes functions that initialize and inactivate the OIL 
 * and/or individual jobs, as well as functions for registering
 * resources RIP modules with the RIP.
 */

#ifndef _OIL_MAIN_H_
#define _OIL_MAIN_H_

/** Global structure encapsulating the OIL's current system state */
extern OIL_TySystem g_SystemState;

/* interface functions */
int SysInit(OIL_eTySystemState);
int SysExit(OIL_eTySystemState);
int JobInit(OIL_eTySystemState);
int JobExit(OIL_eTySystemState);

int RegisterResources(void);
int RegisterRIPModules(void);

#endif /* _OIL_MAIN_H_ */
