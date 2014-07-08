/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:rainstorm.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Rainstorm prototype header defines
 * ( See rainstorm.c source file for more Rainstorm details )
 *
 * N.B. THIS IS PROTOTYPE CODE THAT WILL BE THROWN AWAY
 *      WHEN MAIN DEVELOPMENT STARTS !
 */

#ifndef __RAINSTORM_H__
#define __RAINSTORM_H__

typedef struct RS_ID_BLOCK
{
  uint32 magic[2];
  uint32 version[2];
} RS_ID_BLOCK;

#define RS_MAGIC0 0x5261696E
#define RS_MAGIC1 0x0D0A890A

typedef struct RS_JOB_BLOCK
{
  uint32 id;
  uint32 meta_size;
} RS_JOB_BLOCK;

typedef struct RS_PAGE_BLOCK
{
  uint32 id;
  uint32 rasterStyle;
  uint32 pageWidth;
  uint32 pageHeight;
  uint8  colors, bits, spare1, spare2;
  uint8  bg[4];
  uint32 meta_size;
} RS_PAGE_BLOCK;

typedef struct RS_BAND_BLOCK
{
  uint32 y0, y1;
  uint32 size;
  Bool   nib;
  uint8 *cmds;
} RS_BAND_BLOCK;

#define RS_ABUTTING 0
#define RS_VERTICAL 1

#define RS_VAL4      1
#define RS_UNS8      2
#define RS_SIGN8     3
#define RS_OFF16     4
#define RS_SIZE16    5
#define RS_BYTE      6
#define RS_DBYTE     7
#define RS_NIBBLE    8
#define RS_DELTA4    9
#define RS_DELTA2p2  10

#endif /* protection for multiple inclusion */

/* ==========================================================================
 *
* Log stripped */
