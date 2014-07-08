/** \file
 * \ingroup cstandard
 *
 * $HopeName: HQNc-standard!export:hqtypes.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * C Language standard types
 */

/* ----------------------------------------------------------------------------
   header file:         value              author:              Andrew Cave
   creation date:       21_Aug-1987        last modification:   ##-###-####
   description:

           This header file simply defines the real data type.

---------------------------------------------------------------------------- */

#ifndef HQTYPES_H
#define HQTYPES_H

#include "platform.h"

#ifndef NULL
/** \def NULL
    \brief Definition of NULL pointer

    NULL is defined differently for C++ and C. C restricts the use to pointer
    values by casting to (void *). C++ allows implicit conversion. */
#ifdef __cplusplus
/** Define NULL as 0 for C++ to allow 'SomeType * p = NULL' */
#define NULL (0)
#else
/** Define NULL as (void*)0 for C to limit usage to pointers */
#define NULL ((void*)0)
#endif
#endif

/**
 * Macros to map a real number in the range [0,1] to an array index in the
 * range [0,r-1] (i.e. an array with r elements). The macros round to the
 * nearest integer.
 */

/** \brief Conversion of float to index */
#define INDEX_FROM_FLOAT(r, x)  ((int32)((float)((r) - 1)*(x) + 0.5f))
/** \brief Conversion of double to index */
#define INDEX_FROM_DOUBLE(r, x) ((int32)((double)((r) - 1)*(x) + 0.5))

/** \defgroup stdtypes Standard integer and Bool types
    \ingroup cstandard

    Integer and Bool types for specific sizes

    \{
*/

/** \var int8
    \brief 8-bit signed integer */
/** \var uint8
    \brief 8-bit unsigned integer */
/** \var int16
    \brief 16-bit signed integer */
/** \var uint16
    \brief 16-bit unsigned integer */
/** \var int32
    \brief 32-bit signed integer */
/** \var uint32
    \brief 32-bit unsigned integer */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L

/* If we are using C99, use the stdint.h header */
#include <stdint.h>

/* The C99 standard says these need not exist in an implementation. We will
   require that they do exist, otherwise finding the right size is
   platform-dependent. */

typedef int8_t int8 ;
typedef uint8_t uint8 ;
typedef int16_t int16 ;
typedef uint16_t uint16 ;
typedef int32_t int32 ;
typedef uint32_t uint32 ;
typedef int64_t int64 ;
typedef uint64_t uint64 ;

#ifndef MAXUINT64
/** \brief Maximum value of 64-bit unsigned integer */
#define MAXUINT64 UINT64_MAX
#endif

#ifndef MAXINT64
/** \brief Maximum value of 64-bit signed integer */
#define MAXINT64  INT64_MAX
#endif

#ifndef MININT64
/** \brief Minimum value of 64-bit signed integer */
#define MININT64  INT64_MIN
#endif

/* Assume that C99 implementations can do everything */
/** \brief Note that 64-bit integers are available */
#define HQN_INT64 int64_t
/** \brief Addition, subtraction on 64 bits available */
#define HQN_INT64_OP_ADDSUB  1
/** \brief Bitwise operations on 64 bits available */
#define HQN_INT64_OP_BITWISE 1
/** \brief Multiply/divide on 64 bits available */
#define HQN_INT64_OP_MULDIV  1

#else /* !C99 */

/*  Detect 16-bit compiler: only used for IBMPC's targetting DOS.
    The macro M_I86 is apparently predefined by most 16-bit DOS compilers. */
#if defined(IBMPC) && defined(M_I86)

typedef signed   char                   int8;
typedef signed   int                    int16;
typedef signed   long                   int32;

typedef unsigned char                   uint8;
typedef unsigned int                    uint16;
typedef unsigned long                   uint32;

#else

typedef signed   char                   int8;
typedef signed   short                  int16;
typedef signed   int                    int32;

typedef unsigned char                   uint8;
typedef unsigned short                  uint16;
typedef unsigned int                    uint32;

#endif

#if defined(HQN_INT64)
/* 64-bit native integer types. C99 provides these, if on a 64-bit capable
   architecture. The macros HQN_INT64_OP_ASSIGN, HQN_INT64_OP_BITWISE, and
   HQN_INT64_OP_MULDIV should be tested to determine if assignment, bitwise
   operations (and add/subtract), and multiplication/division are supported.
   A latter capability implies the former capabilities. */

/** \var int64
    \brief 64-bit signed integer

    The macro \c HQN_INT64 will be defined by platform.h if the platform
    supports native 64-bit integers. In this case, the \c int64 and \c uint64
    typedefs will be made available. */

/** \var uint64
    \brief 64-bit unsigned integer

    The macro \c HQN_INT64 will be defined by platform.h if the platform
    supports native 64-bit integers. In this case, the \c int64 and \c uint64
    typedefs will be made available. */
typedef signed HQN_INT64 int64 ;
typedef unsigned HQN_INT64 uint64 ;
#endif /* HQN_INT64 */

#endif /* !C99 */

#ifndef MAXUINT8
/** \brief Maximum value of 8-bit unsigned integer */
#define MAXUINT8  ((uint8)255)
#endif

#ifndef MAXINT8
/** \brief Maximum value of 8-bit signed integer */
#define MAXINT8   ((int8)127)
#endif

#ifndef MININT8
/** \brief Minimum value of 8-bit signed integer */
#define MININT8   ((int8)-128)
#endif

#ifndef MAXUINT16
/** \brief Maximum value of 16-bit unsigned integer */
#define MAXUINT16 ((uint16)65535)
#endif

#ifndef MAXINT16
/** \brief Maximum value of 16-bit signed integer */
#define MAXINT16  ((int16)32767)
#endif

#ifndef MININT16
/** \brief Minimum value of 16-bit signed integer */
#define MININT16  ((int16)-32768)
#endif

#ifndef MAXUINT32
/** \brief Maximum value of 32-bit unsigned integer */
#define MAXUINT32 0xffffffffU
#endif

#ifndef MAXINT32
/** \brief Maximum value of 32-bit signed integer */
#define MAXINT32  ((int32)0x7fffffff)
#endif

#ifndef MININT32
/** \brief Minimum value of 32-bit signed integer */
#define MININT32  ((int32)0x80000000)
#endif

#ifndef MAXINT
/** \brief Maximum value of signed integer

    This is a compatibility definition. Please use the explicitly-sized
    maximum/minimum values instead of this define. */
#define MAXINT  MAXINT32
/** \brief Minimum value of signed integer

    This is a compatibility definition. Please use the explicitly-sized
    maximum/minimum values instead of this define. */
#define MININT  MININT32
#endif

#ifndef MAXUINT
/** \brief Minimum value of unsigned integer

    This is a compatibility definition. Please use the explicitly-sized
    maximum/minimum values instead of this define. */
#define MAXUINT MAXUINT32
#endif


/** \brief Maximum value of size_t as in C99.
 */
#ifndef SIZE_MAX
#ifdef PLATFORM_IS_64BIT
#define SIZE_MAX MAXUINT64
#else /* Assume 32 bit platform. */
#define SIZE_MAX MAXUINT32
#endif
#endif

/*  Define an HQASSERT-able expression to check that the definitions
    in this file are correct.  Can't be done by preprocessor, because
    sizeof() is not necessarily understood by CPP. */
#define HQTYPES_H_CHECK \
        ((sizeof(int8)   == 1) && \
         (sizeof(int16)  == 2) && \
         (sizeof(int32)  == 4) && \
         (sizeof(uint8)  == 1) && \
         (sizeof(uint16) == 2) && \
         (sizeof(uint32) == 4) && \
         (MININT+MAXINT == -1) && \
         (MININT+MAXINT == (int32)MAXUINT) && \
         (1))   /* end of expression marker */


/* SAFE_UINT32_TO_INT32
 *
 * Macro to safely convert a uint32 to an int32, being careful to avoid
 * overflow (and hence undefined behaviour)
 */
#define SAFE_UINT32_TO_INT32(x) \
  ((x) > MAXINT32) ? MININT32 + (((x) - MAXINT32) - 1) : (x)


/* CAST_SIGNED_TO_UINT8, CAST_SIGNED_TO_INT8,
 *   CAST_SIGNED_TO_UINT16, CAST_SIGNED_TO_INT16
 *
 * CAST_UNSIGNED_TO_UINT8, CAST_UNSIGNED_TO_INT8,
 *   CAST_UNSIGNED_TO_UINT16, CAST_UNSIGNED_TO_INT16
 *
 * Macros to stuff a <large> int(or unsigned int) into a smaller one,
 * and complain (assert) if it doesn't fit.
 * The RELEASE_BUILD versions of these macros just do the casts.
 * The primary use of these is to keep compilers from complaining about
 * implied casts in the code, but at the same time to check for
 * range overflows.
 *
 * Example:
 *            extern int32 value;
 *            extern uint16 value2;
 *            uint8 local;
 *            int8 local2;
 *
 *            local = CAST_SIGNED_TO_UINT8(value);
 *            local2 = CAST_UNSIGNED_TO_INT8(value2);
 */

/* Deprecated equivalents for the original macros. As people edit the code,
 * please change from the old names (CAST_TO_*) to the new names
 * (CAST_SIGNED_TO_*) */
#define CAST_TO_UINT8(x) CAST_SIGNED_TO_UINT8(x)
#define CAST_TO_INT8(x) CAST_SIGNED_TO_INT8(x)
#define CAST_TO_UINT16(x) CAST_SIGNED_TO_UINT16(x)
#define CAST_TO_INT16(x) CAST_SIGNED_TO_INT16(x)

#if ! defined( ASSERT_BUILD ) /* { */

#define CAST_SIGNED_TO_UINT8(x)      (uint8) (x)
#define CAST_SIGNED_TO_INT8(x)       (int8)  (x)
#define CAST_SIGNED_TO_UINT16(x)     (uint16)(x)
#define CAST_SIGNED_TO_INT16(x)      (int16) (x)
#define CAST_SIGNED_TO_UINT32(x)     (uint32)(x)
#define CAST_SIGNED_TO_INT32(x)      (int32) (x)
#define CAST_SIGNED_TO_SIZET(x)      (size_t)(x)
#define CAST_UNSIGNED_TO_UINT8(x)    (uint8) (x)
#define CAST_UNSIGNED_TO_INT8(x)     (int8)  (x)
#define CAST_UNSIGNED_TO_UINT16(x)   (uint16)(x)
#define CAST_UNSIGNED_TO_INT16(x)    (int16) (x)
#define CAST_UNSIGNED_TO_UINT32(x)   (uint32)(x)
#define CAST_UNSIGNED_TO_INT32(x)    (int32) (x)
#define CAST_UNSIGNED_TO_SIZET(x)    (size_t)(x)
#define CAST_SIZET_TO_INT32(x)       (int32) (x)
#define CAST_SIZET_TO_UINT32(x)      (uint32)(x)
#define CAST_SIZET_TO_INT16(x)       (int16) (x)
#define CAST_SIZET_TO_UINT16(x)      (uint16)(x)
#define CAST_SIZET_TO_DOUBLE(x)      (double)(x)
#define CAST_SIZET_TO_FLOAT(x)       (float) (x)
#define CAST_PTRDIFFT_TO_INT32(x)    (int32) (x)
#define CAST_PTRDIFFT_TO_UINT32(x)   (uint32)(x)
#define CAST_PTRDIFFT_TO_LONG(x)     (long)  (x)
#define CAST_PTRDIFFT_TO_DOUBLE(x)   (double)(x)
#define CAST_PTRDIFFT_TO_UINT16(x)   (uint16)(x)
#define CAST_INTPTRT_TO_UINT32(x)    (uint32)(x)
#define CAST_INTPTRT_TO_INT32(x)     (int32)(x)
#define CAST_UINTPTRT_TO_UINT32(x)   (uint32)(x)
#define CAST_UINTPTRT_TO_INT32(x)    (int32)(x)
#define CAST_PTRDIFFT_TO_INTPTRT(x)  (intptr_t)(x)
#define CAST_PTRDIFFT_TO_UINTPTRT(x) (intptr_t)(x)
#define CAST_LONG_TO_UINT32(x)       (uint32)(x)
#define CAST_LONG_TO_INT32(x)        (int32)(x)
#define CAST_DOUBLE_TO_UINT32(x)     (uint32)(x)
#define CAST_DOUBLE_TO_INT32(x)      (int32)(x)
#define CAST_FLOAT_TO_UINT32(x)      (uint32)(x)
#define CAST_FLOAT_TO_INT32(x)       (int32)(x)

#else /* defined( ASSERT_BUILD ) } { */

#define SMALLEST_UINT8  ((int32) 0 )
#define LARGEST_UINT8   ((int32) 255)
#define SMALLEST_INT8   ((int32) -128)
#define LARGEST_INT8    ((int32) 127)

#define SMALLEST_UINT16 ((int32)  0 )
#define LARGEST_UINT16  ((int32) 65535)
#define SMALLEST_INT16  ((int32) -32768)
#define LARGEST_INT16   ((int32) 32767)

#define CAST_SIGNED_TO_UINT8(x) \
   (uint8)(((int32)(x) < SMALLEST_UINT8 || (int32)(x) > LARGEST_UINT8) ? \
      HQFAIL("Overflow while casting to uint8"), (x) : (x))

#define CAST_SIGNED_TO_INT8(x) \
   (int8)(((int32)(x) < SMALLEST_INT8 || (int32)(x) > LARGEST_INT8) ? \
      HQFAIL("Overflow while casting to int8"), (x) : (x))

#define CAST_SIGNED_TO_UINT16(x) \
   (uint16)(((int32)(x) < SMALLEST_UINT16 || (int32)(x) > LARGEST_UINT16) ? \
      ( HQFAIL("Overflow while casting to uint16")), (x) : (x))

#define CAST_SIGNED_TO_INT16(x) \
   (int16)(((int32)(x) < SMALLEST_INT16 || (int32)(x) > LARGEST_INT16) ? \
      HQFAIL("Overflow while casting to int16"), (x) : (x))

#define CAST_SIGNED_TO_UINT32(x) \
   (uint32)(((x) < 0 || (x) > MAXUINT32) ? \
      ( HQFAIL("Overflow while casting to uint32")), (x) : (x))

#define CAST_SIGNED_TO_INT32(x) \
   (int32)(((x) < MININT32 || (x) > MAXINT32) ? \
      HQFAIL("Overflow while casting to int32"), (x) : (x))

/* Its assumed size_t is always bigger! */
#define CAST_SIGNED_TO_SIZET(x) (size_t)(x)

/* Same for unsigned values */
#define CAST_UNSIGNED_TO_UINT8(x) \
   (uint8)(((x) & ~LARGEST_UINT8) != 0 ? \
      HQFAIL("Overflow while casting to uint8"), (x) : (x))

#define CAST_UNSIGNED_TO_INT8(x) \
   (int8)(((x) & ~LARGEST_INT8) != 0 ? \
      HQFAIL("Overflow while casting to int8"), (x) : (x))

#define CAST_UNSIGNED_TO_UINT16(x) \
   (uint16)(((x) & ~LARGEST_UINT16) != 0 ? \
      HQFAIL("Overflow while casting to uint16"), (x) : (x))

#define CAST_UNSIGNED_TO_INT16(x) \
   (int16)(((x) & ~LARGEST_INT16) != 0 ? \
      HQFAIL("Overflow while casting to int16"), (x) : (x))

#define CAST_UNSIGNED_TO_UINT32(x) \
   (uint32)(((x) > MAXUINT32) ? \
      HQFAIL("Overflow while casting to uint32"), (x) : (x))

#define CAST_UNSIGNED_TO_INT32(x) \
   (int32)(((x) & ~MAXINT32) != 0 ? \
      HQFAIL("Overflow while casting to int32"), (x) : (x))

/* Its assumed size_t is always bigger! */
#define CAST_UNSIGNED_TO_SIZET(x) (size_t)(x)

#define CAST_SIZET_TO_INT32(x) \
   (int32)(((x) > MAXINT32) ? \
      HQFAIL("Overflow while casting to int32"), (x) : (x))

#define CAST_SIZET_TO_UINT32(x) \
   (uint32)(((x) > MAXUINT32) ? \
      HQFAIL("Overflow while casting to uint32"), (x) : (x))

#define CAST_SIZET_TO_INT16(x) \
   (int16)(((x) > LARGEST_INT16) ? \
      HQFAIL("Overflow while casting to int16"), (x) : (x))

#define CAST_SIZET_TO_UINT16(x) \
   (uint16)(((x) > LARGEST_UINT16) ? \
      HQFAIL("Overflow while casting to uint16"), (x) : (x))

#define CAST_SIZET_TO_DOUBLE(x) \
   (double)(((x) > DBL_MAX) ? \
      HQFAIL("Overflow while casting to double"), (x) : (x))

#define CAST_SIZET_TO_FLOAT(x) \
   (float)(((x) > FLT_MAX) ? \
      HQFAIL("Overflow while casting to float"), (x) : (x))

/* x will be a ptrdiff_t, so no casts necessary. */
#define CAST_PTRDIFFT_TO_INT32(x) \
   (int32)(((x) < MININT32 || (x) > MAXINT32) ? \
      HQFAIL("Overflow while casting to int32"), (x) : (x))

/* x will be a ptrdiff_t, so no casts necessary. */
#define CAST_PTRDIFFT_TO_UINT32(x) \
   (uint32)(((x) < 0 || (x) > MAXUINT32) ? \
      HQFAIL("Overflow while casting to uint32"), (x) : (x))

/* x will be a ptrdiff_t, so no casts necessary. */
#define CAST_PTRDIFFT_TO_UINT16(x) \
   (uint16)(((x) < 0 || (x) > LARGEST_UINT16) ? \
      HQFAIL("Overflow while casting to uint16"), (x) : (x))

/* x will be a ptrdiff_t, so no casts necessary. */
#define CAST_PTRDIFFT_TO_LONG(x) \
   (int32)(((x) < MININT32 || (x) > MAXINT32) ? \
      HQFAIL("Overflow while casting to int32"), (x) : (x))

/* x will be a ptrdiff_t, so no casts necessary. */
#define CAST_PTRDIFFT_TO_DOUBLE(x) \
   (double)(((x) < - DBL_MAX || (x) > DBL_MAX) ? \
      HQFAIL("Overflow while casting to double"), (x) : (x))

#define CAST_INTPTRT_TO_UINT32(x) \
   (uint32)(((x) < 0 || (x) > MAXUINT32) ? \
      HQFAIL("Overflow while casting to uint32"), (x) : (x))

#define CAST_INTPTRT_TO_INT32(x) \
   (int32)(((x) < MININT32 || (x) > MAXINT32) ? \
      HQFAIL("Overflow while casting to int32"), (x) : (x))

#define CAST_UINTPTRT_TO_UINT32(x) \
   (uint32)(((x) > MAXUINT32) ? \
      HQFAIL("Overflow while casting to uint32"), (x) : (x))

#define CAST_UINTPTRT_TO_INT32(x) \
   (int32)(((x) > MAXINT32) ? \
      HQFAIL("Overflow while casting to int32"), (x) : (x))

#define CAST_PTRDIFFT_TO_INTPTRT(x)  (intptr_t)(x)
#define CAST_PTRDIFFT_TO_UINTPTRT(x) (intptr_t)(x)

#define CAST_LONG_TO_UINT32(x) \
   (uint32)(((x) > MAXUINT32) ? \
      HQFAIL("Overflow while casting to uint32"), (x) : (x))

#define CAST_LONG_TO_INT32(x) \
   (int32)(((x) > MAXINT32) ? \
      HQFAIL("Overflow while casting to int32"), (x) : (x))

#define CAST_DOUBLE_TO_UINT32(x) \
   (uint32)(((x) > MAXUINT32) ? \
      HQFAIL("Overflow while casting to uint32"), (x) : (x))

#define CAST_DOUBLE_TO_INT32(x) \
   (int32)(((x) > MAXINT32) ? \
      HQFAIL("Overflow while casting to int32"), (x) : (x))

#define CAST_FLOAT_TO_UINT32(x) \
   (uint32)(((x) > MAXUINT32) ? \
      HQFAIL("Overflow while casting to uint32"), (x) : (x))

#define CAST_FLOAT_TO_INT32(x) \
   (int32)(((x) > MAXINT32) ? \
      HQFAIL("Overflow while casting to int32"), (x) : (x))

#endif /* ! defined( ASSERT_BUILD ) } */

#define CAST_INT32_TO_PTRDIFFT(x) (ptrdiff_t)(x)
#define CAST_UINT32_TO_PTRDIFFT(x) (ptrdiff_t)(x)

/** \brief Hqn standard boolean type.

    Note that we do not use the C++ or C99 definitions of boolean types,
    because we want the type to have the same size in function prototypes and
    structures linked between the different languages. We use the native
    machine integer rather than give it an explicit size for efficiency. */
typedef int HqBool ; /* X11 uses the definition "Bool" for int. */

/** Values for boolean type. */
#ifndef TRUE
/** \brief Bool true value

    C boolean conventions are used. Any non-zero value is true. */
#define TRUE 1
/** \brief Bool false value

    C boolean conventions are used. Zero values are false. */
#define FALSE 0
#endif

/** \brief Validate boolean values

    Check a boolean value to make sure it is true or false. This should only
    be used for Bool variables. C language conventions are normally used, so
    non-zero values are true and zero values are false. */
#define BOOL_IS_VALID(bool_) ((bool_) == TRUE || (bool_) == FALSE)

#ifdef PLATFORM_IS_64BIT
/** Device file descriptors on 64 bit platforms will be 64 bits
   wide. */
typedef intptr_t HqnFileDescriptor ;

#else /* Assume 32 bit platform. */
/** Device file descriptors on 32 bit platforms will be 32 bits
   wide. int32 is used for backward compatibility. */
typedef int32 HqnFileDescriptor ;
#endif

/** \} */

#endif /* HQTYPES_H */

