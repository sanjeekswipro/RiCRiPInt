/** \file
 * \ingroup cstandard
 *
 * $HopeName: HQNc-standard!export:c99protos.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * Define prototypes for C99 functions that may not be available in C89 mode
 * compilers.
 */

#ifndef __C99PROTOS_H__
#define __C99PROTOS_H__

#if defined(_MSC_VER)

/* MSVC doesn't define __STDC__ unless -Za is specified on the command line
   for strict conformance. That option unfortunately disables some useful
   intrinsics. However, the compiler does include vsnprintf and snprintf in
   stdio and stdarg. Microsoft also don't seem to appreciate that some code
   is supposed to be cross-platform. */
#  include <stdio.h>  /* snprintf */
#  include <stdarg.h> /* vsnprintf */

#  define snprintf _snprintf /* Grrr. */

#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L

/* Standard C99-compatible. */
#  include <stdio.h>  /* snprintf */
#  include <stdarg.h> /* vsnprintf */

#elif defined(__APPLE_CC__) && defined(_FORTIFY_SOURCE) && _FORTIFY_SOURCE > 0

/* Apple compiler targeting 10.5+ */
#  include <stdio.h>  /* snprintf */
#  include <stdarg.h> /* vsnprintf */

#else /* not C99 */

/* We need va_list for vsnprintf definition. */
#  if defined(__STDC__) || defined(_MSC_VER)
#    include <stdarg.h>
#  else
#    include <varargs.h>
#  endif
#  include <stddef.h> /* size_t */

extern int snprintf(char *, size_t, const char *, ...);
extern int vsnprintf(char *, size_t, const char *, va_list);

#endif /* not standard C */

#endif
