/* Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWskinkit!src:pgbdev.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/**
 * \file
 * \ingroup skinkit
 * \brief An example implementation of the PageBuffer device type.
 *
 * This example is a minimal implementation illustrating a
 * technique for handling large numbers of device parameters, and the
 * subsidiary use of one device type from another. The exact semantics
 * of the %pagebuffer% device are described in Chapter 6 of the
 * PostScript Language Reference Manual.
 *
 * The pagebuffer device type is the medium for delivering the output
 * raster data from the rip to its final destination. A device called
 * %pagebuffer% must exist in every implementation (otherwise the
 * system will not start), and it is communicated with during the
 * starting sequence, so it must be created in the Sys/ExtraDevices
 * file (any later is too late) with the usual incantation:
 * <pre>
 *     (%pagebuffer%) dup statusdict begin devmount pop end
 *     <<
 *       /Enable true
 *       /DeviceType 16\#ffff0003
 *       /Password 0
 *     >> setdevparams
 * </pre>
 * There is no reason ever to have more than one device mounted of
 * pagebuffer type; and the device is non-relative, so at any time
 * there will only be one file open on it (in practice, for either
 * writing or read/write).
 *
 * The device must be enabled because parameters are set for it in
 * PostScript (indeed you may need to write PostScript to do this),
 * but you would never open a file on the device explicitly in
 * PostScript (though there is nothing that actually prevents you
 * doing so). With the exception of device parameters, the
 * %pagebuffer% device and associated files are controlled entirely by
 * the rip.
 *
 * Each page is treated as a file on the %pagebuffer% device, which is
 * opened before the page contents are delivered and closed again
 * afterwards. The raster data is communicated by the rip via write
 * calls (and sometimes demanded back again via read calls), and
 * essential data about the page of output and the capabilities of the
 * particular implementation of the pagebuffer device is communicated
 * (in both directions) with parameters of the device in the set_param
 * and get_param functions.
 *
 * Though the intended functionality of the device is rather
 * specialized, the functional interface between the rip and files on
 * the device is exactly the same as any other device. It was stated
 * above that that the %pagebuffer% device must exist for the rip to
 * start, but that does not mean initially that its type need be
 * special: it is possible to assign the %pagebufer% device a device
 * type of 10 (decimal) - see ripthread.h - which just implements it
 * as a file on the filing system.  This will allow the rip to start -
 * which getting to that point is the most difficult part of the
 * development - and later the device can be specialized to its
 * intended purpose by reassigning its type number.  (This is only
 * possible because by definition device parameters are ignored when
 * not recognized).
 *
 * This example implementation is very similar to the idea outlined
 * above. It does supply routines for its own device type rather than
 * using a built-in device type, but these simply write the data to
 * and from a named file on the %os% device using subsidiary calls to
 * that device's functions.
 *
 * As well as illustrating this as a technique, another feature shown
 * in this example is the handling of the large numbers of device
 * parameters associated with the pagebuffer device. These parameters,
 * which describe the page, are written as a header in the underlying
 * file created by the pagebuffer device.
 *
 * Producing a functional pagebuffer device for the output desired is
 * the main purpose of your development. Among the considerations you
 * will need to take into account when relacing this minimal
 * implementation are:
 *
 * o Timing. Real output devices are particularly sensitive to the
 * timing behavior of the system.
 *
 * o Dealing with different data formats, and rejecting those which
 * are inappropriate. (Careful use of the SensePageDevice key to the
 * setpagedevice operator can help to restrict the formats the
 * pagebuffer will see).
 *
 * o Correct handling of errors returned to the rip by the pagebuffer
 * device type functions is important for the correct operation of the
 * rip. A real output recorder would generate a variety of error
 * conditions, some of which require a page to be reoutput, others of
 * which will require the rip to abandon the page. For this reason
 * there are some additional error codes which the only the pagebuffer
 * last error function can return.
 *
 * o Storage of incomplete page data (or correct handling of this case
 * as an error). If the rip is short of memory it will render the page
 * built up so far and pass it to the %pagebuffer% device; it will
 * then ask for it back later. To do so it would typically save the
 * data to disk, probably compressing it as it does so. If the
 * %pagebuffer% device is incapable of doing this, it would need to
 * report a DeviceVMError when the attempt is made to open the
 * pagebuffer file when the PrintPage device parameter is false. This
 * would, of course restrict the complexity of pages which the system
 * can print; though this situation will not arise if the whole page
 * can be represented in memory at once.
 */

/* ----------------------------------------------------------------------
   Include files and miscellaneous static data
*/

#ifndef UINT32_MAX
#  define UINT32_MAX 0xFFFFFFFF
#endif

#include "hqstr.h"
#include "kit.h"
#include "ripthread.h"
#include "mem.h"
#include "file.h"
#include "swdevice.h"
#include "skindevs.h"
#include "swoften.h"
#include "zlibutil.h"
#include "devparam.h"
#include "skinkit.h"
#include "ktime.h"
#include "swevents.h"
#include "swtimelines.h"
#include "hqmemset.h"
#include "hqmemcpy.h"

#ifdef HAS_RLE
#include "scrndev.h"
#endif

#ifdef METRO
#include "xpsconfstate.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>

/**
 * \brief A container for band data delivered during partial paint or
 * transparency compositing, and optionally then compressed. */
typedef struct BandHolder {
  uint8 * pData;    /**< A data buffer containing compressed band
                       data.  When band is on disk, this will be
                       NULL.  */
  uint32 bandLength; /**< Number of bytes in pData */
  uint8 *dc_in_mem;  /**< in-memory disk cache */
} BandHolder;


/**
 * \brief Entry in disk cache file's table to hold the details
 * of a band.
 */
typedef struct BandIndex {
  Hq32x2 bandLocation; /**< Location of start of band */
  uint32 bandLength;   /**< Number of bytes of (possibly compressed) band data */
} BandIndex;


/**
 * \brief An arbitrarily chosen maximum size for the in-memory band
 * cache.  When this much memory is used by compressed band data, the
 * band data is flushed to disk.
 */
#define MAX_MEMORY_CACHE_SIZE_BYTES (1 * 512 * 1024) /* 512K */

/**
 * \brief A magic value to denote that the disk band cache file
 * descriptor is currently unset.
 */
#define BAND_CACHE_FD_UNSET (-1)

/**
 * \brief File descriptor of file to which the partial paint band
 * cache is flushed.
 */
static DEVICE_FILEDESCRIPTOR bandCacheFD = BAND_CACHE_FD_UNSET;


/**
 * \brief A magic value set in the band cache band array to denote a
 * band which has never been sent to the PageBuffer device for
 * storage.
 */
#define BAND_NOT_PRESENT_IN_TABLE NULL

/**
 * \brief A magic value set in the band cache band array to denote a
 * band which has been flushed from memory to disk.
 */
#define BAND_SLOT_EVACUATED ((uint8 *)(intptr_t)-1)


/**
 * \brief A magic value set in the band index table to denote a band
 * which cannot yet be entered into the table, because it has not been
 * sent to the PageBuffer device for storage.
 */
#define NO_BAND_YET (0)


/**
 * \brief Structure holding memory for use by band cache.
 */
typedef struct BandMemory
{
  uint8 *mem_base;
  size_t nBands;
  size_t bandSize;

  BandHolder *pBandHolders;

  size_t cbBandMemory;
  uint8 * pBandMemory;

  size_t cbCompressionBuffer;
  uint8 * pCompressionBuffer;

  BandIndex * pBandIndex;

  size_t total;
} BandMemory;

static BandMemory gBandMemory;


/**
 * \brief Top-level structure representing a cache of bands.  One of
 * these is used if a partial paint is happening (see sections
 * 6.4 - 6.6 of the RIP Programmer's Reference Manual - HqnRIP_SDK_ProgRef.pdf).
 *
 * ppBands is set up as an array of BandHolders.  The
 * number of bands is known in advance, so the array does not need
 * resizing later.
 *
 * If a particular band has not yet been written to the cache, its
 * data pointer will be BAND_NOT_PRESENT_IN_TABLE.  If the band has
 * been written, but subsequently flushed to disk, its data pointer
 * will be set to the magic value BAND_SLOT_EVACUATED.
 *
 * The cache only holds bands for the current separation.
 */
typedef struct BandCache {
  BandHolder *pBands;  /**< Array of BandHolder elements.
                             Array index is the band number in the separation. */
  BandIndex * pBandIndex; /**< Array of BandIndex elements */
  uint32 numBands;        /**< Number of elements in above arrays. */
  uint32 numSeparations;  /**< Number of separations. */
  int32 separationIndex;  /**< Index of separation being cached */
  uint32 totalMemoryCacheSize; /**< Current total of compressed bytes
                                  added to the in-memory cache, over
                                  all bands.  This does not include
                                  any bands stored on disk. */
} BandCache;



/**
 * \brief The PGBDescription structure contains a RasterDescription,
 * which is the part exported to the raster callback, and some local
 * fields for keeping track of partial paints.
 */
typedef struct {
  RasterDescription rd;
  HqBool discard_data;        /**< Throw away data. */
  int32 n_bands_in_frame;     /**< Number of bands in a frame */
  int32 n_bands_in_page;      /**< Number of bands in the current page */
  int32 seek_line;            /**< The line seeked to by the RIP */
  int32 seek_band;            /**< The band seeked to by the RIP */
  int32 output_line;          /**< The next line to be output by the RIP */
  int32 band_size ;           /**< The size of a band */
  int32 n_frames_in_raster ;  /**< Number of frames per page */
  HqBool partial_painting;    /**< Is this a partial paint? */
  HqBool compositing;         /**< Is this a compositing paint? */
} PGBDescription ;

/**
 * \brief  Structure to hold device-specific state.
 */
typedef struct PGBDeviceState
{
  z_stream compress_stream;
} PGBDeviceState;

/**
 * \brief Full path to disk cache file name.
 */
static char * pszDiskCacheFullFileName = NULL;


/**
 * \brief If a partial paint is happening, a cache of in-memory bands
 * will be stored here.
 */
static BandCache * pBandCache = NULL;

/** \brief Total memory required to store partial paint separations. */
static size_t ppMemSize, ppMemPeak ;

/**
 * \brief Flag indicating whether the skin has provided a complete,
 * uncompressed framebuffer.
 *
 * If the skin has provided a framebuffer, it will be used for partial paints in
 * preference to the band cache.
 */
static HqBool skin_has_framebuffer;

/** An array of flags indicating if the band has been written by a partial paint.
 *
 * Array index is the band number in the separation. */
static HqBool *p_band_directory;

#ifdef TEST_FRAMEBUFFER

/** \brief The framebuffer. For test purposes only, we allocate a single
 * framebuffer per page.
 */
static unsigned char *gFrameBuffer = NULL;

/** \brief The size of gFrameBuffer */
static size_t gFrameBufferSize = 0;

#endif

/** \brief The page size in pixels.
 */
static unsigned int guPageHeightPixels = 0;

/** \brief The height in lines of each RIP band destined for the framebuffer,
 * if present.
 */
static unsigned int guBandHeight = 0;

/** \brief The number of bytes per line in each RIP band destined for the
 * framebuffer, if present.
 */
static unsigned int guBytesPerLine = 0;

/** \brief The number of color components in each RIP band destined for the
 * framebuffer, if present.
 */
static unsigned int guComponentsPerBand = 0;

/**
 * \brief Flag indicating whether to test framebuffer functionality.
 * It is set by SwLePgbUseFrameBuffer if the application has turned it on from
 * the command line. This is for Testing Only.
 */
static HqBool fUseFrameBuffer = FALSE;

#ifdef CHECKSUM

/**
 * \brief Declaration of (little-endian) checksum function exported by
 * HQNchecksum.
 */
extern uint32 HQNCALL HQCRCchecksum(uint32 crc, uint32 *data, int32 len);

/**
 * \brief Declaration of (big-endian) checksum function exported by
 * HQNchecksum.
 */
extern uint32 HQNCALL HQCRCchecksumreverse(uint32 crc, uint32 *data, int32 len);

/**
 * \brief Declared in kit.h, this states whether to print a page
 * checksum.  It may be switched on by client applications, e.g. via a
 * command-line switch.
 */
int32 fPrintChecksums = FALSE;

/**
 * \brief In a debug build, crc is used to store the checksum for the
 * whole image.  Depending on the endianness of the processor, and the
 * colorType of the page, HQCRCchecksum or HQCRCchecksumreverse needs
 * to be called.
 */
static uint32 crc = 0;

/**
 * \brief Type of the checksum functions.
 */
typedef uint32 (HQNCALL cfunc)( uint32, uint32 *, int32 );

/**
 * \brief Point this at HQCRCchecksum or HQCRCchecksumreverse.
 */
cfunc *crcfunc;


/**
 * \brief Decide which checksum function to use.
 */
static cfunc * find_checksum_func( int32 colorbits )
{
  uint32 word = 0xFF, *pword = &word;
  uint8 *pbyte = (uint8 *) pword;

  /* halftone rasters are always word oriented so
     unaffected by byte-swaps */
  if (colorbits == 1)
    return HQCRCchecksum;

  /* otherwise, check byte ordering of machine */
  if ( *pbyte == 0xFF ){ /* little endian */
    return HQCRCchecksum;
  }
  else { /* big endian */
    return HQCRCchecksumreverse;
  }
}
#endif


/* some forward declarations */
static HqBool instantiateBandMemory(RASTER_REQUIREMENTS *req);
static void destroyBandMemory(void);
static size_t totalBandMemory(RASTER_REQUIREMENTS *req, size_t *sizes);
static HqBool bandCache_New( BandCache ** ppValue,
                             int32 numBands,
                             int32 numSeparations );
static void initBandCache( PGBDescription *pPGB, HqBool first_time);
static void closeBandCache(void);
static void deleteDiskCacheFile(void);
static void closeDiskCacheFile(void);
static HqBool storeBandInCache( PGBDeviceState * pDeviceState,
                                PGBDescription * pPGB,
                                uint8 * pBuffer, uint32 length );
static HqBool getBand( PGBDescription *pPGB,
                       uint8 *pBuffer, uint32 *pExpectedLength );
static HqBool initDiskCacheFileName( const char * pszLeafDiskCacheFileName );
static HqBool updateDiskCache(void);

static HqBool KInitializePGBDescription(PGBDescription * pgb,
                                       const char *filename);
static int32 KParseColorant(DEVICEPARAM * param, HqBool *changedp);
static void KFreePGBDescription(PGBDescription * pgb);

static sw_tl_ref pgb_tl = SW_TL_REF_INVALID;

static DEVICELIST * pBandCacheDevice = NULL;

static int32 RIPCALL pgb_init_device(DEVICELIST *dev);
static DEVICE_FILEDESCRIPTOR RIPCALL pgb_open_file(DEVICELIST *dev,
                                                   uint8 *filename ,
                                                   int32 openflags);
static int32 RIPCALL pgb_read_file(DEVICELIST *dev,
                                   DEVICE_FILEDESCRIPTOR descriptor ,
                                   uint8 *buff ,
                                   int32 len);
static int32 RIPCALL pgb_write_file(DEVICELIST *dev,
                                    DEVICE_FILEDESCRIPTOR descriptor,
                                    uint8 *buff,
                                    int32 len );
static int32 RIPCALL pgb_close_file(DEVICELIST *dev,
                                    DEVICE_FILEDESCRIPTOR descriptor);
static int32 RIPCALL pgb_abort_file(DEVICELIST *dev,
                                    DEVICE_FILEDESCRIPTOR descriptor);
static int32 RIPCALL pgb_seek_file(DEVICELIST *dev,
                                   DEVICE_FILEDESCRIPTOR descriptor,
                                   Hq32x2 *destination ,
                                   int32 whence);
static int32 RIPCALL pgb_bytes_file(DEVICELIST *dev,
                                    DEVICE_FILEDESCRIPTOR descriptor,
                                    Hq32x2 *bytes,
                                    int32 reason);
static int32 RIPCALL pgb_status_file(DEVICELIST *dev,
                                     uint8 *filename ,
                                     STAT *statbuff );
static void* RIPCALL pgb_start_file_list(DEVICELIST *dev,
                                         uint8 *pattern);
static int32 RIPCALL pgb_next_file(DEVICELIST *dev,
                                   void **handle ,
                                   uint8 *pattern,
                                   FILEENTRY *entry);
static int32 RIPCALL pgb_end_file_list(DEVICELIST *dev,
                                       void * handle);
static int32 RIPCALL pgb_rename_file(DEVICELIST *dev,
                                     uint8 *file1 ,
                                     uint8 *file2);
static int32 RIPCALL pgb_delete_file(DEVICELIST *dev,
                                     uint8 *filename );
static int32 RIPCALL pgb_set_param(DEVICELIST *dev,
                                   DEVICEPARAM *param);
static int32 RIPCALL pgb_start_param(DEVICELIST *dev);
static int32 RIPCALL pgb_get_param(DEVICELIST *dev,
                                   DEVICEPARAM *param);
static int32 RIPCALL pgb_status_device(DEVICELIST *dev,
                                       DEVSTAT *devstat);
static int32 RIPCALL pgb_dismount_device(DEVICELIST *dev);
static int32 RIPCALL pgb_ioctl_device(DEVICELIST *dev,
                                      DEVICE_FILEDESCRIPTOR fileDescriptor,
                                      int32 opcode,
                                      intptr_t arg );

static int32 RIPCALL pgb_noerror(DEVICELIST *dev);
static int32 RIPCALL pgb_ioerror(DEVICELIST *dev);
static void* RIPCALL pgb_void(DEVICELIST *dev);
static int32 RIPCALL pgb_spare(void);

#ifdef TEST_FRAMEBUFFER
/**
 * \brief A callback function which specifically tests the framebuffer
 * implementation. It is local to the pgb device. It gives the skin the details of the
 * raster it's about to be handed, and allocate memory to contain it.
 * It sets pRasterRequirements->have_framebuffer to TRUE, if it can allocate
 * enough memory for a framebuffer.
 *
 * \param pRasterRequirements Structure providing details of the
 * raster which the RIP will be providing, and some parameters for the
 * skin to indicate to the RIP how it will proceed.
 *
 * \param fRenderingStarting A flag which is FALSE when this function
 * is being called as a result of a change to the page device which
 * means a difference in the raster structure, or TRUE when no further
 * such changes are possible and rendering is beginning very soon.
 */
static int32 KCallRasterFrameBufferRequirements(RASTER_REQUIREMENTS * pRasterRequirements,
                                                HqBool fRenderingStarting);

/**
 * \brief A callback function which specifically tests the framebuffer
 * implementation. It is local to the pgb device. It asks the skin to provide a memory
 * address range into which to render.
 *
 * \param pRasterDestination Structure providing the band number the
 * RIP is about to render, and pointers to the memory range that the
 * RIP should use.
 *
 * \param nFrameNumber The frame (separation) number.
 */
static int32 KCallRasterFrameBufferDestination(RASTER_DESTINATION * pRasterDestination,
                                               int32 nFrameNumber);
static void freePGBFrameBuffer(void);

#endif

DEVICETYPE PageBuffer_Device_Type = {
  PAGEBUFFER_DEVICE_TYPE, /**< The device ID number */
  0,                      /**< Flags to indicate specifics of device */
  CAST_SIZET_TO_INT32(sizeof(PGBDeviceState)), /**< The size of the private data */
  0,                      /**< Minimum ticks between tickle functions */
  NULL,                   /**< Procedure to service the device */
  skindevices_last_error, /**< Return last error for this device */
  pgb_init_device,        /**< Call to initialize device */
  pgb_open_file,          /**< Call to open file on device */
  pgb_read_file,          /**< Call to read data from file on device */
  pgb_write_file,         /**< Call to write data to file on device */
  pgb_close_file,         /**< Call to close file on device */
  pgb_abort_file,         /**< Call to abort action on the device */
  pgb_seek_file,          /**< Call to seek file on device */
  pgb_bytes_file,         /**< Call to get bytes avail on an open file */
  pgb_status_file,        /**< Call to check status of file */
  pgb_start_file_list,    /**< Call to start listing files */
  pgb_next_file,          /**< Call to get next file in list */
  pgb_end_file_list,      /**< Call to end listing */
  pgb_rename_file,        /**< Rename file on the device */
  pgb_delete_file,        /**< Remove file from device */
  pgb_set_param,          /**< Call to set device parameter */
  pgb_start_param,        /**< Call to start getting device parameters */
  pgb_get_param,          /**< Call to get the next device parameter */
  pgb_status_device,      /**< Call to get the status of the device */
  pgb_dismount_device,    /**< Call to dismount the device */
  pgb_ioerror,            /**< Call to return buffer size */
  pgb_ioctl_device,       /**< ioctl slot */
  pgb_spare,              /**< Spare slot */
};

/* ---------------------------------------------------------------------- */

/**
 * \brief The following structures will hold all of the device
 * parameters so that we can write it all in one go. After the type
 * definition is a static initialization of an instance of the
 * structure to provide suitable defaults for all the parameters;
 * these will be replaced as new values are received.
 *
 * The example does not process all the parameters set for the device;
 * it chooses to ignore some. This is important functionality since
 * new parameters wil be introduced from time to time and the code
 * should be written with upward compatibility in mind. It is unlikely
 * that the implementation will require all of the parameters which
 * are included here, so can be removed as appropriate. This example
 * makes direct use of very few, but they are present mainly as an
 * initial checklist.
 *
 * The system allows for you to add your own additional pagebuffer
 * parameters. This is also shown below (see also the file
 * Sys/ExtraPageDeviceKeys).
*/

typedef struct
{

  /* IMAGE INFORMATION PARAMETERS - see Programmer's Reference manual
     section 6.1.1 for details */

  int32 ImageWidth;       /**< width (in pixels) of one scanline of the
                             active part of the image (not including any
                             padding) */
  int32 ImageHeight;      /**< number of scanlines in the complete image */
  int32 LeftMargin;       /**< in pixels */
  int32 RightMargin;      /**< in pixels */
  int32 TopMargin;        /**< in pixels */
  int32 BottomMargin;     /**< in pixels */
  int32 XResolution;      /**< resolution (in dpi) in the x (width)
                             direction */
  float XResFrac;         /**< plus fractional part */
  int32 YResolution;      /**< resolution (in dpi) in the y (height)
                             direction */
  float YResFrac;
  int32 NegativePrint;    /**< image requires negation by the output recorder
                             or the pagebuffer implementation */

  /* CONTROL OF MEDIA  */

  int32 InputAttributes;  /**< The index of the media cassette selected
                             from among those in the InputAttributes
                             dictionary in the page device  */
  int32 OutputAttributes; /**< The index of the output destination selected
                             from among those in the OutputAttributes
                             dictionary in the page device  */
  int32 Exposure;         /**< output recorder exposure setting */
  int32 NumCopies;        /**< number of copies to print - will be 1 if
                             MultipleCopies below is FALSE */

  int32 InsertSheet;

  /* DATA DESCRIPTION PARAMETERS: how the data to be written is organised
     with respect to the image */

  uint8 JobName[512];
  uint8 JobNameEncoding[256];
  uint8 JobNameUTF8[512];

  int32 JobNumber;

  int32 DocumentNumber;

  int32 BandWidth;        /**< displacement in memory (measured in bits)
                             from the start of one scanline to the
                             next (including end of line padding) */
  int32 BandHeight;       /**< number of scanlines in each band - that
                             is, the number of scanlines in the data
                             delivered on each write call (except
                             possibly the last, which may be shorter) */
  int32 NumBands;         /**< the number of bands which will be delivered
                             before memory will be reused by the rip;
                             Note: NOT the total number of bands making
                             up a page */
  int32 Separation;       /**< sequence number of current (automatically
                             produced) separation of a page */
  int32 PageNumber;       /**< page number of current page (the page number
                             will only be changed when all the
                             (automatically produced) separations making
                             up a page have been delivered; separation
                             indexes these individually */
  int32 PrintPage;        /**< 0 is partial paint, 1 is really output,
                             2 (as yet unimplemented) will be output &
                             keep for copypage */
  int32 RunLength;        /**< TRUE if the RIP is generating RLE output format */
  int32 RunLineOutput;    /**< TRUE if RLE output includes the block headers. */
  int32 RunLineComplete;  /**< TRUE if next RLE write completes line. */

  int32 RasterDepth;      /**< bits per channel, 1, 8, 10 (RLE only), or 16 */

  int32 InterleavingStyle; /**< monochrome, pixel-, band- or frame-interleaved */

  int32 NumChannels;    /**< number of colorant channels. Note that some of these
                           may be blank, for example when separating to a band
                           interleaved device. also where the setup has
                           permitted extra spot colorants there may be more
                           channels than the fixed colorants originally
                           specified. note that for a monochrome device, this is
                           always 1 - it is _not_ the total number of
                           separations. */
  uint8 ColorantFamily[64];/**< the colorant family to which the colorants of this
                              raster belong */

  int32 NumColorants;
  RasterColorant *Colorant;
  int32 ffff;
  uint8 PageBufferType[MAX_PGB_TYPE_LENGTH];
                           /**< Stored in the RasterDescription, this allows
                                switching between different output backends.
                                The special value "None" is used at the
                                pgbdev level to discard data. */
  uint8 OutputTarget[MAX_OUTPUT_TARGET_LENGTH];
                           /**< Passed through to the back-end in the
                                RasterDescription, allowing switching between
                                different "devices" (or file output
                                methodologies). */
  int32 NumSeparations;

  int32 Duplex;       /**< Flag determining whether the output is to be printed
                           duplex or simplex. */
  int32 Tumble;       /**< Flag specifying the relative orientation of page images
                           on opposite sides of a sheet of medium in duplex output. */
  int32 Orientation;  /**< A code specifying the orientation of the page image. */
  int32 Collate;      /**< Flag specifying how the output is to be organized when
                           multiple copies are requested. */
  int32 OutputFaceUp; /**< Flag specifying the order pages are stacked in the
                           output tray. */

  uint8  PtfDocumentDuplex[PT_STRING_LENGTH]; /**< Whether PrintTicket specifies document duplex */
  uint8  PtfJobDuplex[PT_STRING_LENGTH];      /**< Whether PrintTicket specifies job duplex */
  uint8  PtfMediaType[PT_STRING_LENGTH];      /**< Demo of what PrintTicket specifies for page media type */
  uint8  PtfPageOutputColor[PT_STRING_LENGTH];/**< PrintTicket-specified page output color */
  uint32 uPtfOutputDeviceBPP;   /**< Demo of what PrintTicket specifies for page output device bits per pixel */
  uint32 uPtfOutputDriverBPP;   /**< Demo of what PrintTicket specifies for page output driver bits per pixel */
  uint8  uPtfOutputQuality[PT_STRING_LENGTH]; /**< Demo of what PrintTicket specifies for page output quality */
  int32  PtfPageFaceUp; /**< Boolean to keep track of emulated page duplex */
  int32  PtfPageCopyCount; /**< Number of copies of each page as specified in the PrintTicket */
  int32  PtfDocumentCopiesAllPages; /**< Number of copies of each document as specified in the PrintTicket */
  int32  PtfJobCopiesAllDocuments; /**< Number of copies of each job as specified in the PrintTicket */
  int32  PtfSystemErrorLevel;      /**< System error report levels */
  int32  PtfEmulateXPSPT;          /**< Emulate XPS PrintTicket in output */
  uint8  PtfJobOutputOptimization[PT_STRING_LENGTH];   /**< PrintTicket-specified */
  uint8  PtfPagePhotoPrintingIntent[PT_STRING_LENGTH]; /**< PrintTicket-specified */
  uint8  PtfJobHolePunch[PT_STRING_LENGTH];
  uint8  PtfDocumentHolePunch[PT_STRING_LENGTH];
  uint8  PtfJobRollCutAtEndOfJob[PT_STRING_LENGTH];
  uint8  PtfDocumentRollCut[PT_STRING_LENGTH];
  uint8  PtfJobStapleAllDocuments[PT_STRING_LENGTH];
  uint8  PtfDocumentStaple[PT_STRING_LENGTH];
  int32  PtfStapleAngle;
  int32  PtfStapleSheetCapacity;
  uint8  PtfPageBorderless[PT_STRING_LENGTH];
  uint8  PtfJobDeviceLanguage[PT_STRING_LENGTH];
  uint8  PtfJobDeviceLanguage_Level[PT_STRING_LENGTH];
  uint8  PtfJobDeviceLanguage_Encoding[PT_STRING_LENGTH];
  uint8  PtfJobDeviceLanguage_Version[PT_STRING_LENGTH];
  uint8  PtfOutputBin[PT_STRING_LENGTH];
  int32  PtfJobCollateAllDocuments; /**< Whether to collate at job-level (boolean) */
  int32  PtfDocumentCollate; /**< Whether to collate at document-level (boolean) */

  int32 fFavorSpeedOverCompression; /**< Governs how band data is compressed. */
  int32 fAllowBandCompression; /**< Governs whether band data is compressed at all. */
  int32 fDynamicBands; /**< Controls whether the RIP allocates extra bands (memory permitting) ahead of output. */
  int32 packingUnit; /**< Blit packing size. */

  uint32 OptimizedPDFUsageCount;
  uint8  OptimizedPDFId[PDF_ID_LENGTH];
  int32 BandLines ; /**< Number of lines in this band */
  int32 SeparationId ; /**< Omission-independent separation id. */
  uint8 PGBSysmem;     /**< PGB device uses system memory. */
} PageBufferInParameters;

typedef struct
{
  /* READ PARAMETERS: determined by the pagebuffer device for itself to let
     the rip know about its characteristics - see PR 6.1.2 */

  int32 BandsCopied;      /**< Bands disposed of by pagebuffer device */

  int32 WriteAllOutput;   /**< whether to omit blank areas of image or not */
  int32 MultipleCopies;   /**< whether this implementation can do its
                             own multiple copies of the image, or whether
                             the rip has to do this for it */
  int32 PrinterStatus;    /**< number to communicate error condition of
                             printer back to PostScript */
  uint8 PrinterMessage[32]; /**< message to communicate condition of printer
                                back to PostScript */
  /* MaxBandSize omitted: let the rip choose for itself */

} PageBufferOutParameters;

/* ----------------------------------------------------------------------
 * Now the default values:
 */

static PageBufferInParameters pgbinparams =
{
  0, 0,                   /**< ImageWidth, ImageHeight */
  0, 0, 0, 0,             /**< LeftMargin etc */
  300, 0.0f, 300, 0.0f,   /**< X, YResolution */
  FALSE,                  /**< NegativePrint */
  0, 0,                   /**< Input & OutputAttributes */
  0,                      /**< Exposure */
  1,                      /**< NumCopies */
  FALSE,                  /**< InsertSheet */
  "",                     /**< JobName */
  "und",                  /**< JobNameEncoding */
  "",                     /**< JobNameUTF8 */
  0, 0,                   /**< JobNumber, DocumentNumber */
  0, 0,                   /**< BandWidth & Height */
  1,                      /**< NumBands */
  1, 1,                   /**< Separation, PageNumber */
  FALSE,                  /**< PrintPage */
  FALSE,                  /**< RunLength */
  TRUE,                   /**< RunLineOutput */
  TRUE,                   /**< RunLineComplete */
  1,                      /**< RasterDepth */
  interleavingStyle_monochrome, /**< InterleavingStyle */
  1,                      /**< NumChannels */
  "",                     /**< ColorantFamily */
  0,                      /**< NumColorants */
  NULL,                   /**< Colorant */
  0,                      /**< FFFF */
  "",                     /**< PageBufferType starts empty */
  "FILE",                 /**< OutputTarget */
  0,                      /**< NumSeparations */
  FALSE,                  /**< Duplex */
  FALSE,                  /**< Tumble */
  0,                      /**< Orientation */
  TRUE,                   /**< Collate */
  FALSE,                  /**< OutputFaceUp */
  "",                     /* PtfDocumentDuplex */
  "",                     /* PtfJobDuplex */
  "",                     /* PtfMediaType */
  "",                     /* PtfPageOutputColor */
  0,                      /* uPtfOutputDeviceBPP */
  0,                      /* uPtfOutputDriverBPP */
  "",                     /* uPtfOutputQuality */
  TRUE,                   /* PtfPageFaceUp */
  1,                      /* PtfPageCopyCount */
  1,                      /* PtfDocumentCopiesAllPages */
  1,                      /* PtfJobCopiesAllDocuments */
  0,                      /* PtfSystemErrorLevel */
  FALSE,                  /* PtfEmulateXPSPT */
  "",                     /* PtfJobOutputOptimization */
  "",                     /* PtfPagePhotoPrintingIntent */
  "",                     /* PtfJobHolePunch */
  "",                     /* PtfDocumentHolePunch */
  "",                     /* PtfJobRollCutAtEndOfJob */
  "",                     /* PtfDocumentRollCut */
  "",                     /* PtfJobStapleAllDocuments */
  "",                     /* PtfDocumentStaple */
  0,                      /* PtfStapleAngle */
  0,                      /* PtfStapleSheetCapacity */
  "",                     /* PtfPageBorderless */
  "",                     /* PtfJobDeviceLanguage */
  "",                     /* PtfJobDeviceLanguage_Level */
  "",                     /* PtfJobDeviceLanguage_Encoding */
  "",                     /* PtfJobDeviceLanguage_Version */
  "",                     /* PtfOutputBin */
  FALSE,                  /* PtfJobCollateAllDocuments */
  FALSE,                  /* PtfDocumentCollate */

  TRUE,                   /* fFavorSpeedOverCompression */
  TRUE,                   /* fAllowBandCompression */
  FALSE,                  /* fDynamicBands */
  0,                      /* packingUnit */
  0,                      /* OptimizedPDFUsageCount */
  "",                     /* OptimizedPDFId */
  0,                      /* BandLines */
  0,                      /* SeparationId */
  FALSE,                  /* PGBSysmem */
};

static PGBDescription *g_pgb = NULL;

static PageBufferOutParameters pgboutparams =
{
  0,                      /**< BandsCopied */
  FALSE,                  /**< WriteAllOutput */
  FALSE,                  /**< MultipleCopies */
  0x7FFFFF80,             /**< PrinterStatus */
  "Unknown Error"         /**< Printer Message */
};

/* Colorant dictionary keys */
static char aszColorant[] = "Colorant";
static char aszColorantName[] = "ColorantName";
static char aszsRGB[] = "sRGB";
static char aszChannel[] = "Channel";


static PARAM devparams[] =
{
#define RUNLINECOMPLETE_INDEX 0
  /* This param is first because it is set for every RLE block write, so
     needs to be accessed fast. */
  {
    "RunLineComplete", 0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamBoolean, & pgbinparams.RunLineComplete, 0, 1
  },

#define IMAGEWIDTH_INDEX (RUNLINECOMPLETE_INDEX + 1)
  {
    "ImageWidth", 0,    PARAM_WRITEABLE | PARAM_RANGE,
    ParamInteger, & pgbinparams.ImageWidth,  0, MAXINT
  },
#define IMAGEHEIGHT_INDEX (IMAGEWIDTH_INDEX + 1)
  {
    "ImageHeight", 0,   PARAM_WRITEABLE | PARAM_RANGE,
    ParamInteger, & pgbinparams.ImageHeight , 0 , MAXINT
  },
#define LEFTMARGIN_INDEX (IMAGEHEIGHT_INDEX + 1)
  {
    "LeftMargin", 0,    PARAM_WRITEABLE,
    ParamInteger, & pgbinparams.LeftMargin, 0, 0
  },
#define RIGHTMARGIN_INDEX (LEFTMARGIN_INDEX + 1)
  {
    "RightMargin", 0,   PARAM_WRITEABLE,
    ParamInteger, & pgbinparams.RightMargin, 0, 0
  },
#define TOPMARGIN_INDEX (RIGHTMARGIN_INDEX + 1)
  {
    "TopMargin", 0,     PARAM_WRITEABLE,
    ParamInteger, & pgbinparams.TopMargin, 0, 0
  },
#define BOTTOMMARGIN_INDEX (TOPMARGIN_INDEX + 1)
  {
    "BottomMargin", 0,  PARAM_WRITEABLE,
    ParamInteger, & pgbinparams.BottomMargin, 0, 0
  },
#define XRESOLUTION_INDEX (BOTTOMMARGIN_INDEX + 1)
  {
    "XResolution", 0,   PARAM_WRITEABLE | PARAM_RANGE,
    ParamInteger, & pgbinparams.XResolution, 0, MAXINT
  },
#define XRESOLUTIONFRAC_INDEX (XRESOLUTION_INDEX + 1)
  {
    "XResFrac", 0,   PARAM_WRITEABLE | PARAM_RANGE,
    ParamFloat, & pgbinparams.XResFrac, 0, 1
  },
#define YRESOLUTION_INDEX (XRESOLUTIONFRAC_INDEX + 1)
  {
    "YResolution", 0,   PARAM_WRITEABLE | PARAM_RANGE,
    ParamInteger, & pgbinparams.YResolution, 0, MAXINT
  },
#define YRESOLUTIONFRAC_INDEX (YRESOLUTION_INDEX + 1)
  {
    "YResFrac", 0,   PARAM_WRITEABLE | PARAM_RANGE,
    ParamFloat, & pgbinparams.YResFrac, 0, 1
  },
#define NEGATIVEPRINT_INDEX (YRESOLUTIONFRAC_INDEX + 1)
  {
    "NegativePrint", 0, PARAM_WRITEABLE,
    ParamBoolean, & pgbinparams.NegativePrint, 0, 0
  },
#define INPUTATTRIBUTES_INDEX (NEGATIVEPRINT_INDEX + 1)
  {
    "InputAttributes",  0,  PARAM_WRITEABLE,
    ParamInteger, & pgbinparams.InputAttributes, 0, 0
  },
#define OUTPUTATTRIBUTES_INDEX (INPUTATTRIBUTES_INDEX + 1)
  {
    "OutputAttributes", 0,  PARAM_WRITEABLE,
    ParamInteger, & pgbinparams.OutputAttributes, 0, 0
  },
#define NUMCOPIES_INDEX (OUTPUTATTRIBUTES_INDEX + 1)
  {
    "NumCopies", 0,     PARAM_WRITEABLE | PARAM_SET | PARAM_RANGE,
    ParamInteger, & pgbinparams.NumCopies, 0, MAXINT
  },
#define INSERTSHEET_INDEX (NUMCOPIES_INDEX + 1)
  {
    "InsertSheet", 0,     PARAM_WRITEABLE,
    ParamBoolean, & pgbinparams.InsertSheet, 0, 0
  },
#define JOBNAME_INDEX (INSERTSHEET_INDEX + 1)
  {
    "JobName", 0,       PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, pgbinparams.JobName, 0, sizeof(pgbinparams.JobName) - 1
  },
#define JOBNAMEENCODING_INDEX (JOBNAME_INDEX + 1)
  {
    "JobNameEncoding", 0,       PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, pgbinparams.JobNameEncoding, 0, sizeof(pgbinparams.JobNameEncoding) - 1
  },
#define JOBNAMEUTF8_INDEX (JOBNAMEENCODING_INDEX + 1)
  {
    "JobNameUTF8", 0,       PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, pgbinparams.JobNameUTF8, 0, sizeof(pgbinparams.JobNameUTF8) - 1
  },
#define JOBNUMBER_INDEX (JOBNAMEUTF8_INDEX + 1)
  {
    "JobNumber", 0,       PARAM_WRITEABLE,
    ParamInteger, & pgbinparams.JobNumber, 0, 0
  },
#define BANDWIDTH_INDEX (JOBNUMBER_INDEX + 1)
  {
    "BandWidth", 0,     PARAM_WRITEABLE | PARAM_RANGE,
    ParamInteger, & pgbinparams.BandWidth,  0, MAXINT
  },
#define BANDHEIGHT_INDEX (BANDWIDTH_INDEX + 1)
  {
    "BandHeight", 0,    PARAM_WRITEABLE | PARAM_RANGE,
    ParamInteger, & pgbinparams.BandHeight, 0, MAXINT
  },
#define NUMBANDS_INDEX (BANDHEIGHT_INDEX + 1)
  {
    "NumBands", 0,      PARAM_WRITEABLE | PARAM_RANGE,
    ParamInteger, & pgbinparams.NumBands, 1, MAXINT
  },
#define SEPARATION_INDEX (NUMBANDS_INDEX + 1)
  {
    "Separation", 0,    PARAM_WRITEABLE,
    ParamInteger, & pgbinparams.Separation, 0, 0
  },
#define PAGENUMBER_INDEX (SEPARATION_INDEX + 1)
  {
    "PageNumber", 0,    PARAM_WRITEABLE | PARAM_SET | PARAM_RANGE,
    ParamInteger, & pgbinparams.PageNumber, 0, MAXINT
  },
#define DOCUMENTNUMBER_INDEX (PAGENUMBER_INDEX + 1)
  {
    "DocumentNumber", 0, PARAM_WRITEABLE | PARAM_SET | PARAM_RANGE,
    ParamInteger, & pgbinparams.DocumentNumber, 0, MAXINT
  },
#define PRINTPAGE_INDEX (DOCUMENTNUMBER_INDEX + 1)
  {
    "PrintPage", 0,     PARAM_WRITEABLE,
    ParamBoolean, & pgbinparams.PrintPage, 0, 0
  },
#define RUNLENGTH_INDEX (PRINTPAGE_INDEX + 1)
  {
    "RunLength", 0,     PARAM_WRITEABLE,
    ParamBoolean, & pgbinparams.RunLength, 0, 0
  },
#define RASTERDEPTH_INDEX (RUNLENGTH_INDEX + 1)
  {
    "RasterDepth", 0,     PARAM_WRITEABLE | PARAM_RANGE,
    ParamInteger, & pgbinparams.RasterDepth, 0, 16
  },
#define INTERLEAVINGSTYLE_INDEX (RASTERDEPTH_INDEX + 1)
  {
    "InterleavingStyle", 0,     PARAM_WRITEABLE | PARAM_RANGE,
    ParamInteger, & pgbinparams.InterleavingStyle, 1, 4
  },
#define NUMCHANNELS_INDEX (INTERLEAVINGSTYLE_INDEX + 1)
  {
    "NumChannels", 0,     PARAM_WRITEABLE,
    ParamInteger, & pgbinparams.NumChannels, 0, 0
  },
#define COLORANTFAMILY_INDEX (NUMCHANNELS_INDEX + 1)
  {
    "ColorantFamily", 0,       PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, pgbinparams.ColorantFamily,  0, sizeof(pgbinparams.ColorantFamily) - 1
  },
#define NUMCOLORANTS_INDEX (COLORANTFAMILY_INDEX + 1)
  {
    "NumColorants", 0,     PARAM_WRITEABLE,
    ParamInteger, & pgbinparams.NumColorants, 0, 0
  },
#define COLORANT_INDEX (NUMCOLORANTS_INDEX + 1)
  {
    aszColorant, 0, PARAM_WRITEABLE,
    ParamDict, &pgbinparams.Colorant, 0, 0
  },
#define FFFF_INDEX (COLORANT_INDEX + 1)
  {
    "FFFF", 0,    PARAM_WRITEABLE | PARAM_SET | PARAM_RANGE,
    ParamInteger, & pgbinparams.ffff, 0, MAXINT
  },
#define WRITEALLOUTPUT_INDEX (FFFF_INDEX + 1)
  {
    "WriteAllOutput",   0, PARAM_READONLY,
    ParamBoolean, & pgboutparams.WriteAllOutput, 0, 0
  },
#define MULTIPLECOPIES_INDEX (WRITEALLOUTPUT_INDEX + 1)
  {
    "MultipleCopies",   0, PARAM_READONLY | PARAM_SET,
    ParamBoolean, & pgboutparams.MultipleCopies, 0, 0
  },
#define PRINTERSTATUS_INDEX (MULTIPLECOPIES_INDEX + 1)
  {
    "PrinterStatus", 0, PARAM_READONLY | PARAM_SET,
    ParamInteger, & pgboutparams.PrinterStatus, 0, MAXINT
  },
#define PRINTERMESSAGE_INDEX (PRINTERSTATUS_INDEX + 1)
  {
    "PrinterMessage",   0, PARAM_READONLY | PARAM_SET,
    ParamString,  pgboutparams.PrinterMessage,
    0, sizeof(pgboutparams.PrinterMessage)-1
  },
#define PAGEBUFFERTYPE_INDEX (PRINTERMESSAGE_INDEX + 1)
  {
    "PageBufferType",   0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString,  pgbinparams.PageBufferType,
    0, sizeof(pgbinparams.PageBufferType)-1
  },
#define OUTPUTTARGET_INDEX (PAGEBUFFERTYPE_INDEX + 1)
  {
    "OutputTarget",   0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString,  pgbinparams.OutputTarget,
    0, sizeof(pgbinparams.OutputTarget)-1
  },
#define NUMSEPARATIONS_INDEX (OUTPUTTARGET_INDEX + 1)
  {
    "NumSeparations", 0,     PARAM_WRITEABLE,
    ParamInteger, & pgbinparams.NumSeparations, 0, 0
  },
#define BANDSCOPIED_INDEX (NUMSEPARATIONS_INDEX + 1)
  {
    "BandsCopied",   0, PARAM_READONLY | PARAM_SET,
    ParamInteger, & pgboutparams.BandsCopied, 0, 0
  },
#define PTF_DOCUMENTDUPLEX_INDEX (BANDSCOPIED_INDEX + 1)
  {
    "PtfDocumentDuplex",    0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, & pgbinparams.PtfDocumentDuplex,
    0, sizeof(pgbinparams.PtfDocumentDuplex)-1
  },
#define PTF_JOBDUPLEX_INDEX (PTF_DOCUMENTDUPLEX_INDEX + 1)
  {
    "PtfJobDuplex",    0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, & pgbinparams.PtfJobDuplex,
    0, sizeof(pgbinparams.PtfJobDuplex)-1
  },
#define PTF_PAGEMEDIATYPE_INDEX (PTF_JOBDUPLEX_INDEX + 1)
  {
    "PtfMediaType",    0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, & pgbinparams.PtfMediaType,
    0, sizeof(pgbinparams.PtfMediaType)-1
  },
#define PTF_PAGEOUTPUTCOLOR_INDEX (PTF_PAGEMEDIATYPE_INDEX + 1)
  {
    "PtfPageOutputColor",    0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, & pgbinparams.PtfPageOutputColor,
    0, sizeof(pgbinparams.PtfPageOutputColor)-1
  },
#define PTF_OUTPUTDEVICEBPP_INDEX (PTF_PAGEOUTPUTCOLOR_INDEX + 1)
  {
    "PtfOutputDeviceBPP",    0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamInteger, & pgbinparams.uPtfOutputDeviceBPP, 0, MAXINT
    /* note that bits per pixel range is a device-specific value */
  },
#define PTF_OUTPUTDRIVERBPP_INDEX (PTF_OUTPUTDEVICEBPP_INDEX + 1)
  {
    "PtfOutputDriverBPP",    0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamInteger, & pgbinparams.uPtfOutputDriverBPP, 0, MAXINT
    /* note that bits per pixel range is a device-specific value */
  },
#define PTF_PAGEOUTPUTQUALITY_INDEX (PTF_OUTPUTDRIVERBPP_INDEX + 1)
  {
    "PtfOutputQuality",    0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, & pgbinparams.uPtfOutputQuality,
    0, sizeof(pgbinparams.uPtfOutputQuality)-1
  },
#define PTF_PAGEFACEUP_INDEX (PTF_PAGEOUTPUTQUALITY_INDEX + 1)
  {
    "PtfPageFaceUp",    0, PARAM_WRITEABLE,
    ParamBoolean, & pgbinparams.PtfPageFaceUp, 0, 0
  },
#define PTF_PAGECOPYCOUNT_INDEX (PTF_PAGEFACEUP_INDEX + 1)
  {
    "PtfPageCopyCount", 0, PARAM_WRITEABLE | PARAM_SET | PARAM_RANGE,
    ParamInteger, & pgbinparams.PtfPageCopyCount, 1, MAXINT
  },
#define PTF_DOCUMENTCOPYCOUNT_INDEX (PTF_PAGECOPYCOUNT_INDEX + 1)
  {
    "PtfDocumentCopiesAllPages", 0, PARAM_WRITEABLE | PARAM_SET | PARAM_RANGE,
    ParamInteger, & pgbinparams.PtfDocumentCopiesAllPages, 1, MAXINT
  },
#define PTF_JOBCOPYCOUNT_INDEX (PTF_DOCUMENTCOPYCOUNT_INDEX + 1)
  {
    "PtfJobCopiesAllDocuments", 0, PARAM_WRITEABLE | PARAM_SET | PARAM_RANGE,
    ParamInteger, & pgbinparams.PtfJobCopiesAllDocuments, 1, MAXINT
  },
#define PTF_SYSTEMERROR_LEVEL_INDEX (PTF_JOBCOPYCOUNT_INDEX + 1)
  {
    "PtfSystemErrorLevel", 0, PARAM_WRITEABLE | PARAM_SET | PARAM_RANGE,
    ParamInteger, & pgbinparams.PtfSystemErrorLevel,  0, 2
  },
#define PTF_EMULATEXPSPT_INDEX (PTF_SYSTEMERROR_LEVEL_INDEX + 1)
  {
    "PtfEmulateXPSPT", 0, PARAM_WRITEABLE,
    ParamBoolean, & pgbinparams.PtfEmulateXPSPT,  0, 0
  },
#define PTF_JOBOUTPUTOPTIMIZATION_INDEX (PTF_EMULATEXPSPT_INDEX + 1)
  {
    "PtfJobOutputOptimization",    0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, & pgbinparams.PtfJobOutputOptimization,
    0, sizeof(pgbinparams.PtfJobOutputOptimization)-1
  },
#define PTF_PAGEPHOTOPRINTINGINTENT_INDEX (PTF_JOBOUTPUTOPTIMIZATION_INDEX + 1)
  {
    "PtfPagePhotoPrintingIntent",    0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, & pgbinparams.PtfPagePhotoPrintingIntent,
    0, sizeof(pgbinparams.PtfPagePhotoPrintingIntent)-1
  },
#define PTF_JOBHOLEPUNCH_INDEX (PTF_PAGEPHOTOPRINTINGINTENT_INDEX + 1)
{
  "PtfJobHolePunch", 0, PARAM_WRITEABLE | PARAM_RANGE,
  ParamString, & pgbinparams.PtfJobHolePunch,
  0, sizeof(pgbinparams.PtfJobHolePunch)-1
},
#define PTF_DOCUMENTHOLEPUNCH_INDEX (PTF_JOBHOLEPUNCH_INDEX + 1)
  {
    "PtfDocumentHolePunch", 0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, & pgbinparams.PtfDocumentHolePunch,
    0, sizeof(pgbinparams.PtfDocumentHolePunch)-1
  },
#define PTF_JOBROLLCUTATENDOFJOB_INDEX (PTF_DOCUMENTHOLEPUNCH_INDEX + 1)
  {
    "PtfJobRollCutAtEndOfJob", 0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, & pgbinparams.PtfJobRollCutAtEndOfJob,
    0, sizeof(pgbinparams.PtfJobRollCutAtEndOfJob)-1
  },
#define PTF_DOCUMENTROLLCUT_INDEX (PTF_JOBROLLCUTATENDOFJOB_INDEX + 1)
  {
    "PtfDocumentRollCut", 0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, & pgbinparams.PtfDocumentRollCut,
    0, sizeof(pgbinparams.PtfDocumentRollCut)-1
  },
#define PTF_JOBSTAPLEALLDOCUMENTS_INDEX (PTF_DOCUMENTROLLCUT_INDEX + 1)
  {
    "PtfJobStapleAllDocuments", 0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, & pgbinparams.PtfJobStapleAllDocuments,
    0, sizeof(pgbinparams.PtfJobStapleAllDocuments)-1
  },
#define PTF_DOCUMENTSTAPLE_INDEX (PTF_JOBSTAPLEALLDOCUMENTS_INDEX + 1)
  {
    "PtfDocumentStaple", 0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, & pgbinparams.PtfDocumentStaple,
    0, sizeof(pgbinparams.PtfDocumentStaple)-1
  },
#define PTF_STAPLEANGLE_INDEX (PTF_DOCUMENTSTAPLE_INDEX + 1)
  {
    "PtfStapleAngle", 0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamInteger, & pgbinparams.PtfStapleAngle, 0, MAXINT
  },
#define PTF_STAPLESHEETCAPACITY_INDEX (PTF_STAPLEANGLE_INDEX + 1)
  {
    "PtfStapleSheetCapacity", 0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamInteger, & pgbinparams.PtfStapleSheetCapacity, 0, MAXINT
  },
#define PTF_PAGEBORDERLESS_INDEX (PTF_STAPLESHEETCAPACITY_INDEX + 1)
  {
    "PtfPageBorderless", 0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, & pgbinparams.PtfPageBorderless,
    0, sizeof(pgbinparams.PtfPageBorderless)-1
  },
#define PTF_JOBDEVICELANGUAGE_INDEX (PTF_PAGEBORDERLESS_INDEX + 1)
  {
    "PtfJobDeviceLanguage", 0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, & pgbinparams.PtfJobDeviceLanguage,
    0, sizeof(pgbinparams.PtfJobDeviceLanguage)-1
  },
#define PTF_JOBDEVICELANGUAGE_LEVEL_INDEX (PTF_JOBDEVICELANGUAGE_INDEX + 1)
  {
    "PtfJobDeviceLanguage_Level", 0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, & pgbinparams.PtfJobDeviceLanguage_Level,
    0, sizeof(pgbinparams.PtfJobDeviceLanguage_Level)-1
  },
#define PTF_JOBDEVICELANGUAGE_ENCODING_INDEX (PTF_JOBDEVICELANGUAGE_LEVEL_INDEX + 1)
  {
    "PtfJobDeviceLanguage_Encoding", 0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, & pgbinparams.PtfJobDeviceLanguage_Encoding,
    0, sizeof(pgbinparams.PtfJobDeviceLanguage_Encoding)-1
  },
#define PTF_JOBDEVICELANGUAGE_VERSION_INDEX (PTF_JOBDEVICELANGUAGE_ENCODING_INDEX + 1)
  {
    "PtfJobDeviceLanguage_Version", 0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, & pgbinparams.PtfJobDeviceLanguage_Version,
    0, sizeof(pgbinparams.PtfJobDeviceLanguage_Version)-1
  },
#define PTF_OUTPUTBIN_INDEX (PTF_JOBDEVICELANGUAGE_VERSION_INDEX + 1)
  {
    "PtfOutputBin", 0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, & pgbinparams.PtfOutputBin,
    0, sizeof(pgbinparams.PtfOutputBin)-1
  },
#define PTF_JOBCOLLATEALLDOCUMENTS_INDEX (PTF_OUTPUTBIN_INDEX + 1)
  {
    "PtfJobCollateAllDocuments", 0, PARAM_WRITEABLE | PARAM_SET,
    ParamBoolean, & pgbinparams.PtfJobCollateAllDocuments, 0, 0
  },
#define PTF_DOCUMENTCOLLATE_INDEX (PTF_JOBCOLLATEALLDOCUMENTS_INDEX + 1)
  {
    "PtfDocumentCollate", 0, PARAM_WRITEABLE | PARAM_SET,
    ParamBoolean, & pgbinparams.PtfDocumentCollate, 0, 0
  },
#define FAVOR_SPEED_OVER_COMPRESSION_INDEX (PTF_DOCUMENTCOLLATE_INDEX + 1)
  {
    "FavorSpeedOverCompression", 0, PARAM_WRITEABLE | PARAM_SET,
    ParamBoolean, & pgbinparams.fFavorSpeedOverCompression, 0, 0
  },
#define DUPLEX_INDEX (FAVOR_SPEED_OVER_COMPRESSION_INDEX + 1)
  {
    "Duplex", 0, PARAM_WRITEABLE,
    ParamBoolean, & pgbinparams.Duplex, 0, 0
  },
#define TUMBLE_INDEX (DUPLEX_INDEX + 1)
  {
    "Tumble", 0, PARAM_WRITEABLE,
    ParamBoolean, & pgbinparams.Tumble, 0, 0
  },
#define ORIENTATION_INDEX (TUMBLE_INDEX + 1)
  {
    "Orientation", 0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamInteger, & pgbinparams.Orientation, 0, 3
  },
#define COLLATE_INDEX (ORIENTATION_INDEX + 1)
  {
    "Collate", 0, PARAM_WRITEABLE,
    ParamBoolean, & pgbinparams.Collate, 0, 0
  },
#define OUTPUTFACEUP_INDEX (COLLATE_INDEX + 1)
  {
    "OutputFaceUp", 0, PARAM_WRITEABLE,
    ParamBoolean, & pgbinparams.OutputFaceUp, 0, 0
  },
#define ALLOW_BAND_COMPRESSION_INDEX (OUTPUTFACEUP_INDEX + 1)
  {
    "AllowBandCompression", 0, PARAM_WRITEABLE | PARAM_SET,
    ParamBoolean, & pgbinparams.fAllowBandCompression, 0, 0
  },
#define DYNAMICBANDS_INDEX (ALLOW_BAND_COMPRESSION_INDEX + 1)
  {
    "DynamicBands", 0, PARAM_WRITEABLE | PARAM_SET,
    ParamBoolean, & pgbinparams.fDynamicBands, 0, 0
  },

#define PACKINGUNITBITS_INDEX (DYNAMICBANDS_INDEX + 1)
  {
    "PackingUnitBits", 0, PARAM_WRITEABLE,
    ParamInteger, & pgbinparams.packingUnit, 0, 0
  },

#define RUNLINEOUTPUT_INDEX (PACKINGUNITBITS_INDEX + 1)
  {
    "RunLineOutput", 0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamBoolean, & pgbinparams.RunLineOutput, 1, 1
  },

#define OPTIMIZEDPDFUSAGECOUNT_INDEX (RUNLINEOUTPUT_INDEX + 1)
  {
    "OptimizedPDFUsageCount", 0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamInteger, & pgbinparams.OptimizedPDFUsageCount,  0, MAXINT
  },

#define OPTIMIZEDPDFID_INDEX (OPTIMIZEDPDFUSAGECOUNT_INDEX + 1)
  {
    "OptimizedPDFId",    0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, & pgbinparams.OptimizedPDFId,
    0, sizeof(pgbinparams.OptimizedPDFId)-1
  },

#define BANDLINES_INDEX (OPTIMIZEDPDFID_INDEX + 1)
  {
    "BandLines",    0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamInteger, & pgbinparams.BandLines,
    1, MAXINT
  },

#define SEPARATIONID_INDEX (BANDLINES_INDEX + 1)
  {
    "SeparationId", 0,    PARAM_WRITEABLE,
    ParamInteger, & pgbinparams.SeparationId, 0, 0
  },

#define PGB_SYSMEM (SEPARATIONID_INDEX + 1)
  {
    "PGBSysmem",   0, PARAM_WRITEABLE,
    ParamBoolean, & pgbinparams.PGBSysmem, 0, 0
  },
};

/** \brief Number of parameters in devparams array */
#define MAX_PARAMS    (sizeof(devparams)/sizeof(devparams[0]))

static uint32 param_order[MAX_PARAMS];

/** \brief Test whether a parameter has been set */
#define PARAM_IS_SET(_index_)  ((devparams[_index_].flags & PARAM_SET) != 0)

int32 SwLePgbSetCallback(const char *name, int32 type,
                         SwLeParamCallback *pfnPGBCallback)
{
  return KParamSetCallback(devparams, MAX_PARAMS, name, type, pfnPGBCallback) ;
}

/**
 * \brief Will be set to TRUE by SwLePgbSetOverrideToNone if the
 * application has, from the command line, overridden the PostScript
 * configuration of the PageBuffer type to be "None".  FALSE
 * otherwise.
 */
static int32 override_to_none = FALSE;

void SwLePgbSetOverrideToNone(void)
{
  override_to_none = TRUE;
}


void SwLePgbSetMultipleCopies( int32 fFlag )
{
  pgboutparams.MultipleCopies = fFlag;
}


void SwLePgbUseFrameBuffer( int32 fFlag )
{
  fUseFrameBuffer = fFlag;
}

/* ------------------------------------------------------------------------- */
/* Timeline-end event handler. Used to detect end-of-job */
static sw_event_result HQNCALL pgb_endjob(void *context, sw_event *event)
{
  SWMSG_TIMELINE *msg = event->message;

  UNUSED_PARAM(void *, context);

  if ( msg == NULL || event->length != sizeof(SWMSG_TIMELINE) )
    return SW_EVENT_ERROR;

  if ( msg->type == SWTLT_JOB && g_pgb != NULL ) {
    /* Tell the device that we've reached the end of a job. */
    (void)KCallRasterCallback(&g_pgb->rd, NULL);
#ifdef TEST_FRAMEBUFFER
    if ( skin_has_framebuffer && pgbinparams.PrintPage > 0 ) {
      freePGBFrameBuffer();
    }
#endif
    destroyBandMemory();
  }
  return SW_EVENT_CONTINUE;
}

static sw_event_handlers pgb_state_handlers[] = {
  {pgb_endjob, NULL, 0, EVENT_TIMELINE_ENDING, SW_EVENT_NORMAL},
  {pgb_endjob, NULL, 0, EVENT_TIMELINE_ABORTING, SW_EVENT_NORMAL}
} ;

/* ----------------------------------------------------------------------
   The pagebuffer device functions: last error function
*/

static void pgb_set_last_error( DEVICELIST *dev, int32 nError )
{
  UNUSED_PARAM(DEVICELIST *, dev);
  skindevices_set_last_error(nError);
}

/* ----------------------------------------------------------------------
   Stub functions for those with no other purpose
*/

/**
 * \brief The ioerror routine for the pagebuffer device type.
 */
static int32 RIPCALL pgb_ioerror( DEVICELIST *dev )
{
  pgb_set_last_error( dev, DeviceIOError );
  return -1;
}

/**
 * \brief The noerror routine for the pagebuffer device type.
 */
static int32 RIPCALL pgb_noerror( DEVICELIST *dev )
{
  pgb_set_last_error( dev, DeviceNoError );
  return 0;
}

/**
 * \brief The void routine for the pagebuffer device type.
 */
static void* RIPCALL pgb_void( DEVICELIST *dev )
{
  pgb_set_last_error( dev, DeviceNoError );
  return NULL;
}

static int CRT_API param_sort(const void *a, const void *b)
{
  const uint32 *pa = a;
  const uint32 *pb = b;
  return strcmp(devparams[*pa].name, devparams[*pb].name);
}

/**
 * \brief The init_device routine for the pagebuffer device type.
 *
 * In this example, it is required to fill in the parameter name
 * lengths, which we cannot conveniently initialize statically.
 */
static int32 RIPCALL pgb_init_device( DEVICELIST *dev )
{
  PGBDeviceState* pDeviceState = (PGBDeviceState*) dev->private_data;
  uint32 i;

  pgb_tl = SW_TL_REF_INVALID ;

  for ( i = 0; i < MAX_PARAMS; i++ ) {
    devparams[i].namelen = strlen_int32( devparams[i].name );
    param_order[i] = i;
  }
  qsort(param_order, MAX_PARAMS, sizeof(uint32), param_sort);
  devparams[WRITEALLOUTPUT_INDEX].flags |= PARAM_SET;

  g_pgb = (PGBDescription *) MemAlloc( sizeof( PGBDescription ), FALSE, FALSE );
  if ( g_pgb == NULL )
  {
    pgb_set_last_error( dev, DeviceVMError );
    return -1;
  }

  if ( gg_deflateInit(&(pDeviceState->compress_stream),
                      pgbinparams.fFavorSpeedOverCompression
                      ? Z_BEST_SPEED : Z_DEFAULT_COMPRESSION) != Z_OK ) {
    MemFree( g_pgb );
    pgb_set_last_error( dev, DeviceVMError );
    return -1;
  }

  if ( SwRegisterHandlers(pgb_state_handlers,
                          sizeof(pgb_state_handlers)/sizeof(pgb_state_handlers[0]))
       != SW_RDR_SUCCESS ) {
    deflateEnd(&pDeviceState->compress_stream);
    MemFree(g_pgb);
    pgb_set_last_error( dev, DeviceVMError );
    return -1;
  }

#ifdef DEBUG_BUILD
  /* Debug, we don't care if it's not registered properly. */
  (void)SwRegisterRDR(RDR_CLASS_TIMELINE, TL_DEBUG_TYPE_NAME,
                      SWTLT_PGB, STRING_AND_LENGTH("PGBDevBands"),
                      SW_RDR_NORMAL) ;
#endif

  pgb_set_last_error( dev, DeviceNoError );
  return 0;
}

/**
 * \brief The open_file routine for the pagebuffer device type.
 */
static DEVICE_FILEDESCRIPTOR RIPCALL pgb_open_file( DEVICELIST *dev ,
                                                    uint8 *filename , int32 openflags )
{
  UNUSED_PARAM(int32, openflags);
  HQASSERT(filename != NULL, "PGB file open should have phase name") ;

  if ( pgb_tl != SW_TL_REF_INVALID ) {
    /* Only one file allowed at a time */
    pgb_set_last_error( dev, DeviceIOError );
    return -1;
  }

  /* Check that the essential parameters have been set explicitly */
  if (! PARAM_IS_SET( BANDWIDTH_INDEX )   ||
      ! PARAM_IS_SET( BANDHEIGHT_INDEX )  ||
      ! PARAM_IS_SET( IMAGEWIDTH_INDEX )  ||
      ! PARAM_IS_SET( IMAGEHEIGHT_INDEX ) ||
      ! PARAM_IS_SET( PRINTPAGE_INDEX )) {
    pgb_set_last_error( dev, DeviceIOError );
    return -1;
  }

  if ( skin_has_framebuffer && pgbinparams.NumSeparations != 1 ) {
    /* Framebuffer directory only copes with one separation. */
    pgb_set_last_error(dev, DeviceIOError);
    return -1;
  }

  HQASSERT(g_pgb != NULL, "No global PGB handle");
  HqMemZero( g_pgb, sizeof( PGBDescription ) );

  /* get rid of any excess colorants from previous jobs */
  {
    RasterColorant ** ppColorant;
    int32 nColorant;

    for (ppColorant = &pgbinparams.Colorant, nColorant = 0; * ppColorant != NULL;  )
    {
      if (nColorant >= pgbinparams.NumColorants)
      {
        RasterColorant * pColorant = (* ppColorant)->pNext;
        MemFree(* ppColorant);
        (* ppColorant) = pColorant;
      }
      else
      {
        ppColorant = & (* ppColorant)->pNext;
        nColorant++;
      }
    }
  }

  pgbinparams.BandLines = pgbinparams.BandHeight ;
  pgboutparams.BandsCopied = 0 ;

  if (! KInitializePGBDescription(g_pgb, (const char *)filename))
  {
    pgb_set_last_error( dev, DeviceIOError );
    return -1;
  }

  if ( g_pgb->partial_painting ) {
      HqBool first_time = FALSE;

      if ( skin_has_framebuffer ) {
        if ( p_band_directory == NULL ) {
          size_t dir_size = g_pgb->n_bands_in_page * g_pgb->rd.nSeparations
            * sizeof(HqBool);
          p_band_directory = SysAlloc(dir_size);
          HqMemZero(p_band_directory, dir_size);
        }
      } else if ( !g_pgb->discard_data
#ifdef HAS_RLE
         && !g_pgb->rd.runLength
#endif
         ) {
      /* Set up to partial paint bands to a memory cache, then, if
         memory limits are reached, a disk file. No raster callbacks
         are made until the final paint. */
      if ( pBandCache == NULL ) { /* No band cache yet. */
        /* Even though the disk cache will not be created yet, initialise
           its filename now because we have (potentially) received the
           name from the RIP. */
        if ( !initDiskCacheFileName( (const char *)filename)
             || !bandCache_New( &pBandCache,
                                g_pgb->n_bands_in_page, g_pgb->rd.nSeparations ) ) {
          pgb_set_last_error( dev, DeviceVMError );
          return -1;
        }
        /* Remove any existing cache file. */
        deleteDiskCacheFile();
        first_time = TRUE;
      } else {
        HQASSERT(pBandCache->numBands == (uint32)g_pgb->n_bands_in_page,
                 "Band cache number of bands mismatched");
      }
      /* Band cache exists and is needed for writing, init for this sep. */
      if ( pBandCache->separationIndex != g_pgb->rd.separation - 1 )
        initBandCache(g_pgb, first_time);
    }
  }

  if ( (pgb_tl = SwTimelineStart(SWTLT_PGB, SW_TL_REF_INVALID,
                                 0 /*start*/,
                                 SW_TL_EXTENT_INDETERMINATE,
                                 SW_TL_UNIT_BANDS, SW_TL_PRIORITY_NORMAL,
                                 g_pgb /*context*/,
                                 filename, filename ? strlen((const char *)filename) : 0))
       == SW_TL_REF_INVALID ) {
    pgb_set_last_error(dev, DeviceVMError) ;
    return -1 ;
  }

#ifdef CHECKSUM
  /* select HQCRCchecksum/HQCRCchecksumreverse */
  if ( fPrintChecksums )
    crcfunc = find_checksum_func( g_pgb->rd.rasterDepth );
#endif

  if (!g_pgb->compositing && pgbinparams.PrintPage == 0) {
    SkinMonitorf("Partial painting...\n");
  } else {
    ppMemPeak = ppMemSize ;
  }

  pgb_set_last_error( dev, DeviceNoError );
  return pgb_tl;
}

/**
 * \brief The read_file routine for the pagebuffer device type.
 */
static int32 RIPCALL pgb_read_file( DEVICELIST *dev , DEVICE_FILEDESCRIPTOR descriptor ,
                                    uint8 *buff , int32 len )
{
  sw_tl_ref tl = (sw_tl_ref)descriptor ;
  PGBDescription *pgb ;

  if (tl == SW_TL_REF_INVALID || tl != pgb_tl ||
      (pgb = SwTimelineGetContext(tl, 0)) == NULL ) {
    pgb_set_last_error( dev, DeviceIOError );
    return -1;
  }

  if ( !skin_has_framebuffer ) {
    if ( pgb->discard_data ) {
      /* Clear the band if throwing data away */
      HqMemZero(buff, len) ;
    }
    else
    {
      uint32 length = CAST_SIGNED_TO_UINT32(len);
      if ( ! getBand( pgb, buff, &length ) ) {
        pgb_set_last_error( dev, DeviceIOError );
        return -1;
      }
    }
  } else {
    /* buff already points to the band in the framebuffer. */
    if ( !p_band_directory[(pgb->rd.separation - 1) * pgb->n_bands_in_page
                           + pgb->seek_band] )
      /* See getBand() on requesting a band not provided earlier. */
      HqMemZero(buff, len);
  }

  pgb_set_last_error( dev, DeviceNoError );
  return len ;
}


/**
 * \brief The write_file routine for the pagebuffer device type.
 */
static int32 RIPCALL pgb_write_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,
                                     uint8 *buff, int32 len)
{
  PGBDeviceState *pDeviceState = (PGBDeviceState*)dev->private_data;
  sw_tl_ref tl = (sw_tl_ref)descriptor ;
  PGBDescription *pgb ;
  sw_tl_extent advance ;
#ifdef HAS_RLE
  static sw_tl_extent line_fraction = 0.0 ;
#endif

  if (tl == SW_TL_REF_INVALID || tl != pgb_tl ||
      (pgb = SwTimelineGetContext(tl, 0)) == NULL ) {
    pgb_set_last_error( dev, DeviceIOError );
    return -1;
  }

  pgb->rd.band.y1 = pgb->seek_line ;
  pgb->rd.band.advance = pgbinparams.BandLines ;
  pgb->rd.band.y2 = pgb->rd.band.y1 + pgb->rd.band.advance - 1 ;
  pgb->rd.partialPaint = (uint8)pgb->partial_painting;
  advance = pgb->rd.band.advance ;
#ifdef HAS_RLE
  /* When doing partial RLE lines, we don't want the raster to advance its
     notion of the current line, assuming it's keeping track rather than just
     using the y1 and y2 passed in. But we do need to modify the timeline
     progress, so that an update event will be issued to return the band
     to the RIP. */
  if ( pgb->rd.runLength && !pgbinparams.RunLineComplete ) {
    pgb->rd.band.advance = 0 ; /* Will write same line of raster next time. */
    line_fraction += 1.0/1024.0 ; /* Allows for 1023 partial line writes. */
    advance = line_fraction ;
  } else {
    line_fraction = 0.0 ;
  }
#endif

  if ( !pgb->discard_data )
  {
    if ( pgb->partial_painting
#ifdef HAS_RLE
      && ! pgb->rd.runLength
#endif
       )
    {
      /* If there's a framebuffer, we don't need to bother with the
         band cache. */
      if ( skin_has_framebuffer )
        p_band_directory[(pgb->rd.separation - 1) * pgb->n_bands_in_page
                         + pgb->seek_band]
          = TRUE;
      else if ( !storeBandInCache(pDeviceState, pgb, buff, CAST_SIGNED_TO_UINT32(len)) )
        {
          pgb_set_last_error( dev, DeviceIOError );
          return -1;
        }
    } else {
      if ( pgb->seek_line != pgb->output_line ||
           !KCallRasterCallback(&pgb->rd, buff) ) {
        pgb_set_last_error( dev, DeviceIOError );
        return -1;
      }
      pgb->output_line += pgb->rd.band.advance ;
    }

#ifdef CHECKSUM
    if ( fPrintChecksums && ! pgb->partial_painting ) {
      /* divide len by 4 because CRC expects number of 32-bit words,
       * not bytes.  as it happens, for a RasterDescription rd,
       *
       *  (len / 4) == (pgb->rd.bandHeight * pgb->rd.dataWidth / 32)
       *
       * except on the last band, when the band height might be
       * smaller than that cached in the RasterDescription earlier.
       *
       * band_multiplier is a factor when band-interleaved, otherwise
       * it's always 1
       */
      crc = (*crcfunc)( crc, (uint32*)buff, len / 4 );
    }
#endif
  }

  (void)SwTimelineSetProgress((sw_tl_ref)descriptor,
                              (sw_tl_extent)pgb->seek_line + advance) ;

  pgb_set_last_error( dev, DeviceNoError );
  return len;
}

/**
 * \brief The close_file routine for the pagebuffer device type.
 *
 * Note that this function may be called multiple times on the same page if
 * partial painting is invoked.
 */
static int32 RIPCALL pgb_close_file( DEVICELIST *dev , DEVICE_FILEDESCRIPTOR descriptor )
{
  sw_tl_ref tl = (sw_tl_ref)descriptor ;
  PGBDescription *pgb ;

  if (tl == SW_TL_REF_INVALID || tl != pgb_tl ||
      (pgb = SwTimelineGetContext(tl, 0)) == NULL ) {
    pgb_set_last_error( dev, DeviceIOError );
    return -1;
  }

#ifdef CHECKSUM
  if ( fPrintChecksums &&  !pgb->partial_painting ) {
    SkinMonitorf( "CHECKSUM = %x\n", crc );
    crc = 0;
  }
#endif

  if ( !pgbinparams.PGBSysmem ) {
    /* Scratch space will be recycled between partial paints except when
       compositing (yes, this is tricky knowledge about the core's resource
       handling), so flush the cache to disk. Even compositing, flush between
       separations to reuse the cache, just leave the last one unflushed, hoping
       that this is followed by a final paint, that can use it. */
    if ( pBandCache != NULL && pgb->partial_painting )
      if ( !pgb->compositing || pgb->rd.separation != pgb->rd.nSeparations ) {
        updateDiskCache();
        pBandCache->separationIndex = -1; /* mark reusable */
      }
  }

  if ( !pgb->partial_painting && ppMemPeak > 0 ) {
    static HqBool pgbdev_printmem = FALSE; /* patch in a debugger */
    if ( pgbdev_printmem )
      SkinMonitorf("PGB device memory: %u bytes\n", (uint32)ppMemPeak);
  }
  KFreePGBDescription(pgb);

  (void)SwTimelineEnd(tl) ;
  pgb_tl = SW_TL_REF_INVALID ;

  pgb_set_last_error( dev, DeviceNoError );
  return 0; /* success */
}

/**
 * \brief The abort_file function for the pagebuffer device type.
 *
 * This function will be called in place of close_file when an error
 * is reported during rendering which causes rendering to be
 * abandoned.
 *
 * For this example, cleans up in the event of an error by closing and
 * deleting the underlying file.
 */
static int32 RIPCALL pgb_abort_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor )
{
  sw_tl_ref tl = (sw_tl_ref)descriptor ;
  PGBDescription *pgb ;

  UNUSED_PARAM(DEVICELIST *, dev) ;

  if (tl != SW_TL_REF_INVALID &&
      (pgb = SwTimelineGetContext(tl, 0)) != NULL ) {
    /* If we are aborting, close and delete any partial paint band
     * cache and disk file */
    closeBandCache();
    if ( p_band_directory != NULL ) {
      SysFree(p_band_directory);
      p_band_directory = NULL;
    }
    closeDiskCacheFile();
    deleteDiskCacheFile();
    KFreePGBDescription(pgb);

    (void)SwTimelineAbort(tl, 0) ;
    pgb_tl = SW_TL_REF_INVALID ;
  }

  return 0;
}


/**
 * \brief The seek_file routine for the pagebuffer device type.
 *
 * This function is required. Because we have set the WriteAllOutput
 * parameter to TRUE, we know that we will receive all data, but only
 * on the final paint.  Partial paints may leave "holes" in the file.
 * We do not have to fill them with zeros, because of the behavior of
 * WriteAllOutput on the final paint.  If we had set it to FALSE, we
 * would have had to supply a zero band.
 *
 * The seek is to the first line of a band. The PGB parameter BandLines
 * indicates how many lines are in this band.
 *
 * For this example, this is just a matter of seeking in the
 * underlying file, not forgetting to add the size of the header we
 * put at the beginning.
 */
static int32 RIPCALL pgb_seek_file( DEVICELIST *dev , DEVICE_FILEDESCRIPTOR descriptor,
                                    Hq32x2 *destination , int32 whence )
{
  int32  offset;
  sw_tl_ref tl = (sw_tl_ref)descriptor ;
  PGBDescription *pgb ;

  if (tl == SW_TL_REF_INVALID || tl != pgb_tl ||
      (pgb = SwTimelineGetContext(tl, 0)) == NULL ) {
    pgb_set_last_error( dev, DeviceIOError );
    return FALSE;
  }

  if (! Hq32x2ToInt32(destination, &offset)) {
    pgb_set_last_error( dev, DeviceLimitCheck );
    return FALSE;
  }

  switch (whence) {
  case SW_SET:
    break;
  case SW_INCR:
    offset += pgb->seek_line;
    break;
  case SW_XTND:
    offset += pgb->rd.imageHeight * pgb->n_frames_in_raster;
    break;
  default:
    pgb_set_last_error( dev, DeviceIOError );
    return FALSE;
  }

  /* Allow zero for zero-height rasters. */
  if ( offset < 0 ||
       (pgb->rd.imageHeight == 0
        ? offset > 0
        : offset >= pgb->rd.imageHeight * pgb->n_frames_in_raster) ) {
    pgb_set_last_error( dev, DeviceIOError );
    return FALSE;
  }

  pgb->seek_line = offset ;
  if ( pgb->rd.imageHeight == 0 ) {
    pgb->seek_band = 0 ;
  } else {
    /* Convert frame and line number to frame and band index for whole bands. */
    pgb->seek_band = (offset / pgb->rd.imageHeight) * pgb->n_bands_in_frame +
      (offset % pgb->rd.imageHeight) / pgb->rd.bandHeight ;
  }

  Hq32x2FromInt32(destination, offset);

  if ( pgb->partial_painting && !skin_has_framebuffer ) {
    Hq32x2 ppplace ;

    Hq32x2FromInt32(&ppplace, pgb->seek_band * pgb->band_size);

    /* We require that the seek extend the file appropriately. Check if the
       underlying seek did this, and if not, do it ourself. */
    if (! Hq32x2ToInt32(&ppplace, &offset)) {
      pgb_set_last_error( dev, DeviceLimitCheck );
      return FALSE;
    }

    offset -= pgb->seek_band * pgb->band_size ;
    if ( offset > 0 ) {
#define ZEROS_SIZE 4096
      uint8 zeros[ZEROS_SIZE] ;

      HqMemZero(zeros, ZEROS_SIZE);

      while ( offset > 0 ) {
        int32 len = offset ;

        if ( len > ZEROS_SIZE )
          len = ZEROS_SIZE ;

        /*        if ( PKWriteFile(partial_paint_fd, zeros, len, &pkerror) < 0 ) {
         *          pgb_set_last_error( dev, DeviceIOError );
         *          return FALSE;
         *        }
         */

        offset -= len ;
      }
    } else if ( offset < 0 ) {
      pgb_set_last_error( dev, DeviceIOError );
      return FALSE;
    }
  }

  pgb_set_last_error( dev, DeviceNoError );
  return TRUE;
}

/**
 * \brief The bytes_file routine for the pagebuffer device type.
 */
static int32 RIPCALL pgb_bytes_file( DEVICELIST *dev , DEVICE_FILEDESCRIPTOR descriptor,
                                     Hq32x2 *bytes, int32 reason )
{
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);

  if ( reason == SW_BYTES_AVAIL_REL )
  {
    pgb_set_last_error( dev, DeviceNoError );
    Hq32x2FromInt32(bytes, 0);
    return TRUE;
  }

  pgb_set_last_error( dev, DeviceIOError );
  return FALSE;
}


/**
 * \brief The status_file routine for the pagebuffer device type.
 */
static int32 RIPCALL pgb_status_file( DEVICELIST *dev ,
                                      uint8 *filename ,
                                      STAT *statbuff )
{
  UNUSED_PARAM(uint8 *, filename);
  UNUSED_PARAM(STAT *,statbuff);

  return pgb_ioerror( dev );
}

/**
 * \brief The start_file_list routine for the pagebuffer device type.
 */
static void* RIPCALL pgb_start_file_list(DEVICELIST *dev,
                                           uint8 *pattern)
{
  UNUSED_PARAM(uint8 *, pattern);

  return pgb_void( dev );
}

/**
 * \brief The next_file routine for the pagebuffer device type.
 */
static int32 RIPCALL pgb_next_file( DEVICELIST *dev ,
                                    void **handle ,
                                    uint8 *pattern,
                                    FILEENTRY *entry)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(void **, handle);
  UNUSED_PARAM(uint8 *, pattern);
  UNUSED_PARAM(FILEENTRY *, entry);

  return FileNameNoMatch;
}

/**
 * \brief The end_file_list routine for the pagebuffer device type.
 */
static int32 RIPCALL pgb_end_file_list(DEVICELIST *dev,
                                         void * handle)
{
  UNUSED_PARAM(void **, handle);

  return pgb_noerror( dev );
}

/**
 * \brief The rename_file routine for the pagebuffer device type.
 */
static int32 RIPCALL pgb_rename_file( DEVICELIST *dev,
                                      uint8 *file1 ,
                                      uint8 *file2)
{
  UNUSED_PARAM(uint8 *, file1);
  UNUSED_PARAM(uint8 *, file2);

  return pgb_ioerror( dev );
}

/**
 * \brief The delete_file routine for the pagebuffer device type.  It
 * closes and destroys an in-memory or disk-based cache of PGB data
 * that was created in earlier calls to the device.
 */
static int32 RIPCALL pgb_delete_file( DEVICELIST *dev ,
                                      uint8 *filename )
{
  UNUSED_PARAM(uint8 *, filename);

  closeBandCache();
  if ( p_band_directory != NULL ) {
    SysFree(p_band_directory);
    p_band_directory = NULL;
  }
  closeDiskCacheFile();
  deleteDiskCacheFile();
  return pgb_noerror( dev );
}

/* return index into devparams[] for name, or MAX_PARAMS if not found. */
static inline uint32 pgb_find_param(char *name, int32 len)
{
  int32 lo = 0, hi = MAX_PARAMS - 1;

  do { /* Binary search for parameter. */
    int32 mid = (lo + hi) / 2;
    uint32 i = param_order[mid];
    int32 diff = len - devparams[i].namelen;
    int32 cmp = strncmp(name, devparams[i].name,
                        diff > 0 ? len - diff : len);
    if ( cmp == 0 ) /* Same up to minimum length, so compare on length */
      cmp = diff;
    if ( cmp == 0 ) /* Same length and content too. */
      return i;
    if ( cmp < 0 ) {
      hi = mid - 1;
    } else {
      lo = mid + 1;
    }
  } while ( lo <= hi );
  /* Failed to find parameter. */
  return MAX_PARAMS;
}

/**
 * \brief The set_param function for the pagebuffer device type.
 *
 * This implementation uses the structures defined at the head of the file
 * to record the parameters.
 *
 * This is done by a simple search on the name. However, given the
 * very large number of parameters it would be more efficient to use
 * a hashing technique or a binary chop on a table organised
 * alphabetically or something of the kind.
 */
static int32 RIPCALL pgb_set_param(DEVICELIST *dev, DEVICEPARAM *param )
{
  int32 *int_ptr;
  uint8 *str_ptr;
  float *flt_ptr;
  char *lname;
  int32 len;
  uint32 i;
  HqBool changed = FALSE;

  pgb_set_last_error( dev, DeviceNoError );

  len = param->paramnamelen;
  lname = (char *) param->paramname;

  if ( lname == NULL || len <= 0 )
    return ParamConfigError;

  /* Assert an index match near the end of the list. */
  HQASSERT(strcmp(devparams[RUNLINEOUTPUT_INDEX].name, "RunLineOutput") == 0,
           "PGB device parameters out of sync") ;

  i = pgb_find_param(lname, len);
  if ( i >= MAX_PARAMS ) {
#if 0
    SkinMonitorf("PGB param %.*s not known\n", param->paramnamelen,
                 param->paramname);
#endif

    return ParamIgnored;
  }

  if ((devparams[i].flags & PARAM_WRITEABLE) == 0)
    /* The caller is not allowed to set this parameter */
    return ParamConfigError;

  if ( devparams[i].type != param->type )
    return ParamTypeCheck;

  switch ( param->type ) {
  case ParamBoolean:
    if ( devparams[i].flags & PARAM_RANGE ) {
      if ( param->paramval.intval != devparams[i].minval &&
           param->paramval.intval != devparams[i].maxval )
        return ParamRangeCheck;
    }

    int_ptr = devparams[i].addr;
    changed = (*int_ptr != param->paramval.boolval) ;
    *int_ptr = param->paramval.boolval;

    if ( changed ) {
      if ( &pgbinparams.PrintPage == int_ptr ) {
        /* WriteAllOutput is a readonly param (ie, RIP won't change it), so
         * we need to update according to which paint (partial/final) we are
         * on. RIP will pick it up through a get_param call.
         */
        pgboutparams.WriteAllOutput = param->paramval.boolval;
      }
#ifdef METRO
      else if ( &pgbinparams.PtfEmulateXPSPT == int_ptr ) {
        xpspt_setEmulationEnabled(param->paramval.boolval);
      }
#endif
      else if ( &pgbinparams.fFavorSpeedOverCompression == int_ptr ) {
        PGBDeviceState* pDeviceState = (PGBDeviceState*) dev->private_data;
        z_stream new_stream;

        if ( gg_deflateInit(&new_stream,
                            param->paramval.boolval
                            ? Z_BEST_SPEED : Z_DEFAULT_COMPRESSION ) != Z_OK ) {
          pgb_set_last_error( dev, DeviceVMError );
          return ParamError;
        }

        deflateEnd(&pDeviceState->compress_stream);
        HqMemCpy(&pDeviceState->compress_stream, &new_stream, sizeof(new_stream)) ;
      }
    }

    break;

  case ParamInteger:
    if ( devparams[i].flags & PARAM_RANGE ) {
      if ( param->paramval.intval < devparams[i].minval ||
           param->paramval.intval > devparams[i].maxval )
        return ParamRangeCheck;
    }

    int_ptr = devparams[i].addr;
    changed = (*int_ptr != param->paramval.intval) ;
    *int_ptr = param->paramval.intval;

    if ( changed ) {
      if ( &pgbinparams.PtfSystemErrorLevel == int_ptr ) {
        (void)KSetSystemErrorLevel(param->paramval.intval);
      }
    }

    break;

  case ParamString:
    /* If the string is writeable, it MUST have its range checked. */
    HQASSERT((devparams[i].flags & PARAM_WRITEABLE) == 0 ||
             (devparams[i].flags & PARAM_RANGE) != 0,
             "Writeable string must be range checked") ;

    if ( devparams[i].flags & PARAM_RANGE ) {
      if ( param->strvallen > devparams[i].maxval )
        return ParamRangeCheck;
    }

    str_ptr = devparams[i].addr;
    if ( str_ptr[param->strvallen] != 0 ||
         strncmp((char *)str_ptr, (char *)param->paramval.strval,
                 CAST_SIGNED_TO_SIZET(param->strvallen)) != 0 ) {
      /* use HqMemCpy because the string may have ascii NUL in it */
      HqMemCpy(str_ptr, param->paramval.strval, CAST_SIGNED_TO_SIZET(param->strvallen) );
      /* and null terminate our local copy */
      str_ptr[ param->strvallen ] = 0;
      changed = TRUE ;
    }

    break;

  case ParamFloat:
    if ( devparams[i].flags & PARAM_RANGE ) {
      if ((double)param->paramval.floatval < (double)devparams[i].minval ||
          (double)param->paramval.floatval > (double)devparams[i].maxval )
        return ParamRangeCheck;
    }

    flt_ptr = devparams[i].addr;
    changed = (*flt_ptr != param->paramval.floatval) ;
    *flt_ptr = param->paramval.floatval;

    break;

  case ParamDict:
    if (devparams[i].addr == &pgbinparams.Colorant) {
      int32 result = KParseColorant(param, &changed) ;
      if ( result != ParamAccepted )
        return result ;

      break ;
    }
    /* else fall through */

  default:
    return ParamIgnored;
  }

  devparams[i].flags |= PARAM_SET;

#if 0
  SkinMonitorf("Set PGB param %s %s\n", devparams[i].name,
               changed ? "changed" : "same");
#endif

  if ( changed ) {
    if ( devparams[i].skin_hook != NULL ) {
      int32 result = KCallParamCallback(devparams[i].skin_hook,
                                        devparams[i].addr) ;
      if ( result != ParamAccepted )
        return result ;
    }
  }

  return ParamAccepted;
}

/** \brief The start_param and get_param functions for the pagebuffer
   device type use last_param to maintain the local state.
*/
static uint32 last_param = 0;

/**
 * \brief pgb_start_param
 */
static int32 RIPCALL pgb_start_param(DEVICELIST *dev)
{
  uint32 param;
  int32 count;

  pgb_set_last_error( dev, DeviceNoError );
  last_param = 0;

  /* count the number of parameters with a valid value */
  count = 0;
  for ( param = 0; param < MAX_PARAMS; param++ ) {
    if ( PARAM_IS_SET( param ))
      count++;
  }

  return count;
}


static int32 RIPCALL pgb_get_param( DEVICELIST *dev , DEVICEPARAM *param )
{
  int32 *int_ptr;
  uint8 *str_ptr;
  float *flt_ptr;
  int32 len;
  char *lname;
  PARAM *dev_param;

  pgb_set_last_error( dev, DeviceNoError );

  len = param->paramnamelen;
  lname = (char *) param->paramname;

  if ( lname == NULL ) {
    /* return with the next parameter in the list */
    while ( last_param < MAX_PARAMS && ! PARAM_IS_SET( last_param )) {
      ++last_param;
    }
    if ( last_param >= MAX_PARAMS )
      return ParamIgnored;
    dev_param = &devparams[last_param];
    ++last_param;
  } else {
    uint32 i;

    if ( len < 0 )
      return ParamConfigError;

    i = pgb_find_param(lname, len);
    if ( i >= MAX_PARAMS )
      return ParamIgnored;
    if ( ! PARAM_IS_SET( i ))
      return ParamIgnored;
    dev_param = &devparams[i];
  }

  param->paramname = (uint8 *) dev_param->name;
  param->paramnamelen = dev_param->namelen;
  param->type = dev_param->type;

  switch (param->type) {
  case ParamBoolean:
    int_ptr = dev_param->addr;
    param->paramval.boolval = *int_ptr;
    break;

  case ParamInteger:
    int_ptr = dev_param->addr;
    param->paramval.intval = *int_ptr;
    break;

  case ParamString:
    str_ptr = dev_param->addr;
    param->paramval.strval = str_ptr;
    param->strvallen = strlen_int32( (char *) str_ptr );
    break;

  case ParamFloat:
    flt_ptr = dev_param->addr;
    param->paramval.floatval = *flt_ptr;
    break;

  default:
    return ParamIgnored;
  }
  return ParamAccepted;
}

/** \brief The status_device function for the pagebuffer device type.

   This function has a rather specialized interpretation for the
   pagebuffer device. It can be used to give more memory to the rip
   in which it can render: either a whole page's worth (and we know
   from the pgbparams structure how big that is going to be) to
   operate as a frame device, or any spare memory so that it can get
   further ahead of the real printer if it has time to do so.

   In this example, we supply no additional memory.
*/

static int32 RIPCALL pgb_status_device( DEVICELIST *dev , DEVSTAT *devstat )
{
  UNUSED_PARAM(DEVICELIST *, dev);

  devstat->size.low = devstat->size.high = 0;
  devstat->free.low = devstat->free.high = 0;
  devstat->block_size = 1;
  devstat->start = NULL;

  return 0;
}


static int32 RIPCALL pgb_dismount_device(DEVICELIST *dev)
{
  PGBDeviceState* pDeviceState = (PGBDeviceState*) dev->private_data;

  /* We can get here without having called pgb_abort_file or pgb_delete_file
   * so clear up the band cache allocations and delete the disk cache file.
   */
  closeBandCache();
  closeDiskCacheFile();
  deleteDiskCacheFile();

  MemFree( pszDiskCacheFullFileName );
  pszDiskCacheFullFileName = NULL;
  (void)SwDeregisterHandlers(pgb_state_handlers,
                             sizeof(pgb_state_handlers)/sizeof(pgb_state_handlers[0])) ;
  deflateEnd( &(pDeviceState->compress_stream) );
  MemFree( g_pgb );
  g_pgb = NULL ;

#ifdef DEBUG
  (void)SwDeregisterRDR(RDR_CLASS_TIMELINE, TL_DEBUG_TYPE_NAME,
                        SWTLT_PGB, STRING_AND_LENGTH("PGBDevBands")) ;
#endif
  return 0 ;
}


static int32 RIPCALL pgb_ioctl_device( DEVICELIST *dev,
                                       DEVICE_FILEDESCRIPTOR descriptor,
                                       int32 opcode,
                                       intptr_t arg )
{
  int32          return_value = -1;

  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);
  HQASSERT(descriptor == (DEVICE_FILEDESCRIPTOR)-1,
           "File descriptor for IOCtl unexpected") ;

  switch (opcode)
  {
  case DeviceIOCtl_RasterStride:
    return_value = KCallRasterStride((uint32 *)arg);
    break;

  case DeviceIOCtl_RasterRequirements:
    {
      RASTER_REQUIREMENTS *req = (RASTER_REQUIREMENTS *)arg;

#ifdef TEST_FRAMEBUFFER
      if (fUseFrameBuffer)
        return_value = KCallRasterFrameBufferRequirements(req, FALSE);
      else
#endif
        return_value = KCallRasterRequirements(req, FALSE);
      skin_has_framebuffer = req->have_framebuffer;
      /* Abused have_framebuffer field includes a flag indicating
       * OIL/PMS is just providing a band of memory, and this skin
       * code has to manage saved bands for partial-paint.
       */
      if ( ((long *)&req->have_framebuffer)[0] & 0x10000 ) {
        req->have_framebuffer = TRUE;
        skin_has_framebuffer = FALSE;
      }

      if ( pgbinparams.PGBSysmem )
        req->scratch_size = 0;
      else
        req->scratch_size = totalBandMemory(req, NULL);
    }
    break;

  case DeviceIOCtl_RenderingStart:
    {
      RASTER_REQUIREMENTS *req = (RASTER_REQUIREMENTS *)arg;

#ifdef TEST_FRAMEBUFFER
      if (fUseFrameBuffer)
        return_value = KCallRasterFrameBufferRequirements(req, TRUE);
      else
#endif
        return_value = KCallRasterRequirements(req, TRUE);
      /*
       * RenderingStart ioctl is called when rendering is starting for a page,
       * but before the PGB open has occurred. So this is a safe time to
       * instantiate all the various band pointers we need for this page in
       * the pipeline.
       */
      if ( !instantiateBandMemory(req) )
        return_value = -1;
    }
    break;

  case DeviceIOCtl_GetBufferForRaster:
#ifdef TEST_FRAMEBUFFER
    if (fUseFrameBuffer)
      return_value = KCallRasterFrameBufferDestination((RASTER_DESTINATION *)arg, g_pgb->rd.separation - 1);
    else
#endif
      return_value = KCallRasterDestination((RASTER_DESTINATION *)arg, g_pgb->rd.separation - 1);

    break;

  default:
    pgb_set_last_error(dev, DeviceNoError); /* Unimplemented */
    break;
  }

  return return_value;
}


/**
 * \brief pgb_spare
 */
static int32 RIPCALL pgb_spare(void)
{
  return 0;
}


/*
 * Utility functions
 */

/**
 * \brief Return the underlying device of the PGB device, through which all
 * IO operations are carried out.
 *
 * The PGB device sometimes needs to store temporary portions of
 * raster data, such as in low-memory situations, or when the RIP
 * is compositing transparent regions. For reasons of flexibility, the
 * RIP uses a secondary (or "underlying") device for this storage.
 * Once the underlying device has been selected, it will be used
 * for the remainder of this RIP session. The device is chosen according
 * to a preferential search pattern as follows:-
 *
 * - Any device called \c jobtmp is the first choice. The implementation
 *   of this device may not preserve files between jobs. If the SW resources
 *   folder has been compiled into RAM, or has been designated
 *   read-only, a \c jobtmp device can be mounted during RIP
 *   initialization, as demonstrated by MountOtherDevices() in
 *   \ref skintest.c. This device can designate a disk directory
 *   for temporary cache data, such as the PGB band cache, and
 *   so can avoid any undue consumption of RAM for such data.
 *
 * - Any device called \c tmp is the second choice. If the SW resources
 *   folder is a writable directory, there will not be a
 *   \c jobtmp device by default, because MountOtherDevices() does
 *   not mount one. The \c tmp device is typically a sub-directory
 *   of SW, which is suitable when this directory is writable.
 *   When the SW folder is read-only, the \c tmp device is actually
 *   serviced by RAM, which is why it is beneficial to mount
 *   a \c jobtmp device in preference.
 *
 * - The \c os device is the final choice, since neither of
 *   \c tmp or \c jobtmp are guaranteed to exist. The
 *   \c os device is always present. Like \c tmp, it will be
 *   disk-based when the SW folder is writable, and RAM-based
 *   otherwise.
 */

static DEVICELIST * KGetBandCacheDev(void)
{
  if (pBandCacheDevice == NULL)
    pBandCacheDevice = SwFindDevice((uint8 *) "jobtmp" );
  if (pBandCacheDevice == NULL)
    pBandCacheDevice = SwFindDevice((uint8 *)"tmp") ;
  if (pBandCacheDevice == NULL)
    pBandCacheDevice = SwFindDevice((uint8 *)"os") ;
  return pBandCacheDevice;
}

/**
 * \brief Initialize a PGBDescription struct.
 *
 * \param[in] pgb Pointer to the PGBDescription to initialize.
 *
 * \param[in] filename Name of the file to create for storing PGB data
 * in.
 *
 * \return TRUE on success; FALSE otherwise.
 */
static HqBool KInitializePGBDescription(PGBDescription *pgb,
                                        const char *filename)
{
  RasterDescription * pRD = &pgb->rd ;

  pgb->discard_data = (strcmp((char *)pgbinparams.PageBufferType, "None") == 0) || override_to_none;

  pgb->n_bands_in_page = pgb->n_bands_in_frame =
    (pgbinparams.ImageHeight + pgbinparams.BandHeight - 1) / pgbinparams.BandHeight;
  pgb->seek_band = 0;
  pgb->seek_line = 0;
  pgb->output_line = 0;

  /* BandWidth is width in bits, regardless of output mode. */
  pgb->band_size = (pgbinparams.BandWidth >> 3) * pgbinparams.BandHeight;
  pgb->n_frames_in_raster = 1 ;
#ifdef HAS_RLE
  if  (! pgbinparams.RunLength )
#endif
  {
    switch (pgbinparams.InterleavingStyle) {
    case interleavingStyle_monochrome:
      break;
    case interleavingStyle_pixel:
      break;
    case interleavingStyle_band:
      pgb->band_size *= pgbinparams.NumChannels;
      break;
    case interleavingStyle_frame:
      pgb->n_frames_in_raster = pgbinparams.NumChannels;
      pgb->n_bands_in_page = pgb->n_bands_in_frame * pgbinparams.NumChannels;
      break;
    default:
      return FALSE;
    }
  }
#ifdef HAS_RLE
  else
  {
    /* Each line is effectively a band in RLE. The band size is not relevant,
    and varies for each line. */
    pgb->n_bands_in_page = pgbinparams.ImageHeight;
    pgb->band_size = 0;
  }
#endif

  pRD->xResolution = (double)pgbinparams.XResolution + (double)pgbinparams.XResFrac;
  pRD->yResolution = (double)pgbinparams.YResolution + (double)pgbinparams.YResFrac;
  pRD->imageNegate = pgbinparams.NegativePrint;
  pRD->mediaSelect = pgbinparams.InputAttributes;
  pRD->leftMargin = pgbinparams.LeftMargin;
  pRD->rightMargin = pgbinparams.RightMargin;
  pRD->topMargin = pgbinparams.TopMargin;
  pRD->bottomMargin = pgbinparams.BottomMargin;
  pRD->imageWidth = pgbinparams.ImageWidth;
  pRD->imageHeight = pgbinparams.ImageHeight;
  pRD->dataWidth = pgbinparams.BandWidth;
  pRD->bandHeight = pgbinparams.BandHeight;
  pRD->noCopies = pgbinparams.NumCopies;
  pRD->jobNumber = pgbinparams.JobNumber;
  pRD->pageNumber = pgbinparams.PageNumber;
  pRD->documentNumber = pgbinparams.DocumentNumber;
  strcpy((char *)pRD->jobName, (char *)pgbinparams.JobName);
  pRD->outputAttributes = pgbinparams.OutputAttributes;
#ifdef HAS_RLE
  pRD->runLength = pgbinparams.RunLength;
  if (pRD->runLength)
  {
    pRD->screenDataCount = getScreenCount();
    pRD->pScreenData = getScreenList();
    /* hand over responsibility for the memory management of the list */
    setScreenList( NULL );
    setScreenCount( 0 );
  }
#endif
  pRD->insertSheet = pgbinparams.InsertSheet;
  pRD->numChannels = pgbinparams.NumChannels;
  pRD->interleavingStyle = pgbinparams.InterleavingStyle;
  pRD->rasterDepth = pgbinparams.RasterDepth;
  pRD->packingUnit = pgbinparams.packingUnit ;
  pRD->pColorants = pgbinparams.Colorant;
  pRD->separation = pgbinparams.Separation ;
  pRD->nSeparations = pgbinparams.NumSeparations ;
  /* hand over responsibility for the memory management of the list */
  pgbinparams.Colorant = NULL;
  strcpy((char *)pRD->colorantFamily, (char *)pgbinparams.ColorantFamily);
  strcpy((char *)pRD->pageBufferType, (char *)pgbinparams.PageBufferType );
  strcpy((char *)pRD->outputTarget, (char *)pgbinparams.OutputTarget );
  pRD->duplex = pgbinparams.Duplex;
  pRD->tumble = pgbinparams.Tumble;
  pRD->orientation = pgbinparams.Orientation;
  pRD->collate = pgbinparams.Collate;
  pRD->outputFaceUp = pgbinparams.OutputFaceUp;

  /* PrintTicket features */
  strcpy((char *)pRD->PtfDocumentDuplex, (char * )pgbinparams.PtfDocumentDuplex);
  strcpy((char *)pRD->PtfJobDuplex, (char * )pgbinparams.PtfJobDuplex);
  strcpy((char *)pRD->PtfMediaType, (char * )pgbinparams.PtfMediaType);
  strcpy((char *)pRD->PtfPageOutputColor, (char * )pgbinparams.PtfPageOutputColor);
  pRD->PtfOutputDeviceBPP = pgbinparams.uPtfOutputDeviceBPP;
  pRD->PtfOutputDriverBPP = pgbinparams.uPtfOutputDriverBPP;
  strcpy((char *)pRD->PtfOutputQuality, (char * )pgbinparams.uPtfOutputQuality);
  pRD->PtfPageFaceUp = pgbinparams.PtfPageFaceUp;
  pRD->PtfPageCopyCount = pgbinparams.PtfPageCopyCount;
  pRD->PtfDocumentCopiesAllPages = pgbinparams.PtfDocumentCopiesAllPages;
  pRD->PtfJobCopiesAllDocuments = pgbinparams.PtfJobCopiesAllDocuments;
  strcpy((char *)pRD->PtfJobOutputOptimization, (char * )pgbinparams.PtfJobOutputOptimization);
  strcpy((char *)pRD->PtfPagePhotoPrintingIntent, (char * )pgbinparams.PtfPagePhotoPrintingIntent);
  strcpy((char *)pRD->PtfJobHolePunch, (char * )pgbinparams.PtfJobHolePunch);
  strcpy((char *)pRD->PtfDocumentHolePunch, (char * )pgbinparams.PtfDocumentHolePunch);
  strcpy((char *)pRD->PtfJobRollCutAtEndOfJob, (char * )pgbinparams.PtfJobRollCutAtEndOfJob);
  strcpy((char *)pRD->PtfDocumentRollCut, (char * )pgbinparams.PtfDocumentRollCut);
  strcpy((char *)pRD->PtfJobStapleAllDocuments, (char * )pgbinparams.PtfJobStapleAllDocuments);
  strcpy((char *)pRD->PtfDocumentStaple, (char * )pgbinparams.PtfDocumentStaple);
  pRD->PtfStapleAngle = pgbinparams.PtfStapleAngle;
  pRD->PtfStapleSheetCapacity = pgbinparams.PtfStapleSheetCapacity;
  strcpy((char *)pRD->PtfPageBorderless, (char * )pgbinparams.PtfPageBorderless);
  strcpy((char *)pRD->PtfJobDeviceLanguage, (char * )pgbinparams.PtfJobDeviceLanguage);
  strcpy((char *)pRD->PtfJobDeviceLanguage_Level, (char * )pgbinparams.PtfJobDeviceLanguage_Level);
  strcpy((char *)pRD->PtfJobDeviceLanguage_Encoding, (char * )pgbinparams.PtfJobDeviceLanguage_Encoding);
  strcpy((char *)pRD->PtfJobDeviceLanguage_Version, (char * )pgbinparams.PtfJobDeviceLanguage_Version);
  strcpy((char *)pRD->PtfOutputBin, (char * )pgbinparams.PtfOutputBin);
  pRD->PtfJobCollateAllDocuments = pgbinparams.PtfJobCollateAllDocuments;
  pRD->PtfDocumentCollate = pgbinparams.PtfDocumentCollate;
  pRD->OptimizedPDFUsageCount = pgbinparams.OptimizedPDFUsageCount;
  strcpy((char *)pRD->OptimizedPDFId, (char * )pgbinparams.OptimizedPDFId);
  pRD->separation_id = pgbinparams.SeparationId ;

  pgb->partial_painting = !pgbinparams.PrintPage;
  /* Check the "filename" here to see if we're compositing. Ick. */
  pgb->compositing = strcmp((const char *)filename, "Compositing") == 0;
  return TRUE;
}


/** Parse a colorant description parameter, and add it to the current list of
    colorants. */
static int32 KParseColorant(DEVICEPARAM * param, HqBool *changedp)
{
  RasterColorant ** ppColorant;
  RasterColorant  * pColorant;
  DEVICEPARAM     * pParam = param->paramval.compobval;
  int32             nColorant;
  int32             nEntry;

  *changedp = FALSE ;

  for (pParam = param->paramval.compobval, nEntry = 0, nColorant = -1;
       nEntry < param->strvallen;
       pParam++, nEntry++)
  {

    if (pParam->paramnamelen == sizeof(aszColorant) - 1 &&
        strncmp((char *) pParam->paramname, aszColorant, sizeof(aszColorant) - 1) == 0)
    {
      if (pParam->type != ParamInteger)
        return ParamTypeCheck;
      nColorant = pParam->paramval.intval;
      break;
    }
  }
  if (nColorant < 0)
    return ParamRangeCheck;

  ppColorant = &pgbinparams.Colorant;
  nEntry = 0;

  /* Find the nColorant-th RasterColorant, allocating new RasterColorant structures
   * if need be.
   */
  for (;;)
  {
    if (* ppColorant == NULL)
    {
      /* we are short of one or more nodes - make another. Allocate zeroed */
      * ppColorant = (RasterColorant *)MemAlloc(sizeof(RasterColorant), TRUE, FALSE);
      if (* ppColorant == NULL)
        return ParamRangeCheck;

      HqMemZero(*ppColorant, sizeof(RasterColorant));

      *changedp = TRUE ;
    }

    if (nEntry == nColorant)
      break;

    ppColorant = & (* ppColorant)->pNext;
    nEntry++;
  }

  /* *ppColorant is pointing at the RasterColorant structure that needs filling in */
  pColorant = *ppColorant;

  /* This can result in false positives for changes, if the colorant list is
     reduced, and the reduced list is re-used without being trimmed first. */
  if ( pColorant->pNext != NULL )
    *changedp = TRUE ;

  /* Extract the other keys out of the dictionary */
  for (pParam = param->paramval.compobval, nEntry = 0;
       nEntry < param->strvallen;
       pParam++, nEntry++)
  {
    if (pParam->paramnamelen == sizeof(aszColorantName) - 1 &&
        strncmp((char *) pParam->paramname, aszColorantName, CAST_SIGNED_TO_SIZET(pParam->paramnamelen)) == 0)
    {
      /* ColorantName */
      if (pParam->type != ParamString)
        return ParamTypeCheck;
      if ((uint32)pParam->strvallen > sizeof(pColorant->colorantName) - 1)
        return ParamRangeCheck;

      if ( pColorant->colorantName[pParam->strvallen] != 0 ||
           strncmp((char *)pColorant->colorantName,
                   (char *)pParam->paramval.strval,
                   pParam->strvallen) != 0 ) {
        strncpy((char *)pColorant->colorantName,
                (char *)pParam->paramval.strval,
                pParam->strvallen);
        pColorant->colorantName[pParam->strvallen] = 0;
        *changedp = TRUE ;
      }
    }
    else if (pParam->paramnamelen == sizeof(aszChannel) - 1 &&
             strncmp((char *) pParam->paramname, aszChannel, CAST_SIGNED_TO_SIZET(pParam->paramnamelen)) == 0)
    {
      /* Channel */
      if (pParam->type != ParamInteger)
        return ParamTypeCheck;

      if ( pColorant->nChannel != pParam->paramval.intval ) {
        pColorant->nChannel = pParam->paramval.intval;
        *changedp = TRUE ;
      }
    } else if (pParam->paramnamelen == sizeof(aszsRGB) - 1 &&
               strncmp((char *) pParam->paramname, aszsRGB, CAST_SIGNED_TO_SIZET(pParam->paramnamelen)) == 0)
    {
      DEVICEPARAM *compobval ;

      /* sRGB */
      if (pParam->type != ParamArray)
        return ParamTypeCheck;
      if ( (compobval = pParam->paramval.compobval) == NULL )
        return ParamRangeCheck ;
      if (pParam->strvallen != 3)
        return ParamRangeCheck;
      if (compobval[0].type != ParamFloat ||
          compobval[1].type != ParamFloat ||
          compobval[2].type != ParamFloat)
        return ParamTypeCheck;
      if ( pColorant->srgbEquivalent[0] != compobval[0].paramval.floatval ||
           pColorant->srgbEquivalent[1] != compobval[1].paramval.floatval ||
           pColorant->srgbEquivalent[2] != compobval[2].paramval.floatval ) {
        pColorant->srgbEquivalent[0] = compobval[0].paramval.floatval;
        pColorant->srgbEquivalent[1] = compobval[1].paramval.floatval;
        pColorant->srgbEquivalent[2] = compobval[2].paramval.floatval;
        *changedp = TRUE ;
      }
    }
  }
  return ParamAccepted;
}


/**
 * \brief Destructor function for PGBDescription structs.
 *
 * \param[in] pgb Pointer to the PGB to be freed.
 */
static void KFreePGBDescription(PGBDescription *pgb)
{
  RasterDescription *pRD = &pgb->rd ;
  RasterColorant  * pColorant = pRD->pColorants;
  RasterColorant  * pNextColorant;
#ifdef HAS_RLE
  ScreenData      * pScreenData = pRD->pScreenData;
  ScreenData      * pNextScreenData;
#endif

  while (pColorant != NULL)
  {
    pNextColorant = pColorant->pNext;
    MemFree(pColorant);
    pColorant = pNextColorant;
  }

#ifdef HAS_RLE
  while (pScreenData)
  {
    pNextScreenData = pScreenData->sd_next;
    MemFree(pScreenData);
    pScreenData = pNextScreenData;
  }
#endif
}


/**
 * \brief Constructor function for BandCache structs.
 *
 * \param[out] ppValue A non-NULL BandCache pointer to which the
 * allocated BandCache will be assigned.
 *
 * \param[in] numBands The number of bands that will be stored in the cache.
 * This is the number of bands required to hold one separation.
 *
 * \param[in] numSeparations The number of separations in the job.
 *
 * \return TRUE on success; FALSE otherwise.
 */
static HqBool bandCache_New( BandCache ** ppValue,
                             int32 numBands,
                             int32 numSeparations )
{
  HqBool fSuccess = FALSE;
  BandCache * pBC = NULL;

  pBC = (BandCache*) MemAlloc( sizeof( BandCache ), TRUE, FALSE );
  if ( pBC == NULL )
    goto end;

  pBC->numBands = numBands;
  pBC->numSeparations = numSeparations;
  pBC->separationIndex = -1;
  /* no need to set totalMemoryCacheSize field: will be 0 from MemAlloc */

  *ppValue = pBC;
  fSuccess = TRUE;
 end:
  return fSuccess;
}


/**
 * \brief Destructor function for a BandCache struct.
 *
 * \param[in] pValue Pointer to the BandCache to free.
 *
 * Note that this does not free any memory allocated through gBandMemory,
 * e.g. the array of BandHolders (ppBands).
 */
static void bandCache_Free( BandCache *pValue )
{
  MemFree( pValue );
}

/** Calculate the amount of band memory needed for this page. */
static size_t totalBandMemory(RASTER_REQUIREMENTS *req, size_t *sizes)
{
  size_t nBands, bandBytes, bandbuffBytes, compressBytes, total;

  if ( req->page_width <= 1 && req->page_height <= 1 )
    return 0; /* Ignore 1 x 1 and 0 x 0 images */
  if ( skin_has_framebuffer )
    return 0;

  nBands = (req->page_height + req->band_height - 1) / req->band_height;
  /* N.B. for one separation */

  /* For frame interleaved output we need to multiply
   * the number of bands by the number of channels.
   */
  if ( req->interleavingStyle == interleavingStyle_frame )
    nBands *= req->nChannels;

  bandBytes = (req->band_width >> 3) * req->band_height;

  /* For band interleaved output BandWidth only gives
   * the width of one channel, so need to multiply by
   * the number of channels.
   */
  if ( req->interleavingStyle == interleavingStyle_band )
    bandBytes *= req->nChannels;

  HQASSERT(bandBytes <= UINT32_MAX, "Band size too large");
  compressBytes = gg_compressBound(CAST_SIZET_TO_UINT32(bandBytes));
  HQASSERT(compressBytes >= bandBytes, "Compressed bound too small");

  /* Ensure that we can hold at least one band */
  bandbuffBytes = MAX_MEMORY_CACHE_SIZE_BYTES;
  if ( bandbuffBytes < compressBytes )
    bandbuffBytes = compressBytes;

  total = MemAllocAlign(nBands * sizeof(BandHolder)) +
          MemAllocAlign(bandbuffBytes) +
          MemAllocAlign(compressBytes) +
          MemAllocAlign(nBands * sizeof(BandIndex));

  if ( sizes ) {
    sizes[0] = nBands;
    sizes[1] = bandBytes;
    sizes[2] = compressBytes;
    sizes[3] = bandbuffBytes;
  }
  return total;
}


#ifdef TEST_FRAMEBUFFER


/* KCallRasterFrameBufferRequirements and KCallRasterFrameBufferDestination are
   used for testing purposes only. We allocate a singleton framebuffer for the
   whole page.  The choice of allocating memory outside the rip's memory pool is
   deliberate and emphasises the test nature of these procedures. */


static int32 KCallRasterFrameBufferRequirements(RASTER_REQUIREMENTS * pRasterRequirements,
                                                HqBool fRenderingStarting)
{
  int32 result;
  size_t uFrameBufferSize ;

  SwLeProbe(SW_TRACE_KCALLRASTERREQUIREMENTSCALLBACK, SW_TRACETYPE_ENTER, (intptr_t)0);

  if (!fRenderingStarting) {
    /* This is where a customer implementation might calculate whether
     * there is enough memory to continue. If there is not enough memory,
     * it should set:
     * pRasterRequirements->have_framebuffer = FALSE;
     * pRasterRequirements->handled = TRUE;
     * result = -1;
     * It might also decide to allocate raster memory here in order to
     * ensure that the allocation succeeds (to prevent memory contention later),
     * but note that this function will be called many times before
     * the final page device parameters are established in the RIP. */
    pRasterRequirements->have_framebuffer = TRUE;
    pRasterRequirements->handled = TRUE;
    result = 0;
  }
  else
  {
    /* We are really starting, so allocate the buffer */
    uFrameBufferSize = (( pRasterRequirements->page_height / pRasterRequirements->band_height  ) + 1 ) *
      pRasterRequirements->band_height * pRasterRequirements->line_bytes * pRasterRequirements->components ;

    if (gFrameBufferSize != uFrameBufferSize) {
      if (gFrameBuffer != NULL)
        SysFree(gFrameBuffer);

      gFrameBuffer = (unsigned char *) SysAlloc(uFrameBufferSize);

      HQASSERT(gFrameBuffer != NULL, "Not enough memory to allocate FrameBuffer");
    }
    if (gFrameBuffer != NULL) {
      guPageHeightPixels = pRasterRequirements->page_height;
      gFrameBufferSize = uFrameBufferSize;
      guBandHeight = pRasterRequirements->band_height;
      guBytesPerLine = pRasterRequirements->line_bytes;
      guComponentsPerBand = pRasterRequirements->components_per_band;
      pRasterRequirements->have_framebuffer = TRUE;
      pRasterRequirements->handled = TRUE;
      result = 0;
    }
    else {
      pRasterRequirements->handled = TRUE;
      result = -1;
    }
  }

  SwLeProbe(SW_TRACE_KCALLRASTERREQUIREMENTSCALLBACK, SW_TRACETYPE_EXIT, (intptr_t)0);

  return result;
}


static int32 KCallRasterFrameBufferDestination(RASTER_DESTINATION * pRasterDestination,
                                               int32 nFrameNumber)
{
  int32 result;
  size_t uBandSize = guBandHeight * guBytesPerLine * guComponentsPerBand;

  if (gFrameBuffer == NULL) {
    result = -1;
  }
  else {
    SwLeProbe(SW_TRACE_KCALLRASTERDESTINATIONCALLBACK, SW_TRACETYPE_ENTER, (intptr_t)0);

    pRasterDestination->memory_base = gFrameBuffer +
      (guPageHeightPixels * guBytesPerLine * nFrameNumber) + (uBandSize * pRasterDestination->bandnum);

    pRasterDestination->memory_ceiling = pRasterDestination->memory_base + uBandSize - 1;

    SwLeProbe(SW_TRACE_KCALLRASTERDESTINATIONCALLBACK, SW_TRACETYPE_EXIT, (intptr_t)0);

    pRasterDestination->handled = TRUE;
    result = 0;
  }
  return result;
}

/**
 * \brief Free the framebuffer, if present.
 */
static void freePGBFrameBuffer(void)
{
  if (gFrameBuffer != NULL)
  {
    SysFree(gFrameBuffer);
    gFrameBuffer = NULL;
  }

  gFrameBufferSize = 0;
  guPageHeightPixels = 0;
  guBandHeight = 0;
  guBytesPerLine = 0;
  guComponentsPerBand = 0;
}


#endif /* TEST_FRAMEBUFFER */


static void destroyBandMemory(void)
{
  if ( pgbinparams.PGBSysmem && gBandMemory.mem_base != NULL ) {
    /* If we did a partial-paint and then errored before we had the
     * chance to read it back, we may have left a disk-cache file behind.
     * So have to explicitly delete it to avoid asserts about leaking
     * partial-paint memory below
     */
    deleteDiskCacheFile();
    SysFree(gBandMemory.mem_base);
    ppMemSize -= gBandMemory.total;
    gBandMemory.total = 0;
    gBandMemory.mem_base = NULL;
    gBandMemory.pBandHolders = NULL;
    gBandMemory.pBandMemory = NULL;
    gBandMemory.pCompressionBuffer = NULL;
    gBandMemory.pBandIndex = NULL;
    gBandMemory.nBands = 0;
    gBandMemory.bandSize = 0;
    gBandMemory.cbCompressionBuffer = 0;
    gBandMemory.cbBandMemory = 0;
  }
  HQASSERT(ppMemSize == 0, "Leaking partial paint memory");
}


static HqBool instantiateBandMemory(RASTER_REQUIREMENTS *req)
{
  size_t total, sizes[4] = { 0, 0, 0, 0 };
  uint8 *base;

  total = totalBandMemory(req, sizes);
  if ( pgbinparams.PGBSysmem ) {
    if ( gBandMemory.mem_base != NULL && total > gBandMemory.total ) {
      SysFree(gBandMemory.mem_base);
      gBandMemory.mem_base = NULL;
      ppMemSize -= gBandMemory.total;
    }
    if ( gBandMemory.mem_base == NULL ) {
      gBandMemory.mem_base = (uint8 *)SysAlloc(total);
      if ( gBandMemory.mem_base == NULL )
        return FALSE;
      gBandMemory.total = total;
      ppMemSize += total;
      if ( ppMemSize > ppMemPeak )
        ppMemPeak = ppMemSize;
    }
  } else {
    HQASSERT(req->have_framebuffer || req->scratch_band != NULL ||
             pgbinparams.PrintPage, "Didn't get any band memory");
    HQASSERT(total <= req->scratch_size, "Didn't get enough band memory");
    gBandMemory.mem_base = req->scratch_band;
  }
  gBandMemory.nBands = sizes[0];
  gBandMemory.bandSize = sizes[1];
  gBandMemory.cbCompressionBuffer = sizes[2];
  gBandMemory.cbBandMemory = sizes[3];

  if ( skin_has_framebuffer )
    return TRUE;

  base = gBandMemory.mem_base;
  gBandMemory.pBandHolders = (BandHolder *)base;
  base += MemAllocAlign(gBandMemory.nBands * sizeof(BandHolder));
  gBandMemory.pBandMemory = base;
  base += MemAllocAlign(gBandMemory.cbBandMemory);
  gBandMemory.pCompressionBuffer = base;
  base += MemAllocAlign(gBandMemory.cbCompressionBuffer);
  gBandMemory.pBandIndex = (BandIndex *)base;
  return TRUE;
}


/**
 * \brief Initialize a BandCache structure for the current separation.
 */
static void initBandCache( PGBDescription *pPGB, HqBool first_time)
{
  HQASSERT(pBandCache != NULL, "No band cache");

  if ( pBandCache->separationIndex != -1 )
    updateDiskCache();
  pBandCache->separationIndex = pPGB->rd.separation - 1;

  HQASSERT(gBandMemory.nBands >= (uint32)pPGB->n_bands_in_page, "Not enough bands");
  if ( first_time || !pgbinparams.PGBSysmem ) {
    HqMemZero(gBandMemory.pBandHolders, gBandMemory.nBands*sizeof(BandHolder));
  }
  pBandCache->pBands = gBandMemory.pBandHolders;
  HqMemZero( gBandMemory.pBandIndex, gBandMemory.nBands * sizeof(BandIndex) );
  pBandCache->pBandIndex = gBandMemory.pBandIndex;
}


/**
 * \brief If a BandCache exists, close (delete) it.
 */
static void closeBandCache(void)
{
  if ( pBandCache == NULL )
    return;
  destroyBandMemory();
  gBandMemory.mem_base = NULL;
  bandCache_Free( pBandCache );
  pBandCache = NULL;
}


/**
 * \brief Initialises the full path name of the disk cache file.  The
 * file will be in the SW\\tmp directory, and will have the name
 * specified by pszLeafDiskCacheFileName.
 *
 * \param[in] pszLeafDiskCacheFileName Leaf name of the disk cache file.
 *
 * \return TRUE on success; FALSE otherwise.
 */
static HqBool initDiskCacheFileName( const char * pszLeafDiskCacheFileName )
{
  HQASSERT(pszLeafDiskCacheFileName != NULL, "No disk cache file name");

  if ( pszDiskCacheFullFileName )
    return TRUE;

  pszDiskCacheFullFileName = MemAlloc( LONGESTFILENAME, TRUE, FALSE );
  if ( pszDiskCacheFullFileName == NULL )
    return FALSE;

  strcpy(pszDiskCacheFullFileName, "PGB/" );
  (void)strncat( pszDiskCacheFullFileName, pszLeafDiskCacheFileName,
                 LONGESTFILENAME - 5 );
  return TRUE;
}


/**
 * \brief If a disk cache file exists, delete it.
 */
static void deleteDiskCacheFile(void)
{
  if ( pgbinparams.PGBSysmem ) {
    uint32 i;

    if ( pBandCache && pBandCache->pBands ) {
      for ( i = 0; i < pBandCache->numBands; i++ )
      {
        BandHolder *pBandHolder = &pBandCache->pBands[i];

        if ( pBandHolder->dc_in_mem ) {
          SysFree(pBandHolder->dc_in_mem);
          pBandHolder->dc_in_mem = NULL;
          ppMemSize -= pBandHolder->bandLength;
        }
      }
    }
    return;
  }
  if (pszDiskCacheFullFileName)
    (void) (*theIDeleteFile(KGetBandCacheDev())) (KGetBandCacheDev(), (uint8*)pszDiskCacheFullFileName);
}


/**
 * \brief Opens the disk cache file for reading and writing, and
 * stores the file descriptor of the opened cache file in bandCacheFD
 * for later use.
 *
 * \return TRUE on success; FALSE otherwise.
 */
static int32 openDiskCacheFile(void)
{
  HQASSERT(pszDiskCacheFullFileName != NULL, "No disk cache filename");
  if ( ! pszDiskCacheFullFileName )
    return FALSE;

  return ( ( bandCacheFD = (*theIOpenFile(KGetBandCacheDev()))(KGetBandCacheDev(), (uint8*)pszDiskCacheFullFileName, SW_RDWR) ) != BAND_CACHE_FD_UNSET );
}


/**
 * \brief Tests for existence of the disk cache file.
 *
 * \return TRUE if the file exists on disk; FALSE otherwise.
 */
static int32 diskCacheFileExists(void)
{
  struct STAT statusBuffer;

  if ( pgbinparams.PGBSysmem ) {
    return TRUE;
  }
  HQASSERT(pszDiskCacheFullFileName != NULL, "No disk cache filename");
  if ( ! pszDiskCacheFullFileName )
    return FALSE;

  return ((*theIStatusFile(KGetBandCacheDev()))(KGetBandCacheDev(), (uint8*)pszDiskCacheFullFileName, &statusBuffer) != -1);
}


/**
 * \brief Creates the disk cache file, and leaves it open for writing,
 * assigning its file descriptor to bandCacheFD.
 *
 * \return TRUE on success; FALSE otherwise.
 */
static HqBool makeDiskCacheFile(void)
{
  HqBool fResult;

  HQASSERT(pszDiskCacheFullFileName != NULL, "No disk cache filename");
  if ( ! pszDiskCacheFullFileName ) return FALSE;
  HQASSERT(!diskCacheFileExists(), "Disk cache file exists");
  if ( diskCacheFileExists() ) return FALSE;

  fResult = ( ( bandCacheFD = (*theIOpenFile( KGetBandCacheDev() ))( KGetBandCacheDev(), (uint8*)pszDiskCacheFullFileName, SW_RDWR | SW_CREAT) ) != BAND_CACHE_FD_UNSET );

  if ( fResult && theIIoctl( KGetBandCacheDev() ) )
  {
    /* Deactivate any automatic compression on the band cache backing store.
       (Currently, this is a no-op unless the backing store is a RAM device).
       The data in the band cache is already compressed, so further compression
       is counter-productive. We only need to do this when creating the file,
       because the setting persists. */
    (void) (*theIIoctl( KGetBandCacheDev()))( KGetBandCacheDev(), bandCacheFD,
                                              DeviceIOCtl_SetCompressionMode,
                                              IOCTL_COMPRESSION_OFF );
  }

  return fResult;
}


/**
 * \brief Close the disk cache file if it is open.
 */
static void closeDiskCacheFile(void)
{
  if ( pgbinparams.PGBSysmem )
    return;
  if ( bandCacheFD != BAND_CACHE_FD_UNSET )
  {
    (void) ( *theICloseFile(KGetBandCacheDev()))( KGetBandCacheDev(), bandCacheFD);
    bandCacheFD = BAND_CACHE_FD_UNSET;
  }
}


/**
 * \brief Obtain the length, in bytes, of the disk cache file.  The
 * file should have been opened with openDiskCacheFile or
 * makeDiskCacheFile before calling.
 *
 * \param pNumBytes If it exists, the number of bytes in the disk
 * cache file will be written into this argument.
 *
 * \return TRUE on success; FALSE otherwise.
 */
static HqBool getLengthOfDiskCacheFile( Hq32x2 * pNumBytes )
{
  HqBool fResult = FALSE;
  if ( bandCacheFD != BAND_CACHE_FD_UNSET )
  {
    if ( ! (*theIBytesFile(KGetBandCacheDev()))(KGetBandCacheDev(),bandCacheFD, pNumBytes, SW_BYTES_TOTAL_ABS) )
      goto end;

    fResult = TRUE;
  }
 end:
  return fResult;
}


/**
 * \brief Obtains the disk cache file's header information on the band
 * numbered bandNum.  This information is the byte location of the
 * band, and the length of the band, within the disk cache file.
 *
 * \param[in] bandIndex The index number of the band for which to get
 * information.
 *
 * \param[out] pLocation On success, the location of the band will be
 * written into this argument.  This value could be zero if the band
 * has not yet been written out to this PageBuffer device.
 *
 * \param[out] pLength On success, the length in bytes of the band will be
 * written into this argument.  This value could be zero if the band
 * has not yet been written out to this PageBuffer device.
 *
 * \return TRUE on success; FALSE otherwise.
 */
static HqBool getBandInfoFromDiskCache( uint32 bandIndex, Hq32x2 * pLocation, int32 * pLength )
{
  Hq32x2 destination;
  BandIndex indexEntry = {HQ32X2_INIT_ZERO, 0};
  HqBool fResult = FALSE;

  if ( bandCacheFD == BAND_CACHE_FD_UNSET )
    goto end;

  /* seek to absolute position within cache file where this band's
     location info will be */
  Hq32x2FromUint32( &destination, bandIndex * sizeof( BandIndex ) );
  if ( ! ( *theISeekFile( KGetBandCacheDev() ))( KGetBandCacheDev() , bandCacheFD, &destination, SW_SET ) )
    goto end;

  /* read the location info */
  if ( ( *theIReadFile( KGetBandCacheDev()))( KGetBandCacheDev() ,
                   bandCacheFD,
                   (uint8*) &indexEntry,
                   sizeof( BandIndex ) ) < 0 )
    goto end;

  *pLocation = indexEntry.bandLocation;
  *pLength = indexEntry.bandLength;

  fResult = TRUE;
 end:
  return fResult;

}


/**
 * \brief Create a new disk cache file, if it does not exist already.
 *
 * \return TRUE on success; FALSE otherwise.
 */
static int32 createNewDiskCache(void)
{
  /* should not be in this function if disk cache exists */
  HQASSERT(! diskCacheFileExists(), "Disk cache already exists");
  if ( diskCacheFileExists() )
    return FALSE;

  /* make the new disk cache file */
  if ( ! makeDiskCacheFile() )
    return FALSE;

  return TRUE;

}


/**
 * \brief Update the disk cache, flushing all currently in-memory
 * bands to the cache.  This function creates a disk cache if one does
 * not already exist.
 *
 * The disk cache is a file containing compressed band data.  It has a
 * header section, which is an index of band positions and lengths,
 * consisting of an unsigned 32-bit integer for each of the values.
 * Seeking within the disk cache involves reading the header entry for
 * the band number required, and seeking to the position described in
 * the entry.  Reading from the disk cache then involves reading the
 * band length entry's number of bytes from the current byte position.
 * Writing to the disk cache involves writing the band to the end of
 * the file when the band arrives in the PageBuffer device for the
 * first time.  If an updated version of the band is sent to disk
 * again, updateDiskCache will check the size of the original
 * compressed band: if the new band is not bigger, it can be written
 * into its original slot (with the length part of the header entry
 * for the band updated if necessary).  If the updated band is too big
 * for its original slot in the disk cache file, it is written to the
 * end of the file, and the position part of the header entry is
 * updated to point to the new location, as well as the length part
 * being updated.  This means that if bands are updated and after
 * compression are bigger than they were before, the disk cache file
 * will become fragmented.
 *
 * \return TRUE on success; FALSE otherwise.
 */
static HqBool updateDiskCache(void)
{
  uint32 i;
  Hq32x2 currentPosition;
  int32 fResult = FALSE;
  int32 fDiskCacheExisted = FALSE;
  BandIndex * pBandIndex = NULL;

  HQASSERT(pBandCache != NULL, "No band cache");

  if ( pgbinparams.PGBSysmem ) {
    for ( i = 0; i < pBandCache->numBands; i++ ) {
      BandHolder *pBandHolder = &pBandCache->pBands[i];

      if ( pBandHolder->pData == BAND_SLOT_EVACUATED ) {
        ;
      } else if ( pBandHolder->pData == BAND_NOT_PRESENT_IN_TABLE ) {
        ;
      } else {
        HQASSERT(pBandHolder->dc_in_mem == NULL,
                 "Band should not be stored in memory") ;
        pBandHolder->dc_in_mem = SysAlloc(pBandHolder->bandLength);
        if ( pBandHolder->dc_in_mem == NULL )
          return FALSE;
        HqMemCpy(pBandHolder->dc_in_mem, pBandHolder->pData,
                 pBandHolder->bandLength);
        pBandHolder->pData = BAND_SLOT_EVACUATED;
        ppMemSize += pBandHolder->bandLength;
        if ( ppMemSize > ppMemPeak )
          ppMemPeak = ppMemSize;
      }
    }
    /* The entire in-memory cache has now been flushed to disk, so
       re-set the cache size. */
    pBandCache->totalMemoryCacheSize = 0;
    return TRUE;
  }

  /* Consider the current position in file to be the byte _after_
     where we will write the band location index, in a new file; or
     else at the end of an existing file.  The currentPosition
     variable is used to decide where a band will be written */
  if ( ! diskCacheFileExists() )
  {
    uint32 indexSize;
    if ( ! createNewDiskCache() )
      goto end;
    indexSize = sizeof( BandIndex ) * pBandCache->numSeparations * pBandCache->numBands + 1;
    Hq32x2FromUint32( &currentPosition, indexSize );
  }
  else
  {
    if ( ! openDiskCacheFile() )
      goto end;
    if ( ! getLengthOfDiskCacheFile( &currentPosition ) )
      goto end;
    fDiskCacheExisted = TRUE;
  }

  HQASSERT(pBandCache->pBandIndex != NULL, "No band cache band index");
  pBandIndex = pBandCache->pBandIndex;

  if ( fDiskCacheExisted )
  {
    Hq32x2 destination;

    if ( pBandCache->separationIndex > 0 )
    {
      /* Seek to index for this separation */
      Hq32x2FromInt32( &destination, pBandCache->separationIndex * pBandCache->numBands * sizeof( BandIndex ) );
      if ( ! ( *theISeekFile( KGetBandCacheDev() ))( KGetBandCacheDev(), bandCacheFD, &destination, SW_SET ) )
        goto end;
    }
    else
    {
      /* First separation's index is at start of file, which is where we are
       * as we've just opened the file.
       */
      Hq32x2FromInt32( &destination, 0 );
    }

    /* Read index for all of this separation */
    if ( ( *theIReadFile( KGetBandCacheDev() ))( KGetBandCacheDev() ,
                     bandCacheFD,
                     (uint8*) pBandIndex,
                     sizeof( BandIndex ) * pBandCache->numBands) < 0 )
      goto end;

    /* Set file position back to start of index, so we can overwrite it later */
    if ( ! ( *theISeekFile( KGetBandCacheDev() ))( KGetBandCacheDev(), bandCacheFD, &destination, SW_SET ) )
      goto end;
  }

  for ( i = 0; i < pBandCache->numBands; i++ )
  {
    BandHolder *pBandHolder = &pBandCache->pBands[i];

    if ( pBandHolder->pData == BAND_SLOT_EVACUATED )
    {
      /* Band is on disk but not in memory, so nothing to update for
         this band. */
      continue;
    }
    else if ( pBandHolder->pData != BAND_NOT_PRESENT_IN_TABLE )
    {
      Hq32x2 bandLength;

      /* This band is in memory; however, it could also be on disk;
         the one in memory would be its replacement, if so. */
      if ( pBandIndex[ i ].bandLength != NO_BAND_YET )
      {
        /* band IS also on disk.  will it fit in its original
           position?  if not, it must be moved to the end. */
        if ( pBandHolder->bandLength > pBandIndex[ i ].bandLength  )
        {
          pBandIndex[ i ].bandLocation = currentPosition;
        }

        /* Whether it fit in its original slot or had to be moved to
           the end, we must update the length stored in the table. */
        pBandIndex[ i ].bandLength = pBandHolder->bandLength;
      }
      else
      {
        /* Band is in memory but not yet on disk, so we will write it
           at the end of the disk file */
        pBandIndex[ i ].bandLocation = currentPosition;
        pBandIndex[ i ].bandLength = pBandHolder->bandLength;
      }

      Hq32x2FromUint32( &bandLength, pBandHolder->bandLength );
      Hq32x2Add( &currentPosition, &currentPosition, &bandLength );
    }
    else
    {
      /* Band is not present this time around.  If this is the first time then
       * initialise the location slot empty.  Otherwise the band on disk is
       * unchanged. */
      if ( !fDiskCacheExisted )
        pBandIndex[ i ].bandLength = NO_BAND_YET;
    }
  }

  /* Write out the location index, overwriting any previous index */
  if ( ( *theIWriteFile(KGetBandCacheDev()))( KGetBandCacheDev(),
                    bandCacheFD,
                    (uint8*) pBandIndex,
                    sizeof( BandIndex ) * pBandCache->numBands) < 0 )
    goto end;


  /* Now write out the bands themselves, using the in-memory version
   * of the location index to seek to each appropriate position.  If
   * bands are on disk anyway, they won't be overwritten, but if
   * replacements for them are in memory, what happens depends on the
   * length of the replacement band.  If the band is longer than it
   * was when last written to disk, it must be written back at the end
   * of the disk file; if it is the same length or shorter, it can be
   * written in its existing disk location. */

  for ( i = 0; i < pBandCache->numBands; i++ )
  {
    BandHolder *pBandHolder = &pBandCache->pBands[i];
    if ( pBandHolder->pData == BAND_SLOT_EVACUATED )
    {
      /* Band is on disk but not in memory, so nothing to update for
         this band. */
      continue;
    }
    else if ( pBandHolder->pData != BAND_NOT_PRESENT_IN_TABLE )
    {
      /* Band is in memory, and its location has been patched up
         above, so can be written to disk. */
      Hq32x2 destination = pBandIndex[ i ].bandLocation;
      if ( ! ( *theISeekFile( KGetBandCacheDev() ))( KGetBandCacheDev() , bandCacheFD, &destination, SW_SET) )
        goto end;
      if ( ( *theIWriteFile(KGetBandCacheDev()))( KGetBandCacheDev(),
                        bandCacheFD,
                        pBandHolder->pData,
                        pBandHolder->bandLength) < 0 )
        goto end;
      pBandHolder->pData = BAND_SLOT_EVACUATED;
    }
  }

  /* If this is the first update and we're doing separated output
   * initialise the index for the other separations now so that we
   * have good values to read back next time.
   */
  if ( !fDiskCacheExisted && pBandCache->numSeparations > 1 )
  {
    for ( i = 0; i < pBandCache->numBands; i++ )
    {
      pBandIndex[ i ].bandLength = NO_BAND_YET;
    }

    for ( i = 0; i < pBandCache->numSeparations; i++ )
    {
      if ( i != (uint32) pBandCache->separationIndex )
      {
        Hq32x2 destination;

        Hq32x2FromInt32( &destination, i * pBandCache->numBands * sizeof( BandIndex ) );
        if ( ! ( *theISeekFile( KGetBandCacheDev() ))( KGetBandCacheDev(), bandCacheFD, &destination, SW_SET ) )
          goto end;

        if ( ( *theIWriteFile(KGetBandCacheDev()))( KGetBandCacheDev(),
                          bandCacheFD,
                          (uint8*) pBandIndex,
                          sizeof( BandIndex ) * pBandCache->numBands) < 0 )
          goto end;
      }
    }
  }

  /* The entire in-memory cache has now been flushed to disk, so
     re-set the cache size. */
  pBandCache->totalMemoryCacheSize = 0;
  fResult = TRUE;

 end:
  closeDiskCacheFile();
  if ( ! fResult )
    deleteDiskCacheFile();

  return fResult;
}


/**
 * \brief Allocates the next len bytes of gBandMemory.pBandMemory for use by
 * a band.  If there is insufficient free memory in gBandMemory.pBandMemory
 * then the entire in-memory band-cache is first flushed to a disk-based
 * cache to free up the required memory.
 *
 * \param[in] len The size of the band.
 *
 * \return Pointer to memory to hold the band on success; NULL otherwise.
 */
static uint8 * getMemoryForBand( uint32 len )
{
  uint8 * pBand = NULL;

  HQASSERT(gBandMemory.cbBandMemory >= len, "Band memory size too short");

  if ( pBandCache->totalMemoryCacheSize + len > gBandMemory.cbBandMemory )
  {
    if ( ! updateDiskCache() )
      goto end;
    HQASSERT(pBandCache->totalMemoryCacheSize == 0,
             "Total band cache size non-zero");
  }

  pBand = gBandMemory.pBandMemory + pBandCache->totalMemoryCacheSize;
  pBandCache->totalMemoryCacheSize += len;

 end:
  return pBand;
}


/**
 * \brief Store bytes in the PGB store.
 *
 * \param[in] pDeviceState The pagebuffer device state.
 * \param[in] pPGB         The PGB containing the store that will be accessed.
 * \param[in] pBuff        The buffer containing the bytes to be stored.
 * \param[in] length       The number of bytes to read from pBuff.
 *
 * \return TRUE if store succeeded; FALSE otherwise.
 */
static HqBool storeBandInCache( PGBDeviceState *pDeviceState,
                                PGBDescription *pPGB,
                                uint8 *pBuff, uint32 length )
{
  HqBool fResult = FALSE;
  uint32 destinationLen = 0 /* pacify compiler */;
  uint8 * pDestination = NULL;
  int32 compressResult = Z_OK;

  SwLeProbe(SW_TRACE_STOREBANDINCACHE, SW_TRACETYPE_ENTER, (intptr_t)0);

  HQASSERT(pPGB != NULL, "No PGB handle");
  HQASSERT(pBuff != NULL, "No band buffer to store");

  HQASSERT(gBandMemory.bandSize >= length, "Band size too small");

  if ( pgbinparams.fAllowBandCompression ) {
    /* Compress data into a buffer which has already been allocated to be large enough */
    destinationLen = gg_compressBound( length );
    HQASSERT(gBandMemory.pCompressionBuffer != NULL, "No compression buffer");
    HQASSERT(gBandMemory.cbCompressionBuffer >= destinationLen,
             "Compression buffer size too small");
    pDestination = gBandMemory.pCompressionBuffer;

    compressResult = gg_deflate( &(pDeviceState->compress_stream),
                                 pDestination,
                                 &destinationLen,
                                 pBuff,
                                 length );
  }
  else
  {
    /* We don't want to compress band data. */
    destinationLen = length;
    pDestination = pBuff;
  }

  switch ( compressResult )
  {
   case Z_OK:
   {
     uint8 * pExact;     /* buffer into which exact bytes can be copied */

     /* Make sure any existing band in the memory cache is discarded, rather
        than flushed to disk. */
     pBandCache->pBands[pPGB->seek_band].pData = BAND_NOT_PRESENT_IN_TABLE;
     pExact = getMemoryForBand( destinationLen );
     if ( pExact == NULL )
       goto end;
     HqMemCpy( pExact, pDestination, destinationLen );
     /* The memory for the previous data in gBandMemory.pBandMemory will be
        reclaimed when the in-memory cache is next flushed to disk. */
     pBandCache->pBands[pPGB->seek_band].pData = pExact;
     pBandCache->pBands[pPGB->seek_band].bandLength = destinationLen;
     fResult = TRUE;
     break;
   }
   case Z_MEM_ERROR:
   case Z_BUF_ERROR:
   case Z_STREAM_ERROR:
   {
     break;
   }
   default:
     HQFAIL("Unexpected zlib result");
  }

 end:
  SwLeProbe(SW_TRACE_STOREBANDINCACHE, SW_TRACETYPE_EXIT, (intptr_t)0);
  return fResult;
}


/**
 * \brief Seeks within the disk cache file to the start position of
 * the band requested, with the length of the band returned in
 * pLength.
 *
 * \param[in] bandIndex The index number of the band for which to get
 * information.
 *
 * \param[out] pLength The number of bytes in the band bandNum will be
 * written into this value.
 *
 * \return TRUE on success; FALSE otherwise.
 */
static HqBool seekToBandInDiskCache( uint32 bandIndex, int32 * pLength )
{
  HqBool fResult = FALSE;
  Hq32x2 bandLocation;
  int32  bandLength;

  /* if the cache file isn't open, error */
  if ( bandCacheFD == BAND_CACHE_FD_UNSET )
    goto end;

  if ( ! getBandInfoFromDiskCache( bandIndex, &bandLocation, &bandLength ) )
    goto end;

  if (bandLength != 0) {
    /* now have location and length of band */
    if ( ! ( *theISeekFile( KGetBandCacheDev() ))( KGetBandCacheDev() , bandCacheFD, &bandLocation, SW_SET) )
      goto end;
  }

  *pLength = bandLength;
  fResult = TRUE;

 end:
  return fResult;
}


/**
 * \brief Reads the requested number of bytes from the current seek
 * position in the disk cache file, with the bytes returned in
 * ppBuffer, which must be allocated by the caller and of the correct
 * length.
 *
 * \param pBuffer The output buffer
 *
 * \param length The number of bytes to read, and write into ppBuffer.
 *
 * \return TRUE on success; FALSE otherwise.
 */
static HqBool readBytesFromDiskCache( uint8 *pBuffer, int32 length )
{
  HqBool fResult = FALSE;

  /* if the cache file isn't open, error */
  if ( bandCacheFD == BAND_CACHE_FD_UNSET )
    goto end;

  /* we assume seekToBandInDiskCache has been called first */
  if ( ( *theIReadFile( KGetBandCacheDev() ))( KGetBandCacheDev(), bandCacheFD,
                                               pBuffer, length) < 0 )
    goto end;

  fResult = TRUE;

 end:
  return fResult;
}


/**
 * \brief Reads the band with the given index number from the disk
 * cache file.
 *
 * \param[out] pBandHolder A pointer to BandHolder that will be set to describe
 *                      the band.
 *
 * \param[in] bandIndex The index number of the band for which to get
 * information.
 *
 * \return TRUE on success; FALSE otherwise.
 */
static HqBool getBandFromDiskCache( BandHolder *pBandHolder, uint32 bandIndex )
{
  int32 bandLength = 0;
  HqBool fResult = FALSE;

  if ( pgbinparams.PGBSysmem ) {
    BandHolder *bh = &pBandCache->pBands[bandIndex];
    if ( bh->dc_in_mem ) {
      pBandHolder->pData = gBandMemory.pCompressionBuffer;
      pBandHolder->bandLength = bh->bandLength;
      HqMemCpy(gBandMemory.pCompressionBuffer, bh->dc_in_mem, bh->bandLength);
      SysFree(bh->dc_in_mem) ;
      bh->dc_in_mem = NULL ;
      ppMemSize -= bh->bandLength ;
    } else {
      /* Never seen this band before, must be zero one that has been trimmed */
      pBandHolder->pData = NULL;
      pBandHolder->bandLength = 0;
    }
    return TRUE;
  }

  if ( ! openDiskCacheFile() )
    goto end;

  if ( ! seekToBandInDiskCache( bandIndex, &bandLength ) )
    goto end;

  if ( bandLength == 0 ) {
    pBandHolder->pData = NULL;
    fResult = TRUE;
    goto end;
  }

  HQASSERT(gBandMemory.cbCompressionBuffer >= (size_t)bandLength,
           "Compression buffer size too small");
  if ( ! readBytesFromDiskCache( gBandMemory.pCompressionBuffer, bandLength ) )
    goto end;
  pBandHolder->pData = gBandMemory.pCompressionBuffer;
  pBandHolder->bandLength = bandLength;
  fResult = TRUE;

 end:
  closeDiskCacheFile();

  return fResult;
}


/**
 * \brief Read the band described by the state of a PGBDescription
 * into a buffer, either from memory or from disk.  This is the
 * highest level getter function for bands.
 *
 * \param[in] pPGB The PGB descriptor whose seek band is obtained.
 *
 * \param[out] pBuff The buffer into which the band data is read.
 * This must be allocated by the caller -- ultimately the RIP itself.
 *
 * \param[in,out] pExpectedLength Expected length of the decompressed data.
 *
 * \return TRUE on success; FALSE otherwise.
 */
static HqBool getBand( PGBDescription *pPGB,
                       uint8 *pBuff, uint32 *pExpectedLength )
{
  BandHolder bandHolder = { NULL, 0 };
  BandHolder * pBandHolder;
  HqBool fResult = FALSE;

  /* Calculate bandIndex using pBandCache->numBands for the read back.
     Separation omission between render passes means the number of bands may
     change and therefore the read back band index should use the number of
     bands in the cache instead of pPGB->n_bands_in_page. */
  int32 bandIndex = (pPGB->rd.separation - 1) * pBandCache->numBands
                    + pPGB->seek_band;

  if ( pBandCache->separationIndex == pPGB->rd.separation - 1 )
  {
    /* Band is from the cached separation */
    int32 cacheIndex = pPGB->seek_band;

    /* Either band has been flushed to disk or hasn't been read in again yet. */
    if ( pBandCache->pBands[cacheIndex].pData == BAND_SLOT_EVACUATED ||
         pBandCache->pBands[cacheIndex].pData == BAND_NOT_PRESENT_IN_TABLE )
    {
      if ( diskCacheFileExists() )
      {
        pBandHolder = &bandHolder;
        if ( ! getBandFromDiskCache( pBandHolder, bandIndex ) )
          goto end;
      }
      else
      {
        pBandHolder = NULL;
      }
    }
    else
    {
      pBandHolder = &pBandCache->pBands[cacheIndex];
    }
  }
  else
  { /* Band is not from the cached separation */
    pBandHolder = &bandHolder;
    if ( ! getBandFromDiskCache( pBandHolder, bandIndex ) )
      goto end;
  }

  if ( pBandHolder == NULL || pBandHolder->pData == NULL )
  {
    /* If RIP requests a band that it did not provide earlier, it must be a
     * zero band that is omitted during partial paint and in !WriteAllOutput
     * mode. We need to fulfill this request by returning a zero band. */
    HqMemZero(pBuff, *pExpectedLength) ;
    return TRUE;
  }

  if ( pgbinparams.fAllowBandCompression ) {
    /* Decompress the band data. */
    int32 uncompressResult = gg_uncompress( pBuff, pExpectedLength,
                                            pBandHolder->pData, pBandHolder->bandLength );

    switch ( uncompressResult )
    {
    case Z_OK:
      {
        fResult = TRUE;
        break;
      }
    case Z_MEM_ERROR:
    case Z_BUF_ERROR:
      {
        break;
      }
    default:
      {
        /* unexpected */
        HQFAIL("Unexpected zlib result");
      }
    }
  }
  else
  {
    /* Band data was stored uncompressed. */
    HQASSERT(*pExpectedLength == pBandHolder->bandLength,
             "Band data length incorrect");
    HqMemCpy(pBuff, pBandHolder->pData, *pExpectedLength);
    fResult = TRUE;
  }

 end:
  return fResult;
}


void init_C_globals_pgbdev(void)
{
  static PageBufferInParameters pgbinparams_save ;
  static PageBufferOutParameters pgboutparams_save ;
  static HqBool first_time_ever = TRUE;

  if (first_time_ever) {
    first_time_ever = FALSE ;
    HqMemCpy(&pgbinparams_save, &pgbinparams, sizeof(pgbinparams)) ;
    HqMemCpy(&pgboutparams_save, &pgboutparams, sizeof(pgboutparams)) ;
  } else {
    HqMemCpy(&pgbinparams, &pgbinparams_save, sizeof(pgbinparams)) ;
    HqMemCpy(&pgboutparams, &pgboutparams_save, sizeof(pgboutparams)) ;
  }

  HqMemZero(&gBandMemory, sizeof(gBandMemory));
  bandCacheFD = BAND_CACHE_FD_UNSET;
  pszDiskCacheFullFileName = NULL;
  pBandCache = NULL;
  p_band_directory = NULL;
  ppMemSize = ppMemPeak = 0;
#ifdef CHECKSUM
  /* Do not reset this as its set externally via the -x option.
  fPrintChecksums = FALSE;
  */
  crc = 0;
  crcfunc = NULL;
#endif
  pgb_tl = SW_TL_REF_INVALID;
  pBandCacheDevice = NULL;
  g_pgb = NULL;
  override_to_none = FALSE;
  last_param = 0;

  skin_has_framebuffer = FALSE;
#ifdef TEST_FRAMEBUFFER
  gFrameBuffer = NULL;
  gFrameBufferSize = 0;
#endif
  guPageHeightPixels = 0;
  guBandHeight = 0;
  guBytesPerLine = 0;
  guComponentsPerBand = 0;
}

/* end of pgbdev.c */
