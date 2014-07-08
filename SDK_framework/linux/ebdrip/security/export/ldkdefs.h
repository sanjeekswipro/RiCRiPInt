#ifndef __LDKDEFS_H__
#define __LDKDEFS_H__

/* Copyright (C) 2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * $HopeName: SWsecurity!export:ldkdefs.h(EBDSDK_P.1) $
 *
 */


/* Base feature ID for layered options */
#define LDK_BASE_OPTION_ID    700
#define LDKKEY_OPTION(_i_)    (LDK_BASE_OPTION_ID + (_i_))


/* HASP HL keys contain 2048 (0x0800) bytes of read-only memory.  Keys
 * for the Harlequin RIP use this memory thus:
 *
 * 0x0000 - 0x000F   Product ID (identifies key as being a Harlequin RIP key)
 * 0x0010 - 0x001F   Key layout info (initially just a version number)
 * 0x0020 - 0x007F   Product data (the traditional dongle data, though not in the same format)
 * 0x0080 - 0x00FF   Product identity (a human-readable announcement of what the key is for)
 * 0x0100 - 0x0103   Max threads limit
 * 0x0104 - 0x07FF   Unused
 *
 */
#define HASP_PRODUCT_DATA_FLAG_SIZE           0x0001
#define HASP_PRODUCT_DATA_VALUE_SIZE          0x0004


#define HASP_PRODUCT_ID_ADDRESS               0x0000
#define HASP_PRODUCT_ID_SIZE                  0x0010


#define HASP_LAYOUT_VERSION_ADDRESS           0x0010
#define HASP_LAYOUT_VERSION_SIZE              0x0010

#define HASP_LAYOUT_VERSION_FLAG_OFFSET       0x0000
#define HASP_LAYOUT_VERSION_FLAG_V1           0x01


#define HASP_PRODUCT_DATA_ADDRESS             0x0020
#define HASP_PRODUCT_DATA_SIZE                0x0060

#define HASP_PROTECTED_PLUGINS_FLAG_OFFSET    0x0000
#define HASP_POSTSCRIPT_DENIED_FLAG_OFFSET    0x0001
#define HASP_PDF_DENIED_FLAG_OFFSET           0x0002
#define HASP_GUI_DENIED_FLAG_OFFSET           0x0003
#define HASP_WATERMARK_FLAG_OFFSET            0x0005
#define HASP_USE_TIMER_OFFSET                 0x0006
#define HASP_PRODUCT_CODE_USES_BITS_OFFSET    0x0008
#define HASP_LLV_FLAG_OFFSET                  0x0009
#define HASP_HPS_DENIED_FLAG_OFFSET           0x000A
#define HASP_XPS_DENIED_FLAG_OFFSET           0x000B
#define HASP_APPLY_WATERMARK_FLAG_OFFSET      0x000C
#define HASP_TEAMWORK_FLAG_OFFSET             0x000D

#define HASP_DEMO_FLAG_OFFSET                 0x001E
#define HASP_UNLIMITED_RESOLUTION_FLAG_OFFSET 0x001F

#define HASP_PRODUCT_CODE_VALUE_OFFSET        0x0020
#define HASP_RIP_VERSION_VALUE_OFFSET         0x0024
#define HASP_LLV_LOCALES_VALUE_OFFSET         0x0028
#define HASP_RESOLUTION_LIMIT_VALUE_OFFSET    0x002C
#define HASP_CUSTOMER_NUMBER_VALUE_OFFSET     0x0030
#define HASP_SECURITY_NUMBER_VALUE_OFFSET     0x0034


#define HASP_PRODUCT_IDENTITY_ADDRESS         0x0080
#define HASP_PRODUCT_IDENTITY_SIZE            0x0080


#define HASP_MAX_THREADS_LIMIT_ADDRESS        0x0100
#define HASP_MAX_THREADS_LIMIT_SIZE           HASP_PRODUCT_DATA_VALUE_SIZE


#endif


/* Modification history:
* Log stripped */
