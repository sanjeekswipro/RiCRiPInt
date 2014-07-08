/* $HopeName: GGEufst5!rts:tt:fscdefs.h(EBDSDK_P.1) $ */

/* 
 * Copyright (C) 2003 Agfa Monotype Corporation. All rights reserved.
 */
/* $Header: /hope/man5/hope.0/compound/10/GGEufst5/RCS/rts:tt:fscdefs.h,v 1.2.10.1.1.1 2013/12/19 11:24:03 rogerb Exp $ */
/* Log stripped */
/* $Date: 2013/12/19 11:24:03 $ */
/*
   File:    FSCdefs.h

 


   Copyright: © 1988-1990 by Apple Computer, Inc., all rights reserved.



    Change History (most recent first):

    AGFA changes:

	   09-Mar-98  slg	Don't use "long" dcls (incorrect if 64-bit platform)
       15-Jul-97  sbm   Removed compiler warnings related to TT_ROM_ACT.
       10-Jun-93  jfd   Changed all references to "char" to "SB8" for portability.

       18-May-93  mby   In Motorola byte order section, changed casts in
                        SHORTMUL/DIV macros to int32; changed LoopCount to int.

       17-May-93  mby   For HILO byte order, change defs of USHORTMUL,
                        SHORTMUL & SHORTDIV; cast arguments to 32 bits.
                        Change LoopCount typedef from int16 to int.

       08-Feb-93  jfd   VXWorks support.

       21-Dec-92  jfd   If INTEL960, do not define ANSI_DEFS.
       15-Dec-92  jfd   Disabled data typedefs for short, int, long, int8,
                        uint8. (Now defined in PORT.H)
       03-Dec-92  rs    Conditionally compile typedefs for int16, uint16,
                        int32 and uint32 based on INTLENGTH (as is done
                        in port.h) to resolve BORLAND compiler errros.
                        Always define ANSI_DEFS.
       25-Nov-92  mby   Use BYTEORDER == LOHI instead of #ifdef INTEL.
                        Remove #defines for INTEL.
       22-Nov-92  rs    Port to Macintosh - don't typedef Fixed, Fract, Boolean.
       16-Nov-92  mby   Undid change to SWAPW from 10/20. SWAPW has to be cast
                        unsigned short (imagine the result is 0x8001 and it
                        needs to be cast to 32 bits).
       21-Oct-92  mby   Removed GET_x...() macros. See "cgmacros.h"
       20-Oct-92  mby   Fixed bug in SWAPL; cleaned up SWAPW. SWAPX() should
                        always be cast!!
       16-Oct-92  jfd   No longer undefining INTEL if INTEL960.
       05-Oct-92  jfd   Removed define for PASCAL (it is now done in PORT.H)
       28-Sep-92  jfd   Changed conditional compile statement to read 
                        "#if defined(SUN)  || defined(MIPS) || defined(__i960)"
       20-Jul-92  mby   Added typedef for PMEM_HANDLE.
                        Ripped out code that duplicates port.h. port.h is
                        always included before fscdefs.h
       08-Jul-92  rs    Change 'GET_$...' to 'GET_x...', could not compile
                        with BCC.
       07-Jul-92  mby   Added GET_$WORD, GET_$LONG, GET_$BYTE_OFF,
                        GET_$WORD_OFF, & GET_$LONG_OFF macros. These are
                        CPU-independent and require no address alignment.
                        Will eventually replace SWAPW, SWAPL, etc. in the
                        TrueType core.
       01-Jul-92  ss    Changed SUN defines to UNIX to be more general -
                        except for the system specific test for INTEL.
       19-Jun-92  mby   In SWAPL macro make constants long
       27-Apr-92  mby   #define FIXMATH_ENTRY for fontmath.c routines.
                        remove NOT_ON_MOTOROLA
       10-Apr-92  mby   ReleaseSFNTFunc and GetSFNTFunc data types use
                          MEM_HANDLE.
                        Move transMatrix and vectorType typedefs from
                          fontscal.h
                        #define UFST_SUBSYSTEM
       13-Mar-92  ss    Conditionally compile function prototypes for LINT_ARGS.

       02-Oct-91  mby   Add #defines for Intel CPU and ~MAC.

- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

		 <3+>	7/11/91		CKL		Added standard header conditionals
		 <3>	11/27/90	MR		Add #define for PASCAL. [ph]
		 <2>	 11/5/90	MR		Move USHORTMUL from FontMath.h, add Debug definition [rb]
		 <7>	 7/18/90	MR		Add byte swapping macros for INTEL, moved rounding macros from
									fnt.h to here
		 <6>	 7/14/90	MR		changed defines to typedefs for int[8,16,32] and others
		 <5>	 7/13/90	MR		Declared ReleaseSFNTFunc and GetSFNTFunc
		 <4>	  5/3/90	RB		cant remember any changes
		 <3>	 3/20/90	CL		type changes for Microsoft
		 <2>	 2/27/90	CL		getting bbs headers
	   <3.0>	 8/28/89	sjk		Cleanup and one transformation bugfix
	   <2.2>	 8/14/89	sjk		1 point contours now OK
	   <2.1>	  8/8/89	sjk		Improved encryption handling
	   <2.0>	  8/2/89	sjk		Just fixed EASE comment
	   <1.5>	  8/1/89	sjk		Added composites and encryption. Plus some enhancements…
	   <1.4>	 6/13/89	SJK		Comment
	   <1.3>	  6/2/89	CEL		16.16 scaling of metrics, minimum recommended ppem, point size 0
									bug, correct transformed integralized ppem behavior, pretty much
									so
	   <1.2>	 5/26/89	CEL		EASE messed up on “c” comments
	  <•1.1>	 5/26/89	CEL		Integrated the new Font Scaler 1.0 into Spline Fonts
	   <1.0>	 5/25/89	CEL		Integrated 1.0 Font scaler into Bass code for the first time…

	To Do:
*/

#ifndef __FSCDEFS__
#define __FSCDEFS__

/************************************************************************/
#if defined(SUN)  || defined(MIPS)
#define   pascal
#endif /* SUN || MIPS */

#if defined(__i960) || defined (VXWORKS)
#define pascal 
#endif /* __i960 */

/************************************************************************/

#define true 1
#define false 0

#define ONEFIX 		( 1L << 16 )
#define ONEFRAC 	( 1L << 30 )
#define MAKEABS(x)	( (x) < 0 ? (-(x)) : (x) )

typedef SW16 FWord;
typedef UW16 uFWord;

typedef SL32 F26Dot6;

typedef SL32 Fixed;
typedef SL32 Fract;

typedef struct {
    Fixed        transform[3][3];
} transMatrix;

typedef struct {
    Fixed        x, y;
} vectorType;

typedef VOID    (*voidFunc) ();

typedef SL32	LoopCount;
typedef SL32	ArrayIndex;

#define USHORTMUL(a, b) ((UL32)((UL32)(a) * (UL32)(b)))
#define SHORTMUL(a,b)	(SL32)((SL32)(a) * (SL32)(b))
#define SHORTDIV(a,b)	(SL32)((SL32)(a) / (SL32)(b))

/* d is half of the denumerator */
#define FROUND( x, n, d, s ) \
	    x = SHORTMUL(x, n); x += d; x >>= s;

/* <3> */
#define SROUND( x, n, d, halfd ) \
    if ( x < 0 ) { \
	    x = -x; x = SHORTMUL(x, n); x += halfd; x /= d; x = -x; \
	} else { \
	    x = SHORTMUL(x, n); x += halfd; x /= d; \
	}

#endif /*  __FSCDEFS__  */

/* -------------------------------------------------------------------
                            END OF "FSCdefs.h"
   ------------------------------------------------------------------- */

