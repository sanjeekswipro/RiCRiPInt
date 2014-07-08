/* $HopeName: SWcustomer!export:custlib.h(EBDSDK_P.1) $ */
/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __CUSTLIB_H__
#define __CUSTLIB_H__

/* This file provides the interface to the customer library. This allows
 * customer information to be returned from a customised library.
 */

/* Log stripped */

/*----------------------- Includes --------------------------------------- */


/* Dont include product.h here as custlib.h gets includes by v20 */
#include "std.h"

#include "extscrty.h" /* obfuscation for CustomerRipVersionNumber */


#ifdef __cplusplus
extern "C" {
#endif


/* ----------------------- Types ----------------------------------------- */


/*------------------------ Macros ---------------------------------------- */


#define HCMS_NTYPE_BAD          -1      /* rogue value */
#define HCMS_NTYPE_ICC          0       /* ICC Workflow */
#define HCMS_NTYPE_LITE         1       /* Lite HCMS */
#define HCMS_NTYPE_HCP          2       /* HCP */
#define HCMS_NTYPE_FULL         3       /* Full HCMS */
#define HCMS_NTYPE_TOTAL        4

/* ----------------------- Types ----------------------------------------- */


/* ---------------------- Public Macros ---------------------------------- */
  /* These macros are in place to allow building with either the custiface
   * or customer compounds.  They stooge into the customer.h #defines for
   * their information, so should only be used for compounds which are
   * otherwise customised anyway.
   */

#define CustomerMorisawaCustomerNumber() MORISAWA_CUSTOMER_NO

/* ---------------------- Public Functions ------------------------------- */

int32 CustomerOptionalStartupDlg(
#ifndef SW_NO_PROTOTYPES
                           void
#endif
                           );
int32 CustomerOptionalCopyrightDlg(
#ifndef SW_NO_PROTOTYPES
                           void
#endif
                           ) ;

uint32 CustomerHasLinotypeFonts(void);
uint32 CustomerHasBitstreamFonts(void);

int32 CustomerRipVersionNumber(
#ifndef SW_NO_PROTOTYPES
                           void
#endif
                           ) ;
uint8 *CustomerProductName(
#ifndef SW_NO_PROTOTYPES
                           void
#endif
                           ) ;
uint8 *CustomerCustomisationName(
#ifndef SW_NO_PROTOTYPES
                           void
#endif
                           ) ;
uint8 *CustomerLongProductName(
#ifndef SW_NO_PROTOTYPES
                           void
#endif
                               ) ;

char *CustomerCopyrightOemMessage(void);
char **CustomerCopyrightOemAdditional(void);

uint8 *ProductNameParam(
#ifndef SW_NO_PROTOTYPES
                           void
#endif
                           ) ;

void SetRipCustomerNumber(
#ifndef SW_NO_PROTOTYPES
                           uint32 NewRipCustomerNumber
#endif
                          ) ;

int32 RipCustomerNumber(
#ifndef SW_NO_PROTOTYPES
                           void
#endif
                       ) ;

uint8 * CustomerCMSName(
#ifndef SW_NO_PROTOTYPES
                           void
#endif
                               ) ;
uint8 * CustomerSecurityCMSName(
#ifndef SW_NO_PROTOTYPES
                           void
#endif
                               ) ;
uint8 * CustomerLongCMSName(
#ifndef SW_NO_PROTOTYPES
                           void
#endif
                               ) ; 

uint8 * CustomerProductMenu(
#ifndef SW_NO_PROTOTYPES
                           void
#endif
                               ) ;

uint8 * CustomerProductAbout(
#ifndef SW_NO_PROTOTYPES
                           void
#endif
                               ) ;
uint8 * CustomerProductMenuMnemonic(
#ifndef SW_NO_PROTOTYPES
                           void
#endif
                               ) ;
int32 CustomerLookupHCMSType(
#ifndef SW_NO_PROTOTYPES
                    uint8 * typeName
#endif
                                         );
uint8 * CustomerHCMSTypeName(
#ifndef SW_NO_PROTOTYPES
                          int32 typeNumber
#endif
                                          );
int32 WatermarkLicenseDuration
(
#ifndef SW_NO_PROTOTYPES
  void
#endif
);

#ifdef __cplusplus
}
#endif


#endif /* protection for multiple inclusion */
