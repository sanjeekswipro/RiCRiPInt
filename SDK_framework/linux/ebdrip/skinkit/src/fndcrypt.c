/* Copyright (C) 2006-2011 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWskinkit!src:fndcrypt.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

#ifdef TEST_ENCRYPTFONT

#include "ripthread.h"
#include "swdevice.h"
#include "skindevs.h"

#include <string.h>

/**
 * \file
 * \ingroup skinkit
 * \brief Example implementation of the font decoder and encoder filter. If you wish
 * to just test font decoding, define TEST_DECRYPTFONT instead. There are two
 * encryption algorithms supported, to show how to implement multiple
 * strategies.
 *
 * Implementing the encryption filter as the straight inverse of the
 * decryption filter is not secure; it is very vulnerable to chosen plaintext
 * attacks. The path provided by HDLT is a binary encoded userpath; the
 * protection attribute is a combination of all of the protection methods
 * used in constructing the path. If an unprotected path is created, and a
 * charpath from a protected font is added to the path, the whole path will
 * be exported using the protected font's methods. The initial parts of the
 * binary encoded path will be known to the attacker (they are the
 * unprotected part of the path), and this can be exploited to break the
 * encryption method.
 *
 * Do NOT implement a pass-through write_file routine here under any
 * circumstances, it'll make your font data insecure.  It must either
 * encrypt the data it sees securely or return an error.  Checking the
 * openflags on your open call and faulting any call which is not SW_RDONLY
 * would be best.
 *
 * The encryption here is a better example of how to keep your encryption
 * algorithm secure. It demonstrates one half of a method that works in a
 * closed environment. The path supplied to the encryption filter should be
 * stored by the filter, which return the results of a secure hash function
 * applied to the path. The client code can pass the hash value to consumers,
 * who then communicate with the font encryption filter to retrieve the path.
 *
 * Note that encoded userpaths consist of two parts, the arguments and the
 * commands, so in such a case storing two strings under one hash value would
 * be useful.
 *
 * The example "encryption" in this file actually adds a header revealing the
 * strategy used, and then outputs a checksum of the string.
 *
 * The following code is the decryption algorithm in that we're testing for:
 *
 * void encrypt_string_algorithm1( slist, slen )
 * {
 *   int32 xorchar = 0x00;
 *
 *   while ((--slen) >= 0 ) {
 *     slist[ 0 ] ^= xorchar;
 *     xorchar += slist[ 0 ];
 *     slist++;
 *   }
 * }
 *
 * void encrypt_string_algorithm2( slist, slen )
 * {
 *   int32 xorchar = 0x00;
 *
 *   while ((--slen) >= 0 ) {
 *     slist[ 0 ] ^= xorchar;
 *     xorchar += slist[ 0 ];
 *     xorchar += 0x10;
 *     slist++;
 *   }
 * }
 *
 * These encryptions cannot be pre-verified, so the code here will not return
 * DeviceIOError to say "this isn't in my code," most most other features of
 * the filter will be exercised.
 */

static int32 RIPCALL fND_init_device( DEVICELIST *dev );
static DEVICE_FILEDESCRIPTOR RIPCALL fND_open_file( DEVICELIST *dev, uint8 *filename,
                                    int32 openflags );
static int32 RIPCALL fND_read_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, uint8 *buff,
                                    int32 len );
static int32 RIPCALL fND_write_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, uint8 *buff,
                                     int32 len );
static int32 RIPCALL fND_close_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor );
static int32 RIPCALL fND_seek_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, Hq32x2 *destn,
                                    int32 flags );
static int32 RIPCALL fND_bytes_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, Hq32x2 *destn, int32 reason );
static int32 RIPCALL fND_status_file( DEVICELIST *dev, uint8 *filename,
                                      STAT *statbuff );
static void* RIPCALL fND_interp_start_file_list( DEVICELIST *dev, uint8 *pattern );
static int32 RIPCALL fND_interp_next_file( DEVICELIST *dev, void **handle,
                                           uint8 *pattern, FILEENTRY *entry );
static int32 RIPCALL fND_interp_end_file_list( DEVICELIST *dev, void *handle );
static int32 RIPCALL fND_rename_file( DEVICELIST *dev, uint8 *file1, uint8 *file2 );
static int32 RIPCALL fND_delete_file( DEVICELIST *dev, uint8 *filename );
static int32 RIPCALL fND_set_param( DEVICELIST *dev, DEVICEPARAM *param );
static int32 RIPCALL fND_start_param( DEVICELIST *dev );
static int32 RIPCALL fND_get_param( DEVICELIST *dev, DEVICEPARAM *param );
static int32 RIPCALL fND_status_device( DEVICELIST *dev, DEVSTAT *devstat );
static int32 RIPCALL fND_dismount_device( DEVICELIST *dev );
static int32 RIPCALL fND_buffersize( DEVICELIST *dev );
static int32 RIPCALL fND_void( void );


/**
 * \brief Encapsulates the current state of the font encoder/decoder
 * device.
 */
typedef struct _fND_state
{
  int32  decryption_open;  /**< Safety check for multiple opens */
  int32  encryption_open;  /**< Safety check for multiple opens */
  int32  decryption_eofd;  /**< Safety check for finished decryption */

  int32  strategy;         /**< Strategy to attempt... */

  DEVICELIST *swdev;       /**< The device used by SwReadFilterBytes */
  uint8      *swbuf;       /**< Set by SwReadFilterBytes to where we read from */
  int32       swcnt;       /**< Set by SwReadFilterBytes to how many bytes we can read */

  int32  xorchar;          /**< State for decrypting character outline */
  uint16 s1, s2;           /**< Partial checksum results for "encryption" */
} FND_STATE;

/**
 * \brief Encapsulates the set of entry points for the font
 * encoder/decoder device.
 */
DEVICETYPE FontNDcrypt_Device_Type = {
  FONT_ND_CRYPT_DEVICE_TYPE,   /**< the device ID number */
  DEVICEABSOLUTE,              /**< flags to indicate specifics of device */
  sizeof( FND_STATE ),         /**< the size of the private data */
  0,                           /**< ticks between tickle functions */
  NULL,                        /**< procudure to service the device */
  skindevices_last_error,      /**< return last error for this device */
  fND_init_device,             /**< call to initialise device */
  fND_open_file,               /**< call to open file on device */
  fND_read_file,               /**< call to read data from file on device */
  fND_write_file,              /**< call to write data to file on device */
  fND_close_file,              /**< call to close file on device */
  fND_close_file,              /**< call to abort file */
  fND_seek_file,               /**< call to seek file on device */
  fND_bytes_file,              /**< call to get bytes available */
  fND_status_file,             /**< call to check status of file */
  fND_interp_start_file_list,  /**< call to start listing files */
  fND_interp_next_file,        /**< call to get next file in list */
  fND_interp_end_file_list,    /**< call to end listing */
  fND_rename_file,             /**< call to rename a file on the device */
  fND_delete_file,             /**< call to delete a file on the device */
  fND_set_param,               /**< call to set device param */
  fND_start_param,             /**< call to start getting params */
  fND_get_param,               /**< call to get the next device param */
  fND_status_device,           /**< call to get the status of the device */
  fND_dismount_device,         /**< call to dismount the device */
  fND_buffersize,              /**< call to determine buffer size */
  NULL,                        /**< ioctl slot (optional) */
  fND_void                     /**< spare slot */
};

/**
 * \brief Modulus for the Adler 32 algorithm.
 */
#define ADLER32_BASE 65521

/** \brief Calculate an Adler32 checksum, similar to the Flate standard. This is used
   in the path "encryption" to supply a checksum based upon the path string.
   See the comments above about security of the encryption filter. */
static uint32 calculateAdler32(uint8 *byteptr, int32 count, uint16 *s1, uint16 *s2)
{
  uint16 s1local = *s1;
  uint16 s2local = *s2;

  while ( count-- ) {
    s1local = ( uint16 )(( s1local + *byteptr++ ) % ADLER32_BASE );
    s2local = ( uint16 )(( s2local + s1local ) % ADLER32_BASE );
  }

  *s1 = s1local;
  *s2 = s2local;

  /* The Adler-32 checksum is stored as s2 * 65536 + s1. */
  return (uint32)((s2local << 16 ) | s1local);
}

#define FND_RD_DESCRIPTOR    10
#define FND_WT_DESCRIPTOR    11

#define FND_BUFFERSIZE  128
#define FND_MAXSTRATEGY   2

#define FDGetc(_fd)    (--(_fd)->swcnt>=0?((int32)*(_fd)->swbuf++):FDread(_fd))
#define FDUnGetc(_fd)  ((_fd)->swcnt++,(_fd)->swbuf--)

/** \brief Called from FDGetc when needs more data */
static int32 FDread( FND_STATE *pState )
{
  pState->swcnt = SwReadFilterBytes( pState->swdev, &(pState->swbuf) );

  switch ( pState->swcnt ) {
  case -1:
    skindevices_set_last_error(DeviceIOError);
  case 0:
    return -1;

  default:
    /* some data */
    pState->swcnt--;
    return *pState->swbuf++;
  }
}

/*================================================================*/

/**
 * \brief fND_init_device
 */
static int32 RIPCALL fND_init_device( DEVICELIST *dev )
{
  FND_STATE *pState;

  pState = (FND_STATE *)theIPrivate( dev );
  if ( ! pState )
    return -1;

  skindevices_set_last_error(DeviceNoError);
  pState->decryption_open = FALSE;
  pState->encryption_open = FALSE;

  pState->strategy = 1;

  return 0;
}

/**
 * \brief fND_open_file
 *
 * \param dev the device handle
 * \param filename will be null
 * \param openflags is appropriate flag, read/write
 */
static DEVICE_FILEDESCRIPTOR RIPCALL fND_open_file( DEVICELIST *dev, uint8 *filename, int32 openflags )
{
  FND_STATE *pState;

  UNUSED_PARAM( uint8 *, filename );

  pState = (FND_STATE *)theIPrivate( dev );
  if ( ! pState )
    return -1;
  skindevices_set_last_error(DeviceNoError);

  if ( pState->decryption_open || pState->encryption_open ) {
    skindevices_set_last_error(DeviceIOError);
    return -1;
  }

  if ( SW_RDONLY == ( openflags & (SW_RDONLY | SW_WRONLY | SW_RDWR) ) ) {
    pState->decryption_open = TRUE;
    pState->decryption_eofd = FALSE;

    pState->swdev   = dev;                /* device ptr needed by sw*filterbytes */
    pState->swbuf = ( uint8 * )NULL;      /* until set by SwReadFilterBytes */
    pState->swcnt = 0;

    pState->xorchar = 0x00;

    return FND_RD_DESCRIPTOR;
  }

  if ( SW_WRONLY == ( openflags & (SW_RDONLY | SW_WRONLY | SW_RDWR) ) ) {
    pState->encryption_open = TRUE;

    pState->swdev   = dev;
    pState->swbuf = ( uint8 * )NULL;
    pState->swcnt = 0;
    pState->s1 = (uint16)pState->strategy; /* Seed depending on strategy */
    pState->s2 = 0;

    pState->xorchar = 0x00;

    if ( 16 != SwWriteFilterBytes(dev,
                                  1 == pState->strategy ? (unsigned char *)"Strategy ONE... " :
                                  2 == pState->strategy ? (unsigned char *)"Strategy TWO... " :
                                                           (unsigned char *)"Strategy which? ",
                                  16) ) {
      skindevices_set_last_error(DeviceIOError);
      return -1;
    }

    return FND_WT_DESCRIPTOR;
  }

  skindevices_set_last_error(DeviceInvalidAccess);
  return -1;
}

/**
 * \brief fND_read_file
 * \param dev the device handle
 * \param descriptor is filter's unique value returned by openfile,
 * \param buff is the buffer to fill,
 * \param len is space available
 */
static int32 RIPCALL fND_read_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, uint8 *buff, int32 len )
{
  int32 swcnt;
  int32 xorchar;
  FND_STATE *pState;

  pState = ( FND_STATE *)theIPrivate( dev );
  if ( ! pState )
    return -1;
  skindevices_set_last_error(DeviceNoError);

  if ( descriptor != FND_RD_DESCRIPTOR ) {
    skindevices_set_last_error(DeviceIOError);
    return -1;
  }

  if ( ! pState->decryption_open ) {
    skindevices_set_last_error(DeviceIOError);
    return -1;
  }

  if ( pState->decryption_eofd ) {
    return 0;
  }

  swcnt = 0;
  xorchar = pState->xorchar;

  while ((--len) >= 0 ) {
    int32 nc = FDGetc( pState );
    if ( nc == EOF ) {
      pState->decryption_eofd = TRUE;
      break;
    }

    buff[ 0 ] = (uint8)(nc ^ xorchar);
    xorchar += nc;
    if ( pState->strategy == 2 )
      xorchar += 0x10;
    buff++;

    swcnt++;
  }
  pState->xorchar = xorchar;

  return swcnt;
}

/**
 * \brief fND_write_file
 */
static int32 RIPCALL fND_write_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, uint8 *buff, int32 len )
{
  FND_STATE *pState;

  pState = ( FND_STATE *)theIPrivate( dev );
  if ( ! pState )
    return -1;

  if ( descriptor != FND_WT_DESCRIPTOR ) {
    skindevices_set_last_error(DeviceIOError);
    return -1;
  }

  if ( ! pState->encryption_open ) {
    skindevices_set_last_error(DeviceIOError);
    return -1;
  }

  skindevices_set_last_error(DeviceNoError);

  calculateAdler32(buff, len, &pState->s1, &pState->s2);

  return len;
}

/**
 * \brief fND_close_file
 */
static int32 RIPCALL fND_close_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor )
{
  FND_STATE *pState;
  int32 ret = 0;
  int32 i;
  uint8 hexAdler[8];
  uint32 adler32;

  static char hex_table[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
  };

  pState = ( FND_STATE *)theIPrivate( dev );
  if ( ! pState )
    return -1;
  skindevices_set_last_error(DeviceNoError);

  if ( descriptor == FND_RD_DESCRIPTOR ) {
    if ( ! pState->decryption_open ) {
      skindevices_set_last_error(DeviceIOError);
      ret = -1;
    }
    if ( pState->encryption_open ) {
      skindevices_set_last_error(DeviceIOError);
      ret = -1;
    }
  }
  else if ( descriptor == FND_WT_DESCRIPTOR ) {
    if ( ! pState->encryption_open ) {
      skindevices_set_last_error(DeviceIOError);
      ret = -1;
    }
    if ( pState->decryption_open ) {
      skindevices_set_last_error(DeviceIOError);
      ret = -1;
    }

    adler32 = (pState->s2 << 16) | pState->s1;
    for ( i = 0; i < 8; ++i ) {
      hexAdler[i] = (uint8)hex_table[adler32 >> 28]; /* Most significant nibble */
      adler32 <<= 4;
    }

    if ( 8 != SwWriteFilterBytes( dev, hexAdler, 8 ) ) {
      skindevices_set_last_error(DeviceIOError);
      return -1;
    }
  } else {
    /* neither descriptor! */
    skindevices_set_last_error(DeviceIOError);
    ret = -1;
  }

  /* close down our state anyway */
  pState->decryption_open = pState->encryption_open = FALSE;

  return ret;
}

/**
 * \brief fND_seek_file
 */
static int32 RIPCALL fND_seek_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, Hq32x2 *destn, int32 flags )
{
  FND_STATE *pState;

  UNUSED_PARAM( DEVICE_FILEDESCRIPTOR, descriptor );
  UNUSED_PARAM( Hq32x2 *, destn );
  UNUSED_PARAM( int32, flags );

  pState = (FND_STATE *)theIPrivate( dev );
  if ( pState )
    skindevices_set_last_error(DeviceIOError);

  return FALSE;
}

/**
 * \brief fND_bytes_file
 */
static int32 RIPCALL fND_bytes_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor, Hq32x2 *bytes, int32 reason )
{
  FND_STATE *pState;

  UNUSED_PARAM( DEVICE_FILEDESCRIPTOR, descriptor );
  UNUSED_PARAM( Hq32x2 *, bytes );
  UNUSED_PARAM( int32, reason );

  pState = (FND_STATE *)theIPrivate( dev );
  if ( pState )
    skindevices_set_last_error(DeviceIOError);

  return FALSE;
}

/**
 * \brief fND_status_file
 */
static int32 RIPCALL fND_status_file( DEVICELIST *dev, uint8 *filename, STAT *statbuff )
{
  FND_STATE *pState;

  UNUSED_PARAM( uint8 *, filename );
  UNUSED_PARAM( STAT *, statbuff );

  pState = (FND_STATE *)theIPrivate( dev );
  if ( pState )
    skindevices_set_last_error(DeviceIOError);

  return -1;
}

/**
 * \brief *fND_interp_start_file_list
 */
static void* RIPCALL fND_interp_start_file_list( DEVICELIST *dev, uint8 *pattern )
{
  FND_STATE *pState;

  UNUSED_PARAM( uint8 *, pattern );

  pState = (FND_STATE *)theIPrivate( dev );
  if ( pState )
    skindevices_set_last_error(DeviceNoError);

  return ( void * )NULL;
}

/**
 * \brief fND_interp_next_file
 */
static int32 RIPCALL fND_interp_next_file( DEVICELIST *dev, void **handle, uint8 *pattern, FILEENTRY *entry )
{
  FND_STATE *pState;

  UNUSED_PARAM( void **, handle );
  UNUSED_PARAM( uint8 *, pattern );
  UNUSED_PARAM( FILEENTRY *, entry );

  pState = (FND_STATE *)theIPrivate( dev );
  if ( pState )
    skindevices_set_last_error(DeviceNoError);

  return FileNameNoMatch;
}

/**
 * \brief fND_interp_end_file_list
 */
static int32 RIPCALL fND_interp_end_file_list( DEVICELIST *dev, void *handle )
{
  FND_STATE *pState;

  UNUSED_PARAM( void **, handle );

  pState = (FND_STATE *)theIPrivate( dev );
  if ( pState )
    skindevices_set_last_error(DeviceNoError);

  return 0;
}

/**
 * \brief fND_rename_file
 */
static int32 RIPCALL fND_rename_file( DEVICELIST *dev, uint8 *file1, uint8 *file2 )
{
  FND_STATE *pState;

  UNUSED_PARAM( uint8 *, file1 );
  UNUSED_PARAM( uint8 *, file2 );

  pState = (FND_STATE *)theIPrivate( dev );
  if ( pState )
    skindevices_set_last_error(DeviceIOError);

  return -1;
}

/**
 * \brief fND_delete_file
 */
static int32 RIPCALL fND_delete_file( DEVICELIST *dev, uint8 *filename )
{
  FND_STATE *pState;

  UNUSED_PARAM( uint8 *, filename );

  pState = (FND_STATE *)theIPrivate( dev );
  if ( pState )
    skindevices_set_last_error(DeviceIOError);

  return -1;
}

/**
 * \brief fND_set_param
 */
static int32 RIPCALL fND_set_param( DEVICELIST *dev, DEVICEPARAM *param )
{
  FND_STATE *pState;

  pState = (FND_STATE *)theIPrivate( dev );
  if ( ! pState )
    return ParamError;
  skindevices_set_last_error(DeviceNoError);

  if ( (size_t)theIDevParamNameLen( param ) == strlen("Strategy") &&
       strncmp(( char * )theIDevParamName( param ), ( char * )"Strategy",
               theIDevParamNameLen( param )) == 0 ) {
    int32 strategy;
    if ( FND_MAXSTRATEGY == 0 )
      return ParamIgnored;
    if ( theIDevParamType( param ) != ParamInteger )
      return ParamTypeCheck;
    strategy = theIDevParamInteger( param );
    if ( strategy < 1 || strategy > FND_MAXSTRATEGY )
      return ParamRangeCheck;
    pState->strategy = strategy;
    return ParamAccepted;
  }

  return ParamIgnored;
}

/**
 * \brief fND_start_param
 */
static int32 RIPCALL fND_start_param( DEVICELIST *dev )
{
  FND_STATE *pState;

  pState = (FND_STATE *)theIPrivate( dev );
  if ( ! pState )
    return ParamError;
  skindevices_set_last_error(DeviceNoError);

  return 0; /* No parameters to return */
}

/**
 * \brief fND_get_param
 This accepts an explicit request for a param, but not a forall */
static int32 RIPCALL fND_get_param( DEVICELIST *dev, DEVICEPARAM *param )

{
  FND_STATE *pState;

  pState = (FND_STATE *)theIPrivate( dev );
  if ( ! pState )
    return ParamError;
  skindevices_set_last_error(DeviceNoError);

  if ( theIDevParamName( param ) == NULL ) {
    skindevices_set_last_error(DeviceIOError);
    return ParamError;
  }

#define INSTALL_LEN 7
  if ( theIDevParamNameLen( param ) == INSTALL_LEN )
    if ( strncmp(( char * )theIDevParamName( param ), "Install", INSTALL_LEN ) == 0 ) {
      theIDevParamType( param ) = ParamString;
      return ParamAccepted;
    }

  return ParamIgnored;
}

/**
 * \brief fND_status_device
 */
static int32 RIPCALL fND_status_device( DEVICELIST *dev, DEVSTAT *devstat )
{
  FND_STATE *pState;

  UNUSED_PARAM( DEVSTAT *, devstat );

  pState = (FND_STATE *)theIPrivate( dev );
  if ( pState )
    skindevices_set_last_error(DeviceIOError);

  return -1;
}

/**
 * \brief fND_dismount_device
 */
static int32 RIPCALL fND_dismount_device( DEVICELIST *dev )
{
  UNUSED_PARAM( DEVICELIST *, dev );
  return 0;
}

/**
 * \brief fND_buffersize
 */
static int32 RIPCALL fND_buffersize( DEVICELIST *dev )
{
  FND_STATE *pState;

  pState = ( FND_STATE *)theIPrivate( dev );
  if ( ! pState )
    return -1;
  skindevices_set_last_error(DeviceNoError);

  return FND_BUFFERSIZE;
}

/**
 * \brief fND_void
 */
static int32 RIPCALL fND_void( void )
{
  return 0;
}

#else /* #ifdef TEST_ENCRYPTFONT */

int f_nd_crypt = 0;

#endif /* #ifdef TEST_ENCRYPTFONT */

/* end of file fndcrypt.c */
