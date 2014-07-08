/** \file
 * \ingroup jpeg
 *
 * $HopeName: COREjpeg!src:dct_exif_stub.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation functions for DCT (JPEG) filter.
 */

#include "core.h"
#include "fileio.h"
#include "dct.h"
#include "dctimpl.h"
#include "gu_dct.h"

#define EXIF_LEN 5

Bool decode_APP1( FILELIST *flptr, DCTSTATE *dctstate )
{
  int32 len, bytesread = 0 ;

  UNUSED_PARAM(DCTSTATE *, dctstate) ;

  if (( len = dct_get_16bit_num( flptr )) < 0 )
    return FALSE ;
  len -= 2;

  if ( !dct_skip_data(flptr, len-bytesread, len, &bytesread))         /* skip the rest of the field */
    return FALSE;
  return TRUE ;
}


/* Log stripped */
