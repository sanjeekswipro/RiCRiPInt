/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_control!swapi.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 *
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * This header file provides definitions common to more than one external
 * module API.
 */


#ifndef __SWAPI_H__
#define __SWAPI_H__

/**
 * \defgroup PLUGIN_swapi Core module interface definitions
 * \ingroup interface
 * \{
 */

#include <stddef.h> /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

/** Return values for common for all SwRegister* calls. */
enum {
  SW_API_REGISTERED = 0,  /**< Module instance successfully registered. */
  SW_API_ERROR,           /**< Non-specific error during registration. */
  SW_API_NOT_UNIQUE,      /**< Module instance name is not unique. */
  SW_API_NOT_AUTHORISED,  /**< Instance is not authorised to register. */
  SW_API_VERSION_TOO_OLD, /**< Interface version is too old for RIP. */
  SW_API_VERSION_TOO_NEW, /**< Interface version is too new for RIP. */
  SW_API_BAD_UNICODE,     /**< Module's display name is not valid UTF-8. */
  SW_API_INCOMPLETE,      /**< A required entry is incorrect (e.g. a method
                               function pointer is NULL.) */
  SW_API_INSTANCE_SIZE,   /**< The instance size is too small for the
                               version. */
  SW_API_INVALID,         /**< An entry has an invalid value. */
  SW_API_TOO_EARLY,       /**< SwInit() has not been called yet. */
  SW_API_INIT_FAILED      /**< Initialisation of the module failed. */
} ;
/** \brief Result type for SwRegister* calls. */
typedef int sw_api_result ;

/** \brief Type for interface version numbers. */
typedef uint32 sw_api_version ;

/** \brief Required first member for all APIs. */
typedef struct sw_api_info {
  sw_api_version version ;    /**< The version number of the API. */
  const uint8 *name ;         /**< NUL-terminated internal name used for
                                   configuration. Internal names of
                                   registered modules must be unique within
                                   each module type. */
  const uint8 *display_name ; /**< NUL-terminated UTF-8 string, possibly
                                   localised for GUI display. The core RIP
                                   does not use this value. */
  size_t instance_size ;      /**< The size that the RIP will allocate to
                                   construct an \b instance of the API
                                   implementation in which the sw_api_info
                                   structure is embedded. */
} sw_api_info ;

/** \} */ /* end Doxygen grouping */

#ifdef __cplusplus
}
#endif


#endif /* __SWAPI_H__ */
