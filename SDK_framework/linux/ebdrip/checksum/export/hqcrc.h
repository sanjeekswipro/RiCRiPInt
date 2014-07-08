/* $HopeName: HQNchecksum!export:hqcrc.h(EBDSDK_P.1) $ *
 *
 * Copyright (C) 1994-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * Description:
 *   Cyclic Redundancy Checksum utility interface.
 */


#ifndef	__HQCRC_H__
#define	__HQCRC_H__

#include "std.h"

/** \brief Type of CRC functions. */
typedef uint32 (HQNCALL HqCRC_fn)(uint32, uint32 *, int32) ;

/**
 * \brief Declaration of (little-endian) checksum function exported by
 * HQNchecksum.
 */
extern uint32 HQNCALL HQCRCchecksum(uint32 crc, uint32 *data, int32 len);

/**
 * \brief Declaration of (big-endian) checksum function exported by
 * HQNchecksum.
 */
extern uint32 HQNCALL HQCRCchecksumreverse(uint32 crc, uint32 *data, int32 len);

#endif /* !__HQCRC_H__ */
