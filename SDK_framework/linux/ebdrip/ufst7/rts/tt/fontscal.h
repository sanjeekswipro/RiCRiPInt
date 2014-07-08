
/* Copyright (C) 2008 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/* fontscal.h */

/*
   File:    FontScaler.h

   Copyright: 1988-1990 by Apple Computer, Inc., all rights reserved.


- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

		 <3+>	7/11/91		CKL		Added standard header conditionals
		 <3>	11/27/90	MR		Correctly seperate definitions of Rect and Bitmap
		 <2>	 11/5/90	MR		Clean up includes/definitions to ease porting to non-mac
									environments (ex. don't include QuickDraw.h) [rb]
		<10>	 7/18/90	MR		Conditionalize names in FSInput
		 <9>	 7/14/90	MR		rename SQRT2 to FIXEDSQRT2, removed specificID and lowestRecPPEM
									from FSInfo
		 <8>	 7/13/90	MR		FSInput now has a union to save space, points to matrix instead
									of storing it
		 <6>	 6/21/90	MR		Change fillFunc to ReleaseSfntFrag
		 <5>	  6/5/90	MR		remove readmvt and mapcharcodes
		 <4>	  5/3/90	RB		Added memory area for new scan converter. MIKE REED - Removed
									.error from fsinfo structure. Added MapCharCodes and ReadMVT
									calls.
		 <3>	 3/20/90	CL		New comment style for BBS. 
		 <2>	 2/27/90	CL		New CharToIndexMap Table format.
	   <3.5>	11/15/89	CEL		Placed an ifdef around inline MPW calls to the trap. This makes
									it easier to compile for skia and the likes who do not use the
									MPW compiler.
	   <3.4>	11/14/89	CEL		Left Side Bearing should work right for any transformation. The
									phantom points are in, even for components in a composite glyph.
									They should also work for transformations. Device metric are
									passed out in the output data structure. This should also work
									with transformations. Another leftsidebearing along the advance
									width vector is also passed out. whatever the metrics are for
									the component at it's level. Instructions are legal in
									components. Instructions are legal in components. Five
									unnecessary element in the output data structure have been
									deleted. (All the information is passed out in the bitmap data
									structure) fs_FindBMSize now also returns the bounding box.
	   <3.3>	 9/27/89	CEL		Took out devAdvanceWidth & devLeftSideBearing.
	   <3.2>	 9/25/89	CEL		Took out Mac specific functions.
	   <3.1>	 9/15/89	CEL		Re-working dispatcher…
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

#ifndef __FONTSCALER__
#define __FONTSCALER__

typedef struct Rect {
		SW16 top;
		SW16 left;
		SW16 bottom;
		SW16 right;
	} Rect;

/*** Memory shared between all fonts and sizes and transformations ***/
#define KEY_PTR_BASE				0 /* Constant Size ! */
#define WORK_SPACE_BASE				1 /* size is sfnt dependent, can't be shared between grid-fitting and scan-conversion */
/*** Memory that can not be shared between fonts and different sizes, can not dissappear after InitPreProgram() ***/
#define PRIVATE_FONT_SPACE_BASE		2 /* size is sfnt dependent */
#define MAX_MEMORY_AREAS			3 /* this index is not used for memory */

#define NONVALID 0xffff


#if ARABIC_AUTO_HINT
#define ARABIC_UPPER_ACCENT			0x0671 /* upper accented Alef, max y and the fudge will establish a upper bound for upper-accented chars */
#define ARABIC_LOWER_MEDUIM_ACCENT	0x0625 /* lower middle accented Alef, min y and the fudge will establish a base line for lower middle accented chars */
#define ARABIC_UPPER_MEDUIM_ACCENT	0xfe8b /* upper middle accented connected Yeh, max y and the fudge will establish a upper bound for upper-middle-accented chars*/

#define ARABIC_HIGH_NUMERAL			0x0661 /* arabic numeral 1, max and min y used to trap other arabic numerals */										   
#define ARABIC_LOW_NUMERAL			0x0666 /* arabic numeral 6, max and min y used to trap other arabic numerals */
#define ARABIC_UPPER_HIGH			0x0637 /* Tah, max y and the fudge will establish a bound for upper-tall chars */
										   /* min y and fudge will establish a base line bound for all chars except numerals */
#define ARABIC_LOWER_HIGH			0x0639 /* Ein, min y and the fudge will establish a bound for lower-tall chars */
#define ARABIC_LOWER_MEDIUM_HIGH	0x0648 /* Vave, min y establishes the lower bound for lower-medium chars */										      									
#define ARABIC_LOWER_MEDIUM_SHORT	0xfef0 /* connected Yeh, min y establishes the upper bound for lower-medium chars */									       
#define ARABIC_LOWER_SHORT_HIGH		0x0642 /* Ghaf,  min y establishes the lower bound for lower-short chars */
#define ARABIC_LOWER_SHORT_SHORT	0x0633 /* Seen, min y establishes the upper bound for lower-short chars */
										   
#define ARABIC_CENTER_HIGH			0x06d5 /* small round Heh, max y and fudge establishes the upper bound for medial-tall chars */
#define ARABIC_CENTER_SHORT			0xfee3 /* medial Meem, max y and fudge establishes the upper bound for medial-short chars */

#define ARABIC_UPPER_HIGH_DOT		0xfece /* medial Ghein, max y and max y of small round Heh are mathed */
#define ARABIC_UPPER_SHORT_DOT		0x0646 /* Noon, max y and max y of medial Meem are mathed */
#define ARABIC_LOWER_HIGH_DOT		0xfef1 /* Yee, min y and min y of Ein are mathed */
#define ARABIC_LOWER_SHORT_DOT		0x0628 /* Beeh, min y and min y of Ghaf re mathed */

#define ARABIC_UPPER_HIGH_1_DOT		0x063a /* Ghain */
#define ARABIC_UPPER_SHORT_1_DOT    0x0646 /* Noon */
#define ARABIC_UPPER_HIGH_2_DOT		0xfed7 /* Qaf */
#define ARABIC_UPPER_SHORT_2_DOT	0x062a /* Teh */
#define ARABIC_UPPER_HIGH_3_DOT		0xfe9b /* Theh Middle */
#define ARABIC_UPPER_SHORT_3_DOT	0x062b /* Theh */

#define ARABIC_SUBSCRIPT_KASRA		0x0650
#define ARABIC_SUPERSCRIPT_FATHA	0x064e
#define ARABIC_SUPERSCRIPT_SHADDA	0x0651
#define ARABIC_ACCENT_SHADDA_FATHA	0xfc60
#define ARABIC_ACCENT_SHADDA_KASRA	0xfc62
#define ARABIC_SUPERSCRIPT_DAMMA	0x064f
#define ARABIC_SUPERSCRIPT_SUKUN	0x0652
#define ARABIC_SUPERSCRIPT_ALEF	    0x0670
#define ARABIC_SUBSCRIPT_ALEF	    0x0656
#endif /* ARABIC_AUTO_HINT */


#if STIK && (TT_DISK || TT_ROM || TT_ROM_ACT)
#define OUTLINE_CHAR_MASK	0x80

#define STROKE_FIXED_DESIGN_PCT 3277 /* 5% */
#define STROKE_ADVANCE_WIDTH_ADJUST_SHIFT	1

/* outline element types */
#define FS_MOVETO 0
#define FS_LINETO 1
#define FS_QUADTO 2
#define FS_CUBETO 3

#define LC_ACCENT_UNI	0x00F3	/* 'o' acute  01-23-04 jfd */
#define UC_ACCENT_UNI	0x00D3	/* 'O' acute  01-23-04 jfd */
#define ACCENT_UNI	0x00B4	/* 01-23-04 jfd */

typedef struct {
	SL32  size;			/* allocation size in bytes - must be first datum */
	F26Dot6 lo_x;		/* smallest x coordinate */
	F26Dot6 hi_x;		/* largest x coordinate */
	F26Dot6 lo_y;		/* smallest y coordinate */
	F26Dot6 hi_y;		/* largest y coordinate */
#if FS_EDGE_RENDER
    FS_SHORT i_dx;          /* x-advance in pixels - 0 if rotated or skewed */
    FS_SHORT i_dy;          /* y-advance in pixels - 0 if rotated or skewed */
#endif	/* FS_EDGE_RENDER */
	F26Dot6 dx;		/* hi resolution x increment to move character origin */
	F26Dot6 dy;		/* hi resolution y increment to move character origin */
#if FS_EDGE_RENDER
    FS_SHORT nc;            /* number of contours */
	FS_SHORT np;			/* number of curve points (x,y) */
	FS_SHORT polarity;		/* which side in "ink" on? (for emboldening) */
#endif	/* FS_EDGE_RENDER */
	SW16 num;			/* number of elements */
	UB8 *type;			/* type of the elements */
	F26Dot6 *x;		/* x coordinates for the pieces */
	F26Dot6 *y;		/* y coordinates of the pieces */
	/* note: OUTLINEs are allocated in one contiguous piece, so you can delete
	* them with <FS_free>. The pointers are set in creation or by <copy_outline>
	*/
	} FS_OUTLINE;
	
#endif	/* #if STIK && (TT_DISK || TT_ROM || TT_ROM_ACT) */


typedef struct {
    vectorType  advanceWidth;         /* scaled, linear width */
    vectorType  leftSideBearing;      /* leftSideBearingLine + upper left vector */
    vectorType  leftSideBearingLine;     /* see FindBitMapSize */
    vectorType  devAdvanceWidth;      /* *** NONLINEAR WIDTH ***/
    UW16      escapement;           /* UFST; nonscaled width in D.U. (linear) */
	SW16      	yDescender;

	SW16 topSideBearing;
	UW16 advanceHeight;

} metricsType;

typedef struct {                /* UFST needs bounding box information */
    SW16  xMin;
    SW16  yMin;
    SW16  xMax;
    SW16  yMax;
} BBoxType;

/*
 * Output data structure to the Font Scaler.
 */
typedef struct {
	SL32       memorySizes[MAX_MEMORY_AREAS];

	UW16      glyphIndex;

	metricsType metricInfo;
    BBoxType    bboxInfo;

	/* Spline Data */
	UW16		outlinesExist;
	UW16		numberOfContours;
	F26Dot6		*xPtr, *yPtr;
	SW16		*startPtr;
	SW16		*endPtr;
    UB8       *contourPolarity;   
    /* 1=reversed from base character, 0=same direction as base char. -mby 10/26/92 */
	UB8		*onCurve;
    UW16      emResolution;
	/* End of spline data */

	/* Only of interest to editors */
	F26Dot6		*scaledCVT;

#if STIK && (TT_DISK || TT_ROM || TT_ROM_ACT)
	MEM_HANDLE hstik_outl;
	FS_OUTLINE *stik_outl;
	UW16 stik_outl_size;
	MEM_HANDLE hexp_stik_outl;
	FS_OUTLINE *exp_stik_outl;
	UW16 exp_stik_outl_size;
#endif	/* STIK && (TT_DISK || TT_ROM || TT_ROM_ACT) */

	/* for CCC font */
	UW16 stik_font;		/* 0-non-STIK, 1-STIK, 2-STIK CCC */
	BOOLEAN stik_char;
	SW16 cur_stik_char;
	BOOLEAN ccc_font;	/* 0-non-CCC, 1-CCC TTF */

} fs_GlyphInfoType;

/*
 * Input data structure to the Font Scaler.
 *
 * if styleFunc is set to non-zero it will be called just before the transformation
 * will be applied, but after the grid-fitting with a pointer to fs_GlyphInfoType.
 * so this is what styleFunc should be voidFunc StyleFunc( fs_GlyphInfoType *data );
 * For normal operation set this function pointer to zero. */

typedef struct {
    MEM_HANDLE          memoryBases[MAX_MEMORY_AREAS];
#ifdef LINT_ARGS
    VOID * (*GetSfntGlyphPtr) (FSP PBUCKET, UW16, UL32, UL32*, PMEM_HANDLE);
    VOID                (*RelSfntGlyphPtr) (FSP MEM_HANDLE);
#else /* !LINT_ARGS */
    VOID *             (*GetSfntGlyphPtr) ();
    VOID                (*RelSfntGlyphPtr) ();
#endif /* LINT_ARGS */
    PBUCKET             clientID;     /* client private id/stamp (eg. handle for the sfnt ) */

	union {
		struct {
            UW16	platformID;
            UW16	specificID;
            UW16  languageID;
            UW16  extern_font;  
            /* possible values of "extern_font" = 0,1,2,3,254: see cgif.h for details (NOT_A_DOWNLOAD, etc) */
            /* must be set even if PCLEOs aren't enabled */
		} newsfnt;
		struct {
			Fixed			pointSize;
			SW16			xResolution;
			SW16			yResolution;
			Fixed			pixelDiameter;		/* compute engine char from this */
            MEM_HANDLE      transformMatrix;    /* dereference and cast
                                                   to (transMatrix *)  */
		} newtrans;
		struct {
			UL32	characterCode;
			UW16	glyphIndex;
		} newglyph;
	} param;
} fs_GlyphInputType;

#ifndef FIXEDSQRT2
#define FIXEDSQRT2 0x00016A0AL
#endif

#ifdef LINT_ARGS 

EXTERN UW16 fs_OpenFonts( fs_GlyphInputType *inputPtr, fs_GlyphInfoType *outputPtr );
EXTERN UW16 fs_Initialize( FSP fs_GlyphInputType *inputPtr );
EXTERN UW16 fs_NewSfnt( FSP fs_GlyphInputType *inputPtr, fs_GlyphInfoType *outputPtr, UL32 fontStartOff, BOOLEAN ofcff_font );
EXTERN UW16 fs_NewTransformation( FSP fs_GlyphInputType *inputPtr, fs_GlyphInfoType *outputPtr );
EXTERN UW16 fs_NewGlyph( FSP fs_GlyphInputType *inputPtr, fs_GlyphInfoType *outputPtr );
EXTERN UW16 fs_FindCharBearings(FSP fs_GlyphInputType *inputPtr, fs_GlyphInfoType *outputPtr,
                                        SL32 xmin, SL32 ymin, SL32 xmax, SL32 ymax);
EXTERN SL32 fs_GetDesignWidth (FSP fs_GlyphInputType *inputPtr, fs_GlyphInfoType *outputPtr, SL32 n);
EXTERN SL32 fs_GetCharBBox (fs_GlyphInputType *inputPtr, fs_GlyphInfoType *outputPtr);
EXTERN VOID* fs_GetTablePtr (FSP fs_GlyphInputType *inputPtr, SL32 table);
EXTERN VOID fs_GetOffsetAndLength(FSP fs_GlyphInputType*,
								SL32*,
								UL32*,
								SL32);
EXTERN UW16 fs__Contour(FSP fs_GlyphInputType *inputPtr, fs_GlyphInfoType *outputPtr, BOOLEAN useHints); 
/* keb 5/00 */
EXTERN SL32 fs_GetAdvanceWidth( FSP fs_GlyphInputType *inputPtr, fs_GlyphInfoType *outputPtr );
EXTERN UW16 fs_GetAdvanceHeight( FSP fs_GlyphInputType *inputptr, fs_GlyphInfoType *outputptr );

#else /* !LINT_ARGS */

EXTERN UW16 fs_OpenFonts ();
EXTERN UW16 fs_Initialize ();
EXTERN UW16 fs_NewSfnt ();
EXTERN UW16 fs_NewTransformation ();
EXTERN UW16 fs_NewGlyph ();
EXTERN UW16 fs_FindCharBearings();
EXTERN SL32 fs_GetDesignWidth ();
EXTERN SL32 fs_GetCharBBox ();
EXTERN VOID* fs_GetTablePtr ();
EXTERN VOID fs_GetOffsetAndLength ();
EXTERN UW16 fs__Contour();
EXTERN SL32 fs_GetAdvanceWidth();
EXTERN UW16 fs_GetAdvanceHeight();

#endif /* LINT_ARGS */


#endif  /* __FONTSCALER__ */

/* -------------------------------------------------------------------
                            END OF "FontScaler.h"
   ------------------------------------------------------------------- */

