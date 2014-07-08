/* Copyright (C) 2008 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWskinkit!src:zlibutil.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

#include "zlibutil.h"

#include "mem.h"

/**
 * @file
 * @brief zlib utilities.
 */

static voidpf gg_mem_alloc( voidpf opaque, uInt items, uInt size )
{
  size_t cbSize = items * size;

  UNUSED_PARAM( voidpf, opaque );

  return MemAlloc( cbSize, FALSE, FALSE );
}


static void gg_mem_free( voidpf opaque, voidpf address )
{
  UNUSED_PARAM( voidpf, opaque );

  MemFree( address );
}


int32 gg_deflateInit( z_streamp strm, int32 level )
{
    strm->zalloc = gg_mem_alloc;
    strm->zfree = gg_mem_free;
    strm->opaque = Z_NULL;

    return (int32) deflateInit(strm, (int) level);
}


int32 gg_inflateInit( z_streamp strm )
{
    strm->zalloc = gg_mem_alloc;
    strm->zfree = gg_mem_free;
    strm->opaque = Z_NULL;

    return (int32) inflateInit(strm);
}


int32 gg_deflate( z_streamp strm, uint8 * dest, uint32 * pDestLen, const uint8 * source, uint32 sourceLen )
{
    int32 err;
    uLong destLen = (uLong) *pDestLen;
    uLong sLen = (uLong) sourceLen;

    err = (int32) deflateReset(strm);
    if (err != Z_OK) return err;

    strm->next_in = (Bytef *) source;
    strm->avail_in = (uInt) sLen;
#ifdef MAXSEG_64K
    /* Check for source > 64K on 16-bit machine: */
    if ((uLong)strm->avail_in != sLen) return Z_BUF_ERROR;
#endif
    strm->next_out = (Bytef *) dest;
    strm->avail_out = (uInt) destLen;
    if ((uLong)strm->avail_out != destLen) return Z_BUF_ERROR;

    err = (int32) deflate(strm, Z_FINISH);
    if (err != Z_STREAM_END) {
        return err == Z_OK ? Z_BUF_ERROR : err;
    }
    *pDestLen = (uint32) strm->total_out;

    return Z_OK;
}


int32 gg_compress( uint8 * dest, uint32 * pDestLen, const uint8 * source, uint32 sourceLen )
{
  return gg_compress2( dest, pDestLen, source, sourceLen, Z_DEFAULT_COMPRESSION );
}


int32 gg_compress2( uint8 * dest, uint32 * pDestLen, const uint8 * source, uint32 sourceLen, int32 level )
{
    z_stream stream;
    int32 err;
    uLong destLen = (uLong) *pDestLen;
    uLong sLen = (uLong) sourceLen;

    stream.next_in = (Bytef *) source;
    stream.avail_in = (uInt) sLen;
#ifdef MAXSEG_64K
    /* Check for source > 64K on 16-bit machine: */
    if ((uLong)stream.avail_in != sLen) return Z_BUF_ERROR;
#endif
    stream.next_out = (Bytef *) dest;
    stream.avail_out = (uInt) destLen;
    if ((uLong)stream.avail_out != destLen) return Z_BUF_ERROR;

    err = gg_deflateInit(&stream, (int) level);
    if (err != Z_OK) return err;

    err = (int32) deflate(&stream, Z_FINISH);
    if (err != Z_STREAM_END) {
        deflateEnd(&stream);
        return err == Z_OK ? Z_BUF_ERROR : err;
    }
    *pDestLen = (uint32) stream.total_out;

    err = (int32) deflateEnd(&stream);
    return err;
}


int32 gg_uncompress( uint8 * dest, uint32 * pDestLen, const uint8 * source, uint32 sourceLen )
{
    z_stream stream;
    int32 err;
    uLong destLen = (uLong) *pDestLen;
    uLong sLen = (uLong) sourceLen;

    stream.next_in = (Bytef *) source;
    stream.avail_in = (uInt) sLen;
    /* Check for source > 64K on 16-bit machine: */
    if ((uLong)stream.avail_in != sLen) return Z_BUF_ERROR;

    stream.next_out = (Bytef *) dest;
    stream.avail_out = (uInt) destLen;
    if ((uLong)stream.avail_out != destLen) return Z_BUF_ERROR;

    err = gg_inflateInit(&stream);
    if (err != Z_OK) return err;

    err = (int32) inflate(&stream, Z_FINISH);
    if (err != Z_STREAM_END) {
        inflateEnd(&stream);
        if (err == Z_NEED_DICT || (err == Z_BUF_ERROR && stream.avail_in == 0))
            return Z_DATA_ERROR;
        return err;
    }
    *pDestLen = (uint32) stream.total_out;

    err = (int32) inflateEnd(&stream);
    return err;
}


uint32 gg_compressBound( uint32 sourceLen )
{
  return (int32) compressBound((uLong) sourceLen);
}


/* EOF zlibutil.c */
