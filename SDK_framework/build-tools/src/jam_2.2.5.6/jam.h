/*
 * Copyright 1993, 1995 Christopher Seiwald.
 *
 * This file is part of Jam - see jam.c for Copyright information.
 */

/*
 * jam.h - includes and globals for jam
 *
 * 04/08/94 (seiwald) - Coherent/386 support added.
 * 04/21/94 (seiwald) - DGUX is __DGUX__, not just __DGUX.
 * 05/04/94 (seiwald) - new globs.jobs (-j jobs)
 * 11/01/94 (wingerd) - let us define path of Jambase at compile time.
 * 12/30/94 (wingerd) - changed command buffer size for NT (MS-DOS shell).
 * 02/22/95 (seiwald) - Jambase now in /usr/local/lib.
 * 04/30/95 (seiwald) - FreeBSD added.  Live Free or Die.
 * 05/10/95 (seiwald) - SPLITPATH character set up here.
 * 08/20/95 (seiwald) - added LINUX.
 * 08/21/95 (seiwald) - added NCR.
 * 10/23/95 (seiwald) - added SCO.
 * 01/03/96 (seiwald) - SINIX (nixdorf) added.
 * 03/13/96 (seiwald) - Jambase now compiled in; remove JAMBASE variable.
 * 04/29/96 (seiwald) - AIX now has 31 and 42 OSVERs.
 * 11/21/96 (peterk)  - added BeOS with MW CW mwcc
 * 12/21/96 (seiwald) - OSPLAT now defined for NT.
 * 07/19/99 (sickel)  - Mac OS X Server and Client support added
 */

# ifdef VMS

int unlink( char *f ); 	/* In filevms.c */

# include <types.h>
# include <file.h>
# include <stat.h>
# include <stdio.h>
# include <ctype.h>
# include <stdlib.h>
# include <signal.h>
# include <string.h>
# include <time.h>
# include <unixlib.h>

# ifdef __DECC
# define OSSYMS "VMS=true","OS=OPENVMS"
# else
# define OSSYMS "VMS=true","OS=VMS"
# endif

# define MAXLINE 1024 /* longest 'together' actions */
# define SPLITPATH ','
# define EXITOK 1
# define EXITBAD 0

# else

# ifdef NT

# include <fcntl.h>
# include <stdlib.h>
# include <stdio.h>
# include <ctype.h>
# include <malloc.h>
# include <memory.h>
# include <signal.h>
# include <string.h>
# include <time.h>

# define OSSYMS "NT=true","OS=NT"
# define SPLITPATH ';'
/* MAXLINE is longest 'together' actions   NB: must be less than maximum command line length */
# define MAXLINE 4000
# define EXITOK 0
# define EXITBAD 1

/* Switch this off to get the old behaviour */
# define USE_WIN32_API 1

# ifdef USE_WIN32_API
# include <windows.h>
# endif

# else

# ifdef __OS2__

# include <fcntl.h>
# include <stdlib.h>
# include <stdio.h>
# include <ctype.h>
# include <malloc.h>
# include <signal.h>
# include <string.h>
# include <time.h>

# define OSSYMS "OS2=true","OS=OS2"
# define SPLITPATH ';'
# define MAXLINE 996	/* longest 'together' actions */
# define EXITOK 0
# define EXITBAD 1

# else

# ifdef __QNX__

# define unix

# include <fcntl.h>
# include <stdlib.h>
# include <stdio.h>
# include <ctype.h>
# include <malloc.h>
# include <signal.h>
# include <string.h>
# include <time.h>

# define OSSYMS "UNIX=true","OS=QNX"
# define SPLITPATH ':'
# define MAXLINE 9000	/* longest 'together' actions */
# define EXITOK 0
# define EXITBAD 1

# else /* QNX */

# ifdef macintosh
# include <time.h>
# include <stdlib.h>
# include <string.h>
# include <stdio.h>

# define OSSYMS "MAC=true","OS=MAC"
# define SPLITPATH ','
# define MAXLINE 10240	/* longest 'together' actions */
# define EXITOK 0
# define EXITBAD 1

# else /* not MAC */

# include <sys/types.h>
# include <sys/file.h>
# include <sys/stat.h>
# include <fcntl.h>
# ifndef ultrix
# include <stdlib.h>
# endif
# include <stdio.h>
# include <ctype.h>
# if !defined(__bsdi__)&&!defined(__FreeBSD__)
# if !defined(NeXT)&&!defined(__MACHTEN__)&&!defined(__APPLE__)
# if !defined(MVS)
# include <malloc.h>
# endif
# endif
# endif
# include <memory.h>
# include <signal.h>
# include <string.h>
# include <time.h>

# ifdef _AIX
# define unix
# ifdef _AIX41
# define OSSYMS "UNIX=true","OS=AIX","OSVER=41"
# else
# define OSSYMS "UNIX=true","OS=AIX","OSVER=32"
# endif
# endif

# ifdef __BEOS__
# define OSSYMS "UNIX=true","OS=BEOS"
# define unix
# endif

# ifdef __bsdi__
# define OSSYMS "UNIX=true","OS=BSDI"
# endif
# if defined (COHERENT) && defined (_I386)
# define OSSYMS "UNIX=true","OS=COHERENT"
# endif
# ifdef __FreeBSD__
# define OSSYMS "UNIX=true","OS=FREEBSD"
# endif
# ifdef __NetBSD__
# define unix
# define OSSYMS "UNIX=true","OS=NETBSD"
# endif
# ifdef __DGUX__
# define OSSYMS "UNIX=true","OS=DGUX"
# endif
# ifdef __hpux
# define OSSYMS "UNIX=true","OS=HPUX"
# endif
# ifdef __OPENNT
# define unix
# define OSSYMS "UNIX=true","OS=INTERIX"
# endif
# ifdef __sgi
# define OSSYMS "UNIX=true","OS=IRIX"
# endif
# ifdef __ISC
# define OSSYMS "UNIX=true","OS=ISC"
# endif
# ifdef linux
# define OSSYMS "UNIX=true","OS=LINUX"
# endif
# ifdef __Lynx__
# define OSSYMS "UNIX=true","OS=LYNX"
# define unix
# endif
# ifdef __MACHTEN__
# define OSSYMS "UNIX=true","OS=MACHTEN"
# endif
# ifdef __MVS__
# define unix
# define OSSYMS "UNIX=true","OS=MVS"
# endif
# ifdef _ATT4
# define OSSYMS "UNIX=true","OS=NCR"
# endif
# ifdef NeXT
# ifdef __APPLE__
# define OSSYMS "UNIX=true","OS=RHAPSODY"
# else
# define OSSYMS "UNIX=true","OS=NEXT"
# endif
# endif
# ifdef __APPLE__
# define unix
# define OSSYMS "MACOSX=true","OS=MACOSX"
# endif
# ifdef __osf__
# define OSSYMS "UNIX=true","OS=OSF"
# endif
# ifdef _SEQUENT_
# define OSSYMS "UNIX=true","OS=PTX"
# endif
# ifdef M_XENIX
# define OSSYMS "UNIX=true","OS=SCO"
# endif
# ifdef sinix
# define unix
# define OSSYMS "UNIX=true","OS=SINIX"
# endif
# ifdef sun
# if defined(__svr4__) || defined(__SVR4)
# define OSSYMS "UNIX=true","OS=SOLARIS"
# else
# define OSSYMS "UNIX=true","OS=SUNOS"
# endif
# endif
# ifdef ultrix
# define OSSYMS "UNIX=true","OS=ULTRIX"
# endif
# if defined(__USLC__) && !defined(M_XENIX)
# define OSSYMS "UNIX=true","OS=UNIXWARE"
# endif
# ifdef AMIGA
# define OSSYMS "UNIX=true","OS=AMIGA"
# endif
# ifndef OSSYMS
# define OSSYMS "UNIX=true","OS=UNKNOWN"
# endif

# define MAXLINE 20480	/* longest 'together' actions' */
# define SPLITPATH ':'
# define EXITOK 0
# define EXITBAD 1

# endif /* mac */

# endif /* QNX */

# endif /* OS/2 */

# endif /* NT */

# endif /* UNIX */


# ifdef macintosh
# define yield() (SpinCursor(1))
# else
# define yield() ((void)0)
# endif

/* OSPLAT definitions - note the leading , */

# define OSPLATSYM /**/

# if defined( _M_PPC ) || defined( PPC ) || defined( ppc )
# undef OSPLATSYM
# define OSPLATSYM ,"OSPLAT=PPC"
# endif

# if defined( _ALPHA_ ) || defined( __alpha__ )
# undef OSPLATSYM
# define OSPLATSYM ,"OSPLAT=AXP"
# endif

# if defined( _i386_ ) || defined( __i386__ ) || defined( _M_IX86 )
# if !defined( __FreeBSD__ ) && !defined( __OS2__)
# undef OSPLATSYM
# define OSPLATSYM ,"OSPLAT=X86"
# endif
# endif

# ifdef __sparc__
# undef OSPLATSYM
# define OSPLATSYM ,"OSPLAT=SPARC"
# endif

/* You probably don't need to muck with these. */

# if defined(macintosh)
# define MAXSYM	1024	/* longest symbol in the environment */
# elif defined(linux)
# define MAXSYM	65535	/* longest symbol in the environment */
# else
# define MAXSYM	20000	/* longest symbol in the environment */
# endif
# define MAXJPATH 1024	/* longest filename */

# define MAXJOBS 64	/* silently enforce -j limit */
# define MAXARGC 32	/* words in $(JAMSHELL) */

# define CMDBUF 40960	/* size of command blocks */

/* Jam private definitions below. */

# define DEBUG_MAX	10

struct globs {
	int	noexec;
	int	jobs;
	int	quitquick;
	char	debug[DEBUG_MAX];
	FILE	*cmdout;		/* print cmds, not run them */
} ;

extern struct globs globs;

# define DEBUG_MAKE	( globs.debug[ 1 ] )	/* show actions when executed */
# define DEBUG_MAKEQ	( globs.debug[ 2 ] )	/* show even quiet actions */
# define DEBUG_EXEC	( globs.debug[ 2 ] )	/* show text of actons */
# define DEBUG_MAKEPROG	( globs.debug[ 3 ] )	/* show progress of make0 */
# define DEBUG_BIND	( globs.debug[ 3 ] )	/* show when files bound */

# define DEBUG_EXECCMD	( globs.debug[ 4 ] )	/* show execcmds()'s work */

# define DEBUG_COMPILE	( globs.debug[ 5 ] )	/* show rule invocations */

# define DEBUG_HEADER	( globs.debug[ 6 ] )	/* show result of header scan */
# define DEBUG_BINDSCAN	( globs.debug[ 6 ] )	/* show result of dir scan */
# define DEBUG_SEARCH	( globs.debug[ 6 ] )	/* show attempts at binding */

# define DEBUG_VARSET	( globs.debug[ 7 ] )	/* show variable settings */
# define DEBUG_VARGET	( globs.debug[ 8 ] )	/* show variable fetches */
# define DEBUG_VAREXP	( globs.debug[ 8 ] )	/* show variable expansions */
# define DEBUG_IF	( globs.debug[ 8 ] )	/* show 'if' calculations */
# define DEBUG_LISTS	( globs.debug[ 9 ] )	/* show list manipulation */
# define DEBUG_SCAN	( globs.debug[ 9 ] )	/* show scanner tokens */
# define DEBUG_MEM	( globs.debug[ 9 ] )	/* show memory use */

