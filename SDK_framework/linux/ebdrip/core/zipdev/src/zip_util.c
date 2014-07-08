/** \file
 * \ingroup zipdev
 *
 * $HopeName: COREzipdev!src:zip_util.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Utility functions for the PostScript ZIP device.
 */

#include "core.h"
#include "hqmemcmp.h"   /* HqMemCmp */
#include "calendar.h"
#include "swctype.h"    /* tolower */
#include "swerrors.h"
#include "mmcompat.h"

#include "zip_util.h"


/* Constants for PJW hash function */
#define PJW_SHIFT        (4)            /* Per hashed char hash shift */
#define PJW_MASK         (0xf0000000u)  /* Mask for hash top bits */
#define PJW_RIGHT_SHIFT  (24)           /* Right shift distance for hash top bits */

/* Compute a hash on a string.  This an implementation of hashpjw without any
 * branches in the loop. */
uint32 zutl_strhash(
/*@in@*/ /*@notnull@*/
  uint8*  p_string,
  int32   str_len)
{
  uint32 hash = 0;
  uint32 bits = 0;

  HQASSERT((p_string != NULL),
           "zutl_strhash: NULL string  pointer");
  HQASSERT((str_len > 0),
           "zutl_strhash: invalid string length");

  while ( --str_len >= 0 ) {
    hash = (hash << PJW_SHIFT) + *p_string++;
    bits = hash&PJW_MASK;
    hash ^= bits|(bits >> PJW_RIGHT_SHIFT);
  }

  return(hash);

} /* zutl_strhash */


/* Return a lower cased version of a string. */
void zutl_strlower(
/*@in@*/ /*@notnull@*/
  uint8*  p_string,
  int32   len,
/*@out@*/ /*@notnull@*/
  uint8*  p_low_string)
{
  HQASSERT((p_string != NULL),
           "zutl_strlower: NULL string pointer");
  HQASSERT((len > 0),
           "zutl_strlower: invalid string length");
  HQASSERT((p_low_string != NULL),
           "zutl_strlower: NULL returned string pointer");

  do {
    *p_low_string = (uint8)tolower(*p_string);
    p_low_string++;
    p_string++;
  } while ( len-- > 0 );

} /* zutl_strlower */


/* Initialise new string to be scanned. */
void zutl_scan_init(
/*@out@*/ /*@notnull@*/
  ZIP_UTIL_STR_SCAN*  p_scan,
/*@in@*/ /*@notnull@*/ /*@dependent@*/
  uint8*              p_string,
  uint32              len)
{
  HQASSERT((p_scan != NULL),
           "zutl_scan_init: NULL scan pointer");
  HQASSERT((p_string != NULL),
           "zutl_scan_init: NULL string pointer");

  p_scan->p_string = p_string;
  p_scan->len = len;

} /* zutl_scan_init */


/* Match a fixed pattern at the start of the string. */
extern
Bool zutl_scan_string(
/*@in@*/ /*@notnull@*/
  ZIP_UTIL_STR_SCAN*  p_scan,
/*@in@*/ /*@notnull@*/
  uint8*              p_string,
  uint32              len)
{
  HQASSERT((p_scan != NULL),
           "zutl_scan_string: NULL scan pointer");
  HQASSERT((p_string != NULL),
           "zutl_scan_string: NULL string pointer");
  HQASSERT((len > 0),
           "zutl_scan_string: invalid string length");

  /* Quick exit if not enough string left */
  if ( p_scan->len < len ) {
    return(FALSE);
  }

  /* Match the piece number */
  if ( HqMemCmp(p_scan->p_string, len, p_string, len) != 0 ) {
    return(FALSE);
  }

  /* Advance along the string being scanned */
  p_scan->p_string += len;
  p_scan->len -= len;

  return(TRUE);

} /* zutl_scan_string */


/* Parse an unsigned decimal integer in the given string. */
int32 zutl_scan_decimal(
/*@in@*/ /*@notnull@*/
  ZIP_UTIL_STR_SCAN*  p_scan)
{
  uint32  digit;
  uint32  number;
  uint32  len;
  uint8*  str;

  HQASSERT((p_scan != NULL),
           "zutl_scan_decimal: NULL scan pointer");

  str = p_scan->p_string;
  len = p_scan->len;

  /* Must start with a decimal digit */
  if ( (len == 0) || !isdigit(*str) ) {
    return(-1);
  }

  /* No leading zeros */
  if ( (*str == '0') && (len > 1) && isdigit(*(str + 1)) ) {
    return(-1);
  }

  /* Parse the decimal number */
  number = 0;
  do {
    digit = (*str++ - '0');
    if ( (MAXINT - digit)/10 < number ) {
      /* About to overflow - so an error then */
      return(-1);
    }
    number = 10*number + digit;
  } while ( (--len > 0) && isdigit(*str) );

  /* Update source string to after number */
  p_scan->p_string = str;
  p_scan->len = len;

  return(number);

} /* zutl_scan_decimal */


/* Flag if there is any of the string left to be scanned. */
extern
Bool zutl_scan_atend(
/*@in@*/ /*@notnull@*/
  ZIP_UTIL_STR_SCAN*  p_scan)
{
  HQASSERT((p_scan != NULL),
           "zutl_scan_atend: NULL scan pointer");

  return(p_scan->len == 0);

} /* zutl_scan_atend */


/* Set size of buffer to use when reading from the device. */
int32 zutl_dev_bufsize(
/*@in@*/ /*@notnull@*/
  DEVICELIST* p_dev,
  int32       buf_min,
  int32       buf_def)
{
  int32 bufsize;

  HQASSERT((p_dev != NULL),
           "zutl_dev_bufsize: NULL device pointer");
  HQASSERT((buf_min > 0),
           "zutl_dev_bufsize: invalid minimum buffer size");
  HQASSERT((buf_def > 0),
           "zutl_dev_bufsize: invalid default buffer size");
  HQASSERT((buf_min <= buf_def),
           "zutl_dev_bufsize: default buffer size smaller than minimum");

  /* Get device's preferred size. */
  bufsize = -1;
  if ( theIGetBuffSize(p_dev) != NULL ) {
    bufsize = (*theIGetBuffSize(p_dev))(p_dev);
  }
  if ( bufsize < 0 ) {
    /* No preferred size, use default */
    bufsize = buf_def;
  }
  /* Return size no smaller than minimum wanted */
  return(max(buf_min, bufsize));

} /* zutl_dev_bufsize */

/* Returns the current date and time, using the Calendar device to obtain the
 * current time details. */
uint32 zutl_msdos_date(void)
{
  int32           year;
  int32           month;
  int32           day;
  int32           hour;
  int32           minute;
  int32           second;
  int32           running;
  uint32          date_time;

  /* Get date/time values only if the calendar device is running */
  date_time = 0;
  if ( !get_calendar_params(&year, &month, &day, &hour, &minute, &second,
                            &running) ) {
    /* Clear any error raised - most likely device not present */
    error_clear();

  } else if ( running ) {
    year -= 1980;
    /* Pack into DOS date/time format */
    date_time = (((year << 9) | (month << 5) | day) << 16) |
                  ((hour << 11) | (minute << 5) | ((uint32)second > 1));
  }

  return(date_time);

} /* zutl_msdos_date */


/* Function hook for zlib to use the RIP memory allocator. */
void* zutl_zlib_alloc(
/*@in@*/ /*@null@*/
  void*   opaque,
  uint32  items,
  uint32  size)
{
  UNUSED_PARAM(void*, opaque);

  return(mm_alloc_with_header(mm_pool_temp, (mm_size_t)(items*size), MM_ALLOC_CLASS_ZIP_ZLIB));

} /* zutl_zlib_alloc */


/* Function hook for zlib to use the RIP memory deallocator. */
void zutl_zlib_free(
/*@in@*/ /*@null@*/
  void*   opaque,
/*@in@*/ /*@notnull@*/
  void*   p)
{
  UNUSED_PARAM(void*, opaque);

  mm_free_with_header(mm_pool_temp, p);

} /* zutl_zlib_free */


/* Log stripped */
