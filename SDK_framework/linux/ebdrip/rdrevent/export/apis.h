/** \file
 * \ingroup EVENTS
 *
 * $HopeName: SWrdrapi!apis.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 *
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for
 * any reason except as set forth in the applicable Global Graphics license
 * agreement.
 *
 * \brief  This header file defines Types of RDR_CLASS_API, used to identify
 * APIs exposed through the RDR system.
 *
 * When registering an API with RDR, the API version number in date form should
 * be used as the RDR Id, eg:
 *
 * \code
 * SwRegisterRDR(RDR_CLASS_API, RDR_API_MYAPI, 20110201, &api, sizeof(api), 0) ;
 * \endcode
 */

#ifndef __APIS_H__
#define __APIS_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */

enum {
  /** Globally important APIs */
  RDR_API_RDR = 1,        /**< RDR gets the pride of place! */
  RDR_API_EVENT,          /**< The Event system */
  RDR_API_PTHREADS,       /**< pthreads, as defined by threads compound */
  RDR_API_TIMER,          /**< Timer API */
  RDR_API_HTM,            /**< Module screening */
  RDR_API_TIMELINE,       /**< The Timeline system */
  RDR_API_DATA,           /**< The sw_data_api */
  RDR_API_JPEG,           /**< JPEG api */

  /** UFST module related APIs */
  RDR_API_CGIF = 123456,  /**< The skin-supplied UFST CGIF and support fns */
  RDR_API_UFSTCALLBACKS   /**< The module-supplied UFST callback fns */
} ;

/* -------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif

#endif /* __APIS_H__ */
