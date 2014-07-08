
/* Copyright (C) 2008 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/* fnt.h */


/*
   File:    fnt.h

   Copyright: 1987-1990 by Apple Computer, Inc., all rights reserved.


- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

		 <6+>	7/11/91		CKL		Added standard header conditionals
		 <6>	 12/5/90	MR		Remove unneeded leftSideBearing[in,out] and advanceWidth[in,out]
									fields. [rb]
		 <5>	11/27/90	MR		Better typedefs for function pointers [rb]
		 <4>	11/16/90	MR		More debugging code [rb]
		 <3>	 11/5/90	MR		Change globalGS.ppemDot6 to globalGS.fpem. InstrPtrs and
									curvePtr are now all uint8*. [rb]
		 <2>	10/20/90	MR		Restore changes since project died.  Converting to 2.14 vectors,
									smart math routines. [rb]
		<0+>	10/19/90	MR		Restore changes since project died.  Converting to 2.14 vectors,
									smart math routines.
		<10>	 7/26/90	MR		rearrange local graphic state, remove unused parBlockPtr
		 <9>	 7/18/90	MR		change loop variable from long to short, and other Ansi-changes
		 <8>	 7/13/90	MR		Prototypes for function pointers
		 <5>	  6/4/90	MR		Remove MVT
		 <4>	  5/3/90	RB		replaced dropoutcontrol with scancontrolin and scancontrol out
									in global graphics state
		 <3>	 3/20/90	CL		fields for multiple preprograms fields for ppemDot6 and
									pointSizeDot6 changed SROUND to take D/2 as argument
		 <2>	 2/27/90	CL		Added DSPVTL[] instruction.  Dropoutcontrol scanconverter and
									SCANCTRL[] instruction
	   <3.1>	11/14/89	CEL		Fixed two small bugs/feature in RTHG, and RUTG. Added SROUND &
									S45ROUND.
	   <3.0>	 8/28/89	sjk		Cleanup and one transformation bugfix
	   <2.2>	 8/14/89	sjk		1 point contours now OK
	   <2.1>	  8/8/89	sjk		Improved encryption handling
	   <2.0>	  8/2/89	sjk		Just fixed EASE comment
	   <1.7>	  8/1/89	sjk		Added composites and encryption. Plus some enhancements
	   <1.6>	 6/13/89	SJK		Comment
	   <1.5>	  6/2/89	CEL		16.16 scaling of metrics, minimum recommended ppem, point size 0
									bug, correct transformed integralized ppem behavior, pretty much
									so
	   <1.4>	 5/26/89	CEL		EASE messed up on c comments
	  <¥1.3>	 5/26/89	CEL		Integrated the new Font Scaler 1.0 into Spline Fonts

	To Do:
*/
/*	rwb 4/24/90 Replaced dropoutControl with scanControlIn and scanControlOut in
		global graphics state. 
		<3+>	 3/20/90	mrr		Added support for IDEFs.  Made funcDefs long aligned
									by storing int16 length instead of int32 end.
*/


#ifndef __FNT__
#define __FNT__

#if FS_EDGE_HINTS
#include "adftypesystem.h"
#include "adffixedmath.h"
#include "adfinittermsystem.h"
#include "adfgenerate.h"
#include "adfimplicit.h"
#include "adfalgnzonesmaz.h"
#endif	/* FS_EDGE_HINTS */

#define fnt_pixelSize 0x40L
#define fnt_pixelShift 6

#define MAXBYTE_INSTRUCTIONS 256

#define VECTORTYPE					ShortFrac
#define ONEVECTOR					ONESHORTFRAC
#define VECTORMUL(value, component)	ShortFracMul(value, component)
#define VECTORDOT(a,b)				ShortFracDot(a,b)
#define VECTORMULDIV(a,b,c)			ShortMulDiv(a,b,c)
#define ONESIXTEENTHVECTOR			((ONEVECTOR) >> 4)

typedef struct VECTOR {
	VECTORTYPE x;
	VECTORTYPE y;
} VECTOR;

typedef struct {
    F26Dot6  *x; /* The Points the Interpreter modifies */
    F26Dot6  *y; /* The Points the Interpreter modifies */
    F26Dot6  *ox; /* Old Points */
    F26Dot6  *oy; /* Old Points */
    F26Dot6  *oox; /* Old Unscaled Points, really ints */
    F26Dot6  *ooy; /* Old Unscaled Points, really ints */
	UB8 *onCurve; /* indicates if a point is on or off the curve */
	SW16 *sp;  /* Start points */
	SW16 *ep;  /* End points */
    UB8 *cPolR;   /* Contour polarity reverse, 1 byte per contour */
	UB8 *f;  /* Internal flags, one byte for every point */
	SW16 nc;  /* Number of contours */
	SW16 *sc;  /*added 7/23/04 bjg */
} fnt_ElementType;

struct fnt_LocalGraphicStateType; /* declare to remove compiler warnings */

#ifdef LINT_ARGS

typedef VOID (*FntFunc)(struct fnt_LocalGraphicStateType*);
typedef VOID (*FntMoveFunc)(struct fnt_LocalGraphicStateType*, fnt_ElementType*, ArrayIndex, F26Dot6);
typedef F26Dot6 (*FntRoundFunc)(F26Dot6 xin, F26Dot6 engine, struct fnt_LocalGraphicStateType* gs);
typedef F26Dot6 (*FntProjFunc)(struct fnt_LocalGraphicStateType*, F26Dot6, F26Dot6);

#else  /* !LINT_ARGS */

typedef VOID (*FntFunc)();
typedef VOID (*FntMoveFunc)();
typedef F26Dot6 (*FntRoundFunc)();
typedef F26Dot6 (*FntProjFunc)();

#endif /*  LINT_ARGS */



typedef struct {

/* PARAMETERS CHANGEABLE BY TT INSTRUCTIONS */
    F26Dot6 wTCI;     				/* width table cut in */
	F26Dot6 sWCI;     				/* single width cut in */
	F26Dot6 scaledSW; 				/* scaled single width */
	SL32 scanControl;				/* controls kind and when of dropout control */
	SL32 instructControl;			/* controls gridfitting and default setting */
	
	F26Dot6 minimumDistance;		/* moved from local gs  7/1/90  */
	FntRoundFunc RoundValue;		/*								*/
	F26Dot6 	periodMask; 			/* ~(gs->period-1) 				*/
	Fract	period45;				/*								*/
	SW16	period;					/* for power of 2 periods 		*/
	SW16 	phase;					/*								*/
	SW16 	threshold;				/* moved from local gs  7/1/90  */
	
	SW16 deltaBase;
	SW16 deltaShift;
	SW16 angleWeight;
	SW16 sW;         				/* single width, expressed in the same units as the character */
	SB8 autoFlip;   				/* The auto flip Boolean */
	SB8 pad;	
} fnt_ParameterBlock;				/* this is exported to client */

#define ROTATEDGLYPH	0x100
#define STRETCHEDGLYPH  0x200
#define NOGRIDFITFLAG	1
#define DEFAULTFLAG		2

typedef enum {
	PREPROGRAM,
	FONTPROGRAM,
	MAXPREPROGRAMS
} fnt_ProgramIndex;

typedef struct {
    FS_FIXED N;
    FS_FIXED D;
    FS_FIXED scl;
    int shift;
    } scale_rec;

/* typedef F26DOT6 (*ScaleFunc)(scale_rec*, F26DOT6); */
enum ScaleFuncIndex {
    SFI_fnt_FRound,
    SFI_fnt_SRound,
    SFI_fnt_FixRound
};
typedef enum ScaleFuncIndex ScaleFunc;
/************* scaled font enviromnent **************************/
typedef struct {
	FS_USHORT lpm;			/* rotation invarient measure of character size */
#if STIK
	FS_FIXED stroke_pct;
#endif
	} SENV;

typedef struct fnt_GlobalGraphicStateType {
	F26Dot6* stackBase; 			/* the stack area */
	F26Dot6* store; 				/* the storage area */
	F26Dot6* controlValueTable; 	/* the control value table */
	
	UW16	pixelsPerEm; 			/* number of pixels per em as an integer */
	UW16	pointSize; 				/* the requested point size as an integer */
	Fixed	fpem;					/* fractional pixels per em    <3> */
	F26Dot6 engine[4]; 				/* Engine Characteristics */
	
	fnt_ParameterBlock defaultParBlock;	/* variables settable by TT instructions */
	fnt_ParameterBlock localParBlock;

	/* Only the above is exported to Client throught FontScaler.h */

/* VARIABLES NOT DIRECTLY MANIPULABLE BY TT INSTRUCTIONS  */
	
	fnt_funcDef* funcDef; 			/* function Definitions identifiers */
	fnt_instrDef* instrDef;			/* instruction Definitions identifiers */
#ifdef LINT_ARGS
    F26Dot6	(*ScaleFunc)(struct fnt_GlobalGraphicStateType*, F26Dot6); 			/* Call back function to do scaling */
#else /* !LINT_ARGS */
    F26Dot6	(*ScaleFunc)();
#endif /* LINT_ARGS */
	UB8* pgmList[MAXPREPROGRAMS];	/* each program ptr is in here */

    /* These are parameters used by the call back function */
	Fixed fixedScale; 			/* fixed sc aling factor */
	SL32  nScale; 				/* numerator required to scale points to the right size*/
	SL32  dScale; 				/* denumerator required to scale points to the right size */
	SW16  shift; 				/* 2log of dScale */
	SB8   identityTransformation; 	/* true/false  (does not mean identity from a global sense) */
	SB8   non90DegreeTransformation; /* bit 0 is 1 if non-90 degree, bit 1 is 1 if x scale doesn't equal y scale */
	Fixed  xStretch; 			/* Tweaking for glyphs under transformational stress <4> */
	Fixed  yStretch;
	scale_rec			scaleX;
	scale_rec			scaleY;
    ScaleFunc           scaleFuncX;
    ScaleFunc           scaleFuncY;
    scale_rec           scaleCVT;
    ScaleFunc           scaleFuncCVT;
	
    SB8 init; 						/* executing preprogram ?? */
	UB8 pgmIndex;					/* which preprogram is current */
	LoopCount instrDefCount;		/* number of currently defined IDefs */

	sfnt_maxProfileTable*	maxp;
	UW16					cvtCount;
	BOOLEAN					glyphProgram;

	FS_FIXED			metricScalarX;
	FS_FIXED			interpScalarX;
	FS_FIXED			metricScalarY;
	FS_FIXED			interpScalarY;
	FS_FIXED			cvtStretchX;
	FS_FIXED			cvtStretchY;
	BOOLEAN				tterrorcheck;	/* enables/disables extended TT error checking */
	UW16 fpgm_maxFunctionDefs;			/* "maxFunctionDefs" derived from "fpgm" table */
	FS_ULONG x_stroke_width;
	FS_ULONG y_stroke_width;
	FS_ULONG fontflags;
	FS_ULONG stateflags;
	SENV senv;
	
#if FS_EDGE_HINTS
    MAZ_DATA            maz_data;
	BOOLEAN             use_edge_tech;	/* copy of B_USE_EDGE_TECH() */
#endif
	BOOLEAN				non_square_edge;	/* copy of "(FC_ISGRAY() && B_USE_EDGE_TECH() && (m[0] != m[3]))" */
} fnt_GlobalGraphicStateType;

#ifdef LINT_ARGS
typedef F26Dot6 (*GlobalGSScaleFunc)(fnt_GlobalGraphicStateType*, F26Dot6);

#else /* !LINT_ARGS */

typedef F26Dot6 (*GlobalGSScaleFunc)();
#endif /* LINT_ARGS */

/* 
 * This is the local graphics state  
 */
typedef struct fnt_LocalGraphicStateType {
	fnt_ElementType *CE0, *CE1, *CE2; 	/* The character element pointers */
	VECTOR proj; 						/* Projection Vector */
	VECTOR free;						/* Freedom Vector */
	VECTOR oldProj; 					/* Old Projection Vector */
	F26Dot6 *stackPointer;

	UB8 *insPtr; 						/* Pointer to the instruction we are about to execute */
    fnt_ElementType *elements;
    fnt_GlobalGraphicStateType *globalGS;

    ArrayIndex Pt0, Pt1, Pt2; 			/* The internal reference points */
	SW16   roundToGrid;			
	LoopCount loop; 					/* The loop variable */	
	UB8 opCode; 						/* The instruction we are executing */

	/* Above is exported to client in FontScaler.h */

	VECTORTYPE pfProj; /* = pvx * fvx + pvy * fvy */

	FntMoveFunc MovePoint;
	FntProjFunc Project;
	FntProjFunc OldProject;
#ifdef LINT_ARGS
	F26Dot6 (*GetCVTEntry) (struct fnt_LocalGraphicStateType *gs, ArrayIndex n);
	F26Dot6 (*GetSingleWidth) (struct fnt_LocalGraphicStateType *gs);
#else /* !LINT_ARGS */
    F26Dot6 (*GetCVTEntry) ();
    F26Dot6 (*GetSingleWidth) ();
#endif /* LINT_ARGS */

	UB8 *end_of_insPtr; 				/* Pointer to final instruction we are to execute */
	SW16 error;

} fnt_LocalGraphicStateType;

/*
 * Executes the font instructions.
 * This is the external interface to the interpreter.
 *
 * Parameter Description
 *
 * elements points to the character elements. Element 0 is always
 * reserved and not used by the actual character.
 *
 * ptr points at the first instruction.
 * eptr points to right after the last instruction
 *
 * globalGS points at the global graphics state
 *
 * Note: The stuff globalGS is pointing at must remain intact
 *       between calls to this function.
 */

#ifdef LINT_ARGS
EXTERN SL32 fnt_Execute(fnt_ElementType *elements, UB8 *ptr, UB8 *eptr, 
							fnt_GlobalGraphicStateType *globalGS);
/* Export internal rounding routines so globalGraphicsState->defaultParBlock.RoundValue
 * can be set in fsglue.c
 */
EXTERN F26Dot6 fnt_RoundToDoubleGrid(F26Dot6 xin, F26Dot6 engine, fnt_LocalGraphicStateType *gs);  
EXTERN F26Dot6 fnt_RoundDownToGrid(F26Dot6 xin, F26Dot6 engine, fnt_LocalGraphicStateType *gs);  
EXTERN F26Dot6 fnt_RoundUpToGrid(F26Dot6 xin, F26Dot6 engine, fnt_LocalGraphicStateType *gs);  
EXTERN F26Dot6 fnt_RoundToGrid(F26Dot6 xin, F26Dot6 engine, fnt_LocalGraphicStateType *gs);  
EXTERN F26Dot6 fnt_RoundToHalfGrid(F26Dot6 xin, F26Dot6 engine, fnt_LocalGraphicStateType *gs); 
EXTERN F26Dot6 fnt_RoundOff(F26Dot6 xin, F26Dot6 engine, fnt_LocalGraphicStateType *gs);  
EXTERN F26Dot6 fnt_SuperRound(F26Dot6 xin, F26Dot6 engine, fnt_LocalGraphicStateType *gs);  
EXTERN F26Dot6 fnt_Super45Round(F26Dot6 xin, F26Dot6 engine, fnt_LocalGraphicStateType *gs); 

#else /* !LINT_ARGS */
EXTERN SL32 fnt_Execute();
EXTERN F26Dot6 fnt_RoundToDoubleGrid();
EXTERN F26Dot6 fnt_RoundDownToGrid();
EXTERN F26Dot6 fnt_RoundUpToGrid();
EXTERN F26Dot6 fnt_RoundToGrid();
EXTERN F26Dot6 fnt_RoundToHalfGrid();
EXTERN F26Dot6 fnt_RoundOff();
EXTERN F26Dot6 fnt_SuperRound();
EXTERN F26Dot6 fnt_Super45Round();
#endif /* LINT_ARGS */


/*** here are the typedef redefinitions required for the chunk of code taken from iType's fnt.c and autohint.c ***/

#include <stdio.h>

/* this sets the "fontflags" field */
#define FONTFLAG_STIK		(FS_ULONG)0x00000002

/* this sets the "stateflags" field */
#define FLAGS_NO_HINTS		(FS_ULONG)0x00000002

#define MAX_HINTS 32
#define F26DOT6_FLOOR(x) ((x)&~63) 
#define F26DOT6_ROUND(x)  F26DOT6_FLOOR(32+(x)) 
#define LEFTSIDEBEARING 0
#define RIGHTSIDEBEARING 1
#define IS_OUTLINE_CHAR 0x80
#define MYINFINITY  2147483647L
#define NS_CAP_ROUND	0
#define NS_CAP_SQUARE	1
#define NS_X_SQUARE		2
#define NS_X_ROUND		3
#define NS_BASE_SQUARE	4
#define NS_BASE_ROUND	5
#define NS_O_LSB		6
#define NS_O_RSB		7
#define LC_ACCENT		8 /* acute 'o' */
#define UC_ACCENT		9 /* acute 'o' */		
#define NS_CVT_USED		12	/* this is the number of indices used */

#define ARABIC_UPPER_ACCENT_BOT            0
#define ARABIC_UPPER_ACCENT_TOP            1
#define ARABIC_LOWER_MEDIUM_ACCENT_BOT     2
#define ARABIC_UPPER_MEDIUM_ACCENT_BOT     3
#define ARABIC_UPPER_NUMERAL_TOP           4
#define ARABIC_UPPER_NUMERAL_BOT           5
#define ARABIC_LOWER_NUMERAL_TOP           6
#define ARABIC_LOWER_NUMERAL_BOT           7
#define ARABIC_UPPER_HIGH_TOP              8
#define ARABIC_UPPER_HIGH_BOT              9
#define ARABIC_LOWER_HIGH_BOT              10
#define ARABIC_LOWER_MEDIUM_HIGH_BOT       11
#define ARABIC_LOWER_MEDIUM_SHORT_BOT      12
#define ARABIC_LOWER_SHORT_HIGH_BOT        13
#define ARABIC_LOWER_SHORT_SHORT_BOT       14
#define ARABIC_CENTER_HIGH_TOP             15
#define ARABIC_CENTER_SHORT_TOP            16
#define ARABIC_UPPER_HIGH_1_DOT_TOP        17
#define ARABIC_UPPER_HIGH_2_DOT_TOP        18
#define ARABIC_UPPER_HIGH_3_DOT_TOP        19
#define ARABIC_UPPER_SHORT_1_DOT_TOP       20
#define ARABIC_UPPER_SHORT_2_DOT_TOP       21
#define ARABIC_UPPER_SHORT_3_DOT_TOP       22
#define ARABIC_SUPERSCRIPT_DAMMA_TOP       23
#define ARABIC_SUPERSCRIPT_DAMMA_BOT       24
#define ARABIC_SUPERSCRIPT_FATHA_TOP       25
#define ARABIC_SUPERSCRIPT_FATHA_BOT       26
#define ARABIC_SUBSCRIPT_KASRA_TOP         27
#define ARABIC_SUBSCRIPT_KASRA_BOT         28
#define ARABIC_SUPERSCRIPT_SHADDA_TOP      29
#define ARABIC_SUPERSCRIPT_SHADDA_BOT      30
#define ARABIC_SUPERSCRIPT_SUKUN_BOT       31
#define ARABIC_SUPERSCRIPT_SUKUN_TOP       32
#define ARABIC_ACCENT_SHADDA_KASRA_TOP     33
#define ARABIC_ACCENT_SHADDA_KASRA_BOT     34
#define ARABIC_ACCENT_SHADDA_FATHA_TOP     35
#define ARABIC_ACCENT_SHADDA_FATHA_BOT     36
#define ARABIC_SUPERSCRIPT_ALEF_TOP        37
#define ARABIC_SUPERSCRIPT_ALEF_BOT        38
#define ARABIC_SUBSCRIPT_ALEF_TOP          39
#define ARABIC_SUBSCRIPT_ALEF_BOT          40
#define ARABIC_UPPER_ACCENT_STEM_TOP       41
#define ARABIC_CVT_USED 42 /* number of indices used for ARABIC autohinting */

/* default stroke percent for STIK fonts as fixed number (changed from 3% to 5%) */
#define DEFAULT_STROKE_PCT		3277 /* 5% */
/*#define DEFAULT_STROKE_PCT	1966 */ /* 3% in 16.16 */

#if FS_EDGE_HINTS
#define FLAGS_MAZ_ON          (FS_ULONG)0x00100000
#define FLAGS_MAZ_OFF         (FS_ULONG)(~FLAGS_MAZ_ON)
#define ADF_GRID_FIT_NONE         0    /* No grid fitting */
#define ADF_GRID_FIT_PIXEL        1    /* Grid fit to the pixel grid (not used) */
#define ADF_GRID_FIT_MAZ_PIXEL    3    /* Grid fit to the pixel grid using MAZ */
#define ADF_GRID_FIT_BAZ_PIXEL    4    /* Grid fit to the pixel grid using BAZ */
#define APPLYMAZDELTASBEFORE 0
#define APPLYMAZDELTASAFTER  4
#define MAZMODEMASK 3
#endif

#endif  /* __FNT__ */

/* -------------------------------------------------------------------
                            END OF "fnt.h"
   ------------------------------------------------------------------- */

