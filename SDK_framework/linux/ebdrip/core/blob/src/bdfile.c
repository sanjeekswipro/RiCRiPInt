/** \file
 * \ingroup blob
 *
 * $HopeName: COREblob!src:bdfile.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Blob data cache routines to get data from a file source.
 */


#define OBJECT_SLOTS_ONLY

#include "core.h"
#include "objnamer.h"
#include "swerrors.h"
#include "objects.h"
#include "fileio.h"
#include "devices.h"
#include "mm.h"
#include "hqmemcmp.h"
#include "hqmemcpy.h"
#include "paths.h"    /* PROTECTED_*. Yuck. Should be somewhere else. */
#include "blobdata.h"

#include "hqxcrypt.h"
#include "hqxfonts.h"

/** Private data for the file routine. This set of data accessors needs to
   determine whether the close routine should close the file. We keep a copy
   of the file object between the open and close methods; this is not a GC
   root, because open and close should be paired, and will be lower than the
   top interpreter level. */
struct blobdata_private_t {
  DEVICELIST *dev ;  /**< Device on which real blob file is found */
  DEVICE_FILEDESCRIPTOR fhandle ;   /**< Handle of file on device */
  Bool isopen ;      /**< Is it open yet? */
  Bool length_ok ;   /**< Is the length field valid? */
  Hq32x2 length ;    /**< The length of the file. */
  Bool encrypted ;   /**< HQX encrypted? */
  int32 hqxoffset ;  /**< If HQX encrypted, this is the encryption offset. */
  int32 hqxtail ;    /**< If HQX encrypted, this is the encryption tail. */
  int mode ;         /**< Extra opening modes. */
  OBJECT_NAME_MEMBER
} ;

#define BLOBDATA_PRIVATE_NAME "Blobdata file private"

/* Methods to get data out of a file. Data copying is always required. There
   are three sub-cases dealt with:

   1) The source is an open file or seekable filter. Normal file operations
      are used to set locations and get characters and buffers from the file.
   2) The source is a closed device file. The device filename is opened and
      converted into a file object, and closed again on termination.
   3) The source is a device file that has been restored. In this case, the
      restored method will allocate a global string and store the filename in
      it. The filename is opened and the object converted to a file on open,
      and closed on termination. */

/** Match the same underlying file, and device files with the same name. */
static Bool blobdata_file_same(const OBJECT *newo, const OBJECT *cached)
{
  FILELIST *newflptr ;

  HQASSERT(newo && cached, "Objects missing for file comparison") ;
  HQASSERT(oType(*newo) == OFILE, "Blob data source is not a file") ;
  HQASSERT(oType(*cached) == OFILE || oType(*cached) == OSTRING,
           "Blob data source is not a file or a filename") ;

  newflptr = oFile(*newo) ;
  HQASSERT(newflptr, "No filelist in file object") ;

  /* Object identity has already been checked. The file is the same if:
     1) The file pointers are the same
     2) The file names and devices of the files are the same
     3) The file name and device of the new file match a filename */
  if ( oType(*cached) == OFILE ) {
    FILELIST *oldflptr = oFile(*cached) ;

    if ( oldflptr == newflptr )
      return TRUE ;

    if ( theIDeviceList(oldflptr) != NULL &&
         theIDeviceList(oldflptr) == theIDeviceList(newflptr) &&
         HqMemCmp(theICList(oldflptr), theINLen(oldflptr),
                  theICList(newflptr), theINLen(newflptr)) == 0 )
      return TRUE ;
  } else if ( oType(*cached) == OSTRING ) {
    if ( theIDeviceList(newflptr) != NULL ) {
      uint8 *filename, *devicename ;

      /* file_open stores the filename in a FILELIST as a C-string (zero
         terminated), so we can use strcmp to compare it. */
      if ( parse_filename(oString(*cached), theLen(*cached),
                          &devicename, &filename) == DEVICEANDFILE &&
           strcmp((const char *)theIDevName(theIDeviceList(newflptr)),
                  (const char *)devicename) == 0 &&
           strcmp((const char *)theICList(newflptr),
                  (const char *)filename) == 0 )
        return TRUE ;
    }
  }

  return FALSE ;
}

static sw_blob_result blobdata_file_create(OBJECT *file,
                                           blobdata_private_t **data)
{
  blobdata_private_t *state ;

  UNUSED_PARAM(OBJECT *, file) ;

  HQASSERT(oType(*file) == OFILE, "Blob data source is not a file") ;
  HQASSERT(data, "Nowhere for private data") ;

  if ( (state = (blobdata_private_t *)mm_alloc(mm_pool_temp,
                                              sizeof(blobdata_private_t),
                                              MM_ALLOC_CLASS_BLOB_DATA)) == NULL )
    return FAILURE(SW_BLOB_ERROR_MEMORY) ;

  state->dev = NULL ;
  state->fhandle = -1 ;
  state->isopen = FALSE ;
  state->length_ok = FALSE ;
  Hq32x2FromUint32(&state->length, 0) ;
  state->encrypted = FALSE ;
  state->hqxoffset = 0 ;
  state->hqxtail = 0 ;
  state->mode = 0 ;
  NAME_OBJECT(state, BLOBDATA_PRIVATE_NAME) ;
  *data = state ;

  return SW_BLOB_OK ;
}

static void blobdata_file_destroy(OBJECT *file, blobdata_private_t **data)
{
  blobdata_private_t *state ;

  UNUSED_PARAM(OBJECT *, file) ;

  HQASSERT(oType(*file) == OFILE || oType(*file) == OSTRING,
           "Blob data source is not a file or a filename") ;
  HQASSERT(data, "Nowhere for private data") ;

  state = *data ;
  VERIFY_OBJECT(state, BLOBDATA_PRIVATE_NAME) ;

  UNNAME_OBJECT(state) ;
  mm_free(mm_pool_temp, (mm_addr_t)state, sizeof(blobdata_private_t)) ;
  *data = NULL ;
}

/** Lazily open the file the first time we need to check availability of a
   buffer. */
static Bool blobdata_file_lazy(const OBJECT *file, blobdata_private_t *data)
{
  uint8 *filename ;
  DEVICELIST *dev ;
  DEVICE_FILEDESCRIPTOR fhandle ;

  HQASSERT(file, "No blob data source") ;
  HQASSERT(oType(*file) == OFILE || oType(*file) == OSTRING,
           "Blob data source is not a file or a filename") ;
  VERIFY_OBJECT(data, BLOBDATA_PRIVATE_NAME) ;

  HQASSERT(!data->isopen && data->dev == NULL && data->fhandle < 0,
           "Blob data file has been opened already") ;

  /* If the data source is an open filter, there is nothing to do. If it is a
     closed device file, re-open it. We'll use the device interface for all
     device files so that we can test if they are encrypted. */
  if ( oType(*file) == OFILE ) {
    FILELIST *flptr = oFile(*file) ;
    HQASSERT(flptr, "No filelist in file object") ;

    /* If the file is closed, but is not a real device file, we can't use it */
    if ( (dev = theIDeviceList(flptr)) == NULL ) {
      /* If the file is open, mark it so. Open file/filter uses file
         operators. */
      if ( isIOpenFileFilter(file, flptr) ) {
        if ( (data->mode & (SW_RDONLY|SW_RDWR)) != 0 && !isIInputFile(flptr) )
          return error_handler(INVALIDACCESS) ;
        if ( (data->mode & (SW_WRONLY|SW_RDWR)) != 0 && !isIOutputFile(flptr) )
          return error_handler(INVALIDACCESS) ;
        data->isopen = TRUE ;
        return TRUE ;
      }

      return error_handler(UNDEFINEDFILENAME) ;
    }

    filename = theICList(flptr) ;
  } else if ( oType(*file) == OSTRING ) {
    uint8 *devicename ;

    if ( parse_filename(oString(*file), theLen(*file),
                        &devicename, &filename) != DEVICEANDFILE )
      return error_handler(UNDEFINEDFILENAME) ;

    /* Fully specified name */
    if ( NULL == (dev = find_device(devicename)) )
      return error_handler(UNDEFINEDFILENAME) ;
  } else
    return error_handler(TYPECHECK) ;

  /* The device on which the file resides must be enabled and must be
     able to accept filenames. */
  if ( ! isDeviceEnabled(dev) || !isDeviceRelative(dev) )
    return error_handler(UNDEFINEDFILENAME) ;

  if ( (fhandle = (*theIOpenFile(dev))(dev, filename, data->mode)) < 0 )
    return device_error_handler(dev);

  /* Check hqxcrypt status every time the blob is opened. This is required to
     setup the hqxcrypt tables. Do this now, while the pointer is still at
     zero. This reads data from the start of the file. Note that the name
     hqxfont_test is a bit of a misnomer, this is the same test used for other
     HQX-encrypted files. */
  if ( hqxfont_test(fhandle, dev) ) {
    data->encrypted = FALSE;
    data->hqxoffset = 0 ;
    data->hqxtail = 0 ;
  } else {
    /* it is a hqx encrypted blob */
    data->encrypted = TRUE;
    data->hqxoffset = hqxdatastart ;  /* Initialisation vector */
    data->hqxtail = hqxdataextra ;
  }

  data->dev = dev ;
  data->fhandle = fhandle ;
  data->isopen = TRUE ;

  return TRUE ;
}

static sw_blob_result blobdata_file_open(OBJECT *file, blobdata_private_t *data, int mode)
{
  UNUSED_PARAM(OBJECT *, file) ;

  HQASSERT(file, "No blob data source") ;
  HQASSERT(oType(*file) == OFILE || oType(*file) == OSTRING,
           "Blob data source is not a file or a filename") ;
  VERIFY_OBJECT(data, BLOBDATA_PRIVATE_NAME) ;

  data->dev = NULL ;
  data->fhandle = -1 ;
  data->isopen = FALSE ;
  data->length_ok = FALSE ;
  data->mode = mode ;

  return SW_BLOB_OK ;
}

static void blobdata_file_close(OBJECT *file, blobdata_private_t *data)
{
  UNUSED_PARAM(OBJECT *, file) ;

  HQASSERT(oType(*file) == OFILE || oType(*file) == OSTRING,
           "Blob data source is not a file or a filename") ;
  VERIFY_OBJECT(data, BLOBDATA_PRIVATE_NAME) ;

  /* Either both or neither of the file handle and device should be set */
  HQASSERT((data->fhandle < 0) == (data->dev == NULL),
           "Blob file handle and device inconsistent") ;

  /* Ignore errors on closing; we got all the data we came for anyway. */
  if ( data->fhandle >= 0 ) {
    (void)(*theICloseFile(data->dev))(data->dev, data->fhandle) ;

    data->dev = NULL ;
    data->fhandle = -1 ;
  }

  data->isopen = FALSE ;
}

static uint8 *blobdata_file_available(const OBJECT *file,
                                     blobdata_private_t *data,
                                     Hq32x2 start, size_t *length)
{
  UNUSED_PARAM(const OBJECT *, file) ;
  UNUSED_PARAM(blobdata_private_t *, data) ;
  UNUSED_PARAM(Hq32x2, start) ;
  UNUSED_PARAM(size_t *, length) ;

  HQASSERT(file, "No file object") ;
  HQASSERT(oType(*file) == OFILE || oType(*file) == OSTRING,
           "Blob data source is not a file or a filename") ;
  HQASSERT(length, "No available length") ;
  VERIFY_OBJECT(data, BLOBDATA_PRIVATE_NAME) ;

  /* Must always copy data from buffers */
  return NULL ;
}

static size_t blobdata_file_read(const OBJECT *file,
                                 blobdata_private_t *data,
                                 uint8 *buffer,
                                 Hq32x2 start, size_t slength)
{
  HQASSERT(file, "No file object") ;
  HQASSERT(oType(*file) == OFILE || oType(*file) == OSTRING,
           "Blob data source is not a file or a filename") ;
  HQASSERT(buffer, "Nowhere to put data") ;
  HQASSERT(slength > 0, "No data to be read") ;
  VERIFY_OBJECT(data, BLOBDATA_PRIVATE_NAME) ;

  /* If we marked the file for lazy opening, then open it now. */
  if ( !data->isopen && !blobdata_file_lazy(file, data) )
    return FAILURE(0) ;

  if ( (data->mode & (SW_RDONLY|SW_RDWR)) == 0 )
    return FAILURE(0) ;

  /* Either both or neither of the file handle and device should be set */
  HQASSERT((data->fhandle < 0) == (data->dev == NULL),
           "Blob file handle and device inconsistent") ;

  if ( data->fhandle >= 0 ) {
    Hq32x2 here ;
    int32 length = CAST_SIZET_TO_INT32(slength) ;

    Hq32x2AddInt32(&here, &start, data->hqxoffset) ;

    if ( !(*theISeekFile(data->dev))(data->dev, data->fhandle,
                                     &here, SW_SET) ||
         (length = (*theIReadFile(data->dev))(data->dev, data->fhandle,
                                              buffer, length)) < 0 )
      return FAILURE(0) ;

    if ( data->encrypted ) {
      Hq32x2 offset ;
      int32 cryptoffset, hqxmode ;

      Hq32x2AddInt32(&offset, &start, data->hqxoffset) ;
      if ( !Hq32x2ToInt32(&offset, &cryptoffset) )
        return FAILURE(0) ;

      /* Decrypt it, saying where in the file it came from. */
      hqxmode = (data->mode & SW_FONT) ? HQX_DLDFLAG : HQX_LEADERFLAG ;
      hqx_crypt_region(buffer, cryptoffset, length, hqxmode);
    }
  } else {
    FILELIST *flptr ;
    size_t remaining ;

    HQASSERT(oType(*file) == OFILE, "Blob data source is not a file") ;

    flptr = oFile(*file) ;
    HQASSERT(isIOpenFile(flptr), "Blob data file is not open") ;

    /* Set the file position to the requested offset */
    if ( (*theIMySetFilePos(flptr))(flptr, &start) == EOF ) {
      (void)(*theIFileLastError(flptr))(flptr) ;
      return FAILURE(0) ;
    }

    remaining = slength ; /* How much is left to do? */
    do {
      int32 bytes, ch ;

      /* Get a character to prime the file buffer. EOF is not an error, it
         will break out and return the number of bytes already read. */
      if ( (ch = Getc(flptr)) == EOF )
        break ;

      *buffer++ = (uint8)ch ;
      --remaining ;

      /* Copy the rest of the file buffer in one chunk */
      HQASSERT(theICount(flptr) >= 0, "Getc did not prime FILELIST count") ;
      if ( (size_t)theICount(flptr) < remaining )
        bytes = theICount(flptr) ;
      else
        bytes = CAST_SIZET_TO_INT32(remaining) ;

      if ( bytes > 0 ) {
        HqMemCpy(buffer, theIPtr(flptr), bytes) ;
        theICount(flptr) -= bytes ;
        theIPtr(flptr) += bytes ;
        buffer += bytes ;
        remaining -= bytes ;
      }
    } while ( remaining > 0 ) ;

    slength -= remaining ;
  }

  return slength ;
}

static sw_blob_result blobdata_file_write(OBJECT *file,
                                          blobdata_private_t *data,
                                          const uint8 *buffer,
                                          Hq32x2 start, size_t slength)
{
  HQASSERT(file, "No file object") ;
  HQASSERT(oType(*file) == OFILE || oType(*file) == OSTRING,
           "Blob data source is not a file or a filename") ;
  HQASSERT(buffer, "Nowhere to put data") ;
  HQASSERT(slength > 0, "No data to be written") ;
  VERIFY_OBJECT(data, BLOBDATA_PRIVATE_NAME) ;

  /* If we marked the file for lazy opening, then open it now. */
  if ( !data->isopen && !blobdata_file_lazy(file, data) )
    return FAILURE(SW_BLOB_ERROR_EXPIRED) ;

  if ( (data->mode & (SW_WRONLY|SW_RDWR)) == 0 )
    return FAILURE(0) ;

  /* Either both or neither of the file handle and device should be set */
  HQASSERT((data->fhandle < 0) == (data->dev == NULL),
           "Blob file handle and device inconsistent") ;

  if ( data->length_ok ) {
    /* Update length if it will be invalidated by this write. */
    Hq32x2 end ;

    Hq32x2FromSize_t(&end, slength) ;
    Hq32x2Add(&end, &start, &end) ;
    if ( Hq32x2Compare(&end, &data->length) > 0 )
      data->length = end ;

    /* Now if the write fails, we have to invalidate the length_ok marker. */
  }

  if ( data->fhandle >= 0 ) {
    int32 length = CAST_SIZET_TO_INT32(slength) ;

    /* Can't re-write HQX encrypted files. */
    if ( data->encrypted ) {
      data->length_ok = FALSE ;
      return FAILURE(SW_BLOB_ERROR_ACCESS) ;
    }

    if ( !(*theISeekFile(data->dev))(data->dev, data->fhandle,
                                     &start, SW_SET) ||
         (*theIWriteFile(data->dev))(data->dev, data->fhandle,
                                     /*deconstify*/(uint8 *)buffer,
                                     length) != length ) {
      data->length_ok = FALSE ;
      return FAILURE(SW_BLOB_ERROR_WRITE) ;
    }
  } else {
    FILELIST *flptr ;

    HQASSERT(oType(*file) == OFILE, "Blob data source is not a file") ;

    flptr = oFile(*file) ;
    HQASSERT(isIOpenFile(flptr), "Blob data file is not open") ;

    /* Set the file position to the requested offset */
    if ( (*theIMySetFilePos(flptr))(flptr, &start) == EOF ) {
      (void)(*theIFileLastError(flptr))(flptr) ;
      data->length_ok = FALSE ;
      return FAILURE(SW_BLOB_ERROR_WRITE) ;
    }

    /* Writing to blobs is less frequent than reading, do it the slow way for
       now. */
    while ( slength > 0 ) {
      /* Get a character to prime the file buffer. EOF is not an error, it
         will break out and return the number of bytes already read. */
      if ( Putc(*buffer, flptr) == EOF ) {
        data->length_ok = FALSE ;
         return FAILURE(SW_BLOB_ERROR_WRITE) ;
      }

      ++buffer ;
      --slength ;
    }
  }

  return SW_BLOB_OK ;
}

static sw_blob_result blobdata_file_length(const OBJECT *file,
                                           blobdata_private_t *data,
                                           Hq32x2 *length)
{
  HQASSERT(file, "No file object") ;
  HQASSERT(oType(*file) == OFILE || oType(*file) == OSTRING,
           "Blob data source is not a file or a filename") ;
  HQASSERT(length != NULL, "No where to put length") ;
  VERIFY_OBJECT(data, BLOBDATA_PRIVATE_NAME) ;

  /* If we marked the file for lazy opening, then open it now. */
  if ( !data->isopen && !blobdata_file_lazy(file, data) )
    return FAILURE(SW_BLOB_ERROR_EXPIRED) ;

  /* If the length stored is valid, return it. */
  if ( !data->length_ok ) {
    if ( data->fhandle >= 0 ) {
      /* Getting the length of a device file is a *very* expensive
         operation. */
      if ( !(*theIBytesFile(data->dev))(data->dev, data->fhandle,
                                        &data->length, SW_BYTES_TOTAL_ABS) ) {
        (void)device_error_handler(data->dev);
        return FAILURE(SW_BLOB_ERROR_EOF) ;
      }

      if ( data->encrypted ) {
        /* Reduce length by size of initialisation and tail data */
        Hq32x2SubtractInt32(&data->length, &data->length,
                            data->hqxoffset + data->hqxtail) ;
        HQASSERT(Hq32x2Sign(&data->length) >= 0,
                 "HQX leader/trailer exceed length of file") ;
      }
    } else {
      /* File/filter bytes available is likely to fail. */
      Hq32x2 start = HQ32X2_INIT_ZERO ;
      FILELIST *flptr ;

      HQASSERT(oType(*file) == OFILE, "Blob data source is not a file") ;

      flptr = oFile(*file) ;
      HQASSERT(isIOpenFile(flptr), "Blob data file is not open") ;

      /* There isn't a good way of getting a file's length. Reset the file
         position to the start, because RSDs give the bytes available from
         the (unspecified) current position. Ignore it if this fails, in the
         vain hope that other bytes available will do something useful. */
      (void)(*theIMySetFilePos(flptr))(flptr, &start) ;

      if ( (*theIMyBytesAvailable(flptr))(flptr, &data->length) == EOF )
        return FAILURE(SW_BLOB_ERROR_EOF) ;

      HQASSERT(!data->encrypted, "FILELIST should not be hqx encrypted") ;
    }
  }

  data->length_ok = TRUE ;
  *length = data->length ;

  return SW_BLOB_OK ;
}

/** Preserve the blob data if possible by storing the filename in a global
   string. */
static OBJECT *blobdata_file_restored(const OBJECT *file,
                                      blobdata_private_t *data,
                                      int32 savelevel)
{
  DEVICELIST *dev ;
  FILELIST *flptr ;

  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(file, "No blob data source") ;
  VERIFY_OBJECT(data, BLOBDATA_PRIVATE_NAME) ;

  if ( NUMBERSAVES(savelevel) > MAXGLOBALSAVELEVEL &&
       oType(*file) == OFILE &&
       (flptr = oFile(*file)) != NULL &&
       (dev = theIDeviceList(flptr)) != NULL ) {
    int32 devlen = strlen_int32((char *)theIDevName(dev)) ;
    int32 length = devlen + theINLen(flptr) + 2 ;
    OBJECT *string ;

    /* We're in a local save levels. We can re-use the file if it is a real
       device file. Store the full filename (including device name) in
       global memory. */
    if ( (string = get_gomemory(1)) != NULL ) {
      uint8 *smem ;
      if ( (smem = get_gsmemory(length)) != NULL ) {
        smem[0] = '%' ;
        HqMemCpy(smem + 1, theIDevName(dev), devlen) ;
        smem[devlen + 1] = '%' ;
        if ( theINLen(flptr) )
          HqMemCpy(smem + devlen + 2, theICList(flptr), theINLen(flptr)) ;

        oString(*string) = smem ;
        theTags(*string) = OSTRING | LITERAL | READ_ONLY ;
        theLen(*string) = CAST_UNSIGNED_TO_UINT16(length);
        SETGLOBJECTTO(*string, TRUE) ;

        return string ;
      }

      /* Got object memory, but no string memory. Forget the global object,
         it will get restored soon enough. */
      theTags(*string) = ONULL ;
    }
  }

  /* Forget about it, we can't save the data any more */
  return NULL ;
}

static uint8 blobdata_file_protection(const OBJECT *file,
                                      blobdata_private_t *data)
{
  VERIFY_OBJECT(data, BLOBDATA_PRIVATE_NAME) ;

  /* If we marked the file for lazy opening, then open it now. */
  if ( !data->isopen && !blobdata_file_lazy(file, data) )
    return FAILURE(PROTECTED_NONE) ;

  if ( data->encrypted )
    return PROTECTED_HQXRUN ;

  return PROTECTED_NONE ;
}

const blobdata_methods_t blobdata_file_methods = {
  blobdata_file_same,
  blobdata_file_create,
  blobdata_file_destroy,
  blobdata_file_open,
  blobdata_file_close,
  blobdata_file_available,
  blobdata_file_read,
  blobdata_file_write,
  blobdata_file_length,
  blobdata_file_restored,
  blobdata_file_protection,
} ;

/*
Log stripped */
