/** \file
 * \ingroup blob
 *
 * $HopeName: COREblob!src:bdstring.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Blob data cache methods to get data from a string source. No data copying
 * is required.
 */


#define OBJECT_MACROS_ONLY

#include "core.h"
#include "swerrors.h"
#include "hqmemcpy.h"
#include "objects.h"
#include "blobdata.h"
#include "paths.h"

static Bool blobdata_string_same(const OBJECT *newo, const OBJECT *cached)
{
  HQASSERT(newo && cached, "No objects to compare") ;
  HQASSERT(oType(*newo) == OSTRING && oType(*cached) == OSTRING,
           "Blob data source objects are not strings") ;

  /* Object identity has already been checked. See if the two string objects
     refer to the same data. */
  return (theLen(*newo) == theLen(*cached) &&
          oString(*newo) == oString(*cached)) ;
}

static sw_blob_result blobdata_string_create(OBJECT *string,
                                             blobdata_private_t **data)
{
  UNUSED_PARAM(OBJECT *, string) ;
  UNUSED_PARAM(blobdata_private_t **, data) ;

  HQASSERT(oType(*string) == OSTRING, "Blob data source is not a string") ;

  /* The string methods retain no private data. */

  return SW_BLOB_OK ;
}

static void blobdata_string_destroy(OBJECT *string,
                                    blobdata_private_t **data)
{
  UNUSED_PARAM(OBJECT *, string) ;
  UNUSED_PARAM(blobdata_private_t **, data) ;

  HQASSERT(oType(*string) == OSTRING, "Blob data source is not a string") ;
}

static sw_blob_result blobdata_string_open(OBJECT *string,
                                           blobdata_private_t *data,
                                           int mode)
{
  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(oType(*string) == OSTRING, "Blob data source is not a string") ;

  if ( (mode & (SW_RDONLY|SW_RDWR)) != 0 && !oCanRead(*string) )
    return FAILURE(SW_BLOB_ERROR_ACCESS) ;

  if ( (mode & (SW_WRONLY|SW_RDWR)) != 0 && !oCanWrite(*string) )
    return FAILURE(SW_BLOB_ERROR_ACCESS) ;

  return SW_BLOB_OK ;
}

static void blobdata_string_close(OBJECT *string,
                                 blobdata_private_t *data)
{
  UNUSED_PARAM(OBJECT *, string) ;
  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(oType(*string) == OSTRING, "Blob data source is not a string") ;
}

static uint8 *blobdata_string_available(const OBJECT *string,
                                        blobdata_private_t *data,
                                        Hq32x2 start, size_t *length)
{
  size_t offset ;

  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(string, "No string object") ;
  HQASSERT(length, "No available length") ;

  HQASSERT(oType(*string) == OSTRING, "Blob data source is not a string") ;

  if ( !oCanRead(*string) ||
       !Hq32x2ToSize_t(&start, &offset) ||
       offset >= theLen(*string) )
    return NULL ;

  *length = theLen(*string) - offset ;

  return oString(*string) + offset ;
}

static size_t blobdata_string_read(const OBJECT *string,
                                   blobdata_private_t *data,
                                   uint8 *buffer,
                                   Hq32x2 start, size_t length)
{
  size_t offset ;

  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(string, "No string object") ;
  HQASSERT(buffer, "Nowhere to put data") ;
  HQASSERT(length > 0, "No data to be read") ;

  HQASSERT(oType(*string) == OSTRING, "Blob data source is not a string") ;
  HQASSERT(oCanRead(*string), "Cannot read blob source string") ;

  if ( !oCanRead(*string) ||
       !Hq32x2ToSize_t(&start, &offset) ||
       offset >= theLen(*string) )
    return FAILURE(0) ;

  if ( offset + length > theLen(*string) )
    length = theLen(*string) - offset ;

  HqMemCpy(buffer, oString(*string) + offset, length) ;

  return length ;
}

static sw_blob_result blobdata_string_write(OBJECT *string,
                                            blobdata_private_t *data,
                                            const uint8 *buffer,
                                            Hq32x2 start, size_t length)
{
  size_t offset ;

  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(string, "No string object") ;
  HQASSERT(oType(*string) == OSTRING, "Blob data source is not a string") ;

  if ( !oCanWrite(*string) )
    return FAILURE(SW_BLOB_ERROR_ACCESS) ;

  if ( !Hq32x2ToSize_t(&start, &offset) ||
       offset + length > theLen(*string) )
    return FAILURE(SW_BLOB_ERROR_EOF) ;

  HqMemCpy(oString(*string) + offset, buffer, length) ;

  return SW_BLOB_OK ;
}

static sw_blob_result blobdata_string_length(const OBJECT *string,
                                             blobdata_private_t *data,
                                             Hq32x2 *length)
{
  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(string, "No string object") ;
  HQASSERT(length, "Nowhere to return length") ;

  HQASSERT(oType(*string) == OSTRING, "Blob data source is not a string") ;

  Hq32x2FromUint32(length, theLen(*string)) ;

  return SW_BLOB_OK ;
}

static OBJECT *blobdata_string_restored(const OBJECT *string,
                                        blobdata_private_t *data,
                                        int32 slevel)
{
  UNUSED_PARAM(const OBJECT *, string) ;
  UNUSED_PARAM(blobdata_private_t *, data) ;
  UNUSED_PARAM(int32, slevel) ;

  HQASSERT(oType(*string) == OSTRING, "Blob data source is not a string") ;

  /* Always lose access to this string if restored */
  return NULL ;
}

static uint8 blobdata_string_protection(const OBJECT *string,
                                        blobdata_private_t *data)
{
  UNUSED_PARAM(const OBJECT *, string) ;
  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(oType(*string) == OSTRING, "Blob data source is not a string") ;

  return PROTECTED_NONE ;
}

const blobdata_methods_t blobdata_string_methods = {
  blobdata_string_same,
  blobdata_string_create,
  blobdata_string_destroy,
  blobdata_string_open,
  blobdata_string_close,
  blobdata_string_available,
  blobdata_string_read,
  blobdata_string_write,
  blobdata_string_length,
  blobdata_string_restored,
  blobdata_string_protection,
} ;

/*
Log stripped */
