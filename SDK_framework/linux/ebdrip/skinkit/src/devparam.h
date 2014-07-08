/* Copyright (C) 2005-2011 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWskinkit!src:devparam.h(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

#ifndef __DEVPARAM_H__
#define __DEVPARAM_H__

/**
 * \file
 * \ingroup skinkit
 *
 * \brief Contains definitions shared between the PageBuffer (pgbdev)
 * and Screening (scrndev) example device implememtations.
 */

#include "swdevice.h"
#include "skinkit.h"

/** Bitmask of parameter attributes. */
enum {
  PARAM_READONLY = 0,  /**< no flags */
  PARAM_SET = 1,       /**< parameter now has explicitly set valid value */
  PARAM_WRITEABLE = 2, /**< set_param call is allowed to change the value */
  PARAM_RANGE = 4      /**< Says range is given and should be checked */
} ;

/**
 * \brief Encapsulates the meta-data for a single device parameter.
 *
 * Linking device parameter names with their values and providing a
 * semi-automatic method for parsing and accepting values is done via
 * this structure type and an array of these, an element for
 * each parameter.  See set_param, etc. for how these are used.
 */
typedef struct PARAM {
  const char *name;
  int32    namelen;
  int32    flags;
  int32    type;
  void   * addr;
  int32    minval;
  int32    maxval;
  /** External hook (set by skin) called when the parameter changes.
      Return value is one of the Param* values from swdevice.h. */
  SwLeParamCallback *skin_hook ;
} PARAM;

/** \brief Utility function to set skin callbacks.
 *
 * This function should be called through a trampoline function that
 * passes in the parameter array and size. To remove a callback hook, call
 * this function with a \c NULL function pointer.
 *
 * \param params Array of \c PARAM structures in which to add a hook.
 *
 * \param n_params The number of parameters in \a params.
 *
 * \param paramname The parameter name to monitor.
 *
 * \param paramtype The type of the parameter name to monitor. The callback
 * functions use a generic pointer to pass the changed parameter value.
 * Passing the expected type in with the hook function lets the skinkit check
 * that the hook function will be passed the type it expects.
 *
 * \param pfnParamCallback Pointer to a function though which the RIP informs
 * the skin of parameter changes.
 *
 * \retval FALSE If the callback could not be set (either the name of the
 * parameter was not found, or the type of the parameter is incorrect).
 *
 * \retval TRUE If the callback was set.
 */
int32 KParamSetCallback(PARAM params[], size_t n_params,
                        const char *paramname, int32 paramtype,
                        SwLeParamCallback *pfnParamCallback) ;

/**
 * \brief This function informs the skin whenever a monitored parameter
 * changes. If no callback has been registered, nothing happens.
 *
 * \param pfnParamCallback The callback function registered for the parameter.
 *
 * \param param The parameter address.
 *
 * \returns One of the Param* enumeration values from swdevice.h.
 */
int32 KCallParamCallback(SwLeParamCallback *pfnParamCallback,
                         const void *param);



#endif /* __DEVPARAM_H__ */
