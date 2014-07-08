/* Copyright (C) 2008-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!src:zlibutil.h(EBDSDK_P.1) $
 * zlib utilities
 */

#ifndef __ZLIBUTIL_H__
#define __ZLIBUTIL_H__

#include "zlib.h"
#include "std.h"

/**
 * \file
 * \brief zlib utilities
 * \ingroup skinkit
 */

/**
 * \brief Wrapper to zlib deflateInit() which sets strm
 * to uses MemAlloc() / MemFree().
 */
extern int32 gg_deflateInit( z_streamp strm, int32 level );

/**
 * \brief Wrapper to zlib inflateInit() which sets strm
 * to uses MemAlloc() / MemFree().
 */
extern int32 gg_inflateInit( z_streamp strm );

/**
 * \brief Wrapper to zlib deflate() which first resets strm.
 * Expects dest to be big enough to hold compressed data, and returns
 * Z_BUF_ERROR if it is not.
 *
 * \return Z_OK if successful, or one of the zlib errors.
 */
extern int32 gg_deflate( z_streamp strm, uint8 * dest, uint32 * pDestLen, const uint8 * source, uint32 sourceLen );

/**
 * \brief As zlib compress() but uses MemAlloc() / MemFree().
 */
extern int32 gg_compress( uint8 * dest, uint32 * pDestLen, const uint8 * source, uint32 sourceLen );

/**
 * \brief As zlib compress2() but uses MemAlloc() / MemFree().
 */
extern int32 gg_compress2( uint8 * dest, uint32 * pDestLen, const uint8 * source, uint32 sourceLen, int32 level );

/**
 * \brief As zlib uncompress() but uses MemAlloc() / MemFree().
 */
extern int32 gg_uncompress( uint8 * dest, uint32 *pDestLen, const uint8 *source, uint32 sourceLen );

/**
 * \brief As zlib compressBound() but takes/returns uint32.
 */
extern uint32 gg_compressBound( uint32 sourceLen );


#endif

/* EOF zlibutil.h */
