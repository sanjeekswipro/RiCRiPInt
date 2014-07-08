/** \file
 * \ingroup cstandard
 *
 * $HopeName: HQNc-standard!export:osprotos.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * Define prototypes for "standard" functions
 */

#ifndef STD_H
"osprotos.h is internal to the standard component and should only be included by std.h" :-)
#else  /* STD_H - ie. std.h has (correctly) already been included */

#ifdef ANSI_OSPROTOS

#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(WIN32) || defined(_WIN64)
#include <io.h>
#include <direct.h>
#include <process.h>
#endif

#if defined(VXWORKS) || defined(UNIX) || defined(IBMPC) || defined(WIN32) || defined(_WIN64) || defined(idtr30xx) || defined(MACOSX)
#include <sys/types.h>
#else
/* for lseek you will need to define off_t in platform.h */
#endif


#else  /* ! ANSI_OSPROTOS */


#include <strings.h>

#if defined(VXWORKS) || defined(UNIX) || defined(IBMPC) || defined(WIN32) || defined(_WIN64) || defined(idtr30xx)
#include <sys/types.h>
#else
/* you will need to define off_t and size_t (in platform.h) */
#endif

#ifdef ADDRESS_IS_VOID_PTR
extern void *	malloc PROTO((size_t));
extern void *	realloc PROTO((void *, size_t));
#else
extern char *	malloc PROTO((size_t));
extern char *	realloc PROTO((char *, size_t));
#endif

extern void	exit PROTO((int));

extern char *	strcat PROTO((char *s1, const char *s2));
extern int	strcmp PROTO(( char *s1, const char *s2));
extern char *	strcpy PROTO((char *s1, const char *s2));
extern size_t	strlen PROTO((const char *s));
extern char *	strncat PROTO((char *s1, const char *s2, size_t n));
extern int	strncmp PROTO(( char *s1, const char *s2, size_t n));
extern char *	strncpy PROTO((char *s1, const char *s2, size_t n));
extern void *   memchr PROTO((const void *ptr, int val, size_t len));
extern int      memcmp PROTO((const void *ptr1, const void *ptr2, size_t len));
extern void *   memcpy PROTO((void *dest, const void *src, size_t len));
extern void *   memmove PROTO((void *dest, const void *src, size_t len));
extern void *   memset PROTO((void *ptr, int val, size_t len));
extern int	abs PROTO((int));
extern double	fabs PROTO((double));
extern double	sqrt PROTO((double));
extern double	sin PROTO((double));
extern double	cos PROTO((double));
extern double	tan PROTO((double));
extern double	atan PROTO((double));
extern double	atan2 PROTO((double, double));
extern double	pow PROTO((double, double));
extern double	log PROTO((double));
extern double	log10 PROTO((double));

extern double	floor PROTO((double _x));
extern double	ceil PROTO((double _x));
extern double	fmod PROTO((double _x,double _y));
extern double	ldexp PROTO((double _x,int _n));
extern double	modf PROTO((double _x, double *_ip));


#endif  /* ! ANSI_OSPROTOS */


/* Many, some or fewer platforms will need the following, unless they're */
/* RS6000s: */
/* These may break duplicate standard declarations on some platforms,
 * including Win32, Linux, Solaris... */
#if ! ( defined(rs6000) || defined(SGI4K6) || defined(WIN32) || defined(_WIN64) || defined(linux) || defined(Solaris) || defined(MACOSX) || defined(VXWORKS) )
extern off_t lseek PROTO((int filedes, off_t offset, int whence));
#endif

#if defined(SGI4K6)
/* Irix 6.3 has these prototypes in bstring.h
 * From Irix 6.4 onwards they are included in strings.h
 * Include them both here, because strings.h is included by
 * the X header files, and this causes a clash for Irix 6.4 onwards
 */
#include <strings.h>
#include <bstring.h>
#else /* ! SGI4K6*/

#if defined(linux)
#if  !defined(LIBuClibc)
/* Need to make sure we get these prototypes correct, since gcc is very
 * picky, and we don't get them by just including string.h, since they
 * are not ANSI/POSIX. */
#ifdef __cplusplus
extern "C" {
#endif
extern void	bcopy __P ((__const __ptr_t __src, __ptr_t __dest, size_t __n));
extern int	bcmp __P ((__const __ptr_t __s1, __const __ptr_t __s2, size_t __n));
extern void	bzero __P ((__ptr_t __s, size_t __n));
#ifdef __cplusplus
}
#endif
#else /* LIBuClibc */
#include <string.h>
#define bzero(b, len)   (void)memset(b, 0, (size_t)(len))
#endif /* LIBuClibc */
#else
#if defined (MACOSX)

/* Defined in string.h, inside #ifndef _ANSI_SOURCE.
 * Duplicated here
 */
#ifdef __cplusplus
extern "C" {
#endif
int	 bcmp(const void *, const void *, size_t);
void	 bcopy(const void *, void *, size_t);
void	 bzero(void *, size_t);
#ifdef __cplusplus
}
#endif
#else
#if defined (Solaris)
#ifdef __cplusplus
extern "C" {
#endif
extern int bcmp(const void *, const void *, size_t);
extern void bcopy(const void *, void *, size_t);
extern void bzero(void *, size_t);
#ifdef __cplusplus
}
#endif
#else
#if defined (VXWORKS)
extern int  bcmp PROTO((char *b1, char *b2, int length));
extern void bcopy PROTO((const char *b1, char *b2, int length));
extern void bzero PROTO((char *b, int length));
#else
#if defined (THREADX)
   /* bcmp, bcopy, bzero are in the GHS ANSI library */
#else
#if defined (__NetBSD__)
   /* bcmp et al are in string.h as expected */
#else
#ifdef __cplusplus
extern "C" {
#endif
extern int	bcmp PROTO((char *b1, char *b2, int length));
extern void	bcopy PROTO((char *b1, char *b2, int length));
extern void	bzero PROTO((char *b, int length));
#ifdef __cplusplus
}
#endif
#endif /* ! __NetBSD__ */
#endif /* ! THREADX */
#endif /* ! VXWORKS */
#endif /* ! Solaris */
#endif /* ! MACOSX */
#endif /* ! linux */
#endif /* ! SGI4K6*/

#ifdef idtr30xx
#include <string.h>
#define bcopy(b1, b2, len) (void)memmove(b2, b1, (size_t)(len))
#define bzero(b, len)      (void)memset(b, 0, (size_t)(len))
#define bcmp(b1, b2, len)  memcmp(b1, b2, (size_t)(len))
#endif /* idtr30xx */

#if ! (defined(WIN32) || defined(_WIN64) || defined(linux) || defined(__CC_ARM))
/* Windows needs a __declspec(dllimport) in this declaration */
extern long	strtol PROTO((const char *str, char **ptr, int base));
#endif


#ifdef T188
#ifndef mc88100
extern struct direct *readdir();
#endif
#endif

#ifndef CLOCKS_PER_SEC
#include <time.h>

#ifdef T188
#ifndef mc88100
#define CLOCKS_PER_SEC CLK_TCK
#endif
#endif

#endif /* ndef CLOCKS_PER_SEC */


/*
 * Linux has some definitions protected by BSD_SOURCE and SVID_SOURCE.
 * To avoid having to define one of these in the source files concerned,
 * just repeat the troublesome definitions here:
 */
#ifdef linux

#ifndef __USE_BSD
typedef __caddr_t caddr_t;
#define S_ISUID  __S_ISUID
#define S_ISGID  __S_ISGID
#define S_ISVTX  __S_ISVTX
#define S_IREAD  __S_IREAD
#define S_IWRITE __S_IWRITE
#define S_IEXEC  __S_IEXEC
#endif

#endif /* linux */

#endif  /* OSPROTOS_H */

