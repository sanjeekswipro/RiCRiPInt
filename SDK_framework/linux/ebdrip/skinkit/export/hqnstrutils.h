/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!export:hqnstrutils.h(EBDSDK_P.1) $
 */

#ifndef __HQN_STR_UTILS_H__
#define __HQN_STR_UTILS_H__

#include "std.h"


/**
 * \file
 * \ingroup skinkit
 * \brief String-related utility functions.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Implementation of <code>strtod()</code> using the C locale.
 *
 * \param[in]  pStr      Null-terminated string to convert
 * \param[out] ppEndStr  Pointer to character that stops scan
 * \return the value of the floating-point number, except when the
 *         representation would cause an overflow, in which case it
 *         returns <code>+/- HUGE_VAL</code>.
 *
 * \note Equivalent to <code>strtod_l(pStr, ppEndStr, NULL)</code> which is
 *       available as part of the Standard C library but not available
 *       on all platforms.
 */
extern double strToDouble(char* pStr, char** ppEndStr);


/**
 * \brief Convert a <code>double</code> to string using the C locale.
 *
 * \param[in]  pStr   The buffer to contain the string
 * \param[in]  nSize  The size of the preallocated pStr buffer
 * \param[in]  fVal   The value to convert
 * \return a pointer to pStr
 *
 * \note The buffer pointed to by pStr should be big enough to store the
 *       string result otherwise "0" is returned.. This function is needed to
 *       create locale independent printed floats for PostScript.
 */
extern const char* doubleToStr(char* pStr, int32 nSize, double fVal);


/**
 * \brief Simple version of strdup() since some platforms don't have it.
 */
extern uint8* utl_strdup(uint8* str);


#ifdef __cplusplus
}
#endif

#endif /* __HQN_STR_UTILS_H__ */
