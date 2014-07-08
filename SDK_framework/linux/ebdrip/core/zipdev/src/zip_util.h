/** \file
 * \ingroup zipdev
 *
 * $HopeName: COREzipdev!src:zip_util.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Utility functions for the PostScript ZIP device.
 */

#ifndef __ZIP_UTIL_H__
#define __ZIP_UTIL_H__  (1)

#include "swdevice.h"   /* DEVICELIST */

/* Macros to read structure data and coping with endianness for all platforms.
 * Short and long are PKWARE terms for the data sizes.
 */
/*@-exportheader@*/
/** \brief Read an unsigned 16-bit word in Intel little endian order. */
uint16 READ_SHORT(
/*@in@*/ /*@notnull@*/
  uint8*  p_data);
#define READ_SHORT(p) ((uint16)(((uint8*)(p))[0] | (((uint8*)(p))[1] << 8)))
/** \brief Read an unsigned 32-bit word in Intel little endian order. */
uint32 READ_LONG(
/*@in@*/ /*@notnull@*/
  uint8*  p_data);
#define READ_LONG(p)  ((uint32)(((uint8*)(p))[0] | (((uint8*)(p))[1] << 8) | \
                            (((uint8*)(p))[2] << 16) | (((uint8*)(p))[3] << 24)))
/** \brief Read an unsigned 64-bit value in Intel little endian order. */
void READ_LONGLONG(
/*@out@*/ /*@notnull@*/
  HqU32x2*  p_value,
/*@in@*/ /*@notnull@*/
  uint8*    p_data);
#define READ_LONGLONG(v, p) \
MACRO_START \
  (v)->low = READ_LONG(p); \
  (v)->high = READ_LONG(&(p)[4]); \
MACRO_END

/** \brief Write an unsigned 16-bit word in Intel little endian order */
void WRITE_SHORT(
/*@out@*/ /*@notnull@*/
  uint8*  p_data,
  uint16  value);
#define WRITE_SHORT(p, v) \
MACRO_START \
  (p)[0] = CAST_UNSIGNED_TO_UINT8((v) & 0xff); \
  (p)[1] = CAST_UNSIGNED_TO_UINT8((v) >> 8); \
MACRO_END
/** \brief Write an unsigned 32-bit word in Intel little endian order */
void WRITE_LONG(
/*@out@*/ /*@notnull@*/
  uint8*  p_data,
  uint32  value);
#define WRITE_LONG(p, v) \
MACRO_START \
  (p)[0] = CAST_UNSIGNED_TO_UINT8((v) & 0xff); \
  (p)[1] = CAST_UNSIGNED_TO_UINT8(((v) >> 8) & 0xff); \
  (p)[2] = CAST_UNSIGNED_TO_UINT8(((v) >> 16) & 0xff); \
  (p)[3] = CAST_UNSIGNED_TO_UINT8((v) >> 24); \
MACRO_END
/** \brief Write an unsigned 64-bit value in Intel little endian order */
void WRITE_LONGLONG(
/*@out@*/ /*@notnull@*/
  uint8*    p_data,
  HqU32x2*  value);
#define WRITE_LONGLONG(p, v) \
MACRO_START \
  WRITE_LONG((p), (v)->low); \
  WRITE_LONG(&(p)[4], (v)->high); \
MACRO_END
/*@=exportheader@*/


/**
 * \brief Compute a hash on a string.
 *
 * \param[in] p_string
 * Pointer to string to generate hash for.
 * \param[in] str_len
 * Length of string.
 *
 * \return
 * Unsigned 32-bit hash value for the string.
 */
extern
uint32 zutl_strhash(
/*@in@*/ /*@notnull@*/
  uint8*      p_string,
  int32       str_len);

/**
 * \brief Return a lower cased version of a string.
 *
 * This function does not handle multi-byte strings, and only lower cases 7-bit
 * ASCII letters.
 *
 * \param[in] p_string
 * Pointer to original string.
 * \param[in] len
 * Length of string to convert to lower case.
 * \param[out] p_low_string
 * Pointer to buffer to write lower cased version of string to.
 */
extern
void zutl_strlower(
/*@in@*/ /*@notnull@*/
  uint8*      p_string,
  int32       len,
/*@out@*/ /*@notnull@*/
  uint8*      p_low_string);


/** \brief Structure to hold string that will be scanned. */
typedef struct ZIP_UTIL_STR_SCAN {
/*@notnull@*/ /*@observer@*/
  uint8*      p_string;       /**< Pointer to next character to scan. */
  uint32      len;            /**< Number of remaining characters to scan */
} ZIP_UTIL_STR_SCAN;

/**
 * \brief Initialise new string to be scanned.
 *
 * \param[out] p_scan
 * Pointer to scanned string to initialise.
 * \param[in] p_string
 * Pointer to start of string that will be scanned.
 * \param[in] len
 * Length of string to be scanned.
 */
extern
void zutl_scan_init(
/*@out@*/ /*@notnull@*/
  ZIP_UTIL_STR_SCAN*  p_scan,
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  uint8*              p_string,
  uint32              len);

/** \brief Helper for specifying literal strings to match in a scanned string. */
/*@notfunction@*/
#define ZUTL_SCAN_STR(s) (uint8*)(s), (sizeof(s) - 1)

/**
 * \brief Match a fixed pattern at the start of the string.
 *
 * \param[in] p_scan
 * Pointer to source string being scanned.
 * \param[in] p_string
 * Pointer to string to match in source string.
 * \param[in] len
 * Length of string to match.
 *
 * \return
 * \c TRUE if string matches start of scanned string, else \c FALSE.
 */
extern
Bool zutl_scan_string(
/*@in@*/ /*@notnull@*/
  ZIP_UTIL_STR_SCAN*  p_scan,
/*@in@*/ /*@notnull@*/
  uint8*              p_string,
  uint32              len);

/**
 * \brief Parse an unsigned decimal integer in the given string.
 *
 * Valid numbers are in the range 0 to 2^31-1, but cannot be left padded with
 * zeros.
 *
 * \param[in] p_scan
 * Pointer to source string being scanned.
 *
 * \return
 * The non-negative number parsed from the scanned string if one was found, else
 * \c -1.
 */
extern
int32 zutl_scan_decimal(
/*@in@*/ /*@notnull@*/
  ZIP_UTIL_STR_SCAN*  p_scan);

/**
 * \brief Flag if there is any of the string left to be scanned.
 *
 * \param[in] p_scan
 * Pointer to source string being scanned.
 *
 * \return
 * \c TRUE if there is still some string left to be scanned, else \c FALSE.
 */
extern
Bool zutl_scan_atend(
/*@in@*/ /*@notnull@*/
  ZIP_UTIL_STR_SCAN*  p_scan);


/**
 * \brief Set size of buffer to use when reading from the device.
 *
 * \param[in] p_dev
 * Device pointer.
 * \param[in] buf_min
 * Minimum buffer size to use.
 * \param[in] buf_def
 * Default bufffer size to use.
 *
 * \return
 * Size of buffer to use with the device.
 */
extern
int32 zutl_dev_bufsize(
/*@in@*/ /*@notnull@*/
  DEVICELIST* p_dev,
  int32       buf_min,
  int32       buf_def);

/**
 * \brief Returns the current date and time, using the Calendar device to
 * obtain the current time details.
 *
 * \return
 * The date in a packed MSDOS format, or zero if the calendar device is not
 * accessible.
 */
extern
uint32 zutl_msdos_date(void);

/**
 * \brief Function hook for zlib to use the RIP memory allocator.
 *
 * Supplied to zlib at initialisation time so it does not use \c malloc().
 * As there is no structure to hang the memory off or record how much memory is
 * being allocated, we have to use \c mm_alloc_with_header() so we can free what
 * is allocated.
 *
 * \see zutl_zlib_free
 *
 * \param[in] opaque
 * Unused pointer.
 * \param[in] items
 * Number of items to allocate.
 * \param[in] size
 * Size of each item in bytes.
 *
 * \return
 * Pointer to allocated memory, or \c NULL.
 */
extern
void* zutl_zlib_alloc(
/*@in@*/ /*@null@*/
  void*   opaque,
  uint32  items,
  uint32  size);

/**
 * \brief Function hook for zlib to use the RIP memory deallocator.
 *
 * Supplied to zlib at initialisation time so it does not use \c free().
 *
 * \see zutl_zlib_alloc
 *
 * \param[in] opaque
 * Unused pointer.
 * \param[in] p
 * Pointer to memory to deallocate.
 */
extern
void zutl_zlib_free(
/*@in@*/ /*@null@*/
  void*   opaque,
/*@in@*/ /*@notnull@*/
  void*   p);


#endif /* !__ZIP_UTIL_H__ */


/* Log stripped */
