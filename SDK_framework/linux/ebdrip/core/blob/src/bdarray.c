/** \file
 * \ingroup blob
 *
 * $HopeName: COREblob!src:bdarray.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Blob data cache methods to get data from array of string. This is very
 * similar to the TrueType "sfnts" array source, except that sfnts requires
 * each string to be padded with a null byte at the end.
 */


#define OBJECT_MACROS_ONLY

#include "core.h"
#include "swerrors.h"
#include "hqmemcpy.h"
#include "objects.h"
#include "blobdata.h"
#include "paths.h"

static Bool blobdata_array_same(const OBJECT *newo, const OBJECT *cached)
{
  HQASSERT(newo && cached, "No objects to compare") ;
  HQASSERT((oType(*newo) == OARRAY || oType(*newo) == OPACKEDARRAY) &&
           (oType(*cached) == OARRAY || oType(*cached) == OPACKEDARRAY),
           "Blob data source objects are not arrays") ;

  /* Object identity has already been checked. See if the two array objects
     refer to the same data. */
  return (theLen(*newo) == theLen(*cached) &&
          oArray(*newo) == oArray(*cached)) ;
}

static sw_blob_result blobdata_array_create(OBJECT *array,
                                            blobdata_private_t **data)
{
  UNUSED_PARAM(OBJECT *, array) ;
  UNUSED_PARAM(blobdata_private_t **, data) ;

  HQASSERT(oType(*array) == OARRAY || oType(*array) == OPACKEDARRAY,
           "Blob data source is not an array") ;

  /* The array array methods retain no private data. */

  return SW_BLOB_OK ;
}

static void blobdata_array_destroy(OBJECT *array,
                                   blobdata_private_t **data)
{
  UNUSED_PARAM(OBJECT *, array) ;
  UNUSED_PARAM(blobdata_private_t **, data) ;

  HQASSERT(oType(*array) == OARRAY || oType(*array) == OPACKEDARRAY,
           "Blob data source is not an array") ;
}

static sw_blob_result blobdata_array_open(OBJECT *array,
                                          blobdata_private_t *data,
                                          int mode)
{
  UNUSED_PARAM(OBJECT *, array) ;
  UNUSED_PARAM(blobdata_private_t *, data) ;
  UNUSED_PARAM(int, mode) ;

  HQASSERT(oType(*array) == OARRAY || oType(*array) == OPACKEDARRAY,
           "Blob data source is not an array") ;

  /* It's the contents of the array that matter, not the array itself, so
     don't bother access checking the array. */

  return SW_BLOB_OK ;
}

static void blobdata_array_close(OBJECT *array,
                                 blobdata_private_t *data)
{
  UNUSED_PARAM(OBJECT *, array) ;
  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(oType(*array) == OARRAY || oType(*array) == OPACKEDARRAY,
           "Blob data source is not an array") ;
}

static uint8 *blobdata_array_available(const OBJECT *array,
                                       blobdata_private_t *data,
                                       Hq32x2 start, size_t *length)
{
  int32 arraylen ;
  uint32 offset ;

  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(array, "No array object") ;
  HQASSERT(length, "No available length") ;

  HQASSERT(oType(*array) == OARRAY || oType(*array) == OPACKEDARRAY,
           "Blob data source is not an array") ;

  if ( !oCanRead(*array) ||
       !Hq32x2ToUint32(&start, &offset) )
    return NULL ;

  /* array is an array of strings. The full length of each string is used,
     unlike the TrueType sfnts array of strings. We do not enforce the
     constraint that strings should not be split across offset boundaries,
     because the read method can merge segments. */
  arraylen = theLen(*array) ;
  array = oArray(*array) ;

  while ( --arraylen >= 0 ) {
    uint32 srclen ;

    if ( oType(*array) != OSTRING )
      return NULL ;

    srclen = theLen(*array) ;

    if ( offset < srclen ) { /* The frame starts in this string */
      *length = srclen - offset ;
      return oString(*array) + offset ;
    }

    offset -= srclen ;
    ++array ;
  }

  /* Can't ask for a frame that starts at the end of the data */
  return NULL ;
}

static size_t blobdata_array_read(const OBJECT *array,
                                  blobdata_private_t *data,
                                  uint8 *dest,
                                  Hq32x2 start, size_t length)
{
  int32 arraylen ;
  size_t copied = 0 ;
  size_t offset ;
  uint8 *buffer = dest ;

  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(array, "No array object") ;
  HQASSERT(buffer, "Nowhere to put data") ;
  HQASSERT(length > 0, "No data to be read") ;

  HQASSERT(oType(*array) == OARRAY || oType(*array) == OPACKEDARRAY,
           "Blob data source is not an array") ;

  if ( !oCanRead(*array) ||
       !Hq32x2ToSize_t(&start, &offset) )
    return FAILURE(0) ;

  /* array is an array of strings. The full length of each string is used,
     unlike the TrueType sfnts array of strings. We do not enforce the
     constraint that strings should not be split across offset boundaries,
     because the read method can merge segments. */
  arraylen = theLen(*array) ;
  array = oArray(*array) ;

  while ( --arraylen >= 0 ) {
    size_t srclen ;

    if ( oType(*array) != OSTRING )
      return FAILURE(copied) ;

    srclen = theLen(*array) ;

    if ( offset < srclen ) { /* The frame starts in this string */
      size_t copylen = srclen - offset ;

      if ( !oCanRead(*array) )
        return FAILURE(copied) ;

      if ( copylen > length )
        copylen = length ;

      HqMemCpy(buffer, oString(*array) + offset, copylen) ;

      buffer += copylen ;
      copied += copylen ;
      length -= copylen ;

      if ( length == 0 )
        break ;

      offset = srclen ; /* Next iteration copies from start of string */
    }

    offset -= srclen ;
    ++array ;
  }

  return copied ;
}

static sw_blob_result blobdata_array_write(OBJECT *array,
                                           blobdata_private_t *data,
                                           const uint8 *buffer,
                                           Hq32x2 start, size_t length)
{
  int32 arraylen ;
  size_t offset ;

  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(array, "No array object") ;
  HQASSERT(buffer, "Nowhere to get data from") ;
  HQASSERT(length > 0, "No data to be written") ;

  HQASSERT(oType(*array) == OARRAY || oType(*array) == OPACKEDARRAY,
           "Blob data source is not an array") ;

  if ( !oCanRead(*array) )
    return FAILURE(SW_BLOB_ERROR_ACCESS) ;

  if ( !Hq32x2ToSize_t(&start, &offset) )
    return FAILURE(SW_BLOB_ERROR_INVALID) ;

  /* array is an array of strings. The full length of each string is used,
     unlike the TrueType sfnts array of strings. We do not enforce the
     constraint that strings should not be split across offset boundaries,
     because the read method can merge segments. */
  arraylen = theLen(*array) ;
  array = oArray(*array) ;

  while ( --arraylen >= 0 ) {
    size_t srclen ;

    if ( oType(*array) != OSTRING )
      return FAILURE(SW_BLOB_ERROR_EOF) ;

    srclen = theLen(*array) ;

    if ( offset < srclen ) { /* The frame starts in this string */
      size_t copylen = srclen - offset ;

      if ( !oCanWrite(*array) )
        return FAILURE(SW_BLOB_ERROR_ACCESS) ;

      if ( copylen > length )
        copylen = length ;

      HqMemCpy(oString(*array) + offset, buffer, copylen) ;

      buffer += copylen ;
      length -= copylen ;

      if ( length == 0 )
        return SW_BLOB_OK ;

      offset = srclen ; /* Next iteration copies from start of string */
    }

    offset -= srclen ;
    ++array ;
  }

  return FAILURE(SW_BLOB_ERROR_EOF) ;
}

static sw_blob_result blobdata_array_length(const OBJECT *array,
                                            blobdata_private_t *data,
                                            Hq32x2 *length)
{
  int32 arraylen ;

  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(array, "No array object") ;
  HQASSERT(length, "Nowhere to put length") ;

  HQASSERT(oType(*array) == OARRAY || oType(*array) == OPACKEDARRAY,
           "Blob data source is not an array") ;

  if ( !oCanRead(*array) )
    return FAILURE(SW_BLOB_ERROR_ACCESS) ;

  Hq32x2FromUint32(length, 0) ;

  /* array is an array of strings. The full length of each string is used,
     unlike the TrueType sfnts array of strings. We do not enforce the
     constraint that strings should not be split across offset boundaries,
     because the read method can merge segments. */
  arraylen = theLen(*array) ;
  array = oArray(*array) ;

  while ( --arraylen >= 0 ) {
    if ( oType(*array) != OSTRING )
      break ;

    Hq32x2AddUint32(length, length, theLen(*array)) ;

    ++array ;
  }

  return SW_BLOB_OK ;
}

static OBJECT *blobdata_array_restored(const OBJECT *array,
                                       blobdata_private_t *data,
                                       int32 slevel)
{
  UNUSED_PARAM(const OBJECT *, array) ;
  UNUSED_PARAM(blobdata_private_t *, data) ;
  UNUSED_PARAM(int32, slevel) ;

  HQASSERT(oType(*array) == OARRAY || oType(*array) == OPACKEDARRAY,
           "Blob data source is not an array") ;

  /* Always lose access to this array if restored */
  return NULL ;
}

static uint8 blobdata_array_protection(const OBJECT *array,
                                       blobdata_private_t *data)
{
  UNUSED_PARAM(const OBJECT *, array) ;
  UNUSED_PARAM(blobdata_private_t *, data) ;

  return PROTECTED_NONE ;
}

const blobdata_methods_t blobdata_array_methods = {
  blobdata_array_same,
  blobdata_array_create,
  blobdata_array_destroy,
  blobdata_array_open,
  blobdata_array_close,
  blobdata_array_available,
  blobdata_array_read,
  blobdata_array_write,
  blobdata_array_length,
  blobdata_array_restored,
  blobdata_array_protection,
} ;

/*
Log stripped */
