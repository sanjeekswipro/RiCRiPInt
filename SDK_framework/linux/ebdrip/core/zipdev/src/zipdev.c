/** \file
 * \ingroup zipdev
 *
 * $HopeName: COREzipdev!src:zipdev.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implements relative PostScript device on top of a ZIP archive file.
 *
 * Currently only handles the following archive structure:
 * [local-file-header [data-descriptor]]* [directory-file-header]* end-of-directory
 *
 * \todo (in no particular order)
 *
 * - Debug tracing code to find out quickly why a device error was raised.
 *   MRW: started - see Debug device parameter and how used.
 * - Archive entries that are directories need better detection - currently
 *   base on filename ending with '/' (WinZIP) but could be based on external
 *   file attributes but would need to be originating platform aware, and there
 *   are lots of possible generating platforms!
 *   MRW: improved by checking MSDOS external attribute bits.
 * - Check handling of empty archive.
 *   MRW: Really possible?  Empty as in no files entries, i.e. dirs and vols.
 *   Yep really possible - just an end of central directory record.  This also
 *   means the signature is PK^E^F instead of PK^C^D.
 * - Be more clever with STORED files - if seekable always read from the
 *   archive, i.e. only extract if non-seekable stream.  Wont work if opened for
 *   write, only on readonly mounts?
 * - Add SwOften()s - as part of streamed archive support.
 * - Streamed archive support - read into buffer hanging off device context.
 *   In particular the fact that length of compressed file data will be unknown!
 *   In fact move all stack based reading over to use context buffer.
 * - Mark file in error when error in creating or extracting it.
 * .
 */

#include "core.h"
#include "coreinit.h"
#include "swstart.h"
#include "swctype.h"          /* tolower */
#include "lists.h"            /* dll_list_t */
#include "hqmemcpy.h"         /* HqMemCpy */
#include "hqmemcmp.h"         /* HqMemCmp */

#include "mm.h"               /* mm_alloc */
#include "mmcompat.h"         /* mm_alloc_with_header */
#include "gcscan.h"           /* ps_scan_file */
#include "often.h"            /* SwOftenUnsafe */
#include "calendar.h"         /* get_calendar_params */
#include "swerrors.h"         /* error_clear */
#include "monitor.h"
#include "swcopyf.h"

#if defined(METRO)
#include "xps.h"              /* xps_validate_partname_grammar */
#endif

#include "swdevice.h"         /* DEVICELIST */
#include "devices.h"          /* device_type_add() */
#include "devs.h"
/*#include "devparam.h"*/         /* DEVICEPARAM */

#include "zip_archive.h"      /* ZIP_ARCHIVE */
#include "zip_file.h"         /* ZIP_FILE */
#include "zip_util.h"         /* zutl_strlower */
#include "zip_file_stream.h"  /* ZIP_FILE_STREAM_LIST */

#include "zip_sw.h"           /* zip_mount_os */

#include "zipdev.h"

/* --------------- ZIP device parameters --------------- */

/** \brief Param cannot be set. */
#define PARAM_READONLY  (0x00)
/** \brief Param has been set. */
#define PARAM_SET       (0x01)
/** \brief Param can be set. */
#define PARAM_WRITEABLE (0x02)
/** \brief Param has range to be checked on being set. */
#define PARAM_RANGE     (0x04)


/**
 * \brief ZIP device parameter.
 */
typedef struct ZIP_DEVICE_PARAM {
  DEVICEPARAM param;        /**< Param details. */
  int32       flags;        /**< Flags controlling setting and access of the parameter. */
  int32       minval;       /**< Parameter minimum value where applicable. */
  int32       maxval;       /**< Parameter maximum value where applicable. */
} ZIP_DEVICE_PARAM;


/* Macros to check and set various param attribute flags */
/*@-exportheader@*/
/** \brief Parameter has been set. */
Bool zdp_is_set(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE_PARAM* p_param);
#define zdp_is_set(p)       (((p)->flags&PARAM_SET) != 0)

/** \brief Mark parameter as having been set. */
Bool zdp_set(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE_PARAM* p_param);
#define zdp_set(p)          MACRO_START (p)->flags |= PARAM_SET; MACRO_END

/** \brief Parameter can be set. */
Bool zdp_is_writeable(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE_PARAM* p_param);
#define zdp_is_writeable(p) (((p)->flags&PARAM_WRITEABLE) != 0)

/** \brief Parameter has a range that must be checked when set. */
Bool zdp_has_range(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE_PARAM* p_param);
#define zdp_has_range(p)    (((p)->flags&PARAM_RANGE) != 0)

/** \brief Check if a value is in the valid range for the parameter */
Bool zdp_in_range(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE_PARAM* p_param,
  int32             value);
#define zdp_in_range(p, v)  (((v) >= (p)->minval) && ((v) <= (p)->maxval))
/*@=exportheader@*/


/* Helpers for setting up default parameters */
/*@notfunction@*/
#define PARAM_NAME(name)      (uint8*)name, sizeof(name) - 1
/*@notfunction@*/
#define PARAM_STRING(string)  (uint8*)string, sizeof(string) - 1

/* ZIP parameters, and default values, and allowed ranges. */
/** \brief Default parameter set for a new ZIP device. */
static ZIP_DEVICE_PARAM default_zip_param[] = {
/* 0 */ { { PARAM_NAME("Type"), ParamString, PARAM_STRING("FileSystem")},
          PARAM_READONLY|PARAM_SET, 0, 0},
/* 1 */ { { PARAM_NAME("Filename"), ParamString, NULL, 0 },
          PARAM_WRITEABLE|PARAM_RANGE, 0, (LONGESTDEVICENAME + LONGESTFILENAME) },
/* 2 */ { { PARAM_NAME("CheckCRC32"), ParamBoolean, NULL, 0 },  /* Default is false */
          PARAM_WRITEABLE|PARAM_SET, 0, 1 },
/* 3 */ { { PARAM_NAME("IgnoreCase"), ParamBoolean, NULL, 0 },  /* Default is false */
          PARAM_WRITEABLE|PARAM_SET, 0, 1 },
/* 4 */ { { PARAM_NAME("OpenPackage"), ParamBoolean, NULL, 0 },  /* Default is false */
#ifdef METRO
          PARAM_WRITEABLE|PARAM_SET,
#else /* !METRO */
          PARAM_READONLY,                   /* Readonly and not set ensures never is returned */
#endif /* !METRO */
                                     0, 1 },
/* 5 */ { { PARAM_NAME("Close"), ParamBoolean, NULL, 0 },      /* Default is false */
          PARAM_WRITEABLE, 0, 1 },
/* 6 */ { { PARAM_NAME("ReadOnly"), ParamBoolean, NULL, 0 },   /* Default is true */
          PARAM_WRITEABLE|PARAM_SET, 0, 1 },
/* 7 */ { { PARAM_NAME("ArchiveName"), ParamString, NULL, 0 },
          PARAM_WRITEABLE|PARAM_RANGE, 1, (LONGESTDEVICENAME + LONGESTFILENAME) },
/* 8 */ { { PARAM_NAME("Streamed"), ParamBoolean, NULL, 0 },   /* Default is false */
          PARAM_WRITEABLE|PARAM_SET, 0, 1 },
/* 9 */ { { PARAM_NAME("DataSource"), ParamString, NULL, 0 },
          PARAM_WRITEABLE|PARAM_RANGE, 0, MAXPSSTRING },
/* 10 */ { { PARAM_NAME("Flush"), ParamBoolean, NULL, 0 }, /* Default is false */
          PARAM_WRITEABLE, 0, 1 },
/* 11 */ { { PARAM_NAME("ZIP64Files"), ParamBoolean, NULL, 0 }, /* Default is false */
          PARAM_WRITEABLE, 0, 1 }
#ifdef DEBUG_BUILD
/* Parameter Debug provides a means of setting debug flags for a mounted ZIP
 * device.  This parameter (and any other debug only parameters) MUST appear
 * after all normal device parameters.
 */
/* n */,{ { PARAM_NAME("Debug"), ParamInteger, NULL, 0 },
          PARAM_WRITEABLE|PARAM_SET|PARAM_RANGE, 0, MAXINT32 }
#endif /* DEBUG_BUILD */
};
#define NUM_ZIP_PARAMS  NUM_ARRAY_ITEMS(default_zip_param)


/** \brief Device Type parameter index. */
#define ZIP_PARAM_TYPE        (0)
/** \brief Device Filename parameter index. */
#define ZIP_PARAM_FILENAME    (1)
/** \brief Device CheckCRC32 parameter index. */
#define ZIP_PARAM_CHECKCRC32  (2)
/** \brief Device IgnoreCase parameter index. */
#define ZIP_PARAM_IGNORECASE  (3)
/** \brief Device IgnoreCase parameter index. */
#define ZIP_PARAM_OPENPACKAGE (4)
/** \brief Device Closed parameter index. */
#define ZIP_PARAM_CLOSE       (5)
/** \brief Device ReadOnly parameter index. */
#define ZIP_PARAM_READONLY    (6)
/** \brief Device Archive creation name index. */
#define ZIP_PARAM_ARCHIVE     (7)
/** \brief Device Streamed parameter index. */
#define ZIP_PARAM_STREAMED    (8)
/** \brief Device DataSource parameter index. */
#define ZIP_PARAM_DATASOURCE  (9)
/** \brief Device Flush parameter index. */
#define ZIP_PARAM_FLUSH       (10)
/** \brief Device ZIP64Files parameter index. */
#define ZIP_PARAM_ZIP64FILES  (11)

#ifdef DEBUG_BUILD
/** \brief Device Debug parameter index. */
#define ZIP_PARAM_DEBUG       (12)
#endif /* DEBUG_BUILD */

/* --------------- ZIP archive creation file details --------------- */

/**
 * \brief New ZIP archive item.
 *
 * Need to track each item added to a new ZIP archive in order to create central
 * directory and update local file header with compressed file size.
 */
typedef struct ZIP_ARCHIVE_ITEM {
  dll_link_t    link;         /**< Archive item list link. */
  ZIP_FILE*     p_file;       /**< ZIP device file added to archive. */
  Hq32x2        lcal_pos;     /**< Offset from start or archive to local file header. */
  ZIP_CDIR_FILE cdir_file;    /**< Central directory file header record for item. */
  ZIP_FILE_NAME filename;     /**< Name of ZIP device file. */
  Bool          is_zip64;     /**< Item uses ZIP64 extension. */
  ZIP_XTRAFLD_ZIP64 z64_xtrafld; /**< ZIP64 extra field of item. */
  ZIP_ZIP64_DATA_DESC z64_data_desc; /**< ZIP64 data descriptor. */

  OBJECT_NAME_MEMBER
} ZIP_ARCHIVE_ITEM;

/** \brief ZIP archive item structure name used to generate hash checksum. */
#define ZIP_ARCHIVE_ITEM_OBJECT_NAME  "ZIP Item"

/* --------------- ZIP device file iteration --------------- */

/**
 * \brief ZIP device file iterator context.
 *
 * The programmer's reference manual states that filenameforall can go recursive
 * which to my way of thinking means files can be added/deleted within the
 * procedure for filenameforall and should be picked up.  However, looking at
 * the implementation for filenameforall it currently (2005/09/19) builds a list
 * of filenames before invoking the procedure on each one.  Therefore, the
 * filename iterator will never have to be cope with being called recursively!
 *
 * But just in case it does, we need to be able to dynamically change the chain
 * of all current filenames under the feet of active iterators.  To do this we
 * track which file the iterator will look at next for a pattern match so that
 * if the file is deleted we can update the iterator to point at the next file.
 * Also, when a new file is added to the device, any iterator that would end on
 * the next call for the next file is updated to point to the file just added.
 */
typedef struct ZIP_DEVICE_ITER {
  dll_link_t  link;     /**< ZIP Device file iterator list link. */
  ZIP_FILE*   p_file;   /**< Pointer to next file in iteration. */

  OBJECT_NAME_MEMBER
} ZIP_DEVICE_ITER;

/** \brief ZIP device iterator structure name used to generate hash checksum. */
#define ZIP_DEVICE_ITER_OBJECT_NAME "ZIP Iterator"

/* --------------- ZIP device --------------- */

/** \brief ZIP device filename hash table size - needs to be a prime! */
#define ZIP_DEVICE_FILENAME_TABLE_SIZE  (1097u)

/**
 * \brief ZIP device.
 *
 * A ZIP device maintains a list of all the filenames found in the ZIP
 * archive in unsorted order.  New files encountered are always added to the end
 * of this list.  This allows filenameforall to incrementally populate the
 * filename list by reading the archive to find additional filenames and pick
 * them up, where they might appear earlier in the main filename hashtable.
 */
typedef struct ZIP_DEVICE {
  dll_link_t        link;               /**< ZIP device list link. */
/*@dependent@*/
  ZIP_ARCHIVE       archive;            /**< ZIP archive device presents. */
/*@dependent@*/
  FILELIST_CLOSEFILE old_close;         /**< Original closefile function for the archive file. */

  int32             flags;              /* See below */
  int32             zip_id;             /**< Mounted ZIP device id. */
  int32             next_file_id;       /**< Id to use for the next file on the device. */

  ZIP_FILE_DEVICE   file_device;        /**< Device holding ZIP device files. */
/*@owned@*/ /*@null@*/
  uint8*            archive_buffer;     /**< Streamed archive input buffer. */

  ZIP_FILE_LIST file_list[ZIP_DEVICE_FILENAME_TABLE_SIZE];
                                        /**< ZIP device file hashtable. */
  ZIP_FILE_STREAM_LIST stream_list;     /**< List of all open streams. */

  ZIP_FILE_CHAIN    chain;              /**< Chain of all files on the device. */
  dll_list_t        iterators;          /**< List of active filename iterators. */

  ZIP_DEVICE_PARAM  param[NUM_ZIP_PARAMS]; /**< ZIP device parameters. */
  int32             next_param;         /**< Index of next device parameter to return. */

  OBJECT_NAME_MEMBER
} ZIP_DEVICE;


/** \brief List all files on ZIP device currently open. */
#define ZIP_DEVICE_DBG_OPEN_FILES   (0x01)
/** \brief List all files on ZIP that get flushed. */
#define ZIP_DEVICE_DBG_FLUSH_FILES  (0x02)


/** \brief ZIP device structure name used to generate hash checksum. */
#define ZIP_DEVICE_OBJECT_NAME      "ZIP Device"


/** \brief Largest file id possible on a device (>16 million!) */
#define ZIP_DEVICE_FILE_MAX_ID      (0x00ffffff)


/** \brief Do CRC32 checks on extracted files. */
#define ZIP_DEVICE_CALCCRC32        (0x01)
/** \brief Ignore case in filenames. */
#define ZIP_DEVICE_CASEINSENSITIVE  (0x02)
/** \brief MS Open Package Container (OPC) type ZIP archive. */
#define ZIP_DEVICE_OPENPACKAGE      (0x04)
/** \brief Device is read-only. */
#define ZIP_DEVICE_READONLY         (0x08)
/** \brief Creating an archive from device. */
#define ZIP_DEVICE_CREATING         (0x10)
/** \brief Creating an archive from device. */
#define ZIP_DEVICE_ZIP64FILES       (0x20)

/*@-exportheader@*/
/** \brief Do CRC32 checksum checks when extracting files from an archive. */
Bool zdv_checkcrc32(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE* p);
#define zdv_checkcrc32(p)   (((p)->flags&ZIP_DEVICE_CALCCRC32) != 0)

/** \brief Ignore case of filenames in archive. */
Bool zdv_ignorecase(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE* p);
#define zdv_ignorecase(p)   (((p)->flags&ZIP_DEVICE_CASEINSENSITIVE) != 0)

/** \brief Normalise filenames in archive. */
Bool zdv_normalise(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE* p);
#define zdv_normalise(p)    (((p)->flags&ZIP_DEVICE_OPENPACKAGE) != 0)

/** \brief Merge files with recognised name patterns into single logical files. */
Bool zdv_mergefiles(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE* p);
#define zdv_mergefiles(p)   (((p)->flags&ZIP_DEVICE_OPENPACKAGE) != 0)

/** \brief Device is readonly. */
Bool zdv_readonly(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE* p);
#define zdv_readonly(p)     (((p)->flags&ZIP_DEVICE_READONLY) != 0)

/** \brief Device is creating a new archive. */
Bool zdv_creating(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE* p);
#define zdv_creating(p)     (((p)->flags&ZIP_DEVICE_CREATING) != 0)

/** \brief Device is creating files that are all ZIP64. */
Bool zdv_zip64_files(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE* p);
#define zdv_zip64_files(p)  (((p)->flags&ZIP_DEVICE_ZIP64FILES) != 0)

/** \brief Get ZIP device hashtable list for the filename. */
/*@notnull@*/ /*@observer@*/
ZIP_FILE_LIST* zdv_filename_list(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE* p_zipdev,
/*@in@*/ /*@notnull@*/
  ZIP_FILE_NAME* p_file_name);
#define zdv_filename_list(p, f) (&((p)->file_list[zfl_name_hash((f), zdv_ignorecase(p))%ZIP_DEVICE_FILENAME_TABLE_SIZE]))
/*@=exportheader@*/


/** \brief Maximum ZIP device id. */
#define ZIP_DEVICE_MAX_ID  (0xff)

/**
 * \brief List of all mounted ZIP devices.
 *
 * The list of ZIP devices is kept in increasing ZIP_DEVICE::zip_id order.  When a ZIP
 * device is mounted, the smallest unused id is reused and the new device
 * inserted into the list in order.
 */
static dll_list_t dls_zipdevs;

/* --------------- ZIP device GC scanner functions --------------- */

/** \brief ZIP device list GC scan root. */
static mps_root_t zipdev_file_root = NULL;

/**
 * \brief ZIP device archive file pointer gc scanner.
 *
 * Walk the list of current ZIP devices scanning the archive file pointer for
 * any that are currently open and have an input archive associated with them.
 *
 * \param[in] ss
 * The scan state.
 * \param[in] p
 * Pointer to aribtrary scan data - unused.
 * \param[in] s
 * Aribtrary size value - unused.
 *
 * \return
 * \c MPS_RES_OK if archive file pointer scan completed ok, else MPS return
 * valud from failed call to ps_scan_file().
 */
static
mps_res_t MPS_CALL zipdev_file_root_scan(
  mps_ss_t  ss,
  void*     p,
  size_t    s)
{
  ZIP_DEVICE* p_zipdev;

  UNUSED_PARAM(void*, p);
  UNUSED_PARAM(size_t, s);

  MPS_SCAN_BEGIN(ss);
  p_zipdev = DLL_GET_HEAD(&dls_zipdevs, ZIP_DEVICE, link);
  while ( p_zipdev != NULL ) {
    if ( zar_opened(&p_zipdev->archive) && (p_zipdev->archive.flptr != NULL) ) {
      MPS_RETAIN(&p_zipdev->archive.flptr, TRUE);
    }
    p_zipdev = DLL_GET_NEXT(p_zipdev, ZIP_DEVICE, link);
  }
  MPS_SCAN_END(ss);

  return(MPS_RES_OK);

} /* zipdev_file_root_scan */

/* --------------- ZIP device internal functions --------------- */

/**
 * \brief Set device error code and return a value.
 *
 * Allows for calls to set the device error code and function return value in
 * one statement.  For example:<br>
 * <code>return(zdv_errorhandler(p_zipdev, DeviceIOError, -1));</code>
 *
 * The return code must be representable in a 32-but integer.
 *
 * \param[in] p_zipdev
 * Pointer to ZIP device.
 * \param[in] error
 * Device error code to set.
 * \param[in] retcode
 * Return code to return to caller.
 *
 * \return
 * The supplied return code.
 */
static
int32 zdv_errorhandler(
  int32       error,
  int32       retcode)
{
  devices_set_last_error(error);

  return retcode;

} /* zdv_errorhandler */


/**
 * \brief Convert filename to internal filename by skipping any leading /
 *
 * \param[in] filename
 * Pointer to original filename.
 * \param[out] zip_filename
 * Pointer to filled in ZIP filename structure.
 *
 * \return
 * \c TRUE if the filename length is non zero, else \c FALSE.
 */
static
Bool zdv_filename(
  uint8*          filename,
  ZIP_FILE_NAME*  zip_filename)
{
  HQASSERT((filename != NULL),
           "zdv_filename: NULL filename pointer");
  HQASSERT((zip_filename != NULL),
           "zdv_filename: NULL ZIP filename pointer");

  /* Skip any leading directory separator since they should never appear in ZIP archives. */
  if ( *filename == '/' ) {
    filename++;
  }
  zip_filename->name = filename;
  zip_filename->namelength = strlen_int32((char*)zip_filename->name);

  /* Return if filename has a valid length */
  return(zip_filename->namelength > 0);

} /* zdv_filename */


/**
 * \brief Create a new ZIP device file iterator context.
 *
 * The new iterator context is added to the device's list of iterators, the
 * caller does not have to do this.
 *
 * \param[in] p_zipdev
 * Pointer to ZIP device iteration will be on.
 *
 * \return
 * Pointer to new iterator context, else \c NULL.
 */
static
ZIP_DEVICE_ITER* zdv_new_iterator(
/*@notnull@*/ /*@in@*/
  ZIP_DEVICE* p_zipdev)
{
  ZIP_DEVICE_ITER*  p_iterator;

  HQASSERT((p_zipdev != NULL),
           "zdv_new_iterator: NULL zip device pointer");
  HQASSERT((zar_opened(&p_zipdev->archive)),
           "zdv_new_iterator: ZIP archive not yet open");

  /* Allocate new iterator context */
  p_iterator = mm_alloc(mm_pool_temp, sizeof(ZIP_DEVICE_ITER), MM_ALLOC_CLASS_ZIP_ITERATOR);
  if ( p_iterator == NULL ) {
    return(NULL);
  }

  NAME_OBJECT(p_iterator, ZIP_DEVICE_ITER_OBJECT_NAME);

  /* Initialise iterator context */
  DLL_RESET_LINK(p_iterator, link);
  p_iterator->p_file = zfc_get_first(&p_zipdev->chain);

  /* Add serch to list of iterators and get first file in chain */
  DLL_ADD_HEAD(&p_zipdev->iterators, p_iterator, link);

  return(p_iterator);

} /* zdv_new_iterator */


/**
 * \brief Free a ZIP device iterator context.
 *
 * \param[in] p_iterator
 * Pointer to iterator context to be freed.
 */
static
void zdv_free_iterator(
/*@notnull@*/ /*@in@*/
  ZIP_DEVICE_ITER*  p_iterator)
{
  HQASSERT((p_iterator != NULL),
           "zdv_free_iterator: NULL iterator context handle.");
  HQASSERT((DLL_IN_LIST(p_iterator, link)),
           "zdv_free_iterator: iterator not in list");

  VERIFY_OBJECT(p_iterator, ZIP_DEVICE_ITER_OBJECT_NAME);

  /* Remove iterator from list of active iterators */
  DLL_REMOVE(p_iterator, link);

  UNNAME_OBJECT(p_iterator);

  mm_free(mm_pool_temp, p_iterator, sizeof(ZIP_DEVICE_ITER));

} /* zdv_free_iterator */


/**
 * \brief Return iterators current file.
 *
 * \param[in] p_iterator
 * Pointer to device iterator context.
 *
 * \return
 * Pointer to current ZIP device file, or \c NULL if at the end of the device
 * file list.
 */
static
ZIP_FILE* zdv_iterator_current(
/*@notnull@*/ /*@in@*/
  ZIP_DEVICE_ITER*  p_iterator)
{
  HQASSERT((p_iterator != NULL),
           "zdv_iterator_current: NULL iterator context handle.");

  VERIFY_OBJECT(p_iterator, ZIP_DEVICE_ITER_OBJECT_NAME);

  return(p_iterator->p_file);

} /* zdv_iterator_current */


/**
 * \brief Return the next file in the iteration.
 *
 * \param[in] p_zipdev
 * Pointer to ZIP device.
 * \param[in] p_iterator
 * Pointer to device iterator context.
 *
 * \return
 * Pointer to next ZIP device file or \c NULL if the end of the device file list
 * is reached.
 */
static
ZIP_FILE* zdv_iterator_next(
/*@notnull@*/ /*@in@*/
  ZIP_DEVICE*         p_zipdev,
/*@notnull@*/ /*@in@*/
  ZIP_DEVICE_ITER*  p_iterator)
{
  HQASSERT((p_zipdev != NULL),
           "zdv_iterator_next: NULL zip device pointer");
  HQASSERT((p_iterator != NULL),
           "zdv_iterator_next: NULL iterator context handle.");

  VERIFY_OBJECT(p_iterator, ZIP_DEVICE_ITER_OBJECT_NAME);

  /* Get next file in chain after current one. If we are streaming and
     are currently on the last p_file in list, but there is another
     piece, the next p_file will be NULL, so only look for the next
     p_file if the iterator p_file is not NULL. */
  if (p_iterator->p_file != NULL)
    p_iterator->p_file = zfc_get_next(&p_zipdev->chain, p_iterator->p_file);
  return(p_iterator->p_file);

} /* zdv_iterator_next */


/**
 * \brief Update all iterators at the end of the device file list for a new file
 * that has been added to the device.
 *
 * \param[in] p_zipdev
 * Pointer to ZIP device.
 * \param[in] p_file
 * Pointer to device file that is being added to the device.
 */
static
void zdv_iterators_add(
/*@notnull@*/ /*@in@*/
  ZIP_DEVICE* p_zipdev,
/*@notnull@*/ /*@in@*/
  ZIP_FILE*   p_file)
{
  ZIP_DEVICE_ITER* p_iterator;

  HQASSERT((p_zipdev != NULL),
           "zdv_iterators_add: NULL zip device pointer");
  HQASSERT((p_file != NULL),
           "zdv_iterators_add: NULL file pointer");

  /* Walk list of iterators looking for any that are about to end (reached the
   * end of the chain) and set them to return the file just added to the device
   * at the end of the chain.
   */
  p_iterator = DLL_GET_HEAD(&p_zipdev->iterators, ZIP_DEVICE_ITER, link);
  while ( p_iterator != NULL ) {
    if ( p_iterator->p_file == NULL ) {
      p_iterator->p_file = p_file;
    }
    p_iterator = DLL_GET_NEXT(p_iterator, ZIP_DEVICE_ITER, link);
  }

} /* zdv_iterators_add */


/**
 * \brief Update all iterators whose current file is a file about to be deleted
 * from the device.
 *
 * \param[in] p_zipdev
 * Pointer to ZIP device.
 * \param[in] p_file
 * Pointer to device file being deleted from device.
 */
static
void zdv_iterators_remove(
/*@notnull@*/ /*@in@*/
  ZIP_DEVICE* p_zipdev,
/*@notnull@*/ /*@in@*/
  ZIP_FILE*   p_file)
{
  ZIP_DEVICE_ITER* p_iterator;

  HQASSERT((p_zipdev != NULL),
           "zdv_iterators_remove: NULL zip device pointer");
  HQASSERT((p_file != NULL),
           "zdv_iterators_remove: NULL file pointer");

  /* Walk list of iterators looking for any that would return the given file
   * next, and set them to return the file after that one.
   */
  p_iterator = DLL_GET_HEAD(&p_zipdev->iterators, ZIP_DEVICE_ITER, link);
  while ( p_iterator != NULL ) {
    if ( p_iterator->p_file == p_file ) {
      p_iterator->p_file = zfc_get_next(&p_zipdev->chain, p_file);
    }
    p_iterator = DLL_GET_NEXT(p_iterator, ZIP_DEVICE_ITER, link);
  }

} /* zdv_iterators_remove */


/** \brief File being added is a new file, not from an archive. */
#define ZDV_ADD_NEWFILE FALSE
/** \brief File being added is a new file from an archive. */
#define ZDV_ADD_ZIPFILE TRUE

/**
 * \brief Add new file to device file lists.
 *
 * \param[in] p_zipdev
 * Pointer to ZIP device file is to be added to.
 * \param[in] p_file_list
 * Pointer to file list to add file to.
 * \param[in] name
 * Original file name.
 * \param[in] normalised_name
 * File name in normalised form.
 * \param[in] date_time
 * File time (creation for non-ZIP files) in ZIP format.
 * \param[in] f_zipfile
 * Flag indicating if file is from a ZIP archive or not
 * \param[in] pp_file
 * Pointer to returned device file structure.
 *
 * \return
 * \c TRUE if new file created and added to device file lists, else \c FALSE.
 */
static
Bool zdv_add_file(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE*     p_zipdev,
/*@in@*/ /*@notnull@*/
  ZIP_FILE_LIST*  p_file_list,
/*@in@*/ /*@notnull@*/
  ZIP_FILE_NAME*  name,
/*@in@*/ /*@notnull@*/
  ZIP_FILE_NAME*  normalised_name,
  uint32          date_time,
  Bool            f_zipfile,
/*@out@*/ /*@notnull@*/
  ZIP_FILE**      pp_file)
{
  ZIP_FILE* p_file;
#define FILENAME_SIZE       (sizeof(ZIPDEV_FILE_PREFIX "zzffffff"))
  uint8     device_filename[FILENAME_SIZE];

  HQASSERT((p_zipdev != NULL),
           "zdv_add_file: NULL ZIP device pointer");
  HQASSERT((pp_file != NULL),
           "zdv_add_file: NULL pointer to returned pointer");

  /* Check we haven't run out of archive file ids to use */
  if ( p_zipdev->next_file_id > ZIP_DEVICE_FILE_MAX_ID ) {
    return FAILURE(FALSE);
  }

  /* Generate unique filename on file device, of the form ZIP/zzffffff */
  swncopyf(device_filename, FILENAME_SIZE, (uint8*)"%s%02x%06x",
           ZIPDEV_FILE_PREFIX, p_zipdev->zip_id, p_zipdev->next_file_id);

  /* Add new device file to file list */
  p_file = zfl_new(p_file_list, name, normalised_name, date_time,
                   &p_zipdev->file_device, device_filename,
                   f_zipfile, &p_zipdev->archive, zdv_checkcrc32(p_zipdev));
  if ( p_file == NULL ) {
    return FAILURE(FALSE);
  }

  /* Update id for next file */
  p_zipdev->next_file_id++;

  /* Add file to any iterarors that are at the end of the chain. */
  zdv_iterators_add(p_zipdev, p_file);
  /* Update list of all files for filenameforall */
  zfc_append(&p_zipdev->chain, p_file);

  /* Need to return this new file */
  *pp_file = p_file;

  return(TRUE);

} /* zdv_add_file */


/**
 * \brief Add a file from a ZIP archive to the ZIP device.
 *
 * This function is called whenever a new file is found in an archive. This will
 * be from the central directory for a seekable archive and from the local file
 * header for a streamed archive.
 *
 * Archive may have entries for directories which are not relevant to the ZIP
 * device.  These entries are accepted but not added to the device list of
 * entries.
 *
 * A file is considered to be comprised of two parts, the file metadata (name,
 * modification date, compression used, etc.) and the actual file data.  The
 * file metadata is kept in a hashtable, hashed on the filename.  File data is
 * added to the hashtable entry, and it is up to the entry to decide if it is
 * happy with the file data (the obvious issue being duplicate files).
 *
 * If the file is new then a pointer to the new file will be returned through
 * \p pp_file. If the file is not new or is ignored then the returned file
 * pointer will be \c NULL.
 *
 * \param[in] p_zipdev
 * Pointer to ZIP device.
 * \param[in] p_info
 * Pointer to information on file being added.
 * \param[in] file_name
 * Pointer to archive file name.
 * \param[out] pp_file
 * Pointer to returned new logical file pointer.
 *
 * \return
 * \c TRUE if the file has been accepted by the device, else \c FALSE.
 */
static
Bool zdv_add_zipfile(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE*     p_zipdev,
/*@in@*/ /*@notnull@*/
  ZIP_FILE_INFO*  p_info,
/*@in@*/ /*@notnull@*/
  ZIP_FILE_NAME*  file_name,
/*@out@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE_PTR*   pp_file)
{
  uint32          number;
  int32           type;
  ZIP_FILE*       p_file;
/*@dependent@*/ /*@notnull@*/
  ZIP_FILE_LIST*  p_file_list;
#if defined(METRO)
  uint8 normalised[LONGESTFILENAME] ;
#endif
  ZIP_FILE_NAME   normalised_name;

  HQASSERT((p_zipdev != NULL),
           "zdv_add_zipfile: NULL ZIP context pointer");
  HQASSERT((p_info != NULL),
           "zdv_add_zipfile: NULL file info pointer");
  HQASSERT((file_name != NULL),
           "zdv_add_zipfile: NULL filename pointer");
  HQASSERT((pp_file != NULL),
           "zdv_add_zipfile: NULL ZIP file pointer");

  *pp_file = NULL;

  /* Catch ZIP files we can not extract */
  if ( !ZIP_CAN_EXTRACT(p_info) ) {
    return FAILURE(FALSE);
  }

#if defined(METRO)
  if ( zdv_normalise(p_zipdev) ) {
    /* We use the normalised name as the canonical file name when
       processing XPS. */
    int32 i;
    for ( i = 0; i < file_name->namelength; i++ ) {
      normalised[i] = (uint8)tolower(file_name->name[i]);
    }

    file_name->name = normalised;
  }
#endif

  /* Classify the type of the filename */
  type = zfl_piece_type(file_name, zdv_mergefiles(p_zipdev),
                        &file_name->namelength, &number);

  if ( zpt_directory(type) ) {
    /* Directories are not added to the device */

    if ( zar_streamed(&p_zipdev->archive) ) {
      /* But on streamed archives they need to be read over. */
      /** \todo data descriptor? */
    }
    return(TRUE);
  }

  /* Assume that the normalised name is the same as the file name. */
  normalised_name = *file_name;

#if defined(METRO)
  /* We only ignore invalid XPS part names if normalisation is turned
     on. */
  if ( zdv_normalise(p_zipdev) ) {
    /* XPS normalised name is the same as the canonical file name. How
       lucky, nothing to do. */
    if ( !xps_validate_partname_grammar(normalised_name.name, normalised_name.namelength,
                                        XPS_NORMALISE_ZIPNAME)) {
      /* Its not an error if a unit name does not map to a part name,
         it simply gets ignored which is not unlike a directory entry
         in the ZIP archive. */
      error_clear();
      return(TRUE);
    }
  }
#endif

  /* Check if we have seen the filename already */
  p_file_list = zdv_filename_list(p_zipdev, &normalised_name);
  p_file = zfl_find(p_file_list, &normalised_name, zdv_ignorecase(p_zipdev),
                    zdv_normalise(p_zipdev));

  /* Create new device entry if a new filename */
  if ( p_file == NULL ) {
    if ( !zdv_add_file(p_zipdev, p_file_list, file_name, &normalised_name,
                       p_info->date_time, ZDV_ADD_ZIPFILE, &p_file) ) {
      return(FALSE);
    }
  }

  /* Add data piece to file */
  if ( !zfl_add_piece(p_file, p_info, number, type) ) {
    return(FALSE);
  }

  if ( zar_streamed(&p_zipdev->archive) ) {
    DEVICE_FILEDESCRIPTOR temp_fd ;

    /* Streamed archive; extract this piece now */
    if ( !zfl_open(p_file, (SW_RDONLY), &temp_fd)) {
      return (FALSE);
    }

    if ( !zfl_extract(p_file) ) {
      return(FALSE);
    }

    zfl_close(p_file, temp_fd, FALSE);
  }

  *pp_file = p_file;
  return(TRUE);

} /* zdv_add_zipfile */


/** \brief Derive file information from the local header and any ZIP64 extra
 * field.
 *
 * \param[in] p_lcal_file
 * Pointer to local file header.
 * \param[in] offset
 * Pointer to local header offset.
 * \param[in] extras
 * Pointer to start of extra fields.
 * \param[out] p_info
 * Pointer to returned file information.
 */
static
void zdv_create_info(
/*@in@*/ /*@notnull@*/
  ZIP_LCAL_FILE*  p_lcal_file,
/*@in@*/ /*@notnull@*/
  uint32          offset,
/*@in@*/ /*@notnull@*/
  uint8*          extras,
/*@out@*/ /*@notnull@*/
  ZIP_FILE_INFO*  p_info)
{
  ZIP_XTRAFLD_ZIP64 z64_xtrfld;
  uint8*        xtrfld_start;

  HQASSERT((p_lcal_file != NULL),
           "zdv_create_info: NULL local file header pointer");
  HQASSERT((extras != NULL),
           "zdv_create_info: NULL extra fields pointer");
  HQASSERT((p_info != NULL),
           "zdv_create_info: NULL file info pointer");

  /* Common file information and file sizes from local header */
  p_info->flags = p_lcal_file->flags;
  p_info->compression = p_lcal_file->compression;
  p_info->date_time = (p_lcal_file->mod_date<<16)|p_lcal_file->mod_time;
  p_info->crc_32 = p_lcal_file->data_desc.crc_32;
  HqU32x2FromUint32(&p_info->compressed, p_lcal_file->data_desc.compressed);
  HqU32x2FromUint32(&p_info->uncompressed, p_lcal_file->data_desc.uncompressed_size);
  HqU32x2FromUint32(&p_info->offset, offset);
  p_info->zip64 = FALSE;

  /* Check for any ZIP64 extra field */
  if ( (p_lcal_file->version_needed == ZIP_VERSION(4, 5)) &&
       zar_find_xtrafld(extras, p_lcal_file->extras_len, ZAR_EXTRA_ID_ZIP64,
                        &xtrfld_start, &z64_xtrfld.header) ) {
    /* Use ZIP64 extra field values if local file header fields say so */
    zar_read_xtrafld_zip64(xtrfld_start, &z64_xtrfld);
    if ( (p_lcal_file->data_desc.compressed == ZIP_USE_ZIP64FLD_LONG) &&
         ((z64_xtrfld.present&ZIP64_COMPRESSED) != 0) ) {
      p_info->compressed = z64_xtrfld.compressed;
    }
    if ( (p_lcal_file->data_desc.uncompressed_size == ZIP_USE_ZIP64FLD_LONG) &&
         ((z64_xtrfld.present&ZIP64_UNCOMPRESSED) != 0) ) {
      p_info->uncompressed = z64_xtrfld.uncompressed_size;
    }
    if ( (offset == ZIP_USE_ZIP64FLD_LONG) &&
         (z64_xtrfld.present&ZIP64_OFFSET) != 0 ) {
      p_info->offset = z64_xtrfld.lcal_file_hdr_offset;
    }
    p_info->zip64 = TRUE;
  }

} /* zdv_create_info */


/** \brief MSDOS file attribute readonly bit. */
#define MSDOS_READONLY    (0x01)
/** \brief MSDOS file attribute hidden bit. */
#define MSDOS_HIDDEN      (0x02)
/** \brief MSDOS file attribute system bit. */
#define MSDOS_SYSTEM      (0x04)
/** \brief MSDOS file attribute disk volume bit. */
#define MSDOS_VOLUME      (0x08)
/** \brief MSDOS file attribute directory bit. */
#define MSDOS_DIRECTORY   (0x10)
/** \brief MSDOS file attribute archive bit. */
#define MSDOS_ARCHIVE     (0x20)

/** \brief MSDOS normal file attribute value. */
#define MSDOS_NORMAL      (0x00)
/** \brief MSDOS file directory and volume bit mask. */
#define MSDOS_DIRVOL      (MSDOS_VOLUME|MSDOS_DIRECTORY)

/* I have seen some comments on the web that an MSDOS file attribute value of
 * 0x0f is used to indicate a long file name entry in a (VFAT?) directory table.
 * The question is would this be copied through to a ZIP archive?
 */

/**
 * \brief Read all the files in the archive's central directory.
 *
 * This function finds and reads in files from a ZIP archives central directory.
 * Currently only single volume archives are handled.
 *
 * The version made by field of the central directory record to is checked see
 * if the external file attributes field is MS-DOS compatible.  If it is then it
 * is checked to see if the record is for a volume label or a directory, and if
 * it is the record is skipped.
 *
 * InfoZIP documentation notes some differences to the PKWARE specification for
 * the compatibility values.  In particular, InfoZIP uses 11 instead of 10 for
 * NTFS and claim no problems as PKWARE never used 10 anyway.  It should be
 * noted that there is no way to detect which application created a ZIP archive,
 * so for now we always check for the InfoZIP NTFS compatibility.  If it becomes
 * an issue it will have to be controlled by a device parameter.
 *
 * \param[in] p_zipdev
 * Pointer to the ZIP device.
 *
 * \return
 * \c TRUE if the whole central directory is read successfully, else \c FALSE.
 */
static
Bool zdv_read_directory(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE*   p_zipdev)
{
  Bool          is_zip64;
  int32         compat;
  uint32        sig;
  ZIP_FILE*     p_file;
  uint8         filename[LONGESTFILENAME];
  uint8         extras[MAXUINT16];  /** \todo stick in device structure? */
  ZIP_END_CDIR  end_cdir;
  ZIP_CDIR_FILE cdir_file;
  ZIP_ZIP64_CDIR_LOC z64_cdir_locator;
  ZIP_ZIP64_END_CDIR z64_end_cdir;
  Hq32x2        pos;
  Hq32x2        file_pos;
  ZIP_FILE_INFO info;
  ZIP_FILE_NAME file_name;

  HQASSERT((p_zipdev != NULL),
           "zdv_read_directory: NULL context pointer");
  HQASSERT((!zar_streamed(&p_zipdev->archive)),
           "zdv_read_directory: streamed input");

  /* Find and read the end central directory record in order to locate start of
   * central directory file headers. */
  if ( !zar_find_end_cdir(&p_zipdev->archive, &file_pos) ||
       !zar_read_end_cdir(&p_zipdev->archive, &end_cdir) ) {
    return(FALSE);
  }

  is_zip64 = FALSE;
  if ( end_cdir.cdir_offset == ZIP_USE_ZIP64FLD_LONG ) {
    /* Possibly ZIP64 - look for a locator record */
    if ( !zar_find_zip64_cdir_locator(&p_zipdev->archive, &file_pos, &is_zip64) ) {
      return(FALSE);
    }
  }

  if ( is_zip64 ) {
    /* Found a locator, read it and locate ZIP64 end of central directory */
    if ( !zar_read_zip64_cdir_locator(&p_zipdev->archive, &z64_cdir_locator) ) {
      return(FALSE);
    }
    /* Check single disk archive */
    if ( z64_cdir_locator.total_disks > 1 ) {
      return(FALSE);
    }
    /* Read ZIP64 end of central directory record */
    Hq32x2FromU32x2(&file_pos, &z64_cdir_locator.zip64_end_cdir_offset);
    if ( !zar_set_pos(&p_zipdev->archive, &file_pos) ||
         !zar_match_sig(&p_zipdev->archive, PKSIG_ZIP64_ENDCDIR) ||
         !zar_read_zip64_end_cdir(&p_zipdev->archive, &z64_end_cdir) ) {
      return(FALSE);
    }
    /* Check version is the one we expect */
    if ( z64_end_cdir.version_needed != ZIP_VERSION(4, 5) ) {
      return(FALSE);
    }
    /* Pick up start of central directory */
    Hq32x2FromU32x2(&pos, &z64_end_cdir.cdir_start_offset);

  } else {
    if ( (end_cdir.disknum != 0) ||
         (end_cdir.disknum != end_cdir.cdir_start_disknum) ) {
      /* Looks like a spanned ZIP archive - awooga */
      return FAILURE(FALSE);
    }

    Hq32x2FromUint32(&pos, end_cdir.cdir_offset);
  }

  /* Reposition to start of central directory */
  if ( !zar_set_pos(&p_zipdev->archive, &pos) ) {
    return(FALSE);
  }

  /* Read in all the central directory file headers */
  for ( ;; ) {
    /* Read the next sig */
    if ( !zar_next_sig(&p_zipdev->archive, &sig) ) {
      /* Did not find a recognised signature before the end of the archive - awooga */
      return(FALSE);
    }

    switch ( sig ) {
    case PKSIG_CDIR_FILE:
      /* Read a file header ... */
      if ( !zar_read_cdir_file(&p_zipdev->archive, &cdir_file, filename, extras) ) {
        return(FALSE);
      }
      /* ... check not MSDOS/NTFS attributes or if it is its not a volume or directory ... */
      compat = zar_cdir_file_compatibility(&cdir_file);
      if ( !((compat == ZIP_COMPAT_MSDOS) || (compat == ZIP_COMPAT_IZ_NTFS)) ||
           ((cdir_file.external_attributes&MSDOS_DIRVOL) == 0) ) {
        /* ... update for any ZIP64 extra field ... */
        zdv_create_info(&cdir_file.lcal_file, cdir_file.lcal_file_hdr_offset, extras, &info);

        /* ... and add it to the device */
        file_name.name = filename;
        file_name.namelength = cdir_file.lcal_file.name_len;
        if ( !zdv_add_zipfile(p_zipdev, &info, &file_name, &p_file) ) {
          return(FALSE);
        }
      }

      /* Tickle the RIP */
      SwOftenUnsafe();
      break;

    case PKSIG_ZIP64_LOCATOR:
      /* The locator should appear after a ZIP64 end of cdir record, so in
       * theory we should never see a locator in this function. */
      HQFAIL("zdv_read_directory: encountered ZIP64 locator when not expected.");
      /* FALLTHROUGH */

    case PKSIG_ZIP64_ENDCDIR:
    case PKSIG_ENDCDIR:
      /* Read all the files in the central directory ok */
      return(TRUE);

    case PKSIG_LCAL_FILE:
    case PKSIG_DATADESC:
      /* Shouldn't appear unless the start of cdir offset is corrupt. Continue
       * reading sigs until a cdirfile sig is reached. */
      break;

    default:
      HQFAIL("zdv_read_directory: unknown record signature.");
      break;
    }
  }

  /* never reached */

} /* zdv_read_directory */


/**
 * \brief Read all the files in the archive's central directory.
 *
 * This reads in files from a ZIP archive central directory when the
 * ZIP archive is being streamed in. It starts at the current file
 * position.
 *
 * Currently does not validate if the central directory matches the
 * file headers which have been seen. The primary reason we
 * effectively ignore the central directory in a streamed scenario is
 * that the job will have already been printed.
 *
 * \param[in] p_zipdev
 * Pointer to the ZIP device.
 *
 * \return
 * \c TRUE if the whole central directory is read successfully, else \c FALSE.
 */
static
Bool zdv_read_directory_streamed(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE*   p_zipdev)
{
  uint32        sig;
  uint8         filename[LONGESTFILENAME];
  uint8         extras[MAXUINT16];
  ZIP_END_CDIR  end_cdir;
  ZIP_ZIP64_END_CDIR z64_end_cdir;
  ZIP_ZIP64_CDIR_LOC z64_cdir_loc;
  ZIP_CDIR_FILE cdir_file;

  HQASSERT((p_zipdev != NULL),
           "zdv_read_directory: NULL context pointer");

  HQASSERT((zar_streamed(&p_zipdev->archive)),
           "zdv_read_directory: not streamed input");

  /* Read a file header ... */
  if ( !zar_read_cdir_file(&p_zipdev->archive, &cdir_file, filename, extras) ) {
    return(FALSE);
  }

  /* Read in all the central directory file headers */
  for ( ;; ) {
    /* Read the next sig */
    if ( !zar_next_sig(&p_zipdev->archive, &sig) ) {
      /* Did not find a recognised signature before the end of the archive - awooga */
      return(FALSE);
    }

    switch ( sig ) {
    case PKSIG_CDIR_FILE:
      /* Read a file header */
      if ( !zar_read_cdir_file(&p_zipdev->archive, &cdir_file, filename, extras) ) {
        return(FALSE);
      }
      break;

    case PKSIG_ZIP64_ENDCDIR:
      /* Read ZIP64 end cdir record */
      if ( !zar_read_zip64_end_cdir(&p_zipdev->archive, &z64_end_cdir) ) {
        return(FALSE);
      }
      break;

    case PKSIG_ZIP64_LOCATOR:
      /* Read ZIP64 locator record */
      if ( !zar_read_zip64_cdir_locator(&p_zipdev->archive, &z64_cdir_loc) ) {
        return(FALSE);
      }
      break;

    case PKSIG_ENDCDIR:
      /* Read end cdir record */
      if ( !zar_read_end_cdir(&p_zipdev->archive, &end_cdir) ) {
        return(FALSE);
      }
      return(TRUE);

    case PKSIG_LCAL_FILE:
    case PKSIG_DATADESC:
      /* This function should have been called after seeing the first record of
       * the central directory, and local file records should then not appear.
       * However, there should be no problem continuing to read the central
       * directory, ignoring these records.
       */
      HQFAIL("zdv_read_directory_streamed: unexpected record signature.");
      break;

    default:
      HQFAIL("zdv_read_directory_streamed: unknown record signature.");
      break;
    }
  }

  /* never reached */

} /* zdv_read_directory_streamed */

/**
 * \brief Purge the ZIP device of all open streams and files, and close the
 * archive.
 *
 * The list of open file streams are aborted, all files in the hashtable are
 * deleted, and the archive file is closed.
 *
 * \param[in] p_zipdev
 * Pointer to ZIP device.
 * \param[in] flag
 * PostScript file close flag to use when closing the archive file.
 *
 * \return
 * \c 0 if the archive file is closed successfully, else \c -1.
 */
static
int32 zdv_close_archive(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE*   p_zipdev,
  int32         flag)
{
  int32     i_list;
  int32     status = TRUE;

  HQASSERT((p_zipdev != NULL),
           "zdv_close_archive: NULL context pointer");
  HQASSERT((!zar_closed(&p_zipdev->archive)),
           "zdv_close_archive: archive already closed");
  /* When the zip archive is being read from a stream, its not up to
     the ZIP device to close the file. */
  HQASSERT((!zar_streamed(&p_zipdev->archive) || (flag != CLOSE_IMPLICIT)),
           "zdv_close_archive: closing archive implicitly");

  /* Abort any open files on the device */
  zfs_close_list(&p_zipdev->stream_list, TRUE);

  /* Purge all files in the archive from the device */
  for ( i_list = 0; i_list < ZIP_DEVICE_FILENAME_TABLE_SIZE; i_list++ ) {
    zfl_destroy_list(&(p_zipdev->file_list[i_list]));
  }

  /* Remove any closefile hook */
  if ( p_zipdev->archive.flptr != NULL ) {
    theIMyCloseFile(p_zipdev->archive.flptr) = p_zipdev->old_close;
  }
  /* Close the archive */
  if ( (zar_close(&p_zipdev->archive, flag) < 0) || !status ) {
    return(-1);
  }
  return(0);

} /* zdv_close_archive */


/**
 * \brief Hook function to catch the archive file being closed by a \c restore.
 *
 * Since PostScript devices are not subject to save restore the ZIP device needs
 * to know when the archive file is closed by a restore.  This function is
 * inserted into the \c FILELIST pointer for the archive to catch when it is
 * closed by a restore, and the original fcuntion saved in ZIP device so it can
 * be called later.
 *
 * Since the \c FILELIST does not know which ZIP device it is associated with
 * the function searches all the currently mounted ZIP devices to see which one
 * owns the pointer.  Once the correct ZIP device is found it can be closed.
 *
 * \param[in] flptr
 * Pointer of PostScript file being closed.
 * \param[in] flag
 * PostScript file close flag to use when closing the archive file.
 *
 * \return
 * \c 0 if the archive file is closed successfully, else \c -1.
 */
static
int32 zdv_closefile(
/*@in@*/ /*@notnull@*/
  FILELIST*   flptr,
  int32       flag)
{
  ZIP_DEVICE* p_zipdev;

#if 0
  /* When the zip archive is being read from a stream, its not up to
     the ZIP device to close the file. */
  HQASSERT((flag == CLOSE_FORCE),
           "zdv_closefile: archive file closed by other than restore");
#endif

  /* Find zipdevice using the file being closed on a restore */
  p_zipdev = DLL_GET_HEAD(&dls_zipdevs, ZIP_DEVICE, link);
  while ( (p_zipdev != NULL) && (flptr != p_zipdev->archive.flptr) ) {
    p_zipdev = DLL_GET_NEXT(p_zipdev, ZIP_DEVICE, link);
  }

  if ( p_zipdev == NULL ) {
    HQFAIL("zdv_closefile: tried to close unknown archive file");
    /* Punt the file close to try and keep the system going */
    return(theIMyCloseFile(flptr)(flptr, flag));
  }

  /* Close the archive. */
  return(zdv_close_archive(p_zipdev, flag));

} /* zdv_closefile */


/**
 * \brief Open the ZIP device's archive.
 *
 * This function opens the named ZIP archive file and fixes the device parameter
 * values to use when processing the archive.  If the archive is seekable then
 * the central directory is read immediately.
 *
 * The name can be a PS filename or a PS string which will be executed
 * to provide an open file on the operand stack. This open file will
 * be used to read the ZIP archive.
 *
 * This cannot be done in response to trying to open a file in the archive
 * since it calls \c parse_filename() which uses static buffers and cannot be
 * used again until the open has finished.
 *
 * This function also finds the device to extract archive files to.
 *
 * \param[in] p_zipdev
 * Pointer to the ZIP device.
 * \param[in] name
 * Pointer to name of archive to open.
 * \param[in] name_len
 * Length of archive name.
 * \param[in] is_datasource
 * TRUE if the name is a PS string which ought to be executed to provide an
 * open file on the operand stack. FALSE if the name is a PS filename.
 *
 * \return
 * \c TRUE if the archive was opened successfully, else \c FALSE.
 */
static
Bool zdv_open_archive(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE* p_zipdev,
/*@in@*/ /*@notnull@*/
  uint8*      name,
  uint32      name_len,
  Bool        is_datasource)
{
  DEVICELIST* device;
  ZIPDEV_DEVICE_ITERATOR iter;
  ZIP_ARCHIVE* p_archive = NULL;

  HQASSERT((p_zipdev != NULL),
           "zdv_open_archive: NULL context pointer");

  /* Find device to write extracted files to */
  device = zipdev_device_first(&iter);
  if ( device == NULL ) {
    return FAILURE(FALSE);
  }
  zfd_init(&p_zipdev->file_device, device);

  /* Open the archive file */
  if ( name_len == 0 ) {
    /* No archive source for device */
    zar_open_empty(&p_zipdev->archive);

  } else if ( is_datasource ) {
    /* Archive file returned from running PS snippet */
    if ( !zar_open_ps(&p_zipdev->archive, name, name_len,
                      p_zipdev->param[ZIP_PARAM_STREAMED].param.paramval.boolval) ) {
      return(FALSE);
    }

  } else { /* Archive filename given */
    if ( !zar_open_file(&p_zipdev->archive, name, name_len,
                        p_zipdev->param[ZIP_PARAM_STREAMED].param.paramval.boolval) ) {
      return(FALSE);
    }
  }
  p_archive = &p_zipdev->archive;

  if ( name_len > 0 ) {
    /* ZIP archive specified, catch the file being closed with our hook function */
    p_zipdev->old_close = theIMyCloseFile(p_zipdev->archive.flptr);
    theIMyCloseFile(p_zipdev->archive.flptr) = zdv_closefile;
  }

  /* Flag if CRC32s are being calculate and checked */
  if ( p_zipdev->param[ZIP_PARAM_CHECKCRC32].param.paramval.boolval ) {
    p_zipdev->flags |= ZIP_DEVICE_CALCCRC32;
  }
  /* Flag if names are to be case insensitive - needs to be done before reading
   * any file data from the archive.
   */
  if ( p_zipdev->param[ZIP_PARAM_IGNORECASE].param.paramval.boolval ) {
    p_zipdev->flags |= ZIP_DEVICE_CASEINSENSITIVE;
  }
  if ( p_zipdev->param[ZIP_PARAM_OPENPACKAGE].param.paramval.boolval ) {
    p_zipdev->flags |= ZIP_DEVICE_OPENPACKAGE;
  }
  if ( p_zipdev->param[ZIP_PARAM_READONLY].param.paramval.boolval ) {
    p_zipdev->flags |= ZIP_DEVICE_READONLY;
  }

  if ( name_len > 0 ) {
    /* ZIP archive specified */
    if ( !zar_streamed(&p_zipdev->archive) ) {
      if ( zar_complete(&p_zipdev->archive) && !zdv_read_directory(p_zipdev) ) {
        /* Failed to read central directory - looks like the archive is corrupt */
        return(FALSE);
      }

    } else { /* Reading the archive stream-wise - get the first signature */
      return(TRUE);
    }
  }

  return(TRUE);

} /* zdv_open_archive */


/**
 * \brief Add the next logical file in an archive stream to the ZIP device.
 *
 * If the device is at the start or in the middle of extracting a previous
 * archive file it will complete extracting it first.  Any subsequent files that
 * are parts of an existing logical file will be extracted until either a new
 * logical file is found of the central directory encountered.
 *
 * \param[in] p_zipdev
 * Pointer to ZIP device.
 * \param[out] pp_file
 * Pointer to returned logical file pointer, or \c NULL if the central directory
 * is reached.
 *
 * \return
 * \c TRUE if a new logical file or the central directory is found, else \c
 * FALSE.
 */
static
Bool zdv_read_nextfile(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE*   p_zipdev,
/*@out@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE_PTR* pp_file)
{
  uint32      sig;
  ZIP_FILE*   p_file;
  ZIP_LCAL_FILE lcal_file;
  uint8       filename[LONGESTFILENAME];
  uint8       extras[MAXUINT16]; /** \todo - so not happy about this */
  ZIP_FILE_NAME file_name;
  ZIP_FILE_INFO info;

  HQASSERT((p_zipdev != NULL),
           "zdv_read_nextfile: NULL context pointer");
  HQASSERT((zar_opened(&p_zipdev->archive) && zar_streamed(&p_zipdev->archive)),
           "zdv_read_nextfile: called on opened seekable archive - huh?");
  HQASSERT((pp_file != NULL),
           "zdv_read_nextfile: NULL returned file pointer pointer");

  *pp_file = NULL;

  if ( !zar_opened(&p_zipdev->archive) ) {
    /* Archive has not been opened yet */
    return FAILURE(FALSE);
  }

  /* Got a streamed archive so read the next real file in sequence.  Skip any
   * directories.  Stop when reach the central directory (implicit from not
   * being a local file) */

  /* Ensure finished extracting current piece of last file seen in archive */
  p_file = zfc_get_last(&p_zipdev->chain);
  if ( p_file != NULL ) {
    if ( !zfl_extract(p_file) ) {
      return(FALSE);
    }
  }

  for ( ;; ) {
    /* Read the next sig */
    if ( !zar_next_sig(&p_zipdev->archive, &sig) ) {
      /* Did not find a recognised signature before the end of the archive - awooga */
      return(FALSE);
    }

    switch ( sig ) {
    case PKSIG_LCAL_FILE:
      /* Read local file header ... */
      if ( !zar_read_lcal_file(&p_zipdev->archive, &lcal_file, filename, extras) ) {
        return(FALSE);
      }

      /* ... check for any ZIP64 extra field ... */
      zdv_create_info(&lcal_file, 0, extras, &info);

      /* ... and add it to the device */
      file_name.name = filename;
      file_name.namelength = lcal_file.name_len;
      if ( !zdv_add_zipfile(p_zipdev, &info, &file_name, pp_file) ) {
        return(FALSE);
      }
      /* Stop if we have a new file, else continue looking for a new file */
      if ( *pp_file != NULL ) {
        return(TRUE);
      }

      /* Tickle the RIP */
      SwOftenUnsafe();
      break;

    case PKSIG_CDIR_FILE:
      /* Reached the central directory. */
      /** \todo this is the wrong place for this - should only be set
       * when all of the archive data has been read */
      p_zipdev->archive.flags |= ZIP_ARCHIVE_COMPLETE;
      return(zdv_read_directory_streamed(p_zipdev));

    case PKSIG_DATADESC:
    case PKSIG_ENDCDIR:
    case PKSIG_ZIP64_ENDCDIR:
    case PKSIG_ZIP64_LOCATOR:
      /* Shouldn't appear, but ya never know */
      HQFAIL("zdv_read_nextfile: unexpected record signature.");
      break;

    default:
      HQFAIL("zdv_read_nextfile: unknown record signature.");
      break;
    }
  }

  /* never reached */

} /* zdv_read_nextfile */


/**
 * \brief Flush the remaining content of a streamed ZIP archive.
 *
 * \param[in] p_zipdev
 * Pointer to ZIP device.
 *
 * \return
 * \c TRUE if the remainder of the ZIP archive is successfully flushed, else \c
 * FALSE.
 */
static
int32 zdv_flush_archive(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE* p_zipdev)
{
  uint32        sig;
  ZIP_FILE*     p_file;
  uint8         filename[LONGESTFILENAME];
  uint8         extras[MAXUINT16]; /** \todo oo - so not happy about this */
  ZIP_LCAL_FILE lcal_file = {0};
  ZIP_FILE_INFO info;

  HQASSERT((p_zipdev != NULL),
           "zdv_flush_archive: NULL ZIP device pointer");

  /* Catch archive already fully read */
  if ( zar_complete(&p_zipdev->archive) ) {
    return(TRUE);
  }

  /* Flush any file currently being read */
  p_file = zfc_get_last(&p_zipdev->chain);
  if ( p_file != NULL ) {
    if ( !zfl_flush(p_file) ) {
      return(FALSE);
    }
  }

  for ( ;; ) {
    /* Read the next sig */
    if ( !zar_next_sig(&p_zipdev->archive, &sig) ) {
      /* Did not find a recognised signature before the end of the archive - awooga */
      return(FALSE);
    }

    switch ( sig ) {
    case PKSIG_LCAL_FILE:
      /* Read local file header ... */
      if ( !zar_read_lcal_file(&p_zipdev->archive, &lcal_file, filename, extras) ) {
        return(FALSE);
      }

      /* ... check for any ZIP64 extra field ... */
      zdv_create_info(&lcal_file, 0, extras, &info);

      /* ... check it can be extracted ... */
      if ( !ZIP_CAN_EXTRACT(&info) ) {
        return(FALSE);
      }

#ifdef DEBUG_BUILD
      if ( (p_zipdev->param[ZIP_PARAM_DEBUG].param.paramval.intval)&ZIP_DEVICE_DBG_FLUSH_FILES ) {
        uint8 buffer[512];
        swncopyf(buffer, 512, (uint8*)"Flushing file: %.*s\n", lcal_file.name_len, filename);
        monitorf(buffer);
      }
#endif

      /* ... and skip over file data */
      if ( !zfl_skip_lcal_file(&p_zipdev->archive, &info, zdv_checkcrc32(p_zipdev)) ) {
        return(FALSE);
      }

      /* Tickle the RIP */
      SwOftenUnsafe();
      break;

    case PKSIG_CDIR_FILE:
      /* Reached the central directory. */
#ifdef DEBUG_BUILD
      if ( (p_zipdev->param[ZIP_PARAM_DEBUG].param.paramval.intval)&ZIP_DEVICE_DBG_FLUSH_FILES ) {
        monitorf((uint8*)"Flushing central directory...\n");
      }
#endif
      /** \todo this is the wrong place for this - should only be set
       * when all of the archive data has been read */
      p_zipdev->archive.flags |= ZIP_ARCHIVE_COMPLETE;
      return(zdv_read_directory_streamed(p_zipdev));

    case PKSIG_DATADESC:
    case PKSIG_ENDCDIR:
    case PKSIG_ZIP64_ENDCDIR:
    case PKSIG_ZIP64_LOCATOR:
      /* Shouldn't appear, but ya never know */
      HQFAIL("zdv_flush_archive: unexpected record signature.");
      break;

    default:
      HQFAIL("zdv_flush_archive: unknown record signature.");
      break;
    }
  }

  /* never reached */

} /* zdv_flush_archive */

/**
 * \brief Look for a named logical file in a streamed archive, adding it to the
 * device if it is found.
 *
 * This function repeatedly calls \c zdv_read_nextfile() until it either returns
 * a new logical file with the required name or the central directory is
 * reached.
 *
 * \param[in] p_zipdev
 * Pointer to the ZIP device.
 * \param[in] p_name
 * Pointer to the file name to look for.
 * \param[out] pp_file
 * Pointer to returned logical file pointer for named file, or \c NULL if the
 * central directory is reached.
 *
 * \return
 * \c TRUE if the named file is found or the central directory is reached, else
 * \c FALSE.
 */
static
Bool zdv_read_filename(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE*   p_zipdev,
/*@in@*/ /*@notnull@*/
  ZIP_FILE_NAME* p_name,
/*@out@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE_PTR* pp_file)
{
  ZIP_FILE_LIST* p_file_list;

  HQASSERT((p_zipdev != NULL),
           "zdv_read_filename: NULL context pointer");
  HQASSERT((zar_streamed(&p_zipdev->archive)),
           "zdv_read_filename: archive is seekable");
  HQASSERT(((!zar_complete(&p_zipdev->archive))),
           "zdv_read_filename: all of archive has been read");
  HQASSERT((pp_file != NULL),
           "zdv_read_filename: NULL returned file pointer pointer");

  *pp_file = NULL;

  p_file_list = zdv_filename_list(p_zipdev, p_name);

#ifdef INSTRUMENT_ZIP
  monitorf((uint8*)"ZIP: Looking for file: %.*s\n", p_name->namelength,
           p_name->name);
#endif

  /* Keep extracting files until find local file header with matching name, or
   * reach the central directory.
   */
  for ( ;; ) {
    if ( !zdv_read_nextfile(p_zipdev, pp_file) ) {
      return(FALSE);
    }
    if ( *pp_file == NULL ) {
      /* Reached central directory */
      break;
    }
    *pp_file = zfl_find(p_file_list, p_name, zdv_ignorecase(p_zipdev),
                        zdv_normalise(p_zipdev));
    if ( *pp_file != NULL ) {
      /* Found the named file */
      break;
    }
  }

  return(TRUE);

} /* zdv_read_filename */


/**
 * \brief Find the named file in the ZIP device.
 *
 * Since the ZIP device is presenting the archive as a hosted file system there
 * is an implicit root directory - /.  Since archive file names do not include a
 * leading /, it is skipped in the file name to search for if present.
 *
 * \param[in] p_zipdev
 * Pointer to ZIP device.
 * \param[in] p_name
 * Pointer to file name to look for.
 * \param[out] pp_file
 * Pointer to returned logical file pointer. A \c NULL implies the file does not
 * exist.
 *
 * \return
 * \c TRUE if the named file was found or the central directory was reached when
 * processing a streamed archive, else \c FALSE.
 */
static
Bool zdv_find_file(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE*   p_zipdev,
/*@in@*/ /*@notnull@*/
  ZIP_FILE_NAME* p_name,
/*@out@*/ /*@notnull@*/ /*@dependent@*/
  ZIP_FILE_PTR* pp_file)
{
#if defined(METRO)
  uint8 normalised[LONGESTFILENAME] ;
#endif

  HQASSERT((p_zipdev != NULL),
           "zdv_find_file: NULL ZIP context pointer");
  HQASSERT((p_name != NULL),
           "zdv_find_file: NULL filename pointer");

#if defined(METRO)
  if ( zdv_normalise(p_zipdev) ) {
    /* Normalise the file name */
    int32 i ;

    /* We do NOT check that part names asked for match the XPS part
       name grammar. We just lowercase so that comparison is
       case-insensitive. */
    for (i=0; i<p_name->namelength; i++)
      normalised[i] = (uint8)tolower(p_name->name[i]) ;

    p_name->name = normalised ;
  }
#endif

  /* Look for filename in archive hashtable */
  *pp_file = zfl_find(zdv_filename_list(p_zipdev, p_name), p_name,
                      zdv_ignorecase(p_zipdev), zdv_normalise(p_zipdev));
  if ( *pp_file == NULL ) {
    /* Keep reading incomplete archive looking for file name */
    if ( !zar_complete(&p_zipdev->archive) ) {
      return(zdv_read_filename(p_zipdev, p_name, pp_file));
    }
  }

  return(TRUE);

} /* zdv_find_file */


/**
 * \brief Rename a file on the zip device.
 *
 * \param[in] p_zipdev
 * Pointer to ZIP device.
 * \param[in] p_file
 * Pointer to file to rename.
 * \param[in] p_new_name
 * Pointer to new name for file.
 *
 * \return
 * \c TRUE if file renamed successfully, else \c FALSE.
 */
static
Bool zdv_rename_file(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE*   p_zipdev,
/*@in@*/ /*@notnull@*/
  ZIP_FILE*     p_file,
/*@in@*/ /*@notnull@*/
  ZIP_FILE_NAME* p_new_name)
{
/*@dependent@*/ /*@notnull@*/
  ZIP_FILE_LIST*  p_new_file_list;

  HQASSERT((p_file != NULL),
           "zdv_rename_file: NULL file pointer");
  HQASSERT((p_new_name != NULL),
           "zdv_rename_file: NULL new filename pointer");

  /* Find new file list for new name */
  p_new_file_list = zdv_filename_list(p_zipdev, p_new_name);
  return(zfl_rename(p_file, p_new_file_list, p_new_name));

} /* zdv_rename_file */


/**
 * \brief Create new entry for file on ZIP device.
 *
 * \param[in] p_zipdev
 * Pointer to ZIP device.
 * \param[in] filename
 * Name of new logical file.
 * \param[out] pp_file
 * Pointer to returned new ZIP device file.
 *
 * \return
 * \c TRUE if new file is created ok, else \c FALSE.
 */
static
Bool zdv_create_file(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE*     p_zipdev,
/*@in@*/ /*@notnull@*/
  ZIP_FILE_NAME*  filename,
/*@out@*/ /*@notnull@*/
  ZIP_FILE**      pp_file)
{
  uint32          date_time;
/*@dependent@*/ /*@notnull@*/
  ZIP_FILE_LIST*  p_file_list;
/*@dependent@*/ /*@notnull@*/
  ZIP_FILE_NAME   normalised_name;

  HQASSERT((p_zipdev != NULL),
           "zdv_create_file: NULL zip device pointer");
  HQASSERT((filename != NULL),
           "zdv_create_file: NULL filename pointer");

  /** \todo hack to get file writing working */
  normalised_name = *filename;

  date_time = zutl_msdos_date();

  /* Find file list for new file */
  p_file_list = zdv_filename_list(p_zipdev, &normalised_name);

  /* Add file to device */
  if ( !zdv_add_file(p_zipdev, p_file_list, filename, &normalised_name,
                     date_time, ZDV_ADD_NEWFILE, pp_file) ) {
    return(FALSE);
  }

  return(TRUE);

} /* zdv_create_file */


/**
 * \brief Create a new item for a new archive.
 *
 * A whole central directory record is initialised including the data descriptor
 * record which is zeroed as if the archive is being created to a stream.  The
 * data descriptor should be updated before writing the central directory record
 * to the archive.
 *
 * It is assumed that single disk archives will only be created and that all
 * items are flate compressed, there are no extras, and the item has no
 * comment.
 *
 * \param[in] p_file
 * Pointer to ZIP device file that will become the item in the new archive.
 * \param[in] pos
 * Pointer to local file header offset.
 * \param[in] f_streamed
 * New archive is being created as a stream.
 * \param[in] f_zip64
 * Create ZIP64 file item.
 * \param[in] compression
 * Compression to use on new item file data in archive.
 *
 * \return
 * Pointer to new archive item or \c NULL if failed to create one.
 */
static
ZIP_ARCHIVE_ITEM* zdv_new_item(
/*@in@*/ /*@notnull@*/
  ZIP_FILE*     p_file,
/*@in@*/ /*@notnull@*/
  Hq32x2*       pos,
  Bool          f_streamed,
  Bool          f_zip64,
  int32         compression)
{
  uint32            date_time;
  ZIP_ARCHIVE_ITEM* p_item;

  HQASSERT((p_file != NULL),
           "zdv_new_item: NULL file pointer");
  HQASSERT(((compression == ZIPCOMP_STORED) || (compression == ZIPCOMP_DEFLATE)),
           "zdv_new_item: invalid file data compression");

  /* Allocate memory for item */
  p_item = mm_alloc(mm_pool_temp, sizeof(ZIP_ARCHIVE_ITEM), MM_ALLOC_CLASS_ZIP_ITEM);
  if ( p_item == NULL ) {
    return(NULL);
  }

  NAME_OBJECT(p_item, ZIP_ARCHIVE_ITEM_OBJECT_NAME);

  /* Initialise archive item */
  DLL_RESET_LINK(p_item, link);

  p_item->p_file = p_file;
  zfl_name(p_file, &p_item->filename);
  p_item->lcal_pos = *pos;
  p_item->is_zip64 = FALSE;

  /* Fill in central directory file header */
  p_item->cdir_file.made_by_version = ZIP_VERSION_MADEBY(ZIP_COMPAT_MSDOS, ZIP_VERSION(2, 0));

  p_item->cdir_file.lcal_file.version_needed = ZIP_VERSION(2, 0);
  p_item->cdir_file.lcal_file.flags = 0;
  if ( f_streamed ) {
    p_item->cdir_file.lcal_file.flags |= ZIPFLG_USE_DATADESC;
  }
  p_item->cdir_file.lcal_file.compression = CAST_UNSIGNED_TO_UINT16(compression);
  date_time = zfl_datetime(p_item->p_file);
  p_item->cdir_file.lcal_file.mod_time = CAST_UNSIGNED_TO_UINT16(date_time&0xffff);
  p_item->cdir_file.lcal_file.mod_date = CAST_UNSIGNED_TO_UINT16(date_time >> 16);
  p_item->cdir_file.lcal_file.data_desc.crc_32 = 0;
  p_item->cdir_file.lcal_file.data_desc.compressed = 0;
  p_item->cdir_file.lcal_file.data_desc.uncompressed_size = 0;
  p_item->cdir_file.lcal_file.name_len = CAST_UNSIGNED_TO_UINT16(p_item->filename.namelength);
  p_item->cdir_file.lcal_file.extras_len = 0;

  p_item->cdir_file.comment_len = 0;
  p_item->cdir_file.start_disk_number = 0;
  p_item->cdir_file.internal_attributes = 0;
  p_item->cdir_file.external_attributes = 0;
  Hq32x2ToUint32(&p_item->lcal_pos, &p_item->cdir_file.lcal_file_hdr_offset);

  if ( f_zip64 ) {
    /* Update local file header fields for ZIP64 */
    p_item->is_zip64 = TRUE;

    p_item->cdir_file.made_by_version = ZIP_VERSION_MADEBY(ZIP_COMPAT_MSDOS, ZIP_VERSION(4, 5));

    p_item->cdir_file.lcal_file.version_needed = ZIP_VERSION(4, 5);
    p_item->cdir_file.lcal_file.data_desc.compressed = ZIP_USE_ZIP64FLD_LONG;
    p_item->cdir_file.lcal_file.data_desc.uncompressed_size = ZIP_USE_ZIP64FLD_LONG;

    p_item->cdir_file.lcal_file.extras_len = ZAR_XTRAFLD_ZIP64SIZE;

    if ( Hq32x2CompareUint32(&p_item->lcal_pos, MAXUINT32) >= 0) {
      /* Redirect file offset if it wont fit */
      p_item->cdir_file.lcal_file_hdr_offset = ZIP_USE_ZIP64FLD_LONG;
    }

    /* Initialise ZIP64 extra field record */
    p_item->z64_xtrafld.header.header_id = ZAR_EXTRA_ID_ZIP64;
    p_item->z64_xtrafld.header.data_size = ZAR_XTRAFLD_ZIP64SIZE - ZAR_XTRAFLD_HDRSIZE;

    HqU32x2FromUint32(&p_item->z64_xtrafld.uncompressed_size, 0);
    HqU32x2FromUint32(&p_item->z64_xtrafld.compressed, 0);
    HqU32x2From32x2(&p_item->z64_xtrafld.lcal_file_hdr_offset, &p_item->lcal_pos);
    p_item->z64_xtrafld.start_disk_number = 0;
    p_item->z64_xtrafld.present = ZIP64_ALLPRESENT;
  }

  return(p_item);

} /* zdv_new_item */


/**
 * \brief Free an item.
 *
 * \param[in] p_item
 * Pointer to archive item record to free.
 */
static
void zdv_free_item(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE_ITEM* p_item)
{
  HQASSERT((p_item != NULL),
           "zdv_free_item: NULL item pointer.");
  HQASSERT((DLL_IN_LIST(p_item, link)),
           "zdv_free_item: item not in list");

  VERIFY_OBJECT(p_item, ZIP_ARCHIVE_ITEM_OBJECT_NAME);

  /* Remove from list of archive items before freeing */
  DLL_REMOVE(p_item, link);

  UNNAME_OBJECT(p_item);

  mm_free(mm_pool_temp, p_item, sizeof(ZIP_ARCHIVE_ITEM));

} /* zdv_free_item */


/**
 * \brief Write archive item data to the archive.
 *
 * \param[in] p_reader
 * Pointer to data reader.
 * \param[in] p_item
 * Pointer to item to write data to archive for.
 * \param[in] archive
 * Archive to write item data to.
 *
 * \return
 * \c TRUE if item data successfully written to the archive, else \c FALSE.
 */
static
Bool zdv_write_file_data(
/*@in@*/ /*@notnull@*/
  ZIP_FILE_READER*  p_reader,
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE_ITEM* p_item,
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*      archive)
{
  Bool  status = TRUE;
#define BUFFER_SIZE (16384)
  uint8 buffer[BUFFER_SIZE];
  int32 bytes_read;

  /* Open new file to read data from */
  if ( !zfr_open(p_reader, p_item->p_file, p_item->cdir_file.lcal_file.compression) ) {
    return(FALSE);
  }

  /* Write compressed file data to new archive */
  do {
    bytes_read = zfr_read_data(p_reader, buffer, BUFFER_SIZE);
    if ( (bytes_read < 0) ||
         ((bytes_read > 0) &&
          (zar_write_raw(archive, buffer, bytes_read) != bytes_read)) ) {
      status = FALSE;
      break;
    }
    /* Tickle the RIP */
    SwOftenUnsafe();
  } while ( bytes_read > 0 );

  HQASSERT(((bytes_read == 0) || !status),
           "zdv_write_file_data: invalid break condition writing item data");

  /* Close file data reader */
  zfr_close(p_reader);

  return(status);

} /* zdv_write_file_data */


/**
 * \brief Add ZIP device files as new archive items.
 *
 * \param[in] archive
 * Archive to write items to.
 * \param[in] f_streamed
 * Create the archive stream wise (use data descriptors).
 * \param[in] p_chain
 * Pointer to start of file chain on ZIP device.
 * \param[in] f_all_zip64
 * Make all file items ZIP64.
 * \param[out] item_list
 * Pointer to list of archive items added.
 * \param[out] c_items
 * Number of archive items in the list.
 *
 * \return
 * \c TRUE if all items are added to the archive, else \c FALSE.
 */
static
Bool zdv_add_items(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  archive,
  Bool          f_streamed,
/*@in@*/ /*@notnull@*/
  ZIP_FILE_CHAIN* p_chain,
  Bool          f_all_zip64,
/*@out@*/ /*@notnull@*/
  dll_list_t*   item_list,
/*@out@*/ /*@notnull@*/
  HqU32x2*      c_items)
{
  uint8       extras[ZAR_XTRAFLD_ZIP64SIZE];
  int32       status;
  int32       compression;
  ZIP_FILE*   p_file;
  ZIP_FILE_READER* p_reader;
  ZIP_ARCHIVE_ITEM* p_item;
  HqU32x2     extent;
  Hq32x2      pos;
  Bool        f_zip64;

  HQASSERT((item_list != NULL),
           "zdv_add_items: NULL item list pointer");
  HQASSERT((archive != NULL),
           "zdv_add_items: NULL archive pointer");

  /* Get a file reader */
  p_reader = zfr_new();
  if ( p_reader == NULL ) {
    return FAILURE(FALSE);
  }

  status = TRUE;
  HqU32x2FromUint32(c_items, 0);

  /* First write out all files as local file headers and compressed data */
  p_file = zfc_get_first(p_chain);
  while ( p_file != NULL ) {
    /* Get position of local file header in new archive */
    if ( !zar_get_pos(archive, &pos) ) {
      status = FALSE;
      break;
    }

    /* For now derive compression from size of file */
    /** \todo fine tune lower limit for when to apply STORE */
    zfl_size(p_file, &extent);
    compression = (!HqU32x2IsZero(&extent)) ? ZIPCOMP_DEFLATE : ZIPCOMP_STORED;
    /* Check if need to create a ZIP64 item */
    /** \todo cope with compressed being > 4GB */
    f_zip64 = f_all_zip64 ||
                (Hq32x2CompareUint32(&pos, MAXUINT32) > 0) ||
                (HqU32x2CompareUint32(&extent, MAXUINT32) > 0);

    /* Create new item for file and add to list for new archive */
    p_item = zdv_new_item(p_file, &pos, f_streamed, f_zip64, compression);
    if ( p_item == NULL ) {
      status = FALSE;
      break;
    }
    DLL_ADD_TAIL(item_list, p_item, link);

    /* Increase count of items. */
    HqU32x2AddUint32(c_items, c_items, 1);

    /* If ZIP64 item, generate ZIP64 extra field */
    if ( p_item->is_zip64 ) {
      zar_create_xtrafld_zip64(extras, &p_item->z64_xtrafld);
    }
    /* Write local file header and data to the archive */
    if ( !zar_write_lcal_file(archive, &p_item->cdir_file.lcal_file, p_item->filename.name, extras) ||
         !zdv_write_file_data(p_reader, p_item, archive) ) {
      status = FALSE;
      break;
    }

    if ( p_item->is_zip64 ) {
      /* ZIP64 item */
      if ( f_streamed ) {
        /* Append the data descriptor */
        zfr_z64_data_desc(p_reader, &p_item->z64_data_desc);
        if ( !zar_write_zip64_data_desc(archive, &p_item->z64_data_desc) ) {
          status = FALSE;
          break;
        }
      } else { /* Update CRC in local file header */
        if ( !zar_update_data_desc_crc32(archive, &p_item->lcal_pos, zfr_data_desc_crc32(p_reader)) ) {
          status = FALSE;
          break;
        }
        /* Update ZIP64 extra field (assumes no other extra fields present) */
        zfr_z64_xtrafld(p_reader, &p_item->z64_xtrafld);
        zar_create_xtrafld_zip64(extras, &p_item->z64_xtrafld);
        if ( !zar_update_extras(archive, &p_item->lcal_pos, &p_item->cdir_file.lcal_file, extras) ) {
          status = FALSE;
          break;
        }
      }

    } else { /* Plain ol' ZIP item */
      if ( !zfr_data_desc(p_reader, &p_item->cdir_file.lcal_file.data_desc) ) {
        /* Failure means compressed data was > 4GB */
        status = FALSE;
        break;
      }
      if ( f_streamed ) {
        /* Append the data descriptor */
        if ( !zar_write_data_desc(archive, &p_item->cdir_file.lcal_file.data_desc) ) {
          status = FALSE;
          break;
        }
      } else { /* Update local file header data descriptor fields */
        if ( !zar_update_data_desc(archive, &p_item->lcal_pos, &p_item->cdir_file.lcal_file.data_desc) ) {
          status = FALSE;
          break;
        }
      }
    }

    /* Get next file on device */
    p_file = zfc_get_next(p_chain, p_file);
  }

  /* Must free file data reader */
  zfr_free(p_reader);

  return(status);

} /* zdv_add_items */


/**
 * \brief Add central directory records for archive items.
 *
 * The end of central directory record is updated with the offset of the first
 * central directory record, and the total number of entries in the central
 * directory.
 *
 * \param[in] archive
 * Archive to write central directory records to.
 * \param[in] item_list
 * Pointer to list of archive items to add central directory records for.
 * \param[out] p_cdir_offset
 * Pointer to returned start of central directory offset.
 *
 * \return
 * \c TRUE if all central directory records are written to the archive
 * successfully, else \c FALSE.
 */
static
Bool zdv_add_cdir(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  archive,
/*@in@*/ /*@notnull@*/
  dll_list_t*   item_list,
/*@out@*/ /*@notnull@*/
  HqU32x2*      p_cdir_offset)
{
  uint8   extras[ZAR_XTRAFLD_ZIP64SIZE];
  ZIP_ARCHIVE_ITEM* p_item;
  Hq32x2 pos;

  HQASSERT((item_list != NULL),
           "zdv_add_cdir: NULL item list pointer");

  /* Note where central directory starts */
  if ( !zar_get_pos(archive, &pos) )
    return FALSE;
  HqU32x2From32x2(p_cdir_offset, &pos);

  /* Write out central directory header record for each item */
  p_item = DLL_GET_HEAD(item_list, ZIP_ARCHIVE_ITEM, link);
  while ( p_item != NULL ) {
    if ( p_item->is_zip64 ) {
      zar_create_xtrafld_zip64(extras, &p_item->z64_xtrafld);
    }
    if ( !zar_write_cdir_file(archive, &p_item->cdir_file, p_item->filename.name, extras) ) {
      return(FALSE);
    }

    /* Tickle the RIP - could be thousands of files */
    SwOftenUnsafe();

    p_item = DLL_GET_NEXT(p_item, ZIP_ARCHIVE_ITEM, link);
  }

  return(TRUE);

} /* zdv_add_cdir */


/**
 * \brief Add central directory end record to archive.
 *
 * \param[in] archive
 * Archive to write central directory end record to.
 * \param[in] c_items
 * Number of items add to the archive.
 * \param[out] p_cdir_offset
 * Pointer to start of central directory offset.
 *
 * \return
 * \c TRUE if all central directory end record was written to the archive
 * successfully, else \c FALSE.
 */
static
Bool zdv_add_cdir_end(
/*@in@*/ /*@notnull@*/
  ZIP_ARCHIVE*  archive,
/*@in@*/ /*@notnull@*/
  HqU32x2*      c_items,
/*@in@*/ /*@notnull@*/
  HqU32x2*      p_cdir_offset)
{
  ZIP_END_CDIR  end_cdir;
  ZIP_ZIP64_END_CDIR  z64_end_cdir;
  ZIP_ZIP64_CDIR_LOC  z64_cdir_loc;
  HqU32x2 cdir_end_offset;
  HqU32x2 cdir_size;
  Hq32x2 pos;
  uint32  temp32;

  HQASSERT((c_items != NULL),
           "zdv_add_cdir_end: NULL pointer to item count");
  HQASSERT((p_cdir_offset != NULL),
           "zdv_add_cdir_end: NULL pointer to cdir start offset");

  /* Calculate size of central directory */
  if ( !zar_get_pos(archive, &pos) )
    return FALSE;
  HqU32x2From32x2(&cdir_end_offset, &pos);
  HqU32x2Subtract(&cdir_size, &cdir_end_offset, p_cdir_offset);

  if ( (HqU32x2CompareUint32(c_items, MAXUINT16) > 0) ||
       (HqU32x2CompareUint32(p_cdir_offset, MAXUINT32) > 0) ||
       (HqU32x2CompareUint32(&cdir_size, MAXUINT32) > 0) ) {
    /* Create a ZIP64 end of central directory and locator */
    HqU32x2FromUint32(&z64_end_cdir.end_cdir_size, ZAR_ZIP64_ENDCDIR_RECSIZE);
    z64_end_cdir.made_by_version = ZIP_VERSION_MADEBY(ZIP_COMPAT_MSDOS, ZIP_VERSION(4, 5));
    z64_end_cdir.version_needed = ZIP_VERSION(4, 5);
    z64_end_cdir.this_disk_number = 0;
    z64_end_cdir.start_disk_number = 0;
    z64_end_cdir.total_cdir_entries_this_disk = *c_items;
    z64_end_cdir.total_cdir_entries = z64_end_cdir.total_cdir_entries_this_disk;
    z64_end_cdir.cdir_size = cdir_size;
    z64_end_cdir.cdir_start_offset = *p_cdir_offset;

    if ( !zar_write_zip64_end_cdir(archive, &z64_end_cdir) ) {
      return(FALSE);
    }

    /* Create a ZIP64 end of central directory locator */
    z64_cdir_loc.start_disk_number = 0;
    z64_cdir_loc.zip64_end_cdir_offset = cdir_end_offset;
    z64_cdir_loc.total_disks = 1;
    if ( !zar_write_zip64_cdir_locator(archive, &z64_cdir_loc) ){
      return(FALSE);
    }

    /* Special end central directory record as using ZIP64 extensions */
    end_cdir.cdir_entries_total = end_cdir.cdir_entries_thisdisk = ZIP_USE_ZIP64FLD_SHORT;
    end_cdir.cdir_offset = ZIP_USE_ZIP64FLD_LONG;
    end_cdir.cdir_size = ZIP_USE_ZIP64FLD_LONG;

  } else { /* Fill in plain ol' ZIP end central directory record */
    HqU32x2ToUint32(c_items, &temp32);
    end_cdir.cdir_entries_total = end_cdir.cdir_entries_thisdisk = CAST_UNSIGNED_TO_UINT16(temp32);
    HqU32x2ToUint32(&cdir_size, &end_cdir.cdir_size);
    HqU32x2ToUint32(p_cdir_offset, &end_cdir.cdir_offset);
  }

  end_cdir.disknum = 0;
  end_cdir.cdir_start_disknum = 0;
  end_cdir.comment_length = 0;

  return(zar_write_end_cdir(archive, &end_cdir));

} /* zdv_add_cdir_end */


/**
 * \brief Create a new archive from the files on the ZIP device.
 *
 * Note it is possible to create an archive with no items in it!
 *
 * \param[in] p_zipdev
 * Pointer to ZIP device to create new archive from.
 * \param[in] name
 * Pointer to name of archive to create.
 * \param[in] name_len
 * Length of archive name
 *
 * \return
 * \c TRUE if new archive is created succesfully, else \c FALSE.
 */
static
Bool zdv_create_archive(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE* p_zipdev,
/*@in@*/ /*@notnull@*/
  uint8*      name,
  uint32      name_len)
{
  Bool        f_streamed;
  Bool        status = FALSE;
  ZIP_ARCHIVE_ITEM* p_item;
  ZIP_ARCHIVE archive;
  dll_list_t  items;
  HqU32x2     c_items;
  HqU32x2     cdir_offset;

  HQASSERT((p_zipdev != NULL),
           "zdv_create_archive: NULL device pointer");

  if ( zdv_readonly(p_zipdev) || !zar_complete(&p_zipdev->archive) ||
       !zfs_isempty(&p_zipdev->stream_list)) {
    /* Error making archive from readonly device, incomplete archive, or files
     * currently open */
    return(FALSE);
  }

  /* Gate device to prevent modifications to set of files */
  p_zipdev->flags |= ZIP_DEVICE_CREATING;

  /* Create the new archive file */
  zar_init(&archive);
  if ( zar_open_create(&archive, name, name_len) ) {
    /* Initialise new archive item list */
    DLL_RESET_LIST(&items);

    /* Pick up if ZIP should be stream created */
    f_streamed = p_zipdev->param[ZIP_PARAM_STREAMED].param.paramval.boolval;

    if ( p_zipdev->param[ZIP_PARAM_ZIP64FILES].param.paramval.boolval ) {
      /** \todo Should this be set when set or used? */
      p_zipdev->flags |= ZIP_DEVICE_ZIP64FILES;
    } else {
      p_zipdev->flags &= ~ZIP_DEVICE_ZIP64FILES;
    }

    /* Write out items, then central directory, then directory end */
    status = zdv_add_items(&archive, f_streamed, &p_zipdev->chain,
                           zdv_zip64_files(p_zipdev), &items, &c_items) &&
      zdv_add_cdir(&archive, &items, &cdir_offset) &&
      zdv_add_cdir_end(&archive, &c_items, &cdir_offset);

    /* Release item resources */
    p_item = DLL_GET_HEAD(&items, ZIP_ARCHIVE_ITEM, link);
    while ( p_item != NULL ) {
      zdv_free_item(p_item);
      p_item = DLL_GET_HEAD(&items, ZIP_ARCHIVE_ITEM, link);
    }
    /* Ensure new archive is closed */
    status = (zar_close(&archive, CLOSE_EXPLICIT) == 0) && status;
  }

  /* And we are open for business once again */
  p_zipdev->flags &= ~ZIP_DEVICE_CREATING;

  return(status);

} /* zdv_create_archive */

#ifdef DEBUG_BUILD

/**
 * \brief Perform actions when the debug parameter has been set.
 *
 * Some device debug values trigger immediate debug actions so need to be
 * processed when the parameter is set.  This function handles those trigger
 * actions.
 *
 * \param[in] p_zipdev
 * Pointer to ZIP device.
 */
static
void zdv_debug(
/*@in@*/ /*@notnull@*/
  ZIP_DEVICE* p_zipdev)
{
  int32 debug;

  HQASSERT((p_zipdev != NULL),
           "zdv_debug: NULL ZIP device pointer");

  /* Extract and process Debug parameter */
  debug = p_zipdev->param[ZIP_PARAM_DEBUG].param.paramval.intval;

  if ( (debug&ZIP_DEVICE_DBG_OPEN_FILES) != 0 ) {
    zfs_dbg_dump(&p_zipdev->stream_list);
    /* Clear list open flags file so list is not dumped when another debug flag
     * is set */
    debug &= ~ZIP_DEVICE_DBG_OPEN_FILES;
    p_zipdev->param[ZIP_PARAM_DEBUG].param.paramval.intval = debug;
  }

} /* zdv_debug */

#endif /* DEBUG_BUILD */

/* --------------- ZIP device interface --------------- */


/**
 * \brief Initialise a new ZIP device.
 *
 * \param[in] dev
 * Pointer to ZIP device to initialise.
 *
 * \return
 * \c 0 if the device was initialised successfully, else \c -1.
 */
static
int32 RIPCALL zip_device_init(
/*@in@*/ /*@notnull@*/
  DEVICELIST* dev)
{
  int32       i;
  int32       zip_id;
  ZIP_DEVICE* p_zipdev;
  ZIP_DEVICE* p_zipdevT;

  HQASSERT((dev != NULL),
           "zip_device_init: NULL devicelist pointer");

  p_zipdev = (ZIP_DEVICE*)(dev->private_data);

  /* Name object as memory has been allocated - by the RIP */
  NAME_OBJECT(p_zipdev, ZIP_DEVICE_OBJECT_NAME);

  /* Find unused zip id to use.  Devices are held in ascending id order. */
  zip_id = 0;
  p_zipdevT = DLL_GET_HEAD(&dls_zipdevs, ZIP_DEVICE, link);
  while ( (p_zipdevT != NULL) && (zip_id == p_zipdevT->zip_id) ) {
    p_zipdevT = DLL_GET_NEXT(p_zipdevT, ZIP_DEVICE, link);
    zip_id++;
  }
  if ( zip_id > ZIP_DEVICE_MAX_ID ) {
    return(zdv_errorhandler(DeviceLimitCheck, -1));
  }
  HQASSERT(((p_zipdevT == NULL) || (zip_id < p_zipdevT->zip_id)),
           "zip_device_init: active zip device list out of order");
  p_zipdev->zip_id = zip_id;
  /* Add device in sequence */
  DLL_RESET_LINK(p_zipdev, link);
  if ( p_zipdevT == NULL ) {
    DLL_ADD_TAIL(&dls_zipdevs, p_zipdev, link);
  } else {
    DLL_ADD_BEFORE(p_zipdevT, p_zipdev, link);
  }

  /* Initialise ZIP archive - can check status before opening it */
  zar_init(&p_zipdev->archive);

  /* Reset ZIP device flags */
  p_zipdev->flags = 0;

  /* Initialise device filename hash table to be empty */
  for ( i = 0; i < ZIP_DEVICE_FILENAME_TABLE_SIZE; i++ ) {
    zfl_init_list(&p_zipdev->file_list[i]);
  }

  /* Initialise file stream list */
  zfs_init_list(&p_zipdev->stream_list);

  /* Initialise archive file squence id counter */
  p_zipdev->next_file_id = 0;

  /* Initialize lists of all file names and iterators */
  zfc_init(&p_zipdev->chain);
  DLL_RESET_LIST(&p_zipdev->iterators);

  /* Set up default parameter list */
  HqMemCpy(&p_zipdev->param, &default_zip_param, sizeof(default_zip_param));
  p_zipdev->next_param = 0;

  /* Explicitly initialise the parameter values.
   * Device initially will mount readonly, case sensitive, not check CRC32
   * values, and not merge files (XPS specific handling)
   */
  p_zipdev->param[ZIP_PARAM_CHECKCRC32].param.paramval.boolval = FALSE;
  p_zipdev->param[ZIP_PARAM_IGNORECASE].param.paramval.boolval = FALSE;
  p_zipdev->param[ZIP_PARAM_OPENPACKAGE].param.paramval.boolval = FALSE;
  p_zipdev->param[ZIP_PARAM_CLOSE].param.paramval.boolval = FALSE;
  p_zipdev->param[ZIP_PARAM_READONLY].param.paramval.boolval = TRUE;
  p_zipdev->param[ZIP_PARAM_STREAMED].param.paramval.boolval = FALSE;
  p_zipdev->param[ZIP_PARAM_FLUSH].param.paramval.boolval = FALSE;
  p_zipdev->param[ZIP_PARAM_ZIP64FILES].param.paramval.boolval = FALSE;

  /* Initialise device error */
  zdv_errorhandler(DeviceNoError, 0);

  /* Device now initialised and ready to go */
  return(0);

} /* zip_device_init */


/**
 * \brief Open a new file stream on a ZIP device.
 *
 * The ZIP device only supports opening files for reading.
 *
 * \param[in] dev
 * Pointer to ZIP device.
 * \param[in] filename
 * Pointer to name of file to open stream on.
 * \param[in] openflags
 * Open flags.
 *
 * \return
 * A value equal or greater than \c 0 if a stream was successfully opened, else
 * \c -1.
 */
static
DEVICE_FILEDESCRIPTOR RIPCALL zip_open_file(
/*@in@*/ /*@notnull@*/
  DEVICELIST* dev,
/*@in@*/ /*@notnull@*/
  uint8*      filename,
  int32       openflags)
{
  DEVICE_FILEDESCRIPTOR fd;
  Bool        f_new_file;
  ZIP_DEVICE* p_zipdev;
  ZIP_FILE*   p_file = NULL;
  ZIP_FILE_NAME zip_filename;

#ifdef INSTRUMENT_ZIP
  monitorf((uint8*)"ZIP: Opening file: %s\n", filename);
#endif

  HQASSERT((dev != NULL),
           "zip_open_file: NULL device pointer");
  HQASSERT((filename != NULL),
           "zip_open_file: NULL filename pointer");
  HQASSERT(((openflags&SW_APPEND) == 0),
           "zip_open_file: SW_APPEND not supported in the device");

  p_zipdev = (ZIP_DEVICE*)(dev->private_data);

  VERIFY_OBJECT(p_zipdev, ZIP_DEVICE_OBJECT_NAME);

  if ( zar_closed(&p_zipdev->archive) ) {
    /* Can't open file on a closed device */
    return(zdv_errorhandler(DeviceUndefined, -1));
  }
  if ( zdv_creating(p_zipdev) ) {
    /* Can't open file when creating an archive */
    return(zdv_errorhandler(DeviceInvalidAccess, -1));
  }

  /* Check only one open mode is specified */
  switch ( openflags&(SW_RDONLY|SW_WRONLY|SW_RDWR) ) {
  case SW_RDONLY:
    /* Check for inconsistent read only flags */
    if ( openflags&(SW_APPEND|SW_CREAT|SW_TRUNC) ) {
      return(zdv_errorhandler(DeviceInvalidAccess, -1));
    }
    break;

  case SW_WRONLY:
  case SW_RDWR:
    if ( !zdv_readonly(p_zipdev) ) {
      break;
    }
    /*@fallthrough@*/
  default:
    return(zdv_errorhandler(DeviceInvalidAccess, -1));
  }

  /* Create internal filename and look for file on device */
  if ( !zdv_filename(filename, &zip_filename) ) {
    return(zdv_errorhandler(DeviceUndefined, -1));
  }
  if ( !zdv_find_file(p_zipdev, &zip_filename, &p_file) ) {
    return(zdv_errorhandler(DeviceIOError, -1));
  }

  f_new_file = (p_file == NULL);
  if ( f_new_file ) {
    if ( (openflags&SW_RDONLY) != 0 ) {
      /* File doesn't exist when just reading */
      return(zdv_errorhandler(DeviceUndefined, -1));
    } else if ( (openflags&SW_CREAT) == 0 ) {
      /* File doesn't exist when writing but not creating */
      return(zdv_errorhandler(DeviceInvalidAccess, -1));
    }

    /* Create new file on device */
    if ( !zdv_create_file(p_zipdev, &zip_filename, &p_file) ) {
      return(zdv_errorhandler(DeviceUndefined, -1));
    }

  } else {
    if ( (openflags&(SW_CREAT|SW_EXCL)) == (SW_CREAT|SW_EXCL) ) {
      /* File exists but want to exclusively create it */
      return(zdv_errorhandler(DeviceInvalidAccess, -1));
    }
    if ( zfl_is_excl(p_file) ||
         (zfl_is_open(p_file) && ((openflags&SW_EXCL) != 0)) ) {
      /* File is already exclusive, or in use when asking for exclusive. */
      return(zdv_errorhandler(DeviceInvalidAccess, -1));
    }
  }

  /* Open new stream on the file */
  fd = zfs_open(&p_zipdev->stream_list, p_file, openflags);
  if ( fd < 0 ) {
    return(zdv_errorhandler(DeviceIOError, -1));
  }

  /* And return the stream descriptor */
  return(fd);

} /* zip_open_file */


/**
 * \brief Read from a file stream on the ZIP device.
 *
 * Reading from a file in a ZIP archive will cause more file data to be
 * extracted if insufficient has been extracted so far.  It is more efficient to
 * seek to the end of the file stream to force all of the file data to be
 * extracted - see zip_seek_file().
 *
 * \param[in] dev
 * Pointer to ZIP device.
 * \param[in] descriptor
 * File descriptor to read from.
 * \param[in] buff
 * Buffer to read file data into.
 * \param[in] len
 * Amount of data to read.
 *
 * \return
 * The number of bytes succesfully read from the file, or \c -1 if there was an
 * error.
 */
static
int32 RIPCALL zip_read_file(
/*@in@*/ /*@notnull@*/
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR descriptor,
/*@in@*/ /*@notnull@*/
  uint8*      buff,
  int32       len)
{
  int32       bytes;
  ZIP_DEVICE* p_zipdev;
  Bool nextPieceReady = TRUE;
#ifdef INSTRUMENT_ZIP
  ZIP_FILE_NAME filename;
#endif

  HQASSERT((dev != NULL),
           "zip_read_file: NULL device pointer");
  HQASSERT((descriptor >= 0),
           "zip_read_file: invalid descriptor");
  HQASSERT((buff != NULL),
           "zip_read_file: NULL buffer pointer");
  HQASSERT((len > 0),
           "zip_read_file: invalid read length");

  p_zipdev = (ZIP_DEVICE*)(dev->private_data);

  VERIFY_OBJECT(p_zipdev, ZIP_DEVICE_OBJECT_NAME);

  if ( zar_closed(&p_zipdev->archive) ) {
    /* Device has been purged on archive file closing */
    return(zdv_errorhandler(DeviceIOError, -1));
  }

  if (zar_streamed(&p_zipdev->archive)) {
    /* Check that we have the next piece ready. */
    if (! zfs_next_piece_ready(&p_zipdev->stream_list, descriptor, &nextPieceReady))
      return zdv_errorhandler(DeviceIOError, -1);
  }

#ifdef INSTRUMENT_ZIP
  if (! nextPieceReady) {
    zfs_get_name(&p_zipdev->stream_list, descriptor, &filename);
    monitorf((uint8*)"ZIP: Next piece for file: %.*s not ready.\n",
             filename.namelength, filename.name);
  }
#endif

  while (! nextPieceReady) {
    ZIP_FILE_NAME name;
    ZIP_FILE_PTR filePointer;
    zfs_get_name(&p_zipdev->stream_list, descriptor, &name);
    if (! zdv_read_filename(p_zipdev, &name, &filePointer) ||
        filePointer == NULL)
      return zdv_errorhandler(DeviceIOError, -1);

    if (! zfs_next_piece_ready(&p_zipdev->stream_list, descriptor, &nextPieceReady))
      return zdv_errorhandler(DeviceIOError, -1);
  }

  /* Do read on the stream */
  bytes = zfs_read(&p_zipdev->stream_list, descriptor, buff, len);
  if ( bytes < 0 ) {
    return(zdv_errorhandler(DeviceIOError, -1));
  }

#ifdef INSTRUMENT_ZIP
  zfs_get_name(&p_zipdev->stream_list, descriptor, &filename);
  monitorf((uint8*)"ZIP: Read %d bytes from file: %.*s\n", bytes,
           filename.namelength, filename.name);
#endif

  return(bytes);

} /* zip_read_file */


/**
 * \brief Write to a file stream on the ZIP device.
 *
 * Writing to file on a ZIP device is not supported.
 *
 * \param[in] dev
 * Pointer to ZIP device.
 * \param[in] descriptor
 * File descriptor to write to.
 * \param[in] buff
 * Buffer containing data to write.
 * \param[in] len
 * Amount of data in buffer to write.
 *
 * \return
 * The number of bytes succesfully written to the file, or \c -1 if there was an
 * error.
 */
static
int32 RIPCALL zip_write_file(
/*@in@*/ /*@notnull@*/
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR descriptor,
/*@in@*/ /*@notnull@*/
  uint8*      buff,
  int32       len)
{
  int32 bytes;
  ZIP_DEVICE* p_zipdev;

  HQASSERT((dev != NULL),
           "zip_write_file: NULL device pointer");
  HQASSERT((descriptor >= 0),
           "zip_write_file: invalid descriptor");
  HQASSERT((buff != NULL),
           "zip_write_file: NULL buffer pointer");
  HQASSERT((len >= 0),
           "zip_write_file: invalid read length");

  p_zipdev = (ZIP_DEVICE*)(dev->private_data);

  VERIFY_OBJECT(p_zipdev, ZIP_DEVICE_OBJECT_NAME);

  if ( zar_closed(&p_zipdev->archive) ) {
    /* Device has been purged on archive file closing */
    return(zdv_errorhandler(DeviceIOError, -1));
  }

  /* Do write on the stream */
  bytes = zfs_write(&p_zipdev->stream_list, descriptor, buff, len);
  if ( bytes < 0 ) {
    return(zdv_errorhandler(DeviceIOError, -1));
  }

  return(bytes);

} /* zip_write_file */


/**
 * \brief Close file stream on the ZIP device.
 *
 * \param[in] dev
 * Pointer to ZIP device.
 * \param[in] descriptor
 * File descriptor to close.
 *
 * \return
 * \c 0 if the close was successful, else \c -1.
 */
static
int32 RIPCALL zip_close_file(
/*@in@*/ /*@notnull@*/
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR descriptor)
{
  ZIP_DEVICE* p_zipdev;

  HQASSERT((dev != NULL),
           "zip_close_file: NULL device pointer");

  p_zipdev = (ZIP_DEVICE*)(dev->private_data);

#ifdef INSTRUMENT_ZIP
  {
    ZIP_FILE_NAME filename;
    zfs_get_name(&p_zipdev->stream_list, descriptor, &filename);
    monitorf((uint8*)"ZIP: Closing file: %.*s\n", filename.namelength,
             filename.name);
  }
#endif

  VERIFY_OBJECT(p_zipdev, ZIP_DEVICE_OBJECT_NAME);

  return(zfs_close(&p_zipdev->stream_list, descriptor, FALSE));

} /* zip_close_file */


/**
 * \brief Abort file stream on the ZIP device.
 *
 * \param[in] dev
 * Pointer to ZIP device.
 * \param[in] descriptor
 * File descriptor to abort.
 *
 * \return
 * \c 0 if the abort was successful, else \c -1.
 */
static
int32 RIPCALL zip_abort_file(
/*@in@*/ /*@notnull@*/
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR descriptor)
{
  ZIP_DEVICE* p_zipdev;

  HQASSERT((dev != NULL),
           "zip_abort_file: NULL device pointer");

  p_zipdev = (ZIP_DEVICE*)(dev->private_data);

  VERIFY_OBJECT(p_zipdev, ZIP_DEVICE_OBJECT_NAME);

  return(zfs_close(&p_zipdev->stream_list, descriptor, TRUE));

} /* zip_abort_file */


/**
 * \brief Reposition a file stream opened on a ZIP device.
 *
 * Repositioning a file stream beyond the end of what has been extracted so far
 * will cause more file data to be extracted.  Repositioning to the end of the
 * file will cause the whole file to be extracted.
 *
 * \param[in] dev
 * Pointer to ZIP device.
 * \param[in] descriptor
 * File descriptor of stream to reposition.
 * \param[in,out] destn
 * Pointer to file position to set to.
 * \param[in] flags
 * Flags controlling how to seek in the file.
 *
 * \return
 * \c TRUE if the file stream was repositioned, else \c FALSE.
 */
static
Bool RIPCALL zip_seek_file(
/*@in@*/ /*@notnull@*/
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR descriptor,
/*@in@*/ /*@notnull@*/
  Hq32x2*     destn,
  int32       flags)
{
  ZIP_DEVICE* p_zipdev;

  HQASSERT((dev != NULL),
           "zip_seek_file: NULL device pointer");

  p_zipdev = (ZIP_DEVICE*)(dev->private_data);

  VERIFY_OBJECT(p_zipdev, ZIP_DEVICE_OBJECT_NAME);

  if ( zar_closed(&p_zipdev->archive) ) {
    /* Device has been purged on archive file closing */
    return(zdv_errorhandler(DeviceIOError, FALSE));
  }

  /* Do the seek on the file stream */
  if ( !zfs_seek(&p_zipdev->stream_list, descriptor, destn, flags) ) {
    return(zdv_errorhandler(DeviceIOError, FALSE));
  }

  return(TRUE);

} /* zip_seek_file */


/**
 * \brief Return the number of bytes available from the file stream.
 *
 * The number of bytes returned for a file in a streamed ZIP archive that has
 * not been completely extracted may be less than the final file size.
 *
 * \param[in] dev
 * Pointer to ZIP device.
 * \param[in] descriptor
 * File descriptor of stream.
 * \param[out] bytes
 * Pointer to returned number of bytes available.
 * \param[in] reason
 * Flag controlling whether to return remaining bytes, or all bytes.
 *
 * \return
 * \c TRUE if the number of bytes available is known, else \c FALSE.
 */
static
Bool RIPCALL zip_bytes_file(
/*@in@*/ /*@notnull@*/
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR descriptor,
/*@out@*/ /*@notnull@*/
  Hq32x2*     bytes,
  int32       reason)
{
  ZIP_DEVICE* p_zipdev;

  HQASSERT((dev != NULL),
           "zip_bytes_file: NULL device pointer");
  HQASSERT((descriptor >= 0),
           "zip_bytes_file: invalid file descriptor");
  HQASSERT((bytes != NULL),
           "zip_bytes_file: NULL pointer to returned bytes");
  HQASSERT(((reason == SW_BYTES_AVAIL_REL) || (reason == SW_BYTES_TOTAL_ABS)),
           "zip_bytes_file: invalid bytes reason");

  p_zipdev = (ZIP_DEVICE*)(dev->private_data);

  VERIFY_OBJECT(p_zipdev, ZIP_DEVICE_OBJECT_NAME);

  if ( zar_closed(&p_zipdev->archive) ) {
    /* Device has been purged on archive file closing */
    return(zdv_errorhandler(DeviceIOError, FALSE));
  }

  if ( !zfs_bytes_avail(&p_zipdev->stream_list, descriptor, reason, bytes) ) {
    return(zdv_errorhandler(DeviceIOError, FALSE));
  }

  return(TRUE);

} /* zip_bytes_file */


/**
 * \brief Return information for a file on a ZIP device.
 *
 * A ZIP archive by default only stores the last modification time for each file
 * so the same data-time information has to be used for all three file data-time
 * values.
 *
 * Also the date-time value is a combination of DOS format date and time values
 * such that the date-time values of two ZIP archive files will order correctly.
 *
 * \param[in] dev
 * Pointer to ZIP device.
 * \param[in] filename
 * Name of file to get information on.
 * \param[in] statbuff
 * Pointer to returned file information.
 *
 * \return
 * \c 0 if got file information successfully, else \c -1.
 */
static
int32 RIPCALL zip_status_file(
/*@in@*/ /*@notnull@*/
  DEVICELIST* dev,
/*@in@*/ /*@notnull@*/
  uint8*      filename,
/*@out@*/ /*@notnull@*/
  STAT*       statbuff)
{
  ZIP_FILE*   p_file = NULL;
  ZIP_DEVICE* p_zipdev;
  ZIP_FILE_NAME zip_filename;

#ifdef INSTRUMENT_ZIP
  monitorf((uint8*)"ZIP: Stat for file %s\n", filename);
#endif

  HQASSERT((dev != NULL),
           "zip_status_file: NULL device pointer");
  HQASSERT((filename != NULL),
           "zip_status_file: NULL filename pointer");
  HQASSERT((statbuff != NULL),
           "zip_status_file: NULL status pointer");

  p_zipdev = (ZIP_DEVICE*)(dev->private_data);

  VERIFY_OBJECT(p_zipdev, ZIP_DEVICE_OBJECT_NAME);

  if ( zar_closed(&p_zipdev->archive) ) {
    /* Can't stat a file on a close device */
    return(zdv_errorhandler(DeviceInvalidAccess, -1));
  }

  /* Create internal device filename of existing file */
  if ( !zdv_filename(filename, &zip_filename) ) {
    return(zdv_errorhandler(DeviceUndefined, -1));
  }

  /* Find named file in archive. */
  if ( !zdv_find_file(p_zipdev, &zip_filename, &p_file) ) {
    return(zdv_errorhandler(DeviceIOError, -1));
  }
  if ( p_file == NULL ) {
    return(zdv_errorhandler(DeviceUndefined, -1));
  }

  /* Return file status information */
  zfl_size(p_file, &statbuff->bytes);
  statbuff->referenced = statbuff->modified =
    statbuff->created = zfl_datetime(p_file);

  return(0);

} /* zip_status_file */


/**
 * \brief Initialise ZIP device filename iterator.
 *
 * See zip_next_file() and zip_end_file_list().
 *
 * \param[in] dev
 * Pointer to ZIP device.
 * \param[in] pattern
 * Pointer to filename matching pattern.
 *
 * \return
 * Opaque handle to device filename iterator, or NULL if device is not open or
 * an iterator could not be created, in which case the device last error is set
 * to \c DeviceInvalidAccess or \c DeviceIOError respectively.
 */
static /*@observer@*/ /*@null@*/
void* RIPCALL zip_start_file_list(
/*@in@*/ /*@notnull@*/
  DEVICELIST* dev,
/*@in@*/ /*@notnull@*/
  uint8*      pattern)
{
  ZIP_DEVICE*       p_zipdev;
  ZIP_DEVICE_ITER*  p_iter;

#ifdef INSTRUMENT_ZIP
  monitorf((uint8*)"ZIP: Starting a file list.\n");
#endif

  UNUSED_PARAM(uint8*, pattern);

  HQASSERT((dev != NULL),
           "zip_start_file_list: NULL device pointer");

  p_zipdev = (ZIP_DEVICE*)(dev->private_data);

  VERIFY_OBJECT(p_zipdev, ZIP_DEVICE_OBJECT_NAME);

  /* RIP checks device last error when NULL returned - DeviceNoError means not
   * an error, e.g. all files have been deleted from the device, anything else
   * will raise an error.
   */
  zdv_errorhandler(DeviceNoError, 0);
  p_iter = NULL;
  if ( zar_opened(&p_zipdev->archive) ) {
    p_iter = zdv_new_iterator(p_zipdev);
    if ( p_iter == NULL ) {
      zdv_errorhandler(DeviceIOError, 0);
    }
  }

  return((void*)p_iter);

} /* zip_start_file_list */


/**
 * \brief Get next filename on ZIP device that matches the pattern.
 *
 * If there are no more file names to match and not all of the archive files
 * have been read in, then read in more files until either a match is found, or
 * all the files have been read in.
 *
 * Any leading slash in the pattern is skipped over since ZIP files cannot have
 * them.  It is not an error to start a pattern with /, nor will it prevent
 * matching any files with patterns such as / followed by *.
 *
 * See zip_start_file_list() and zip_end_file_list().
 *
 * \param[in] dev
 * Pointer to ZIP device.
 * \param[in] handle
 * Pointer to opaque filename iterator handle.
 * \param[in] pattern
 * Filename pattern.
 * \param[out] entry
 * Returned file name that matches the pattern.
 *
 * \return
 * One of the following:
 * - \c FileNameMatch if a file name matches the pattern,
 * - \c FileNameNoMatch if there are no more files that match the pattern, or
 * - \c FileNameError if there was an error getting the name of a file.
 * .
 */
static
int32 RIPCALL zip_next_file(
/*@in@*/ /*@notnull@*/
  DEVICELIST* dev,
/*@in@*/ /*@notnull@*/
  void**      handle,
/*@in@*/ /*@notnull@*/
  uint8*      pattern,
/*@out@*/ /*@notnull@*/
  FILEENTRY*  entry)
{
  int32       pattern_len;
  ZIP_FILE*   p_file;
  ZIP_DEVICE* p_zipdev;
  ZIP_DEVICE_ITER* p_iterator;

  /* Note that if zip_start_file_list() returns NULL, this function
     will not be called. */
  HQASSERT((dev != NULL),
           "zip_next_file: NULL device pointer");
  HQASSERT((handle != NULL),
           "zip_next_file: NULL handle pointer");
  HQASSERT((pattern != NULL),
           "zip_next_file: NULL pattern pointer");
  HQASSERT((entry != NULL),
           "zip_next_file: NULL returned entry pointer");

  p_zipdev = (ZIP_DEVICE*)(dev->private_data);

  VERIFY_OBJECT(p_zipdev, ZIP_DEVICE_OBJECT_NAME);

  if ( zar_closed(&p_zipdev->archive) ) {
    /* Device was closed during filename iteration */
    return(zdv_errorhandler(DeviceInvalidAccess, FileNameError));
  }

  /* Skip any leading slash */
  if ( *pattern == '/' ) {
    pattern++;
  }
  pattern_len = strlen_int32((char*)pattern);

  p_iterator = *(ZIP_DEVICE_ITER**)handle;
  p_file = zdv_iterator_current(p_iterator);
  for (;;) {
    if ( p_file == NULL ) {
      /* Haven't seen all the archive files yet, read next file */
      if ( zar_opened(&p_zipdev->archive) && !zar_complete(&p_zipdev->archive) ) {
        if ( !zdv_read_nextfile(p_zipdev, &p_file) ) {
          return(zdv_errorhandler(DeviceIOError, FileNameError));
        }
      }

      /* No more files in the archive - no more matches possible */
      if ( p_file == NULL ) {
        return(FileNameNoMatch);
      }
    }

    /* Get name of file before getting pointer to next file */
    /** \todo - should this be supported based on canonical?  Also doesnt handle ignorecase! */
    zfl_name(p_file, entry);
    p_file = zdv_iterator_next(p_zipdev, p_iterator);
    if ( SwLengthPatternMatch(pattern, pattern_len, entry->name, entry->namelength) ) {
      return(FileNameMatch);
    }
  }

  /* NEVER REACHED */

} /* zip_next_file */


/**
 * \brief Terminate iterating over ZIP device file names.
 *
 * See zip_start_file_list() and zip_next_file().
 *
 * \param[in] dev
 * Pointer to ZIP device.
 * \param[in] handle
 * Pointer to opaque file iterator handle.
 *
 * \return
 * \c 0 if iterator terminated successfully, else \c -1.
 */
static
int32 RIPCALL zip_end_file_list(
/*@in@*/ /*@notnull@*/
  DEVICELIST* dev,
/*@in@*/ /*@notnull@*/
  void*       handle)
{
  ZIP_DEVICE* p_zipdev;
  ZIP_DEVICE_ITER* p_iterator;

  /* Note that if zip_start_file_list() returns NULL, this function
     will not be called. */
  HQASSERT((dev != NULL),
           "zip_next_file: NULL device pointer");
  HQASSERT((handle != NULL),
           "zip_next_file: NULL iterator handler pointer");

  p_zipdev = (ZIP_DEVICE*)(dev->private_data);

  VERIFY_OBJECT(p_zipdev, ZIP_DEVICE_OBJECT_NAME);

  /* Free off iterator context */
  p_iterator = (ZIP_DEVICE_ITER*)handle;
  zdv_free_iterator(p_iterator);

  return(0);

} /* zip_end_file_list */


/**
 * \brief Rename a file on a ZIP device.
 *
 * Renaming of files is only supported on non read-only devices, where the file
 * is not currently open, and no file with the new name already exists.
 *
 * \param[in] dev
 * Pointer to ZIP device.
 * \param[in] file1
 * Name of existing file.
 * \param[in] file2
 * New name for existing file.
 *
 * \return
 * \c 0 if the file is renamed successfully, else \c -1.
 */
static
int32 RIPCALL zip_rename_file(
/*@in@*/ /*@notnull@*/
  DEVICELIST* dev,
/*@in@*/ /*@notnull@*/
  uint8*      file1,
/*@in@*/ /*@notnull@*/
  uint8*      file2)
{
  ZIP_DEVICE* p_zipdev;
  ZIP_FILE*   p_file = NULL;
  ZIP_FILE*   p_new_file;
  ZIP_FILE_NAME zip_file;

  HQASSERT((dev != NULL),
           "zip_rename_file: NULL device pointer");

  p_zipdev = (ZIP_DEVICE*)(dev->private_data);

  VERIFY_OBJECT(p_zipdev, ZIP_DEVICE_OBJECT_NAME);

  /* file1 does not exist */
  if ( zar_closed(&p_zipdev->archive) ) {
    return(zdv_errorhandler(DeviceUndefined, -1));
  }
  if ( zdv_readonly(p_zipdev) || zdv_creating(p_zipdev) ) {
    /* Can't rename on a RO device or when creating an archive. */
    return(zdv_errorhandler(DeviceInvalidAccess, -1));
  }

  /* Empty internal filenames are treated as undefinedfilename. */
  if ( !zdv_filename(file1, &zip_file) ) {
    return(zdv_errorhandler(DeviceUndefined, -1));
  }

  /* See if file to rename exists */
  if ( !zdv_find_file(p_zipdev, &zip_file, &p_file) ) {
    return(zdv_errorhandler(DeviceIOError, -1));
  }
  if ( p_file == NULL ) {
    return(zdv_errorhandler(DeviceUndefined, -1));
  }

  /* Can't rename file if currently open. */
  if ( zfl_is_open(p_file) ) {
    return(zdv_errorhandler(DeviceInvalidAccess, -1));
  }

  /* Empty internal filenames are treated as undefinedfilename. */
  if ( !zdv_filename(file2, &zip_file) ) {
    return(zdv_errorhandler(DeviceUndefined, -1));
  }

  /* See if new filename already exists */
  p_new_file = p_file;
  if ( !zdv_find_file(p_zipdev, &zip_file, &p_new_file) ) {
    return(zdv_errorhandler(DeviceIOError, -1));
  }
  if ( p_new_file == p_file ) {
    /* Handle renaming a file to itself as ok */
    return(0);
  }
  if ( p_new_file != NULL ) {
    return(zdv_errorhandler(DeviceInvalidAccess, -1));
  }

#if defined(METRO)
  /* Can't think of any reason why you would want to rename a file on a ZIP
   * device setup for reading XPS jobs.  If it is decided it is needed then need
   * to add support for normalising the new filename.
   */
  HQTRACE(zdv_normalise(p_zipdev),
          ("zip_rename_file: unexpectedlty renaming on XPS enabled ZIP device"));
#endif /* METRO */

  /* Rename the file */
  if ( !zdv_rename_file(p_zipdev, p_file, &zip_file) ) {
    return(zdv_errorhandler(DeviceIOError, -1));
  }

  return(0);

} /* zip_rename_file */


/**
 * Common delete file implementation.
 */
static int32 RIPCALL deleteFileCommon(ZIP_DEVICE* p_zipdev, uint8* filename)
{
  ZIP_FILE*   p_file = NULL;
  ZIP_FILE_NAME zip_filename;

  /* Empty internal filenames are treated as undefinedfilename. */
  if ( !zdv_filename(filename, &zip_filename) ) {
    return(zdv_errorhandler(DeviceUndefined, -1));
  }

  /* Find named file in archive. */
  if ( !zdv_find_file(p_zipdev, &zip_filename, &p_file) ) {
    return(zdv_errorhandler(DeviceIOError, -1));
  }
  if ( p_file == NULL ) {
    return(zdv_errorhandler(DeviceUndefined, -1));
  }

  /* Can't delete file if currently open or filenameforall in progress. */
  if ( zfl_is_open(p_file) ) {
    return(zdv_errorhandler(DeviceInvalidAccess, -1));
  }

  /* First update any iterators to skip the file being deleted, then remove the
   * file from the chain of all files on the device, and finally delete the file
   * from the device. */
  zdv_iterators_remove(p_zipdev, p_file);
  zfc_remove(&p_zipdev->chain, p_file);
  if ( !zfl_delete(p_file) ) {
    return(zdv_errorhandler(DeviceIOError, -1));
  }

  return 0;
}

/**
 * \brief Delete a file from a ZIP device.
 *
 * Deleting of files is only supported on non read-only devices where the file
 * is not currently open.
 *
 * \param[in] dev
 * Pointer to ZIP device.
 * \param[in] filename
 * Name of file to delete.
 *
 * \return
 * \c 0 if file deleted successfully, else \c -1.
 */
static
int32 RIPCALL zip_delete_file(
/*@in@*/ /*@notnull@*/
  DEVICELIST* dev,
/*@in@*/ /*@notnull@*/
  uint8*      filename)
{
  ZIP_DEVICE* p_zipdev;

  HQASSERT((dev != NULL),
           "zip_delete_file: NULL device pointer");
  HQASSERT((filename != NULL),
           "zip_delete_file: NULL filename pointer");

  p_zipdev = (ZIP_DEVICE*)(dev->private_data);

  VERIFY_OBJECT(p_zipdev, ZIP_DEVICE_OBJECT_NAME);

  if ( zar_closed(&p_zipdev->archive) ) {
    /* Can't delete from closed. */
    return(zdv_errorhandler(DeviceUndefined, -1));
  }

  if ( zdv_readonly(p_zipdev) || zdv_creating(p_zipdev) ) {
    /* Can't delete from RO device or when creating an archive. */
    return(zdv_errorhandler(DeviceInvalidAccess, -1));
  }

  return deleteFileCommon(p_zipdev, filename);

} /* zip_delete_file */


/**
 * \brief Set a ZIP device parameter.
 *
 * The ZIP archive file is opened as soon as the filename parameter is set.
 * Once that happens no other device parameters can be set. As PostScript
 * dictionaries do not guarantee the order that entries are iterated over, the
 * archive filename should be set in a separate call to \c setdevparams.
 *
 * \param[in] dev
 * Pointer to ZIP device.
 * \param[in] param
 * Pointer to parameter to set in device.
 *
 * \return
 * One of the following:
 * - \c ParamAccepted if the parameter was known and has been set,
 * - \c ParamIgnored is the parameter was not known,
 * - \c ParamTypeCheck if the type of the parameter was wrong,
 * - \c ParamConfigError if the device parameter cannot be set, or
 * - \c ParamError if some other error occurred while setting the parameter.
 * .
 */
static
int32 RIPCALL zip_set_param(
/*@in@*/ /*@notnull@*/
  DEVICELIST*   dev,
/*@in@*/ /*@notnull@*/
  DEVICEPARAM*  param)
{
  uint8*      strval;
  uint32      strvallen;
  int32       i_param;
  ZIP_DEVICE_PARAM*  p_zip_param;
  ZIP_DEVICE* p_zipdev;

  HQASSERT((dev != NULL),
           "zip_set_param: NULL device pointer");
  HQASSERT((param != NULL),
           "zip_set_param: NULL param pointer");

  p_zipdev = (ZIP_DEVICE*)(dev->private_data);

  VERIFY_OBJECT(p_zipdev, ZIP_DEVICE_OBJECT_NAME);

  /* See if parameter is recognised */
  p_zip_param = p_zipdev->param;
  for ( i_param = 0; i_param < NUM_ZIP_PARAMS; i_param++ ) {
    if ( (param->paramnamelen == p_zip_param->param.paramnamelen) &&
         (HqMemCmp(p_zip_param->param.paramname, param->paramnamelen,
                   param->paramname, param->paramnamelen) == 0) ) {
      break;
    }
    p_zip_param++;
  }
  if ( i_param >= NUM_ZIP_PARAMS ) {
    return(ParamIgnored);
  }

  /* Error if of the wrong type or cannot write to param */
  if ( p_zip_param->param.type != param->type) {
    return(ParamTypeCheck);
  }
  if ( (p_zip_param->flags&PARAM_WRITEABLE) == 0 ) {
    return(ParamConfigError);
  }

  switch ( p_zip_param->param.type ) {
  case ParamString:
    if (i_param == ZIP_PARAM_DATASOURCE) {
      /* Can only set the ZIP file stream object name once */
      if (zdp_is_set(p_zip_param)) {
        return(ParamConfigError);
      }
      /* For now, you can't use DataSource for writable ZIP devices */
      if ( !p_zipdev->param[ZIP_PARAM_READONLY].param.paramval.boolval ) {
        return(ParamConfigError);
      }
      /* You can't set DataSource if Filename has already been set */
      if (zdp_is_set(&p_zipdev->param[ZIP_PARAM_FILENAME])) {
        return(ParamConfigError);
      }
    }

    if (i_param == ZIP_PARAM_FILENAME) {
      /* Can only set the ZIP file source once */
      if (zdp_is_set(p_zip_param)) {
        return(ParamConfigError);
      }
      /* You can't set Filename if DataSource has already been set */
      if (zdp_is_set(&p_zipdev->param[ZIP_PARAM_DATASOURCE])) {
        return(ParamConfigError);
      }
    }

    /* Check string length is acceptable */
    if ( zdp_has_range(p_zip_param) &&
         !zdp_in_range(p_zip_param, param->strvallen) ) {
      return(ParamRangeCheck);
    }
    /* Only allow zero length archive name if device is to be writable */
    if ( (i_param == ZIP_PARAM_FILENAME) && (param->strvallen == 0) &&
         p_zipdev->param[ZIP_PARAM_READONLY].param.paramval.boolval ) {
      return(ParamRangeCheck);
    }

    /* Create copy of parameter ready to replace current value if ok */
    if ( param->strvallen > 0 ) {
      strval = mm_alloc(mm_pool_temp, param->strvallen, MM_ALLOC_CLASS_ZIP_PARAM);
      if ( strval == NULL ) {
        return(zdv_errorhandler(DeviceVMError, ParamError));
      }
      HqMemCpy(strval, param->paramval.strval, param->strvallen);
    } else {
      strval = NULL;
    }
    strvallen = param->strvallen;

    /* Open archive if setting the filename */
    if ( i_param == ZIP_PARAM_FILENAME ) {
      if ( !zdv_open_archive(p_zipdev, strval, strvallen, FALSE) ) {
        mm_free(mm_pool_temp, strval, strvallen);
        return(zdv_errorhandler(DeviceIOError, ParamError));
      }

      /* If opened for writing to mark device as now writable */
      if ( !p_zipdev->param[ZIP_PARAM_READONLY].param.paramval.boolval ) {
        dev->flags |= DEVICEWRITABLE;
      }

    } else if ( i_param == ZIP_PARAM_ARCHIVE ) {
      /* Create new archive if one already opened */
      if ( !zar_opened(&p_zipdev->archive) ||
           !zdv_create_archive(p_zipdev, strval, strvallen) ) {
        mm_free(mm_pool_temp, strval, strvallen);
        return(zdv_errorhandler(DeviceIOError, ParamError));
      }
    } else if ( i_param == ZIP_PARAM_DATASOURCE ) {
      /* Run PS string and bind archive open handle to it. */
      if ( !zdv_open_archive(p_zipdev, strval, strvallen, TRUE) ) {
        mm_free(mm_pool_temp, strval, strvallen);
        return(zdv_errorhandler(DeviceIOError, ParamError));
      }

      /* For now, you are NOT allowed to open a writable ZIP device
         via giving a file source. */
#if 0
      /* If opened for writing to mark device as now writable */
      if ( !p_zipdev->param[ZIP_PARAM_READONLY].param.paramval.boolval ) {
        dev->flags |= DEVICEWRITABLE;
      }
#endif
    }

    /* We can now free the previous string value and assign the new good value */
    if ( p_zip_param->param.paramval.strval != NULL ) {
      mm_free(mm_pool_temp, p_zip_param->param.paramval.strval, p_zip_param->param.strvallen);
    }
    p_zip_param->param.paramval.strval = strval;
    p_zip_param->param.strvallen = strvallen;
    break;

  case ParamBoolean:
    if ( ((i_param == ZIP_PARAM_CHECKCRC32) ||
          (i_param == ZIP_PARAM_IGNORECASE) ||
          (i_param == ZIP_PARAM_READONLY) ||
          (i_param == ZIP_PARAM_OPENPACKAGE)) &&

          (zdp_is_set(&p_zipdev->param[ZIP_PARAM_FILENAME]) ||
           zdp_is_set(&p_zipdev->param[ZIP_PARAM_DATASOURCE]))) {
      /* Can't change after archive has been opened */
      return(ParamConfigError);
    }

    if ( i_param == ZIP_PARAM_CLOSE ) {
      /* Close only takes true value - false will not reopen an archive! */
      if ( !param->paramval.boolval ) {
        return(ParamRangeCheck);
      }
      /* Archive must be opened in order to close it! */
      if ( !zar_opened(&p_zipdev->archive) ) {
        return(ParamConfigError);
      }
      /* Close the archive */
      if ( zdv_close_archive(p_zipdev, CLOSE_EXPLICIT) < 0 ) {
        return(zdv_errorhandler(DeviceIOError, ParamError));
      }

    } else if ( i_param == ZIP_PARAM_FLUSH ) {
      /* Can only be true */
      if ( !param->paramval.boolval ) {
        return(ParamRangeCheck);
      }
      /* Archive must be opened for streaming in order to flush it */
      if ( !(zar_opened(&p_zipdev->archive) && zar_streamed(&p_zipdev->archive)) ) {
        return(ParamConfigError);
      }
      /* Do the deed ... */
      if ( !zdv_flush_archive(p_zipdev) ) {
        return(zdv_errorhandler(DeviceIOError, ParamError));
      }
    }

    p_zip_param->param.paramval.boolval = param->paramval.boolval;
    break;

  case ParamInteger:
#ifdef DEBUG_BUILD
    if ( i_param == ZIP_PARAM_DEBUG ) {
      p_zip_param->param.paramval.intval = param->paramval.intval;
      /* Do any immediate processing of debug flags required */
      zdv_debug(p_zipdev);
      break;
    }
#endif /* DEBUG_BUILD */

  case ParamFloat:
  case ParamDict:
  case ParamArray:
    /*@fallthrough@*/

  default:
    return(ParamIgnored);
  }

  /* Note parameter has been set */
  zdp_set(p_zip_param);

  return(ParamAccepted);

} /* zip_set_param */


/**
 * \brief Return number of parameters set on a ZIP device.
 *
 * This function also resets the parameter iterator state ready for following
 * calls to zip_get_param().
 *
 * \param[in] dev
 * Pointer to ZIP device.
 *
 * \return
 * Number of parameters set on the device.
 */
static
int32 RIPCALL zip_start_param(
/*@in@*/ /*@notnull@*/
  DEVICELIST* dev)
{
  int32       c_param;
  int32       i_param;
  ZIP_DEVICE* p_zipdev;
  ZIP_DEVICE_PARAM*  p_param;

  HQASSERT((dev != NULL),
           "zip_start_param: NULL devicelist pointer");

  p_zipdev = (ZIP_DEVICE*)(dev->private_data);

  VERIFY_OBJECT(p_zipdev, ZIP_DEVICE_OBJECT_NAME);

  /* Reset iterator state */
  p_zipdev->next_param = 0;

  /* Return number of set params */
  i_param = NUM_ZIP_PARAMS;
  c_param = 0;
  p_param = p_zipdev->param;
  do {
    if ( zdp_is_set(p_param) ) {
      c_param++;
    }
    p_param++;
  } while ( --i_param > 0 );

  return(c_param);

} /* zip_start_param */


/**
 * \brief Get either the next in sequence or named ZIP device parameter.
 *
 * Call zip_start_param() first in order to get all the set parameters of a
 * device by repeatedly calling this function.
 *
 * \param[in] dev
 * Pointer to ZIP device.
 * \param[in,out] param
 * Pointer to returned device parameter.  If the \c paramname field is not \c
 * NULL then the named parameter is returned, else the next set parameter in
 * sequence is returned.
 *
 * \return
 * One of the following:
 * - \c ParamAccepted if a device parameter is returned,
 * - \c ParamIgnored if a named parameter does not exist or has not been set,
 * - \c ParamError if there was an error getting the parameter, or
 * - \c 0 if there are no more set parameters to return.
 * .
 */
static
int32 RIPCALL zip_get_param(
/*@in@*/ /*@notnull@*/
  DEVICELIST*   dev,
/*@in@*/ /*@notnull@*/
  DEVICEPARAM*  param)
{
  int32       i_param;
  ZIP_DEVICE* p_zipdev;
  ZIP_DEVICE_PARAM*  p_zip_param
#ifndef S_SPLINT_S
                                  = NULL
#endif /* !S_SPLINT_S */
                                        ;

  HQASSERT((dev != NULL),
           "zip_get_param: NULL device pointer");
  HQASSERT((param != NULL),
           "zip_get_param: NULL param pointer");

  p_zipdev = (ZIP_DEVICE*)(dev->private_data);

  VERIFY_OBJECT(p_zipdev, ZIP_DEVICE_OBJECT_NAME);

  if ( param->paramname == NULL ) {
    /* Want next set param in list */
    for ( i_param = p_zipdev->next_param; i_param < NUM_ZIP_PARAMS; i_param++ ) {
      p_zip_param = &(p_zipdev->param[i_param]);
      if ( zdp_is_set(p_zip_param) ) {
        break;
      }
    }
    /* No more params to return */
    if ( i_param >= NUM_ZIP_PARAMS ) {
      return(0);
    }

  } else { /* Want named parameter */
    p_zip_param = p_zipdev->param;
    for ( i_param = 0; i_param < NUM_ZIP_PARAMS; i_param++ ) {
      if ( (param->paramnamelen == p_zip_param->param.paramnamelen) &&
           (HqMemCmp(param->paramname, param->paramnamelen,
                     p_zip_param->param.paramname, param->paramnamelen) == 0) ) {
        break;
      }
      p_zip_param++;
    }
    /* Parameter name not known or not set */
    if ( (i_param >= NUM_ZIP_PARAMS) || !zdp_is_set(p_zip_param) ) {
      return(ParamIgnored);
    }
  }

  /* Return selected param */
  HqMemCpy(param, p_zip_param, sizeof(DEVICEPARAM));

  /* Remember next indexed param to return if set.
   * Note: named get will reset iteration! */
  p_zipdev->next_param = ++i_param;

  return(ParamAccepted);

} /* zip_get_param */


/**
 * \brief Return information about the ZIP device.
 *
 * Reflect status of file device used for holding files on ZIP device.
 *
 * \param[in] dev
 * Pointer to ZIP device.
 * \param[out] devstat
 * Pointer to returned device information.
 *
 * \return
 * \c 0 if able to return device information, else \c -1.
 */
static
int32 RIPCALL zip_status_device(
/*@in@*/ /*@notnull@*/
  DEVICELIST* dev,
/*@out@*/ /*@notnull@*/
  DEVSTAT*    devstat)
{
  ZIP_DEVICE* p_zipdev;

  HQASSERT((dev != NULL),
           "zip_status_device: NULL device pointer");
  HQASSERT((devstat != NULL),
           "zip_status_device: NULL device status pointer");

  p_zipdev = (ZIP_DEVICE*)(dev->private_data);

  VERIFY_OBJECT(p_zipdev, ZIP_DEVICE_OBJECT_NAME);

  /* Return status of underlying file device */
  return((theIStatusDevice(zfd_device(&p_zipdev->file_device)))(zfd_device(&p_zipdev->file_device), devstat));

} /* zip_status_device */


/**
 * \brief Dismount a ZIP device.
 *
 * \param[in] dev
 * Pointer to ZIP device.
 *
 * \return
 * \c 0 if the device was successfully dismounted, else \c -1.
 */
static
int32 RIPCALL zip_dev_dismount(
/*@in@*/ /*@notnull@*/
  DEVICELIST* dev)
{
  int32       retcode;
  int32       i_param;
  ZIP_DEVICE_PARAM*  p_zip_param;
  ZIP_DEVICE* p_zipdev;
  ZIP_DEVICE_ITER* p_iterator;

  HQASSERT((dev != NULL),
           "zip_dev_dismount: NULL device pointer");

  p_zipdev = (ZIP_DEVICE*)(dev->private_data);

  VERIFY_OBJECT(p_zipdev, ZIP_DEVICE_OBJECT_NAME);

  /* Block dismount if in the process of creating an archive */
  if ( zdv_creating(p_zipdev) ) {
    return(zdv_errorhandler(DeviceInvalidAccess, -1));
  }

  /* Close the archive if not already closed */
  retcode = 0;
  if ( !zar_closed(&p_zipdev->archive) ) {
    retcode = zdv_close_archive(p_zipdev, CLOSE_EXPLICIT);
  }

  /* Free off any string device parameters that were set */
  p_zip_param = p_zipdev->param;
  for ( i_param = 0; i_param < NUM_ZIP_PARAMS; i_param++ ) {
    if ( (p_zip_param->param.type == ParamString) &&
         zdp_is_writeable(p_zip_param) &&
         zdp_is_set(p_zip_param) &&
         (p_zip_param->param.paramval.strval != NULL) ) {
      mm_free(mm_pool_temp, p_zip_param->param.paramval.strval, p_zip_param->param.strvallen);
    }
    p_zip_param++;
  }

  /* Free off any iterators still hanging around */
  p_iterator = DLL_GET_HEAD(&p_zipdev->iterators, ZIP_DEVICE_ITER, link);
  while ( p_iterator != NULL ) {
    zdv_free_iterator(p_iterator);
    p_iterator = DLL_GET_HEAD(&p_zipdev->iterators, ZIP_DEVICE_ITER, link);
  }

  /* Remove from list of ZIP devices */
  DLL_REMOVE(p_zipdev,link);

  UNNAME_OBJECT(p_zipdev);

  /* Return any error from closing archive file */
  if ( retcode == -1 ) {
    return(zdv_errorhandler(DeviceIOError, -1));
  }
  return(0);

} /* zip_dev_dismount */


/**
 * \brief Return preferred buffer size to use with a ZIP device.
 *
 * The preferred buffer size of a ZIP device is the preferred bufer size of the
 * device used to extract the archive files to, as ultimately all stream
 * operations are done on this device.
 *
 * \param[in] dev
 * Pointer to ZIP device.
 *
 * \return
 * Preferred buffer size in bytes.
 */
static
int32 RIPCALL zip_buffer_size(
/*@in@*/ /*@notnull@*/
  DEVICELIST* dev)
{
  ZIP_DEVICE* p_zipdev;

  HQASSERT((dev != NULL),
           "zip_buffer_size: NULL device pointer");

  p_zipdev = (ZIP_DEVICE*)(dev->private_data);

  VERIFY_OBJECT(p_zipdev, ZIP_DEVICE_OBJECT_NAME);

  return(zfd_bufsize(&p_zipdev->file_device));

} /* zip_buffer_size */


/**
 * \brief ZIP device ioctl function.
 *
 * Not supported.
 *
 * \param[in] dev
 * Pointer to ZIP device.
 * \param[in] fd
 * File stream descriptor.
 * \param[in] opcode
 * IOCTL opcode.
 * \param[in] arg
 * Opcode argument.
 *
 * \return
 * \c 0 on success, else \c -1.
 */
static
int32 RIPCALL zip_ioctl(
/*@in@*/ /*@notnull@*/
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR fd,
  int32       opcode,
  intptr_t    arg)
{
  ZIP_DEVICE* p_zipdev;

  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, fd);
  UNUSED_PARAM(intptr_t, arg);

  HQASSERT((dev != NULL), "zip_ioctl: NULL device pointer");

  p_zipdev = (ZIP_DEVICE*)(dev->private_data);
  VERIFY_OBJECT(p_zipdev, ZIP_DEVICE_OBJECT_NAME);

  switch (opcode) {
    default:
      return(-1);

    case ZIP_IOCTL_DISCARD_ENTRY:
      if (zar_closed(&p_zipdev->archive) || zdv_creating(p_zipdev)) {
        /* Can't discard from closed archive or when creating an archive. */
        return zdv_errorhandler(DeviceInvalidAccess, -1);
      }
      return deleteFileCommon(p_zipdev, (uint8*)arg);

    case ZIP_IOCTL_NEXT_PIECE_READY:
      /* The next piece is always ready when we're not streaming. */
      if (! zar_streamed(&p_zipdev->archive))
        return 1;
      else {
        Bool nextPieceReady = TRUE;
        if (zdv_creating(p_zipdev))
          return zdv_errorhandler(DeviceInvalidAccess, -1);

        if (! zfs_next_piece_ready(&p_zipdev->stream_list, fd, &nextPieceReady))
          return zdv_errorhandler(DeviceIOError, -1);

        if (nextPieceReady)
          return 1;
        return 0;
      }
  }
} /* zip_ioctl */


/**
 * \brief Miscellaneous ZIP device function.
 *
 * Currently does nothing.
 *
 * \return
 * \c 0 on success, else \c -1.
 */
static
int32 RIPCALL zip_spare(void)
{
  return(-1);

} /* zip_spare */


/** \brief Public ZIP device interface. */
static DEVICETYPE Zip_Device_Type = {
  ZIP_DEVICE_TYPE,                 /* the device ID number */
  DEVICERELATIVE,                  /* flags to indicate specifics of device */
  sizeof(ZIP_DEVICE),              /* the size of the private_data */
  0,                               /* minimum ticks between tickle functions */
  NULL,                            /* procedure to service the device */
  devices_last_error,              /* return last error for this device */
  zip_device_init,                 /* call to initialise device */
  zip_open_file,                   /* call to open file on device */
  zip_read_file,                   /* call to read data from file on device */
  zip_write_file,                  /* call to write data to file on device */
  zip_close_file,                  /* call to close file on device */
  zip_abort_file,                  /* call to abort action on the device */
  zip_seek_file,                   /* call to seek file on device */
  zip_bytes_file,                  /* call to get bytes avail on an open file */
  zip_status_file,                 /* call to check status of file */
  zip_start_file_list,             /* call to start listing files */
  zip_next_file,                   /* call to get next file in list */
  zip_end_file_list,               /* call to end listing */
  zip_rename_file,                 /* rename file on the device */
  zip_delete_file,                 /* remove file from device */
  zip_set_param,                   /* call to set device parameter */
  zip_start_param,                 /* call to start getting device parameters */
  zip_get_param,                   /* call to get the next device parameter */
  zip_status_device,               /* call to get the status of the device */
  zip_dev_dismount,                /* call to dismount the device */
  zip_buffer_size,                 /* call to determine buffer size */
  zip_ioctl,
  zip_spare                        /* spare slots */
};


static void init_C_globals_zipdev(void)
{
  static Bool first_time_ever = TRUE ;
  static ZIP_DEVICE_PARAM default_zip_param_save[sizeof(default_zip_param) / sizeof(ZIP_DEVICE_PARAM)] ;

  if (first_time_ever) {
    first_time_ever = FALSE ;
    HqMemCpy(default_zip_param_save, default_zip_param, sizeof(default_zip_param));
  } else {
    HqMemCpy(default_zip_param, default_zip_param_save, sizeof(default_zip_param));
  }

  DLL_RESET_LIST(&dls_zipdevs);
  zipdev_file_root = NULL;
}

static Bool zipdev_swstart(SWSTART *params)
{
  int32 i ;

  for (i = 0; params[i].tag != SWEndTag; i++) {
    if (params[i].tag == SWZipArchiveNameTag) {
      zipArchiveName = (uint8*)params[i].value.pointer_value;
      break;
    }
  }

  if (! device_type_add(&Zip_Device_Type))
    return FALSE ;

  zip_mount_os();

  /* Create root last so we force cleanup on success. */
  if ( mps_root_create(&zipdev_file_root, mm_arena, mps_rank_exact(), 0,
                       zipdev_file_root_scan, NULL, 0) != MPS_RES_OK)
    return FAILURE(FALSE) ;

  return TRUE ;
} /* zipdev_init */


static Bool zipdev_postboot(void)
{
  ZIPDEV_DEVICE_ITERATOR zip_iter;
  DEVICELIST *device ;

#define ZIP_FILE_PATTERN (uint8 *)(ZIPDEV_FILE_PREFIX "*")
  /* Remove any extracted ZIP files lying around from previous run */
  for ( device = zipdev_device_first(&zip_iter);
        device != NULL;
        device = zipdev_device_next(&zip_iter) ) {
    Bool disable = FALSE;
    struct old_file_list {
      struct old_file_list *next ;
      uint8 filename[1] ; /* Extendable allocation */
    } *files = NULL ;
    void *handle ;

    if ( !isDeviceEnabled(device) ) {
      SetEnableDevice(device);
      disable = TRUE;
    }

    /* Ignore errors from devicelist functions and file delete */
    if ( (handle = theIStartList(device)(device, ZIP_FILE_PATTERN)) != NULL ) {
      FILEENTRY file ;
      while ( theINextList(device)(device, &handle, ZIP_FILE_PATTERN, &file) == FileNameMatch ) {
        /* Stash away name; the DEVICELIST semantics do not specify whether
           delete operations can be performed in the middle of an
           enumeration, so assume not. If the memory cannot be allocated to
           store the name, quietly ignore the error, leaving the file on the
           device until next time. */
        struct old_file_list *match = mm_alloc_with_header(mm_pool_temp,
                                                           sizeof(struct old_file_list) + file.namelength,
                                                           MM_ALLOC_CLASS_ZIP_FILE_NAME) ;
        if ( match != NULL ) { /* Ignore MM failure */
          HqMemCpy(&match->filename[0], file.name, file.namelength) ;
          match->filename[file.namelength] = '\0' ;
          match->next = files ;
          files = match ;
        }
      }
      (void)theIEndList(device)(device, handle) ;
    }

    /* Delete any stored names */
    while ( files != NULL ) {
      struct old_file_list *next = files->next ;
      /* Note that the delete may fail; who knows what processes (e.g. virus
         checkers) may have the file open. If so we just ignore the failure and
         hope to catch it next time we boot. */
      (void)theIDeleteFile(device)(device, files->filename) ;
      mm_free_with_header(mm_pool_temp, files) ;
      files = next ;
    }

    if ( disable ) {
      ClearEnableDevice(device);
    }
  }

  return TRUE ;
}

static void zipdev_finish(void)
{
  mps_root_destroy(zipdev_file_root);

  zip_unmount_os();
} /* zipdev_finish */

IMPORT_INIT_C_GLOBALS(zip_sw)

void zipdev_C_globals(core_init_fns *fns)
{
  init_C_globals_zipdev() ;
  init_C_globals_zip_sw() ;

  fns->swstart = zipdev_swstart ;
  fns->postboot = zipdev_postboot ;
  fns->finish = zipdev_finish ;
}

/* Log stripped */
