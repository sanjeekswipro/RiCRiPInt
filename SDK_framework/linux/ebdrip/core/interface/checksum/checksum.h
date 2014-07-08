/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_checksum!checksum.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 */

#ifndef __CHECKSUM_H__
#define __CHECKSUM_H__

#ifdef __cplusplus
extern "C" {
#endif

/* This macro computes a 2 decimal digit checksum for a 16-bit unsigned
   value stored in an int32 */

#define CHECKSUM( Value , Checksum , Feature ) MACRO_START \
  int32 _v = (Value);                    \
  Checksum = 0;                          \
  Checksum += 2 * (_v % 10); _v /= 10;   \
  Checksum += 3 * (_v % 10); _v /= 10;   \
  Checksum += 1 * (_v % 10); _v /= 10;   \
  Checksum += 2 * (_v % 10); _v /= 10;   \
  Checksum += 3 * (_v % 10); _v /= 10;   \
  Checksum = ((Checksum) + (Feature)) % 100; \
MACRO_END

#ifdef __cplusplus
}
#endif


#endif /* protection for multiple inclusion */
