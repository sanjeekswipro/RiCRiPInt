/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!src:fdsfnts.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Blob data cache methods to get data from TrueType "sfnts" array source.
 */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "swerrors.h"
#include "hqmemcpy.h"
#include "objects.h"
#include "fontdata.h"
#include "paths.h"

static Bool fontdata_sfnts_same(const OBJECT *newo, const OBJECT *cached)
{
  HQASSERT(newo && cached, "No objects to compare") ;
  HQASSERT((oType(*newo) == OARRAY || oType(*newo) == OPACKEDARRAY) &&
           (oType(*cached) == OARRAY || oType(*cached) == OPACKEDARRAY),
           "Font data source objects are not arrays") ;

  /* Object identity has already been checked. See if the two array objects
     refer to the same data. */
  return (theLen(*newo) == theLen(*cached) &&
          oArray(*newo) == oArray(*cached)) ;
}

static sw_blob_result fontdata_sfnts_create(OBJECT *sfnts,
                                            blobdata_private_t **data)
{
  UNUSED_PARAM(OBJECT *, sfnts) ;
  UNUSED_PARAM(blobdata_private_t **, data) ;

  HQASSERT(oType(*sfnts) == OARRAY || oType(*sfnts) == OPACKEDARRAY,
           "Font data source is not an array") ;

  /* The sfnts array methods retain no private data. */

  return SW_BLOB_OK ;
}

static void fontdata_sfnts_destroy(OBJECT *sfnts,
                                   blobdata_private_t **data)
{
  UNUSED_PARAM(OBJECT *, sfnts) ;
  UNUSED_PARAM(blobdata_private_t **, data) ;

  HQASSERT(oType(*sfnts) == OARRAY || oType(*sfnts) == OPACKEDARRAY,
           "Font data source is not an array") ;
}

static sw_blob_result fontdata_sfnts_open(OBJECT *sfnts,
                                          blobdata_private_t *data,
                                          int mode)
{
  UNUSED_PARAM(OBJECT *, sfnts) ;
  UNUSED_PARAM(blobdata_private_t *, data) ;
  UNUSED_PARAM(int, mode) ;

  HQASSERT(oType(*sfnts) == OARRAY || oType(*sfnts) == OPACKEDARRAY,
           "Font data source is not an array") ;

  return SW_BLOB_OK ;
}

static void fontdata_sfnts_close(OBJECT *sfnts,
                                 blobdata_private_t *data)
{
  UNUSED_PARAM(OBJECT *, sfnts) ;
  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(oType(*sfnts) == OARRAY || oType(*sfnts) == OPACKEDARRAY,
           "Font data source is not an array") ;
}

static uint8 *fontdata_sfnts_available(const OBJECT *sfnts,
                                       blobdata_private_t *data,
                                       Hq32x2 start, size_t *length)
{
  int32 sfntlen ;
  uint32 offset ;

  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(sfnts, "No array object") ;
  HQASSERT(length, "No available length") ;

  HQASSERT(oType(*sfnts) == OARRAY || oType(*sfnts) == OPACKEDARRAY,
           "Font data source is not an array") ;

  if ( !oCanRead(*sfnts) ||
       !Hq32x2ToUint32(&start, &offset) )
    return NULL ;

  /* sfnts is an array of strings. The Type 42 spec says that the length of
     each string must be odd, and the last byte of each string is ignored
     (TN5012, 31 Jul 98, p.8). We don't enforce the constraint that strings
     must not cross TrueType table boundaries, it isn't necessary because the
     font data cache will take care of merging spanning requests. */
  sfntlen = theLen(*sfnts) ;
  sfnts = oArray(*sfnts) ;

  while ( --sfntlen >= 0 ) {
    uint32 srclen ;

    if ( oType(*sfnts) != OSTRING || theLen(*sfnts) < 1 )
      return NULL ;

    srclen = theLen(*sfnts) & ~1 ; /* ignore odd last byte if present [28823]*/

    if ( offset < srclen ) { /* The frame starts in this string */
      *length = srclen - offset ;
      return oString(*sfnts) + offset ;
    }

    offset -= srclen ;
    ++sfnts ;
  }

  /* Can't ask for a frame that starts at the end of the data */
  return NULL ;
}

static size_t fontdata_sfnts_read(const OBJECT *sfnts,
                                  blobdata_private_t *data,
                                  uint8 *buffer,
                                  Hq32x2 start, size_t length)
{
  int32 sfntlen ;
  size_t copied = 0 ;
  size_t offset ;

  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(sfnts, "No array object") ;
  HQASSERT(buffer, "Nowhere to put data") ;
  HQASSERT(length > 0, "No data to be read") ;

  HQASSERT(oType(*sfnts) == OARRAY || oType(*sfnts) == OPACKEDARRAY,
           "Font data source is not an array") ;

  if ( !oCanRead(*sfnts) ||
       !Hq32x2ToSize_t(&start, &offset) )
    return FAILURE(0) ;

  /* sfnts is an array of strings. The Type 42 spec says that the length of
     each string must be odd, and the last byte of each string is ignored
     (TN5012, 31 Jul 98, p.8). We don't enforce the constraint that strings
     must not cross TrueType table boundaries, it isn't necessary because the
     font data cache will take care of merging spanning requests. */
  sfntlen = theLen(*sfnts) ;
  sfnts = oArray(*sfnts) ;

  while ( --sfntlen >= 0 ) {
    size_t srclen ;

    if ( oType(*sfnts) != OSTRING || theLen(*sfnts) < 1 )
      return copied ;

    srclen = theLen(*sfnts) & ~1 ; /* ignore odd last byte if present [28823]*/

    if ( offset < srclen ) { /* The frame starts in this string */
      size_t copylen = srclen - offset ;

      if ( !oCanRead(*sfnts) )
        return FAILURE(copied) ;

      if ( copylen > length )
        copylen = length ;

      HqMemCpy(buffer, oString(*sfnts) + offset, copylen) ;

      buffer += copylen ;
      copied += copylen ;
      length -= copylen ;

      if ( length == 0 )
        break ;

      offset = srclen ; /* Next iteration copies from start of string */
    }

    offset -= srclen ;
    ++sfnts ;
  }

  return copied ;
}

static sw_blob_result fontdata_sfnts_write(OBJECT *sfnts,
                                           blobdata_private_t *data,
                                           const uint8 *buffer,
                                           Hq32x2 start, size_t length)
{
  int32 sfntlen ;
  size_t offset ;

  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(sfnts, "No array object") ;
  HQASSERT(buffer, "Nowhere to get data from") ;
  HQASSERT(length > 0, "No data to be written") ;

  HQASSERT(oType(*sfnts) == OARRAY || oType(*sfnts) == OPACKEDARRAY,
           "Font data source is not an array") ;

  if ( !oCanRead(*sfnts) )
    return FAILURE(SW_BLOB_ERROR_ACCESS) ;

  if ( !Hq32x2ToSize_t(&start, &offset) )
    return FAILURE(SW_BLOB_ERROR_INVALID) ;

  /* sfnts is an array of strings. The Type 42 spec says that the length of
     each string must be odd, and the last byte of each string is ignored
     (TN5012, 31 Jul 98, p.8). We don't enforce the constraint that strings
     must not cross TrueType table boundaries, it isn't necessary because the
     font data cache will take care of merging spanning requests. */
  sfntlen = theLen(*sfnts) ;
  sfnts = oArray(*sfnts) ;

  while ( --sfntlen >= 0 ) {
    size_t srclen ;

    if ( oType(*sfnts) != OSTRING || theLen(*sfnts) < 1 )
      return FAILURE(SW_BLOB_ERROR_INVALID) ;

    srclen = theLen(*sfnts) & ~1 ; /* ignore odd last byte if present [28823]*/

    if ( offset < srclen ) { /* The frame starts in this string */
      size_t copylen = srclen - offset ;

      if ( !oCanWrite(*sfnts) )
        return FAILURE(SW_BLOB_ERROR_ACCESS) ;

      if ( copylen > length )
        copylen = length ;

      HqMemCpy(oString(*sfnts) + offset, buffer, copylen) ;

      buffer += copylen ;
      length -= copylen ;

      if ( length == 0 )
        return SW_BLOB_OK ;

      offset = srclen ; /* Next iteration copies from start of string */
    }

    offset -= srclen ;
    ++sfnts ;
  }

  return FAILURE(SW_BLOB_ERROR_EOF) ;
}

static sw_blob_result fontdata_sfnts_length(const OBJECT *sfnts,
                                            blobdata_private_t *data,
                                            Hq32x2 *length)
{
  int32 arraylen ;

  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(sfnts, "No array object") ;
  HQASSERT(length, "Nowhere to put length") ;

  HQASSERT(oType(*sfnts) == OARRAY || oType(*sfnts) == OPACKEDARRAY,
           "Blob data source is not an array") ;

  if ( !oCanRead(*sfnts) )
    return FAILURE(SW_BLOB_ERROR_ACCESS) ;

  Hq32x2FromUint32(length, 0) ;

  /* array is an array of strings. The full length of each string is used,
     unlike the TrueType sfnts array of strings. We do not enforce the
     constraint that strings should not be split across offset boundaries,
     because the read method can merge segments. */
  arraylen = theLen(*sfnts) ;
  sfnts = oArray(*sfnts) ;

  while ( --arraylen >= 0 ) {
    if ( oType(*sfnts) != OSTRING )
      break ;

    Hq32x2AddUint32(length, length, theLen(*sfnts) & ~1) ;

    ++sfnts ;
  }

  return SW_BLOB_OK ;
}

static OBJECT *fontdata_sfnts_restored(const OBJECT *sfnts,
                                       blobdata_private_t *data,
                                       int32 slevel)
{
  UNUSED_PARAM(const OBJECT *, sfnts) ;
  UNUSED_PARAM(blobdata_private_t *, data) ;
  UNUSED_PARAM(int32, slevel) ;

  HQASSERT(oType(*sfnts) == OARRAY || oType(*sfnts) == OPACKEDARRAY,
           "Font data source is not an array") ;

  /* Always lose access to this array if restored */
  return NULL ;
}

static uint8 fontdata_sfnts_protection(const OBJECT *sfnts,
                                       blobdata_private_t *data)
{
  UNUSED_PARAM(const OBJECT *, sfnts) ;
  UNUSED_PARAM(blobdata_private_t *, data) ;

  return PROTECTED_NONE ;
}

const blobdata_methods_t blobdata_sfnts_methods = {
  fontdata_sfnts_same,
  fontdata_sfnts_create,
  fontdata_sfnts_destroy,
  fontdata_sfnts_open,
  fontdata_sfnts_close,
  fontdata_sfnts_available,
  fontdata_sfnts_read,
  fontdata_sfnts_write,
  fontdata_sfnts_length,
  fontdata_sfnts_restored,
  fontdata_sfnts_protection,
} ;

/*
Log stripped */
