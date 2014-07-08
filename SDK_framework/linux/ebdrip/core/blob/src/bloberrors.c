/** \file
 * \ingroup blob
 *
 * $HopeName: COREblob!src:bloberrors.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Translation between sw_blob_api errors and core RIP errors.
 */

#include "core.h"
#include "swerrors.h"
#include "swblobapi.h"

int32 error_from_sw_blob_result(sw_blob_result result)
{
  switch ( result ) {
  case SW_BLOB_OK:
    HQFAIL("Should not call this function on success") ;
    return UNREGISTERED;
  case SW_BLOB_ERROR_EOF:
    return IOERROR ;
  case SW_BLOB_ERROR_MEMORY:
    return VMERROR ;
  case SW_BLOB_ERROR_INVALID:
    return UNDEFINEDRESULT ;
  case SW_BLOB_ERROR_ACCESS:
    return INVALIDACCESS ;
  case SW_BLOB_ERROR_EXPIRED:
    return UNDEFINED ;
  default:
    HQFAIL("Unhandled blob API error") ;
    return UNREGISTERED ;
  }
}

/* Log stripped */
