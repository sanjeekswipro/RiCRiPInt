/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxluserstream.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Wrappers around CoreRIP FILELIST*, Getc() and Ungetc() that at
 * least trace all the bytes read (and unread) from a PCLXL data
 * stream. Also implements the recording and playback of user defined
 * streams.
 *
 * Also provides a sneaky peek at the next byte (or EOF) that
 * would/will be read from PCLXL data stream which is typically used
 * within an operator handler to see if there is some embedded data
 * following the operator byte code.
 */

#include "core.h"
#include "swctype.h"
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
#include "hqmemset.h"

#include "pclxlattributes.h"
#include "pclxlparsercontext.h"
#include "pclxldebug.h"
#include "pclxlstream.h"
#include "pclxluserstream.h"
#include "pclxlerrors.h"


/* ============================================================================
 * PCL XL user defined stream storage.
 * ============================================================================
 */

/* User defined streams are kept in a linked list of data blocks. */

#define USER_STREAM_DATA_BLOCK_SIZE 1024

typedef struct USER_STREAM_DATA_BLOCK {
  uint8 data[USER_STREAM_DATA_BLOCK_SIZE] ;
  struct USER_STREAM_DATA_BLOCK *next ;
  struct USER_STREAM_DATA_BLOCK *prev ;
} USER_STREAM_DATA_BLOCK ;

typedef struct PCLXLUserStreamStorage {
  uint8 *name ;
  uint32 name_len ;
  /* Total bytes in the stream. */
  uint32 total_bytes ;
  /* List of data blocks for this stream. */
  USER_STREAM_DATA_BLOCK *first_data_block ;
  /* Pointer to the last data block. */
  USER_STREAM_DATA_BLOCK *last_data_block ;
  /* Where to put the next byte when creating writing to this
     stream. */
  uint8 *write_pos ;
} PCLXLUserStreamStorage ;

static Bool create_user_stream_storage(PCLXLUserStreamStorage **storage,
                                       uint8 *name, uint32 name_len,
                                       mm_pool_t memory_pool)
{
  PCLXLUserStreamStorage *new_storage ;
  HQASSERT(storage != NULL, "storage is NULL") ;
  *storage = NULL ;

  if ((new_storage = mm_alloc(memory_pool,
                              sizeof(PCLXLUserStreamStorage) + name_len,
                              MM_ALLOC_CLASS_PCLXL_STREAM)) == NULL)
    return FALSE ;

  /* We lazily create the data blocks. */
  new_storage->first_data_block = NULL ;
  new_storage->last_data_block = NULL ;
  new_storage->total_bytes = 0 ;
  new_storage->write_pos = NULL ;
  new_storage->name = (uint8*)new_storage + sizeof(PCLXLUserStreamStorage) ;
  new_storage->name_len = name_len ;
  HqMemCpy(new_storage->name, name, name_len) ;

  *storage = new_storage ;

  return TRUE ;
}

static void destroy_user_stream_storage(PCLXLUserStreamStorage **storage,
                                        mm_pool_t memory_pool)
{
  USER_STREAM_DATA_BLOCK *data_blocks ;

  HQASSERT(storage != NULL, "storage is NULL") ;
  HQASSERT(*storage != NULL, "*storage is NULL") ;

  /* Free data blocks. */
  data_blocks = (*storage)->first_data_block ;
  while (data_blocks != NULL) {
    USER_STREAM_DATA_BLOCK *data = data_blocks ;
    data_blocks = data_blocks->next ;
    mm_free(memory_pool, data, sizeof(USER_STREAM_DATA_BLOCK)) ;
  }

  /* Free structure. */
  mm_free(memory_pool, *storage,
          sizeof(PCLXLUserStreamStorage) + (*storage)->name_len) ;
  *storage = NULL ;
}

/* ============================================================================
 * Cache for PCL XL user defined streams. Maps stream names to their
 * storage.
 * ============================================================================
 */

#define STREAM_CACHE_TABLE_SIZE 37

typedef struct PCLXLStreamCacheEntry {
  PCLXLUserStreamStorage *storage ;
  Bool is_running ;
  struct PCLXLStreamCacheEntry *next ;
} PCLXLStreamCacheEntry ;

struct PCLXLStreamCache {
  PCLXLStreamCacheEntry* table[STREAM_CACHE_TABLE_SIZE] ;
  mm_pool_t memory_pool ;
  uint32 table_size ;
} ;

static PCLXLStreamCacheEntry *user_stream_being_defined ;

/* If we are executing even one user defined stream, we can NOT define
   a stream. */
static int32 user_stream_being_executed ;

static
Bool pclxl_stream_cache_find(PCLXLStreamCache *stream_cache, uint8 *string, uint32 length,
                             PCLXLStreamCacheEntry **entry, PCLXLStreamCacheEntry **prev,
                             uint32 *hash)
{
  PCLXLStreamCacheEntry *curr, *temp_prev = NULL ;

  HQASSERT(stream_cache != NULL, "stream_cache is NULL") ;
  HQASSERT(entry != NULL, "entry is NULL") ;
  HQASSERT(hash != NULL, "hash is NULL") ;
  HQASSERT(string != NULL, "string is NULL") ;
  HQASSERT(length > 0, "length is not greater than zero") ;

  /* Compute a hash on a string. This an implementation of hashpjw
     without any branches in the loop. */
  {
/* Constants for PJW hash function. */
#define PJW_SHIFT        (4)            /* Per hashed char hash shift */
#define PJW_MASK         (0xf0000000u)  /* Mask for hash top bits */
#define PJW_RIGHT_SHIFT  (24)           /* Right shift distance for hash top bits */

    uint32 bits = 0 ;
    uint32 i = length ;
    uint8 *p = string ;

    *hash = 0;
    while ( i-- > 0 ) {
      *hash = (*hash << PJW_SHIFT) + *p++ ;
      bits = *hash & PJW_MASK ;
      *hash ^= bits | (bits >> PJW_RIGHT_SHIFT) ;
    }

    *hash = *hash % STREAM_CACHE_TABLE_SIZE ;
  }

  *prev = NULL ;
  *entry = NULL ;

  for (curr = stream_cache->table[*hash]; curr != NULL; curr = curr->next) {
    HQASSERT(curr->storage != NULL, "storage is NULL") ;
    if (curr->storage->name_len == length &&
        HqMemCmp(curr->storage->name, curr->storage->name_len,
                 string, length) == 0) {
      *entry = curr ;
      *prev = temp_prev ;
      return TRUE ;
    }
    temp_prev = curr ;
  }

  return FALSE ;
}

static
void stream_free_entry(PCLXLStreamCache *stream_cache,
                       PCLXLStreamCacheEntry *entry)
{
  HQASSERT(entry->storage != NULL, "storage is NULL") ;
  destroy_user_stream_storage(&(entry->storage), stream_cache->memory_pool) ;
  mm_free(stream_cache->memory_pool, entry,
          sizeof(PCLXLStreamCacheEntry)) ;
}

/* A FALSE return simply means that nothing was deleted as it did not
   exist within the cache. */
static
void pclxl_stream_cache_remove(PCLXLStreamCache *stream_cache,
                               uint8 *string, uint32 length)
{
  PCLXLStreamCacheEntry *entry, *prev ;
  uint32 hash ;

  HQASSERT(stream_cache != NULL, "stream_cache is NULL") ;
  HQASSERT(string != NULL, "string is NULL") ;
  HQASSERT(length > 0, "length is not greater than zero") ;

  if (pclxl_stream_cache_find(stream_cache, string, length, &entry, &prev, &hash)) {
    /* Unlink. */
    if (prev == NULL) {
      stream_cache->table[hash] = entry->next ;
    } else {
      prev->next = entry->next ;
    }
    stream_free_entry(stream_cache, entry) ;
  }
}

static
PCLXLStreamCacheEntry* pclxl_stream_cache_get(PCLXLStreamCache *stream_cache, uint8 *string, uint32 length)
{
  PCLXLStreamCacheEntry *entry, *prev ;
  uint32 hash ;

  HQASSERT(stream_cache != NULL, "stream_cache is NULL") ;
  HQASSERT(string != NULL, "string is NULL") ;
  HQASSERT(length > 0, "length is not greater than zero") ;

  if (pclxl_stream_cache_find(stream_cache, string, length, &entry, &prev, &hash))
    return entry ;

  return NULL ;
}

Bool pclxl_stream_cache_insert(PCLXLStreamCache *stream_cache,
                               uint8 *string, int32 length,
                               PCLXLStreamCacheEntry **new_stream_entry)
{
  PCLXLStreamCacheEntry *entry, *prev ;
  uint32 hash ;

  HQASSERT(stream_cache != NULL, "stream_cache is NULL") ;
  HQASSERT(string != NULL, "string is NULL") ;
  HQASSERT(length > 0, "length is not greater than zero") ;
  HQASSERT(new_stream_entry != NULL, "stream_entry is NULL") ;

  *new_stream_entry = NULL ;

  if (pclxl_stream_cache_find(stream_cache, string, length, &entry, &prev, &hash)) {
    if (prev == NULL) {
      stream_cache->table[hash] = entry->next ;
    } else {
      prev->next = entry->next ;
    }
    stream_free_entry(stream_cache, entry) ;
  }

  if ((entry = mm_alloc(stream_cache->memory_pool,
                        sizeof(PCLXLStreamCacheEntry), MM_ALLOC_CLASS_PCLXL_STREAM)) == NULL) {
    return FALSE ;
  } else {
    entry->next = stream_cache->table[hash] ;
    stream_cache->table[hash] = entry ;
    entry->is_running = FALSE ;
  }

  if (! create_user_stream_storage(&entry->storage, string, length,
                                   stream_cache->memory_pool)) {
    mm_free(stream_cache->memory_pool, entry, MM_ALLOC_CLASS_PCLXL_STREAM) ;
    return FALSE ;
  }

  *new_stream_entry = entry ;
  return TRUE ;
}

Bool pclxl_stream_cache_create(PCLXLStreamCache **stream_cache, uint32 table_size,
                               mm_pool_t memory_pool)
{
  PCLXLStreamCache *new_stream_cache ;
  int32 i ;

  UNUSED_PARAM(uint32, table_size) ;
  HQASSERT(stream_cache != NULL, "stream_cache is NULL") ;

  if ((new_stream_cache = mm_alloc(memory_pool,
                                   sizeof(PCLXLStreamCache),
                                   MM_ALLOC_CLASS_PCLXL_STREAM)) == NULL) {
    *stream_cache = NULL ;
    return FALSE ;
  }
  for (i=0; i < STREAM_CACHE_TABLE_SIZE; i++) {
    new_stream_cache->table[i] = NULL ;
  }

#if 0
  new_stream_cache->table_size = table_size ;
#else
  new_stream_cache->table_size = STREAM_CACHE_TABLE_SIZE ;
#endif
  new_stream_cache->memory_pool = memory_pool ;

  *stream_cache = new_stream_cache ;

  return TRUE ;
}

void pclxl_stream_cache_destroy(PCLXLStreamCache **stream_cache)
{
  int32  i ;
  PCLXLStreamCacheEntry *curr, *next_entry ;
  HQASSERT(stream_cache != NULL, "stream_cache is NULL") ;
  HQASSERT(*stream_cache != NULL, "*stream_cache is NULL") ;

  /* If we really need a faster way to iterate over entires we should
     maintain a stack of them. */
  for (i=0; i < STREAM_CACHE_TABLE_SIZE; i++) {
    curr = (*stream_cache)->table[i] ;
    while (curr != NULL) {
      next_entry = curr->next ;
      stream_free_entry(*stream_cache, curr) ;
      curr = next_entry ;
    }
  }

  mm_free((*stream_cache)->memory_pool,
          *stream_cache, sizeof(PCLXLStreamCache)) ;
  *stream_cache = NULL ;
}

/* Records the specified byte within the active user defined stream
   definition. */
static
Bool push_user_stream_byte(PCLXLStreamCache *stream_cache,
                           PCLXLStreamCacheEntry *stream_entry, int32 ch)
{
  PCLXLUserStreamStorage *storage ;

  HQASSERT(stream_cache != NULL, "stream_cache is NULL") ;
  HQASSERT(ch >= 0 && ch <= 255, "umm, ch appears to be invalid") ;
  HQASSERT(user_stream_being_defined != NULL, "user_stream_being_defined is NULL") ;
  storage = stream_entry->storage ;
  HQASSERT(storage != NULL, "storage is NULL") ;

  /* Do we need a new data block to store this byte? */
  if (storage->total_bytes % USER_STREAM_DATA_BLOCK_SIZE == 0) {
    USER_STREAM_DATA_BLOCK *data ;

    data = mm_alloc(stream_cache->memory_pool,
                    sizeof(USER_STREAM_DATA_BLOCK),
                    MM_ALLOC_CLASS_PCLXL_STREAM) ;
    if (data == NULL)
      return FALSE ;
    storage->write_pos = &(data->data)[0] ;
    data->next = NULL ;
    data->prev = storage->last_data_block ;

    if (storage->first_data_block == NULL) {
      storage->first_data_block = data ;
    } else {
      storage->last_data_block->next = data ;
    }
    storage->last_data_block = data ;
  }

  HQASSERT( storage->write_pos >= &(storage->last_data_block->data)[0] &&
            storage->write_pos < (&(storage->last_data_block->data)[0] +
            USER_STREAM_DATA_BLOCK_SIZE),
            "storage->write_pos has become corrupt" ) ;

  *(storage->write_pos)++ = (uint8)ch ;
  storage->total_bytes++ ;

  return TRUE ;
}

/* ============================================================================
 * PCL XL user defined stream device.
 * ============================================================================
 */

/* This is so that the XL parser can be invoked on a FILELIST which
   obtains its data from a user defined stream held in memory. */

#define PCLXL_USER_STREAMS_DEVICE_TYPE (34)

#define PCLXL_USER_STREAM_DEVICE_OBJECT_NAME "PCL XL user defined stream device"

#define PCLXL_USER_STREAM_DEVICE_NAME "_pclxl_user_stream_device"

/* The device from which we will attempt to read all user defined
   streams. */
DEVICELIST *pclxl_user_streams_device = NULL ;

/* Maximum PCL XL user streams device id. */
#define PCLXL_USER_STREAMS_DEVICE_MAX_ID (0xff)

/* List of all mounted PCL XL user defined stream devices.
 *
 * The list of PCLXL_USER_STREAMS devices is kept in increasing
 * PCLXL_USER_STREAMS_DEVICE::pclxl_user_streams_id order. When a PCL
 * XL user defined stream device is mounted, the smallest unused id is
 * reused and the new device inserted into the list in order.
 */
static dll_list_t dls_pclxl_user_stream_devs ;

typedef struct PCLXL_USER_STREAMS_DEVICE {
  dll_link_t           link ;                  /* PCL XL user defined stream device list link. */
  int32                pclxl_user_streams_id ; /* Mounted PCL XL user defined stream device id. */

  OBJECT_NAME_MEMBER
} PCLXL_USER_STREAMS_DEVICE ;

static int32 Getc_stream(DEVICE_FILEDESCRIPTOR descriptor) ;

/* ============================================================================
 * PCL XL user defined stream device functions.
 * ============================================================================
 */
static int32 RIPCALL pclxl_user_streams_init_device (DEVICELIST * dev)
{
  int32 pclxl_user_streams_id ;
  PCLXL_USER_STREAMS_DEVICE* pclxl_user_streams_device ;
  PCLXL_USER_STREAMS_DEVICE* pclxl_user_streams_deviceT ;

  devices_set_last_error(DeviceNoError);

  pclxl_user_streams_device = (PCLXL_USER_STREAMS_DEVICE*)dev->private_data;

  /* Name object as memory has been allocated - by the RIP */
  NAME_OBJECT(pclxl_user_streams_device, PCLXL_USER_STREAM_DEVICE_OBJECT_NAME) ;

  /* Find unused PCL XL id to use. Devices are held in ascending id order. */
  pclxl_user_streams_id = 0 ;
  pclxl_user_streams_deviceT = DLL_GET_HEAD(&dls_pclxl_user_stream_devs, PCLXL_USER_STREAMS_DEVICE, link) ;
  while ( (pclxl_user_streams_deviceT != NULL) && (pclxl_user_streams_id == pclxl_user_streams_deviceT->pclxl_user_streams_id) ) {
    pclxl_user_streams_deviceT = DLL_GET_NEXT(pclxl_user_streams_deviceT, PCLXL_USER_STREAMS_DEVICE, link) ;
    pclxl_user_streams_id++ ;
  }
  if ( pclxl_user_streams_id > PCLXL_USER_STREAMS_DEVICE_MAX_ID ) {
    devices_set_last_error(DeviceLimitCheck) ;
    return -1 ;
  }
  HQASSERT(((pclxl_user_streams_deviceT == NULL) || (pclxl_user_streams_id < pclxl_user_streams_deviceT->pclxl_user_streams_id)),
           "Active pclxl_user_streams device list out of order") ;
  pclxl_user_streams_device->pclxl_user_streams_id = pclxl_user_streams_id ;
  /* Add device in sequence */
  DLL_RESET_LINK(pclxl_user_streams_device, link) ;
  if ( pclxl_user_streams_deviceT == NULL ) {
    DLL_ADD_TAIL(&dls_pclxl_user_stream_devs, pclxl_user_streams_device, link) ;
  } else {
    DLL_ADD_BEFORE(pclxl_user_streams_deviceT, pclxl_user_streams_device, link) ;
  }

  return 0 ;
}

static DEVICE_FILEDESCRIPTOR RIPCALL pclxl_user_streams_open_file(DEVICELIST * dev,
                                                                  uint8 * filename, int32 openflags)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  UNUSED_PARAM(uint8 *, filename) ;
  UNUSED_PARAM(int32, openflags) ;
  devices_set_last_error(DeviceNoError) ;
  return 1 ;
}

static int32 RIPCALL pclxl_user_streams_read_file(DEVICELIST * dev,
                                                  DEVICE_FILEDESCRIPTOR descriptor,
                                                  uint8 * buf, int32 len)
{
  uint8* limit = buf + len ;
  uint8* insert = buf ;
  int32 ch ;
  PCLXL_USER_STREAMS_DEVICE *pclxl_user_streams_device ;

  HQASSERT(dev != NULL, "dev is NULL") ;
  HQASSERT(buf != NULL, "buf is NULL") ;

  pclxl_user_streams_device = (PCLXL_USER_STREAMS_DEVICE*)dev->private_data ;
  VERIFY_OBJECT(pclxl_user_streams_device, PCLXL_USER_STREAM_DEVICE_OBJECT_NAME) ;

  while (insert != limit) {
    if ( (ch = Getc_stream(descriptor)) == EOF)
      break ;
    *insert++ = (uint8)ch ;
  }
  len = CAST_PTRDIFFT_TO_INT32(insert - buf) ;

  devices_set_last_error(DeviceNoError) ;
  return len ;
}

static int32 RIPCALL pclxl_user_streams_write_file(DEVICELIST *dev ,
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

static int32 RIPCALL pclxl_user_streams_close_file(DEVICELIST *dev,
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

static int32 RIPCALL pclxl_user_streams_seek_file(DEVICELIST * dev,
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

static int32 RIPCALL pclxl_user_streams_bytes_file(DEVICELIST * dev,
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

static int32 RIPCALL pclxl_user_streams_status_file(DEVICELIST * dev,
                                                    uint8 * filename,
                                                    STAT * statbuf)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  UNUSED_PARAM(uint8 *, filename) ;
  UNUSED_PARAM(STAT *, statbuf) ;
  devices_set_last_error(DeviceIOError) ;
  return -1 ;
}

static void * RIPCALL pclxl_user_streams_start_list(DEVICELIST * dev,
                                                    uint8 * pattern)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  UNUSED_PARAM(uint8 *, pattern) ;
  devices_set_last_error(DeviceNoError) ;
  return NULL ;
}

static int32 RIPCALL pclxl_user_streams_next_list(DEVICELIST * dev, void ** handle,
                                                  uint8 * pattern, FILEENTRY *entry)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  UNUSED_PARAM(void **, handle) ;
  UNUSED_PARAM(uint8 *, pattern) ;
  UNUSED_PARAM(FILEENTRY *, entry) ;
  devices_set_last_error(DeviceIOError) ;
  return -1 ;
}

static int32 RIPCALL pclxl_user_streams_end_list(DEVICELIST * dev, void * handle)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  UNUSED_PARAM(void *, handle) ;
  devices_set_last_error(DeviceIOError) ;
  return -1 ;
}

static int32 RIPCALL pclxl_user_streams_rename_file(DEVICELIST * dev, uint8 * file1,
                                                    uint8 * file2)
{
  UNUSED_PARAM(DEVICELIST *, dev );
  UNUSED_PARAM(uint8 *, file1) ;
  UNUSED_PARAM(uint8 *, file2) ;
  devices_set_last_error(DeviceIOError) ;
  return -1 ;
}

static int32 RIPCALL pclxl_user_streams_delete_file(DEVICELIST * dev, uint8 * filename)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  UNUSED_PARAM(uint8 *, filename) ;
  devices_set_last_error(DeviceIOError) ;
  return -1 ;
}

static int32 RIPCALL pclxl_user_streams_set_param(DEVICELIST * dev, DEVICEPARAM * param)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  UNUSED_PARAM(DEVICEPARAM *, param) ;
  devices_set_last_error(DeviceNoError) ;
  return ParamIgnored ;
}

static int32 RIPCALL pclxl_user_streams_start_param(DEVICELIST * dev)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  devices_set_last_error(DeviceNoError) ;
  return 0 ;
}

static int32 RIPCALL pclxl_user_streams_get_param(DEVICELIST * dev, DEVICEPARAM * param)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  UNUSED_PARAM(DEVICEPARAM *, param) ;
  devices_set_last_error(DeviceNoError) ;
  return ParamIgnored ;
}

static int32 RIPCALL pclxl_user_streams_status_device(DEVICELIST * dev, DEVSTAT * devstat)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  UNUSED_PARAM(DEVSTAT *, devstat) ;
  devices_set_last_error(DeviceIOError) ;
  return -1 ;
}

static int32 RIPCALL pclxl_user_streams_dismount(DEVICELIST * dev)
{
  PCLXL_USER_STREAMS_DEVICE* pclxl_user_streams_device ;
  HQASSERT((dev != NULL), "dev is NULL")  ;

  pclxl_user_streams_device = (PCLXL_USER_STREAMS_DEVICE*)(dev->private_data) ;
  VERIFY_OBJECT(pclxl_user_streams_device, PCLXL_USER_STREAM_DEVICE_OBJECT_NAME) ;

  if ( isDeviceUndismountable(dev) ) {
    devices_set_last_error(DeviceIOError) ;
    return -1 ;
  } else {
    devices_set_last_error(DeviceNoError) ;

    /* Remove from list of PCL XL user defined stream devices. */
    DLL_REMOVE(pclxl_user_streams_device, link) ;

    return 0 ;
  }
}

static int32 RIPCALL pclxl_user_streams_buffer_size(DEVICELIST * dev)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  devices_set_last_error(DeviceIOError) ;
  return -1 ;
}

static int32 RIPCALL pclxl_user_streams_spare(void)
{
  devices_set_last_error(DeviceIOError) ;
  return -1 ;
}

DEVICETYPE Pclxl_User_Streams_Device_Type = {
  PCLXL_USER_STREAMS_DEVICE_TYPE,      /* the device ID number */
  DEVICESMALLBUFF ,                    /* flags to indicate specifics of device */
  sizeof(PCLXL_USER_STREAMS_DEVICE),   /* the size of the private data */
  0,                                   /* minimum ticks between tickle functions */
  NULL,                                /* procedure to service the device */
  devices_last_error,                  /* return last error for this device */
  pclxl_user_streams_init_device,      /* call to initialise device */
  pclxl_user_streams_open_file,        /* call to open file on device */
  pclxl_user_streams_read_file,        /* call to read data from file on device */
  pclxl_user_streams_write_file,       /* call to write data to file on device */
  pclxl_user_streams_close_file,       /* call to close file on device */
  pclxl_user_streams_close_file,       /* call to abort action on the device */
  pclxl_user_streams_seek_file,        /* call to seek file on device */
  pclxl_user_streams_bytes_file,       /* call to get bytes avail on an open file */
  pclxl_user_streams_status_file,      /* call to check status of file */
  pclxl_user_streams_start_list,       /* call to start listing files */
  pclxl_user_streams_next_list,        /* call to get next file in list */
  pclxl_user_streams_end_list,         /* call to end listing */
  pclxl_user_streams_rename_file,      /* rename file on the device */
  pclxl_user_streams_delete_file,      /* remove file from device */
  pclxl_user_streams_set_param,        /* call to set device parameter */
  pclxl_user_streams_start_param,      /* call to start getting device parameters */
  pclxl_user_streams_get_param,        /* call to get the next device parameter */
  pclxl_user_streams_status_device,    /* call to get the status of the device */
  pclxl_user_streams_dismount,         /* call to dismount the device */
  pclxl_user_streams_buffer_size,
  NULL,                                /* ignore ioctl calls */
  pclxl_user_streams_spare             /* spare slots */
} ;

/* ============================================================================
 * Mount a single PCL XL user defined stream reading device. This is
 * done at the start/end of a PCL XL job because the device needs
 * access to the PCL XL context.
 * ============================================================================
 */

Bool pclxl_mount_user_defined_stream_device(void)
{
  PCLXL_USER_STREAMS_DEVICE *stream_dev ;

  pclxl_user_streams_device = device_alloc(STRING_AND_LENGTH(PCLXL_USER_STREAM_DEVICE_NAME)) ;

  if (! device_connect(pclxl_user_streams_device, PCLXL_USER_STREAMS_DEVICE_TYPE, PCLXL_USER_STREAM_DEVICE_NAME,
                       DEVICEUNDISMOUNTABLE|DEVICEENABLED, TRUE)) {
    device_free(pclxl_user_streams_device) ;
    pclxl_user_streams_device = NULL ;
    return FALSE ;
  }

  stream_dev = (PCLXL_USER_STREAMS_DEVICE*)(pclxl_user_streams_device->private_data) ;
  VERIFY_OBJECT(stream_dev, PCLXL_USER_STREAM_DEVICE_OBJECT_NAME) ;

  return TRUE ;
}

/* Unmount the PCL XL user defined stream reading device. */
void pclxl_unmount_user_defined_stream_device(void)
{
  if (pclxl_user_streams_device != NULL) {
    /* Do our best to unmount the PCL XL user stream read device. */
    ClearUndismountableDevice(pclxl_user_streams_device) ;

    /* Call the device dismount directly as PS semantics do not
       apply. */
    if ((*theIDevDismount( pclxl_user_streams_device ))( pclxl_user_streams_device ) == -1) {
      HQFAIL("Unable to dismount PCL XL user stream read device.") ;
    }

    device_free(pclxl_user_streams_device) ;
    pclxl_user_streams_device = NULL ;
  }
}

/* ============================================================================
 * User defined stream open/close and read bytes.
 * ============================================================================
 */
#define MAX_STREAM_NEST_LEVEL 32
#define STREAM_CLOSED -1

typedef struct PCLXLStreamReadInstance {
  /* Basically a read stream file descriptor. */
  int32 read_id ;
  /* Pointer to the data block we are reading. */
  USER_STREAM_DATA_BLOCK *current_read_data_block ;
  /* Where to read the next byte from when executing this stream. */
  uint8 *read_pos ;
  /* When a stream is opened for reading, we layer a FILELIST onto
     it. */
  /* Used when reading a stream. How many bytes left to read. */
  uint32 total_remaining ;
  FILELIST read_stream ;
  /* Keep the FILELIST buffer the same size as a macro data block. */
  uint8 read_buf[USER_STREAM_DATA_BLOCK_SIZE] ;
  /* Pointer to the stream we are reading. */
  PCLXLUserStreamStorage *storage ;
} PCLXLStreamReadInstance ;

/* How many read streams do we have? */
static uint32 num_read_streams ;
static PCLXLStreamReadInstance read_streams[MAX_STREAM_NEST_LEVEL] ;

static Bool open_stream_for_read(PCLXL_PARSER_CONTEXT parser_context,
                                 PCLXLStreamCacheEntry *stream_entry,
                                 FILELIST** readstream)
{
  PCLXLUserStreamStorage *storage ;
  PCLXLStreamReadInstance *read_instance ;
  int32 i;
  int32 readstream_id ;

  HQASSERT(parser_context != NULL, "parser_context is NULL") ;
  HQASSERT(stream_entry != NULL, "stream_entry is NULL") ;

  HQASSERT(readstream != NULL, "readstream is NULL") ;
  HQASSERT(pclxl_user_streams_device != NULL, "pclxl_user_streams_device is NULL") ;

  *readstream = NULL ;
  storage = stream_entry->storage ;
  HQASSERT(storage != NULL, "storage is NULL") ;

  if (num_read_streams == MAX_STREAM_NEST_LEVEL) {
    PCLXL_ERROR_HANDLER(parser_context->pclxl_context, PCLXL_SS_USERSTREAM, PCLXL_STREAM_NESTING_FULL,
                        ("Stream nesting level is greater than %d", MAX_STREAM_NEST_LEVEL));
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
  read_instance->current_read_data_block = storage->first_data_block ;
  read_instance->read_pos = &(read_instance->current_read_data_block->data)[0] ;
  read_instance->total_remaining = storage->total_bytes ;
  read_instance->storage = storage ;

  init_filelist_struct(&(read_instance->read_stream),
                       NAME_AND_LENGTH("PCLXLStreamFileList"),
                       REALFILE_FLAG | READ_FLAG | OPEN_FLAG,
                       readstream_id /* descriptor */,
                       &(read_instance->read_buf)[0] /* buf */,
                       USER_STREAM_DATA_BLOCK_SIZE /* buf size */,
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
                       pclxl_user_streams_device /* device */,
                       NULL /* underfile */,
                       NULL /* next */) ;

  num_read_streams++ ;
  *readstream = &(read_instance->read_stream) ;

  return TRUE ;
}

void close_stream(
  PCLXL_CONTEXT pclxl_context,
  FILELIST **readstream)
{
  PCLXLStreamReadInstance *read_instance ;
  PCLXLStreamCacheEntry* stream_entry;
  DEVICE_FILEDESCRIPTOR readstream_id ;

  HQASSERT(readstream != NULL, "readstream is NULL") ;
  HQASSERT(*readstream != NULL, "*readstream is NULL") ;

  if (num_read_streams > 0) {
    readstream_id = theIDescriptor((*readstream)) ;
    HQASSERT((readstream_id >= 0 && readstream_id < MAX_STREAM_NEST_LEVEL),
             "Read stream file descriptor is corrupt.") ;

    read_instance = &read_streams[readstream_id] ;

    read_instance->read_id = STREAM_CLOSED ;
    read_instance->current_read_data_block = NULL ;
    read_instance->read_pos = NULL ;
    read_instance->total_remaining = 0 ;
    read_instance->read_stream.typetag = tag_LIMIT ;
    num_read_streams-- ;
    *readstream = NULL ;

    stream_entry = pclxl_stream_cache_get(pclxl_context->stream_cache,
                                          read_instance->storage->name,
                                          read_instance->storage->name_len);
    HQASSERT((stream_entry != NULL),
             "User stream that is being closed not found in cache");
    HQASSERT((stream_entry->is_running),
             "Closing user stream that is not running");
    stream_entry->is_running = FALSE;
  }
}

/* Do not call this directly, this function is used by the macro read
   device implementation only. */
static int32 Getc_stream(DEVICE_FILEDESCRIPTOR descriptor)
{
  PCLXLUserStreamStorage *storage ;
  PCLXLStreamReadInstance *read_instance ;

  HQASSERT((descriptor >= 0 && descriptor < MAX_STREAM_NEST_LEVEL),
           "Read stream file descriptor is corrupt.") ;

  read_instance = &read_streams[descriptor] ;
  storage = read_instance->storage ;

  HQASSERT(storage != NULL, "storage is invalid") ;
  HQASSERT(read_instance->current_read_data_block != NULL, "current_read_data_block is NULL") ;

  /* End of macro stream. */
  if (read_instance->total_remaining == 0)
    return EOF ;

  /* We need to move onto the next data block. */
  if (read_instance->read_pos - &(read_instance->current_read_data_block->data)[0]
      == USER_STREAM_DATA_BLOCK_SIZE) {
    read_instance->current_read_data_block = read_instance->current_read_data_block->next ;
    read_instance->read_pos = &(read_instance->current_read_data_block->data)[0] ;
    HQASSERT(read_instance->current_read_data_block != NULL,
             "no next block yet there are still bytes remaining") ;
  }

  read_instance->total_remaining-- ;
  return (int32)*(read_instance->read_pos)++ ;
}

/* ============================================================================
 * PCL XL user defined stream device init/finish. Executed at RIP
 * startup/shutdown.
 * ============================================================================
 */

void init_C_globals_pclxluserstream(void) ;

void pclxl_initialize_streams(void)
{
  /* The base re-initialisation is the same as RIP startup, so share the
     global reset. */
  init_C_globals_pclxluserstream() ;
}

Bool pclxl_user_streams_init(void)
{
  return device_type_add(&Pclxl_User_Streams_Device_Type) ;
}

void pclxl_user_streams_finish(void)
{
}

/* ============================================================================
 * PCL XL operators below here.
 * ============================================================================
 */

static
PCLXL_ATTR_MATCH stream_name_match[2] = {
#define STREAMNAME_STREAM_NAME    (0)
  {PCLXL_AT_StreamName | PCLXL_ATTR_REQUIRED},
  PCLXL_MATCH_END
};

/*
 * Tag 0x5b BeginStream
 */
Bool
pclxl_op_begin_stream(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXLStreamCacheEntry* new_stream_entry;
  uint8* name;
  uint32 name_len;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, stream_name_match,
                             pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* StreamName */
  pclxl_attr_get_byte_len(stream_name_match[STREAMNAME_STREAM_NAME].result, &name, &name_len);

  /* If we are executing a user defined stream, throw an error. */
  if (user_stream_being_executed > 0) {
    /** \todo Whats the error?? */
    return FALSE ;
  }

  /* This is a unique insert so if a stream by the same name exists,
     it will be deleted. */
  if (! pclxl_stream_cache_insert(parser_context->pclxl_context->stream_cache,
                                  name, name_len, &new_stream_entry)) {
    return FALSE ;
  }

  user_stream_being_defined = new_stream_entry ;

  return TRUE ;
}

/*
 * Tag 0x5c ReadStream
 */
Bool
pclxl_op_read_stream(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define READSTREAM_STREAM_DATA_LENGTH (0)
    {PCLXL_AT_StreamDataLength | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_EMBEDDED_READER embedded_reader;
  uint32 stream_data_length;
  uint8 ch ;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* StreamDataLength */
  stream_data_length = pclxl_attr_get_uint(match[READSTREAM_STREAM_DATA_LENGTH].result);

  /* If we are executing a user defined stream, throw an error. */
  if (user_stream_being_executed > 0) {
    /** \todo Whats the error?? */
    return FALSE ;
  }

  if ( !pclxl_stream_embedded_init(parser_context->pclxl_context,
                                   pclxl_parser_current_stream(parser_context),
                                   parser_context->data_source_big_endian,
                                   &embedded_reader) ) {
    return(FALSE);
  }

  if ( stream_data_length != pclxl_embedded_length(&embedded_reader) ) {
    PCLXL_ERROR_HANDLER(parser_context->pclxl_context, PCLXL_SS_USERSTREAM, PCLXL_ILLEGAL_DATA_LENGTH,
                        ("Data stream length does not match embedded data length"));
    return(FALSE);
  }

  while ( stream_data_length-- > 0 ) {
    if ( !pclxl_embedded_read_bytes(&embedded_reader, &ch, 1) ) {
      return(FALSE);
    }
    if ( !push_user_stream_byte(parser_context->pclxl_context->stream_cache,
                                user_stream_being_defined, ch) ) {
      return FALSE ;
    }
  }

  return TRUE ;
}

/*
 * Tag 0x5d EndStream
 */
Bool
pclxl_op_end_stream(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  user_stream_being_defined = NULL ;

  if ( !pclxl_attr_set_match_empty(parser_context->attr_set, pclxl_context,
                                   PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* If we are executing a user defined stream, throw an error. */
  if (user_stream_being_executed > 0) {
    /** \todo Whats the error?? */
    return FALSE ;
  }

  return TRUE ;
}

/*
 * Tag 0x5e ExecStream
 */
Bool
pclxl_op_exec_stream(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXLStreamCacheEntry* stream_entry;
  uint8* name;
  uint32 name_len;
  FILELIST* flptr;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, stream_name_match,
                             pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* StreamName */
  pclxl_attr_get_byte_len(stream_name_match[STREAMNAME_STREAM_NAME].result, &name, &name_len);

  /* If the stream entry does not exist, its an error. */
  if ( (stream_entry = pclxl_stream_cache_get(parser_context->pclxl_context->stream_cache,
                                              name, name_len)) == NULL) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_USERSTREAM, PCLXL_STREAM_UNDEFINED,
                        ("stream name not found"));
    return(FALSE);
  }

  /* We are not allowed to run any stream recursively. */
  if ( stream_entry->is_running ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_USERSTREAM, PCLXL_STREAM_CALLING_ITSELF,
                        ("Stream is already running. They cannot be run recursively"));
    return(FALSE);
  }

  /* Open the stream for read. */
  if ( !open_stream_for_read(parser_context, stream_entry, &flptr) ) {
    return FALSE ;
  }

  stream_entry->is_running = TRUE;

  return(pclxl_parser_push_stream(parser_context, flptr));
}

/*
 * Tag 0x5f RemoveStream
 */
Bool
pclxl_op_remove_stream(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXLStreamCacheEntry* stream_entry;
  uint8* name;
  uint32 name_len;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, stream_name_match,
                             pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* StreamName */
  pclxl_attr_get_byte_len(stream_name_match[STREAMNAME_STREAM_NAME].result, &name, &name_len);

  /* If the stream entry does not exist, its an error. */
  if ( (stream_entry = pclxl_stream_cache_get(parser_context->pclxl_context->stream_cache,
                                              name, name_len)) == NULL) {
    PCLXL_WARNING_HANDLER(pclxl_context, PCLXL_SS_USERSTREAM, PCLXL_UNDEFINED_STREAM_NOT_REMOVED,
                          ("Undefined stream not removed"));
    return(TRUE);
  }

  if ( stream_entry->is_running ) {
    PCLXL_WARNING_HANDLER(pclxl_context, PCLXL_SS_USERSTREAM, PCLXL_STREAM_ALREADY_RUNNING,
                          ("Cannot remove stream when in use"));
    return(TRUE);
  }

  pclxl_stream_cache_remove(pclxl_context->stream_cache, name, name_len);

  HQASSERT(user_stream_being_defined == NULL,
           "Umm, user stream being defined should be NULL") ;

  return TRUE ;
}

void init_C_globals_pclxluserstream(void)
{
  int32 i ;

  DLL_RESET_LIST(&dls_pclxl_user_stream_devs);
  user_stream_being_executed = 0 ;
  user_stream_being_defined = NULL ;
  pclxl_user_streams_device = NULL ;

  num_read_streams = 0 ;

  for (i=0; i < MAX_STREAM_NEST_LEVEL; i++) {
    read_streams[i].read_id = STREAM_CLOSED ;
    read_streams[i].current_read_data_block = NULL ;
    read_streams[i].read_pos = NULL ;
    read_streams[i].total_remaining = 0 ;
    read_streams[i].read_stream.typetag = tag_LIMIT ;
  }
}

/******************************************************************************
* Log stripped */
