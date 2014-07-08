/** \file
 * \ingroup zipdev
 *
 * $HopeName: COREzipdev!src:zip_file_stream.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implements file streams for files extracted from a ZIP archive.
 */

#ifndef __ZIP_FILE_STREAM_H__
#define __ZIP_FILE_STREAM_H__  (1)

#include "objnamer.h" /* OBJECT_NAME_MEMBER */
#include "lists.h"    /* dll_link_t */
#include "zip_file.h" /* ZIP_FILE */


/**
 * \brief A list of streams.
 */
typedef struct ZIP_FILE_STREAM_LIST {
  dll_list_t  streams;      /** \brief List of open file streams. */

  OBJECT_NAME_MEMBER
} ZIP_FILE_STREAM_LIST;


/**
 * \brief Initialise a stream list.
 *
 * Before a ZIP file stream list can be used, this function must be called to
 * set it up ready for use.
 *
 * \param[out] p_list
 * Pointer to stream list to initialise.
 */
extern
void zfs_init_list(
/*@out@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list);


/**
 * \brief Open a new file stream on a logical file.
 *
 * Open a new read only stream on the extracted file. Find the lowest unused
 * descriptor, insert the new stream into the list in descriptor sequence, and
 * return the descriptor.
 *
 * \param[in] p_list
 * Pointer to stream list to add the new stream to.
 * \param[in] p_file
 * Pointer to the logical file to open the stream on.
 * \param[in] openflags
 * Flags controlling opening of file.
 *
 * \return
 * A new file stream descriptor on the logical file, or \c -1 if it failed to
 * open.
 */
extern
DEVICE_FILEDESCRIPTOR zfs_open(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list,
/*@in@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_FILE*             p_file,
  int32                 openflags);

/**
 * \brief Close an existing ZIP file stream.
 *
 * The stream on the extracted file is closed before removing the stream from
 * the list and freeing it off.
 *
 * If the ZIP device has been forcible closed then all the open file streams
 * are closed, but it is possible for the interpreter to try and close a stream
 * again (either explicitly via a closefile or via a restore) in which case we
 * can jusat say it succeeded.
 *
 * \param[in] p_list
 * Pointer to stream list containing file descriptor.
 * \param[in] fd
 * File descriptor to close.
 * \param[in] f_abort
 * Flag indicating if the file is being aborted.
 *
 * \return
 * \c 0 if the file was closed/aborted without error, else \c -1.
 */
extern
int32 zfs_close(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list,
  DEVICE_FILEDESCRIPTOR fd,
  Bool                  f_abort);


/**
 * \brief Close all streams in a stream list.
 *
 * \param[in] p_list
 * Pointer to stream list having all streams in it closed.
 * \param[in] f_abort
 * Flag indicating if the stream list is being aborted.
 */
extern
void zfs_close_list(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list,
  Bool                  f_abort);


/**
 * \brief Return the number of bytes available to be read on the stream.
 *
 * \param[in] p_list
 * Pointer to stream list containing file descriptor.
 * \param[in] fd
 * File descriptor of stream to find the number of available bytes.
 * \param[in] reason
 * Flag to indicate the type of byte count wanted.
 * \param[out] bytes
 * The number of bytes available on the stream.
 *
 * \return
 * \c TRUE if found number of available bytes successfully, else \c FALSE.
 */
extern
Bool zfs_bytes_avail(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list,
  DEVICE_FILEDESCRIPTOR fd,
  int32                 reason,
/*@out@*/ /*@notnull@*/
  Hq32x2*               bytes);

/** \brief 'ready' will be set to false if the next piece of the passed file is
 * required before any read can take place, but is not currently available.
 * Returns false on error.
 *
 * \param[in] p_list
 * Pointer to stream list containing file descriptor.
 * \param[in] fd
 * File descriptor of stream to find the number of available bytes.
 * \param[out] ready
 * Will be set to true of false accordingly.
 */
extern Bool zfs_next_piece_ready(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list,
  DEVICE_FILEDESCRIPTOR fd,
  Bool* ready);

/** \brief Return the name of the specified file.
 *
 * \param[in] p_list
 * Pointer to stream list containing file descriptor.
 * \param[in] fd
 * File descriptor of stream to find the number of available bytes.
 * \param[out] p_name
 * Pointer to a ZIP_FILE_NAME object that will be set to the name of the archive
 * entry. No memory will be allocated, and the name pointed to should be
 * considered read-only.
 */
extern void zfs_get_name(ZIP_FILE_STREAM_LIST* p_list,
                         DEVICE_FILEDESCRIPTOR fd,
                         ZIP_FILE_NAME* p_name);

/**
 * \brief Read from a stream.
 *
 * \param[in] p_list
 * Pointer to stream list containing file descriptor.
 * \param[in] fd
 * File descriptor of stream to read from.
 * \param[out] buff
 * Pointer to buffer to read data into.
 * \param[in] len
 * Amount of data to read.
 *
 * \return
 * Result of reading from the underlying file.
 */
extern
int32 zfs_read(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list,
  DEVICE_FILEDESCRIPTOR fd,
/*@out@*/ /*@notnull@*/
  uint8*                buff,
  int32                 len);

/**
 * \brief Write to a stream.
 *
 * \param[in] p_list
 * Pointer to stream list containing file descriptor.
 * \param[in] fd
 * File descriptor of stream to write to.
 * \param[out] buff
 * Pointer to buffer to write data from.
 * \param[in] len
 * Amount of data to write.
 *
 * \return
 * Result of reading from the underlying file.
 */
extern
int32 zfs_write(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list,
  DEVICE_FILEDESCRIPTOR fd,
/*@in@*/ /*@notnull@*/
  uint8*                buff,
  int32                 len);

/**
 * \brief Do a seek on the file stream.
 *
 * \param[in] p_list
 * Pointer to file stream list containing descriptor.
 * \param[in] fd
 * Stream file descriptor.
 * \param[in,out] destn
 * Position to seek to, or current position, based on \p flags.
 * \param[in] flags
 * Flags indicating type of seek to do.
 *
 * \return
 * \c TRUE if seek on file stream succeeded, else \c FALSE.
 */
extern
Bool zfs_seek(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list,
  DEVICE_FILEDESCRIPTOR fd,
/*@in@*/ /*@out@*/ /*@notnull@*/
  Hq32x2*               destn,
  int32                 flags);


/** \brief Return true if there are no open file streams. */
extern Bool zfs_isempty(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list);
#define zfs_isempty(l)  (DLL_LIST_IS_EMPTY(&((l)->streams)))

/**
 * \brief Write to the monitor the name and descriptor all streams in the list.
 *
 * \param[in] p_list
 * Pointer to file stream list.
 */
extern
void zfs_dbg_dump(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list);

#endif /* !__ZIP_FILE_STREAM_H__ */


/* Log stripped */
