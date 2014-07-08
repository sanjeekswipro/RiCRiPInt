/** \file
 * \ingroup zipdev
 *
 * $HopeName: COREzipdev!src:zip_archive.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * ZIP archive reader.
 */

#include "core.h"

#include "hqmemcpy.h"     /* HqMemCpy */
#include "hqmemset.h"

#include "objects.h"
#include "fileio.h"       /* FILELIST */
#include "stacks.h"
#include "miscops.h"      /* run_ps_string */
#include "swerrors.h"
#include "progress.h"     /* setReadFileProgress */

#include "objnamer.h"     /* OBJECT_NAME_MEMBER */

#include "zip_util.h"     /* zutl_dev_bufsize */
#include "zip_archive.h"


/** \brief Default archive reading buffer size. */
#define ZIP_ARCHIVE_BUFFER_SIZE   (16384)

/** \brief Size of a ZIP archive record signature. */
#define ZIP_SIG_SIZE              (4)

/* Offsets to fields in local file header record after signature */
/** \brief Offset to version needed field. */
#define LCALFILE_VERSNEEDED       (0)
/** \brief Offset to general flags field. */
#define LCALFILE_FLAGS            (2)
/** \brief Offset to compression id field. */
#define LCALFILE_COMPRESSION      (4)
/** \brief Offset to modification time field. */
#define LCALFILE_MODTIME          (6)
/** \brief Offset to modification date field. */
#define LCALFILE_MODDATE          (8)
/** \brief Offset to CRC32 checksum field. */
#define LCALFILE_CRC32            (10)
/** \brief Offset to compressed file size field. */
#define LCALFILE_COMPRESSEDSIZE   (14)
/** \brief Offset to uncompressed file size field. */
#define LCALFILE_UNCOMPRESSEDSIZE (18)
/** \brief Offset to file name length field. */
#define LCALFILE_NAMELENGTH       (22)
/** \brief Offset to extras field length field. */
#define LCALFILE_EXTRASLENGTH     (24)
/** \brief Offset to start of the file name. */
#define LCALFILE_FILENAME         (26)

/* Offsets to fields in file data descriptor record after signature */
/** \brief Offset to CRCC32 checksum field. */
#define DATADESC_CRC32            (0)
/** \brief Offset to compressed file size field. */
#define DATADESC_COMPRESSEDSIZE   (4)
/** \brief Offset to uncompressed file size field. */
#define DATADESC_UNCOMPRESSEDSIZE (8)

/* Offsets to fields in ZIP64 file data descriptor record after signature */
/** \brief Offset to CRCC32 checksum field. */
#define DATADESC_ZIP64_CRC32      (0)
/** \brief Offset to compressed file size field. */
#define DATADESC_ZIP64_COMPRESSEDSIZE (4)
/** \brief Offset to uncompressed file size field. */
#define DATADESC_ZIP64_UNCOMPRESSEDSIZE (12)

/* Offsets to fields in extra field header */
/** \brief Offset to extra field header id. */
#define XTRFLD_HEADER_ID          (0)
/** \brief Offset to extra field data size. */
#define XTRFLD_DATA_SIZE          (2)

/* Offsets to fields in ZIP64 extra field record including header */
/** \brief Offset to ZIP64 extra field uncompressed file size. */
#define XTRA_ZIP64_UNCOMPRESSED   (4)
/** \brief Offset to ZIP64 extra field compressed file size. */
#define XTRA_ZIP64_COMPRESSED     (12)
/** \brief Offset to ZIP64 extra field local file header offset. */
#define XTRA_ZIP64_OFFSET         (20)
/** \brief Offset to ZIP64 extra field file start disk. */
#define XTRA_ZIP64_STARTDISK      (28)

/* Offsets to fields in central directory file header record after signature */
/** \brief Offset to made by version field. */
#define CDIRFILE_MADEBYVERS       (0)
/** \brief Offset to version needed field. */
#define CDIRFILE_VERSNEEDED       (2)
/** \brief Offset to general flags field. */
#define CDIRFILE_FLAGS            (4)
/** \brief Offset to compression if field. */
#define CDIRFILE_COMPRESSION      (6)
/** \brief Offset to modification time field. */
#define CDIRFILE_MODTIME          (8)
/** \brief Offset to modification date field. */
#define CDIRFILE_MODDATE          (10)
/** \brief Offset to CRC32 checksum field. */
#define CDIRFILE_CRC32            (12)
/** \brief Offset to compressed file size field. */
#define CDIRFILE_COMPRESSEDSIZE   (16)
/** \brief Offset to uncompressed file size field. */
#define CDIRFILE_UNCOMPRESSEDSIZE (20)
/** \brief Offset to file name length field. */
#define CDIRFILE_NAMELENGTH       (24)
/** \brief Offset to extras field length field. */
#define CDIRFILE_EXTRASLENGTH     (26)
/** \brief Offset to file comment length field. */
#define CDIRFILE_COMMENTLENGTH    (28)
/** \brief Offset to local file header archive number field. */
#define CDIRFILE_STARTDISK        (30)
/** \brief Offset to internal attributes field. */
#define CDIRFILE_INTERNALATTR     (32)
/** \brief Offset to external attributes field. */
#define CDIRFILE_EXTERNALATTR     (34)
/** \brief Offset to local file header archive offset field. */
#define CDIRFILE_LCALFILOFFSET    (38)
/** \brief Offset to start of the file name. */
#define CDIRFILE_FILENAME         (42)


/** \brief Offset to ZIP64 size of end of central directory record. */
#define ZIP64_ENDCDIR_ENDSIZE     (0)
/** \brief Offset to ZIP64 archive made by version. */
#define ZIP64_ENDCDIR_MADEBYVERS  (8)
/** \brief Offset to ZIP64 version needed to extract. */
#define ZIP64_ENDCDIR_VERSNEEDED  (10)
/** \brief Offset to ZIP64 this disk number. */
#define ZIP64_ENDCDIR_THISDISKNUM (12)
/** \brief Offset to ZIP64 disk number where central directory starts. */
#define ZIP64_ENDCDIR_STARTDISKNUM (16)
/** \brief Offset to ZIP64 number of central directory entries on this disk. */
#define ZIP64_ENDCDIR_ENTRIESTHISDISK (20)
/** \brief Offset to ZIP64 total number of entries in central directory. */
#define ZIP64_ENDCDIR_ENTRIESTOTAL (28)
/** \brief Offset to ZIP64 size of central directory. */
#define ZIP64_ENDCDIR_CDIRSIZE    (36)
/** \brief Offset to ZIP64 start of central directory offset. */
#define ZIP64_ENDCDIR_CDIROFFSET  (44)

/** \brief Offset to number of disk with the start of the ZIP64 end of central directory. */
#define ZIP64_CDIRLOC_STARTDISKNUM (0)
/** \brief Offset to start of ZIP64 end of central directory record. */
#define ZIP64_CDIRLOC_ENDCDIROFFSET (4)
/** \brief Offset to total number of disks in the archive. */
#define ZIP64_CDIRLOC_DISKSTOTAL  (12)

/* Offsets to fields in end central directory record after signature */
/** \brief Offset to current archive number field. */
#define ENDCDIR_THISDISKNUM       (0)
/** \brief Offset to central directory start archive number field. */
#define ENDCDIR_CDIRSTARTDISKNUM  (2)
/** \brief Offset to number of central directory file headers on this archive field. */
#define ENDCDIR_CDIRENTRIESTHISDISK (4)
/** \brief Offset to toal number of central directory file headers field. */
#define ENDCDIR_CDIRENTRIESTOTAL  (6)
/** \brief Offset to central directory size field. */
#define ENDCDIR_CDIRSIZE          (8)
/** \brief Offset to offset of start of central directory field. */
#define ENDCDIR_CDIROFFSET        (12)
/** \brief Offset to archive comment length field. */
#define ENDCDIR_COMMENTLENGTH     (16)
/** \brief Offset to start of archive comment. */
#define ENDCDIR_COMMENT           (18)

/** \brief ZIP archive structure name used to generate hash checksum. */
#define ZIP_ARCHIVE_OBJECT_NAME   "ZIP Archive Device"


/* Initialise ZIP archive file structure */
void zar_init(
/*@out@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive)
{
  HQASSERT((p_archive != NULL),
           "zar_init: NULL ZIP archive pointer");

  NAME_OBJECT(p_archive, ZIP_ARCHIVE_OBJECT_NAME);

  /* Initialise state to closed archive */
  p_archive->flptr = NULL;
  p_archive->flags = 0;
  p_archive->bufsize = 0;
  Hq32x2FromInt32(&p_archive->extent, 0);

} /* zar_init */


/**
 * \brief Update archive state for file object.
 *
 * \param[out] p_archive
 * Pointer to archive state to update.
 * \param[in] p_fileo
 * Pointer to open PS file object to update archive state from.
 */
static
void zar_init_file(
/*@out@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@in@*/ /*@notnull@*/
  OBJECT*       p_fileo)
{
  DEVICELIST* device;

  HQASSERT((p_archive != NULL),
           "zar_init_file: NULL archive pointer");
  HQASSERT((p_fileo != NULL),
           "zar_init_file: NULL archive file object pointer");
  HQASSERT((oType(*p_fileo) == OFILE),
           "zar_init_file: file object is not a file");

  /* The archive is effectively open. We need to close the archive file for
   * any error from now on.  Attach file to archive state */
  p_archive->flags |= ZIP_ARCHIVE_OPEN;
  p_archive->flptr = oFile(*p_fileo);

  /* Add ZIP archive to list of files progress tracked */
  setReadFileProgress(p_archive->flptr);

  /* Get device preferred buffer size for reading the archive */
  device = theIDeviceList(p_archive->flptr);
  p_archive->bufsize = zutl_dev_bufsize(device, 4096, ZIP_ARCHIVE_BUFFER_SIZE);

} /* zar_init_file */


/* Device status for device containing archive. */
int32 zar_dev_status(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@out@*/ /*@notnull@*/
  DEVSTAT*      devstat)
{
  HQASSERT((p_archive != NULL),
           "zar_dev_status: NULL ZIP archive pointer");

  VERIFY_OBJECT(p_archive, ZIP_ARCHIVE_OBJECT_NAME);

  return((theIStatusDevice(theIDeviceList(p_archive->flptr)))(theIDeviceList(p_archive->flptr), devstat));

} /* zar_dev_status */


/** \brief Open archive file for writing. */
#define ZAR_OPEN_WRITE  (0)
/** \brief Open archive file for reading. */
#define ZAR_OPEN_READ   (1)

/**
 * \brief Return a file object from opening the named file.
 *
 * \param[in] filename
 * Pointer to archive filename.  The filename should be a PS filename ([%dev%]filename)
 * \param[in] name_len
 * Length of archive filename.
 * \param[in] open_mode
 * Flag indicating if the archive should be opened for reading or writing -
 * ZAR_OPEN_READ or ZAR_OPEN_WRITE.
 * \param[out] p_fileo
 * Pointer to returned PS fileobject.
 *
 * \return
 * \c TRUE if the file is opened successfully, else \c FALSE.
 */
static
Bool zar_fileopen(
/*@in@*/ /*@notnull@*/
  uint8*  filename,
  uint32  name_len,
  int32   open_mode,
/*@out@*/ /*@notnull@*/
  OBJECT* p_fileo)
{
  corecontext_t *context = get_core_context_interp();
  Bool    gallocmode;
  Bool    rc;
  OBJECT  theo = OBJECT_NOTVM_NOTHING ;
  static int32 fs_flags[2] = {SW_WRONLY|SW_TRUNC|SW_CREAT|SW_EXCL, SW_RDONLY};
  static int32 ps_flags[2] = {WRITE_FLAG, READ_FLAG};

  HQASSERT((filename != NULL),
           "zar_fileopen: NULL pointer to filenae");
  HQASSERT((name_len > 0),
           "zar_fileopen: invalid filename length");
  HQASSERT(((open_mode == ZAR_OPEN_WRITE) || (open_mode == ZAR_OPEN_READ)),
           "zar_fileopen: invalid file open mode");
  HQASSERT((p_fileo != NULL),
           "zar_fileopen: NULL pointer to returned file object");

  gallocmode = setglallocmode(context, open_mode == ZAR_OPEN_WRITE);

  theTags(theo) = OSTRING|UNLIMITED|LITERAL;
  SETGLOBJECTTO(theo, gallocmode);
  theLen(theo) = CAST_TO_UINT16(name_len);
  oString(theo) = filename;

  /* Open archive with appropriate flags */
  rc = file_open(&theo, fs_flags[open_mode], ps_flags[open_mode], FALSE, 0, p_fileo);

  setglallocmode(context, gallocmode);

  return(rc);

} /* zar_fileopen */


/**
 * \brief Check if the archive file has to be read as a stream.
 *
 * \param[out] p_archive
 * Pointer to archive to check the archive source file.
 * \param[in] as_stream
 * Force reading the archive source as a stream.
 *
 * \return
 * \c TRUE if archive file stream state set up, or \c FALSE if there is a
 * problem finding the extent of a seekable archive.
 */
static
Bool zar_check_streamed(
/*@out@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
  Bool          as_stream)
{
  DEVICELIST* device;
  DEVICE_FILEDESCRIPTOR fd;

  HQASSERT((p_archive != NULL),
           "zar_check_streamed: NULL archive pointer");
  HQASSERT((!zar_creating(p_archive)),
           "zar_check_streamed: checking input streaming on output archive");

  /* Reading an existing archive - see if complete or streamed */
  if ( !as_stream ) {
    /* Don't want a stream but need to check if archive is being streamed */
    if ( file_seekable(p_archive->flptr) ) {
      /* Find end position of archive */
      device = theIDeviceList(p_archive->flptr);
      fd = theIDescriptor(p_archive->flptr);
      if ( !(*theIBytesFile(device))(device, fd, &p_archive->extent, SW_BYTES_TOTAL_ABS) ) {
        HQFAIL("Finding extent of seekable ZIP archive failed?");
        (void)zar_close(p_archive, CLOSE_EXPLICIT);
        return FAILURE(FALSE);
      }
      /* Flag archive data as being completely available */
      p_archive->flags |= ZIP_ARCHIVE_COMPLETE;
    }
  }

  if ( !zar_complete(p_archive) ) {
    /* If archive is not complete then must be being streamed */
    p_archive->flags |= ZIP_ARCHIVE_STREAMED;
  }

  return(TRUE);

} /* zar_check_streamed */


/* Open archive from running executable PS. */
Bool zar_open_ps(
/*@out@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@in@*/ /*@notnull@*/
  uint8*        ps,
  uint32        ps_len,
  Bool          as_stream)
{
  OBJECT*   p_fileo;
  FILELIST* flptr;

  HQASSERT((p_archive != NULL),
           "zar_open_ps: NULL archive pointer");
  HQASSERT((ps != NULL),
           "zar_open_ps: NULL PS string pointer");
  HQASSERT((ps_len > 0),
           "zar_open_ps: invalid PS string length");

  /* Execute string to produce a file object */
  if ( !run_ps_string_len(ps, ps_len) ) {
    return(FALSE);
  }

  /* Check top of stack is open readable file */
  if ( isEmpty(operandstack) ) {
    return(FALSE);
  }
  p_fileo = theTop(operandstack);
  if ( oType(*p_fileo) != OFILE ) {
    return FAILURE(FALSE);
  }
  flptr = oFile(*p_fileo);
  if ( !isIOpenFileFilter(p_fileo, flptr) || !isIInputFile(flptr) || isIEof(flptr) ) {
    return FAILURE(FALSE);
  }

  /* Pop the file off the stack. */
  pop(&operandstack);

  /* Initialise state from file object */
  zar_init_file(p_archive, p_fileo);

  /* Check if archive is to be stream consumed */
  if ( !zar_check_streamed(p_archive, as_stream) ) {
    return(FALSE);
  }

  return(TRUE);

} /* zar_open_ps */


/* Open archive from file name. */
Bool zar_open_file(
/*@out@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@in@*/ /*@notnull@*/
  uint8*        archive_name,
  uint32        name_len,
  Bool          as_stream)
{
  OBJECT  fileo = OBJECT_NOTVM_NOTHING ;

  HQASSERT((p_archive != NULL),
           "zar_open_file: NULL archive pointer");
  HQASSERT((archive_name != NULL),
           "zar_open_file: NULL archive name pointer");
  HQASSERT((name_len > 0),
           "zar_open_file: invalid archive name length");

  /* Get readable file object for archive filename */
  if ( !zar_fileopen(archive_name, name_len, ZAR_OPEN_READ, &fileo) ) {
    return(FALSE);
  }

  /* Initialise state from file object */
  zar_init_file(p_archive, &fileo);

  /* Check if archive is to be stream consumed */
  if ( !zar_check_streamed(p_archive, as_stream) ) {
    return(FALSE);
  }

  return(TRUE);

} /* zar_open_file */


/* Open archive with no archive source. */
void zar_open_empty(
/*@out@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive)
{
  HQASSERT((p_archive != NULL),
           "zar_open_empty: NULL archive pointer");

  /* No ZIP archive to read - implicitly open and complete */
  p_archive->flags |= ZIP_ARCHIVE_OPEN;
  p_archive->flags |= ZIP_ARCHIVE_COMPLETE;

} /* zar_open_empty */


/* Open new archive ready to create content. */
Bool zar_open_create(
/*@out@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@in@*/ /*@notnull@*/
  uint8*        archive_name,
  uint32        name_len)
{
  OBJECT  fileo = OBJECT_NOTVM_NOTHING ;

  HQASSERT((p_archive != NULL),
           "zar_open_create: NULL archive pointer");
  HQASSERT((archive_name != NULL),
           "zar_open_create: NULL archive name pointer");
  HQASSERT((name_len > 0),
           "zar_open_create: invalid archive name len");

  /* Get writeable file object for archive filename */
  if ( !zar_fileopen(archive_name, name_len, ZAR_OPEN_WRITE, &fileo) ) {
    return(FALSE);
  }

  /* Initialise state from file object */
  zar_init_file(p_archive, &fileo);

  /* Mark archive as being created. */
  p_archive->flags |= ZIP_ARCHIVE_CREATING;

  return(TRUE);

} /* zar_open_create */


/* Close an open archive. */
int32 zar_close(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
  int32         flag)
{
  HQASSERT((p_archive != NULL),
           "zar_close: NULL archive pointer");
  HQASSERT((zar_opened(p_archive)),
           "zar_close: archive is not open");
  /* When the zip archive is being read from a stream, its not up to
     the ZIP device to close the file. */
  HQASSERT((!zar_streamed(p_archive) || (flag != CLOSE_IMPLICIT)),
           "zar_close: closing archive file implicitly");

  VERIFY_OBJECT(p_archive, ZIP_ARCHIVE_OBJECT_NAME);

  /* Flag archive is no longer open */
  p_archive->flags &= ~ZIP_ARCHIVE_OPEN;

  /* Progress tracking is updated and closed as part of closing the file. */
  if ( p_archive->flptr != NULL ) {
    return((*theIMyCloseFile(p_archive->flptr))(p_archive->flptr, flag));
  }
  p_archive->flptr = NULL;
  return(0);

} /* zar_close */


/* Reposition the current point on the archive file. */
Bool zar_set_pos(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@in@*/ /*@notnull@*/
  Hq32x2*       p_offset)
{
  HQASSERT((p_archive != NULL),
           "zar_set_pos: NULL archive pointer");
  HQASSERT((!zar_streamed(p_archive)),
           "zar_set_pos: archive is not seekable");

  VERIFY_OBJECT(p_archive, ZIP_ARCHIVE_OBJECT_NAME);

  if ((*theIMyResetFile(p_archive->flptr))(p_archive->flptr) == EOF) {
    return (*theIFileLastError(p_archive->flptr))(p_archive->flptr);
  }

  return((*theIMySetFilePos(p_archive->flptr))(p_archive->flptr, p_offset) == 0);
} /* zar_set_pos */


/* Get the current file position on the archive file. */
Bool zar_get_pos(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@out@*/ /*@notnull@*/
  Hq32x2*       p_offset)
{
  HQASSERT((p_archive != NULL),
           "zar_get_pos: NULL archive pointer");
  HQASSERT((zar_creating(p_archive) || !zar_streamed(p_archive)),
           "zar_get_pos: archive is not seekable");

  VERIFY_OBJECT(p_archive, ZIP_ARCHIVE_OBJECT_NAME);

  Hq32x2FromInt32(p_offset, 0);
  return((*theIMyFilePos(p_archive->flptr))(p_archive->flptr, p_offset) == 0);

} /* zar_get_pos */

/* Read data directly from the archive file. */
int32 zar_read_raw(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@out@*/ /*@notnull@*/
  uint8*        buffer,
  int32         len)
{
  int32  bytes;

  HQASSERT((p_archive != NULL),
           "zar_read_raw: NULL archive pointer");
  HQASSERT((buffer != NULL),
           "zar_read_raw: NULL buffer pointer");
  HQASSERT((len >= 0),
           "zar_read_raw: invalid read length");

  VERIFY_OBJECT(p_archive, ZIP_ARCHIVE_OBJECT_NAME);

  if ( file_read(p_archive->flptr, buffer, len, &bytes) == 0 )
    return FAILURE(-1);

  /* Update archive reading progress */
  {
    static int32 urfp = 0;

    /**
     * We seem to be doing zillions of calls to updateReadFileProgress and
     * are swamping the RDR/Event system. Probably need some time based
     * throttling, but for simplicity just thrown away 99% of them. This
     * solves the performance issue, but I think we are getting loads of
     * single byte reads here, so there may be a deeper issue ?
     *
     * \todo BMJ 07-Feb-14 : Better RDR throttling.
     */
    if ( urfp++ == 100 ) {
      updateReadFileProgress();
      urfp = 0;
    }
  }
  /* Return number of bytes read */
  return bytes ;
} /* zar_read_raw */


/**
 * \brief Read an exact amount of archive data.
 *
 * \param[in] p_archive
 * Pointer to archive.
 * \param[out] buffer
 * Pointer to buffer to read data into.
 * \param[in] len
 * Number of bytes to read.
 *
 * \return
 * \c TRUE if amount of archive data successfully read, else \c FALSE.
 */
static
int32 zar_read_raw_exact(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@out@*/ /*@notnull@*/
  uint8*        buffer,
  int32         len)
{
  int32 bytes_read;

  HQASSERT((p_archive != NULL),
           "zar_read_raw_exact: NULL archive pointer");
  HQASSERT((buffer != NULL),
           "zar_read_raw_exact: NULL buffer pointer");
  HQASSERT((len > 0),
           "zar_read_raw_exact: invalid read length");

  VERIFY_OBJECT(p_archive, ZIP_ARCHIVE_OBJECT_NAME);

  /* Read requested amount of archive data, no less */
  do {
    bytes_read = zar_read_raw(p_archive, buffer, len);
    if ( bytes_read <= 0 ) {
      return(FALSE);
    }
    len -= bytes_read;
  } while ( len > 0 );

  return(TRUE);

} /* zar_read_raw_exact */


/**
 * \brief Skip over an amount archive data.
 *
 * \param[in] p_archive
 * Pointer to archive.
 * \param[in] bytes
 * Number of bytes to skip over.
 *
 * \return
 * \c TRUE if skipped archive data successfully, else \c FALSE.
 */
static
Bool zar_skip_raw(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
  int32         bytes)
{
  int32 buf_size;
  uint8 buffer[ZIP_ARCHIVE_BUFFER_SIZE];

  HQASSERT((p_archive != NULL),
           "zar_skip_raw: NULL context pointer");
  HQASSERT((bytes >= 0),
           "zar_skip_raw: invalid byte count");

  VERIFY_OBJECT(p_archive, ZIP_ARCHIVE_OBJECT_NAME);

  /* No-op check */
  if ( bytes == 0 ) {
    return(TRUE);
  }

  /* Consume the required number of bytes */
  do {
    buf_size = min(bytes, ZIP_ARCHIVE_BUFFER_SIZE);
    if ( !zar_read_raw_exact(p_archive, buffer, buf_size) ) {
      return(FALSE);
    }
    bytes -= buf_size;
  } while ( bytes > 0 );

  return(TRUE);

} /* zar_skip_raw */


/* Write data directly to an archive file. */
int32 zar_write_raw(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@in@*/ /*@notnull@*/
  uint8*        buffer,
  int32         len)
{
  DEVICE_FILEDESCRIPTOR fd;
  DEVICELIST* device;

  HQASSERT((p_archive != NULL),
           "zar_write_raw: NULL archive pointer");
  HQASSERT((buffer != NULL),
           "zar_write_raw: NULL buffer pointer");
  HQASSERT((len >= 0),
           "zar_write_raw: invalid read length");
  HQASSERT((zar_creating(p_archive)),
           "zar_write_raw: archive not open for creation");

  VERIFY_OBJECT(p_archive, ZIP_ARCHIVE_OBJECT_NAME);

  /* Write directly to the archive file */
  device = theIDeviceList(p_archive->flptr);
  fd = theIDescriptor(p_archive->flptr);
  return((*theIWriteFile(device))(device, fd, buffer, len));

} /* zar_write_raw */


/* PKZIP archive structure signatures.  These are the basic ones from v4.5
 * Note: 0x504b is PK ;-)
 */
static uint8 pksig_lcal_file[ZIP_SIG_SIZE] = {0x50, 0x4b, 0x03, 0x04};
static uint8 pksig_datadesc[ZIP_SIG_SIZE]  = {0x50, 0x4b, 0x07, 0x08};
static uint8 pksig_cdir_file[ZIP_SIG_SIZE] = {0x50, 0x4b, 0x01, 0x02};
static uint8 pksig_endcdir[ZIP_SIG_SIZE] = {0x50, 0x4b, 0x05, 0x06};
static uint8 pksig_zip64_endcdir[ZIP_SIG_SIZE] = {0x50, 0x4b, 0x06, 0x06};
static uint8 pksig_zip64_cdirloc[ZIP_SIG_SIZE] = {0x50, 0x4b, 0x06, 0x07};

/* These are the extra signatures that are not currently supported */
#if 0
static uint8 pksig_file_digitalsig[ZIP_SIG_SIZE] = {0x50, 0x4b, 0x05, 0x05};
static uint8 pksig_archive_xtra_data[ZIP_SIG_SIZE] = {0x50, 0x4b, 0x06, 0x08};
#endif

/* Lead byte for record signatures - P in ASCII */
#define ZIP_SIG_P  (0x50)

/**
 * \brief Array of valid signatures to use when checking what the next record
 * sig is.
 */
static uint32 pksigs[] = {
  PKSIG_LCAL_FILE,
  PKSIG_CDIR_FILE,
  PKSIG_ENDCDIR,
  PKSIG_DATADESC,
  PKSIG_ZIP64_ENDCDIR,
  PKSIG_ZIP64_LOCATOR
};

#define NUM_PKSIGS  NUM_ARRAY_ITEMS(pksigs)

/**
 * \brief
 *
 * \param[in] buffer
 * Pointer to buffer containing 4 bytes to check.
 * \param[out] p_sig
 * Pointer to returned signature id if found.
 *
 * \return
 * \c TRUE if a record signature is matched, else \c FALSE.
 */
static
Bool match_sig(
  uint8   buffer[ZIP_SIG_SIZE],
  uint32* p_sig)
{
  int32   i_pksigs;
  uint32  sig;

  HQASSERT((buffer != NULL),
           "match_sig: NULL buffer pointer");
  HQASSERT((p_sig != NULL),
           "match_sig: NULL returned sig pointer");

  /* Detect sig of next record */
  sig = READ_LONG(buffer);
  i_pksigs = 0;
  do {
    if ( sig == pksigs[i_pksigs] ) {
      *p_sig = sig;
      return(TRUE);
    }
  } while ( ++i_pksigs < NUM_PKSIGS );

  return(FALSE);

} /* match_sig */


/* Read and identify the next signature in an archive. */
Bool zar_read_sig(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@out@*/ /*@notnull@*/
  uint32*       p_sig)
{
  uint8   buffer[ZIP_SIG_SIZE];

  HQASSERT((!zar_creating(p_archive)),
           "zar_read_sig: archive open for creating only");

  if ( !zar_read_raw_exact(p_archive, buffer, ZIP_SIG_SIZE) ) {
    return(FALSE);
  }

  return(match_sig(buffer, p_sig));

} /* zar_read_sig */


/* Find and identify the next signature in an archive.
 * Use a 4-byte buffer so that the archive file pointer is left pointing to
 * the start of the record content.
 * Algorithm is:
 * o Look for a 'P'
 * o Reallign the 'P' with the start of the buffer (shuffle rest of buffer
 *   down and top up to 4 bytes again.)
 * o Look for signature.
 * o Continue search immediately after the 'P' found.
 * o Rinse and repeat until find signature or EOF.
 */
Bool zar_next_sig(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@out@*/ /*@notnull@*/
  uint32*       p_sig)
{
  int32   i;
  uint8   buffer[ZIP_SIG_SIZE];

  HQASSERT((!zar_creating(p_archive)),
           "zar_next_sig: archive open for creating only");

  for (;;) {
    if ( !zar_read_raw_exact(p_archive, buffer, ZIP_SIG_SIZE) ) {
      return(FALSE);
    }

    i = 0;
    do {
      if ( buffer[i] == ZIP_SIG_P ) {
        if ( i > 0 ) {
          HqMemMove(buffer, &buffer[i], ZIP_SIG_SIZE - i);
          if ( !zar_read_raw_exact(p_archive, &buffer[ZIP_SIG_SIZE - i], i) ) {
            return(FALSE);
          }
        }
        if ( match_sig(buffer, p_sig) ) {
          return(TRUE);
        }
        i = 0;
      }
    } while ( ++i < ZIP_SIG_SIZE );
  }
  /* NEVERREACHED */

} /* zar_next_sig */


/* Read and match a signature from an archive. */
Bool zar_match_sig(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@out@*/ /*@notnull@*/
  uint32        sig)
{
  uint8   buffer[ZIP_SIG_SIZE];

  HQASSERT((!zar_creating(p_archive)),
           "zar_match_sig: archive open for creating only");

  if ( !zar_read_raw_exact(p_archive, buffer, ZIP_SIG_SIZE) ) {
    return(FALSE);
  }

  return(READ_LONG(buffer) == sig);

} /* zar_match_sig */


/* Find the start of the central directory end record. */
Bool zar_find_end_cdir(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@out@*/ /*@notnull@*/
  Hq32x2*       file_pos)
{
  int32   f_endcdir_found;
  int32   buf_size;
  uint8*  p_b;
  uint8   buffer[ZIP_ARCHIVE_BUFFER_SIZE + 3];

  HQASSERT((p_archive != NULL),
           "zar_find_end_cdir: NULL archive pointer");
  HQASSERT((file_pos != NULL),
           "zar_find_end_cdir: NULL returned file position pointer");
  HQASSERT((!zar_streamed(p_archive)),
           "zar_find_end_cdir: archive file is not seekable");
  HQASSERT((!zar_creating(p_archive)),
           "zar_find_end_cdir: archive open for creating only");

  VERIFY_OBJECT(p_archive, ZIP_ARCHIVE_OBJECT_NAME);

  /* Now search backward through file for end central directory signature.
   * Since the signature could span the boundary between blocks scanned we
   * append the first three bytes of the previous block to the end of what we
   * read in.  Initialise to nulls to prevent random data causing a match for
   * the first block. */
  HqMemZero(buffer, ZIP_ARCHIVE_BUFFER_SIZE + 3);
  buf_size = ZIP_ARCHIVE_BUFFER_SIZE;
  f_endcdir_found = FALSE;
  *file_pos = p_archive->extent;
  do {
    /* Move back a buffers worth */
    Hq32x2SubtractInt32(file_pos, file_pos, ZIP_ARCHIVE_BUFFER_SIZE);
    if ( Hq32x2Sign(file_pos) < 0 ) {
      /* Gone beyond start of file, reduce size of buffer to compensate, and set
       * seek position to start of file */
      if ( !Hq32x2ToInt32(file_pos, &buf_size) )
        return FAILURE(FALSE);

      buf_size += ZIP_ARCHIVE_BUFFER_SIZE;
      Hq32x2FromInt32(file_pos, 0);
      /* Copy first 3 bytes to after reduced buffer size in case signature
       * spanned blocks */
      p_b = &buffer[buf_size];
      *p_b++ = buffer[0];
      *p_b++ = buffer[1];
      *p_b   = buffer[2];
    }
    if ( !zar_set_pos(p_archive, file_pos) ) {
      return FAILURE(FALSE);
    }

    /* Read in a buffers worth */
    if ( !zar_read_raw_exact(p_archive, buffer, buf_size) ) {
      return(FALSE);
    }

    /* Search buffer for end central directory signature */
    p_b = buffer;
    do {
      if ( *p_b == 'P' ) {
        if ( p_b[1] == pksig_endcdir[1] ) {
          if ( p_b[2] == pksig_endcdir[2] ) {
            if ( p_b[3] == pksig_endcdir[3] ) {
              f_endcdir_found = TRUE;
              /*@innerbreak@*/
              break;
            }
            p_b++;
          }
          p_b++;
        }
      }
    } while ( ++p_b != &buffer[buf_size] );

    /* Copy first 3 bytes to end of buffer in case signature spanned blocks */
    if ( !f_endcdir_found ) {
      *p_b++ = buffer[0];
      *p_b++ = buffer[1];
      *p_b   = buffer[2];
    }
  } while ( !f_endcdir_found && !Hq32x2IsZero(file_pos) );

  if ( !f_endcdir_found ) {
    /* Did not find signature - corrupt archive methinks */
    return FAILURE(FALSE);
  }

  /* Reposition file after sig for end record ready to read the rest of it */
  Hq32x2AddInt32(file_pos, file_pos, (CAST_PTRDIFFT_TO_INT32(p_b - buffer) + ZIP_SIG_SIZE));
  return(zar_set_pos(p_archive, file_pos));

} /* zar_find_end_cdir */


/* Read the central directory end record. */
Bool zar_read_end_cdir(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@out@*/ /*@notnull@*/
  ZIP_END_CDIR* p_end_cdir)
{
  uint32  len;
  int32   bytes;
  uint8   buffer[ZAR_ENDCDIR_RECSIZE - ZIP_SIG_SIZE];

  HQASSERT((p_archive != NULL),
           "zar_read_end_cdir: NULL archive pointer");
  HQASSERT((p_end_cdir != NULL),
           "zar_read_end_cdir: NULL end record pointer");
  HQASSERT((!zar_creating(p_archive)),
           "zar_read_end_cdir: archive open for creating only");

  VERIFY_OBJECT(p_archive, ZIP_ARCHIVE_OBJECT_NAME);

  /* Read the end record (minus any comment) */
  if ( !zar_read_raw_exact(p_archive, buffer, (ZAR_ENDCDIR_RECSIZE - ZIP_SIG_SIZE)) ) {
    return(FALSE);
  }

  /* Extract the fields */
  p_end_cdir->disknum = READ_SHORT(&buffer[ENDCDIR_THISDISKNUM]);
  p_end_cdir->cdir_start_disknum = READ_SHORT(&buffer[ENDCDIR_CDIRSTARTDISKNUM]);
  p_end_cdir->cdir_entries_thisdisk = READ_SHORT(&buffer[ENDCDIR_CDIRENTRIESTHISDISK]);
  p_end_cdir->cdir_entries_total = READ_SHORT(&buffer[ENDCDIR_CDIRENTRIESTOTAL]);
  p_end_cdir->cdir_size = READ_LONG(&buffer[ENDCDIR_CDIRSIZE]);
  p_end_cdir->cdir_offset = READ_LONG(&buffer[ENDCDIR_CDIROFFSET]);
  p_end_cdir->comment_length = READ_SHORT(&buffer[ENDCDIR_COMMENTLENGTH]);

  /* Read any comment - use existing buffer to read over it in chunks, comment
   * may be shorter than declared, which we treat as ok */
  if ( p_end_cdir->comment_length > 0 ) {
    len = p_end_cdir->comment_length;
    do {
      bytes = min(len, ZAR_ENDCDIR_RECSIZE - ZIP_SIG_SIZE);
      if ( (bytes = zar_read_raw(p_archive, buffer, bytes)) < 0 ) {
        return(FALSE);
      }
    } while ( (bytes > 0) && (len -= bytes) > 0);
    /* Update comment length with actual length */
    p_end_cdir->comment_length = CAST_UNSIGNED_TO_UINT16(p_end_cdir->comment_length - len);
  }

  return(TRUE);

} /* zar_read_end_cdir */


/* Fill in the end of central directory record. */
Bool zar_create_end_cdir(
/*@out@*/ /*@notnull@*/
  uint8         buffer[ZAR_ENDCDIR_RECSIZE],
/*@in@*/ /*@notnull@*/
  ZIP_END_CDIR* p_end_cdir)
{
  uint8*  field;

  HQASSERT((buffer != NULL),
           "zar_create_end_cdir: NULL returned cdir end record buffer pointer");
  HQASSERT((p_end_cdir != NULL),
           "zar_create_end_cdir: NULL cdir end record pointer");

  /* Build end of central directory record */
  HqMemCpy(buffer, pksig_endcdir, ZIP_SIG_SIZE);
  field = &buffer[ZIP_SIG_SIZE];
  WRITE_SHORT(&field[ENDCDIR_THISDISKNUM], p_end_cdir->disknum);
  WRITE_SHORT(&field[ENDCDIR_CDIRSTARTDISKNUM], p_end_cdir->cdir_start_disknum);
  WRITE_SHORT(&field[ENDCDIR_CDIRENTRIESTHISDISK], p_end_cdir->cdir_entries_thisdisk);
  WRITE_SHORT(&field[ENDCDIR_CDIRENTRIESTOTAL], p_end_cdir->cdir_entries_total);
  WRITE_LONG(&field[ENDCDIR_CDIRSIZE], p_end_cdir->cdir_size);
  WRITE_LONG(&field[ENDCDIR_CDIROFFSET], p_end_cdir->cdir_offset);
  WRITE_SHORT(&field[ENDCDIR_COMMENTLENGTH], p_end_cdir->comment_length);

  return(TRUE);

} /* zar_create_end_cdir */


/* Write the central directory end record. */
Bool zar_write_end_cdir(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@in@*/ /*@notnull@*/
  ZIP_END_CDIR* p_end_cdir)
{
  uint8   buffer[ZAR_ENDCDIR_RECSIZE];

  /* Fill in cdir end record buffer */
  if ( !zar_create_end_cdir(buffer, p_end_cdir) ) {
    return(FALSE);
  }

  /* Write end of directory record to the archive */
  return(zar_write_raw(p_archive, buffer, sizeof(buffer)) == sizeof(buffer));

} /* zar_write_end_cdir */


/* Read a central directory file header record. */
Bool zar_read_cdir_file(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@out@*/ /*@notnull@*/
  ZIP_CDIR_FILE*  p_cdir_file,
/*@out@*/ /*@notnull@*/
  uint8*          filename,
/*@out@*/ /*@notnull@*/
  uint8*          extras)
{
  uint8   buffer[ZAR_CDIRFILE_RECSIZE - ZIP_SIG_SIZE];
  ZIP_LCAL_FILE* localFile;

  HQASSERT((p_archive != NULL),
           "zar_read_cdir_file: NULL archive pointer");
  HQASSERT((p_cdir_file != NULL),
           "zar_read_cdir_file: NULL central file header pointer");
  HQASSERT((filename != NULL),
           "zar_read_cdir_file: NULL filename pointer");
  HQASSERT((extras != NULL),
           "zar_read_cdir_file: NULL extras pointer");
  HQASSERT((!zar_creating(p_archive)),
           "zar_read_cdir_file: archive open for creating only");

  VERIFY_OBJECT(p_archive, ZIP_ARCHIVE_OBJECT_NAME);

  *filename = '\0';

  /* Read the end record (minus any comment) */
  if ( !zar_read_raw_exact(p_archive, buffer, (ZAR_CDIRFILE_RECSIZE - ZIP_SIG_SIZE)) ) {
    return(FALSE);
  }

  /* Read in the main part of the central directory file header */
  p_cdir_file->made_by_version = READ_SHORT(&buffer[CDIRFILE_MADEBYVERS]);

  localFile = &p_cdir_file->lcal_file;
  localFile->version_needed = READ_SHORT(&buffer[CDIRFILE_VERSNEEDED]);
  localFile->flags = READ_SHORT(&buffer[CDIRFILE_FLAGS]);
  localFile->compression = READ_SHORT(&buffer[CDIRFILE_COMPRESSION]);
  localFile->mod_time = READ_SHORT(&buffer[CDIRFILE_MODTIME]);
  localFile->mod_date = READ_SHORT(&buffer[CDIRFILE_MODDATE]);

  localFile->data_desc.crc_32 = READ_LONG(&buffer[CDIRFILE_CRC32]);
  localFile->data_desc.compressed = READ_LONG(&buffer[CDIRFILE_COMPRESSEDSIZE]);
  localFile->data_desc.uncompressed_size = READ_LONG(&buffer[CDIRFILE_UNCOMPRESSEDSIZE]);

  localFile->name_len = READ_SHORT(&buffer[CDIRFILE_NAMELENGTH]);
  localFile->extras_len = READ_SHORT(&buffer[CDIRFILE_EXTRASLENGTH]);

  p_cdir_file->comment_len = READ_SHORT(&buffer[CDIRFILE_COMMENTLENGTH]);
  p_cdir_file->start_disk_number = READ_SHORT(&buffer[CDIRFILE_STARTDISK]);
  p_cdir_file->internal_attributes = READ_SHORT(&buffer[CDIRFILE_INTERNALATTR]);
  p_cdir_file->external_attributes = READ_LONG(&buffer[CDIRFILE_EXTERNALATTR]);
  p_cdir_file->lcal_file_hdr_offset = READ_LONG(&buffer[CDIRFILE_LCALFILOFFSET]);

  /* Read in the filename */
  if ( (localFile->name_len == 0) ||
       (localFile->name_len > LONGESTFILENAME) ) {
    /* Filename is too long or came from stdin and therefore has no name! */
    return FAILURE(FALSE);
  }
  if ( !zar_read_raw_exact(p_archive, filename, localFile->name_len) ) {
    return(FALSE);
  }

  /* Read any extra fields */
  if ( (localFile->extras_len > 0) &&
       !zar_read_raw_exact(p_archive, extras, localFile->extras_len) ) {
    return(FALSE);
  }

  /* Skip the file comment */
  if ( !zar_skip_raw(p_archive, p_cdir_file->comment_len) ) {
    return(FALSE);
  }

  return(TRUE);

} /* zar_read_cdir_file */


void zar_create_cdir_file(
/*@out@*/ /*@notnull@*/
  uint8           buffer[ZAR_CDIRFILE_RECSIZE],
/*@in@*/ /*@notnull@*/
  ZIP_CDIR_FILE*  p_cdir_file)
{
  uint8*  field;
  ZIP_LCAL_FILE* lcal_file;

  /* Build central direcrtory file header */
  HqMemCpy(buffer, pksig_cdir_file, ZIP_SIG_SIZE);
  field = &buffer[ZIP_SIG_SIZE];
  WRITE_SHORT(&field[CDIRFILE_MADEBYVERS], p_cdir_file->made_by_version);

  lcal_file = &p_cdir_file->lcal_file;
  WRITE_SHORT(&field[CDIRFILE_VERSNEEDED], lcal_file->version_needed);
  WRITE_SHORT(&field[CDIRFILE_FLAGS], lcal_file->flags);
  WRITE_SHORT(&field[CDIRFILE_COMPRESSION], lcal_file->compression);
  WRITE_SHORT(&field[CDIRFILE_MODTIME], lcal_file->mod_time);
  WRITE_SHORT(&field[CDIRFILE_MODDATE], lcal_file->mod_date);

  WRITE_LONG(&field[CDIRFILE_CRC32], lcal_file->data_desc.crc_32);
  WRITE_LONG(&field[CDIRFILE_COMPRESSEDSIZE], lcal_file->data_desc.compressed);
  WRITE_LONG(&field[CDIRFILE_UNCOMPRESSEDSIZE], lcal_file->data_desc.uncompressed_size);

  WRITE_SHORT(&field[CDIRFILE_NAMELENGTH], lcal_file->name_len);
  WRITE_SHORT(&field[CDIRFILE_EXTRASLENGTH], lcal_file->extras_len);

  WRITE_SHORT(&field[CDIRFILE_COMMENTLENGTH], p_cdir_file->comment_len);
  WRITE_SHORT(&field[CDIRFILE_STARTDISK], p_cdir_file->start_disk_number);
  WRITE_SHORT(&field[CDIRFILE_INTERNALATTR], p_cdir_file->internal_attributes);
  WRITE_LONG(&field[CDIRFILE_EXTERNALATTR], p_cdir_file->external_attributes);

  WRITE_LONG(&field[CDIRFILE_LCALFILOFFSET], p_cdir_file->lcal_file_hdr_offset);

} /* zar_create_cdir_file */


/* Write a central directory file header record. */
Bool zar_write_cdir_file(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/
  ZIP_CDIR_FILE*  p_cdir_file,
/*@in@*/ /*@notnull@*/
  uint8*          filename,
/*@in@*/ /*@notnull@*/
  uint8*          extras)
{
  uint8   buffer[ZAR_CDIRFILE_RECSIZE];

  HQASSERT((filename != NULL),
           "zar_write_cdir_file: NULL filename pointer");
  HQASSERT(((p_cdir_file->lcal_file.extras_len == 0) || (extras != NULL)),
           "zar_write_cdir_file: NULL extras pointer");

  /* Write central directory file header and filename to the archive. */
  zar_create_cdir_file(buffer, p_cdir_file);
  return((zar_write_raw(p_archive, buffer, sizeof(buffer)) == sizeof(buffer)) &&
         (zar_write_raw(p_archive, filename,
                        p_cdir_file->lcal_file.name_len) == p_cdir_file->lcal_file.name_len) &&
         ((p_cdir_file->lcal_file.extras_len == 0) ||
          (zar_write_raw(p_archive, extras, p_cdir_file->lcal_file.extras_len) == p_cdir_file->lcal_file.extras_len)));

} /* zar_write_cdir_file */


/* Read a local file header record. */
Bool zar_read_lcal_file(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@out@*/ /*@notnull@*/
  ZIP_LCAL_FILE*  p_lcal_file,
/*@out@*/ /*@notnull@*/
  uint8*          filename,
/*@out@*/ /*@notnull@*/
  uint8*          extras)
{
  uint8 buffer[ZAR_LCALFILE_RECSIZE - ZIP_SIG_SIZE];

  HQASSERT((p_archive != NULL),
           "zar_read_lcal_file: NULL archive pointer");
  HQASSERT((p_lcal_file != NULL),
           "zar_read_lcal_file: NULL local file header pointer");
  HQASSERT((filename != NULL),
           "zar_read_lcal_file: NULL filename pointer");
  HQASSERT((!zar_creating(p_archive)),
           "zar_read_lcal_file: archive open for creating only");

  VERIFY_OBJECT(p_archive, ZIP_ARCHIVE_OBJECT_NAME);

  *filename = '\0';

  /* Read the local file header record (minus any extras) */
  if ( !zar_read_raw_exact(p_archive, buffer, (ZAR_LCALFILE_RECSIZE - ZIP_SIG_SIZE)) ) {
    return(FALSE);
  }

  p_lcal_file->version_needed = READ_SHORT(&buffer[LCALFILE_VERSNEEDED]);
  p_lcal_file->flags = READ_SHORT(&buffer[LCALFILE_FLAGS]);
  p_lcal_file->compression = READ_SHORT(&buffer[LCALFILE_COMPRESSION]);
  p_lcal_file->mod_time = READ_SHORT(&buffer[LCALFILE_MODTIME]);
  p_lcal_file->mod_date = READ_SHORT(&buffer[LCALFILE_MODDATE]);

  p_lcal_file->data_desc.crc_32 = READ_LONG(&buffer[LCALFILE_CRC32]);
  p_lcal_file->data_desc.compressed = READ_LONG(&buffer[LCALFILE_COMPRESSEDSIZE]);
  p_lcal_file->data_desc.uncompressed_size = READ_LONG(&buffer[LCALFILE_UNCOMPRESSEDSIZE]);

  p_lcal_file->name_len = READ_SHORT(&buffer[LCALFILE_NAMELENGTH]);
  p_lcal_file->extras_len = READ_SHORT(&buffer[LCALFILE_EXTRASLENGTH]);

  /* Read in the filename */
  if ( (p_lcal_file->name_len == 0) ||
       (p_lcal_file->name_len > LONGESTFILENAME) ) {
    /* Filename is too long or came from stdin and therefore has no name! */
    return FAILURE(FALSE);
  }
  if ( !zar_read_raw_exact(p_archive, filename, p_lcal_file->name_len) ) {
    return(FALSE);
  }

  /* Read in any extra fields */
  if ( (p_lcal_file->extras_len > 0) &&
       !zar_read_raw_exact(p_archive, extras, p_lcal_file->extras_len) ) {
    return(FALSE);
  }

  return(TRUE);

} /* zar_read_lcal_file */


/**
 * \brief Read a ZIP extra field header record.
 *
 * \param[in] extras
 * Pointer to extra field data to read header from.
 * \param[out] p_header
 * Pointer to returned extra field header structure.
 */
static
void zar_read_xtrafld_header(
/*@in@*/ /*@notnull@*/
  uint8*          extras,
/*@out@*/ /*@notnull@*/
  ZIP_EXTRA_HDR*  p_header)
{
  HQASSERT((extras != NULL),
           "zar_read_xtrafld_header: NULL extra field pointer");
  HQASSERT((p_header != NULL),
           "zar_read_xtrafld_header: NULL returned field header pointer");

  p_header->header_id = READ_SHORT(&extras[XTRFLD_HEADER_ID]);
  p_header->data_size = READ_SHORT(&extras[XTRFLD_DATA_SIZE]);

} /* zar_read_xtrafld_header */


/* Return header and offset for an extra field. */
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
  ZIP_EXTRA_HDR*  p_header)
{
  uint8*  p_end = p_extras + extras_len;
  uint8*  p_xtrfld;

  HQASSERT((p_extras != NULL),
           "zar_find_xtrafld: NULL extra fields pointer");
  HQASSERT((header_id <= MAXUINT16),
           "zar_find_xtrafld: invalid extra field id");
  HQASSERT((p_start != NULL),
           "zar_find_xtrafld: NULL returned field pointer");
  HQASSERT((p_header != NULL),
           "zar_find_xtrafld: NULL returned field header pointer");

  /* Find first match on the id but check that all stated data is present */
  p_xtrfld = p_extras;
  p_end = p_extras + extras_len;
  while ( (p_end - p_xtrfld) >= (ptrdiff_t)ZAR_XTRAFLD_HDRSIZE ) {
    zar_read_xtrafld_header(p_xtrfld, p_header);
    if ( p_header->header_id == header_id ) {
      *p_start = p_xtrfld;
      return((p_end - p_xtrfld) > (ptrdiff_t)p_header->data_size);
    }
    p_xtrfld += p_header->data_size;
  }

  return(FALSE);

} /* zar_find_xtrafld */


/* Fill in file header record for file. */
void zar_create_lcal_file(
/*@out@*/ /*@notnull@*/
  uint8           buffer[ZAR_LCALFILE_RECSIZE],
/*@in@*/ /*@notnull@*/
  ZIP_LCAL_FILE*  p_lcal_file)
{
  uint8* field;

  HQASSERT((buffer != NULL),
           "zar_create_lcal_file: NULL returned header buffer pointer");
  HQASSERT((p_lcal_file != NULL),
           "zar_create_lcal_file: NULL local file record pointer");

  /* Build local file header */
  HqMemCpy(buffer, pksig_lcal_file, ZIP_SIG_SIZE);
  field = &buffer[ZIP_SIG_SIZE];
  WRITE_SHORT(&field[LCALFILE_VERSNEEDED], p_lcal_file->version_needed);
  WRITE_SHORT(&field[LCALFILE_FLAGS], p_lcal_file->flags);
  WRITE_SHORT(&field[LCALFILE_COMPRESSION], p_lcal_file->compression);
  WRITE_SHORT(&field[LCALFILE_MODTIME], p_lcal_file->mod_time);
  WRITE_SHORT(&field[LCALFILE_MODDATE], p_lcal_file->mod_date);

  WRITE_LONG(&field[LCALFILE_CRC32], p_lcal_file->data_desc.crc_32);
  WRITE_LONG(&field[LCALFILE_COMPRESSEDSIZE], p_lcal_file->data_desc.compressed);
  WRITE_LONG(&field[LCALFILE_UNCOMPRESSEDSIZE], p_lcal_file->data_desc.uncompressed_size);

  WRITE_SHORT(&field[LCALFILE_NAMELENGTH], p_lcal_file->name_len);
  WRITE_SHORT(&field[LCALFILE_EXTRASLENGTH], p_lcal_file->extras_len);

} /* zar_create_lcal_file */


/* Write a local file header record. */
Bool zar_write_lcal_file(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/
  ZIP_LCAL_FILE*  p_lcal_file,
/*@in@*/ /*@notnull@*/
  uint8*          filename,
/*@in@*/ /*@notnull@*/
  uint8*          extras)
{
  uint8  buffer[ZAR_LCALFILE_RECSIZE];

  HQASSERT((filename != NULL),
           "zar_write_lcal_file: NULL filename pointer");
  HQASSERT((extras != NULL),
           "zar_write_lcal_file: NULL extras pointer");

  /* Write local file header and filename to the archive. */
  zar_create_lcal_file(buffer, p_lcal_file);
  return((zar_write_raw(p_archive, buffer, sizeof(buffer)) == sizeof(buffer)) &&
         (zar_write_raw(p_archive, filename, p_lcal_file->name_len) == p_lcal_file->name_len) &&
         ((p_lcal_file->extras_len == 0) ||
          (zar_write_raw(p_archive, extras, p_lcal_file->extras_len))));

} /* zar_write_lcal_file */


/* Read a data descriptor record. */
Bool zar_read_data_desc(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@out@*/ /*@notnull@*/
  ZIP_DATA_DESC*  p_data_desc)
{
  uint8   buffer[ZAR_DATADESC_RECSIZE];
  int32   offset;

  HQASSERT((p_archive != NULL),
           "zar_read_data_desc: NULL archive pointer");
  HQASSERT((p_data_desc != NULL),
           "zar_read_data_desc: NULL data descriptor pointer");
  HQASSERT((!zar_creating(p_archive)),
           "zar_read_data_desc: archive open for creating only");

  VERIFY_OBJECT(p_archive, ZIP_ARCHIVE_OBJECT_NAME);

  /* Data descriptor does not always have a sig, so read the smaller size and see
   * if there is a sig, and then read the last 4 bytes if there is one. */
  if ( !zar_read_raw_exact(p_archive, buffer, ZAR_DATADESC_RECSIZE - ZIP_SIG_SIZE) ) {
    return(FALSE);
  }

  offset = 0;
  if ( READ_LONG(buffer) == PKSIG_DATADESC ) {
    if ( !zar_read_raw_exact(p_archive, &buffer[ZAR_DATADESC_RECSIZE - ZIP_SIG_SIZE], ZIP_SIG_SIZE) ) {
      return(FALSE);
    }
    offset = ZIP_SIG_SIZE;
  }

  /* Read the data descriptor offset if sig was present */
  p_data_desc->crc_32 = READ_LONG(&buffer[DATADESC_CRC32 + offset]);
  p_data_desc->compressed = READ_LONG(&buffer[DATADESC_COMPRESSEDSIZE + offset]);
  p_data_desc->uncompressed_size = READ_LONG(&buffer[DATADESC_UNCOMPRESSEDSIZE + offset]);

  return(TRUE);

} /* zar_read_data_desc */


/* Read a ZIP64 data descriptor record. */
Bool zar_read_zip64_data_desc(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@out@*/ /*@notnull@*/
  ZIP_ZIP64_DATA_DESC* p_z64_data_desc)
{
  uint8 buffer[ZAR_ZIP64_DATADESC_RECSIZE];
  int32 offset;

  HQASSERT((p_archive != NULL),
           "zar_zip64_read_data_desc: NULL archive pointer");
  HQASSERT((p_z64_data_desc != NULL),
           "zar_zip64_read_data_desc: NULL data descriptor pointer");
  HQASSERT((!zar_creating(p_archive)),
           "zar_zip64_read_data_desc: archive open for creating only");

  VERIFY_OBJECT(p_archive, ZIP_ARCHIVE_OBJECT_NAME);

  /* Data descriptor does not always have a sig, so read the smaller size and see
   * if there is a sig, and then read the last 4 bytes if there is one. */
  if ( !zar_read_raw_exact(p_archive, buffer, ZAR_ZIP64_DATADESC_RECSIZE - ZIP_SIG_SIZE) ) {
    return(FALSE);
  }

  offset = 0;
  if ( READ_LONG(buffer) == PKSIG_DATADESC ) {
    if ( !zar_read_raw_exact(p_archive, &buffer[ZAR_ZIP64_DATADESC_RECSIZE - ZIP_SIG_SIZE], ZIP_SIG_SIZE) ) {
      return(FALSE);
    }
    offset = ZIP_SIG_SIZE;
  }

  /* Read the ZIP64 data descriptor offset if sig was present */
  p_z64_data_desc->crc_32 = READ_LONG(&buffer[DATADESC_ZIP64_CRC32 + offset]);
  READ_LONGLONG(&p_z64_data_desc->compressed, &buffer[DATADESC_ZIP64_COMPRESSEDSIZE + offset]);
  READ_LONGLONG(&p_z64_data_desc->uncompressed_size, &buffer[DATADESC_ZIP64_UNCOMPRESSEDSIZE + offset]);

  return(TRUE);

} /* zar_zip64_read_data_desc */


/* Fill in file data descriptor record for file. */
void zar_create_data_desc(
/*@out@*/ /*@notnull@*/
  uint8           buffer[ZAR_DATADESC_RECSIZE],
/*@in@*/ /*@notnull@*/
  ZIP_DATA_DESC*  data_desc)
{
  uint8*  field;

  HQASSERT((buffer != NULL),
           "zar_create_data_desc: NULL returned data descriptor buffer pointer");
  HQASSERT((data_desc != NULL),
           "zar_create_data_desc: NULL data descriptor pointer");

  /* Build the data descriptor record */
  HqMemCpy(buffer, pksig_datadesc, ZIP_SIG_SIZE);
  field = &buffer[ZIP_SIG_SIZE];
  WRITE_LONG(&field[DATADESC_CRC32], data_desc->crc_32);
  WRITE_LONG(&field[DATADESC_COMPRESSEDSIZE], data_desc->compressed);
  WRITE_LONG(&field[DATADESC_UNCOMPRESSEDSIZE], data_desc->uncompressed_size);

} /* zar_create_data_desc */


/* Fill in ZIP64 file data descriptor record for file. */
void zar_create_zip64_data_desc(
/*@out@*/ /*@notnull@*/
  uint8           buffer[ZAR_ZIP64_DATADESC_RECSIZE],
/*@in@*/ /*@notnull@*/
  ZIP_ZIP64_DATA_DESC* z64_data_desc)
{
  uint8*  field;

  HQASSERT((buffer != NULL),
           "zar_create_zip64_data_desc: NULL returned data descriptor buffer pointer");
  HQASSERT((z64_data_desc != NULL),
           "zar_create_zip64_data_desc: NULL data descriptor pointer");

  /* Build the data descriptor record */
  HqMemCpy(buffer, pksig_datadesc, ZIP_SIG_SIZE);
  field = &buffer[ZIP_SIG_SIZE];
  WRITE_LONG(&field[DATADESC_ZIP64_CRC32], z64_data_desc->crc_32);
  WRITE_LONGLONG(&field[DATADESC_ZIP64_COMPRESSEDSIZE], &z64_data_desc->compressed);
  WRITE_LONGLONG(&field[DATADESC_ZIP64_UNCOMPRESSEDSIZE], &z64_data_desc->uncompressed_size);

} /* zar_create_zip64_data_desc */


/* Write a data descriptor to the archive. */
Bool zar_write_data_desc(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/
  ZIP_DATA_DESC*  data_desc)
{
  uint8 buffer[ZAR_DATADESC_RECSIZE];

  /* Fill in the data descriptor record */
  zar_create_data_desc(buffer, data_desc);
  return(zar_write_raw(p_archive, buffer, ZAR_DATADESC_RECSIZE) == ZAR_DATADESC_RECSIZE);

} /* zar_write_data_desc */


/* Update the data descriptor field of a local file header. */
Bool zar_update_data_desc(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/
  Hq32x2*         lcal_file_offset,
/*@in@*/ /*@notnull@*/
  ZIP_DATA_DESC*  data_desc)
{
  uint8   buffer[ZAR_DATADESC_RECSIZE];
  Hq32x2  save_pos;
  Hq32x2  pos;

  /* Save the current archive position */
  if ( !zar_get_pos(p_archive, &save_pos) ) {
    return(FALSE);
  }

  /* Reposition to start of data descriptor data */
  Hq32x2AddInt32(&pos, lcal_file_offset, LCALFILE_CRC32);
  if ( !zar_set_pos(p_archive, &pos) ) {
    return(FALSE);
  }

  /* Write data descriptor - less the sig */
  zar_create_data_desc(buffer, data_desc);
  if ( zar_write_raw(p_archive, &buffer[DATADESC_CRC32],
                     (ZAR_DATADESC_RECSIZE - ZIP_SIG_SIZE)) != (ZAR_DATADESC_RECSIZE - ZIP_SIG_SIZE) ) {
    return(FALSE);
  }

  /* Return to original position in archive */
  return(zar_set_pos(p_archive, &save_pos));

} /* zar_update_data_desc */


/* Update the data descriptor field of a local file header. */
Bool zar_update_data_desc_crc32(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/
  Hq32x2*         lcal_file_offset,
/*@in@*/ /*@notnull@*/
  uint32          crc32)
{
  uint8   buffer[sizeof(uint32)];
  Hq32x2  save_pos;
  Hq32x2  pos;

  /* Save the current archive position */
  if ( !zar_get_pos(p_archive, &save_pos) ) {
    return(FALSE);
  }

  /* Reposition to start of data descriptor data */
  Hq32x2AddInt32(&pos, lcal_file_offset, LCALFILE_CRC32);
  if ( !zar_set_pos(p_archive, &pos) ) {
    return(FALSE);
  }

  /* Convert CRC checksum and write into the local file header */
  WRITE_LONG(buffer, crc32);
  if ( zar_write_raw(p_archive, buffer, sizeof(uint32)) != sizeof(uint32) ) {
    return(FALSE);
  }

  /* Return to original position in archive */
  return(zar_set_pos(p_archive, &save_pos));

} /* zar_update_data_desc_crc32 */


/* Write a ZIP64 data descriptor to the archive. */
Bool zar_write_zip64_data_desc(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/
  ZIP_ZIP64_DATA_DESC* z64_data_desc)
{
  uint8 buffer[ZAR_ZIP64_DATADESC_RECSIZE];

  /* Fill in and write the data descriptor record */
  zar_create_zip64_data_desc(buffer, z64_data_desc);
  return(zar_write_raw(p_archive, buffer, ZAR_ZIP64_DATADESC_RECSIZE) == ZAR_ZIP64_DATADESC_RECSIZE);

} /* zar_write_zip64_data_desc */


/* Update the extras field of a local file header. */
Bool zar_update_extras(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/
  Hq32x2*         lcal_file_offset,
/*@in@*/ /*@notnull@*/
  ZIP_LCAL_FILE*  p_lcal_file,
/*@in@*/ /*@notnull@*/
  uint8*          extras)
{
  Hq32x2  save_pos;
  Hq32x2  pos;

  HQASSERT((p_lcal_file != NULL),
           "zar_update_extras: NULL local file header pointer");
  HQASSERT((p_lcal_file->extras_len > 0),
           "zar_update_extras: update extras field with zero length");

  /* Save the current archive position */
  if ( !zar_get_pos(p_archive, &save_pos) ) {
    return(FALSE);
  }

  /* Reposition to start of extras field */
  Hq32x2AddInt32(&pos, lcal_file_offset, (ZAR_LCALFILE_RECSIZE + p_lcal_file->name_len));
  if ( !zar_set_pos(p_archive, &pos) ) {
    return(FALSE);
  }

  /* Write data descriptor - less the sig */
  if ( zar_write_raw(p_archive, extras, p_lcal_file->extras_len) != p_lcal_file->extras_len ) {
    return(FALSE);
  }

  /* Return to original position in archive */
  return(zar_set_pos(p_archive, &save_pos));

} /* zar_update_extras */


/* Locate start of file data in a seekable archive. */
Bool zar_locate_file(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  p_archive,
/*@in@*/ /*@out@*/ /*@notnull@*/
  Hq32x2*       file_pos)
{
  ZIP_LCAL_FILE lcal_file;
  uint8         filename[LONGESTFILENAME];
  uint8         extras[MAXUINT16]; /** \todo oo - this is nasty */

  HQASSERT((p_archive != NULL),
           "zar_locate_file: NULL archive pointer");
  HQASSERT((!zar_streamed(p_archive)),
           "zar_locate_file: archive not seekable");
  HQASSERT((file_pos != NULL),
           "zar_locate_file: NULL file pos pointer");
  HQASSERT((!zar_creating(p_archive)),
           "zar_locate_file: archive open for creating only");

  VERIFY_OBJECT(p_archive, ZIP_ARCHIVE_OBJECT_NAME);

  /* Reposition to local file header and check sig is right */
  if ( !zar_set_pos(p_archive, file_pos) ||
       !zar_match_sig(p_archive, PKSIG_LCAL_FILE) ) {
    return FAILURE(FALSE);
  }

  /* Read header to find size of extra field */
  if ( !zar_read_lcal_file(p_archive, &lcal_file, filename, extras) ) {
    return(FALSE);
  }

  /* Update file position to start of file data */
  Hq32x2AddInt32(file_pos, file_pos, (ZAR_LCALFILE_RECSIZE + lcal_file.name_len + lcal_file.extras_len));

  return(TRUE);

} /* zar_locate_file */


/** \brief Create a ZIP extra field header record.
 *
 * \param[out] buffer
 * Pointer to buffer to hold the extra field header record.
 * \param[in] p_extra_header
 * Pointer to extra field header structure.
 */
static
void zar_create_xtrafld_header(
/*@out@*/ /*@notnull@*/
  uint8           buffer[ZAR_XTRAFLD_HDRSIZE],
/*@in@*/ /*@notnull@*/
  ZIP_EXTRA_HDR*  p_extra_header)
{
  WRITE_SHORT(&buffer[XTRFLD_HEADER_ID], p_extra_header->header_id);
  WRITE_SHORT(&buffer[XTRFLD_DATA_SIZE], p_extra_header->data_size);

} /* zar_create_xtrafld_header */


/* Read the ZIP64 extra field. */
void zar_read_xtrafld_zip64(
/*@in@*/ /*@notnull@*/
  uint8*          p_xtrafld,
/*@out@*/ /*@notnull@*/
  ZIP_XTRAFLD_ZIP64* p_zip64)
{
  int32 total;

  HQASSERT((p_xtrafld != NULL),
           "zar_read_xtrafld_zip64: NULL extra field pointer");
  HQASSERT((p_zip64 != NULL),
           "zar_read_xtrafld_zip64: NULL pointer to returned ZIP64 data");

  /* Not all of the extra field may be present - read as much as is there. Check
   * the value offsets (which include the header) against the extra field size. */
  p_zip64->present = 0;
  total = p_zip64->header.data_size + ZAR_XTRAFLD_HDRSIZE;
  if ( total < XTRA_ZIP64_COMPRESSED ) {
    return;
  }
  READ_LONGLONG(&p_zip64->uncompressed_size, &p_xtrafld[XTRA_ZIP64_UNCOMPRESSED]);
  p_zip64->present |= ZIP64_UNCOMPRESSED;
  if ( total < XTRA_ZIP64_OFFSET ) {
    return;
  }
  READ_LONGLONG(&p_zip64->compressed, &p_xtrafld[XTRA_ZIP64_COMPRESSED]);
  p_zip64->present |= ZIP64_COMPRESSED;
  if ( total < XTRA_ZIP64_STARTDISK ) {
    return;
  }
  READ_LONGLONG(&p_zip64->lcal_file_hdr_offset, &p_xtrafld[XTRA_ZIP64_OFFSET]);
  p_zip64->present |= ZIP64_OFFSET;
  if ( total < ZAR_XTRAFLD_ZIP64SIZE + ZAR_XTRAFLD_HDRSIZE ) {
    return;
  }
  p_zip64->start_disk_number = READ_LONG(&p_xtrafld[XTRA_ZIP64_STARTDISK]);
  p_zip64->present |= ZIP64_STARTDISK;

} /* zar_read_xtrafld_zip64 */


/* Create a ZIP64 extra field record. */
void zar_create_xtrafld_zip64(
/*@out@*/ /*@notnull@*/
  uint8   buffer[ZAR_XTRAFLD_ZIP64SIZE],
/*@in@*/ /*@notnull@*/
  ZIP_XTRAFLD_ZIP64* p_zip64)
{
  HQASSERT((buffer != NULL),
           "zar_create_xtrafld_zip64: NULL buffer pointer");
  HQASSERT((p_zip64 != NULL),
           "zar_create_xtrafld_zip64: NULL ZIP64 extra field pointer");

  zar_create_xtrafld_header(buffer, &p_zip64->header);
  WRITE_LONGLONG(&buffer[XTRA_ZIP64_UNCOMPRESSED], &p_zip64->uncompressed_size);
  WRITE_LONGLONG(&buffer[XTRA_ZIP64_COMPRESSED], &p_zip64->compressed);
  WRITE_LONGLONG(&buffer[XTRA_ZIP64_OFFSET], &p_zip64->lcal_file_hdr_offset);
  WRITE_LONG(&buffer[XTRA_ZIP64_STARTDISK], p_zip64->start_disk_number);

} /* zar_create_xtrafld_zip64 */


/* Write a ZIP64 extra field record to the archive. */
Bool zar_write_xtrafld_zip64(
  ZIP_ARCHIVE*    p_archive,
  ZIP_XTRAFLD_ZIP64* p_zip64)
{
  uint8   buffer[ZAR_XTRAFLD_ZIP64SIZE];

  zar_create_xtrafld_zip64(buffer, p_zip64);
  return(zar_write_raw(p_archive, buffer, ZAR_XTRAFLD_ZIP64SIZE) == ZAR_XTRAFLD_ZIP64SIZE);

} /* zar_write_xtrafld_zip64 */


/* Look for a ZIP64 central directory locator record. */
Bool zar_find_zip64_cdir_locator(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/
  Hq32x2*         file_pos,
/*@out@*/ /*@notnull@*/
  Bool*           p_found)
{
  HQASSERT((file_pos != NULL),
           "zar_find_zip64_cdir_locator: NULL file position pointer");
  HQASSERT((p_found != NULL),
           "zar_find_zip64_cdir_locator: NULL pointer to returned found flag");

  /* ZIP64 end of central directory locator record is fixed offset in front of
   * end of central directory record */
  *p_found = FALSE;
  Hq32x2SubtractInt32(file_pos, file_pos, ZAR_ZIP64_CDIRLOCATOR_RECSIZE + ZIP_SIG_SIZE);
  if ( Hq32x2Sign(file_pos) >= 0 ) {
    if ( !zar_set_pos(p_archive, file_pos) ) {
      return(FALSE);
    }
    *p_found = zar_match_sig(p_archive, PKSIG_ZIP64_LOCATOR);
  }

  return(TRUE);

} /* zar_find_zip64_cdir_locator */


/* Read a ZIP64 central directory locator record. */
Bool zar_read_zip64_cdir_locator(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@out@*/ /*@notnull@*/
  ZIP_ZIP64_CDIR_LOC* p_z64_cdir_locator)
{
  uint8 buffer[ZAR_ZIP64_CDIRLOCATOR_RECSIZE - ZIP_SIG_SIZE];

  VERIFY_OBJECT(p_archive, ZIP_ARCHIVE_OBJECT_NAME);

  /* Read the ZIP64 end record locator (minus any extra data) */
  if ( !zar_read_raw_exact(p_archive, buffer, (ZAR_ZIP64_CDIRLOCATOR_RECSIZE - ZIP_SIG_SIZE)) ) {
    return(FALSE);
  }

  /* Extract the fields */
  p_z64_cdir_locator->start_disk_number = READ_LONG(&buffer[ZIP64_CDIRLOC_STARTDISKNUM]);
  READ_LONGLONG(&p_z64_cdir_locator->zip64_end_cdir_offset, &buffer[ZIP64_CDIRLOC_ENDCDIROFFSET]);
  p_z64_cdir_locator->total_disks = READ_LONG(&buffer[ZIP64_CDIRLOC_DISKSTOTAL]);

  return(TRUE);

} /* zar_read_zip64_cdir_loc */


/* Create a ZIP64 central directory locator record. */
void zar_create_zip64_cdir_locator(
/*@out@*/ /*@notnull@*/
  uint8             buffer[ZAR_ZIP64_CDIRLOCATOR_RECSIZE],
/*@in@*/ /*@notnull@*/
  ZIP_ZIP64_CDIR_LOC* p_z64_cdir_locator)
{
  uint8*  field;

  HqMemCpy(buffer, pksig_zip64_cdirloc, ZIP_SIG_SIZE);
  field = &buffer[ZIP_SIG_SIZE];
  WRITE_LONG(&field[ZIP64_CDIRLOC_STARTDISKNUM], p_z64_cdir_locator->start_disk_number);
  WRITE_LONGLONG(&field[ZIP64_CDIRLOC_ENDCDIROFFSET], &p_z64_cdir_locator->zip64_end_cdir_offset);
  WRITE_LONG(&field[ZIP64_CDIRLOC_DISKSTOTAL], p_z64_cdir_locator->total_disks);

} /* zar_create_zip64_cdir_locator */


/* Write a ZIP64 central directory locator record to the archive. */
Bool zar_write_zip64_cdir_locator(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/
  ZIP_ZIP64_CDIR_LOC* p_z64_cdir_locator)
{
  uint8   buffer[ZAR_ZIP64_CDIRLOCATOR_RECSIZE];

  zar_create_zip64_cdir_locator(buffer, p_z64_cdir_locator);
  return(zar_write_raw(p_archive, buffer, ZAR_ZIP64_CDIRLOCATOR_RECSIZE) == ZAR_ZIP64_CDIRLOCATOR_RECSIZE);

} /* zar_write_zip64_cdir_locator */


/* Read a ZIP64 end of central directory record. */
Bool zar_read_zip64_end_cdir(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@out@*/ /*@notnull@*/
  ZIP_ZIP64_END_CDIR* p_z64_end_cdir)
{
  uint8 buffer[ZAR_ZIP64_ENDCDIR_RECSIZE - ZIP_SIG_SIZE];
  HqU32x2 amount;
  int32 read;

  /* Read the ZIP64 end record (minus any extra data) */
  if ( !zar_read_raw_exact(p_archive, buffer, (ZAR_ZIP64_ENDCDIR_RECSIZE - ZIP_SIG_SIZE)) ) {
    return(FALSE);
  }

  /* Extract the fields */
  READ_LONGLONG(&p_z64_end_cdir->end_cdir_size, &buffer[ZIP64_ENDCDIR_ENDSIZE]);
  p_z64_end_cdir->made_by_version = READ_SHORT(&buffer[ZIP64_ENDCDIR_MADEBYVERS]);
  p_z64_end_cdir->version_needed = READ_SHORT(&buffer[ZIP64_ENDCDIR_VERSNEEDED]);
  p_z64_end_cdir->this_disk_number = READ_LONG(&buffer[ZIP64_ENDCDIR_THISDISKNUM]);
  p_z64_end_cdir->start_disk_number = READ_LONG(&buffer[ZIP64_ENDCDIR_STARTDISKNUM]);
  READ_LONGLONG(&p_z64_end_cdir->total_cdir_entries_this_disk, &buffer[ZIP64_ENDCDIR_ENTRIESTHISDISK]);
  READ_LONGLONG(&p_z64_end_cdir->total_cdir_entries, &buffer[ZIP64_ENDCDIR_ENTRIESTOTAL]);
  READ_LONGLONG(&p_z64_end_cdir->cdir_size, &buffer[ZIP64_ENDCDIR_CDIRSIZE]);
  READ_LONGLONG(&p_z64_end_cdir->cdir_start_offset, &buffer[ZIP64_ENDCDIR_CDIROFFSET]);

  HQTRACE((HqU32x2CompareUint32(&p_z64_end_cdir->end_cdir_size, ZAR_ZIP64_ENDCDIR_RECSIZE) < 0),
          ("zar_read_zip64_end_cdir: warning - ZIP64 end central directory smaller than expected"));

  /* AWOOGA - some MS test XPS files have the record size less than the minimum
   * even though the extra fields are there and valid. */
  if ( HqU32x2CompareUint32(&p_z64_end_cdir->end_cdir_size, ZAR_ZIP64_ENDCDIR_RECSIZE) > 0 ) {
    /* Skip over any extra data in the record */
    HqU32x2SubtractUint32(&amount, &p_z64_end_cdir->end_cdir_size, ZAR_ZIP64_ENDCDIR_RECSIZE);
    while ( !HqU32x2IsZero(&amount) ) {
      read = MAXINT32;
      if ( HqU32x2CompareUint32(&amount, read) <= 0 ) {
        HqU32x2ToInt32(&amount, &read);
      }
      if ( !zar_skip_raw(p_archive, read) ) {
        return(FALSE);
      }
      HqU32x2SubtractUint32(&amount, &amount, (uint32)read);
    }
  }

  return(TRUE);

} /* zar_read_zip64_end_cdir */


/* Create a ZIP64 end of central directory record. */
void zar_create_zip64_end_cdir(
/*@out@*/ /*@notnull@*/
  uint8           buffer[ZAR_ZIP64_ENDCDIR_RECSIZE],
/*@in@*/ /*@notnull@*/
  ZIP_ZIP64_END_CDIR* p_z64_end_cdir)
{
  uint8*  field;

  HQASSERT((buffer != NULL),
           "zar_create_zip64_end_cdir: NULL buffer pointer");
  HQASSERT((p_z64_end_cdir != NULL),
           "zar_create_zip64_end_cdir: NULL ZIP64 end cdir pointer");

  HqMemCpy(buffer, pksig_zip64_endcdir, ZIP_SIG_SIZE);
  field = &buffer[ZIP_SIG_SIZE];
  WRITE_LONGLONG(&field[ZIP64_ENDCDIR_ENDSIZE], &p_z64_end_cdir->end_cdir_size);
  WRITE_SHORT(&field[ZIP64_ENDCDIR_MADEBYVERS], p_z64_end_cdir->made_by_version);
  WRITE_SHORT(&field[ZIP64_ENDCDIR_VERSNEEDED], p_z64_end_cdir->version_needed);
  WRITE_LONG(&field[ZIP64_ENDCDIR_THISDISKNUM], p_z64_end_cdir->this_disk_number);
  WRITE_LONG(&field[ZIP64_ENDCDIR_STARTDISKNUM], p_z64_end_cdir->start_disk_number);
  WRITE_LONGLONG(&field[ZIP64_ENDCDIR_ENTRIESTHISDISK], &p_z64_end_cdir->total_cdir_entries_this_disk);
  WRITE_LONGLONG(&field[ZIP64_ENDCDIR_ENTRIESTOTAL], &p_z64_end_cdir->total_cdir_entries);
  WRITE_LONGLONG(&field[ZIP64_ENDCDIR_CDIRSIZE], &p_z64_end_cdir->cdir_size);
  WRITE_LONGLONG(&field[ZIP64_ENDCDIR_CDIROFFSET], &p_z64_end_cdir->cdir_start_offset);

} /* zar_create_zip64_end_cdir */


/* Write a ZIP64 end of central directory record to the archive. */
Bool zar_write_zip64_end_cdir(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/
  ZIP_ZIP64_END_CDIR* p_z64_end_cdir)
{
  uint8   buffer[ZAR_ZIP64_ENDCDIR_RECSIZE];

  zar_create_zip64_end_cdir(buffer, p_z64_end_cdir);
  return(zar_write_raw(p_archive, buffer, ZAR_ZIP64_ENDCDIR_RECSIZE) == ZAR_ZIP64_ENDCDIR_RECSIZE);

} /* zar_write_zip64_end_cdir */


/* Log stripped */
