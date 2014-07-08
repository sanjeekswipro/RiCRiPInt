/** \file
 * \ingroup cstandard
 *
 * $HopeName: HQNc-standard!export:hqcstass.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * "Custom" Assert and Trace functions (called from HqAssert and HqTrace).
 * Each platform should define its own version of these functions.
 *
 * Typically HqCustomAssert should display the message to the user (unless
 * it is NULL) and interrupt the program (or drop into a debugger).
 *
 * HqCustomTrace will usually do nothing.
 */

#ifndef __HQCSTASS_H__
#define __HQCSTASS_H__

/* ----------------------- Includes ---------------------------------------- */

#include <stdarg.h>
#include "hqncall.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------- Types ------------------------------------------- */

enum { AssertNotRecursive, AssertRecursive };

/** \brief Function type for assert handler. */
typedef void (HQNCALL HqCustomAssert_fn)(const char *pszFilename, int nLine,
                                         const char *pszFormat, va_list vlist,
                                         int assertflag);

/** \brief Function type for trace handler. */
typedef void (HQNCALL HqCustomTrace_fn)(const char *pszFilename, int nLine,
                                        const char *pszFormat, va_list vlist);

typedef struct {
  HqCustomAssert_fn *assert_handler ;
  HqCustomTrace_fn  *trace_handler ;
} HqAssertHandlers_t ;

/** \brief Function type that gets assertion functions from a module. */
typedef void (HQNCALL GetHqAssertHandlers_fn)(HqAssertHandlers_t *functions) ;

/* ----------------------- Functions --------------------------------------- */

/* \brief Set custom functions to handle assertions and tracing.

   HQNc-standard provides default implementations of HqCustomAssert and
   HqCustomTrace that write to stderr and stdout. These are not appropriate
   for all use cases, so we allow them to be overridden by calling this
   function. */
void HQNCALL SetHqAssertHandlers(HqAssertHandlers_t *functions) ;

/** \brief Get the custom assertion functions provided by a module.

    This function is used by some dynamic libraries to hook into assertion
    functions provided by the host program. The linker options should be set
    to export this function for dynamic libraries that provide assert
    handlers. */
void HQNCALL GetHqAssertHandlers(HqAssertHandlers_t *functions) ;

/* It is customary to name the assert handler functions HqCustomAssert and
   HqCustomTrace. These prototypes exist to help make them easy to use. */
void HQNCALL HqCustomAssert(const char *pszFilename, int nLine,
                            const char *pszFormat, va_list vlist,
                            int assertflag);
void HQNCALL HqCustomTrace(const char *pszFilename, int nLine,
                           const char *pszFormat, va_list vlist);

#ifdef __cplusplus
}
#endif

#endif  /* __HQCSTASS_H__ */

