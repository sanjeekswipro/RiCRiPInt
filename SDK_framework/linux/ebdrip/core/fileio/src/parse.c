/** \file
 * \ingroup fileio
 *
 * $HopeName: COREfileio!src:parse.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Filename parsing. Split device and filename parts, and return separately
 * in null-terminated strings.
 */


#include "core.h"
#include "swdevice.h"
#include "fileio.h"
#include "hqmemcpy.h"

/* ----------------------------------------------------------------------------
   function:            parse_filename    author:              Luke Tunmer
   creation date:       09-Oct-1991       last modification:   ##-###-####
   arguments:
   description:

   Gets the device name and file name parts from the PS string. It places the
   null-terminated results into static buffers, and makes pbuff_device and
   pbuff_file point to them.
---------------------------------------------------------------------------- */

static uint8 buff_device[ LONGESTDEVICENAME ] ;
static uint8 buff_file[ LONGESTFILENAME ] ;

int32 parse_filename(uint8 *name, int32 len, uint8 **pbuff_device, uint8 **pbuff_file)
{
  uint8 *p ;
  int32 count ;

  *pbuff_file = buff_file ;
  *pbuff_device = buff_device ;

  if ( len == 0 ) {
    buff_file[ 0 ] = '\0' ;
    buff_device[ 0 ] = '\0' ;
    return NODEVICEORFILE ;
  }

  p = name ;
  if ( *p == ( uint8 )'%' ) {
    len-- ;
    /* unpack device name */
    count = 0 ;
    while ( *(++p) != ( uint8 )'%' ) {
      buff_device[count] = *p ;
      if ( ++count == LONGESTDEVICENAME )
        return PARSEERROR ;
      if ( count == len ) {
        /* just the device name */
        buff_device[ count ] = '\0' ;
        buff_file[ 0 ] = '\0' ;
        return JUSTDEVICE ;
      }
      if ( *p == ( uint8 )'\\' ) {
        ++p ;
        buff_device[count] = *p ;
        if ( ++count == LONGESTDEVICENAME )
          return PARSEERROR ;
        if ( count == len ) {
          /* just the device name */
          buff_device[ count ] = '\0' ;
          buff_file[ 0 ] = '\0' ;
          return JUSTDEVICE ;
        }
      }
    }
    buff_device[ count++ ] = '\0' ;
    if ( count == len ) {
      buff_file[ 0 ] = '\0' ;
      return JUSTDEVICE ;
    }
    len -= count ;
    if (len > LONGESTFILENAME)
      return PARSEERROR ;
    p++ ;
    HqMemCpy( buff_file , p , len ) ;
    buff_file[ len ] = '\0' ;
    *pbuff_device = buff_device ;
    *pbuff_file = buff_file ;
    return DEVICEANDFILE ;
  } else {
    /* name has no device prefix */
    if (len > LONGESTFILENAME)
      return PARSEERROR ;
    HqMemCpy( buff_file , p , len ) ;
    buff_file[ len ] = '\0' ;
    buff_device[ 0 ] = '\0' ;
    return JUSTFILE ;
  }
}

/*
Log stripped */
