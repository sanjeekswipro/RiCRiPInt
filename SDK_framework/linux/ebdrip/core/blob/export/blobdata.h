/** \file
 * \ingroup blob
 *
 * $HopeName: COREblob!export:blobdata.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions and structures for an implementation of the blob interface,
 * using the unified data cache interface. This interface provides a
 * mechanism to access and cache data used for blob charstring definitions,
 * external module blobs, and other data sources. The data cache provides
 * safe caching and restoring of data.
 */

#ifndef __BLOBDATA_H__
#define __BLOBDATA_H__

#include "mm.h"
#include "objnamer.h"
#include <stddef.h>    /* size_t */
#include "swblobapi.h"
#include "swblobfactory.h"

#define BLOB_MAX_ALIGNMENT 8

struct OBJECT ; /* from COREobjects */
struct FILELIST ; /* from COREileio */
struct core_init_fns ; /* from SWcore */

/** \brief Initialise C globals for blob. */
void blobdata_C_globals(struct core_init_fns *fns) ;


/** \brief An opaque instance pointer for each data cache. */
typedef struct blobdata_cache_t blobdata_cache_t ;

/** \brief The data store backing a particular blob. */
typedef struct blobdata_t blobdata_t ;

/** \brief The access methods for a blob source. */
typedef struct blobdata_methods_t blobdata_methods_t ;

/** \brief Private data for the access methods for a blob source. */
typedef struct blobdata_private_t blobdata_private_t ;


/** \brief Set up a blob source from a block of memory.

    The underlying data source for the blob will only be valid for the
    remainder of the current save level. When the underlying data source goes
    out of scope, blob methods will return \c SW_BLOB_ERROR_EXPIRED.

    \param[in] memory A pointer to a block of memory which the blob will read
    from and/or write to. This memory is assumed to remain valid for as long
    as the blob reference is open.

    \param[in] size The size of the block of memory.

    \param[in] mode An access mode of \c SW_RDONLY, \c SW_WRONLY, or \c
    SW_RDWR, possibly combined with the flags \c SW_FONT and \c SW_EXCL, as
    defined in "swdevice.h".

    \param[in] store A blob data store which will manage the allocation,
    and purging of data blocks.

    \param[out] blob A pointer where a new blob reference will be stored.

    \returns If the blob was created successfully, a non-NULL pointer to an
    open blob instance will be stored in \c blob, and \c SW_BLOB_OK is
    returned. The caller must ensure that the \c close method is called on
    the blob's implementation. If an error occurred, one of the \c
    sw_blob_result error codes is returned.
*/
sw_blob_result blob_from_memory(/*@notnull@*/ /*@in@*/ void *memory, size_t size,
                                int32 mode,
                                /*@notnull@*/ /*@in@*/ blobdata_cache_t *store,
                                /*@notnull@*/ /*@out@*/ sw_blob_instance **blob) ;

/** \brief Set up a blob source from an open \c FILELIST.

    The underlying data source for the blob may only be valid for the
    remainder of the current save level. If the file is a real file on an
    identifiable device, it will be re-opened if it accessed after the end of
    the current save level. Filters are only valid to the end of the current
    save level. When the underlying data source goes out of scope, blob
    methods will return \c SW_BLOB_ERROR_EXPIRED.

    \param[in] file An open file.

    \param[in] mode An access mode of \c SW_RDONLY, \c SW_WRONLY, or \c
    SW_RDWR, possibly combined with the flags \c SW_FONT and \c SW_EXCL, as
    defined in "swdevice.h". If none of the access mode flags are set, the
    RIP will derive the access mode from the file itself.

    \param[in] store A blob data store which will manage the allocation,
    and purging of data blocks.

    \param[out] blob A pointer where a new blob reference will be stored.

    \returns If the blob was created successfully, a non-NULL pointer to an
    open blob instance will be stored in \c blob, and \c SW_BLOB_OK is
    returned. The caller must ensure that the \c close method is called on
    the blob's implementation. If an error occurred, one of the \c
    sw_blob_result error codes is returned.
 */
sw_blob_result blob_from_file(/*@notnull@*/ /*@in@*/ struct FILELIST *file,
                              int32 mode,
                              /*@notnull@*/ /*@in@*/ blobdata_cache_t *store,
                              /*@notnull@*/ /*@out@*/ sw_blob_instance **blob) ;

/** \brief Set up a blob source from an OBJECT (OSTRING, OARRAY, OLONGSTRING
    or OFILE).

    The lifetime of the underlying blob source depends on the type and save
    level of the object. Strings and arrays are only valid to the end of the
    current save level. If a file objects is a real file on an identifiable
    device, it will be re-opened if it accessed after the end of the current
    save level. Filters are only valid to the end of the current save level.
    When the underlying data source goes out of scope, blob methods will
    return \c SW_BLOB_ERROR_EXPIRED.

    \param[in] object A valid object.

    \param[in] mode An access mode of \c SW_RDONLY, \c SW_WRONLY, or \c
    SW_RDWR, possibly combined with the flags \c SW_FONT and \c SW_EXCL, as
    defined in "swdevice.h". If none of the access mode flags are set, the
    RIP will derive the access mode from the object itself.

    \param[in] store A blob data store which will manage the allocation,
    and purging of data blocks.

    \param[out] blob A pointer where a new blob reference will be stored.

    \returns If the blob was created successfully, a non-NULL pointer to an
    open blob instance will be stored in \c blob, and \c SW_BLOB_OK is
    returned. The caller must ensure that the \c close method is called on
    the blob's implementation. If an error occurred, one of the \c
    sw_blob_result error codes is returned.
*/
sw_blob_result blob_from_object(/*@notnull@*/ /*@in@*/ struct OBJECT *object,
                                int32 mode,
                                /*@notnull@*/ /*@in@*/ blobdata_cache_t *store,
                                /*@notnull@*/ /*@out@*/ sw_blob_instance **blob) ;

/** \brief Set up a blob source from an OBJECT, using a specified set of
    methods to access it.

    The lifetime of the underlying blob source depends on the type and save
    level of the object. Strings and arrays are only valid to the end of the
    current save level. If a file objects is a real file on an identifiable
    device, it will be re-opened if it accessed after the end of the current
    save level. Filters are only valid to the end of the current save level.
    When the underlying data source goes out of scope, blob methods will
    return \c SW_BLOB_ERROR_EXPIRED.

    \param[in] object A valid object.

    \param[in] mode An access mode of \c SW_RDONLY, \c SW_WRONLY, or \c
    SW_RDWR, possibly combined with the flags \c SW_FONT and \c SW_EXCL, as
    defined in "swdevice.h". If none of the access mode flags are set, the
    RIP will derive the access mode from the object itself.

    \param[in] methods A set of methods to access the object data.

    \param[in] store A blob data store which will manage the allocation,
    and purging of data blocks.

    \param[out] blob A pointer where a new blob reference will be stored.

    \returns If the blob was created successfully, a non-NULL pointer to an
    open blob instance will be stored in \c blob, and \c SW_BLOB_OK is
    returned. The caller must ensure that the \c close method is called on
    the blob's implementation. If an error occurred, one of the \c
    sw_blob_result error codes is returned.
*/
sw_blob_result blob_from_object_with_methods(/*@notnull@*/ /*@in@*/ struct OBJECT *object,
                                             int32 mode,
                                             /*@notnull@*/ /*@in@*/ blobdata_cache_t *store,
                                             /*@notnull@*/ /*@in@*/ const blobdata_methods_t *methods,
                                             /*@notnull@*/ /*@out@*/ sw_blob_instance **blob) ;

/** \brief Initialise a blob data cache instance, returning an opaque
    reference to it.

    \param name  A name for debugging purposes.

    \param data_limit The maximum amount of data held in the cache before
    data blocks are recycled. This is measured in bytes.

    \param read_quantum The default block size used for reading data. This
    should be a multiple of the disk block size for best performance. This
    size should be a power of two.

    \param alloc_quantum All block allocations are rounded up to a multiple
    of this size. This makes it easier to find blocks that can be resized or
    recycled. This size should be a factor of the read quantum.

    \param trim_limit The maximum number of closed or unused data stores
    which will be retained for possible re-use.

    \param pool The MPS pool that allocations are taken for this cache.

    \return NULL if an error occurred. Otherwise, an opaque reference which
    is used by blob methods to identify which cache a blob is attached to.
*/
blobdata_cache_t *blob_cache_init(char *name,
                                  size_t data_limit,
                                  size_t read_quantum,
                                  size_t alloc_quantum,
                                  uint32 trim_limit,
                                  mm_cost_t cost,
                                  Bool multi_thread_safe,
                                  mm_pool_t pool);

/** \brief Destroy a blob data cache instance.

    \param cache A pointer to the data cache instance to destroy. */
void blob_cache_destroy(blobdata_cache_t **cache) ;


/** \brief Used to set the blob data cache size limit.

    This is not a hard limit; it is the amount of memory the cache will use
    before attempting to recycle blocks. The total amount of data in use can
    exceed the limit.

    \param cache The data cache instance whose limit will be set.

    \param limit The data size (in bytes) that will be kept before attempting
    to recycle allocated data blocks.
*/
void blob_cache_set_limit(blobdata_cache_t *cache, size_t limit) ;

/** \brief Used to get the blob data cache size limit.

    This is not a hard limit; it is the amount of memory the cache will use
    before attempting to recycle blocks. The total amount of data in use can
    exceed the limit.

    \param cache The data cache instance whose limit will be retrieved.

    \return The data size (in bytes) that will be kept before attempting to
    recycle allocated data blocks.
*/
size_t blob_cache_get_limit(blobdata_cache_t *cache) ;

/** \brief Access methods for underlying blob data.

    The blob data cache represents the source of blob data as an object
    reference and a set of methods. The methods are called to start and
    finish access to the source object, to get buffers of data from the
    source object, and when restoring VM. The open/close methods will NOT be
    nested; many calls to available may happen within the context of each
    open/close pair. The object provided to the open, close, and available
    callbacks is a copy of the object provided in the sw_blob_source; it may
    be modified by any or all of these routines if desired. */
struct blobdata_methods_t {
  /** This method is called to compare an object with one stored in the data
      cache. If it returns TRUE, the blob data cache assumes the objects point
      to the same object. It might, for instance, determine if two file
      references refer to the same device file. Object identity is always
      checked before calling this routine, and the routine will only be called
      for entries that use the same blob data methods (so a string data source
      will not be confused with a file name data source represented as a
      string). This routine will be called during blobdata_open to check if an
      existing blob data cache entry matches the source object. */
  Bool (*same)(/*@notnull@*/ /*@in@*/ const struct OBJECT *new,
               /*@notnull@*/ /*@in@*/ const struct OBJECT *cached) ;

  /** The create and destroy methods are called when a blobdata object is
      created and destroyed. The create routine may allocate a private state
      structure, which is passed to the subsequent calls to open, close, data
      availability and restore routines. */
  sw_blob_result (*create)(/*@notnull@*/ /*@in@*/ struct OBJECT *source,
                           /*@notnull@*/ /*@out@*/ blobdata_private_t **data) ;

  /** The create and destroy methods are called when a blobdata object is
      created and destroyed. The destroy routine must reset the private
      pointer to NULL if the create routine sets it. */
  void (*destroy)(/*@notnull@*/ /*@in@*/ struct OBJECT *source,
                  /*@notnull@*/ /*@in@*/ blobdata_private_t **data) ;

  /** The open and close methods are called to start and finish access to a
      data object. Persistant state required between open and close should be
      stored in the private structure allocated by the create method. A
      typical open method would be to open a file using its filename. */
  sw_blob_result (*open)(/*@notnull@*/ /*@in@*/ struct OBJECT *source,
                         /*@null@*/ /*@in@*/ blobdata_private_t *data,
                         int mode) ;

  /** The open and close methods are called to start and finish access to a
      data object. A typical close method would be to close a file
      descriptor. */
  void (*close)(/*@notnull@*/ /*@in@*/ struct OBJECT *source,
                /*@null@*/ /*@in@*/ blobdata_private_t *data) ;

  /** The available method is called between open and close methods to find
      out whether data is available WITHOUT COPYING. It should return NULL and
      leave the length value alone if the underlying data would change between
      calls (e.g. data in a file buffer changes when more data is read). If
      the data does not require copying, it should return a pointer to the
      data and reset the length value to indicate how much more data is
      available from that position (e.g. the addresses in a string or array
      data source will not change). It should return NULL if there is no data
      available at that offset. Persistant state required between the open and
      close routines should be stored in the private structure allocated by
      the create method. */
  uint8 *(*available)(/*@notnull@*/ /*@in@*/ const struct OBJECT *source,
                      /*@null@*/ /*@in@*/ blobdata_private_t *data,
                      Hq32x2 offset,
                      /*@notnull@*/ /*@out@*/ size_t *length) ;

  /** The read method is called read a block of data from the source, either
      into a buffer provided by the blob data cache, or a buffer supplied by
      the original caller. It is called with an offset into the data and a
      positive length, and should return the number of bytes read from the
      data source; this will be less than the number requested if there is an
      error, or if the end of the data source occurs before the requested
      length (in which case it should return the number of bytes actually
      read). Persistant state required between the open and close routines
      should be stored in the private structure allocated by the create
      method. We take a slightly cavalier attitude to errors; so long as we
      can read enough data to cover the frame, we don't care that there was
      an error. */
  size_t (*read)(/*@notnull@*/ /*@in@*/ const struct OBJECT *source,
                 /*@null@*/ /*@in@*/ blobdata_private_t *data,
                 /*@notnull@*/ /*@out@*/ uint8 *buffer,
                 Hq32x2 offset, size_t length) ;

  /** The write method is called write a block of data to the source. It will
      never be called between open and close calls. It is called with an
      offset into the data and a positive length, and should return TRUE if
      all of the bytes were written successfully to the data source. */
  sw_blob_result (*write)(/*@notnull@*/ /*@in@*/ struct OBJECT *source,
                          /*@null@*/ /*@in@*/ blobdata_private_t *data,
                          /*@notnull@*/ /*@in@*/ const uint8 *buffer,
                          Hq32x2 offset, size_t length) ;

  /** The length method is called to get the length of a data source. It will
      return a value indicating if the length could be obtained from the
      source. */
  sw_blob_result (*length)(/*@notnull@*/ /*@in@*/ const struct OBJECT *source,
                           /*@null@*/ /*@in@*/ blobdata_private_t *data,
                           /*@notnull@*/ /*@out@*/ Hq32x2 *length) ;

  /** The restored method is called when the VM system is about to restore the
      object used to open a blobdata structure. It should not be called while
      the object data is in use. The routine may either return NULL,
      indicating there is no further interest in the blob data, or return a
      pointer to an object that will allow continued access to the data after
      the restore. (This object will usually be global, but must be at a lower
      save level than the object being restored. Local allocations at the time
      of this call will be at the same level as the object being restored, and
      thus are pointless.) */
  struct OBJECT *(*restored)(/*@notnull@*/ /*@in@*/ const struct OBJECT *source,
                             /*@null@*/ /*@in@*/ blobdata_private_t *data,
                             int32 savelevel) ;

  /** The protection routine is used to determine what level of encryption or
      protection is applied to the raw blob data. If no protection is used,
      zero may be returned, otherwise the PROTECTED_* enumeration in paths.h
      should be used. */
  uint8 (*protection)(/*@notnull@*/ /*@in@*/ const struct OBJECT *source,
                      /*@null@*/ /*@in@*/ blobdata_private_t *data) ;
} ;

/* These sets of pre-defined methods exist, for the common cases of loading
   data from a single string, a file, or an array of strings. More
   complicated methods may be provided by the caller. */
extern const blobdata_methods_t blobdata_string_methods ; /* bdstring.c */
extern const blobdata_methods_t blobdata_longstring_methods ; /* bdlstring.c */
extern const blobdata_methods_t blobdata_file_methods ;   /* bdfile.c */
extern const blobdata_methods_t blobdata_array_methods ;  /* bdarray.c */

/** \brief This is an internal hook for the VM system to call the restored
    methods of cached data. */
void blob_restore_commit(int32 savelevel) ;

/** \brief This is an internal hook for the VM system to perform a GC scan of
    all of the the blob data caches. */
mps_res_t MPS_CALL blobdata_scan(mps_ss_t ss, void *p, size_t s) ;


/** \brief A general blob data store.

    This blob store is usable by core modules which don't want to specialise
    their data caches. It is purged aggressively. */
extern blobdata_cache_t *global_blob_store ;

/** \brief A blob API implementation specialised to handle Core RIP
    objects. */
extern const sw_blob_api sw_blob_api_objects ;

/** \brief A blob factory instance for opening named files from the RIP's
    file system, attached to the sw_blob_api_objects implementation. */
extern const sw_blob_factory_instance sw_blob_factory_objects ;

/*
Log stripped */
#endif /* protection for multiple inclusion */
