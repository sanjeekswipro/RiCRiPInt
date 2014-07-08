/** \file
 * \ingroup pdf
 *
 * $HopeName: COREpdf_base!src:stream.c(EBDSDK_P.1) $
 * $Id: src:stream.c,v 1.7.10.1.1.1 2013/12/19 11:25:03 anon Exp $
 *
 * Copyright (C) 2001-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Common base functions for PDF in and out
 */


#include "core.h"
#include "objects.h"
#include "fileio.h"
#include "stream.h"


/* ---------------------------------------------------------------------- */
FILELIST *streamLookup( OBJECT *theo )
{
  FILELIST *flptr ;
  FILELIST *uflptr ;

  HQASSERT( theo , "theo is NULL in streamLookup" ) ;
  HQASSERT( oType(*theo) == OFILE ,
            "theo is not a stream in streamLookup" ) ;

  flptr = oFile(*theo) ;

  HQASSERT( flptr , "flptr is NULL in streamLookup." ) ;
  HQASSERT( isIFilter( flptr ) , "flptr is not a filter." ) ;

  /* The stream may be underlying a number of filters. I assume here
   * that the stream filter is the lowest filter in the chain.
   */

  uflptr = flptr->underlying_file ;

  while ( uflptr && ( uflptr->filter_id == flptr->underlying_filter_id )) {
    uflptr = uflptr->underlying_file ;
    if ( uflptr || isIFilter( flptr->underlying_file ))
      flptr = flptr->underlying_file ;
  }

  return flptr ;
}

/* ---------------------------------------------------------------------- */
OBJECT *streamLookupDict( OBJECT *theo )
{
  FILELIST *flptr = streamLookup( theo ) ;

  HQASSERT( flptr, "Couldn't get underlying stream in streamLookupDict." ) ;

  return & ( theIParamDict( flptr ) ) ;
}

/*
Log stripped */
