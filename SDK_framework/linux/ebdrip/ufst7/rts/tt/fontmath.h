
/* Copyright (C) 2008 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/* fontmath.h */


/*
   File:    FontMath.h

    Copyright: 1990 by Apple Computer, Inc., all rights reserved.


    Change History (most recent first):

	   12-Jun-01  slg	Datatype changes (replace C datatypes with standard 
						UFST datatypes, using "INTG" (native int) for "int")
	   08-Dec-00  slg	Nearly complete replacement of fixed-point routines - 
							taken from "iType"'s fixed.h 
	   09-Mar-98  slg	Don't use "long" dcls (incorrect if 64-bit platform)
       06-Aug-93  mby   Change NEGINFINITY def to 80000001, from 80000000
       28-Nov-92  rs    Conditionalize on _H_MacHeaders.
       22-Nov-92  rs    Port to Macintosh - conditionally remove FixRatio, FixMul.
       27-Apr-92  mby   Conditionally compile function defs with LINT_ARGS.

- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

		 <4+>	7/11/91		CKL		Added standard header conditionals
		 <4>	11/27/90	MR		make pascal declaration a macro, conditionalize traps -vs-
									externs for Fix/Frac math routines. [ph]
		 <3>	 11/5/90	MR		Move [U]SHORTMUL into FSCdefs.h Rename FixMulDiv to LongMulDiv.
									[rb]
		 <2>	10/20/90	MR		Add some new math routines (stolen from skia). [rj]
		 <1>	 4/11/90	dba		first checked in

	To Do:
*/

#ifndef __FONTMATH__
#define __FONTMATH__

/************* OLD section *************************/
#define FIXONEHALF			0x00008000
#define ONESHORTFRAC		(1 << 14)

#define FIXROUND( x )		(SW16)(((x) + FIXONEHALF) >> 16)

typedef short ShortFrac;			/* 2.14 */
/************* end OLD section *************************/

#ifndef LO_WORD
#define HI_WORD(x) ((UL32)(x) >> 16)
#define LO_WORD(x) ((x) & 0xFFFF)
#endif

#define FIXED_ONE (1L << 16)
#define FRACT_ONE (1L << 30)
#define SHORTFRACT_ONE (1 << 14)

#define FIXED_ONEHALF (1L << 15)
#define FRACT_ONEHALF (1L << 29)
#define SHORTFRACT_ONEHALF (1 << 13)

/* smallest half integer >= y */
#define ABOVE(y) (((y + FIXED_ONEHALF - 1) & (-FIXED_ONE)) + FIXED_ONEHALF)

/* largest half integer < y */
#define BELOW(y) (((y - FIXED_ONEHALF - 1) & (-FIXED_ONE)) + FIXED_ONEHALF)

#define TRUNC(x)	(SW16)((x)>>16)
#define ROUND(x)	TRUNC((x)+FIXED_ONEHALF)

#ifdef FS_EDGE_TECH
#define FS_FLOOR(x)    (FS_SHORT)((x)>>16)
#define FS_ROUND(val)  FS_FLOOR((val)+FIXED_ONEHALF)
#define FIXEDTODOT6(n)  (((n) + (1L << 9)) >> 10)
#endif	/* defined(FS_EDGE_TECH) */

/* sqrt of 2.30 number */
FRACT FracSqrt(FRACT x);				
#define ShortFracDot(a,b) (ShortFrac)(((SL32)a*b)>>14)

/* select the proper variant of MULDIV, VARMUL, and VARDIV */

#if (SIZEOF_LONG == 8)
/* first: native 64 bit ints */
#define VarMul(a,b,c) varmul_64(a,b,c)
#define VarDiv(a,b,c) vardiv_64(a,b,c)
#define FixMul(a,b) (FS_FIXED)varmul_64(a,b,16)
#define FixDiv(a,b) (FS_FIXED)vardiv_64(a,b,16)
#define Mul26Dot6(a,b) (F26DOT6)varmul_64(a,b,6)
#define Div26Dot6(a,b) (F26DOT6)vardiv_64(a,b,6)
#define LongMulDiv(a,b,c) muldiv_64(a,b,c)
#define ShortMulDiv(a,b,c) muldiv_64(a,(SL32)b,(SL32)c)
#define FracMul(a,b) (FRACT)varmul_64(a,b,30)
#define FracDiv(a,b) (FRACT)vardiv_64(a,b,30)
#define ShortFracMul(a,b) (F26DOT6)varmul_64(a,(SL32)b,14)

#elif defined(HAS_ASM)
/* second: user supplied assembly code */
#define VarMul(a,b,c) varmul_asm(a,b,c)
#define VarDiv(a,b,c) vardiv_asm(a,b,c)
#define FixMul(a,b) (FS_FIXED)varmul_asm(a,b,16)
#define FixDiv(a,b) (FS_FIXED)vardiv_asm(a,b,16)
#define Mul26Dot6(a,b) (F26DOT6)varmul_asm(a,b,6)
#define Div26Dot6(a,b) (F26DOT6)vardiv_asm(a,b,6)
#define LongMulDiv(a,b,c) muldiv_asm(a,b,c)
#define ShortMulDiv(a,b,c) muldiv_asm(a,(SL32)b,(SL32)c)
#define FracMul(a,b) (FRACT)varmul_asm(a,b,30)
#define FracDiv(a,b) (FRACT)vardiv_asm(a,b,30)
#define ShortFracMul(a,b) (F26DOT6)varmul_asm(a,(SL32)b,14)

#elif defined(HAS_FS_INT64)
/* third: compiler supported 64 bit integers */
#define VarMul(a,b,c) varmul_64(a,b,c)
#define VarDiv(a,b,c) vardiv_64(a,b,c)
#define FixMul(a,b) (FS_FIXED)varmul_64(a,b,16)
#define FixDiv(a,b) (FS_FIXED)vardiv_64(a,b,16)
#define Mul26Dot6(a,b) (F26DOT6)varmul_64(a,b,6)
#define Div26Dot6(a,b) (F26DOT6)vardiv_64(a,b,6)
#define LongMulDiv(a,b,c) muldiv_64(a,b,c)
#define ShortMulDiv(a,b,c) muldiv_64(a,(SL32)b,(SL32)c)
#define FracMul(a,b) (FRACT)varmul_64(a,b,30)
#define FracDiv(a,b) (FRACT)vardiv_64(a,b,30)
#define ShortFracMul(a,b) (F26DOT6)varmul_64(a,(SL32)b,14)

#else
/* otherwise 32 bit ANSI C */
#define VarMul(a,b,c) varmul(a,b,c)
#define VarDiv(a,b,c) vardiv(a,b,c)
#define FixMul(a,b) (FS_FIXED)varmul(a,b,16)
#define FixDiv(a,b) (FS_FIXED)vardiv(a,b,16)
#define Mul26Dot6(a,b) (F26DOT6)varmul(a,b,6)
#define Div26Dot6(a,b) (F26DOT6)vardiv(a,b,6)
#define LongMulDiv(a,b,c) muldiv(a,b,c)
#define ShortMulDiv(a,b,c) muldiv(a,(SL32)b,(SL32)c)
#define FracMul(a,b) (FRACT)varmul(a,b,30)
#define FracDiv(a,b) (FRACT)vardiv(a,b,30)
#define ShortFracMul(a,b) (F26DOT6)varmul(a,(SL32)b,14)
#endif /* SIZEOF_LONG */

#endif  /* __FONTMATH__ */

/* -------------------------------------------------------------------
                            END OF "FontMath.h"
   ------------------------------------------------------------------- */

