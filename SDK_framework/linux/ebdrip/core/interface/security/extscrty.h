/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_security!extscrty.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 *
 * \brief
 * This file contains common information between the core RIP security
 * and the security mechanism that is in place.
 */

#ifndef __EXTSCRTY_H__
#define __EXTSCRTY_H__

#include "ripcall.h"
#include "std.h" /* int32 */

#ifdef __cplusplus
extern "C" {
#endif

/* Give the security calls names that are not obvious. */
#define fullTestSecurityDevice          getPageText
#define testSecurityDevice              putPageText
#define passedSecurityDevice            nextPageText
#define forcedFullTestSecurityDevice    isPageSizeOK
#define CustomerRipVersionNumber        getMinorPageID
#define DoGetCustomerRipVersionNumber   doGetMinorPageID

/* Values indicating full product and demonstration. */
#define FULLPRODUCT             ((int32)0x00FFFF00u)
#define DEMONSTRATIONPRODUCT    ((int32)0xF00FF0F0u)

#define PROTECTED_PLUGINS       ((int32)0x0F0FF00Fu)
#define UNPROTECTED_PLUGINS     ((int32)0xF0F00FF0u)

/* Define the masks for the security test results. */
#define fullTestSecurityMask    ((int32)0xA3280A13u)
#define testSecurityMask        ((int32)0xED0C5821u)


/* Indices into array passed to fullTestSecurityDevice,testSecurityDevice */
#define iCustomerID         0
#define iProductCode        1
#define iProductVersion     2
#define iDemonstrationOnly  3
#define iResolutionLimit    4
#define iSecurityNumber     5
#define iFeatureLevel       6
#define iProtectedPlugins   7
#define ifPostScriptDenied  8
#define ifPDFDenied         9
#define ifHPSDenied         10
#define ifXPSDenied         11
#define ifApplyWatermark    12
#define ifTeamWork          13
#define nTestSecDevParams   14

int32 RIPCALL fullTestSecurityDevice RIPPROTO((int32 *checkCode, int32 *aParams[ nTestSecDevParams ])) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *fullTestSecurityDevice_fn_t) RIPPROTO((int32 *checkCode, int32 *aParams[ nTestSecDevParams ]));

/* testSecurityDevice is exported from dongle.c in the security component */
int32 RIPCALL testSecurityDevice
RIPPROTO((
  int32 * checkCode,
  int32 * aParams[ nTestSecDevParams ]
));
/* Typedef with same signature. */
typedef int32 (RIPCALL *testSecurityDevice_fn_t) RIPPROTO((int32 *checkCode, int32 *aParams[ nTestSecDevParams ]));

/* Typedef with same signature. */
typedef void (RIPCALL startupDongleTest_fn_t) RIPPROTO((void));

/* Perform security check and silently initialize variables. */
int32 RIPCALL startupDongleTestSilently RIPPROTO((void));
/* Typedef with same signature. */
typedef int32 (RIPCALL *startupDongleTestSilently_fn_t) RIPPROTO((void));

/* Report any errors resulting from previous security check. */
int32 RIPCALL startupDongleTestReport RIPPROTO((int32 fTestPassed));
/* Typedef with same signature. */
typedef void (RIPCALL *startupDongleTestReport_fn_t) RIPPROTO((int32 fTestPassed));

void RIPCALL endSecurityDevice RIPPROTO((void));
/* Typedef with same signature. */
typedef void (RIPCALL *endSecurityDevice_fn_t) RIPPROTO((void));

#ifdef __cplusplus
}
#endif


#endif /* protection for multiple inclusion */
