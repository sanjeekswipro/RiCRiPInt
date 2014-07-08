
/* Copyright (C) 2008-2013 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/* ufstport.h */


#ifndef __PORT__
#define __PORT__


/************************************************************************/
/*                      Configuration Conditionals                      */
/************************************************************************/

#define  YES            1           /* Configuration feature enable     */
#define  NO             0           /* Configuration feature disable    */
#define  LOHI           1           /* Addr:LoByte, Addr+1:HiByte       */
#define  HILO           0           /* Addr:HiByte, Addr+1:LoByte       */

/************************************************************************/
/*                    Compiler - OS Configurations			            */
/************************************************************************/
/*																		*/
/* Use make file to Define only one of the following configurations:	*/
/*																		*/
/* #if defined (GCCx86)				GNU C Compiler on x86 				*/
/* #if defined (_AM29K)             AMD 29K HighC Compiler           	*/
/* #if defined (_WIN32),(_WIN32_WCE) Microsoft C, C++					*/
/* #if defined (AIX),(_AIX)			AIX - UNIX							*/
/* #if defined (SUN),(SUN_ANSI)    SUN C - UNIX                     	*/
/* #if defined (SUN_64)			    SUN C - 64-bit UNIX                	*/
/* #if defined (VXWORKS) 			VXWORKS - UNIX						*/
/* #if defined (__i960)				INTEL960      						*/
/* #if defined (UFST_HP8K)												*/
/* #if defined (UFST_LINUX)			LINUX								*/
/*                                                                      */
/************************************************************************/

/* customer code add */
/* MS Visual studio uses a type modifier to flag types that change size to match
   ptr size in order to diagnose compatibility issues. We use the PTRSIZETYPE to
   accomodate this */
#ifdef PTRSIZETYPE
#undef PTRSIZETYPE
#endif

#if defined(_Wp64)
#define PTRSIZETYPE __w64
#else
#define PTRSIZETYPE
#endif
/* end customer code add */

/*-------------------------------------------------------*/
#if defined (GCCx86)				/* GNU C Compiler on x86 */
#define  ANSI_DEFS
#define  LINT_ARGS      YES         /* function decl with arguments     */
#define  BYTEORDER      LOHI        /* byte ordering is Hi-Lo bytes     */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define HAS_FS_INT64
typedef long long FS_INT64;
#define PATHNAMELEN  128
#endif /* GCCx86 */

/*-------------------------------------------------------*/
#if defined (_AM29K)                /* AMD 29K Compiler                 */
#define  ANSI_DEFS
#define  LINT_ARGS      YES         /* function decl with arguments     */
#define  BYTEORDER      HILO        /* byte ordering is Hi-Lo bytes     */
#define  SIGNEDCHAR     YES          /* char is treated as signed byte   */
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define PATHNAMELEN  63
#endif /* AM29K */

/*-------------------------------------------------------*/
#if (defined(_WIN32) || defined(_WIN32_WCE))
#if defined( _WINDOWS)
#undef _WINDOWS
#endif
#define  UFST_MSDOS					/* Windows flag for UFST               */
#ifndef  _CONSOLE
#define  _CONSOLE
#endif
#define  ANSI_DEFS
#define  LINT_ARGS      YES         /* argument checking identifier */
#define  BYTEORDER      LOHI        /* byte ordering is Lo-Hi bytes     */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  INTLENGTH      32          /* int length of 32 bits            */

#ifdef _WIN64
#define  NAT_ALIGN      8          /* natural struct alignment is __int64 */
#define  MEM_ALIGN      7			/* normally, NAT_ALIGN - 1 */
typedef unsigned __int64 UL64;	/* 64-bit unsigned longword */
typedef signed __int64   SL64;  /* 64-bit unsigned longword */

#else
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */
#endif

#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define HAS_FS_INT64
typedef __int64 FS_INT64;
#define PATHNAMELEN  255
#endif /* _WIN32, _WIN32_WCE */

/*-------------------------------------------------------*/
#if (defined(AIX) || defined(_AIX)) /* AIX C - UNIX                     */

#define  UFST_UNIX                  /* UNIX flag for UFST               */
#define  ANSI_DEFS
#define  LINT_ARGS      YES         /* function decl without arguments  */

#define  BYTEORDER      HILO        /* byte ordering is Hi-Lo bytes     */
#define  SIGNEDCHAR     NO          /* char is treated as unsigned byte */
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3           /* normally, NAT_ALIGN - 1          */

#ifdef __64BIT__
#define  SIZEOF_LONG    8           /* length in bytes of a "long" (64) */
typedef	unsigned long	UL64;		/* 64-bit unsigned longword         */
typedef	signed long		SL64;		/* 64-bit signed longword           */
#define HAS_FS_INT64
typedef long FS_INT64;

#else
#define  SIZEOF_LONG    4           /* length in bytes of a "long" (32) */
#endif

#define  PATHNAMELEN    255
#endif  /* AIX */

/*-------------------------------------------------------*/
#if defined (SUN)                    /* SUN C - UNIX                    */
#define  UFST_UNIX                  /* UNIX flag for UFST               */
#define  LINT_ARGS      NO          /* function decl without arguments  */
#define  BYTEORDER      HILO        /* byte ordering is Lo-Hi bytes     */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  INTLENGTH      32          /* int length of 32 bits            */

#ifdef   SPARC
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */

#else
#define  NAT_ALIGN      2           /* natural struct alignment is word */
#define  MEM_ALIGN      1			/* normally, NAT_ALIGN - 1 */
#endif

#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define PATHNAMELEN  255
#endif	/* SUN */

/*-------------------------------------------------------*/
/* This definition block is identical to the previous SUN block, except that
 ANSI_DEFS and LINT_ARGS are enabled. This is necessary in order to test the
 new multithreading feature on the SUN platform, so that reentrancy works. */
#if defined (SUN_ANSI)               /* SUN C - UNIX                    */
#define  UFST_UNIX                  /* UNIX flag for UFST               */
#define  ANSI_DEFS
#define  LINT_ARGS      YES         /* function decl with arguments  */
#define  BYTEORDER      HILO        /* byte ordering is Lo-Hi bytes     */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  INTLENGTH      32          /* int length of 32 bits            */

#ifdef   SPARC
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */

#else
#define  NAT_ALIGN      2           /* natural struct alignment is word */
#define  MEM_ALIGN      1			/* normally, NAT_ALIGN - 1 */
#endif

#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define PATHNAMELEN  255
#endif	/* SUN_ANSI */

/*-------------------------------------------------------*/
/* This definition block is based on the SUN_ANSI block, with changes added
to work on our new 64-bit SUN platform. */
#if defined (SUN_64)               /* SUN C - UNIX                    */
#define  UFST_UNIX                  /* UNIX flag for UFST               */
#define  ANSI_DEFS
#define  LINT_ARGS      YES         /* function decl with arguments  */
#define  BYTEORDER      HILO        /* byte ordering is Hi-Lo bytes     */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  NAT_ALIGN      8           /* natural struct alignment is quad */
#define  MEM_ALIGN      7			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	8			/* length in bytes of a "long"		*/
typedef	unsigned long	UL64;		/* 64-bit unsigned longword         */
typedef	signed long		SL64;		/* 64-bit signed longword           */
#define HAS_FS_INT64
typedef long FS_INT64;
#define PATHNAMELEN  255
#endif	/* SUN_ANSI */

/*-------------------------------------------------------*/
#if defined (VXWORKS) 				/* VXWORKS        */
#define  UFST_UNIX                  /* UNIX flag for UFST               */
#pragma align 4
#define  LINT_ARGS      NO          /* function decl without arguments  */
#define  BYTEORDER      LOHI        /* byte ordering is Lo-Hi bytes     */
#define  SIGNEDCHAR     NO          /* char is treated as unsigned byte */
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define PATHNAMELEN  63
#endif	/* VXWORKS */

/*-------------------------------------------------------*/
#if   defined (__i960)				/* INTEL960      */
#define  LINT_ARGS      NO          /* function decl without arguments  */
#define  BYTEORDER      LOHI        /* byte ordering is Lo-Hi bytes     */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define PATHNAMELEN  63
#endif  /* __i960  */

/*-------------------------------------------------------*/
#if defined(UFST_HP8K)
#ifndef UFST_UNIX
#define UFST_UNIX
#endif
#define  LINT_ARGS      NO          /* function decl without arguments  */
#define  BYTEORDER      HILO        /* byte ordering is Lo-Hi bytes     */
#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#define  INTLENGTH      32          /* int length of 32 bits            */

#if defined(__LP64__)
#define  NAT_ALIGN      8      /* 8 byte word LP64 data model      */
#define  MEM_ALIGN      3	    /* normally, NAT_ALIGN - 1 */

#else
#define  NAT_ALIGN      4      /* 4 byte word ILP32 data model     */
#define  MEM_ALIGN      3 	    /* normally, NAT_ALIGN - 1 */
#endif

#define  SIZEOF_LONG	4    	    /* length in bytes of a "long"	*/
#define PATHNAMELEN  255
#endif	/* UFST_HP8K */

/*-------------------------------------------------------*/
#if defined (UFST_LINUX)
#define  ANSI_DEFS
#define  LINT_ARGS      YES         /* argument checking identifier */
#define  BYTEORDER      LOHI        /* byte ordering is Lo-Hi bytes     */

#if defined (ARM_TS7200)
#define  SIGNEDCHAR     NO         /* char is treated as unsigned byte   */
#else

#define  SIGNEDCHAR     YES         /* char is treated as signed byte   */
#endif

#define  INTLENGTH      32          /* int length of 32 bits            */
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3			/* normally, NAT_ALIGN - 1 */
#define  SIZEOF_LONG	4			/* length in bytes of a "long"		*/
#define HAS_FS_INT64
typedef long long FS_INT64;
#define PATHNAMELEN  255
#endif /* UFST_LINUX */
/*-------------------------------------------------------*/
#if defined (UFST_NETBSD)
#define  UFST_UNIX                  /* UNIX flag for UFST               */
#define  ANSI_DEFS
#define  LINT_ARGS      YES         /* argument checking identifier */
#define  BYTEORDER      LOHI        /* byte ordering is Lo-Hi bytes     */
#define  INTLENGTH      32          /* int length of 32 bits            */
#define  NAT_ALIGN      4           /* natural struct alignment is long */
#define  MEM_ALIGN      3	    /* normally, NAT_ALIGN - 1          */
#define  SIZEOF_LONG	4	    /* length in bytes of a "long"	*/
#define HAS_FS_INT64
typedef long long FS_INT64;
#define PATHNAMELEN  255
#endif /* UFST_NETBSD */

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

/* if default "int" is signed */
typedef  short          SW16;       /* 16-bit signed word               */
typedef  int            SL32;       /* 32-bit signed longword           */

#elif INTLENGTH == 64

	/* no such platforms yet... */

#endif	/* INTLENGTH cases */

/**********************/
/*	native datatypes  */
/**********************/

typedef	char			FILECHAR;
typedef unsigned int   UINTG;
typedef int            INTG;	/* if default "int" is signed */

/*******************/
/* more data types */
/*******************/

#define  VOID           void        /* void data type                   */

/* match size of BOOLEAN in _WIN32 (AFX) libraries */
#if (defined(_WIN32) || defined(_WIN32_WCE))
typedef UB8	BOOLEAN;

#else
/* actually, why can't BOOLEAN be an 8-bit value by default? */
typedef  SW16           BOOLEAN;    /* boolean ( TRUE = 1, FALSE = 0 )  */

#endif	/* _WIN32 || _WIN32_WCE */

typedef  double         DOUBLE;     /* floating point double precision  */
typedef  SL32           CGFIXED;    /* 32 bit fixed point number        */

typedef  SW16           NZCOUNTER;  /* used for NON_Z_WIND = 1          */

typedef double FPNUM;

#ifdef _WINDOWS
#define UFST_MAXPATHLEN        128 /* max length of pathname (incl null) */
#else
#define UFST_MAXPATHLEN        65  /* max length of pathname (incl null) */
#endif /* _WINDOWS */

/**********************************************/
/* Seemingly-redundant data types definitions */
/**********************************************/

typedef  INTG           COUNTER;    /* machine's word signed counter    */
typedef  UINTG          UCOUNTER;   /* machine's word unsigned counter  */

/* typedefs used by SWP code in various places throughout UFST... */
typedef SL32 FRACT;				/* as 2.30 */
typedef SL32 FS_FIXED;			/* as 16.16 */
typedef SL32 F26DOT6;			/* as 26.6 */

/* additional basic types used by iType code brought over into UFST */

typedef VOID FS_VOID;

typedef SL32 FS_LONG;
typedef UL32 FS_ULONG;
typedef SW16 FS_SHORT;
typedef UW16 FS_USHORT;
typedef SB8  FS_TINY;
typedef UB8  FS_BYTE;

typedef FS_SHORT SHORTFRACT;		/* as 2.14 */
#define FRAC2FIX(x) ((FS_FIXED)(x)<<2)

typedef UB8 CACHE_ENTRY;
typedef SB8 FS_BOOLEAN;

#if FS_EDGE_HINTS || FS_EDGE_RENDER

/* derived ADF types (moved from adftypesystem.h) */
typedef        FS_TINY         ADF_I8;
typedef        FS_SHORT        ADF_I16;
typedef        FS_LONG         ADF_I32;
typedef        FS_BYTE         ADF_U8;
typedef        FS_USHORT       ADF_U16;
typedef        FS_ULONG        ADF_U32;
typedef        FS_FIXED        ADF_F32;
typedef        FS_FIXED        ADF_F64;
typedef        FS_BOOLEAN      ADF_Bool;
typedef        FS_VOID         ADF_Void;
/* end derived ADF types */

typedef ADF_I32         ADF_I1616;
typedef ADF_I32         ADF_R32;
typedef VOID	        ADF_FS_VOID;
typedef FS_BYTE         KSTf0_data;   /* Kern Sub-Table format 0 data */

/* inline keyword not supported in Sun environment */
#if defined(SUN)|| defined(SUN_ANSI) || defined(SUN_64)
#define FS_INLINE
#else
#define FS_INLINE       __inline
#endif

#endif	/* FS_EDGE_HINTS || FS_EDGE_RENDER */

/************************************************************************/
/*                      Data Types Definitions                          */
/************************************************************************/
/*
** The common problem of mapping various pointer types is partialy
** addressed here.
*/

/* Pointer Data Types Definitions */

typedef NZCOUNTER *  PNZCOUNTER;
typedef SB8 * LPSB8;
typedef UB8 * LPUB8;
typedef UW16 * LPUW16;
typedef SW16 * LPSW16;
typedef SL32 * LPSL32;
typedef UL32 * LPUL32;


/* customer code add */
/* Integer conversion from/to pointer support */
#if defined(_WIN64) || defined(_LP64)
/* 64-bit pointers */
typedef UL64 PTRSIZETYPE ULPtr; /* unsigned integer type the size of a pointer */
typedef SL64 PTRSIZETYPE SLPtr; /* signed integer type the size of a pointer */
#else
/* add EXPLICIT detection for other 64-bit environments here, as needed */
/* otherwise default to old rules (assuming ptrs are same size as longs) */
#if (NAT_ALIGN == 8)
/* 64-bit pointers */
typedef UL64 PTRSIZETYPE ULPtr; /* unsigned integer type the size of a pointer */
typedef SL64 PTRSIZETYPE SLPtr; /* signed integer type the size of a pointer */
#else
/* 32-bit pointers */
typedef UL32 PTRSIZETYPE ULPtr; /* unsigned integer type the size of a pointer */
typedef SL32 PTRSIZETYPE SLPtr; /* signed integer type the size of a pointer */
#endif	/* NAT_ALIGN */
#endif	/* _WIN64 */
/* end customer code add */

typedef CGFIXED * PCGFIXED;
typedef SB8 ** LPLPSB8;
typedef UB8 ** LPLPUB8;
typedef VOID * PVOID;
typedef PVOID * PPVOID;
typedef FPNUM * PFPNUM;


typedef struct word_point_struct {
   SW16 x;                      /* point x coordinate                   */
   SW16 y;                      /* point y coordinate                   */
} SW16POINT;

typedef struct word_vector_struct {
   SW16 x;                      /* vector x component                   */
   SW16 y;                      /* vector y component                   */
} SW16VECTOR;
typedef  SW16VECTOR * PSW16VECTOR;

typedef struct
{
    SW16VECTOR ll;
    SW16VECTOR ur;
} BOX;
typedef BOX * PBOX;

typedef struct
{
    SL32 x;
    SL32 y;
} SL32VECTOR;
typedef  SL32VECTOR * PSL32VECTOR;

typedef struct
{
    FPNUM x;
    FPNUM y;
} FPVECTOR;

typedef struct
{
    SL32VECTOR ll;
    SL32VECTOR ur;
} SL32BOX;

typedef struct word_window_struct {
   SW16 Top;                    /* top edge coordinate                  */
   SW16 Left;                   /* left edge coordinate                 */
   SW16 Bottom;                 /* bottom edge coordinate               */
   SW16 Right;                  /* right edge coordinate                */
} SW16WINDOW;

typedef struct
{
    CGFIXED x;
    CGFIXED y;
} CGPOINTFX;

typedef CGPOINTFX FS_FIXED_POINT;	/* 01-28-02 jfd */
typedef struct {
    FS_FIXED x,y;
    } FIXED_VECTOR;

typedef  SL32        INTR;
typedef  SL32VECTOR  INTRVECTOR;
typedef  SL32BOX     INTRBOX;

typedef  INTRVECTOR * PINTRVECTOR;

/************************************************************************/
/*                      Storage Class Definitions                       */
/************************************************************************/

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
!defined(_AM29K) && !defined (_WIN32_WCE)

#define MAYBE_MALLOC_H \
!defined(VXWORKS) && !defined(__i960)

#define MAYBE_IO_H \
(defined (UFST_MSDOS) || defined (__OS2__)) && !defined (_WIN32_WCE)

/* VXWORKS clause may need additional test */
#define MAYBE_UNISTD_H \
(defined(UFST_UNIX) && !defined(__i960) && !defined(_AM29K)) || defined(VXWORKS)

#define MAYBE_MEMORY_H \
!defined(__i960) && !defined(VXWORKS)

#define MAYBE_ERRNO_H \
	!defined (_WIN32_WCE)

#define MAYBE_SHARE_H \
	_MSC_VER >= 1400	/* Visual C++ 2005 */

#define SYS_PATH_USES_BACKSLASH \
defined (UFST_MSDOS) || defined (__OS2__) || defined (GCCx86)

/* USE_BINARY_READMODE test should match conditions in mixmodel.h */
#define USE_BINARY_READMODE \
defined (UFST_MSDOS) || defined (__OS2__) || defined (GCCx86) \
	|| defined(__i960)  || defined (_WIN32_WCE)

#endif	/* __PORT__	*/

