/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlstream.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

#ifndef __PCLXLSTREAM_H__
#define __PCLXLSTREAM_H__ (1)

#include "fileioh.h"

/** \brief An XL stream. */
typedef struct PCLXLSTREAM {
  sll_link_t  next;               /**< Link for list of streams. */
  FILELIST*   flptr;              /**< The stream file list. */
  Bool        big_endian;         /**< Binary encoding of data in stream. */
  uint32      last_tag;           /**< Last byte read in this stream when reading a tag. */
  uint32      op_counter;         /**< Count of last operator seen in this stream. */
  int32       protocol_version;   /**< Current protocol version of the stream. */
} PCLXLSTREAM;

/**
 * \brief Check if the stream has reached EOF.
 *
 * \param[in] p_stream
 * Stream pointer.
 *
 * \return
 * \c TRUE if the stream has reached EOF.
 */
extern
Bool pclxl_stream_eof(
  PCLXLSTREAM*  p_stream);
#define pclxl_stream_eof(s)             (isIEof((s)->flptr))


/**
 * \brief Get the index for the last operator tag read from the stream.
 *
 * \param[in] p_stream
 * Stream pointer.
 *
 * \return
 * The index of the last operator tag read from the stream.
 */
extern
uint32 pclxl_stream_op_counter(
  PCLXLSTREAM*  p_stream);
#define pclxl_stream_op_counter(s)      ((s)->op_counter)

/**
 * \brief Get the last tag byte read from the stream.
 *
 * \param[in] p_stream
 * Stream pointer.
 *
 * \return
 * The last tag byte read from the stream.
 */
extern
uint32 pclxl_stream_op_tag(
  PCLXLSTREAM*  p_stream);
#define pclxl_stream_op_tag(s)          ((s)->last_tag)

/**
 * \brief Get the current protocol version for the stream.
 *
 * \param[in] p_stream
 * Stream pointer.
 *
 * \return
 * The stream's current protocol version in packed form.
 */
extern
int32 pclxl_stream_protocol(
  PCLXLSTREAM*  p_stream);
#define pclxl_stream_protocol(s)        ((s)->protocol_version)

/**
 * \brief Check if the current stream protocol version is equal to or greater
 * than the one given.
 *
 * \param[in] p_stream
 * Stream pointer.
 * \param[in] protocol_version
 * Protocol version to compare against.
 *
 * \return
 * \c TRUE if the stream protocol version is at least the one given.
 */
extern
Bool pclxl_stream_min_protocol(
  PCLXLSTREAM*  p_stream,
  int32         protocol_version);
#define pclxl_stream_min_protocol(s, p) ((s)->protocol_version >= (p))

/**
 * \brief Check if the current stream protocol version is less than or equal to
 * than the one given.
 *
 * \param[in] p_stream
 * Stream pointer.
 * \param[in] protocol_version
 * Protocol version to compare against.
 *
 * \return
 * \c TRUE if the stream protocol version is no more than the one given.
 */
extern
Bool pclxl_stream_max_protocol(
  PCLXLSTREAM*  p_stream);
#define pclxl_stream_max_protocol(s, p) ((s)->protocol_version <= (p))

/**
 * \brief Initialise an XL stream with default values.
 *
 * \param[in] p_stream
 * Stream pointer.
 */
extern
void pclxl_stream_init(
  PCLXLSTREAM*  p_stream);

/**
 * \brief Start an XL stream on the file list.
 *
 * The file list is read to parse the stream header.  The stream is left
 * pointing at the first byte after the end of header (LF) byte.
 * The endianness and XL protocol version have been setup.
 *
 * If there is no stream header, or the stream header is invalid, then an error
 * is logged and FALSE is returned.  If the stream just contains a UEL then the
 * file list is closed and FALSE is returned but no error is logged.
 *
 * \param[in] pclxl_context
 * XL interpreter context.
 * \param[in] p_stream
 * Stream pointer.
 * \param[in] flptr
 * File list pointer for the stream
 *
 * \return
 * \c TRUE if the stream has been started on the file list, else \c FALSE.
 */
extern
Bool pclxl_stream_start(
  PCLXL_CONTEXT pclxl_context,
  PCLXLSTREAM*  p_stream,
  FILELIST*     flptr);

/**
 * \brief Read the next tag for a datatype, attribute, or operator in the
 * stream.
 *
 * The stream is read until the next tag is seen for a datatype, attribute, or
 * operator.  Embedded stream headers are parsed and the stream endianness and
 * protocol version updated as specified.  Whitespace bytes are skipped over.
 *
 * A UEL sequence causes the stream to be closed, and FALSE to be returned but
 * no error to be logged.
 *
 * Any other byte tag value, or an invalid stream header, causes an error to be
 * logged and FALSE to be returned.
 *
 * \param[in] pclxl_context
 * XL interpreter context.
 * \param[in] p_stream
 * Stream pointer.
 * \param[in] p_tag
 * Pointer to returned datatype, attribute, or operator tag.
 *
 * \return
 * \c TRUE if a valid tag byte has been seen, else /c FALSE.
 */
extern
Bool pclxl_stream_next_tag(
  PCLXL_CONTEXT pclxl_context,
  PCLXLSTREAM*  p_stream,
  uint32*       p_tag);

/**
 * \brief Read an attribute id from the stream.
 *
 * The attribute id is read according to the id data type.
 *
 * \param[in] p_stream
 * Stream pointer.
 * \param[in] type_tag
 * Type tag for the attribute id, ubyte or uint16.
 * \param[in] p_tag
 * Pointer to returned attribute id.
 *
 * \return
 * \c TRUE if a valid attribute id has been read, else /c FALSE.
 */
extern
Bool pclxl_stream_read_attribute(
  PCLXLSTREAM*  p_stream,
  uint32        type_tag,
  uint32*       p_attribute);

/**
 * \brief Read values from a stream based on the datatype tag.
 *
 * The function will always return the requested number of element values unless
 * the end of the stream is reached.
 *
 * \param[in] p_stream
 * Stream pointer.
 * \param[in] datatype_tag
 * Data type tag for the data values.
 * \param[out] buffer
 * Pointer to memory for values read.
 * \param[in] count
 * Number of values to read.
 *
 * \return
 * \c TRUE if all the data values have been read, else \c FALSE.
 */
extern
Bool pclxl_stream_read_data(
  PCLXLSTREAM*  p_stream,
  uint32        datatype_tag,
  void*         buffer,
  uint32        count);

/**
 * \brief Read length of a value array.
 *
 * This function is DEPRECATED in favour of using the embedded data functions.
 *
 * \param[in] pclxl_context
 * XL interpreter context.
 * \param[in] p_stream
 * Stream pointer.
 * \param[out] p_length
 * Pointer to returned length value.
 *
 * \return
 * \c TRUE if the array length has been read, else \c FALSE.
 */
extern
Bool pclxl_stream_read_array_length(
  PCLXL_CONTEXT pclxl_context,
  PCLXLSTREAM*  p_stream,
  uint32*       p_length);

/**
 * \brief Read length of embedded data.
 *
 * This function is DEPRECATED in favour of using the embedded data functions.
 *
 * \param[in] pclxl_context
 * XL interpreter context.
 * \param[in] p_stream
 * Stream pointer.
 * \param[out] p_length
 * Pointer to returned length value.
 *
 * \return
 * \c TRUE if the embedded data length has been read, else \c FALSE.
 */
extern
Bool pclxl_stream_read_data_length(
  PCLXL_CONTEXT pclxl_context,
  PCLXLSTREAM*  p_stream,
  uint32*       p_length);

/** \brief Data source reader state. */
typedef struct PCLXL_EMBEDDED_READER {
  PCLXLSTREAM*  p_stream;       /**< Stream to use for reading embedded data. */
  Bool          big_endian;     /**< Data organisation of embedded data. */
  uint32        length;         /**< Original embedded data length in bytes. */
  uint32        remaining;      /**< Remaining bytes of embedded data. */
  Bool          insufficient;   /**< Insufficient embedded data for the last read. */
} PCLXL_EMBEDDED_READER;

/**
 * \brief Get the original length of embedded data.
 *
 * \param[in] p_reader
 * Embedded reader pointer.
 *
 * \return
 * The original length of the embedded data.
 */
extern
uint32 pclxl_embedded_length(
  PCLXL_EMBEDDED_READER*  p_reader);
#define pclxl_embedded_length(e)        ((e)->length)

/**
 * \brief Get the remaining length of embedded data.
 *
 * \param[in] p_reader
 * Embedded reader pointer.
 *
 * \return
 * The remaining length of the embedded data.
 */
extern
uint32 pclxl_embedded_remaining(
  PCLXL_EMBEDDED_READER*  p_reader);
#define pclxl_embedded_remaining(e)     ((e)->remaining)

/**
 * \brief Check if there was insufficient data for the last read.
 *
 * \param[in] p_reader
 * Embedded reader pointer.
 *
 * \return
 * \c TRUE if there was insufficient data for the last read of embedded data,
 * else \c FALSE.
 */
extern
Bool pclxl_embedded_insufficient(
  PCLXL_EMBEDDED_READER*  p_reader);
#define pclxl_embedded_insufficient(e)  ((e)->insufficient)

/**
 * \brief Initialise an embedded reader on the stream with the given data
 * organisation.
 *
 * \param[in] pclxl_context
 * XL interpreter context.
 * \param[in] p_stream
 * Stream pointer.
 * \param[in] big_endian
 * Data organisation for reading the embedded data.
 * \param[out] p_embedded
 * Pointer to embedded reader to initialise.
 *
 * \return
 * \c TRUE if the embedded data length can be read, else \c FALSE.
 */
extern
Bool pclxl_stream_embedded_init(
  PCLXL_CONTEXT           pclxl_context,
  PCLXLSTREAM*            p_stream,
  Bool                    big_endian,
  PCLXL_EMBEDDED_READER*  p_embedded);

/**
 * \brief Read number of data values from the embedded data.
 *
 * Embedded data values can be either bye 8 bit or 16 bit signed and unsigned
 * integer values so can be stored with full range in a 32 bit signed integer.
 *
 * \param[in] p_reader
 * Embedded reader pointer.
 * \param[in] datatype
 * Datatype for the values to be read.
 * \param[out] buffer
 * Pointer to buffer.
 * \param[in] count
 * Number of data values to read.
 *
 * \return
 * \c TRUE if there was sufficient data and all the requested data values could
 * be read, else \c FALSE.
 */
extern
Bool pclxl_embedded_read_data(
  PCLXL_EMBEDDED_READER*  p_embedded,
  uint32                  datatype,
  int32*                  buffer,
  uint32                  count);

/**
 * \brief Read number of bytes from the embedded data.
 *
 * \param[in] p_reader
 * Embedded reader pointer.
 * \param[out] buffer
 * Pointer to buffer.
 * \param[in] len
 * Number of bytes to read.
 *
 * \return
 * \c TRUE if there was sufficient data and all the requested bytes could be
 * read, else \c FALSE.
 */
extern
Bool pclxl_embedded_read_bytes(
  PCLXL_EMBEDDED_READER*  p_embedded,
  uint8*                  buffer,
  uint32                  len);

/**
 * \brief Flush the embedded data of any remaining data.
 *
 * \param[in] p_reader
 * Embedded reader pointer.
 */
extern
Bool pclxl_embedded_flush(
  PCLXL_EMBEDDED_READER*  p_embedded);

/* DEPRECATED - to be removed! */
extern
void pclxl_stream_close(
  PCLXL_CONTEXT pclxl_context,
  PCLXLSTREAM*  p_stream);

#endif /* !__PCLXLSTREAM_H__ */

/* Log stripped */

/* EOF */
