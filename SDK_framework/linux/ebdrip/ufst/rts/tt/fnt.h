/* $HopeName: GGEufst5!rts:tt:fnt.h(EBDSDK_P.1) $ */

/* 
 * Copyright (C) 2003 Agfa Monotype Corporation. All rights reserved.
 */
/* $Header: /hope/man5/hope.0/compound/10/GGEufst5/RCS/rts:tt:fnt.h,v 1.2.10.1.1.1 2013/12/19 11:24:04 rogerb Exp $ */
/* Log stripped */
/* $Date: 2013/12/19 11:24:04 $ */
/*
   File:    fnt.h



   Copyright: © 1987-1990 by Apple Computer, Inc., all rights reserved.



    Change History (most recent first):

    AGFA changes:

       24-Jan-00  slg	Remove unneeded function fnt_Init(), associated unused
						fnt_FractPoint, MAXANGLES, and GlobalGraphicState items
						"function", "anglePoint", and "angleDistance". 
       27-Apr-93  rs    Declare 'fnt_LocalGraphicStateType' to remove compiler
                        warnings.
       26-Oct-92  mby   Add fnt_ElementType.cPolR, for mirrored compound chars.
       28-Apr-92  mby   Conditionally compile function protos for LINT_ARGS.

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
	   <1.7>	  8/1/89	sjk		Added composites and encryption. Plus some enhancements…
	   <1.6>	 6/13/89	SJK		Comment
	   <1.5>	  6/2/89	CEL		16.16 scaling of metrics, minimum recommended ppem, point size 0
									bug, correct transformed integralized ppem behavior, pretty much
									so
	   <1.4>	 5/26/89	CEL		EASE messed up on “c” comments
	  <•1.3>	 5/26/89	CEL		Integrated the new Font Scaler 1.0 into Spline Fonts

	To Do:
*/
/*	rwb 4/24/90 Replaced dropoutControl with scanControlIn and scanControlOut in
		global graphics state. 
		<3+>	 3/20/90	mrr		Added support for IDEFs.  Made funcDefs long aligned
									by storing int16 length instead of int32 end.
*/


#ifndef __FNT__
#define __FNT__

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

typedef struct {
    SL32 start;		/* offset to first instruction */
	UW16 length;		/* number of bytes to execute <4> */
	UW16 pgmIndex;	/* index to appropriate preprogram for this func (0..1) */
} fnt_funcDef;

/* <4> pretty much the same as fnt_funcDef, with the addition of opCode */
typedef struct {
	SL32 start;
	UW16 length;
	UB8  pgmIndex;
	UB8  opCode;
} fnt_instrDef;

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
	
    SB8 init; 						/* executing preprogram ?? */
	UB8 pgmIndex;					/* which preprogram is current */
	LoopCount instrDefCount;		/* number of currently defined IDefs */

#ifdef AGFADEBUG
	sfnt_maxProfileTable*	maxp;
	UW16					cvtCount;
	UW16					glyphIndex;
	BOOLEAN					glyphProgram;
#endif

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

	jmp_buf	env;		/* always be at the end, since it is unknown size */

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


#endif  /* __FNT__ */

/* -------------------------------------------------------------------
                            END OF "fnt.h"
   ------------------------------------------------------------------- */

