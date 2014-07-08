/** \file
 * \ingroup tiffcore
 *
 * $HopeName: tiffcore!src:tifffile.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * High level core tiff reader routines
 */

#include "core.h"

#include "mm.h"         /* mm_alloc() */
#include "swdevice.h"   /* DEVICELIST */
#include "hq32x2.h"     /* Hq32x2 */
#include "hqmemcpy.h"   /* HqMemCpy */
#include "swerrors.h"   /* IOERROR */
#include "progress.h"   /* setReadFileProgress() */
#include "uvms.h"       /* UVS() */

#include "tifffile.h"


/*
 * TIFF file context structure.  Looks a little weedy now, but
 * just you wait ...
 */
struct tiff_file_s {
  FILELIST*     flptr;          /* Pointer to TIFF source file */
  int32         f_local_endian; /* File data in local endianness */
  uint32        offset;         /* offset from begining of the file for the TIFF header */
  mm_pool_t     mm_pool;
};


/*
 * tiff_file_seekable() tests to see if the specified PS file
 * is seekable, returning TRUE if it is, else FALSE.
 */
Bool tiff_file_seekable(
  FILELIST*   flptr)      /* O */
{
  DEVICE_FILEDESCRIPTOR d;
  DEVICELIST* dev;
  Hq32x2      ext;

  HQASSERT((flptr != NULL),
           "tiff_file_seekable: NULL file pointer");

  /* RSD filters are by definition seekable */
  if ( isIFilter(flptr) && isIRSDFilter(flptr) ) {
    return TRUE ;
  }

  /* No underlying device - looks like a non RSD filter and not seekable */
  dev = theIDeviceList(flptr);
  if ( dev == NULL ) {
    return FALSE ;
  }

  /* Check file size - implies seekability */
  d = theIDescriptor(flptr);
  return ((*theIBytesFile(dev))(dev, d, &ext, SW_BYTES_TOTAL_ABS) > 0);

} /* Function tiff_file_seekable */


/*
 * tiff_new_file() creates a new file context for the given source
 * file pointer.  If the context is created ok then it returns TRUE,
 * else it returns FALSE and sets VMERROR.
 */
Bool tiff_new_file(
  FILELIST*       flptr,          /* I */
  mm_pool_t       mm_pool,        /* I */
  tiff_file_t**   pp_new_file)    /* O */
{
  tiff_file_t*  p_file;
  Hq32x2 pos;

  HQASSERT((flptr != NULL),
           "tiff_new_file: NULL file pointer");
  HQASSERT((flptr != NULL),
           "tiff_new_file: NULL pointer to returned pointer");

  /* Allocate context signalling VMERROR as required */
  p_file = mm_alloc(mm_pool, sizeof(tiff_file_t), MM_ALLOC_CLASS_TIFF_FILE);
  if ( p_file != NULL ) {
    /* Initialise context */
    p_file->flptr = flptr;
    p_file->f_local_endian = FALSE;
    p_file->mm_pool = mm_pool;
    if ( (*theIMyFilePos(flptr))(flptr, &pos) == EOF ) {
      return FALSE;
    }
      /* now put the result into the offsets array */
    HQASSERT(pos.high == 0,"fileposition > 32 bit");
    p_file->offset = pos.low;

    setReadFileProgress(flptr);
  }
  else
    return error_handler(VMERROR);

  *pp_new_file = p_file;
  return TRUE;
} /* Function tiff_new_file */


/*
 * tiff_free_file() releases a file context and drops the file pointer
 * on the floor (i.e. does nothing with it).
 */
void tiff_free_file(
  tiff_file_t**   pp_file)        /* I */
{
  HQASSERT((pp_file != NULL),
           "tiff_free_file: NULL pointer to context");
  HQASSERT((*pp_file != NULL),
           "tiff_free_file: NULL context pointer");

  closeReadFileProgress((*pp_file)->flptr);

  /* Free context and set original pointer to NULL */
  mm_free((*pp_file)->mm_pool, *pp_file, sizeof(tiff_file_t));
  *pp_file = NULL;

} /* Function tiff_free_file */


/*
 * tiff_file_seek() tries to set the current file position to the
 * the one given.  Returns TRUE if set position ok, else FALSE.
 */
Bool tiff_file_seek(
  tiff_file_t*  p_file,           /* I */
  uint32        offset)           /* I */
{
  FILELIST* flptr;
  Hq32x2    file_offset;

  HQASSERT((p_file != NULL),
           "tiff_file_seek: NULL file context");

  flptr = p_file->flptr;

#if NEEDMYRESETFILE
 (void)(*theIMyResetFile(flptr))(flptr);
#endif

  /* Flag if we reach the end of file when seeking */
  Hq32x2FromUint32(&file_offset, offset + p_file->offset);
  return (*theIMySetFilePos(flptr))(flptr, &file_offset) != EOF ;

} /* Function tiff_file_seek */


/*
 * tiff_file_pos() returns the current file offset of the underlying TIFF file.
 */
Bool tiff_file_pos(
  tiff_file_t*  p_file,           /* I */
  uint32*       offset)           /* O */
{
  FILELIST* flptr;
  Hq32x2    file_offset;

  HQASSERT((p_file != NULL),
           "tiff_file_pos: NULL file context");
  HQASSERT((offset != NULL),
           "tiff_file_pos: NULL pointer to returned offset");

  flptr = p_file->flptr;

  if ( (*theIMyFilePos(flptr))(flptr, &file_offset) != EOF ) {
    return Hq32x2ToUint32(&file_offset, offset);
  }
  return FALSE ;

} /* Function tiff_file_pos */


/*
 * tiff_file_read() reads count bytes from the file into the
 * buffer pointed to by p_buffer.
 */
Bool tiff_file_read(
  tiff_file_t*  p_file,           /* I */
  uint8*        p_buffer,         /* I */
  uint32        count)            /* I */
{
  FILELIST *flptr ;
  int32  bytes_read;

  HQASSERT((p_file != NULL),
           "tiff_file_read: NULL file context");

  flptr = p_file->flptr ;
  HQASSERT(flptr, "No TIFF file") ;

  /* Ensure we don't read to the end of the file so its not closed automatically */
  if ( !isIFilter(flptr) || isIRSDFilter(flptr) ) {
    Hq32x2    avail;

    if ( (*theIMyBytesAvailable(flptr))(flptr, &avail) == EOF ) {
      return detail_error_handler(IOERROR, "Reached end of file prematurely.");
    }
    /* The largest size of a strip is 4GB so count being a uint32 should be
     * no problem.  Therefore we should have no problem writing the bytes
     * available from a Hq32x2 into it if there is less than count available.
     * ASSERT it just in case.
     */
    if ( Hq32x2CompareUint32(&avail, count) < 0 ) {
      Bool res = Hq32x2ToUint32(&avail, &count);
      UNUSED_PARAM(Bool, res) ;
      HQASSERT((res),
               "tiff_read: it has all gone horribly wrong.");
    }
  }

  if ( count == 0 ) {
    /* No more to read, but it is not file read error */
    return TRUE ;
  }

  if ( file_read(flptr, p_buffer, CAST_UNSIGNED_TO_INT32(count), &bytes_read) <= 0 )
    return FALSE ;

  HQASSERT((uint32)bytes_read == count,
           "tiff_file_read: failed to read right number of bytes");

  updateReadFileProgress();

  return TRUE;
} /* Function tiff_file_read */


/*
 * tiff_set_endian() sets the endianness of data in the file -
 * this will affect all subsequent reads of data.
 */
void tiff_set_endian(
  tiff_file_t*    p_file,           /* I */
  int32           f_machine)        /* I */
{
  HQASSERT((p_file != NULL),
           "tiff_set_endian: NULL file pointer");

  p_file->f_local_endian = f_machine;

} /* Function tiff_set_endian */


/*
 * tiff_get_ascii() reads in a ASCII value.  The function returns TRUE
 * if the value is successfully read, else FALSE and sets IOERROR.
 */
Bool tiff_get_ascii(
  tiff_file_t*    p_file,           /* I */
  uint32          count,            /* I */
  tiff_ascii_t*   p_ascii)          /* I */
{
  HQASSERT((p_file != NULL),
           "tiff_get_ascii: NULL file pointer");
  HQASSERT((count > 0),
           "tiff_get_ascii: nothing to read");
  HQASSERT((p_ascii != NULL),
           "tiff_get_ascii: NULL pointer to returned ASCII");

  if ( !tiff_file_read(p_file, (uint8 *)p_ascii, ENTRY_SIZE_TYPE_ASCII * count) )
    return detail_error_handler(IOERROR, "TIFF file truncated.");

  return TRUE ;

} /* Function tiff_get_ascii */


/*
 * tiff_get_byte() reads in a BYTE value.  The function returns TRUE
 * if the value is successfully read, else FALSE and sets IOERROR.
 */
Bool tiff_get_byte(
  tiff_file_t*    p_file,           /* I */
  uint32          count,            /* I */
  tiff_byte_t*    p_byte)           /* I */
{
  HQASSERT((p_file != NULL),
           "tiff_get_byte: NULL file pointer");
  HQASSERT((count > 0),
           "tiff_get_byte: nothing to read");
  HQASSERT((p_byte != NULL),
           "tiff_get_byte: NULL pointer to returned BYTEs");

  if ( !tiff_file_read(p_file, (uint8 *)p_byte, ENTRY_SIZE_TYPE_BYTE * count) )
    return detail_error_handler(IOERROR, "TIFF file truncated.");

  return TRUE ;

} /* Function tiff_get_byte */


/*
 * tiff_get_sbyte() reads in a SBYTE value.  The function returns TRUE
 * if the value is successfully read, else FALSE and sets IOERROR.
 */
Bool tiff_get_sbyte(
  tiff_file_t*    p_file,           /* I */
  uint32          count,            /* I */
  tiff_sbyte_t*   p_sbyte)          /* I */
{
  HQASSERT((p_file != NULL),
           "tiff_get_sbyte: NULL file pointer");
  HQASSERT((count > 0),
           "tiff_get_sbyte: nothing to read");
  HQASSERT((p_sbyte != NULL),
           "tiff_get_sbyte: NULL pointer to returned SBYTEs");

  if ( !tiff_file_read(p_file, (uint8 *)p_sbyte, ENTRY_SIZE_TYPE_SBYTE * count) )
    return detail_error_handler(IOERROR, "TIFF file truncated.");

  return TRUE ;

} /* Function tiff_get_sbyte */


/*
 * tiff_get_short() reads in a SHORT value, according to the endianness of the
 * buffer as set by tiff_set_endian() above.  The function return TRUE
 * if the value is successfully read, else FALSE and sets IOERROR.
 */
Bool tiff_get_short(
  tiff_file_t*    p_file,           /* I */
  uint32          count,            /* I */
  tiff_short_t*   p_short)          /* I */
{
  HQASSERT((p_file != NULL),
           "tiff_get_short: NULL file pointer");
  HQASSERT((count > 0),
           "tiff_get_short: nothing to read");
  HQASSERT((p_short != NULL),
           "tiff_get_short: NULL pointer to returned short");

  if ( !tiff_file_read(p_file, (uint8 *)p_short, ENTRY_SIZE_TYPE_SHORT * count) )
    return detail_error_handler(IOERROR, "TIFF file truncated.");

  if ( !p_file->f_local_endian )
    BYTE_SWAP16_BUFFER(p_short, p_short, count * 2) ;

  return TRUE ;

} /* Function tiff_get_short */


/*
 * tiff_get_long() reads in a LONG value, according to the endianness of the
 * buffer as set by tiff_set_endian() above.  The function return TRUE
 * if the value is successfully read, else FALSE and sets IOERROR.
 */
Bool tiff_get_long(
  tiff_file_t*    p_file,           /* I */
  uint32          count,            /* I */
  tiff_long_t*    p_long)           /* I */
{
  uint8*  pb;
  uint8   buffer[ENTRY_SIZE_TYPE_LONG];

  HQASSERT((p_file != NULL),
           "tiff_get_long: NULL file pointer");
  HQASSERT((count > 0),
           "tiff_get_long: nothing to read");
  HQASSERT((p_long != NULL),
           "tiff_get_long: NULL pointer to returned long");

  do {
    if ( !tiff_file_read(p_file, buffer, ENTRY_SIZE_TYPE_LONG) )
      return detail_error_handler(IOERROR, "TIFF file truncated.");

    /* Pick out the LONG as appropriate */
    pb = (uint8*)p_long;
    if ( p_file->f_local_endian ) {
      pb[0] = buffer[0];
      pb[1] = buffer[1];
      pb[2] = buffer[2];
      pb[3] = buffer[3];

    } else {
      pb[0] = buffer[3];
      pb[1] = buffer[2];
      pb[2] = buffer[1];
      pb[3] = buffer[0];
    }

    p_long++;
  } while ( --count > 0 );

  return TRUE ;

} /* Function tiff_get_long */



Bool tiff_get_double(
  tiff_file_t*    p_file,           /* I */
  uint32          count,            /* I */
  tiff_double_t*  p_double)         /* I */
{
  uint8*  pb;
  uint8   buffer[ENTRY_SIZE_TYPE_DOUBLE];

  HQASSERT((p_file != NULL),
           "tiff_get_double: NULL file pointer");
  HQASSERT((count > 0),
           "tiff_get_double: nothing to read");
  HQASSERT((p_double != NULL),
           "tiff_get_double: NULL pointer to returned double");

  do {
    if ( !tiff_file_read(p_file, buffer, ENTRY_SIZE_TYPE_DOUBLE) )
      return detail_error_handler(IOERROR, "TIFF file truncated.");

    /* Pick out the LONG as appropriate */
    pb = (uint8*)p_double;
    if ( p_file->f_local_endian ) {
      pb[0] = buffer[0];
      pb[1] = buffer[1];
      pb[2] = buffer[2];
      pb[3] = buffer[3];
      pb[4] = buffer[4];
      pb[5] = buffer[5];
      pb[6] = buffer[6];
      pb[7] = buffer[7];

    } else {
      pb[0] = buffer[7];
      pb[1] = buffer[6];
      pb[2] = buffer[5];
      pb[3] = buffer[4];
      pb[4] = buffer[3];
      pb[5] = buffer[2];
      pb[6] = buffer[1];
      pb[7] = buffer[0];
    }

    p_double++;
  } while ( --count > 0 );

  return TRUE ;


}



/*
 * tiff_get_rational() reads in a RATIONAL value, according to the endianness of the
 * buffer as set by tiff_set_endian() above.  The function return TRUE
 * if the value is successfully read, else FALSE and sets IOERROR.
 */
Bool tiff_get_rational(
  tiff_file_t*    p_file,           /* I */
  uint32          count,            /* I */
  tiff_rational_t rational)         /* I */
{
  uint8*  pb;
  uint8   buffer[ENTRY_SIZE_TYPE_RATIONAL];

  HQASSERT((p_file != NULL),
           "tiff_get_rational: NULL file pointer");
  HQASSERT((count > 0),
           "tiff_get_rational: nothing to read");
  HQASSERT((rational != NULL),
           "tiff_get_rational: NULL pointer to returned rational");

  do {
    if ( !tiff_file_read(p_file, buffer, ENTRY_SIZE_TYPE_RATIONAL) )
      return detail_error_handler(IOERROR, "TIFF file truncated.");

    /* Pick out the two LONGs as appropriate */
    pb = (uint8*)&(rational[RATIONAL_NUMERATOR]);
    if ( p_file->f_local_endian ) {
      /* Get numerator */
      pb[0] = buffer[0];
      pb[1] = buffer[1];
      pb[2] = buffer[2];
      pb[3] = buffer[3];

      /* Get denominator */
      pb = (uint8*)&(rational[RATIONAL_DENOMINATOR]);
      pb[0] = buffer[4];
      pb[1] = buffer[5];
      pb[2] = buffer[6];
      pb[3] = buffer[7];

    } else { /* Get numerator */
      pb[0] = buffer[3];
      pb[1] = buffer[2];
      pb[2] = buffer[1];
      pb[3] = buffer[0];

      /* Get denominator */
      pb = (uint8*)&(rational[RATIONAL_DENOMINATOR]);
      pb[0] = buffer[7];
      pb[1] = buffer[6];
      pb[2] = buffer[5];
      pb[3] = buffer[4];
    }

    rational++;
  } while ( --count > 0 );

  return TRUE ;

} /* Function tiff_get_rational */


/* same as tiff_get_rational but for SRATIONALs */
Bool tiff_get_srational(
  tiff_file_t*    p_file,           /* I */
  uint32          count,            /* I */
  tiff_srational_t srational)       /* I */
{
  uint8*  pb;
  uint8   buffer[ENTRY_SIZE_TYPE_SRATIONAL];

  HQASSERT((p_file != NULL),
           "tiff_get_srational: NULL file pointer");
  HQASSERT((count > 0),
           "tiff_get_srational: nothing to read");
  HQASSERT((srational != NULL),
           "tiff_get_srational: NULL pointer to returned srational");

  do {
    if ( !tiff_file_read(p_file, buffer, ENTRY_SIZE_TYPE_SRATIONAL) )
      return detail_error_handler(IOERROR, "TIFF file truncated.");

    /* Pick out the two LONGs as appropriate */
    pb = (uint8*)&(srational[RATIONAL_NUMERATOR]);
    if ( p_file->f_local_endian ) {
      /* Get numerator */
      pb[0] = buffer[0];
      pb[1] = buffer[1];
      pb[2] = buffer[2];
      pb[3] = buffer[3];

      /* Get denominator */
      pb = (uint8*)&(srational[RATIONAL_DENOMINATOR]);
      pb[0] = buffer[4];
      pb[1] = buffer[5];
      pb[2] = buffer[6];
      pb[3] = buffer[7];

    } else { /* Get numerator */
      pb[0] = buffer[3];
      pb[1] = buffer[2];
      pb[2] = buffer[1];
      pb[3] = buffer[0];

      /* Get denominator */
      pb = (uint8*)&(srational[RATIONAL_DENOMINATOR]);
      pb[0] = buffer[7];
      pb[1] = buffer[6];
      pb[2] = buffer[5];
      pb[3] = buffer[4];
    }

    srational++;
  } while ( --count > 0 );

  return TRUE ;

} /* Function tiff_get_srational */


/* Log stripped */
