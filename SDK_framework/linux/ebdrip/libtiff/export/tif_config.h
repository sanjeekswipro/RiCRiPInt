/* $HopeName: HQNlibtiff!export:tif_config.h(EBDSDK_P.1) $ */

/* Define to 1 if you have the <assert.h> header file. */
#define HAVE_ASSERT_H 1

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* Define as 0 or 1 according to the floating point format suported by the
   machine */
#define HAVE_IEEEFP 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <io.h> header file. */
#ifdef WIN32
#define HAVE_IO_H 1
#endif

/* Define to 1 if you have the <unistd.h> header file. */
#if defined( linux ) || defined( MACOSX ) || defined( Solaris )
#define HAVE_UNISTD_H 1
#endif

/* Define to 1 if you have the <search.h> header file. */
#define HAVE_SEARCH_H 1

/* The size of a `int', as computed by sizeof. */
#define SIZEOF_INT 4

/* The size of a `long', as computed by sizeof. */
#define SIZEOF_LONG 4

/* Set the native cpu bit order */
#define HOST_FILLORDER FILLORDER_LSB2MSB

/* Define to 1 if your processor stores words with the most significant byte
   first (like Motorola and SPARC, unlike Intel and VAX). */
#if defined (MACINTOSH) && defined (__BIG_ENDIAN__)
#define WORDS_BIGENDIAN 1
#elif defined (_PPC_) || defined (__sparc__)
#define WORDS_BIGENDIAN 1
#endif

/* Define to `__inline__' or `__inline' if that's what the C compiler
   calls it, or to nothing if 'inline' is not supported under any name.  */
#ifndef __cplusplus
# ifndef inline
#  define inline __inline
# endif
#endif

#ifdef WIN32
/* Substitute for deprecated method. */
#define lfind _lfind
#endif

