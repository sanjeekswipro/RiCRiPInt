/**
 * $HopeName: HQNlibicu_3_4!export:unicode:phqn.h(EBDSDK_P.1) $
 * \file
 * \brief
 * Platform and configuration definitions for ICU using HQNc-standard and
 * HQNc-unicode definitions.
 * Adapted from HQNlibicu!export:unicode:phqn.h(trunk.4)
 *
 * Copyright (C) 2004-2006 Global Graphics Software Ltd.  All Rights Reserved.
 *Global Graphics Software Ltd. Confidential Information.
 */

#ifndef PHQN_H
#define PHQN_H 1


/* Get HQNc-standard definitions. */
#include "std.h"

/* uniset.cpp defines these as functions. */
#undef max
#undef min

/* Define whether inttypes.h is available */
#define U_HAVE_INTTYPES_H 0

/*
 * Define what support for C++ streams is available.
 *     If U_IOSTREAM_SOURCE is set to 199711, then <iostream> is available
 * (1997711 is the date the ISO/IEC C++ FDIS was published), and then
 * one should qualify streams using the std namespace in ICU header
 * files.
 *     If U_IOSTREAM_SOURCE is set to 198506, then <iostream.h> is
 * available instead (198506 is the date when Stroustrup published
 * "An Extensible I/O Facility for C++" at the summer USENIX conference).
 *     If U_IOSTREAM_SOURCE is 0, then C++ streams are not available and
 * support for them will be silently suppressed in ICU.
 *
 */

#ifndef U_IOSTREAM_SOURCE
#define U_IOSTREAM_SOURCE 0
#endif

#ifndef UCONFIG_PLUGGABLE_FILE_IO
#define UCONFIG_PLUGGABLE_FILE_IO 1
#endif

/* Linked statically with the RIP */
#define U_STATIC_IMPLEMENTATION 1

/* TODO: XP_MAC on macintosh? */

/*===========================================================================*/
/* Compiler and environment features                                         */
/*===========================================================================*/

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
# define U_C99 1
#else
# define U_C99 0
#endif

/* Define whether namespace is supported */
#if !defined(VXWORKS)
#define U_HAVE_NAMESPACE 1
#endif

/* Determines the endianness of the platform */
#if defined(highbytefirst)
# define U_IS_BIG_ENDIAN 1
#else
# define U_IS_BIG_ENDIAN 0
#endif

/* 1 or 0 to enable or disable threads.  If undefined, default is: enable threads. */
#define ICU_USE_THREADS 0

#if defined(DEBUG_BUILD)
#define U_DEBUG 1
#else
#define U_DEBUG 0
#endif

#if defined(RELEASE_BUILD)
#define U_RELEASE 1
#else
#define U_RELEASE 0
#endif

/* Determine whether to disable renaming or not. This overrides the
   setting in umachine.h which is for all platforms. */
#ifndef U_DISABLE_RENAMING
#define U_DISABLE_RENAMING 0
#endif

/* Determine whether to override new and delete. */
#ifndef U_OVERRIDE_CXX_ALLOCATION
#define U_OVERRIDE_CXX_ALLOCATION 1
#endif

/* Determine whether to override placement new and delete for STL. */
#ifndef U_HAVE_PLACEMENT_NEW
#define U_HAVE_PLACEMENT_NEW 1
#endif

/* Determine whether to enable tracing. */
#if defined(DEBUG_BUILD)
#define U_ENABLE_TRACING 1
#else
#define U_ENABLE_TRACING 0
#endif

/*===========================================================================*/
/* Generic data types                                                        */
/*===========================================================================*/

#include <stdlib.h>
#include <stddef.h>

/* Define the platform we're on. */
#if defined(WIN32)
#ifndef U_WINDOWS
#define U_WINDOWS
#endif
#endif

/* If your platform does not have the <inttypes.h> header, you may
   need to edit the typedefs below. */
#if !U_C99
#if !defined(Solaris)
#if !defined(VXWORKS)
#if !defined(MACOSX) 
/* MacOS X defines these in <ppc/types.h> even when not C99. This is a
   nuisance, it makes compatibility harder. */
#if !defined(__int8_t_defined) /* Linux also, in sys/types.h */
typedef int8 int8_t;
typedef int16 int16_t;
typedef int32 int32_t;
#ifdef HQN_INT64
typedef int64 int64_t;
#endif
/* define this to avoid the reverse double def problem */
#define __int8_t_defined 1
#endif
#endif
typedef uint8 uint8_t;
typedef uint16 uint16_t;
typedef uint32 uint32_t;
#ifdef HQN_INT64
typedef uint64 uint64_t;
#endif

#else
#ifdef HQN_INT64
typedef int64 int64_t;
typedef uint64 uint64_t;
#endif
#endif /* !VXWORKS */

#else
#ifdef HQN_INT64
typedef int64 int64_t;
typedef uint64 uint64_t;
#endif
#endif /* !Solaris */

#endif /* !C99 */

#ifdef HQN_INT64
#define U_INT64_MIN MININT64
#define U_INT64_MAX MAXINT64
#define U_UINT64_MIN MAXUINT64
#ifndef INT64_C
#define INT64_C(x) INT64(x)
#endif
#ifndef UINT64_C
#define UINT64_C(x) UINT64(x)
#endif
#endif

/*===========================================================================*/
/* Information about wchar support                                           */
/*===========================================================================*/

#if defined(WIN32)
#define U_HAVE_WCHAR_H   1
#define U_SIZEOF_WCHAR_T  2
#elif defined(MACOSX)
#define U_HAVE_WCHAR_H   0
#define U_SIZEOF_WCHAR_T  0 /* Reset to 4 in umachine.h, which is right for MacOS X */
#elif defined(VXWORKS)
#define U_HAVE_WCHAR_H   0
#define U_SIZEOF_WCHAR_T  0
#else
#define U_HAVE_WCHAR_H   1
#define U_SIZEOF_WCHAR_T  0 /* Reset to 4 in umachine.h, which is right for Linux */
#endif

/*===========================================================================*/
/* Do we have wcscpy and other similar functions                             */
/*===========================================================================*/

#if defined(WIN32) || C99
#define U_HAVE_WCSCPY    1
#endif

/*===========================================================================*/
/* Information about POSIX support                                           */
/*===========================================================================*/
#undef U_HAVE_NL_LANGINFO
#undef U_HAVE_NL_LANGINFO_CODESET
#undef U_NL_LANGINFO_CODESET

#undef U_TZSET
#define U_HAVE_TIMEZONE 0
#undef U_TZNAME

#define U_HAVE_MMAP     0
#define U_HAVE_POPEN    0

/*===========================================================================*/
/* Symbol import-export control                                              */
/*===========================================================================*/

#ifdef U_STATIC_IMPLEMENTATION
#define U_EXPORT
#define U_EXPORT2
#define U_IMPORT
#elif defined(WIN32)
#define U_EXPORT __declspec(dllexport)
#define U_EXPORT2 __cdecl
#define U_IMPORT __declspec(dllimport)
#else /* !WIN32 */
#define U_EXPORT
#define U_EXPORT2
#define U_IMPORT
#endif /* !WIN32 */

/*===========================================================================*/
/* Code alignment and C function inlining                                    */
/*===========================================================================*/

#define U_INLINE inline

#if defined(_MSC_VER) && defined(_M_IX86)
#define U_ALIGN_CODE(val)    __asm      align val
#else
#define U_ALIGN_CODE(val)
#endif


/*===========================================================================*/
/* Programs used by ICU code                                                 */
/*===========================================================================*/

#undef U_MAKE

/*===========================================================================*/
/* Configuration options for HQN builds. These can be overridden by Jam.     */
/*===========================================================================*/

/* Comment out as we need all of them for transforming
 * data files */
/* Turn normalisation on under variant if required. */
#ifndef UCONFIG_NO_NORMALIZATION
#define UCONFIG_NO_NORMALIZATION 1
#endif

/* Legacy conversions needed (more than ASCII, UTF-*, ISO-8859-1). */
#ifndef UCONFIG_NO_LEGACY_CONVERSION
#define UCONFIG_NO_LEGACY_CONVERSION 0
#endif

/* No word-boundary finding */
#ifndef UCONFIG_NO_BREAK_ITERATION
#define UCONFIG_NO_BREAK_ITERATION 1
#endif

/* No international domain names */
#ifndef UCONFIG_NO_IDNA
#define UCONFIG_NO_IDNA 1
#endif

/* No collation */
#ifndef UCONFIG_NO_COLLATION
#define UCONFIG_NO_COLLATION 1
#endif

/* No formatting */
#ifndef UCONFIG_NO_FORMATTING
#define UCONFIG_NO_FORMATTING 1
#endif

/* No transliteration */
#ifndef UCONFIG_NO_TRANSLITERATION
#define UCONFIG_NO_TRANSLITERATION 1
#endif

/* No regular expressions */
#ifndef UCONFIG_NO_REGULAR_EXPRESSIONS
#define UCONFIG_NO_REGULAR_EXPRESSIONS 1
#endif

/* No service registration */
#define UCONFIG_NO_SERVICE 1

/* Hide deprecated APIs */
#undef U_HIDE_DRAFT_API /* Unfortunately required for ucnv.h */
#undef U_HIDE_DEPRECATED_API /* Unfortunately required for unorm.cpp */

/* Do we allow ICU users to use the draft APIs by default? */
#define U_DEFAULT_SHOW_DRAFT 1

#endif /* PHQN_H */

/* Log stripped */
