/** \file
 * \ingroup tiffcore
 *
 * $HopeName: tiffcore!export:tifffile.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * High level API to access Tiff files
 */


#ifndef __TIFFFILE_H__
#define __TIFFFILE_H__ (1)

#include "fileio.h"       /* FILELIST */
#include "tiftypes.h"     /* tiff_byte_t */

#define NEEDMYRESETFILE 0
/*
 *  File context opaque type.
 */
typedef struct tiff_file_s tiff_file_t;


/*
 * tiff_file_seekable() tests to see if the specified PS file
 * is seekable, returning TRUE if it is, else FALSE.
 */
Bool tiff_file_seekable(
  FILELIST*   flptr);             /* I */


/*
 * tiff_new_file() creates a new file context for the given source
 * file pointer.  If the context is created ok then it returns TRUE,
 * else it returns FALSE and sets VMERROR.
 * tiff_free_file() releases a file context and drops the file pointer
 * on the floor (i.e. does nothing with it).
 */
Bool tiff_new_file(
  FILELIST*       flptr,          /* I */
  mm_pool_t       mm_pool,        /* I */
  tiff_file_t**   pp_new_file);   /* O */

void tiff_free_file(
  tiff_file_t**   pp_file);       /* I */


/*
 * tiff_set_endian() sets the endianness of data in the file -
 * this will affect all subsequent reads of data.
 */
void tiff_set_endian(
  tiff_file_t*    p_file,         /* I */
  int32           f_machine);     /* I */


/*
 * tiff_get_byte(), tiff_get_short(), tiff_get_long(), and tiff_get_rational()
 * read in values of their type, according to the endianness of the file as
 * set by tiff_set_endian() above (except for byte).  The functions
 * return TRUE if the value is successfully read, else FALSE and sets IOERROR.
 */
Bool tiff_get_ascii(
  tiff_file_t*    p_file,           /* I */
  uint32          count,            /* I */
  tiff_ascii_t*   p_ascii);         /* O */

Bool tiff_get_byte(
  tiff_file_t*    p_file,           /* I */
  uint32          count,            /* I */
  tiff_byte_t*    p_byte);          /* O */

Bool tiff_get_sbyte(
  tiff_file_t*    p_file,           /* I */
  uint32          count,            /* I */
  tiff_sbyte_t*   p_sbyte);         /* O */

Bool tiff_get_short(
  tiff_file_t*    p_file,           /* I */
  uint32          count,            /* I */
  tiff_short_t*   p_short);         /* O */

Bool tiff_get_long(
  tiff_file_t*    p_file,           /* I */
  uint32          count,            /* I */
  tiff_long_t*    p_long);          /* O */

Bool tiff_get_rational(
  tiff_file_t*    p_file,           /* I */
  uint32          count,            /* I */
  tiff_rational_t rational);        /* O */

Bool tiff_get_srational(
  tiff_file_t*    p_file,           /* I */
  uint32          count,            /* I */
  tiff_srational_t srational);      /* O */

#define tiff_get_sshort(p_file,count,p_sshort)      tiff_get_short((p_file),(count),(tiff_short_t*)(p_sshort))
#define tiff_get_slong(p_file,count,p_slong)        tiff_get_long ((p_file),(count),(tiff_long_t*)(p_slong))
#define tiff_get_float(p_file,count,p_float)        tiff_get_long ((p_file),(count),(tiff_long_t*)(p_float))

Bool tiff_get_double(
  tiff_file_t*    p_file,           /* I */
  uint32          count,            /* I */
  tiff_double_t*  p_double);        /* O */

/*
 * tiff_file_seek() tries to set the current file position to the
 * the one given.  Returns TRUE if set position ok, else FALSE.
 */
Bool tiff_file_seek(
  tiff_file_t*  p_file,           /* I */
  uint32        offset);          /* I */

Bool tiff_file_pos(
  tiff_file_t*  p_file,           /* I */
  uint32*       offset);          /* O */

/*
 * tiff_file_read() reads up to count bytes from the file into the
 * buffer pointed to by p_buffer, returning actual number read in
 * p_read.  If the file is read ok then TRUE is returned, else FALSE.
 */
Bool tiff_file_read(
  tiff_file_t*  p_file,           /* I */
  uint8*        pb,               /* I */
  uint32        count);           /* I */

#endif /* !__TIFFFILE_H__ */


/* Log stripped */
