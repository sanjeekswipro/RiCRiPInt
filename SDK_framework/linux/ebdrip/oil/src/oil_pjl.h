/* Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_pjl.h(EBDSDK_P.1) $
 * 
 */
/*! \file
 *  \ingroup OIL
 *  \brief OIL PJL functions header
 *
 * This file contains interfaces for the OIL wrapper to the PJL handling functionality.
 */

#ifndef _OIL_PJL_H_
#define _OIL_PJL_H_

#include "oil.h"
#include "pms_export.h"


extern void OIL_PjlInit( void );
extern void OIL_PjlSetEnvironment( PMS_TyJob * pms_pstJobCurrent );
extern PMS_TyJob *OIL_PjlGetEnvironment( void );
extern void OIL_PjlClearEnvironment( void );
extern void OIL_PjlJobFailed( unsigned int uJobID );
extern int OIL_PjlParseData( unsigned char * pData, size_t cbDataLen, size_t * pcbDataConsumed );
extern void OIL_PjlExit( void );

extern void OIL_PjlReportDeviceStatus( int statusCode, unsigned char * pzDisplay, int fOnline );
extern void OIL_PjlReportJobCancel( unsigned int uJobID );
extern void OIL_PjlReportEoj( OIL_TyJob * pJob );
extern void OIL_PjlReportPageStatus( int nPages );

extern int OIL_PjlGetJobStart( void );
extern int OIL_PjlGetJobEnd( void );
extern int OIL_PjlShouldOutputPage( int nPage, OIL_TyJob * pJob );

extern int OIL_PjlMapPdlValue( int ePJL_PDL );

extern void OIL_ResetPJLAppData(void);

extern char *OIL_PjlGetFileName(void);

#endif /* _OIL_PJL_H_ */
