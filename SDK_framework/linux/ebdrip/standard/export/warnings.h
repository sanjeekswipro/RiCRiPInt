/** \file
 * \ingroup cstandard
 *
 * $HopeName: HQNc-standard!export:warnings.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * Macros and such used primarily to eliminate compiler warnings.
 *
 * The intent is that this file should contain macros which are or
 * can be used for various compilers to work around various warnings
 * which the compilers generate.
 */

#ifndef __WARNINGS_H__
#define __WARNINGS_H__

#ifdef _MSC_VER
/*
 * For MSVC compiler, we don't care about the following warnings:
 * warning C4127: conditional expression is a constant
 *
 * NOTE: Any warnings turned off here will be turned off for all source code
 * in the translation unit appearing after the include of this file.  If you
 * just need to prevent warnings in 3rd party include files then use the
 * following pattern:
 *
 * #pragma warning(disable: dddd ...)
 * #include "3rdparty.h"
 * #pragma warning(default: dddd ...)
 */
#pragma warning (disable : 4127)

#endif




/*****************************************************
 * UNUSED(type, param) - unused formal parameter.
 * Creates a dummy usage of an unused parameter for those compilers
 * which do not understand the "unused" pragma.  "type" is the parameter
 * type.
 *
 *****************************************************/

#if defined(lint) || defined(NO_DUMMYPARAM)

# define UNUSED_PARAM(type, param) /* VOID */

#else  /* !defined(lint) && [!NO_DUMMYPARAM] */

#if defined(MAC68K) && !defined(__GNUC__) && !defined(__SC__)
       /* define UNUSED_PARAM for MPW "C" compiler, whose optimizer is dumb */
#define UNUSED_PARAM(type, param) \
  { param; }

#else

#ifdef SGI
       /* The SGI compiler can use the definition used below. */
#define UNUSED_PARAM(type, param) \
  (void)param;

#else
       /* definition of UNUSED_PARAM for all other compilers */
#define UNUSED_PARAM(type, param) \
  { \
     type dummy_param; \
     type dummy_param2; \
     dummy_param = param; \
     dummy_param2 = dummy_param; \
     dummy_param = dummy_param2; \
  }

#endif /* SGI */
#endif /* which compiler? */
#endif /* defined(HAS_PRAGMA_UNUSED) || defined(lint) */



#endif  /* __WARNINGS_H__ */

