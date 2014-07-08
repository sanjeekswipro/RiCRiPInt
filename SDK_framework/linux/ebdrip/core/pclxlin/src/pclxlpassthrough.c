/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlpassthrough.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * "PassThrough" operator handling functions
 */

#include "core.h"
#include "display.h"
#include "swdevice.h"
#include "lists.h"
#include "mm.h"
#include "gcscan.h"
#include "objects.h"
#include "objnamer.h"
#include "fileio.h"
#include "devices.h"
#include "devs.h"
#include "hqmemcpy.h"
#include "hqmemcmp.h"
#include "matrix.h"
#include "gstack.h"
#include "graphics.h"
#include "timing.h"

#include "pclxlattributes.h"
#include "pclxlparsercontext.h"
#include "pclxldebug.h"
#include "pclxlscan.h"
#include "pclxlerrors.h"
#include "pclxlpassthrough.h"
#include "pclxlpsinterface.h"
#include "pclxloperators.h"
#include "pclxlpage.h"
#include "pcl.h"

#define MAX_STREAM_NEST_LEVEL 1
#define STREAM_CLOSED -1
#define PASS_THROUGH_DATA_BLOCK_SIZE 4096

typedef struct PCLXLPassThroughReadInstance {
  /* Basically a read stream file descriptor. */
  int32 read_id ;
  /* When a pass through is opened for reading, we layer a FILELIST
     onto it. */
  PCLXL_PARSER_CONTEXT parser_context ;
  FILELIST read_stream ;
  uint8 read_buf[PASS_THROUGH_DATA_BLOCK_SIZE] ;
} PCLXLPassThroughReadInstance ;

/* How many read streams do we have? */
static uint32 num_read_streams ;
static PCLXLPassThroughReadInstance read_streams[MAX_STREAM_NEST_LEVEL] ;

/* ============================================================================
 * PCL XL pass through device.
 * ============================================================================
 */

/* This is so that the PCL5c parser can be invoked on a FILELIST which
   obtains its data from one or more PassThrough PCL XL commands which
   require a PCLXL_STREAM */

#define PCLXL_PASS_THROUGH_DEVICE_TYPE (35)

#define PCLXL_PASS_THROUGH_DEVICE_OBJECT_NAME "PCL XL pass through stream device"

#define PCLXL_PASS_THROUGH_DEVICE_NAME "_pclxl_pass_through_device"

/* The device from which we will attempt to read all pass through
   streams. */
DEVICELIST *pclxl_pass_through_device = NULL ;

/* Maximum PCL XL pass through device id. */
#define PCLXL_PASS_THROUGH_DEVICE_MAX_ID (0xff)

/* List of all mounted PCL XL pass through devices.
 *
 * The list of PCLXL_PASS_THROUGH devices is kept in increasing
 * PCLXL_PASS_THROUGH_DEVICE::pclxl_pass_through_id order. When a PCL
 * XL pass through device is mounted, the smallest unused id is reused
 * and the new device inserted into the list in order.
 */
static dll_list_t dls_pclxl_pass_through_devs ;

typedef struct PCLXL_PASS_THROUGH_DEVICE {
  dll_link_t           link ;                  /* PCL XL pass through device list link. */
  int32                pclxl_pass_through_id ; /* Mounted PCL XL pass through device id. */

  OBJECT_NAME_MEMBER
} PCLXL_PASS_THROUGH_DEVICE ;

/* ============================================================================
 * PCL XL pass through device functions.
 * ============================================================================
 */
static int32 RIPCALL pclxl_pass_through_init_device(DEVICELIST * dev)
{
  int32 pclxl_pass_through_id ;
  PCLXL_PASS_THROUGH_DEVICE* pclxl_pass_through_device ;
  PCLXL_PASS_THROUGH_DEVICE* pclxl_pass_through_deviceT ;

  devices_set_last_error(DeviceNoError);

  pclxl_pass_through_device = (PCLXL_PASS_THROUGH_DEVICE*)dev->private_data;

  /* Name object as memory has been allocated - by the RIP */
  NAME_OBJECT(pclxl_pass_through_device, PCLXL_PASS_THROUGH_DEVICE_OBJECT_NAME) ;

  /* Find unused PCL XL id to use. Devices are held in ascending id order. */
  pclxl_pass_through_id = 0 ;
  pclxl_pass_through_deviceT = DLL_GET_HEAD(&dls_pclxl_pass_through_devs, PCLXL_PASS_THROUGH_DEVICE, link) ;
  while ( (pclxl_pass_through_deviceT != NULL) && (pclxl_pass_through_id == pclxl_pass_through_deviceT->pclxl_pass_through_id) ) {
    pclxl_pass_through_deviceT = DLL_GET_NEXT(pclxl_pass_through_deviceT, PCLXL_PASS_THROUGH_DEVICE, link) ;
    pclxl_pass_through_id++ ;
  }
  if ( pclxl_pass_through_id > PCLXL_PASS_THROUGH_DEVICE_MAX_ID ) {
    devices_set_last_error(DeviceLimitCheck) ;
    return -1 ;
  }
  HQASSERT(((pclxl_pass_through_deviceT == NULL) || (pclxl_pass_through_id < pclxl_pass_through_deviceT->pclxl_pass_through_id)),
           "Active pclxl_pass_through device list out of order") ;
  pclxl_pass_through_device->pclxl_pass_through_id = pclxl_pass_through_id ;
  /* Add device in sequence */
  DLL_RESET_LINK(pclxl_pass_through_device, link) ;
  if ( pclxl_pass_through_deviceT == NULL ) {
    DLL_ADD_TAIL(&dls_pclxl_pass_through_devs, pclxl_pass_through_device, link) ;
  } else {
    DLL_ADD_BEFORE(pclxl_pass_through_deviceT, pclxl_pass_through_device, link) ;
  }

  devices_set_last_error(DeviceNoError) ;
  return 0 ;
}

static DEVICE_FILEDESCRIPTOR RIPCALL pclxl_pass_through_open_file(DEVICELIST * dev,
                                                                  uint8 * filename, int32 openflags)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  UNUSED_PARAM(uint8 *, filename) ;
  UNUSED_PARAM(int32, openflags) ;
  devices_set_last_error(DeviceNoError) ;
  return 1 ;
}

static
int32 RIPCALL pclxl_pass_through_read_file(
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR descriptor,
  uint8*  buf,
  int32   len)
{
  PCLXL_PARSER_CONTEXT parser_context;

  UNUSED_PARAM(DEVICELIST*, dev);

  HQASSERT((len > 0),
           "Attempting to read 0 bytes for some reason.");

#ifdef DEBUG_BUILD
  {
    PCLXL_PASS_THROUGH_DEVICE *pclxl_pass_through_device;

    pclxl_pass_through_device = (PCLXL_PASS_THROUGH_DEVICE*)dev->private_data;
    VERIFY_OBJECT(pclxl_pass_through_device, PCLXL_PASS_THROUGH_DEVICE_OBJECT_NAME);
  }
#endif /* DEBUG_BUILD */

  parser_context = read_streams[descriptor].parser_context;
  HQASSERT((parser_context->doing_pass_through),
           "Reading passthrough data when not doing passthrough");

  if ( pclxl_embedded_remaining(&parser_context->pass_through_reader) == 0 ) {
    /* Look for another PassThrough command. Signal any error with EOF so
     * processing PCL5 commands stops. */
    /* We do not set_exit_parser to TRUE /FALSE before and after this scan as we
     * will scan until the next operator which is not a PassThrough. This is a
     * special case coded into pclxl_scan() */
    if ( pclxl_scan(parser_context) < EOF ) {
      return(0);
    }
  }
  len = min((uint32)len, pclxl_embedded_remaining(&parser_context->pass_through_reader));
  if ( (len > 0) &&
       !pclxl_embedded_read_bytes(&parser_context->pass_through_reader, buf, (uint32)len) ) {
    return(0);
  }
  devices_set_last_error(DeviceNoError);
  return(len);
}

static int32 RIPCALL pclxl_pass_through_write_file(DEVICELIST *dev ,
                                                   DEVICE_FILEDESCRIPTOR descriptor,
                                                   uint8 *buf,
                                                   int32 len)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor) ;
  UNUSED_PARAM(uint8 *, buf) ;
  UNUSED_PARAM(int32, len) ;
  devices_set_last_error(DeviceNoError) ;
  return len ;
}

static int32 RIPCALL pclxl_pass_through_close_file(DEVICELIST *dev,
                                                   DEVICE_FILEDESCRIPTOR descriptor)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor) ;
  devices_set_last_error(DeviceNoError) ;
  if (descriptor != 1) {
    devices_set_last_error(DeviceIOError) ;
    return -1 ;
  }
  return 0 ;
}

static int32 RIPCALL pclxl_pass_through_seek_file(DEVICELIST * dev,
                                                  DEVICE_FILEDESCRIPTOR descriptor,
                                                  Hq32x2 * destn, int32 flags)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor) ;
  UNUSED_PARAM(Hq32x2 *, destn) ;
  UNUSED_PARAM(int32, flags) ;
  devices_set_last_error(DeviceIOError) ;
  return FALSE ;
}

static int32 RIPCALL pclxl_pass_through_bytes_file(DEVICELIST * dev,
                                                   DEVICE_FILEDESCRIPTOR descriptor,
                                                   Hq32x2 * bytes, int32 reason)
{
  UNUSED_PARAM( DEVICELIST *, dev ) ;
  UNUSED_PARAM( DEVICE_FILEDESCRIPTOR, descriptor ) ;
  UNUSED_PARAM( Hq32x2 *, bytes ) ;
  UNUSED_PARAM( int32, reason ) ;

  devices_set_last_error(DeviceIOError) ;
  return FALSE ;
}

static int32 RIPCALL pclxl_pass_through_status_file(DEVICELIST * dev,
                                                    uint8 * filename,
                                                    STAT * statbuf)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  UNUSED_PARAM(uint8 *, filename) ;
  UNUSED_PARAM(STAT *, statbuf) ;
  devices_set_last_error(DeviceIOError) ;
  return -1 ;
}

static void * RIPCALL pclxl_pass_through_start_list(DEVICELIST * dev,
                                                    uint8 * pattern)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  UNUSED_PARAM(uint8 *, pattern) ;
  devices_set_last_error(DeviceNoError) ;
  return NULL ;
}

static int32 RIPCALL pclxl_pass_through_next_list(DEVICELIST * dev, void ** handle,
                                                  uint8 * pattern, FILEENTRY *entry)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  UNUSED_PARAM(void **, handle) ;
  UNUSED_PARAM(uint8 *, pattern) ;
  UNUSED_PARAM(FILEENTRY *, entry) ;
  devices_set_last_error(DeviceIOError) ;
  return -1 ;
}

static int32 RIPCALL pclxl_pass_through_end_list(DEVICELIST * dev, void * handle)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  UNUSED_PARAM(void *, handle) ;
  devices_set_last_error(DeviceIOError) ;
  return -1 ;
}

static int32 RIPCALL pclxl_pass_through_rename_file(DEVICELIST * dev, uint8 * file1,
                                                    uint8 * file2)
{
  UNUSED_PARAM(DEVICELIST *, dev );
  UNUSED_PARAM(uint8 *, file1) ;
  UNUSED_PARAM(uint8 *, file2) ;
  devices_set_last_error(DeviceIOError) ;
  return -1 ;
}

static int32 RIPCALL pclxl_pass_through_delete_file(DEVICELIST * dev, uint8 * filename)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  UNUSED_PARAM(uint8 *, filename) ;
  devices_set_last_error(DeviceIOError) ;
  return -1 ;
}

static int32 RIPCALL pclxl_pass_through_set_param(DEVICELIST * dev, DEVICEPARAM * param)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  UNUSED_PARAM(DEVICEPARAM *, param) ;
  devices_set_last_error(DeviceNoError) ;
  return ParamIgnored ;
}

static int32 RIPCALL pclxl_pass_through_start_param(DEVICELIST * dev)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  devices_set_last_error(DeviceNoError) ;
  return 0 ;
}

static int32 RIPCALL pclxl_pass_through_get_param(DEVICELIST * dev, DEVICEPARAM * param)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  UNUSED_PARAM(DEVICEPARAM *, param) ;
  devices_set_last_error(DeviceNoError) ;
  return ParamIgnored ;
}

static int32 RIPCALL pclxl_pass_through_status_device(DEVICELIST * dev, DEVSTAT * devstat)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  UNUSED_PARAM(DEVSTAT *, devstat) ;
  devices_set_last_error(DeviceIOError) ;
  return -1 ;
}

static int32 RIPCALL pclxl_pass_through_dismount(DEVICELIST * dev)
{
  PCLXL_PASS_THROUGH_DEVICE* pclxl_pass_through_device ;
  HQASSERT((dev != NULL), "dev is NULL")  ;

  pclxl_pass_through_device = (PCLXL_PASS_THROUGH_DEVICE*)(dev->private_data) ;
  VERIFY_OBJECT(pclxl_pass_through_device, PCLXL_PASS_THROUGH_DEVICE_OBJECT_NAME) ;

  if ( isDeviceUndismountable(dev) ) {
    devices_set_last_error(DeviceIOError) ;
    return -1 ;
  } else {
    devices_set_last_error(DeviceNoError) ;

    /* Remove from list of PCL XL pass through devices. */
    DLL_REMOVE(pclxl_pass_through_device, link) ;

    return 0 ;
  }
}

static int32 RIPCALL pclxl_pass_through_buffer_size(DEVICELIST * dev)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  devices_set_last_error(DeviceIOError) ;
  return -1 ;
}

static int32 RIPCALL pclxl_pass_through_spare(void)
{
  devices_set_last_error(DeviceIOError) ;
  return -1 ;
}

DEVICETYPE Pclxl_Pass_Through_Device_Type = {
  PCLXL_PASS_THROUGH_DEVICE_TYPE,      /* the device ID number */
  DEVICESMALLBUFF ,                    /* flags to indicate specifics of device */
  sizeof(PCLXL_PASS_THROUGH_DEVICE),   /* the size of the private data */
  0,                                   /* minimum ticks between tickle functions */
  NULL,                                /* procedure to service the device */
  devices_last_error,                  /* return last error for this device */
  pclxl_pass_through_init_device,      /* call to initialise device */
  pclxl_pass_through_open_file,        /* call to open file on device */
  pclxl_pass_through_read_file,        /* call to read data from file on device */
  pclxl_pass_through_write_file,       /* call to write data to file on device */
  pclxl_pass_through_close_file,       /* call to close file on device */
  pclxl_pass_through_close_file,       /* call to abort action on the device */
  pclxl_pass_through_seek_file,        /* call to seek file on device */
  pclxl_pass_through_bytes_file,       /* call to get bytes avail on an open file */
  pclxl_pass_through_status_file,      /* call to check status of file */
  pclxl_pass_through_start_list,       /* call to start listing files */
  pclxl_pass_through_next_list,        /* call to get next file in list */
  pclxl_pass_through_end_list,         /* call to end listing */
  pclxl_pass_through_rename_file,      /* rename file on the device */
  pclxl_pass_through_delete_file,      /* remove file from device */
  pclxl_pass_through_set_param,        /* call to set device parameter */
  pclxl_pass_through_start_param,      /* call to start getting device parameters */
  pclxl_pass_through_get_param,        /* call to get the next device parameter */
  pclxl_pass_through_status_device,    /* call to get the status of the device */
  pclxl_pass_through_dismount,         /* call to dismount the device */
  pclxl_pass_through_buffer_size,
  NULL,                                /* ignore ioctl calls */
  pclxl_pass_through_spare             /* spare slots */
} ;

/* ============================================================================
 * Mount a single PCL XL pass through reading device. This is done at
 * the start/end of a PCL XL job because the device needs access to
 * the PCL XL context.
 * ============================================================================
 */

Bool pclxl_mount_pass_through_device(void)
{
  PCLXL_PASS_THROUGH_DEVICE *stream_dev ;

  pclxl_pass_through_device = device_alloc(STRING_AND_LENGTH(PCLXL_PASS_THROUGH_DEVICE_NAME)) ;

  if (! device_connect(pclxl_pass_through_device, PCLXL_PASS_THROUGH_DEVICE_TYPE, PCLXL_PASS_THROUGH_DEVICE_NAME,
                       DEVICEUNDISMOUNTABLE|DEVICEENABLED, TRUE)) {
    device_free(pclxl_pass_through_device) ;
    pclxl_pass_through_device = NULL ;
    return FALSE ;
  }

  stream_dev = (PCLXL_PASS_THROUGH_DEVICE*)(pclxl_pass_through_device->private_data) ;
  VERIFY_OBJECT(stream_dev, PCLXL_PASS_THROUGH_DEVICE_OBJECT_NAME) ;

  return TRUE ;
}

/* Unmount the PCL XL pass through reading device. */
void pclxl_unmount_pass_through_device(void)
{
  if (pclxl_pass_through_device != NULL) {
    /* Do our best to unmount the PCL XL pass through device. */
    ClearUndismountableDevice(pclxl_pass_through_device) ;

    /* Call the device dismount directly as PS semantics do not
       apply. */
    if ((*theIDevDismount( pclxl_pass_through_device ))( pclxl_pass_through_device ) == -1) {
      HQFAIL("Unable to dismount PCL XL pass through device.") ;
    }

    device_free(pclxl_pass_through_device) ;
    pclxl_pass_through_device = NULL ;
  }
}

/* ============================================================================
 * Pass through open/close and read bytes.
 * ============================================================================
 */

static Bool open_pass_through_for_read(PCLXL_PARSER_CONTEXT parser_context,
                                       FILELIST** readstream)
{
  PCLXLPassThroughReadInstance *read_instance ;
  int32 i;
  int32 readstream_id ;

  HQASSERT(parser_context != NULL, "parser_context is NULL") ;
  HQASSERT(readstream != NULL, "readstream is NULL") ;
  HQASSERT(pclxl_pass_through_device != NULL, "pclxl_pass_through_device is NULL") ;

  *readstream = NULL ;

  if (num_read_streams == MAX_STREAM_NEST_LEVEL) {
    PCLXL_ERROR_HANDLER(parser_context->pclxl_context, PCLXL_SS_KERNEL, PCLXL_STREAM_NESTING_FULL,
                        ("Stream nesting level greater than %d", MAX_STREAM_NEST_LEVEL));
    return FALSE ;
  }

  /* Find an empty slot. */
  for (i=0; i < MAX_STREAM_NEST_LEVEL; i++) {
    if (read_streams[i].read_id == STREAM_CLOSED)
      break ;
  }
  readstream_id = i ;

  HQASSERT(readstream_id < MAX_STREAM_NEST_LEVEL,
           "Something odd with finding an empty slot.") ;

  read_instance = &read_streams[readstream_id] ;

  HQASSERT(read_instance->read_stream.typetag == tag_LIMIT,
           "FILELIST has become corrupt.") ;

  read_instance->read_id = readstream_id ;
  read_instance->parser_context = parser_context ;

  init_filelist_struct(&(read_instance->read_stream),
                       NAME_AND_LENGTH("PCLXLStreamFileList"),
                       REALFILE_FLAG | READ_FLAG | OPEN_FLAG,
                       readstream_id /* descriptor */,
                       &(read_instance->read_buf)[0] /* buf */,
                       PASS_THROUGH_DATA_BLOCK_SIZE /* buf size */,
                       FileFillBuff, /* fillbuff */
                       FileFlushBufError, /* flushbuff */
                       FileInit, /* initfile */
                       FileClose, /* closefile */
                       FileDispose, /* disposefile */
                       FileBytes, /* bytesavail */
                       FileReset, /* resetfile */
                       FilePos, /* filepos */
                       FileSetPos, /* setfilepos */
                       FileFlushFile, /* flushfile */
                       FileEncodeError, /* filterencode */
                       FileDecodeError, /* filterdecode */
                       FileLastError, /* lasterror */
                       -1 /* filterstate */,
                       pclxl_pass_through_device /* device */,
                       NULL /* underfile */,
                       NULL /* next */) ;

  num_read_streams++ ;
  *readstream = &(read_instance->read_stream) ;

  return TRUE ;
}

void close_pass_through(FILELIST **readstream)
{
  PCLXLPassThroughReadInstance *read_instance ;
  DEVICE_FILEDESCRIPTOR readstream_id ;

  HQASSERT(readstream != NULL, "readstream is NULL") ;
  HQASSERT(*readstream != NULL, "*readstream is NULL") ;

  if (num_read_streams > 0) {
    readstream_id = theIDescriptor((*readstream)) ;
    HQASSERT((readstream_id >= 0 && readstream_id < MAX_STREAM_NEST_LEVEL),
             "Read stream file descriptor is corrupt.") ;

    read_instance = &read_streams[readstream_id] ;

    read_instance->read_id = STREAM_CLOSED ;
    read_instance->read_stream.typetag = tag_LIMIT ;
    num_read_streams-- ;
    *readstream = NULL ;
  }
}


/* ============================================================================
 * PCL XL pass through device init/finish. Executed at RIP
 * startup/shutdown.
 * ============================================================================
 */
void init_C_globals_pclxlpassthrough(void)
{
  int32 i;

  num_read_streams = 0 ;

  for (i=0; i < MAX_STREAM_NEST_LEVEL; i++) {
    read_streams[i].read_id = STREAM_CLOSED ;
    read_streams[i].read_stream.typetag = tag_LIMIT ;
  }

  pclxl_pass_through_device = NULL ;
  DLL_RESET_LIST(&dls_pclxl_pass_through_devs);
}

Bool pclxl_pass_through_init(void)
{
  return device_type_add(&Pclxl_Pass_Through_Device_Type) ;
}

void pclxl_pass_through_finish(void)
{
}

/* ============================================================================
 * PCL XL operators below here.
 * ============================================================================
 */

/*
 * pclxl_new_passthrough_state_info() allocates (and zero-fills)
 * a PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO structure
 */

static PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO*
pclxl_new_passthrough_state_info(PCLXL_CONTEXT pclxl_context)
{
  PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO* new_state_info;

  if ( ((new_state_info = mm_alloc(pclxl_context->memory_pool,
                                   sizeof(PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO),
                                   MM_ALLOC_CLASS_PCLXL_PASSTHROUGH_STATE)) == NULL) )
  {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INSUFFICIENT_MEMORY,
                               ("Failed to allocate new PCLXL_OT_PCL5C_STATE_INFO for PCL Passthrough"));

    return NULL;
  }
  else
  {
    (void) memset((void*) new_state_info, 0, sizeof(PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO));

    new_state_info->pclxl_context = pclxl_context;

    new_state_info->font_sel_criteria_changed = pclxl_context->graphics_state->char_details.current_font.pcl5_font_selection_criteria.initialized;

    return new_state_info;
  }

  /*NOTREACHED*/
}

void
pclxl_free_passthrough_state_info(PCLXL_CONTEXT pclxl_context,
                                  PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO* state_info)
{
  mm_free(pclxl_context->memory_pool,
          state_info,
          sizeof(PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO));
}

static void
pclxl_init_passthrough_state_info(PCLXL_CONTEXT             pclxl_context,
                                  int32                     pass_through_type,
                                  PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO *state_info)
{
  SYSTEMVALUE x, y ;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_GRAPHICS_STATE ps_save_graphics_state;
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;
  PCLXL_MEDIA_DETAILS current_media_details = &non_gs_state->current_media_details;
  PCLXL_MEDIA_DETAILS requested_media_details = &non_gs_state->requested_media_details;
  PCLXL_CHAR_DETAILS char_details = &graphics_state->char_details;
  PCLXL_FONT_DETAILS font_details = &char_details->current_font;
  PCL5_FONT_SELECTION_CRITERIA* pcl5_font_selection_criteria = &font_details->pcl5_font_selection_criteria;
  int i;

  UNUSED_PARAM(int32, pass_through_type) ;

  HQASSERT(state_info->pclxl_context == pclxl_context,
           "This PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO does not match the PCLXL_CONTEXT that it was allocated for");

  /*
   * Note that we explicitly do *not* (re-)initialize
   * the pcl5_ctxt structure field
   * because it is entirely the responsibility of
   * the PCL[5] interpreter to maintain this field.
   */

  /*
   * We provide a copy of our physical_page_ctm
   * to use as a possible starting point
   * for PCL5 to establish its own CTM
   *
   * PCL5 will possibly update this location
   * with its own revised CTM if/when it performs a media/page size change
   * or a page orientation change or handles a PCL5 "reset" command
   *
   * We could then choose to use this as the basis of re-establishing
   * our own CTM upon return.
   *
   * However PCLXL and PCL5 currently communicate as much information as possible
   * via the current page device configuration. So this CTM may not actually get used.
   */

  state_info->physical_page_ctm = graphics_state->physical_page_ctm ;

  /*
   * Despite what the PCLXL protocol class specification version 3.0 says
   * about the PassThrough command handling, there are more cases where
   * changes made as part of the passed-through PCL5 commands
   * that have an effect upon the subsequent PCLXL page(s)
   *
   * Including the PCLXL clip path and/or media/page size
   *
   * Therefore we have a number of flags that are initialized to False
   * by PCLXL just before the start of a passthrough.
   * They are then set by PCL5 as "hints" about what PCLXL might like to do
   * immediately upon return from the passthrough
   */

  state_info->use_pcl5_page_clip = FALSE;
  state_info->use_pcl5_page_setup = FALSE;

  for ( i = 0 ; i < NUM_ARRAY_ITEMS(state_info->pcl5_page_clip_offsets) ; i++ )
    state_info->pcl5_page_clip_offsets[i] = 0;

  state_info->units_per_measure_x = non_gs_state->units_per_measure.res_x * 72 /
    pclxl_ps_units_per_pclxl_uom(non_gs_state->measurement_unit) ;

  state_info->units_per_measure_y = non_gs_state->units_per_measure.res_y * 72 /
    pclxl_ps_units_per_pclxl_uom(non_gs_state->measurement_unit) ;

  state_info->current_point = graphics_state->current_point ;

  MATRIX_TRANSFORM_XY(graphics_state->current_point_xy.x,
                      graphics_state->current_point_xy.y, x, y,
                      &graphics_state->current_ctm) ;

  state_info->current_point_x = x ;
  state_info->current_point_y = y ;

  /*
   * We now need to walk back down the PCLXL graphics state stack
   * to find the graphics_state that contains the most recent
   * Postscript save object, because we need to pass the addres of this location
   * to PCL5 for passthroughs that perform a page throw
   */

  for ( ps_save_graphics_state = pclxl_context->graphics_state ;
        ((ps_save_graphics_state != NULL) && (ps_save_graphics_state->postscript_op != PCLXL_PS_SAVE_RESTORE)) ;
        ps_save_graphics_state = ps_save_graphics_state->parent_graphics_state )
    ;

  if ( ps_save_graphics_state != NULL )
  {
    state_info->pclxl_save_object          = &ps_save_graphics_state->ps_save_object;
  }
  else
  {
    state_info->pclxl_save_object          = NULL;
  }

  /*
   * Note that we must reset font_sel_criteria_changed (to True)
   * if PCLXL's notion of PCL5 font selection criteria has:
   * a) been initialized and b) PCL5 has not yet been informed of
   * these initial criteria *OR* when the criteria have been changed
   * since the last time that PCL5 was informed
   */

  state_info->font_sel_criteria_changed = (pcl5_font_selection_criteria->initialized && !pcl5_font_selection_criteria->pcl5_informed);
  state_info->font_sel_initialized =   pcl5_font_selection_criteria->initialized;
  state_info->font_sel_symbol_set =    pcl5_font_selection_criteria->symbol_set;
  state_info->font_sel_spacing =       pcl5_font_selection_criteria->spacing;
  state_info->font_sel_pitch =         pcl5_font_selection_criteria->pitch;
  state_info->font_sel_height =        pcl5_font_selection_criteria->height;
  state_info->font_sel_style =         pcl5_font_selection_criteria->style;
  state_info->font_sel_weight =        pcl5_font_selection_criteria->weight;
  state_info->font_sel_typeface =      pcl5_font_selection_criteria->typeface;

  state_info->pclxl_page_number            = non_gs_state->page_number;
  state_info->pclxl_duplex_page_side_count = non_gs_state->duplex_page_side_count;
  state_info->pclxl_duplex                 = current_media_details->duplex;

  state_info->pclxl_requested_orientation           = requested_media_details->orientation;
  state_info->pclxl_requested_media_size_value_type = requested_media_details->media_size_value_type;
  state_info->pclxl_requested_media_size_enum       = requested_media_details->media_size;
  state_info->pclxl_requested_media_size_name       = requested_media_details->media_size_name;
  state_info->pclxl_requested_media_size_xy.x       = requested_media_details->media_size_xy.x;
  state_info->pclxl_requested_media_size_xy.y       = requested_media_details->media_size_xy.y;
  state_info->pclxl_requested_media_type            = requested_media_details->media_type;
  state_info->pclxl_requested_media_source          = requested_media_details->media_source;
  state_info->pclxl_requested_media_destination     = requested_media_details->media_destination;
  state_info->pclxl_requested_duplex                = requested_media_details->duplex;
  state_info->pclxl_requested_simplex_page_side     = requested_media_details->simplex_page_side;
  state_info->pclxl_requested_duplex_page_side      = requested_media_details->duplex_page_side;
  state_info->pclxl_requested_duplex_binding        = requested_media_details->duplex_binding;
}

static void
pclxl_update_passed_back_state(PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO* state_info,
                               int32 pass_through_type,
                               PCLXL_CONTEXT pclxl_context)
{
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;
  PCLXL_MEDIA_DETAILS current_media_details = &non_gs_state->current_media_details;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_CHAR_DETAILS char_details = &graphics_state->char_details;
  PCLXL_FONT_DETAILS font_details = &char_details->current_font;
  PCL5_FONT_SELECTION_CRITERIA* pcl5_font_selection_criteria = &font_details->pcl5_font_selection_criteria;

  UNUSED_PARAM(int32, pass_through_type) ;

  non_gs_state->page_number = state_info->pclxl_page_number;

  non_gs_state->duplex_page_side_count = state_info->pclxl_duplex_page_side_count;

  current_media_details->duplex = state_info->pclxl_duplex;

  /*
   * Note that we do *not* update our own PCLXL physical_page_ctm
   * from the updated value in the passthrough state info
   * because we do not necessarily want use this value
   * So IFF we need to use it, then we use it directly
   * from the state_info structure field
   */

  pcl5_font_selection_criteria->pcl5_informed = TRUE;

  if ( state_info->font_sel_initialized &&
       state_info->font_sel_criteria_changed )
  {
    pcl5_font_selection_criteria->initialized = state_info->font_sel_initialized;
    pcl5_font_selection_criteria->symbol_set = state_info->font_sel_symbol_set;
    pcl5_font_selection_criteria->spacing = state_info->font_sel_spacing;
    pcl5_font_selection_criteria->pitch = state_info->font_sel_pitch;
    pcl5_font_selection_criteria->height = state_info->font_sel_height;
    pcl5_font_selection_criteria->style = state_info->font_sel_style;
    pcl5_font_selection_criteria->weight = state_info->font_sel_weight;
    pcl5_font_selection_criteria->typeface = state_info->font_sel_typeface;

    /*
     * Ok, that has tracked any changes to the PCL5 font selection criteria
     * Note that we only *use* these changed criteria next time that
     * we encounter an explicit PCLSelectFont operation in PCLXL,
     * or in a subsequent PCL5 Passthrough
     */

    state_info->font_sel_criteria_changed = FALSE;
  }
}

/*
 * Tag 0xbf PassThrough
 */
Bool
pclxl_op_pass_through(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context ;
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;
  PCLXL_MEDIA_DETAILS requested_media_details = &non_gs_state->requested_media_details;
  PCLXL_MEDIA_DETAILS current_media_details = &non_gs_state->current_media_details;
  PCLXL_CONFIG_PARAMS config_params = &pclxl_context->config_params;
  DL_STATE *page = pclxl_context->corecontext->page;
  PCLXLSTREAM* p_stream ;
  FILELIST *readstream ;

  /* Flush any PCL6 characters to the DL */
  if (!finishaddchardisplay(pclxl_context->corecontext->page, 1)) {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to flush chars to DL"));
    return (FALSE);
  }

  p_stream = pclxl_parser_current_stream(parser_context);

  if ( !pclxl_attr_set_match_empty(parser_context->attr_set, pclxl_context,
                                   PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  if ( (!pclxl_context->passthrough_state_info) &&
       ((pclxl_context->passthrough_state_info =
           pclxl_new_passthrough_state_info(pclxl_context)) == NULL) )
  {
      /*
       * Oops, we have failed to allocate a PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO structure
       * A suitable error message has already been logged
       * but we obviously cannot continue with this PCLPassThrough command
       * because we have nowhere to place the state info to be passed to
       * PCL5
       */

      return FALSE;
  }

  /*
   * Setup a new FILELIST which is used for PCL5c interpretation where
   * the reading of data recursively invokes the PCLXL interpreter to
   * obtain more data for consecutive PCLXL PassThrough commands. When
   * a non-PassThrough command is found, signal EOF to the PCL5c
   * interpreter.
   */
  if ( !pclxl_stream_embedded_init(parser_context->pclxl_context, p_stream,
                                   parser_context->data_source_big_endian,
                                   &parser_context->pass_through_reader) ) {
    return FALSE;
  }

  /*
   * We must now determine whether this is the first Passthrough
   * out of a possible contiguous sequence of them)
   * or whether it is just yet another subsequent follow-on Passthrough
   * (typically but not exclusively because the original PCL5 was longer than
   * 65535 bytes and so had to be split into 65535-byte chunks)
   *
   * Note that this concept of being a "first"
   * versus "subsequent"/"continuation" Passthrough
   * should not be confused with the concept of the first Passthrough on the page.
   * Nor with the concept of whether the page already has some "marks"
   * on it and thus this being a "snippet" Passthrough
   */

  if ( !parser_context->doing_pass_through )
  {
    /*
     * This the first PCL[5]PasstThrough,
     * possibly in a contiguous sequence of PCL[5]PassThrough commands.
     *
     * We must therefore initialize the passthrough state info structure
     * with the current PCLXL state items that the PCL5 interpreter needs
     * to commence handling the following to-be-passed-through PCL5 operations
     *
     * This includes our current physical page CTM, page number,
     * duplex settings, logical page orientation,
     * current cursor position and (requested) media type
     * and any PCL5 font selection criteria
     */

    PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO* state_info = pclxl_context->passthrough_state_info;

    int32 pass_through_type = (displaylistisempty(page) ?
                               PCLXL_WHOLE_JOB_PASS_THROUGH :
                               PCLXL_SNIPPET_JOB_PASS_THROUGH);

    Bool  is_whole_job_pass_through = (pass_through_type == PCLXL_WHOLE_JOB_PASS_THROUGH);

    Bool  gsave_grestore_needed_around_passthrough = !is_whole_job_pass_through;

#ifdef DEBUG_BUILD
    Bool  reevaluate_page_setup = (is_whole_job_pass_through ||
                                   (pclxl_context->config_params.debug_pclxl & PCLXL_DEBUG_PAGE_CTM));
#else
    Bool  reevaluate_page_setup = is_whole_job_pass_through;
#endif

    Bool status = TRUE ;

    PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
                ("PassThrough(%s(%d byte%s), existing_marks_on_page = %s)",
                 (is_whole_job_pass_through ? "whole-job" : "snippet-mode"),
                 pclxl_embedded_remaining(&parser_context->pass_through_reader),
                 (pclxl_embedded_remaining(&parser_context->pass_through_reader) == 1 ? "" : "s"),
                 (displaylistisempty(page) ? "FALSE" : "TRUE")));

    /*
     * If we are doing "whole-job pass-through"
     * then, whilst we still need to "push" and "pop" our own PCLXL graphics state
     * there is a possibility that we should avoid doing the Postscript "gsave"/"grestore"
     * around the PCL5 commands
     *
     * Therefore pclxl_push_gs() accepts an enumeration that specifies
     * what Postscript operation, if any, must be performed
     * to go along with our PCLXL graphics state "push"
     *
     * And pclxl_pop_gs() accepts a boolean that says whether or not
     * the "pop" is being performed as a direct result of a PopGS operator
     * which is *not* the case here because the "pop" will be the indirect result
     * of this Passthrough operation.
     */

    pclxl_save_clip_record(pclxl_context);

    if ( !pclxl_push_gs(pclxl_context, (gsave_grestore_needed_around_passthrough ? PCLXL_PS_GSAVE_GRESTORE : PCLXL_PS_NO_OP_SET_CTM)) )
      return FALSE ;

    /*
     * Note that PCL5 needs access to a CTM that is based around centi-points.
     * And because it does its own (quirky) margin setup and this is dependent upon
     * page orientation and duplex settings we need to pass these settings
     * (in PCLXL_TO_PCL5_PASSTHROUGH_STATE and/or the current page device config)
     * We need to supply a centi-point based CTM *with no page orientation applied*
     * Fortunately we just happen to have this in our "physical page CTM", so that's handy ;-)
     */

    gs_setctm(&pclxl_context->graphics_state->physical_page_ctm, FALSE) ;

    pclxl_init_passthrough_state_info(pclxl_context, pass_through_type, state_info) ;

    /*
     * We must now remove any PCLXL clippath that is in effect
     * to allow the PCL5 pass through to access the entire page.
     *
     * Actually PCL5 will probably set up a clip rectangle
     * just inside the edge of the page as the printable area
     */

    if ( !pclxl_set_clip_to_page(pclxl_context->graphics_state) )
      return FALSE ;

    if ( !open_pass_through_for_read(parser_context, &readstream) )
      return FALSE ;

    parser_context->doing_pass_through = TRUE ;

    /*
     * Execute bytes as PCL5c commands.
     * And *immediately* after pcl5_execute_stream() returns (successfully)
     * We must call(back) the PCL5 interpreter to allow it
     * an opportunity to update the page count, duplex settings
     * and requested media type that may/will have changed during
     * the handling of the PCL5 data stream.
     */

    PROBE(SW_TRACE_INTERPRET_PCL5, 1,
          status = (pcl5_execute_stream(pclxl_context->corecontext,
                                        readstream,
                                        config_params->job_config_dict,
                                        pass_through_type,
                                        state_info) &&
                    pcl5_update_passthrough_state_info(state_info)));

    parser_context->doing_pass_through = FALSE ;

    close_pass_through(&readstream) ;

    PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
                ("Returned-from PassThrough(), status = %s, marks_on_page = %s, use_pcl5_clip_path = %s, use_pcl5_page_setup = %s, [PCL5] font_sel_criteria_changed =  %s, pcl5_clip_path_offsets = (%d, %d, %d, %d)",
                (status ? "TRUE" : "FALSE"),
                 (displaylistisempty(page) ? "FALSE" : "TRUE"),
                 (state_info->use_pcl5_page_clip ? "TRUE" : "FALSE"),
                 (state_info->use_pcl5_page_setup ? "TRUE" : "FALSE"),
                 ((state_info->font_sel_initialized && state_info->font_sel_criteria_changed) ? "TRUE" : "FALSE"),
                 state_info->pcl5_page_clip_offsets[0],
                 state_info->pcl5_page_clip_offsets[1],
                 state_info->pcl5_page_clip_offsets[2],
                 state_info->pcl5_page_clip_offsets[3]
                 ));

    /*
     * Ok, we have completed the PCL5 pass through command sequence.
     * Flush any pending PCL5 chars to the DL (before adding anything else
     * to the DL in PCLXL, or doing the displaylistisempty test below).
     *
     * Also re-instate the PCLXL graphics state
     * We must always pop the current (PCLXL) graphics state
     * And re-instate the previous clip path
     */

    /** \todo Can there be any pending PCL5 chars, i.e. can we exit PCL5
     *  without going through pcl5_end_text_run?
     */
    if (status)
      status = finishaddchardisplay(pclxl_context->corecontext->page, 1);

    /* Assume that this may have changed the /V or /W mode
     * todo Review this for trunk
     */
    non_gs_state->text_mode_changed = TRUE;

    status = (pclxl_pop_gs(pclxl_context, FALSE) &&
              status);

    /*
     * But it also appears that any changes to the PCL5 font selection criteria
     * actually affect the PCLXL (future) font selection criteria
     * *outside* the scope of the PushGS/PopGS around the Passthrough
     */

    if ( status )
      pclxl_update_passed_back_state(state_info,
                                     pass_through_type,
                                     pclxl_context);

    /*
     * However what else we re-instate depends upon what sort of pass-through
     * we performed and what state the passed-through PCL5 commands
     * left the current page in.
     */

    if ( pass_through_type == PCLXL_SNIPPET_JOB_PASS_THROUGH )
    {
      /*
       * This was a snippet passthrough,
       * (i.e. there were already some marks on the current page *before* the passthrough)
       * then the PCL5 commands were not allowed to throw pages, perform a PCL5 "reset"
       * or make any media size or orientation changes.
       *
       * In which case we all we have to do is
       * pop the graphics state (as done above)
       * and re-instate the PCLXL CTM and PCLXL clip path.
       */

      PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
                  ("Resetting PCLXL CTM after snippet-mode passthrough, existing_marks_on_page = %s",
                  (displaylistisempty(page) ? "FALSE" : "TRUE")));

      return (status &&
              pclxl_ps_set_current_ctm(&pclxl_context->graphics_state->current_ctm) &&
              pclxl_restore_clip_record(pclxl_context)
             );
    }
    else if ( (reevaluate_page_setup) && (!displaylistisempty(page)) )
    {
      /*
       * This was a "whole-job" passthrough AND
       * there are (now) some marks on the current page.
       *
       * So we must attempt to set-up the corresponding PCLXL
       * page orientation/CTM etc *without erasing the existing marks*
       *
       * The question is: Has there been any PCL5 "reset"
       * or a PCL5-requested media size or orientation change?
       *
       * We could "make a guess" by re-obtaining the current media details
       * and comparing them against our previously requested media details
       * and then attempting to guess what re-setup we need to perform
       *
       * But in doing this we would have to replicate some
       * PCL5-specific implementation detail because it appears
       * that we must sometimes adopt the PCL5 clip path
       * rather than reinstating our own saved PCLXL clip path
       *
       * However PCL5 has conveniently provided us with some "hints"
       * including whether or not we should use our own saved PCLXL clip path
       * or whether to set up a PCL5 printable page clip rectangle.
       *
       * And whether PCL5 thinks we should accept any media size change or orientation change
       *
       * It is of course upto us to decide whether to act upon these "hints"
       * But just how convenient it is to get these "hints" :-)
       */

      if ( !status ||
           !pclxl_ps_set_current_ctm(&pclxl_context->graphics_state->physical_page_ctm) ||
           !pclxl_get_current_media_details(pclxl_context,
                                            requested_media_details,
                                            current_media_details) ||
           /* Note that we will pick up any PCL5 media size change and/or orientation change here */
           !pclxl_set_default_ctm(pclxl_context,
                                  pclxl_context->graphics_state,
                                  non_gs_state) )
      {
        return FALSE;
      }

      if ( state_info->use_pcl5_page_clip )
      {
        /*
         * We have been told by PCL5 that we should use the clip path that *it* has set up,
         * presumably because it has put some marks on the page.
         *
         * We must remember to discard our saved PCLXL clip path
         */

        PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
                    ("Re-evaluated PCL5-marked page set-up and adopting PCL5 clip path"));

        pclxl_free_clip_record(pclxl_context->graphics_state);

        return pclxl_ps_set_page_clip(pclxl_context,
                                      &state_info->physical_page_ctm,
                                      (uint32*) state_info->pcl5_page_clip_offsets);
      }
      else if ( requested_media_details->orientation != current_media_details->orientation )
      {
        /*
         * This branch is really just a bit of belt'n'braces coding.
         * If PCL5 did not recommend re-establishing its clip path
         * *BUT* there has none-the-less been an orientation change
         * then it appears that we should not re-instate the saved PCLXL clip path
         */

        PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
                    ("Re-evaluated PCL5-marked page set-up but discarded *both* clip paths, setting clip path to PCLXL page clip"));

        pclxl_free_clip_record(pclxl_context->graphics_state);

        return pclxl_set_page_clip(pclxl_context);
      }
      else
      {
        /*
         * Ok, no "hint" from PCL5 that we should use its printable page [area] clip path
         * and no orientation change.
         * So it looks like it is safe to re-instate our saved PCLXL clip path
         */

        PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
                    ("Re-evaluated PCL5-marked page set-up and restoring saved PCLXL clip path"));

        return pclxl_restore_clip_record(pclxl_context);
      }
    }
    else
    {
      /*
       * This was a "whole-job" passthrough BUT
       * the current page is *unmarked*
       *
       * According to the PCLXL Passthrough spec this would seem to mean
       * that we simply reinstate the PCLXL page set up and clip path
       *
       * But it seems that whilst the HP4700 does re-establish the page size/orientation
       * to be the PCLXL page set-up, it does not necessarily restore the saved PCLXL clip path
       *
       * We allow for this by allowing PCL5 to "hint"
       * whether or not PCLXL should re-instate its own saved clip path
       * or whether PCLXL should adopt the PCL5 clip path
       *
       * It is of course upto us, here, whether or not to accept this hint
       *
       * Note that we have a choice here:
       * We can either just use the previous "physical_page_ctm"
       * as the starting point (and thus avoid the setpagedevice call)
       * Or we can perform a full-blown pclxl_setup_page_device()
       */

      PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
                  ("Re-evaluating page set-up reverting to previous PCLXL page set-up because the current page is blank"));

      if ( !status ||
#define PCLXL_PASSTHROUGH_USES_SETPAGEDEVICE_TO_RESET_BLANK_PAGE 1
#if PCLXL_PASSTHROUGH_USES_SETPAGEDEVICE_TO_RESET_BLANK_PAGE
           !pclxl_setup_page_device(pclxl_context,
                                    current_media_details,
                                    requested_media_details,
                                    current_media_details) ||
#else
           /*
            * If we want to avoid calling pclxl_setup_page_device
            * we must still perform *some* of its functionality
            */
           !pclxl_ps_set_current_ctm(&pclxl_context->graphics_state->physical_page_ctm) ||
           !pclxl_get_current_media_details(pclxl_context,
                                            requested_media_details,
                                            current_media_details) ||
#endif
           !pclxl_set_default_ctm(pclxl_context,
                                  pclxl_context->graphics_state,
                                  non_gs_state) )
      {
        return FALSE;
      }

      if ( state_info->use_pcl5_page_clip )
      {
        PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
                    ("Re-evaluated PCLXL page set-up but adopting PCL5 clip path"));

        pclxl_free_clip_record(pclxl_context->graphics_state);

        return pclxl_ps_set_page_clip(pclxl_context,
                                      &state_info->physical_page_ctm,
                                      (uint32*) state_info->pcl5_page_clip_offsets);
      }
      else
      {
        PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
                    ("Re-evaluated PCLXL page set-up and restoring saved PCLXL clip path"));

        return pclxl_restore_clip_record(pclxl_context);
      }
    }
  }
  else
  {
    /*
     * This is a continuation PCLPassThrough command
     * in a contiguous sequence of PCL[5]PassThrough commands
     *
     * The PCL5 state is already correctly set up
     * (and we have no need to re-synchronize the
     * PCLXL context/graphics state yet)
     *
     * So I am not sure that we have to do anything
     * (because this branch is only entered when "doing_pass_through"
     * is already true)
     */

    PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
                ("Passthrough(continuation-passthrough(%d byte%s), existing_marks_on_page = %s)",
                 pclxl_embedded_remaining(&parser_context->pass_through_reader),
                 (pclxl_embedded_remaining(&parser_context->pass_through_reader) == 1 ? "" : "s"),
                 (displaylistisempty(page) ? "FALSE" : "TRUE")));

    return TRUE;
  }
}

/******************************************************************************
* Log stripped */
