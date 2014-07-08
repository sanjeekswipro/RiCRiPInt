/*
 * Copyright (c) 2007 Global Graphics Software Ltd. All Rights Reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * $HopeName: HQNzlib!export:zlib_redefs.h(EBDSDK_P.1) $
 */

#ifndef ZLIB_REDEFS_H_
#define ZLIB_REDEFS_H_

/* Add prefix to exported zlib functions to prevent name clashes when */
/* used in 3rd party code.                                             */
#define zlibVersion ggs_zlib_zlibVersion

#define deflate ggs_zlib_deflate
#define deflateEnd ggs_zlib_deflateEnd

#define inflate_ggs ggs_zlib_inflate_ggs
#define inflate ggs_zlib_inflate
#define inflateEnd ggs_zlib_inflateEnd

#define deflateSetDictionary ggs_zlib_deflateSetDictionary
#define deflateCopy ggs_zlib_deflateCopy
#define deflateReset ggs_zlib_deflateReset
#define deflateParams ggs_zlib_deflateParams
#define deflateTune ggs_zlib_deflateTune
#define deflateBound ggs_zlib_deflateBound
#define deflatePrime ggs_zlib_deflatePrime
#define deflateSetHeader ggs_zlib_deflateSetHeader

#define inflateSetDictionary ggs_zlib_inflateSetDictionary
#define inflateSync ggs_zlib_inflateSync
#define inflateCopy ggs_zlib_inflateCopy
#define inflateReset ggs_zlib_inflateReset
#define inflatePrime ggs_zlib_inflatePrime
#define inflateGetHeader ggs_zlib_inflateGetHeader
#define inflateBack ggs_zlib_inflateBack
#define inflateBackEnd ggs_zlib_inflateBackEnd

#define zlibCompileFlags ggs_zlib_zlibCompileFlags

#define compress ggs_zlib_compress
#define compress2 ggs_zlib_compress2
#define compressBound ggs_zlib_compressBound
#define uncompress ggs_zlib_uncompress

#define gzopen ggs_zlib_gzopen
#define gzdopen ggs_zlib_gzdopen
#define gzsetparams ggs_zlib_gzsetparams
#define gzread ggs_zlib_gzread
#define gzwrite ggs_zlib_gzwrite
#define gzprintf ggs_zlib_gzprintf
#define gzputs ggs_zlib_gzputs
#define gzgets ggs_zlib_gzgets
#define gzputc ggs_zlib_gzputc
#define gzgetc ggs_zlib_gzgetc
#define gzungetc ggs_zlib_gzungetc
#define gzflush ggs_zlib_gzflush
#define gzseek ggs_zlib_gzseek
#define gzrewind ggs_zlib_gzrewind
#define gztell ggs_zlib_gztell
#define gzeof ggs_zlib_gzeof
#define gzdirect ggs_zlib_gzdirect
#define gzclose ggs_zlib_gzclose
#define gzerror ggs_zlib_gzerror
#define gzclearerr ggs_zlib_gzclearerr

#define adler32 ggs_zlib_adler32
#define adler32_combine ggs_zlib_adler32_combine
#define crc32 ggs_zlib_crc32
#define crc32_combine ggs_zlib_crc32_combine

#define deflateInit_ ggs_zlib_deflateInit_
#define inflateInit_ ggs_zlib_inflateInit_
#define deflateInit2_ ggs_zlib_deflateInit2_
#define inflateInit2_ ggs_zlib_inflateInit2_
#define inflateBackInit_ ggs_zlib_inflateBackInit_

#define zError ggs_zlib_zError
#define inflateSyncPoint ggs_zlib_inflateSyncPoint
#define get_crc_table ggs_zlib_get_crc_table

#define _dist_code _ggs_dist_code
#define _length_code _ggs_length_code
#define _tr_align _ggs_tr_align
#define _tr_flush_block _ggs_tr_flush_block
#define _tr_init _ggs_tr_init
#define _tr_stored_block _ggs_tr_stored_block
#define _tr_tally _ggs_tr_tally
#define deflate_copyright ggs_deflate_copyright
#define inflate_copyright ggs_inflate_copyright
#define inflate_fast ggs_inflate_fast
#define inflate_table ggs_inflate_table
#define z_errmsg ggs_z_errmsg
#define zcalloc ggs_zcalloc
#define zcfree ggs_zcfree


#endif /* ZLIB_REDEFS_H_ */
