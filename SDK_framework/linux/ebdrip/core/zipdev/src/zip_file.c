/** \file
 * \ingroup zipdev
 *
 * $HopeName: COREzipdev!src:zip_file.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implements logical files in a ZIP archive.
 */

#include "core.h"
#include "hqmemcpy.h"     /* HqMemCpy */
#include "hqmemcmp.h"     /* HqMemCmp */
#include "swcopyf.h"      /* swcopyf */
#include "swctype.h"      /* isdigit, tolower */
#include "swerrors.h"

#include "mm.h"           /* mm_alloc */
#include "mmcompat.h"     /* mm_alloc_with_header */
#include "often.h"        /* SwOftenUnsafe */
#include "monitor.h"

#include "zlib.h"         /* inflate */

#include "zipdev.h"       /* ZIPDEV_FILE_PREFIX */
#include "zip_util.h"     /* zutl_dev_bufsize */
#include "zip_file.h"


/** \brief File data extract buffer size. */
#define ZIP_FILE_BUFFER_SIZE  (16384)

/** \brief Default ZLIB memory level for its internal compression state.
 * This maps to approximately 256KiB.
 */
#define ZIP_MEM_DEFAULT       (8)

/** \brief The size of the read-ahead buffer used when reading stored archive
entries of unknown size in a streaming consumption mode. This is large enough to
hold a complete ZIP64 data descriptor, which is the largest pattern we try to
match, although because of the optional signature field, we only bother matching
the CRC and the compressed size.
*/
#define READ_AHEAD_BUFFER_SIZE (ZAR_ZIP64_DATADESC_RECSIZE - 4)

/**
 * \brief A physical file used as a piece of a logical file in a ZIP archive.
 *
 * \note
 * Each piece does not have to have the same compression method.
 * \note
 * ZIP file general flag is a uint16, so we store them in the bottom 16
 * bits of the file flags, and hold file processing flags in the top 16 bits.
 */
typedef struct ZIP_FILE_PIECE {
  dll_link_t    link;             /**< Piece file list link */
  uint32        number;           /**< Piece file sequence number */
  Hq32x2        file_pos;         /**< Position for next read from archive. */

  int32         compression;      /**< Compression used for file data. */

  HqU32x2       extracted;        /**< Bytes of file data extracted so far. */
  HqU32x2       compressed_left;  /**< Bytes of compressed data left to process. */
  HqU32x2       uncompressed_size; /**< Uncompressed file size. */
  Bool          zip64;            /** True if the piece is stored using zip64. */

  uint32        crc_32_calc;      /**< CRC32 checksum from extracted file data. */
  uint32        crc_32;           /**< CRC32 checksum from file header. */

  uint32        flags;            /**< Flags - ZIP general flags in lower 16 bits. */

  /* This buffer is only used when reading stored entries of unknown size in
  streaming consumption mode. */
  int32         read_ahead_size;
  uint8         readAheadBuffer[READ_AHEAD_BUFFER_SIZE];

  OBJECT_NAME_MEMBER
} ZIP_FILE_PIECE;

/** \brief ZIP file piece structure name used to generate hash checksum. */
#define ZIP_FILE_PIECE_OBJECT_NAME  "ZIP File Piece"

/** \brief zlib has reached the end of the compressed stream. */
#define ZIP_FILE_PIECE_FLATEEND (0x010000)

/*@-exportheader@*/
/** \brief Has the end of the Flate stream has been seen. */
extern
Bool zfp_flate_end(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_PIECE* p_piece);
#define zfp_flate_end(p)    (((p)->flags&ZIP_FILE_PIECE_FLATEEND) != 0)

/** \brief Should the data descriptor be read. */
extern
Bool zfp_data_desc(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_PIECE* p_piece);
#define zfp_data_desc(p)    (((p)->flags&ZIPFLG_USE_DATADESC) != 0)

/** \brief ZIP file piece compression. */
extern
int32 zfp_compression(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_PIECE* p_piece);
#define zfp_compression(p)  ((p)->compression)
/*@=exportheader@*/

/**
 * \brief A list of physical pieces to make up a logical file.
 */
typedef dll_list_t ZIP_FILE_PIECE_LIST;

/**
 * \brief File data extractor function type.
 */
typedef int32 (*ZIP_FILE_EXTRACTOR)(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*   p_file,
/*@out@*/ /*@notnull@*/
  uint8*      buff,
  int32       len);

/** \brief Size of name of file stored on underlying file device. */
#define ZIP_FILENAME_LEN  (16)

/**
 * \brief A logical file in a ZIP archive.
 *
 * The main file containing the ZIP format data is a ZIP archive, and the files
 * within the archive are ZIP files.  Therefore, the following file structure
 * refers to a file in the archive, not the archive file!
 *
 * \note
 * ZIP file date/time fields are in MS-DOS format as follows:
 * - Date word:
 *   - bits 0-4  day (1-31)
 *   - bits 5-8  month (1-12)
 *   - bits 9-15 year (four digit year minus 1980)
 * - Time word:
 *   - bits 0-4   2 second increments (0-29)
 *   - bits 5-10  minutes (0-59)
 *   - bits 11-15 hours (0-24)
 *   .
 * .
 * These are not converted to host specific file times since this is platform
 * dependent and this is the RIP.  Unless we create a DOS to OS date/time
 * conversion device.
 * They are packed into a single 4 byte integer with date in the high order word
 * and time in the low order word so that file date/times within the archive
 * compare sensibly.
 */
struct ZIP_FILE {
  dll_link_t    link;         /**< Logical file list link. */

  uint32        flags;        /**< ZIP file flags. */
/*@dependent@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive;    /**< ZIP archive file is extracted from. */

  ZIP_FILE_PIECE_LIST pieces; /**< List of physical file pieces. */
  uint32        next_piece;   /**< Number of next piece to be extracted. */
  uint32        last_piece;   /**< Number of last piece. */
  HqU32x2       piece_size;   /**< Size of last piece added to the file. */

/*@partial@*/
  z_stream      zlib_state;   /**< zlib state for decompression. */
/*@owned@*/ /*@null@*/
  uint8*        in_buffer;    /**< Buffer holding piece compressed data. */

  ZIP_FILE_PIECE* activePiece; /**< The piece the extractor is configured for. */
/*@temp@*/ /*@notnull@*/
  ZIP_FILE_EXTRACTOR extractor; /**< File data extractor function pointer. */

  HqU32x2       extracted;    /**< Total file data extracted so far. */
/*@dependent@*/ /*@notnull@*/
  ZIP_FILE_DEVICE* p_device;  /**< Device to extract the file data to. */
  DEVICE_FILEDESCRIPTOR fd;   /**< File descriptor to extract file data to. */

  HqU32x2       total_size;   /**< Total uncompressed file size. */
  uint32        date_time;    /**< Last mod date/time. */
  uint32        c_opens;      /**< Number of concurrent opens on the file. */

/*@dependent@*/ /*@null@*/
  ZIP_FILE*     p_next;       /**< Next in chain of all files - for filenameforall. */
  ZIP_FILE*     p_prev;       /**< Previous in chain of all files. */

  uint8         filename[ZIP_FILENAME_LEN]; /**< Name of file on file device. Must be NUL terminated. */
  ZIP_FILE_NAME name;         /**< File name, not NUL terminated. */
  ZIP_FILE_NAME normalised_name; /**< Normalised file name. */

  OBJECT_NAME_MEMBER
};


/** \brief ZIP file structure name used to generate hash checksum. */
#define ZIP_FILE_OBJECT_NAME  "ZIP File"

/** \brief ZIP file list structure name used to generate hash checksum. */
#define ZIP_FILE_LIST_OBJECT_NAME  "ZIP File List"


/** \brief The file is from a ZIP archive. */
#define ZIP_FILE_ZIPFILE      (0x01)
/** \brief The whole file has been extracted. */
#define ZIP_FILE_COMPLETE     (0x02)
/** \brief Do CRC32 validation on extracted file. */
#define ZIP_FILE_CRCCHECK     (0x04)
/** \brief File has been opened for exclusive access. */
#define ZIP_FILE_EXCL         (0x08)
/** \brief Number of last piece is known. */
#define ZIP_FILE_LAST_KNOWN   (0x10)
/** \brief Last piece has been processed. */
#define ZIP_FILE_LAST_DONE    (0x20)
/** \brief File is/has been extracted and exists on disk. */
#define ZIP_FILE_ON_DISK      (0x40)
/** \brief zlib has been initialised. */
#define ZIP_FILE_ZLIB_ACTIVE  (0x80)
/** \brief Extracted file data is being flushed. */
#define ZIP_FILE_FLUSHING     (0x0100)
/** \brief Archive file data is being skipped over. */
#define ZIP_FILE_SKIPPING     (0x0200)


/*@-exportheader@*/
/** \brief Has the extracted file been created on disk. */
extern
Bool zfl_on_disk(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE* p_file);
#define zfl_on_disk(p)      (((p)->flags&ZIP_FILE_ON_DISK) != 0)

/** \brief Is the file from a ZIP archive? */
extern
Bool zfl_zipfile(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE* p_file);
#define zfl_zipfile(p)      (((p)->flags&ZIP_FILE_ZIPFILE) != 0)

/** \brief Has all the file data been extracted. */
extern
Bool zfl_complete(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE* p_file);
#define zfl_complete(p)     (((p)->flags&ZIP_FILE_COMPLETE) != 0)

/** \brief Has the file been opened for exclusive access. */
extern
Bool zfl_exclusive(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE* p_file);
#define zfl_exclusive(p)    (((p)->flags&ZIP_FILE_EXCL) != 0)

/** \brief Has the last piece of the file been seen. */
extern
Bool zfl_crcheck(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE* p_file);
#define zfl_crcheck(p)      (((p)->flags&ZIP_FILE_CRCCHECK) != 0)

/** \brief Has the last piece of the file been seen. */
extern
Bool zfl_last_set(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE* p_file);
#define zfl_last_set(p)     (((p)->flags&ZIP_FILE_LAST_KNOWN) != 0)

/** \brief Has the last piece of the file been extracted. */
extern
Bool zfl_last_done(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE* p_file);
#define zfl_last_done(p)    (((p)->flags&ZIP_FILE_LAST_DONE) != 0)

/** \brief Has the zlib state been initialised. */
extern
Bool zfl_zlib_init(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE* p_file);
#define zfl_zlib_init(p)    (((p)->flags&ZIP_FILE_ZLIB_ACTIVE) != 0)

/** \brief Is the extracted file data being flushed. */
extern
Bool zfl_flushing(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE* p_file);
#define zfl_flushing(p)     (((p)->flags&ZIP_FILE_FLUSHING) != 0)

/** \brief Is the extracted file data being skipped. */
extern
Bool zfl_skipping(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE* p_file);
#define zfl_skipping(p)     (((p)->flags&ZIP_FILE_SKIPPING) != 0)

/** \brief Return size of storage used for filename.
 *
 * Includes an additional byte so a terminating NUL can be appended.
 */
extern
Bool zfl_filename_size(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE_NAME*  p_name);
#define zfl_filename_size(p)  ((p)->namelength + 1)
/*@=exportheader@*/


/**
 * \brief Initialise logical file piece state.
 *
 * \param[out] p_piece
 * Pointer to logical file piece to initialise.
 * \param[in] p_archive
 * Pointer to archive containing file piece.
 * \param[in] p_info
 * Pointer to file information.
 * \param[in] number
 * Sequence number of piece for logical file.
 */
static
void zfp_init(
/*@out@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_FILE_PIECE* p_piece,
/*@in@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_FILE_INFO*  p_info,
/*@in@*/ /*@dependent@*/ /*@notnull@*/
  uint32          number)
{
  NAME_OBJECT(p_piece, ZIP_FILE_PIECE_OBJECT_NAME);

  HQASSERT((p_piece != NULL),
           "zfp_init: NULL file piece pointer");
  HQASSERT((p_archive != NULL),
           "zfp_init: NULL ZIP archive pointer");
  HQASSERT((p_info != NULL),
           "zfp_init: NULL file information pointer");

  /* Initialize file piece list link */
  DLL_RESET_LINK(p_piece, link);

  /* The piece number */
  p_piece->number = number;

  /* Offset of local file header */
  Hq32x2FromU32x2(&p_piece->file_pos, &p_info->offset);

  /* File compressed and extracted sizes */
  Hq32x2FromInt32(&p_piece->extracted, 0);
  p_piece->compressed_left = p_info->compressed;
  p_piece->uncompressed_size = p_info->uncompressed;

  /* Is the file using zip64? */
  p_piece->zip64 = p_info->zip64;

  /* Compression used */
  p_piece->compression = p_info->compression;

  /* Set up CRC32 checksumming */
  p_piece->crc_32 = p_info->crc_32;
  p_piece->crc_32_calc = crc32(0, Z_NULL, 0);

  /* File data flags */
  p_piece->flags = p_info->flags;
  if ( !zar_streamed(p_archive) ) {
    /* File is being created from the central directory file header so we don't
     * need to read the data descriptor to get the crc-32 and final file size.
     */
    p_piece->flags &= ~ZIPFLG_USE_DATADESC;
  }

} /* zfp_init */


/**
 * \brief Create a new file piece.
 *
 * \param[in] p_info
 * Pointer to file information.
 * \param[in] p_archive
 * Pointer to archive containing file piece.
 * \param[in] number
 * Pointer to number of piece.
 *
 * \return
 * Pointer to new logical file piece if created ok, else \c NULL.
 */
static /*@only@*/ /*@null@*/
ZIP_FILE_PIECE* zfp_create(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_INFO*  p_info,
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
  uint32          number)
{
  ZIP_FILE_PIECE* p_piece;

  /* Allocate file piece data struct */
  p_piece = mm_alloc(mm_pool_temp, sizeof(ZIP_FILE_PIECE), MM_ALLOC_CLASS_ZIP_FILE_PIECE);
  if ( p_piece == NULL ) {
    return FAILURE(NULL);
  }

  /* Initialise new file piece */
  zfp_init(p_piece, p_archive, p_info, number);

  return(p_piece);

} /* zfp_create */


/**
 * \brief Release any resources used by a file piece.
 *
 * \param[in] p_piece
 * Pointer to file piece.
 */
static
void zfp_destroy(
/*@in@*/ /*@only@*/ /*@notnull@*/
  ZIP_FILE_PIECE* p_piece)
{
  HQASSERT((p_piece != NULL),
           "zfp_destroy: NULL file piece pointer");
  HQASSERT((!DLL_IN_LIST(p_piece, link)),
           "zfp_destroy: file piece still in list");

  VERIFY_OBJECT(p_piece, ZIP_FILE_PIECE_OBJECT_NAME);

  UNNAME_OBJECT(p_piece);

  mm_free(mm_pool_temp, p_piece, sizeof(ZIP_FILE_PIECE));

} /* zfp_destroy */


/* Calculate hash value for file name. */
uint32 zfl_name_hash(
/*@in@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_FILE_NAME*  p_name,
  Bool            f_ignore_case)
{
  uint8 buffer[LONGESTFILENAME];

  HQASSERT((p_name != NULL),
           "zfl_name_hash: NULL file name pointer");
  HQASSERT((p_name->name != NULL),
           "zfl_name_hash: NULL name pointer");
  HQASSERT((p_name->namelength > 0),
           "zfl_name_hash: invalid name length");

  /* Hash is based on lower case version if ignoring case */
  if ( f_ignore_case ) {
    zutl_strlower(p_name->name, p_name->namelength, buffer);
    return(zutl_strhash(buffer, p_name->namelength));
  }

  return(zutl_strhash(p_name->name, p_name->namelength));

} /* zfl_name_hash */


/**
 * \brief Make copy of a file name.
 *
 * The caller should allocate an appropriate amount of storage for the copy of
 * the file name and set dest.name to it.  There must be 1 extra byte of storage
 * so a terminating NUL can be added so the filename can be easily printed as a
 * C string in any debug.
 *
 * \param[in] dest
 * Destination for copy of file name.
 * \param[in] src
 * File name to be copied.
 */
static
void zfl_copy_filename(
/*@out@*/ /*@notnull@*/
  ZIP_FILE_NAME*  dest,
/*@in@*/ /*@notnull@*/
  ZIP_FILE_NAME*  src)
{
  HQASSERT((dest != NULL),
           "zfl_copy_filename: NULL pointer dest file name");
  HQASSERT((dest->name != NULL),
           "zfl_copy_filename: NULL dest file name pointer");
  HQASSERT((src != NULL),
           "zfl_copy_filename: NULL pointer dest file name");
  HQASSERT((src != NULL),
           "zfl_copy_filename: NULL pointer src file name");

  dest->namelength = src->namelength;
  /* Nul terminate the filename so can be used in debug printing */
  dest->name[dest->namelength] = '\0';
  HqMemCpy(dest->name, src->name, src->namelength);

} /* zfl_copy_filename */


/**
 * \brief Extract file data from an archive that has been STOREd.
 *
 * \param[in] p_file
 * Pointer to logical file.
 * \param[out] buff
 * Pointer to buffer to write extracted data to.
 * \param[in] len
 * Length of buffer to write extracted data to.
 *
 * \return
 * The number of bytes read, or -1 on error.
 */
static
Bool zfl_extract_store(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*     p_file,
/*@out@*/ /*@notnull@*/
  uint8*        buff,
  int32         len)
{
  ZIP_FILE_PIECE* p_piece;
  HqU32x2 remaining;

  HQASSERT((p_file != NULL),
           "zfl_extract_store: NULL file pointer");
  HQASSERT((!DLL_LIST_IS_EMPTY(&p_file->pieces)),
           "zfl_extract_store: no piece to extract from");
  HQASSERT((buff != NULL),
           "zfl_extract_store: NULL buffer pointer");
  HQASSERT((len > 0),
           "zfl_extract_store: invalid buffer length");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  p_piece = DLL_GET_HEAD(&p_file->pieces, ZIP_FILE_PIECE, link);

  /* Make sure we don't read beyond end of the file data */
  HqU32x2Subtract(&remaining, &p_piece->uncompressed_size, &p_piece->extracted);
  if ( HqU32x2CompareUint32(&remaining, len) < 0 ) {
    /* len cannot be greater than MAXINT32 and remaining is less than len, so
     * can convert remaining to len without checking it actually fits */
    HqU32x2ToUint32(&remaining, (uint32*)&len);
  }

  if ( len == 0 ) {
    /* All file data has been extracted */
    return(0);
  }

  /* Read file data from archive */
  return(zar_read_raw(p_file->p_archive, buff, len));

} /* zfl_extract_store */

/** Read a byte from the passed 'archive' using the passed piece's read-ahead
buffer. Returns false on error.

ORIGINAL COMMENT
If the end of file has been reached once the byte has been read, 'endOfFile'
will be set to true, otherwise it will be false. Thus, this method will always
read at least one byte for any single piece. This means that it is not possible
to read a zero-sized piece from an archive.

Detecting the end of a stored piece uses a heuristic method which would return
an immediate EOF for any stored piece which started with a number of leading
zeros (12 or 20 bytes worth depending on zip32/64). I think this is more likely
than a stored piece of zero length, and therefore the code is written to avoid
this false EOF condition, at the cost of barfing on zero-sized stored entries.

UPDATED COMMENT
A stored piece with a zero length has been seen in the wild, from MS's MXDW
driver - request 63369.  The function has been changed to test for EOF before
reading the first byte.  As noted above, this means stored files starting with
12/20 zero bytes (i.e. without a descriptor signature) will appear truncated.
If they also appear in the wild, we will need a plan B (possibly, after an all
zero data descriptor there is a valid record signature - no garbage data before
the next record).
*/
static
Bool readByteUsingReadAheadBuffer(
  ZIP_FILE_PIECE* piece,
  ZIP_ARCHIVE*    archive,
  uint8*          byte,
  Bool*           endOfFile)
{
#define DATADESC_SIG 0x08074b50
  uint32 crc_32;
  uint8* buffer = piece->readAheadBuffer;
  uint8* local_buffer = buffer;
  Bool signaturePresent;

  *endOfFile = FALSE;

  /* Check for a matching CRC (looking after any optional signature) followed by
   * a compressed data size matching how much we have read so far.  We currently
   * ignore the uncompressed data size.
   */

  crc_32 = READ_LONG(buffer);
  signaturePresent = (crc_32 == DATADESC_SIG);
  if ( signaturePresent ) {
    local_buffer += 4;
    crc_32 = READ_LONG(local_buffer);
  }

  if ( crc_32 == piece->crc_32_calc ) {
    if ( piece->zip64 ) {
      HqU32x2 size;
      READ_LONGLONG(&size, local_buffer + 4);
      *endOfFile = (HqU32x2Compare(&size, &piece->extracted) == 0);

    } else {
      uint32 sizeInDataDesc = READ_LONG(local_buffer + 4);
      *endOfFile = (HqU32x2CompareUint32(&piece->extracted, sizeInDataDesc) == 0);
    }

    if ( *endOfFile ) {
      piece->crc_32 = piece->crc_32_calc;

      /* Consume the uncompressed size (or the remaining 4 bytes of it for zip64
       * archives) if a signature was present. */
      if ( signaturePresent ) {
        uint8 discardBuffer[4];
        if (zar_read_raw(archive, discardBuffer, 4) != 4)
          return FALSE;
      }
      return(TRUE);
    }
  }

  /* Return first byte in the buffer, updating the CRC and the total extracted.
   * For non-ZIP64 files, the total extracted should not exceed 4GB */
  *byte = buffer[0];
  piece->crc_32_calc = crc32(piece->crc_32_calc, byte, 1);
  HqU32x2AddUint32(&piece->extracted, &piece->extracted, 1);
  if ( !piece->zip64 ) {
    uint32 size;
    if ( !HqU32x2ToUint32(&piece->extracted, &size) ) {
      return(FALSE);
    }
  }

  /* Update read ahead buffer - shuffle buffer down and read next byte */
  HqMemMove(buffer, &buffer[1], piece->read_ahead_size - 1);
  if ( zar_read_raw(archive, &buffer[piece->read_ahead_size - 1], 1) != 1 ) {
    return(FALSE);
  }

  return TRUE;
}

/**
 * \brief Extract file data from an archive that has been STOREd, and whose size
 *        is not known (because the archive was created by a streaming producer).
 *
 * \param[in] p_file
 * Pointer to logical file.
 * \param[out] buff
 * Pointer to buffer to write extracted data to.
 * \param[in] len
 * Length of buffer to write extracted data to.
 *
 * \return
 * The number of bytes read, or -1 on error.
 */
static int32 zfl_extract_store_unknown_size(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*     p_file,
/*@dependent@*/ /*@notnull@*/
  uint8*        buff,
  int32         len)
{
  int32 count;
  Bool eof;
  ZIP_FILE_PIECE* p_piece;
  ZIP_ARCHIVE* p_archive;

  HQASSERT((p_file != NULL), "zfl_extract_store_unknown_size: NULL file pointer");
  HQASSERT((!DLL_LIST_IS_EMPTY(&p_file->pieces)),
           "zfl_extract_store_unknown_size: NULL current file data pointer");
  HQASSERT((buff != NULL), "zfl_extract_store_unknown_size: NULL buffer pointer");
  HQASSERT((len > 0), "zfl_extract_store_unknown_size: invalid buffer length");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  p_piece = DLL_GET_HEAD(&p_file->pieces, ZIP_FILE_PIECE, link);
  p_archive = p_file->p_archive;

  /* The uncompressed size remains at zero until the whole file has been read. */
  if ( !Hq32x2IsZero(&p_piece->uncompressed_size) ) {
    return(0);
  }

  /* Read ahead length is size of data descriptor less the signature */
  p_piece->read_ahead_size = (p_piece->zip64 ? ZAR_ZIP64_DATADESC_RECSIZE : ZAR_DATADESC_RECSIZE) - 4;

  if ( Hq32x2IsZero(&p_piece->extracted) ) {
    /* First read - fill the read-ahead buffer. */
    if ( zar_read_raw(p_archive, p_piece->readAheadBuffer,
                      p_piece->read_ahead_size) != p_piece->read_ahead_size ) {
      return FALSE;
    }
  }

  count = 0;
  do {
    if ( !readByteUsingReadAheadBuffer(p_piece, p_archive, buff++, &eof) ) {
      return(-1);
    }
    if ( eof ) {
      /* Only set the uncompressed size now. */
      p_piece->uncompressed_size = p_piece->extracted;
      break;
    }
    count++;
  } while ( --len > 0 );

  return(count);
}

/**
 * \brief Extract file data from an archive that has been deFLATEd.
 *
 * \param[in] p_file
 * Pointer to logical file.
 * \param[out] buff
 * Pointer to buffer to write extracted data to.
 * \param[in] len
 * Length of buffer to write extracted data to.
 *
 * \return
 * Number of bytes extracted, or \c -1 if an error occurred extracting file data.
 */
static
int32 zfl_extract_deflate(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*     p_file,
/*@dependent@*/ /*@notnull@*/
  uint8*        buff,
  int32         len)
{
  int32       retcode;
  int32       buf_len;
  int32       bytes_read;
  ZIP_FILE_PIECE* p_piece;

  HQASSERT((p_file != NULL),
           "zfl_extract_deflate: NULL file pointer");
  HQASSERT((!DLL_LIST_IS_EMPTY(&p_file->pieces)),
           "zfl_extract_store: NULL current file data pointer");
  HQASSERT((buff != NULL),
           "zfl_extract_deflate: NULL buffer pointer");
  HQASSERT((len > 0),
           "zfl_extract_deflate: invalid buffer length");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  p_piece = DLL_GET_HEAD(&p_file->pieces, ZIP_FILE_PIECE, link);

  /* Quick exit when already reached stream end */
  if ( zfp_flate_end(p_piece) ) {
    return(0);
  }

  /* Setup output buffer */
  p_file->zlib_state.next_out = buff;
  p_file->zlib_state.avail_out = len;
  bytes_read = 0;

  do {
    /* If there is more compressed to read and zlib has read all we have given
     * it so far, then read more compressed data from the archive
     */
    if ( (HqU32x2CompareUint32(&p_piece->compressed_left, 0) == 1) &&
         (p_file->zlib_state.avail_in == 0) ) {
      int32 compressedLeft;

      if ( HqU32x2CompareUint32(&p_piece->compressed_left, MAXINT32) > 0 ) {
        compressedLeft = MAXINT32;
      } else {
        HqU32x2ToUint32(&p_piece->compressed_left, (uint32*)&compressedLeft);
      }

      buf_len = min(zar_bufsize(p_file->p_archive), compressedLeft);

      p_file->zlib_state.next_in = p_file->in_buffer;

      /*@-nullpass@*/
      bytes_read = zar_read_raw(p_file->p_archive, p_file->in_buffer, buf_len);
      /*@=nullpass@*/
      if ( bytes_read < 0 ) {
        /* Error doing the read */
        return FAILURE(-1);
      }
      if ( bytes_read == 0 ) {
        /* No more data when expected. */
        retcode = Z_DATA_ERROR;
        break;
      }

      /* Reduce amount of compressed data left to read */
      HqU32x2SubtractUint32(&p_piece->compressed_left, &p_piece->compressed_left,
                            bytes_read);

      /* Let zlib know how much flate data there is */
      p_file->zlib_state.avail_in = bytes_read;
    }
    /* Inflate as much compressed file data as possible */
    retcode = inflate(&p_file->zlib_state, Z_SYNC_FLUSH);
  } while ( (retcode == Z_OK) && (p_file->zlib_state.avail_out > 0) );

  switch ( retcode ) {
  case Z_STREAM_END:
    /* Note we have reached the end of compressed file data */
    p_piece->flags |= ZIP_FILE_PIECE_FLATEEND;
    /*@fallthrough@*/
  case Z_OK:
    bytes_read = len - p_file->zlib_state.avail_out;
    break;

  case Z_BUF_ERROR:
  case Z_NEED_DICT:
    HQFAIL("zfl_extract_deflate: unexpected return from inflate()");
    /*@fallthrough@*/
  case Z_DATA_ERROR:
  case Z_STREAM_ERROR:
  case Z_MEM_ERROR:
    return FAILURE(-1);
  }

  return(bytes_read);

} /* zfl_extract_deflate */

/**
 * \brief Extract file data from an archive that has been deFLATEd, but whose
 *        compressed and uncompressed size is unknown.
 *
 * \param[in] p_file
 * Pointer to logical file.
 * \param[out] buff
 * Pointer to buffer to write extracted data to.
 * \param[in] len
 * Length of buffer to write extracted data to.
 *
 * \return
 * Number of bytes extracted, or \c -1 if an error occurred extracting file data.
 */
static
int32 zfl_extract_deflate_unknown_size(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*     p_file,
/*@dependent@*/ /*@notnull@*/
  uint8*        buff,
  int32         len)
{
  int32 retcode;
  int32 bytes_read;
  uint8 byte;
  ZIP_FILE_PIECE* p_piece;

  HQASSERT((p_file != NULL),
           "zfl_extract_deflate_unknown_size: NULL file pointer");
  HQASSERT((!DLL_LIST_IS_EMPTY(&p_file->pieces)),
           "zfl_extract_deflate_unknown_size: NULL current file data pointer");
  HQASSERT((buff != NULL),
           "zfl_extract_deflate_unknown_size: NULL buffer pointer");
  HQASSERT((len > 0),
           "zfl_extract_deflate_unknown_size: invalid buffer length");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  p_piece = DLL_GET_HEAD(&p_file->pieces, ZIP_FILE_PIECE, link);

  /* Quick exit when already reached stream end */
  if ( zfp_flate_end(p_piece) ) {
    return(0);
  }

  /* Setup input/output buffer */
  p_file->zlib_state.avail_in = 0;
  p_file->zlib_state.next_out = buff;
  p_file->zlib_state.avail_out = len;
  bytes_read = 0;

  do {
    /* If there is more compressed to read and zlib has read all we have given
     * it so far, then read more compressed data from the archive
     */
    if (p_file->zlib_state.avail_in == 0) {
      bytes_read = zar_read_raw(p_file->p_archive, &byte, 1);

      if ( bytes_read < 0 ) {
        /* Error doing the read */
        return FAILURE(-1);
      }
      if ( bytes_read == 0 ) {
        /* No more data when expected. */
        retcode = Z_DATA_ERROR;
        break;
      }

      p_file->zlib_state.next_in = &byte;
      p_file->zlib_state.avail_in = 1;
    }
    /* Inflate as much compressed file data as possible */
    retcode = inflate(&p_file->zlib_state, Z_SYNC_FLUSH);
  } while ( (retcode == Z_OK) && (p_file->zlib_state.avail_out > 0) );

  switch ( retcode ) {
  case Z_STREAM_END:
    /* Note we have reached the end of compressed file data */
    p_piece->flags |= ZIP_FILE_PIECE_FLATEEND;
    /*@fallthrough@*/
  case Z_OK:
    bytes_read = len - p_file->zlib_state.avail_out;
    break;

  case Z_BUF_ERROR:
  case Z_NEED_DICT:
    HQFAIL("zfl_extract_deflate: unexpected return from inflate()");
    /*@fallthrough@*/
  case Z_DATA_ERROR:
  case Z_STREAM_ERROR:
  case Z_MEM_ERROR:
    return FAILURE(-1);
  }

  return(bytes_read);
}

/**
 * \brief Setup logical piece extraction state for new piece.
 *
 * \param[in,out] p_file
 * Pointer to logical file to setup for extracting from the next piece.
 * \param[in] p_piece
 * Pointer to piece we want to extract.
 *
 * \return
 * \c TRUE if setup file for extraction successfully, else \c FALSE.
 */
static
Bool zfl_setup_piece(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
/*@in@*/ /*@null@*/ /*@dependent@*/
  ZIP_FILE_PIECE* p_piece)
{
  Bool zlibAvailable;
  Bool zlibRequired = FALSE;

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  p_file->activePiece = p_piece;

  zlibAvailable = (p_file->flags & ZIP_FILE_ZLIB_ACTIVE) != 0;

  /* Will the next piece need a zlib state */
  if ( p_piece != NULL ) {
    VERIFY_OBJECT(p_piece, ZIP_FILE_PIECE_OBJECT_NAME);

    /* Find start of next piece data */
    if ( !zar_streamed(p_file->p_archive)) {
      if ( !zar_locate_file(p_file->p_archive, &p_piece->file_pos) )
        return FALSE;
    }

    /* Is zlib required to extract the next piece? */
    zlibRequired = zfp_compression(p_piece) == ZIPCOMP_DEFLATE;

    /* Choose the extractor function. */
    if (zlibRequired) {
      /* Next piece is flated. If we are streaming and don't know the size of the
      entry, use the method which detects stream termination. */
      if (zar_streamed(p_file->p_archive) && zfp_data_desc(p_piece))
        p_file->extractor = zfl_extract_deflate_unknown_size;
      else
        p_file->extractor = zfl_extract_deflate;
    }
    else {
      /* Next piece is stored. If we are streaming and don't know the size of the
      entry, use the method which detects stream termination. */
      if (zar_streamed(p_file->p_archive) && zfp_data_desc(p_piece))
        p_file->extractor = zfl_extract_store_unknown_size;
      else
        p_file->extractor = zfl_extract_store;
    }
  }

  if (zlibRequired) {
    if (zlibAvailable) {
      /* Reset zlib. */
      if (inflateReset(&p_file->zlib_state) != Z_OK)
        return FAILURE(FALSE);
    }
    else {
      /* Initialise zlib. */
      p_file->in_buffer = mm_alloc(mm_pool_temp, zar_bufsize(p_file->p_archive),
                                   MM_ALLOC_CLASS_ZIP_FILE_BUFFER);
      if (p_file->in_buffer == NULL)
        return FAILURE(FALSE);

      p_file->zlib_state.next_in = p_file->zlib_state.next_out = Z_NULL;
      p_file->zlib_state.avail_in = p_file->zlib_state.avail_out = 0;

      /* Although not obviously documented, this is the correct way to
      initialise zlib to decode flate streams. From a Google on newsgroups:
      inflate() requires a ZLib stream as input. If you want to decompress a
      pure deflate stream (for example a GZip compressed file), you have to use
      _inflateInit2(). You have to pass the size of the sliding window as
      negative value. In most cases you have to use -15 because this results in
      a sliding window size of 32 KB which is the default setting for deflate.
      */
      if ( inflateInit2(&p_file->zlib_state, -MAX_WBITS) != Z_OK )
        return FAILURE(FALSE);

      p_file->flags |= ZIP_FILE_ZLIB_ACTIVE;
    }
  }
  else {
    if (zlibAvailable) {
      /* Release zlib. */
      if (inflateEnd(&p_file->zlib_state) != Z_OK)
        return FAILURE(FALSE);

      mm_free(mm_pool_temp, p_file->in_buffer, zar_bufsize(p_file->p_archive));
      p_file->in_buffer = NULL;
      p_file->flags &= ~ZIP_FILE_ZLIB_ACTIVE;
    }
  }

  return TRUE;
} /* zfl_setup_piece */


/**
 * \brief Move onto the next logical file piece for extraction if possible.
 *
 * If the previous piece was the last piece of the logical file then the
 * returned piece pointer is \c NULL and the file is flagged as having extracted
 * from the last piece, testable with zfl_last_done().
 *
 * \param[in] p_file
 * Pointer to logical file.
 * \param[out] pp_piece
 * Pointer to returned pointer to next piece to continue extracting with.
 *
 * \return
 * \c TRUE if set up logical file for extracting from the piece successfully,
 * else \c FALSE.
 */
static
Bool zfl_next_piece(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
/*@out@*/ /*@notnull@*/
  ZIP_FILE_PIECE** pp_piece)
{
  ZIP_FILE_PIECE* p_piece_prev;
  ZIP_FILE_PIECE* p_piece_next;
  ZIP_FILE_PIECE* p_piece;

  HQASSERT((p_file != NULL),
           "zfl_next_piece: NULL file pointer");
  HQASSERT((!DLL_LIST_IS_EMPTY(&p_file->pieces)),
           "zfl_next_piece: empty piece list");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  p_piece_prev = DLL_GET_HEAD(&p_file->pieces, ZIP_FILE_PIECE, link);
  HQASSERT(HqU32x2Compare(&p_piece_prev->extracted,
                          &p_piece_prev->uncompressed_size) == 0,
           "zfl_next_piece: have not finished extracting previous piece");

  p_piece_next = NULL;
  if ( !zfl_last_set(p_file) || (p_piece_prev->number < p_file->last_piece) ) {
    /* Haven't seen the last piece yet, move onto next available */
    p_file->next_piece++;

    p_piece = DLL_GET_NEXT(p_piece_prev, ZIP_FILE_PIECE, link);
    if ( (p_piece != NULL) && (p_piece->number == p_file->next_piece) ) {
      /* Next piece is the one expected */
      p_piece_next = p_piece;
    }

  } else { /* Just done last piece */
    p_file->flags |= ZIP_FILE_LAST_DONE;
  }

  /* Return what we think the next piece will be - possible not seen yet */
  *pp_piece = p_piece_next;

  if ( !zfl_setup_piece(p_file, p_piece_next) ) {
    return(FALSE);
  }

  /* Finished with the previous piece - remove from list and destroy if not
   * skipping the file data */
  DLL_REMOVE(p_piece_prev, link);
  if ( !zfl_skipping(p_file) ) {
    zfp_destroy(p_piece_prev);
  }

  return(TRUE);

} /* zfl_next_piece */


/**
 * \brief Get a pointer to the logical file's current piece being extracted.
 *
 * \param[in] p_file
 * Pointer to logical file.
 *
 * \return
 * Pointer to current piece if there is one and it is the one expected, else \c NULL.
 */
static /*@null@*/ /*@dependent@*/
ZIP_FILE_PIECE* zfl_current_piece(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE* p_file)
{
  ZIP_FILE_PIECE* p_piece;

  HQASSERT((p_file != NULL),
           "zfl_current_piece: NULL file pointer");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  p_piece = DLL_GET_HEAD(&p_file->pieces, ZIP_FILE_PIECE, link);

  if ( p_piece != NULL ) {
    if ( p_piece->number > p_file->next_piece ) {
      return(NULL);
    }
    HQASSERT((p_piece->number == p_file->next_piece),
             "zfl_current_piece: old piece still on list");
  }

  return(p_piece);

} /* zfl_current_piece */


/* Add physical file data piece to logical file. */
Bool zfl_add_piece(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
/*@in@*/ /*@notnull@*/
  ZIP_FILE_INFO*  p_info,
  uint32          number,
  int32           type)
{
  ZIP_FILE_PIECE* p_piece;
  ZIP_FILE_PIECE* p_new_piece = NULL;

#ifdef INSTRUMENT_ZIP
  monitorf((uint8*)"ZIP: Adding piece number %d for file %.*s\n", number,
           p_file->name.namelength, p_file->name.name);
#endif

  HQASSERT((p_file != NULL),
           "zfl_add_piece: NULL file pointer");
  HQASSERT((!zpt_directory(type)),
           "zfl_add_piece: adding directory");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  if ( zfl_last_set(p_file) &&
       (zpt_last(type) || (number > p_file->last_piece)) ) {
    /* Already seen last piece or number is larger than that of last piece! */
    return FAILURE(FALSE);
  }

  /* Find position in piece list for this piece */
  p_piece = DLL_GET_HEAD(&p_file->pieces, ZIP_FILE_PIECE, link);
  while ( (p_piece != NULL) && (p_piece->number < number) ) {
    p_piece = DLL_GET_NEXT(p_piece, ZIP_FILE_PIECE, link);
  }
  /* Check not duplicate or last with trailing pieces */
  if ( (p_piece != NULL) &&
       (zpt_last(type) || (number == p_piece->number)) ) {
    return FAILURE(FALSE);
  }

  /* Create new file piece and add to piece list */
  p_new_piece = zfp_create(p_info, p_file->p_archive, number);
  if ( p_new_piece == NULL ) {
    return FAILURE(FALSE);
  }
  if ( p_piece != NULL ) {
    /* Intervening piece - add before next higher one */
    DLL_ADD_BEFORE(p_piece, p_new_piece, link);

  } else { /* Highest numbered piece so far - add at end */
    DLL_ADD_TAIL(&p_file->pieces, p_new_piece, link);
    if ( zpt_last(type) ) {
      /* Record seen last (or only) piece for file */
      p_file->flags |= ZIP_FILE_LAST_KNOWN;
      p_file->last_piece = p_new_piece->number;
    }
  }

  /* Record increase in logical file size due to piece */
  HqU32x2Add(&p_file->piece_size, &p_file->piece_size, &p_new_piece->uncompressed_size);
  HqU32x2Add(&p_file->total_size, &p_file->total_size, &p_new_piece->uncompressed_size);

  return(TRUE);

} /* zfl_add_piece */

/**
 * \brief Initialise logical file state.
 *
 * \param[out] p_file
 * Pointer to logical file state to initialise.
 * \param[in] f_zipfile
 * Logical file data comes from a ZIP archive.
 * \param[in] p_archive
 * Pointer to ZIP archive containing logical file data.
 * \param[in] p_device
 * Pointer to the device to write logical file data to.
 * \param[in] date_time
 * \param[in] f_crccheck
 * Do a CRC check on extracted archive file data.
 * \param[in] name
 * Original name for logical file.
 * \param[in] normalised_name
 * Normalised name for logical file.
 * \param[in] f_names_same
 * Original and normalised names are the same.
 * \param[in] device_name
 * Name for logical file data on the file device.
 */
static
void zfl_init(
/*@out@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_FILE*       p_file,
  Bool            f_zipfile,
/*@in@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@dependent@*/ /*@null@*/
  ZIP_FILE_DEVICE* p_device,
/*@in@*/ /*@dependent@*/ /*@notnull@*/
  uint8*          device_name,
  uint32          date_time,
  Bool            f_crccheck,
/*@in@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_FILE_NAME*  name,
/*@in@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_FILE_NAME*  normalised_name,
  Bool            f_names_same)
{
  HQASSERT((p_file != NULL),
           "zfl_init: ZIP file pointer NULL");
  HQASSERT((name != NULL),
           "zfl_init: ZIP file name pointer NULL");
  HQASSERT((!f_zipfile || (p_archive != NULL)),
           "zfl_init: ZIP archive pointer NULL for ZIP file");
  HQASSERT((normalised_name != NULL),
           "zfl_init: ZIP file normalised name pointer NULL");

  NAME_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  /* Initialise file list link */
  DLL_RESET_LINK(p_file, link);

  /* Not in list of all files yet */
  p_file->p_next = NULL;
  p_file->p_prev = NULL;

  /* Reset file extractor state list */
  DLL_RESET_LIST(&p_file->pieces);
  p_file->next_piece = p_file->last_piece = 0;
  Hq32x2FromInt32(&p_file->piece_size, 0);

  /* Clear file flags */
  p_file->flags = 0;

  if ( f_zipfile ) {
    /* Note if file is from a ZIP archive */
    p_file->flags |= ZIP_FILE_ZIPFILE;
  }

  if ( zfl_zipfile(p_file) ) {
    /* The archive to extract the file from */
    p_file->p_archive = p_archive;

    /* Setup zlib memory allocator interface */
    p_file->zlib_state.zalloc = zutl_zlib_alloc;
    p_file->zlib_state.zfree = zutl_zlib_free;
    p_file->zlib_state.opaque = NULL;

    /* Initial extractor state is for stored data */
    p_file->in_buffer = NULL;
    p_file->activePiece = NULL;
    p_file->extractor = zfl_extract_store;

    /* Amount of file data extracted */
    Hq32x2FromInt32(&p_file->extracted, 0);

    if ( f_crccheck ) {
      /* Note if need to do crc32 check on extraction */
      p_file->flags |= ZIP_FILE_CRCCHECK;
    }
  }

  /* Size of file */
  Hq32x2FromInt32(&p_file->total_size, 0);

  /* File creation/modification date time */
  p_file->date_time = date_time;

  /* And the device to write the extracted file to */
  p_file->p_device = p_device;
  p_file->fd = -1;
  p_file->c_opens = 0;

  HQASSERT((strlen_int32((char*)device_name) < ZIP_FILENAME_LEN),
           "zfl_init: ZIP file device filename too long");
  HqMemCpy(p_file->filename, device_name, strlen_int32((char*)device_name) + 1);

  /* Copy the file name */
  zfl_copy_filename(&p_file->name, name);

  /* If the normalised name is different it is stored after the name */
  if ( f_names_same ) {
    p_file->normalised_name = p_file->name;
  } else {
    p_file->normalised_name.name = p_file->name.name + zfl_filename_size(&p_file->name);
    zfl_copy_filename(&p_file->normalised_name, normalised_name);
  }

} /* zfl_init */


/* Add a new logical file from a ZIP archive to a list of existing files. */
/*@null@*/ /*@dependent@*/
ZIP_FILE* zfl_new(
/*@in@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_FILE_LIST*  p_list,
/*@in@*/ /*@null@*/
  ZIP_FILE_NAME*  name,
/*@in@*/ /*@null@*/
  ZIP_FILE_NAME*  normalised_name,
  uint32          date_time,
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE_DEVICE* p_device,
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  uint8*          device_filename,
  Bool            f_zipfile,
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_ARCHIVE*    p_archive,
  Bool            f_crccheck)
{
  ZIP_FILE* p_file;
  Bool f_names_same;
  mm_size_t alloc_size[2] ;
  mm_addr_t alloc_result[2];
  mm_alloc_class_t alloc_class[2];

  HQASSERT((name != NULL),
           "zfl_new: NULL filename pointer");
  HQASSERT((normalised_name != NULL),
           "zfl_new: NULL normalised filename pointer");
  HQASSERT((!f_zipfile || (p_archive != NULL)),
           "zfl_new: NULL archive pointer");
  HQASSERT((p_device != NULL),
           "zfl_new: NULL device pointer");

  VERIFY_OBJECT(p_list, ZIP_FILE_LIST_OBJECT_NAME);

  /* If the original and normalised names are the same only keep one copy and
   * have both name pointers point to it. */
  f_names_same = (HqMemCmp(name->name, name->namelength,
                              normalised_name->name, normalised_name->namelength) == 0) ;

  /* Allocate memory for file structure and names. */
  alloc_size[0] = sizeof(ZIP_FILE);
  alloc_class[0] = MM_ALLOC_CLASS_ZIP_FILE;
  alloc_size[1] = zfl_filename_size(name);
  if ( !f_names_same ) {
    alloc_size[1] += zfl_filename_size(normalised_name);
  }
  alloc_class[1] = MM_ALLOC_CLASS_ZIP_FILE_NAME;
  if ( MM_FAILURE == mm_alloc_multi_hetero(mm_pool_temp, 2, alloc_size,
                                           alloc_class, alloc_result) ) {
    return FAILURE(NULL);
  }
  p_file = alloc_result[0];
  p_file->name.name = alloc_result[1];

  /* Initialise the file structure */
  zfl_init(p_file, f_zipfile, p_archive, p_device, device_filename, date_time,
           f_crccheck, name, normalised_name, f_names_same);

  /* New file - add to head of file list. */
  /** \todo If archive is streamed, most likely want filename just added so head
   *        of the list is good.  However, if seekable and we dump the central
   *        directory into the table we would want the early files which are now at
   *        the end of the list.  So, could make list update dependent on
   *        archive being streamed or seekable. */
  DLL_ADD_HEAD(&p_list->files, p_file, link);

  return(p_file);

} /* zfl_new */


/**
 * \brief Close the file stream used for writing extracted file data.
 *
 * \param[in] p_file
 * Pointer to logical file.
 */
static
void zfl_close_internal(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE* p_file)
{
  HQASSERT((p_file != NULL),
           "zfl_close_internal: NULL file pointer");
  HQASSERT((!zfl_skipping(p_file)),
           "zfl_close_internal: closing file being skipped");
  HQASSERT((p_file->fd >= 0),
           "zfl_close_internal: internal file descriptor invalid");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  /* Close file on the underlying file device */
  (void)(*theICloseFile(zfd_device(p_file->p_device)))(zfd_device(p_file->p_device), p_file->fd);
  p_file->fd = -1;

} /* zfl_close_internal */


/**
 * \brief Free memory allocated for the file name.
 *
 * \param[in] p_file
 * Pointer to logical file.
 */
static
void zfl_free_filename(
/*@in@*/ /*@notnull@*/
  ZIP_FILE*     p_file)
{
  int32 name_len;

  HQASSERT((p_file != NULL),
           "zfl_free_filename: NULL file pointer");
  HQASSERT((p_file->name.name != NULL),
           "zfl_free_filename: NULL file name pointer");

  /* Free file names. Remember that filenames have a NUL added so can be easily
   * printed.
   */
  name_len = zfl_filename_size(&p_file->name);
  if ( p_file->normalised_name.name != p_file->name.name ) {
    name_len += zfl_filename_size(&p_file->normalised_name);
  }
  mm_free(mm_pool_temp, p_file->name.name, name_len);

} /* zfl_free_filename */


/**
 * \brief Free off all pieces for a file from an archive.
 *
 * Releases all resources in use from processing the pieces.
 *
 * \param[in] p_file
 * Pointer to logical file.
 */
static
void zfl_free_pieces(
/*@in@*/ /*@notnull@*/
  ZIP_FILE*   p_file)
{
  int32           res;
/*@owned@*/
  ZIP_FILE_PIECE* p_piece;

  HQASSERT((p_file != NULL),
           "zfl_free_pieces: NULL file pointer");
  HQASSERT((zfl_zipfile(p_file)),
           "zfl_free_pieces: file is not from archive");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  /* Free off any remaining pieces for the file */
  p_piece = DLL_GET_HEAD(&p_file->pieces, ZIP_FILE_PIECE, link);
  while ( p_piece != NULL ) {
    DLL_REMOVE(p_piece, link);
    zfp_destroy(p_piece);
    p_piece = DLL_GET_HEAD(&p_file->pieces, ZIP_FILE_PIECE, link);
  }

  /* Release any zlib resources if in use */
  if ( zfl_zlib_init(p_file) ) {
    res = inflateEnd(&p_file->zlib_state);
    HQASSERT((res == Z_OK),
             "zfl_free_pieces: failed to end zlib state");
  }

  /* Release zlib input buffer if allocated */
  if ( p_file->in_buffer != NULL ) {
    mm_free(mm_pool_temp, p_file->in_buffer, zar_bufsize(p_file->p_archive));
    p_file->in_buffer = NULL;
  }

} /* zfl_free_pieces */


/**
 * \brief Destroy all resources used by a logical file.
 *
 * Destroy all current and pending file pieces, tidy up zlib state and input
 * buffer if current piece needed it, close the stream for writing extracted
 * data if not already closed before deleting the extracted file, and finally
 * free off the logical file structure.
 *
 * \param[in] p_file
 * Pointer to logical file.
 *
 * \return
 * \c TRUE if logical file destroyed ok, else \c FALSE.
 */
static
Bool zfl_destroy(
/*@in@*/ /*@notnull@*/ /*@only@*/
  ZIP_FILE*     p_file)
{
  Bool  status = TRUE;

  HQASSERT((p_file != NULL),
           "zfl_destroy: NULL file pointer");
  HQASSERT((!DLL_IN_LIST(p_file, link)),
           "zfl_destroy: file still in list");
  HQASSERT((p_file->c_opens == 0),
           "zfl_destroy: not all opens have been closed");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  if ( zfl_zipfile(p_file) ) {
    /* Destroy any remaining archive pieces */
    zfl_free_pieces(p_file);

    /* Close extracted file stream if still open */
    if ( zfl_on_disk(p_file) && !zfl_complete(p_file) ) {
      zfl_close_internal(p_file);
    }
  }

  /* Delete the file if it exists on disk */
  if ( zfl_on_disk(p_file) ) {
    status = (*theIDeleteFile(zfd_device(p_file->p_device)))(zfd_device(p_file->p_device), p_file->filename) == 0;
    p_file->flags &= ~ZIP_FILE_ON_DISK;
  }

  /* Free of the file name */
  zfl_free_filename(p_file);

  /* Free file structure */
  UNNAME_OBJECT(p_file);
  mm_free(mm_pool_temp, p_file, sizeof(ZIP_FILE));

  return(status);

} /* zfl_destroy */


/**
 * \brief Extract an amount file data from an archive.
 *
 * Extract up to the requested amount of logical file data from the archive.  As
 * many file pieces that are available are read to achieve the requested byte
 * count.
 *
 * \param[in] p_file
 * Pointer to logical file.
 * \param[in] bytes
 * Number of bytes to extract.
 *
 * \return
 * \c TRUE if successfully extracted file data, else \c FALSE.
 */
static
Bool zfl_extract_len(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*     p_file,
  uint32        bytes)
{
  int32           bytes_read;
  uint8           buffer[ZIP_FILE_BUFFER_SIZE];
  ZIP_FILE_PIECE* p_piece;

  HQASSERT((p_file != NULL),
           "zfl_extract_len: NULL file pointer");
  HQASSERT((zfl_zipfile(p_file)),
           "zfl_extract_len: extracting file not in ZIP.");
  HQASSERT((!zfl_complete(p_file)),
           "zfl_extract_len: extracting to complete file");
  HQASSERT((bytes > 0),
           "zfl_extract_len: invalid size to extract");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  p_piece = zfl_current_piece(p_file);
  if ( p_piece == NULL ) {
    /* No file piece to extract from! */
    return FAILURE(FALSE);
  }

  /* Setup the piece for extraction if it's not already. */
  if (p_file->activePiece != p_piece)
    zfl_setup_piece(p_file, p_piece);

  /* Reposition on the archive to continue reading */
  if ( !zar_streamed(p_file->p_archive) ) {
    /* On seekable archives position on piece file data */
    if ( !zar_set_pos(p_file->p_archive, &p_piece->file_pos) ) {
      return FAILURE(FALSE);
    }
  }

  do {
    /* Extract a buffer load of file data from the archive */
    bytes_read = (*p_file->extractor)(p_file, buffer, ZIP_FILE_BUFFER_SIZE);
    if ( bytes_read < 0 ) {
      return FAILURE(FALSE);
    }

    if ( bytes_read == 0 ) {
      /* Read piece data descriptor if required */
      if ( zfp_data_desc(p_piece) ) {
        /* Note that zfl_extract_store_unknown_size() reads the data descriptor
        as part of its normal operation, so we don't need to read it again. */
        if (p_file->extractor != zfl_extract_store_unknown_size) {
          /* Reached EOF on piece file data */
          if ( p_piece->zip64 ) {
            ZIP_ZIP64_DATA_DESC data_desc;
            if ( !zar_read_zip64_data_desc(p_file->p_archive, &data_desc) )
              return(FALSE);
            p_piece->crc_32 = data_desc.crc_32;
            p_piece->uncompressed_size = data_desc.uncompressed_size;

          } else {
            ZIP_DATA_DESC data_desc;
            if ( !zar_read_data_desc(p_file->p_archive, &data_desc) )
              return(FALSE);
            p_piece->crc_32 = data_desc.crc_32;
            HqU32x2FromUint32(&p_piece->uncompressed_size, data_desc.uncompressed_size);
          }
        }
      }

      /* Do CRC32 check on piece if requested */
      if ( zfl_crcheck(p_file) &&
           (p_piece->crc_32 != p_piece->crc_32_calc) ) {
        return FAILURE(FALSE);
      }

      /* Move onto next piece, if there is one */
      if ( !zfl_next_piece(p_file, &p_piece) ) {
        return(FALSE);
      }

      if ( zfl_last_done(p_file) ) {
        /* That was the last piece - logical file is now complete. */
        p_file->flags |= ZIP_FILE_COMPLETE;
        if ( !zfl_skipping(p_file) ) {
          zfl_close_internal(p_file);
        }

      } else if ( p_piece != NULL ) {
        /* There is another piece we can continue extracting from */
        continue;
      } /* else we have not got the next piece yet */

      HQASSERT((!zfl_complete(p_file) || DLL_LIST_IS_EMPTY(&p_file->pieces)),
               "zfl_extract_len: remaining pieces after end of logical file");
      break;
    }

    /* Update CRC32 if requested - note that zfl_extract_store_unknown_size()
       updates the crc as part of it's normal operation. */
    if ( zfl_crcheck(p_file) &&
         p_file->extractor != zfl_extract_store_unknown_size ) {
      p_piece->crc_32_calc = crc32(p_piece->crc_32_calc, buffer, bytes_read);
    }
    if ( !zfl_flushing(p_file) ) {
      /* And write extracted data to the logical file if not flushing */
      if ( (*theIWriteFile(zfd_device(p_file->p_device)))(zfd_device(p_file->p_device), p_file->fd,
                                                          buffer, bytes_read) <= 0 ) {
        return FAILURE(FALSE);
      }
    }

    /* Tickle the RIP */
    SwOftenUnsafe();

    /* Update amount of piece and logical file that has been extracted.
       Note that zfl_extract_store_unknown_size updates the extracted amount as
       part of its normal operation. */
    if ( p_file->extractor != zfl_extract_store_unknown_size ) {
      HqU32x2AddUint32(&p_piece->extracted, &p_piece->extracted, bytes_read);
    }

    HqU32x2AddUint32(&p_file->extracted, &p_file->extracted, bytes_read);

    bytes -= (uint32)bytes_read;
  } while ( bytes > 0 );

  if ( !zfl_complete(p_file) && !zar_streamed(p_file->p_archive) ) {
    /* Remember were we got to on the read */
    if ( (p_piece != NULL) &&
         !zar_get_pos(p_file->p_archive, &p_piece->file_pos) ) {
      return FAILURE(FALSE);
    }
  }

  return(TRUE);

} /* zfl_extract_len */


/* Finish extracting data for the current file piece of a logical file. */
Bool zfl_extract(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file)
{
  ZIP_FILE_PIECE* p_piece;

  HQASSERT((p_file != NULL),
           "zfl_extract: NULL file pointer");
  HQASSERT((zfl_zipfile(p_file)),
           "zfl_extract: extracting file not in ZIP.");
  HQASSERT((zar_streamed(p_file->p_archive)),
           "zfl_extract: extracting file data on seekable archive");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  if ( zfl_complete(p_file) ) {
    /* Nothing to do if the file is already complete */
    return(TRUE);
  }

  p_piece = zfl_current_piece(p_file);
  if ( p_piece == NULL ) {
    /* May have already extracted the whole piece */
    return(TRUE);
  }

  /* Force read of whole current piece */
  return(zfl_extract_len(p_file, MAXUINT32));

} /* zfl_extract */


/**
 * \brief Extract all the data for a ZIP archive file.
 *
 * \param[in] p_file
 * Pointer to logical file.
 *
 * \return
 * \c TRUE if all the data is successfully extracted, else \c FALSE.
 */
static
Bool zfl_extract_all(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file)
{
  HQASSERT((p_file != NULL),
           "zfl_extract_all: NULL file pointer");
  HQASSERT((zfl_zipfile(p_file)),
           "zfl_extract_all: extracting file not in ZIP.");
  HQASSERT((!zfl_complete(p_file)),
           "zfl_extract_all: extracting for file already extracted");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  /* Keep extracting from the file until complete */
  do {
    if ( !zfl_extract_len(p_file, MAXUINT32) ) {
      return FAILURE(FALSE);
    }
  } while ( !zfl_complete(p_file) );

  return(TRUE);

} /* zfl_extract_all */

/** Query the passed file to see if more data must be read before 'length' bytes
can be read.

The result is provided in the passed 'moreRequired'. Returns FALSE on error.
*/
static
Bool zfl_more_extraction_required(
  ZIP_FILE* p_file,
  DEVICE_FILEDESCRIPTOR fd,
  int32 len,
  Bool* moreRequired)
{
  Hq32x2 file_pos;
  HqU32x2 temp;

  HQASSERT((p_file != NULL),
           "zfl_more_extraction_required: NULL file pointer");
  HQASSERT((fd >= 0),
           "zfl_more_extraction_required: invalid file descriptor");
  HQASSERT((len > 0),
           "zfl_more_extraction_required: invalid required data count");
  HQASSERT(moreRequired != NULL,
           "zfl_more_extraction_required: NULL file pointer");

  *moreRequired = FALSE;

  /* Check if read requires more of the file to be extracted */
  Hq32x2FromInt32(&file_pos, 0);
  if ( !(*theISeekFile(zfd_device(p_file->p_device)))(zfd_device(p_file->p_device),
                                                      fd, &file_pos, SW_INCR) ) {
    return FAILURE(FALSE);
  }
  Hq32x2AddInt32(&file_pos, &file_pos, len);
  HqU32x2From32x2(&temp, &file_pos);
  if ( HqU32x2Compare(&temp, &p_file->extracted) <= 0 ) {
    /* Don't need to extract any more data */
    return TRUE;
  }

  *moreRequired = TRUE;
  return TRUE;
}

/**
 * \brief Check if required number of bytes from ZIP archive file are available.
 *
 * Ensures that sufficient data has been extracted from the ZIP archive item
 * prior to any read or write.
 *
 * \param[in] p_file
 * Pointer to logical file.
 * \param[in] fd
 * File descriptor of file device stream used for extracted data.
 * \param[in] len
 * Count of bytes to be available after extraction.
 *
 * \return
 * \c TRUE if requested number of bytes are available, else \c FALSE.
 */
static
Bool zfl_extract_more(
/*@in@*/ /*@notnull@*/
  ZIP_FILE*   p_file,
  DEVICE_FILEDESCRIPTOR fd,
  int32       len)
{
  uint32    length;
  Hq32x2    file_pos;
  HqU32x2   u_file_pos;

  HQASSERT((p_file != NULL),
           "zfl_extract_more: NULL file pointer");
  HQASSERT((fd >= 0),
           "zfl_extract_more: invalid file descriptor");
  HQASSERT((len > 0),
           "zfl_extract_more: invalid required data count");

  /* Check if read requires more of the file to be extracted */
  Hq32x2FromInt32(&file_pos, 0);
  if ( !(*theISeekFile(zfd_device(p_file->p_device)))(zfd_device(p_file->p_device),
                                                      fd, &file_pos, SW_INCR) ) {
    return FAILURE(FALSE);
  }

  /* If we are streaming and there is some amount of data available, don't try
  to read the next piece yet. */
  HqU32x2From32x2(&u_file_pos, &file_pos);
  if (zar_streamed(p_file->p_archive) &&
      HqU32x2Compare(&u_file_pos, &p_file->extracted) < 0) {
    return TRUE;
  }

  HqU32x2AddUint32(&u_file_pos, &u_file_pos, len);
  if ( HqU32x2Compare(&u_file_pos, &p_file->extracted) <= 0 ) {
    /* Don't need to extract any more data */
    return(TRUE);
  }

  /* Need to extract more data for operation to be valid */
  /** \todo round length up to end of next buffer? */
  HqU32x2Subtract(&u_file_pos, &u_file_pos, &p_file->extracted);
  if ( !HqU32x2ToUint32(&u_file_pos, &length) )
    return FAILURE(FALSE);

  return(zfl_extract_len(p_file, length));

} /* zfl_extract_more */


/* Open a new file stream on the logical file. */
Bool zfl_open(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
  int32           openflags,
  DEVICE_FILEDESCRIPTOR* fd)
{
  ZIP_FILE_PIECE* p_piece;

  HQASSERT((p_file != NULL),
           "zfl_open: NULL ZIP file pointer");
  HQASSERT((fd != NULL),
           "zfl_open: NULL returned fd pointer");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  if ( zfl_zipfile(p_file) ) {

    if ( (openflags&SW_TRUNC) == 0 ) {
      p_piece = zfl_current_piece(p_file);

      /* Not truncating file */
      if ( !zfl_on_disk(p_file) ) {
        /* Haven't extracted the file yet - create file to write extracted data to */
        p_file->fd = (*theIOpenFile(zfd_device(p_file->p_device)))(zfd_device(p_file->p_device), p_file->filename,
                                                                   (SW_WRONLY|SW_CREAT|SW_TRUNC));
        if ( p_file->fd < 0 ) {
          return FAILURE(FALSE);
        }
        p_file->flags |= ZIP_FILE_ON_DISK;
      }

      /* If open for appending then we need all the data to be present! */
      if ( (openflags&SW_APPEND) &&
           !zfl_complete(p_file) && !zfl_extract_all(p_file) ) {
        return FAILURE(FALSE);
      }

    } else { /* File is to be truncated */
      if ( zfl_on_disk(p_file) && !zfl_complete(p_file) ) {
        /* Not going to be writing any more data so close internal file */
        zfl_close_internal(p_file);
      }
      /* Lose any remaining archive file pieces */
      zfl_free_pieces(p_file);

      /* Reset size of file */
      Hq32x2FromInt32(&p_file->total_size, 0);
      /* From now on treat file as ordinary file */
      p_file->flags &= ~ZIP_FILE_ZIPFILE;
    }
  }

  /* Create a new file stream on the device file */
  *fd = (*theIOpenFile(zfd_device(p_file->p_device)))(zfd_device(p_file->p_device), p_file->filename, openflags);
  if ( *fd < 0 ) {
    return FAILURE(FALSE);
  }

  if ( !zfl_zipfile(p_file) || !zfl_on_disk(p_file) ) {
    /* Mark file as now on disk if not from archive, or truncated on first access */
    p_file->flags |= ZIP_FILE_ON_DISK;
  }

  /* Track successful opens */
  p_file->c_opens++;

  if ( openflags&SW_EXCL ) {
    /* Mark exclusive if requested */
    p_file->flags |= ZIP_FILE_EXCL;
  }

  return(TRUE);

} /* zfl_open */


/* Close a file stream on the extracted file. */
int32 zfl_close(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*   p_file,
  DEVICE_FILEDESCRIPTOR fd,
  Bool        f_abort)
{
  HQASSERT((p_file != NULL),
           "zfl_close: NULL file pointer");
  HQASSERT((fd >= 0),
           "zfl_close: invalid descriptor");
  HQASSERT((zfl_is_open(p_file)),
           "zfl_close: no streams open on file");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  /* Decrement the counter of simultaneous opens, and if it hits zero clears the
   * exclusive access flag (even if it is not set - not worth the extra check.)
   * Call the PS file abort or close function on the stream used to read the
   * extracted file depending on \p f_abort.
   */
  p_file->c_opens--;
  if ( p_file->c_opens == 0 ) {
    /* No longer exclusive access */
    p_file->flags &= ~ZIP_FILE_EXCL;
  }

  return(f_abort
          ? (*theIAbortFile(zfd_device(p_file->p_device)))(zfd_device(p_file->p_device), fd)
          : (*theICloseFile(zfd_device(p_file->p_device)))(zfd_device(p_file->p_device), fd));

} /* zfl_close */


/* Query that the next piece of the passed file is required and available. */
Bool zfl_next_piece_ready(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
  DEVICE_FILEDESCRIPTOR fd,
/*@out@*/ /*@notnull@*/
  Bool*           ready)
{
  Bool  moreRequired;

  HQASSERT((p_file != NULL),
           "zfl_next_piece_ready: NULL file pointer");
  HQASSERT((fd >= 0),
           "zfl_next_piece_ready: invalid file descriptor");
  HQASSERT((ready != NULL),
           "zfl_next_piece_ready: NULL pointer to returned ready flag");

  *ready = TRUE;

  if (zfl_zipfile(p_file) && !zfl_complete(p_file)) {
    /* Do we need to extract more to read a single byte? */
    if ( zfl_more_extraction_required(p_file, fd, 1, &moreRequired) ) {
      *ready = !moreRequired || (zfl_current_piece(p_file) != NULL);
    } else {
      return(FALSE);
    }
  }

  return TRUE;
}

/* Read data from a logical file. */
int32 zfl_read(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
  DEVICE_FILEDESCRIPTOR fd,
/*@out@*/ /*@notnull@*/
  uint8*          buff,
  int32           len)
{
  HQASSERT((p_file != NULL),
           "zfl_read: NULL file pointer");
  HQASSERT((buff != NULL),
           "zfl_read: NULL buffer pointer");
  HQASSERT((!zfl_flushing(p_file)),
           "zfl_read: file has been flushed");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  if ( zfl_zipfile(p_file) && !zfl_complete(p_file) ) {
    /* Check if need to extract more data to file device first */
    if ( !zfl_extract_more(p_file, fd, len) ) {
      return(-1);
    }
  }

  /* Read from the extracted file */
  return((*theIReadFile(zfd_device(p_file->p_device)))(zfd_device(p_file->p_device), fd, buff, len));

} /* zfl_read */


/* Write data to a logical file. */
int32 zfl_write(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
  DEVICE_FILEDESCRIPTOR fd,
/*@in@*/ /*@notnull@*/
  uint8*          buff,
  int32           len)
{
  HQASSERT((p_file != NULL),
           "zfl_write: NULL file pointer");
  HQASSERT((!zfl_flushing(p_file)),
           "zfl_write: file has been flushed");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  if ( zfl_zipfile(p_file) && !zfl_complete(p_file) ) {
    /* Check if need to extract more data to file device first */
    if ( !zfl_extract_more(p_file, fd, len) ) {
      return(-1);
    }
  }

  /* Write to the file */
  return((*theIWriteFile(zfd_device(p_file->p_device)))(zfd_device(p_file->p_device), fd, buff, len));

} /* zfl_write */


/* Do a seek on a stream opened on the logical file. */
Bool zfl_seek(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
  DEVICE_FILEDESCRIPTOR fd,
/*@in@*/ /*@out@*/ /*@notnull@*/
  Hq32x2*         destn,
  int32           flags)
{
  uint32          data_len;
  uint32          length;
  Hq32x2          file_pos;
  HqU32x2         u_file_pos;

  HQASSERT((p_file != NULL),
           "zfl_seek: NULL file pointer");
  HQASSERT((!zfl_flushing(p_file)),
           "zfl_seek: file has been flushed");
  HQASSERT((fd >= 0),
           "zfl_seek: invalid descriptor");
  HQASSERT((destn != NULL),
           "zfl_seek: NULL seek result pointer");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

#ifdef INSTRUMENT_ZIP
  monitorf((uint8*)"ZIP: Seeking on file: %s\n", p_file->filename);
#endif

  if ( zfl_zipfile(p_file) && !zfl_complete(p_file) ) {
    /* Still extracting file data - check if seek pos yet to be extracted */
    if ( flags == SW_INCR ) {
      Hq32x2FromInt32(&file_pos, 0);
      /* Get current file position */
      if ( !(*theISeekFile(zfd_device(p_file->p_device)))(zfd_device(p_file->p_device), fd, &file_pos, SW_INCR) ) {
        return FAILURE(FALSE);
      }
      Hq32x2Add(&file_pos, &file_pos, destn);
      HqU32x2From32x2(&u_file_pos, &file_pos);

    } else if ( flags == SW_XTND ) {
      /* Get size of so it is all extracted. */
      HQTRACE(zar_streamed(p_file->p_archive),
              ("zfl_seek: XTND on stream may not be supported."));
      zfl_size(p_file, &u_file_pos);

    } else { /* Get position to seek to */
      HQASSERT((flags == SW_SET),
               "zlf_seek: unrecognised seek flag.");
      HqU32x2From32x2(&u_file_pos, destn);
    }

    if ( HqU32x2Compare(&u_file_pos, &p_file->extracted) > 0 ) {
      /* Seeking into unextracted data - extract to new seek position plus an
       * extra buffers worth for a possible follow on read */
      HqU32x2Subtract(&u_file_pos, &u_file_pos, &p_file->extracted);
      if ( !HqU32x2ToUint32(&u_file_pos, &length) )
        return FAILURE(FALSE);
      data_len = min((MAXUINT32 - length), (uint32)zar_bufsize(p_file->p_archive));
      if ( !zfl_extract_len(p_file, (length + data_len)) ) {
        return(FALSE);
      }
    }
  }

  /* Do seek on underlying file */
  return((*theISeekFile(zfd_device(p_file->p_device)))(zfd_device(p_file->p_device), fd, destn, flags));

} /* zfl_seek */


/* Flush logical file. */
Bool zfl_flush(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file)
{
  HQASSERT((p_file != NULL),
           "zfl_flush: NULL file pointer");
  HQASSERT((!zfl_flushing(p_file)),
           "zfl_flush: file already flushed");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  if ( !zfl_is_open(p_file) || !zfl_zipfile(p_file) || zfl_complete(p_file) ) {
    return(TRUE);
  }

  /* Mark file as being flushed and finish consuming file archive data */
  p_file->flags |= ZIP_FILE_FLUSHING;
  return(zfl_extract_all(p_file));

} /* zfl_flush */


/* Skip over an archive entry for a streamed archive. */
Bool zfl_skip_lcal_file(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE_INFO*  p_info,
  Bool            f_crccheck)
{
  ZIP_FILE        file;
  ZIP_FILE_PIECE  piece;
  uint8           name[2];
#define LENGTH_AND_POINTER(s)   (sizeof("" s "" ) - 1), (uint8*)("" s "")
  static ZIP_FILE_NAME filename = { LENGTH_AND_POINTER("F") };

  HQASSERT((p_archive != NULL),
           "zfl_skip_lcal_file: NULL ZIP archive pointer");
  HQASSERT((zar_streamed(p_archive)),
           "zfl_skip_lcal_file: skipping file in non-streamed archive");

  /* ZIP file needs allocated storage for a copy of the filename, the stack
   * should be fine. */
  file.name.name = name;

  /* Fake a logical file and it's piece so extraction function does its thing. */
  zfl_init(&file, TRUE, p_archive, NULL, (uint8*)"", 0, f_crccheck, &filename, &filename, TRUE);
  zfp_init(&piece, p_archive, p_info, 0);

  /* Add the piece to the file */
  DLL_ADD_HEAD(&file.pieces, &piece, link);

  /* Treat the file as having a single piece */
  file.flags |= ZIP_FILE_LAST_KNOWN;
  file.last_piece = piece.number;

  /* Set the file piece up for extraction */
  if ( !zfl_setup_piece(&file, &piece) ) {
    return(FALSE);
  }

  /* Flag file data is being skipped, not flushed.  Used to ensure there are no
   * attempts to free the file and piece structures on the stack. */
  file.flags |= ZIP_FILE_SKIPPING;

  /* Fake file has to appear open in order to flush it. */
  file.c_opens = 1;
  return(zfl_flush(&file));

} /* zfl_skip_lcal_file */


/* Reset a logical file list to be empty. */
void zfl_init_list(
/*@out@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_FILE_LIST*  p_list)
{
  HQASSERT((p_list != NULL),
           "zfl_init_list: NULL list pointer");

  DLL_RESET_LIST(&p_list->files);

  /* Only real chance to do this now - not brilliant but better than nothing at all. */
  NAME_OBJECT(p_list, ZIP_FILE_LIST_OBJECT_NAME);

} /* zfl_init_list */


/* Destroy all resources used by all files in the logical file list. */
void zfl_destroy_list(
/*@in@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_FILE_LIST*  p_list)
{
/*@only@*/
  ZIP_FILE* p_file;

  HQASSERT((p_list != NULL),
           "zfl_destroy_list: NULL list pointer");

  VERIFY_OBJECT(p_list, ZIP_FILE_LIST_OBJECT_NAME);

  p_file = DLL_GET_HEAD(&p_list->files, ZIP_FILE, link);
  while ( p_file != NULL ) {
    DLL_REMOVE(p_file, link);
    (void)zfl_destroy(p_file);
    p_file = DLL_GET_HEAD(&p_list->files, ZIP_FILE, link);
  }

} /* zfl_destroy_list */


/* Find a file by name in a logical file list. */
/*@dependent@*/ /*@null@*/
ZIP_FILE* zfl_find(
/*@in@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_FILE_LIST*  p_list,
/*@in@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_FILE_NAME*  p_name,
  Bool            f_ignore_case,
  Bool            f_use_normalised)
{
  ZIP_FILE* p_file;
  uint8*    name;
  uint8     name_lc[LONGESTFILENAME];
  ZIP_FILE_NAME* filename;

  HQASSERT((p_list != NULL),
           "zfl_find: NULL file list pointer");
  HQASSERT((p_name != NULL),
           "zfl_find: NULL name pointer");
  HQASSERT((p_name->name != NULL),
           "zfl_find: NULL file name pointer");
  HQASSERT((p_name->namelength > 0),
           "zfl_find: invalid file name length");

  VERIFY_OBJECT(p_list, ZIP_FILE_LIST_OBJECT_NAME);

  name = p_name->name;
  if ( f_ignore_case ) {
    /* Convert file name to look for to common lower case */
    zutl_strlower(name, p_name->namelength, name_lc);
    name = name_lc;
  }

  /* Find matching filename in list */
  p_file = DLL_GET_HEAD(&p_list->files, ZIP_FILE, link);
  while ( p_file != NULL ) {
    filename = f_use_normalised ? &p_file->normalised_name : &p_file->name;

    if ( filename->namelength == p_name->namelength ) {
      if ( !f_ignore_case ) {
        if ( HqMemCmp(filename->name, filename->namelength, name, filename->namelength) == 0 ) {
          break;
        }

      } else { /* Avoid converting whole file name when it wont match */
        int32 len = filename->namelength;
        uint8 *p2 = filename->name;
        uint8 *p1 = name;
        do {
          if ( *p1++ != tolower(*p2) ) {
            /*@innerbreak@*/
            break;
          }
          p2++;
        } while ( --len > 0 );
        if ( len == 0 ) {
          /* Found match - break out of loop over list */
          break;
        }
      }
    }
    p_file = DLL_GET_NEXT(p_file, ZIP_FILE, link);
  }

  return(p_file);

} /* zfl_find */


/* Rename the file */
Bool zfl_rename(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
/*@in@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_FILE_LIST*  p_list,
/*@in@*/ /*@notnull@*/
  ZIP_FILE_NAME*  p_name)
{
  uint8*  name;

  HQASSERT((p_file != NULL),
           "zfl_rename: NULL file pointer");
  HQASSERT((p_list != NULL),
           "zfl_rename: NULL new file list pointer");
  HQASSERT((p_name != NULL),
           "zfl_rename: NULL new file name pointer");
  HQASSERT((p_name->name != NULL),
           "zfl_rename: NULL file name pointer");
  HQASSERT((p_name->namelength > 0),
           "zfl_rename: invalid file name length");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);
  VERIFY_OBJECT(p_list, ZIP_FILE_LIST_OBJECT_NAME);

  /* Allocate memory for new filename first, so if it fails we have not lost the
   * original file name. Add 1 to allow adding NUL for easy debug printing.
   */
  name = mm_alloc(mm_pool_temp, zfl_filename_size(p_name), MM_ALLOC_CLASS_ZIP_FILE_NAME);
  if ( name == NULL ) {
    return FAILURE(FALSE);
  }

  /* Now safe to free the original file name */
  zfl_free_filename(p_file);

  /* Pick up the new file name */
  p_file->name.name = name;
  zfl_copy_filename(&p_file->name, p_name);

  /* For now set normalised name to new name */
  p_file->normalised_name = p_file->name;

  /* Move file from its original list to the new one. */
  DLL_REMOVE(p_file, link);
  DLL_ADD_HEAD(&p_list->files, p_file, link);

  return(TRUE);

} /* zfl_rename */


/* Delete the file. */
Bool zfl_delete(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file)
{
  HQASSERT((p_file != NULL),
           "zfl_delete: NULL file pointer");
  HQASSERT((!zfl_is_open(p_file)),
           "zfl_delete: file is open");
  HQASSERT((DLL_IN_LIST(p_file, link)),
           "zfl_delete: file is not in file list");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  /* Remove file from its containing file list */
  DLL_REMOVE(p_file, link);

  /* Finally destory the file internals */
  return(zfl_destroy(p_file));

} /* zfl_delete */


/* Initialise file chain structure. */
void zfc_init(
  ZIP_FILE_CHAIN* p_chain)
{
  HQASSERT((p_chain != NULL),
           "zfc_init: NULL chain pointer");

  p_chain->p_first = p_chain->p_last = NULL;

} /* zfc_init */


/* Append a file to the file chain. */
void zfc_append(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_CHAIN* p_chain,
/*@in@*/ /*@notnull@*/
  ZIP_FILE*       p_file)
{
  HQASSERT((p_chain != NULL),
           "zfc_append: NULL chain pointer");
  HQASSERT((p_file != NULL),
           "zfc_append: NULL file pointer");

  if ( p_chain->p_first != NULL ) {
    /* Add file to end of chain */
    p_chain->p_last->p_next = p_file;
    p_file->p_prev = p_chain->p_last;

  } else { /* First file in chain */
    p_chain->p_first = p_file;
  }

  /* Update last file in chain */
  p_chain->p_last = p_file;

} /* zfc_append */


/* Remove a file from the file chain. */
void zfc_remove(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_CHAIN* p_chain,
/*@in@*/ /*@notnull@*/
  ZIP_FILE*       p_file)
{
  HQASSERT((p_chain != NULL),
           "zfc_remove: NULL chain pointer");
  HQASSERT((p_file != NULL),
           "zfc_remove: NULL file pointer");
  HQASSERT(((p_file->p_prev != NULL) || (p_chain->p_first == p_file)),
           "zfc_remove: file not in chain");

  if ( p_file->p_next != NULL ) {
    /* Link next file with previous */
    p_file->p_next->p_prev = p_file->p_prev;

  } else { /* File was last in chain */
    p_chain->p_last = p_file->p_prev;
  }

  if ( p_file->p_prev != NULL ) {
    /* Link previous file with next */
    p_file->p_prev->p_next = p_file->p_next;

  } else { /* File was first in chain */
    p_chain->p_first = p_file->p_next;
  }

  /* Clear chain pointers */
  p_file->p_next = p_file->p_prev = NULL;

} /* zfc_remove */


/* Get first file in chain of all files. */
ZIP_FILE* zfc_get_first(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_CHAIN* p_chain)
{
  HQASSERT((p_chain != NULL),
           "zfc_get_first: NULL chain pointer");

  return(p_chain->p_first);

} /* zfc_get_first */


/* Get next file in chain of all files. */
ZIP_FILE* zfc_get_next(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_CHAIN* p_chain,
/*@in@*/ /*@notnull@*/
  ZIP_FILE*       p_file)
{
  UNUSED_PARAM(ZIP_FILE_CHAIN*, p_chain);

  HQASSERT((p_chain != NULL),
           "zfc_get_next: NULL chain pointer");
  HQASSERT((p_file != NULL),
           "zfc_get_next: NULL file pointer");

  return(p_file->p_next);

} /* zfc_get_next */


/* Get last file in chain of all files. */
ZIP_FILE* zfc_get_last(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_CHAIN* p_chain)
{
  HQASSERT((p_chain != NULL),
           "zfc_get_last: NULL chain pointer");

  return(p_chain->p_last);

} /* zfc_get_last */


/* Return the name of the logical file. */
void zfl_name(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
/*@out@*/ /*@notnull@*/
  ZIP_FILE_NAME*  p_name)
{
  HQASSERT((p_file != NULL),
           "zfl_name: NULL file pointer");
  HQASSERT((p_name != NULL),
           "zfl_name: NULL pointer to returned name");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  /* Return original file name. */
  *p_name = p_file->name;

} /* zfl_name */


/* Is the logical file currently opened for exclusive access. */
Bool zfl_is_excl(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file)
{
  HQASSERT((p_file != NULL),
           "zfl_is_excl: NULL file pointer");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  return(zfl_exclusive(p_file));

} /* zfl_is_excl */


/* Is the logical file currently opened. */
Bool zfl_is_open(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file)
{
  HQASSERT((p_file != NULL),
           "zfl_is_open: NULL file pointer");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  /* True if any streams open on the file */
  return(p_file->c_opens > 0);

} /* zfl_is_open */


/* Get the currently known total size of a logical file. */
void zfl_size(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
/*@out@*/ /*@notnull@*/
  HqU32x2*        bytes)
{
  STAT  stat;

  HQASSERT((p_file != NULL),
           "zfl_size: NULL file pointer");
  HQASSERT((bytes != NULL),
           "zfl_size: NULL returned bytes pointer");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  if ( !zfl_zipfile(p_file) ) {
    /* Get internal name of file and then find its size. */
    (*theIStatusFile(zfd_device(p_file->p_device)))(zfd_device(p_file->p_device), p_file->filename, &stat);
    *bytes = stat.bytes;

  } else if ( !zar_streamed(p_file->p_archive) || zfl_complete(p_file) ) {
    /* Size of file is known */
    *bytes = p_file->total_size;

  } else {
    /* In the process of extracting the file from a stream so the final size is
     * not yet known. */
    HqU32x2FromUint32(bytes, 0);
  }

} /* zfl_size */


/* Return the last modification data and time of a ZIP file entry. */
uint32 zfl_datetime(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file)
{
  HQASSERT((p_file != NULL),
           "zfl_datetime: NULL file pointer");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  return(p_file->date_time);

} /* zfl_datetime */


/* Get the size of the last piece added to a logical file. */
HqU32x2 zfl_piece_size(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE* p_file)
{
  HqU32x2 size;

  HQASSERT((p_file != NULL),
           "zfl_piece_size: NULL file pointer");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  size = p_file->piece_size;
  HqU32x2FromUint32(&p_file->piece_size, 0);
  return(size);

} /* zfl_piece_size */


/* Get the number of available bytes for an open stream on a logical file. */
Bool zfl_bytes_avail(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
  DEVICE_FILEDESCRIPTOR fd,
  int32           reason,
/*@in@*/ /*@notnull@*/
  Hq32x2*         bytes)
{
  Hq32x2    file_pos;
  HqU32x2   u_file_pos;
  HqU32x2   u_bytes;

  HQASSERT((p_file != NULL),
           "zfl_bytes_avail: NULL file pointer");
  HQASSERT((fd >= 0),
           "zfl_bytes_avail: invalid descriptor");
  HQASSERT((bytes != NULL),
           "zfl_bytes_avail: NULL bytes pointer");

  VERIFY_OBJECT(p_file, ZIP_FILE_OBJECT_NAME);

  if ( zfl_zipfile(p_file) && !zfl_complete(p_file) ) {
    /* File is from ZIP archive and has not been completely extracted yet.  Need
     * to get expected total size of archive file, and then work out where we
     * are in the file.
     */
    zfl_size(p_file, &u_bytes);
    if ( !HqU32x2IsZero(&u_bytes) ) {
      if ( reason == SW_BYTES_AVAIL_REL ) {
        /* Find remaining bytes after current position in file */
        Hq32x2FromInt32(&file_pos, 0);
        if ( !(*theISeekFile(zfd_device(p_file->p_device)))(zfd_device(p_file->p_device), fd, &file_pos, SW_INCR) ) {
          return FAILURE(FALSE);
        }
        HqU32x2From32x2(&u_file_pos, &file_pos);
        HqU32x2Subtract(&u_bytes, &u_bytes, &u_file_pos);
      }
    }
    Hq32x2FromU32x2(bytes, &u_bytes);

  } else { /* File is on file device so punt request */
    return((*theIBytesFile(zfd_device(p_file->p_device)))(zfd_device(p_file->p_device), fd, bytes, reason));
  }

  return(TRUE);

} /* zfl_bytes_avail */


/** \brief ZIP file device structure name used to generate hash checksum. */
#define ZIP_FILE_DEVICE_OBJECT_NAME "ZIP File Device"

/* Initialise a ZIP file device. */
void zfd_init(
/*@out@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE_DEVICE*  p_file_device,
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  DEVICELIST*       p_device)
{
  HQASSERT((p_file_device != NULL),
           "zfd_init: NULL file device pointer");
  HQASSERT((p_device != NULL),
           "zfd_init: NULL device pointer");

  NAME_OBJECT(p_file_device, ZIP_FILE_DEVICE_OBJECT_NAME);

  p_file_device->device = p_device;
  p_file_device->bufsize = zutl_dev_bufsize(p_device, 4096, ZIP_FILE_BUFFER_SIZE);

} /* zfd_init */


/* Classify the piece type of a physical ZIP file from it's name. */
int32 zfl_piece_type(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_NAME*  name,
  Bool            f_merge_files,
/*@out@*/ /*@notnull@*/
  int32*          p_name_len,
/*@out@*/ /*@notnull@*/
  uint32*         p_number)
{
  int32   type;
  uint8*  p_str;
  uint8*  p_end;

#ifndef METRO
  UNUSED_PARAM(Bool, f_merge_files);
#endif /* !METRO */

  HQASSERT((name->name != NULL),
           "zfl_piece_type: NULL filename pointer");
  HQASSERT((name->namelength > 0),
           "zfl_piece_type: invalid filename length");
  HQASSERT((p_name_len != NULL),
           "zfl_piece_type: NULL pointer to returned name length");
  HQASSERT((p_number != NULL),
           "zfl_piece_type: NULL pointer to returned piece number");

  *p_name_len = name->namelength;
  *p_number = 0;

  /* Check end of name for what looks like a directory name */
  p_end = name->name + name->namelength;
  p_str = p_end - 1;
  if ( *p_str == '/' ) {
    /* Got a directory */
    type = ZIP_FILE_PIECE_DIRECTORY;
    return(type);
  }
  /* Its a file, and assume single not part of a series and therefore last in a
   * series of 1
   */
  type = (ZIP_FILE_PIECE_FILE|ZIP_FILE_PIECE_LASTPIECE);

#ifdef METRO
  if ( f_merge_files ) {
    int32   number;
    Bool    f_last;
    uint8*  p_start;
    ZIP_UTIL_STR_SCAN strscan;

    /* Find start of pattern by looking backwards from the end */
    while ( (p_str != name->name) && (*p_str != '/') ) {
      p_str--;
    }
    p_start = p_str;

    /* Scan for piece number */
    zutl_scan_init(&strscan, p_str, CAST_PTRDIFFT_TO_UINT32(p_end - p_str));
    if ( !zutl_scan_string(&strscan, ZUTL_SCAN_STR("/[")) ) {
      return(type);
    }
    number = zutl_scan_decimal(&strscan);
    if ( number < 0 ) {
      return(type);
    }
    if ( !zutl_scan_string(&strscan, ZUTL_SCAN_STR("].")) ) {
      return(type);
    }

    /* Check for optional last specifier */
    f_last = zutl_scan_string(&strscan, ZUTL_SCAN_STR("last."));

    /* Match .piece at the end */
    if ( !zutl_scan_string(&strscan, ZUTL_SCAN_STR("piece")) ||
         !zutl_scan_atend(&strscan) ) {
      return(type);
    }

    /* Passed all checks for eDoc part piece, so setup piece info:
     * Clear last piece flag if not last, return number of piece, and adjust
     * filename length to ignore the piece specifier.
     */
    if ( !f_last ) {
      type &= ~ZIP_FILE_PIECE_LASTPIECE;
    }
    *p_number = (uint32)number;
    *p_name_len = CAST_PTRDIFFT_TO_INT32(p_start - name->name);
  }
#endif /* METRO */

  return(type);

} /* zfl_piece_type */


/**
 * \brief File data reader function pointer type.
 *
 * \param[in] p_reader
 * Pointer to file data reader context.
 * \param[in] buffer
 * Pointer to buffer to read file data in to.
 * \param[in] len
 * Length of buffer
 *
 * \return
 * Number of bytes of file data returned, or \c -1 if there was an error.
 */
typedef int32 (*ZIP_FILE_DATA_READER)(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE_READER*  p_reader,
/*@out@*/ /*@notnull@*/
  uint8*            buff,
  int32             len);

/**
 * \brief ZIP file data reader context structure.
 */
struct ZIP_FILE_READER {
  int32           flags;        /**< Flags on state of compressing file data. */
  ZIP_FILE*       p_file;       /**< ZIP file to be compressed. */
/*@temp@*/ /*@notnull@*/
  uint8*          buffer;       /**< Buffer to read file data to compress into. */
  int32           buf_len;      /**< Length of file data buffer. */
  uint32          crc_32;       /**< File data CRC32 checksum. */
  DEVICE_FILEDESCRIPTOR fd;     /**< File descriptor to read file data from the file device. */
  HqU32x2         compressed_size; /**< Compressed size of file data. */
  HqU32x2         extent;       /**< Size of file being read. */
  HqU32x2         data_left;    /**< Amount of file data left to compress. */
  ZIP_FILE_DATA_READER data_reader; /**< File data reader function pointer. */
/*@partial@*/
  z_stream        zlib_state;   /**< zlib state used for compression. */

  OBJECT_NAME_MEMBER
} ;

/** \brief ZIP file reader structure name used to generate hash checksum. */
#define ZIP_FILE_READER_OBJECT_NAME  "ZIP Reader"

/** \brief Completed reading file data. */
#define ZFR_FINISHED      (0x01)
/** \brief Flate compressor has been initialised. */
#define ZFR_FLATE_INIT    (0x02)

/** \brief Return true if all file data has been read. */
extern
Bool zfr_read_all(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE_READER*  p_reader);
#define zfr_read_all(r) (((r)->flags & ZFR_FINISHED) != 0)

/** \brief Return true if flate compressor has been initialised. */
extern
Bool zfr_flate_init(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE_READER*  p_reader);
#define zfr_flate_init(r)   (((r)->flags & ZFR_FLATE_INIT) != 0)


/**
 * \brief Read file data from mounted archive.
 *
 * This function should not be called once all the compressed file data has been
 * returned - this can be checked by calling zfr_read_all().
 *
 * \param[in] p_reader
 * Pointer to file data reader
 * \param[in] buffer
 * Pointer to buffer to fill with file data from the archive.
 * \param[in] buf_len
 * Length of buffer to be filled.
 *
 * \return
 * Number of bytes of archive file data returned, or \c -1 if error while
 * reading archive file data.
 */
static
int32 zfr_copy_archive(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_READER*  p_reader,
/*@out@*/ /*@notnull@*/
  uint8*            buffer,
  int32             buf_len)
{
  int32   bytes_read;
  uint8*  p_read;
  ZIP_FILE* p_file;

  HQASSERT((p_reader != NULL),
           "zfr_copy_archive: NULL file reader pointer");
  HQASSERT((buffer != NULL),
           "zfr_copy_archive: NULL buffer pointer");
  HQASSERT((buf_len > 0),
           "zfr_copy_archive: invalid buffer length");
  HQASSERT((p_reader->p_file != NULL),
           "zfr_copy_archive: no file setup for reader");
  HQASSERT((!zfr_read_all(p_reader)),
           "zfr_copy_archive: already finished reading data");

  VERIFY_OBJECT(p_reader, ZIP_FILE_READER_OBJECT_NAME);

  p_file = p_reader->p_file;
  p_read = buffer;
  do {
    /* Read file data from archive */
    if ( HqU32x2CompareUint32(&p_reader->data_left, MAXINT32) > 0 ) {
      bytes_read = MAXINT32;
    } else {
      HqU32x2ToUint32(&p_reader->data_left, (uint32*)&bytes_read);
    }

    bytes_read = min(buf_len, bytes_read);
    bytes_read = zar_read_raw(p_file->p_archive, p_read, bytes_read);
    if ( bytes_read <= 0 ) {
      return FAILURE(-1);
    }

    p_read += bytes_read;
    buf_len -= bytes_read;
    HqU32x2SubtractUint32(&p_reader->data_left, &p_reader->data_left, bytes_read);
    if ( HqU32x2IsZero(&p_reader->data_left) ) {
      /* Finished reading all the file data from the archive */
      p_reader->flags |= ZFR_FINISHED;
      break;
    }

  } while ( buf_len > 0 );

  /* Return number of bytes read into buffer */
  return(CAST_PTRDIFFT_TO_INT32(p_read - buffer));

} /* zfr_copy_archive */


/**
 * \brief Read file data from file device.
 *
 * This function should not be called once all the compressed file data has been
 * returned - this can be checked by calling zfr_read_all().
 *
 * \param[in] p_reader
 * Pointer to file data reader
 * \param[in] buffer
 * Pointer to buffer to fill with file data.
 * \param[in] buf_len
 * Length of buffer to be filled.
 *
 * \return
 * Number of bytes of file data returned, or \c -1 if error while reading file
 * data.
 */
static
int32 zfr_copy_file(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_READER*  p_reader,
/*@out@*/ /*@notnull@*/
  uint8*            buffer,
  int32             buf_len)
{
  int32   bytes_read;
  uint8*  p_read;
  ZIP_FILE* p_file;

  HQASSERT((p_reader != NULL),
           "zfr_copy_file: NULL file reader pointer");
  HQASSERT((buffer != NULL),
           "zfr_copy_file: NULL buffer pointer");
  HQASSERT((buf_len > 0),
           "zfr_copy_file: invalid buffer length");
  HQASSERT((p_reader->p_file != NULL),
           "zfr_copy_file: no file setup for reader");
  HQASSERT((!zfr_read_all(p_reader)),
           "zfr_copy_file: already finished reading data");

  VERIFY_OBJECT(p_reader, ZIP_FILE_READER_OBJECT_NAME);

  p_file = p_reader->p_file;
  p_read = buffer;
  do {
    /* Read file data from file */
    if ( HqU32x2CompareUint32(&p_reader->data_left, MAXINT32) > 0 ) {
      bytes_read = MAXINT32;
    } else {
      HqU32x2ToUint32(&p_reader->data_left, (uint32*)&bytes_read);
    }

    bytes_read = min(buf_len, bytes_read);
    bytes_read = (theIReadFile(zfd_device(p_file->p_device))(zfd_device(p_file->p_device),
                                                             p_reader->fd, p_reader->buffer, bytes_read));
    if ( bytes_read <= 0 ) {
      /* Error reading file data */
      return FAILURE(-1);
    }

    p_read += bytes_read;
    buf_len -= bytes_read;
    HqU32x2SubtractUint32(&p_reader->data_left, &p_reader->data_left, bytes_read);
    if ( HqU32x2IsZero(&p_reader->data_left) ) {
      /* Finished readingall the data from the file device */
      p_reader->flags |= ZFR_FINISHED;
      break;
    }

  } while ( buf_len > 0 );

  /* Return number of bytes read into buffer */
  return(CAST_PTRDIFFT_TO_INT32(p_read - buffer));

} /* zfr_copy_file */


/**
 * \brief Read file data and flate compress it.
 *
 * This function should not be called once all the compressed file data has been
 * returned - this can be checked by calling zfr_read_all().
 *
 * \param[in] p_reader
 * Pointer to file data reader
 * \param[in] buffer
 * Pointer to buffer to fill with compressed file data.
 * \param[in] buf_len
 * Length of buffer to be filled.
 *
 * \return
 * Number of bytes of compressed file data returned, or \c -1 if error while
 * generating compressed file data.
 */
static
int32 zfr_compress_file(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_READER*  p_reader,
/*@out@*/ /*@notnull@*/
  uint8*            buffer,
  int32             buf_len)
{
  int32     action;
  int32     retcode;
  int32     bytes_read;
  int32     compressed_bytes;
  ZIP_FILE* p_file;

  HQASSERT((p_reader != NULL),
           "zfr_compress_file: NULL file reader pointer");
  HQASSERT((buffer != NULL),
           "zfr_compress_file: NULL file data pointer");
  HQASSERT((buf_len > 0),
           "zfr_compress_file: invalid buffer length");
  HQASSERT((p_reader->p_file != NULL),
           "zfr_compress_file: no file setup for reader");
  HQASSERT((!zfr_read_all(p_reader)),
           "zfr_compress_file: already finished compressing data");

  VERIFY_OBJECT(p_reader, ZIP_FILE_READER_OBJECT_NAME);

  p_file = p_reader->p_file;

  p_reader->zlib_state.next_out = buffer;
  p_reader->zlib_state.avail_out = buf_len;
  action = Z_NO_FLUSH;
  do {
    if ( HqU32x2CompareUint32(&p_reader->data_left, 0) == 1 &&
         p_reader->zlib_state.avail_in == 0 ) {
      /* There is still some more file data, and zlib has compressed all so far */
      p_reader->zlib_state.next_in = p_reader->buffer;

      /* Get buffers worth of data from file */
      if ( HqU32x2CompareUint32(&p_reader->data_left, MAXINT32) > 0 ) {
        bytes_read = MAXINT32;
      } else {
        HqU32x2ToUint32(&p_reader->data_left, (uint32*)&bytes_read);
      }

      bytes_read = min(buf_len, bytes_read);
      bytes_read = (theIReadFile(zfd_device(p_file->p_device))(zfd_device(p_file->p_device),
                                                               p_reader->fd, p_reader->buffer, bytes_read));
      if ( bytes_read <= 0 ) {
        /* Error reading file data */
        return FAILURE(-1);
      }

      /* Update crc32 checksum on uncompressed file data */
      p_reader->crc_32 = crc32(p_reader->crc_32, p_reader->buffer, bytes_read);

      /* Update how much data is left to be read */
      HqU32x2SubtractUint32(&p_reader->data_left, &p_reader->data_left, bytes_read);

      /* Let zlib know how much data there is */
      p_reader->zlib_state.avail_in = bytes_read;
    }

    if ( Hq32x2IsZero(&p_reader->data_left) ) {
      /* No more file data to read, finish deflation */
      action = Z_FINISH;
    }
    HQASSERT((p_reader->zlib_state.avail_out > 0),
             "zfr_compress_file: calling deflate() with no space to write to");
    retcode = deflate(&p_reader->zlib_state, action);

    /* Tickle the RIP - need to do here since can read a lot of file data before
     * producing much deflate output. */
    SwOftenUnsafe();
  } while ( (retcode == Z_OK) && (p_reader->zlib_state.avail_out > 0) );

  switch ( retcode ) {
  case Z_STREAM_END:
    /* Flag we have finished compressing file data */
    p_reader->flags |= ZFR_FINISHED;
    /*@fallthrough@*/
  case Z_OK:
    /* Calculate number of bytes of compressed data for this call, and running
     * total */
    compressed_bytes = buf_len - p_reader->zlib_state.avail_out;
    if ( HqU32x2CompareUint32(&p_reader->compressed_size,
                              MAXUINT32 - compressed_bytes) > 0) {
      /* Error - more than 4GB of compressed data! */
      return FAILURE(-1);
    }
    HQASSERT((compressed_bytes > 0),
             "zfr_compress_file: returning without any new data");
    HqU32x2AddUint32(&p_reader->compressed_size, &p_reader->compressed_size,
                     compressed_bytes);
    return(compressed_bytes);

  default:
    HQFAIL("zfl_compress_file: unknown return from deflate()");
    /*@fallthrough@*/
  case Z_BUF_ERROR:
  case Z_STREAM_ERROR:
    HQFAIL("zfl_compress_file: unexpected return from deflate()");
    /* Error while flate compressing file data */
    return FAILURE(-1);
  }

  /* never reached */

} /* zfr_compress_file */


/* Create a new file data reader. */
ZIP_FILE_READER* zfr_new(void)
{
  mm_size_t alloc_size[2] ;
  mm_addr_t alloc_result[2];
  mm_alloc_class_t alloc_class[2];
  ZIP_FILE_READER* p_reader;

  /* Allocate reader and buffer in one go */
  alloc_size[0] = sizeof(ZIP_FILE_READER);
  alloc_class[0] = MM_ALLOC_CLASS_ZIP_READER;
  /* 32768 is a good size for zlib flate compression since is window limit */
  alloc_size[1] = 32768;
  alloc_class[1] = MM_ALLOC_CLASS_ZIP_READER_BUFFER;
  if ( MM_FAILURE == mm_alloc_multi_hetero(mm_pool_temp, 2, alloc_size,
                                           alloc_class, alloc_result) ) {
    return FAILURE(NULL);
  }

  p_reader = alloc_result[0];

  NAME_OBJECT(p_reader, ZIP_FILE_READER_OBJECT_NAME);

  /* Initialise reader state */
  p_reader->flags = 0;
  p_reader->p_file = NULL;
  p_reader->buffer = alloc_result[1];
  p_reader->buf_len = CAST_SIZET_TO_INT32(alloc_size[1]);
  p_reader->crc_32 = 0;
  p_reader->fd = -1;
  Hq32x2FromInt32(&p_reader->compressed_size, 0);
  Hq32x2FromInt32(&p_reader->data_left, 0);
  p_reader->data_reader = NULL;

  /* Set up flate memory allocator. */
  p_reader->zlib_state.zalloc = zutl_zlib_alloc;
  p_reader->zlib_state.zfree = zutl_zlib_free;
  p_reader->zlib_state.opaque = NULL;

  return(p_reader);

} /* zfr_new */


/* Free a file data reader. */
void zfr_free(
  ZIP_FILE_READER*  p_reader)
{
  int32 res;

  HQASSERT((p_reader != NULL),
           "zfr_free: NULL file reader pointer");
  HQASSERT((p_reader->p_file == NULL),
           "zfr_free: file still open on reader");
  HQASSERT((p_reader->buffer != NULL),
           "zfr_free: NULL buffer pointer");

  VERIFY_OBJECT(p_reader, ZIP_FILE_READER_OBJECT_NAME);

  if ( zfr_flate_init(p_reader) ) {
    /* Free flate resources */
    res = deflateEnd(&p_reader->zlib_state);
    HQASSERT((res == Z_OK),
             "zfr_free: failed to end zlib state");
    p_reader->flags &= ~ZFR_FLATE_INIT;
  }

  /* Free off file data buffer and reader */
  mm_free(mm_pool_temp, p_reader->buffer, p_reader->buf_len);

  UNNAME_OBJECT(p_reader);

  mm_free(mm_pool_temp, p_reader, sizeof(ZIP_FILE_READER));

} /* zfr_free */


/* Open a new file to read its data. */
Bool zfr_open(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_READER*  p_reader,
/*@in@*/ /*@notnull@*/
  ZIP_FILE*         p_file,
  int32             compression)
{
  Bool            f_copy;
  ZIP_FILE_PIECE* p_piece = NULL;

  HQASSERT((p_reader != NULL),
           "zfr_open: NULL file reader pointer");
  HQASSERT((p_file != NULL),
           "zfr_open: NULL file pointer");
  HQASSERT((p_reader->p_file == NULL),
           "zfr_open: reader currently setup for a file");
  HQASSERT(((compression == ZIPCOMP_STORED) || (compression == ZIPCOMP_DEFLATE)),
           "zfr_open: invalid file data compression");

  VERIFY_OBJECT(p_reader, ZIP_FILE_READER_OBJECT_NAME);

  /* Get amount of file data to read */
  zfl_size(p_file, &p_reader->extent);

  /* Clear complete flag for any previous file read */
  p_reader->flags &= ~ZFR_FINISHED;

  /* Test if we can copy from source archive */
  f_copy = FALSE;
  if ( zfl_zipfile(p_file) && !zfl_on_disk(p_file) && (p_file->last_piece == 0) ) {
    p_piece = DLL_GET_HEAD(&p_file->pieces, ZIP_FILE_PIECE, link);
    f_copy = (zfp_compression(p_piece) == compression);
  }

  if ( f_copy ) {
    /* Copy from archive - reposition archive to start of file data */
    if ( !zar_locate_file(p_file->p_archive, &p_piece->file_pos) ) {
      return(FALSE);
    }

    /* Set file data info from piece */
    p_reader->data_left = p_reader->compressed_size = p_piece->compressed_left;
    p_reader->crc_32 = p_piece->crc_32;

    /* Function to use to get compressed file data */
    p_reader->data_reader = zfr_copy_archive;

  } else { /* Read file data from file device */

    /* Ensure extracted all data from archive for archive files */
    if ( zfl_zipfile(p_file) && !zfl_complete(p_file) ) {
      if ( !zfl_extract(p_file) ) {
        return(FALSE);
      }
    }

    /* Open a stream on the file data */
    p_reader->fd = (*theIOpenFile(zfd_device(p_file->p_device)))(zfd_device(p_file->p_device), p_file->filename, SW_RDONLY);
    if ( p_reader->fd < 0 ) {
      return FAILURE(FALSE);
    }

    /* Reset CRC32 checksum */
    p_reader->crc_32 = crc32(0, Z_NULL, 0);
    Hq32x2FromInt32(&p_reader->compressed_size, 0);

    /* Amount of file data to be read */
    p_reader->data_left = p_reader->extent;

    /* Initialise zlib state */
    p_reader->zlib_state.next_in = Z_NULL;
    p_reader->zlib_state.avail_in = 0;
    p_reader->zlib_state.next_out = Z_NULL;
    p_reader->zlib_state.avail_out = 0;
    /* Deflate state initialised in zfr_new() and wil be reset in zfr_close() */

    /* Function to use to get compressed file data */
    p_reader->data_reader = zfr_copy_file;
    if ( compression == ZIPCOMP_DEFLATE ) {
      p_reader->data_reader = zfr_compress_file;

      if ( zfr_flate_init(p_reader) ) {
        /* Compressor already set up - reset for next file to be compressed */
        if ( deflateReset(&p_reader->zlib_state) != Z_OK ) {
          return FAILURE(FALSE);
        }

      } else { /* First time initialisation of flate compressor */
        if ( deflateInit2(&p_reader->zlib_state, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                          -MAX_WBITS, ZIP_MEM_DEFAULT, Z_DEFAULT_STRATEGY) != Z_OK ) {
          return FAILURE(FALSE);
        }
        p_reader->flags |= ZFR_FLATE_INIT;
      }
    }
  }

  if ( HqU32x2IsZero(&p_reader->extent) ) {
    /* File is empty - no data to return */
    HQASSERT((compression == ZIPCOMP_STORED),
             "zfr_open: flagging no data when not STOREd");
    p_reader->flags |= ZFR_FINISHED;
  }

  p_reader->p_file = p_file;

  return(TRUE);

} /* zfr_open */


/* Close current file associated with reader. */
void zfr_close(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_READER*  p_reader)
{
  ZIP_FILE*   p_file;

  HQASSERT((p_reader != NULL),
           "zfr_close: NULL file reader pointer");
  HQASSERT((p_reader->p_file != NULL),
           "zfr_close: file reader has no associated file");

  VERIFY_OBJECT(p_reader, ZIP_FILE_READER_OBJECT_NAME);

  if ( p_reader->data_reader == zfr_compress_file ) {
    /* Compressing file data */
    HQASSERT((p_reader->fd >= 0),
             "zfr_close: file data stream not valid");

    /* Close stream on file data */
    p_file = p_reader->p_file;
    (void)(*theICloseFile(zfd_device(p_file->p_device)))(zfd_device(p_file->p_device), p_reader->fd);

    p_reader->fd = -1;

    p_reader->data_reader = zfr_copy_file;
  }

  p_reader->p_file = NULL;

} /* zfr_close */


/* Read file data. */
int32 zfr_read_data(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_READER*  p_reader,
/*@in@*/ /*@notnull@*/
  uint8*            buffer,
/*@in@*/ /*@notnull@*/
  int32             buf_len)
{
  int32 bytes_read;
  int32 bytes;

  HQASSERT((p_reader != NULL),
           "zfr_read_data: NULL file reader pointer");
  HQASSERT((buffer != NULL),
           "zfr_read_data: NULL data buffer pointer");
  HQASSERT((buf_len > 0),
           "zfr_read_data: invalid data buffer length");

  VERIFY_OBJECT(p_reader, ZIP_FILE_READER_OBJECT_NAME);

  /* Fill buffer with more file data */
  bytes = 0;
  while ( !zfr_read_all(p_reader) && (buf_len > 0) ) {
    bytes_read = (*p_reader->data_reader)(p_reader, buffer, buf_len);
    if ( bytes_read < 0 ) {
      return FAILURE(-1);
    }
    bytes += bytes_read;
    buf_len -= bytes_read;
  }

  return(bytes);

} /* zfr_read_data */


/* Fill in data descriptor for file whose data has been read. */
Bool zfr_data_desc(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_READER*  p_reader,
/*@out@*/ /*@notnull@*/
  ZIP_DATA_DESC*    p_desc)
{
  HQASSERT((p_reader != NULL),
           "zfr_data_desc: NULL file reader pointer");
  HQASSERT((p_desc != NULL),
           "zfr_data_desc: NULL data descriptor pointer");
  HQASSERT((zfr_read_all(p_reader)),
           "zfr_data_desc: have not finished reading file data");

  VERIFY_OBJECT(p_reader, ZIP_FILE_READER_OBJECT_NAME);

  /* CRC32 and compressed size is based on file reader */
  p_desc->crc_32 = p_reader->crc_32;
  if ( !HqU32x2ToUint32(&p_reader->compressed_size, &p_desc->compressed) ) {
    return(FALSE);
  }
  HqU32x2ToUint32(&p_reader->extent, &p_desc->uncompressed_size);

  return(TRUE);

} /* zfr_data_desc */


/* Return CRC32 checksum for file read. */
uint32 zfr_data_desc_crc32(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_READER*  p_reader)
{
  HQASSERT((p_reader != NULL),
           "zfr_data_desc_crc32: NULL file reader pointer");
  HQASSERT((zfr_read_all(p_reader)),
           "zfr_data_desc_crc32: have not finished reading file data");

  VERIFY_OBJECT(p_reader, ZIP_FILE_READER_OBJECT_NAME);

  return(p_reader->crc_32);

} /* zfr_data_desc */


/* Fill in ZIP64 data descriptor for file whose data has been read. */
void zfr_z64_data_desc(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_READER*  p_reader,
/*@out@*/ /*@notnull@*/
  ZIP_ZIP64_DATA_DESC*  p_z64_desc)
{
  HQASSERT((p_reader != NULL),
           "zfr_z64_data_desc: NULL file reader pointer");
  HQASSERT((p_z64_desc != NULL),
           "zfr_z64_data_desc: NULL ZIP64 data descriptor pointer");
  HQASSERT((zfr_read_all(p_reader)),
           "zfr_z64_data_desc: have not finished reading file data");

  VERIFY_OBJECT(p_reader, ZIP_FILE_READER_OBJECT_NAME);

  /* CRC32 and compressed size is based on file reader */
  p_z64_desc->crc_32 = p_reader->crc_32;
  p_z64_desc->compressed = p_reader->compressed_size;
  p_z64_desc->uncompressed_size = p_reader->extent;

} /* zfr_z64_data_desc */


/* Fill in ZIP64 data descriptor for file whose data has been read. */
void zfr_z64_xtrafld(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_READER*  p_reader,
/*@out@*/ /*@notnull@*/
  ZIP_XTRAFLD_ZIP64* p_z64_xtrafld)
{
  HQASSERT((p_reader != NULL),
           "zfr_z64_xtrafld: NULL file reader pointer");
  HQASSERT((p_z64_xtrafld != NULL),
           "zfr_z64_xtrafld: NULL ZIP64 extra field pointer");
  HQASSERT((zfr_read_all(p_reader)),
           "zfr_z64_xtrafld: have not finished reading file data");

  VERIFY_OBJECT(p_reader, ZIP_FILE_READER_OBJECT_NAME);

  /* Update ZIP64 extra field from file reader */
  p_z64_xtrafld->uncompressed_size = p_reader->extent;
  p_z64_xtrafld->compressed = p_reader->compressed_size;

} /* zfr_z64_data_desc */


/* Log stripped */
