/* $HopeName: GGEufst5!rts:tt:fontscal.h(EBDSDK_P.1) $ */

/* 
 * Copyright (C) 2003 Agfa Monotype Corporation. All rights reserved.
 */
/* $Header: /hope/man5/hope.0/compound/10/GGEufst5/RCS/rts:tt:fontscal.h,v 1.2.10.1.1.1 2013/12/19 11:24:03 rogerb Exp $ */
/* Log stripped */
/* $Date: 2013/12/19 11:24:03 $ */
/*
   File:    FontScaler.h

 
 

   Copyright: © 1988-1990 by Apple Computer, Inc., all rights reserved.



    Change History (most recent first):

    AGFA changes:
   25-Sep-02 jfd  Added new defines STROKE_FIXED_DESIGN_PCT and
                        STROKE_ADVANCE_WIDTH_ADJUST_SHIFT.
   11-Feb-02  jfd   In fs_GlyphInfoType struct, moved fields "stik_font" and "stik_char"
                          so that they are unconditionally compiled to resolve compiler errors.
   07-Feb-02  jfd   In FS_OUTLINE structure, changed type of "type" field
                    from (SB8 *) to (UB8 *) to resolve compiler warnings.
	Moved "stik_font" and "stik_char" fields from IF_STATE
	to fs_GlyphInfoType structure.
	   23-Jan-02  jfd   Removed TNODE, TLIST and FS_BITMAP structures.
       14-Jan-02  jfd   Added Stik font support.
          31-Aug-01	slg	Added prototype for fs_GetOffsetAndLength() 
         05-Jul-01  jfd Added new argument to fs_NewSfnt() - OpenType CFF font flag.
         26-May-00  keb Added prototypes for fs_GetAdvanceWidth and fs_Get_AdvanceHeight
         14-apR-00  keb Added advanceHeight to metricsType in support of vertical writing
	   24-Jan-00  slg	Remove unused VOID_FUNC_PTR_BASE, top_bearing, and
	   					version fields; put back fs_GetAdvanceWidth(). 
	   11-Dec-98  keb   Added code to support ASIANVERT for XLfonts
       31-Aug-98  keb   modified for xl2.0 font processing support
       11-Dec-94  mby   Added new args to prototype for fs_FindCharBearings()
       16-Jul-93  maib  Added prototype for fs_FindCharBearings()
       15-Jan-93  mby   Added function prototypes for interface routines
                        fs_GetFontBBox() and fs_GetCharBBox().
                        Added bboxInfo  to fs_GlyphInfoType struct.
                        Removed outlineCacheSize from fs_GlyphInfoType.
                        Change fs_GetDesignWidth to int.

       25-Nov-92  mby   changed PASCAL 'int32' to 'int32 PASCAL' in function
                        declarations with !LINT_ARGS.

       26-Oct-92  mby   Add "fs_GlyphInfoType.contourPolarity" to indicate
                        loop polarity reversal (for mirrored components).

       03-Oct-92  rs    Changes toward ANSI C - pascal -> PASCAL.

       20-Jul-92  mby   GlyphInputType changes:  GetSfntGlyphPtr() replaces
                        GetHSfntGlyphPtr() and GetMSfntGlyphPtr().
                        RelSfntGlyphPtr replaces RelHSfntGlyphPtr().

       07-Jul-92  mby   fs_GetDesignWidth() <== fs_GetNonScaledWidth()
                        Change prototype of fs_GetDesignWidth()
                        Delete glyphPtr, glyphLen, charID from InputType structure.

       04-Jun-92  mby   In fs_GlyphInputType: add extern_font; clientID is
                        now a voidPtr; add function pointer GetHSfntGlyphPtr;
                        if TT PCLEO reader enabled, add glyphPtr, glyphLen,
                        charID, and function pointer GetMSfntGlyphPtr.
                        Add func. prototype for fs_GetTablePtr()

       28-Apr-92  mby   Port to SUN.

       09-Apr-92  mby   Don't need fs_SizeOfOutlines(),
                        fs_SaveOutlines(), and fs_RestoreOutlines()

       17-Mar-92  mby   In fs_GlyphInputType structure, memoryBases and
                        transformMatrix declared with MEM_HANDLE.

       20-Feb-92  mby   #ifdef UFST_SUBSYSTEM ==>
                        don't need fs_FindBitmapSize(),
                        fs_ContourScan(),and fs_GetAdvanceWidth()

       31-Jan-92  mby   Added 'emResolution' to fs_GlyphInfoType.

       28-Jan-92  mby   Added 'escapement' to metricsType.

       20-Sep-91  mby   Client interface function declarations must be
                        "int32 pascal ..." instead of "pascal int32 ..."
                        with Microsoft compilers.

       01-Jan-97  dlk   Added 'uint16 languageID' field to help distinguish
                        between GB and BIG5 encoded Asian fonts.

TO DO:  In fs_GlyphInfoType change data pointers to handle-offset pairs.

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

#if STIK && (TT_DISK || TT_ROM || TT_ROM_ACT)	/* 12-20-01 jfd */
#define OUTLINE_CHAR_MASK	0x80
#define STROKE_FIXED_DESIGN_PCT				(((FIXED_ONE) * 3) / 100)	/* 3% */
#define STROKE_ADVANCE_WIDTH_ADJUST_SHIFT	1

/* outline element types */
#define FS_MOVETO 0
#define FS_LINETO 1
#define FS_QUADTO 2
#define FS_CUBETO 3

#define LC_ACCENT_UNI	0x00F3	/* 'o' acute  01-23-04 jfd */
#define UC_ACCENT_UNI	0x00D3	/* 'O' acute  01-23-04 jfd */
#define ACCENT_UNI	0x00B4	/* 01-23-04 jfd */

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
typedef struct {
	/* LONG */ SL32  size;			/* allocation size in bytes - must be first datum */
	/* FS_FIXED */ F26Dot6 lo_x;		/* smallest x coordinate */
	/* FS_FIXED */ F26Dot6 hi_x;		/* largest x coordinate */
	/* FS_FIXED */ F26Dot6 lo_y;		/* smallest y coordinate */
	/* FS_FIXED */ F26Dot6 hi_y;		/* largest y coordinate */
	/* FS_FIXED */ F26Dot6 dx;		/* hi resolution x increment to move character origin */
	/* FS_FIXED */ F26Dot6 dy;		/* hi resolution y increment to move character origin */
	/* SHORT */ SW16 num;			/* number of elements */
	/* BYTE */ UB8 *type;			/* type of the elements */
	/* FS_FIXED */ F26Dot6 *x;		/* x coordinates for the pieces */
	/* FS_FIXED */ F26Dot6 *y;		/* y coordinates of the pieces */
	/* note: OUTLINEs are allocated in one contiguous piece, so you can delete
	* them with <FS_free>. The pointers are set in creation or by <copy_outline>
	*/
	} FS_OUTLINE;
#endif

/*keb 4/00 */
typedef struct {
    vectorType  advanceWidth;         /* scaled, linear width */
    vectorType  leftSideBearing;      /* leftSideBearingLine + upper left vector */
    vectorType  leftSideBearingLine;     /* see FindBitMapSize */
    vectorType  devAdvanceWidth;      /* *** NONLINEAR WIDTH ***/
    UW16      escapement;           /* UFST; nonscaled width in D.U. (linear) */
	SW16      	yDescender;
/* #if GET_VERTICAL_METRICS	08-20-04 */ 
	SW16 topSideBearing;
	UW16 advanceHeight;
/* #endif  // JWD, 04-22-03.  08-20-04 */

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
    UB8       *contourPolarity;   /* 1=reversed from base character,
                         0=same direction as base char. -mby 10/26/92 */
	UB8		*onCurve;
    UW16      emResolution;       /* mby 1/31/92 */
	/* End of spline data */

	/* Only of interest to editors */
	F26Dot6		*scaledCVT;

#if STIK && (TT_DISK || TT_ROM || TT_ROM_ACT)	/* 01-10-02 jfd */
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
/** SL32              *sfntDirectory; **/

	union {
		struct {
            UW16	platformID;
            UW16	specificID;
            UW16  languageID;
            UW16  extern_font;  /* "normal" font=0; PCLEO=1 */
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
			UL32	characterCode;  /* rjl 4/11/2002 - was uint16 */
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

