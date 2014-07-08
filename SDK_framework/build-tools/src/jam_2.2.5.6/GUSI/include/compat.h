/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)compat.h	8.1 (Berkeley) 6/2/93
 */

#ifndef	_COMPAT_H_
#define	_COMPAT_H_

/*
 * If your system doesn't specify a max size for a SIZE_T, check
 * to make sure this is the right one.
 */
#ifndef SIZE_T_MAX
#define	SIZE_T_MAX	UINT_MAX
#endif

/*
 * If you don't have POSIX 1003.1 signals, the signal code surrounding the 
 * temporary file creation is intended to block all of the possible signals
 * long enough to create the file and unlink it.  All of this stuff is
 * intended to use old-style BSD calls to fake POSIX 1003.1 calls.
 */
#ifdef NO_POSIX_SIGNALS
#define sigemptyset(set)	(*(set) = 0)
#define sigfillset(set)		(*(set) = ~(sigset_t)0, 0)
#define sigaddset(set,signo)	(*(set) |= sigmask(signo), 0)
#define sigdelset(set,signo)	(*(set) &= ~sigmask(signo), 0)
#define sigismember(set,signo)	((*(set) & sigmask(signo)) != 0)

#define SIG_BLOCK	1
#define SIG_UNBLOCK	2
#define SIG_SETMASK	3

static int __sigtemp;		/* For the use of sigprocmask */

#define sigprocmask(how,set,oset) \
	((__sigtemp = (((how) == SIG_BLOCK) ? \
	sigblock(0) | *(set) : (((how) == SIG_UNBLOCK) ? \
	sigblock(0) & ~(*(set)) : ((how) == SIG_SETMASK ? \
	*(set) : sigblock(0))))), ((oset) ? \
	(*(oset) = sigsetmask(__sigtemp)) : sigsetmask(__sigtemp)), 0)
#endif

#if defined(SYSV) || defined(SYSTEM5) || defined(macintosh)
#define	index(a, b)			strchr(a, b)
#define	rindex(a, b)		strrchr(a, b)
#define	bzero(a, b)			memset(a, 0, b)
#define	bcmp(a, b, n)		memcmp(a, b, n)
#define	bcopy(a, b, n)		memmove(b, a, n)
#endif

/* POSIX 1003.2 RE limit. */
#ifndef	_POSIX2_RE_DUP_MAX
#define	_POSIX2_RE_DUP_MAX	255
#endif

#ifndef	WCOREDUMP			/* 4.4BSD extension */
#define	WCOREDUMP(a)	0
#endif

#ifndef _POSIX2_RE_DUP_MAX
#define	_POSIX2_RE_DUP_MAX	255
#endif

#ifndef NULL				/* ANSI C #defines NULL everywhere. */
#define	NULL		0
#endif

#ifndef	MAX
#define	MAX(_a,_b)	((_a)<(_b)?(_b):(_a))
#endif
#ifndef	MIN
#define	MIN(_a,_b)	((_a)<(_b)?(_a):(_b))
#endif

#ifndef _BSD_VA_LIST_
#define	_BSD_VA_LIST_	char *
#endif

#endif /* !_COMPAT_H_ */
