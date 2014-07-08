
/* Copyright (C) 2008 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/* fsglue.h */


/*
   File:    FSglue.h

   Copyright: © 1988-1990 by Apple Computer, Inc., all rights reserved.


- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

		 <8+>	7/11/91		CKL		Added standard header conditionals
		 <8>	12/11/90	MR		Add use-my-metrics support for devMetrics in component glyphs.
									[rb]
		 <7>	 12/5/90	MR		Remove unneeded leftSideBearing and advanceWidth fields. [rb]
		 <6>	 12/5/90	RB		Change reverseContours to unused1, since we don't use it, since
									the scan converter now uses non-zero winding number fill. [mr]
		 <5>	11/27/90	MR		Need two scalars: one for (possibly rounded) outlines and cvt,
									and one (always fractional) metrics. [rb]
		 <4>	 11/5/90	MR		Add Release macro
		 <3>	10/31/90	MR		Add fontFlags field to key (copy of header.flag) [rb]
		 <2>	10/20/90	MR		Change to new scaling routines/parameters, removed scaleFunc
									from key. [rb]
		<12>	 7/18/90	MR		Change error return type to int
		<11>	 7/13/90	MR		Declared function pointer prototypes, Debug fields for runtime
									range checking
		 <8>	 6/21/90	MR		Add field for ReleaseSfntFrag
		 <7>	  6/5/90	MR		remove vectorMappingF
		 <6>	  6/4/90	MR		Remove MVT
		 <5>	  6/1/90	MR		Thus endeth the too-brief life of the MVT...
		 <4>	  5/3/90	RB		adding support for new scan converter and decryption.
		 <3>	 3/20/90	CL		Added function pointer for vector mapping
		 							Removed devRes field
									Added fpem field
		 <2>	 2/27/90	CL		Change: The scaler handles both the old and new format
									simultaneously! It reconfigures itself during runtime !  Changed
									transformed width calculation.  Fixed transformed component bug.
	   <3.1>	11/14/89	CEL		Left Side Bearing should work right for any transformation. The
									phantom points are in, even for components in a composite glyph.
									They should also work for transformations. Device metric are
									passed out in the output data structure. This should also work
									with transformations. Another leftsidebearing along the advance
									width vector is also passed out. whatever the metrics are for
									the component at it's level. Instructions are legal in
									components. Now it is legal to pass in zero as the address of
									memory when a piece of the sfnt is requested by the scaler. If
									this happens the scaler will simply exit with an error code!
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
	  <¥1.1>	 5/26/89	CEL		Integrated the new Font Scaler 1.0 into Spline Fonts
	   <1.0>	 5/25/89	CEL		Integrated 1.0 Font scaler into Bass code for the first time

	To Do:
*/
/*		<3+>	 3/20/90	mrr		Added flag executeFontPgm, set in fs_NewSFNT
*/

#ifndef __FSGLUE__
#define __FSGLUE__

#define POINTSPERINCH				72
#define MAX_ELEMENTS				2
#define MAX_TWILIGHT_CONTOURS		1

#define TWILIGHTZONE 0 /* The point storage */
#define GLYPHELEMENT 1 /* The actual glyph */



/* use the lower ones for public phantom points */
/* public phantom points start here */
#define LEFTSIDEBEARING 0
#define RIGHTSIDEBEARING 1
/* private phantom points start here */
#define ORIGINPOINT 2
#define LEFTEDGEPOINT 3
/* total number of phantom points */
#define PHANTOMCOUNT 4


typedef struct {
	Fixed x;
	Fixed y;
} point_coords;

/*** Offset table ***/
typedef struct {
	SL32			interpreterFlagsOffset;
	SL32			startComponentOffset;	/*added 7/23/04 bjg */
	SL32			startPointOffset;
	SL32			endPointOffset;
	SL32			oldXOffset;
	SL32			oldYOffset;
	SL32			scaledXOffset;
	SL32			scaledYOffset;
	SL32			newXOffset;
	SL32			newYOffset;
	SL32			onCurveOffset;
    SL32           contourPolarityOffset;      /* mby, 10/26/92 */
#if FS_EDGE_HINTS
    FS_LONG            MAZOutlineContoursOffset;
    FS_LONG            MAZOutlinePointsOffset;
    FS_LONG            MAZHSegsOffset;
    FS_LONG            MAZVSegsOffset;
    FS_LONG            MAZOutlineContourListOffset;
    FS_LONG            MAZOutlineSegPairsOffset;
#endif
} fsg_OffsetInfo;


/*** Element Information ***/
typedef struct {
	SL32				stackBaseOffset;
	fsg_OffsetInfo		offsets[MAX_ELEMENTS];
	fnt_ElementType 	interpreterElements[MAX_ELEMENTS];
} fsg_ElementInfo;

typedef struct {
    UL32  Offset;
    UL32  Length;
} fsg_OffsetLength;

typedef struct {
	Fixed xScale;
	Fixed yScale;
} fsg_transformationMagic;

/*** The Internal Key ***/
typedef struct fsg_SplineKey {
    PBUCKET             clientID;
#ifdef LINT_ARGS
    VOID * (*GetSfntGlyphPtr) (FSP PBUCKET, UW16, UL32, UL32*, PMEM_HANDLE);
    VOID                (*RelSfntGlyphPtr) (FSP MEM_HANDLE);
    UW16              (*mappingF)(FSP struct fsg_SplineKey*, UL32);  /* mapping function */
#else /* !LINT_ARGS */
    VOID *             (*GetSfntGlyphPtr) ();
    VOID                (*RelSfntGlyphPtr) ();
    UW16              (*mappingF)();      /* mapping function */
#endif /* LINT_ARGS */
    UW16          extern_font;            /* possible values of "extern_font" = 0,1,2,3,254: see cgif.h for details (NOT_A_DOWNLOAD, etc) */

	SL32				mappOffset;			/* Offset to platform mapping data */
	SW16				glyphIndex;			/* */

    MEM_HANDLE *        memoryBases;        /* array of memory handles */

	fsg_ElementInfo		elementInfoRec;		/* element info structure */

	UW16			emResolution;			/* used to be SL32 <4> */

	Fixed			fixedPointSize;			/* user point size */
	Fixed			interpScalar;			/* scalar for instructable things */
	Fixed			metricScalar;			/* scalar for metric things */

	transMatrix		currentTMatrix; /* Current Transform Matrix */
	transMatrix		localTMatrix; /* Local Transform Matrix */
	SB8			localTIsIdentity;
	SB8			identityTransformation;
    SW16           indexToLocFormat;       /* from FontHeader */

	UW16			fontFlags;				/* copy of header.flags */

	Fixed			pixelDiameter;
	UW16			nonScaledAW;
	SW16			nonScaledLSB;
	SL32			scanControl;				/* flags for dropout control etc.  */
	
	/* for key->memoryBases[PRIVATE_FONT_SPACE_BASE] */
	SL32			offset_storage;
	SL32			offset_functions;
	SL32			offset_instrDefs;		/* <4> */
	SL32			offset_controlValues;
	SL32			offset_globalGS;

    SL32           glyphLength;            /* for comp chars, sum of pieces */
	
	/* copy of profile */
	sfnt_maxProfileTable	maxProfile;

	SL32	cvtCount;

    fsg_OffsetLength  offsetTableMap[sfnt_NUMTABLEINDEX];
	UW16			numberOf_LongHorMetrics;
	
	UW16			totalContours; /* for components */
	UW16			totalComponents; /* for components */
	UW16			weGotComponents; /* for components */
	UW16			compFlags;
	SW16			arg1, arg2;
    SW16           mirror_char;   /* = 1 if base char is mirrored; else 0 */
    point_coords    devLSB, devRSB;
	
	fsg_transformationMagic tInfo;
	fsg_transformationMagic globalTInfo;
	
	SL32			instructControl;	/* set to inhibit execution of instructions */	
	
	UW16			numberOfRealPointsInComponent;
	UB8			executePrePgm;
	UB8			executeFontPgm;		/* <4> */
	UB8			useMyMetrics;

	SW16        	yDescender;
	SW16        	yAscender;

	UW16          numberOf_LongVerMetrics;
	SW16 topSideBearing;
	UW16 advanceHeight;

	SW16 error;
	SW16 num_comps;
	SW16 num_contours;
	SW16 num_points;
	SW16 num_levels;
	SW16 base_np;
	SW16 base_nc; 
	UW16 fpgm_maxFunctionDefs;		/* max FDEF number derived from the "fpgm" table (which  */
									/* may not match maxp->maxFunctionDefs in corrupt fonts) */
} fsg_SplineKey;


/***************/
/** INTERFACE **/
/***************/

#ifdef LINT_ARGS
EXTERN UL32 fsg_KeySize(VOID);
EXTERN UL32 fsg_PrivateFontSpaceSize(fsg_SplineKey *key);
EXTERN SL32 fsg_GridFit(FSP fsg_SplineKey *key, BOOLEAN useHints);
EXTERN SW16 get_fpgm_maxFDEF(FSP fsg_SplineKey *, UW16 *);

#else /* !LINT_ARGS */

EXTERN UL32 fsg_KeySize();
EXTERN UL32 fsg_PrivateFontSpaceSize();
EXTERN SL32 fsg_GridFit();
EXTERN SW16 get_fpgm_maxFDEF();
#endif /* LINT_ARGS */


/***************/

/* Private Data Types */
typedef struct {
	SW16 xMin;
	SW16 yMin;
	SW16 xMax;
	SW16 yMax;
} sfnt_BBox;

/* matrix routines */

/*
 * ( x1 y1 1 ) = ( x0 y0 1 ) * matrix;
 */

#ifdef LINT_ARGS
EXTERN VOID fsg_FixXYMul( Fixed* x, Fixed* y, transMatrix* matrix );

/*
 *   B = A * B;		<4>
 *
 *         | a  b  0  |
 *    B =  | c  d  0  | * B;
 *         | 0  0  1  |
 */
EXTERN VOID fsg_MxConcat2x2(transMatrix* matrixA, transMatrix* matrixB);

/*
 * scales a matrix by sx and sy.
 *
 *              | sx 0  0  |
 *    matrix =  | 0  sy 0  | * matrix;
 *              | 0  0  1  |
 */
EXTERN VOID fsg_MxScaleAB(Fixed sx, Fixed sy, transMatrix *matrixB);

EXTERN VOID fsg_ReduceMatrix(fsg_SplineKey* key);

/*
 *	Used in FontScaler.c and MacExtra.c, lives in FontScaler.c
 */
UW16 fsg_RunFontProgram( FSP fsg_SplineKey* key );


/* 
** Other externally called functions.  Prototype calls added on 4/5/90
*/
VOID fsg_IncrementElement(fsg_SplineKey *key, SL32 n, SL32 numPoints, SL32 numContours);

VOID fsg_InitInterpreterTrans(FSP fsg_SplineKey *key  );

SL32 fsg_InnerGridFit(FSP fsg_SplineKey *key, SW16 useHints,
	sfnt_BBox *bbox, SL32 sizeOfInstructions, UB8 *instructionPtr, SL32 finalCompositePass);

VOID fsg_SetUpElement(FSP fsg_SplineKey *key, SL32 n); 

UL32 fsg_WorkSpaceSetOffsets(fsg_SplineKey *key); 

UW16 fsg_SetDefaults( FSP fsg_SplineKey* key );

SL32 fsg_RunPreProgram(FSP fsg_SplineKey *key);

EXTERN F26DOT6 ScaleFuncCall(ScaleFunc f, scale_rec*r, F26DOT6 v);
EXTERN ScaleFunc compute_scaling(scale_rec *, FS_FIXED, int);
#else /* !LINT_ARGS */

EXTERN VOID fsg_FixXYMul();
EXTERN VOID fsg_MxConcat2x2();
EXTERN VOID fsg_MxScaleAB();
EXTERN VOID fsg_ReduceMatrix();
UW16 fsg_RunFontProgram();
VOID fsg_IncrementElement();
VOID fsg_InitInterpreterTrans();
SL32 fsg_InnerGridFit();
VOID fsg_SetUpElement();
UL32 fsg_WorkSpaceSetOffsets();
UW16 fsg_SetDefaults();
SL32 fsg_RunPreProgram();
EXTERN F26DOT6 ScaleFuncCall();
EXTERN ScaleFunc compute_scaling();
#endif /* LINT_ARGS */


#endif  /* __FSGLUE__ */

/* -------------------------------------------------------------------
                            END OF "FSglue.h"
   ------------------------------------------------------------------- */

