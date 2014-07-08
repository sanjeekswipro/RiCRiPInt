/** \file
 * \ingroup zipdev
 *
 * $HopeName: COREzipdev!src:zip_archive.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Data structures, constants, and interface for reading ZIP archives
 * based on PKWARE .ZIP File Format Specification except where noted.
 */

#ifndef __ZIP_ARCHIVE_H__
#define __ZIP_ARCHIVE_H__  (1)


#include "hq32x2.h"   /* Hq32x2 */
#include "objnamer.h" /* OBJECT_NAME_MEMBER */
#include "fileio.h"   /* FILELIST */


/** \brief A ZIP archive. */
typedef struct ZIP_ARCHIVE {
/*@notnull@*/ /*@dependent@*/
  FILELIST*   flptr;    /**< Filelist for the source ZIP archive. */
  int32       flags;    /**< ZIP archive state. */
  int32       bufsize;  /**< Buffer size to use when reading from the archive. */
  Hq32x2      extent;   /**< Size of non-streamed archive. */

  OBJECT_NAME_MEMBER
} ZIP_ARCHIVE;


/** \brief All of the archive files have been seen. */
#define ZIP_ARCHIVE_COMPLETE    (0x01)
/** \brief The archive filelist is currently open */
#define ZIP_ARCHIVE_OPEN        (0x02)
/** \brief The archive is on a non-seekable stream. */
#define ZIP_ARCHIVE_STREAMED    (0x04)
/** \brief The archive is being created. */
#define ZIP_ARCHIVE_CREATING    (0x08)

/** \brief Local file header record size including signature. */
#define ZAR_LCALFILE_RECSIZE    (30)
/** \brief Data descriptor record size including optional signature.  */
#define ZAR_DATADESC_RECSIZE    (16)
/** \brief ZIP64 data descriptor record size including optional signature.  */
#define ZAR_ZIP64_DATADESC_RECSIZE (24)
/** \brief Central directory file header record size including signature. */
#define ZAR_CDIRFILE_RECSIZE    (46)
/** \brief Central directory end record size including signature. */
#define ZAR_ENDCDIR_RECSIZE     (22)
/** \brief ZIP64 central directory end record size including signature. */
#define ZAR_ZIP64_ENDCDIR_RECSIZE (56)
/** \brief ZIP64 end of central directory locator record size including signature. */
#define ZAR_ZIP64_CDIRLOCATOR_RECSIZE (20)

/** \brief Extra field header size. */
#define ZAR_XTRAFLD_HDRSIZE     (4)
/** \brief ZIP64 extra field data size. */
#define ZAR_XTRAFLD_ZIP64SIZE   (ZAR_XTRAFLD_HDRSIZE + 28)

/** \brief Header id for the ZIP64 extra field. */
#define ZAR_EXTRA_ID_ZIP64      (0x0001)

/** \brief The archive file is not-seekable. */
extern Bool zar_streamed(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE* p_archive);
#define zar_streamed(p) (((p)->flags&ZIP_ARCHIVE_STREAMED) != 0)
/** \brief All files in the archive have been seen. */
extern Bool zar_complete(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE* p_archive);
#define zar_complete(p) (((p)->flags&ZIP_ARCHIVE_COMPLETE) != 0)
/** \brief The archive file is currently open for reading. */
extern Bool zar_opened(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE* p_archive);
#define zar_opened(p)   (((p)->flags&ZIP_ARCHIVE_OPEN) != 0)
/** \brief The archive is open for creation only. */
extern Bool zar_creating(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE* p_archive);
#define zar_creating(p)  (((p)->flags&ZIP_ARCHIVE_CREATING) != 0)
/** \brief The archive file is closed. */
extern Bool zar_closed(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE* p_archive);
#define zar_closed(p)   (!zar_opened(p))
/** \brief Size of buffer to use when reading from the archive. */
extern Bool zar_bufsize(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE* p_archive);
#define zar_bufsize(p)  ((p)->bufsize)


/* Data structure for the various ZIP archive records */

/**
 * \brief ZIP archive local file data descriptor.
 *
 * This record should only be present if bit 3 of the general flags is set.
 */
typedef struct ZIP_DATA_DESC {
  uint32        crc_32;                 /**< CRC32 checksum of uncompressed file data. */
  uint32        compressed;             /**< Size of file data compressed. */
  uint32        uncompressed_size;      /**< Size of file data uncompressed. */
} ZIP_DATA_DESC;

/**
 * \brief ZIP64 archive local file data descriptor.
 *
 * This record should only be present if bit 3 of the general flags is set and
 * the archive item uses ZIP64 extension.
 */
typedef struct ZIP_ZIP64_DATA_DESC {
  uint32        crc_32;                 /**< CRC32 checksum of uncompressed file data. */
  HqU32x2       compressed;             /**< Size of file data compressed. */
  HqU32x2       uncompressed_size;      /**< Size of file data uncompressed. */
} ZIP_ZIP64_DATA_DESC;

/**
 * \brief ZIP archive local file header entries.
 *
 * If the archive is streamed, then bit 3 of the general flags should be set and
 * the CRC32, and compressed and uncompressed fields will be 0 and should be
 * picked up from the data descriptor.
 *
 * \note
 * While the local file header layout is the same as in the central directory
 * file header (see \ref ZIP_CDIR_FILE), the \c extras_len field can be
 * different. Yep, you can have alternate/extra extras in the two headers.
 */
typedef struct ZIP_LCAL_FILE {
  uint16        version_needed;         /**< Minimum ZIP version required to extract. */
  uint16        flags;                  /**< General purpose bit flags about the file. */
  uint16        compression;            /**< Compression method used to encode the file data. */
  uint16        mod_time;               /**< File modification time - MS-DOS format. */
  uint16        mod_date;               /**< File modification date - MS-DOS format. */
  ZIP_DATA_DESC data_desc;              /**< File data descriptor. */
  uint16        name_len;               /**< File name length. */
  uint16        extras_len;             /**< Extras field length. */
} ZIP_LCAL_FILE;


/**
 * \brief ZIP archive extra field header.
 */
typedef struct ZIP_EXTRA_HDR {
  uint16        header_id;              /**< Extra field header id. */
  uint16        data_size;              /**< Extra field data size. */
} ZIP_EXTRA_HDR;


/**
 * \brief ZIP64 extra field record.
 */
typedef struct ZIP_XTRAFLD_ZIP64 {
  ZIP_EXTRA_HDR header;                 /**< Extra field header. */
  HqU32x2       uncompressed_size;      /**< File uncompressed size. */
  HqU32x2       compressed;             /**< File compressed size. */
  HqU32x2       lcal_file_hdr_offset;   /**< Local file header offset. */
  uint32        start_disk_number;      /**< Archive disk where file starts. */
  int32         present;                /**< Bit flag indicating field value present. */
} ZIP_XTRAFLD_ZIP64;

/* Bit flags set in ZIP64 extra field present flag */
#define ZIP64_UNCOMPRESSED  (0x01)      /**< ZIP64 extra field uncompressed value present flag. */
#define ZIP64_COMPRESSED    (0x02)      /**< ZIP64 extra field compressed value present flag. */
#define ZIP64_OFFSET        (0x04)      /**< ZIP64 extra field local file header offset value present flag. */
#define ZIP64_STARTDISK     (0x08)      /**< ZIP64 extra field start disk value present flag. */
/** \brief All ZIP64 extra field values are present. */
#define ZIP64_ALLPRESENT    (ZIP64_UNCOMPRESSED|ZIP64_COMPRESSED|ZIP64_OFFSET|ZIP64_STARTDISK)


/* \brief Value for 32 bit ZIP record field whose value should be read from a
 * ZIP64 record. */
#define ZIP_USE_ZIP64FLD_LONG   (0xffffffffu)
/* \brief Value for 16 bit ZIP record field whose value should be read from a
 * ZIP64 record. */
#define ZIP_USE_ZIP64FLD_SHORT  ((uint16)0xffffu)

/**
 * \brief ZIP64 end of central directory record.
 */
typedef struct ZIP_ZIP64_END_CDIR {
  HqU32x2       end_cdir_size;          /**< Size of ZIP64 end of central directory record */
  uint16        made_by_version;        /**< Host compatibility and ZIP version used to encode the file. */
  uint16        version_needed;         /**< Minimum ZIP version required to extract. */
  uint32        this_disk_number;       /**< Number of disk containing this record. */
  uint32        start_disk_number;      /**< Number of disk containing start of central directory. */
  HqU32x2       total_cdir_entries_this_disk; /**< Number of central directory records on this disk. */
  HqU32x2       total_cdir_entries;     /**< Total number of entries in central directory. */
  HqU32x2       cdir_size;              /**< Total size of the central directory. */
  HqU32x2       cdir_start_offset;      /**< Offset of central directory on starting disk */
} ZIP_ZIP64_END_CDIR;

/**
 * \brief ZIP64 start of ZIP64 end of central directory locator record.
 */
typedef struct ZIP_ZIP64_CDIR_LOC {
  uint32        start_disk_number;      /**< Number of disk containing start of central directory. */
  HqU32x2       zip64_end_cdir_offset;  /**< Offset of the ZIP64 end of central directory record. */
  uint32        total_disks;            /**< Total number of disks in the archive. */
} ZIP_ZIP64_CDIR_LOC;

/**
 * \brief ZIP archive central directory file header.
 */
typedef struct ZIP_CDIR_FILE {
  uint16        made_by_version;        /**< Host compatibility and ZIP version used to encode the file. */
  ZIP_LCAL_FILE lcal_file;              /**< Same layout as local file header but see note. */
  uint16        comment_len;            /**< Length of file comment. */
  uint32        start_disk_number;      /**< Number of archive volume containing local file header. */
  uint16        internal_attributes;    /**< Internal attribute flags. */
  uint32        external_attributes;    /**< External attribute flags. */
  uint32        lcal_file_hdr_offset;   /**< Offset to start of local file header in archive volume it is in. */
} ZIP_CDIR_FILE;

/**
 * \brief  ZIP archive central directory end record.
 */
typedef struct ZIP_END_CDIR {
  uint16        disknum;                /**< Number of this archive volume. */
  uint16        cdir_start_disknum;     /**< Archive volume with start of central directory. */
  uint16        cdir_entries_thisdisk;  /**< Number of entries in central directory on this disk. */
  uint16        cdir_entries_total;     /**< Total number of entries in all central directories. */
  uint32        cdir_size;              /**< Size of central directory. */
  uint32        cdir_offset;            /**< Offset to start of central directory in archive volume it is in. */
  uint16        comment_length;         /**< Length of archive comment. */
} ZIP_END_CDIR;


/** \brief File data is compressed */
#define ZIPFLG_ENCRYPTED        (1<<0)
/** \brief File data compression setting. */
#define ZIPFLG_COMPRESSION_OPTS ((1<<1)|(1<<2)) /* 2 bits! */
/** \brief File data is followed by a data descriptor. */
#define ZIPFLG_USE_DATADESC     (1<<3)
/** \brief File data is patch data. */
#define ZIPFLG_PATCH_DATA       (1<<5)
/** \brief File data is encrypted with strong encryption. */
#define ZIPFLG_ENCRYPTED_STRONG (1<<6)
/** \brief File data is compressed. */
#define ZIPFLG_ENCRYPTED_CDIR   (1<<13)

/** \brief ZIP file data options not supported */
#define ZIPFLG_ERROR            (ZIPFLG_ENCRYPTED|ZIPFLG_PATCH_DATA|ZIPFLG_ENCRYPTED_STRONG|ZIPFLG_ENCRYPTED_CDIR)


/** \brief File data is not compressed (STORE). */
#define ZIPCOMP_STORED          (0)
/** \brief File data is compressed with SHRUNK. */
#define ZIPCOMP_SHRUNK          (1)
/** \brief File data is compressed with REDUCE1. */
#define ZIPCOMP_REDUCED_1       (2)
/** \brief File data is compressed with REDUCE2. */
#define ZIPCOMP_REDUCED_2       (3)
/** \brief File data is compressed with REDUCE3. */
#define ZIPCOMP_REDUCED_3       (4)
/** \brief File data is compressed with REDUCE3. */
#define ZIPCOMP_REDUCED_4       (5)
/** \brief File data is compressed with IMPLODE. */
#define ZIPCOMP_IMPLODED        (6)
/** \brief File data is compressed with TOKEN. */
#define ZIPCOMP_TOKEN_RESERVED  (7)
/** \brief File data is compressed with FLATE. */
#define ZIPCOMP_DEFLATE         (8)
/** \brief File data is compressed with FLATE64. */
#define ZIPCOMP_DEFLATE64       (9)
/** \brief File data is compressed with DCL IMPLODE. */
#define ZIPCOMP_DCL_IMPLODE     (10)
/** \brief Reserved compression id. */
#define ZIPCOMP_PKWARE_RESERVED (11)
/** \brief File data is compressed with BZIP2. */
#define ZIPCOMP_BZIP2           (12)

/** \brief File attribute is compatible with MS-DOS, OS/2, and non-NTFS NT
 * (later inclusion of VFAT and NTFS). */
#define ZIP_COMPAT_MSDOS        (0)
/** \brief File attribute is compatible with Amiga. */
#define ZIP_COMPAT_AMIGA        (1)
/** \brief File attribute is compatible with OpenVMS. */
#define ZIP_COMPAT_OPENVMS      (2)
/** \brief File attribute is compatible with Unix. */
#define ZIP_COMPAT_UNIX         (3)
/** \brief File attribute is compatible with VM/CMS. */
#define ZIP_COMPAT_VM_CMS       (4)
/** \brief File attribute is compatible with Atari ST. */
#define ZIP_COMPAT_ATARI_ST     (5)
/** \brief File attribute is compatible with OS/2 HPFS. */
#define ZIP_COMPAT_OS2_HPFS     (6)
/** \brief File attribute is compatible with Macintosh. */
#define ZIP_COMPAT_MACINTOSH    (7)
/** \brief File attribute is compatible with Z-System. */
#define ZIP_COMPAT_Z_SYSTEM     (8)
/** \brief File attribute is compatible with CP/M. */
#define ZIP_COMPAT_CP_M         (9)
/** \brief File attribute is compatible with Windows NTFS - InfoZIP document
 * that this value has never been used by PKWARE. */
#define ZIP_COMPAT_WIN_NTFS     (10)
/** \brief File attribute is compatible with TOPS-20 - used by InfoZIP. */
#define ZIP_COMPAT_IZ_TOPS_20   (10)
/** \brief File attribute is compatible with MVS (0S/390 - Z/OS). */
#define ZIP_COMPAT_MVS          (11)
/** \brief File attribute is compatible with Windows NTFS - used by InfoZIP
 * since September 1993. */
#define ZIP_COMPAT_IZ_NTFS      (11)
/** \brief File attribute is compatible with VSE. */
#define ZIP_COMPAT_VSE          (12)
/** \brief File attribute is compatible with SMS/QDOS - used by InfoZIP. */
#define ZIP_COMPAT_IZ_SMQ_QDOS  (12)
/** \brief File attribute is compatible with Acorn Risc. */
#define ZIP_COMPAT_ACORN_RISC   (13)
/** \brief File attribute is compatible with VFAT. */
#define ZIP_COMPAT_VFAT         (14)
/** \brief File attribute is compatible with alternate MVS. */
#define ZIP_COMPAT_ALT_MVS      (15)
/** \brief File attribute is compatible with BeOS. */
#define ZIP_COMPAT_BEOS         (16)
/** \brief File attribute is compatible with Tandem. */
#define ZIP_COMPAT_TANDEM       (17)
/** \brief File attribute is compatible with OS/400. */
#define ZIP_COMPAT_OS_400       (18)
/** \brief File attribute is compatible with OS/X (Darwin). */
#define ZIP_COMPAT_OS_X         (19)

/**
 * \brief Generate ZIP record version made by field value.
 *
 * The version made by field has the host OS in the top 8 bits and the version
 * used in the bottom 8 bits.  The version field value created with the
 * ZIP_VERSION() macro can be used with this macro.
 *
 * \param[in] os
 * Effective host system OS.
 * \param[in] version
 * ZIP specification version number used to encode the file.
 *
 * \return
 * ZIP version made by field value.
 */
extern
uint16 ZIP_VERSION_MADEBY(
  uint8   os,
  uint8   version);
#define ZIP_VERSION_MADEBY(o, v)   CAST_TO_UINT16((CAST_TO_UINT8(o) << 16) | CAST_TO_UINT8(v))

/**
 * \brief Generate ZIP version field value.
 *
 * \param[in] major
 * Major version number.
 * \param[in] minor
 * Minor version number.
 *
 * \return
 * ZIP version value.
 */
extern
uint16 ZIP_VERSION(
  uint8   major,
  uint8   minor);
#define ZIP_VERSION(m, n)   CAST_TO_UINT16(CAST_TO_UINT8(m)*10 + CAST_TO_UINT8(n))

/** \brief Get the compatibility of the external file attribute information. */
extern
uint32 zar_cdir_file_compatibility(
/*@in@*/ /*@notnull@*/
  ZIP_CDIR_FILE* p_cdir_file);
#define zar_cdir_file_compatibility(p) (uint32)(((p)->made_by_version & 0xff00) >> 8)

/** \brief Get the ZIP specification version supported by ZIP creator for file. */
extern
uint32 zar_cdir_file_encode_ver(
/*@in@*/ /*@notnull@*/
  ZIP_CDIR_FILE* p_cdir_file);
#define zar_cdir_file_encode_ver(p) (uint32)((p)->made_by_version & 0xff)

/**
 * \brief Initialise ZIP archive file structure .
 *
 * \param[out] p_archive
 * Pointer to an archive to initialise.
 */
extern
void zar_init(
/*@out@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive);


/**
 * \brief Open archive from running executable PS.
 *
 * \param[out] p_archive
 * Pointer to archive state.
 * \param[in] ps
 * Pointer to executable PS.
 * \param[in] ps_len
 * Length of executable PS.
 * \param[in] as_stream
 * Whether to force reading archive as a stream.
 *
 * \return
 * \c TRUE if archive source opened ok, else \c FALSE.
 */
Bool zar_open_ps(
/*@out@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@in@*/ /*@notnull@*/
  uint8*        ps,
  uint32        ps_len,
  Bool          as_stream);


/**
 * \brief Open archive from PS file name.
 *
 * \param[out] p_archive
 * Pointer to archive state.
 * \param[in] archive_name
 * Pointer to PS filename.
 * \param[in] name_len
 * Length of PS filename.
 * \param[in] as_stream
 * Whether to force reading archive as a stream.
 *
 * \return
 * \c TRUE if archive source opened ok, else \c FALSE.
 */
Bool zar_open_file(
/*@out@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@in@*/ /*@notnull@*/
  uint8*        archive_name,
  uint32        name_len,
  Bool          as_stream);


/**
 * \brief Open archive with no archive source.
 *
 * This initialises the archive state when there is no archive to use as the
 * initial set of files on the ZIP device.  This will typically be used when
 * creating a new archive from files added to a ZIP device.
 *
 * \param[out] p_archive
 * Pointer to an archive structure to update with empty archive state.
 */
extern
void zar_open_empty(
/*@out@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive);


/**
 * \brief Open new archive ready to create content.
 *
 * \param[out] p_archive
 * Pointer to archive state to update for new archive file.
 * \param[in] archive_name
 * Pointer to PS filename for new archive.
 * \param[in] name_len
 * Length of PS filename.
 *
 * \return
 * \c TRUE if new archive file is opened successfully, else \c FALSE.
 */
extern
Bool zar_open_create(
/*@out@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@in@*/ /*@notnull@*/
  uint8*        archive_name,
  uint32        name_len);


/**
 * \brief Close an open archive.
 *
 * \param[in] p_archive
 * Pointer to open archive to close.
 * \param[in] flag
 * PostScript close flag.
 *
 * \return
 * \c 0 if the archive file is closed successfully, else \c -1.
 */
extern
int32 zar_close(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
  int32         flag);


/**
 * \brief Device status for device containing archive.
 *
 * \param[in] p_archive
 * Pointer to open archive to to get device status for.
 * \param[out] devstat
 * Pointer to returned device status structure.
 *
 * \return
 * \c 0 if the device status was retrieved, else \c -1.
 */
extern
int32 zar_dev_status(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@out@*/ /*@notnull@*/
  DEVSTAT*      devstat);


/**
 * \brief Reposition the current point on the archive file.
 *
 * \param[in] p_archive
 * Pointer to the archive.
 * \param[in] p_offset
 * Pointer to file offset to reposition to.
 *
 * \return
 * \c TRUE if the seek on the archive file was successful, else \c FALSE.
 */
extern
Bool zar_set_pos(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@in@*/ /*@notnull@*/
  Hq32x2*       p_offset);

/**
 * \brief Get the current file position on the archive file.
 *
 * \param[in] p_archive
 * Pointer to the archive.
 * \param[out] p_offset
 * Pointer to returned file position.
 *
 * \return
 * \c TRUE if successfully retrieved the current position in the archive file,
 * else \c FALSE.
 */
extern
Bool zar_get_pos(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@out@*/ /*@notnull@*/
  Hq32x2*       p_offset);


/**
 * \brief Read data directly from the archive file.
 *
 * \param[in] p_archive
 * Pointer to the archive to read from.
 * \param[out] buffer
 * Pointer to buffer to read archive data into.
 * \param[in] len
 * Amount of archive data to read.
 *
 * \return
 * The number of bytes successfully read, or -1 if there was a error while
 * reading.
 */
extern
int32 zar_read_raw(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@out@*/ /*@notnull@*/
  uint8*        buffer,
  int32         len);


/**
 * \brief Write data directly to an archive file.
 *
 * \param[in] p_archive
 * Pointer to the archive to write to.
 * \param[in] buffer
 * Pointer to buffer to write to the archive.
 * \param[in] len
 * Amount of data to write.
 *
 * \return
 * The number of bytes successfully written, or -1 if there was a error while
 * writing.
 */
extern
int32 zar_write_raw(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@in@*/ /*@notnull@*/
  uint8*        buffer,
  int32         len);


/** \brief Macro to make 32-bit version of a ZIP record signature */
#define ZIP_SIG(a, b, c, d)   ((uint32)(((d) << 24) | ((c) << 16) | ((b) << 8) | (a)))

/** \brief An invalid signature id */
#define PKSIG_UNDEFINED     ZIP_SIG(0xff, 0xff, 0xff, 0xff)
/** \brief Local file header signature id. */
#define PKSIG_LCAL_FILE     ZIP_SIG(0x50, 0x4b, 0x03, 0x04)
/** \brief Central directory file header signature id. */
#define PKSIG_CDIR_FILE     ZIP_SIG(0x50, 0x4b, 0x01, 0x02)
/** \brief Central directory end recored signature id. */
#define PKSIG_ENDCDIR       ZIP_SIG(0x50, 0x4b, 0x05, 0x06)
/** \brief Data descriptor signature id. */
#define PKSIG_DATADESC      ZIP_SIG(0x50, 0x4b, 0x07, 0x08)
/** \brief End of ZIP64 central directory signature id. */
#define PKSIG_ZIP64_ENDCDIR ZIP_SIG(0x50, 0x4b, 0x06, 0x06)
/** \brief ZIP64 central directory locator signature id. */
#define PKSIG_ZIP64_LOCATOR ZIP_SIG(0x50, 0x4b, 0x06, 0x07)

/**
 * \brief Read and identify a signature from an archive.
 *
 * It is assumed that the file position on the archive when this function is
 * called is before an expected signature.  To look for the next signature
 * appearing in the archive used zar_next_sig().
 *
 * \param[in] p_archive
 * Pointer to archive to read the signature from.
 * \param[out] p_sig
 * Pointer to returned signature.
 *
 * \return
 * \c TRUE if read a recognised signature from the archive, else \c FALSE.
 */
extern
Bool zar_read_sig(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@out@*/ /*@notnull@*/
  uint32*       p_sig);

/**
 * \brief Find the next recognised signature in an archive.
 *
 * Look for a recognised record signature in the archive, reading ahead as
 * required.  On exit, the archive file position is at the start of the data
 * after the found record signature.
 *
 * \param[in] p_archive
 * Pointer to archive to find the signature in.
 * \param[out] p_sig
 * Pointer to returned signature.
 *
 * \return
 * \c TRUE if found a recognised signature from the archive, else \c FALSE.
 */
extern
Bool zar_next_sig(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@out@*/ /*@notnull@*/
  uint32*       p_sig);

/**
 * \brief Read and match a signature from an archive.
 *
 * \param[in] p_archive
 * Pointer to archive to read the signature from.
 * \param[in] sig
 * Signature to match in archive.
 *
 * \return
 * \c TRUE if read a signature was matched in the archive, else \c FALSE.
 */
extern
Bool zar_match_sig(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@out@*/ /*@notnull@*/
  uint32        sig);


/**
 * \brief Find the start of the central directory end record.
 *
 * The function leaves the archive file position after the central directory end
 * record signature.
 *
 * \param[in] p_archive
 * Pointer to archive to search.
 * \param[out] file_pos
 * Start position of end of central directory record.
 *
 * \return
 * \c TRUE if successfully found the central directory end record, else \c
 * FALSE.
 */
extern
Bool zar_find_end_cdir(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@out@*/ /*@notnull@*/
  Hq32x2*       file_pos);

/**
 * \brief Read the central directory end record.
 *
 * \param[in] p_archive
 * Pointer to archive positioned ready to read the central directory end record.
 * \param[out] p_end_cdir
 * Pointer to central directory end record to initialise from the archive.
 *
 * \return
 * \c TRUE if the central directory end record is successfully read, else \c
 * FALSE.
 */
extern
Bool zar_read_end_cdir(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@out@*/ /*@notnull@*/
  ZIP_END_CDIR* p_end_cdir);

/**
 * \brief Write the central directory end record.
 *
 * \param[in] p_archive
 * Pointer to archive positioned ready to write the central directory end record.
 * \param[in] p_end_cdir
 * Pointer to central directory end record to write to the archive.
 *
 * \return
 * \c TRUE if the central directory end record is successfully written, else \c
 * FALSE.
 */
extern
Bool zar_write_end_cdir(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@in@*/ /*@notnull@*/
  ZIP_END_CDIR* p_end_cdir);

/**
 * \brief Fill in the end of central directory record.
 *
 * The end of central directory record buffer is a fixed sized record - it does
 * not include the variable length archive comment.
 *
 * To create a ZIP64 end of central directory record see
 * zar_create_zip64_end_cdir().
 *
 * \param[out] buffer
 * Pointer to memory buffer for filled in end of central directory record.
 * \param[in] p_end_cdir
 * Pointer to end of central directory structure.
 *
 * \return
 * \C FALSE if the archive's start of central directory offset cannot fit in the
 * 4-byte field, else \c TRUE.
 */
extern
Bool zar_create_end_cdir(
/*@out@*/ /*@notnull@*/
  uint8         buffer[ZAR_ENDCDIR_RECSIZE],
/*@in@*/ /*@notnull@*/
  ZIP_END_CDIR* p_end_cdir);

/**
 * \brief Read a central directory file header record.
 *
 * The function leaves the archive file position at the start of the next record
 * in the central directory.
 *
 * \param[in] p_archive
 * Pointer to archive positioned ready to read a central directory file header
 * record.
 * \param[out] p_cdir_file
 * Pointer to central directory file header record to initialise from the archive.
 * \param[out] filename
 * Pointer to returned filename.
 * \param[out] extras
 * Pointer to returned extra fields.
 *
 * \return
 * \c TRUE if the central directory file header is successfully read, else \c
 * FALSE.
 */
extern
Bool zar_read_cdir_file(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@out@*/ /*@notnull@*/
  ZIP_CDIR_FILE*  p_cdir_file,
/*@out@*/ /*@notnull@*/
  uint8*          filename,
/*@out@*/ /*@notnull@*/
  uint8*          extras);

/**
 * \brief Write a central directory file header record.
 *
 * \param[in] p_archive
 * Pointer to archive positioned ready to write a central directory file header
 * record.
 * \param[in] p_cdir_file
 * Pointer to central directory file header record to write to the archive.
 * \param[in] filename
 * Pointer to filename to write.
 * \param[in] extras
 * Pointer to extra field data.
 *
 * \return
 * \c TRUE if the central directory file header is successfully written, else \c
 * FALSE.
 */
extern
Bool zar_write_cdir_file(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/
  ZIP_CDIR_FILE*  p_cdir_file,
/*@in@*/ /*@notnull@*/
  uint8*          filename,
/*@in@*/ /*@notnull@*/
  uint8*          extras);


/**
 * \brief Fill in the central directory file record.
 *
 * The central directory file record buffer is a fixed sized record - it does
 * not include the variable length extras or comment.
 *
 * \param[out] buffer
 * Pointer to memory buffer for filled in central directory file record.
 * \param[in] p_cdir_file
 * Pointer to central directory file structure.
 */
extern
void zar_create_cdir_file(
/*@out@*/ /*@notnull@*/
  uint8           buffer[ZAR_CDIRFILE_RECSIZE],
/*@in@*/ /*@notnull@*/
  ZIP_CDIR_FILE*  p_cdir_file);


/**
 * \brief Read a local file header record.
 *
 * The function leaves the archive file position at the start of the file data.
 *
 * \param[in] p_archive
 * Pointer to archive positioned ready to read a local file header record.
 * \param[out] p_lcal_file
 * Pointer to local file header record to initialise from the archive.
 * \param[out] filename
 * Pointer to returned filename.
 * \param[out] extras
 * Pointer to returned extra fields.
 *
 * \return
 * \c TRUE if the local file header is successfully read, else \c FALSE.
 */
extern
Bool zar_read_lcal_file(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@out@*/ /*@notnull@*/
  ZIP_LCAL_FILE*  p_lcal_file,
/*@out@*/ /*@notnull@*/
  uint8*          filename,
/*@out@*/ /*@notnull@*/
  uint8*          extras);

/** \brief Return header and offset for an extra field.
 *
 * The search stops at the first matching extra field header id.  If there is
 * not enough remaining extra field data for the size of the field in the
 * header, then the field is not detected as being present.
 *
 * \param[in] p_extras
 * Pointer to start of archive item's extra field.
 * \param[in] extras_len
 * Length of archive item's extra field.
 * \param[in] header_id
 * Header id of extra field to look for.
 * \param[out] p_start
 * Pointer to returned start of extra field.
 * \param[out] p_header
 * Pointer to returned header for extra field.
 *
 * \return
 * \c TRUE if the extra field is present and all the data exists in the extra
 * field buffer, else \c FALSE.
 */
extern
Bool zar_find_xtrafld(
/*@in@*/ /*@notnull@*/
  uint8*          p_extras,
/*@in@*/ /*@notnull@*/
  uint32          extras_len,
/*@in@*/ /*@notnull@*/
  uint32          header_id,
/*@out@*/ /*@notnull@*/
  uint8**         p_start,
/*@out@*/ /*@notnull@*/
  ZIP_EXTRA_HDR*  p_header);

/**
 * \brief Write a local file header record.
 *
 * \param[in] p_archive
 * Pointer to archive to write local file header to.
 * \param[in] p_lcal_file
 * Pointer to local file header record to write to the archive.
 * \param[in] filename
 * Pointer to filename.
 * \param[in] extras
 * Pointer to any extra field data.
 *
 * \return
 * \c TRUE if the local file header was successfully written, else \c FALSE.
 */
extern
Bool zar_write_lcal_file(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/
  ZIP_LCAL_FILE*  p_lcal_file,
/*@in@*/ /*@notnull@*/
  uint8*          filename,
/*@in@*/ /*@notnull@*/
  uint8*          extras);

/**
 * \brief Fill in the file header record for file.
 *
 * The local file header buffer is a fixed sized record - it does not include
 * the variable length extras or filename.
 *
 * \param[out] buffer
 * Pointer to memory buffer for filled in header record.
 * \param[in] p_lcal_file
 * Pointer to local file header structure.
 */
extern
void zar_create_lcal_file(
/*@out@*/ /*@notnull@*/
  uint8           buffer[ZAR_LCALFILE_RECSIZE],
/*@in@*/ /*@notnull@*/
  ZIP_LCAL_FILE*  p_lcal_file);

/**
 * \brief Read a data descriptor record.
 *
 * The function leaves the archive file position at the start of the following
 * record.
 *
 * \param[in] p_archive
 * Pointer to archive positioned ready to read data descriptor.
 * \param[out] p_data_desc
 * Pointer to data descriptor to initialise from the archive.
 *
 * \return
 * \c TRUE if the data descriptor is successfully read, else \c FALSE.
 */
extern
Bool zar_read_data_desc(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@out@*/ /*@notnull@*/
  ZIP_DATA_DESC*  p_data_desc);

/**
 * \brief Read a ZIP64 data descriptor record.
 *
 * The function leaves the archive file position at the start of the following
 * record.
 *
 * \param[in] p_archive
 * Pointer to archive positioned ready to read data descriptor.
 * \param[out] p_z64_data_desc
 * Pointer to ZIP64 data descriptor to initialise from the archive.
 *
 * \return
 * \c TRUE if the data descriptor is successfully read, else \c FALSE.
 */
extern
Bool zar_read_zip64_data_desc(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@out@*/ /*@notnull@*/
  ZIP_ZIP64_DATA_DESC* p_z64_data_desc);

/**
 * \brief Write a data descriptor to the archive.
 *
 * \param[in] p_archive
 * Pointer to archive positioned at the start of the local file header to be
 * updated.
 * \param[in] data_desc
 * Data descriptor to update local file header with.
 *
 * \return
 * \c TRUE if the local file header is successfully written, else \c FALSE.
 */
extern
Bool zar_write_data_desc(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/
  ZIP_DATA_DESC*  data_desc);

/**
 * \brief Update the data descriptor field of a local file header.
 *
 * \param[in] p_archive
 * Pointer to archive.
 * \param[in] lcal_file_offset
 * Pointer to file position for local file header.
 * \param[in] data_desc
 * Pointer to updated data descriptor structure.
 *
 * \return
 * \c TRUE if the data descriptor field is successfully updated, else \c FALSE.
 */
extern
Bool zar_update_data_desc(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/
  Hq32x2*         lcal_file_offset,
/*@in@*/ /*@notnull@*/
  ZIP_DATA_DESC*  data_desc);

/**
 * \brief Update the CRC32 checksum field of the local file header.
 *
 * \param[in] p_archive
 * Pointer to archive.
 * \param[in] lcal_file_offset
 * Pointer to file position for local file header.
 * \param[in] crc32
 * CRC32 checksum value for the file.
 *
 * \return
 * \c TRUE if the CRC32 checksum value is successfully updated, else \c FALSE.
 */
extern
Bool zar_update_data_desc_crc32(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/
  Hq32x2*         lcal_file_offset,
/*@in@*/ /*@notnull@*/
  uint32          crc32);

/**
 * \brief Write a files ZIP64 data descriptor to the archive.
 *
 * \param[in] p_archive
 * Pointer to archive positioned at the start of the local file header to be
 * updated.
 * \param[in] z64_data_desc
 * ZIP64 data descriptor to update local file header with.
 *
 * \return
 * \c TRUE if the local file header is successfully written, else \c FALSE.
 */
extern
Bool zar_write_zip64_data_desc(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/
  ZIP_ZIP64_DATA_DESC* z64_data_desc);

/**
 * \brief Update the extras field of a local file header.
 *
 * \param[in] p_archive
 * Pointer to archive.
 * \param[in] lcal_file_offset
 * Pointer to file position for local file header.
 * \param[in] p_lcal_file
 * Pointer to local file header structure.
 * \param[in] extras
 * Pointer to updated extras field.
 *
 * \return
 * \c TRUE if the extras field is successfully updated, else \c FALSE.
 */
extern
Bool zar_update_extras(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/
  Hq32x2*         lcal_file_offset,
/*@in@*/ /*@notnull@*/
  ZIP_LCAL_FILE*  p_lcal_file,
/*@in@*/ /*@notnull@*/
  uint8*          extras);

/**
 * \brief Fill in file data descriptor record.
 *
 * \param[out] buffer
 * Pointer to memory buffer for filled in header record.
 * \param[in] data_desc
 * Pointer to data descriptor structure.
 */
extern
void zar_create_data_desc(
/*@out@*/ /*@notnull@*/
  uint8           buffer[ZAR_DATADESC_RECSIZE],
/*@in@*/ /*@notnull@*/
  ZIP_DATA_DESC*  data_desc);

/**
 * \brief Fill in file ZIP64 data descriptor record.
 *
 * \param[out] buffer
 * Pointer to memory buffer for filled in header record.
 * \param[in] z64_data_desc
 * Pointer to data descriptor structure.
 */
extern
void zar_create_zip64_data_desc(
/*@out@*/ /*@notnull@*/
  uint8           buffer[ZAR_ZIP64_DATADESC_RECSIZE],
/*@in@*/ /*@notnull@*/
  ZIP_ZIP64_DATA_DESC* z64_data_desc);

/**
 * \brief Locate start of file data in a seekable archive.
 *
 * \param[in] p_archive
 * Pointer to archive containing file data.
 * \param[in,out] file_pos
 * Position of local file header, updated with position of file data.
 *
 * \return
 * \c TRUE if successfully located start of file data, else \c FALSE.
 */
extern
Bool zar_locate_file(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@in@*/ /*@out@*/ /*@notnull@*/
  Hq32x2*       file_pos);

/**
 * \brief Read the ZIP64 extra field.
 *
 * The present flag in the returned ZIP64 extra field structure contains flags
 * indicating which field values were present.
 *
 * \param[in] p_xtrafld
 * Pointer to buffer containing ZIP64 extra field record including header.
 * \param[out] p_zip64
 * Pointer to filled in ZIP64 extra field structure.
 */
extern
void zar_read_xtrafld_zip64(
/*@in@*/ /*@notnull@*/
  uint8*          p_xtrafld,
/*@out@*/ /*@notnull@*/
  ZIP_XTRAFLD_ZIP64* p_zip64);

/**
 * \brief Create a ZIP64 extra field record.
 *
 * \param[out] buffer
 * Pointer to buffer to hold the ZIP64 extra field record, including header.
 * \param[in] p_zip64
 * Pointer to a ZIP64 extra field structure.
 */
extern
void zar_create_xtrafld_zip64(
/*@out@*/ /*@notnull@*/
  uint8   buffer[ZAR_XTRAFLD_ZIP64SIZE],
/*@in@*/ /*@notnull@*/
  ZIP_XTRAFLD_ZIP64* p_zip64);

/**
 * \brief Write a ZIP64 extra field record to the archive.
 *
 * \param[in] p_archive
 * Pointer to archive to add the record to.
 * \param[in] p_zip64
 * Pointer to a ZIP64 extra field structure.
 *
 * \return
 * \c TRUE if record successfully written to the archive, else \c FALSE.
 */
extern
Bool zar_write_xtrafld_zip64(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/
  ZIP_XTRAFLD_ZIP64* p_zip64);

/**
 * \brief Look for a ZIP64 central directory locator record.
 *
 * \param[in] p_archive
 * Pointer to archive.
 * \param[in] file_pos
 * Position of start of end of central directory in the archive.
 * \param[out] p_found
 * Pointer to returned ZIP64 central directory locator found flag.
 *
 * \return
 * \c TRUE if archive was searched without problems, else \c FALSE.
 */
extern
Bool zar_find_zip64_cdir_locator(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/
  Hq32x2*         file_pos,
/*@out@*/ /*@notnull@*/
  Bool*           p_found);

/**
 * \brief Read a ZIP64 central directory locator record.
 *
 * \param[in] p_archive
 * Pointer to archive.
 * \param[out] p_z64_cdir_locator
 * Pointer to returned ZIP64 central directory locator structure.
 *
 * \return
 * \c TRUE if locator was read successfully, else \c FALSE.
 */
extern
Bool zar_read_zip64_cdir_locator(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@out@*/ /*@notnull@*/
  ZIP_ZIP64_CDIR_LOC* p_z64_cdir_locator);

/**
 * \brief Create a ZIP64 central directory locator record.
 *
 * \param[out] buffer
 * Pointer to buffer to hold the ZIP64 central directory locator record, including header.
 * \param[in] p_z64_cdir_locator
 * Pointer to ZIP64 central directory locator structure.
 *
 * \return
 * \c TRUE if locator was read successfully, else \c FALSE.
 */
extern
void zar_create_zip64_cdir_locator(
/*@out@*/ /*@notnull@*/
  uint8             buffer[ZAR_ZIP64_CDIRLOCATOR_RECSIZE],
/*@in@*/ /*@notnull@*/
  ZIP_ZIP64_CDIR_LOC* p_z64_cdir_locator);

/**
 * \brief Write a ZIP64 central directory locator record to the archive.
 *
 * \param[in] p_archive
 * Pointer to archive to add the record to.
 * \param[in] p_z64_cdir_locator
 * Pointer to a ZIP64 central directory locator structure.
 *
 * \return
 * \c TRUE if record successfully written to the archive, else \c FALSE.
 */
extern
Bool zar_write_zip64_cdir_locator(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/
  ZIP_ZIP64_CDIR_LOC* p_z64_cdir_locator);

/**
 * \brief Read a ZIP64 end of central directory record.
 *
 * \param[in] p_archive
 * Pointer to archive.
 * \param[out] p_z64_end_cdir
 * Pointer to returned ZIP64 end of central directory structure.
 *
 * \return
 * \c TRUE if locator was read successfully, else \c FALSE.
 */
extern
Bool zar_read_zip64_end_cdir(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@out@*/ /*@notnull@*/
  ZIP_ZIP64_END_CDIR* p_z64_end_cdir);

/**
 * \brief Create a ZIP64 end of central directory record.
 *
 * \param[out] buffer
 * Pointer to buffer to hold the ZIP64 end of central directory record, including header.
 * \param[in] p_z64_end_cdir
 * Pointer to ZIP64 end of central directory structure.
 *
 * \return
 * \c TRUE if locator was read successfully, else \c FALSE.
 */
extern
void zar_create_zip64_end_cdir(
/*@out@*/ /*@notnull@*/
  uint8           buffer[ZAR_ZIP64_ENDCDIR_RECSIZE],
/*@in@*/ /*@notnull@*/
  ZIP_ZIP64_END_CDIR* p_z64_end_cdir);

/**
 * \brief Write a ZIP64 end of central directory record to the archive.
 *
 * \param[in] p_archive
 * Pointer to archive to add the record to.
 * \param[in] p_z64_end_cdir
 * Pointer to a ZIP64 end of central directory structure.
 *
 * \return
 * \c TRUE if record successfully written to the archive, else \c FALSE.
 */
extern
Bool zar_write_zip64_end_cdir(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/
  ZIP_ZIP64_END_CDIR* p_z64_end_cdir);

#endif /* !__ZIP_ARCHIVE_H__ */

/* Log stripped */
