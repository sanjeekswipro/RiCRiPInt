/** \file
 * \ingroup platform
 *
 * $HopeName: HQNc-standard!export:platform.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2014 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * Platform-dependent defines. These definitions are used to turn on or off
 * features in the code. Products should use abstract definitions to turn
 * features on or off, rather than using compiler, processor or platform
 * tests directly. The abstract feature definitions are set in this file.
 */

#ifndef __PLATFORM_H__
#define __PLATFORM_H__

/**
 * \defgroup platform Platform-dependent defines.
 * \ingroup cstandard
 * \{
 */


/* ========================================================================= */
/* === PLATFORMS SECTION === */
/* ========================================================================= */

/* The productCode in the dongle now holds a byte describing the
 * platform(s) the product may run on and PLATFORM_ID contains a byte
 * which this is checked against.  The highest 3 bits identify the
 * operating system and the lowest 5 bits identify the machine. If the
 * level number in the dongle is zero the product will run on all
 * platforms.  NB: A gap of several numbers has been left after
 * Macintosh for the addition of new Macintosh platforms since the
 * Mactivator only returns a byte and therefore would not be able to
 * read the entire productCode in the event of it being expanded into
 * a word.
 */

#define PLATFORM_MACHINE_MASK   0x1f
#define PLATFORM_OS_MASK        0xe0

#define PLATFORM_MACHINE        ( PLATFORM_ID & PLATFORM_MACHINE_MASK )
#define PLATFORM_OS             ( PLATFORM_ID & PLATFORM_OS_MASK )

#define P_MACOS           (0<<5)
#define P_MACOS_PARALLEL  (1<<5)
#define P_UNIX_PARALLEL   (2<<5)
#define P_WINNT_PARALLEL  (3<<5)
#define P_UNIX            (4<<5)
#define P_WINNT           (5<<5)
#define P_WIN31           (6<<5)
#define P_CUSTOM          (7<<5)

#define P_MAC68K        1
#define P_POWERMAC      2
#define P_INTEL         7
#define P_ALPHA         8
#define P_DESKSTATION   9
#define P_SPARC         10
#define P_SGI           11
#define P_HPSNAKE       12
#define P_CLIPPER       13
#define P_RS6000        14
#define P_HP9000        15
#define P_T188          16
#define P_MC88100       17
#define P_AM29K         18
#define P_MIPS          19
#define P_PPC           20
#define P_ARM200        21
#define P_ARMv5         22
#define P_INTEL64       23
#define P_ARMv6         24
#define P_ARMv7         25

/* Turn gcc ARM defines into generic ARM defines. */
/* If your compiler does not define what you expect, then check 
 * to see if it is one of the sub variants. e.g.
 *     __ARM_ARCH_7__ is not defined, but __ARM_ARCH_7A__ is.
*/
#if defined(__ARM_ARCH_5__)
#  if !defined(ARM_ARCH)
#    define ARM_ARCH 5
#  elif ARM_ARCH != 5
#    error Incompatible ARM architectures defined
#  endif
#elif defined(__ARM_ARCH_6__)
#  if !defined(ARM_ARCH)
#    define ARM_ARCH 6
#  elif ARM_ARCH != 6
#    error Incompatible ARM architectures defined
#  endif
#elif defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__)
#  if !defined(ARM_ARCH)
#    define ARM_ARCH 7
#  elif ARM_ARCH != 7
#    error Incompatible ARM architectures defined
#  endif
#endif

#if defined(__ARMEL__)
#  if !defined(ARM_LE)
#    define ARM_LE
#  elif defined(ARM_BE)
#    error Incompatible ARM endianness defined
#  endif
#elif defined(__ARMEB__)
#  if !defined(ARM_BE)
#    define ARM_BE
#  elif defined(ARM_LE)
#    error Incompatible ARM endianness defined
#  endif
#endif

/* NetBSD */
#if defined(__NetBSD__)
#  define NORMAL_OS               P_UNIX
#  if !defined(__i386)
#    error "NetBSD: only Intel 32bit builds supported"
#  endif /* !i386 */
#  undef PLATFORM_IS_64BIT
#  define PLATFORM_IS_32BIT       1
#  define FLOAT_TO_INT_IS_SLOW    1
#  define lowbytefirst            1
#  define bitsgoright             1
   /* MACHINE is also defined /usr/include/sys/param.h */
#  if defined(MACHINE)
#    undef MACHINE
#  endif /* MACHINE */
#  define MACHINE                 P_INTEL
#  define MACHINE_WITHOUT_64BIT   P_INTEL

#  define HAS_STATVFS             1

/* For without these rocks will fall from the sky and there will be much gnashing
 * of teeth. */
#  include <float.h>
#  include <stddef.h>
#  include <stdint.h>

#  define HQN_INT64 long long int
#  define HQN_INT64_OP_ADDSUB  1 /* Addition, subtraction on 64 bits available */
#  define HQN_INT64_OP_BITWISE 1 /* Bitwise operations on 64 bits available */
#  define HQN_INT64_OP_MULDIV  1 /* Multiply/divide on 64 bits available */
#  define MAXUINT64 0xffffffffffffffffULL
#  define MAXINT64  0x7fffffffffffffffLL
#  define MININT64  0x8000000000000000LL
   /* 64-bit constants */
#  define INT64(x) x ## LL
#  define UINT64(x) x ## ULL

#endif /* __NetBSD__ */

/* ------------------------------------------------- */
/* VXWORKS */
/* ------------------------------------------------- */
#if defined(VXWORKS)
#  include <vxWorks.h>
#  include <stdarg.h>
#  include <setjmp.h>
#  include <stddef.h>
#  include <float.h>

#  define NORMAL_OS            P_UNIX
#  define NO_PTHREADS
   /* #define waitpid_defined  0 */
#  undef I960

#  if defined(_PPC_)
#    define MACHINE            P_PPC
#    define highbytefirst      1
#    define bitsgoright        1
#    define PLATFORM_IS_32BIT  1
#    undef PLATFORM_IS_64BIT
#  endif /* _PPC_ */

#  if defined(_MIPS_)
#    define MACHINE            P_MIPS
#    define highbytefirst      1
#    define bitsgoright        1
#    define PLATFORM_IS_32BIT  1
#    undef PLATFORM_IS_64BIT
#  endif /* _MIPS */

   /* #if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
      #if defined(__GNUC__) */

#  if ! defined( S_SPLINT_S )
     typedef long intptr_t ;
     typedef unsigned long uintptr_t ;
#  endif

   /* #endif __GNUC__
      #endif !C99 */

   /* Need own va_copy which copies pointer contents */
#  ifndef va_copy
#    ifdef __va_copy
#      define va_copy(dest, src) __va_copy((dest), (src))
#    else
#      define va_copy(dest, src) *(dest) = *(src)
#    endif
#  endif

#endif /* VXWORKS */

/* THREADX/ARM9 */
/* Presently not sure about endianness of processor need to re-visit */

#if defined(CPU_ARM9)

#  ifndef THREADX
#    define THREADX             1
#  endif

#  define MACHINE                 P_ARMv5
#  undef Unaligned_32bit_access
#  define USE_PROTOTYPES_MIXED
#  define lowbytefirst            1
#  define bitsgoright             1
#  define FLOAT_TO_INT_IS_SLOW    1
#  define PLATFORM_IS_32BIT       1
#  undef  PLATFORM_IS_64BIT

#elif defined(CPU_ARM)

#  error "Need to define the other ARM types"

#endif

/* ------------------------------------------------- */
/* THREADX */
/* ------------------------------------------------- */
#if defined(THREADX)
#  include <stdarg.h>
#  include <setjmp.h>
#  include <stddef.h>
#  include <float.h>

#  define NORMAL_OS            P_CUSTOM
#  define PARALLEL_OS          P_CUSTOM

#  define NO_PTHREADS
   /* #define waitpid_defined  0 */
#  undef I960

#  include <stdint.h>

#  ifndef __intptr_t_defined
#    define __intptr_t_defined   1
#  endif
#  ifndef _INTPTR_T_DEFINED
#    define _INTPTR_T_DEFINED    1
#  endif

#  ifndef __uintptr_t_defined
#    define __uintptr_t_defined  1
#  endif
#  ifndef _UINTPTR_T_DEFINED
#    define _UINTPTR_T_DEFINED   1
#  endif

#  define off_t                  long

#  define NO_PTHREADS


#endif /* THREADX */

/* ------------------------------------------------- */
/* WINDOWS */
/* ------------------------------------------------- */
#if (defined(IBMPC) || defined(WIN32) || defined(I960) || defined(_WIN64))

#  if defined(_WIN64)
#    define PLATFORM_IS_64BIT  1
#    undef PLATFORM_IS_32BIT
#  elif defined(WIN32)
#    define PLATFORM_IS_32BIT  1
#    undef PLATFORM_IS_64BIT
#  endif

#  include <stddef.h>
#  include <float.h>

#  if defined(_MSC_VER) && _MSC_VER >= 1500 /* if VC 9 or greater */
#    include <basetsd.h>
#  endif

   /* Only VC 6 or less builds need these defined. */
#  if defined(_MSC_VER) && _MSC_VER < 1310

     /* WIN DDK defines these. See io.h and stdarg.h in WIN DDK
        2600.1106. If not using the WIN DDK, we need to define (u)intptr_t
        ourselves. */
#    ifndef _INTPTR_T_DEFINED
       typedef int intptr_t ;
#    endif
#    ifndef _UINTPTR_T_DEFINED
       typedef unsigned int uintptr_t ;
#    endif

#  elif defined(__MINGW32__)
#    include <stdint.h>
#  endif

   /* A build on Windows assumes P_WINNT. At run time the application may
    * want to take into account that the platform it is running
    * on could be Win3.1, Win95, NT, Win2000, WinXP, or Win2003.
    */
#  define NORMAL_OS             P_WINNT

#  ifdef MULTI_PROCESS
#    define PARALLEL_OS         P_WINNT_PARALLEL
#  endif

#  define CANNOT_CAST_LVALUE    1
#  define ANSI_SUPER            1

#  define USE_PROTOTYPES_MIXED

#  ifdef WIN32
#    define MULTIDBG_TO_FILE    1
#  endif

#  if defined(_MIPS_)

#    define MACHINE             P_MIPS
#    define lowbytefirst        1
#    define bitsgoright         1

#  elif defined(_PPC_)

#    define MACHINE             P_PPC
#    define lowbytefirst        1
#    define bitsgoright         1

#  elif defined(I960)

     /* Variant of Intel */
#    define MACHINE                 P_INTEL
#    define Unaligned_32bit_access  1
#    define off_t                   long  /* offset, for lseek */
#    define USE_PROTOTYPES_MIXED
#    define lowbytefirst            1
#    define bitsgoright             1

#  else /* Assume Intel (x86) */

#    if defined (WIN64)
#      define MACHINE                P_INTEL64
#      define MACHINE_WITHOUT_64BIT  P_INTEL
#    else
#      define MACHINE                P_INTEL
#      define MACHINE_WITH_64BIT     P_INTEL64
#    endif
#    define Unaligned_32bit_access   1
#    define lowbytefirst             1
#    define bitsgoright              1
#    define FLOAT_TO_INT_IS_SLOW     1

#  endif /* Machine type */

   /* Calling convention for C runtime functions, e.g. qsort callbacks */
#  define CRT_API __cdecl

   /* int64 support in MSVC applies to all machine types. */
#  if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
#    if defined(_MSC_VER) && _MSC_VER >= 1200
#      define HQN_INT64 __int64
#      define HQN_INT64_OP_ADDSUB  1 /* Addition, subtraction on 64 bits available */
#      define HQN_INT64_OP_BITWISE 1 /* Bitwise operations on 64 bits available */
#      if PLATFORM_IS_64BIT
#        define HQN_INT64_OP_MULDIV 1 /* Multiply/divide on 64 bits available */
#      else
#        undef  HQN_INT64_OP_MULDIV    /* Do NOT allow multiply/divide on 64 bits */
#      endif

#      include <limits.h>

#      ifndef MAXUINT64
#        define MAXUINT64 _UI64_MAX
#      endif

#      ifndef MAXINT64
#        define MAXINT64 _I64_MAX
#      endif

#      ifndef MININT64
#        define MININT64 _I64_MIN
#      endif

       /* 64-bit constants */
#      define INT64(x) x
#      define UINT64(x) x
#    endif /* _MSC_VER */
#  endif /* !C99 */

#endif /* IBMPC */

#if (defined(idtr3081) || defined(idtr3051)) && !defined(idtr30xx)
#  define idtr30xx                1
#endif

#if defined(idtr30xx)

#  define ANSI_SUPER              1
#  define USE_PROTOTYPES_MIXED    1

   /* #define HAS_PRAGMA_UNUSED   1 */
   /* #define CANNOT_CAST_LVALUE  1 */

   /* #define Can_Shift_32        1 */
#  define ZEROMEMORY              1
   /* #define SIGNALTYPE          void */
   /* #define RESETSIGNALS        1 */
   /* #define PID_TYPE            int */  /* rather than pid_t */
#  define NO_UALARM               1
#  define align_to_double         1
   /* #define promoted_real       ? */
   /* #define off_t               long */  /* offset, for lseek */

#  define highbytefirst           1
#  define bitsgoright             1
   /* #define Unaligned_32bit_access  1 */

#endif /* idtr30xx */


/* ------------------------------------------------- */
/* MACINTOSH */
/* ------------------------------------------------- */
#ifdef  MACINTOSH

#  include <float.h>

#  define PLATFORM_IS_32BIT 1
#  undef PLATFORM_IS_64BIT

#  ifndef _MACHTYPES_H_

#    ifndef _INTPTR_T
#      define _INTPTR_T
       typedef long intptr_t;
#    endif

#    ifndef _UINTPTR_T
#      define _UINTPTR_T
       typedef unsigned long uintptr_t;
#    endif

#  endif

#  if !defined(__STDDEF_H__)
#    ifndef _PTRDIFF_T
#      define _PTRDIFF_T
       typedef int ptrdiff_t;
#    endif
#  endif

#  define NORMAL_OS      P_MACOS
#  ifdef MULTI_PROCESS
#    define PARALLEL_OS  P_MACOS_PARALLEL
#  endif

   /* please use tabs every 8 columns for editing this file */
#  define ANSI_SUPER              1
#  define BCOPY_OVERLAP_SAFE /* Since it maps down onto BlockMove */
#  define ZEROMEMORY              1

   /* math.h now uses "long double", but used to be defined with
    * extended.  In PPCC, extended is a synonym for long double.  In C
    * (68K), long double is a synonym for extended.  In GCC neither
    * are synonyms for either.  :-(
    */
#  define MAC_SYSHDRS_LONGDBLPROTOS  1
#  define promoted_real      double


#  define MACHINE                 P_POWERMAC

#  define Unaligned_32bit_access  1
#  ifdef __BIG_ENDIAN__
       /* i.e. PowerPC */
#    define Can_Shift_32          1
#    define highbytefirst         1
#  endif
#  ifdef __LITTLE_ENDIAN__
       /* i.e. Intel */
#   define lowbytefirst           1

#  endif
#  define bitsgoright             1

   /* int64 support in gcc applies to all MACINTOSH platform types */
#  if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
#    if defined(__GNUC__)
#      define HQN_INT64 long long int
#      define HQN_INT64_OP_ADDSUB  1 /* Addition, subtraction on 64 bits available */
#      define HQN_INT64_OP_BITWISE 1 /* Bitwise operations on 64 bits available */
#      undef  HQN_INT64_OP_MULDIV    /* Do NOT allow multiply/divide on 64 bits */

#      define MAXUINT64 0xffffffffffffffffULL
#      define MAXINT64  0x7fffffffffffffffLL
#      define MININT64  0x8000000000000000LL
         /* 64-bit constants */
#      define INT64(x) x ## LL
#      define UINT64(x) x ## ULL
#    endif /* __GNUC */
#  endif /* !C99 */
#endif  /* MACINTOSH */


/* ------------------------------------------------- */
/* OTHER UNIX */
/* ------------------------------------------------- */
#if defined(alpha)
#  define UNIX                    1
#  define MACHINE                 P_ALPHA
#  define highbytefirst           1
#  define bitsgoright             1
#  define align_to_double         1
#  define HAS_SHL_LOAD            1       /* Dynamic code loading */
#  define SIGNALTYPE              void
#  define CHECK_ATAN2_ARGS        1
#endif /* alpha */

#if defined(sun4) || defined(sun3)
#  define UNIX                    1
#  define highbytefirst           1
#  define bitsgoright             1
#  define SIGNALTYPE              void
#endif /* suns */

#ifdef sun386i
#  define UNIX                    1
#  define lowbytefirst            1
#  define bitsgoleft              1
#endif /* sun386i */

#ifdef sparc

#  if defined(Solaris)
#    include <float.h>
#  endif

#  define UNIX                    1
#  define MACHINE                 P_SPARC
#  define highbytefirst           1
#  define bitsgoright             1
#  define PLATFORM_IS_32BIT 1
#  undef PLATFORM_IS_64BIT

#  ifndef _SYS_INT_TYPES_H
     typedef int                     intptr_t;
#    define __intptr_t_defined
     typedef unsigned int            uintptr_t;
#    define __uintptr_t_defined
#  endif

#  define SIGNALTYPE              void
#  define PID_TYPE                int     /* rather than pid_t */
#  define HAS_DLOPEN              1       /* Dynamic code loading */

   /*
    * Really should use some Solaris OS header identifier
    * but I can't find one as yet. This will turn the prototype
    * off for Solaris and Ultrasparc builds
    */
#  if !defined(Solaris) && !defined(Ultrasparc)
#    define NEEDS_SYSTEM_PROTOTYPE  1
#  endif

#  ifndef CLOCKS_PER_SEC
#    define CLOCKS_PER_SEC          1000000
#  endif

#  if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
#    if defined(__GNUC__)
#      define HQN_INT64 long long int
#      define HQN_INT64_OP_ADDSUB  1 /* Addition, subtraction on 64 bits available */
#      define HQN_INT64_OP_BITWISE 1 /* Bitwise operations on 64 bits available */
#      define HQN_INT64_OP_MULDIV  1 /* Multiply/divide on 64 bits available */

#      define MAXUINT64 0xffffffffffffffffULL
#      define MAXINT64  0x7fffffffffffffffLL
#      define MININT64  0x8000000000000000LL
       /* 64-bit constants */
#      define INT64(x) x ## LL
#      define UINT64(x) x ## ULL
#    endif /* __GNUC__ */
#  endif /* !C99 */

#endif /* sparc */

/* ------------------------------------------------- */
/* LINUX */
/* ------------------------------------------------- */
#ifdef linux

#  include <stddef.h>
#  include <float.h>

#  if defined(__i386__) || defined(__LP64__)

     /* x86-linux is the most common architecture */
#    define FLOAT_TO_INT_IS_SLOW     1
#    define lowbytefirst             1
#    define bitsgoright              1

     /* 64bit Linux */
#    if defined(__LP64__)

#      define PLATFORM_IS_64BIT 1
#      undef PLATFORM_IS_32BIT
#      define MACHINE                P_INTEL64
#      define MACHINE_WITHOUT_64BIT  P_INTEL
#      define USE_INLINE_MEMCPY      1
#      define USE_INLINE_MEMMOVE     1

#      ifndef __intptr_t_defined
         typedef long int            intptr_t;
#        define __intptr_t_defined
#      endif
#      ifndef __uintptr_t_defined
         typedef unsigned long int   uintptr_t;
#        define __uintptr_t_defined
#      endif

     /* 32bit Linux */
#    else

#      define PLATFORM_IS_32BIT 1
#      undef PLATFORM_IS_64BIT
#      define MACHINE                P_INTEL
#      define MACHINE_WITHOUT_64BIT  P_INTEL

#      ifndef __intptr_t_defined
         typedef int                 intptr_t;
#        define __intptr_t_defined
#      endif
#      ifndef __uintptr_t_defined
         typedef unsigned int        uintptr_t;
#        define __uintptr_t_defined
#      endif

#    endif

#  elif defined(_PPC_)      /* PowerPC */
#    define MACHINE                P_PPC
#    define highbytefirst          1
#    define bitsgoright            1
#    define PLATFORM_IS_32BIT      1
#    undef PLATFORM_IS_64BIT

#    ifndef __intptr_t_defined
       typedef int                 intptr_t;
#      define __intptr_t_defined
#    endif
#    ifndef __uintptr_t_defined
       typedef unsigned int        uintptr_t;
#      define __uintptr_t_defined
#    endif

#  elif ARM_ARCH == 5 /* linux/arm port */
#    define MACHINE              P_ARMv5
#    if defined(ARM_LE)
#      define lowbytefirst         1
#    elif defined(ARM_BE)
#      define highbytefirst        1
#    else
#      error ARM compiler neither low nor high byte endian
#    endif
#    define bitsgoright     1
#    define PLATFORM_IS_32BIT 1
#    undef PLATFORM_IS_64BIT
#    if defined(LIBuClibc)
#      define NO_BCOPY
#      define NO_BZERO
#      define NO_BCMP
#    endif
#    ifndef __intptr_t_defined
       typedef int              intptr_t;
#      define __intptr_t_defined
#    endif
#    ifndef __uintptr_t_defined
       typedef unsigned int     uintptr_t;
#      define __uintptr_t_defined
#    endif

#  elif ARM_ARCH == 6 /* ARM version 6 architecture */
#    define MACHINE              P_ARMv6
#    if defined(ARM_LE)
#      define lowbytefirst         1
#    elif defined(ARM_BE)
#      define highbytefirst        1
#    else
#      error ARM compiler neither low nor high byte endian
#    endif
#    define bitsgoright     1
#    define PLATFORM_IS_32BIT 1
#    undef PLATFORM_IS_64BIT
#    ifndef __intptr_t_defined
       typedef int              intptr_t;
#      define __intptr_t_defined
#    endif
#    ifndef __uintptr_t_defined
       typedef unsigned int     uintptr_t;
#      define __uintptr_t_defined
#    endif

#  elif ARM_ARCH == 7 /* ARM version 6 architecture */
#    define MACHINE              P_ARMv7
#    if defined(ARM_LE)
#      define lowbytefirst         1
#    elif defined(ARM_BE)
#      define highbytefirst        1
#    else
#      error ARM compiler neither low nor high byte endian
#    endif
#    define bitsgoright     1
#    define PLATFORM_IS_32BIT 1
#    undef PLATFORM_IS_64BIT
#    ifndef __intptr_t_defined
       typedef int              intptr_t;
#      define __intptr_t_defined
#    endif
#    ifndef __uintptr_t_defined
       typedef unsigned int     uintptr_t;
#      define __uintptr_t_defined
#    endif

#  else  /* Not a recognised Linux platform type. */
#    error "Not a recognised Linux platform type"
#  endif

#  define UNIX                    1
#  define ANSI_SUPER              1
   /* bcopy(), apart from being non-standard, is identical in
    * implementation to memmove() (in glibc, at least).  Therefore, we may
    * as well use the standard function.  memcpy(), on the other hand, is
    * hand-written assembler, so I guess (!) is likely to be faster than
    * the C-coded memmove()/bcopy(). */
#  define USE_INLINE_MEMCPY       1
#  define USE_INLINE_MEMMOVE      1
#  define SIGNALTYPE              void
#  define RESETSIGNALS            1
#  define PID_TYPE                pid_t
#  define HAS_DLOPEN              1       /* Dynamic code loading */
#  define NO_UALARM               1
   /*
   #ifndef CLOCKS_PER_SEC
   #define CLOCKS_PER_SEC          1000000
   #endif
   */

#  if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
#    if defined(__GNUC__)
#      define HQN_INT64 long long int
#      define HQN_INT64_OP_ADDSUB  1 /* Addition, subtraction on 64 bits available */
#      define HQN_INT64_OP_BITWISE 1 /* Bitwise operations on 64 bits available */
#      undef  HQN_INT64_OP_MULDIV    /* Do NOT allow multiply/divide on 64 bits */

#      define MAXUINT64 0xffffffffffffffffULL
#      define MAXINT64  0x7fffffffffffffffLL
#      define MININT64  0x8000000000000000LL
       /* 64-bit constants */
#      define INT64(x) x ## LL
#      define UINT64(x) x ## ULL
#    endif /* __GNUC__ */
#  endif /* !C99 */
#endif /* linux */


#if defined( arm200 )

#  define lowbytefirst            1
#  define NOThighbytefirst        1
#  define bitsgoleft              1
#  define NOTbitsgoright          1

#  define ANSI_SUPER              1
#  define Can_Shift_32            1
#  define ZEROMEMORY              1
   typedef long                    off_t;
#  define MACHINE                 P_ARM200
#  define NORMAL_OS               P_UNIX
#  define NO_BCOPY
#  define NO_BZERO
#  define NO_BCMP

#endif /* arm200 */

#if defined( mips ) && !defined( SGI ) && !defined( _MIPS_ ) && !defined( dec )
#  ifndef UNIX
#    define UNIX                  1
#  endif

#  define highbytefirst           1
#  define bitsgoright             1
#  define SIGNALTYPE              void
#endif /* mips */

#if defined( dec ) && defined( ultrix_mips )
#  ifndef UNIX
#    define UNIX                  1
#  endif

#  define lowbytefirst            1
#  define bitsgoright             1
#  define SIGNALTYPE              void
#endif /* dec & ultrix_mips */

#ifdef rs6000
#  define UNIX                    1
#  define MACHINE                 P_RS6000
#  define ADDRESS_IS_VOID_PTR     1
#  define highbytefirst           1
#  define bitsgoright             1
#  define SIGNALTYPE              void

#  if 0
#    define off_t                long
#  endif
#endif /* rs6000 */

#ifdef SGI
#  ifndef UNIX
#    define UNIX                    1
#  endif

#  define MACHINE                 P_SGI
#  define highbytefirst           1
#  define bitsgoright             1
#  define ADDRESS_IS_VOID_PTR     1
#  define RESETSIGNALS            1
#  define CANNOT_CAST_LVALUE      1
#  define SIGNALTYPE              void
#  define NO_STAT_NAME_LEN        1
#  define STATFS_THREE_ARGS       1
#  define STATFS_NO_BAVAIL        1

#  ifndef SGI4K6
     /* IRIX 6 now does have ualarm - and posix threads */
#    define NO_UALARM               1
#    define NO_PTHREADS             1
#  endif /* !SGI4K6 */

#  if defined( SGI4K6 ) || defined( SGI4K5 ) || defined( SGI5 )
     /* IRIX 5.x has shared libraries */
#    define HAS_DLOPEN              1
#  else
     /* IRIX 3.x and 4.x does not */
#    define HAS_LDOPEN              1
#  endif /* Irix 3.x, 4.x */

#  if defined( SGI4K5 ) || defined( SGI5 )
#    define NO_UNICODE
#  endif

#  if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
#    if defined(_COMPILER_VERSION) && _COMPILER_VERSION >= 720
#      define HQN_INT64 long long int
#      define HQN_INT64_OP_ADDSUB  1 /* Addition, subtraction on 64 bits available */
#      define HQN_INT64_OP_BITWISE 1 /* Bitwise operations on 64 bits available */
#      define HQN_INT64_OP_MULDIV  1 /* Multiply/divide on 64 bits available */

#      define UINTPTR_MAX 0xffffffffffffffffLLU
#      define MAXUINT64 0xffffffffffffffffLLU
#      define MAXINT64  0x7fffffffffffffffLL
#      define MININT64  0x8000000000000000LL
       /* 64-bit constants */
#      define INT64(x) x ## LL
#      define UINT64(x) x ## ULL
#    endif /* MIPSPro compiler */
#  endif /* !C99 */

#endif /* SGI */

#ifdef hp9000
#  define UNIX                    1
#  define MACHINE                 P_HP9000
#  define highbytefirst           1
#  define bitsgoright             1
#  define SIGNALTYPE              void
#endif /* hp9000 */

#ifdef apollo
#  define UNIX                    1
#  define MACHINE                 ??? /* unknown */
#  define highbytefirst           1
#  define bitsgoright             1
#  define SIGNALTYPE              void
#endif /* apollo */

#ifdef sony
#  define UNIX                    1
#  define MACHINE                 ??? /* unknown */
#  define highbytefirst           1
#  define bitsgoright             1
#  define SIGNALTYPE              void
#endif /* sony */

#ifdef __hppa    /* Snake: HP Precision Architecture */
#  define UNIX                    1
#  define MACHINE                 P_HPSNAKE
#  define highbytefirst           1
#  define bitsgoright             1
#  define align_to_double         1
#  define SIGNALTYPE              void
#  define ADDRESS_IS_VOID_PTR     1
#  define ANSI_OSPROTOS           1
#  define LACKING_GETWD           1
#  define RESETSIGNALS            1
   /* the snake compiler has problems if NULL is a (void *) */
#  define NULL 0

#  define HAS_SHL_LOAD            1       /* Dynamic code loading */
#endif /* hppa */

#ifdef T188 /* tadpole system */
#  define UNIX                    1
#  define MACHINE                 P_T188
#  define highbytefirst           1
#  define bitsgoright             1
#  define align_to_double         1
#  define SIGNALTYPE              void
#  define NO_STAT_NAME_LEN        1
#  define BSD_READDIR             1
#else /* T188 */
#  define waitpid_defined         1
#endif /* T188 */

#ifdef mc88100
#  define UNIX                    1
#  define MACHINE                 P_MC88100
#  define highbytefirst           1
#  define bitsgoright             1
#  define align_to_double         1
#  define SIGNALTYPE              void
#  define DIRECT_READDIR          1
#endif /* mc88100 */

#ifdef _AM29K
#  define AMD29K
#endif

#ifdef AMD29K
#  define UNIX                    1
#  define MACHINE                 P_AM29K
#  define highbytefirst           1
#  define bitsgoright             1
#  define align_to_double         1
#  pragma Off (Char_default_unsigned)
#  define SIGNALTYPE              void
#  define off_t long
#endif

#ifdef __clipper__
#  define CLIPPER
#endif

#ifdef CLIPPER
#  define UNIX                    1
#  define MACHINE                 P_CLIPPER
#  define ANSI_OSPROTOS           1
#  define lowbytefirst            1
#  define bitsgoleft              1
#  define SIGNALTYPE              int
#  define align_to_double         1
#  define NO_UALARM               1
#  define RESETSIGNALS            1
#  define NO_STAT_NAME_LEN        1
#  define SELECT_IS_BROKEN        1
#endif

#ifdef Solaris
#  define UNIX          1
#  define USE_INLINE_MEMCPY
#  define USE_INLINE_MEMMOVE
#  define NO_BZERO      1
#  define NO_BCMP       1
#  define NO_STAT_NAME_LEN 1
#  define RESETSIGNALS  1
#  define LACKING_GETWD 1
#  define HAS_STATVFS   1

#  ifndef POSIX_THREADS
#    define SOLARIS_THREADS  /* use solaris threads */
#    define NO_PTHREADS      /* don't use posix threads */
#  endif
#endif

#if !defined(UINTPTR_MAX)
#  if PLATFORM_IS_64BIT
#    define UINTPTR_MAX 0xFFFFFFFFFFFFFFFFull
#  else
#    define UINTPTR_MAX 0xFFFFFFFFu
#  endif
#endif

/* ========================================================================= */
/* === PLATFORM_ID SECTION === */
/* ========================================================================= */
#ifdef UNIX
   /* All unixes have the same OS ID */
#  define NORMAL_OS               P_UNIX
#  ifdef MULTI_PROCESS
#    define PARALLEL_OS             P_UNIX_PARALLEL
#  endif
#endif


#ifdef MULTI_PROCESS

#  ifndef PARALLEL_OS
#    error "do not have a parallel Operating System ID"
     === "do not have a parallel Operating System ID"
#  endif
#  define PLATFORM_ID             (MACHINE | PARALLEL_OS)
#  define PLATFORM_ID_WITHOUT_MP  (MACHINE | NORMAL_OS)
#else /* ! MULTI_PROCESS */
#  define PLATFORM_ID             (MACHINE | NORMAL_OS)

#endif /* ! MULTI_PROCESS */

#ifdef PLATFORM_IS_64BIT

   /* Similar defines for 32-bit equivalent machine */
#  ifndef MACHINE_WITHOUT_64BIT
#    error "do not have a 32-bit equivalent machine ID"
     === "do not have a 32-bit equivalent machine ID"
#  endif
#  ifdef MULTI_PROCESS
#    define PLATFORM_ID_WITHOUT_64BIT         (MACHINE_WITHOUT_64BIT | PARALLEL_OS)
#    define PLATFORM_ID_WITHOUT_MP_AND_64BIT  (MACHINE_WITHOUT_64BIT | NORMAL_OS)
#  else /* ! MULTI_PROCESS */
#    define PLATFORM_ID_WITHOUT_64BIT         (MACHINE_WITHOUT_64BIT | NORMAL_OS)
#  endif /* ! MULTI_PROCESS */

#else /* ! PLATFORM_IS_64BIT */

   /* Similar defines for any 64-bit equivalent machine
    * Won't be defined if there is no 64-bit equivalent
    */
#  ifdef MACHINE_WITH_64BIT
#    ifdef MULTI_PROCESS
#      define PLATFORM_ID_WITH_64BIT             (MACHINE_WITH_64BIT | PARALLEL_OS)
#      define PLATFORM_ID_WITH_64BIT_WITHOUT_MP  (MACHINE_WITH_64BIT | NORMAL_OS)
#    else /* ! MULTI_PROCESS */
#      define PLATFORM_ID_WITH_64BIT             (MACHINE_WITH_64BIT | NORMAL_OS)
#    endif /* ! MULTI_PROCESS */
#  endif /* ! MACHINE_WITH_64BIT */

#endif /* ! PLATFORM_IS_64BIT */




/* ========================================================================= */
/* === GENERAL SECTION === */
/* ========================================================================= */

#ifdef USE_TRADITIONAL
#  define VARARGS                 1
#else
#  define STDARGS                 1
#endif  /* USE_TRADITIONAL */

/* default POSIX definition for process id type for kill() etc... */

#ifndef PID_TYPE
#  define PID_TYPE pid_t
#endif /* !PID_TYPE */

#if defined(__STDC__) || defined(__GNUC__)
#  ifndef  ANSI_SUPER
#    ifndef rs6000
#      define  ANSI_SUPER
#    endif
#  endif
#endif

#ifdef ANSI_SUPER
#  ifndef USE_PROTOTYPES
#    define USE_PROTOTYPES
#  endif
#  ifndef ANSI_OSPROTOS
#    define ANSI_OSPROTOS
#  endif
#endif

#if defined(ANSI_SUPER) || defined(HAS_CONST)
   /* leave type-qualifier "const" alone */
#else
#  define const
#endif

#if defined(ANSI_SUPER) || defined(HAS_SIGNED)
   /* leave signed type-specifiers alone */
#else
#  define signed
   /* //\\ if this breaks a source file, replace "signed" by "signed int" in that source file. */
#endif

/* HIGH/LOWBYTEFIRST
 *  i) ensure one of high/lowbytefirst is defined
 * ii) undef lowbytefirst - all code should only switch on high defined or not.
 */
#ifdef  highbytefirst
#  ifdef  lowbytefirst
#    error "Error in platform.h - BOTH highbytefirst and lowbytefirst defined!!!"
     === "Error in platform.h - BOTH highbytefirst and lowbytefirst defined!!!"
#  else
     /* highbytefirst - all well */
#  endif
#else
#  ifdef  lowbytefirst
     /* lowbytefirst - all well */
#    undef lowbytefirst
     /* (all code should only switch on high defined or not) */
#  else
#    error "Error in platform.h - neither highbytefirst nor lowbytefirst defined"
     === "Error in platform.h - neither highbytefirst nor lowbytefirst defined"
#  endif
#endif

/* Bitsgoright/bitsgoleft
 *  i) ensure one of bitsgoright/bitsgoleft is defined
 * ii) undef bitsgoleft - all code should only switch on right defined or not.
 */
#ifdef  bitsgoright
#  ifdef  bitsgoleft
#    error "Error in platform.h - BOTH bitsgoright and bitsgoleft defined!!!"
     === "Error in platform.h - BOTH bitsgoright and bitsgoleft defined!!!"
#  else
     /* bitsgoright - all well */
#  endif
#else
#  ifdef bitsgoleft
     /* bitsgoleft - all well */
#    undef bitsgoleft
     /* (all code should only switch on bits right defined or not) */
#  else
#    error "Error in platform.h - neither bitsgoright nor bitsgoleft defined"
     === "Error in platform.h - neither bitsgoright nor bitsgoleft defined" :-)
#  endif
#endif

/* STDARGS/VARARGS
 * ensure one of STDARGS/VARARGS is defined
 */
#ifdef  STDARGS
#  ifdef  VARARGS
#    error "Error in platform.h - BOTH STDARGS and VARARGS defined!!!"
     === "Error in platform.h - BOTH STDARGS and VARARGS defined!!!"
#  else
     /* STDARGS - all well */
#  endif
#else
#  ifdef  VARARGS
   /* VARARGS - all well */
#  else
#    error "Error in platform.h - neither STDARGS nor VARARGS defined"
     === "Error in platform.h - neither STDARGS nor VARARGS defined"
#  endif
#endif


#ifndef promoted_real
   /* Use promoted_real in all ANSI prototype declarations which refer */
   /* to a real value passed from an old-style declaration */
#  define promoted_real double
#endif /* !promoted_real_type */

/* C++ and C99 define an "inline" keyword. Many other compilers
 * support explicit inlining requests. We define the "inline" macro on
 * non C99/C++ compilation to inline if the compiler supports it, so
 * we can avoid long, complicated, and impossible to debug
 * macros. Note we don't protect this inside __STDC__ because some
 * compilers (MSVC included) only define __STDC__ when in a "strict
 * conformance" mode, which disables extensions.
 */
#if !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L)

#  if defined(MACOSX) && defined(__OSSERVICES__)
#    error Include std.h BEFORE MacOS X headers
     === "Include std.h BEFORE MacOS X headers"
#  endif

#  if defined(__GNUC__)
     /* No idea when this was introduced. It's somewhat academic, since
      * 2.9x and 3.x support explicit inlining, and those are the earliest
      * versions used now.
      */
#    define inline __inline__ /* GNU C compiler. */
#    define forceinline __attribute__((always_inline)) /* GNU C compiler. */
#  elif defined(_MSC_VER) && _MSC_VER >= 1200
     /* Do NOT use __inline; MSVC 6 does a very poor job of evaluating inlining
        candidates. */
#    define inline __forceinline /* MSVC 6 and later. */
#    define forceinline __forceinline /* MSVC 6 and later. */
#  elif defined(__sgi) && defined(_COMPILER_VERSION) && _COMPILER_VERSION >= 720
#    define inline __inline /* SGI MIPSPro Irix compiler. */
#  else
#    define inline /* inline not supported */
#  endif

#endif /* Not C++ or C99 */

#ifndef forceinline
#  define forceinline inline
#endif

#ifndef forceinline
#  define forceinline inline
#endif

/* Does right shift of signed values sign-extend? This is implementation
   defined, according to the C Standard. */
#if defined(_MSC_VER)
   /* MSVC defines right shift to be arithmetic. */
#  define RIGHT_SHIFT_IS_SIGNED 1
#elif defined(__GNUC__)
   /* GCC uses arithmetic right shift. This is not explicitly defined anywhere
    * in the documentation, the closes I've seen to a definitive statement on it
    * are these:
    *
    * http://gcc.gnu.org/ml/gcc/2000-04/msg00152.html
    * http://lists.gnu.org/archive/html/autoconf-patches/2001-08/msg00104.html
    */
#  define RIGHT_SHIFT_IS_SIGNED 1
#endif

/* Normal va_copy, for when not defined by system or above. C99 stdarg.h
   defines va_copy. MSVC12 stdarg.h defines va_copy as a macro regardless
   of standard level. */
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
#  if !defined(_MSC_VER) || _MSC_VER < 1800
#    ifndef va_copy
#      ifdef __va_copy
#        define va_copy(dest, src) __va_copy((dest), (src))
#      else
#        define va_copy(dest, src) (dest) = (src)
#      endif
#    endif
#  endif
#endif

/* If there is no defined calling convention then we don't
 * need to define one
 */
#ifndef CRT_API
#  define CRT_API
#endif

/* Define how to inform the compiler of pointer aliasing that breaks
 * the strict definition of the C89/C99 rules.
 */

#if ((__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 3))

  /* Accesses to objects with types with this attribute are not
   * subjected to type-based alias analysis, but are instead assumed to
   * be able to alias any other type of objects, just like the `char'
   * type.  See `-fstrict-aliasing' for more information on aliasing
   * issues.
   */
#  define MAY_ALIAS(type_) type_ __attribute__((__may_alias__))

#else

#  define MAY_ALIAS(type_) type_ /*may alias*/

#endif

/* ========================================================================= */
/* === END OF GENERAL SECTION === */
/* ========================================================================= */

/** \} */

#endif /* protection for multiple inclusion */


