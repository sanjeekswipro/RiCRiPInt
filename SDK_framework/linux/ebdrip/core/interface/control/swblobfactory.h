/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_control!swblobfactory.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief This header file provides definitions for the BLOB (Binary Large
 * Object) factory.
 *
 * The BLOB factory is used by modules that need to initiate access to blobs
 * independently.
 */


#ifndef __SWBLOBFACTORY_H__
#define __SWBLOBFACTORY_H__

/** \ingroup swblobapi
 * \{
 */

#include "ripcall.h"
#include <stddef.h> /* size_t */
#include "swapi.h"
#include "swdevice.h" /* SW_RDRW flags et. al. */
#include "swblobapi.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Version numbers defined for the blob API. */
enum {
  SW_BLOB_FACTORY_API_VERSION_20071116 = 1 /**< Current version */
#ifdef CORE_INTERFACE_PRIVATE
  , SW_BLOB_FACTORY_API_VERSION_NEXT /* Do not ever use this name. */
  /* SW_BLOB_FACTORY_API_VERSION is provided so that the Harlequin Core RIP
     can test compatibility with current versions, without revising the
     registration code for every interface change.

     Implementations of sw_blob_factory_api within the Harlequin Core RIP
     should NOT use this; they should have explicit version numbers. Using
     explicit version numbers will allow them to be backwards-compatible
     without modifying the code for every interface change.
  */
  , SW_BLOB_FACTORY_API_VERSION = SW_BLOB_FACTORY_API_VERSION_NEXT - 1
#endif
} ;

/* \brief Type definition of the blob factory implementation. */
typedef struct sw_blob_factory_api sw_blob_factory_api ;

/** \brief An instance of the blob factory implementation.
 *
 * This structure represents a parameterised blob factory.
 *
 * There is only one public field in this structure, which is the required
 * implementation pointer. The RIP will subclass instances to add private
 * instance specific data.
 *
 * The RIP manages the memory for this structure. Modules should not attempt
 * to copy the structure, or access any data except the public fields.
 */
typedef struct sw_blob_factory_instance {
  /* Entries present in SW_BLOB_FACTORY_API_VERSION_20071116: */

  /** \brief Pointer to the factory implementation.

       API methods for a blob factory instance should always be called by
       indirecting through the instance's implementation field.
  */
  const sw_blob_factory_api *implementation ;

  /* End of entries present in SW_BLOB_FACTORY_API_VERSION_20071116 */
} sw_blob_factory_instance ;

/** \brief A structure containing callback functions to create BLOBs from
 * named entities.
 *
 * The RIP will provide the module with these callbacks to create BLOB
 * objects based on named entities. The callbacks will be presented in the
 * form of factory instances, which are subclassed by the RIP to encapsulate
 * parameters for named blob providers.
 */
struct sw_blob_factory_api {
  /** \brief Version number, name, display name, instance size.

      This is REQUIRED to be the first field. */
  sw_api_info info ;

  /* Entries present in SW_BLOB_FACTORY_API_VERSION_20071116: */

  /** \brief Create a new blob data source based upon a named entity.

      This method allows modules to request the RIP to provide a data blob
      from a location named by the module. This is method is typically
      connected to an implementation that opens named files on the RIP's
      filesystem.

      \param[in] instance The sw_blob_factory_instance pointer provided by
      the RIP.

      \param[in] name The name to use to open a blob. The semantics of the
      name depend on the implementation. For the file system access
      implementation, the name must be an absolute device name, a device name
      and file name, or just a file name.

      \param[in] name_len The length of the name.

      \param[in] mode An access mode composed of one of SW_RDWR,
      SW_WRONLY, or SW_RDONLY, possibly combined with the flags SW_EXCL
      and SW_FONT defined in swdevice.h.

      \param[out] blob On a successful exit, an open blob reference is stored
      here.

      \retval SW_BLOB_OK Returned if the blob was opened, in which case the
      \a blob parameter is updated with a reference to the blob. The blob's
      \p implementation->close() call MUST be called when the module no
      longer needs the blob to allow the RIP to deallocate the resources used
      by the blob.

      \retval SW_BLOB_ERROR_MEMORY Returned if the RIP cannot
      allocate a blob instance.

      \retval SW_BLOB_ERROR_INVALID Returned if any of the parameters
      are invalid (e.g., the name, implementation or return pointer are NULL,
      or the filename is too long).

      \retval SW_BLOB_ERROR_ACCESS Returned if the access mode is not
      supported.
  */
  sw_blob_result (RIPCALL *open_named)(/*@in@*/ /*@notnull@*/ sw_blob_factory_instance *instance,
                                       /*@in@*/ /*@notnull@*/ uint8 *name,
                                       size_t name_len,
                                       int mode,
                                       /*@out@*/ /*@notnull@*/ sw_blob_instance **blob) ;

  /* End of entries present in SW_BLOB_FACTORY_API_VERSION_20071116 */
} ;

#ifdef __cplusplus
}
#endif

/** \} */ /* end Doxygen grouping */


#endif /* __SWBLOBFACTORY_H__ */
