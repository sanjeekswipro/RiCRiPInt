/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_control!swmemapi.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2011 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 *
 * \brief
 * This header file provides definitions for the RIP's memory callback API.
 *
 * This API is provided to modules which may need to do memory allocation, to
 * allow the RIP to track and manage allocations from modules.
 */


#ifndef __SWMEMAPI_H__
#define __SWMEMAPI_H__
/** \defgroup PLUGIN_callbackAPI Callback APIs
 * \ingroup interface
 * \{
 */

/** \defgroup swmemapi Memory allocation callback API
 * \ingroup PLUGIN_callbackAPI
 * \{
 */

#include "ripcall.h"
#include <stddef.h> /* size_t */
#include "swapi.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Version numbers defined for the memory management callback API. */
enum {
  SW_MEMORY_API_VERSION_20070525 = 2, /**< Obsolete as of 20071110 */
  SW_MEMORY_API_VERSION_20071110      /**< Current version */
  /* new versions go here */
#ifdef CORE_INTERFACE_PRIVATE
  , SW_MEMORY_API_VERSION_NEXT /* Do not ever use this name. */
  /* SW_MEMORY_API_VERSION is provided so that the Harlequin Core RIP can test
     compatibility with current versions, without revising the registration
     code for every interface change.

     Implementations of sw_memory_api within the Harlequin Core RIP should NOT
     use this; they should have explicit version numbers. Using explicit
     version numbers will allow them to be backwards-compatible without
     modifying the code for every interface change.
  */
  , SW_MEMORY_API_VERSION = SW_MEMORY_API_VERSION_NEXT - 1
#endif
} ;

/** \brief Type definition of the memory callback interface implementation. */
typedef struct sw_memory_api sw_memory_api ;

/** \brief An instance of the memory callback API implementation.
 *
 * There is only one public field in this structure, which is the required
 * implementation pointer. The RIP will subclass instances to add private
 * instance specific data.
 *
 * The RIP manages the memory for this structure. Modules should not attempt
 * to copy the structure, or access any data except the public fields.
 */
typedef struct sw_memory_instance {
  /* Entries present in SW_MEMORY_API_VERSION_20071110: */

  /** \brief Pointer to the API implementation.

       API methods for a memory allocator instance should always be called by
       indirecting through the instance's implementation field.
  */
  const sw_memory_api *implementation ;

  /* End of entries present in SW_MEMORY_API_VERSION_20071110 */
} sw_memory_instance ;

/** \brief A structure containing callback functions for memory allocation.
 *
 * The RIP will provide the module with these callbacks (in the form of
 * memory API instances), which should be used in preference to those
 * provided by the platform. Failure to do this may result in difficulties in
 * configuring the rip for optimal performance. Memory API instances are
 * subclassed by the RIP to encapsulate parameters for particular memory
 * allocators.
 */
struct sw_memory_api {
  /** Version number, name, display name, and instance size. This is REQUIRED
      to be the first field. */
  sw_api_info info ;

  /* Entries present in SW_MEMORY_API_VERSION_20071110: */

  /** \brief Allocate a block of memory \a size bytes long.
   *
   * \param[in] instance A sw_memory_instance pointer provided to by the RIP.
   *
   * \param[in] size The size, in bytes, of the memory allocation request.
   *
   * \return A valid pointer to memory if the allocation succeeded, \c NULL
   * if the allocation failed.
   */

  void *(RIPCALL *alloc)(sw_memory_instance *instance, size_t size) ;

  /** \brief Free a previously allocated block.
   *
   * \param[in] instance A sw_memory_instance pointer provided by the RIP.
   *
   * \param[in] memory A pointer previously allocated by the \p alloc()
   * method, which has not yet been freed. It is acceptable to pass \c NULL
   * as the memory parameter, it will be ignored.
   */
  void (RIPCALL *free)(sw_memory_instance *instance, void *memory) ;

  /* End of entries present in SW_MEMORY_API_VERSION_20071110 */
} ;

#ifdef __cplusplus
}
#endif

/** \} */ /* end PLUGIN_callbackAPI group */
/** \} */ /* end interface group */


#endif /* __SWMEMAPI_H__ */
