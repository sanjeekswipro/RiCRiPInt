/* impl.h.mpslib: RAVENBROOK MEMORY POOL SYSTEM LIBRARY INTERFACE
 *
 * $Id: export:mpslib.h,v 1.15.1.1.1.1 2013/12/19 11:27:03 anon Exp $
 * $HopeName: SWmps!export:mpslib.h(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2002-2013 Global Graphics Software Ltd. All rights reserved.
 *
 * .readership: MPS client application developers, MPS developers.
 * .sources: design.mps.lib
 *
 * .purpose: The purpose of this file is to declare the functions and types
 * required for the MPS library interface.
 */

#ifndef mpslib_h
#define mpslib_h

#include "mps.h"

#include <stddef.h>

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
# include <stdint.h>
  typedef uint_fast64_t mps_clock_t ;
#elif defined(_MSC_VER) && _MSC_VER >= 1200
  typedef unsigned __int64 mps_clock_t ;
#elif defined(__GNUC__)
# ifdef _LP64
    typedef unsigned long int mps_clock_t ;
# else
    typedef unsigned long long int mps_clock_t ;
# endif
#else
# error No suitable definition for mps_clock_t
#endif

extern int MPS_CALL mps_lib_get_EOF(void);
#define mps_lib_EOF     (mps_lib_get_EOF())

typedef struct mps_lib_stream_s mps_lib_FILE;

extern mps_lib_FILE* MPS_CALL mps_lib_get_stderr(void);
extern mps_lib_FILE* MPS_CALL mps_lib_get_stdout(void);
#define mps_lib_stderr  (mps_lib_get_stderr())
#define mps_lib_stdout  (mps_lib_get_stdout())

extern int MPS_CALL mps_lib_fputc(int, mps_lib_FILE *);
extern int MPS_CALL mps_lib_fputs(const char *, mps_lib_FILE *);

extern void MPS_CALL mps_lib_assert_fail(const char *);

extern void *(MPS_CALL mps_lib_memset)(void *, int, size_t);
extern void *(MPS_CALL mps_lib_memcpy)(void *, const void *, size_t);
extern int (MPS_CALL mps_lib_memcmp)(const void *, const void *, size_t);


extern mps_clock_t MPS_CALL mps_clock(void);


extern unsigned long MPS_CALL mps_lib_telemetry_control(void);

/** \brief Filter function for events logged to MPS log.

    \param[in] event  A pointer to an MPS EventUnion structure.
    \param[in] length The length of the MPS EventUnion structure.

    \retval TRUE  Add event to MPS log
    \retval FALSE Do not add event to MPS log
*/
extern mps_bool_t MPS_CALL mps_lib_event_filter(const void *event, size_t length) ;

/** \brief Set MPS telemetry logging default options. */
extern void MPS_CALL mps_lib_telemetry_defaults(const char *filename,
                                                unsigned long options) ;

#endif /* mpslib_h */
