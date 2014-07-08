/* Copyright (C) 1995-2011 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWskinkit!src:fdecrypt.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

#include <stdio.h>
#include <string.h>

#include "ripthread.h"
#include "swdevice.h"
#include "skindevs.h"

#ifdef TEST_DECRYPTFONT

/**
 * \file
 * \ingroup skinkit
 * \brief Example implementation of the font decoder filter.
 *
 * If you wish to test font encoding as well as decoding, define the
 * symbol \c TEST_ENCRYPTFONT instead of \c TEST_DECRYPTFONT. There are two
 * algorithms supported by this code, showing how to implement multiple strategies.
 *
 * The following code is the encryption algorithm that we're testing for:
 *
 * void encrypt_string_algorithm1( slist , slen )
 * {
 *   int32 xorchar = 0x00 ;
 *
 *   while ((--slen) >= 0 ) {
 *     slist[ 0 ] ^= xorchar ;
 *     xorchar += slist[ 0 ] ;
 *     slist++ ;
 *   }
 * }
 *
 * void encrypt_string_algorithm2( slist , slen )
 * {
 *   int32 xorchar = 0x00 ;
 *
 *   while ((--slen) >= 0 ) {
 *     slist[ 0 ] ^= xorchar ;
 *     xorchar += slist[ 0 ] ;
 *     xorchar += 0x10 ;
 *     slist++ ;
 *   }
 * }
 *
 * These encryptions cannot be pre-verified, so the code here will not return
 * DeviceIOError to say "this isn't in my code," most most other features of
 * the filter will be exercised.
 */

static int32 RIPCALL fd_init_device( DEVICELIST *dev );
static int32 RIPCALL fd_open_file( DEVICELIST *dev , uint8 *filename ,
                          int32 openflags );
static int32 RIPCALL fd_read_file( DEVICELIST *dev , int32 descriptor , uint8 *buff ,
                          int32 len );
static int32 RIPCALL fd_write_file( DEVICELIST *dev , int32 descriptor , uint8 *buff ,
                           int32 len );
static int32 RIPCALL fd_close_file( DEVICELIST *dev , int32 descriptor );
static int32 RIPCALL fd_seek_file( DEVICELIST *dev , int32 descriptor , Hq32x2 *destn ,
                          int32 flags );
static int32 RIPCALL fd_bytes_file( DEVICELIST *dev , int32 descriptor , Hq32x2 *bytes,
                            int32 reason );
static int32 RIPCALL fd_status_file( DEVICELIST *dev , uint8 *filename ,
                            STAT *statbuff );
static void* RIPCALL fd_interp_start_file_list( DEVICELIST *dev , uint8 *pattern );
static int32 RIPCALL fd_interp_next_file( DEVICELIST *dev , void **handle ,
                                 uint8 *pattern , FILEENTRY *entry );
static int32 RIPCALL fd_interp_end_file_list( DEVICELIST *dev , void *handle );
static int32 RIPCALL fd_rename_file( DEVICELIST *dev , uint8 *file1 , uint8 *file2 );
static int32 RIPCALL fd_delete_file( DEVICELIST *dev , uint8 *filename );
static int32 RIPCALL fd_set_param( DEVICELIST *dev, DEVICEPARAM *param );
static int32 RIPCALL fd_start_param( DEVICELIST *dev );
static int32 RIPCALL fd_get_param( DEVICELIST *dev , DEVICEPARAM *param );
static int32 RIPCALL fd_status_device( DEVICELIST *dev , DEVSTAT *devstat );
static int32 RIPCALL fd_dismount_device( DEVICELIST *dev );
static int32 RIPCALL fd_buffersize( DEVICELIST *dev );
static int32 RIPCALL fd_void( void );

/**
 * \brief Encapsulates the state of the font decryption device.
 */
typedef struct _fd_state
{
  int32  decryption_open ;      /**< Safety check for multiple opens */
  int32  decryption_eofd ;      /**< Safety check for finished decryption */

  int32  decryption_strategy ;  /**< Strategy to attempt... */

  DEVICELIST *swdev ;           /**< The device used by SwReadFilterBytes */
  uint8      *swbuf ;           /**< Set by SwReadFilterBytes to where we read from */
  int32       swcnt ;           /**< Set by SwReadFilterBytes to how many bytes we can read */

  int32  xorchar ;              /**< State for decrypting character outline */
} FD_STATE ;


/**
 * \brief Device type structure for the font decryption device.
 */
DEVICETYPE FontNDcrypt_Device_Type = {
  FONT_ND_CRYPT_DEVICE_TYPE ,           /**< the device ID number */
  DEVICEABSOLUTE ,                      /**< flags to indicate specifics of device */
  sizeof( FD_STATE ) ,                  /**< the size of the private data */
  0,                                    /**< ticks between tickle functions */
  NULL ,                                /**< procudure to service the device */
  skindevices_last_error ,              /**< return last error for this device */
  fd_init_device ,                      /**< call to initialise device */
  fd_open_file ,                        /**< call to open file on device */
  fd_read_file ,                        /**< call to read data from file on device */
  fd_write_file ,                       /**< call to write data to file on device */
  fd_close_file ,                       /**< call to close file on device */
  fd_close_file ,                       /**< call to abort file */
  fd_seek_file ,                        /**< call to seek file on device */
  fd_bytes_file,                        /**< call to get bytes available */
  fd_status_file ,                      /**< call to check status of file */
  fd_interp_start_file_list,            /**< call to start listing files */
  fd_interp_next_file,          /**< call to get next file in list */
  fd_interp_end_file_list,              /**< call to end listing */
  fd_rename_file,                       /**< call to rename a file on the device */
  fd_delete_file,                       /**< call to delete a file on the device */
  fd_set_param ,                        /**< call to set device param */
  fd_start_param ,                      /**< call to start getting params */
  fd_get_param ,                        /**< call to get the next device param */
  fd_status_device ,                    /**< call to get the status of the device */
  fd_dismount_device ,          /**< call to dismount the device */
  fd_buffersize ,                       /**< call to determine buffer size */
  (DEVICELIST_IOCTL)fd_void ,   /**< spare slots */
  fd_void
} ;


#define FD_DESCRIPTOR    0

#define FD_BUFFERSIZE  128
#define FD_MAXSTRATEGY   2

#define FDGetc(_fd)             (--(_fd)->swcnt>=0?((int32)*(_fd)->swbuf++):FDread(_fd))
#define FDUnGetc(_fd)           ((_fd)->swcnt++,(_fd)->swbuf--)

/** \brief Called from FDGetc when needs more data */
static int32 FDread( FD_STATE *FDSTATE )
{
  FDSTATE->swcnt = SwReadFilterBytes( FDSTATE->swdev , &(FDSTATE->swbuf) ) ;

  switch ( FDSTATE->swcnt ) {
  case -1:
    skindevices_set_last_error(DeviceIOError) ;
  case 0:
    return -1 ;

  default:
    /* some data */
    FDSTATE->swcnt-- ;
    return *FDSTATE->swbuf++ ;
  }
}

/*================================================================*/

/**
 * \brief fd_init_device
 */
static int32 RIPCALL fd_init_device( DEVICELIST *dev )
{
  FD_STATE *FDSTATE = (FD_STATE *)theIPrivate( dev ) ;
  if ( ! FDSTATE )
    return -1 ;

  skindevices_set_last_error(DeviceNoError) ;

  FDSTATE->decryption_open = FALSE ;

  FDSTATE->decryption_strategy = 1 ;

  return 0 ;
}

/**
 * \brief fd_open_file
 * \param dev the device handle
 * \param filename will be null
 * \param openflags appropriate flag, read/write
 */
static int32 RIPCALL fd_open_file( DEVICELIST *dev , uint8 *filename , int32 openflags )
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused( filename , openflags )
#endif /* HAS_PRAGMA_UNUSED */
  FD_STATE *FDSTATE ;

  FDSTATE = (FD_STATE *)theIPrivate( dev ) ;
  if ( ! FDSTATE )
    return -1 ;
  skindevices_set_last_error(DeviceNoError) ;

  if ( FDSTATE->decryption_open ) {
    skindevices_set_last_error(DeviceIOError) ;
    return -1 ;
  }

  FDSTATE->decryption_open = TRUE ;
  FDSTATE->decryption_eofd = FALSE ;

  FDSTATE->swdev   = dev ;              /**< device ptr needed by sw*filterbytes */
  FDSTATE->swbuf = ( uint8 * )NULL ;    /**< until set by SwReadFilterBytes */
  FDSTATE->swcnt = 0 ;

  FDSTATE->xorchar = 0x00 ;

  return FD_DESCRIPTOR ;
}

/* \brief fd_read_file
 * \param dev the device handle
 * \param descriptor filter's unique value returned by openfile,
 * \param buff the buffer to fill,
 * \param len space available
 */
static int32 RIPCALL fd_read_file( DEVICELIST *dev , int32 descriptor , uint8 *buff , int32 len )
{
  int32 swcnt ;
  int32 xorchar ;
  FD_STATE *FDSTATE ;

  FDSTATE = ( FD_STATE *)theIPrivate( dev ) ;
  if ( ! FDSTATE )
    return -1 ;
  skindevices_set_last_error(DeviceNoError) ;

  if ( descriptor != FD_DESCRIPTOR ) {
    skindevices_set_last_error(DeviceIOError) ;
    return -1 ;
  }

  if ( ! FDSTATE->decryption_open ) {
    skindevices_set_last_error(DeviceIOError) ;
    return -1 ;
  }

  if ( FDSTATE->decryption_eofd ) {
    return 0 ;
  }

  swcnt = 0 ;
  xorchar = FDSTATE->xorchar ;

  while ((--len) >= 0 ) {
    int32 nc = FDGetc( FDSTATE ) ;
    if ( nc == EOF ) {
      FDSTATE->decryption_eofd = TRUE ;
      break ;
    }

    buff[ 0 ] = nc ^ xorchar ;
    xorchar += nc ;
    if ( FDSTATE->decryption_strategy == 2 )
      xorchar += 0x10 ;
    buff++ ;

    swcnt++ ;
  }
  FDSTATE->xorchar = xorchar ;

  return swcnt ;
}

/**
 * \brief fd_write_file
 */
static int32 RIPCALL fd_write_file( DEVICELIST *dev , int32 descriptor , uint8 *buff , int32 len )
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused( descriptor , buff , len )
#endif /* HAS_PRAGMA_UNUSED */
  FD_STATE *FDSTATE ;

  FDSTATE = ( FD_STATE *)theIPrivate( dev ) ;
  if ( FDSTATE )
    skindevices_set_last_error(DeviceInvalidAccess) ;

  return -1 ;
}

/**
 * \brief fd_close_file
 */
static int32 RIPCALL fd_close_file( DEVICELIST *dev , int32 descriptor )
{
  FD_STATE *FDSTATE ;

  FDSTATE = ( FD_STATE *)theIPrivate( dev ) ;
  if ( ! FDSTATE )
    return -1 ;
  skindevices_set_last_error(DeviceNoError) ;

  if ( descriptor != FD_DESCRIPTOR ) {
    skindevices_set_last_error(DeviceIOError) ;
    return -1 ;
  }

  if ( ! FDSTATE->decryption_open ) {
    skindevices_set_last_error(DeviceIOError) ;
    return -1 ;
  }

  FDSTATE->decryption_open = FALSE ;

  return 0 ;
}

/**
 * \brief fd_seek_file
 */
static int32 RIPCALL fd_seek_file( DEVICELIST *dev , int32 descriptor , Hq32x2 *destn , int32 flags )
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused( descriptor , destn , flags )
#endif /* HAS_PRAGMA_UNUSED */
  FD_STATE *FDSTATE ;

  FDSTATE = (FD_STATE *)theIPrivate( dev ) ;
  if ( FDSTATE )
    skindevices_set_last_error(DeviceIOError) ;

  return FALSE ;
}

/**
 * \brief fd_bytes_file
 */
static int32 RIPCALL fd_bytes_file( DEVICELIST *dev , int32 descriptor , Hq32x2 *bytes, int32 reason )
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused( descriptor , bytes, reason )
#endif /* HAS_PRAGMA_UNUSED */
  FD_STATE *FDSTATE ;

  FDSTATE = (FD_STATE *)theIPrivate( dev ) ;
  if ( FDSTATE )
    skindevices_set_last_error(DeviceIOError) ;

  return FALSE ;
}

/**
 * \brief fd_status_file
 */
static int32 RIPCALL fd_status_file( DEVICELIST *dev , uint8 *filename , STAT *statbuff )
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused( filename , statbuff )
#endif /* HAS_PRAGMA_UNUSED */
  FD_STATE *FDSTATE ;

  FDSTATE = (FD_STATE *)theIPrivate( dev ) ;
  if ( FDSTATE )
    skindevices_set_last_error(DeviceIOError) ;

  return -1 ;
}

/**
 * \brief fd_interp_start_file_list
 */
static void* RIPCALL fd_interp_start_file_list( DEVICELIST *dev , uint8 *pattern )
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused( pattern )
#endif /* HAS_PRAGMA_UNUSED */
  FD_STATE *FDSTATE ;

  FDSTATE = (FD_STATE *)theIPrivate( dev ) ;
  if ( FDSTATE )
    skindevices_set_last_error(DeviceNoError) ;

  return ( void * )NULL ;
}

/**
 * \brief fd_interp_next_file
 */
static int32 RIPCALL fd_interp_next_file( DEVICELIST *dev , void **handle , uint8 *pattern , FILEENTRY *entry )
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused( handle , pattern , entry )
#endif /* HAS_PRAGMA_UNUSED */
  FD_STATE *FDSTATE ;

  FDSTATE = (FD_STATE *)theIPrivate( dev ) ;
  if ( FDSTATE )
    skindevices_set_last_error(DeviceNoError) ;

  return FileNameNoMatch ;
}

/**
 * \brief fd_interp_end_file_list
 */
static int32 RIPCALL fd_interp_end_file_list( DEVICELIST *dev , void *handle )
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused( handle )
#endif /* HAS_PRAGMA_UNUSED */
  FD_STATE *FDSTATE ;

  FDSTATE = (FD_STATE *)theIPrivate( dev ) ;
  if ( FDSTATE )
    skindevices_set_last_error(DeviceNoError) ;

  return 0 ;
}

/**
 * \brief fd_rename_file
 */
static int32 RIPCALL fd_rename_file( DEVICELIST *dev , uint8 *file1 , uint8 *file2 )
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused( file1 , file2 )
#endif /* HAS_PRAGMA_UNUSED */
  FD_STATE *FDSTATE ;

  FDSTATE = (FD_STATE *)theIPrivate( dev ) ;
  if ( FDSTATE )
    skindevices_set_last_error(DeviceIOError) ;

  return -1 ;
}

/**
 * \brief fd_delete_file
 */
static int32 RIPCALL fd_delete_file( DEVICELIST *dev , uint8 *filename )
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused( filename )
#endif /* HAS_PRAGMA_UNUSED */
  FD_STATE *FDSTATE ;

  FDSTATE = (FD_STATE *)theIPrivate( dev ) ;
  if ( FDSTATE )
    skindevices_set_last_error(DeviceIOError) ;

  return -1 ;
}

/**
 * \brief fd_set_param
 */
static int32 RIPCALL fd_set_param( DEVICELIST *dev, DEVICEPARAM *param )
{
  FD_STATE *FDSTATE ;

  FDSTATE = (FD_STATE *)theIPrivate( dev ) ;
  if ( ! FDSTATE )
    return ParamError ;
  skindevices_set_last_error(DeviceNoError) ;

  if ( theIDevParamNameLen( param ) == strlen( "Strategy" ) &&
       strncmp(( char * )theIDevParamName( param ) , ( char * )"Strategy" ,
               theIDevParamNameLen( param )) == 0 ) {
    int32 strategy ;
    if ( FD_MAXSTRATEGY == 0 )
      return ParamIgnored ;
    if ( theIDevParamType( param ) != ParamInteger )
      return ParamTypeCheck ;
    strategy = theIDevParamInteger( param ) ;
    if ( strategy < 1 || strategy > FD_MAXSTRATEGY )
      return ParamRangeCheck ;
    FDSTATE->decryption_strategy = strategy ;
    return ParamAccepted ;
  }

  return ParamIgnored ;
}

/**
 * \brief fd_start_param
 */
static int32 RIPCALL fd_start_param( DEVICELIST *dev )
{
  FD_STATE *FDSTATE ;

  FDSTATE = (FD_STATE *)theIPrivate( dev ) ;
  if ( FDSTATE )
    skindevices_set_last_error(DeviceNoError) ;

  return 0 ; /* No parameters to return */
}

/**
 * \brief fd_get_param.  This accepts an explicit request for a param, but not a forall
 */
static int32 RIPCALL fd_get_param( DEVICELIST *dev , DEVICEPARAM *param )
{
  FD_STATE *FDSTATE ;

  FDSTATE = (FD_STATE *)theIPrivate( dev ) ;
  if ( ! FDSTATE )
    return ParamError ;
  skindevices_set_last_error(DeviceNoError) ;

  if ( theIDevParamName( param ) == NULL ) {
    skindevices_set_last_error(DeviceIOError) ;
    return ParamError ;
  }

#define INSTALL_LEN 7
  if ( theIDevParamNameLen( param ) == INSTALL_LEN )
    if ( strncmp(( char * )theIDevParamName( param ) , "Install" , INSTALL_LEN ) == 0 ) {
      theIDevParamType( param ) = ParamString ;
      return ParamAccepted ;
    }

  return ParamIgnored ;
}

/**
 * \brief fd_status_device
 */
static int32 RIPCALL fd_status_device( DEVICELIST *dev , DEVSTAT *devstat )
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused( devstat )
#endif /* HAS_PRAGMA_UNUSED */
  FD_STATE *FDSTATE ;

  FDSTATE = (FD_STATE *)theIPrivate( dev ) ;
  if ( FDSTATE )
    skindevices_set_last_error(DeviceIOError) ;

  return -1 ;
}

/**
 * \brief fd_dismount_device
 */
static int32 RIPCALL fd_dismount_device( DEVICELIST *dev )
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused( dev )
#endif /* HAS_PRAGMA_UNUSED */

  return 0 ;
}

/**
 * \brief fd_buffersize
 */
static int32 RIPCALL fd_buffersize( DEVICELIST *dev )
{
  FD_STATE *FDSTATE ;

  FDSTATE = ( FD_STATE *)theIPrivate( dev ) ;
  if ( ! FDSTATE )
    return -1 ;
  skindevices_set_last_error(DeviceNoError) ;

  return FD_BUFFERSIZE ;
}

/**
 * \brief fd_void
 */
static int32 RIPCALL fd_void( void )
{
  return 0 ;
}

#else /* #ifdef TEST_DECRYPTFONT */

int32 fdecrypt = 0 ;

#endif /* #ifdef TEST_DECRYPTFONT */

/* end of file fdecrypt.c */
