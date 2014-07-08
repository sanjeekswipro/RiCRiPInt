/** \file
 * \ingroup zipdev
 *
 * $HopeName: COREzipdev!src:writeonly_zipdev.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * A device that allows files to be written directly to a zip archive.
 *
 * The device has a strict usage pattern:
 *
 * 1. Set 'BufferSize' to use when compressing file data (defaults to 16KB).
 * 2. Set the 'ArchiveName' parameter to create the archive.
 * 3. Write files; there can be no more than one file open at any time.
 * 4. Set the 'ArchiveName' parameter to a zero-length string.
 *
 * The archive must be written to a seekable device (it wouldn't be too hard to
 * lift this restriction).
 *
 * This device does not support reading or listing of the files on the device;
 * it's just a black box you throw data into.
 */

#include "core.h"
#include "coreinit.h"

#include "lists.h"
#include "zlib.h"
#include "hqmemcpy.h"
#include "hqmemcmp.h"
#include "mm.h"
#include "devices.h"
#include "devs.h"
#include "swerrors.h"

#include "zip_util.h"
#include "zip_archive.h"
#include "writeonly_zipdev.h"


/* -- Private Macros */

/** Default buffer size. */
#define WOZ_DEFAULT_BUFSIZE  (16384)

/** Extract the WriteonlyZip* from 'dev', storing it in 'self'. */
#define GET_SELF(dev_, self_) MACRO_START \
  HQASSERT((dev_) != NULL, "'dev' cannot be null."); \
  (self_) = (WriteonlyZip*)(dev_)->private_data; \
  VERIFY_OBJECT((self_), WRITEONLY_ZIP_NAME); \
MACRO_END

/* Helper for static initialisation of DEVICEPARAM structures. */
#define PARAM_NAME(name)  STRING_AND_LENGTH(name)

/* Evaluates to TRUE if the passed parameters have the same name and type. */
#define PARAM_NAME_TYPE_EQUAL(a_, b_) \
  ((a_)->type == (b_)->type && HqMemCmp((a_)->paramname, (a_)->paramnamelen, \
                                        (b_)->paramname, (b_)->paramnamelen) == 0)


/** Template set of device parameters; this set is copied to a local instance
copy during construction. */
static DEVICEPARAM defaultParams[] = {
  {PARAM_NAME("ArchiveName"), ParamString, NULL, 0},
  {PARAM_NAME("BufferSize"), ParamInteger, NULL, 0},
  {PARAM_NAME("Type"), ParamString, NULL, 0},
  {PARAM_NAME("ZIP64Files"), ParamBoolean, NULL, 0}
};

/* Indexes of device paramters in parameters array */
#define WOZ_ARCHIVENAME   (0)
#define WOZ_BUFFERSIZE    (1)
#define WOZ_TYPE          (2)
#define WOZ_ZIP64FILES    (3)

static uint8* type = (uint8*)"FileSystem";

/** The number of parameters for this device. */
#define TOTAL_DEVICE_PARAMS NUM_ARRAY_ITEMS(defaultParams)

/* -- Private types */

/* Data on a list item */
typedef struct ZIP_ITEM {
  dll_link_t    link;         /**< Item hash table linked-list link - this must be first. */
  struct ZIP_ITEM* p_next;    /**< Next ZIP item in list of all items on the device. */
  FILEENTRY     filename;     /**< Normalised item name. */
  ZIP_CDIR_FILE cdir_item;    /**< Central directory record of item. */
  Bool          is_zip64;     /**< Item uses ZIP64 extension. */
  ZIP_XTRAFLD_ZIP64 z64_xtrafld; /**< ZIP64 extra field of item. */
  uint32        crc_32;       /**< CRC32 value from compressing file data. */
  HqU32x2       uncompressed; /**< Uncompressed size of item. */
  HqU32x2       compressed;   /**< Compressed size of item. */

  OBJECT_NAME_MEMBER
} ZIP_ITEM;

/** Return total storage for a ZIP item.  Includes space for a terminating NUL
 * for the filename for easy debugging. */
size_t ZIP_ITEM_SIZE(
  FILEENTRY*    f);
#define ZIP_ITEM_SIZE(f)  (sizeof(ZIP_ITEM) + (f)->namelength + 1)

#define ZIP_ITEM_NAME     "ZIPItem"

#define WOZ_FILENAME_TABLE_SIZE (1027u)

/* Hash table of ZIP items keyed on file name. */
typedef struct ZIP_ITEMS {
  dll_list_t    items[WOZ_FILENAME_TABLE_SIZE]; /**< Double linked list of ZIP items. */

  OBJECT_NAME_MEMBER
} ZIP_ITEMS;

#define ZIP_ITEMS_NAME  "ZIPItems"

/** Get file name list from ZIP items hash table */
dll_list_t* WOZ_TABLE_LIST(
  ZIP_ITEMS*  z,
  FILEENTRY*  f);
#define WOZ_TABLE_LIST(z, f)  (&((z)->items[zutl_strhash((f)->name, (f)->namelength)%WOZ_FILENAME_TABLE_SIZE]))

/** WriteonlyZip object structure. */
typedef struct {
  dll_link_t    link;         /**< Linked-list entry - this must be first. */

  uint32        flags;        /**< Device status flags. */

  z_stream      zlibState;    /**< Z-Lib state object, used when deflating data. */

  ZIP_ARCHIVE   archive;      /**< Archive into which compressed data is written. */
  FILELIST_CLOSEFILE old_close; /**< Original closefile function for the archive file. */

  ZIP_ITEMS*    items;        /**< Hash table of file data, indexed by normalised filenames. */
  ZIP_ITEM*     p_first_item; /**< Pointer to first item added to the device. */
  ZIP_ITEM*     p_last_item;  /**< Pointer to the last item added to the device. */

  uint8*        buffer;       /**< Pointer to output buffer - allocated memory. */
  uint8*        buff_next;    /**< Pointer to start of unused output buffer. */
  int32         buff_avail;   /**< Amount of output buffer still to be filled in. */
  int32         buff_size;    /**< Size of output buffer. */
  HqU32x2       buff_total;   /**< Running total of bytes written to archive - used for file positions */

  int32         next_param;   /**< Index of next parameter to return. */

  DEVICEPARAM   params[TOTAL_DEVICE_PARAMS]; /**< Device parameters. */

  OBJECT_NAME_MEMBER
} WriteonlyZip;


/** \brief An archive is currently being created. */
#define WOZ_ARCHIVE_OPEN    (0x01)
/** \brief An error occurred during creation of last archive started. */
#define WOZ_ARCHIVE_ERROR   (0x02)
/** \brief An archive item file is currently open. */
#define WOZ_FILE_OPEN       (0x04)
/** \brief Generate ZIP64 file entries. */
#define WOZ_ZIP64_FILES     (0x08)


/** \brief Is a new archive currently being created. */
Bool woz_is_archive_open(
  WriteonlyZip* woz);
#define woz_is_archive_open(w)  (((w)->flags & WOZ_ARCHIVE_OPEN) != 0)

/** \brief Is there an error generating content for the last opened archive. */
Bool woz_is_archive_error(
  WriteonlyZip* woz);
#define woz_is_archive_error(w) (((w)->flags & WOZ_ARCHIVE_ERROR) != 0)

/** \brief Is a file item currently open on the device. */
Bool woz_is_file_open(
  WriteonlyZip* woz);
#define woz_is_file_open(w)     (((w)->flags & WOZ_FILE_OPEN) != 0)

/** \brief Create ZIP64 file entries. */
Bool woz_create_zip64_files(
  WriteonlyZip* woz);
#define woz_create_zip64_files(w) (((w)->flags & WOZ_ZIP64_FILES) != 0)


/** \brief Set named device status flags. */
void WOZ_SET_FLAGS(
  WriteonlyZip*   woz,
  int32           flags);
#define WOZ_SET_FLAGS(w, f)     MACRO_START (w)->flags |= (f); MACRO_END

/** \brief Clear named device status flags. */
void WOZ_CLEAR_FLAGS(
  WriteonlyZip*   woz,
  int32           flags);
#define WOZ_CLEAR_FLAGS(w, f)   MACRO_START (w)->flags &= ~(f); MACRO_END


/** Object name macro. */
#define WRITEONLY_ZIP_NAME "WriteonlyZip"


/* Device only ever has one file open at a time, so use a nice non-0 value */
#define WOZ_FD  (0x1234678)


/** \brief List of all mounted Write-only ZIP devices; used in GC scanning. */
static dll_list_t wozInstances;


static
Bool filenames_equal(
  FILEENTRY*  fn1,
  FILEENTRY*  fn2)
{
  HQASSERT((fn1 != NULL),
           "filenames_equal: NULL pointer to first file name");
  HQASSERT((fn2 != NULL),
           "filenames_equal: NULL pointer to second file name");

  return((fn1 == fn2) ||
         ((fn1->namelength == fn2->namelength) &&
          ((fn1->name == fn2->name) ||
           (HqMemCmp(fn1->name, fn1->namelength, fn2->name, fn2->namelength) == 0))));

} /* filenames_equal */


static
Bool normalise_filename(
  FILEENTRY*  p_fileentry,
  uint8*      buffer,
  uint8*      filename)
{
  uint8   ch;
  Bool    seen_slash = FALSE;
  Bool    drop_leading_slash = TRUE;

  HQASSERT((p_fileentry != NULL),
           "normalise_filename: NULL pointer to returned fileentry");
  HQASSERT((buffer != NULL),
           "normalise_filename: NULL pointer to normalised filename buffer");
  HQASSERT((filename != NULL),
           "normalise_filename: NULL pointer to original filename");

  /* Copy original filename into normalised buffer, translating back slashes
   * into forward slashes, and dropping any leading slashes altogether.
   * Detect sequences of forward slashes, including any leading slash (i.e.
   * handle file names that start with //.)
   */
  p_fileentry->name = buffer;
  while ( (ch = *filename++) != '\0' ) {
    switch ( ch ) {
    case '\\':
      ch = '/';
      /* FALLTHROUGH */

    case '/':
      if ( seen_slash ) {
        return(FALSE);
      }
      seen_slash = TRUE;
      if ( drop_leading_slash ) {
        break;
      }
      /* FALLTHROUGH */

    default:
      seen_slash = seen_slash && (ch == '/');
      drop_leading_slash = FALSE;
      *buffer++ = ch;
    }
  }

  /* Calculate length of normalised name */
  p_fileentry->namelength = (int32)(buffer - p_fileentry->name);

  /* Filename can't be empty or end with a forward slash */
  return((p_fileentry->namelength > 0) &&
         (p_fileentry->name[p_fileentry->namelength - 1] != '/'));

} /* normalise_filename */

static
ZIP_ITEM* woz_find_item(
  ZIP_ITEMS*  zip_items,
  FILEENTRY*  filename)
{
  ZIP_ITEM*   p_item;
  dll_list_t* item_list;

  HQASSERT((filename != NULL),
           "woz_find_item: NULL filename pointer");

  if ( zip_items == NULL ) {
    return(NULL);
  }

  VERIFY_OBJECT(zip_items, ZIP_ITEMS_NAME);

  item_list = WOZ_TABLE_LIST(zip_items, filename);
  p_item = DLL_GET_HEAD(item_list, ZIP_ITEM, link);
  while ( p_item != NULL ) {
    if ( filenames_equal(&p_item->filename, filename) ) {
      break;
    }
    p_item = DLL_GET_NEXT(p_item, ZIP_ITEM, link);
  }

  return(p_item);

} /* woz_find_item*/

static
ZIP_ITEM* woz_lookup_filename(
  ZIP_ITEMS*  zip_items,
  uint8*      filename)
{
  FILEENTRY     fe_filename;
  uint8         fn_buffer[LONGESTFILENAME];

  HQASSERT((filename != NULL),
           "woz_lookup_filename: NULL file name pointer");

  /* Is the file name syntax supported */
  if ( !normalise_filename(&fe_filename, fn_buffer, filename) ) {
    return(NULL);
  }

  return(woz_find_item(zip_items, &fe_filename));

} /* woz_lookup_filename */

static
void woz_add_item(
  ZIP_ITEMS*  zip_items,
  ZIP_ITEM*   p_item)
{
  HQASSERT((zip_items != NULL),
           "woz_add_item: NULL hash table pointer");
  HQASSERT((p_item != NULL),
           "woz_add_item: NULL ZIP item pointer");

  VERIFY_OBJECT(zip_items, ZIP_ITEMS_NAME);
  VERIFY_OBJECT(p_item, ZIP_ITEM_NAME);

  DLL_ADD_HEAD(WOZ_TABLE_LIST(zip_items, &p_item->filename), p_item, link);

} /* woz_add_item */


static
ZIP_ITEM* woz_create_item(
  FILEENTRY*  filename,
  Bool        is_zip64,
  HqU32x2*    offset)
{
  uint32    dateTime;
  ZIP_ITEM* p_item;

  HQASSERT((filename != NULL),
           "woz_create_item: NULL file name pointer");
  HQASSERT((offset != NULL),
           "woz_create_item: NULL file offset pointer");
  HQASSERT(((HqU32x2CompareUint32(offset, MAXUINT32) <= 0) || is_zip64),
           "woz_create_item: local file header offset overflow");

  if ( (p_item = mm_alloc(mm_pool_temp, ZIP_ITEM_SIZE(filename), MM_ALLOC_CLASS_WO_ZIP)) != NULL ) {
    NAME_OBJECT(p_item, ZIP_ITEM_NAME);

    /* Reset list links */
    DLL_RESET_LINK(p_item, link);
    p_item->p_next = NULL;

    p_item->is_zip64 = is_zip64;
    p_item->crc_32 = crc32(0, Z_NULL, 0);
    HqU32x2FromUint32(&p_item->uncompressed, 0);
    HqU32x2FromUint32(&p_item->compressed, 0);

    /* Copy the filename - NUL terminate for easier debugging */
    p_item->filename.name = (uint8*)(p_item + 1);
    p_item->filename.namelength = filename->namelength;
    HqMemCpy(p_item->filename.name, filename->name, filename->namelength);
    p_item->filename.name[p_item->filename.namelength] = '\0';

    /* Fill in a central directory record */
    p_item->cdir_item.made_by_version = ZIP_VERSION_MADEBY(ZIP_COMPAT_MSDOS, ZIP_VERSION(2, 0));

    p_item->cdir_item.lcal_file.version_needed = ZIP_VERSION(2, 0);
    p_item->cdir_item.lcal_file.flags = ZIPFLG_USE_DATADESC;
    p_item->cdir_item.lcal_file.compression = CAST_UNSIGNED_TO_UINT16(ZIPCOMP_DEFLATE);

    dateTime = zutl_msdos_date();
    p_item->cdir_item.lcal_file.mod_time = CAST_UNSIGNED_TO_UINT16(dateTime & 0xffff);
    p_item->cdir_item.lcal_file.mod_date = CAST_UNSIGNED_TO_UINT16(dateTime >> 16);

    p_item->cdir_item.lcal_file.data_desc.crc_32 = 0;
    p_item->cdir_item.lcal_file.data_desc.compressed = 0;
    p_item->cdir_item.lcal_file.data_desc.uncompressed_size = 0;

    p_item->cdir_item.lcal_file.name_len = CAST_UNSIGNED_TO_UINT16(p_item->filename.namelength);
    p_item->cdir_item.lcal_file.extras_len = 0;

    p_item->cdir_item.comment_len = 0;
    p_item->cdir_item.start_disk_number = 0;
    p_item->cdir_item.internal_attributes = 0;
    p_item->cdir_item.external_attributes = 0;
    HqU32x2ToUint32(offset, &p_item->cdir_item.lcal_file_hdr_offset);

    if ( p_item->is_zip64 ) {
      /* Update local file header fields for ZIP64 */
      p_item->cdir_item.made_by_version = ZIP_VERSION_MADEBY(ZIP_COMPAT_MSDOS, ZIP_VERSION(4, 5));

      p_item->cdir_item.lcal_file.version_needed = ZIP_VERSION(4, 5);
      p_item->cdir_item.lcal_file.data_desc.compressed = ZIP_USE_ZIP64FLD_LONG;
      p_item->cdir_item.lcal_file.data_desc.uncompressed_size = ZIP_USE_ZIP64FLD_LONG;

      p_item->cdir_item.lcal_file.extras_len = ZAR_XTRAFLD_ZIP64SIZE;

      if ( HqU32x2CompareUint32(offset, MAXUINT32) >= 0 ) {
        /* Redirect file offset if it wont fit */
        p_item->cdir_item.lcal_file_hdr_offset = ZIP_USE_ZIP64FLD_LONG;
      }

      /* Initialise ZIP64 extra field record */
      p_item->z64_xtrafld.header.header_id = ZAR_EXTRA_ID_ZIP64;
      p_item->z64_xtrafld.header.data_size = ZAR_XTRAFLD_ZIP64SIZE - ZAR_XTRAFLD_HDRSIZE;

      HqU32x2FromUint32(&p_item->z64_xtrafld.uncompressed_size, 0);
      HqU32x2FromUint32(&p_item->z64_xtrafld.compressed, 0);
      p_item->z64_xtrafld.lcal_file_hdr_offset = *offset;
      p_item->z64_xtrafld.start_disk_number = 0;
    }
  }

  return(p_item);

} /* woz_create_item */


static
void woz_destroy_item(
  ZIP_ITEM* p_item)
{
  HQASSERT((p_item != NULL),
           "woz_destroy_item: NULL ZIP item pointer");

  VERIFY_OBJECT(p_item, ZIP_ITEM_NAME);
  UNNAME_OBJECT(p_item);

  mm_free(mm_pool_temp, p_item, ZIP_ITEM_SIZE(&p_item->filename));

} /* woz_destroy_item */


/* Allocate and initialise ZIP item hash table */
static
ZIP_ITEMS* woz_items_create(void)
{
  int32       i;
  ZIP_ITEMS*  zip_items;

  zip_items = mm_alloc(mm_pool_temp, sizeof(ZIP_ITEMS), MM_ALLOC_CLASS_WO_ZIP);
  if ( zip_items != NULL ) {
    for ( i = 0; i < WOZ_FILENAME_TABLE_SIZE; i++ ) {
      DLL_RESET_LIST(&zip_items->items[i]);
    }
  }
  NAME_OBJECT(zip_items, ZIP_ITEMS_NAME);

  return(zip_items);

} /* woz_items_create */


/* Empty ZIP item hash table and then free it */
static
void woz_items_destroy(
  ZIP_ITEMS*  zip_items)
{
  int32     i;
  ZIP_ITEM* p_item;

  HQASSERT((zip_items != NULL),
           "woz_items_destroy: NULL pointer to ZIP items hash table");

  VERIFY_OBJECT(zip_items, ZIP_ITEMS_NAME);

  for ( i = 0; i < WOZ_FILENAME_TABLE_SIZE; i++ ) {
    while ( (p_item = DLL_GET_HEAD(&zip_items->items[i], ZIP_ITEM, link)) != NULL ) {
      DLL_REMOVE(p_item, link);
      woz_destroy_item(p_item);
    }
    HQASSERT((DLL_LIST_IS_EMPTY(&zip_items->items[i])),
             "zip_items_destroy: zip items being leaked.");
  }

  UNNAME_OBJECT(zip_items);
  mm_free(mm_pool_temp, zip_items, sizeof(ZIP_ITEMS));

} /* woz_items_destroy */



/* Add buffer of data to the internal buffer. */
static
Bool woz_write_buffer(
  WriteonlyZip* self,
  uint8*        buffer,
  int32         length)
{
  int32 bytes;

  HQASSERT((self != NULL),
           "woz_write_buffer: NULL self");
  HQASSERT((buffer != NULL),
           "woz_write_buffer: NULL buffer pointer");
  HQASSERT((length > 0),
           "woz_write_buffer: invalid buffer length");

  /** \todo need to catch wrap around? */
  /* Update total data written to archive (assumes success) */
  HqU32x2AddUint32(&self->buff_total, &self->buff_total, (uint32)length);

  /* Standard code to fill buffer with source data, writing it out when it gets
   * full. */
  do {
    bytes = min(length, self->buff_avail);
    HqMemCpy(self->buff_next, buffer, bytes);

    buffer += bytes;
    length -= bytes;
    self->buff_next += bytes;
    self->buff_avail -= bytes;

    if ( self->buff_avail == 0 ) {
      if ( zar_write_raw(&self->archive, self->buffer, self->buff_size) != self->buff_size ) {
        return(FALSE);
      }

      self->buff_avail = self->buff_size;
      self->buff_next = self->buffer;
    }
  } while ( length > 0 );

  return(TRUE);

} /* woz_write_buffer */

/* Write out compressed file data. */
static
Bool woz_write_filedata(
  WriteonlyZip* woz,
  ZIP_ITEM*     p_item,
  z_streamp     zlibState)
{
  HQASSERT((woz != NULL),
           "woz_write_filedata: NULL WriteonlyZip pointer");
  HQASSERT((p_item != NULL),
           "woz_write_filedata: NULL archive item pointer");
  HQASSERT((zlibState != NULL),
           "woz_write_filedata: NULL zlib state pointer");

  /* Update the compressed size with remainder of buffer filled in. */
  HqU32x2AddUint32(&p_item->compressed, &p_item->compressed, woz->buff_avail);

  /* Write the compressed data to the archive. */
  if ( zar_write_raw(&woz->archive, woz->buffer, woz->buff_size) != woz->buff_size ) {
    return(FALSE);
  }

  /* Reset zlib output buffer */
  zlibState->next_out = woz->buffer;
  woz->buff_avail = zlibState->avail_out = woz->buff_size;

  return(TRUE);

} /* woz_write_filedata */

static
Bool woz_write_datadesc(
  WriteonlyZip*  woz)
{
  int           zlibResult;
  z_streamp     zlibState;
  ZIP_ITEM*     p_item;
  union {
    uint8       datadesc[ZAR_DATADESC_RECSIZE];
    uint8       z64_datadesc[ZAR_ZIP64_DATADESC_RECSIZE];
  } buffer;
  ZIP_ZIP64_DATA_DESC z64_data_desc;

  HQASSERT((woz != NULL),
           "woz_write_datadesc: NULL device pointer");

  /* If the file was not empty, write any remaining data to the archive. */
  p_item = woz->p_last_item;

  zlibState = &woz->zlibState;
  zlibState->next_out = woz->buff_next;
  zlibState->avail_out = woz->buff_avail;

  /* If there was no data written for this entry, we still need to write the
   * flate stream end-of-data marker. */
  woz->zlibState.next_in = NULL;
  woz->zlibState.avail_in = 0;
  do {
    zlibResult = deflate(&woz->zlibState, Z_FINISH);
    if ( (zlibResult != Z_OK) && (zlibResult != Z_STREAM_END) ) {
      WOZ_SET_FLAGS(woz, WOZ_ARCHIVE_ERROR);
      return(FALSE);
    }

    if ( zlibState->avail_out == 0 ) {
      /* Filled buffer, write to disc */
      if ( !woz_write_filedata(woz, p_item, zlibState) ) {
        WOZ_SET_FLAGS(woz, WOZ_ARCHIVE_ERROR);
        return(FALSE);
      }
    }

  } while ( zlibResult != Z_STREAM_END );

  HQASSERT((woz->buff_avail >= (int32)zlibState->avail_out),
           "woz_write_datadesc: zlib used more space than available");

  /* Finish updating compressed size with reduction in available buffer */
  HqU32x2AddUint32(&p_item->compressed, &p_item->compressed, (woz->buff_avail - zlibState->avail_out));

  /** \todo want to catch wrap around? */
  /* Update total data written to archive so far */
  HqU32x2Add(&woz->buff_total, &woz->buff_total, &p_item->compressed);

  woz->buff_avail = zlibState->avail_out;
  woz->buff_next = zlibState->next_out;

  /* Add data descriptor of required type */
  if ( p_item->is_zip64 ) {
    z64_data_desc.crc_32 = p_item->crc_32;
    z64_data_desc.compressed = p_item->compressed;
    z64_data_desc.uncompressed_size = p_item->uncompressed;

    zar_create_zip64_data_desc(buffer.z64_datadesc, &z64_data_desc);
    if ( !woz_write_buffer(woz, buffer.z64_datadesc, ZAR_ZIP64_DATADESC_RECSIZE) ) {
      WOZ_SET_FLAGS(woz, WOZ_ARCHIVE_ERROR);
      return(FALSE);
    }

  } else { /* Update local file header data descriptor */
    p_item->cdir_item.lcal_file.data_desc.crc_32 = p_item->crc_32;
    /* Check compressed or uncompressed size is not > 4GB */
    if ( !HqU32x2ToUint32(&p_item->compressed, &p_item->cdir_item.lcal_file.data_desc.compressed) ||
         !HqU32x2ToUint32(&p_item->uncompressed, &p_item->cdir_item.lcal_file.data_desc.uncompressed_size) ) {
      WOZ_SET_FLAGS(woz, WOZ_ARCHIVE_ERROR);
      return(FALSE);
    }

    zar_create_data_desc(buffer.datadesc, &p_item->cdir_item.lcal_file.data_desc);
    if ( !woz_write_buffer(woz, buffer.datadesc, ZAR_DATADESC_RECSIZE) ) {
      WOZ_SET_FLAGS(woz, WOZ_ARCHIVE_ERROR);
      return(FALSE);
    }
  }

  return(TRUE);

} /* woz_write_datadesc */


static
Bool woz_setup(
  WriteonlyZip* woz,
  int32         buffer_size)
{
  HQASSERT((woz != NULL),
           "woz_archive_create: NULL woz pointer");
  HQASSERT((buffer_size > 0),
           "woz_archive_create: invalid buffer size");

  woz->buff_size = buffer_size;
  woz->buffer = mm_alloc(mm_pool_temp, woz->buff_size, MM_ALLOC_CLASS_WO_ZIP);
  if ( woz->buffer == NULL ) {
    return(FALSE);
  }
  woz->buff_next = woz->buffer;
  woz->buff_avail = woz->buff_size;
  HqU32x2FromUint32(&woz->buff_total, 0);

  HQASSERT((woz->items == NULL),
           "woz_archive_create: WOZ items hash table already exists.");
  if ( (woz->items = woz_items_create()) == NULL ) {
    return(FALSE);
  }

  return(TRUE);

} /* woz_setup */

static
void woz_free(
  WriteonlyZip* woz)
{
  HQASSERT((woz != NULL),
           "woz_free: NULL woz pointer");
  HQASSERT((!woz_is_archive_open(woz)),
           "woz_free: freeing up open archive");

  /* Free off output buffer */
  if ( woz->buffer != NULL ) {
    mm_free(mm_pool_temp, woz->buffer, woz->buff_size);
    woz->buffer = NULL;
  }

  /* Free off items hash table */
  if ( woz->items != NULL ) {
    woz_items_destroy(woz->items);
    woz->items = NULL;
  }
  woz->p_first_item = woz->p_last_item = NULL;

  /* Shutdown zlib if we actually started it. */
  if ( woz->zlibState.zalloc == zutl_zlib_alloc ) {
    int32 status;

    status = deflateEnd(&woz->zlibState);
#ifdef DEBUG_BUILD
    /* Failure is not really a problem, resources are still freed.  The
     * additional error codes indicate zlib was in the middle of a compression
     * when it was ended, which it shouldn't be unless there was an error
     * creating the archive. */
    switch ( status ) {
    case Z_STREAM_ERROR:
    case Z_DATA_ERROR:
      HQASSERT((woz_is_archive_error(woz)),
               "woz_free: in the middle of compression, please report");
      break;

    default:
      HQFAIL("woz_free: unexpected status from deflateEnd, please report");
      /* FALLTHROUGH */
    case Z_OK:
      break;
    }
#endif /* DEBUG_BUILD */
    woz->zlibState.zalloc = Z_NULL;
    woz->zlibState.zfree = Z_NULL;
  }

  /* Reset the ZIP archive */
  zar_init(&woz->archive);

  /* Set file to closed - do this after shutting zlib down since the debug
   * trace above depends on the file open flag to catch possible bug conditions. */
  WOZ_CLEAR_FLAGS(woz, WOZ_FILE_OPEN);

} /* woz_free */


/** Create the archive; returns FALSE on error.
*/
static
Bool createArchive(
  WriteonlyZip* self,
  uint8*        archivename,
  int32         namelen)
{
  HQASSERT((self != NULL),
           "createArchive: NULL woz pointer");
  HQASSERT((archivename != NULL),
           "createArchive: NULL archive name pointer");
  HQASSERT((namelen > 0),
           "createArchive: invalid archive name length");

  if ( !zar_open_create(&self->archive, archivename, namelen) ) {
    return(FALSE);
  }

  WOZ_SET_FLAGS(self, WOZ_ARCHIVE_OPEN);
  return TRUE;
}

/** Write the archive central directory; returns FALSE on error.
*/
static
Bool woz_write_cdir(
  WriteonlyZip* self)
{
  ZIP_ITEM* p_item;
  union {
    uint8   cdirfile[ZAR_CDIRFILE_RECSIZE];
    uint8   z64_xtrafld[ZAR_XTRAFLD_ZIP64SIZE];
    uint8   z64_endcdir[ZAR_ZIP64_ENDCDIR_RECSIZE];
    uint8   z64_locator[ZAR_ZIP64_CDIRLOCATOR_RECSIZE];
    uint8   endcdir[ZAR_ENDCDIR_RECSIZE];
  } buffer;
  ZIP_END_CDIR end_cdir;
  ZIP_ZIP64_END_CDIR z64_end_cdir;
  ZIP_ZIP64_CDIR_LOC z64_cdir_loc;
  uint32    c_items;
  HqU32x2   cdir_size;
  HqU32x2   cdir_offset;
  HqU32x2   z64_end_cdir_offset;

  HQASSERT((self != NULL),
           "woz_write_cdir: NULL pointer to write only ZIP device");
  HQASSERT((!woz_is_archive_error(self)),
           "woz_write_cdir: writing cdir after generation error");
  HQASSERT((!woz_is_file_open(self)),
           "woz_write_cdir: writing cdir with file open");

  /* Remember the start of the central directory */
  cdir_offset = self->buff_total;

  /* Write the central directory records for each item */
  p_item = self->p_first_item;
  c_items = 0;
  while ( p_item != NULL ) {
    /* Add cdir file header and filename */
    zar_create_cdir_file(buffer.cdirfile, &p_item->cdir_item);
    if ( !woz_write_buffer(self, buffer.cdirfile, ZAR_CDIRFILE_RECSIZE) ||
         !woz_write_buffer(self, p_item->filename.name, p_item->cdir_item.lcal_file.name_len) ) {
      return(FALSE);
    }
    if ( p_item->is_zip64 ) {
      /* Update and add ZIP64 extra field */
      p_item->z64_xtrafld.uncompressed_size = p_item->uncompressed;
      p_item->z64_xtrafld.compressed = p_item->compressed;
      zar_create_xtrafld_zip64(buffer.z64_xtrafld, &p_item->z64_xtrafld);
      if ( !woz_write_buffer(self, buffer.z64_xtrafld, ZAR_XTRAFLD_ZIP64SIZE) ) {
        return(FALSE);
      }
    }
    if ( c_items == MAXUINT32 ) {
      /* Maxed out our item count, wow, ~10^9 files! */
      return(FALSE);
    }
    c_items += 1;
    p_item = p_item->p_next;
  }

  /* Calculate size of central directory */
  HqU32x2Subtract(&cdir_size, &self->buff_total, &cdir_offset);

  /* Check if we need to create ZIP64 central directory records */
  if ( (c_items > MAXUINT16) ||
       (HqU32x2CompareUint32(&cdir_offset, MAXUINT32) > 0) ||
       (HqU32x2CompareUint32(&cdir_size, MAXUINT32) > 0) ) {
    z64_end_cdir_offset = self->buff_total;

    /* Create a ZIP64 end of central directory */
    HqU32x2FromUint32(&z64_end_cdir.end_cdir_size, ZAR_ZIP64_ENDCDIR_RECSIZE);
    z64_end_cdir.made_by_version = ZIP_VERSION_MADEBY(ZIP_COMPAT_MSDOS, ZIP_VERSION(4, 5));
    z64_end_cdir.version_needed = ZIP_VERSION(4, 5);
    z64_end_cdir.this_disk_number = 0;
    z64_end_cdir.start_disk_number = 0;
    HqU32x2FromUint32(&z64_end_cdir.total_cdir_entries_this_disk, c_items);
    z64_end_cdir.total_cdir_entries = z64_end_cdir.total_cdir_entries_this_disk;
    z64_end_cdir.cdir_size = cdir_size;
    z64_end_cdir.cdir_start_offset = cdir_offset;

    zar_create_zip64_end_cdir(buffer.z64_endcdir, &z64_end_cdir);
    if ( !woz_write_buffer(self, buffer.z64_endcdir, ZAR_ZIP64_ENDCDIR_RECSIZE) ) {
      return(FALSE);
    }

    /* Create a ZIP64 end of central directory locator */
    z64_cdir_loc.start_disk_number = 0;
    z64_cdir_loc.zip64_end_cdir_offset = z64_end_cdir_offset;
    z64_cdir_loc.total_disks = 1;

    zar_create_zip64_cdir_locator(buffer.z64_locator, &z64_cdir_loc);
    if ( !woz_write_buffer(self, buffer.z64_locator, ZAR_ZIP64_CDIRLOCATOR_RECSIZE) ) {
      return(FALSE);
    }

    /* Flag end cdir record fields as being set in ZIP64 end cdir record */
    end_cdir.cdir_entries_total = end_cdir.cdir_entries_thisdisk = ZIP_USE_ZIP64FLD_SHORT;
    end_cdir.cdir_offset = ZIP_USE_ZIP64FLD_LONG;
    end_cdir.cdir_size = ZIP_USE_ZIP64FLD_LONG;

  } else { /* Plain ol' ZIP archive */
    end_cdir.cdir_entries_thisdisk = end_cdir.cdir_entries_total = CAST_UNSIGNED_TO_UINT16(c_items);

    /* Record offset and size of the central directory. */
    HqU32x2ToUint32(&cdir_size, &end_cdir.cdir_size);
    HqU32x2ToUint32(&cdir_offset, &end_cdir.cdir_offset);
  }

  end_cdir.disknum = 0;
  end_cdir.cdir_start_disknum = 0;
  end_cdir.comment_length = 0;

  /* Write the end of central directory record, and close the archive */
  if ( !zar_create_end_cdir(buffer.endcdir, &end_cdir) ||
       !woz_write_buffer(self, buffer.endcdir, ZAR_ENDCDIR_RECSIZE) ) {
    return(FALSE);
  }

  /* Flush remaining buffered archive content */
  if ( self->buff_avail < self->buff_size ) {
    /* Write any final part of buffer */
    int32 bytes = self->buff_size - self->buff_avail;

    if ( zar_write_raw(&self->archive, self->buffer, bytes) != bytes ) {
      return(FALSE);
    }
    /* Update total bytes written to archive */
    HqU32x2AddUint32(&self->buff_total, &self->buff_total, bytes);
  }

  return(TRUE);
}


static
Bool closeArchive(
  WriteonlyZip* self)
{
  HQASSERT((self != NULL),
           "closeArchive: NULL woz pointer");
  HQASSERT((woz_is_archive_open(self)),
           "closeArchive: closing an archive without one being open");

  WOZ_CLEAR_FLAGS(self, WOZ_ARCHIVE_OPEN);

  /* Remove any closefile hook */
  if ( self->archive.flptr != NULL ) {
    theIMyCloseFile(self->archive.flptr) = self->old_close;
  }

  /* Close the archive file */
  return(zar_close(&self->archive, CLOSE_EXPLICIT) == 0);
}


static
int32 woz_closefile(
  FILELIST*   flptr,
  int32       flag)
{
  WriteonlyZip* p_woz;

  /* Find device with matching archive file pointer to close it */
  p_woz = DLL_GET_HEAD(&wozInstances, WriteonlyZip, link);
  while ( (p_woz != NULL) && (p_woz->archive.flptr != flptr) ) {
    p_woz = DLL_GET_NEXT(p_woz, WriteonlyZip, link);
  }

  /* Close file even if it isn't for a device */
  if ( p_woz == NULL ) {
    HQFAIL("woz_closefile: tried to close unknown archive file");
    return(theIMyCloseFile(flptr)(flptr, flag));
  }

  /* If an archive is still open then record an error */
  if ( woz_is_archive_open(p_woz) ) {
    WOZ_SET_FLAGS(p_woz, WOZ_ARCHIVE_ERROR);
  }

  return(closeArchive(p_woz) ? 0 : -1);

} /* woz_closefile */


/** Set the 'lastError' field to 'error'.

\return The passed 'retVal'.
*/
static
int32 setLastError(
  WriteonlyZip* self,
  int32         error,
  int32         retVal)
{
  UNUSED_PARAM(WriteonlyZip*, self);

  devices_set_last_error(error);
  return retVal;
}

/** Device instance constructor; returns 0 is the device was initialised
successfully; returns -1 on error.
*/
static
int32 RIPCALL wozInit(
  DEVICELIST* dev)
{
  WriteonlyZip* self;

  HQASSERT((dev != NULL),
           "wozInit: NULL device pointer");

  self = (WriteonlyZip*)dev->private_data;
  NAME_OBJECT(self, WRITEONLY_ZIP_NAME);

  /* Add instance to the global list. */
  DLL_RESET_LINK(self, link);
  DLL_ADD_TAIL(&wozInstances, self, link);

  /* State flags - not open, no error, no file open */
  WOZ_CLEAR_FLAGS(self, WOZ_ARCHIVE_OPEN|WOZ_ARCHIVE_ERROR|WOZ_FILE_OPEN);

  /* Initialise the zlib state object ... */
  self->zlibState.zalloc = Z_NULL;
  self->zlibState.zfree = Z_NULL;
  self->zlibState.opaque = Z_NULL;
  self->zlibState.next_in = Z_NULL;
  self->zlibState.avail_in = 0;
  self->zlibState.next_out = Z_NULL;
  self->zlibState.avail_out = 0;

  /* ... ZIP archive ... */
  zar_init(&self->archive);

  /* ... item hash table and list ... */
  self->items = NULL;
  self->p_first_item = NULL;
  self->p_last_item = NULL;

  /* ... internal buffer ... */
  self->buffer = NULL;
  self->buff_next = 0;
  self->buff_avail = 0;
  self->buff_size = 0;
  HqU32x2FromUint32(&self->buff_total, 0);

  /* ... error ... */
  (void)setLastError(self, DeviceNoError, 0);

  /* ... parameter iterator ... */
  self->next_param = 0;

  /* ... and parameters */
  HqMemCpy(self->params, defaultParams, sizeof(defaultParams));
  /* Set initial buffer size */
  self->params[WOZ_BUFFERSIZE].paramval.intval = WOZ_DEFAULT_BUFSIZE;
  /* Type parameter is constant for all instances */
  self->params[WOZ_TYPE].paramval.strval = type;
  self->params[WOZ_TYPE].strvallen = sizeof(type) - 1;

  return 0;
}

/** Open a file on the device. Files may only be opened for writing, and only
one file may be open at any one time.

\return The file handle, or -1 on error.
*/
static
DEVICE_FILEDESCRIPTOR RIPCALL wozOpenFile(
  DEVICELIST* dev,
  uint8*      filename,
  int32       openflags)
{
  WriteonlyZip* self;
  ZIP_ITEM* p_item;
  Bool      is_zip64;
  FILEENTRY fe_filename;
  union {
    uint8   lcalfile[ZAR_LCALFILE_RECSIZE];
    uint8   z64_xtrafld[ZAR_XTRAFLD_ZIP64SIZE];
  } buffer;
  uint8     fn_buffer[LONGESTFILENAME];

  HQASSERT((filename != NULL),
           "wozOpenFile: NULL file name pointer");

  GET_SELF(dev, self);

  /* Can't open a file after an error generating the archive */
  if ( woz_is_archive_open(self) && woz_is_archive_error(self) ) {
    return(setLastError(self, DeviceIOError, -1));
  }

  /* Normalise the file name */
  if ( !normalise_filename(&fe_filename, fn_buffer, filename) ) {
    return(setLastError(self, DeviceUndefined, -1));
  }

  /* See if the file name is known to the device - behaviour then depends on
   * openflags. */
  p_item = woz_find_item(self->items, &fe_filename);

  switch ( openflags&(SW_RDONLY|SW_WRONLY|SW_RDWR) ) {
  case SW_RDWR:
  case SW_RDONLY:
    /* Catch invalid read only flags - or opening a known file for reading
     * (we're a write only device) */
    if ( (openflags & (SW_APPEND|SW_CREAT|SW_TRUNC)) ||
         (p_item != NULL) ) {
      return(setLastError(self, DeviceInvalidAccess, -1));
    }
    /* Just report we don't know the named file */
    return(setLastError(self, DeviceUndefined, -1));

  case SW_WRONLY:
    /* Cannot write to a known file - we're a write-once only device */
    if ( p_item != NULL ) {
      return(setLastError(self, DeviceIOError, -1));
    }
    /* Report unknown file if file is not known and not creating */
    if ( (openflags & SW_CREAT) == 0 ) {
      return(setLastError(self, DeviceUndefined, -1));
    }
    /* Cannot open a new file if one is currently open */
    if ( !woz_is_archive_open(self) || woz_is_file_open(self) ) {
      return(setLastError(self, DeviceIOError, -1));
    }
    /* Don't care about remaining flags as we are creating the file */
    break;

  default: /* Unknown open flags */
    HQFAIL("wozOpenFile: invalid openflags, please report");
    return(setLastError(self, DeviceInvalidAccess, -1));
  }

  /* Set up zlib for next file - initialise first time in otherwise reset */
  if ( self->zlibState.zalloc == Z_NULL ) {
    self->zlibState.zalloc = zutl_zlib_alloc;
    self->zlibState.zfree = zutl_zlib_free;
    if ( deflateInit2(&self->zlibState, Z_BEST_SPEED, Z_DEFLATED, -MAX_WBITS, 8,
                      Z_DEFAULT_STRATEGY) != Z_OK ) {
      WOZ_SET_FLAGS(self, WOZ_ARCHIVE_ERROR);
      return setLastError(self, DeviceIOError, -1);
    }

  } else if ( deflateReset(&self->zlibState) != Z_OK ) {
    WOZ_SET_FLAGS(self, WOZ_ARCHIVE_ERROR);
    return setLastError(self, DeviceIOError, -1);
  }

  /* Create ZIP item for new file and add to the hashtable and creation list.
   * Items are added to the end of the creation list so the central directory is
   * in the same order as the files - for no particular reason.
   * Add ZIP64 extra field if forced or archive > 4GB. */
  is_zip64 = woz_create_zip64_files(self) ||
              (HqU32x2CompareUint32(&self->buff_total, MAXUINT32) > 0);
  if ( (p_item = woz_create_item(&fe_filename, is_zip64, &self->buff_total)) == NULL ) {
    WOZ_SET_FLAGS(self, WOZ_ARCHIVE_ERROR);
    return(setLastError(self, DeviceVMError, -1));
  }
  woz_add_item(self->items, p_item);
  if ( self->p_last_item != NULL ) {
    self->p_last_item->p_next = p_item;
  } else {
    self->p_first_item = p_item;
  }
  self->p_last_item = p_item;

  /* Write the local file header to the archive */
  zar_create_lcal_file(buffer.lcalfile, &p_item->cdir_item.lcal_file);
  if ( !woz_write_buffer(self, buffer.lcalfile, ZAR_LCALFILE_RECSIZE) ) {
    WOZ_SET_FLAGS(self, WOZ_ARCHIVE_ERROR);
    return(setLastError(self, DeviceIOError, -1));
  }
  /* Add filename */
  if ( !woz_write_buffer(self, p_item->filename.name, p_item->filename.namelength) ) {
    WOZ_SET_FLAGS(self, WOZ_ARCHIVE_ERROR);
    return(setLastError(self, DeviceIOError, -1));
  }
  if ( p_item->is_zip64 ) {
    /* Add ZIP64 extra field */
    zar_create_xtrafld_zip64(buffer.z64_xtrafld, &p_item->z64_xtrafld);
    if ( !woz_write_buffer(self, buffer.z64_xtrafld, ZAR_XTRAFLD_ZIP64SIZE) ) {
      WOZ_SET_FLAGS(self, WOZ_ARCHIVE_ERROR);
      return(setLastError(self, DeviceIOError, -1));
    }
  }

  /* Track that a file is now open */
  WOZ_SET_FLAGS(self, WOZ_FILE_OPEN);

  return(WOZ_FD);
}

/** Read from open file; not allowed, always returns -1.
*/
static
int32 RIPCALL wozReadFile(
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR fd,
  uint8*      data,
  int32       length)
{
  WriteonlyZip* self;

  GET_SELF(dev, self);

  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, fd);
  UNUSED_PARAM(uint8*, data);
  UNUSED_PARAM(int32, length);

  return setLastError(self, DeviceIOError, -1);
}

/** Write data to the archive.
*/
static
int32 RIPCALL wozWriteFile(
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR fd,
  uint8*      data,
  int32       length)
{
  WriteonlyZip* self;
  z_streamp zlibState;
  ZIP_ITEM* p_item;

  HQASSERT((data != NULL),
           "wozWriteFile: NULL data pointer");
  HQASSERT((length >= 0),
           "wozWriteFile: invalid data length");

  GET_SELF(dev, self);

  if ( !woz_is_file_open(self) || fd != WOZ_FD || woz_is_archive_error(self) ) {
    return setLastError(self, DeviceIOError, -1);
  }

  p_item = self->p_last_item;

  /* Setup zlib deflate buffers */
  zlibState = &self->zlibState;
  zlibState->next_in = data;
  zlibState->avail_in = length;

  zlibState->next_out = self->buff_next;
  zlibState->avail_out = self->buff_avail;

  /* Compress the data and write it to the archive. */
  do {
    if ( deflate(zlibState, Z_NO_FLUSH) != Z_OK ) {
      WOZ_SET_FLAGS(self, WOZ_ARCHIVE_ERROR);
      return setLastError(self, DeviceIOError, -1);
    }

    if ( zlibState->avail_out == 0 ) {
      /* Filled buffer, write to disc */
      if ( !woz_write_filedata(self, p_item, zlibState) ) {
        WOZ_SET_FLAGS(self, WOZ_ARCHIVE_ERROR);
        return(setLastError(self, DeviceIOError, -1));
      }
    }

  } while ( zlibState->avail_in > 0 );

  HQASSERT((self->buff_avail >= (int32)zlibState->avail_out),
           "wozWriteFile: zlib used more space than available");

  /* Update compressed size with reduction in available buffer */
  HqU32x2AddUint32(&p_item->compressed, &p_item->compressed, (self->buff_avail - zlibState->avail_out));

  self->buff_avail = zlibState->avail_out;
  self->buff_next = zlibState->next_out;

  /* Update uncompressed size for data compressed */
  HqU32x2AddUint32(&p_item->uncompressed, &p_item->uncompressed, length);

  /* Update the data checksum */
  p_item->crc_32 = crc32(p_item->crc_32, data, length);

  return length;
}

/** Close the current file.
*/
static
int32 RIPCALL wozCloseFile(
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR fd)
{
  WriteonlyZip* self;

  GET_SELF(dev, self);

  HQASSERT((woz_is_archive_open(self) || woz_is_archive_error(self)),
           "wozCloseFile: closing open file when not creating an archive");

  if ( !woz_is_file_open(self) || (fd != WOZ_FD) || woz_is_archive_error(self) ) {
    return(setLastError(self, DeviceIOError, -1));
  }

  /* Write remaining item data and the data descriptor */
  if ( !woz_write_datadesc(self) ) {
    return(setLastError(self, DeviceIOError, -1));
  }

  /* Flag file is closed */
  WOZ_CLEAR_FLAGS(self, WOZ_FILE_OPEN);

  return(0);
}

/** Abort the current file.
*/
static
int32 RIPCALL wozAbortFile(
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR fd)
{
  WriteonlyZip* self;

  GET_SELF(dev, self);

  HQASSERT((woz_is_archive_open(self)),
           "wozAbortFile: aborting open file when not creating an archive");

  if ( !woz_is_file_open(self) || (fd != WOZ_FD) || woz_is_archive_error(self) ) {
    return(setLastError(self, DeviceIOError, -1));
  }

  /* Don't bother writing any data descriptor - flag file is closed */
  WOZ_CLEAR_FLAGS(self, WOZ_FILE_OPEN);

  /* Therefore archive is no longer valid */
  WOZ_SET_FLAGS(self, WOZ_ARCHIVE_ERROR);

  return(0);
}

/** File seek.
*/
static
Bool RIPCALL wozSeekFile(
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR fd,
  Hq32x2*     destn,
  int32       flags)
{
  WriteonlyZip* self;

  HQASSERT((destn != NULL),
           "wozSeekFile: NULL pointer to returned file position");

  GET_SELF(dev, self);

  if ( woz_is_archive_open(self) && (fd == WOZ_FD) && !woz_is_archive_error(self) &&
       Hq32x2IsZero(destn) ) {
    switch ( flags ) {
    case SW_SET: /* The device does not support seeks, return error */
      break;

    case SW_XTND: /* Effectively a no-op so allow.  Used for files opened with (a) */
      /* FALLTHROUGH */
    case SW_INCR: /* Report amount of file data sent to device */
      Hq32x2FromU32x2(destn, &self->p_last_item->uncompressed);
      return(TRUE);

    default:
      HQFAIL("wozSeekFile: invalid seek flags, please report");
      break;
    }
  }

  return(setLastError(self, DeviceIOError, FALSE));
}

/** Get bytes available for reading.
*/
static
Bool RIPCALL wozBytesFile(
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR fd,
  Hq32x2*     bytes,
  int32       reason)
{
  WriteonlyZip* self;

  HQASSERT((bytes != NULL),
           "wozBytesFile: NULL pointer to returned bytes");

  GET_SELF(dev, self);

  if ( woz_is_archive_open(self) && (fd == WOZ_FD) && !woz_is_archive_error(self) &&
       (reason == SW_BYTES_TOTAL_ABS) ) {
    /* Return amount of data so far */
    Hq32x2FromU32x2(bytes, &self->p_last_item->uncompressed);
    return(TRUE);
  }

  /* write only device so never any bytes available to be read */
  HQASSERT((reason == SW_BYTES_AVAIL_REL),
           "wozBytesFile: unknown reason flag");
  return setLastError(self, DeviceIOError, FALSE);
}

/** Get file information - not supported.
*/
static
int32 RIPCALL wozFileStatus(
  DEVICELIST* dev,
  uint8*      filename,
  STAT*       statbuff)
{
  WriteonlyZip* self;
  ZIP_ITEM*     p_item;

  HQASSERT((filename != NULL),
           "wozFileStatus: NULL file name pointer");
  HQASSERT((statbuff != NULL),
           "wozFileStatus: NULL stat pointer");

  GET_SELF(dev, self);

  /* See if we know the file */
  if ( (p_item = woz_lookup_filename(self->items, filename)) == NULL ) {
    return(setLastError(self, DeviceUndefined, -1));
  }

  /* Generate status information */
  Hq32x2FromU32x2(&statbuff->bytes, &p_item->uncompressed);
  statbuff->referenced = statbuff->modified =
    statbuff->created = (p_item->cdir_item.lcal_file.mod_date << 16) | p_item->cdir_item.lcal_file.mod_time;

  return(0);
}

/** Start listing files on device.
*/
static
void* RIPCALL wozStartFileListing(
  DEVICELIST* dev,
  uint8*      pattern)
{
  WriteonlyZip* self;

  UNUSED_PARAM(uint8*, pattern);

  GET_SELF(dev, self);

  /* RIP checks device last error when NULL returned - DeviceNoError means not
   * an error, i.e. there are no files on the device.
   */
  setLastError(self, DeviceNoError, 0);
  return((void*)self->p_first_item);
}

/** Get next file on device - not supported.
*/
static
int32 RIPCALL wozNextFile(
  DEVICELIST* dev,
  void**      handle,
  uint8*      pattern,
  FILEENTRY*  entry)
{
  int32         pattern_len;
  WriteonlyZip* self;
  ZIP_ITEM*     p_item;
  ZIP_ITEM**    p_iter;

  HQASSERT((handle != NULL),
           "wozNextFile: NULL iteration handle pointer");
  HQASSERT((pattern != NULL),
           "wozNextFile: NULL pattern pointer");
  HQASSERT((entry != NULL),
           "wozNextFile: NULL returned file name entry pointer");

  GET_SELF(dev, self);

  /* Ignore any leading slash as the device ignores them when adding files */
  if ( *pattern == '/' ) {
    pattern++;
  }
  pattern_len = strlen_int32((char*)pattern);

  p_iter = (ZIP_ITEM**)handle;
  for (;;) {
    p_item = *p_iter;
    if ( p_item == NULL ) {
      return(FileNameNoMatch);
    }
    *p_iter = p_item->p_next;
    if ( SwLengthPatternMatch(pattern, pattern_len, p_item->filename.name, p_item->filename.namelength) ) {
      *entry = p_item->filename;
      return(FileNameMatch);
    }
  }

  /* NEVER REACHED */
}

/** Stop listing files.
*/
static
int32 RIPCALL wozEndFileListing(
  DEVICELIST* dev,
  void*       handle)
{
  UNUSED_PARAM(DEVICELIST*, dev);
  UNUSED_PARAM(void*, handle);

  return 0;
}

/** Rename the specified files - not supported.
*/
static
int32 RIPCALL wozRenameFile(
  DEVICELIST* dev,
  uint8*      file1,
  uint8*      file2)
{
  WriteonlyZip* self;
  ZIP_ITEM*     p_item;

  UNUSED_PARAM(uint8*, file2);

  HQASSERT((file1 != NULL),
           "wozRenameFile: NULL original file name pointer");

  GET_SELF(dev, self);

  /* See if we know the file */
  if ( (p_item = woz_lookup_filename(self->items, file1)) == NULL ) {
    return(setLastError(self, DeviceUndefined, -1));
  }

  /* Cannot rename files on this device */
  return(setLastError(self, DeviceInvalidAccess, -1));
}

/** Delete the specified file - not supported on this device.
*/
static
int32 RIPCALL wozDeleteFile(
  DEVICELIST* dev,
  uint8*      filename)
{
  WriteonlyZip* self;
  ZIP_ITEM*     p_item;

  HQASSERT((filename != NULL),
           "wozDeleteFile: NULL file name pointer");

  GET_SELF(dev, self);

  /* See if we know the file */
  if ( (p_item = woz_lookup_filename(self->items, filename)) == NULL ) {
    return(setLastError(self, DeviceUndefined, -1));
  }

  /* Cannot delete the file from this device */
  return(setLastError(self, DeviceInvalidAccess, -1));
}

/** Set a parameter on this device. The device cannot be used until the
'ArchiveName' parameter has been set, as this completes the construction of the
device, and allows files to be written to it.

<p>Once the contents of the archive have been written, it must be closed by
setting the 'ArchiveName' parameter to a zero-length string.
*/
static
int32 RIPCALL wozSetParam(
  DEVICELIST*   dev,
  DEVICEPARAM*  param)
{
  int32 status;
  int32 result = ParamIgnored;
  WriteonlyZip* self;

  HQASSERT((param != NULL),
           "wozSetParam: NULL parameter pointer");

  GET_SELF(dev, self);

  if ( PARAM_NAME_TYPE_EQUAL(param, &self->params[WOZ_ARCHIVENAME]) ) {
    DEVICEPARAM* archiveName = &self->params[WOZ_ARCHIVENAME];

    if ( param->strvallen > 0 ) {
      /* Got a new archive name - expect to be opening a new archive */

      /* Only one archive at a time */
      if ( woz_is_archive_open(self) ) {
        return(setLastError(self, DeviceLimitCheck, ParamError));
      }
      /* Start a new archive - create the ZIP archive file */
      if ( !createArchive(self, param->paramval.strval, param->strvallen) ) {
        return(setLastError(self, DeviceIOError, ParamError));
      }

      /* Initialise device and remember archive name */
      if ( !woz_setup(self, self->params[WOZ_BUFFERSIZE].paramval.intval) ||
           ((archiveName->paramval.strval = mm_alloc(mm_pool_temp, param->strvallen,
                                                     MM_ALLOC_CLASS_WO_ZIP)) == NULL) ) {
        (void)closeArchive(self);
        woz_free(self);
        return(setLastError(self, DeviceVMError, ParamError));
      }
      HqMemCpy(archiveName->paramval.strval, param->paramval.strval, param->strvallen);
      archiveName->strvallen = param->strvallen;

      /* Clear archive creation error for new archive */
      WOZ_CLEAR_FLAGS(self, WOZ_ARCHIVE_ERROR);

      /* ZIP archive specified, catch the file being closed with our hook function */
      self->old_close = theIMyCloseFile(self->archive.flptr);
      theIMyCloseFile(self->archive.flptr) = woz_closefile;

      result = ParamAccepted;

    } else { /* Empty archive name - expect to be closing an archive */

      if ( woz_is_archive_open(self) ) {
        /* Abort any item file still open */
        if ( woz_is_file_open(self) ) {
          (void)wozAbortFile(dev, WOZ_FD);
        }

        /* Write central directory only if no errors */
        status = !woz_is_archive_error(self) && woz_write_cdir(self);

        /* Close the archive regardless of any errors */
        status = closeArchive(self) && status;

        /* Free off the cached archive name */
        mm_free(mm_pool_temp, archiveName->paramval.strval, archiveName->strvallen);
        archiveName->paramval.strval = NULL;
        archiveName->strvallen = 0;

        /* Free off internal state */
        woz_free(self);

        if ( !status ) {
          return(setLastError(self, DeviceIOError, ParamError));
        }

        result = ParamAccepted;

      } else { /* No archive currently open - do nothing. */
        result = ParamIgnored;
      }
    }

  } else if ( PARAM_NAME_TYPE_EQUAL(param, &self->params[WOZ_BUFFERSIZE]) ) {
    /* BufferSize */
    DEVICEPARAM* buffer_size = &self->params[WOZ_BUFFERSIZE];

    /* Can only set the buffer size if an archive is not open */
    if ( !woz_is_archive_open(self) ) {
      if ( param->paramval.intval > 0 ) {
        buffer_size->paramval.intval = param->paramval.intval;
        result = ParamAccepted;
      } else {
        result = ParamRangeCheck;
      }
    } else {
      result = ParamConfigError;
    }

  } else if ( PARAM_NAME_TYPE_EQUAL(param, &self->params[WOZ_TYPE]) ) {
    /* Type */
    result = ParamConfigError;

  } else if ( PARAM_NAME_TYPE_EQUAL(param, &self->params[WOZ_ZIP64FILES]) ) {
    /* ZIP64Files */

    /* Can only be set if an archive is not open */
    if ( !woz_is_archive_open(self) ) {
      if ( param->paramval.boolval ) {
        WOZ_SET_FLAGS(self, WOZ_ZIP64_FILES);
      }
      result = ParamAccepted;

    } else {
      result = ParamConfigError;
    }
  }

  return result;
}

/** Start a parameter enumeration.

\return The number of parameters to enumerate.
*/
static
int32 RIPCALL wozStartParam(
  DEVICELIST* dev)
{
  WriteonlyZip* self;

  GET_SELF(dev, self);

  self->next_param = 0;

  return TOTAL_DEVICE_PARAMS;
}

/** Parameter query method.
*/
static
int32 RIPCALL wozGetParam(
  DEVICELIST*   dev,
  DEVICEPARAM*  param)
{
  WriteonlyZip* self;

  HQASSERT((param != NULL),
           "wozGetParam: NULL parameter poiinter");

  GET_SELF(dev, self);

  if ( param->paramname == NULL ) {
    if ( self->next_param > TOTAL_DEVICE_PARAMS ) {
      return(0);
    }
    *param = self->params[self->next_param];
    self->next_param++;
  } else {
    if ( PARAM_NAME_TYPE_EQUAL(param, &self->params[WOZ_ARCHIVENAME]) ) {
      *param = self->params[WOZ_ARCHIVENAME];
    } else if ( PARAM_NAME_TYPE_EQUAL(param, &self->params[WOZ_BUFFERSIZE]) ) {
      *param = self->params[WOZ_BUFFERSIZE];
    } else if ( PARAM_NAME_TYPE_EQUAL(param, &self->params[WOZ_TYPE]) ) {
      *param = self->params[WOZ_TYPE];
    } else if ( PARAM_NAME_TYPE_EQUAL(param, &self->params[WOZ_ZIP64FILES]) ) {
      *param = self->params[WOZ_ZIP64FILES];
    } else {
      return(ParamIgnored);
    }
  }

  return ParamAccepted;
}


/** Fill-in the passed DEVSTAT structure.
*/
static
int32 RIPCALL wozDeviceStatus(
  DEVICELIST* dev,
  DEVSTAT*    status)
{
  WriteonlyZip* self;

  HQASSERT((status != NULL),
           "wozDeviceStatus: NULL device status pointer");

  GET_SELF(dev, self);

  if ( !woz_is_archive_open(self) ) {
    return(setLastError(self, DeviceInvalidAccess, -1));
  }

  return(zar_dev_status(&self->archive, status));
}


/** Device dismount;
*/
static
int32 RIPCALL wozDismount(
  DEVICELIST* dev)
{
  WriteonlyZip* self;

  GET_SELF(dev, self);

  /* The RIP should prevent the device being dismounted while a file being added
   * to the archive is still open. */
  HQASSERT((!woz_is_file_open(self)),
           "wozDismount: dismounting while a file is open");

  /* Close archive file if still open */
  if ( woz_is_archive_open(self) ) {
    (void)closeArchive(self);
  }

  /* Free off device resources */
  woz_free(self);

  /* Free any parameters that have been allocated. */
  if ( self->params[WOZ_ARCHIVENAME].paramval.strval != NULL ) {
    mm_free(mm_pool_temp, self->params[WOZ_ARCHIVENAME].paramval.strval,
            self->params[WOZ_ARCHIVENAME].strvallen);
  }

  /* Remove this instance from the global instance list. */
  DLL_REMOVE(self, link);

  UNNAME_OBJECT(self);

  return 0;
}

/** IO Control method; does nothing.
*/
static
int32 RIPCALL wozIOControl(
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR fd,
  int32       opcode,
  intptr_t    arg)
{
  WriteonlyZip* self;

  GET_SELF(dev, self);

  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, fd);
  UNUSED_PARAM(int32, opcode);
  UNUSED_PARAM(intptr_t, arg);

  return(-1);
}

/** \brief Public Write-only ZIP Device interface. This structure exports the
methods required by the Scriptworks device interface. */
static DEVICETYPE wozDeviceType = {
  WRITEONLY_ZIP_DEVICE_TYPE,       /* the device ID number */
  /* flags to indicate specifics of device */
  DEVICERELATIVE | DEVICEWRITABLE | DEVICENOSEARCH,
  sizeof(WriteonlyZip),            /* the size of the private_data */
  0,                               /* tickles not required */
  NULL,                            /* procedure to service the device */
  devices_last_error,              /* return last error for this device */
  wozInit,                         /* initialise device */
  wozOpenFile,
  wozReadFile,
  wozWriteFile,
  wozCloseFile,
  wozAbortFile,
  wozSeekFile,
  wozBytesFile,
  wozFileStatus,
  wozStartFileListing,
  wozNextFile,
  wozEndFileListing,
  wozRenameFile,
  wozDeleteFile,
  wozSetParam,
  wozStartParam,
  wozGetParam,
  wozDeviceStatus,
  wozDismount,
  NULL,
  wozIOControl,
  NULL
};

/** \brief ZIP device list GC scan root. */
static mps_root_t wozGCRoot = NULL;

/** \brief Write-only ZIP device archive file pointer gc scanner; retain all
open files used by open write-only zip devices.
*/
static
mps_res_t MPS_CALL wozRootScan(mps_ss_t ss, void* p, size_t s)
{
  WriteonlyZip* self;

  UNUSED_PARAM(void*, p);
  UNUSED_PARAM(size_t, s);

  MPS_SCAN_BEGIN(ss);
  self = DLL_GET_HEAD(&wozInstances, WriteonlyZip, link);
  while (self != NULL) {
    MPS_RETAIN(&self->archive.flptr, TRUE);
    self = DLL_GET_NEXT(self, WriteonlyZip, link);
  }
  MPS_SCAN_END(ss);

  return MPS_RES_OK;
}


/* -- Public methods */
static void init_C_globals_writeonly_zipdev(void)
{
  DLL_RESET_LIST(&wozInstances);
  wozGCRoot = NULL ;
}

/** Init method; this must be called before any other method. Adds this device
to the list of all known devices, and registers a garbage collection root.
*/
static Bool writeonly_zipdev_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART*, params) ;

  if (! device_type_add(&wozDeviceType))
    return FALSE ;

  /* Create root last so we force cleanup on success. */
  if ( mps_root_create(&wozGCRoot, mm_arena, mps_rank_exact(), 0, wozRootScan,
                       NULL, 0) != MPS_RES_OK)
    return FAILURE(FALSE) ;

  return TRUE ;
}

/** Finish method; destroy the garbage collection root. No other methods in this
file should be called after this method.
*/
static void writeonly_zipdev_finish(void)
{
  mps_root_destroy(wozGCRoot);
}

void writeonly_zipdev_C_globals(core_init_fns *fns)
{
  init_C_globals_writeonly_zipdev() ;

  fns->swstart = writeonly_zipdev_swstart ;
  fns->finish = writeonly_zipdev_finish ;
}


/* Log stripped */
