
/* Copyright (C) 2008 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/* fscdefs.h */


/*
   File:    FSCdefs.h

   Copyright: 1988-1990 by Apple Computer, Inc., all rights reserved.


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
	   <1.5>	  8/1/89	sjk		Added composites and encryption. Plus some enhancements
	   <1.4>	 6/13/89	SJK		Comment
	   <1.3>	  6/2/89	CEL		16.16 scaling of metrics, minimum recommended ppem, point size 0
									bug, correct transformed integralized ppem behavior, pretty much
									so
	   <1.2>	 5/26/89	CEL		EASE messed up on c comments
	  <•1.1>	 5/26/89	CEL		Integrated the new Font Scaler 1.0 into Spline Fonts
	   <1.0>	 5/25/89	CEL		Integrated 1.0 Font scaler into Bass code for the first time…

	To Do:
*/

#ifndef __FSCDEFS__
#define __FSCDEFS__

/************************************************************************/

#if defined(SUN) || defined(__i960) || defined (VXWORKS)
#define pascal 
#endif

/************************************************************************/

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

#if STIK
#define NON_SQUARE_COMPOSITE_STIK(key, ttOutput, glyphDataFormat)	\
	(	((UW16)(PSWAPW(glyphDataFormat)) == STIK_FORMAT_AA ||	\
		 (UW16)(PSWAPW(glyphDataFormat)) == CCC_FORMAT_AA)	&&	\
		 (if_state.m[0] != if_state.m[3]) &&	\
		 (key->weGotComponents == 1 && key->num_levels == 0) && \
		 (!FC_ISGRAY(&if_state.fcCur)) )
#endif

#endif /*  __FSCDEFS__  */

/* -------------------------------------------------------------------
                            END OF "FSCdefs.h"
   ------------------------------------------------------------------- */

