/** \file
 * \ingroup blob
 *
 * $HopeName: COREblob!src:bdlstring.c(EBDSDK_P.1) $
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

static Bool blobdata_longstring_same(const OBJECT *newo, const OBJECT *cached)
{
  HQASSERT(newo && cached, "No objects to compare") ;
  HQASSERT(oType(*newo) == OLONGSTRING && oType(*cached) == OLONGSTRING,
           "Blob data source objects are not strings") ;

  /* Object identity has already been checked. See if the two string objects
     refer to the same data. */
  return (theLSLen(*oLongStr(*newo)) == theLSLen(*oLongStr(*cached)) &&
          theLSCList(*oLongStr(*newo)) == theLSCList(*oLongStr(*cached))) ;
}

static sw_blob_result blobdata_longstring_create(OBJECT *string,
                                                 blobdata_private_t **data)
{
  UNUSED_PARAM(OBJECT *, string) ;
  UNUSED_PARAM(blobdata_private_t **, data) ;

  HQASSERT(oType(*string) == OLONGSTRING, "Blob data source is not a string") ;

  /* The string methods retain no private data. */

  return SW_BLOB_OK ;
}

static void blobdata_longstring_destroy(OBJECT *string,
                                        blobdata_private_t **data)
{
  UNUSED_PARAM(OBJECT *, string) ;
  UNUSED_PARAM(blobdata_private_t **, data) ;

  HQASSERT(oType(*string) == OLONGSTRING, "Blob data source is not a string") ;
}

static sw_blob_result blobdata_longstring_open(OBJECT *string,
                                               blobdata_private_t *data,
                                               int mode)
{
  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(oType(*string) == OLONGSTRING, "Blob data source is not a string") ;

  if ( (mode & (SW_RDONLY|SW_RDWR)) != 0 && !oCanRead(*string) )
    return FAILURE(SW_BLOB_ERROR_ACCESS) ;

  if ( (mode & (SW_WRONLY|SW_RDWR)) != 0 && !oCanWrite(*string) )
    return FAILURE(SW_BLOB_ERROR_ACCESS) ;

  return SW_BLOB_OK ;
}

static void blobdata_longstring_close(OBJECT *string,
                                      blobdata_private_t *data)
{
  UNUSED_PARAM(OBJECT *, string) ;
  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(oType(*string) == OLONGSTRING, "Blob data source is not a string") ;
}

static uint8 *blobdata_longstring_available(const OBJECT *string,
                                            blobdata_private_t *data,
                                            Hq32x2 start, size_t *length)
{
  size_t offset ;

  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(string, "No string object") ;
  HQASSERT(length, "No available length") ;

  HQASSERT(oType(*string) == OLONGSTRING, "Blob data source is not a string") ;

  if ( !oCanRead(*string) ||
       !Hq32x2ToSize_t(&start, &offset) ||
       offset >= CAST_SIGNED_TO_UINT32(theLSLen(*oLongStr(*string))) )
    return NULL ;

  *length = theLSLen(*oLongStr(*string)) - offset ;

  return theLSCList(*oLongStr(*string)) + offset ;
}

static size_t blobdata_longstring_read(const OBJECT *string,
                                       blobdata_private_t *data,
                                       uint8 *buffer,
                                       Hq32x2 start, size_t length)
{
  size_t offset ;

  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(string, "No string object") ;
  HQASSERT(buffer, "Nowhere to put data") ;
  HQASSERT(length > 0, "No data to be read") ;

  HQASSERT(oType(*string) == OLONGSTRING, "Blob data source is not a string") ;
  HQASSERT(oCanRead(*string), "Cannot read blob source string") ;

  if ( !oCanRead(*string) ||
       !Hq32x2ToSize_t(&start, &offset) ||
       offset >= CAST_SIGNED_TO_UINT32(theLSLen(*oLongStr(*string))) )
    return FAILURE(0) ;

  if ( offset + length > CAST_SIGNED_TO_UINT32(theLSLen(*oLongStr(*string))) )
    length = theLSLen(*oLongStr(*string)) - offset ;

  HqMemCpy(buffer, theLSCList(*oLongStr(*string)) + offset, length) ;

  return length ;
}

static sw_blob_result blobdata_longstring_write(OBJECT *string,
                                                blobdata_private_t *data,
                                                const uint8 *buffer,
                                                Hq32x2 start, size_t length)
{
  size_t offset ;

  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(string, "No string object") ;
  HQASSERT(oType(*string) == OLONGSTRING, "Blob data source is not a string") ;

  if ( !oCanWrite(*string) )
    return FAILURE(SW_BLOB_ERROR_ACCESS) ;

  if ( !Hq32x2ToSize_t(&start, &offset) ||
       offset + length > CAST_SIGNED_TO_UINT32(theLSLen(*oLongStr(*string))) )
    return FAILURE(SW_BLOB_ERROR_EOF) ;

  HqMemCpy(theLSCList(*oLongStr(*string)) + offset, buffer, length) ;

  return SW_BLOB_OK ;
}

static sw_blob_result blobdata_longstring_length(const OBJECT *string,
                                                 blobdata_private_t *data,
                                                 Hq32x2 *length)
{
  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(string, "No string object") ;
  HQASSERT(length, "Nowhere to return length") ;

  HQASSERT(oType(*string) == OLONGSTRING, "Blob data source is not a string") ;

  Hq32x2FromInt32(length, theLSLen(*oLongStr(*string))) ;

  return SW_BLOB_OK ;
}

static OBJECT *blobdata_longstring_restored(const OBJECT *string,
                                            blobdata_private_t *data,
                                            int32 slevel)
{
  UNUSED_PARAM(const OBJECT *, string) ;
  UNUSED_PARAM(blobdata_private_t *, data) ;
  UNUSED_PARAM(int32, slevel) ;

  HQASSERT(oType(*string) == OLONGSTRING, "Blob data source is not a string") ;

  /* Always lose access to this string if restored */
  return NULL ;
}

static uint8 blobdata_longstring_protection(const OBJECT *string,
                                            blobdata_private_t *data)
{
  UNUSED_PARAM(const OBJECT *, string) ;
  UNUSED_PARAM(blobdata_private_t *, data) ;

  HQASSERT(oType(*string) == OLONGSTRING, "Blob data source is not a string") ;

  return PROTECTED_NONE ;
}

const blobdata_methods_t blobdata_longstring_methods = {
  blobdata_longstring_same,
  blobdata_longstring_create,
  blobdata_longstring_destroy,
  blobdata_longstring_open,
  blobdata_longstring_close,
  blobdata_longstring_available,
  blobdata_longstring_read,
  blobdata_longstring_write,
  blobdata_longstring_length,
  blobdata_longstring_restored,
  blobdata_longstring_protection,
} ;

/*
Log stripped */
