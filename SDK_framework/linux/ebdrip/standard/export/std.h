/** \file
 * \ingroup cstandard
 *
 * $HopeName: HQNc-standard!export:std.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * Standard types and definitions for use in all C/C++ products.
 */

#ifndef STD_H
#define STD_H

/*
 *      Standard header file to be included by all products
 */

/* ----------------------- Includes ---------------------------------------- */

#include "platform.h"
#include "hqncall.h"
#include "proto.h"
#include "osprotos.h"   /* This must be before hqtypes.h */
#include "hqtypes.h"    /* platform.h must be included before this file */
#include "hq32x2.h"
#include "hqassert.h"
#include "warnings.h"
#include "hqbitops.h"
#include "hqstr.h"

/**
 * \defgroup cstandard C Standard Definitions.
 * \ingroup core
 *
 * These definitions provide a common set of sized integral types, and
 * platform abstraction macros.
 *
 * \{
 */


/* ----------------------- Macros ------------------------------------------ */

#ifndef MACRO_START
/** MACRO_START should be used at the start of multi-line macros, to make them
    safe for use in all contexts. */
#define MACRO_START do {

/** MACRO_END should be matched with MACRO_START at the end of multi-line
    macros, to make them safe for use in all contexts. */
#ifndef lint
#define MACRO_END } while (0)
#else
extern int lint_flag;
#define MACRO_END } while (lint_flag)
#endif
#endif

/** EMPTY_STATEMENT should be used when no statement is required (for example
    in a for loop whose conditions are sufficient to search for and find an
    item), to prevent compiler warnings. */
#ifndef EMPTY_STATEMENT
#define EMPTY_STATEMENT() MACRO_START MACRO_END
#endif

/*
 * Some debuggers ignore static variables/functions - to get a debugable
 * version of a particluar file, declare STATIC to be nothing at the top of it.
 */
#ifndef STATIC
#define STATIC static

/** Define an expression that can be used with HQASSERT to check that
    all assumptions/preconditions are satisfied on any given target
    platform.  Since some conditions can't be tested at preprocessor
    time (most notably sizeof(..) values), the expression must be
    used in a runtime expression like HQASSERT. */
#define STD_H_CHECK \
        (HQTYPES_H_CHECK)  /* && with any others as necessary */

#endif  /* !STATIC */

/*
 * Definitions of min, max.
 */
#ifdef MIN
#undef MIN
#endif

#ifdef MAX
#undef MAX
#endif

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#if !defined(_MSC_VER)

#define min(a,b) ( ((a) < (b)) ? (a) : (b) )
#define max(a,b) ( ((a) > (b)) ? (a) : (b) )

#else /* _MSC_VER */

/* MSVC optimised version. These intrinsics are in all version of the CRT
   since Win98. */
#define min(a,b) __min((a),(b))
#define max(a,b) __max((a),(b))


#endif /* _MSC_VER */

#if ! defined( ASSERT_BUILD )

#define ptrdiff_uint32(x) ((uint32)((ptrdiff_t)(x)))
#define ptrdiff_int32(x) ((int32)((ptrdiff_t)(x)))

#else

#define ptrdiff_uint32(x) CAST_PTRDIFFT_TO_UINT32(x)
#define ptrdiff_int32(x) CAST_PTRDIFFT_TO_INT32(x)

#endif

/** Return the number of items in an initialised array. This can be used in
    initialisers. */
#define NUM_ARRAY_ITEMS( _array_ ) \
 ( sizeof( _array_ ) / sizeof( (_array_)[ 0 ] ) )

#define STRING_AND_LENGTH(s)  (uint8*)(""s""), sizeof(""s"") - 1

/* \} */

#endif  /* _STD_H_ */

