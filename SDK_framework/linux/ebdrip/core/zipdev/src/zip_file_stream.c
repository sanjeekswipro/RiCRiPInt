/** \file
 * \ingroup zipdev
 *
 * $HopeName: COREzipdev!src:zip_file_stream.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implements file streams on a PostScript ZIP device.
 *
 * A ZIP file stream maps the stream descriptor returned by the ZIP device to
 * the file descriptor used to read the data from the extracted file stored on
 * the ZIP file device. It also maintains the association between the descriptor
 * and the logical file it is open on.
 *
 * To keep ZIP file stream descriptors unique they are kept in a file stream
 * list. List entries are kept in decreasing descriptor order.  New streams use
 * the first unused descriptor in a file list found by searching from the back
 * of the lsit.
 */

#include "core.h"

#include "monitor.h"          /* monitorf */

#include "mm.h"               /* mm_alloc */

#include "zip_file.h"         /* ZIP_FILE */
#include "zip_file_stream.h"


/**
 * \brief A file stream open on a logical file on a ZIP device
 *
 * Keeps the association between the ZIP file stream descriptor and the
 * descriptor on the extracted file, as well as the original file.
 */
typedef struct ZIP_FILE_STREAM {
  dll_link_t  link;         /**< File stream list link. */
  DEVICE_FILEDESCRIPTOR fd;           /**< ZIP file stream descriptor. */
  DEVICE_FILEDESCRIPTOR real_fd;      /**< File descriptor on extracted file. */
/*@dependent@*/ /*@notnull */
  ZIP_FILE*   p_zip_file;   /**< Logical file stream is open on. */

  OBJECT_NAME_MEMBER
} ZIP_FILE_STREAM;

/** \brief ZIP file stream structure name used to generate hash checksum. */
#define ZIP_FILE_STREAM_OBJECT_NAME "ZIP File Stream"

/** \brief ZIP file stream list structure name used to generate hash checksum. */
#define ZIP_FILE_STREAM_LIST_OBJECT_NAME "ZIP File Stream List"


/* Initialise a stream list. */
void zfs_init_list(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list)
{
  HQASSERT((p_list != NULL),
           "zfs_init_list: NULL stream list pointer");

  DLL_RESET_LIST(&p_list->streams);

  NAME_OBJECT(p_list, ZIP_FILE_STREAM_LIST_OBJECT_NAME);

} /* zfs_init_list */


/**
 * \brief Find the stream associated with a ZIP file stream descriptor.
 *
 * \param[in] p_list
 * Pointer to stream list to search.
 * \param[in] fd
 * Descriptor of the ZIP file stream to find.
 *
 * \return
 * Pointer to the file stream associated with descriptor if it is found, else \c
 * NULL.
 */
static /*@null@*/ /*@dependent@*/
ZIP_FILE_STREAM* zfs_find(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list,
  DEVICE_FILEDESCRIPTOR fd)
{
  ZIP_FILE_STREAM*  p_stream;

  HQASSERT((p_list != NULL),
           "zfs_find: NULL stream list pointer");
  HQASSERT((fd >= 0),
           "zfs_find: invalid descriptor");

  VERIFY_OBJECT(p_list, ZIP_FILE_STREAM_LIST_OBJECT_NAME);

  /* Search until we find or go beyond it in the list */
  p_stream = DLL_GET_HEAD(&p_list->streams, ZIP_FILE_STREAM, link);
  while ( (p_stream != NULL) && (p_stream->fd >= fd) ) {
    if ( p_stream->fd == fd ) {
      break;
    }
    p_stream = DLL_GET_NEXT(p_stream, ZIP_FILE_STREAM, link);
  }

#ifdef ASSERT_BUILD
  if ( p_stream != NULL ) {
    VERIFY_OBJECT(p_stream, ZIP_FILE_STREAM_OBJECT_NAME);
  }
#endif /* ASSERT_BUILD */

  return(p_stream);

} /* zfs_find */


/* Open a new file stream on a logical file. */
DEVICE_FILEDESCRIPTOR zfs_open(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list,
/*@in@*/ /*@dependent@*/ /*@notnull@*/
  ZIP_FILE*         p_file,
  int32             openflags)
{
  DEVICE_FILEDESCRIPTOR fd;
  DEVICE_FILEDESCRIPTOR real_fd;
  ZIP_FILE_STREAM*  p_stream;
  ZIP_FILE_STREAM*  p_new_stream;

  HQASSERT((p_list != NULL),
           "zfs_open: NULL stream list pointer");
  HQASSERT((p_file != NULL),
           "zfs_open: NULL file pointer");

  VERIFY_OBJECT(p_list, ZIP_FILE_STREAM_LIST_OBJECT_NAME);

  /* Open new stream on extracted file */
  if ( !zfl_open(p_file, openflags, &real_fd) ) {
    return(-1);
  }

  /* Allocate stream */
  p_new_stream = mm_alloc(mm_pool_temp, sizeof(ZIP_FILE_STREAM), MM_ALLOC_CLASS_ZIP_FILE_STREAM);
  if ( p_new_stream == NULL ) {
    /* Close the file stream again */
    (void)zfl_close(p_file, real_fd, TRUE);
    return(-1);
  }

  /* Find available file descriptor for new stream */
  fd = 0;
  p_stream = DLL_GET_TAIL(&p_list->streams, ZIP_FILE_STREAM, link);
  while ( (p_stream != NULL) && (p_stream->fd == fd) ) {
    p_stream = DLL_GET_PREV(p_stream, ZIP_FILE_STREAM, link);
    fd++;
  }
  HQASSERT(((p_stream == NULL) || (fd < p_stream->fd)),
           "zfs_open: active stream list out of order");

  NAME_OBJECT(p_new_stream, ZIP_FILE_STREAM_OBJECT_NAME);

  /* Set returned and real descriptor for file stream */
  p_new_stream->fd = fd;
  p_new_stream->real_fd = real_fd;

  /* Link to logical file stream is on */
  p_new_stream->p_zip_file = p_file;

  DLL_RESET_LINK(p_new_stream, link);
  if ( p_stream != NULL ) {
    /* Add to stream list in order  */
    HQASSERT((p_stream->fd != p_new_stream->fd),
             "zfs_new: descriptor already in use");
    DLL_ADD_AFTER(p_stream, p_new_stream, link);

  } else { /* List is empty or descriptor is at the beginning */
    DLL_ADD_HEAD(&p_list->streams, p_new_stream, link);
  }

  return(p_new_stream->fd);

} /* zfs_open */


/* Close an existing ZIP file stream. */
int32 zfs_close(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST*  p_list,
  DEVICE_FILEDESCRIPTOR  fd,
  Bool              f_abort)
{
  int32             retcode;
  ZIP_FILE_STREAM*  p_stream;

  HQASSERT((p_list != NULL),
           "zfs_close: NULL stream list pointer");
  HQASSERT((fd >= 0),
           "zfs_close: invalid descriptor");

  VERIFY_OBJECT(p_list, ZIP_FILE_STREAM_LIST_OBJECT_NAME);

  /* Find the stream and close the real stream on the extracted file */
  p_stream = zfs_find(p_list, fd);
  if ( p_stream == NULL ) {
    return(0);
  }
  retcode = zfl_close(p_stream->p_zip_file, p_stream->real_fd, f_abort);

  /* Remove from stream list and free it */
  DLL_REMOVE(p_stream, link);

  UNNAME_OBJECT(p_stream);

  mm_free(mm_pool_temp, p_stream, sizeof(ZIP_FILE_STREAM));

  /* Return result of closing underlying stream no the extracted file */
  return(retcode);

} /* zfs_close */


/* Close all streams in a stream list. */
void zfs_close_list(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list,
  Bool              f_abort)
{
  ZIP_FILE_STREAM*  p_stream;

  HQASSERT((p_list != NULL),
           "zfs_close_list: NULL list pointer");

  VERIFY_OBJECT(p_list, ZIP_FILE_STREAM_LIST_OBJECT_NAME);

  /* Abort any open files */
  p_stream = DLL_GET_HEAD(&p_list->streams, ZIP_FILE_STREAM, link);
  while ( p_stream != NULL ) {
    (void)zfs_close(p_list, p_stream->fd, f_abort);
    p_stream = DLL_GET_HEAD(&p_list->streams, ZIP_FILE_STREAM, link);
  }

} /* zfs_close_list */


/* Return the number of bytes available to be read on the stream. */
Bool zfs_bytes_avail(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list,
  DEVICE_FILEDESCRIPTOR fd,
  int32                 reason,
/*@out@*/ /*@notnull@*/
  Hq32x2*               bytes)
{
  ZIP_FILE_STREAM*  p_stream;

  HQASSERT((bytes != NULL),
           "zfs_bytes_avail: NULL returned bytes pointer");

  VERIFY_OBJECT(p_list, ZIP_FILE_STREAM_LIST_OBJECT_NAME);

  p_stream = zfs_find(p_list, fd);
  if ( p_stream == NULL ) {
    HQFAIL("zfs_bytes_avail: unknown descriptor");
    return(FALSE);
  }

  return(zfl_bytes_avail(p_stream->p_zip_file, p_stream->real_fd, reason, bytes));

} /* zfs_bytes_avail */

/** 'ready' will be set to false if the next piece of the passed file is required
but is not available. Returns false on error.
*/
Bool zfs_next_piece_ready(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list,
  DEVICE_FILEDESCRIPTOR fd,
  Bool* ready)
{
  ZIP_FILE_STREAM*  p_stream;

  HQASSERT(p_list != NULL && ready != NULL,
           "zfs_next_piece_ready - invalid parameters.");

  /* Find descriptor in list */
  p_stream = zfs_find(p_list, fd);
  if ( p_stream == NULL ) {
    HQFAIL("zfs_read: unknown descriptor");
    return FALSE;
  }

  return zfl_next_piece_ready(p_stream->p_zip_file, p_stream->real_fd, ready);
}

/** Return the name of the specified file.
*/
void zfs_get_name(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list,
  DEVICE_FILEDESCRIPTOR fd,
/*@out@*/ /*@notnull@*/
  ZIP_FILE_NAME* p_name)
{
  ZIP_FILE_STREAM*  p_stream;

  /* Find descriptor in list */
  p_stream = zfs_find(p_list, fd);
  if ( p_stream == NULL ) {
    HQFAIL("zfs_read: unknown descriptor");
    return;
  }

  zfl_name(p_stream->p_zip_file, p_name);
}

/* Read from a stream. */
int32 zfs_read(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list,
  DEVICE_FILEDESCRIPTOR fd,
/*@out@*/ /*@notnull@*/
  uint8*      buff,
  int32       len)
{
  ZIP_FILE_STREAM*  p_stream;

  *buff = 0;

  /* Find descriptor in list */
  p_stream = zfs_find(p_list, fd);
  if ( p_stream == NULL ) {
    HQFAIL("zfs_read: unknown descriptor");
    return(-1);
  }

  /* Do read on extracted file */
  return(zfl_read(p_stream->p_zip_file, p_stream->real_fd, buff, len));

} /* zfs_read */


/* Write to a stream. */
int32 zfs_write(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list,
  DEVICE_FILEDESCRIPTOR fd,
/*@in@*/ /*@notnull@*/
  uint8*      buff,
  int32       len)
{
  ZIP_FILE_STREAM*  p_stream;

  /* Find descriptor in list */
  p_stream = zfs_find(p_list, fd);
  if ( p_stream == NULL ) {
    HQFAIL("zfs_write: unknown descriptor");
    return(-1);
  }

  /* Do read on extracted file */
  return(zfl_write(p_stream->p_zip_file, p_stream->real_fd, buff, len));

} /* zfs_write */


/* Do a seek on the file stream. */
Bool zfs_seek(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list,
  DEVICE_FILEDESCRIPTOR fd,
/*@in@*/ /*@out@*/ /*@notnull@*/
  Hq32x2*     destn,
  int32       flags)
{
/*@shared@*/
  ZIP_FILE_STREAM*  p_stream;

  HQASSERT((p_list != NULL),
           "zfs_seek: NULL stream list pointer");
  HQASSERT((fd >= 0),
           "zfs_seek: invalid descriptor");
  HQASSERT((destn != NULL),
           "zfs_seek: NULL pointer to returned seek position");

  VERIFY_OBJECT(p_list, ZIP_FILE_STREAM_LIST_OBJECT_NAME);

  /* Find descriptor in list */
  p_stream = zfs_find(p_list, fd);
  if ( p_stream == NULL ) {
    HQFAIL("zfs_seek: unknown descriptor");
    return(FALSE);
  }

  /* Do seek on the logical file */
  return(zfl_seek(p_stream->p_zip_file, p_stream->real_fd, destn, flags));

} /* zfs_seek */

#ifdef DEBUG_BUILD

/* Write to the monitor the name and descriptor all streams in the list. */
void zfs_dbg_dump(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_STREAM_LIST* p_list)
{
  ZIP_FILE_STREAM*  p_stream;
  ZIP_FILE_NAME     filename;

  HQASSERT((p_list != NULL),
           "zfs_dbg_dump: NULL stream list pointer");

  /* Report all open file streams - filename and zipdev file stream descriptor */
  p_stream = DLL_GET_HEAD(&p_list->streams, ZIP_FILE_STREAM, link);
  while ( p_stream != NULL ) {
    zfl_name(p_stream->p_zip_file, &filename);
    /* No UVS since internal debug builds only */
    monitorf((uint8*)"zipdev: Open filename:%.*s fd:%x\n", filename.namelength, filename.name, p_stream->fd);
    p_stream = DLL_GET_NEXT(p_stream, ZIP_FILE_STREAM, link);
  }

} /* zfs_dbg_dump */

#endif /* DEBUG_BUILD */


/* Log stripped */
