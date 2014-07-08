/** \file
 * \ingroup images
 *
 * $HopeName: SWv20!src:imb32.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * API to 32bit image block storage abstraction
 */

#ifndef __IMB32_H__
#define __IMB32_H__

/**
 * Different styles of 32bit image data
 */
enum
{
  FLT0TO1 = 0,          /* 32bit float data in the range 0 to +1 */
  FLT0TO1WAS8BIT = 1,   /* 0 to +1 float data that was 8bit samples */
  FLTM4TOP4 = 2,        /* 32bit float data in the range -4 to +4 */
  FLTISBYTES = 3,       /* Debugging option, data is actually 8bit samples */
};

int32 imb32_compress(int32 style, float *src, uint32 width, uint32 height,
                     uint32 *dst, uint32 maxbytes);

Bool imb32_decompress(uint32 *src, uint32 bytes, float *dst, uint32 width,
                      uint32 height);

#endif /* __IMB32_H__ protection from multiple inclusion */

/* Log stripped */
