/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __MD5_H__
#define __MD5_H__

/* $HopeName: HQNmd5!export:md5.h(EBDSDK_P.1) $
 *
* Log stripped */

#ifdef __cplusplus
extern "C" {
#endif

#define MD5_OUTPUT_LEN (16)

/* Header for MD5 checksum calculation */

void md5_progressive(uint8* in,
                     uint32 len,
                     uint8  out[MD5_OUTPUT_LEN],
                     uint32 total_len,
                     uint32 first_data,
                     uint32 last_data);

#define md5( in, len, out ) md5_progressive( in, len, out, len, TRUE, TRUE )

#ifdef __cplusplus
}
#endif


#endif

/* end of md5.h */
