/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_control!swblobapi.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief This header file provides definitions for the BLOB (Binary Large
 * Object) interface to the core RIP.
 *
 * The BLOB interface is used to access large data chunks, either using a
 * memory mapping API or a file reading API.
 */


#ifndef __SWBLOBAPI_H__
#define __SWBLOBAPI_H__

/** \defgroup swblobapi Binary large object callback API
 * \ingroup PLUGIN_callbackAPI
 * \{
 */

#include "ripcall.h"
#include <stddef.h> /* size_t */
#include "swapi.h"
#include "swdevice.h" /* SW_RDRW flags et. al. */

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Version numbers defined for the blob API. */
enum {
  SW_BLOB_API_VERSION_20070614 = 1, /**< Obsolete as of 20071115 */
  SW_BLOB_API_VERSION_20071115      /**< Current version. */
#ifdef CORE_INTERFACE_PRIVATE
  , SW_BLOB_API_VERSION_NEXT /* Do not ever use this name. */
  /* SW_BLOB_API_VERSION is provided so that the Harlequin Core RIP can test
     compatibility with current versions, without revising the registration
     code for every interface change.

     Implementations of sw_blob_api within the Harlequin Core RIP should NOT
     use this; they should have explicit version numbers. Using explicit
     version numbers will allow them to be backwards-compatible without
     modifying the code for every interface change.
  */
  , SW_BLOB_API_VERSION = SW_BLOB_API_VERSION_NEXT - 1
#endif
} ;

/* \brief Type definition of the blob API implementation. */
typedef struct sw_blob_api sw_blob_api ;

/** \brief An instance of the blob API implementation.
 *
 * This structure represents an open data source.
 *
 * There is only one public field in this structure, which is the required
 * implementation pointer. The RIP will subclass instances to add private
 * instance specific data.
 *
 * The RIP manages the memory for this structure. Modules should not attempt
 * to copy the structure, or access any data except the public fields.
 */
typedef struct sw_blob_instance {
  /* Entries present in SW_BLOB_API_VERSION_20071115: */

  /** \brief Pointer to the API implementation.

       API methods for a blob instance should always be called by indirecting
       through the instance's implementation field.
  */
  const sw_blob_api *implementation ;

  /* End of entries present in SW_BLOB_API_VERSION_20071115 */
} sw_blob_instance ;

/** \brief An instance of a blob memory mapping context.
 *
 * There is only one public field in this structure, which is the required
 * implementation pointer. The RIP will subclass instances to add private
 * instance specific data.
 *
 * The RIP manages the memory for this structure. Modules should not attempt
 * to copy the structure, or access any data except the public fields.
 */
typedef struct sw_blob_map {
  /* Entries present in SW_BLOB_API_VERSION_20071115: */

  /** \brief Pointer to the API implementation.

       API methods for a blob map should always be called by indirecting
       through the map's implementation field.
  */
  const sw_blob_api *implementation ;

  /* End of entries present in SW_BLOB_API_VERSION_20071115 */
} sw_blob_map ;

/** \brief Return values from sw_blob_api functions. */
enum {
  /* Success codes present in SW_BLOB_API_VERSION_20071115: */
  SW_BLOB_OK = 0,         /**< Success return value for sw_blob_api methods. */
  /* End of success codes present in SW_BLOB_API_VERSION_20071115 */

  /* Errors present in SW_BLOB_API_VERSION_20071115: */
  SW_BLOB_ERROR = 1,      /**< Non-specific error, also minimum error value.
                               Please avoid using this if possible. */
  SW_BLOB_ERROR_EOF,      /**< End of data stream. */
  SW_BLOB_ERROR_MEMORY,   /**< Out of memory. */
  SW_BLOB_ERROR_INVALID,  /**< An invalid blob reference was used. */
  SW_BLOB_ERROR_ACCESS,   /**< The access mode requested cannot be used. */
  SW_BLOB_ERROR_WRITE,    /**< A write to the blob failed. */
  SW_BLOB_ERROR_EXPIRED  /**< The underlying data source has expired. */
  /* End of errors present in SW_BLOB_API_VERSION_20071115 */
} ;

/** \brief Type of return values from sw_blob_api functions. */
typedef int sw_blob_result ;

/** \brief Possible values of the blob region map alignment. */
enum {
  /* Entries present in SW_BLOB_API_VERSION_20071115: */
  SW_BLOB_ALIGNMENT_NONE = 1,  /**< No specific alignment of mappings. */
  SW_BLOB_ALIGNMENT_16BIT = 2, /**< Align mappings to 2-byte boundaries. */
  SW_BLOB_ALIGNMENT_32BIT = 4, /**< Align mappings to 4-byte boundaries. */
  SW_BLOB_ALIGNMENT_64BIT = 8  /**< Align mappings to 8-byte boundaries. */
  /* End of entries present in SW_BLOB_API_VERSION_20071115 */
} ;

/** \brief An integer used to indicate the type of encryption or protection
    applied to the original blob. */
typedef int sw_blob_protection ;

/**
 * \brief A structure containing callback functions for BLOB (Binary Large
 * Object) access.
 *
 * The rip provides the module with an API that can be used to access data
 * sources through either a memory mapping interface or a file-like API.
 * Where possible, the module should use the memory mapping interface to
 * directly read data, because the RIP may be able to use zero-copy
 * techniques to provide data.
 */
struct sw_blob_api {
  /** \brief Version number, name, display name, instance size.

      This is REQUIRED to be the first field. */
  sw_api_info info ;

  /* Entries present in SW_BLOB_API_VERSION_20071115: */

  /** \brief Create a retainable reference to a blob data source.

      This method should be used whenever a module wishes to retain a
      reference to a blob after the end of the call that supplied it.
      Data sources supplied by the RIP are automatically invalidated at the
      end of the method that passed them to a module.

      If the data source backing the blob becomes invalid before the blob is
      closed, then all of the API calls referring to the blob or to memory
      maps opened from it will return error codes (SW_BLOB_ERROR_EXPIRED
      usually).

      A module should release all references to a blob as soon as possible
      to ensure best performance.

      This method can also be used to request a different set of access
      permissions for a blob.

      \param[in] blob An existing blob data source.

      \param[in] mode An access mode composed of one of SW_RDWR,
      SW_WRONLY, or SW_RDONLY, possibly combined with the flags SW_EXCL
      and SW_FONT defined in swdevice.h.

      \param[out] reference A pointer in which the new blob reference will be
      stored.

      \retval SW_BLOB_OK Returned if the blob was opened, in which case the
      \a reference parameter is updated with a reference to the blob. The \p
      close() call must be called when the module no longer needs the blob to
      allow the RIP to deallocate the resources used by the blob.

      \retval SW_BLOB_ERROR_MEMORY Returned if the RIP cannot
      allocate a blob reference.

      \retval SW_BLOB_ERROR_ACCESS Returned if the access mode does not match
      the capability of the underlying data source. Some blob implementations
      may check access lazily, and may allow creation of blob references which
      cannot be accessed for read and/or write. Such an implementation may
      return SW_BLOB_OK to this method call, but fail with
      SW_BLOB_ERROR_ACCESS later.

      \retval SW_BLOB_ERROR_INVALID Returned if the \a blob parameter is
      invalid.

      \retval SW_BLOB_ERROR_EXPIRED Returned if the \a blob parameter has
      passed out of scope.
  */
  sw_blob_result (RIPCALL *open)(/*@in@*/ /*@notnull@*/ sw_blob_instance *blob,
                                 int mode,
                                 /*@out@*/ /*@notnull@*/ sw_blob_instance **reference) ;

  /** \brief Close a blob reference, releasing its resources.

      Note that it is permissible to close a blob before its memory mapping
      contexts are closed, however any resources used by the blob will not
      actually be released until the last mapping context is closed.

      \param[in,out] blob A pointer to the blob reference to be closed. The
      blob reference is invalidated by this call.
  */
  void (RIPCALL *close)(/*@in@*/ /*@notnull@*/ sw_blob_instance **blob) ;

  /** \brief Get the length of a blob.

      \param[in] blob The blob whose length is to be found.

      \param[out] bytes On exit, the length of the blob. The total length of
      a blob may not be available if the underlying data source is streamed.

      \retval SW_BLOB_OK Returned if the length was available, in which case
      the \a bytes parameter contains the length of the data source.

      \retval SW_BLOB_ERROR_ACCESS Returned if the length of the blob was
      not available, in which case the \a bytes parameter is not modified.

      \retval SW_BLOB_ERROR_INVALID Returned if the blob instance is invalid.

      \retval SW_BLOB_ERROR_EXPIRED Returned if the underlying data source
      has expired.
  */
  sw_blob_result (RIPCALL *length)(/*@in@*/ /*@notnull@*/ sw_blob_instance *blob,
                                   /*@out@*/ /*@notnull@*/ Hq32x2 *bytes) ;

  /** \brief Read a buffer of data from the current position in a blob.

      This call fills a buffer with bytes read from the current position of
      the blob, updating the current position to point after the bytes read.
      It will return less than the requested number of bytes only if the end
      of the data source was encountered, or an error occurred.

      \param[in] blob The blob from which to read data.

      \param[out] buffer A buffer into which the data will be read.

      \param[in] byteswanted The number of bytes to read. The caller is
      responsible for making sure the buffer is at least this size.

      \param[out] bytesread On a successful exit, the number of bytes actually
      read from the blob is stored here.

      \retval SW_BLOB_OK Returned if bytes were read from the blob without an
      error occurring. The \a bytesread parameter will be updated with the
      amount of data actually stored in the buffer. The \p read() function
      will try to fully satisfy the read request; the only time fewer than
      the requested number of bytes will be returned is when the request
      spans the end of the data stream.

      \retval SW_BLOB_ERROR_EOF If there is no data left to read, this will
      be returned, and a value of zero will be stored in the \a bytesread
      field.

      \retval SW_BLOB_ERROR_INVALID Returned if the blob instance is
      invalid.

      \retval SW_BLOB_ERROR_EXPIRED Returned if the underlying data source is
      no longer valid.

      \retval SW_BLOB_ERROR_ACCESS Returned if the blob does not support read
      access.
  */
  sw_blob_result (RIPCALL *read)(/*@in@*/ /*@notnull@*/ sw_blob_instance *blob,
                                 /*@out@*/ /*@notnull@*/ void *buffer,
                                 size_t byteswanted,
                                 /*@out@*/ /*@notnull@*/ size_t *bytesread) ;

  /** \brief Write a buffer of data at the current position in a blob.

      This call stores a buffer of bytes at the current position in the blob,
      updating the current position to point after the bytes written. It will
      only write less than the requested number of bytes if an error
      occurred.

      Note that blobs cannot be written to if any memory mapping contexts of
      any blob instances are mapped onto the underlying data source.

      \param[in] blob The blob to which data is written.

      \param[in] buffer A buffer from which the data will be written.

      \param[in] bytestowrite The number of bytes to write. The caller is
      responsible for making sure the buffer is at least this size.

      \retval SW_BLOB_OK Returned if the write request was fully completed.

      \retval SW_BLOB_ERROR_INVALID Returned if the blob instance is invalid.

      \retval SW_BLOB_ERROR_EXPIRED Returned if the underlying data source is
      no longer valid.

      \retval SW_BLOB_ERROR_ACCESS Returned if the blob does not support
      write access, or if any memory mapping contexts are currently open on
      the blob.
  */
  sw_blob_result (RIPCALL *write)(/*@in@*/ /*@notnull@*/ sw_blob_instance *blob,
                                  /*@in@*/ /*@notnull@*/ void *buffer,
                                  size_t bytestowrite) ;

  /** \brief Adjust the blob's current position.

      Some blob implementations may lazily update the position in the
      underlying data source, and so may return success for this method, but
      fail on a subsequent read or a write.

      \param[in] blob The blob whose current position is to be set.

      \param[in] where The new current position of the blob, relative to the
      \a offset.

      \param[in] offset One of the constants SW_SET, SW_INCR, or
      SW_XTND, defined in swdevice.h. The \a offset indicates whether the
      position set is relative to the start, current position, or end of the
      underlying data source respectively.

      \retval SW_BLOB_OK Returned if the position was successfully updated.

      \retval SW_BLOB_ERROR_INVALID Returned if the blob instance is
      invalid.

      \retval SW_BLOB_ERROR_EXPIRED Returned if the underlying data source is
      out of scope.

      \retval SW_BLOB_ERROR_EOF Returned if the position is set beyond the
      end of the data source, and the blob is not writable (if it is, the
      data source will be extended as necessary).

      \retval SW_BLOB_ERROR_ACCESS Returned if the underlying data source
      is not seekable.
  */
  sw_blob_result (RIPCALL *seek)(/*@in@*/ /*@notnull@*/ sw_blob_instance *blob,
                                 Hq32x2 where, int offset) ;

  /** \brief Query the current position in the blob.

      \param[in] blob The blob whose current position is to be found.

      \param[out] where On a successful exit, the current position in the
      blob is stored here.

      \retval SW_BLOB_OK Returned if the blob was valid, in which case the
      location is stored in the \a where parameter.

      \retval SW_BLOB_ERROR_INVALID Returned if the blob instance is invalid.

      \retval SW_BLOB_ERROR_EXPIRED Returned if the underlying data source is
      out of scope.
  */
  sw_blob_result (RIPCALL *tell)(/*@in@*/ /*@notnull@*/ sw_blob_instance *blob,
                                 /*@out@*/ /*@notnull@*/ Hq32x2 *where) ;

  /** \brief Query the type of protection used for the blob data.

      The protection type is an integer, representing the type of encryption
      or protection of the original blob source. The value of 0 indicates no
      protection. Values in the range 1-255 are reserved for use by Global
      Graphics.

      \param[in] blob The blob whose protection type is to be found.

      \param[out] protection One of the sw_blob_protection enumeration
      values.

      \retval SW_BLOB_OK Returned if the blob was valid, in which case the
      type of protection is stored in the \a protection parameter.

      \retval SW_BLOB_ERROR_INVALID Returned if the blob instance is invalid.

      \retval SW_BLOB_ERROR_EXPIRED Returned if the underlying data source is
      out of scope.
  */
  sw_blob_result (RIPCALL *protection)(/*@in@*/ /*@notnull@*/ sw_blob_instance *blob,
                                       /*@out@*/ /*@notnull@*/ sw_blob_protection *protection) ;

  /** \brief Open a memory mapping context for a blob.

  The blob memory mapping interface allows clients to request access to
  regions of data which can be accessed directly through memory pointers. If
  possible, the RIP will provide these pointers without copying data.

  \param[in] blob The blob which to which the subsequent memory mapping
  operations apply.

  \param[out] map On a successful exit, an open memory mapping
  context will be stored here.

  Multiple memory map contexts may be opened on the same blob
  simultaneously. All mappings created in a context will remain valid
  until the corresponding \p map_close() call, after which they will
  cease to be be valid and pointers to the mapped memory must not be
  dereferenced. The RIP will allocate and deallocate memory as
  necessary to map requested sections of a blob. Modules must not
  modify RIP memory returned by the memory mapping API.

  \retval SW_BLOB_OK Returned if the mapping context was created
  successfully.

  \retval SW_BLOB_ERROR_MEMORY Returned if the RIP cannot allocate a
  blob mapping context.

  \retval SW_BLOB_ERROR_ACCESS Returned if the underlying data source
  cannot be read.

  \retval SW_BLOB_ERROR_INVALID Returned if the blob instance is
  invalid.

  \retval SW_BLOB_ERROR_EXPIRED Returned if the underlying data
  source is out of scope.

  If the \p map_open() call succeeds, a corresponding \p map_close() call
  MUST be made when the access to the mapped sections is complete, to
  allow the RIP to recover the resources associated with the mapping
  context.

  It is permissable to close a blob reference which has open memory mapping
  contexts, and continue to use the memory mapping contexts. In this case,
  resources used by the blob will not be released until the last memory
  mapping context is closed.
  */
  sw_blob_result (RIPCALL *map_open)(/*@in@*/ /*@notnull@*/ sw_blob_instance *blob,
                                     /*@out@*/ /*@notnull@*/ sw_blob_map **map) ;

  /** \brief Map a section of a blob into a contiguous block of memory.

  This call ensures that a section of the blob starting at the
  specified start address and with the specified length is mapped into
  memory, and returns a pointer to the memory in which the blob is
  mapped. The data mapping will not copy data if possible, but if
  different alignments are requested, natural boundaries in the
  underlying data source are crossed, or overlapping regions are
  mapped, the data may be copied into temporary buffers managed by the
  RIP. All mapping pointers are invalid as soon as the memory mapping
  context is closed, and must not be dereferenced.

  Memory mapping a region does not modify the current blob position.

  \param[in] map The memory mapping context in which the mapping is to
  be made.

  \param[in] start The start location of the data to be mapped in the
  blob.

  \param[in] length The length of the data region to be mapped.

  \param[in] alignment This parameter determines how the returned data
  pointer should be aligned. A value of SW_BLOB_ALIGNMENT_NONE indicates
  that any alignment is acceptable. The value SW_BLOB_ALIGNMENT_16BIT is
  used for 16-bit alignment, SW_BLOB_ALIGNMENT_32BIT for 32-bit alignment,
  and SW_BLOB_ALIGNMENT_64BIT for 64-bit alignment. For types of these
  sizes, the alignment enumeration values are the same as \c sizeof(type).

  \param[out] mapping After a successful exit, a pointer to a
  contiguous block of memory containing the data requested, aligned as
  requested, is stored in this location. The pointer remains valid
  until the mapping context is closed.

  \retval SW_BLOB_OK Returned if the mapping is made successfully.
  \retval SW_BLOB_ERROR_MEMORY Returned if the RIP cannot allocate a
  mapping buffer.
  \retval SW_BLOB_ERROR_EOF Returned if the mapping extends beyond
  the end of the underlying data source.
  \retval SW_BLOB_ERROR_INVALID Returned if the mapping context,
  mapping location pointer, or the alignment parameter are invalid.
  \retval SW_BLOB_ERROR_EXPIRED Returned if the underlying data
  source is out of scope.
  */
  sw_blob_result (RIPCALL *map_region)(/*@in@*/ /*@notnull@*/ sw_blob_map *map,
                                       Hq32x2 start, size_t length, size_t alignment,
                                       /*@out@*/ /*@notnull@*/ uint8 **mapping) ;

  /** \brief Close a memory mapping context for a blob.

      If \p map_open() succeeded, this method MUST be called to delete the
      memory mapping context. Calling this method allows the RIP to reclaim
      allocated buffers in the memory mapping context.

      Note that it is permissible to close a blob before its memory mapping
      contexts are closed, however any resources required by the blob will
      not actually be released until the last mapping context is closed.

      \param[in,out] map The memory mapping context to be closed. The
      mapping context is invalidated by this call.

      After this call, any pointers returned by the \p map_region() method in
      this context must not be dereferenced. */
  void (RIPCALL *map_close)(/*@in@*/ /*@notnull@*/ sw_blob_map **map) ;

  /* End of entries present in SW_BLOB_API_VERSION_20071115 */
} ;

#ifdef __cplusplus
}
#endif

/** \} */ /* end Doxygen grouping */


#endif /* __SWBLOBAPI_H__ */
