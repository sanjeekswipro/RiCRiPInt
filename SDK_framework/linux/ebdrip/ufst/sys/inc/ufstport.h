/* $HopeName: GGEufst5!sys:inc:ufstport.h(EBDSDK_P.1) $ */

/*
 * Copyright (C) 2004 Agfa Monotype Corporation. All rights reserved.
 */
/* $Header: /hope/man5/hope.0/compound/10/GGEufst5/RCS/sys:inc:ufstport.h,v 1.11.4.1.1.1 2013/12/19 11:24:03 rogerb Exp $ */
/* $Date: 2013/12/19 11:24:03 $ */
/*
*  port.h
*
*
*
*  History:
*  ---------
*   19-Jul-91 jfd Changed "MSWINDOWS" to "_WINDOWS".
*   10-Jun-91 jfd Made declaration of FPNUM conditional absed on INT_FP.
*   06-Apr-91 awr Added floating point definition.
*   30-Jan-91 dET Added definitions for pointer types.
*   27-Nov-90 cg  Changed typedef statement for BOOLEAN from int to WORD
*   07-Aug-88 mac Changed VOIDTYPE definition to YES for MSC
*   07-Jul-88 mac Changed VOID definition from typedef to #define because
*                 VOID does not apply to variables, only to functions.
*                 Defined MAX_LONG, MIN_LONG, and MAX_ULONG.
*   27-Jun-88 mac Fixed BOOLEAN typedef comment ( TRUE=1, FALSE=0 ).
*   07-Jun-88 mac Added configuration definitions and conditionals.
*             mac Added ABS and LABS macro definitions.
*   11-May-88 mac Change PATHNAMELEN to 63 for MS-DOS
*   16-Mar-88 mac Added typedef FIXED : 32 bit fixed point number
*   23-Feb-88 mac Added MIN_WORD
*   15-Feb-88 mac Clean up and reorganization of typedefs
*    1-Nov-87     Initial Release
*   15-Dec-91 awr Added typedefs LONGVECTOR, PLONGVECTOR.
*   07-Mar-92 rs  Add support for Borland C++ compiler.
*    8-Mar-92 awr Changed exponent of FPNUM structure to int from WORD
*   13-Mar-92 rs  Cleanup of constants '(LONG)0x...' to '0x...L'.
*   19-Mar-92 awr Moved BOX from if_type.h and added LONGBOX
*   24 Mar 92 ss  Conditional compile of MAX_UWORD & MAX_ULONG with 'U'
*                 modifier for MSDOS only. (not available on Sun)
*   02-Apr-92 rs  Software portability changes:
*                 BYTE  -> SB8,  PBYTE  -> LPSB8
*                 UBYTE -> UB8,  PUBYTE -> LPUB8
*                 WORD  -> SW16, PWORD  -> LPSW16
*                 UWORD -> UB16, PUWORD -> LPUW16
*                 LONG  -> SL32, PLONG  -> LPSL32
*                 ULONG -> UL32, PULONG -> LPUL32
*   21-Apr-92 rs  Conditionalize 'TRUE' & 'FALSE' to work w/Windows.
*   14-Jun-92 awr Added typedefs INTR, INTRVECTOR, INTRBOX
*   25-Jun-92 rs  Add 'PINTR'.
*   28 Jun 92 ss  Added entry for MIPS machine.
*   07-Aug-92 rs  Compatibility w/Windows 3.1 (FIXED, POINTFX).
*   08-Aug-92 rs    Changes for Watcom C386 compiler.
*   23-Sep-92 jfd Added entry for INTEL960 machine (__i960) .
*   30-Sep-92 jfd Added NAT_ALIGN values (natural alignment of a structure)
*                 for each entry.
*   05-Oct-92 jfd Added define for PASCAL based on ANSI C.
*                 Changed define for ENTRY for windows callback entry
*                 function from "pascal" to "PASCAL".
*   14-Nov-92 rs  Port to Macintosh.
*   05-Dec-92 rs  Temporarily change NAT_ALIGN to 4 for Mac because fm.c
*                 expects 4 or "nothing" for expand_buf, etc.
*   15-Dec-92 jfd Moved data typedefs for short, int and long from FSCDEFS.H.
*                 Moved data typedefs for uint8, ufix8, fix15, ufix16,
*                 fix31, and ufix32 from STDEF.H.
*   08-Feb-93 jfd VXWorks support.
*                 Deleted #define LOCAL from storage class definitions
*                 because it is never referenced.
*  30-Mar-93  rs  Add '#define ANSI_DEFS' for Borland & Watcom compilers.
*  10-Jun-93  jfd Set SIGNEDCHAR to NO for VXWORKS, eliminating the need to
*                 use the "-f" switch when compiling.
*                 Changed the typedef for "int8" from "char" to "SB8".
*  22-Jul-93  awr Added typedef PCOUNTER
*  08-Dec-93  rs  General cleanup - break Watcom into DOS vs OS2.
*                 Add #define of "CGFLAT32" to indicate 32 bit addresss
*                 space (only checked on DOS & OS2 platforms for now).
*  13-Dec-93  jfd Fixed typo within block "#if (defined(_WINDOWS) &&
*                 !defined(__BORLANDC__) && !defined(__WATCOMC__))" -
*                 Comment had been added in front of "#ifndef MSDOS"
*                 statement making it invalid.
*  28-Jan-94  jfd For SUN compiles using metaware compiler (for 29k),
*                 #define LINTARGS and ANSI_DEFS.
*  04-Feb-94  jfd/dbk Added support for _AM29K.
*  03-Jun-94  jfd Changed all occurrences of ENTRY to CGENTRY to resolve
*                 conflict on some compilers.
*  05-Jul-94 mby  FCO change from 1.12.1.1: Made changes for Borland 3.1 Windows compilation.
*  10-Aug-94 jfd  Removed MBY's Borland 3.1 Windows change due to problem
*                 with Borland 4.0 .
*  18-Aug-94 jfd  Added typedef for LPLPUB8.
*  17-Sep-94 rs   If _WINDOWS, define ANSI_DEFS and LINT_ARGS.
*  14-Dec-94 rs   Added typedef for LPHPUB8.
*                 Added define for HUGEPTR.
*                 General cleanup.
*  09-Jan-95 rob  Add 'HPSB8' for > 64KB support in DOS/Windows.
*  14-Feb-95 mby  Defined 'NZCOUNTER' type for NON_Z_WIND support.
*  23-Feb-95 jfd  For WATCOM, changed NAT_ALIGN from 2 to 4.
*  27-Nov-95 mby  Added OS configurations for _OSK, _OS9000.
*  30-Jan-96 mby  Added typedefs for INTG, UINTG.
*  15-Apr-96 mby  Take out OS9 conditionals around BOOLEAN, SUCCESS, TRUE, FALSE
*  12-Sep-96 dbk  Provide support for MicroSoft Visual C 4.x
*  23-Oct-96      Added emboldening for PCL 6 emulation.
*  29-Oct-96 mby  Changed BYTEORDER in the OS9000 section from LOHI to HILO.
*  14-Apr-97 mby  Added "mips" defininition.
*                 Eliminated "LINTARGS" - "LINT_ARGS" does the same thing.
*                 PASCAL is always defined to "", except for Macintosh.
*  16-Jun-97 slg  Added "psos" definition
*  18-Jun-97 slg  Added "ptv_os" definition
*  11-Jul-97 slg  Added Microware-compatibility section for PTV_OS; conditional
*				  compiles for BOOLEAN and SUCCESS (for Microware/MAUI compile)
*  11-Jul-97 slg  "XLfont" commented out (nothing links) - see comment below
*  21-Aug-97 keb  Undefined _WINDOWS if MSVC is defined to resolve WIN96 APP conflict
*  03-Sep-97 slg  Change FPNUM type to "float" rather than "double" for PTV_OS
*					(workaround for compiler bug)
*  16-Sep-97 slg  Add support for ARM chip running Helios.
*  10-Feb-98 slg  Shouldn't use filenames/pathnames if ROM modes
*  20-Feb-98 jwd  Added support for 64-bit Alpha and SGI.
*  02-Mar-98 dah  Added AGFATOOLS to allow ROM simulation
*  09-Mar-98 slg  Clean up "long" usage, get rid of unused typedefs
*  08-Jul-98 slg  Get rid of "u_int*" defines (no longer used in Surfer
*				  code); change Microware-conditional definition of BOOLEAN
*				  so that it works for reentrant-enabled Surfer API code.
*  31-Aug-98 slg  BOOLEAN needs to be UB8 if building MSVC (so that type is
*				  compatible with VisualC++ type, for Windows applications);
*				  also, #define PASCAL only if not yet defined
*  16-Oct-98 slg  Microware BOOLEAN special case is necessary even if not
*					building Surfer
*  11-Jan-99 slg  Add CONST definition (to mark read-only global data);
*				  remove unused datatypes ufix8, ufix16, ufix32; rearrange
*				  typedefs slightly (put redundant defs together)
*  19-Jan-99 aof  Removed SGI and added UFST_MIPS64. Added UL64, SL64, LPUL64
*			 slg  and LPSL64. Also (Sandra) at customer request, changed ALPHA
*				  configuration define to UFST_ALPHA_UNIX (because Alpha runs
*				  WinNT as well as UNIX), and to avoid conflict with existing
*				  ALPHA define.	Set/use UFST_UNIX rather than UNIX as UFST's
*				  internal flag (again, to avoid conflict). Make CONST an
*				  empty declaration for UFST_UNIX cases. Define ABS(), LABS(),
*				  MAX(), MIN() macros only if not previously defined.
*				  TEMPORARY: define UNIX as well as UFST_UNIX, till all files
*				  with UFST_UNIX changes can be checked in.
*  22-Jan-99 slg  Get rid of UNIX setting completely - all is UFST_UNIX now.
*  01-Jun-99 dlk  Added defines for GCCx86 (for GNU compiler).
*  09-Aug-99 slg  Added define block for ST20TP3; also add several #defines
*					of the form MAYBE_FOO_H, to be used when deciding
*					whether to include target-specific header files
*					(so that we only need to modify the test in the single
*					file port.h, rather than throughout the source code).
*  11-Aug-99 slg  Now set/use UFST_MSDOS rather than MSDOS as UFST's
*					internal flag (to avoid conflict with customer define).
*					Create temporary define KEEP_OLD_MSDOS_DEFINE, to use
*					until all UFST source code is converted to use UFST_MSDOS.
*					Set FILENAMELEN and PATHNAMELEN within each individual
*					define block, to solve a conflict (and so that we will
*					remember to set these values when creating a new define
*					block). Configuration-choices comment now matches code.
*  23-Aug-99 slg  Add CHARG type = native "char" type (which might be UB8 or
*					SB8, depending on compiler). This type should be used
*					where we need to match native "char" type (i.e. paths
*					or string operations).  Add SYS_PATH_USES_BACKSLASH	and
*					USE_BINARY_READMODE centralized #defines.
*  08-Dec-00 slg  Add new synonyms for UFST standard types (used by SWP code
*					in multiple locations): CHAR, BYTE, SHORT, USHORT, LONG,
*					ULONG, FIXED, FRACT, FS_FIXED, F26DOT26.
*  11-Dec-00 slg  Add new defines in configuration blocks which have 64-bit
*					integers (either native or software-implemented). These
*					defines are used to access higher-performance fixed-point
*					arithmetic routines. New defines added in:	MSVC, GCCx86,
*					MIPS/mips, and NAT_ALIGN==8 (which currently means one of
*					UFST_MIPS64 or UFST_ALPHA_UNIX).
*  03-May-01 slg  Get rid of AGFATOOLS define; CHARG typedef renamed to FILECHAR;
*					use SB8 rather than old FILECHAR type;
*					obsolete MAYBE_UNIX_H test removed.
*  11-Jun-01 slg  Ah well - can't use new synonyms for UFST types after all
*					(because of conflict with Windows types) - get rid of
*					CHAR, BYTE, SHORT, USHORT, LONG, ULONG, FIXED.
*				  Also add SIZEOF_LONG define to all definition blocks (usually
*					but NOT ALWAYS equal to NAT_ALIGN) - used for certain
*					platform-specific performance improvements.
* 14-Jun-01 swp  changed MSVC "PATHNAMELEN" to 255
* 18-Jul-01 aof  changed mips and sun "PATHNAMELEN" to 255
* 28-Jan-02 jfd  Added typedef for FS_FIXED_POINT.
*
*/

#ifndef __PORT__
#define __PORT__


/* If this is defined, both "MSDOS" and "UFST_MSDOS" will be defined in port.h */
/* If this is not defined, only "UFST_MSDOS" will be defined in port.h */
/*** #define KEEP_OLD_MSDOS_DEFINE ***/	/*** REMOVED (sandra, 7 Feb 2000) ***/

/************************************************************************/
/*                      Configuration Conditionals                      */
/************************************************************************/

#define  YES            1           /* Configuration feature enable     */
#define  NO             0           /* Configuration feature disable    */
#define  LOHI           1           /* Addr:LoByte, Addr+1:HiByte       */
#define  HILO           0           /* Addr:HiByte, Addr+1:LoByte       */

#if !defined(PASCAL)
#define PASCAL                      /* encountered in RTS/TT            */
#endif

/************************************************************************/
/*                    Compiler - OS Configurations			            */
/************************************************************************/
/*																		*/
/* Use make file to Define only one of the following configurations:	*/
/*																		*/
/* #if defined (GCCx86)				GNU C Compiler on x86 				*/
/* #if defined (_AM29K)             AMD 29K HighC Compiler           	*/
/* #if defined (MSVC)				Microsoft Visual C - 4.x+         	*/
/* #if defined (__WATCOMC__)      	Watcom 32 bit DOS or OS/2       	*/
/* #if defined (MSC)				Microsoft C - Windows 3.0			*/
/* #if defined (__BORLANDC__)		Borland C++ (DOS or Windows)		*/
/* #if defined (AIX),(_AIX)			AIX - UNIX							*/
/* #if defined (SUN), (SUN_ANSI)    SUN C - UNIX                     	*/
/* #if defined (MIPS), (mips)		MIPS - UNIX SYS V 					*/
/* #if defined (VXWORKS) 			VXWORKS - UNIX						*/
/* #if defined (__i960)				INTEL960      						*/
/* #if defined (VAX)                VAX C - VMS                      	*/
/* #if defined (UFST_MIPS64)        MIPS 64-bit, Native              	*/
/* #if defined (UFST_ALPHA_UNIX)    DEC Alpha (UNIX) 64-bit, Native		*/
/* #id defined (UFST_HP8K)												*/
/* #if defined (_OSK)				Xcompiler for Microware OS (68K)	*/
/* #if defined (_OS9000)			Xcompiler for Microware OS (PPC)	*/
/* #if defined (_PSOS)				Xcompiler for PSOS (PPC)			*/
/* #if defined (PTV_OS)				Xcompiler for PTV_OS (PPC)			*/
/* #if defined (__ARM)				Xcompiler for Helios (ARM)			*/
/* #if defined (__CC_ARM)			Xcompiler for RVDS (ARM)			*/
/* #if defined (ST20TP3)			Xcompiler for ST20 TP3				*/
/* #if defined (_WIN32_WCE)			Microsoft embedded C++ 4.0 			*/
/* #if defined (UFST_LINUX)			LINUX			*/
/* #if defined (UFST_MACOSX)			Mac OS X			*/
/*                                                                      */
/************************************************************************/

/*-------------------------------------------------------*/
#if defined (GCCx86)				/* GNU C Compiler on x86 */
#define  ANSI_DEFS
#define  CGFLAT32       1           /* Flat memory model*/
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  BYTEORDER      LOHI        /* byte ordering is Hi-Lo bytes     */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  VOIDTYPE       YES         /* compiler does have void type     */
#define  LINT_ARGS      YES         /* function decl with arguments     */
#define  ENTRYTYPE      NO          /* _export callback function def    */
#define FILENAMELEN  13
#define PATHNAMELEN  128
/* these typedefs are used to improve fixed-point-arithmetic performance */
#define HAS_FS_INT64
typedef long long FS_INT64;
#endif /* GCCx86 */
/*-------------------------------------------------------*/
#if defined (_AM29K)                /* AMD 29K Compiler                 */
#define  ANSI_DEFS
#define  CGFLAT32       1           /*Flat memory model*/
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  BYTEORDER      HILO        /* byte ordering is Hi-Lo bytes     */
#define  SIGNEDCHAR     YES          /* char is treated as signed byte   */
#define  VOIDTYPE       YES         /* compiler does have void type     */
#ifdef   LINT_ARGS
#undef   LINT_ARGS
#endif
#define  LINT_ARGS      YES         /* function decl with arguments     */
#define  ENTRYTYPE      NO          /* _export callback function def    */
#define FILENAMELEN  13
#define PATHNAMELEN  63
#endif /* AM29K */
/*-------------------------------------------------------*/
#if defined (MSVC)
#if defined( _WINDOWS)
#undef _WINDOWS
#endif
#if defined(KEEP_OLD_MSDOS_DEFINE)	/*** TEMPORARY (sandra, 11 Aug 99) ***/
#ifndef  MSDOS
#define  MSDOS
#endif
#endif /* KEEP_OLD_MSDOS_DEFINE */
#define  UFST_MSDOS					/* Windows flag for UFST               */
#ifndef  _CONSOLE
#define  _CONSOLE
#endif
#define  CGFLAT32  1
#define  ANSI_DEFS
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  BYTEORDER      LOHI        /* byte ordering is Lo-Hi bytes     */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  VOIDTYPE       YES         /* compiler does have void type     */
#define  LINT_ARGS      YES         /* argument checking identifier */
#define  ENTRYTYPE      NO          /* _export callback function def    */
#if defined(_WIN64)
#define  NAT_ALIGN      8           /* natural struct alignment is long */
#define  MEM_ALIGN      7			/* normally, NAT_ALIGN - 1 */
#else
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */
#endif
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define FILENAMELEN  13
#define PATHNAMELEN  255			/*** SWP 6/14/01  ... was 63 ***/
/* these typedefs are used to improve fixed-point-arithmetic performance */
#define HAS_FS_INT64
typedef __int64 FS_INT64;
#if defined(_WIN64)
typedef	__int64	UL64;		/* 64-bit unsigned longword         */
typedef	__int64	SL64;		/* 64-bit signed longword           */
#endif
#endif /* MSVC */
/*-------------------------------------------------------*/
#if defined (__WATCOMC__) /* only support 32 bit Watcom at this time */
#if defined (__386__) /* add 12/8/93 - rs */
#define CGFLAT32  1
#define  ANSI_DEFS
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  BYTEORDER      LOHI        /* byte ordering is Lo-Hi bytes     */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  VOIDTYPE       YES         /* compiler does have void type     */
#define  LINT_ARGS      YES         /* argument checking identifier */
#define  ENTRYTYPE      NO          /* _export callback function def    */
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
/* not defined previously - UFST_MSDOS values used */
#define FILENAMELEN  13
#define PATHNAMELEN  63
#endif /* __386__ */
#endif /* __WATCOMC__ */
/*-------------------------------------------------------*/
#if (defined(MSC) || (defined(_WINDOWS) && !defined(__BORLANDC__) && !defined(__WATCOMC__)))
 /* Microsoft C - Windows 3.0 */
#if defined(KEEP_OLD_MSDOS_DEFINE)	/*** TEMPORARY (sandra, 11 Aug 99) ***/
#ifndef  MSDOS
#define  MSDOS
#endif
#endif /* KEEP_OLD_MSDOS_DEFINE */
#define  UFST_MSDOS					/* Windows flag for UFST               */
#define  ANSI_DEFS
#define  INTLENGTH      16          /* int length of 16 bits            */
#define  BYTEORDER      LOHI        /* byte ordering is Lo-Hi bytes     */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  VOIDTYPE       YES         /* compiler does have void type     */
#define  LINT_ARGS      YES         /* argument checking identifier */
#if defined(_WINDOWS)
#define  ENTRYTYPE      YES         /* _export callback function def    */
#else
#define  ENTRYTYPE      NO          /* _export callback function def    */
#endif
#define  NAT_ALIGN      2           /* natural struct alignment is word */
#define  MEM_ALIGN      1			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#if (defined(M_I86SM) || defined(M_I86MM) || defined(M_I86TM))
#define FARPTR __far
#else
#define FARPTR
#endif
#if (HUGE_PTR_SUPPORT)
#define HUGEPTR __huge
#endif
#define FILENAMELEN  13
#define PATHNAMELEN  63
#endif /* Windows w/MSC compiler */
/*-------------------------------------------------------*/
#if defined (__BORLANDC__)          /* Borland C++ - MSDOS or Windows   */
/* make this look like MSC 6.0+ */
#define _MSC_VER    600
#if defined(__TINY__)
#define M_I86TM 1
#elif defined(__SMALL__)
#define M_I86SM 1
#elif defined(__MEDIUM__)
#define M_I86MM 1
#elif defined(__COMPACT__)
#define M_I86CM 1
#elif defined(__LARGE__)
#define M_I86LM 1
#endif /* (__TINY__) */
#if defined(KEEP_OLD_MSDOS_DEFINE)	/*** TEMPORARY (sandra, 11 Aug 99) ***/
#ifndef  MSDOS
#define  MSDOS
#endif
#endif /* KEEP_OLD_MSDOS_DEFINE */
#define  UFST_MSDOS					/* Windows flag for UFST               */
#define  ANSI_DEFS
#define  INTLENGTH      16          /* int length of 16 bits            */
#define  BYTEORDER      LOHI        /* byte ordering is Lo-Hi bytes     */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  VOIDTYPE       YES         /* compiler does have void type     */
#define  LINT_ARGS      YES         /* argument checking identifier */
#define  NAT_ALIGN      2           /* natural struct alignment is word */
#define  MEM_ALIGN      1			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#if (defined(M_I86SM) || defined(M_I86MM) || defined(M_I86TM))
#define FARPTR __far
#else
#define FARPTR
#endif
#if (HUGE_PTR_SUPPORT)
#define HUGEPTR __huge
#endif
#if (!defined(_Windows))
#define  ENTRYTYPE      NO          /* _export callback function def    */
#else /* Borland C++ & Windows */
#define  ENTRYTYPE      YES         /* _export callback function def    */
#if (!defined(_WINDOWS))
#define _WINDOWS    1
#endif
#endif /* (!defined(_Windows)) */
#define FILENAMELEN  13
#define PATHNAMELEN  63
#endif /* defined(__BORLANDC__) */
/*-------------------------------------------------------*/
#if (defined(AIX) || defined(_AIX)) /* AIX C - UNIX                     */
#define  ANSI_DEFS
#define  UFST_UNIX                  /* UNIX flag for UFST               */
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  BYTEORDER      HILO        /* byte ordering is Hi-Lo bytes     */
#define  SIGNEDCHAR     NO          /* char is treated as unsigned byte */
#define  VOIDTYPE       YES         /* compiler does have void type     */
#define  LINT_ARGS      YES         /* function decl without arguments  */
#define  ENTRYTYPE      NO          /* _export callback function def    */
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3           /* normally, NAT_ALIGN - 1          */
#ifdef __64BIT__
#define  SIZEOF_LONG    8           /* length in bytes of a "long" (64) */
#else
#define  SIZEOF_LONG    4           /* length in bytes of a "long" (32) */
#endif
#define  FILENAMELEN    31
#define  PATHNAMELEN    255
#endif  /* AIX */
/*-------------------------------------------------------*/
#if defined (SUN)                    /* SUN C - UNIX                    */
#define  UFST_UNIX                  /* UNIX flag for UFST               */
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  BYTEORDER      HILO        /* byte ordering is Lo-Hi bytes     */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  VOIDTYPE       YES         /* compiler does have void type     */
#define  LINT_ARGS      NO          /* function decl without arguments  */
#define  ENTRYTYPE      NO          /* _export callback function def    */
#ifdef   SPARC
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */
#else
#define  NAT_ALIGN      2           /* natural struct alignment is word */
#define  MEM_ALIGN      1			/* normally, NAT_ALIGN - 1 */
#endif
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define FILENAMELEN  13
#define PATHNAMELEN  255            /* This was 63 ...AOF               */
#endif	/* SUN */
/*-------------------------------------------------------*/
/* This definition block is identical to the previous SUN block, except that
 ANSI_DEFS and LINT_ARGS are enabled. This is necessary in order to test the
 new multithreading feature on the SUN platform, so that reentrancy works. */
#if defined (SUN_ANSI)               /* SUN C - UNIX                    */
#define  UFST_UNIX                  /* UNIX flag for UFST               */
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  BYTEORDER      HILO        /* byte ordering is Lo-Hi bytes     */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  VOIDTYPE       YES         /* compiler does have void type     */
#define  ANSI_DEFS
#define  LINT_ARGS      YES         /* function decl with arguments  */
#define  ENTRYTYPE      NO          /* _export callback function def    */
#ifdef   SPARC
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */
#else
#define  NAT_ALIGN      2           /* natural struct alignment is word */
#define  MEM_ALIGN      1			/* normally, NAT_ALIGN - 1 */
#endif
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define FILENAMELEN  13
#define PATHNAMELEN  255            /* This was 63 ...AOF               */
#endif	/* SUN_ANSI */
/*-------------------------------------------------------*/
#if (defined(MIPS) || defined(mips)) && !defined(VXWORKS)  /* MIPS - UNIX SYS V */
#define  ANSI_DEFS
#define  UFST_UNIX                  /* UNIX flag for UFST               */
#define  UNIX_SYSV
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  BYTEORDER      HILO        /* byte ordering is Lo-Hi bytes     */
/* System defaults char to unsigned, but forced to signed by -signed */
/* on link line (CFLAGS)                                             */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  VOIDTYPE       YES         /* compiler does have void type     */
#define  LINT_ARGS      YES         /* argument checking identifier     */
#define  ENTRYTYPE      NO          /* _export callback function def    */
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define FILENAMELEN  13
#define PATHNAMELEN  255            /* This was 63 ...AOF               */
/* these typedefs are used to improve fixed-point-arithmetic performance */
#define HAS_FS_INT64
typedef long long FS_INT64;
#endif  /* MIPS/mips && !VXWORKS */
/*-------------------------------------------------------*/
#if defined (VXWORKS) 				/* VXWORKS        */
#define  UFST_UNIX                  /* UNIX flag for UFST               */
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  BYTEORDER      HILO        /* byte ordering is Lo-Hi bytes     */
#ifdef __DCC__ /* diab */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte */
#else
#define  SIGNEDCHAR     NO          /* char is treated as unsigned byte */
#endif
#define  VOIDTYPE       YES         /* compiler does have void type     */
#define  LINT_ARGS      NO          /* function decl without arguments  */
#define  ENTRYTYPE      NO          /* _export callback function def    */
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define FILENAMELEN  13
#define PATHNAMELEN  63
#endif	/* VXWORKS */
/*-------------------------------------------------------*/
#if   defined (__i960)				/* INTEL960      */
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  BYTEORDER      LOHI        /* byte ordering is Lo-Hi bytes     */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  VOIDTYPE       YES         /* compiler does have void type     */
#define  LINT_ARGS      NO          /* function decl without arguments  */
#define  ENTRYTYPE      NO          /* _export callback function def    */
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define FILENAMELEN  13
#define PATHNAMELEN  63
#endif  /* __i960  */
/*-------------------------------------------------------*/
#if defined (VAX)                    /* VAX C - VMS                      */
#ifndef  VMS
#define  VMS                        /* VMS                              */
#endif
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  BYTEORDER      LOHI        /* byte ordering is Lo-Hi bytes     */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  VOIDTYPE       YES         /* compiler does have void type     */
#define  LINT_ARGS      YES         /* function decl with arguments     */
#define  ENTRYTYPE      NO          /* _export callback function def    */
#define  NAT_ALIGN      2           /* natural struct alignment is word */
#define  MEM_ALIGN      1			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define FILENAMELEN  13
#define PATHNAMELEN  63
#endif	/* VAX */
/*-------------------------------------------------------*/
#if defined (UFST_MIPS64)           /* MIPS 64-bit, Native              */
#define  ANSI_DEFS
#define  CGFLAT32       1           /* Flat memory model                */
#define  NAT_ALIGN      8           /* natural struct alignment is quad */
#define  MEM_ALIGN      7			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	8			/* length in bytes of a "long"		*/
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  BYTEORDER      HILO        /* byte ordering is Hi-Lo bytes     */
#define  SIGNEDCHAR     NO          /* char is treated as signed byte   */
#define  VOIDTYPE       YES         /* compiler does have void type     */
#ifdef   LINT_ARGS
#undef   LINT_ARGS
#endif
#define  LINT_ARGS      YES         /* function decl with arguments     */
#define  ENTRYTYPE      NO          /* _export callback function def    */
/* not defined previously - UFST_ALPHA_UNIX values used */
#define FILENAMELEN  13
#define PATHNAMELEN  255
#endif /* UFST_MIPS64 */
/*-------------------------------------------------------*/
#if defined (UFST_ALPHA_UNIX)       /* DEC Alpha (UNIX) 64-bit, Native Compiler */
#define  UFST_UNIX                  /* UNIX flag for UFST               */
#define  ANSI_DEFS
#define  CGFLAT32       1           /* Flat memory model                */
#define  NAT_ALIGN      8           /* natural struct alignment is quad */
#define  MEM_ALIGN      7			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	8			/* length in bytes of a "long"		*/
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  BYTEORDER      LOHI        /* Assume byte ordering of Hi-Lo    */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  VOIDTYPE       YES         /* compiler does have void type     */
#ifdef   LINT_ARGS
#undef   LINT_ARGS
#endif
#define  LINT_ARGS      YES         /* function decl with arguments     */
#define  ENTRYTYPE      NO          /* _export callback function def    */
#define FILENAMELEN  13
#define PATHNAMELEN  255
#endif /* UFST_ALPHA_UNIX */
/*-------------------------------------------------------*/
#if defined(UFST_HP8K)
#ifndef UFST_UNIX
#define UFST_UNIX
#endif
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  BYTEORDER      HILO        /* byte ordering is Lo-Hi bytes     */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  VOIDTYPE       YES         /* compiler does have void type     */
#define  LINT_ARGS      NO          /* function decl without arguments  */
#define  ENTRYTYPE      NO          /* _export callback function def    */
#if defined(__LP64__)
#define  NAT_ALIGN      8      /* 8 byte word LP64 data model      */
#define  MEM_ALIGN      3	    /* normally, NAT_ALIGN - 1 */
#else
#define  NAT_ALIGN      4      /* 4 byte word ILP32 data model     */
#define  MEM_ALIGN      3 	    /* normally, NAT_ALIGN - 1 */
#endif
#define  SIZEOF_LONG	4    	    /* length in bytes of a "long"	*/
#define FILENAMELEN  13
#define PATHNAMELEN  255
#endif	/* UFST_HP8K */
/*-------------------------------------------------------*/
#if defined (_OSK) || defined (_OS9000)
#define  ANSI_DEFS
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  BYTEORDER      HILO        /* byte ordering is Hi-Lo bytes     */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  VOIDTYPE       YES         /* compiler does have void type     */
#define  LINT_ARGS      YES         /* function decl with arguments     */
#define  ENTRYTYPE      NO          /* _export callback function def    */
#define  NAT_ALIGN      4           /* natural struct alignment is word */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#include <modes.h>					/* this is a Microware header file  */
#define FILENAMELEN  13
#define PATHNAMELEN  128
#endif /* _OSK || _OS9000 */
/*-------------------------------------------------------*/
#if defined (_PSOS)
#define  ANSI_DEFS
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  BYTEORDER      HILO        /* byte ordering is Hi-Lo bytes     */
#define  SIGNEDCHAR     NO          /* char is treated as unsigned byte */
#define  VOIDTYPE       YES         /* compiler does have void type     */
#define  LINT_ARGS      YES         /* function decl with arguments     */
#define  ENTRYTYPE      NO          /* _export callback function def    */
#define  NAT_ALIGN      4           /* natural struct alignment is word */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define FILENAMELEN  13
#define PATHNAMELEN  128
#endif /* _PSOS */
/*-------------------------------------------------------*/
#if defined (PTV_OS)
#define  ANSI_DEFS
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  BYTEORDER      HILO        /* byte ordering is Hi-Lo bytes     */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  VOIDTYPE       YES         /* compiler does have void type     */
#define  LINT_ARGS      YES         /* function decl with arguments     */
#define  ENTRYTYPE      NO          /* _export callback function def    */
#define  NAT_ALIGN      4           /* natural struct alignment is word */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define FILENAMELEN  13
#define PATHNAMELEN  128
#endif /* PTV_OS */
/*-------------------------------------------------------*/
#if defined (__ARM)
#define ANSI_DEFS
#define INTLENGTH	32
#define BYTEORDER	LOHI
#define SIGNEDCHAR	NO
/* "signedchar yes" ALSO worked for MicroType - however, we haven't ported
TrueType yet - one of the "signedchar" options might fail in that case */
/* #define SIGNEDCHAR YES */
#define VOIDTYPE	YES
#define LINT_ARGS	YES
#define ENTRYTYPE	NO
#define NAT_ALIGN	4
#define MEM_ALIGN   3			/* normally, NAT_ALIGN - 1 */
#define SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define FILENAMELEN  13
#define PATHNAMELEN  128
#endif /* ARM */
/*-------------------------------------------------------*/
#if defined (__CC_ARM)
#define ANSI_DEFS
#define INTLENGTH	32
#define BYTEORDER	LOHI
#define SIGNEDCHAR	NO
#define VOIDTYPE	YES
#define LINT_ARGS	YES
#define ENTRYTYPE	NO
#define NAT_ALIGN	4
#define MEM_ALIGN   3			/* normally, NAT_ALIGN - 1 */
#define SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define FILENAMELEN  13
#define PATHNAMELEN  128
#endif /* __CC_ARM */
/*-------------------------------------------------------*/
#if defined (ST20TP3)
#define  ANSI_DEFS
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  BYTEORDER      LOHI        /* byte ordering is Intel		    */
#define  SIGNEDCHAR     NO          /* char is treated as unsigned byte */
#define  VOIDTYPE       YES         /* compiler does have void type     */
#define  LINT_ARGS      YES         /* function decl with arguments     */
#define  ENTRYTYPE      NO          /* _export callback function def    */
#define  NAT_ALIGN      4           /* natural struct alignment is word */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */
#define SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define FILENAMELEN  13
#define PATHNAMELEN  128
#endif /* ST20TP3 */
/*-------------------------------------------------------*/
#if defined (UFST_LINUX)
#define  ANSI_DEFS
#define  INTLENGTH      32          /* int length of 32 bits            */
#if defined(_PPC_)
#define  BYTEORDER      HILO        /* byte ordering is Hi-Lo bytes     */
#define  SIGNEDCHAR     NO         /* char is treated as signed byte   */
#elif defined(__ARMEL__) /* GCC ARM low-byte */
#define  BYTEORDER      LOHI        /* byte ordering is Lo-Hi bytes     */
#define  SIGNEDCHAR     NO         /* char is treated as unsigned byte   */
#elif defined(__ARMEB__) /* GCC ARM high-byte */
#define  BYTEORDER      HILO        /* byte ordering is Hi-Lo bytes     */
#define  SIGNEDCHAR     NO         /* char is treated as unsigned byte   */
#else
#define  BYTEORDER      LOHI        /* byte ordering is Lo-Hi bytes     */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#endif
#define  VOIDTYPE       YES         /* compiler does have void type     */
#define  LINT_ARGS      YES         /* argument checking identifier */
#define  ENTRYTYPE      NO          /* _export callback function def    */
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define FILENAMELEN  13
#define PATHNAMELEN  255			/*** SWP 6/14/01  ... was 63 ***/
/* these typedefs are used to improve fixed-point-arithmetic performance */
#define HAS_FS_INT64
typedef long long FS_INT64;
#endif /* UFST_LINUX */
/*-------------------------------------------------------*/
#if defined (UFST_NETBSD)
#ifndef UFST_UNIX
#define UFST_UNIX
#endif
#define  ANSI_DEFS
#define  INTLENGTH      32          /* int length of 32 bits            */
#if defined(__ARMEL__) /* GCC ARM low-byte */
#define  BYTEORDER      LOHI        /* byte ordering is Lo-Hi bytes     */
#define  SIGNEDCHAR     NO          /* char is treated as unsigned byte */
#elif defined(__ARMEB__) /* GCC ARM high-byte */
#define  BYTEORDER      HILO        /* byte ordering is Hi-Lo bytes     */
#define  SIGNEDCHAR     NO          /* char is treated as unsigned byte */
#else
#define  BYTEORDER      LOHI        /* byte ordering is Lo-Hi bytes     */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#endif
#define  VOIDTYPE       YES         /* compiler does have void type     */
#define  LINT_ARGS      YES         /* argument checking identifier     */
#define  ENTRYTYPE      NO          /* _export callback function def    */
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3           /* normally, NAT_ALIGN - 1          */
#define  SIZEOF_LONG	4           /* length in bytes of a "long"	*/
#define FILENAMELEN  13
#define PATHNAMELEN  255			/*** SWP 6/14/01  ... was 63 ***/
/* these typedefs are used to improve fixed-point-arithmetic performance */
#define HAS_FS_INT64
typedef long long FS_INT64;
#endif /* UFST_NETBSD */
/*-------------------------------------------------------*/
#if defined (UFST_MACOSX)				/* Mac OS X */
#define  ANSI_DEFS
#define  CGFLAT32       1           /* Flat memory model*/
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define  INTLENGTH      32          /* int length of 32 bits            */
#ifdef __BIG_ENDIAN__
#define  BYTEORDER      HILO        /* byte ordering is Hi-Lo bytes     */
#endif
#ifdef __LITTLE_ENDIAN__
#define  BYTEORDER      LOHI        /* byte ordering is Lo-Hi bytes     */
#endif
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  VOIDTYPE       YES         /* compiler does have void type     */
#define  LINT_ARGS      YES         /* function decl with arguments     */
#define  ENTRYTYPE      NO          /* _export callback function def    */
#define FILENAMELEN  13
#define PATHNAMELEN  128
/* these typedefs are used to improve fixed-point-arithmetic performance */
#define HAS_FS_INT64
typedef long long FS_INT64;
#endif /* UFST_MACOSX */
/*-------------------------------------------------------*/


/* If the following is true, then the special (slower) chunk processing in fill.c
   and nzwind.c doesn't need to happen */
#define NATORDER  ((RASTER_ORG == EIGHT_BIT_CHUNK)                \
                 || ((BYTEORDER==LOHI) && (LEFT_TO_RIGHT_BIT_ORDER == 0)) \
                 || ((BYTEORDER==HILO) && (LEFT_TO_RIGHT_BIT_ORDER == 1)))


/************************************************************************/
/*                      Typedef Definitions                             */
/************************************************************************/

#if      LINT_ARGS == NO
#undef   LINT_ARGS
#endif

#if ENTRYTYPE == YES
#define CGENTRY _export far PASCAL  /* windows callback entry function  */
#else
#define CGENTRY
#endif


#if SIGNEDCHAR == YES
typedef  char           SB8;        /* signed byte ( -127, 128 )        */
typedef  unsigned char  UB8;        /* unsigned byte ( 0, 255 )         */
#else
typedef  signed char    SB8;        /* signed byte ( -127, 128 )        */
typedef  unsigned char  UB8;        /* unsigned byte ( 0, 255 )         */
#endif

/*********************************************/
/* data types that depend on INTLENGTH value */
/*********************************************/

#if INTLENGTH == 16
typedef  int            SW16;       /* 16-bit signed word               */
typedef  unsigned int   UW16;       /* 16-bit unsigned word             */
typedef  long           SL32;       /* 32-bit signed longword           */
typedef  unsigned long  UL32;       /* 32-bit unsigned longword         */

#elif INTLENGTH == 32
typedef  unsigned short UW16;       /* 16-bit unsigned word             */
typedef  unsigned int   UL32;       /* 32-bit unsigned longword         */
#if defined (PTV_OS)
/* if default "int" is unsigned */
typedef  signed short   SW16;       /* 16-bit signed word               */
typedef  signed int     SL32;       /* 32-bit signed longword           */
#else
/* if default "int" is signed */
typedef  short          SW16;       /* 16-bit signed word               */
typedef  int            SL32;       /* 32-bit signed longword           */
#endif	/* defined (PTV_OS)	*/

#elif INTLENGTH == 64

	/* no such platforms yet... */

#endif	/* INTLENGTH cases */

#if (SIZEOF_LONG == 8)
typedef	unsigned long	UL64;		/* 64-bit unsigned longword         */
typedef	signed long		SL64;		/* 64-bit signed longword           */

/* these typedefs are used to improve fixed-point-arithmetic performance */
#define HAS_FS_INT64
typedef long FS_INT64;

#endif /* 64-bit longs */

/**********************/
/*	native datatypes  */
/**********************/

typedef	char			FILECHAR;
typedef unsigned int	UINTG;

#if  defined (PTV_OS)
/* if default "int" is unsigned */
typedef  signed int     INTG;
#else
/* if default "int" is signed */
typedef  int            INTG;
#endif

/*******************/
/* more data types */
/*******************/

#if VOIDTYPE == YES
#define  VOID           void        /* void data type                   */
#else
#define  VOID           int         /* void data type changed to int    */
#endif

/* if we are using the Microware OS, then BOOLEAN needs to be
 32-bit rather than 16-bit - otherwise, the size of "if_state" will be
 inconsistent in /surflib and /surfer/demo, and bad things will happen. */
#if (defined (_OSK) || defined (_OS9000))

/* don't define BOOLEAN if source file includes Microware/MAUI header files,
 as BOOLEAN is already defined in MAUI header files (as an enum!) */
#if !defined (_USE_MAUI_TYPES)
typedef  SL32           BOOLEAN;    /* boolean ( TRUE = 1, FALSE = 0 )  */
#endif

/* similarly, to match size of BOOLEAN in MSVC (AFX) libraries */
#elif (defined(MSVC) || defined(_WIN32_WCE))
typedef UB8	BOOLEAN;

#else
/* actually, why can't BOOLEAN be an 8-bit value by default? */
typedef  SW16           BOOLEAN;    /* boolean ( TRUE = 1, FALSE = 0 )  */

#endif	/* (defined (_OSK) || defined (_OS9000)) */

typedef  double         DOUBLE;     /* floating point double precision  */
typedef  SL32           CGFIXED;    /* 32 bit fixed point number        */

typedef  SW16           NZCOUNTER;  /* used for NON_Z_WIND = 1          */

#if INT_FP                          /* with 16 bit fractional part      */
typedef struct                      /* Floating point                   */
{
    SL32 n;    /* normalized, signed mantissa */
    INTG e;    /* exponent                    */
} FPNUM;
#else
#if  defined (PTV_OS)
typedef float FPNUM;
#else
typedef double FPNUM;
#endif
#endif

/**********************************************/
/* Seemingly-redundant data types definitions */
/**********************************************/

typedef  INTG           COUNTER;    /* machine's word signed counter    */
typedef  UINTG          UCOUNTER;   /* machine's word unsigned counter  */

/* typedefs used by SWP code in various places throughout UFST... */
typedef SL32 FRACT;				/* as 2.30 */
typedef SL32 FS_FIXED;			/* as 16.16 */
typedef SL32 F26DOT6;			/* as 26.6 */

/************************************************************************/
/*                      Data Types Definitions                          */
/************************************************************************/
/*
** The common problem of mapping various pointer types is partialy
** addressed here.
*/

#if !defined FARPTR
#define FARPTR
#endif
#if !defined HUGEPTR
#define HUGEPTR
#endif

    /* Pointer Data Types Definitions */
typedef COUNTER FARPTR  * PCOUNTER;
typedef NZCOUNTER FARPTR *  PNZCOUNTER;
typedef SB8   FARPTR * LPSB8;
#if (HUGE_PTR_SUPPORT)
typedef SB8   HUGEPTR * HPSB8;
typedef UB8   HUGEPTR * HPUB8;
#endif
typedef UB8   FARPTR * LPUB8;
typedef UW16  FARPTR * LPUW16;
typedef SW16  FARPTR * LPSW16;
typedef SL32  FARPTR * LPSL32;
typedef UL32  FARPTR * LPUL32;
#if (SIZEOF_LONG == 8)
typedef SL64  FARPTR * LPSL64;
typedef UL64  FARPTR * LPUL64;
#endif /* 64-bit longs */
typedef CGFIXED FARPTR * PCGFIXED;
typedef SB8   FARPTR * FARPTR * LPLPSB8;
typedef UB8   FARPTR * FARPTR * LPLPUB8;
typedef UB8   HUGEPTR * FARPTR * LPHPUB8;
typedef SB8   HUGEPTR * FARPTR * LPHPSB8;
typedef VOID  FARPTR * PVOID;
typedef PVOID  FARPTR * PPVOID;
typedef FPNUM FARPTR * PFPNUM;


typedef struct word_point_struct {
   SW16 x;                      /* point x coordinate                   */
   SW16 y;                      /* point y coordinate                   */
} SW16POINT;
typedef SW16POINT FARPTR * PSW16POINT;

typedef struct word_vector_struct {
   SW16 x;                      /* vector x component                   */
   SW16 y;                      /* vector y component                   */
} SW16VECTOR;
typedef  SW16VECTOR FARPTR * PSW16VECTOR;

typedef struct
{
    SW16VECTOR ll;
    SW16VECTOR ur;
} BOX;
typedef BOX FARPTR * PBOX;

typedef struct
{
    SL32 x;
    SL32 y;
} SL32VECTOR;
typedef  SL32VECTOR FARPTR * PSL32VECTOR;

typedef struct
{
    FPNUM x;
    FPNUM y;
} FPVECTOR;
typedef  FPVECTOR FARPTR * PFPVECTOR;

typedef struct
{
    SL32VECTOR ll;
    SL32VECTOR ur;
} SL32BOX;
typedef SL32BOX FARPTR * PSL32BOX;

typedef struct word_window_struct {
   SW16 Top;                    /* top edge coordinate                  */
   SW16 Left;                   /* left edge coordinate                 */
   SW16 Bottom;                 /* bottom edge coordinate               */
   SW16 Right;                  /* right edge coordinate                */
} SW16WINDOW;
typedef  SW16WINDOW FARPTR * PSW16WINDOW;

typedef struct
{
    CGFIXED x;
    CGFIXED y;
} CGPOINTFX;
typedef CGPOINTFX FARPTR * PCGPOINTFX;

typedef CGPOINTFX FS_FIXED_POINT;	/* 01-28-02 jfd */


#if (INTR_SIZE == 16)
typedef  SW16        INTR;
typedef  SW16VECTOR  INTRVECTOR;
typedef  BOX         INTRBOX;
#elif (INTR_SIZE == 32)
typedef  SL32        INTR;
typedef  SL32VECTOR  INTRVECTOR;
typedef  SL32BOX     INTRBOX;
#endif

typedef  INTR  FARPTR * PINTR;
typedef  INTRVECTOR  FARPTR * PINTRVECTOR;
typedef  INTRBOX     FARPTR * PINTRBOX;

/************************************************************************/
/*                      Storage Class Definitions                       */
/************************************************************************/

#define  REG            register    /* register variable                */
#define  EXTERN         extern      /* external variable                */
#define  MLOCAL         static      /* local to module                  */
#define  GLOBAL         /**/        /* global variable                  */
#define	 CONST			const		/* read-only data					*/


/*  You might need to redefine CONST in some environments: for example,
if "const" is not a defined keyword under the _FOO compiler, just add

#ifdef _FOO
#define	CONST
#endif
*/

/************************************************************************/
/*                      Path and File name lengths                      */
/************************************************************************/

typedef FILECHAR PATHNAME [PATHNAMELEN];

/************************************************************************/
/*                      Macro Definitions                               */
/************************************************************************/

#if !defined( ABS )
#define ABS(a)    ( ((a) < (SW16)0) ? -(a) : (a) )
#endif
#if !defined( LABS )
#define LABS(a)   ( ((a) < 0L) ? -(a) : (a) )
#endif
#if !defined( MIN )
#define MIN(a,b)  ( ((a) < (b)) ? (a) : (b) )
#endif
#if !defined( MAX )
#define MAX(a,b)  ( ((a) < (b)) ? (b) : (a) )
#endif

/************************************************************************/
/*                      Miscellaneous Definitions                       */
/************************************************************************/

#define  FAILURE        (-1)        /* Function failure return val      */
#ifndef SUCCESS
#define  SUCCESS        (0)         /* Function success return val      */
#endif

#ifndef TRUE /* conditionalize to work w/Windows - 4/21/92 - rs */
#define  TRUE           (1)         /* Function TRUE  value             */
#endif
#ifndef FALSE /* conditionalize to work w/Windows - 4/21/92 - rs */
#define  FALSE          (0)         /* Function FALSE value             */
#endif

#define  MAX_WORD       ((SW16)(0x7FFF))
#define  MIN_WORD       ((SW16)(0x8000))
#define  MAX_LONG       (0x7FFFFFFFL)
#define  MIN_LONG       (0x80000000L)

#if defined (UFST_MSDOS) || defined (__OS2__)
#define  MAX_UWORD       ((UW16)(0xFFFFU))
#define  MAX_ULONG       (0xFFFFFFFFUL)
#else
#define  MAX_UWORD       ((UW16)(0xFFFF))
#define  MAX_ULONG       (0xFFFFFFFFL)
#endif

/************************************************************************/
/* 		Defines used to include target-specific header files			*/
/************************************************************************/

#define	MAYBE_FCNTL_H \
!defined(_AM29K) && !defined(_OSK) && !defined(_OS9000) && !defined(ST20TP3) \
	&& !(defined (__ARM) && defined (_WIN32_WCE))  && !defined (__CC_ARM)

#define	MAYBE_FCNTL_HIF_H \
defined(_AM29K) && !defined(SUN)

#define MAYBE_MALLOC_H \
!defined(VXWORKS) && !defined(_OSK) && !defined(_OS9000) && !defined(__ARM) \
	&& !defined(__i960) && !defined(ST20TP3)  && !defined(UFST_MACOSX) && !defined (__CC_ARM)

#define MAYBE_IO_H \
defined (UFST_MSDOS) || defined (__OS2__)

/* VXWORKS clause may need additional test */
#define MAYBE_UNISTD_H \
(defined(UFST_UNIX) && !defined(__i960) && !defined(_AM29K)) || defined(VXWORKS) || defined(UFST_LINUX)

#define MAYBE_MEMORY_H \
!defined(__WATCOMC__) && !defined(__i960) && !defined(VXWORKS) && !defined (__CC_ARM)

#define USING_16_BIT_DOS \
defined(UFST_MSDOS) && !defined(CGFLAT32)

#define SYS_PATH_USES_BACKSLASH \
defined (UFST_MSDOS) || defined (__OS2__) || defined (GCCx86)

/* USE_BINARY_READMODE test should match conditions in mixmodel.h */
#define USE_BINARY_READMODE \
defined (UFST_MSDOS) || defined (__OS2__) || defined (GCCx86) \
	|| defined(__WATCOMC__) || defined(__i960)  || defined (_WIN32_WCE)

#endif	/* __PORT__	*/

