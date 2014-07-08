/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxltags.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

#include "core.h"

#include "pclxltags.h"

uint8 xl_tag_table[256] = {
  XL_TT_WS,           /* 0x00 - NUL */
  XL_TT_NOTUSED,      /* 0x01 */
  XL_TT_NOTUSED,      /* 0x02 */
  XL_TT_NOTUSED,      /* 0x03 */
  XL_TT_NOTUSED,      /* 0x04 */
  XL_TT_NOTUSED,      /* 0x05 */
  XL_TT_NOTUSED,      /* 0x06 */
  XL_TT_NOTUSED,      /* 0x07 */
  XL_TT_NOTUSED,      /* 0x08 */
  XL_TT_WS,           /* 0x09 - HT */
  XL_TT_WS,           /* 0x0a - LF */
  XL_TT_WS,           /* 0x0b - VT */
  XL_TT_WS,           /* 0x0c - FF */
  XL_TT_WS,           /* 0x0d - CR */
  XL_TT_BINDING,      /* 0x0e */
  XL_TT_BINDING,      /* 0x0f */
  XL_TT_BINDING,      /* 0x10 */
  XL_TT_BINDING,      /* 0x11 */
  XL_TT_BINDING,      /* 0x12 */
  XL_TT_BINDING,      /* 0x13 */
  XL_TT_BINDING,      /* 0x14 */
  XL_TT_BINDING,      /* 0x15 */
  XL_TT_BINDING,      /* 0x16 */
  XL_TT_BINDING,      /* 0x17 */
  XL_TT_BINDING,      /* 0x18 */
  XL_TT_BINDING,      /* 0x19 */
  XL_TT_BINDING,      /* 0x1a */
  XL_TT_NOTUSED,      /* 0x1b - ESC */
  XL_TT_BINDING,      /* 0x1c */
  XL_TT_BINDING,      /* 0x1d */
  XL_TT_BINDING,      /* 0x1e */
  XL_TT_BINDING,      /* 0x1f */
  XL_TT_WS,           /* 0x20 - SPACE */
  XL_TT_BINDING,      /* 0x21 */
  XL_TT_BINDING,      /* 0x22 */
  XL_TT_BINDING,      /* 0x23 */
  XL_TT_BINDING,      /* 0x24 */
  XL_TT_BINDING,      /* 0x25 */
  XL_TT_BINDING,      /* 0x26 */
  XL_TT_BINDING,      /* 0x27 - ASCII binding */
  XL_TT_BINDING,      /* 0x28 - Big endian binding */
  XL_TT_BINDING,      /* 0x29 - Little endian binding */
  XL_TT_BINDING,      /* 0x2a */
  XL_TT_BINDING,      /* 0x2b */
  XL_TT_BINDING,      /* 0x2c */
  XL_TT_BINDING,      /* 0x2d */
  XL_TT_BINDING,      /* 0x2e */
  XL_TT_BINDING,      /* 0x2f */
  XL_TT_BINDING,      /* 0x30 */
  XL_TT_BINDING,      /* 0x31 */
  XL_TT_BINDING,      /* 0x32 */
  XL_TT_BINDING,      /* 0x33 */
  XL_TT_BINDING,      /* 0x34 */
  XL_TT_BINDING,      /* 0x35 */
  XL_TT_BINDING,      /* 0x36 */
  XL_TT_BINDING,      /* 0x37 */
  XL_TT_BINDING,      /* 0x38 */
  XL_TT_BINDING,      /* 0x39 */
  XL_TT_BINDING,      /* 0x3a */
  XL_TT_BINDING,      /* 0x3b */
  XL_TT_BINDING,      /* 0x3c */
  XL_TT_BINDING,      /* 0x3d */
  XL_TT_BINDING,      /* 0x3e */
  XL_TT_BINDING,      /* 0x3f */
  XL_TT_NOTUSED,      /* 0x40 */
  XL_TT_OPERATOR,     /* 0x41 */
  XL_TT_OPERATOR,     /* 0x42 */
  XL_TT_OPERATOR,     /* 0x43 */
  XL_TT_OPERATOR,     /* 0x44 */
  XL_TT_RESERVED,     /* 0x45 */
  XL_TT_OPERATOR,     /* 0x46 */
  XL_TT_OPERATOR,     /* 0x47 */
  XL_TT_OPERATOR,     /* 0x48 */
  XL_TT_OPERATOR,     /* 0x49 */
  XL_TT_RESERVED,     /* 0x4a */
  XL_TT_RESERVED,     /* 0x4b */
  XL_TT_RESERVED,     /* 0x4c */
  XL_TT_RESERVED,     /* 0x4d */
  XL_TT_RESERVED,     /* 0x4e */
  XL_TT_OPERATOR,     /* 0x4f */
  XL_TT_OPERATOR,     /* 0x50 */
  XL_TT_OPERATOR,     /* 0x51 */
  XL_TT_OPERATOR,     /* 0x52 */
  XL_TT_OPERATOR,     /* 0x53 */
  XL_TT_OPERATOR,     /* 0x54 */
  XL_TT_OPERATOR,     /* 0x55 */
  XL_TT_OPERATOR,     /* 0x56 */
  XL_TT_OPERATOR,     /* 0x57 */
  XL_TT_OPERATOR,     /* 0x58 */
  XL_TT_RESERVED,     /* 0x59 */
  XL_TT_RESERVED,     /* 0x5a */
  XL_TT_OPERATOR,     /* 0x5b */
  XL_TT_OPERATOR,     /* 0x5c */
  XL_TT_OPERATOR,     /* 0x5d */
  XL_TT_OPERATOR,     /* 0x5e */
  XL_TT_OPERATOR,     /* 0x5f */
  XL_TT_OPERATOR,     /* 0x60 */
  XL_TT_OPERATOR,     /* 0x61 */
  XL_TT_OPERATOR,     /* 0x62 */
  XL_TT_OPERATOR,     /* 0x63 */
  XL_TT_OPERATOR,     /* 0x64 */
  XL_TT_OPERATOR,     /* 0x65 */
  XL_TT_OPERATOR,     /* 0x66 */
  XL_TT_OPERATOR,     /* 0x67 */
  XL_TT_OPERATOR,     /* 0x68 */
  XL_TT_OPERATOR,     /* 0x69 */
  XL_TT_OPERATOR,     /* 0x6a */
  XL_TT_OPERATOR,     /* 0x6b */
  XL_TT_OPERATOR,     /* 0x6c */
  XL_TT_OPERATOR,     /* 0x6d */
  XL_TT_OPERATOR,     /* 0x6e */
  XL_TT_OPERATOR,     /* 0x6f */
  XL_TT_OPERATOR,     /* 0x70 */
  XL_TT_OPERATOR,     /* 0x71 */
  XL_TT_OPERATOR,     /* 0x72 */
  XL_TT_OPERATOR,     /* 0x73 */
  XL_TT_OPERATOR,     /* 0x74 */
  XL_TT_OPERATOR,     /* 0x75 */
  XL_TT_OPERATOR,     /* 0x76 */
  XL_TT_OPERATOR,     /* 0x77 */
  XL_TT_OPERATOR,     /* 0x78 */
  XL_TT_OPERATOR,     /* 0x79 */
  XL_TT_OPERATOR,     /* 0x7a */
  XL_TT_OPERATOR,     /* 0x7b */
  XL_TT_OPERATOR,     /* 0x7c */
  XL_TT_OPERATOR,     /* 0x7d */
  XL_TT_OPERATOR,     /* 0x7e */
  XL_TT_OPERATOR,     /* 0x7f */
  XL_TT_OPERATOR,     /* 0x80 */
  XL_TT_OPERATOR,     /* 0x81 */
  XL_TT_RESERVED,     /* 0x82 - Doc'd as BeginUserDefinedLineCap but not seen */
  XL_TT_RESERVED,     /* 0x83 - Doc'd as EndUserDefinedLineCap but not seen */
  XL_TT_OPERATOR,     /* 0x84 */
  XL_TT_OPERATOR,     /* 0x85 */
  XL_TT_OPERATOR,     /* 0x86 */
  XL_TT_RESERVED,     /* 0x87 */
  XL_TT_RESERVED,     /* 0x88 */
  XL_TT_RESERVED,     /* 0x89 */
  XL_TT_RESERVED,     /* 0x8a */
  XL_TT_RESERVED,     /* 0x8b */
  XL_TT_RESERVED,     /* 0x8c */
  XL_TT_RESERVED,     /* 0x8d */
  XL_TT_RESERVED,     /* 0x8e */
  XL_TT_RESERVED,     /* 0x8f */
  XL_TT_RESERVED,     /* 0x90 */
  XL_TT_OPERATOR,     /* 0x91 */
  XL_TT_OPERATOR,     /* 0x92 */
  XL_TT_OPERATOR,     /* 0x93 */
  XL_TT_OPERATOR,     /* 0x94 */
  XL_TT_OPERATOR,     /* 0x95 */
  XL_TT_OPERATOR,     /* 0x96 */
  XL_TT_OPERATOR,     /* 0x97 */
  XL_TT_OPERATOR,     /* 0x98 */
  XL_TT_OPERATOR,     /* 0x99 */
  XL_TT_RESERVED,     /* 0x9a */
  XL_TT_OPERATOR,     /* 0x9b */
  XL_TT_RESERVED,     /* 0x9c */
  XL_TT_OPERATOR,     /* 0x9d */
  XL_TT_OPERATOR,     /* 0x9e */
  XL_TT_OPERATOR,     /* 0x9f */
  XL_TT_OPERATOR,     /* 0xa0 */
  XL_TT_OPERATOR,     /* 0xa1 */
  XL_TT_OPERATOR,     /* 0xa2 */
  XL_TT_OPERATOR,     /* 0xa3 */
  XL_TT_RESERVED,     /* 0xa4 */
  XL_TT_RESERVED,     /* 0xa5 */
  XL_TT_RESERVED,     /* 0xa6 */
  XL_TT_RESERVED,     /* 0xa7 */
  XL_TT_OPERATOR,     /* 0xa8 */
  XL_TT_OPERATOR,     /* 0xa9 */
  XL_TT_RESERVED,     /* 0xaa */
  XL_TT_RESERVED,     /* 0xab */
  XL_TT_RESERVED,     /* 0xac */
  XL_TT_RESERVED,     /* 0xad */
  XL_TT_RESERVED,     /* 0xae */
  XL_TT_RESERVED,     /* 0xaf */
  XL_TT_OPERATOR,     /* 0xb0 */
  XL_TT_OPERATOR,     /* 0xb1 */
  XL_TT_OPERATOR,     /* 0xb2 */
  XL_TT_OPERATOR,     /* 0xb3 */
  XL_TT_OPERATOR,     /* 0xb4 */
  XL_TT_OPERATOR,     /* 0xb5 */
  XL_TT_OPERATOR,     /* 0xb6 */
  XL_TT_RESERVED,     /* 0xb7 */
  XL_TT_OPERATOR,     /* 0xb8 */
  XL_TT_OPERATOR,     /* 0xb9 */
  XL_TT_RESERVED,     /* 0xba */
  XL_TT_RESERVED,     /* 0xbb */
  XL_TT_RESERVED,     /* 0xbc */
  XL_TT_RESERVED,     /* 0xbd */
  XL_TT_RESERVED,     /* 0xbe */
  XL_TT_OPERATOR,     /* 0xbf */
  XL_TT_DATATYPE,     /* 0xc0 */
  XL_TT_DATATYPE,     /* 0xc1 */
  XL_TT_DATATYPE,     /* 0xc2 */
  XL_TT_DATATYPE,     /* 0xc3 */
  XL_TT_DATATYPE,     /* 0xc4 */
  XL_TT_DATATYPE,     /* 0xc5 */
  XL_TT_RESERVED,     /* 0xc6 - reserved for data type */
  XL_TT_RESERVED,     /* 0xc7 - reserved for data type */
  XL_TT_DATATYPE,     /* 0xc8 */
  XL_TT_DATATYPE,     /* 0xc9 */
  XL_TT_DATATYPE,     /* 0xca */
  XL_TT_DATATYPE,     /* 0xcb */
  XL_TT_DATATYPE,     /* 0xcc */
  XL_TT_DATATYPE,     /* 0xcd */
  XL_TT_RESERVED,     /* 0xce - reserved for data type */
  XL_TT_RESERVED,     /* 0xcf - reserved for data type */
  XL_TT_DATATYPE,     /* 0xd0 */
  XL_TT_DATATYPE,     /* 0xd1 */
  XL_TT_DATATYPE,     /* 0xd2 */
  XL_TT_DATATYPE,     /* 0xd3 */
  XL_TT_DATATYPE,     /* 0xd4 */
  XL_TT_DATATYPE,     /* 0xd5 */
  XL_TT_RESERVED,     /* 0xd6 - reserved for data type */
  XL_TT_RESERVED,     /* 0xd7 - reserved for data type */
  XL_TT_RESERVED,     /* 0xd8 - Doc'd as (xy) array but not seen */
  XL_TT_RESERVED,     /* 0xd9 - Doc'd as (xy) array but not seen */
  XL_TT_RESERVED,     /* 0xda - Doc'd as (xy) array but not seen */
  XL_TT_RESERVED,     /* 0xdb - Doc'd as (xy) array but not seen */
  XL_TT_RESERVED,     /* 0xdc - Doc'd as (xy) array but not seen */
  XL_TT_RESERVED,     /* 0xdd - Doc'd as (xy) array but not seen */
  XL_TT_RESERVED,     /* 0xde - reserved for data type */
  XL_TT_RESERVED,     /* 0xdf - reserved for data type */
  XL_TT_DATATYPE,     /* 0xe0 */
  XL_TT_DATATYPE,     /* 0xe1 */
  XL_TT_DATATYPE,     /* 0xe2 */
  XL_TT_DATATYPE,     /* 0xe3 */
  XL_TT_DATATYPE,     /* 0xe4 */
  XL_TT_DATATYPE,     /* 0xe5 */
  XL_TT_RESERVED,     /* 0xe6 - reserved for data type */
  XL_TT_RESERVED,     /* 0xe7 - reserved for data type */
  XL_TT_RESERVED,     /* 0xe8 - Doc'd as (xy, xy) box array but not seen */
  XL_TT_RESERVED,     /* 0xe9 - Doc'd as (xy, xy) box array but not seen */
  XL_TT_RESERVED,     /* 0xea - Doc'd as (xy, xy) box array but not seen */
  XL_TT_RESERVED,     /* 0xeb - Doc'd as (xy, xy) box array but not seen */
  XL_TT_RESERVED,     /* 0xec - Doc'd as (xy, xy) box array but not seen */
  XL_TT_RESERVED,     /* 0xed - Doc'd as (xy, xy) box array but not seen */
  XL_TT_RESERVED,     /* 0xee - reserved for data type */
  XL_TT_RESERVED,     /* 0xef - reserved for data type */
  XL_TT_RESERVED,     /* 0xf0 */
  XL_TT_RESERVED,     /* 0xf1 */
  XL_TT_RESERVED,     /* 0xf2 */
  XL_TT_RESERVED,     /* 0xf3 */
  XL_TT_RESERVED,     /* 0xf4 */
  XL_TT_RESERVED,     /* 0xf5 */
  XL_TT_RESERVED,     /* 0xf6 */
  XL_TT_RESERVED,     /* 0xf7 */
  XL_TT_ATTRIBUTE,    /* 0xf8 */
  XL_TT_NOTUSED,      /* 0xf9 */
  XL_TT_EMBEDDED,     /* 0xfa */
  XL_TT_EMBEDDED,     /* 0xfb */
  XL_TT_RESERVED,     /* 0xfc */
  XL_TT_RESERVED,     /* 0xfd */
  XL_TT_RESERVED,     /* 0xfe */
  XL_TT_RESERVED,     /* 0xff */
};

/* Log stripped */

/* EOF */
