#ifndef __LDKVENDOR_H__
#define __LDKVENDOR_H__

/* Copyright (C) 2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * $HopeName: SWsecurity!export:ldkvendor.h(EBDSDK_P.1) $
 *
 */

#include "hasp_api.h"


/* Vendor related defines */
#if defined(LDKVENDOR_DEMOMA)

/* Defines for demo vendor code */
#define LDK_VENDOR_NAME              "DEMOMA"
#define LDK_VENDOR_ID                37515
#define LDK_QUOTED_VENDOR_ID         "37515"

/* Feature ID for RIP */
#define LDK_RIP_FEATURE_ID           379
#define LDK_QUOTED_RIP_FEATURE_ID    "379"
#define LDK_RIP_PRODUCT_ID           379
#define LDK_RIP_NAME                 "Harlequin  RIP"

#elif defined(LDKVENDOR_KZXQC)

/* Defines for GGS vendor code */
#define LDK_VENDOR_NAME              "KZXQC"
#define LDK_VENDOR_ID                109670
#define LDK_QUOTED_VENDOR_ID         "109670"

/* Feature ID for RIP */
#define LDK_RIP_FEATURE_ID           410
#define LDK_QUOTED_RIP_FEATURE_ID    "410"
#define LDK_RIP_PRODUCT_ID           410
#define LDK_RIP_NAME                 "Harlequin RIP"

#else
#error "Unknown / undefined LDKVENDOR_xxx in ldkvendor.h"
#endif


extern hasp_vendor_code_t vendor_code;


#endif


/* Modification history:
* Log stripped */
