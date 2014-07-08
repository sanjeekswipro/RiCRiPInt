/** \file
 * \ingroup zipdev
 *
 * $HopeName: COREzipdev!src:zip_file.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Logical ZIP file interface.
 */

#ifndef __ZIP_FILE_H__
#define __ZIP_FILE_H__  (1)


#include "swdevice.h"     /* DEVICELIST */
#include "objnamer.h"     /* OBJECT_NAME_MEMBER */
#include "lists.h"        /* sll_list_t */
#include "zip_archive.h"  /* ZIP_ARCHIVE */


/** \brief Device used to write extracted files are. */
typedef struct ZIP_FILE_DEVICE {
/*@notnull@*/ /*@dependent@*/
  DEVICELIST*       device;     /**< Device to write extracted files to. */
  int32             bufsize;    /**< Device preferred buffer size. */

  OBJECT_NAME_MEMBER
} ZIP_FILE_DEVICE;


/** \brief ZIP file information common to ZIP and ZIP64. */
typedef struct ZIP_FILE_INFO {
  int32             flags;        /**< ZIP general file flags. */
  int32             compression;  /**< Compression used with file. */
  uint32            date_time;    /**< ZIP file date and time information. */
  uint32            crc_32;       /**< CRC32 checksum for the file. */
  HqU32x2           compressed;   /**< Compressed size of file. */
  HqU32x2           uncompressed; /**< Size of file without compression. */
  HqU32x2           offset;       /**< Local file header offset. */
  Bool              zip64;        /**< ZIP64 flag - used for streamed input with data descriptors. */
} ZIP_FILE_INFO;

/**
 * \brief Check if file can be extracted based on local file header flags.
 *
 * \param[in] p_info
 * Pointer to file information.
 *
 * \return
 * \c TRUE if the file can be extracted, else \c FALSE.
 */
extern
Bool ZIP_CAN_EXTRACT(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_INFO*  p_info);
#define ZIP_CAN_EXTRACT(p)  ((((p)->flags&ZIPFLG_ERROR) == 0) && \
                             (((p)->compression == ZIPCOMP_STORED) || ((p)->compression == ZIPCOMP_DEFLATE)))

/**
 * \brief Initialise a ZIP file device.
 *
 * \param[out] p_file_device
 * Pointer to ZIP file device to initialise.
 * \param[in] p_device
 * Device pointer to initialise ZIP file device with.
 */
extern
void zfd_init(
/*@out@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE_DEVICE*  p_file_device,
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  DEVICELIST*       p_device);

/** \brief Get the PostScript device to write extracted files to. */
extern Bool zfd_device(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_DEVICE* p_zfd);
#define zfd_device(p)   ((p)->device)

/** \brief Get preferred buffer size for ZIP file device. */
extern Bool zfd_bufsize(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_DEVICE* p_zfd);
#define zfd_bufsize(p)  ((p)->bufsize)


/**
 * \brief Name of a logical file in a ZIP archive.
 *
 * Archive file entry names are counted strings, and there is nothing to stop
 * them containing NULs.  This coincides with PostScript strings so we can use
 * the FILEENTRY type as a minor optimisation when doing filename iteration as
 * we can use the passed in FILEENTRY structure.
 */
typedef FILEENTRY ZIP_FILE_NAME;

typedef struct ZIP_FILE ZIP_FILE;
typedef /*@dependent@*/ /*@notnull@*/ ZIP_FILE* ZIP_FILE_PTR;

/** \brief List of ZIP files. */
typedef struct ZIP_FILE_LIST {
  dll_list_t  files;          /**< \brief List of ZIP files. */

  OBJECT_NAME_MEMBER
} ZIP_FILE_LIST;


/**
 * \brief Calculate hash value for file name.
 *
 * When file name case is being ignored, all file names have their ASCII
 * alphabetic characters converted to lower case.  The ZIP device is then case
 * preserving but case insensitive.
 *
 * This will not work with file names that use multi-byte encodings.
 *
 * \param[in] p_name
 * Pointer to ZIP file name.
 * \param[in] f_ignore_case
 * Flag if file name case is being ignored.
 *
 * \return
 * File name hash value.
 */
extern
uint32 zfl_name_hash(
/*@in@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_FILE_NAME*  p_name,
  Bool            f_ignore_case);


/** \brief ZIP entry is a file piece */
#define ZIP_FILE_PIECE_FILE       (0x00)
/** \brief ZIP entry is a directory */
#define ZIP_FILE_PIECE_DIRECTORY  (0x01)
/** \brief ZIP file piece is last piece */
#define ZIP_FILE_PIECE_LASTPIECE  (0x02)

/**
 * \brief Classify the piece type of a physical ZIP file from it's name.
 *
 * \param[in] name
 * File name to classify.
 * \param[in] f_merge_files
 * Flag if files should be merged that match known name patterns.
 * \param[out] p_name_len
 * Length of file name after removing any piece information.
 * \param[out] p_number
 * Number of piece.
 *
 * \return
 * Piece type information which can be tested with zpt_directory(), zpt_file(),
 * and zpt_last().
 */
extern
int32 zfl_piece_type(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_NAME*  name,
  Bool            f_merge_files,
/*@out@*/ /*@notnull@*/
  int32*          p_name_len,
/*@out@*/ /*@notnull@*/
  uint32*         p_number);

/** \brief Is the piece a directory. */
extern Bool zpt_directory(int32 type);
#define zpt_directory(t)      (((t)&ZIP_FILE_PIECE_DIRECTORY) != 0)

/** \brief Is the piece a file. */
extern Bool zpt_file(int32 type);
#define zpt_file(t)           (!zpt_directory(t))

/** \brief Is the piece the last piece. */
extern Bool zpt_last(int32 type);
#define zpt_last(t)           (((t)&ZIP_FILE_PIECE_LASTPIECE) != 0)


/**
 * \brief Add a new logical file from a ZIP archive to a list of existing files.
 *
 * \param[in] p_list
 * List of existing files.
 * \param[in] name
 * Logical file name.
 * \param[in] normalised_name
 * Unicode normalised version of the file name.
 * \param[in] date_time
 * File creation date and time in ZIP archive item format.
 * \param[in] p_device
 * Pointer to ZIP file device the file will be extracted to.
 * \param[in] device_filename
 * Name for extracted file on ZIP file device.
 * \param[in] f_zipfile
 * Flag that file is from a ZIP archive.
 * \param[in] p_archive
 * Pointer to ZIP archive logical file is from.
 * \param[in] f_crccheck
 * CRC32 checksums should be checked after extracting file data.
 *
 * \return
 * Pointer to new logical file in the file list, else \c NULL.
 */
extern /*@null@*/ /*@dependent@*/
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
  Bool            f_crccheck);


/**
 * \brief Add physical file data piece to logical file.
 *
 * The function first checks that the piece number and last status is consistent
 * with all other file pieces seen for the logical file.  This means on success
 * that the logical file piece list is consistent - ordered by number with the
 * last possibly set, although it is possible for there to be holes in the
 * sequence if the pieces are added out of sequence.
 *
 * \param[in] p_file
 * Pointer to logical file.
 * \param[in] p_info
 * Pointer to file information.
 * \param[in] number
 * Piece sequence number.
 * \param[in] type
 * Type of piece.
 *
 * \return
 * \c TRUE if piece is added to logical file, else \c FALSE.
 */
extern
Bool zfl_add_piece(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
/*@in@*/ /*@notnull@*/
  ZIP_FILE_INFO*  p_info,
  uint32          number,
  int32           type);


/**
 * \brief Finish extracting data for the current file piece of a logical file.
 *
 * When extracting files from a streamed archive we need to extract file pieces
 * as we encounter them so we can find the next logical file.  As such, logical
 * files will only ever have one piece in their piece list to extract.  We can
 * force its extraction by trying to extract 4GB of data.
 *
 * \param[in] p_file
 * Pointer to logical file.
 *
 * \return
 * \c TRUE if successfully extracted file piece data, else \c FALSE.
 */
extern
Bool zfl_extract(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file);


/**
 * \brief Open a new file stream on the logical file.
 *
 * \param[in] p_file
 * Pointer to logical file.
 * \param[in] openflags
 * Flags controlling how to open the file.
 * \param[out] fd
 * File descriptor for new file stream
 *
 * \return
 * \c TRUE of opened a new stream on the logical file, or \c FALSE if not.
 */
extern
Bool zfl_open(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
  int32           openflags,
/*@out@*/ /*@notnull@*/
  DEVICE_FILEDESCRIPTOR* fd);

/**
 * \brief Read data from a logical file.
 *
 * \param[in] p_file
 * Pointer to logical file.
 * \param[in] fd
 * File stream to read data from.
 * \param[out] buff
 * Pointer to buffer to read data into.
 * \param[in] len
 * Length of buffer.
 *
 * \return
 * The number of bytes successfully read, or \c -1 if there was an error reading
 * from the file stream.
 */
extern
int32 zfl_read(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
  DEVICE_FILEDESCRIPTOR fd,
/*@out@*/ /*@notnull@*/
  uint8*          buff,
  int32           len);

/**
 * \brief Write data to a logical file.
 *
 * \param[in] p_file
 * Pointer to logical file.
 * \param[in] fd
 * File stream to write data to.
 * \param[out] buff
 * Pointer to buffer to write data from.
 * \param[in] len
 * Length of buffer.
 *
 * \return
 * The number of bytes successfully written, or \c -1 if there was an error
 * writing to the file stream.
 */
extern
int32 zfl_write(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
  DEVICE_FILEDESCRIPTOR fd,
/*@in@*/ /*@notnull@*/
  uint8*          buff,
  int32           len);

/**
 * \brief Do a seek on a stream opened on the logical file.
 *
 * \param[in] p_file
 * Pointer to logical file.
 * \param[in] fd
 * Open file descriptor on logical file.
 * \param[in,out] destn
 * Position to seek to, or current position, based on \p flags.
 * \param[in] flags
 * Flag indicating type of seek to do.
 *
 * \return
 * \c TRUE if seek on file stream succeeded, else \c FALSE.
 */
extern
Bool zfl_seek(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
  DEVICE_FILEDESCRIPTOR fd,
/*@in@*/ /*@out@*/ /*@notnull@*/
  Hq32x2*         destn,
  int32           flags);


/**
 * \brief Flush logical file.
 *
 * If the file is being read from a ZIP archive then the file is truncated to
 * the amount of data extracted so far and the remainder of the file data in the
 * archive is consumed and thrown away.
 *
 * This function does nothing for files that are opened for write-only, or do
 * not come from a ZIP archive.
 *
 * \param[in] p_file
 * Pointer to logical file.
 *
 * \return
 * \c TRUE if logical file was flushed ok, else \c FALSE.
 */
extern
Bool zfl_flush(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file);


/**
 * \brief Skip over an archive entry for a streamed archive.
 *
 * \param[in] p_archive
 * Pointer to ZIP archive.
 * \param[in] p_info
 * Pointer to file information.
 * \param[in] f_crccheck
 * Check CRC32 checksum for skipped archive file.
 *
 * \return
 * \c TRUE if entry was skipped ok, else \c FALSE.
 */
extern
Bool zfl_skip_lcal_file(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_ARCHIVE*    p_archive,
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE_INFO*  p_info,
  Bool            f_crccheck);


/**
 * \brief Close a file stream on the extracted file.
 *
 * \param[in] p_file
 * Pointer to logical file.
 * \param[in] fd
 * File descriptor being closed.
 * \param[in] f_abort
 * Flag indicating if the close is due to an abort.
 *
 * \return
 * \c 0 if the file was closed/aborted without error, else \c -1.
 */
extern
int32 zfl_close(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
  DEVICE_FILEDESCRIPTOR fd,
  Bool            f_abort);


/**
 * \brief Reset a logical file list to be empty.
 *
 * \param[in] p_list
 * Pointer to logical file list.
 */
extern
void zfl_init_list(
/*@out@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_FILE_LIST*  p_list);

/**
 * \brief Destroy all resources used by all files in the logical file list.
 *
 * \param[in] p_list
 * Pointer to logical file list.
 */
extern
void zfl_destroy_list(
/*@in@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_FILE_LIST*  p_list);


/**
 * \brief Find a file by name in a logical file list.
 *
 * If the case of file names is being ignored convert the name to find and the
 * file name to a common case, which is lower case for no particular reason.
 *
 * \param[in] p_list
 * Pointer to logical file list.
 * \param[in] p_name
 * Pointer to file name to look for.
 * \param[in] f_ignore_case
 * Ignore case of file names when searching.
 * \param[in] f_use_normalised
 * Use Unicode normalised version of file name.
 *
 * \return
 * Pointer to existing logical file with the same name, else \c NULL.
 */
extern /*@dependent@*/ /*@null@*/
ZIP_FILE* zfl_find(
/*@in@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_FILE_LIST*  p_list,
/*@in@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_FILE_NAME*  p_name,
  Bool            f_ignore_case,
  Bool            f_use_normalised);


/**
 * \brief Rename the file.
 *
 * \param[in] p_file
 * Point to logical file to rename.
 * \param[in] p_list
 * Pointer to logical file list for new name.
 * \param[in] p_name
 * Pointer to new file name
 *
 * \return
 * TRUE if logical file was successfully renamed, else \c FALSE.
 */
extern
Bool zfl_rename(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
/*@in@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_FILE_LIST*  p_list,
/*@in@*/ /*@notnull@*/
  ZIP_FILE_NAME*  p_name);


/**
 * \brief Delete the file.
 *
 * \param[in] p_file
 * Point to logical file to delete.
 *
 * \return
 * TRUE if logical file was successfully deleted, else \c FALSE.
 */
extern
Bool zfl_delete(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file);


/**
 * \brief Is the logical file currently opened for exclusive access.
 *
 * \param[in] p_file
 * Pointer to logical file.
 *
 * \return
 * \c TRUE if the file is currently open for exclusive access, else \c FALSE.
 */
extern
Bool zfl_is_excl(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file);

/**
 * \brief Is the logical file currently opened.
 *
 * \param[in] p_file
 * Pointer to logical file.
 *
 * \return
 * \c TRUE if the file is currently open, else \c FALSE.
 */
extern
Bool zfl_is_open(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file);


/** \brief Maintains a list of files in the order that they appear in a ZIP
 * archive or have been created on the ZIP device. */
typedef struct ZIP_FILE_CHAIN {
/*@null@*/
  ZIP_FILE* p_first;    /**< Pointer to first file in chain. */
/*@null@*/
  ZIP_FILE* p_last;     /**< Pointer to last file in chain. */
} ZIP_FILE_CHAIN;


/**
 * \brief Initialise file chain structure.
 *
 * \param[in] p_chain
 * Pointer to file chain to initialise.
 */
extern
void zfc_init(
/*@out@*/ /*@notnull@*/
  ZIP_FILE_CHAIN* p_chain);

/**
 * \brief Append a file to the file chain.
 *
 * \param[in] p_chain
 * Pointer to chain to append file to.
 * \param[in] p_file
 * Pointer to file to append to the chain.
 */
extern
void zfc_append(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_CHAIN* p_chain,
/*@in@*/ /*@notnull@*/
  ZIP_FILE*       p_file);

/**
 * \brief Remove a file from the file chain.
 *
 * \param[in] p_chain
 * Pointer to chain to remove file from.
 * \param[in] p_file
 * Pointer to file to remove from the chain.
 */
extern
void zfc_remove(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_CHAIN* p_chain,
/*@in@*/ /*@notnull@*/
  ZIP_FILE*       p_file);

/**
 * \brief Get first file in chain of all files.
 *
 * \param[in] p_chain
 * Pointer to chain to be iterated over.
 *
 * \return
 * Pointer to first file in chain, or \c NULL if the chain is empty.
 */
extern
ZIP_FILE* zfc_get_first(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_CHAIN* p_chain);

/**
 * \brief Get next file in chain of all files.
 *
 * \param[in] p_chain
 * Pointer to chain being iterated.
 * \param[in] p_file
 * Pointer to previously returned file in the chain.
 *
 * \return
 * Pointer to next file in chain, or \c NULL when no more files in the chain.
 */
extern
ZIP_FILE* zfc_get_next(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_CHAIN* p_chain,
/*@in@*/ /*@notnull@*/
  ZIP_FILE*       p_file);

/**
 * \brief Get last file in chain of all files.
 *
 * \param[in] p_chain
 * Pointer to chain to get last file from.
 *
 * \return
 * Pointer to last file in chain, or \c NULL if the chain is empty.
 */
extern
ZIP_FILE* zfc_get_last(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_CHAIN* p_chain);


/**
 * \brief Return the name of the logical file.
 *
 * \param[in] p_file
 * Pointer to logical file.
 * \param[out] p_name
 * Pointer to returned file name.
 */
extern
void zfl_name(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
/*@out@*/ /*@notnull@*/
  ZIP_FILE_NAME*  p_name);

/**
 * \brief Get the currently known total size of a logical file.
 *
 * If the archive is streamed and we have not finished extracting the file, then
 * we cannot no what the size is so a size of 0 is currently returned.
 *
 * \todo Should the size for a streamed file be what has currently been
 * extracted?
 *
 * \param[in] p_file
 * Pointer to logical file.
 * \param[out] bytes
 * Pointer to returned file size.
 */
extern
void zfl_size(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
/*@out@*/ /*@notnull@*/
  HqU32x2*        bytes);

/**
 * \brief Get the size of the last piece added to a logical file.
 *
 * The ZIP device size needs to increase with each piece added.  The size of all
 * pieces added to a logical file is tracked until it is read with this call,
 * wehere the added pieces size is reset to 0.  This allows the device to
 * increase its size after adding a piece to file rather than have to iterate
 * over all logical files each time.
 *
 * \param[in] p_file
 * Pointer to logical file.
 *
 * \return
 * Size of logical file piece in bytes.
 */
extern
HqU32x2 zfl_piece_size(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE* p_file);

/**
 * \brief Return the last modification data and time of a ZIP file entry.
 *
 * Date and time are in DOS format, with the data in the two 16 bits and the
 * time in the lower 16 bits. This ensures correct ordering when comparing times
 * of files from a ZIP archive.
 *
 * \param[in] p_file
 * Pointer to logical file.
 *
 * \return
 * Last file modification date and time.
 */
extern
uint32 zfl_datetime(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file);

/**
 * \brief Get the number of available bytes for an open stream on a logical file.
 *
 * As the file may not be completely extracted we cannot punt the request to the
 * underlying file device, but we also don't need to extract any more data since
 * the underlying file position is valid and we know the extracted size of
 * the file.
 *
 * \param[in] p_file
 * Pointer to logical file.
 * \param[in] fd
 * File descriptor of stream on logical file.
 * \param[in] reason
 * Type of byte count required.
 * \param[out] bytes
 * Pointer to returned available byte count.
 *
 * \return
 * \c TRUE if able to find available bytes, else FALSE.
 */
extern
Bool zfl_bytes_avail(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*       p_file,
  DEVICE_FILEDESCRIPTOR fd,
  int32           reason,
/*@out@*/ /*@notnull@*/
  Hq32x2*         bytes);

/**
 * \brief Query that the next piece of the passed file is required and
 * available.
 *
 * The returned piece ready flag is \c TRUE if the next piece is required and
 * available.
 *
 * \param[in] p_file
 * Pointer to ZIP file.
 * \param[in] fd
 * File descriptor for ZIP file.
 * \param[out] ready
 * Pointer to returned piece ready flag.
 *
 * \return
 * \c TRUE if successfully checked next piece, else \c FALSE.
*/
extern
Bool zfl_next_piece_ready(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE*         p_file,
  DEVICE_FILEDESCRIPTOR fd,
/*@out@*/ /*@notnull@*/
  Bool*             ready);


/** \brief File data reader context pointer. */
typedef struct ZIP_FILE_READER ZIP_FILE_READER;

/**
 * \brief Create a new file data reader.
 *
 * \return
 * Pointer to new file data reader if created successfully, else \c NULL.
 */
extern
ZIP_FILE_READER* zfr_new(void);

/**
 * \brief Free a file data reader.
 *
 * \param[in] p_reader
 * Pointer to file data reader to be freed.
 */
extern
void zfr_free(
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE_READER*  p_reader);


/**
 * \brief Open a new file to read its data.
 *
 * \param[in] p_reader
 * Pointer to file data reader.
 * \param[in] p_file
 * Pointer to logical file to read.
 * \param[in] compression
 * Type of compression to use for file data.
 *
 * \return
 * \c TRUE if file opened successfully, else \c FALSE.
 */
extern
Bool zfr_open(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_READER*  p_reader,
/*@in@*/ /*@notnull@*/
  ZIP_FILE*         p_file,
  int32             compression);

/**
 * \brief Close current file associated with reader whose data is being read.
 *
 * The current file being read must be closed after reading its data, even if
 * there was an error.
 *
 * \param[in] p_reader
 * Pointer to file data reader currently reading data from a file.
 */
extern
void zfr_close(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_READER*  p_reader);


/**
 * \brief Read file data.
 *
 * \param[in] p_reader
 * Pointer to file data reader.
 * \param[in] buffer
 * Pointer to buffer to return file data in.
 * \param[in] buf_len
 * Length of buffer.
 *
 * \return
 * The number of bytes of file data returned in the buffer.  \c 0 if there are
 * no more file data to return.  \c -1 if there was an error while reading the
 * file data.
 */
extern
int32 zfr_read_data(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_READER*  p_reader,
/*@in@*/ /*@notnull@*/
  uint8*            buffer,
/*@in@*/ /*@notnull@*/
  int32             buf_len);

/**
 * \brief Fill in data descriptor for file whose data has been read.
 *
 * This function should only be called after all the file data has been read.
 * It can be called after zfr_close() has been called, but should be called
 * before zfr_open() is called for the next file.
 *
 * \param[in] p_reader
 * Pointer to file data reader.
 * \param[out] p_desc
 * Pointer to ZIP data descriptor to be filled in.
 *
 * \return
 * \c FALSE if the compressed file size is greater than 4GB, else \c TRUE.
 */
extern
Bool zfr_data_desc(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_READER*  p_reader,
/*@out@*/ /*@notnull@*/
  ZIP_DATA_DESC*    p_desc);

/**
 * \brief Return CRC32 checksum for file read.
 *
 * \param[in] p_reader
 * Pointer to file data reader.
 *
 * \return
 * CRC32 checksum for the file.
 */
extern
uint32 zfr_data_desc_crc32(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_READER*  p_reader);

/**
 * \brief Fill in ZIP64 data descriptor for file whose data has been read.
 *
 * This function should only be called after all the file data has been read.
 * It can be called after zfr_close() has been called, but should be called
 * before zfr_open() is called for the next file.
 *
 * \param[in] p_reader
 * Pointer to file data reader.
 * \param[out] p_z64_desc
 * Pointer to ZIP64 data descriptor to be filled in.
 */
extern
void zfr_z64_data_desc(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_READER*  p_reader,
/*@out@*/ /*@notnull@*/
  ZIP_ZIP64_DATA_DESC* p_z64_desc);

/**
 * \brief Update a ZIP64 extra field for file whose data has been read.
 *
 * Only the file compressed and uncompressed size fields are updated.
 *
 * This function should only be called after all the file data has been read.
 * It can be called after zfr_close() has been called, but should be called
 * before zfr_open() is called for the next file.
 *
 * \param[in] p_reader
 * Pointer to file data reader.
 * \param[out] p_z64_xtrafld
 * Pointer to ZIP64 extra field structure to update.
 */
extern
void zfr_z64_xtrafld(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_READER*  p_reader,
/*@out@*/ /*@notnull@*/
  ZIP_XTRAFLD_ZIP64* p_z64_xtrafld);

#endif /* !__ZIP_FILE_H__ */

/* Log stripped */
